/* SPDX-License-Identifier: GPL-3.0-only */
#include "shelfbuild.h"

#include "bgm.h"
#include "collect.h"
#include "db.h"
#include "esmedia.h"
#include "favorites.h"
#include "gamedel.h"
#include "homesys.h"
#include "mp4c.h"
#include "pico8.h"
#include "pminstall.h"
#include "savedlib.h"
#include "shelfdir.h"
#include "shots_screen.h"
#include "shotname.h"
#include "sort.h"
#include "stats.h"
#include "texload.h"
#include "theme.h"
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Drop systems with nothing in them.
 *
 * A card is a promise that opening it leads somewhere, and nine cards where
 * three have games is eight swipes to find the one you wanted. systems.cfg
 * stays the full list of what ZyperOS knows how to run; this is only what is
 * worth showing today.
 *
 * Compacted in place rather than filtered at draw time because sys_cursor
 * indexes sys.systems[] directly in a dozen places - the tint, the card art,
 * the launch, the resume - and an index layer over all of them would be a lot
 * of surface for a cosmetic rule. Both arrays move together or the shelf
 * shows one system's card over another's games.
 *
 * Textures are not freed here: this runs before prime_home, so there
 * are none yet. */
/* The systems with games as the last scan found them, shown or not, in the
 * home's order: MENU > Display > Home Systems's rows (shelf_have_list) */
static shelf_have g_have[CFG_MAX_SYSTEMS];
static int g_nhave;

int shelf_have_list(const shelf_have **out)
{
	if (out) *out = g_have;
	return g_nhave;
}

/* A shelf's listing let go, as it leaves the home */
static void view_free(sysview *v)
{
	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	free(v->album);
	lib_free(&v->list);
	shelf_dirs_free(v);
	memset(v, 0, sizeof *v);
}

/* And a system with games turned off there (src/homesys.h) leaves the home
 * the same way, but its listing is kept, after the home's shelves (app.parked,
 * sys_kept): its games stay on Favorites and Recents, start from there and
 * resume at boot on them, as Davistoa asked to hide every system and live in
 * Favorites (my OK, 2026-11-19). Random Game and Search are the home's and
 * leave them out.
 *
 * A system whose cores need GPU frames on a handheld without them
 * (config/systems.cfg's needs lines, plat_gpu_frames: the Brick today) leaves
 * the home with its listing let go, and it is no row of Home Systems either,
 * as it could not be turned on. Its games leave Favorites, Recents and the
 * resume at boot with it, so none reaches a core that cannot run it. */
static void hide_empty_systems(app *a)
{
	system_cfg *off_sys = NULL;
	sysview *off_view = NULL;
	int i, n = 0, off = 0, kept = 0, gpu = 0;

	g_nhave = 0;
	a->parked = 0;
	for (i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];

		if (v->list.count <= 0) continue;
		if (a->sys.systems[i].needs_gpu_frames && !plat_gpu_frames()) {
			view_free(v);
			gpu++;
			continue;
		}
		if (g_nhave < CFG_MAX_SYSTEMS) {
			snprintf(g_have[g_nhave].tag, sizeof g_have[0].tag, "%s", a->sys.systems[i].tag);
			snprintf(g_have[g_nhave].name, sizeof g_have[0].name, "%s", a->sys.systems[i].name);
			g_nhave++;
		}
		if (!homesys_shown(a->sys.systems[i].tag)) {
			/* Set aside, and put after the home's shelves below: the home's
			 * are compacted over this slot */
			if (!off_sys) {
				off_sys = malloc((size_t)a->sys.count * sizeof *off_sys);
				off_view = malloc((size_t)a->sys.count * sizeof *off_view);
			}
			if (off_sys && off_view) {
				off_sys[kept] = a->sys.systems[i];
				off_view[kept++] = *v;
				memset(v, 0, sizeof *v);
			} else {
				view_free(v);   /* no memory: off the home and the groups, as before */
			}
			off++;
			continue;
		}
		if (n != i) {
			a->sys.systems[n] = a->sys.systems[i];
			a->view[n] = a->view[i];
			memset(&a->view[i], 0, sizeof a->view[i]);
		}
		n++;
	}
	if (n + off + gpu != a->sys.count)
		fprintf(stderr, "scan: %d of %d systems have games\n", n + off + gpu, a->sys.count);
	if (off)
		fprintf(stderr, "scan: %d of them off the home (MENU > Home Systems), "
		        "%d kept for Favorites and Recents\n", off, kept);
	if (gpu)
		fprintf(stderr, "scan: %d of them need GPU frames, which this handheld has not\n", gpu);
	a->sys.count = n;
	for (i = 0; i < kept; i++) {
		a->sys.systems[n + i] = off_sys[i];
		a->view[n + i] = off_view[i];
	}
	a->parked = kept;
	free(off_sys);
	free(off_view);
	if (a->sys_cursor >= n) a->sys_cursor = n ? n - 1 : 0;
}

/* ---- folders on a shelf (src/shelfdir.h) ---- */

void shelf_dirs_init(sysview *v)
{
	game_list *all;
	int nf;

	v->dir[0] = '\0';
	v->nfold = 0;
	if (v->all || !sd_any(&v->list) || !(all = malloc(sizeof *all))) return;
	*all = v->list;
	memset(&v->list, 0, sizeof v->list);
	if ((nf = sd_build(all, "", &v->list)) < 0) {
		/* No memory for the top's folders: every game, as it always was */
		lib_free(&v->list);
		v->list = *all;
		free(all);
		return;
	}
	v->all = all;
	v->nfold = nf;
}

void shelf_dirs_free(sysview *v)
{
	if (v->all) {
		lib_free(v->all);
		free(v->all);
		v->all = NULL;
	}
	v->nfold = 0;
	v->dir[0] = '\0';
}

bool shelf_show_dir(sysview *v, const char *dir)
{
	game_list shown = { 0 };
	char want[LIB_PATH];
	int nf, i;

	if (!v->all) return false;
	snprintf(want, sizeof want, "%s", dir ? dir : "");
	/* Up to the nearest folder that still holds a game: one emptied under
	 * it (a delete, a transfer) is no folder any more */
	for (;;) {
		if ((nf = sd_build(v->all, want, &shown)) < 0) return false;
		if (shown.count > 0 || !want[0]) break;
		sd_parent(want, want, sizeof want);
	}
	for (i = 0; v->tex && i < v->list.count; i++)
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
	lib_free(&v->list);
	v->list = shown;
	v->nfold = nf;
	snprintf(v->dir, sizeof v->dir, "%s", want);
	if (v->list.count > 0) {
		v->tex = calloc((size_t)v->list.count, sizeof *v->tex);
		v->tw = calloc((size_t)v->list.count, sizeof *v->tw);
		v->th = calloc((size_t)v->list.count, sizeof *v->th);
		v->cb = calloc((size_t)v->list.count, sizeof *v->cb);
		if (!v->tex || !v->tw || !v->th || !v->cb) {
			free(v->tex); free(v->tw); free(v->th); free(v->cb);
			v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
			lib_free(&v->list);
			v->nfold = 0;
			return false;
		}
	}
	if (v->cursor >= v->list.count) v->cursor = v->list.count > 0 ? v->list.count - 1 : 0;
	return true;
}

/* ---- the groups: Favorites and Recents ----------------------------------
 *
 * Shelves of games from every other shelf, at the front of the home:
 * Favorites first, then Recents. Each holds copies of its games and an owner
 * map, the index of each game's own system, which moves when a shelf is put
 * in or taken out in front of it: shelf_insert and shelf_remove move every
 * group's owners with the shelves. */

/* The group with `tag`, by its tag, or -1 */
static int group_at(const app *a, const char *tag)
{
	for (int i = 0; i < a->sys.count; i++)
		if (a->view[i].owner && !strcmp(a->sys.systems[i].tag, tag)) return i;
	return -1;
}

/* A shelf put in at `at`, empty: those from there on up one, the systems off
 * the home after them too (sys_kept), and every group's owners that named
 * them with them */
static void shelf_insert(app *a, int at)
{
	int i, k;

	for (i = sys_kept(a); i > at; i--) {
		a->sys.systems[i] = a->sys.systems[i - 1];
		a->view[i] = a->view[i - 1];
	}
	a->sys.count++;
	memset(&a->sys.systems[at], 0, sizeof a->sys.systems[0]);
	memset(&a->view[at], 0, sizeof a->view[0]);
	for (i = 0; i < a->sys.count; i++)
		for (k = 0; i != at && a->view[i].owner && k < a->view[i].list.count; k++)
			if (a->view[i].owner[k] >= at) a->view[i].owner[k]++;
}

/* The shelf at `at` taken out, its memory already let go: the rest down one,
 * the systems off the home with them, every group's owners with them */
static void shelf_remove(app *a, int at)
{
	int i, k, end = sys_kept(a);

	for (i = at; i + 1 < end; i++) {
		a->sys.systems[i] = a->sys.systems[i + 1];
		a->view[i] = a->view[i + 1];
	}
	a->sys.count--;
	memset(&a->sys.systems[end - 1], 0, sizeof a->sys.systems[0]);
	memset(&a->view[end - 1], 0, sizeof a->view[0]);
	for (i = 0; i < a->sys.count; i++)
		for (k = 0; a->view[i].owner && k < a->view[i].list.count; k++)
			if (a->view[i].owner[k] > at) a->view[i].owner[k]--;
}

/* A group's shelf filled in at `at` (shelf_insert made room): its name, tag,
 * card and lime, its games and their owners, and the arrays every shelf
 * has. False with no memory, the shelf taken out again and nothing kept. */
static bool group_fill(app *a, int at, const char *name, const char *tag, const char *card,
                       game_entry *items, int *owner, int n)
{
	sysview *v = &a->view[at];

	snprintf(a->sys.systems[at].name, CFG_STR, "%s", name);
	snprintf(a->sys.systems[at].tag, sizeof a->sys.systems[at].tag, "%s", tag);
	snprintf(a->sys.systems[at].card, CFG_STR, "%s", card);
	/* ZyperOS's, not a console's: lime, as MP4 is (see ZYPEROS_ACCENT) */
	a->sys.systems[at].accent = ZYPEROS_ACCENT;
	v->list.items = items;
	v->list.count = n;
	v->list.scanned = true;
	v->owner = owner;
	/* Its View, which it keeps as a console's shelf does (MENU > View) */
	v->listed = view_listed(tag);
	v->grid = !v->listed && view_grid(tag);
	v->video = !v->listed && !v->grid && view_video(tag);
	v->tex = calloc((size_t)n, sizeof *v->tex);
	v->tw  = calloc((size_t)n, sizeof *v->tw);
	v->th  = calloc((size_t)n, sizeof *v->th);
	/* cb with the rest. The scan allocates these four together and this path
	 * allocated three, so Favorites had a NULL where every other shelf had an
	 * array - and game_get_tex writes to it on the first card it draws. */
	v->cb  = calloc((size_t)n, sizeof *v->cb);
	if (v->tex && v->tw && v->th && v->cb) return true;
	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	v->owner = NULL;
	v->list.count = 0;
	shelf_remove(a, at);
	free(items); free(owner);
	return false;
}

/* A group's shelf taken off: its game list and owner map are this file's
 * own allocations rather than lib_scan's, and the game_entry values in it are
 * copies - the originals belong to the shelves they came from and must not
 * be touched. */
static void group_drop(app *a, const char *tag)
{
	int at = group_at(a, tag), i;
	sysview *v;

	if (at < 0) return;
	v = &a->view[at];
	for (i = 0; i < v->list.count; i++)
		if (v->tex && v->tex[i]) SDL_DestroyTexture(v->tex[i]);
	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	free(v->list.items);
	free(v->owner);
	v->owner = NULL;
	v->list.count = 0;
	shelf_remove(a, at);
}

/* Where a group's cursor and order were, by game rather than index, to be put
 * back once it is built again (group_note, group_put_back) */
typedef struct {
	bool had;
	int  sort, cursor;
	char tag[16], file[LIB_PATH];
} group_keep;

static void group_note(const app *a, const char *gtag, group_keep *k)
{
	int at = group_at(a, gtag);
	const sysview *v;

	memset(k, 0, sizeof *k);
	if (at < 0) return;
	v = &a->view[at];
	k->had = true;
	k->sort = v->sort;
	k->cursor = v->cursor;
	if (v->cursor >= 0 && v->cursor < v->list.count) {
		snprintf(k->tag, sizeof k->tag, "%s", a->sys.systems[v->owner[v->cursor]].tag);
		snprintf(k->file, sizeof k->file, "%s", v->list.items[v->cursor].file);
	}
}

/* Its order put back and the shelf sorted in it, and its cursor on the game
 * it was on, or where it was when that game is gone */
static void group_put_back(app *a, const char *gtag, const group_keep *k)
{
	int at = group_at(a, gtag), c = -1;
	sysview *v;

	if (at < 0) return;
	v = &a->view[at];
	if (k->had) v->sort = k->sort;
	sort_shelf(a, at);
	for (int i = 0; k->file[0] && i < v->list.count; i++)
		if (!strcmp(v->list.items[i].file, k->file) &&
		    !strcmp(a->sys.systems[v->owner[i]].tag, k->tag))
			c = i;
	if (c < 0 && k->had) c = k->cursor < v->list.count ? k->cursor : v->list.count - 1;
	v->cursor = c > 0 ? c : 0;
	cf_reset(&v->cf, v->cursor);
}

