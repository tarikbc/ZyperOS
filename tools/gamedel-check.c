/* Delete Game, on a card made of small files (src/gamedel.h).
 *
 *   make check-gamedel
 *
 * The card holds four kinds of game, each with saves, states, a cover, a
 * video and an achievement set, beside neighbours whose names are nearly
 * theirs:
 *
 *   - a cartridge, Tetris (World).gb, beside Tetris (World) (Rev 1).gb;
 *   - a multi-disc set, a folder holding its .m3u and three .chd discs;
 *   - a .cue at the top of PlayStation/ with its two .bin tracks and its
 *     .sbi, beside Crash Bandicoot 2's .cue and .bin, and two .cue sheets
 *     that name one .bin;
 *   - an arcade set, mslug.zip, beside mslug2.zip and neogeo.zip.
 *
 * For each delete the whole card is listed before and after, and what went
 * must be exactly what the game owned: nothing of a neighbour's, never
 * neogeo.zip, never a file another game shares (a .srm two systems' games
 * write, a .bin a second sheet names). Its rows in the library database go
 * - its details and title, its favorite, its play sessions - and the
 * neighbours' stay. Refused outright, with nothing touched: neogeo.zip
 * itself, a sheet that names a file outside its folder, and a game folder
 * that is a link to somewhere else.
 *
 * The other games come from lib_scan_listing over the same folders, as the
 * launcher's shelves have them. Links src/gamedel.c, src/library.c,
 * src/db.c, src/favorites.c and src/stats.c, and not SDL.
 */
#include "../src/db.h"
#include "../src/favorites.h"
#include "../src/gamedel.h"
#include "../src/library.h"
#include "../src/stats.h"

#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-gamedel-check-XXXXXX";
static char roms[256], saves[256], states[256];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- the card -------------------------------------------------------------- */

static void path_of(char *out, size_t n, const char *rel)
{
	snprintf(out, n, "%s/%s", root, rel);
}

/* A file at `rel` under the card, its folders made; `text` its content */
static void put(const char *rel, const char *text)
{
	char p[1200], *s;
	FILE *f;

	path_of(p, sizeof p, rel);
	for (s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) {
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	f = fopen(p, "wb");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text ? text : "x", f);
	fclose(f);
}

static bool there(const char *rel)
{
	char p[1200];
	struct stat st;

	path_of(p, sizeof p, rel);
	return lstat(p, &st) == 0;
}

/* ---- the card listed, before and after ----------------------------------- */

typedef struct { char **p; int n, cap; } listing;
static listing *g_into;

static int list_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	const char *rel = path + strlen(root);
	char buf[1200];

	(void)st; (void)ftw;
	if (!*rel) return 0;
	rel++;
	/* The databases change with every row; what they say is checked by row */
	if (!strncmp(rel, "userdata", 8) || strstr(rel, "library.db")) return 0;
	snprintf(buf, sizeof buf, "%s%s", rel, flag == FTW_D || flag == FTW_DP ? "/" : "");
	if (g_into->n == g_into->cap) {
		g_into->cap = g_into->cap ? g_into->cap * 2 : 256;
		g_into->p = realloc(g_into->p, (size_t)g_into->cap * sizeof *g_into->p);
	}
	g_into->p[g_into->n++] = strdup(buf);
	return 0;
}

static void list_card(listing *l)
{
	memset(l, 0, sizeof *l);
	g_into = l;
	nftw(root, list_one, 16, FTW_PHYS);
}

static bool listed(const listing *l, const char *p)
{
	for (int i = 0; i < l->n; i++)
		if (!strcmp(l->p[i], p)) return true;
	return false;
}

static void list_free(listing *l)
{
	for (int i = 0; i < l->n; i++) free(l->p[i]);
	free(l->p);
	memset(l, 0, sizeof *l);
}

/* What went between `before` and `after` must be `gone`, NULL-ended, exactly */
static void went_exactly(const char *what, const listing *before, const listing *after,
                         const char *const *gone)
{
	int i, n = 0;

	for (i = 0; gone[i]; i++, n++) {
		if (!listed(before, gone[i])) {
			printf("  FAIL %s: %s was not on the card to begin with\n", what, gone[i]);
			failures++;
		} else if (listed(after, gone[i])) {
			printf("  FAIL %s: %s is still there\n", what, gone[i]);
			failures++;
		}
	}
	for (i = 0; i < before->n; i++) {
		bool expected = false;

		if (listed(after, before->p[i])) continue;
		for (int k = 0; gone[k]; k++)
			if (!strcmp(gone[k], before->p[i])) expected = true;
		if (!expected) {
			printf("  FAIL %s: %s went, and is not the game's\n", what, before->p[i]);
			failures++;
		}
	}
	for (i = 0; i < after->n; i++)
		if (!listed(before, after->p[i])) {
			printf("  FAIL %s: %s appeared\n", what, after->p[i]);
			failures++;
		}
	(void)n;
}

/* ---- the shelves ----------------------------------------------------------- */

typedef struct { char folder[64], tag[16], core[32], exts[128]; } shelf;
static shelf g_shelf[8];
static int g_nshelves;
static game_list g_list[8];

