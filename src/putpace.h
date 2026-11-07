/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PUTPACE_H
#define ZYPEROS_PUTPACE_H

/* How many finished pictures one frame puts on screen (src/texload.h's
 * texload_deliver), in plain C with the clock handed in, so
 * tools/putpace-check.c holds the rules without a window or a GPU.
 *
 * The frame thread turns each finished picture into a texture: an upload,
 * 0.04 ms for a 512 x 656 card on an M3 and several times that on the Pixel
 * 2's Mali G31. Landing on a shelf, or a fast scroll, finishes a dozen at
 * once, and all of them in one frame was a frame the shelf hitched on. So a
 * frame takes pictures while they fit in a share of it, and the rest wait for
 * the next frames.
 *
 * Whether the next one fits is a guess at what it will cost, made before it
 * is taken: the cost of a megapixel, learned from the ones put on screen so
 * far, times its pixels. By pixels because the pictures are of every size -
 * a home screen still is 128 x 128 and a system's atlas up to a megapixel -
 * and a guess by the picture would be the still's when the atlas came. The
 * guess goes up at once when one costs more than it said, and comes down an
 * eighth of the way when one costs less: a slow upload is believed at once,
 * a fast one only after a few. Nothing is guessed before the first has been
 * timed; the first of every frame goes in whatever the guess, which is what
 * makes a frame always put something on screen, however slow the device.
 *
 * So a frame spends no more than its share, give or take how far the last
 * guess was out, and when one picture alone costs more than the share, that
 * one picture: an upload cannot be cut in two. */

#include <stdbool.h>

/* Every picture counts as at least this many pixels: the call that makes a
 * texture costs something whatever its size, and a 128 x 128 still taken at
 * its pixels alone would teach a cost a megapixel never comes near. */
#define PUTPACE_LEAST_PX 65536L

typedef struct {
	float share;     /* ms of a frame pictures may take */
	float per_mpx;   /* ms a megapixel has taken; 0 before the first */
	float spent;     /* ms this frame, so far */
	int   put;       /* pictures this frame, so far */
} putpace;

/* A pace with `share` ms of each frame, knowing nothing yet. */
void putpace_init(putpace *p, float share);

/* A frame begins: nothing spent, nothing put. What has been learned is kept. */
void putpace_frame(putpace *p);

/* What a picture of `px` pixels is guessed to cost, in ms. 0 before the first
 * has been timed, and for px 0, which is no picture: the frame then makes the
 * card itself, a cost no upload says anything about. */
float putpace_guess(const putpace *p, long px);

/* Whether a picture of `px` pixels may go in this frame: the first of a frame
 * always may, the rest while what is spent and what it is guessed to cost
 * stay inside the share. */
bool putpace_room(const putpace *p, long px);

/* One went in: `px` pixels (0 for none) in `ms`. */
void putpace_took(putpace *p, long px, float ms);

#endif
