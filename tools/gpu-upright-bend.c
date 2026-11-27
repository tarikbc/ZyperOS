/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's CRT Curved, the right way up on the TrimUI Brick's way of
 * drawing it, for make check-gpu-upright (tools/gpu-upright-check.py): SDL's
 * OpenGL ES 2 renderer, the Brick's, on SDL's offscreen video and Mesa's
 * llvmpipe. ZyperOS's frame drawn into a target texture the screen's size,
 * as src/device/brick.c draws it into bend_tex while the curve is on, then
 * bent onto the window as its plat_screen_finish bends it (sfx_draw_curved,
 * then the mask), and the window read back top row first. The picture's
 * top-left quarter is white and the rest black: it must stay top left, the
 * other three far darker. Exits 77 where SDL's renderer is not OpenGL ES 2's. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform.h"
#include "../src/screenfx.h"
#include "../src/screenfx_draw.h"

/* What src/screenfx_draw.c asks of the device: the old screens are drawn, as
 * on a Brick whose profile has gpu_shaders */
bool plat_gpu_shaders(void) { return true; }

#define W ZYPEROS_SCREEN_W
#define H ZYPEROS_SCREEN_H

/* The canvas's width is the run's (src/canvas.h), so the read-back is
 * sized for the widest */
static uint8_t g_px[CANVAS_W_MAX * CANVAS_H * 4];

/* The mean light of a box of the window */
static double light(int x0, int y0, int x1, int y1)
{
	double sum = 0;
	int x, y, n = 0;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++, n++) {
			const uint8_t *p = g_px + ((size_t)y * W + (size_t)x) * 4;
			sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
		}
	return n ? sum / n : 0;
}

int main(void)
{
	SDL_Rect all = { 0, 0, W, H }, quarter = { 0, 0, W / 2, H / 2 };
	SDL_RendererInfo info;
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Texture *bend_tex;
	double q[4];
	int i;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("skipped: no SDL video (%s)\n", SDL_GetError());
		return 77;
	}
	win = SDL_CreateWindow("bend", 0, 0, W, H, 0);
	ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
	if (!ren || SDL_GetRendererInfo(ren, &info) != 0 || strcmp(info.name, "opengles2")) {
		printf("skipped: no OpenGL ES 2 renderer under SDL's offscreen video (%s)\n",
		       ren ? info.name : SDL_GetError());
		return 77;
	}
	plat_screen_shader("crt-curved", 100);

	/* src/device/brick.c's screen_target while the curve is on */
	bend_tex = SDL_CreateTexture(ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET, W, H);
	if (!bend_tex) {
		printf("FAIL bend: no target texture (%s)\n", SDL_GetError());
		return 1;
	}
	SDL_SetTextureBlendMode(bend_tex, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(ren, bend_tex);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
	SDL_RenderFillRect(ren, &quarter);

	/* Its plat_screen_finish, the frame in bend_tex */
	SDL_SetRenderTarget(ren, NULL);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	sfx_draw_curved(ren, bend_tex, &all, 0.0);
	sfx_draw_mask(ren, W, H, &all, 0.0);
	if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA32, g_px, W * 4) != 0) {
		printf("FAIL bend: the window could not be read (%s)\n", SDL_GetError());
		return 1;
	}

	for (i = 0; i < 4; i++) {
		const int cx = (i % 2 ? 3 : 1) * W / 4, cy = (i / 2 ? 3 : 1) * H / 4;
		q[i] = light(cx - W / 16, cy - H / 16, cx + W / 16, cy + H / 16);
	}
	if (!(q[0] > 100 && q[1] < q[0] / 2 && q[2] < q[0] / 2 && q[3] < q[0] / 2)) {
		printf("FAIL bend: CRT Curved on the launcher's screens: the bright quarter is not top "
		       "left (light top left %.0f, top right %.0f, bottom left %.0f, bottom right %.0f)\n",
		       q[0], q[1], q[2], q[3]);
		return 1;
	}
	printf("ok bend: the launcher's CRT Curved, drawn from a target texture as the Brick's is, "
	       "the right way up on SDL's OpenGL ES 2 renderer\n");
	SDL_DestroyTexture(bend_tex);
	sfx_forget();
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
