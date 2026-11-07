/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef MUSE_SNAP_H
#define MUSE_SNAP_H

#include <stdbool.h>

/* A game's video snap, for the shelf: one short clip, muted, looping.
 *
 * Beside MP4's video (vid.c) and never through it, because the two are
 * different things playing at the same time. MP4's picture follows its
 * sound's clock; a snap has no sound to follow and must not touch the one
 * Muse is playing, so it runs on the wall clock in a thread of its own, at
 * a lower priority than both the sound and MP4's pictures, into frames of
 * its own (src/snapframe.h).
 *
 * Decoded only while asked for: the launcher asks after a game has had the
 * focus for a few seconds and stops it the moment the focus moves, a menu
 * opens or the screen goes dark, and the daemon stops it too when the
 * launcher goes away. Nothing is decoded ahead but the first picture, which
 * the launcher asks for held (below) while its wait bar fills. */

bool snap_init(void);
void snap_quit(void);

/* Play `path` from its start, looping; NULL or "" stops. A second call with
 * the file already playing changes nothing. `vol` is its sound, 0 to 100:
 * 0 is muted (MENU > Scraper > Video Snaps > Sound, muted to start), and then
 * nothing of the sound is even decoded. Its sound goes to ALSA's "default",
 * the system's mix, beside whatever else is playing; the launcher sends 0
 * while Muse plays, so a snap never talks over music somebody chose.
 *
 * `hold` opens it and puts out its first picture, then waits there with no
 * clock and no sound: the launcher asks for it held halfway through the
 * shelf's wait (2026-11-07: the picture took half a second after the
 * bar was full), and the same path with `hold` false starts it from that
 * picture the moment the bar is. Only the hold changing keeps the file open;
 * a new path or volume starts again. */
void snap_play(const char *path, int vol, bool hold);

#endif
