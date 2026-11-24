/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SORT_H
#define ZYPEROS_SORT_H

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "favorites.h"
#include "i18n.h"
#include "library.h"
#include "stats.h"

/* What order a shelf is in.
 *
 * PER SYSTEM, not global. Alphabetical is the only sane way through 261
 * Genesis ROMs, and the worst way through the six-game shelf you are actually
 * working your way down - the two shelves want different answers and there is
 * no reason they should have to agree. Stored as `sort.<TAG>`, keyed on the
 * tag for the reason `display.<TAG>` is: renaming a folder or reordering
 * systems.cfg must not hand a system somebody else's setting.
 *
 * EVERY ORDER FALLS BACK TO DISPLAY TITLE, then filename for equal titles.
 * Two games with no play time, or copied onto the card in the same second,
 * must not trade places between two draws of the
 * same shelf - a list that reshuffles when nothing changed reads as broken
 * even when the top of it is right. The fallback is what makes the order a
 * function of the data rather than of whatever qsort felt like.
 *
 * Descending on the three numeric orders, which puts a zero at the bottom for
 * free: a game with no sessions has nothing to say about play time, and the
 * bottom is where "nothing to say" belongs. No special case needed for it.
 *
 * Favorites First is not an order but a split on top of one (sort_favorites_first):
 * the shelf's favorites ahead of its other games, each part in the order chosen.
 *
 * What is NOT here, and why: times launched, which tracks play time closely
 * enough that it would be a second copy of the same shelf. Publisher, year, genre and
 * region are not here because nothing on the card knows them - the box art
 * scrapers key on filename rather than a catalog, so those would mean
 * carrying metadata with no source. */
typedef struct {
	const char *name;   /* what the setting stores */
	const char *label;  /* what the menu row shows, in English */
	str_id      text;   /* and in the language that is on: sort_label */
} sort_order;

static const sort_order SORTS[] = {
	{ "name",   "Name",           S_SORT_NAME           },
	{ "played", "Play Time",      S_SYSMENU_PLAY_TIME   },
	{ "recent", "Last Played",    S_SORT_LAST_PLAYED    },
	{ "added",  "Recently Added", S_SORT_RECENTLY_ADDED },
	/* A collection's own two (DESIGN.md 11.56): Added, its list's order,
	 * the order its games were put in it, and System, by the home's order
	 * of the systems, A to Z in each. No games shelf offers them. */
	{ "list",   "Added",          S_SORT_ADDED          },
	{ "system", "System",         S_SORT_SYSTEM         },
};
#define SORT_COUNT ((int)(sizeof SORTS / sizeof SORTS[0]))
/* The orders a games shelf steps round: the first four */
#define SORT_SHELF  4
#define SORT_LIST   4
#define SORT_SYSTEM 5
/* Collections' (src/collect.h), in their row's order: Added first, its
 * default, as a list reads; then Name, Last Played and System */
static const int SORT_COLL_ORDERS[] = { SORT_LIST, 0, 2, SORT_SYSTEM };
#define SORT_COLL ((int)(sizeof SORT_COLL_ORDERS / sizeof SORT_COLL_ORDERS[0]))
/* Screenshots' (DESIGN.md 11.68): Recently Added first, the newest shot,
 * as it ships; then Name and System, each game's newest first
 * (src/shots_screen.c's shots_sort) */
static const int SORT_SHOTS_ORDERS[] = { 3, 0, SORT_SYSTEM };
#define SORT_SHOTS ((int)(sizeof SORT_SHOTS_ORDERS / sizeof SORT_SHOTS_ORDERS[0]))
/* Which orders a shelf's Sort By steps round: a games shelf's, a
 * collection's or Screenshots' (sort_choices' `set`; true is a
 * collection's, as it was a flag) */
enum { SORT_SET_SHELF, SORT_SET_COLL, SORT_SET_SHOTS };

/* Order i's label in the language that is on, what the row shows; past the
 * ends, by name's */
static inline const char *sort_label(int i)
{
	return T(SORTS[i >= 0 && i < SORT_COUNT ? i : 0].text);
}

static inline int sort_index(const char *name)
{
	int i;

	if (name)
		for (i = 0; i < SORT_COUNT; i++)
			if (!strcmp(SORTS[i].name, name)) return i;
	return 0;                                   /* by name, as it always was */
}