/* A system's row from config/systems.cfg, by its tag: the folder, the core
 * and the extensions the shelf really has */
static const shelf *system_row(const char *tag)
{
	FILE *f = fopen("config/systems.cfg", "r");
	char line[1024];
	shelf *s = &g_shelf[g_nshelves];

	CHECK(f != NULL);
	while (f && fgets(line, sizeof line, f)) {
		char *field[10], *save = NULL, *p;
		int n = 0;

		if (strncmp(line, "sys|", 4)) continue;
		line[strcspn(line, "\r\n")] = '\0';
		for (p = strtok_r(line, "|", &save); p && n < 10; p = strtok_r(NULL, "|", &save))
			field[n++] = p;
		if (n < 8 || strcmp(field[4], tag)) continue;
		snprintf(s->folder, sizeof s->folder, "%s", field[2]);
		snprintf(s->tag, sizeof s->tag, "%s", field[4]);
		/* The default core, the first of a list */
		snprintf(s->core, sizeof s->core, "%.*s", (int)strcspn(field[3], ","), field[3]);
		snprintf(s->exts, sizeof s->exts, "%s", field[7]);
		fclose(f);
		return &g_shelf[g_nshelves++];
	}
	if (f) fclose(f);
	printf("  FAIL no %s row in config/systems.cfg\n", tag);
	failures++;
	exit(1);
}

/* Every shelf read again, as a rescan would, and every game on them but
 * `skip` (in `folder`) handed over as the others */
static int others_of(const char *folder, const char *skip, gd_other *out, int max)
{
	int n = 0;

	for (int s = 0; s < g_nshelves; s++) {
		lib_scan_listing(roms, g_shelf[s].folder, g_shelf[s].exts, &g_list[s]);
		for (int i = 0; i < g_list[s].count && n < max; i++) {
			const game_entry *e = &g_list[s].items[i];

			if (!strcmp(g_shelf[s].folder, folder) && !strcmp(e->file, skip)) continue;
			out[n++] = (gd_other){ g_shelf[s].folder, e->file, e->name,
			                       lib_shelf_hooked(g_shelf[s].exts), e->dir };
		}
	}
	return n;
}

static bool on_shelf(const shelf *s, const char *file)
{
	game_list l = { 0 };
	bool found = false;

	lib_scan_listing(roms, s->folder, s->exts, &l);
	for (int i = 0; i < l.count; i++)
		if (!strcmp(l.items[i].file, file)) found = true;
	lib_free(&l);
	return found;
}

/* ---- the database ---------------------------------------------------------- */

static void rows_for(const shelf *s, const char *file, const char *title)
{
	game_meta m;
	char key[700];

	memset(&m, 0, sizeof m);
	snprintf(m.title, sizeof m.title, "%s", title);
	snprintf(m.year, sizeof m.year, "1995");
	snprintf(m.synopsis, sizeof m.synopsis, "A game about %s.", title);
	CHECK(db_game_set(db_lib(), s->folder, file, &m));
	if (!fav_is(s->tag, file)) CHECK(fav_toggle(s->tag, file));
	CHECK(fav_save());
	snprintf(key, sizeof key, "sess.1730000000.100.%s\t%s", s->tag, file);
	CHECK(db_set_str(db_lib(), key, "600\tquit"));
	snprintf(key, sizeof key, "sess.1730086400.200.%s\t%s", s->tag, file);
	CHECK(db_set_str(db_lib(), key, "300\tlost"));
	snprintf(key, sizeof key, "map.%s.%s", s->tag, file);
	CHECK(db_set_str(db_lib(), key, "a:b"));
	snprintf(key, sizeof key, "stick.%s.%s", s->tag, file);
	CHECK(db_set_str(db_lib(), key, "1"));
}

struct sess_count { const char *tag, *file; int n; };

static bool count_sess(const char *key, const char *value, void *ctx)
{
	struct sess_count *c = ctx;
	const char *tab = strchr(key, '\t');
	size_t tl = strlen(c->tag);

	(void)value;
	if (tab && !strcmp(tab + 1, c->file) && (size_t)(tab - key) > tl &&
	    !strncmp(tab - tl, c->tag, tl) && tab[-(long)tl - 1] == '.')
		c->n++;
	return true;
}

static int sessions(const shelf *s, const char *file)
{
	struct sess_count c = { s->tag, file, 0 };

	db_each_prefix(db_lib(), "sess.", count_sess, &c);
	return c.n;
}

static bool fav_row(const shelf *s, const char *file)
{
	char key[700];

	snprintf(key, sizeof key, "fav.%s\t%s", s->tag, file);
	return db_has(db_lib(), key);
}

static bool details(const shelf *s, const char *file)
{
	game_meta m;

	return db_game_get(db_lib(), s->folder, file, &m);
}

