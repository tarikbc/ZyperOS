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
 * In a folder of games (game_entry.dir): the folder stays and only the
 * game's own part takes the name, its file or its disc folder; what its
 * format keeps beside it is looked for in that folder of games; a name
 * another game has in any folder of the shelf is taken.
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
static const shelf PSP = { "PSP", "PSP", "ppsspp", "iso,cso,chd,pbp" };

static const shelf *ALL[] = { &GB, &GBC, &PS1, &ARC, &SCUMM, &NDS, &PRT, &PSP };
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
			                           lib_shelf_hooked(ALL[i]->exts), e->dir };
		}
	}
	if (!me) { printf("  (no %s on %s)\n", file, s->folder); *why = GR_GONE; return false; }
	{
		gd_game g = { s->folder, s->tag, s->core, s->exts, me->file, me->name, me->dir };
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
		CHECK(gr_renames("mgba", "Tetris (World).gb", 0) && !gr_renames("fbneo", "mslug.zip", 0) &&
		      !gr_renames("mame2003_plus", "galaga.zip", 0) && !gr_renames("port", "Celeste.sh", 0) &&
		      !gr_renames("flycast2021", "ikaruga.zip", 0) && gr_renames("flycast2021", "Crazy Taxi.chd", 0) &&
		      !gr_renames("mgba", "neogeo.zip", 0));
		/* Past the folders of games it is kept in */
		CHECK(gr_renames("mgba", "Rom Hacks/Pokemon Brown.gb", 9) &&
		      !gr_renames("flycast2021", "NAOMI/ikaruga.zip", 5) &&
		      gr_renames("flycast2021", "Crazy Taxi/ct.zip", 0) &&
		      !gr_renames("mgba", "BIOS/neogeo.zip", 4) &&
		      !gr_renames("mgba", "../x.gb", 2) && !gr_renames("mgba", "a//x.gb", 2) &&
		      !gr_renames("mgba", "x.gb", 4) && !gr_renames("mgba", "Rom Hacks", 9));
		CHECK(!strcmp(gd_state_base("Tekken 6/EBOOT.PBP", "Tekken 6", false), "Tekken 6") &&
		      !strcmp(gd_state_base("Rom Hacks/x.gb", "x", false), "x.gb") &&
		      !strcmp(gd_state_base("x.gb", "x", true), "x.gb"));
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

	printf("in a folder of games:\n");
	put("Roms/Game Boy/Rom Hacks/Pokemon Brown.gb", "rom");
	put("Roms/Game Boy/Rom Hacks/Pokemon Crystal Clear.gb", "rom");
	put("Roms/Game Boy/Rom Hacks/Gen 2/Pokemon Prism.gb", "rom");
	put("Roms/Game Boy/.media/Pokemon Brown.png", "png");
	put("Saves/Pokemon Brown.srm", "save");
	put("shared/.zyperos/Game Boy/Pokemon Brown.gb.auto.state", "st");
	put("shared/.zyperos/Game Boy/Pokemon Prism.gb.auto.state", "st");
	fav_toggle("GB", "Rom Hacks/Pokemon Brown.gb");
	fav_save();
	session("GB", "Rom Hacks/Pokemon Brown.gb", 400);
	CHECK(rename_game(&GB, "Rom Hacks/Pokemon Brown.gb", "Brown Remix", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "Rom Hacks/Brown Remix.gb"));
	CHECK(there("Roms/Game Boy/Rom Hacks/Brown Remix.gb") && !there("Roms/Game Boy/Rom Hacks/Pokemon Brown.gb"));
	CHECK(there("Roms/Game Boy/Rom Hacks/Pokemon Crystal Clear.gb") &&      /* the folder stays */
	      there("Roms/Game Boy/Rom Hacks/Gen 2/Pokemon Prism.gb"));
	CHECK(!there("Roms/Game Boy/Brown Remix") && !there("Roms/Game Boy/Brown Remix.gb"));
	CHECK(there("Saves/Brown Remix.srm") && !there("Saves/Pokemon Brown.srm"));
	CHECK(there("shared/.zyperos/Game Boy/Brown Remix.gb.auto.state") &&
	      !there("shared/.zyperos/Game Boy/Pokemon Brown.gb.auto.state"));
	CHECK(there("Roms/Game Boy/.media/Brown Remix.png") && !there("Roms/Game Boy/.media/Pokemon Brown.png"));
	CHECK(fav_is("GB", "Rom Hacks/Brown Remix.gb") && !fav_is("GB", "Rom Hacks/Pokemon Brown.gb"));
	CHECK(db_has(db_lib(), "sess.400.400.GB\tRom Hacks/Brown Remix.gb") &&
	      !db_has(db_lib(), "sess.400.400.GB\tRom Hacks/Pokemon Brown.gb"));
	/* Two folders deep */
	CHECK(rename_game(&GB, "Rom Hacks/Gen 2/Pokemon Prism.gb", "Prism", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "Rom Hacks/Gen 2/Prism.gb") && there("Roms/Game Boy/Rom Hacks/Gen 2/Prism.gb"));
	CHECK(there("shared/.zyperos/Game Boy/Prism.gb.auto.state") && !there("Roms/Game Boy/Prism.gb"));
	/* A name another game has in any folder of the shelf: one .media for all */
	CHECK(!rename_game(&GB, "TETRIS DX TEST.gb", "pokemon crystal clear", &why, &done) && why == GR_TAKEN);
	CHECK(!rename_game(&GB, "Rom Hacks/Brown Remix.gb", "Tetris (World) (Rev 1)", &why, &done) &&
	      why == GR_TAKEN);
	CHECK(there("Roms/Game Boy/TETRIS DX TEST.gb") && there("Roms/Game Boy/Rom Hacks/Brown Remix.gb"));

	printf("a disc sheet and a disc folder in a folder of games:\n");
	put("Roms/PlayStation/PS1 Hacks/Spyro Hack.cue", "FILE \"Spyro Hack.bin\" BINARY");
	put("Roms/PlayStation/PS1 Hacks/Spyro Hack.bin", "bin");
	put("Roms/PlayStation/PS1 Hacks/Spyro Hack.sbi", "sbi");
	put("Roms/PlayStation/PS1 Hacks/Crash Hack.chd", "chd");    /* two games: a folder of them */
	put("Roms/PlayStation/Spyro Hack.sbi", "another's");
	put("Roms/PlayStation/PS1 Hacks/FF7 Hack/FF7 Hack.m3u", "FF7 Hack (Disc 1).chd");
	put("Roms/PlayStation/PS1 Hacks/FF7 Hack/FF7 Hack (Disc 1).chd", "chd");
	put("Roms/PlayStation/.media/FF7 Hack.png", "png");
	CHECK(rename_game(&PS1, "PS1 Hacks/Spyro Hack.cue", "Spyro Remix", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "PS1 Hacks/Spyro Remix.cue"));
	CHECK(there("Roms/PlayStation/PS1 Hacks/Spyro Remix.cue") && there("Roms/PlayStation/PS1 Hacks/Spyro Remix.sbi"));
	CHECK(there("Roms/PlayStation/PS1 Hacks/Spyro Hack.bin"));             /* named in the sheet */
	CHECK(there("Roms/PlayStation/Spyro Hack.sbi") && !there("Roms/PlayStation/Spyro Remix.sbi"));
	CHECK(rename_game(&PS1, "PS1 Hacks/FF7 Hack/FF7 Hack.m3u", "FF7 Remix", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "PS1 Hacks/FF7 Remix/FF7 Hack.m3u"));
	CHECK(there("Roms/PlayStation/PS1 Hacks/FF7 Remix/FF7 Hack (Disc 1).chd") &&
	      !there("Roms/PlayStation/PS1 Hacks/FF7 Hack"));
	CHECK(there("Roms/PlayStation/PS1 Hacks/Spyro Remix.cue") && there("Roms/PlayStation/PS1 Hacks/Crash Hack.chd") &&
	      !there("Roms/PlayStation/FF7 Remix"));
	CHECK(there("Roms/PlayStation/.media/FF7 Remix.png"));

	printf("a PSP game's EBOOT.PBP:\n");
	put("Roms/PSP/Tekken 6/EBOOT.PBP", "pbp");
	put("shared/.zyperos/PSP/Tekken 6.auto.state", "st");
	CHECK(rename_game(&PSP, "Tekken 6/EBOOT.PBP", "Tekken Six", &why, &done) && !done.failed);
	CHECK(!strcmp(P.new_file, "Tekken Six/EBOOT.PBP") && there("Roms/PSP/Tekken Six/EBOOT.PBP"));
	CHECK(there("shared/.zyperos/PSP/Tekken Six.auto.state") && !there("shared/.zyperos/PSP/Tekken 6.auto.state"));

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
	/* Its entry for a game in a folder of games: "./Hacks/..." */
	put("Roms/nds/Hacks/Mario Kart Plus.nds", "rom");
	put("Roms/nds/images/Mario Kart Plus-image.png", "png");
	put("Roms/nds/gamelist.xml",
	    "<gameList>\n<game>\n<path>./MKDS.nds</path>\n<name>Mario Kart DS</name>\n"
	    "<image>./images/MKDS-image.png</image>\n</game>\n"
	    "<game>\n<path>./Hacks/Mario Kart Plus.nds</path>\n<name>Mario Kart Plus</name>\n"
	    "<image>./images/Mario Kart Plus-image.png</image>\n</game>\n</gameList>\n");
	es_forget();
	CHECK(rename_game(&NDS, "Hacks/Mario Kart Plus.nds", "MK Plus", &why, &done) && !done.failed && done.es_list);
	CHECK(!strcmp(P.new_file, "Hacks/MK Plus.nds") && there("Roms/nds/Hacks/MK Plus.nds") &&
	      there("Roms/nds/images/MK Plus-image.png") && !there("Roms/nds/images/Mario Kart Plus-image.png"));
	{
		char p[1200], buf[1200] = "";
		FILE *f;

		snprintf(p, sizeof p, "%s/nds/gamelist.xml", roms);
		if ((f = fopen(p, "r"))) { size_t r = fread(buf, 1, sizeof buf - 1, f); buf[r] = '\0'; fclose(f); }
		CHECK(strstr(buf, "<path>./Hacks/MK Plus.nds</path>") &&
		      strstr(buf, "<image>./images/MK Plus-image.png</image>") &&
		      strstr(buf, "<path>./MKDS.nds</path>"));
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
