/* SPDX-License-Identifier: GPL-3.0-only */
/* snapsub.dylib: a game's video snap picture in place of the host build's test
 * picture, for a --snap shot of the launcher (build-native/zyperos).
 *
 * The host build has no Muse to decode a snap, so `--snap K` turns the cover
 * into a test picture (src/mp4video.c, mp4v_test_picture), a 320x240 IYUV
 * texture filled with SDL_UpdateYUVTexture. Loaded with DYLD_INSERT_LIBRARIES,
 * this hands that call the planes of SNAP_I420 instead: a raw 320x240 yuv420p
 * picture (ffmpeg -pix_fmt yuv420p -f rawvideo), one frame of the game's snap.
 * Everything else the launcher draws, the turn from the cover to the snap
 * included, is its own. Nothing else is changed.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define DYLD_INTERPOSE(rep, orig)                                              \
	__attribute__((used)) static struct {                                  \
		const void *replacement, *replacee;                            \
	} interpose_##orig __attribute__((section("__DATA,__interpose"))) = {  \
		(const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig };

enum { W = 320, H = 240 };

static int sub_UpdateYUVTexture(SDL_Texture *t, const SDL_Rect *rect, const Uint8 *y, int yp,
                                const Uint8 *u, int up, const Uint8 *v, int vp)
{
	static Uint8 *pic;
	static int tried;
	const char *path = getenv("SNAP_I420");
	int w = 0, h = 0;

	if (path && !rect && SDL_QueryTexture(t, NULL, NULL, &w, &h) == 0 && w == W && h == H && yp == W) {
		if (!tried) {
			FILE *f = fopen(path, "rb");
			tried = 1;
			pic = malloc(W * H * 3 / 2);
			if (!f || fread(pic, 1, W * H * 3 / 2, f) != W * H * 3 / 2) {
				fprintf(stderr, "snapsub: cannot read %s\n", path);
				free(pic);
				pic = NULL;
			}
			if (f) fclose(f);
		}
		if (pic) return SDL_UpdateYUVTexture(t, rect, pic, W, pic + W * H, W / 2, pic + W * H * 5 / 4, W / 2);
	}
	return SDL_UpdateYUVTexture(t, rect, y, yp, u, up, v, vp);
}

DYLD_INTERPOSE(sub_UpdateYUVTexture, SDL_UpdateYUVTexture)
