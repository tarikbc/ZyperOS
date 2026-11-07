/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Search Games: what a typed query finds across every system's shelf.
 *
 * A game is found when every word typed is in its title - the scraped one
 * where there is one, else the cleaned file name - or in its file name, so
 * "zelda link" finds "The Legend of Zelda: A Link to the Past", and "usa" or
 * "rev 1" still find a dump by its tags. Case, accents and punctuation do not
 * count: "pokemon" finds "Pokémon", "ff7" finds nothing it should not.
 *
 * The best first: a title that starts with what was typed, then titles where
 * each word starts a word, then any title it is in, then the file name only;
 * ties by title. MENU > Search Games (main.c's search_screen) types the query
 * on the on-screen keyboard and shows the list; A starts the game.
 *
 * Pure, so tools/search-check.c holds it on a build machine. */
#ifndef ZYPEROS_SEARCH_H
#define ZYPEROS_SEARCH_H

#include <stddef.h>

/* The most results shown; a query that finds more says how many, so the
 * player types another letter rather than scroll through hundreds. */
#define SR_SHOWN 30

/* One game a search can find: its shelf and place there, and what to match */
typedef struct {
	const char *title;   /* what the shelf shows */
	const char *name;    /* the file name, without its extension */
	int shelf, game;
} sr_item;

/* `in` lower-cased, Latin accents taken off, everything that is not a letter
 * or a digit a single space, no space at either end. */
void sr_fold(const char *in, char *out, size_t n);

/* Every item `query` finds, best first, the first `max` of them into `out`.
 * Returns how many were found in all, which may be more than `max`; 0 for
 * a query with nothing in it to look for. */
int sr_search(const char *query, const sr_item *items, int n, sr_item *out, int max);

#endif