static void rows_there(const char *what, const shelf *s, const char *file, bool want)
{
	bool d = details(s, file), f = fav_row(s, file), fm;
	char key[700];
	bool mk, sk;
	int n = sessions(s, file);

	fav_load();
	fm = fav_is(s->tag, file);
	snprintf(key, sizeof key, "map.%s.%s", s->tag, file);
	mk = db_has(db_lib(), key);
	snprintf(key, sizeof key, "stick.%s.%s", s->tag, file);
	sk = db_has(db_lib(), key);
	if (want) {
		if (!mk || !sk) {
			printf("  FAIL %s: %s's map/stick went (%d/%d)\n", what, file, mk, sk);
			failures++;
		}
		if (!d || !f || !fm || n != 2) {
			printf("  FAIL %s: %s's rows went (details %d, favorite %d/%d, sessions %d)\n",
			       what, file, d, f, fm, n);
			failures++;
		}
	} else if (mk || sk) {
		printf("  FAIL delete_drops_map_and_stick: %s's map/stick stayed (%d/%d)\n", file, mk, sk);
		failures++;
	} else if (d || f || fm || n) {
		printf("  FAIL %s: %s's rows stayed (details %d, favorite %d/%d, sessions %d)\n",
		       what, file, d, f, fm, n);
		failures++;
	}
}

/* ---- one delete ------------------------------------------------------------ */

static gd_plan g_plan;
static gd_other g_others[256];

/* The game deleted as the details page deletes it: the plan, the run, the
 * rows. True when it was not refused. */
static bool delete_game(const shelf *s, const char *file, const char *name, gd_why *why,
                        gd_done *done)
{
	gd_card card = { roms, saves, states };
	gd_game g = { s->folder, s->tag, s->core, s->exts, file, name, 0 };
	int n = others_of(s->folder, file, g_others, 256);

	/* The folder of games it is kept in, as the shelf's scan has it */
	for (int k = 0; k < g_nshelves; k++)
		if (!strcmp(g_shelf[k].folder, s->folder))
			for (int i = 0; i < g_list[k].count; i++)
				if (!strcmp(g_list[k].items[i].file, file)) g.dir = g_list[k].items[i].dir;

	memset(done, 0, sizeof *done);
	if (!gd_plan_make(&card, &g, g_others, n, &g_plan)) {
		*why = g_plan.why;
		return false;
	}
	*why = GD_OK;
	gd_plan_run(&g_plan, done);
	if (done->game_gone) gd_forget(s->folder, s->tag, file);
	return true;
}

/* A refused delete: the reason, and the card and its rows as they were */
static void refused(const char *what, const shelf *s, const char *file, const char *name,
                    gd_why want)
{
	listing before, after;
	gd_why why;
	gd_done done;
	static const char *const none[] = { NULL };

	printf("%s\n", what);
	list_card(&before);
	CHECK(!delete_game(s, file, name, &why, &done));
	CHECK(why == want);
	CHECK(done.files == 0 && done.folders == 0);
	list_card(&after);
	went_exactly(what, &before, &after, none);
	list_free(&before);
	list_free(&after);
}

static void deleted(const char *what, const shelf *s, const char *file, const char *name,
                    const char *const *gone, int files)
{
	listing before, after;
	gd_why why;
	gd_done done;

	printf("%s\n", what);
	list_card(&before);
	CHECK(delete_game(s, file, name, &why, &done));
	CHECK(why == GD_OK);
	CHECK(done.game_gone);
	CHECK(done.failed == 0);
	if (done.files != files) {
		printf("  FAIL %s: %d files deleted, %d expected\n", what, done.files, files);
		failures++;
	}
	CHECK(done.bytes > 0);
	list_card(&after);
	went_exactly(what, &before, &after, gone);
	CHECK(!on_shelf(s, file));
	rows_there(what, s, file, false);
	list_free(&before);
	list_free(&after);
}

/* ---- the Ports shelf ------------------------------------------------------- */

/* As PortMaster lays a card out (2026-11-08, Tarik's Pixel 2): a port whose
 * port.json names its script and its folder, one whose script's GAMEDIR
 * names its folder, PortMaster's own script and folder, a script nothing
 * names a folder for, two ports that share a folder, and a port folder whose
 * script is gone */
