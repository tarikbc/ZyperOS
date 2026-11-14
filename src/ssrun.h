/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SSRUN_H
#define ZYPEROS_SSRUN_H

#include <stdbool.h>

#include "ssfetch.h"

/* ---- one game, without blocking a frame ---------------------------------
 *
 * The same work as ss_lookup, driven a step at a time, because the loop that
 * would wait for it is the loop that reads the power button - and a
 * ScreenScraper call measured 5.5 seconds per game. A handheld that stops
 * answering its own power button for five seconds is not slow, it is broken.
 *
 * One at a time: there is a single async slot in net.c, and this needs it for
 * the lookup and then again for the cover.
 *
 * ss_run_step returns 1 while it is working, 0 when it has finished, and -1
 * when it could not - no account, no answer, a name that did not survive the
 * check, or no cover on offer. -1 is the ordinary case for a game they do not
 * have, and it is the caller's signal to fall through to libretro. Valid
 * metadata is stored before the cover request, even when there is no cover
 * or the download later fails or is canceled. */
bool ss_run_begin(const char *folder, const char *file, const char *stem,
                  const char *rom_dir, const char *exts);
/* Same validation and CRC retry, but no image request. Step returns 0 only
 * when metadata was saved, even when the reply contains no cover. Existing
 * images are never opened for writing. Requires an open library database. */
bool ss_run_begin_metadata(const char *folder, const char *file, const char *stem,
                           const char *rom_dir, const char *exts);
/* The lookup alone, for a game's Cover and Video page (DESIGN.md 11.34):
 * the same validation, retry and text saved, then done (0) with the reply
 * kept for ss_run_result - what pictures and video are on offer - and no
 * file fetched. */
bool ss_run_begin_lookup(const char *folder, const char *file, const char *stem,
                         const char *rom_dir, const char *exts);
/* That reply once the step has said 0, else NULL. Gone at ss_run_cancel. */
const ss_result *ss_run_result(void);
int  ss_run_step(void);
void ss_run_cancel(void);

/* Where it got to, for a screen to say. Never a URL: that carries the
 * account. */
const char *ss_run_where(void);

/* ---- video snaps (src/snaps.h) -------------------------------------------
 *
 * Whether a console's games get their snap with their cover: a question the
 * launcher answers (its settings live where this file does not reach), set
 * once at start. Unset, never. With it, a lookup that found the game also
 * fetches its "video-normalized" file to <rom_dir>/.media/<stem>.mp4 when
 * there is none - beside the cover and through the same check, renamed into
 * place only when it starts as an MP4 does, so a refusal's page never stands
 * where the shelf would play it. A metadata-only lookup (a game with a cover
 * already) fetches it too: that is how snaps reach a library that has its
 * covers. The step's answer stays the cover's or the text's; the snap is
 * extra, and ss_run_got_video says whether it came. */
/* Asked per game, by its system's folder and its launch path: a system's
 * switch and which of its games (src/snaps.h, snaps_get and snaps_for) */
void ss_run_videos(bool (*want)(const char *folder, const char *file));
bool ss_run_video_wanted(const char *folder, const char *file);
bool ss_run_got_video(void);

/* ---- what a long run needs to know when to stop -------------------------
 *
 * Requests left on the account today, or -1 until a reply has said. Every
 * reply carries the account's own counters, so this costs nothing and is the
 * only number that is actually true - the day's 20,000 is shared with any
 * other device signed in as the same person, and with the same card scraped
 * twice.
 *
 * Survives ss_run_cancel: it is a fact about the account, not about the game
 * that was being looked up. */
int ss_run_left(void);

/* Whether the last refusal was "not now" rather than "not this game": HTTP
 * 429. The two arrive identically otherwise - curl runs with `fail`, so every
 * refusal is one exit code and no body - and they want opposite responses. A
 * 429 is worth waiting a beat and asking again; a 404 means they do not have
 * this game and asking again would be rude and useless. */
bool ss_run_too_many(void);

/* Whether the last lookup's refusal is true of every game after it, so a run
 * stops asking (src/artscrape.c) and libretro does the rest. From the status,
 * as ScreenScraper's api page lists them (read 2026-11-13):
 *
 *   SS_SPENT      430 "your scrape quota is spent for today", or 431 "sort
 *                 your ROM files and come back tomorrow" (the day's unknown
 *                 games are spent): nothing more until tomorrow
 *   SS_CLOSED     401 "closed to non-members and inactive members", the
 *                 server full, or 423 "fully closed"
 *   SS_NO_ANSWER  no status at all (no network, the wait ran out, TLS) or a
 *                 server error, 5xx: one is a blip, two in a row are the
 *                 run's, as SteamGridDB's are (src/sgdbrun.h)
 *   SS_UNKNOWN    they do not know this game: a 404, a reply with no game in
 *                 it, or one whose name fails the check after its retry.
 *                 About this game alone; a run remembers it for a while
 *                 (db_ss_miss_set) and counts it toward a console's run of
 *                 misses (src/artscrape.c)
 *   SS_GO_ON      anything else, also about this game alone
 *
 * Before this, each of these was "a game they do not have", so a run asked
 * every game on a shelf, a second or two each and sixty with no answer, all
 * refused, before libretro had its turn: minutes with no art on the screen.
 * 429 stays ss_run_too_many's. Survives ss_run_cancel, as that does. */
typedef enum { SS_GO_ON, SS_SPENT, SS_CLOSED, SS_NO_ANSWER, SS_UNKNOWN } ss_refusal;
ss_refusal ss_run_refusal(void);

#endif