/* How many orders the shelf's Sort By offers: set SORT_SET_*'s */
static inline int sort_choices(int set)
{
	return set == SORT_SET_SHOTS ? SORT_SHOTS : set == SORT_SET_COLL ? SORT_COLL : SORT_SHELF;
}

/* Its k-th, the order's index in SORTS */
static inline int sort_choice(int set, int k)
{
	int n = sort_choices(set);

	k = (k % n + n) % n;
	return set == SORT_SET_SHOTS ? SORT_SHOTS_ORDERS[k] : set == SORT_SET_COLL ? SORT_COLL_ORDERS[k] : k;
}

/* Where order `i` is in the shelf's row; its first when it offers none */
static inline int sort_choice_of(int set, int i)
{
	for (int k = 0; k < sort_choices(set); k++)
		if (sort_choice(set, k) == i) return k;
	return 0;
}

/* The order after `i` (`d` 1) or before it (-1), round the shelf's own */
static inline int sort_step_in(int set, int i, int d)
{
	return sort_choice(set, sort_choice_of(set, i) + d);
}

static inline int sort_step(int i, int d)
{
	return sort_step_in(false, i, d);
}

/* Whether an order needs the session rows read before it can be applied. */
static inline bool sort_needs_stats(int order)
{
	return order == 1 || order == 2;
}

/* Recently Added, the one order that reads an entry's added time, which a
 * scan leaves unknown (library.h): the shelf fills it before such a sort */
static inline bool sort_needs_added(int order)
{
	return order == 3;
}

static int sort_by_name(const void *pa, const void *pb)
{
	const game_entry *a = pa, *b = pb;

	return lib_name_cmp(a, b);
}

/* The three numeric orders, all shaped the same: bigger first, name breaks a
 * tie. Written out rather than folded into one comparator with a key function,
 * because qsort gives a comparator no way to carry which key it wants and a
 * file-static holding it would be a global that exists for one call. */
static int sort_by_played(const void *pa, const void *pb)
{
	const game_entry *a = pa, *b = pb;

	if (a->play_secs != b->play_secs)
		return a->play_secs < b->play_secs ? 1 : -1;
	return sort_by_name(pa, pb);
}

static int sort_by_recent(const void *pa, const void *pb)
{
	const game_entry *a = pa, *b = pb;

	if (a->last_played != b->last_played)
		return a->last_played < b->last_played ? 1 : -1;
	return sort_by_name(pa, pb);
}

static int sort_by_added(const void *pa, const void *pb)
{
	const game_entry *a = pa, *b = pb;

	if (a->added != b->added)
		return a->added < b->added ? 1 : -1;
	return sort_by_name(pa, pb);
}

/* A collection's Added: its list's order, which its shelf puts in `added`
 * (the game's line), first line first */
static int sort_by_list(const void *pa, const void *pb)
{
	const game_entry *a = pa, *b = pb;

	if (a->added != b->added)
		return a->added < b->added ? -1 : 1;
	return sort_by_name(pa, pb);
}

/* Put `n` entries in `order`.
 *
 * The play-time and last-played keys are read ONCE PER GAME into the entry
 * before sorting, rather than looked up from inside the comparator. stats
 * finds a row by walking its list, so a lookup is linear in the number of
 * sessions - paying that inside a comparator would multiply it by n log n for
 * no reason. One pass costs n lookups and the sort is then plain field
 * comparisons.
 *
 * This re-folds the session rows, which the play-time screen also uses. That
 * screen re-folds on entry under its own window, so neither reads the other's
 * answer - but they are one set of rows and not two. See stats_lookup. */
static inline void sort_apply(game_entry *v, int n, int order, const char *tag)
{
	int i;

	if (!v || n <= 0) return;
	if (order < 0 || order >= SORT_COUNT) order = 0;

	if (sort_needs_stats(order)) {
		stats_summarize(STATS_ALL, false, (long)time(NULL));
		for (i = 0; i < n; i++) {
			v[i].play_secs = 0;
			v[i].last_played = 0;
			stats_lookup(tag, v[i].file, &v[i].play_secs, &v[i].last_played);
		}
	}

	switch (order) {
	case 1:  qsort(v, (size_t)n, sizeof *v, sort_by_played); break;
	case 2:  qsort(v, (size_t)n, sizeof *v, sort_by_recent); break;
	case 3:  qsort(v, (size_t)n, sizeof *v, sort_by_added);  break;
	case SORT_LIST: qsort(v, (size_t)n, sizeof *v, sort_by_list); break;
	default: qsort(v, (size_t)n, sizeof *v, sort_by_name);   break;
	}
}

