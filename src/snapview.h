/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SNAPVIEW_H
#define ZYPEROS_SNAPVIEW_H

#include <stdbool.h>

#include <SDL.h>

/* A video snap's pictures, the launcher's half: the newest one the daemon
 * put in the snap's frames (src/snapframe.h, written by src/muse/snap.c), as
 * a YUV texture, the way src/mp4video.c reads MP4's. Apart from that file so
 * the two can be on screen at once and neither ever shows the other's.
 *
 * snapv_begin marks the moment a snap is asked for: only a picture of a file
 * the daemon opened after it counts, so the last game's last frame is never
 * taken for this game's first. */
void snapv_begin(void);

/* Whether a newer picture of the snap asked for is waiting: two atomic
 * reads, no copy, for a loop deciding whether a frame is worth drawing. */
bool snapv_pending(void);

/* A newer picture uploaded: true when the texture changed. */
bool snapv_update(SDL_Renderer *r);

/* The picture to draw, or NULL before the asked-for snap's first one. Its
 * size in `w` and `h`, its pixels' own shape counted. */
SDL_Texture *snapv_texture(int *w, int *h);

/* The texture given back: when the snap stops, and before the renderer goes. */
void snapv_release(void);

#endif
