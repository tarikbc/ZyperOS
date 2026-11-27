/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MP4D_AHEAD_H
#define MP4D_AHEAD_H

#include <stdbool.h>
#include <stdint.h>

/* The sound decoded ahead of the output: a ring of about half a second of
 * 48 kHz stereo between the player thread, which decodes into it, and a
 * writer thread of its own, which takes it out a period at a time and
 * writes it to the output (src/mp4d/pcm.c).
 *
 * Why: the Pixel 2's dmix keeps 43 ms for the card (four periods of 512
 * frames, board/px2's asound.conf) and the decoder wrote 21 ms at a time,
 * on a CPU that idles at 408 MHz; a chunk that took longer than the 20 ms
 * left was an underrun, heard on a USB DAC as a crackle (2026-11-24, on the
 * Discord). Now a slow chunk is covered by the half second already decoded,
 * and the writer, which only copies, is never late.
 *
 * Pure C and pthreads, no ALSA, so tools/ahead-check.c runs it on a
 * computer, against a stand-in for the output. */

#define AHEAD_FRAMES 24000   /* the ring: half a second at 48 kHz */
#define AHEAD_WRITE  512     /* the most the writer hands the output at once: a period */

/* The output, called on the writer thread with nothing of the ring locked:
 * every one of `n` frames written, or a negative number for an output that
 * will take no more (the writer stops writing until ahead_hold). */
typedef long (*ahead_sink)(const int16_t *frames, int n);

/* The writer thread, started once; false when it would not start */
bool ahead_start(ahead_sink sink);

/* `n` frames in, waiting while the ring is full. False when the output has
 * failed (ahead_failed) or the writer was never started. */
bool ahead_push(const int16_t *frames, int n);

/* Frames not yet handed to the output: the ring's, and the writer's in its
 * hand */
long ahead_frames(void);

/* The output failed, and ahead_push says false, until the next hold */
bool ahead_failed(void);

/* For the output's own work (an open, a close, a drop): waits for a write
 * in progress to end, keeps the writer from starting another, and throws
 * away what the ring holds, which was for the old position or output;
 * clears a failure. ahead_release lets the writer go on. */
void ahead_hold(void);
void ahead_release(void);

#endif