/* Build the Favorites shelf: one shelf whose games come from every other.
 *
 * Resolved against the systems that actually scanned, on the home or turned
 * off it (app.parked), so a favorite whose ROM is off the card simply does not
 * appear - which is the same rule as hiding an empty system, and better than a
 * card that opens onto a game that is not there. Sorted by name like every
 * other shelf rather than by the order someone pressed Y, so it reads as a
 * shelf and not as a history.
 *
 * Inserted at the front, which shifts every system index up by one, so the
 * owners recorded during resolution are corrected afterwards. Runs before
 * prime_home, so there are no textures to move with them.
 *
 * It is NOT a system: no core, no folder, no extensions. Nothing may read
 * those from a->sys.systems[] for a game on this shelf - shelf_owner() is how
 * every one of them is found instead. */
static void build_favorites_shelf(app *a)
{
	game_entry *items;
	int *owner;
	int n = 0, i, k, si, real;

	if (fav_count() <= 0 || sys_kept(a) <= 0 || !homesys_shown(HOMESYS_FAV)) return;
	if (sys_kept(a) >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for a Favorites shelf\n");
		return;
	}

	items = calloc(FAV_MAX, sizeof *items);
	owner = calloc(FAV_MAX, sizeof *owner);
	if (!items || !owner) { free(items); free(owner); return; }

	real = sys_kept(a);                 /* the systems off the home too */
	for (i = 0; i < fav_count(); i++) {
		const char *tag, *file;
		bool found = false;
		if (!fav_at(i, &tag, &file)) continue;
		for (si = 0; si < real; si++) {
			if (strcmp(a->sys.systems[si].tag, tag)) continue;
			const game_list *gl = sys_games(&a->view[si]);

			for (k = 0; k < gl->count; k++) {
				if (strcmp(gl->items[k].file, file)) continue;
				items[n] = gl->items[k];
				owner[n] = si;
				n++;
				found = true;
				break;
			}
			break;
		}
		/* Said out loud. A favorite that does not resolve is either a ROM
		 * that has left the card or a key that never matched, and silently
		 * showing one fewer game than the file lists is the kind of thing
		 * that gets noticed months later. */
		if (!found) fprintf(stderr, "fav: unresolved %s\t%s\n", tag, file);
		if (n >= FAV_MAX) break;
	}
	if (n == 0) { free(items); free(owner); return; }

	/* Insertion sort on the shown title, carrying the owner with it. n is at
	 * most FAV_MAX and realistically a dozen. */
	for (i = 1; i < n; i++) {
		game_entry t = items[i];
		int to = owner[i], j = i - 1;
		while (j >= 0 && lib_name_cmp(&items[j], &t) > 0) {
			items[j + 1] = items[j];
			owner[j + 1] = owner[j];
			j--;
		}
		items[j + 1] = t;
		owner[j + 1] = to;
	}

	/* First on the home: every shelf moves up one, Recents with its owners */
	shelf_insert(a, 0);
	for (i = 0; i < n; i++) owner[i]++;
	if (!group_fill(a, 0, T(S_LIBRARY_FAVORITES), HOMESYS_FAV, "FAVORITES.png", items, owner, n))
		return;
	fprintf(stderr, "scan: %-16s %d game%s\n", "Favorites", n, n == 1 ? "" : "s");
}

/* Take the Favorites shelf back off (group_drop) */
static void drop_favorites_shelf(app *a)
{
	group_drop(a, HOMESYS_FAV);
}

/* Build the Recents shelf: the HOMESYS_RECENTS games played last, newest
 * first, from every system with games, on the home or turned off it (MENU >
 * Display > Home Systems; my picks, 2026-11-15 and 2026-11-19). After
 * Favorites, first when there is none. From the sessions the play time keeps
 * (stats_recent), resolved against the systems' listings as favorites are: a
 * game off the card is not on it, and the next one is. No game played yet,
 * no shelf: a card should lead somewhere, as an empty Favorites does not
 * show.
 *
 * Its order is Last Played as it ships (src/sort.h), which keeps it newest
 * first as games are played; MENU > Sort By sorts it otherwise, as it does
 * Favorites, and is kept by its tag. Built again when a game ends
 * (refresh_recents_shelf), so the game just played is first. */
static void build_recents_shelf(app *a)
{
	enum { LOOK = HOMESYS_RECENTS * 6 };      /* rows to resolve the ten from */
	stats_recent_row *rows;
	game_entry *items;
	int *owner;
	int n = 0, i, k, si, nr, real, at;

	if (sys_kept(a) <= 0 || !homesys_shown(HOMESYS_RECENT)) return;
	if (sys_kept(a) >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for a Recents shelf\n");
		return;
	}
	rows = calloc(LOOK, sizeof *rows);
	items = calloc(HOMESYS_RECENTS, sizeof *items);
	owner = calloc(HOMESYS_RECENTS, sizeof *owner);
	if (!rows || !items || !owner) { free(rows); free(items); free(owner); return; }

	real = sys_kept(a);                 /* the systems off the home too */
	nr = stats_recent(rows, LOOK);
	for (i = 0; i < nr && n < HOMESYS_RECENTS; i++)
		for (si = 0; si < real; si++) {
			const game_list *gl;

			if (a->view[si].owner || strcmp(a->sys.systems[si].tag, rows[i].tag)) continue;
			gl = sys_games(&a->view[si]);
			for (k = 0; k < gl->count; k++)
				if (!strcmp(gl->items[k].file, rows[i].file)) {
					items[n] = gl->items[k];
					items[n].last_played = rows[i].last;
					owner[n++] = si;
					break;
				}
			break;
		}
	free(rows);
	if (n == 0) { free(items); free(owner); return; }

	at = group_at(a, HOMESYS_FAV) + 1;           /* after Favorites, or first */
	shelf_insert(a, at);
	for (i = 0; i < n; i++)
		if (owner[i] >= at) owner[i]++;
	if (!group_fill(a, at, T(S_LIBRARY_RECENTS), HOMESYS_RECENT, "RECENTS.png", items, owner, n))
		return;
	a->view[at].sort = sort_index("recent");
	fprintf(stderr, "scan: %-16s %d game%s\n", "Recents", n, n == 1 ? "" : "s");
}

static void drop_recents_shelf(app *a)
{
	group_drop(a, HOMESYS_RECENT);
}

/* ---- Collections (src/collect.h, DESIGN.md 11.56) -----------------------
 *
 * A group like Favorites and Recents, after them, once a collection has a
 * game on the shelves. Its shelf is one folder deep. At its top, a folder
 * card for each collection with a game on the shelves, A to Z: the first
 * v->nfold entries, each one's `file`, `name` and `title` the collection's
 * name and `folder` how many of its games are here, owned by the group
 * itself, so its card wears the group's lime (and Folder Color) as a
 * system's folder wears its system's. In one (v->dir its name), its games,
 * each owned by its own system as on Favorites, in the shelf's Sort By; a
 * game's `added` is its line, for Added. A game off the card is not in it,
 * and its line stays in the list. A game of a system turned off the home
 * stays, as it does on Favorites and Recents (app.parked). */

/* The game a collection's line names, on a system's shelf, on the home or
 * turned off it (sys_kept): its shelf and its place in every game of it
 * (sys_games). Never a group's, never MP4's. */
static bool coll_resolve(const app *a, const char *folder, const char *file, int *si, int *k)
{
	for (int i = 0; i < sys_kept(a); i++) {
		const game_list *gl;

		if (a->view[i].owner || is_mp4(&a->sys.systems[i]) ||
		    strcasecmp(a->sys.systems[i].folder, folder))
			continue;
		gl = sys_games_c(&a->view[i]);
		for (int g = 0; g < gl->count; g++)
			if (!gl->items[g].folder && !strcmp(gl->items[g].file, file)) {
				*si = i;
				*k = g;
				return true;
			}
	}
	return false;
}

/* Collection c's games on the shelves, into items and owner (up to max), in
 * its list's order, each once; how many */
static int coll_gather(const app *a, int c, game_entry *items, int *owner, int max)
{
	char folder[COLL_LINE], file[COLL_LINE];
	int n = 0, si, k, j;

	for (int l = 0; l < coll_lines(c) && (!items || n < max); l++) {
		if (!coll_game(c, l, folder, sizeof folder, file, sizeof file) ||
		    !coll_resolve(a, folder, file, &si, &k))
			continue;
		if (!items) { n++; continue; }
		for (j = 0; j < n; j++)
			if (owner[j] == si && !strcmp(items[j].file, file)) break;
		if (j < n) continue;                       /* a list naming it twice */
		items[n] = sys_games_c(&a->view[si])->items[k];
		items[n].added = l;
		owner[n++] = si;
	}
	return n;
}

int shelf_coll_at(const app *a)
{
	return group_at(a, HOMESYS_COLL);
}

int shelf_recents_at(const app *a)
{
	return group_at(a, HOMESYS_RECENT);
}

/* How many games the collections hold on the shelves, each once however
 * many lists name it: the home's count under Collections */
static int g_coll_games;

int shelf_coll_games(void)
{
	return g_coll_games;
}

static int coll_count_games(const app *a)
{
	char folder[COLL_LINE], file[COLL_LINE];
	int at[CFG_MAX_SYSTEMS + 1], total = 0, n = 0, si, k;
	unsigned char *seen;

	for (si = 0; si < sys_kept(a) && si < CFG_MAX_SYSTEMS; si++) {   /* off the home too */
		at[si] = total;
		total += sys_games_c(&a->view[si])->count;
	}
	if (!(seen = calloc((size_t)(total ? total : 1), 1))) return 0;
	for (int c = 0; c < coll_count(); c++)
		for (int l = 0; l < coll_lines(c); l++)
			if (coll_game(c, l, folder, sizeof folder, file, sizeof file) &&
			    coll_resolve(a, folder, file, &si, &k) && si < CFG_MAX_SYSTEMS && !seen[at[si] + k]) {
				seen[at[si] + k] = 1;
				n++;
			}
	free(seen);
	return n;
}

/* The top's entries, a folder card a collection with a game here, into a
 * new list; how many, or -1 with no memory */
static int coll_top(const app *a, int self, game_entry **items_out, int **owner_out)
{
	game_entry *items = calloc(COLL_MAX, sizeof *items);
	int *owner = calloc(COLL_MAX, sizeof *owner), n = 0;

	if (!items || !owner) { free(items); free(owner); return -1; }
	for (int c = 0; c < coll_count(); c++) {
		int games = coll_gather(a, c, NULL, NULL, COLL_LINES);

		if (!games) continue;
		snprintf(items[n].name, sizeof items[n].name, "%s", coll_name(c));
		snprintf(items[n].title, sizeof items[n].title, "%s", coll_name(c));
		snprintf(items[n].file, sizeof items[n].file, "%s", coll_name(c));
		items[n].folder = games;
		items[n].added = LIB_ADDED_UNKNOWN;
		owner[n++] = self;
	}
	*items_out = items;
	*owner_out = owner;
	return n;
}

/* Build the Collections shelf, at its top: after Favorites and Recents,
 * first when there are neither; none with no collection that has a game on
 * the shelves, as an empty Favorites shows none */
static void build_collections_shelf(app *a)
{
	game_entry *items;
	int *owner, n, at, fav, rec;

	if (coll_count() <= 0 || sys_kept(a) <= 0 || !homesys_shown(HOMESYS_COLL)) return;
	if (sys_kept(a) >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for a Collections shelf\n");
		return;
	}
	fav = group_at(a, HOMESYS_FAV);
	rec = group_at(a, HOMESYS_RECENT);
	at = (fav > rec ? fav : rec) + 1;
	if ((n = coll_top(a, at, &items, &owner)) <= 0) {
		if (n == 0) { free(items); free(owner); }
		fprintf(stderr, "scan: %-16s none with a game on the shelves (%d list%s)\n", "Collections",
		        coll_count(), coll_count() == 1 ? "" : "s");
		return;
	}
	shelf_insert(a, at);
	if (!group_fill(a, at, T(S_LIBRARY_COLLECTIONS), HOMESYS_COLL, "COLLECTIONS.png", items, owner, n))
		return;
	a->view[at].nfold = n;
	a->view[at].sort = SORT_LIST;
	g_coll_games = coll_count_games(a);
	fprintf(stderr, "scan: %-16s %d collection%s, %d game%s\n", "Collections", n, n == 1 ? "" : "s",
	        g_coll_games, g_coll_games == 1 ? "" : "s");
}

static void drop_collections_shelf(app *a)
{
	int at = group_at(a, HOMESYS_COLL);

	if (at >= 0) {
		a->view[at].nfold = 0;
		a->view[at].dir[0] = '\0';
	}
	group_drop(a, HOMESYS_COLL);
}

