/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SNAPSHELF_H
#define ZYPEROS_SNAPSHELF_H

#include <stdbool.h>
#include <stdint.h>

/* When the shelf's focused cover turns into its video snap, and back.
 *
 * My picks in the Scraper lab, 2026-11-06 (DESIGN.md 11.19): after a
 * game has had the focus for SNAPS_WAIT_MS (3 s), its snap is asked for; when
 * its first picture is there, the cover turns into it over **turn** (300 ms),
 * widening to the snap's own shape; it loops. When the focus moves the snap
 * is gone at once - the card is already sliding away - and when a menu opens
 * over the shelf, or the screen goes dark, it goes back to the cover over
 * **quick** (120 ms). Either way the daemon is told to stop that same frame,
 * so nothing is decoded that nobody sees.
 *
 * Pure: the caller says what is focused and whether the shelf is in front,
 * this says what to ask the daemon for and how far the cover has turned.
 * tools/snapshelf-check.c holds the timing. */

typedef enum { SNS_IDLE, SNS_WAIT, SNS_ASKED, SNS_IN, SNS_PLAY, SNS_OUT } sns_phase;

typedef struct {
	sns_phase phase;
	char      key[1024];     /* the focused game's snap, "" for none */
	uint32_t  t;             /* when the phase began */
	float     from;          /* how far it had turned when it began to go back */
} snapshelf;

/* One frame. `path` is the focused game's snap file, or "" when it has none
 * (or snaps are off, or the console's are). `front` says the shelf is what
 * is on screen, still, lit and with nothing over it. `picture` says the snap
 * asked for has its first picture. Returns the snap to ask the daemon for,
 * "" for none, and puts in *k how far the cover has turned into it, 0 to 1,
 * eased. */
const char *snapshelf_step(snapshelf *s, const char *path, bool front,
                           bool picture, uint32_t now, float *k);

/* Whether the cover is turning one way or the other: a frame is wanted. */
bool snapshelf_moving(const snapshelf *s);

#endif
