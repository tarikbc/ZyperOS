/* SPDX-License-Identifier: GPL-3.0-only */
/* A video picture's colors through a render target, with SDL's GLES2 renderer:
 *
 *   tools/yuvtarget-check.sh    (in Docker, against the device's own SDL)
 *
 * On the Pixel 2 every video snap came out with red and blue swapped, because
 * SDL's GLES2 renderer swaps the two inside an ARGB8888 target for RGB
 * textures and draw colors but not for a YUV texture (src/platform.h,
 * PLAT_TARGET_FORMAT). This draws what the shelf draws - an IYUV picture with
 * BT.601 bars, blended and faded as a snap is (games.c), beside an ARGB8888
 * texture and a fill of the same colors - into a target of PLAT_TARGET_FORMAT,
 * that onto the window as the Pixel 2 shows its screen, and reads the window
 * back: every bar must come out its own color. The same into an ARGB8888
 * target is printed too, not judged: it shows whether SDL still swaps, which
 * is what PLAT_TARGET_FORMAT is for. tools/targets-check.py holds the
 * launcher's targets to PLAT_TARGET_FORMAT. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform.h"

#define BAR 16
#define NBARS 3

static const struct {
	const char *name;
	Uint8 y, u, v;     /* BT.601 studio range */
	Uint32 rgb;
} BARS[NBARS] = {
	{ "red",   81,  90, 240, 0xFF0000 },
	{ "green", 145, 54,  34, 0x00FF00 },
	{ "blue",  41, 240, 110, 0x0000FF },
};

static int failures;

static int close_to(Uint32 got, Uint32 want)
{
	int s;

	for (s = 0; s < 24; s += 8) {
		int d = (int)((got >> s) & 0xFF) - (int)((want >> s) & 0xFF);

		if (d < -6 || d > 6) return 0;
	}
	return 1;
}

/* Bars: YUV on the top row of cells, ARGB8888 on the second, fills on the
 * third, each `BAR` square; read back as ARGB8888 (0xRRGGBB) */
static void draw(SDL_Renderer *r, Uint32 tfmt, Uint32 out[3][NBARS])
{
	const int w = NBARS * BAR, h = 3 * BAR;
	Uint8 yp[NBARS * BAR * BAR], up[NBARS * BAR * BAR / 4], vp[NBARS * BAR * BAR / 4];
	Uint32 argb[NBARS * BAR * BAR], *px = malloc((size_t)w * h * 4);
	SDL_Texture *yuv = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
	                                     NBARS * BAR, BAR);
	SDL_Texture *rgb = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
	                                     NBARS * BAR, BAR);
	SDL_Texture *t = SDL_CreateTexture(r, tfmt, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Rect row = { 0, 0, w, BAR };
	int x, y, b;

	if (!px || !yuv || !rgb || !t) {
		printf("  FAIL textures: %s\n", SDL_GetError());
		failures++;
		exit(1);
	}
	for (y = 0; y < BAR; y++)
		for (x = 0; x < NBARS * BAR; x++) {
			b = x / BAR;
			yp[y * NBARS * BAR + x] = BARS[b].y;
			argb[y * NBARS * BAR + x] = 0xFF000000u | BARS[b].rgb;
			if (y < BAR / 2 && x < NBARS * BAR / 2) {
				up[y * NBARS * BAR / 2 + x] = BARS[x * 2 / BAR].u;
				vp[y * NBARS * BAR / 2 + x] = BARS[x * 2 / BAR].v;
			}
		}
	SDL_UpdateYUVTexture(yuv, NULL, yp, NBARS * BAR, up, NBARS * BAR / 2, vp, NBARS * BAR / 2);
	SDL_UpdateTexture(rgb, NULL, argb, NBARS * BAR * 4);
	SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT601);
	/* As the shelf draws a snap: blended, its alpha mod set and put back */
	SDL_SetTextureBlendMode(yuv, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);

	SDL_SetRenderTarget(r, t);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_SetTextureAlphaMod(yuv, 255);
	SDL_RenderCopy(r, yuv, NULL, &row);
	row.y += BAR;
	SDL_RenderCopy(r, rgb, NULL, &row);
	for (b = 0; b < NBARS; b++) {
		SDL_Rect cell = { b * BAR, 2 * BAR, BAR, BAR };

		SDL_SetRenderDrawColor(r, BARS[b].rgb >> 16, (BARS[b].rgb >> 8) & 0xFF,
		                       BARS[b].rgb & 0xFF, 255);
		SDL_RenderFillRect(r, &cell);
	}
	/* The screen onto the window, as pixel2.c's show_screen does */
	SDL_SetRenderTarget(r, NULL);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_RenderCopy(r, t, NULL, &(SDL_Rect){ 0, 0, w, h });
	if (SDL_RenderReadPixels(r, &(SDL_Rect){ 0, 0, w, h }, SDL_PIXELFORMAT_ARGB8888, px, w * 4)) {
		printf("  FAIL read back: %s\n", SDL_GetError());
		failures++;
	}
	for (y = 0; y < 3; y++)
		for (b = 0; b < NBARS; b++)
			out[y][b] = px[(y * BAR + BAR / 2) * w + b * BAR + BAR / 2] & 0xFFFFFF;
	SDL_DestroyTexture(t);
	SDL_DestroyTexture(rgb);
	SDL_DestroyTexture(yuv);
	free(px);
}

int main(void)
{
	static const char *const KIND[3] = { "IYUV", "ARGB8888 texture", "fill" };
	SDL_Window *w;
	SDL_Renderer *r;
	SDL_RendererInfo ri;
	SDL_version v;
	Uint32 got[3][NBARS];
	int k, b;

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("FAIL video: %s\n", SDL_GetError());
		return 1;
	}
	w = SDL_CreateWindow("yuvtarget", 0, 0, NBARS * BAR, 3 * BAR, SDL_WINDOW_OPENGL);
	r = w ? SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED) : NULL;
	if (!r || SDL_GetRendererInfo(r, &ri) != 0 || strcmp(ri.name, "opengles2") != 0) {
		printf("FAIL no opengles2 renderer: %s\n", r ? ri.name : SDL_GetError());
		return 1;
	}
	SDL_GetVersion(&v);
	printf("SDL %d.%d.%d, %s renderer, %s video\n", v.major, v.minor, v.patch, ri.name,
	       SDL_GetCurrentVideoDriver());

	draw(r, PLAT_TARGET_FORMAT, got);
	printf("into a %s target (PLAT_TARGET_FORMAT):\n", SDL_GetPixelFormatName(PLAT_TARGET_FORMAT));
	for (k = 0; k < 3; k++)
		for (b = 0; b < NBARS; b++) {
			int ok = close_to(got[k][b], BARS[b].rgb);

			printf("  %s %-16s %-5s #%06x%s\n", ok ? "ok  " : "FAIL", KIND[k], BARS[b].name,
			       got[k][b], ok ? "" : " (its own color expected)");
			failures += !ok;
		}

	draw(r, SDL_PIXELFORMAT_ARGB8888, got);
	printf("into an SDL_PIXELFORMAT_ARGB8888 target (not judged; what the launcher drew in before):\n");
	for (b = 0; b < NBARS; b++)
		printf("       %-16s %-5s #%06x%s\n", KIND[0], BARS[b].name, got[0][b],
		       close_to(got[0][b], BARS[b].rgb) ? "" : "  <- swapped by SDL");

	SDL_Quit();
	if (failures) printf("%d FAILED\n", failures);
	else printf("ok: a video's colors survive a PLAT_TARGET_FORMAT target\n");
	return failures != 0;
}