static void make_ports(void)
{
	put("Roms/Ports/StardewValley.sh", "#!/bin/bash\nGAMEDIR=\"/$directory/ports/stardewvalley\"\n");
	put("Roms/Ports/stardewvalley/port.json",
	    "{\"version\": 2, \"name\": \"stardewvalley.zip\",\n"
	    " \"items\": [\"stardewvalley/\", \"StardewValley.sh\"],\n"
	    " \"attr\": {\"title\": \"Stardew Valley\", \"porter\": [\"x\"]}}\n");
	put("Roms/Ports/stardewvalley/gamedata/Stardew Valley.exe", "exe");
	put("Roms/Ports/stardewvalley/conf/save.dat", "save");
	put("Roms/Ports/.media/StardewValley.png", "png");
	put("Roms/Ports/Celeste.sh", "#!/bin/bash\nexport GAMEDIR=/$directory/ports/celeste/\ncd $GAMEDIR\n");
	put("Roms/Ports/celeste/Celeste.exe", "exe");
	put("Roms/Ports/PortMaster.sh", "");
	put("Roms/Ports/PortMaster/libs/mono.squashfs", "mono");
	put("Roms/Ports/PortMaster/control.txt", "control");
	put("Roms/Ports/Mystery.sh", "#!/bin/bash\necho hello\n");
	put("Roms/Ports/Twin One.sh", "#!/bin/bash\n");
	put("Roms/Ports/Twin Two.sh", "#!/bin/bash\n");
	put("Roms/Ports/twinone/port.json", "{\"items\": [\"twinone/\", \"common/\", \"Twin One.sh\"]}");
	put("Roms/Ports/twintwo/port.json", "{\"items\": [\"twintwo/\", \"common/\", \"Twin Two.sh\"]}");
	put("Roms/Ports/twinone/a.dat", "a");
	put("Roms/Ports/twintwo/b.dat", "b");
	put("Roms/Ports/common/engine.bin", "engine");
	put("Roms/Ports/gtavc/port.json",
	    "{\"items\": [\"gtavc/\", \"Grand Theft Auto Vice City.sh\"],"
	    " \"attr\": {\"title\": \"Grand Theft Auto: Vice City\"}}");
	put("Roms/Ports/gtavc/data.bin", "data");
	/* A port.json that names PortMaster's folder with a trailing dot, which
	 * the card's exFAT can resolve to PortMaster itself; and a folder the
	 * shelf lists as a game of its own (a disc image at its top) that a
	 * port's port.json names too */
	put("Roms/Ports/Sly.sh", "#!/bin/bash\n");
	put("Roms/Ports/sly/port.json", "{\"items\": [\"sly/\", \"PortMaster./\", \"PortMaster.\", \"disc/\", \"Sly.sh\"]}");
	put("Roms/Ports/sly/sly.dat", "sly");
	put("Roms/Ports/disc/game.iso", "iso");
}

/* ---- the card's games ------------------------------------------------------ */

static const shelf *GB, *GBC, *PS1, *ARC, *PRT;

static void make_card(void)
{
	char m3u[512];

	/* Game Boy: the cartridge, and the revision beside it */
	put("Roms/Game Boy/Tetris (World).gb", "tetris");
	put("Roms/Game Boy/.media/Tetris (World).png", "png");
	put("Roms/Game Boy/.media/Tetris (World).mp4", "mp4");
	/* the cover Cover and Video put aside, and the revision's */
	put("Roms/Game Boy/.media/.before/Tetris (World).png", "png");
	put("Roms/Game Boy/.media/.before/Tetris (World) (Rev 1).png", "png");
	put("Roms/Game Boy/.cheevos/Tetris (World).set", "set");
	put("Saves/Tetris (World).srm", "srm");
	put("Saves/Tetris (World).sav", "sav");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.auto.state", "state");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.auto.bmp", "bmp");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.1.state", "state");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.1.bmp", "bmp");

	put("Roms/Game Boy/Tetris (World) (Rev 1).gb", "tetris 1.1");
	put("Roms/Game Boy/.media/Tetris (World) (Rev 1).png", "png");
	put("Roms/Game Boy/.cheevos/Tetris (World) (Rev 1).set", "set");
	put("Saves/Tetris (World) (Rev 1).srm", "srm");
	put("shared/.zyperos/Game Boy/Tetris (World) (Rev 1).gb.auto.state", "state");

	/* Two systems' games of one stem, which write one .srm */
	put("Roms/Game Boy/Pokemon (USA).gb", "red");
	put("Roms/Game Boy/.media/Pokemon (USA).png", "png");
	put("Roms/Game Boy Color/Pokemon (USA).gbc", "gold");
	put("Saves/Pokemon (USA).srm", "srm");

	/* PlayStation: the multi-disc set in its folder */
	snprintf(m3u, sizeof m3u, "Final Fantasy VII (USA) (Disc 1).chd\r\n"
	                          "Final Fantasy VII (USA) (Disc 2).chd\r\n"
	                          "# a comment\r\n"
	                          "Final Fantasy VII (USA) (Disc 3).chd\r\n");
	put("Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u", m3u);
	put("Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1).chd", "d1");
	put("Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 2).chd", "d2");
	put("Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 3).chd", "d3");
	put("Roms/PlayStation/.media/Final Fantasy VII (USA).png", "png");
	put("Roms/PlayStation/.media/Final Fantasy VII (USA).mp4", "mp4");
	put("Roms/PlayStation/.cheevos/Final Fantasy VII (USA).set", "set");
	put("Saves/Final Fantasy VII (USA).srm", "srm");
	put("shared/.zyperos/PlayStation/Final Fantasy VII (USA).m3u.auto.state", "state");
	put("shared/.zyperos/PlayStation/Final Fantasy VII (USA).m3u.auto.bmp", "bmp");

	/* The .cue and its tracks at the top, beside the sequel's */
	put("Roms/PlayStation/Crash Bandicoot (USA).cue",
	    "\xEF\xBB\xBF" "FILE \"Crash Bandicoot (USA) (Track 1).bin\" BINARY\r\n"
	    "  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
	    "FILE \"Crash Bandicoot (USA) (Track 2).bin\" BINARY\r\n"
	    "  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n");
	put("Roms/PlayStation/Crash Bandicoot (USA) (Track 1).bin", "track 1");
	put("Roms/PlayStation/Crash Bandicoot (USA) (Track 2).bin", "track 2");
	put("Roms/PlayStation/Crash Bandicoot (USA).sbi", "sbi");
	put("Roms/PlayStation/.media/Crash Bandicoot (USA).png", "png");
	put("Saves/Crash Bandicoot (USA).srm", "srm");
	put("shared/.zyperos/PlayStation/Crash Bandicoot (USA).cue.auto.state", "state");

	put("Roms/PlayStation/Crash Bandicoot 2 (USA).cue",
	    "FILE \"Crash Bandicoot 2 (USA).bin\" BINARY\n  TRACK 01 MODE2/2352\n");
	put("Roms/PlayStation/Crash Bandicoot 2 (USA).bin", "crash 2");
	put("Roms/PlayStation/.media/Crash Bandicoot 2 (USA).png", "png");
	put("Saves/Crash Bandicoot 2 (USA).srm", "srm");
	put("shared/.zyperos/PlayStation/Crash Bandicoot 2 (USA).cue.auto.state", "state");

	/* Two sheets that name one .bin */
	put("Roms/PlayStation/Spyro the Dragon (USA).cue",
	    "FILE \"Spyro the Dragon (USA).bin\" BINARY\n");
	put("Roms/PlayStation/Spyro the Dragon (USA) [fixed].cue",
	    "FILE Spyro the Dragon (USA).bin BINARY\n");
	put("Roms/PlayStation/Spyro the Dragon (USA).bin", "spyro");

	/* A sheet that names a file outside its folder, and the file */
	put("Roms/PlayStation/Escape (USA).cue", "FILE \"../../Secret/escape.bin\" BINARY\n");
	put("Secret/escape.bin", "not a track of anything on the card");

	/* A game folder that is a link to a folder outside the card's Roms */
	put("Secret/Linked (USA)/Linked (USA).m3u", "Linked (USA).chd\n");
	put("Secret/Linked (USA)/Linked (USA).chd", "linked");
	{
		char from[1200], to[1200];

		path_of(to, sizeof to, "Secret/Linked (USA)");
		path_of(from, sizeof from, "Roms/PlayStation/Linked (USA)");
		CHECK(symlink(to, from) == 0);
	}

	/* Arcade: the set, its sequel and the Neo Geo BIOS beside them */
	put("Roms/Arcade/mslug.zip", "PK mslug");
	put("Roms/Arcade/mslug2.zip", "PK mslug2");
	put("Roms/Arcade/neogeo.zip", "PK neogeo");
	put("Roms/Arcade/.media/mslug.png", "png");
	put("Roms/Arcade/.media/mslug.mp4", "mp4");
	put("Roms/Arcade/.media/mslug2.png", "png");
	put("Saves/fbneo/mslug.nv", "nv");
	put("Saves/fbneo/mslug.hi", "hi");
	put("Saves/fbneo/mslug.fs", "fs");
	put("Saves/fbneo/mslug2.nv", "nv");
	put("shared/.zyperos/Arcade/mslug.zip.auto.state", "state");
	put("shared/.zyperos/Arcade/mslug.zip.2.state", "state");
	put("shared/.zyperos/Arcade/mslug.zip.2.bmp", "bmp");
	put("shared/.zyperos/Arcade/mslug2.zip.auto.state", "state");
}

