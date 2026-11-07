/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_MP4VIDEO_H
#define ZYPEROS_MP4VIDEO_H

#include <stdbool.h>

#include <SDL.h>

/* MP4's video, the launcher's half: the newest picture the Muse daemon put in
 * the shared frames (src/mp4frame.h, written by src/muse/vid.c), as a YUV
 * texture the GPU turns into color as it draws it.
 *
 * Polled from the player's loop, never waited on. The daemon decides when
 * each picture is due - it holds the sound's clock - so all this does is take
 * the newest one whenever there is a newer one, which is also why it needs no
 * clock of its own. */

/* A picture newer than the last one taken, uploaded: true when the texture
 * changed and the screen wants presenting. */
bool mp4v_update(SDL_Renderer *r);

/* The picture to draw, or NULL when there is none yet for the file playing -
 * the daemon has opened it and not decoded its first frame, or it is another
 * file's picture still sitting in the frames. */
SDL_Texture *mp4v_texture(void);

/* The largest rectangle inside `box` with the picture's shape - its pixels'
 * own shape counted, for the anamorphic DVD rips that store 720x480 for a
 * 16:9 picture - centered in it. `box` itself when there is no picture. */
SDL_Rect mp4v_fit(SDL_Rect box);

/* A test picture in place of the daemon's, `w` x `h`: color bars over a
 * gradient, for a host shot of the player (--video), which has no daemon. */
void mp4v_test_picture(SDL_Renderer *r, int w, int h);

#endif
