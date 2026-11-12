/* Rename, on a card made of small files (src/gamern.h).
 *
 *   make check-gamern
 *
 * Each rename lists the card before and after: what moved must be exactly
 * the game's (its file or folder, its saves, states, Auto's language, cover,
 * video, Before's cover, achievement set, and the EmulationStation pictures
 * and video named after it), each under the new name; what another game
 * shares is copied, so that game keeps its own; and the library rows follow
 * (details with the title emptied, favorite, play sessions, Pak, where the
 * covers came from). Refused with nothing moved: a name no card takes, the
 * name it has, another game's name on the shelf, a path that would land on
 * another file, an arcade set, a BIOS set and a port. A change of case is a
 * rename even where the file system ignores case.
 *
 * Links src/gamern.c, src/gamedel.c, src/esmedia.c, src/library.c,
 * src/db.c, src/favorites.c, src/stats.c and src/portinfo.c, and not SDL.
 */
#include "../src/db.h"
#include "../src/favorites.h"
#include "../src/gamern.h"
#include "../src/library.h"
#include "../src/stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-gamern-check-XXXXXX";
static char roms[300], saves[300], states[300];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *rel, const char *text)
{
	char p[1200], *s;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	for (s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) {
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	if (!(f = fopen(p, "wb"))) { perror(p); exit(1); }
	fputs(text, f);
	fclose(f);
}

static bool there(const char *rel)
{
	char p[1200];

	snprintf(p, sizeof p, "%s/%s", root, rel);
	return access(p, F_OK) == 0;
}

/* The name a directory lists for `rel`'s last part, exactly (a card that
 * ignores case finds either spelling) */
static bool listed(const char *dir, const char *name)
{
	char cmd[1500], line[600];
	FILE *f;
	bool found = false;

	snprintf(cmd, sizeof cmd, "ls '%s/%s'", root, dir);
	if (!(f = popen(cmd, "r"))) return false;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (!strcmp(line, name)) found = true;
	}
	pclose(f);
	return found;
}

typedef struct { const char *folder, *tag, *core, *exts; } shelf;
static const shelf GB = { "Game Boy", "GB", "mgba", "gb,dmg,zip" };
static const shelf GBC = { "Game Boy Color", "GBC", "mgba", "gbc,cgb,zip" };
static const shelf PS1 = { "PlayStation", "PS1", "pcsx_rearmed", "cue,chd,pbp,m3u,ccd,toc,iso" };
static const shelf ARC = { "Arcade", "ARCADE", "fbneo", "zip,7z" };
static const shelf SCUMM = { "ScummVM", "SCUMMVM", "scummvm", "scummvm" };
static const shelf NDS = { "nds", "NDS", "dsperate", "nds,zip" };
static const shelf PRT = { "Ports", "PRT", "port", "sh" };

static const shelf *ALL[] = { &GB, &GBC, &PS1, &ARC, &SCUMM, &NDS, &PRT };
#define NSHELVES ((int)(sizeof ALL / sizeof ALL[0]))

/* Rename game `file` of `s` to `to`, against every other game on the card's
 * shelves as lib_scan_listing lists them; `p` holds the plan after */
static gr_plan P;

static bool rename_game(const shelf *s, const char *file, const char *to, gr_why *why, gr_done *done)
{
	static game_list lists[NSHELVES];
	static gd_other others[512];
	int no = 0, i, k;
	gd_card card = { roms, saves, states };
	const game_entry *me = NULL;

	for (i = 0; i < NSHELVES; i++) {
		lib_free(&lists[i]);
		lib_scan_listing(roms, ALL[i]->folder, ALL[i]->exts, &lists[i]);
		for (k = 0; k < lists[i].count; k++) {
			const game_entry *e = &lists[i].items[k];

			if (ALL[i] == s && !strcmp(e->file, file)) { me = e; continue; }
			others[no++] = (gd_other){ ALL[i]->folder, e->file, e->name,
			                           lib_shelf_hooked(ALL[i]->exts) };
		}
	}
	if (!me) { printf("  (no %s on %s)\n", file, s->folder); *why = GR_GONE; return false; }
	{
		gd_game g = { s->folder, s->tag, s->core, s->exts, me->file, me->name };
		char old_file[LIB_PATH], old_name[LIB_NAME];

		snprintf(old_file, sizeof old_file, "%s", me->file);
		snprintf(old_name, sizeof old_name, "%s", me->name);
		if (!gr_plan_make(&card, &g, others, no, to, &P)) { *why = P.why; return false; }
		*why = GR_OK;
		gr_plan_run(&P, done);
		gr_follow(s->folder, s->tag, old_file, old_name, P.new_file, P.new_name);
	}
	return done->game_moved;
}

