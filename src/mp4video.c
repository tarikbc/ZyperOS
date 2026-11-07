/* SPDX-License-Identifier: GPL-3.0-only */
/* See mp4video.h. */
#include "mp4video.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mp4frame.h"
#include "platform.h"

static mpf_head    *g_head;          /* the frames, mapped read-only */
static unsigned     g_tried_ms;      /* the last look for the file */
static SDL_Texture *g_tex;
static int          g_tw, g_th;      /* g_tex's size */
static uint32_t     g_seen;          /* the serial of the picture in g_tex */
static uint32_t     g_file;          /* the file it came from */
static uint32_t     g_color = MPF_BT601;
static bool         g_test;          /* mp4v_test_picture's, not the daemon's */

/* The daemon makes the file when it starts and never removes it, so once
 * mapped it stays right; until then it is looked for twice a second at most. */
static bool mapped(void)
{
	void *p;
	int fd;

	if (g_head) return true;
	if (g_tried_ms && plat_now_ms() - g_tried_ms < 500) return false;
	g_tried_ms = plat_now_ms();
	if ((fd = open(MPF_PATH, O_RDONLY | O_CLOEXEC)) < 0) return false;
	p = mmap(NULL, MPF_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) return false;
	g_head = p;
	return true;
}

bool mp4v_update(SDL_Renderer *r)
{
	const mpf_slot *sl;
	const uint8_t *y;
	uint32_t seq, w, h, cw;
	int s;

	if (!mapped() || (s = mpf_peek(g_head, g_seen, &seq)) < 0) return false;
	sl = &g_head->slot[s];
	w = sl->w;
	h = sl->h;
	if (!g_tex || g_tw != (int)w || g_th != (int)h) {
		if (g_tex) SDL_DestroyTexture(g_tex);
		g_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		                          (int)w, (int)h);
		if (!g_tex) {
			fprintf(stderr, "mp4: no %ux%u video texture: %s\n", w, h, SDL_GetError());
			g_seen = sl->serial;          /* not tried again every pass */
			return false;
		}
		SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_NONE);
		g_tw = (int)w;
		g_th = (int)h;
	}
	cw = mpf_chroma(w);
	y = mpf_slot_data(g_head, s);
	/* Straight from the shared pages to the GPU, no copy of our own. A
	 * picture overwritten while it went (mpf_still_good) is taken again on
	 * the next pass; the texture shows it for one frame at most. */
	SDL_UpdateYUVTexture(g_tex, NULL, y, (int)w,
	                     y + (size_t)w * h, (int)cw,
	                     y + (size_t)w * h + (size_t)cw * mpf_chroma(h), (int)cw);
	g_file = sl->file;
	g_color = sl->color;
	if (!mpf_still_good(g_head, s, seq)) return true;
	g_seen = sl->serial;
	return true;
}

SDL_Texture *mp4v_texture(void)
{
	if (g_test) return g_tex;
	if (!g_tex || !g_head || g_file != g_head->file) return NULL;
	/* The matrix the GPU's YUV shader uses, which SDL reads as it draws:
	 * set for this picture each time it is asked for. */
	SDL_SetYUVConversionMode(g_color == MPF_FULL  ? SDL_YUV_CONVERSION_JPEG
	                       : g_color == MPF_BT709 ? SDL_YUV_CONVERSION_BT709
	                                              : SDL_YUV_CONVERSION_BT601);
	return g_tex;
}

SDL_Rect mp4v_fit(SDL_Rect box)
{
	double aspect, k;
	SDL_Rect out = box;

	if (!g_tex || g_th <= 0) return box;
	aspect = (double)g_tw / g_th;
	if (g_head && g_head->sar_num > 0 && g_head->sar_den > 0)
		aspect *= (double)g_head->sar_num / g_head->sar_den;
	k = (double)box.w / box.h;
	if (aspect >= k) {
		out.w = box.w;
		out.h = (int)(box.w / aspect + 0.5);
	} else {
		out.h = box.h;
		out.w = (int)(box.h * aspect + 0.5);
	}
	out.x = box.x + (box.w - out.w) / 2;
	out.y = box.y + (box.h - out.h) / 2;
	return out;
}

/* The texture given back, for a renderer about to be torn down. */
static void mp4v_release(void)
{
	if (g_tex) SDL_DestroyTexture(g_tex);
	g_tex = NULL;
	g_tw = g_th = 0;
	g_seen = 0;
}

void mp4v_test_picture(SDL_Renderer *r, int w, int h)
{
	/* The bars in YUV (BT.601 studio range): white, yellow, cyan, green,
	 * magenta, red, blue, and black. */
	static const uint8_t BARS[8][3] = {
		{ 235, 128, 128 }, { 210, 16, 146 }, { 170, 166, 16 }, { 145, 54, 34 },
		{ 106, 202, 222 }, { 81, 90, 240 }, { 41, 240, 110 }, { 16, 128, 128 },
	};
	int cw = (w + 1) / 2, ch = (h + 1) / 2, x, y;
	uint8_t *yp = SDL_malloc((size_t)w * h), *up = SDL_malloc((size_t)cw * ch),
	        *vp = SDL_malloc((size_t)cw * ch);

	if (!yp || !up || !vp) goto done;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			yp[y * w + x] = y < h * 2 / 3 ? BARS[x * 8 / w][0] : (uint8_t)(16 + 219 * x / w);
	for (y = 0; y < ch; y++)
		for (x = 0; x < cw; x++) {
			bool bar = y < ch * 2 / 3;

			up[y * cw + x] = bar ? BARS[x * 8 / cw][1] : 128;
			vp[y * cw + x] = bar ? BARS[x * 8 / cw][2] : 128;
		}
	mp4v_release();
	g_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!g_tex) goto done;
	SDL_UpdateYUVTexture(g_tex, NULL, yp, w, up, cw, vp, cw);
	g_tw = w;
	g_th = h;
	g_color = MPF_BT601;
	g_test = true;
done:
	SDL_free(yp);
	SDL_free(up);
	SDL_free(vp);
}
