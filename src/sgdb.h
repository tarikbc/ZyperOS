/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SGDB_H
#define ZYPEROS_SGDB_H

#include <stdbool.h>
#include <stddef.h>

/* SteamGridDB, the third source of box art, on the device.
 *
 * It was a computer's only (tools/scrape-sgdb.py), and it is the same thing
 * here: the covers libretro and ScreenScraper have none of - homebrew, hacks,
 * ports, games nobody has filed - from SteamGridDB's portrait "grids", which
 * are box-shaped (600x900), which is what the shelf draws. Asked LAST, after
 * both others have missed, the order the computer tool runs in.
 *
 * THE KEY BELONGS TO WHOEVER HOLDS THE DEVICE. SteamGridDB hands one out free
 * to any account (steamgriddb.com, Preferences, API) and every request carries
 * it, so it is typed on the keyboard or pasted into the File Transfer page and
 * kept in the per-device database, 0600, beside the other accounts. It is a
 * credential in every sense the code knows: never in the repository, never in
 * a log line, never in a URL (it travels as a request header, which net.c
 * writes into a 0600 curl config), and `--dump` shows its length rather than
 * its value (db.c's secret_key, by the ".key" in its name). Shown, it is the
 * last four characters and nothing more.
 *
 * Everything here that can be pure is, because the parts worth getting wrong
 * cannot be seen on a device: which of SteamGridDB's names is the game a file
 * is named after, and which of the images on offer is a cover the shelf can
 * draw. tools/sgdb-check.c holds both to written replies, and
 * tools/sgdb-check.py holds the matching rule to the computer tool's own. */

#define SGDB_API "https://www.steamgriddb.com/api/v2"

/* Keys are 32 letters and digits; room to spare, so a longer one is not cut
 * short into a key that is merely wrong. */
#define SGDB_KEY_MAX 64

/* How close a name must be to count as the game: the computer tool's 0.85.
 * SteamGridDB searches by name across every platform, so a looser rule hands
 * a homebrew game the cover of the commercial game it is named after. */
#define SGDB_MATCH 0.85

/* What is known about the key that is kept. */
typedef enum {
	SGDB_NONE,        /* no key */
	SGDB_UNCHECKED,   /* kept without an answer: checked on the next run */
	SGDB_WORKS,       /* SteamGridDB answered to it */
	SGDB_REFUSED,     /* SteamGridDB refused it (401 or 403) */
} sgdb_state;

/* ---- the key -------------------------------------------------------------- */

/* Read it from the per-device database. True when a usable key is kept. */
bool sgdb_key_load(void);
bool sgdb_have_key(void);
sgdb_state sgdb_key_state(void);

/* What was typed or pasted, made into a key: surrounding white space and a
 * pasted "Bearer " dropped, then letters and digits only, 16 to
 * SGDB_KEY_MAX - 1 of them. False, with `out` empty, for anything else. */
bool sgdb_key_clean(const char *in, char *out, size_t n);

/* Keep a key, already cleaned, with what is known about it; forget it. */
bool sgdb_key_keep(const char *key, sgdb_state st);
void sgdb_key_forget(void);

/* A run's verdict on the kept key (a 401 is SGDB_REFUSED, a 200 SGDB_WORKS),
 * kept so the menu can say it. Nothing when there is no key or no change. */
void sgdb_key_mark(sgdb_state st);

/* "••••" and the key's last four characters, or "" with no key. The only
 * form in which the key is ever shown, on the device or on the web page. */
void sgdb_key_masked(char *out, size_t n);
void sgdb_key_tail(char *out, size_t n);

/* "Authorization: Bearer <key>", for net.c's header. A credential: the caller
 * wipes it once the request has started. False with no key. */
bool sgdb_auth_header(char *out, size_t n);

/* The key itself, for the one caller that needs the value rather than a
 * request: the log pack's list of strings to mask, in case one ever reached
 * a log. NULL with no key. */
const char *sgdb_secret(void);

/* ---- checking a key once, when it is entered ---------------------------
 *
 * In a thread of its own, because the two places a key is entered both have a
 * frame loop to keep going - the keyboard's wait panel, where the power button
 * is read, and File Transfer, which is serving a page while it checks.
 *
 *   sgdb_try_begin  cleans what was typed and starts the check; BAD for
 *                   something that is not a key, BUSY when a check is already
 *                   running, RUNNING when it has started
 *   sgdb_try_poll   from the main thread, every frame or every request. When
 *                   the check is over it settles, once: a key SteamGridDB
 *                   answered to is kept as SGDB_WORKS (WORKS); one that got
 *                   no answer at all - no network, or the USB link, which
 *                   has no internet - is kept as SGDB_UNCHECKED, for the
 *                   next Box Art run to check (KEPT); one they refused is not
 *                   kept (REFUSED). It goes on returning that outcome until
 *                   the next begin, so a page polling for it cannot miss it.
 *   sgdb_try_said   the sentence for the outcome, for a message or a page. */
typedef enum {
	SGDB_TRY_IDLE,
	SGDB_TRY_RUNNING,
	SGDB_TRY_WORKS,
	SGDB_TRY_KEPT,
	SGDB_TRY_REFUSED,
	SGDB_TRY_BAD,
	SGDB_TRY_BUSY,
} sgdb_try;

sgdb_try    sgdb_try_begin(const char *typed);
sgdb_try    sgdb_try_poll(void);
const char *sgdb_try_said(void);

/* ---- matching, pure ------------------------------------------------------ */

/* What to search for, as the computer tool's title() has it: a pack's
 * numbering ("036 - ") and every [..] and (..) tag taken off, CamelCase split
 * when there is no space at all, _ and - as spaces. Numbering is kept when it
 * is the whole title ("2048 (homebrew)"), in both, since a search for nothing
 * finds nothing. */
void   sgdb_title(const char *stem, char *out, size_t n);

/* The tool's close(): the ratio of the two titles after art_norm. */
double sgdb_close(const char *want, const char *name);

/* The best game in a /search/autocomplete reply for `want` - the first of
 * those that score highest, as Python's max() takes it. True only when it
 * reaches SGDB_MATCH; `score` and `name` say what the best was either way,
 * for the log. */
bool   sgdb_pick_game(const char *json, size_t len, const char *want,
                      long *id, char *name, size_t nn, double *score);

/* The first grid in a /grids/game reply that this device can draw: PNG or
 * JPEG (the card's SDL_image has no WebP), and an https:// address. */
bool   sgdb_pick_grid(const char *json, size_t len, char *url, size_t n);

/* Every cover on offer in a grids reply, in its order, up to `max`: PNG or
 * JPEG, each with its small copy ("thumb", or the picture itself when it has
 * none) and who drew it, for a game's Cover and Video page (DESIGN.md
 * 11.34). Pure. */
typedef struct {
	long id;
	char url[512];
	char thumb[512];
	char author[64];
} sgdb_grid;
int sgdb_list_grids(const char *json, size_t len, sgdb_grid *out, int max);

/* The two API addresses. Neither carries the key. False if cut short. */
bool   sgdb_search_url(const char *want, char *out, size_t n);
bool   sgdb_grids_url(long id, char *out, size_t n);

#endif