/* Favorites First (DESIGN.md 11.70): the favorites of system `tag` among the
 * `n` games moved ahead of the rest, the order inside each part kept, so a
 * shelf in any order is that order twice: its favorites, then its other games.
 * How many favorites now lead; 0 when there was no memory, and nothing moved.
 *
 * Whether a game is a favorite is asked once per game (fav_is walks its set)
 * before anything moves, never per comparison. The favorites are lifted out
 * into a buffer of their own, at most FAV_MAX of them; the rest close up
 * toward the end in place, from the back, so none is read after it is written
 * over; and the favorites go in at the front. */
static inline int sort_favorites_first(game_entry *v, int n, const char *tag)
{
	unsigned char *is;
	game_entry *fav;
	int i, k = 0, w = n;

	if (!v || n <= 0 || !tag) return 0;
	if (!(is = malloc((size_t)n))) return 0;
	for (i = 0; i < n; i++)
		if ((is[i] = !v[i].folder && fav_is(tag, v[i].file))) k++;
	if (k == 0 || k == n) { free(is); return k; }
	if (!(fav = malloc(sizeof *fav * (size_t)k))) { free(is); return 0; }
	for (i = 0, k = 0; i < n; i++)
		if (is[i]) fav[k++] = v[i];
	for (i = n - 1; i >= 0; i--)
		if (!is[i]) v[--w] = v[i];
	memcpy(v, fav, sizeof *fav * (size_t)k);
	free(fav);
	free(is);
	return k;
}

/* The same, for a shelf whose games come from several systems - Favorites.
 *
 * Two things differ, and getting either wrong is silent. A game's play time is
 * under ITS OWN system's tag, `tags[owner[i]]`, and never under the shelf's:
 * asked by the shelf's tag, every Favorites game has none, and the play-time
 * orders fall back to name without saying so. And `owner` runs parallel to the
 * games and is the only record of which core runs each one, so it has to move
 * with them: sorted apart, a game launches under another system's core.
 *
 * qsort moves elements, not pairs, so for the length of the sort each game
 * rides in a pair with its owner, the game first - which is what lets the
 * ordinary comparators read a pair as the game it starts with. False when
 * there was no memory, and nothing moved. */
struct sort_pair { game_entry e; int owner; };

/* A collection's System: by the home's order of the games' systems, which
 * is their owners' order on it, A to Z in each */
static int sort_by_system(const void *pa, const void *pb)
{
	const struct sort_pair *a = pa, *b = pb;

	if (a->owner != b->owner) return a->owner < b->owner ? -1 : 1;
	return sort_by_name(pa, pb);
}

static inline bool sort_apply_owned(game_entry *v, int *owner, int n, int order,
                                    const char *const *tags)
{
	struct sort_pair *p;
	int i;

	if (!v || !owner || n <= 0) return true;
	if (order < 0 || order >= SORT_COUNT) order = 0;

	if (sort_needs_stats(order)) {
		stats_summarize(STATS_ALL, false, (long)time(NULL));
		for (i = 0; i < n; i++) {
			v[i].play_secs = 0;
			v[i].last_played = 0;
			stats_lookup(tags[owner[i]], v[i].file,
			             &v[i].play_secs, &v[i].last_played);
		}
	}

	p = malloc(sizeof *p * (size_t)n);
	if (!p) return false;
	for (i = 0; i < n; i++) { p[i].e = v[i]; p[i].owner = owner[i]; }
	qsort(p, (size_t)n, sizeof *p,
	      order == 1 ? sort_by_played : order == 2 ? sort_by_recent
	    : order == 3 ? sort_by_added  : order == SORT_LIST ? sort_by_list
	    : order == SORT_SYSTEM ? sort_by_system : sort_by_name);
	for (i = 0; i < n; i++) { v[i] = p[i].e; owner[i] = p[i].owner; }
	free(p);
	return true;
}

#endif
