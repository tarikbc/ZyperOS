/* SPDX-License-Identifier: GPL-3.0-only */
/* Achievement badges. See src/rabadge.h. */
#include "rabadge.h"
#include "cheevos.h"
#include "net.h"

#include <SDL.h>
#include <SDL_image.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool rb_path(const char *set_path, const char *name, char *out, size_t n)
{
	const char *slash;

	if (!n) return false;
	out[0] = '\0';
	if (!set_path || !*set_path || !name || !*name) return false;
	slash = strrchr(set_path, '/');
	/* Beside the set: <dir of the set>/badges/<name>.png */
	snprintf(out, n, "%.*sbadges/%s.png", slash ? (int)(slash - set_path + 1) : 0,
	         set_path, name);
	return true;
}

/* Where a badge is fetched from */
static void rb_url(const char *name, char *out, size_t n)
{
	snprintf(out, n, RB_URL "%s.png", name ? name : "");
}

bool rb_have(const char *set_path, const char *name)
{
	char p[1024];

	return rb_path(set_path, name, p, sizeof p) && access(p, R_OK) == 0;
}

/* ---- as textures ------------------------------------------------------------ */

static struct {
	char         key[1100];   /* the file, and whether locked */
	SDL_Texture *tex;
	unsigned     used;
} g_tex[RB_TEXTURES];
static unsigned g_tick;

/* Badges asked for and not on the card, so the list does not look for them
 * again on every frame it draws: each was a 1,100-byte key, 48 compares and
 * an access() a row a frame on a set nobody has fetched. A miss holds until
 * a fetch lands files (g_landings moves) or the textures are let go. By the
 * key's 64-bit FNV-1a hash: a collision only hides a badge until then. */
#define RB_MISSES 64
static struct {
	uint64_t hash;
	int      landings;
} g_miss[RB_MISSES];
static int g_nmiss, g_miss_next;
static SDL_atomic_t g_landings;     /* runs that landed files: run() moves it */

static uint64_t key_hash(const char *k)
{
	uint64_t h = 1469598103934665603ULL;

	while (*k) h = (h ^ (unsigned char)*k++) * 1099511628211ULL;
	return h;
}

static bool known_missing(uint64_t h, int landings)
{
	int i;

	for (i = 0; i < g_nmiss; i++)
		if (g_miss[i].hash == h && g_miss[i].landings == landings) return true;
	return false;
}

static void note_missing(uint64_t h, int landings)
{
	g_miss[g_miss_next].hash = h;
	g_miss[g_miss_next].landings = landings;
	g_miss_next = (g_miss_next + 1) % RB_MISSES;
	if (g_nmiss < RB_MISSES) g_nmiss++;
}

/* The locked look: grey, three quarters as bright, at 70% - as the
 * achievements lab drew it */
static void lock_pixels(SDL_Surface *s)
{
	int x, y;

	for (y = 0; y < s->h; y++) {
		uint8_t *p = (uint8_t *)s->pixels + (size_t)y * s->pitch;

		for (x = 0; x < s->w; x++, p += 4) {
			unsigned l = (p[2] * 77u + p[1] * 150u + p[0] * 29u) >> 8;

			l = l * 3 / 4;
			p[0] = p[1] = p[2] = (uint8_t)l;
			p[3] = (uint8_t)(p[3] * 7 / 10);
		}
	}
}

/* Corners a sixth of the side, antialiased: the lab's 10 on a 54-unit row */
static void round_pixels(SDL_Surface *s)
{
	double rad = s->w / 6.0;
	int x, y;

	for (y = 0; y < s->h; y++) {
		uint8_t *p = (uint8_t *)s->pixels + (size_t)y * s->pitch;

		for (x = 0; x < s->w; x++, p += 4) {
			double dx = x + 0.5 < rad ? rad - x - 0.5 : (x + 0.5 > s->w - rad ? x + 0.5 - (s->w - rad) : 0);
			double dy = y + 0.5 < rad ? rad - y - 0.5 : (y + 0.5 > s->h - rad ? y + 0.5 - (s->h - rad) : 0);
			double d;

			if (dx <= 0 || dy <= 0) continue;
			d = sqrt(dx * dx + dy * dy) - rad;
			if (d > 0.5) p[3] = 0;
			else if (d > -0.5) p[3] = (uint8_t)(p[3] * (0.5 - d));
		}
	}
}

