/* SPDX-License-Identifier: GPL-3.0-only */
/* Which systems get videos and for which games (src/snaps.h, 2026-11-13).
 *
 *     make check-snaps
 *
 * The real src/snaps.c on a database in memory: a system's videos are off
 * until it is turned on, a card that was getting them in 1.0.4 still does,
 * a system's own switch wins over that, Videos for is kept and read, and its
 * rule is all / favorites / played. And the list a page counts videos by. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/db.h"
#include "../src/snaps.h"

static int failures;

#define CHECK(cond, what)                                                     \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: %s\n        at %s:%d: %s\n", what, __FILE__, __LINE__, #cond); \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static void touch(const char *path)
{
	FILE *f = fopen(path, "wb");

	if (f) { fputs("x", f); fclose(f); }
}

int main(void)
{
	char root[] = "/tmp/zyperos-snaps-XXXXXX", p[600];
	char **v;
	int n;

	printf("snaps: which systems get videos, and for which games\n");
	CHECK(db_init(":memory:", ":memory:", NULL), "no database");

	printf("  off to start, every system:\n");
	CHECK(!snaps_get("NES") && !snaps_get("SNES"), "a system gets videos it was never turned on for");

	printf("  a card that was getting them in 1.0.4 still does, but not a console turned off:\n");
	snaps_set_fetch(true);
	snaps_set_console("SNES", false);
	CHECK(snaps_get("NES"), "1.0.4's Fetch with Box Art was forgotten");
	CHECK(!snaps_get("SNES"), "1.0.4's console turned off gets videos");

	printf("  a system's own switch wins, both ways:\n");
	snaps_set_get("NES", false);
	snaps_set_get("SNES", true);
	CHECK(!snaps_get("NES") && snaps_get("SNES"), "the system's own switch did not win");
	snaps_set_fetch(false);
	CHECK(snaps_get("SNES") && !snaps_get("Genesis"), "the switch followed the old setting");
	CHECK(!snaps_get("") && !snaps_get(NULL), "a system with no folder gets videos");

	printf("  covers on to start, and a system's switch kept:\n");
	CHECK(snaps_covers("NES") && snaps_covers("Arcade"), "covers are not on to start");
	snaps_set_covers("Arcade", false);
	CHECK(!snaps_covers("Arcade") && snaps_covers("NES"), "the covers switch was not kept, or not its own");
	snaps_set_covers("Arcade", true);
	CHECK(snaps_covers("Arcade"), "covers did not come back on");

	printf("  Videos for: all to start, kept, and its rule:\n");
	CHECK(snaps_for() == SNAPS_FOR_ALL, "Videos for is not all to start");
	snaps_set_for(SNAPS_FOR_PLAYED);
	CHECK(snaps_for() == SNAPS_FOR_PLAYED, "Videos for was not kept");
	snaps_set_for(99);
	CHECK(snaps_for() == SNAPS_FOR_ALL, "a wrong choice was kept");
	CHECK(snaps_for_game(SNAPS_FOR_ALL, false, false), "all left a game out");
	CHECK(snaps_for_game(SNAPS_FOR_FAVORITES, true, false) && !snaps_for_game(SNAPS_FOR_FAVORITES, false, true),
	      "favorites is not the favorites");
	CHECK(snaps_for_game(SNAPS_FOR_PLAYED, false, true) && !snaps_for_game(SNAPS_FOR_PLAYED, true, false),
	      "played is not the games played");

	printf("  a system's videos, listed sorted, only .mp4:\n");
	CHECK(mkdtemp(root) != NULL, "no folder");
	snprintf(p, sizeof p, "%s/NES", root);        mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/NES/.media", root); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/NES/.media/Metroid.mp4", root);  touch(p);
	snprintf(p, sizeof p, "%s/NES/.media/Contra.MP4", root);   touch(p);
	snprintf(p, sizeof p, "%s/NES/.media/Contra.png", root);   touch(p);
	snprintf(p, sizeof p, "%s/NES/.media/.hidden.mp4", root);  touch(p);
	v = snaps_stems(root, "NES", &n);
	CHECK(n == 2 && v && !strcmp(v[0], "Contra") && !strcmp(v[1], "Metroid"), "the list is wrong");
	snaps_stems_free(v, n);
	v = snaps_stems(root, "SNES", &n);
	CHECK(!v && n == 0, "a system with no folder listed something");
	snaps_delete(root, "NES");
	snprintf(p, sizeof p, "%s/NES/.media/Contra.png", root);   unlink(p);
	snprintf(p, sizeof p, "%s/NES/.media/.hidden.mp4", root);  unlink(p);
	snprintf(p, sizeof p, "%s/NES/.media", root); rmdir(p);
	snprintf(p, sizeof p, "%s/NES", root);        rmdir(p);
	rmdir(root);

	db_shutdown();
	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok: videos only where they were asked for\n");
	return 0;
}