static void session(const char *tag, const char *file, int t)
{
	char key[600];

	snprintf(key, sizeof key, "sess.%d.%d.%s\t%s", t, t, tag, file);
	db_set_str(db_lib(), key, "60\texit");
}

int main(void)
{
	char dev[400], lib[400], t[256];
	gr_why why;
	gr_done done;
	game_meta m = { 0 };

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

	/* A cartridge with everything, beside a .gbc of its stem on another
	 * shelf, which shares its save */
	put("Roms/Game Boy/Tetris (World).gb", "rom");
	put("Roms/Game Boy/Tetris (World) (Rev 1).gb", "rom");
	put("Roms/Game Boy/.media/Tetris (World).png", "png");
	put("Roms/Game Boy/.media/Tetris (World).mp4", "mp4");
	put("Roms/Game Boy/.media/.before/Tetris (World).png", "png");
	put("Roms/Game Boy/.cheevos/Tetris (World).set", "set");
	put("Roms/Game Boy Color/Tetris (World).gbc", "rom");
	put("Saves/Tetris (World).srm", "save");
	put("Saves/Tetris (World) (Rev 1).srm", "rev1");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.auto.state", "st");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.auto.bmp", "bmp");
	put("shared/.zyperos/Game Boy/Tetris (World).gb.3.state", "st");
	put("shared/.zyperos/Game Boy/Tetris (World) (Rev 1).gb.auto.state", "st");
	snprintf(m.title, sizeof m.title, "Tetris");
	snprintf(m.year, sizeof m.year, "1989");
	CHECK(db_game_set(db_lib(), "Game Boy", "Tetris (World).gb", &m));
	fav_toggle("GB", "Tetris (World).gb");
	fav_save();
	session("GB", "Tetris (World).gb", 100);
	session("GB", "Tetris (World).gb", 200);
	session("GBC", "Tetris (World).gbc", 300);
	db_set_str(db_lib(), "pak.GB.Tetris (World).gb", "memory");
	db_set_str(db_lib(), "artsrc.Game Boy/Tetris (World)", "lr");

	printf("names:\n");
	{
		char o[256];

		CHECK(gr_name_ok("  Tetris DX  ", o, sizeof o) && !strcmp(o, "Tetris DX"));
		CHECK(gr_name_ok("Pok\xc3\xa9mon Red", o, sizeof o));
		CHECK(!gr_name_ok("", o, sizeof o) && !gr_name_ok("   ", o, sizeof o));
		CHECK(!gr_name_ok(".hidden", o, sizeof o) && !gr_name_ok("dots.", o, sizeof o));
		CHECK(!gr_name_ok("a/b", o, sizeof o) && !gr_name_ok("a\\b", o, sizeof o) &&
		      !gr_name_ok("a:b", o, sizeof o) && !gr_name_ok("a*b", o, sizeof o) &&
		      !gr_name_ok("a?b", o, sizeof o) && !gr_name_ok("a\"b", o, sizeof o) &&
		      !gr_name_ok("a<b", o, sizeof o) && !gr_name_ok("a|b", o, sizeof o) &&
		      !gr_name_ok("a\tb", o, sizeof o));
		CHECK(!gr_name_ok("bad \xc3", o, sizeof o));
		CHECK(gr_renames("mgba", "Tetris (World).gb") && !gr_renames("fbneo", "mslug.zip") &&
		      !gr_renames("mame2003_plus", "galaga.zip") && !gr_renames("port", "Celeste.sh") &&
		      !gr_renames("flycast2021", "ikaruga.zip") && gr_renames("flycast2021", "Crazy Taxi.chd") &&
		      !gr_renames("mgba", "neogeo.zip"));
	}

	printf("a cartridge:\n");
	CHECK(rename_game(&GB, "Tetris (World).gb", "Tetris DX Test", &why, &done) && why == GR_OK);
	CHECK(!done.failed && done.copied == 1);
	CHECK(!there("Roms/Game Boy/Tetris (World).gb") && there("Roms/Game Boy/Tetris DX Test.gb"));
	CHECK(there("Roms/Game Boy/Tetris (World) (Rev 1).gb"));                 /* the neighbour */
	CHECK(there("Saves/Tetris (World).srm") && there("Saves/Tetris DX Test.srm"));   /* copied */
	CHECK(there("Saves/Tetris (World) (Rev 1).srm"));
	CHECK(there("Roms/Game Boy/.media/Tetris DX Test.png") && !there("Roms/Game Boy/.media/Tetris (World).png"));
	CHECK(there("Roms/Game Boy/.media/Tetris DX Test.mp4"));
	CHECK(there("Roms/Game Boy/.media/.before/Tetris DX Test.png"));
	CHECK(there("Roms/Game Boy/.cheevos/Tetris DX Test.set"));
	CHECK(there("shared/.zyperos/Game Boy/Tetris DX Test.gb.auto.state") &&
	      there("shared/.zyperos/Game Boy/Tetris DX Test.gb.auto.bmp") &&
	      there("shared/.zyperos/Game Boy/Tetris DX Test.gb.3.state") &&
	      !there("shared/.zyperos/Game Boy/Tetris (World).gb.auto.state"));
	CHECK(there("shared/.zyperos/Game Boy/Tetris (World) (Rev 1).gb.auto.state"));
	CHECK(!db_game_title(db_lib(), "Game Boy", "Tetris DX Test.gb", t, sizeof t));   /* emptied */
	CHECK(db_game_get(db_lib(), "Game Boy", "Tetris DX Test.gb", &m) && !strcmp(m.year, "1989"));
	CHECK(!db_game_get(db_lib(), "Game Boy", "Tetris (World).gb", &m));
	CHECK(fav_is("GB", "Tetris DX Test.gb") && !fav_is("GB", "Tetris (World).gb"));
	fav_load();
	CHECK(fav_is("GB", "Tetris DX Test.gb") && !fav_is("GB", "Tetris (World).gb"));
	CHECK(db_get_str(db_lib(), "pak.GB.Tetris DX Test.gb", t, sizeof t, NULL) && !strcmp(t, "memory"));
	CHECK(!db_has(db_lib(), "pak.GB.Tetris (World).gb"));
	CHECK(db_has(db_lib(), "artsrc.Game Boy/Tetris DX Test") && !db_has(db_lib(), "artsrc.Game Boy/Tetris (World)"));
	CHECK(db_has(db_lib(), "sess.100.100.GB\tTetris DX Test.gb") && db_has(db_lib(), "sess.200.200.GB\tTetris DX Test.gb"));
	CHECK(!db_has(db_lib(), "sess.100.100.GB\tTetris (World).gb"));
	CHECK(db_has(db_lib(), "sess.300.300.GBC\tTetris (World).gbc"));       /* the other's */

	printf("refused, nothing moved:\n");
	CHECK(!rename_game(&GB, "Tetris DX Test.gb", "a/b", &why, &done) && why == GR_BAD_NAME);
	CHECK(!rename_game(&GB, "Tetris DX Test.gb", "Tetris DX Test", &why, &done) && why == GR_SAME);
	CHECK(!rename_game(&GB, "Tetris DX Test.gb", "tetris (world) (rev 1)", &why, &done) && why == GR_TAKEN);
	put("Saves/Lone.srm", "someone else's");
	CHECK(!rename_game(&GB, "Tetris DX Test.gb", "Lone", &why, &done) && why == GR_TAKEN);
	CHECK(there("Roms/Game Boy/Tetris DX Test.gb") && !there("Roms/Game Boy/Lone.gb"));
	put("Roms/Arcade/mslug.zip", "set");
	put("Roms/Arcade/neogeo.zip", "bios");
	CHECK(!rename_game(&ARC, "mslug.zip", "Metal Slug", &why, &done) && why == GR_KEEPS_NAME);
	put("Roms/Ports/Celeste.sh", "#!/bin/sh");
	CHECK(!rename_game(&PRT, "Celeste.sh", "Celeste 2", &why, &done) && why == GR_KEEPS_NAME);
	CHECK(there("Roms/Arcade/mslug.zip") && there("Roms/Ports/Celeste.sh"));

	printf("a change of case:\n");
	CHECK(rename_game(&GB, "Tetris DX Test.gb", "TETRIS DX TEST", &why, &done) && why == GR_OK && !done.failed);
	CHECK(listed("Roms/Game Boy", "TETRIS DX TEST.gb") && !listed("Roms/Game Boy", "Tetris DX Test.gb"));
	CHECK(listed("Saves", "TETRIS DX TEST.srm"));

	printf("a disc sheet and a disc folder:\n");
	put("Roms/PlayStation/Crash (USA).cue", "FILE \"Crash (USA).bin\" BINARY");
	put("Roms/PlayStation/Crash (USA).bin", "bin");
	put("Roms/PlayStation/Crash (USA).sbi", "sbi");
	put("Saves/Crash (USA).srm", "card");
	put("Roms/PlayStation/Final Fantasy VII/Final Fantasy VII.m3u", "FF7 (Disc 1).chd");
	put("Roms/PlayStation/Final Fantasy VII/FF7 (Disc 1).chd", "chd");
	put("shared/.zyperos/PlayStation/Final Fantasy VII.m3u.auto.state", "st");
	put("Roms/PlayStation/.media/Final Fantasy VII.png", "png");
	CHECK(rename_game(&PS1, "Crash (USA).cue", "Crash Bandicoot", &why, &done) && !done.failed);
	CHECK(there("Roms/PlayStation/Crash Bandicoot.cue") && there("Roms/PlayStation/Crash Bandicoot.sbi"));
	CHECK(there("Roms/PlayStation/Crash (USA).bin") && !there("Roms/PlayStation/Crash Bandicoot.bin"));
	CHECK(there("Saves/Crash Bandicoot.srm") && !there("Saves/Crash (USA).srm"));
	CHECK(rename_game(&PS1, "Final Fantasy VII/Final Fantasy VII.m3u", "FF VII", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "FF VII/Final Fantasy VII.m3u"));
	CHECK(there("Roms/PlayStation/FF VII/FF7 (Disc 1).chd") && !there("Roms/PlayStation/Final Fantasy VII"));
	CHECK(there("shared/.zyperos/PlayStation/Final Fantasy VII.m3u.auto.state"));   /* the disc's */
	CHECK(there("Roms/PlayStation/.media/FF VII.png"));

	printf("a hooked folder:\n");
	put("Roms/ScummVM/Sky/sky.scummvm", "sky");
	put("shared/.zyperos/ScummVM/Sky.auto.state", "st");
	CHECK(rename_game(&SCUMM, "Sky/sky.scummvm", "Beneath a Steel Sky", &why, &done) && !done.failed);
	CHECK(there("Roms/ScummVM/Beneath a Steel Sky/sky.scummvm"));
	CHECK(there("shared/.zyperos/ScummVM/Beneath a Steel Sky.auto.state"));

	printf("EmulationStation's:\n");
	put("Roms/nds/Mario Kart DS (USA).nds", "rom");
	put("Roms/nds/images/Mario Kart DS (USA)-image.png", "png");
	put("Roms/nds/videos/Mario Kart DS (USA)-video.mp4", "mp4");
	put("Roms/nds/gamelist.xml",
	    "<gameList>\n<game>\n<path>./Mario Kart DS (USA).nds</path>\n<name>Mario Kart DS</name>\n"
	    "<image>./images/Mario Kart DS (USA)-image.png</image>\n</game>\n</gameList>\n");
	CHECK(rename_game(&NDS, "Mario Kart DS (USA).nds", "MKDS", &why, &done) && !done.failed && done.es_list);
	CHECK(there("Roms/nds/MKDS.nds") && there("Roms/nds/images/MKDS-image.png") &&
	      there("Roms/nds/videos/MKDS-video.mp4") && !there("Roms/nds/images/Mario Kart DS (USA)-image.png"));
	{
		char p[1200], buf[400] = "";
		FILE *f;

		snprintf(p, sizeof p, "%s/nds/gamelist.xml", roms);
		if ((f = fopen(p, "r"))) { size_t r = fread(buf, 1, sizeof buf - 1, f); buf[r] = '\0'; fclose(f); }
		CHECK(strstr(buf, "<path>./MKDS.nds</path>") && strstr(buf, "<image>./images/MKDS-image.png</image>"));
	}

	{
		char cmd[400];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) { printf("gamern: %d FAILED\n", failures); return 1; }
	printf("gamern: ok\n");
	return 0;
}
