/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Display > Game Names (DESIGN.md 11.71): Short, as it ships, or
 * File Name, every game by its file's whole name.
 *
 * 2026-11-23, DoctorIssa on the Discord, ~900 games: the brackets are how
 * one dump is told from another, "(v1.1)", a region, a disc, and the shelf
 * cut them all off. The real scan over a real folder, with the real
 * database: a downloaded title is the shelf's name under Short and gives
 * way to the file's under File Name; a game in a folder of its own goes by
 * its folder; the article stays where the file has it; the name order and
 * a refresh follow the setting both ways. */
#include "../src/db.h"
#include "../src/library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what); failures++; } } while (0)

static char root[] = "/tmp/zyperos-gamenames-XXXXXX";

static void touch(const char *rel)
{
	char p[1200];
	FILE *f;

	snprintf(p, sizeof p, "%s/Roms/%s", root, rel);
	f = fopen(p, "wb");
	if (f) { fputs("x", f); fclose(f); }
}

static void folder(const char *rel)
{
	char p[1200];

	snprintf(p, sizeof p, "%s/Roms/%s", root, rel);
	mkdir(p, 0755);
}

static int rm_one(const char *p, const struct stat *s, int flag, struct FTW *w)
{
	(void)s; (void)flag; (void)w;
	return remove(p);
}

/* The entry whose launch path is `file`, or NULL */
static const game_entry *by_file(const game_list *l, const char *file)
{
	int k = lib_find_entry(l, NULL, 0, file);

	return k >= 0 ? &l->items[k] : NULL;
}

static const char *title_of(const game_list *l, const char *file)
{
	const game_entry *e = by_file(l, file);

	return e ? e->title : "(missing)";
}

static void scan(game_list *l)
{
	char roms[1100];

	snprintf(roms, sizeof roms, "%s/Roms", root);
	memset(l, 0, sizeof *l);
	lib_scan(roms, "PS", "chd,cue,m3u,bin", l);
}

#define FF7    "Final Fantasy VII (USA) (Disc 1).chd"
#define ZELDA  "Legend of Zelda, The - Link's Awakening (USA, Europe) (Rev 2).chd"
#define MARIO  "Mario Bros (v1.1).chd"
#define SET    "Chrono Cross (USA)/Chrono Cross (USA) (Disc 1).cue"
#define ALPHA  "Alpha (Japan).chd"

int main(void)
{
	char lib[1200], dev[1200], out[LIB_NAME];
	game_list l;
	game_meta m;

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(lib, sizeof lib, "%s/library.db", root);
	snprintf(dev, sizeof dev, "%s/device.db", root);
	if (!db_init(dev, lib, NULL)) { fprintf(stderr, "no database\n"); return 1; }
	folder("");
	folder("PS");
	folder("PS/Chrono Cross (USA)");
	touch("PS/" FF7);
	touch("PS/" ZELDA);
	touch("PS/" MARIO);
	touch("PS/Chrono Cross (USA)/Chrono Cross (USA) (Disc 1).cue");
	touch("PS/Chrono Cross (USA)/Chrono Cross (USA) (Disc 1).bin");
	touch("PS/" ALPHA);
	/* A downloaded name for Mario Bros, as MENU > Downloads' Names keeps it */
	memset(&m, 0, sizeof m);
	snprintf(m.title, sizeof m.title, "%s", "Mario Bros. (Arcade Classic)");
	CHECK(db_game_set(db_lib(), "PS", MARIO, &m), "the downloaded name is kept");

	/* Short, as it ships: nothing changes for anyone who never sets it */
	CHECK(!lib_names_file(), "Game Names starts on Short");
	scan(&l);
	CHECK(!strcmp(title_of(&l, FF7), "Final Fantasy VII"), "Short cuts the brackets");
	CHECK(!strcmp(title_of(&l, ZELDA), "The Legend of Zelda - Link's Awakening"),
	      "Short puts the article in front");
	CHECK(!strcmp(title_of(&l, MARIO), "Mario Bros. (Arcade Classic)"),
	      "Short shows the downloaded name, its own brackets in it");
	CHECK(!strcmp(title_of(&l, SET), "Chrono Cross"), "Short names a set by its folder, cut");

	/* File Name: the file's whole name, over a downloaded one */
	lib_names_set(true);
	CHECK(lib_names_file(), "File Name is on once set");
	lib_free(&l);
	scan(&l);
	CHECK(!strcmp(title_of(&l, FF7), "Final Fantasy VII (USA) (Disc 1)"),
	      "File Name keeps every bracket, less the extension");
	CHECK(!strcmp(title_of(&l, ZELDA), "Legend of Zelda, The - Link's Awakening (USA, Europe) (Rev 2)"),
	      "File Name leaves the article where the file has it");
	CHECK(!strcmp(title_of(&l, MARIO), "Mario Bros (v1.1)"),
	      "File Name is over the downloaded name: its version is what the player asked to see");
	CHECK(!strcmp(title_of(&l, SET), "Chrono Cross (USA)"),
	      "a game in a folder of its own goes by its folder's whole name");
	/* By the name on screen: A, C, F, L, M */
	CHECK(l.count == 5 && !strcmp(l.items[0].file, ALPHA) && !strcmp(l.items[1].file, SET) &&
	      !strcmp(l.items[2].file, FF7) && !strcmp(l.items[3].file, ZELDA) &&
	      !strcmp(l.items[4].file, MARIO), "the shelf is in the order of the names it shows");
	lib_display_title(db_lib(), "PS", MARIO, "Mario Bros (v1.1)", out, sizeof out);
	CHECK(!strcmp(out, "Mario Bros (v1.1)"), "lib_display_title answers the same as the scan");

	/* Back to Short in place, as MENU's row does (refresh_game_titles):
	 * each entry its short name again, and it says it changed */
	lib_names_set(false);
	{
		game_entry *e = (game_entry *)by_file(&l, MARIO);

		CHECK(e && lib_refresh_title(db_lib(), "PS", e), "a refresh to Short changes the title");
		CHECK(e && !strcmp(e->title, "Mario Bros. (Arcade Classic)"), "and gives the downloaded name back");
		lib_names_set(true);
		CHECK(e && lib_refresh_title(db_lib(), "PS", e), "a refresh to File Name changes it again");
		CHECK(e && !strcmp(e->title, "Mario Bros (v1.1)"), "to the file's whole name");
		CHECK(e && !lib_refresh_title(db_lib(), "PS", e), "and a second refresh changes nothing");
	}
	lib_free(&l);
	db_shutdown();
	nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (failures) return 1;
	printf("ok: Game Names: Short cuts the brackets, File Name shows the whole file name, over a downloaded one\n");
	return 0;
}