bool shelf_coll_show(app *a, int s, const char *dir)
{
	sysview *v = &a->view[s];
	game_entry *items = NULL;
	int *owner = NULL, n = -1, c = dir && dir[0] ? coll_find(dir) : -1, i;
	bool inside;

	if (s != group_at(a, HOMESYS_COLL)) return false;
	if (c >= 0) {
		int want = coll_gather(a, c, NULL, NULL, COLL_LINES);

		if (want > 0 && (items = calloc((size_t)want, sizeof *items)) &&
		    (owner = calloc((size_t)want, sizeof *owner)))
			n = coll_gather(a, c, items, owner, want);
		else { free(items); free(owner); items = NULL; owner = NULL; }
	}
	/* Its top: asked for, or the collection went (deleted, emptied) */
	inside = n > 0;
	if (!inside && (n = coll_top(a, s, &items, &owner)) < 0) return false;
	for (i = 0; v->tex && i < v->list.count; i++)
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	free(v->list.items); free(v->owner);
	v->tex = calloc((size_t)(n ? n : 1), sizeof *v->tex);
	v->tw = calloc((size_t)(n ? n : 1), sizeof *v->tw);
	v->th = calloc((size_t)(n ? n : 1), sizeof *v->th);
	v->cb = calloc((size_t)(n ? n : 1), sizeof *v->cb);
	v->list.items = items;
	v->list.count = n;
	v->owner = owner;
	if (!v->tex || !v->tw || !v->th || !v->cb) {
		free(v->tex); free(v->tw); free(v->th); free(v->cb);
		v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
		v->list.count = 0;
		return false;
	}
	if (inside) {
		snprintf(v->dir, sizeof v->dir, "%s", coll_name(c));
		v->nfold = 0;
	} else {
		v->dir[0] = '\0';
		v->nfold = n;
	}
	if (v->cursor >= v->list.count) v->cursor = v->list.count > 0 ? v->list.count - 1 : 0;
	return true;
}

/* Where the Collections shelf was, to be put back once it is built again:
 * its order, the collection it was in, and the entry it was on by name
 * (and system), or its place */
typedef struct {
	bool had;
	int sort, cursor;
	char dir[COLL_NAME], tag[16], file[LIB_PATH];
} coll_keep;

static void coll_note(const app *a, coll_keep *k)
{
	int at = group_at(a, HOMESYS_COLL);
	const sysview *v;

	memset(k, 0, sizeof *k);
	if (at < 0) return;
	v = &a->view[at];
	k->had = true;
	k->sort = v->sort;
	k->cursor = v->cursor;
	snprintf(k->dir, sizeof k->dir, "%s", v->dir);
	if (v->cursor >= 0 && v->cursor < v->list.count) {
		snprintf(k->tag, sizeof k->tag, "%s", a->sys.systems[v->owner[v->cursor]].tag);
		snprintf(k->file, sizeof k->file, "%s", v->list.items[v->cursor].file);
	}
}

static void coll_put_back(app *a, const coll_keep *k)
{
	int at = group_at(a, HOMESYS_COLL), c = -1;
	sysview *v;

	if (at < 0) return;
	v = &a->view[at];
	if (k->had) v->sort = k->sort;
	if (k->dir[0]) shelf_coll_show(a, at, k->dir);
	sort_shelf(a, at);
	for (int i = 0; k->file[0] && i < v->list.count; i++)
		if (!strcmp(v->list.items[i].file, k->file) &&
		    (v->list.items[i].folder || !strcmp(a->sys.systems[v->owner[i]].tag, k->tag)))
			c = i;
	if (c < 0 && k->had) c = k->cursor < v->list.count ? k->cursor : v->list.count - 1;
	v->cursor = c > 0 ? c : 0;
	cf_reset(&v->cf, v->cursor);
}

