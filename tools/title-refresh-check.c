/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual main.c sort, refresh and scrape-exit hooks; only view resources and
 * scrape transport are stubbed. This needs no window, network or device. */
#include "../src/library.h"
#include "../src/sort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#define CFG_MAX_SYSTEMS 4
typedef struct { char folder[64], tag[16]; } system_cfg;
typedef struct { int cursor; } coverflow;
typedef struct { game_list list; int *owner; int sort, cursor, dropped; coverflow cf; } sysview;
typedef struct { struct { int count; system_cfg systems[4]; } sys; sysview view[4]; int sys_cursor; bool running; } app;
typedef enum { MENU_STAY, MENU_CHANGED, MENU_FAILED } menu_result;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
/* The card's Roms, where sort_shelf reads an added time a scan left unknown */
static char roms[] = "/tmp/zyperos-added-XXXXXX";
static const char *P_ROMS = roms;
static void dated(const char *rel, bool dir, long when)
{
	char path[512]; struct utimbuf t = { (time_t)when, (time_t)when };
	snprintf(path, sizeof path, "%s/%s", roms, rel);
	if (dir) CHECK(mkdir(path, 0700) == 0);
	else { FILE *f = fopen(path, "wb"); CHECK(f != NULL); if (f) fclose(f); }
	CHECK(utime(path, &t) == 0);
}
static bool is_muse(const system_cfg *s) { return !strcmp(s->tag, "MUSE"); }
static void muse_order_view(sysview *v) { (void)v; failures++; }
static int shelf_owner(app *a, int s, int i) { return a->view[s].owner ? a->view[s].owner[i] : s; }
static void free_view_textures(sysview *v) { v->dropped++; }
static void cf_reset(coverflow *cf, int cursor) { cf->cursor = cursor; }
static menu_result next_result;
/* The scrape screen's panel (main.c's menu motion): opened and closed once */
static int panels_open, panels_closed;
static int panel_open(void) { return ++panels_open; }
static void panel_close(app *a, int lv, bool cut)
{
	(void)a; (void)cut;
	CHECK(lv == panels_open);
	panels_closed++;
}
static menu_result art_run(app *a, const char *only, const char *one,
                           const char *stem, const char *file, unsigned accent, int lv)
{
	game_meta m = {0};
	(void)a; (void)one; (void)stem; (void)accent;
	CHECK(lv == panels_open && panels_closed == panels_open - 1);
	strcpy(m.title, "Alpha");
	CHECK(db_game_set(db_lib(), only, file, &m));
	return next_result;
}
#include "title-refresh-hooks.h"

