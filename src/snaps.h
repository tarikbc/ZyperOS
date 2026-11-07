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

bool snaps_fetch(void);           /* a Box Art run fetches them: off to start */
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

#endif