void refresh_collections_shelf(app *a)
{
	char tag[sizeof a->sys.systems[0].tag];
	coll_keep k;
	int i;

	if (a->sys.count <= 0) return;
	texload_bump();
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	coll_note(a, &k);
	drop_collections_shelf(a);
	build_collections_shelf(a);
	coll_put_back(a, &k);
	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, tag)) break;
	if (i < a->sys.count) {
		a->sys_cursor = i;
	} else {
		/* Stood on Collections, and it went: its last game left it */
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	if (a->sys.count > 0) a->tint = a->sys.systems[a->sys_cursor].accent;
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* ---- Screenshots (src/shotname.h, DESIGN.md 11.68) ----------------------
 *
 * A group like Collections, after it (first when there are none of the
 * three), once the card has a shot: the Screenshot hotkey's pictures,
 * Screenshots/<System>/<Game> 2026-11-18 21.40.05.png, and OpenBOR's own,
 * Saves/openbor/ScreenShots/<module> - 0000.png, one entry a picture. An
 * entry is its game's name and title, owned by its system as on Favorites,
 * so the card has the game's cover and the facts its system; its `file` is
 * the picture's own path from the card's root, and its `added` the time it
 * was taken (src/shotname.h's seconds), the newest-first order. A shot whose
 * game is not on the card any more (deleted, renamed: a picture is the
 * player's, and Delete Game leaves it) stays, under the name it was taken
 * with.
 *
 * Read once a scan and again after a game that took a shot
 * (refresh_shots_shelf): a folder read of Screenshots/ and of each system's
 * folder in it, and OpenBOR's, names only (OpenBOR's time is its file's);
 * the games found by name against the listings in memory, a sort of each
 * folder's names and a look-up a game. The systems turned off the home too
 * (app.parked), as Favorites. */
#define SHOTS_MAX 2000          /* the newest on the shelf; a game_entry each */
#define SHOTS_READ_MAX 8000     /* names read at most, newest kept */
#define SHOTS_OPENBOR "OPENBOR" /* the OpenBOR shelf's tag (config/systems.cfg) */

typedef struct {
	char file[256];             /* its name in its folder */
	unsigned short glen;        /* the game's part, the name's first glen bytes */
	bool openbor;               /* in OpenBOR's folder, not Screenshots/ */
	long when;
	int owner, at;              /* its system, and its game in sys_games or -1 */
} shot_found;

/* Whether the card had a shot at the last scan, on the shelf or not: Home
 * Systems' row for it (src/homesys_screen.c) */
static bool g_shots_have;

bool shelf_shots_have(void)
{
	return g_shots_have;
}

int shelf_shots_at(const app *a)
{
	return group_at(a, HOMESYS_SHOT);
}

static int shot_game_cmp(const char *a, size_t na, const char *b, size_t nb)
{
	int c = memcmp(a, b, na < nb ? na : nb);

	return c ? c : na < nb ? -1 : na > nb;
}

static int shot_by_game(const void *x, const void *y)
{
	const shot_found *a = x, *b = y;

	return shot_game_cmp(a->file, a->glen, b->file, b->glen);
}

static int shot_by_when(const void *x, const void *y)
{
	const shot_found *a = x, *b = y;

	return a->when != b->when ? (a->when > b->when ? -1 : 1) : strcmp(a->file, b->file);
}

/* The shots of one folder (`dir`, `openbor` its kind) of system `si` added
 * to f[*n..]; false when the read should stop (`any` and one found) */
static bool shots_read_dir(const char *dir, bool openbor, int si, shot_found **f, int *n, int *cap, bool any)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) return true;
	while ((e = readdir(d)) && *n < SHOTS_READ_MAX) {
		char game[LIB_NAME], path[CFG_STR * 4 + 256];
		long when;
		size_t len = strlen(e->d_name);

		if (e->d_name[0] == '.' || len >= sizeof (*f)->file) continue;
		if (openbor) {
			/* No time in OpenBOR's name: its file's, as its wall clock */
			struct stat st;
			struct tm tm;
			time_t t;

			if (!shot_parse_openbor(e->d_name, game, sizeof game, NULL)) continue;
			snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
			if (stat(path, &st) != 0) continue;
			t = st.st_mtime;
			localtime_r(&t, &tm);
			when = shot_secs(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			if (when < 0) when = 0;
		} else if (!shot_parse(e->d_name, game, sizeof game, &when)) {
			continue;
		}
		if (*n == *cap) {
			shot_found *g = realloc(*f, (size_t)(*cap ? *cap * 2 : 64) * sizeof **f);

			if (!g) break;
			*f = g;
			*cap = *cap ? *cap * 2 : 64;
		}
		memcpy((*f)[*n].file, e->d_name, len + 1);
		(*f)[*n].glen = (unsigned short)strlen(game);
		(*f)[*n].openbor = openbor;
		(*f)[*n].when = when;
		(*f)[*n].owner = si;
		(*f)[*n].at = -1;
		(*n)++;
		if (any) break;
	}
	closedir(d);
	return !(any && *n);
}

/* Every shot on the card of a system with games, into *out (malloc'd, the
 * caller's), its game found where it is on the card; how many. `any`: stop
 * at the first, for whether there is one. */
static int shots_read(const app *a, shot_found **out, bool any)
{
	char dir[CFG_STR * 4];
	shot_found *f = NULL;
	int n = 0, cap = 0, si;

	*out = NULL;
	for (si = 0; si < sys_kept(a); si++) {
		const system_cfg *sc = &a->sys.systems[si];
		const game_list *gl;
		int from = n, k;

		/* Never a group's, never MP4's, and a folder read once when two
		 * systems share it */
		if (a->view[si].owner || is_mp4(sc) || !sc->folder[0]) continue;
		for (k = 0; k < si; k++)
			if (!a->view[k].owner && !strcmp(a->sys.systems[k].folder, sc->folder)) break;
		if (k < si) continue;
		snprintf(dir, sizeof dir, "%s/%s/%s", P_CARD, SHOT_DIR, sc->folder);
		if (!shots_read_dir(dir, false, si, &f, &n, &cap, any)) break;
		/* OpenBOR's own, under OpenBOR (my pick in the Screenshots lab) */
		if (!strcmp(sc->tag, SHOTS_OPENBOR)) {
			snprintf(dir, sizeof dir, "%s/%s", P_CARD, SHOT_OPENBOR_DIR);
			if (!shots_read_dir(dir, true, si, &f, &n, &cap, any)) break;
		}
		if (n == from) continue;
		/* Its games by the name the shot was taken with: the hotkey's
		 * shot_game_part, OpenBOR's module (its .pak's name) */
		qsort(f + from, (size_t)(n - from), sizeof *f, shot_by_game);
		gl = sys_games_c(&a->view[si]);
		for (k = 0; k < gl->count; k++) {
			shot_found key, *hit;

			if (gl->items[k].folder) continue;
			shot_game_part(gl->items[k].name, key.file, sizeof key.file);
			key.glen = (unsigned short)strlen(key.file);
			if (!(hit = bsearch(&key, f + from, (size_t)(n - from), sizeof *f, shot_by_game))) continue;
			while (hit > f + from && !shot_by_game(hit - 1, &key)) hit--;
			for (; hit < f + n && !shot_by_game(hit, &key); hit++)
				if (hit->at < 0) hit->at = k;
		}
	}
	*out = f;
	return n;
}

/* Build the Screenshots shelf: after Favorites, Recents and Collections,
 * first when there are none of them; none with no shot on the card, as an
 * empty Favorites shows none. Recently Added as it ships, the newest
 * first. */
static void build_shots_shelf(app *a)
{
	shot_found *f;
	game_entry *items;
	int *owner, n, i, at, fav, rec, col;

	g_shots_have = false;
	if (sys_kept(a) <= 0) return;
	if (!homesys_shown(HOMESYS_SHOT)) {
		/* Off the home: whether there is one, for its row there */
		g_shots_have = shots_read(a, &f, true) > 0;
		free(f);
		return;
	}
	if (sys_kept(a) >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for a Screenshots shelf\n");
		return;
	}
	if ((n = shots_read(a, &f, false)) <= 0) { free(f); return; }
	g_shots_have = true;
	qsort(f, (size_t)n, sizeof *f, shot_by_when);
	if (n > SHOTS_MAX) n = SHOTS_MAX;
	items = calloc((size_t)n, sizeof *items);
	owner = calloc((size_t)n, sizeof *owner);
	if (!items || !owner) { free(items); free(owner); free(f); return; }
	for (i = 0; i < n; i++) {
		const system_cfg *sc = &a->sys.systems[f[i].owner];
		game_entry *e = &items[i];

		if (f[i].at >= 0) {
			const game_entry *g = &sys_games(&a->view[f[i].owner])->items[f[i].at];

			snprintf(e->name, sizeof e->name, "%s", g->name);
			snprintf(e->title, sizeof e->title, "%s", g->title);
		} else {
			/* No game of that name on the card: the picture alone */
			snprintf(e->name, sizeof e->name, "%.*s", (int)f[i].glen, f[i].file);
			lib_display_title(db_lib(), sc->folder, "", e->name, e->title, sizeof e->title);
		}
		if (f[i].openbor) snprintf(e->file, sizeof e->file, "%s/%s", SHOT_OPENBOR_DIR, f[i].file);
		else snprintf(e->file, sizeof e->file, "%s/%s/%s", SHOT_DIR, sc->folder, f[i].file);
		e->added = f[i].when;
		owner[i] = f[i].owner;
	}
	free(f);
	fav = group_at(a, HOMESYS_FAV);
	rec = group_at(a, HOMESYS_RECENT);
	col = group_at(a, HOMESYS_COLL);
	at = fav > rec ? fav : rec;
	at = (at > col ? at : col) + 1;
	shelf_insert(a, at);
	for (i = 0; i < n; i++)
		if (owner[i] >= at) owner[i]++;
	if (!group_fill(a, at, T(S_LIBRARY_SCREENSHOTS), HOMESYS_SHOT, "SCREENSHOTS.png", items, owner, n))
		return;
	a->view[at].sort = sort_index("added");
	fprintf(stderr, "scan: %-16s %d screenshot%s\n", "Screenshots", n, n == 1 ? "" : "s");
}

static void drop_shots_shelf(app *a)
{
	shots_view_drop();
	group_drop(a, HOMESYS_SHOT);
}

/* Where the Screenshots shelf was, to be put back once it is built again:
 * its order, its View as the player left it (the viewer A opens included),
 * and the shot it was on, by its file, or its place */
typedef struct {
	bool had, listed, grid, video;
	int sort, cursor;
	char file[LIB_PATH];
} shots_keep;

static void shots_note(const app *a, shots_keep *k)
{
	int at = group_at(a, HOMESYS_SHOT);
	const sysview *v;

	memset(k, 0, sizeof *k);
	if (at < 0) return;
	v = &a->view[at];
	k->had = true;
	k->sort = v->sort;
	k->cursor = v->cursor;
	k->listed = v->listed;
	k->grid = v->grid;
	k->video = v->video;
	if (v->cursor >= 0 && v->cursor < v->list.count)
		snprintf(k->file, sizeof k->file, "%s", v->list.items[v->cursor].file);
}

static void shots_put_back(app *a, const shots_keep *k)
{
	int at = group_at(a, HOMESYS_SHOT), c = -1;
	sysview *v;

	if (at < 0) return;
	v = &a->view[at];
	if (k->had) {
		v->sort = k->sort;
		v->listed = k->listed;
		v->grid = k->grid;
		v->video = k->video;
	}
	sort_shelf(a, at);
	for (int i = 0; k->file[0] && i < v->list.count; i++)
		if (!strcmp(v->list.items[i].file, k->file)) c = i;
	/* The shot it was on gone (deleted): the one that took its place */
	if (c < 0 && k->had) c = k->cursor < v->list.count ? k->cursor : v->list.count - 1;
	v->cursor = c > 0 ? c : 0;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
}

void refresh_shots_shelf(app *a)
{
	char tag[sizeof a->sys.systems[0].tag];
	shots_keep k;
	int i, was = a->sys_cursor;

	if (a->sys.count <= 0) return;
	texload_bump();
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	shots_note(a, &k);
	drop_shots_shelf(a);
	build_shots_shelf(a);
	shots_put_back(a, &k);
	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, tag)) break;
	if (i < a->sys.count) {
		a->sys_cursor = i;
	} else {
		/* Stood on Screenshots, and it went (its last shot deleted): the
		 * home, on what took its place */
		a->sys_cursor = was < a->sys.count ? was : a->sys.count - 1;
		if (a->sys_cursor < 0) a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	if (a->sys.count > 0) a->tint = a->sys.systems[a->sys_cursor].accent;
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Rebuild the Favorites shelf in place, for use the moment a favorite
 * changes. Restarting the launcher to see a star take effect is not an
 * answer.
 *
 * Drop and rebuild rather than patch: the shelf is a sorted projection of a
 * set over every other shelf, and the four cases - it appears, it grows, it
 * shrinks, it goes away - are one line each this way and four separate
 * index-juggling routines the other.
 *
 * Everything is re-found by TAG afterwards, never by index. Inserting or
 * removing the shelf moves every system up or down by one, so the index the
 * caller was standing on means something different by the time this returns.
 * The tag does not move. */
void refresh_favorites_shelf(app *a)
{
	/* Favorites is rebuilt in place, so its indices now name other games. */
	texload_bump();
	char tag[sizeof a->sys.systems[0].tag];
	int cur, i, order = 0;

	if (a->sys.count <= 0) return;
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	cur = a->view[a->sys_cursor].cursor;
	if ((i = group_at(a, HOMESYS_FAV)) >= 0) order = a->view[i].sort;

	drop_favorites_shelf(a);
	build_favorites_shelf(a);
	/* Built in name order with its setting cleared, so the order chosen for
	 * it is put back: without this, favoriting a game turned the shelf back
	 * to Name, and the menu said Name, until the next boot read the setting
	 * again. */
	if ((i = group_at(a, HOMESYS_FAV)) >= 0) {
		a->view[i].sort = order;
		sort_shelf(a, i);
	}

	for (i = 0; i < a->sys.count; i++)
		if (strcmp(a->sys.systems[i].tag, tag) == 0) break;

	if (i < a->sys.count) {
		a->sys_cursor = i;
		/* The Favorites list can shrink under the cursor - un-favoriting the
		 * game you are looking at is the ordinary way to use this. */
		if (cur >= a->view[i].list.count)
			cur = a->view[i].list.count ? a->view[i].list.count - 1 : 0;
		a->view[i].cursor = cur;
		cf_reset(&a->view[i].cf, cur);
	} else {
		/* The shelf being stood on no longer exists, which happens exactly
		 * once: un-favoriting the last favorite while inside Favorites. There
		 * is no list to stay in, so go back out to the shelves. */
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Recents built again, after a game ended (src/launch_screen.c), was
 * renamed or deleted: the game played last first. The shelf stood on is kept
 * by its tag, as refresh_favorites_shelf keeps it, since Recents coming or
 * going moves every index after it; on Recents itself the cursor stays on the
 * game it was on, which is now where the play put it, and its order as it
 * was. */
void refresh_recents_shelf(app *a)
{
	char tag[sizeof a->sys.systems[0].tag];
	group_keep k;
	int i;

	if (a->sys.count <= 0) return;
	texload_bump();
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	group_note(a, HOMESYS_RECENT, &k);
	drop_recents_shelf(a);
	build_recents_shelf(a);
	group_put_back(a, HOMESYS_RECENT, &k);
	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, tag)) break;
	if (i < a->sys.count) {
		a->sys_cursor = i;
	} else {
		/* Stood on Recents, and it went: no game resolves any more */
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	if (a->sys.count > 0) a->tint = a->sys.systems[a->sys_cursor].accent;
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* The names of the groups, Favorites, Recents and Collections, in the
 * language that is on: they are words, where every other shelf's name is a
 * system's */
void shelf_groups_rename(app *a)
{
	int i;

	if ((i = group_at(a, HOMESYS_FAV)) >= 0)
		snprintf(a->sys.systems[i].name, sizeof a->sys.systems[i].name, "%s", T(S_LIBRARY_FAVORITES));
	if ((i = group_at(a, HOMESYS_RECENT)) >= 0)
		snprintf(a->sys.systems[i].name, sizeof a->sys.systems[i].name, "%s", T(S_LIBRARY_RECENTS));
	if ((i = group_at(a, HOMESYS_COLL)) >= 0)
		snprintf(a->sys.systems[i].name, sizeof a->sys.systems[i].name, "%s", T(S_LIBRARY_COLLECTIONS));
	if ((i = group_at(a, HOMESYS_SHOT)) >= 0)
		snprintf(a->sys.systems[i].name, sizeof a->sys.systems[i].name, "%s", T(S_LIBRARY_SCREENSHOTS));
}

/* MP4's shelf: a card per album, and which album each card is. Built in the
 * folder's own order, by artist; the order chosen for it is put on it by
 * sort_all once the setting has been read, the way every shelf's is. */
static void mp4_fill_view(sysview *v)
{
	int n = g_mp4.nalbums;

	if (n <= 0) return;
	v->list.items = calloc((size_t)n, sizeof *v->list.items);
	v->tex = calloc((size_t)n, sizeof *v->tex);
	v->tw = calloc((size_t)n, sizeof *v->tw);
	v->th = calloc((size_t)n, sizeof *v->th);
	v->cb = calloc((size_t)n, sizeof *v->cb);
	v->album = calloc((size_t)n, sizeof *v->album);
	if (!v->list.items || !v->tex || !v->tw || !v->th || !v->cb || !v->album) {
		free(v->list.items); free(v->tex); free(v->tw); free(v->th); free(v->cb);
		free(v->album);
		memset(v, 0, sizeof *v);
		return;
	}
	v->list.count = n;
	v->list.scanned = true;
	mp4_order_view(v);
}

/* MP4's shelf in its order, v->sort: which album each card is, and each
 * card's entry from its album - the artist in `name`, the album in `title`,
 * which the card is called, and its folder in `file`, which is how a change
 * of order finds the album that was selected. Filled afresh rather than
 * sorted in place: an entry is only ever a copy of the library's names. The
 * letter jump goes by whatever the shelf is in order of. */
void mp4_order_view(sysview *v)
{
	int k;

	/* The arrays hold every album; the shelf is the kind it shows. */
	if (!v->album) return;
	v->list.count = ml_shelf_order_kind(&g_mp4, (ml_order)v->sort, mp4_kind_shown(),
	                                    v->album);
	for (k = 0; k < v->list.count; k++) {
		game_entry *e = &v->list.items[k];
		int al = v->album[k];

		snprintf(e->name, sizeof e->name, "%s", g_mp4.artists[mp4_artist_of(al)].name);
		snprintf(e->title, sizeof e->title, "%s", g_mp4.albums[al].name);
		mp4_album_dir(al, e->file, sizeof e->file);
	}
	v->jump_by_name = v->sort == ML_BY_ARTIST;
}

/* A free name for `name` in `dir`: itself, or with " (2)", " (3)" ... before
 * its extension. False when none fits. */
static bool free_name(const char *dir, const char *name, char *out, size_t n)
{
	const char *dot = strrchr(name, '.');
	int stem = dot ? (int)(dot - name) : (int)strlen(name);
	int k;

	if (snprintf(out, n, "%s/%s", dir, name) >= (int)n) return false;
	for (k = 2; access(out, F_OK) == 0; k++) {
		if (k > 99) return false;
		if (snprintf(out, n, "%s/%.*s (%d)%s", dir, stem, name, k, dot ? dot : "")
		    >= (int)n) return false;
	}
	return true;
}

/* Songs loose at the top of Music/, in no folder, moved into Music/Singles,
 * where MP4 reads them as an album (ml_album's `singles`): loose, they were
 * on no shelf. Eric's call, 2026-10-05. A name already taken there gets " (2)"
 * and on, so nothing is overwritten. And Singles gets its cover, shipped with
 * ZyperOS (tools/gensingles.py), at Music/.media/Singles.png, where every
 * album's is kept, unless it already has one. A rename on the same card, so
 * it costs a folder read when there is nothing to move. */
static void mp4_tidy_singles(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16];
	char from[LIB_PATH * 2], to[LIB_PATH * 2 + 16];
	char (*loose)[256] = NULL;
	int nloose = 0, cap = 0, moved = 0, i;
	struct dirent *e;
	struct stat st;
	DIR *d;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (!(d = opendir(music))) return;
	/* Listed first, moved after: renaming out of a folder being read is
	 * allowed to show or skip entries. */
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !ml_is_audio(e->d_name)) continue;
		if (snprintf(from, sizeof from, "%s/%s", music, e->d_name) >= (int)sizeof from ||
		    stat(from, &st) != 0 || !S_ISREG(st.st_mode) ||
		    strlen(e->d_name) >= sizeof loose[0])
			continue;
		if (nloose == cap) {
			char (*more)[256] = realloc(loose, (size_t)(cap ? cap * 2 : 16) * sizeof *loose);

			if (!more) break;
			loose = more;
			cap = cap ? cap * 2 : 16;
		}
		snprintf(loose[nloose++], sizeof loose[0], "%s", e->d_name);
	}
	closedir(d);
	for (i = 0; i < nloose; i++) {
		mkdir(singles, 0755);
		snprintf(from, sizeof from, "%s/%s", music, loose[i]);
		if (free_name(singles, loose[i], to, sizeof to) && rename(from, to) == 0) moved++;
		else fprintf(stderr, "mp4: could not move %s into %s\n", loose[i], ML_SINGLES);
	}
	free(loose);
	if (moved)
		fprintf(stderr, "mp4: moved %d loose song%s into Music/%s\n",
		        moved, moved == 1 ? "" : "s", ML_SINGLES);
	singles_cover();
}

/* Singles' cover, the one shipped with ZyperOS, at Music/.media/Singles.png,
 * once there is a Singles and unless it has a cover already. At every scan,
 * and when the transfer screen opens, since a cover taken off the card there
 * was otherwise back only at the next restart (2026-10-05). */
void singles_cover(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16], cover[LIB_PATH * 2], src[LIB_PATH * 2];
	struct stat st;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (stat(singles, &st) != 0 || !S_ISDIR(st.st_mode)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.jpg", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.png", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(src, sizeof src, "%s/.media", music);
	mkdir(src, 0755);
	snprintf(src, sizeof src, "%s/singles.png", P_ROOT);
	copy_file(src, cover);
}

/* ---- MP4: albums filed under their artists -----------------------------
 *
 * An album folder straight in Music/ is its own artist on the shelf, so
 * Trompe Le Monde sorted under T rather than with the Pixies. Its tags say who
 * it is by, and MP4's player reads them (src/mp4d/tags.h): with a clear
 * answer it moves to Music/<artist>/<album>, its cover and Album Art's memory
 * of it with it, and the shelf is read again once. Eric's call, 2026-10-05.
 *
 * Left where it is when the tags give no clear answer (a podcast, a mix), when
 * the answer is its own name, when Music/<artist>/<album> is already there,
 * and for now when MP4 is playing from it. Never Singles, never a book. The
 * player is not up at the first scan of a boot, so this happens a moment
 * after, one album at a time, from the main loop. */
typedef struct { char dir[LIB_PATH]; unsigned asked; bool done; } flat_album;
static flat_album *g_flat;
static int         g_nflat;
bool        g_mp4_moved;            /* something moved: read it again */

/* The albums straight in Music/, after a scan. */
static void flat_albums_find(void)
{
	int i;

	free(g_flat);
	g_nflat = 0;
	g_flat = calloc((size_t)(g_mp4.nalbums > 0 ? g_mp4.nalbums : 1), sizeof *g_flat);
	if (!g_flat) return;
	for (i = 0; i < g_mp4.nalbums; i++) {
		const ml_album *al = &g_mp4.albums[i];
		const char *p, *slash;

		if (al->book || al->singles || al->n <= 0) continue;
		p = g_mp4.tracks[al->first].path;            /* Music/<album>/<track> */
		if (strncmp(p, "Music/", 6) || !(slash = strchr(p + 6, '/')) ||
		    strchr(slash + 1, '/'))
			continue;
		snprintf(g_flat[g_nflat++].dir, sizeof g_flat[0].dir, "%.*s", (int)(slash - p), p);
	}
}

/* An artist's name as a folder: what the card cannot store in one becomes -,
 * and no leading or trailing space or trailing dot. */
static void folder_name(const char *in, char *out, size_t n)
{
	size_t o = 0, len;

	while (*in == ' ') in++;
	for (; *in && o + 1 < n; in++)
		out[o++] = strchr("/\\:*?\"<>|", *in) || (unsigned char)*in < 0x20 ? '-' : *in;
	out[o] = '\0';
	for (len = o; len && (out[len - 1] == ' ' || out[len - 1] == '.'); len--) out[len - 1] = '\0';
}

/* Move Music/<album> under its artist. 1 moved, 0 left where it is, -1 not
 * now (MP4 is playing from it). */
static int file_album(const char *rel, const char *artist)
{
	const char *album = strrchr(rel, '/') + 1;
	char who[256], from[LIB_PATH * 2], dir[LIB_PATH * 2], to[LIB_PATH * 2 + 256];
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 600], key[LIB_PATH + 300], val[96];
	static const char *const EXT[] = { "jpg", "png" };
	size_t rl = strlen(rel);
	const char *t;
	int k;

	folder_name(artist, who, sizeof who);
	if (!who[0] || !strcmp(who, album) || !strcmp(who, ML_SINGLES)) return 0;
	for (k = 0; (t = mp4c_track(k)); k++)
		if (!strncmp(t, rel, rl) && t[rl] == '/') return -1;

	snprintf(from, sizeof from, "%s/%s", P_CARD, rel);
	snprintf(dir, sizeof dir, "%s/Music/%s", P_CARD, who);
	snprintf(to, sizeof to, "%s/%s", dir, album);
	if (access(to, F_OK) == 0) {
		fprintf(stderr, "mp4: Music/%s stays: Music/%s/%s is there already\n",
		        album, who, album);
		return 0;
	}
	mkdir(dir, 0755);
	if (rename(from, to) != 0) {
		fprintf(stderr, "mp4: could not move Music/%s under Music/%s\n", album, who);
		return 0;
	}
	/* Its cover, from Music/.media to the artist's. */
	for (k = 0; k < 2; k++) {
		snprintf(cfrom, sizeof cfrom, "%s/Music/.media/%s.%s", P_CARD, album, EXT[k]);
		if (access(cfrom, F_OK) != 0) continue;
		snprintf(cto, sizeof cto, "%s/.media", dir);
		mkdir(cto, 0755);
		snprintf(cto, sizeof cto, "%s/.media/%s.%s", dir, album, EXT[k]);
		rename(cfrom, cto);
	}
	/* And what Album Art remembers of it, so it is not fetched again. */
	snprintf(key, sizeof key, "mp4art.%s", rel);
	if (db_get_str(db_lib(), key, val, sizeof val, "") && val[0]) {
		db_del(db_lib(), key);
		snprintf(key, sizeof key, "mp4art.Music/%s/%s", who, album);
		db_set_str(db_lib(), key, val);
	}
	fprintf(stderr, "mp4: filed Music/%s under Music/%s\n", album, who);
	return 1;
}

