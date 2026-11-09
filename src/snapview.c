/* SPDX-License-Identifier: GPL-3.0-only */
/* See snapview.h. A small copy of mp4video.c's reading, for a second set of
 * frames; MP4's own reader is left exactly as it is. */
#include "snapview.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mp4frame.h"
#include "platform.h"
#include "snapframe.h"

static mpf_head    *g_head;
static unsigned     g_tried_ms;
static SDL_Texture *g_tex;
static int          g_tw, g_th;      /* the picture's own size */
static int          g_xw, g_xh;      /* the texture's: mpf_tex_size's */
static uint32_t     g_seen, g_file, g_from;
static uint32_t     g_color = MPF_BT601;

static bool mapped(void)
{
	void *p;
	int fd;

	if (g_head) return true;
	if (g_tried_ms && plat_now_ms() - g_tried_ms < 500) return false;
	g_tried_ms = plat_now_ms();
	if ((fd = open(SNAP_FRAMES, O_RDONLY | O_CLOEXEC)) < 0) return false;
	p = mmap(NULL, MPF_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) return false;
	g_head = p;
	return true;
}

void snapv_begin(void)
{
	g_from = mapped() ? __atomic_load_n(&g_head->file, __ATOMIC_ACQUIRE) : 0;
	g_file = g_from;
}

bool snapv_pending(void)
{
	uint32_t seq;
	int s;

	if (!mapped() || (s = mpf_peek(g_head, g_seen, &seq)) < 0) return false;
	return g_head->slot[s].file != g_from;
}

/* The whole texture black (studio range), so what is past a smaller picture
 * is black and never a texel of an old one at its edge */
static void clear_texture(void)
{
	size_t cw = mpf_chroma((uint32_t)g_xw), ch = mpf_chroma((uint32_t)g_xh);
	uint8_t *y = malloc((size_t)g_xw * g_xh + 2 * cw * ch);

	if (!y) return;
	memset(y, 16, (size_t)g_xw * g_xh);
	memset(y + (size_t)g_xw * g_xh, 128, 2 * cw * ch);
	SDL_UpdateYUVTexture(g_tex, NULL, y, g_xw, y + (size_t)g_xw * g_xh, (int)cw,
	                     y + (size_t)g_xw * g_xh + cw * ch, (int)cw);
	free(y);
}

bool snapv_update(SDL_Renderer *r)
{
	const mpf_slot *sl;
	const uint8_t *y;
	uint32_t seq, w, h, cw;
	int s;

	if (!mapped() || (s = mpf_peek(g_head, g_seen, &seq)) < 0) return false;
	sl = &g_head->slot[s];
	/* Not the snap asked for yet: the daemon has not opened it */
	if (sl->file == g_from) { g_seen = sl->serial; return false; }
	w = sl->w;
	h = sl->h;
	if (!g_tex || g_tw != (int)w || g_th != (int)h) {
		uint32_t xw, xh;

		mpf_tex_size(w, h, &xw, &xh);
		/* A texture of the size the GPU takes (mpf_tex_size), kept while the
		 * pictures still fit it: a snap smaller than 640x480 is drawn from
		 * its corner of one that size */
		if (!g_tex || g_xw != (int)xw || g_xh != (int)xh) {
			if (g_tex) SDL_DestroyTexture(g_tex);
			g_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
			                          (int)xw, (int)xh);
			if (!g_tex) {
				fprintf(stderr, "snap: no %ux%u texture: %s\n", xw, xh, SDL_GetError());
				g_seen = sl->serial;
				g_tw = g_th = g_xw = g_xh = 0;
				return false;
			}
			SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_BLEND);
			g_xw = (int)xw;
			g_xh = (int)xh;
		}
		if (xw != w || xh != h) clear_texture();
		g_tw = (int)w;
		g_th = (int)h;
	}
	cw = mpf_chroma(w);
	y = mpf_slot_data(g_head, s);
	{
		SDL_Rect at = { 0, 0, (int)w, (int)h };

		SDL_UpdateYUVTexture(g_tex, &at, y, (int)w,
		                     y + (size_t)w * h, (int)cw,
		                     y + (size_t)w * h + (size_t)cw * mpf_chroma(h), (int)cw);
	}
	g_file = sl->file;
	g_color = sl->color;
	if (!mpf_still_good(g_head, s, seq)) return true;
	g_seen = sl->serial;
	return true;
}

SDL_Texture *snapv_texture(int *w, int *h, SDL_Rect *src)
{
	double aspect;

	if (!g_tex || !g_head || g_file == g_from || g_file != g_head->file ||
	    !__atomic_load_n(&g_head->active, __ATOMIC_ACQUIRE))
		return NULL;
	SDL_SetYUVConversionMode(g_color == MPF_FULL ? SDL_YUV_CONVERSION_JPEG
	                                             : SDL_YUV_CONVERSION_BT601);
	aspect = (double)g_tw / g_th;
	if (g_head->sar_num > 0 && g_head->sar_den > 0)
		aspect *= (double)g_head->sar_num / g_head->sar_den;
	if (w) *w = (int)(g_th * aspect + 0.5);
	if (h) *h = g_th;
	if (src) *src = (SDL_Rect){ 0, 0, g_tw, g_th };
	return g_tex;
}

void snapv_release(void)
{
	if (g_tex) SDL_DestroyTexture(g_tex);
	g_tex = NULL;
	g_tw = g_th = g_xw = g_xh = 0;
}
