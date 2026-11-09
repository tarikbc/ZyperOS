/* SPDX-License-Identifier: GPL-3.0-only */
/* The Cover and Video page's pictures, decoded off the frame: mediatex.h. */
#include "mediatex.h"

#include <stdio.h>
#include <string.h>
#include <SDL_image.h>

#define MT_SLOTS 24
#define MT_PATH  600

typedef struct {
	int  key, px;
	char path[MT_PATH];
	bool used;
} ask;

typedef struct {
	int          key;
	unsigned     gen;
	SDL_Surface *s;
	bool         used;
} done;

static SDL_Thread *g_thread;
static SDL_mutex  *g_lock;
static SDL_cond   *g_wake;
static bool        g_run;
static unsigned    g_gen;
static ask         g_ask[MT_SLOTS];
static done        g_done[MT_SLOTS];

SDL_Surface *mt_load(const char *path, int px)
{
	SDL_Surface *raw = IMG_Load(path), *src, *dst;
	double k, sx, sy;
	int tw, th, x, y;

	if (!raw) return NULL;
	src = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!src) return NULL;
	if (px <= 0 || (src->w <= px && src->h <= px)) return src;
	k  = (double)px / (src->w > src->h ? src->w : src->h);
	tw = (int)(src->w * k + 0.5); if (tw < 1) tw = 1;
	th = (int)(src->h * k + 0.5); if (th < 1) th = 1;
	if (!(dst = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_ARGB8888))) {
		SDL_FreeSurface(src);
		return NULL;
	}
	/* A box average, as artshrink.c's: text and hard edges stay readable */
	sx = (double)src->w / tw;
	sy = (double)src->h / th;
	for (y = 0; y < th; y++) {
		Uint32 *drow = (Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch);
		int y0 = (int)(y * sy), y1 = (int)((y + 1) * sy);

		if (y1 <= y0) y1 = y0 + 1;
		if (y1 > src->h) y1 = src->h;
		for (x = 0; x < tw; x++) {
			unsigned a = 0, rr = 0, gg = 0, bb = 0, n = 0;
			int x0 = (int)(x * sx), x1 = (int)((x + 1) * sx), xx, yy;

			if (x1 <= x0) x1 = x0 + 1;
			if (x1 > src->w) x1 = src->w;
			for (yy = y0; yy < y1; yy++) {
				const Uint32 *srow = (const Uint32 *)((Uint8 *)src->pixels + yy * src->pitch);

				for (xx = x0; xx < x1; xx++) {
					Uint32 p = srow[xx];

					a += (p >> 24) & 0xFF; rr += (p >> 16) & 0xFF;
					gg += (p >> 8) & 0xFF; bb += p & 0xFF;
					n++;
				}
			}
			drow[x] = n ? ((a / n) << 24 | (rr / n) << 16 | (gg / n) << 8 | (bb / n)) : 0;
		}
	}
	SDL_FreeSurface(src);
	return dst;
}

static int worker(void *unused)
{
	(void)unused;
	SDL_LockMutex(g_lock);
	while (g_run) {
		ask a = { 0 };
		unsigned gen;
		SDL_Surface *s;
		int i;

		for (i = 0; i < MT_SLOTS && !a.used; i++)
			if (g_ask[i].used) { a = g_ask[i]; g_ask[i].used = false; }
		if (!a.used) {
			SDL_CondWait(g_wake, g_lock);
			continue;
		}
		gen = g_gen;
		SDL_UnlockMutex(g_lock);
		s = mt_load(a.path, a.px);
		SDL_LockMutex(g_lock);
		/* Asked before the page closed: not this page's */
		if (gen != g_gen) { SDL_FreeSurface(s); continue; }
		for (i = 0; i < MT_SLOTS; i++)
			if (!g_done[i].used) {
				g_done[i] = (done){ a.key, gen, s, true };
				break;
			}
		if (i == MT_SLOTS) SDL_FreeSurface(s);
	}
	SDL_UnlockMutex(g_lock);
	return 0;
}

bool mt_start(void)
{
	if (g_thread) return true;
	if (!g_lock && !(g_lock = SDL_CreateMutex())) return false;
	if (!g_wake && !(g_wake = SDL_CreateCond())) return false;
	g_run = true;
	if (!(g_thread = SDL_CreateThread(worker, "zyperos-mediatex", NULL))) {
		g_run = false;
		return false;
	}
	return true;
}

void mt_forget(void)
{
	int i;

	if (!g_lock) return;
	SDL_LockMutex(g_lock);
	g_gen++;
	for (i = 0; i < MT_SLOTS; i++) {
		g_ask[i].used = false;
		if (g_done[i].used) SDL_FreeSurface(g_done[i].s);
		g_done[i].used = false;
	}
	SDL_UnlockMutex(g_lock);
}

void mt_stop(void)
{
	if (!g_thread) return;
	mt_forget();
	SDL_LockMutex(g_lock);
	g_run = false;
	SDL_CondSignal(g_wake);
	SDL_UnlockMutex(g_lock);
	SDL_WaitThread(g_thread, NULL);
	g_thread = NULL;
}

void mt_ask(int key, const char *path, int px)
{
	int i, free_at = -1;

	if (!path || !path[0]) return;
	if (!g_thread) {
		/* No worker: decoded here, the frame waits for it */
		SDL_Surface *s = mt_load(path, px);

		for (i = 0; i < MT_SLOTS; i++)
			if (!g_done[i].used) { g_done[i] = (done){ key, g_gen, s, true }; return; }
		SDL_FreeSurface(s);
		return;
	}
	SDL_LockMutex(g_lock);
	for (i = 0; i < MT_SLOTS; i++) {
		if (g_ask[i].used && g_ask[i].key == key) { free_at = i; break; }
		if (!g_ask[i].used && free_at < 0) free_at = i;
	}
	if (free_at >= 0) {
		ask *a = &g_ask[free_at];

		a->key = key;
		a->px = px;
		snprintf(a->path, sizeof a->path, "%s", path);
		a->used = true;
		SDL_CondSignal(g_wake);
	}
	SDL_UnlockMutex(g_lock);
}

bool mt_take(int *key, SDL_Surface **s)
{
	int i;
	bool got = false;

	if (g_lock) SDL_LockMutex(g_lock);
	for (i = 0; i < MT_SLOTS; i++)
		if (g_done[i].used && g_done[i].gen == g_gen) {
			*key = g_done[i].key;
			*s = g_done[i].s;
			g_done[i].used = false;
			got = true;
			break;
		}
	if (g_lock) SDL_UnlockMutex(g_lock);
	return got;
}
