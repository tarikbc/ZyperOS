/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SGDBRUN_H
#define ZYPEROS_SGDBRUN_H

#include <stdbool.h>

/* ---- one game from SteamGridDB, without blocking a frame -----------------
 *
 * Three requests, one after another on net.c's one async slot, each started
 * or polled by a step and never waited for, because the loop that steps this
 * is the loop that reads the power button:
 *
 *   search   /search/autocomplete/<title>, the game by sgdb_title's name,
 *            taken only at SGDB_MATCH or better (sgdb_pick_game)
 *   grids    /grids/game/<id>, portrait 600x900, static, PNG or JPEG
 *   image    the first grid, from their CDN, beside the cover as
 *            <dest>.sgdb and renamed over it only once it has arrived whole
 *            and starts as a picture - so a replace that finds nothing, or
 *            an answer that is a page rather than an image, keeps the cover
 *            that was there
 *
 * then art_shrink, down to the size the shelf draws (512 on the long side)
 * and to PNG, as the computer tool saves them.
 *
 * sgdb_run_step returns 1 while it works, 0 when the cover is in place, and -1
 * when there is none to be had. After a -1, the three questions below say
 * whether it was this game or something that will be true of the next one too.
 *
 * RATE. Their API asks clients to be gentle and answers 429 when they are not.
 * One request at a time is already gentle; requests to the API are also kept
 * SGDB_GAP_MS apart, and the run (artscrape.c) waits and asks once more after
 * a 429, and stops asking after a second. */
bool sgdb_run_begin(const char *stem, const char *dest);
int  sgdb_run_step(void);
void sgdb_run_cancel(void);

/* Why the last -1, when it was not simply "they have no cover for it":
 *
 *   too_many   429: not now. Worth one more try after a wait.
 *   refused    401 or 403: the key. No later game will do better, and the
 *              key is marked SGDB_REFUSED for the menu to say.
 *   no_answer  nothing answered, or their server failed (5xx). */
bool sgdb_run_too_many(void);
bool sgdb_run_refused(void);
bool sgdb_run_no_answer(void);

#endif