SDL_Texture *rb_texture(SDL_Renderer *r, const char *set_path, const char *name,
                        bool locked)
{
	char path[1024], key[1100];
	SDL_Surface *img, *argb;
	int i, slot = 0, landings;
	uint64_t h;

	if (!r || !rb_path(set_path, name, path, sizeof path)) return NULL;
	snprintf(key, sizeof key, "%s%s", locked ? "L" : "E", path);
	for (i = 0; i < RB_TEXTURES; i++) {
		if (g_tex[i].tex && !strcmp(g_tex[i].key, key)) {
			g_tex[i].used = ++g_tick;
			return g_tex[i].tex;
		}
		if (g_tex[i].used < g_tex[slot].used) slot = i;
	}
	h = key_hash(key);
	landings = SDL_AtomicGet(&g_landings);
	if (known_missing(h, landings)) return NULL;
	if (access(path, R_OK) != 0 || !(img = IMG_Load(path))) {
		note_missing(h, landings);
		return NULL;
	}
	argb = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(img);
	if (!argb) return NULL;
	if (locked) lock_pixels(argb);
	round_pixels(argb);
	if (g_tex[slot].tex) SDL_DestroyTexture(g_tex[slot].tex);
	g_tex[slot].tex = SDL_CreateTextureFromSurface(r, argb);
	SDL_FreeSurface(argb);
	if (!g_tex[slot].tex) return NULL;
	SDL_SetTextureBlendMode(g_tex[slot].tex, SDL_BLENDMODE_BLEND);
	snprintf(g_tex[slot].key, sizeof g_tex[slot].key, "%s", key);
	g_tex[slot].used = ++g_tick;
	return g_tex[slot].tex;
}

void rb_textures_free(void)
{
	int i;

	for (i = 0; i < RB_TEXTURES; i++) {
		if (g_tex[i].tex) SDL_DestroyTexture(g_tex[i].tex);
		g_tex[i].tex = NULL;
		g_tex[i].used = 0;
	}
	g_nmiss = g_miss_next = 0;
}

/* ---- the fetch, on a thread of its own ---------------------------------- */

/* The most one run asks for: a set's whole list, the largest measured at 203 */
#define RB_MAX CHV_MAX

static struct {
	SDL_mutex  *lock;
	SDL_Thread *thread;
	bool        busy;
	int         landed;
	int         n;
	char        url[RB_MAX][96];
	char        path[RB_MAX][1024];
} g;

static int run(void *unused)
{
	const char *urls[RB_MAX], *paths[RB_MAX];
	char dir[1024];
	int i, got;

	(void)unused;
	/* The badges folder, beside the set's own */
	snprintf(dir, sizeof dir, "%s", g.path[0]);
	if (strrchr(dir, '/')) *strrchr(dir, '/') = '\0';
	mkdir(dir, 0755);
	for (i = 0; i < g.n; i++) { urls[i] = g.url[i]; paths[i] = g.path[i]; }
	/* A second a badge at worst on a slow link, and never less than half a
	 * minute: a whole set on the device's Wi-Fi is a few seconds */
	got = net_get_pngs(urls, paths, g.n, 30 + g.n);
	fprintf(stderr, "badges: %d of %d fetched into %s\n", got, g.n, dir);
	SDL_LockMutex(g.lock);
	g.landed += got;
	g.busy = false;
	SDL_UnlockMutex(g.lock);
	if (got > 0) SDL_AtomicAdd(&g_landings, 1);
	return 0;
}

void rb_fetch_loaded(void)
{
	int i, n = 0;
	bool busy;

	if (!g.lock && !(g.lock = SDL_CreateMutex())) return;
	SDL_LockMutex(g.lock);
	busy = g.busy;
	SDL_UnlockMutex(g.lock);
	if (busy || chv_count() <= 0 || !net_online()) return;

	for (i = 0; i < chv_count() && n < RB_MAX; i++) {
		const cheevo *c = chv_at(i);

		if (!c->badge[0] || rb_have(chv_source(), c->badge)) continue;
		if (!rb_path(chv_source(), c->badge, g.path[n], sizeof g.path[n])) continue;
		rb_url(c->badge, g.url[n], sizeof g.url[n]);
		n++;
	}
	if (!n) return;
	if (g.thread) { SDL_WaitThread(g.thread, NULL); g.thread = NULL; }
	g.n = n;
	g.busy = true;
	if (!(g.thread = SDL_CreateThread(run, "zyperos-badges", NULL))) g.busy = false;
}

int rb_landed(void)
{
	int k;

	if (!g.lock) return 0;
	SDL_LockMutex(g.lock);
	k = g.landed;
	g.landed = 0;
	SDL_UnlockMutex(g.lock);
	return k;
}
