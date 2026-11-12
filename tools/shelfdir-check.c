/* SPDX-License-Identifier: GPL-3.0-only */
/* Folders on a shelf (src/shelfdir.h), on made-up lists: what a folder shows,
 * its folders first by name with their counts at every depth, then its own
 * games in the list's order; the top; a folder's own folder; and a folder
 * with no game at any depth, which is none.
 *
 *   make check-shelfdir
 */
#include "../src/shelfdir.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static game_entry G[32];
static int NG;

/* A game at `file`, kept in the folder its first `dir` bytes name */
static void game(const char *file, unsigned dir, const char *name)
{
	game_entry *g = &G[NG++];

	memset(g, 0, sizeof *g);
	snprintf(g->file, sizeof g->file, "%s", file);
	snprintf(g->name, sizeof g->name, "%s", name);
	snprintf(g->title, sizeof g->title, "%s", name);
	g->dir = (unsigned short)dir;
}

int main(void)
{
	game_list all = { 0 }, shown = { 0 };
	char out[LIB_PATH];
	int nf;

	/* The shelf in its scan's order, A to Z by name */
	game("Advance Wars (USA).gba", 0, "Advance Wars (USA)");
	game("Rom Hacks/Pokemon/Gaia.gba", 17, "Gaia");
	game("Homebrew/Anguna.gba", 8, "Anguna");
	game("Golden Sun (USA).gba", 0, "Golden Sun (USA)");
	game("Rom Hacks/Metroid Redux.gba", 9, "Metroid Redux");
	game("Rom Hacks/Pokemon/Unbound.gba", 17, "Unbound");
	game("Rom Hacks2/Other.gba", 10, "Other");
	game("homebrew b/Kunoichi.gba", 10, "Kunoichi");
	/* A disc set kept in a folder: the set's own folder is the game's */
	game("RPGs/Xeno/Xeno (Disc 1).cue", 4, "Xeno");
	all.items = G;
	all.count = NG;

	printf("the top: its folders A to Z, each with its games at every depth, then its games:\n");
	CHECK(sd_any(&all));
	nf = sd_build(&all, "", &shown);
	CHECK(nf == 5 && shown.count == 7);
	CHECK(!strcmp(shown.items[0].file, "Homebrew") && shown.items[0].folder == 1);
	CHECK(!strcmp(shown.items[1].file, "homebrew b") && shown.items[1].folder == 1);
	CHECK(!strcmp(shown.items[2].file, "Rom Hacks") && shown.items[2].folder == 3);
	CHECK(!strcmp(shown.items[3].file, "Rom Hacks2") && shown.items[3].folder == 1);
	CHECK(!strcmp(shown.items[4].file, "RPGs") && shown.items[4].folder == 1);
	CHECK(!strcmp(shown.items[4].name, "RPGs") && !strcmp(shown.items[4].title, "RPGs"));
	CHECK(!strcmp(shown.items[5].file, "Advance Wars (USA).gba") && !shown.items[5].folder);
	CHECK(!strcmp(shown.items[6].file, "Golden Sun (USA).gba"));

	printf("a folder: its own folders, then its games, and not a folder of a like name:\n");
	nf = sd_build(&all, "Rom Hacks", &shown);
	CHECK(nf == 1 && shown.count == 2);
	CHECK(!strcmp(shown.items[0].file, "Rom Hacks/Pokemon") && shown.items[0].folder == 2);
	CHECK(!strcmp(shown.items[0].name, "Pokemon") && shown.items[0].dir == 9);
	CHECK(!strcmp(shown.items[1].file, "Rom Hacks/Metroid Redux.gba"));
	nf = sd_build(&all, "Rom Hacks/Pokemon", &shown);
	CHECK(nf == 0 && shown.count == 2);
	CHECK(!strcmp(shown.items[0].name, "Gaia") && !strcmp(shown.items[1].name, "Unbound"));

	printf("a disc set in a folder is a game there, never a folder:\n");
	nf = sd_build(&all, "RPGs", &shown);
	CHECK(nf == 0 && shown.count == 1 && !strcmp(shown.items[0].name, "Xeno") &&
	      !shown.items[0].folder);

	printf("a folder that is not there shows nothing:\n");
	CHECK(sd_build(&all, "Gone", &shown) == 0 && shown.count == 0);
	CHECK(sd_build(&all, "Rom", &shown) == 0 && shown.count == 0);

	printf("where a game is, and a folder's own folder:\n");
	CHECK(sd_in(&G[1], "Rom Hacks/Pokemon") && !sd_in(&G[1], "Rom Hacks") && !sd_in(&G[1], ""));
	CHECK(sd_in(&G[0], "") && !sd_in(&G[0], "Rom Hacks"));
	CHECK(!sd_in(&G[6], "Rom Hacks"));
	sd_dir_of(&G[1], out, sizeof out);
	CHECK(!strcmp(out, "Rom Hacks/Pokemon"));
	sd_dir_of(&G[8], out, sizeof out);
	CHECK(!strcmp(out, "RPGs"));
	sd_parent("Rom Hacks/Pokemon", out, sizeof out);
	CHECK(!strcmp(out, "Rom Hacks"));
	sd_parent("Rom Hacks", out, sizeof out);
	CHECK(!strcmp(out, ""));
	sd_parent("", out, sizeof out);
	CHECK(!strcmp(out, ""));
	CHECK(!strcmp(sd_base("Rom Hacks/Pokemon"), "Pokemon") && !strcmp(sd_base("RPGs"), "RPGs"));

	printf("a shelf with no game in a folder has no folders:\n");
	all.count = 1;
	CHECK(!sd_any(&all));
	nf = sd_build(&all, "", &shown);
	CHECK(nf == 0 && shown.count == 1);
	all.count = 0;
	CHECK(sd_build(&all, "", &shown) == 0 && shown.count == 0);
	lib_free(&shown);

	if (failures) {
		printf("check-shelfdir: %d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("ok: a shelf's folders, first and counted, and its games in them\n");
	return 0;
}
