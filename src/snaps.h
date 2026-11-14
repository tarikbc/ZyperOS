/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SNAPS_H
#define ZYPEROS_SNAPS_H

#include <stdbool.h>
#include <stddef.h>

#include "design.h"

/* Video snaps: a few seconds of a game's own screen, from ScreenScraper
 * (its small "video-normalized" file), kept beside the cover as
 * Roms/<console>/.media/<name>.mp4 and played on the shelf in the cover's
 * place after the game has had the focus for SNAPS_WAIT_MS.
 *
 * What is here is the settings and the files: whether a run fetches them,
 * whether the shelf plays them, their sound, which consoles have them on,
 * and the room they take. The settings are the device's (db_dev), like the
 * rest of MENU. MENU > Scraper > Video Snaps is their page (DESIGN.md 11.19;
 * My picks in the Scraper lab, 2026-11-06). No SDL, so the checks link
 * it. */

/* DESIGN.md 8.2's hold-snap */
#define SNAPS_WAIT_MS DS_MS_HOLD_SNAP
/* Halfway through that wait, the snap is asked for held: its file opened and
 * its first picture ready, so the cover turns the moment the bar is full.
 * 2026-11-07: "when the video bar finishes loading, it still takes
 * half a second to show the video. We should load the video as the bar fills
 * up, or like 500ms after it starts filling up, to avoid affecting scrolling
 * performance". Not at once: a shelf being scrolled asks for nothing. */
#define SNAPS_ASK_MS (SNAPS_WAIT_MS / 2)

bool snaps_fetch(void);           /* 1.0.4's Fetch with Box Art: read by snaps_get */
void snaps_set_fetch(bool on);
bool snaps_play(void);            /* the shelf plays them: on to start */
void snaps_set_play(bool on);
int  snaps_sound(void);           /* SNAP_MUTED (to start), SNAP_QUIET, SNAP_FULL */
void snaps_set_sound(int s);
bool snaps_console_on(const char *folder);    /* on to start */
void snaps_set_console(const char *folder, bool on);

/* <roms>/<folder>/.media/<stem>.mp4. False if it would be cut short. */
bool snaps_path(const char *roms, const char *folder, const char *stem,
                char *out, size_t n);

/* The room a console's snaps take, and taking it back: every .mp4 in its
 * .media folder. Delete answers the bytes it gave back. */
unsigned long long snaps_bytes(const char *roms, const char *folder);
unsigned long long snaps_delete(const char *roms, const char *folder);

/* ---- which systems get videos, and for which games (2026-11-13) ----------
 *
 * MENU > Scraper's Systems grid switches a system's videos: whether a run
 * gets them for it. Off to start, because each video is about 2.5 MB of
 * the card and one ScreenScraper lookup (Tarik: "Is it getting video for
 * all my roms? What is 1.4gb?"). Getting only: whether the shelf plays
 * the videos a system has is snaps_play and snaps_console_on, as before,
 * so videos already on the card play as they did.
 *
 * A system with no switch of its own follows what 1.0.4 did: Fetch with
 * Box Art on, and the console not turned off. So a card that was getting
 * videos still gets them, and one that never did gets none. */
bool snaps_get(const char *folder);
void snaps_set_get(const char *folder, bool on);

/* Which games of a system with videos on get one: all, the favorites, or
 * the games played at least once. All to start. */
enum { SNAPS_FOR_ALL, SNAPS_FOR_FAVORITES, SNAPS_FOR_PLAYED, SNAPS_FOR_COUNT };
int  snaps_for(void);
void snaps_set_for(int which);
/* The rule, pure: whether a game gets a video under `which` */
bool snaps_for_game(int which, bool favorite, bool played);

/* And whether a run gets a system's covers (MENU > Scraper's Systems, the
 * grid's other mark; artscrape.c's art_covers_rule): on to start. Kept here
 * beside the videos' switch, the same kind of setting for the same page. */
bool snaps_covers(const char *folder);
void snaps_set_covers(const char *folder, bool on);

/* The stems of a system's videos, sorted (strcmp), for a count without a
 * stat a game: *n of them, freed with snaps_stems_free. NULL with *n 0 for
 * none. */
char **snaps_stems(const char *roms, const char *folder, int *n);
void snaps_stems_free(char **v, int n);

#endif