static void the_rules(void)
{
	printf("the rules:\n");
	CHECK(gd_is_bios("neogeo.zip"));
	CHECK(gd_is_bios("NEOGEO.ZIP"));
	CHECK(gd_is_bios("pgm.7z"));
	CHECK(gd_is_bios("awbios.zip"));
	CHECK(!gd_is_bios("mslug.zip"));
	CHECK(!gd_is_bios("neogeo.cue"));
	CHECK(!gd_is_bios("neogeo/neogeo.zip"));
	CHECK(!gd_is_bios(NULL));
	CHECK(gd_shelf_deletes("mgba"));
	CHECK(gd_shelf_deletes("run:Emulators/Flycast/Flycast.sh"));
	/* A port too: its port.json or its GAMEDIR says which folders are its */
	CHECK(gd_shelf_deletes("port"));
	/* The next game, or the one before at the end, or none */
	CHECK(gd_cursor_after(5, 2) == 2);
	CHECK(gd_cursor_after(5, 0) == 0);
	CHECK(gd_cursor_after(5, 4) == 3);
	CHECK(gd_cursor_after(2, 1) == 0);
	CHECK(gd_cursor_after(1, 0) == -1);
	CHECK(gd_cursor_after(0, 0) == -1);
}

int main(void)
{
	char dev[300], lib[300];

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(roms, sizeof roms, "%s/Roms", root);
	snprintf(saves, sizeof saves, "%s/Saves", root);
	snprintf(states, sizeof states, "%s/shared/.zyperos", root);
	put("userdata/.keep", "");
	put("shared/.zyperos/.keep", "");
	snprintf(dev, sizeof dev, "%s/userdata/zyperos.db", root);
	snprintf(lib, sizeof lib, "%s/library.db", states);
	CHECK(db_init(dev, lib, NULL));
	fav_load();

	GB = system_row("GB");
	GBC = system_row("GBC");
	PS1 = system_row("PS1");
	ARC = system_row("ARCADE");
	PRT = system_row("PRT");
	make_ports();
	make_card();

	the_rules();

	rows_for(GB, "Tetris (World).gb", "Tetris");
	rows_for(GB, "Tetris (World) (Rev 1).gb", "Tetris");
	rows_for(PS1, "Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u", "Final Fantasy VII");
	rows_for(PS1, "Crash Bandicoot (USA).cue", "Crash Bandicoot");
	rows_for(PS1, "Crash Bandicoot 2 (USA).cue", "Crash Bandicoot 2");
	rows_for(ARC, "mslug.zip", "Metal Slug");
	rows_for(ARC, "mslug2.zip", "Metal Slug 2");
	rows_for(ARC, "neogeo.zip", "Neo Geo");

	refused("neogeo.zip, the BIOS every Neo Geo set needs, is refused:",
	        ARC, "neogeo.zip", "neogeo", GD_BIOS);
	rows_there("neogeo.zip refused", ARC, "neogeo.zip", true);
	refused("a sheet naming ../../Secret/escape.bin is refused whole:",
	        PS1, "Escape (USA).cue", "Escape (USA)", GD_OUTSIDE);
	refused("a game folder that links outside Roms is refused:",
	        PS1, "Linked (USA)/Linked (USA).m3u", "Linked (USA)", GD_OUTSIDE);
	refused("a key that climbs out is refused:",
	        PS1, "../Game Boy/Tetris (World).gb", "Tetris (World)", GD_OUTSIDE);

	/* 1.0.5: games kept in folders of games (src/shelfdir.h). A game goes
	 * alone, never the folder it is kept in; a disc set in one goes with its
	 * own folder; a sheet's tracks beside it in the folder go with it. */
	put("Roms/Game Boy/Rom Hacks/Tetris DX Hack.gb", "hack");
	put("Roms/Game Boy/Rom Hacks/Other Hack.gb", "other");
	put("Roms/Game Boy/.media/Tetris DX Hack.png", "png");
	put("Saves/Tetris DX Hack.srm", "srm");
	put("shared/.zyperos/Game Boy/Tetris DX Hack.gb.auto.state", "state");
	put("Roms/PlayStation/RPGs/Xenogears (USA)/Xenogears (USA) (Disc 1).cue",
	    "FILE \"Xenogears (USA) (Disc 1).bin\" BINARY\n");
	put("Roms/PlayStation/RPGs/Xenogears (USA)/Xenogears (USA) (Disc 1).bin", "x");
	put("Roms/PlayStation/RPGs/Vagrant Story (USA).chd", "vs");
	put("Roms/PlayStation/RPGs/Legend (USA).cue", "FILE \"Legend (USA) (Track 1).bin\" BINARY\n");
	put("Roms/PlayStation/RPGs/Legend (USA) (Track 1).bin", "l");
	put("Saves/Xenogears (USA) (Disc 1).srm", "srm");
	{
		static const char *const gone[] = {
			"Roms/Game Boy/Rom Hacks/Tetris DX Hack.gb",
			"Roms/Game Boy/.media/Tetris DX Hack.png",
			"Saves/Tetris DX Hack.srm",
			"shared/.zyperos/Game Boy/Tetris DX Hack.gb.auto.state",
			NULL
		};
		deleted("a game in a folder of games: the game, never the folder or its neighbour:",
		        GB, "Rom Hacks/Tetris DX Hack.gb", "Tetris DX Hack", gone, 4);
		CHECK(there("Roms/Game Boy/Rom Hacks/Other Hack.gb"));
	}
	{
		static const char *const gone[] = {
			"Roms/PlayStation/RPGs/Xenogears (USA)/",
			"Roms/PlayStation/RPGs/Xenogears (USA)/Xenogears (USA) (Disc 1).cue",
			"Roms/PlayStation/RPGs/Xenogears (USA)/Xenogears (USA) (Disc 1).bin",
			"Saves/Xenogears (USA) (Disc 1).srm",
			NULL
		};
		deleted("a disc set in a folder of games: its own folder, and not the one it is in:",
		        PS1, "RPGs/Xenogears (USA)/Xenogears (USA) (Disc 1).cue", "Xenogears (USA)", gone, 3);
		CHECK(there("Roms/PlayStation/RPGs/Vagrant Story (USA).chd"));
	}
	{
		static const char *const gone[] = {
			"Roms/PlayStation/RPGs/Legend (USA).cue",
			"Roms/PlayStation/RPGs/Legend (USA) (Track 1).bin",
			NULL
		};
		deleted("a sheet loose in a folder of games: its track beside it goes too:",
		        PS1, "RPGs/Legend (USA).cue", "Legend (USA)", gone, 2);
		CHECK(there("Roms/PlayStation/RPGs/Vagrant Story (USA).chd"));
	}

	{
		static const char *const gone[] = {
			"Roms/Game Boy/Tetris (World).gb",
			"Roms/Game Boy/.media/Tetris (World).png",
			"Roms/Game Boy/.media/Tetris (World).mp4",
			"Roms/Game Boy/.media/.before/Tetris (World).png",
			"Roms/Game Boy/.cheevos/Tetris (World).set",
			"Saves/Tetris (World).srm",
			"Saves/Tetris (World).sav",
			"shared/.zyperos/Game Boy/Tetris (World).gb.auto.state",
			"shared/.zyperos/Game Boy/Tetris (World).gb.auto.bmp",
			"shared/.zyperos/Game Boy/Tetris (World).gb.1.state",
			"shared/.zyperos/Game Boy/Tetris (World).gb.1.bmp",
			NULL
		};
		deleted("a cartridge: Tetris (World).gb, and not its Rev 1 beside it:",
		        GB, "Tetris (World).gb", "Tetris (World)", gone, 11);
		rows_there("the cartridge's neighbour", GB, "Tetris (World) (Rev 1).gb", true);
	}
	{
		/* The .srm is the Game Boy Color game's too, and stays */
		static const char *const gone[] = {
			"Roms/Game Boy/Pokemon (USA).gb",
			"Roms/Game Boy/.media/Pokemon (USA).png",
			NULL
		};
		deleted("a cartridge whose .srm a Game Boy Color game of its name writes too:",
		        GB, "Pokemon (USA).gb", "Pokemon (USA)", gone, 2);
		CHECK(there("Saves/Pokemon (USA).srm"));
	}
	{
		static const char *const gone[] = {
			"Roms/PlayStation/Final Fantasy VII (USA)/",
			"Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u",
			"Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1).chd",
			"Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 2).chd",
			"Roms/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 3).chd",
			"Roms/PlayStation/.media/Final Fantasy VII (USA).png",
			"Roms/PlayStation/.media/Final Fantasy VII (USA).mp4",
			"Roms/PlayStation/.cheevos/Final Fantasy VII (USA).set",
			"Roms/PlayStation/.cheevos/",           /* nothing else was in it */
			"Saves/Final Fantasy VII (USA).srm",
			"shared/.zyperos/PlayStation/Final Fantasy VII (USA).m3u.auto.state",
			"shared/.zyperos/PlayStation/Final Fantasy VII (USA).m3u.auto.bmp",
			NULL
		};
		deleted("a multi-disc set: its folder, .m3u and three discs:",
		        PS1, "Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u",
		        "Final Fantasy VII (USA)", gone, 10);
	}
	{
		static const char *const gone[] = {
			"Roms/PlayStation/Crash Bandicoot (USA).cue",
			"Roms/PlayStation/Crash Bandicoot (USA) (Track 1).bin",
			"Roms/PlayStation/Crash Bandicoot (USA) (Track 2).bin",
			"Roms/PlayStation/Crash Bandicoot (USA).sbi",
			"Roms/PlayStation/.media/Crash Bandicoot (USA).png",
			"Saves/Crash Bandicoot (USA).srm",
			"shared/.zyperos/PlayStation/Crash Bandicoot (USA).cue.auto.state",
			NULL
		};
		deleted("a .cue at the top: its tracks and .sbi, and not the sequel's:",
		        PS1, "Crash Bandicoot (USA).cue", "Crash Bandicoot (USA)", gone, 7);
		rows_there("the sequel", PS1, "Crash Bandicoot 2 (USA).cue", true);
	}
	{
		/* The .bin stays with the other sheet that names it */
		static const char *const gone[] = { "Roms/PlayStation/Spyro the Dragon (USA).cue", NULL };

		deleted("a .cue whose .bin another .cue names too:",
		        PS1, "Spyro the Dragon (USA).cue", "Spyro the Dragon (USA)", gone, 1);
		CHECK(there("Roms/PlayStation/Spyro the Dragon (USA).bin"));
		CHECK(on_shelf(PS1, "Spyro the Dragon (USA) [fixed].cue"));
	}
	{
		static const char *const gone[] = {
			"Roms/Arcade/mslug.zip",
			"Roms/Arcade/.media/mslug.png",
			"Roms/Arcade/.media/mslug.mp4",
			"Saves/fbneo/mslug.nv",
			"Saves/fbneo/mslug.hi",
			"Saves/fbneo/mslug.fs",
			"shared/.zyperos/Arcade/mslug.zip.auto.state",
			"shared/.zyperos/Arcade/mslug.zip.2.state",
			"shared/.zyperos/Arcade/mslug.zip.2.bmp",
			NULL
		};
		deleted("an arcade set beside neogeo.zip: the set, never the BIOS:",
		        ARC, "mslug.zip", "mslug", gone, 9);
		CHECK(there("Roms/Arcade/neogeo.zip"));
		CHECK(there("Roms/Arcade/mslug2.zip"));
		rows_there("the sequel set", ARC, "mslug2.zip", true);
		rows_there("the BIOS set", ARC, "neogeo.zip", true);
	}
	{
		/* The last game on the Game Boy shelf but one; its cover was the
		 * last in .media, which goes with it */
		static const char *const gone[] = {
			"Roms/Game Boy/Tetris (World) (Rev 1).gb",
			"Roms/Game Boy/.media/Tetris (World) (Rev 1).png",
			"Roms/Game Boy/.media/.before/Tetris (World) (Rev 1).png",
			"Roms/Game Boy/.media/.before/",
			"Roms/Game Boy/.media/",
			"Roms/Game Boy/.cheevos/Tetris (World) (Rev 1).set",
			"Roms/Game Boy/.cheevos/",
			"Saves/Tetris (World) (Rev 1).srm",
			"shared/.zyperos/Game Boy/Tetris (World) (Rev 1).gb.auto.state",
			"shared/.zyperos/Game Boy/",
			NULL
		};
		deleted("the shelf's last game, with the folders it leaves empty:",
		        GB, "Tetris (World) (Rev 1).gb", "Tetris (World) (Rev 1)", gone, 6);
		CHECK(there("Roms/Game Boy/"));          /* the console's folder stays */
	}

	{
		/* A game already taken off the card by File Transfer: its saves and
		 * cover go all the same */
		static const char *const gone[] = {
			"Roms/PlayStation/.media/Crash Bandicoot 2 (USA).png",
			"Roms/PlayStation/.media/",             /* the last cover in it */
			"Saves/Crash Bandicoot 2 (USA).srm",
			"shared/.zyperos/PlayStation/Crash Bandicoot 2 (USA).cue.auto.state",
			"shared/.zyperos/PlayStation/",         /* the last state in it */
			NULL
		};
		char p[1200];

		path_of(p, sizeof p, "Roms/PlayStation/Crash Bandicoot 2 (USA).cue");
		CHECK(unlink(p) == 0);
		path_of(p, sizeof p, "Roms/PlayStation/Crash Bandicoot 2 (USA).bin");
		CHECK(unlink(p) == 0);
		deleted("a game whose files are already gone: what is left of it:",
		        PS1, "Crash Bandicoot 2 (USA).cue", "Crash Bandicoot 2 (USA)", gone, 3);
	}

	/* The Ports shelf: a port's script and its own folders, by its port.json
	 * or its GAMEDIR; never PortMaster's, never a folder another port names */
	{
		static const char *const gone[] = {
			"Roms/Ports/StardewValley.sh", "Roms/Ports/stardewvalley/",
			"Roms/Ports/stardewvalley/port.json", "Roms/Ports/stardewvalley/gamedata/",
			"Roms/Ports/stardewvalley/gamedata/Stardew Valley.exe",
			"Roms/Ports/stardewvalley/conf/", "Roms/Ports/stardewvalley/conf/save.dat",
			"Roms/Ports/.media/StardewValley.png", "Roms/Ports/.media/",
			NULL
		};
		deleted("a port: its script and the folder its port.json names, with its cover:",
		        PRT, "StardewValley.sh", "StardewValley", gone, 5);
	}
	{
		static const char *const gone[] = {
			"Roms/Ports/Celeste.sh", "Roms/Ports/celeste/", "Roms/Ports/celeste/Celeste.exe", NULL
		};
		deleted("a port with no port.json: the folder its script's GAMEDIR names:",
		        PRT, "Celeste.sh", "Celeste", gone, 2);
	}
	{
		static const char *const gone[] = {
			"Roms/Ports/Twin One.sh", "Roms/Ports/twinone/", "Roms/Ports/twinone/port.json",
			"Roms/Ports/twinone/a.dat", NULL
		};
		deleted("two ports that share a folder: the one deleted leaves it to the other:",
		        PRT, "Twin One.sh", "Twin One", gone, 3);
		CHECK(there("Roms/Ports/common/engine.bin"));
	}
	{
		/* Never PortMaster's folder by a name the filesystem would bend onto
		 * it, never a folder the shelf lists as a game of its own */
		static const char *const gone[] = {
			"Roms/Ports/Sly.sh", "Roms/Ports/sly/", "Roms/Ports/sly/port.json", "Roms/Ports/sly/sly.dat",
			NULL
		};
		deleted("a port.json naming \"PortMaster.\" and a folder game: neither goes with the port:",
		        PRT, "Sly.sh", "Sly", gone, 3);
		CHECK(there("Roms/Ports/PortMaster/libs/mono.squashfs"));
		CHECK(there("Roms/Ports/disc/game.iso"));
	}
	refused("PortMaster, which every port runs on, is refused:", PRT, "PortMaster.sh", "PortMaster",
	        GD_RUNTIME);
	refused("a port nothing names a folder for is refused:", PRT, "Mystery.sh", "Mystery",
	        GD_PORT_UNKNOWN);
	{
		/* ...but PortMaster's plan is made, folder and all, for its size */
		gd_card card = { roms, saves, states };
		gd_game g = { PRT->folder, PRT->tag, PRT->core, PRT->exts, "PortMaster.sh", "PortMaster" };
		int n = others_of(PRT->folder, "PortMaster.sh", g_others, 256);
		char want[600];
		bool has = false;

		printf("PortMaster's plan holds its folder, for Storage's size of it:\n");
		CHECK(!gd_plan_make(&card, &g, g_others, n, &g_plan) && g_plan.why == GD_RUNTIME);
		snprintf(want, sizeof want, "%s/Ports/PortMaster", roms);
		for (int i = 0; i < g_plan.n; i++)
			if (!strcmp(g_plan.path[i], want) && g_plan.kind[i] == GD_TREE) has = true;
		CHECK(has);
	}

	db_shutdown();
	{
		char cmd[400];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) {
		printf("check-gamedel: %d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("check-gamedel: ok\n");
	return 0;
}