/* Whether any album is still waiting for its answer: the shelf is read again
 * once they all have one, not once per album moved. */
bool flat_albums_waiting(void)
{
	int i;

	for (i = 0; i < g_nflat; i++)
		if (!g_flat[i].done) return true;
	return false;
}

/* One step: take what the player has answered, and ask about the next album.
 * From the main loop and the transfer screen's. */
void mp4_file_albums(void)
{
	char dir[LIB_PATH * 2], name[256];
	unsigned now = plat_now_ms();
	int i;

	while (mp4c_artist_take(dir, sizeof dir, name, sizeof name))
		for (i = 0; i < g_nflat; i++) {
			int r;

			if (g_flat[i].done || strcmp(g_flat[i].dir, dir)) continue;
			r = name[0] ? file_album(dir, name) : 0;
			if (r < 0) { g_flat[i].asked = 0; break; }   /* playing: ask later */
			g_flat[i].done = true;
			if (r > 0) g_mp4_moved = true;
			break;
		}
	/* One at a time: the next once this one is answered, or after five
	 * seconds for an answer lost with the player. */
	for (i = 0; i < g_nflat; i++) {
		if (g_flat[i].done) continue;
		if (g_flat[i].asked && now - g_flat[i].asked < 5000) return;
		if (mp4c_artist_ask(g_flat[i].dir)) g_flat[i].asked = now ? now : 1;
		return;
	}
}

/* MP4's card, at the end of the shelf, when there is music to play.
 *
 * After Favorites and after the empty systems are hidden, so it is neither
 * compacted away for having no games nor shifted by Favorites being inserted
 * in front: it is appended, and every later insertion moves it along with
 * everything else. Its view holds the albums - see mp4_fill_view - so A on
 * the card opens a shelf of their covers, the way a console's opens its
 * games.
 *
 * Hidden when the folder is empty or missing, the same rule an empty system
 * and an empty Favorites shelf follow: a card should lead somewhere. */
static void build_mp4_shelf(app *a, bool tidy)
{
	system_cfg *s;
	int i;

	/* Everything indexed by album goes with the albums: a rescan can put a
	 * different album at every index. */
	g_mp4_gen++;
	g_nflat = 0;
	ml_free(&g_mp4);
	free(g_cov);
	g_cov = NULL;
	free(g_book_done);
	g_book_done = NULL;
	np_forget();
	snprintf(g_mp4_root, sizeof g_mp4_root, "%s", P_CARD);
	/* Loose songs into Singles at a start or a rescan, never from a process
	 * beside a program (mp4_shelf_alone), which only reads the card */
	if (tidy) mp4_tidy_singles();
	if (!ml_scan_card(g_mp4_root, &g_mp4) || g_mp4.ntracks == 0) {
		fprintf(stderr, "scan: %-16s no music, books or videos in %s\n", "MP4", g_mp4_root);
		return;
	}
	g_cov = calloc((size_t)g_mp4.nalbums, sizeof *g_cov);
	g_book_done = calloc((size_t)g_mp4.nalbums, sizeof *g_book_done);
	if (!g_book_done) { ml_free(&g_mp4); return; }
	flat_albums_find();
	for (i = 0; i < g_mp4.nalbums; i++) {
		char key[LIB_PATH + 8], val[16];

		if (!g_mp4.albums[i].book) continue;
		book_key(i, key, sizeof key);
		db_get_str(db_dev(), key, val, sizeof val, "");
		g_book_done[i] = !strcmp(val, "finished");
	}
	{
		char show[16];

		/* All is where the shelf starts since 2026-11-09. A handheld from
		 * before kept "music" or "videos" - often not chosen at all but
		 * left by the shelf following what played - so it starts on All
		 * once, and a kind chosen after that is kept. */
		if (!db_has(db_dev(), "mp4.show_all_once")) {
			db_set_str(db_dev(), "mp4.show", "all");
			db_set_str(db_dev(), "mp4.show_all_once", "1");
		}
		db_get_str(db_dev(), "mp4.show", show, sizeof show, "all");
		g_mp4_kind = !strcmp(show, "books") ? ML_BOOKS : !strcmp(show, "videos") ? ML_VIDEOS
		            : !strcmp(show, "music") ? ML_MUSIC : ML_ALL;
	}
	if (sys_kept(a) >= CFG_MAX_SYSTEMS) return;
	/* The last shelf of the home: the systems off it move up one */
	i = a->sys.count;
	shelf_insert(a, i);
	s = &a->sys.systems[i];
	/* MP4, the media player, once the audio player Muse (until 2026-11-05).
	 * The tag and the card's file keep that name, MUSE: settings and themes
	 * are keyed by them, and a card's .last. */
	snprintf(s->name, CFG_STR, "%s", "MP4");
	snprintf(s->tag, sizeof s->tag, "%s", "MUSE");
	snprintf(s->card, CFG_STR, "%s", "MUSE.png");
	s->accent = ZYPEROS_ACCENT;
	mp4_fill_view(&a->view[i]);
	fprintf(stderr, "scan: %-16s %d artists, %d albums, %d books, %d videos, %d tracks\n", "MP4",
	        g_mp4.nartists, ml_count(&g_mp4, false), ml_count(&g_mp4, true),
	        ml_count_kind(&g_mp4, ML_VIDEOS), g_mp4.ntracks);
}

/* MP4's card alone, for a process that shows no other shelf: the in-game
 * menu over a program of its own that keeps MP4 playing (SELECT there), and
 * the follower beside it (src/mp4hand.h), which keeps a book's place by the
 * same library. The card read and not written: no loose song moved */
void mp4_shelf_alone(app *a)
{
	build_mp4_shelf(a, false);
}

/* ---------- the saved library (src/savedlib.h) ---------------------------
 *
 * A start's shelves from the saved library when it has them, and the card
 * read again once the shelf is up (lib_check, from main.c): on my card,
 * 8,844 games on exFAT, the scan was the longest thing before the first
 * frame, 600-700 ms after a cold boot (2026-11-07). What each shelf showed is
 * kept here, one per system in systems.cfg, so the check can tell whether
 * the card is still that. */

#ifndef ZYPEROS_VERSION
#define ZYPEROS_VERSION "dev"
#endif

static struct {
	char     folder[LIB_NAME], exts[LIB_NAME];
	/* The folder systems.cfg names and its EmulationStation names, for the
	 * check to choose the folder again (shelf_folder_choose) */
	char     home[LIB_NAME], es[160];
	uint64_t shown;           /* lib_listing_digest of the listing the shelf showed */
	uint64_t saved;           /* and of the saved file's, when it had one */
	bool     in_file;
	bool     pm;              /* the Ports shelf, listed with PortMaster's entry */
	bool     splore;          /* the PICO-8 shelf, listed with Splore's entry */
} g_shown[CFG_MAX_SYSTEMS];
static int g_nshown;
static int g_saved_extra;     /* the file's shelves for folders no system has */
static SDL_atomic_t g_lib_gen;  /* one more at every scan: a check older is moot */
static SDL_mutex *g_save_lock;

static void saved_path(char *out, size_t n)
{
	snprintf(out, n, "%s/saved-library", P_USERDATA);
}

/* ---------- PortMaster's entry on the Ports shelf (src/pminstall.h) -------
 *
 * The Ports shelf lists Roms/Ports/PortMaster.sh whether it is on the card or
 * not, wherever the card's ZyperOS has the installer (the Pixel 2's, whose
 * launch.sh runs ports): the same entry a scan makes of the real one, so the
 * saved library, the card read again after a start, a favorite and the play
 * time are all the same before and after it is installed. Only the A on it
 * differs: the page that installs it, while it is not on the card. */

bool g_pm_offer;

static bool pm_installer_here(void)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s", P_ROOT, PM_INSTALLER);
	return access(p, R_OK) == 0;
}

static bool is_ports(const system_cfg *s)
{
	return !strcmp(s->core, "port");
}

bool pm_on_card(const char *folder)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s/%s/%s", P_ROMS, folder, PM_FOLDER, PM_PROGRAM);
	if (access(p, R_OK) != 0) return false;
	snprintf(p, sizeof p, "%s/%s/%s", P_ROMS, folder, PM_SCRIPT);
	return access(p, R_OK) == 0;
}

bool pm_offer_refresh(const app *a)
{
	g_pm_offer = false;
	if (!pm_installer_here()) return false;
	/* Ports off the home too: its entry may be on Favorites or Recents */
	for (int i = 0; i < sys_kept(a); i++)
		if (is_ports(&a->sys.systems[i])) {
			g_pm_offer = !pm_on_card(a->sys.systems[i].folder);
			break;
		}
	return g_pm_offer;
}

bool pm_is_offer(const app *a, int sys, const game_entry *g)
{
	return g_pm_offer && g && sys >= 0 && sys < sys_kept(a) &&
	       is_ports(&a->sys.systems[sys]) && !strcasecmp(g->file, PM_SCRIPT);
}

bool pm_is_entry(const app *a, int sys, const game_entry *g)
{
	return g && sys >= 0 && sys < sys_kept(a) && is_ports(&a->sys.systems[sys]) &&
	       !strcasecmp(g->file, PM_SCRIPT);
}

