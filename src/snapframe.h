/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SNAPFRAME_H
#define ZYPEROS_SNAPFRAME_H

/* A game's video snap, on its way from the Muse daemon to the shelf.
 *
 * The same shared frames as MP4's video (src/mp4frame.h: three slots, a
 * seqlock, YUV 4:2:0) in a file of its own, because the two play at once:
 * a snap on the shelf must not stop the music Muse is playing, so it has its
 * own thread and its own pictures (src/muse/snap.c), and the launcher reads
 * them beside MP4's (src/snapview.c).
 *
 * The file is made at MPF_SIZE but in /tmp, which is RAM: only the pages a
 * picture is written to are ever there. A snap is small (ScreenScraper's
 * video-normalized), so three of its pictures are a few hundred kilobytes. */

/* A check builds its own reader against a file of its own */
#ifndef SNAP_FRAMES
#define SNAP_FRAMES "/tmp/muse-snap"
#endif

#endif