static void put(game_entry *e, const char *name, const char *file)
{
	memset(e, 0, sizeof *e); strcpy(e->name, name); strcpy(e->title, name); strcpy(e->file, file);
}
int main(void)
{
	app a = {0};
	game_entry nes[2], snes[1], fav[3], muse[1];
	int owners[3] = {1, 2, 1};
	CHECK(db_init(":memory:", ":memory:", NULL));
	a.sys.count = 4; a.sys_cursor = 0;
	strcpy(a.sys.systems[0].tag, "FAV");
	strcpy(a.sys.systems[1].tag, "NES"); strcpy(a.sys.systems[1].folder, "NES");
	strcpy(a.sys.systems[2].tag, "SNES"); strcpy(a.sys.systems[2].folder, "SNES");
	strcpy(a.sys.systems[3].tag, "MUSE");
	a.view[0].list = (game_list){.items = fav, .count = 3}; a.view[0].owner = owners;
	a.view[1].list = (game_list){.items = nes, .count = 2};
	a.view[2].list = (game_list){.items = snes, .count = 1};
	a.view[3].list = (game_list){.items = muse, .count = 1};
	/* Same filename on two systems. Refresh and movement must retain owners. */
	for (int outcome = MENU_STAY; outcome <= MENU_FAILED; outcome++) {
		game_meta empty = {0}; strcpy(empty.title, "Zulu");
		CHECK(db_game_set(db_lib(), "NES", "same.zip", &empty));
		put(&nes[0], "Middle", "middle.zip"); put(&nes[1], "Zulu", "same.zip");
		put(&snes[0], "Zulu", "same.zip");
		put(&muse[0], "Artist", "album"); strcpy(muse[0].title, "Album");
		fav[0] = nes[0]; fav[1] = snes[0]; fav[2] = nes[1];
		owners[0] = 1; owners[1] = 2; owners[2] = 1;
		a.view[0].cursor = 2; a.view[1].cursor = 1;
		next_result = (menu_result)outcome;
		CHECK(art_screen(&a, "NES", nes[1].title, nes[1].name, nes[1].file, 0) == next_result);
		CHECK(panels_open == outcome + 1 && panels_closed == panels_open);
		CHECK(a.view[0].cursor == 0 && a.view[1].cursor == 0);
		CHECK(owners[0] == 1 && !strcmp(fav[0].file, "same.zip") && !strcmp(fav[0].title, "Alpha"));
		CHECK(!strcmp(snes[0].title, "Zulu") && !strcmp(muse[0].title, "Album"));
		CHECK(!strcmp(nes[0].name, "Zulu") && !strcmp(nes[0].file, "same.zip"));
		CHECK(a.view[0].cf.cursor == 0 && a.view[1].cf.cursor == 0);
		CHECK(a.view[0].dropped == outcome + 1 && a.view[1].dropped == outcome + 1);
		CHECK(a.view[2].dropped == 0 && a.view[3].dropped == 0);
		refresh_game_titles(&a);
		CHECK(a.view[0].dropped == outcome + 1); /* unchanged labels do not churn textures */
	}
	/* The selected SNES copy must stay selected when NES gets a new title. */
	a.view[0].cursor = lib_find_entry(&a.view[0].list, owners, 2, "same.zip");
	game_meta m = {0}; strcpy(m.title, "ZZZ");
	CHECK(db_game_set(db_lib(), "NES", "same.zip", &m));
	refresh_game_titles(&a);
	CHECK(owners[a.view[0].cursor] == 2 && !strcmp(fav[a.view[0].cursor].file, "same.zip"));
	/* Recently-added order keeps its primary key through a title change. */
	a.view[1].sort = 3;
	nes[0].added = !strcmp(nes[0].file, "middle.zip") ? 20 : 10;
	nes[1].added = !strcmp(nes[1].file, "middle.zip") ? 20 : 10;
	strcpy(m.title, "A new title"); CHECK(db_game_set(db_lib(), "NES", "same.zip", &m));
	refresh_game_titles(&a);
	CHECK(!strcmp(nes[0].file, "middle.zip") && !strcmp(nes[a.view[1].cursor].file, "same.zip"));
	/* A scan leaves the added time unknown; Recently Added reads it from each
	 * game's own system's folder (Favorites' by owner): a game's file, a disc
	 * set's folder, 0 for one that is gone */
	CHECK(mkdtemp(roms) != NULL);
	dated("NES", true, 100); dated("SNES", true, 100);
	dated("NES/old.zip", false, 1000); dated("SNES/new.zip", false, 3000);
	dated("NES/Set", true, 2000); dated("NES/Set/Set.m3u", false, 500);
	{	/* The folder's time last: making the file in it moved it to now */
		char path[512]; struct utimbuf t = { 2000, 2000 };
		snprintf(path, sizeof path, "%s/NES/Set", roms); CHECK(utime(path, &t) == 0);
	}
	put(&fav[0], "Old", "old.zip"); put(&fav[1], "Set", "Set/Set.m3u"); put(&fav[2], "New", "new.zip");
	owners[0] = 1; owners[1] = 1; owners[2] = 2;
	for (int i = 0; i < 3; i++) fav[i].added = LIB_ADDED_UNKNOWN;
	put(&snes[0], "Gone", "gone.zip"); snes[0].added = LIB_ADDED_UNKNOWN;
	a.view[0].sort = 3; a.view[2].sort = 3;
	sort_shelf(&a, 0); sort_shelf(&a, 2);
	CHECK(!strcmp(fav[0].file, "new.zip") && fav[0].added == 3000 && owners[0] == 2);
	CHECK(!strcmp(fav[1].file, "Set/Set.m3u") && fav[1].added == 2000 && owners[1] == 1);
	CHECK(!strcmp(fav[2].file, "old.zip") && fav[2].added == 1000 && owners[2] == 1);
	CHECK(snes[0].added == 0);
	/* An order that does not read it leaves it unknown, and reads nothing */
	put(&nes[0], "B", "old.zip"); put(&nes[1], "A", "same.zip");
	nes[0].added = nes[1].added = LIB_ADDED_UNKNOWN; a.view[1].sort = 0;
	sort_shelf(&a, 1);
	CHECK(!strcmp(nes[0].name, "A") && nes[0].added == LIB_ADDED_UNKNOWN && nes[1].added == LIB_ADDED_UNKNOWN);
	{
		const char *gone[] = { "NES/old.zip", "SNES/new.zip", "NES/Set/Set.m3u" };
		char path[512];
		for (int i = 0; i < 3; i++) { snprintf(path, sizeof path, "%s/%s", roms, gone[i]); unlink(path); }
		snprintf(path, sizeof path, "%s/NES/Set", roms); rmdir(path);
		snprintf(path, sizeof path, "%s/NES", roms); rmdir(path);
		snprintf(path, sizeof path, "%s/SNES", roms); rmdir(path);
		rmdir(roms);
	}
	db_shutdown();
	if (failures) return 1;
	puts("ok: scrape exits refresh titles, textures, sorting and selection without reboot");
	return 0;
}