bool pm_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                   char *out, size_t n)
{
	if (!pm_is_entry(a, sys, g)) return false;
	snprintf(out, n, "%s/%s", P_ROOT,
	         theme_ix == DS_DARK ? PM_COVER_DARK : PM_COVER_LIGHT);
	return access(out, R_OK) == 0;
}

bool pm_icon_path(char *out, size_t n)
{
	snprintf(out, n, "%s/%s", P_ROOT, PM_ICON);
	return access(out, R_OK) == 0;
}

int pm_shelf_offers(const app *a, int sys)
{
	const sysview *v = &a->view[sys];

	const game_list *gl = sys_games_c(v);

	/* A group's, by each entry's own system: Recents has Splore once it was
	 * opened, and its Games count it no more than the PICO-8 shelf does */
	if (v->owner) {
		int n = 0;

		for (int i = 0; !v->all && i < v->list.count; i++)
			if (splore_is_entry(a, v->owner[i], &v->list.items[i]) ||
			    pm_is_offer(a, v->owner[i], &v->list.items[i]))
				n++;
		return n;
	}
	/* Splore's entry is the PICO-8 shelf's own, never a game */
	if (!strcmp(a->sys.systems[sys].tag, P8_TAG)) return splore_listing_has(gl) ? 1 : 0;
	if (!g_pm_offer || !is_ports(&a->sys.systems[sys])) return 0;
	for (int i = 0; i < gl->count; i++)
		if (!strcasecmp(gl->items[i].file, PM_SCRIPT)) return 1;
	return 0;
}

bool pm_only_offer(const app *a, int sys)
{
	return a->view[sys].list.count == 1 && pm_shelf_offers(a, sys) == 1;
}

/* ---------- Splore's entry on the PICO-8 shelf (src/pico8.h) --------------
 *
 * Wherever the card's ZyperOS has the script that runs your PICO-8 (the
 * Pixel 2's, whose launch.sh runs a shelf's script), the PICO-8 shelf lists
 * Splore first, PICO-8 on the card or not (Tarik's pick, 2026-11-19): A
 * opens Splore once your PICO-8 is there, and until then a page that says
 * how to add it. */

static bool splore_here(void)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s", P_ROOT, P8_SCRIPT);
	return access(p, R_OK) == 0;
}

bool p8_on_card(const system_cfg *s)
{
	return core_ready(s, P8_CORE);
}

bool splore_is_entry(const app *a, int sys, const game_entry *g)
{
	return g && sys >= 0 && sys < a->sys.count && !strcmp(a->sys.systems[sys].tag, P8_TAG) &&
	       splore_file(g->file);
}

bool splore_is_offer(const app *a, int sys, const game_entry *g)
{
	return splore_is_entry(a, sys, g) && !p8_on_card(&a->sys.systems[sys]);
}

bool splore_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                       char *out, size_t n)
{
	if (!splore_is_entry(a, sys, g)) return false;
	snprintf(out, n, "%s/%s", P_ROOT, theme_ix == DS_DARK ? P8_COVER_DARK : P8_COVER_LIGHT);
	return access(out, R_OK) == 0;
}

bool own_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                    char *out, size_t n)
{
	return pm_cover_path(a, sys, g, theme_ix, out, n) ||
	       splore_cover_path(a, sys, g, theme_ix, out, n);
}

/* ---------- EmulationStation's folders (config/systems.cfg, es lines) ----
 *
 * A system's shelf reads ONE folder: its own while that has a game, and
 * otherwise the one of its EmulationStation names on the card with the most
 * games, the first listed on a tie. Chosen whenever the card is read - a
 * rescan, and the check after a start that came from the saved library -
 * and kept by home folder (lib_folder_set) for every load of systems.cfg
 * after it (shelf_folders_apply). A start from the saved library takes the
 * folder the file has, so it reads nothing more off the card. */

void shelf_folders_apply(systems_cfg *s)
{
	for (int i = 0; s && i < s->count; i++)
		lib_folder_get(s->systems[i].home, s->systems[i].folder, sizeof s->systems[i].folder);
}

/* Roms/'s folders, read once for a scan that reads the card and only when a
 * system there has EmulationStation names: one listing of about forty
 * entries */
typedef struct {
	char (*name)[LIB_NAME];
	int  n;
	bool read;
} roms_dirs;

static void roms_dirs_read(roms_dirs *d)
{
	DIR *dh;
	struct dirent *e;
	int cap = 0;

	d->read = true;
	if (!(dh = opendir(P_ROMS))) return;
	while ((e = readdir(dh))) {
		if (e->d_name[0] == '.') continue;
		if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) continue;
		if (e->d_type == DT_UNKNOWN) {
			char p[LIB_PATH * 2];
			struct stat st;

			snprintf(p, sizeof p, "%s/%s", P_ROMS, e->d_name);
			if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		}
		if (d->n == cap) {
			int more = cap ? cap * 2 : 64;
			char (*bigger)[LIB_NAME] = realloc(d->name, (size_t)more * sizeof *bigger);

			if (!bigger) break;
			d->name = bigger;
			cap = more;
		}
		snprintf(d->name[d->n++], LIB_NAME, "%s", e->d_name);
	}
	closedir(dh);
}

static void roms_dirs_free(roms_dirs *d)
{
	free(d->name);
	memset(d, 0, sizeof *d);
}

/* The folder of that name on the card, as the card spells it: exFAT takes
 * any case, a host's file system may not */
static const char *roms_dirs_find(const roms_dirs *d, const char *name)
{
	for (int i = 0; i < d->n; i++)
		if (!strcasecmp(d->name[i], name)) return d->name[i];
	return NULL;
}

#define ES_PICK_MAX 12

/* Which folder a system with EmulationStation names reads, into `folder`,
 * with its listing in `out`. The losers are named in the log, with what
 * they hold. `tag` is for the log only. */
static void shelf_folder_choose(const char *tag, const char *home, const char *es,
                                const char *exts, const roms_dirs *d,
                                char *folder, size_t fn, game_list *out)
{
	system_cfg one;
	const char *there[ES_PICK_MAX];
	game_list lists[ES_PICK_MAX];
	int games[ES_PICK_MAX], n = 0, k, pick, home_games;
	char name[LIB_NAME];

	memset(&one, 0, sizeof one);
	snprintf(one.es, sizeof one.es, "%s", es);
	for (k = 0; n < ES_PICK_MAX && cfg_es_at(&one, k, name, sizeof name); k++) {
		const char *on_card = roms_dirs_find(d, name);

		if (on_card && strcasecmp(on_card, home)) there[n++] = on_card;
	}
	lib_scan_listing(P_ROMS, home, exts, out);
	snprintf(folder, fn, "%s", home);
	if (n == 0) return;
	home_games = out->count;
	memset(lists, 0, sizeof lists);
	for (k = 0; k < n; k++) {
		lib_scan_listing(P_ROMS, there[k], exts, &lists[k]);
		games[k] = lists[k].count;
	}
	pick = lib_folder_pick(home_games, games, n);
	if (pick >= 0) {
		fprintf(stderr, "scan: %-16s reads Roms/%s (%d game%s): Roms/%s has none\n", tag,
		        there[pick], games[pick], games[pick] == 1 ? "" : "s", home);
		lib_free(out);
		*out = lists[pick];
		memset(&lists[pick], 0, sizeof lists[pick]);
		snprintf(folder, fn, "%s", there[pick]);
	}
	for (k = 0; k < n; k++) {
		if (k != pick && games[k] > 0)
			fprintf(stderr, "scan: %-16s does not read Roms/%s (%d game%s): one folder a shelf, "
			        "Roms/%s's\n", tag, there[k], games[k], games[k] == 1 ? "" : "s", folder);
		lib_free(&lists[k]);
	}
}

void shelf_folders_from_card(systems_cfg *s)
{
	roms_dirs dirs = { 0 };
	game_list l = { 0 };

	for (int i = 0; s && i < s->count; i++) {
		system_cfg *sc = &s->systems[i];

		if (!sc->es[0]) continue;
		if (!dirs.read) roms_dirs_read(&dirs);
		shelf_folder_choose(sc->tag, sc->home, sc->es, sc->exts, &dirs, sc->folder,
		                    sizeof sc->folder, &l);
		lib_free(&l);
		lib_folder_set(sc->home, sc->folder);
	}
	roms_dirs_free(&dirs);
}

/* An untaken saved shelf for one of a system's EmulationStation names,
 * taken: the folder a start before this one chose (shelf_folder_choose).
 * `folder` gets the saved spelling. */
static bool saved_take_es(saved_lib *saved, const system_cfg *sc, char *folder, size_t fn,
                          game_list *out)
{
	char name[LIB_NAME];

	for (int k = 0; cfg_es_at(sc, k, name, sizeof name); k++)
		for (int i = 0; i < saved->count; i++) {
			saved_shelf *sh = &saved->shelves[i];

			if (sh->taken || strcasecmp(sh->folder, name) || strcmp(sh->exts, sc->exts)) continue;
			snprintf(folder, fn, "%s", sh->folder);
			return savedlib_take(saved, sh->folder, sh->exts, out);
		}
	return false;
}

int scan_all_from(app *a, saved_lib *saved, bool save)
{
	int from_saved = 0;
	bool pm_installer = pm_installer_here();
	bool splore = splore_here();
	roms_dirs dirs = { 0 };

	SDL_AtomicIncRef(&g_lib_gen);
	g_nshown = 0;
	for (int i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];
		system_cfg *sc = &a->sys.systems[i];
		bool kept = saved && savedlib_take(saved, sc->folder, sc->exts, &v->list);
		uint64_t digest;

		bool pm = is_ports(sc) && pm_installer;
		bool sp = splore && !strcmp(sc->tag, P8_TAG);

		/* The saved library under one of its EmulationStation names: the
		 * folder the last read of the card chose */
		if (!kept && saved && sc->es[0] &&
		    saved_take_es(saved, sc, sc->folder, sizeof sc->folder, &v->list)) {
			kept = true;
			lib_folder_set(sc->home, sc->folder);
		}
		if (!kept && sc->es[0]) {
			if (!dirs.read) roms_dirs_read(&dirs);
			shelf_folder_choose(sc->tag, sc->home, sc->es, sc->exts, &dirs,
			                    sc->folder, sizeof sc->folder, &v->list);
			lib_folder_set(sc->home, sc->folder);
		} else if (!kept) {
			lib_scan_listing(P_ROMS, sc->folder, sc->exts, &v->list);
		}
		if (pm) pm_listing_add(&v->list);
		if (sp) splore_listing_add(&v->list);
		digest = lib_listing_digest(&v->list);
		if (g_nshown < CFG_MAX_SYSTEMS) {
			snprintf(g_shown[g_nshown].folder, sizeof g_shown[0].folder, "%s", sc->folder);
			snprintf(g_shown[g_nshown].exts, sizeof g_shown[0].exts, "%s", sc->exts);
			snprintf(g_shown[g_nshown].home, sizeof g_shown[0].home, "%s", sc->home);
			snprintf(g_shown[g_nshown].es, sizeof g_shown[0].es, "%s", sc->es);
			g_shown[g_nshown].shown = digest;
			g_shown[g_nshown].saved = kept ? digest : 0;
			g_shown[g_nshown].in_file = kept;
			g_shown[g_nshown].pm = pm;
			g_shown[g_nshown].splore = sp;
			g_nshown++;
		}
		from_saved += kept;
		lib_scan_finish(sc->folder, &v->list);
		shelf_dirs_init(v);
	}
	roms_dirs_free(&dirs);
	g_saved_extra = saved ? savedlib_untaken(saved) : 0;
	/* Read off the card (a rescan): the file is this now */
	if (save) {
		savedlib_row rows[CFG_MAX_SYSTEMS];
		char path[LIB_PATH];
		int n = 0;

		for (int i = 0; i < a->sys.count && n < CFG_MAX_SYSTEMS; i++, n++)
			rows[n] = (savedlib_row){ a->sys.systems[i].folder, a->sys.systems[i].exts,
			                          sys_games(&a->view[i]) };
		saved_path(path, sizeof path);
		if (g_save_lock) SDL_LockMutex(g_save_lock);
		if (!savedlib_write(path, ZYPEROS_VERSION, rows, n))
			fprintf(stderr, "saved library: could not write %s\n", path);
		if (g_save_lock) SDL_UnlockMutex(g_save_lock);
	}
	for (int i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];
		const game_list *gl = sys_games(v);

		if (v->list.count > 0) {
			v->tex = calloc((size_t)v->list.count, sizeof *v->tex);
			v->tw = calloc((size_t)v->list.count, sizeof *v->tw);
			v->th = calloc((size_t)v->list.count, sizeof *v->th);
			v->cb = calloc((size_t)v->list.count, sizeof *v->cb);
			/* A count with no array behind it would be dereferenced on the
			 * next frame. An empty shelf is the honest answer. */
			if (!v->tex || !v->tw || !v->th || !v->cb) {
				free(v->tex); free(v->tw); free(v->th); free(v->cb);
				v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
				lib_free(&v->list);
				shelf_dirs_free(v);
				gl = &v->list;
			}
		}
		if (v->all)
			fprintf(stderr, "scan: %-16s %d game%s, %d folder%s at the top\n",
			        a->sys.systems[i].folder, gl->count, gl->count == 1 ? "" : "s",
			        v->nfold, v->nfold == 1 ? "" : "s");
		else
			fprintf(stderr, "scan: %-16s %d game%s\n", a->sys.systems[i].folder,
			        gl->count, gl->count == 1 ? "" : "s");
		/* What was left out, and what the folder takes, so a game that is
		 * on the card and not on the shelf has its reason in the log. */
		if (gl->skipped) {
			char names[LIB_SKIPS_SHOWN * (LIB_NAME + 4)] = "";
			int k, shown = gl->skipped < LIB_SKIPS_SHOWN ? gl->skipped : LIB_SKIPS_SHOWN;

			for (k = 0; k < shown; k++) {
				size_t at = strlen(names);

				snprintf(names + at, sizeof names - at, "%s\"%s\"", k ? ", " : "",
				         gl->skipped_eg[k]);
			}
			fprintf(stderr, "scan: %-16s left out %d (it takes %s): %s%s\n",
			        a->sys.systems[i].folder, gl->skipped,
			        a->sys.systems[i].exts[0] ? a->sys.systems[i].exts : "anything",
			        names, gl->skipped > shown ? ", and more" : "");
		}
	}
	hide_empty_systems(a);
	build_favorites_shelf(a);
	build_recents_shelf(a);
	/* The lists read again at each scan: a list copied onto the card, or
	 * written by hand, shows after a rescan as a game does */
	coll_load(P_CARD);
	build_collections_shelf(a);
	build_shots_shelf(a);
	build_mp4_shelf(a, true);
	pm_offer_refresh(a);
	return from_saved;
}

