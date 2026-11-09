/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ARTSRC_H
#define ZYPEROS_ARTSRC_H

#include <stdbool.h>
#include <stddef.h>

/* Where a game's cover came from: which source, what kind of picture, which
 * region, and for SteamGridDB who drew it. Kept in the card's library
 * database (db_lib), beside the scrape's text, so a game's Cover and Video
 * page (DESIGN.md 11.34) can credit the cover on the shelf and mark the
 * offer that is the same picture.
 *
 * Two records a game: the cover on the shelf, and the one Use as Cover put
 * aside in .media/.before (the page's Before, which Put Back swaps back).
 * Keyed by the console's folder and the game's name as its cover is named
 * (.media/<name>.png), "artsrc.<folder>/<name>" and "artbefore.<folder>/<name>".
 * A cover added by hand, or fetched before this was kept, has none: the page
 * then credits nothing rather than guessing.
 *
 * The value is one line, "<source>|<kind>|<region>|<by>", every part plain
 * text without a "|": source ss, lr or sg; kind box, box3d, title, shot or
 * fan; region ScreenScraper's code ("us", "jp") or empty; by the artist's
 * name, or libretro's name for the picture, or empty. */

typedef enum { AS_NONE = -1, AS_LR, AS_SS, AS_SG, AS_SOURCES } as_source;
typedef enum { AK_BOX, AK_BOX3D, AK_TITLE, AK_SHOT, AK_FAN, AK_KINDS } as_kind;

typedef struct {
	as_source src;
	as_kind   kind;
	char      region[8];
	char      by[200];         /* as long as a libretro name (artscrape.c's NAME_MAX_) */
} art_source;

/* Pure, for tools/media-check.c */
bool artsrc_format(const art_source *s, char *out, size_t n);
bool artsrc_parse(const char *v, art_source *out);
/* The same picture, as far as a record can say: source, kind, region and by */
bool artsrc_same(const art_source *a, const art_source *b);
/* "ScreenScraper", "libretro", "SteamGridDB": a product's name, never
 * translated */
const char *artsrc_name(as_source s);
bool artsrc_key(const char *prefix, const char *folder, const char *name, char *out, size_t n);

/* The records, in db_lib. False when there is no library or no record. */
bool artsrc_get(const char *folder, const char *name, art_source *out);
bool artsrc_set(const char *folder, const char *name, const art_source *s);
bool artsrc_get_before(const char *folder, const char *name, art_source *out);
/* The shelf's record becomes Before's and Before's the shelf's, either
 * missing as it is: Use as Cover and Put Back move the files the same way */
void artsrc_swap(const char *folder, const char *name);
/* Both records gone: the game was deleted */
void artsrc_forget(const char *folder, const char *name);

#endif