void scan_all_start(app *a, bool use_saved)
{
	saved_lib saved;
	char path[LIB_PATH];
	bool have = false;

	if (use_saved) {
		saved_path(path, sizeof path);
		have = savedlib_load(path, ZYPEROS_VERSION, &saved);
		if (!have) fprintf(stderr, "saved library: none to use, the card read\n");
	}
	if (have) {
		int systems = a->sys.count, n = scan_all_from(a, &saved, false);

		fprintf(stderr, "saved library: %d of %d shelves from it, the rest read off the card\n",
		        n, systems);
		savedlib_free(&saved);
	} else {
		scan_all_from(a, NULL, false);
	}
}

struct lib_check {
	int         gen;
	int         n;
	saved_shelf fresh[CFG_MAX_SYSTEMS];
	uint64_t    shown[CFG_MAX_SYSTEMS], saved[CFG_MAX_SYSTEMS];
	bool        in_file[CFG_MAX_SYSTEMS];
	bool        pm[CFG_MAX_SYSTEMS], splore[CFG_MAX_SYSTEMS];
	/* Each shelf's folder systems.cfg names and its EmulationStation names,
	 * and whether the check chose another folder than the one shown */
	char        home[CFG_MAX_SYSTEMS][LIB_NAME], es[CFG_MAX_SYSTEMS][160];
	bool        moved[CFG_MAX_SYSTEMS];
	/* What lib_check_import wrote of each folder's gamelist.xml */
	es_import_list got[CFG_MAX_SYSTEMS];
	bool        rewrite;          /* the file is not the card, or there is none */
	char        changed[96];      /* the folders the shelves are behind on, for the log */
	bool        behind;           /* and whether there are any */
	unsigned    ms;
};

lib_check *lib_check_begin(void)
{
	lib_check *c;

	if (g_nshown <= 0 || !(c = calloc(1, sizeof *c))) return NULL;
	if (!g_save_lock) g_save_lock = SDL_CreateMutex();
	c->gen = SDL_AtomicGet(&g_lib_gen);
	c->n = g_nshown;
	c->rewrite = g_saved_extra > 0;
	_Static_assert(sizeof c->fresh[0].folder == sizeof g_shown[0].folder &&
	               sizeof c->fresh[0].exts == sizeof g_shown[0].exts, "the same fields");
	for (int k = 0; k < c->n; k++) {
		memcpy(c->fresh[k].folder, g_shown[k].folder, sizeof c->fresh[k].folder);
		memcpy(c->fresh[k].exts, g_shown[k].exts, sizeof c->fresh[k].exts);
		c->shown[k] = g_shown[k].shown;
		c->saved[k] = g_shown[k].saved;
		c->in_file[k] = g_shown[k].in_file;
		c->pm[k] = g_shown[k].pm;
		c->splore[k] = g_shown[k].splore;
		memcpy(c->home[k], g_shown[k].home, sizeof c->home[k]);
		memcpy(c->es[k], g_shown[k].es, sizeof c->es[k]);
	}
	return c;
}

void lib_check_run(lib_check *c)
{
	unsigned t0 = plat_now_ms();
	char path[LIB_PATH];
	roms_dirs dirs = { 0 };

	for (int k = 0; k < c->n; k++) {
		uint64_t d;

		if (c->es[k][0]) {
			char was[LIB_NAME];

			/* The folder chosen again, from the card as it is now: an
			 * EmulationStation folder copied on since the saved library
			 * was written, or ZyperOS's own given games */
			if (!dirs.read) roms_dirs_read(&dirs);
			snprintf(was, sizeof was, "%s", c->fresh[k].folder);
			shelf_folder_choose(c->home[k], c->home[k], c->es[k], c->fresh[k].exts, &dirs,
			                    c->fresh[k].folder, sizeof c->fresh[k].folder,
			                    &c->fresh[k].list);
			c->moved[k] = strcmp(was, c->fresh[k].folder) != 0;
			if (c->moved[k]) c->rewrite = true;
		} else {
			lib_scan_listing(P_ROMS, c->fresh[k].folder, c->fresh[k].exts, &c->fresh[k].list);
		}
		if (c->pm[k]) pm_listing_add(&c->fresh[k].list);
		if (c->splore[k]) splore_listing_add(&c->fresh[k].list);
		d = lib_listing_digest(&c->fresh[k].list);
		if (!c->in_file[k] || d != c->saved[k]) c->rewrite = true;
		if (d != c->shown[k] || c->moved[k]) {
			size_t at = strlen(c->changed);

			if (at + strlen(c->fresh[k].folder) + 3 < sizeof c->changed)
				snprintf(c->changed + at, sizeof c->changed - at, "%s%s", at ? ", " : "",
				         c->fresh[k].folder);
			c->behind = true;
		}
	}
	if (c->rewrite) {
		savedlib_row rows[CFG_MAX_SYSTEMS];

		for (int k = 0; k < c->n; k++)
			rows[k] = (savedlib_row){ c->fresh[k].folder, c->fresh[k].exts, &c->fresh[k].list };
		saved_path(path, sizeof path);
		SDL_LockMutex(g_save_lock);
		/* Not over a rescan's: the card may have changed since this read it */
		if (SDL_AtomicGet(&g_lib_gen) == c->gen &&
		    !savedlib_write(path, ZYPEROS_VERSION, rows, c->n))
			fprintf(stderr, "saved library: could not write %s\n", path);
		SDL_UnlockMutex(g_save_lock);
	}
	roms_dirs_free(&dirs);
	c->ms = plat_now_ms() - t0;
}


/* Read the card again and rebuild every shelf, for when something outside the
 * launcher has changed what is on it - which today means Wi-Fi File Transfer.
 *
 * Drop and rebuild rather than patch, on the same reasoning
 * drop_favorites_shelf gives: the cases are "a game appeared", "a game went",
 * "a system that was empty now has games", "a system emptied", and "the shelf
 * you are standing on is one of those". Patching five cases correctly is
 * harder than doing the whole thing again, and the whole thing is three
 * directory reads.
 *
 * The config is re-read because hide_empty_systems does not hide, it COMPACTS:
 * a system with no games is removed from systems[] and its entry overwritten.
 * Uploading the first ROM for a system is precisely what this feature is for,
 * so without the reload that system could never come back. */
bool g_order_release;

void system_order_load(void)
{
	char v[16];

	db_get_str(db_dev(), "systems.order", v, sizeof v, "az");
	g_order_release = !strcmp(v, "release");
}

void system_order_apply(systems_cfg *s)
{
	if (!g_order_release) cfg_sort_systems(s);
}

/* Every shelf built again: rescan_all's, from the card, and lib_check's,
 * from the listings it read (`pre`). `keep_place` keeps the cursor on the
 * game it was on, which a check asks for: it comes by itself, a moment
 * after the shelf is up, and nobody asked for the list to start over. */
static void rebuild(app *a, saved_lib *pre, bool keep_place)
{
	char tag[sizeof a->sys.systems[0].tag];
	char path[CFG_STR * 2], file[LIB_PATH] = "", dir[LIB_PATH] = "";
	int i;

	/* Indices move when a system appears or empties, so the tag is the only
	 * handle on "where the player was" that survives the rebuild. */
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	/* The folder of it the shelf showed, which a rescan keeps: a game copied
	 * into it shows where it went */
	if (a->sys.count > 0) snprintf(dir, sizeof dir, "%s", a->view[a->sys_cursor].dir);
	if (keep_place && a->sys.count > 0) {
		const sysview *v = &a->view[a->sys_cursor];

		if (v->cursor >= 0 && v->cursor < v->list.count)
			snprintf(file, sizeof file, "%s", v->list.items[v->cursor].file);
	}

	/* First, and on their own: their lists and owner maps are this file's
	 * allocations rather than lib_scan's, and their textures are their own. */
	drop_shots_shelf(a);
	drop_collections_shelf(a);
	drop_recents_shelf(a);
	drop_favorites_shelf(a);

	free_all_textures(a);
	/* The systems off the home with the rest: hide_empty_systems sets them
	 * aside again */
	for (i = 0; i < sys_kept(a); i++) {
		free(a->view[i].tex); free(a->view[i].tw); free(a->view[i].th);
		free(a->view[i].cb);
		free(a->view[i].album);                         /* MP4's, else NULL */
		lib_free(&a->view[i].list);
		shelf_dirs_free(&a->view[i]);
		memset(&a->view[i], 0, sizeof a->view[i]);
	}
	a->parked = 0;

	/* menu_shelf_width measures every row of every system's menu once and
	 * caches it. The set of systems is exactly what it measured across, so a
	 * rescan invalidates it - and the case that shows is a card that booted
	 * empty: with no systems there are no game-menu rows to measure, the
	 * panel is sized for the ZyperOS menu alone, and the first system to
	 * arrive gets "Integer tall" cut off inside a frame measured before it
	 * existed. */
	a->menu_w = 0;

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (cfg_load_systems(path, &a->sys)) shelf_folders_apply(&a->sys);
	else {
		/* The card was readable a moment ago, so this is a card that has gone
		 * away underneath us. An empty shelf is the honest picture. */
		fprintf(stderr, "rescan: no usable %s\n", path);
		a->sys.count = 0;
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
		return;
	}
	system_order_apply(&a->sys);

	/* A rescan read the card, so the saved library is that now; a check's
	 * listings are the file already (lib_check_run) */
	scan_all_from(a, pre, !pre);
	display_load(a);     /* indexes by tag, so it is safe to run again */
	p8_take_now();       /* a PICO-8 zip copied since: Rescan takes it */
	core_load(a);
	sort_load(a);
	view_load(a);
	sort_all(a);

	a->sys_cursor = 0;
	for (i = 0; i < a->sys.count; i++)
		if (strcmp(a->sys.systems[i].tag, tag) == 0) { a->sys_cursor = i; break; }

	/* Whether the caller can stay where it was.
	 *
	 * Rescanning from inside a system's game list should leave you in that
	 * list looking at the new games, so the screen is not forced here. But the
	 * shelf being stood on can have emptied and gone - deleting the last ROM
	 * for a system over the network is the ordinary way - and then there is no
	 * list to stay in. Same rule refresh_favorites_shelf follows when the last
	 * favorite goes. */
	if (i >= a->sys.count || !a->sys.count) a->screen = SCREEN_SYSTEMS;

	if (a->sys.count) {
		sysview *v = &a->view[a->sys_cursor];
		int at = 0;

		/* Into the folder again, or as near it as is still there; on
		 * Collections, into the collection (src/collect.h) */
		if (dir[0] && (v->all || a->sys_cursor == shelf_coll_at(a)) && i < a->sys.count)
			shelf_open_dir(a, a->sys_cursor, dir, NULL);
		for (int k = 0; file[0] && k < v->list.count; k++)
			if (!strcmp(v->list.items[k].file, file)) { at = k; break; }
		a->tint = a->sys.systems[a->sys_cursor].accent;
		/* The rebuilt view starts at zero, or on the kept game, and its
		 * coverflow has to agree, or the shelf animates from wherever the
		 * old one happened to be. */
		v->cursor = at;
		cf_reset(&v->cf, at);
		if (keep_place) games_reset(&v->row, at, v->list.count, plat_now_ms());
	}
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Every shelf built again from the saved library, keeping the place: what
 * MENU > Display > Home Systems asks when it closes on a change. No read of
 * the card for a shelf the file has (a system turned on again comes from
 * it); the card for one it has not. */
void shelves_reload(app *a)
{
	saved_lib saved;
	char path[LIB_PATH];

	saved_path(path, sizeof path);
	if (savedlib_load(path, ZYPEROS_VERSION, &saved)) {
		rebuild(a, &saved, true);
		savedlib_free(&saved);
	} else {
		rebuild(a, NULL, true);
	}
}

void rescan_all(app *a)
{
	/* The card read again: EmulationStation's indexes with it, and the
	 * menu music's folders on the player's own thread */
	es_forget();
	rebuild(a, NULL, false);
	bgm_rescan();
}

/* The saved library as the shelves are now, after a change the launcher
 * made itself and so knows without reading the card: the folder's digest
 * for the check, and the file for the next start (src/savedlib.h) */
static void saved_follow(app *a, const char *folder)
{
	savedlib_row rows[CFG_MAX_SYSTEMS];
	char path[LIB_PATH];
	int i, n = 0;

	for (i = 0; i < g_nshown; i++) {
		int k;

		if (strcmp(g_shown[i].folder, folder)) continue;
		g_shown[i].shown = 0;
		for (k = 0; k < sys_kept(a); k++)
			if (!a->view[k].owner && !strcmp(a->sys.systems[k].folder, folder))
				g_shown[i].shown = lib_listing_digest(sys_games(&a->view[k]));
		g_shown[i].saved = g_shown[i].shown;
	}
	/* The systems off the home too: the file is every system's */
	for (i = 0; i < sys_kept(a) && n < CFG_MAX_SYSTEMS; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (a->view[i].owner || !strcmp(sc->tag, "MUSE") || !sc->folder[0]) continue;
		rows[n++] = (savedlib_row){ sc->folder, sc->exts, sys_games(&a->view[i]) };
	}
	saved_path(path, sizeof path);
	if (g_save_lock) SDL_LockMutex(g_save_lock);
	if (!savedlib_write(path, ZYPEROS_VERSION, rows, n))
		fprintf(stderr, "saved library: could not write %s\n", path);
	if (g_save_lock) SDL_UnlockMutex(g_save_lock);
}

/* One shelf's system taken off the home, or one of the systems off it
 * (app.parked): its arrays freed, the rest moved down, as
 * hide_empty_systems compacts them */
static void drop_system(app *a, int si)
{
	sysview *v = &a->view[si];
	int i, end = sys_kept(a);

	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	free(v->album);
	lib_free(&v->list);
	shelf_dirs_free(v);
	for (i = si; i + 1 < end; i++) {
		a->sys.systems[i] = a->sys.systems[i + 1];
		a->view[i] = a->view[i + 1];
	}
	if (si < a->sys.count) a->sys.count--;
	else a->parked--;
	memset(&a->sys.systems[end - 1], 0, sizeof a->sys.systems[0]);
	memset(&a->view[end - 1], 0, sizeof a->view[0]);
}

/* A game deleted from the card (src/gamedel.h, DESIGN.md 11.31): off its own
 * shelf and off Favorites, in place, with no read of the card. The shelf
 * stood on keeps its place and its cursor goes to the next game, or the one
 * before at the end (gd_cursor_after); a shelf left empty goes, as an empty
 * system always has (hide_empty_systems), and the home is shown on the
 * system that took its place - or, with no games left on the card, the empty
 * home. Favorites keeps the game its cursor was on.
 *
 * In place rather than rescan_all, which is a read of every console's folder
 * for one file the launcher itself just deleted: on my card, 8,844 games on
 * exFAT, that is most of a second (lib_scan_listing). */
void shelf_drop_game(app *a, const char *tag, const char *file)
{
	char stand[sizeof a->sys.systems[0].tag], folder[CFG_STR] = "";
	group_keep fav, rec;
	coll_keep col;
	shots_keep shot;
	int was_sys, si, k, i;

	if (a->sys.count <= 0) return;
	snprintf(stand, sizeof stand, "%s", a->sys.systems[a->sys_cursor].tag);
	was_sys = a->sys_cursor;
	/* Every index from here on can name another game */
	free_all_textures(a);
	SDL_AtomicIncRef(&g_lib_gen);       /* a check that read the card before is moot */

	group_note(a, HOMESYS_FAV, &fav);
	group_note(a, HOMESYS_RECENT, &rec);
	coll_note(a, &col);
	shots_note(a, &shot);
	drop_shots_shelf(a);
	drop_collections_shelf(a);
	drop_recents_shelf(a);
	drop_favorites_shelf(a);

	/* Its own system, on the home or off it (a game of one turned off is
	 * deleted from Favorites or Recents) */
	for (si = 0; si < sys_kept(a); si++)
		if (!strcmp(a->sys.systems[si].tag, tag)) break;
	if (si < sys_kept(a) && a->view[si].all &&
	    (k = lib_find_entry(a->view[si].all, NULL, 0, file)) >= 0) {
		/* A shelf with folders: off every game of the system, and the
		 * folder shown made again, its cursor on the entry that took the
		 * game's place, or up a folder when that one emptied */
		sysview *v = &a->view[si];
		game_list *all = v->all;
		int at = v->cursor;

		snprintf(folder, sizeof folder, "%s", a->sys.systems[si].folder);
		memmove(&all->items[k], &all->items[k + 1],
		        (size_t)(all->count - k - 1) * sizeof *all->items);
		all->count--;
		shelf_show_dir(v, v->dir);
		sort_shelf(a, si);
		v->cursor = at < v->list.count ? at : v->list.count > 0 ? v->list.count - 1 : 0;
		fprintf(stderr, "delete: %s %s off its shelf, %d game%s left\n", tag, file,
		        all->count, all->count == 1 ? "" : "s");
		saved_follow(a, folder);
		if (all->count == 0) {
			fprintf(stderr, "delete: %s has no games left, off the home\n", tag);
			drop_system(a, si);
		}
	} else if (si < sys_kept(a) && (k = lib_find_entry(&a->view[si].list, NULL, 0, file)) >= 0) {
		sysview *v = &a->view[si];
		size_t rest = (size_t)(v->list.count - k - 1);

		snprintf(folder, sizeof folder, "%s", a->sys.systems[si].folder);
		memmove(&v->list.items[k], &v->list.items[k + 1], rest * sizeof *v->list.items);
		if (v->tex) memmove(&v->tex[k], &v->tex[k + 1], rest * sizeof *v->tex);
		if (v->tw)  memmove(&v->tw[k],  &v->tw[k + 1],  rest * sizeof *v->tw);
		if (v->th)  memmove(&v->th[k],  &v->th[k + 1],  rest * sizeof *v->th);
		if (v->cb)  memmove(&v->cb[k],  &v->cb[k + 1],  rest * sizeof *v->cb);
		v->list.count--;
		/* Its cursor: on the game, it is now on the next one; past the
		 * end, on the one before */
		if (v->cursor > k) v->cursor--;
		if (v->cursor >= v->list.count) v->cursor = v->list.count > 0 ? v->list.count - 1 : 0;
		fprintf(stderr, "delete: %s %s off its shelf, %d game%s left\n", tag, file,
		        v->list.count, v->list.count == 1 ? "" : "s");
		saved_follow(a, folder);
		if (v->list.count == 0) {
			fprintf(stderr, "delete: %s has no games left, off the home\n", tag);
			drop_system(a, si);
		}
	}
	/* The groups again, each on the game it was on, or where it was when
	 * that was the game deleted */
	build_favorites_shelf(a);
	build_recents_shelf(a);
	build_collections_shelf(a);
	/* its shots stay, with no game (a picture is the player's) */
	build_shots_shelf(a);
	group_put_back(a, HOMESYS_FAV, &fav);
	group_put_back(a, HOMESYS_RECENT, &rec);
	coll_put_back(a, &col);
	shots_put_back(a, &shot);

	/* Back where the player stood. On the game's own shelf its cursor was
	 * the game, and moved above to the next one or the one before at the
	 * end (gd_cursor_after's rule); on Favorites the same, above */
	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, stand)) break;
	if (i < a->sys.count) {
		sysview *v = &a->view[i];

		a->sys_cursor = i;
		cf_reset(&v->cf, v->cursor);
		games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	} else {
		/* The shelf stood on emptied and went: the home, on the system now
		 * in its place */
		a->sys_cursor = was_sys < a->sys.count ? was_sys : a->sys.count - 1;
		if (a->sys_cursor < 0) a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	if (a->sys.count > 0) a->tint = a->sys.systems[a->sys_cursor].accent;
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Rename (src/gamern.h, DESIGN.md 11.41): the game's entry under its new
 * file and name, in place, as shelf_drop_game takes one off - no read of the
 * card. Its title is the library's for the new file, which a rename empties
 * (db_game_move), so the new name tidied (lib_title). Its shelf put in
 * order again, its cursor still on it where it was on it; Favorites made
 * again from the shelves; the saved library told. */
bool shelf_rename_game(app *a, const char *tag, const char *file, const char *new_file,
                       const char *new_name)
{
	char folder[CFG_STR], was[LIB_PATH] = "";
	int si, k, at;
	sysview *v;
	game_entry *e;

	/* On the home or off it: a game of a system turned off is renamed from
	 * Favorites or Recents */
	for (si = 0; si < sys_kept(a); si++)
		if (!a->view[si].owner && !strcmp(a->sys.systems[si].tag, tag)) break;
	if (si >= sys_kept(a)) return false;
	v = &a->view[si];
	/* Every game of the system (sys_games), and on a shelf with folders the
	 * folder it shows too, if the game is in it: two copies of one entry,
	 * since `list` is made from `all` (src/shelfdir.h). The folder of games
	 * stays the game's, so it stays in the folder shown. */
	if ((k = lib_find_entry(sys_games(v), NULL, 0, file)) < 0) return false;
	snprintf(folder, sizeof folder, "%s", a->sys.systems[si].folder);
	k = lib_find_entry(&v->list, NULL, 0, file);
	if (v->cursor >= 0 && v->cursor < v->list.count)
		snprintf(was, sizeof was, "%s", v->cursor == k ? new_file : v->list.items[v->cursor].file);
	for (int c = 0; c < 2; c++) {
		game_list *gl = c ? &v->list : v->all;

		if (!gl || (k = lib_find_entry(gl, NULL, 0, file)) < 0) continue;
		e = &gl->items[k];
		snprintf(e->file, sizeof e->file, "%s", new_file);
		snprintf(e->name, sizeof e->name, "%s", new_name);
		lib_display_title(db_lib(), folder, e->file, e->name, e->title, sizeof e->title);
	}
	/* Every index from here on can name another game */
	free_all_textures(a);
	SDL_AtomicIncRef(&g_lib_gen);       /* a check that read the card before is moot */
	sort_shelf(a, si);
	if (was[0] && (at = lib_find_entry(&v->list, NULL, 0, was)) >= 0) v->cursor = at;
	cf_reset(&v->cf, v->cursor);
	fprintf(stderr, "rename: %s %s is %s on its shelf\n", tag, file, new_file);
	saved_follow(a, folder);
	refresh_favorites_shelf(a);
	/* Its sessions moved with it (src/gamern.c), so Recents has it again */
	refresh_recents_shelf(a);
	/* And its lines in the collections (src/collect.h) */
	refresh_collections_shelf(a);
	/* Its shots keep their names, with no game now (DESIGN.md 11.68) */
	refresh_shots_shelf(a);
	return true;
}

bool lib_check_apply(app *a, lib_check *c)
{
	saved_lib pre;

	if (!c->behind) {
		fprintf(stderr, "saved library: the card is as the shelves show it (%d shelves read in %u ms)\n",
		        c->n, c->ms);
		return false;
	}
	if (c->gen != SDL_AtomicGet(&g_lib_gen)) return false;      /* read again since */
	fprintf(stderr, "saved library: the card changed (%s), the shelves read again\n", c->changed);
	/* A folder the check chose again, for the systems.cfg rebuild reads */
	for (int k = 0; k < c->n; k++)
		if (c->moved[k]) lib_folder_set(c->home[k], c->fresh[k].folder);
	pre.shelves = c->fresh;
	pre.count = c->n;
	rebuild(a, &pre, true);
	if (a->sys.count > 0) {
		const sysview *v = &a->view[a->sys_cursor];

		fprintf(stderr, "saved library: still on %s, %s\n", a->sys.systems[a->sys_cursor].tag,
		        v->list.count > 0 ? v->list.items[v->cursor].file : "no game");
	}
	return true;
}

void lib_check_free(lib_check *c)
{
	if (!c) return;
	for (int k = 0; k < c->n; k++) {
		lib_free(&c->fresh[k].list);
		es_import_free(&c->got[k]);
	}
	free(c);
}

void lib_check_import(lib_check *c)
{
	unsigned t0 = plat_now_ms();
	int rows = 0;

	for (int k = 0; c && k < c->n; k++)
		if (c->fresh[k].list.count > 0)
			rows += es_import(P_ROMS, c->fresh[k].folder, &c->fresh[k].list, &c->got[k]);
	if (rows)
		fprintf(stderr, "gamelist.xml: details of %d game%s into the library (%u ms)\n", rows,
		        rows == 1 ? "" : "s", plat_now_ms() - t0);
}

void lib_check_imported(lib_check *c,
                        void (*fn)(const char *folder, const es_import_list *l, void *ctx), void *ctx)
{
	for (int k = 0; c && k < c->n; k++)
		if (c->got[k].n) fn(c->fresh[k].folder, &c->got[k], ctx);
}
