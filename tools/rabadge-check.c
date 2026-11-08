/* SPDX-License-Identifier: GPL-3.0-only */
/* Achievement badges, their cache, their fetch and their fallback, through the
 * real src/rabadge.c and src/net.c, with no network.
 *
 *     make check-badges
 *
 * What a badge owes a handheld, none of which shows until a card is without
 * one: a frame never waits for a fetch, a badge that has not come is a plain
 * icon of the same size rather than a hole, the fetch asks for what is
 * missing and nothing else, and what the server answers that is not a picture
 * (RetroAchievements answers a name it does not know with a 404 page) is
 * never kept as one.
 *
 * curl is all there is between the fetch and the network, and this check is
 * its own curl, as tools/signin-check.c is: src/net.c is read in whole with
 * this binary first on its list of curls (NET_CURL_BINARIES) and a folder of
 * its own for the request's config (NET_TMP, from the Makefile). Run as curl
 * - with -K and a config - it answers each url/output pair as the server it
 * plays does, which STANDIN in its environment chooses: `fast` writes a 64
 * pixel PNG for every name but "gone", which gets a 404 page; `slow` is `fast`
 * after a pause, so a frame that waited could be told by its time.
 *
 * Then the two pictures the launcher draws itself (src/noticeart.h): the
 * plain badge in both looks and both themes, and Zyper, in his own colors,
 * with his shadow on Beard and without it on Salt & Pepper.
 */
#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/cheevos.h"
#include "../src/design.h"
#include "../src/net.h"
#include "../src/noticeart.h"
#include "../src/rabadge.h"

#ifndef NET_TMP
#error "build with -DNET_TMP, a folder of this check's own (the Makefile does)"
#endif

#define ROOT NET_TMP "-card"      /* the card: the set, and its badges beside it */
#define LOG  NET_TMP "-log"       /* what the stand-in was asked, run by run */

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* ---- src/net.c, with this binary for its curl --------------------------- */

static char g_self[1024];
static bool g_online = true;

#define NET_CURL_BINARIES g_self
#define net_online net_online_of_this_computer
#include "../src/net.c"
#undef net_online

/* Whatever the step says, so "no network" is a step and the rest run on a
 * computer with none */
bool net_online(void) { return g_online; }

/* ---- the stand-in ---------------------------------------------------------- */

static void write_png(const char *path)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
	int x, y;

	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++)
			((Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch))[x] =
				0xFF000000u | (Uint32)(x * 4) << 16 | (Uint32)(y * 4) << 8 | 0x80u;
	IMG_SavePNG(s, path);
	SDL_FreeSurface(s);
}

/* The quoted value after `key = ` on a line of the config */
static bool cfg_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);

	if (strncmp(line, key, k) || strncmp(line + k, " = \"", 4)) return false;
	line += k + 4;
	snprintf(out, n, "%s", line);
	if (strrchr(out, '"')) *strrchr(out, '"') = '\0';
	return true;
}

static int standin(int argc, char **argv)
{
	const char *mode = getenv("STANDIN");
	FILE *cfg, *log = fopen(LOG, "a");
	char line[2048], url[1024] = "", out[1024];
	int pairs = 0;

	if (!log || argc != 3 || strcmp(argv[1], "-K") || !(cfg = fopen(argv[2], "r"))) return 99;
	fputs("call\n", log);
	if (mode && !strcmp(mode, "slow")) usleep(700 * 1000);
	while (fgets(line, sizeof line, cfg)) {
		line[strcspn(line, "\n")] = '\0';
		if (cfg_value(line, "url", url, sizeof url)) continue;
		if (!cfg_value(line, "output", out, sizeof out)) continue;
		pairs++;
		fprintf(log, "url %s\n", url);
		if (strstr(url, "gone")) {
			/* RetroAchievements' answer to a name it does not know */
			FILE *f = fopen(out, "w");

			if (f) { fputs("<!doctype html><title>404</title>Not Found\r\n", f); fclose(f); }
		} else {
			write_png(out);
		}
	}
	fclose(cfg);
	fclose(log);
	return pairs ? 0 : 98;
}

/* ---- helpers --------------------------------------------------------------- */

static int log_count(const char *prefix)
{
	FILE *f = fopen(LOG, "r");
	char line[2048];
	int n = 0;

	if (!f) return 0;
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, prefix, strlen(prefix))) n++;
	fclose(f);
	return n;
}

static bool log_has(const char *needle)
{
	FILE *f = fopen(LOG, "r");
	char line[2048];
	bool has = false;

	if (!f) return false;
	while (fgets(line, sizeof line, f))
		if (strstr(line, needle)) has = true;
	fclose(f);
	return has;
}

static void sh(const char *fmt, const char *arg)
{
	char cmd[1200];

	snprintf(cmd, sizeof cmd, fmt, arg);
	if (system(cmd) != 0) { printf("  (could not run: %s)\n", cmd); failures++; }
}

/* Pixel (x, y) of a surface as 0xAARRGGBB */
static Uint32 px(SDL_Surface *s, int x, int y)
{
	return ((Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch))[x];
}

static bool has_color(SDL_Surface *s, int r, int g, int b, int tol)
{
	int x, y;

	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			Uint32 p = px(s, x, y);

			if ((p >> 24) == 255 &&
			    abs((int)((p >> 16) & 255) - r) <= tol && abs((int)((p >> 8) & 255) - g) <= tol &&
			    abs((int)(p & 255) - b) <= tol)
				return true;
		}
	return false;
}

static int count_alpha(SDL_Surface *s, int lo, int hi)
{
	int x, y, n = 0;

	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			int a = (int)(px(s, x, y) >> 24);

			if (a >= lo && a <= hi) n++;
		}
	return n;
}

/* ---- the set ---------------------------------------------------------------- */

static char g_set[512];

static void write_set(void)
{
	FILE *f;

	snprintf(g_set, sizeof g_set, "%s/Roms/GBA/.cheevos/Game.set", ROOT);
	sh("mkdir -p '%s/Roms/GBA/.cheevos'", ROOT);
	f = fopen(g_set, "w");
	/* The format cheevos.c reads (tools/cheevos-check.c): a header, then a
	 * `#:` row an achievement with its badge last, and its conditions */
	fputs("#! zyperos-cheevos 2\tgame=100\tconsole=5\ttitle=Test Game\n", f);
	fputs("#:\t1\t5\tOne\tFirst.\t10001\n", f);
	fputs("1\t0xH0000=1\n", f);
	fputs("#:\t2\t10\tTwo\tSecond.\t10002\n", f);
	fputs("2\t0xH0000=2\n", f);
	fputs("#:\t3\t25\tThree\tThird, a badge RA does not have.\tgone3\n", f);
	fputs("3\t0xH0000=3\n", f);
	fclose(f);
}

/* ---- the checks ------------------------------------------------------------- */

static void paths(void)
{
	char p[256];

	CHECK(rb_path("/c/Roms/GBA/.cheevos/Game.set", "10001", p, sizeof p) &&
	      !strcmp(p, "/c/Roms/GBA/.cheevos/badges/10001.png"),
	      "a badge is kept beside the set, in badges/: %s", p);
	CHECK(rb_path("Game.set", "7", p, sizeof p) && !strcmp(p, "badges/7.png"),
	      "a set with no folder keeps its badges in badges/: %s", p);
	CHECK(!rb_path("/c/x.set", "", p, sizeof p) && !p[0], "no name, no path");
	CHECK(!rb_path(NULL, "1", p, sizeof p), "no set, no path");
	CHECK(!rb_path("/c/x.set", "1", p, 0), "no room, no path");
}

static void textures(SDL_Renderer *r, const ds_theme *light, const ds_theme *dark)
{
	int w = 0, h = 0, ow = 0, oh = 0;
	SDL_Texture *plain_e, *plain_l, *real, *miss;
	long t0;
	int i;

	/* ---- nothing on the card: every icon is a plain badge ---------------- */
	rb_textures_free();
	CHECK(!rb_texture(r, g_set, "10001", false), "a badge that is not on the card has no texture");
	plain_e = rb_icon(r, g_set, "10001", false, light);
	plain_l = rb_icon(r, g_set, "10001", true, light);
	CHECK(plain_e && plain_l, "a badge that is not there is a plain one, never a hole");
	CHECK(plain_e != plain_l, "earned and locked plain badges are two pictures");
	if (plain_e) {
		SDL_QueryTexture(plain_e, NULL, NULL, &w, &h);
		CHECK(w == 64 && h == 64, "the plain badge is 64 pixels square like the real ones, not %d x %d", w, h);
	}
	CHECK(rb_icon(r, g_set, "10001", false, light) == plain_e, "the plain badge is made once, then kept");
	CHECK(rb_icon(r, g_set, "10001", false, dark) != plain_e, "the other theme has a plain badge of its own");
	CHECK(rb_icon(r, g_set, NULL, false, light), "no name is a plain badge too");
	CHECK(rb_icon(r, NULL, "10001", true, light), "no set is a plain badge too");
	CHECK(!rb_icon(NULL, g_set, "10001", false, light), "no renderer, no texture");

	/* ---- offline: nothing is fetched, and nothing waits ------------------- */
	g_online = false;
	sh("rm -f '%s'", LOG);
	rb_fetch_loaded();
	usleep(150 * 1000);
	CHECK(log_count("call") == 0, "offline, no fetch is started");
	CHECK(rb_landed() == 0, "offline, nothing lands");
	CHECK(!rb_have(g_set, "10001"), "offline, the card still has no badge");

	/* ---- online, on a slow link: the call returns, frames go on ------------ */
	g_online = true;
	setenv("STANDIN", "slow", 1);
	t0 = now_ms();
	rb_fetch_loaded();
	CHECK(now_ms() - t0 < 200, "starting the fetch took %ld ms: it waited for the network", now_ms() - t0);
	t0 = now_ms();
	for (i = 0; i < 2000; i++) {
		rb_icon(r, g_set, "10001", i & 1, light);
		rb_icon(r, g_set, "10002", false, light);
	}
	CHECK(now_ms() - t0 < 500, "4000 icons while the fetch ran took %ld ms: a frame would have waited", now_ms() - t0);
	CHECK(rb_icon(r, g_set, "10001", false, light) == plain_e, "while it runs, the badge is the plain one");
	rb_fetch_loaded();   /* a second ask while one runs starts nothing */

	for (i = 0; i < 100 && !rb_landed(); i++) usleep(100 * 1000);
	CHECK(i < 100, "the badges never landed");
	usleep(100 * 1000);
	CHECK(log_count("call") == 1, "one curl for the whole set, not one a badge or a second for the second ask: %d", log_count("call"));
	CHECK(log_count("url ") == 3, "the three missing badges were asked for: %d", log_count("url "));
	CHECK(log_has("/Badge/10001.png") && log_has("/Badge/10002.png"),
	      "the badges are asked for at media.retroachievements.org/Badge/");

	/* ---- what landed ------------------------------------------------------- */
	CHECK(rb_have(g_set, "10001") && rb_have(g_set, "10002"), "the two badges are on the card");
	CHECK(!rb_have(g_set, "gone3"), "a 404 page is not kept as a badge");
	{
		char p[512];
		struct stat st;

		rb_path(g_set, "10001", p, sizeof p);
		CHECK(stat(p, &st) == 0 && st.st_size > 100 && st.st_size < 16384, "a badge is a small file");
		snprintf(p + strlen(p), sizeof p - strlen(p), ".part");
		CHECK(access(p, F_OK) != 0, "no half-written file is left beside it");
	}
	real = rb_texture(r, g_set, "10001", false);
	CHECK(real != NULL, "the first frame after the landing finds the badge, though a miss was kept before it");
	if (real) {
		SDL_QueryTexture(real, NULL, NULL, &ow, &oh);
		CHECK(ow == 64 && oh == 64, "a badge is drawn from a 64 pixel texture");
	}
	CHECK(rb_icon(r, g_set, "10001", false, light) == real && real != plain_e,
	      "a badge that has come replaces the plain one");
	miss = rb_icon(r, g_set, "gone3", false, light);
	CHECK(miss == plain_e, "a badge RetroAchievements does not have stays the plain one");
	CHECK(rb_icon(r, g_set, "10002", true, light) != rb_icon(r, g_set, "10002", false, light),
	      "the locked look of a badge is its own picture, made from the one file");

	/* ---- a later ask fetches only what is missing ----------------------------- */
	sh("rm -f '%s'", LOG);
	setenv("STANDIN", "fast", 1);
	rb_fetch_loaded();
	for (i = 0; i < 100 && log_count("call") == 0; i++) usleep(50 * 1000);
	for (i = 0; i < 100 && !rb_landed() && log_count("call") == 1 && i < 20; i++) usleep(50 * 1000);
	usleep(200 * 1000);
	CHECK(log_count("url ") == 1 && log_has("/Badge/gone3.png"),
	      "the next ask is for the one badge still missing, not the set: %d asked", log_count("url "));
	rb_textures_free();
}

static void art(const ds_theme *light, const ds_theme *dark)
{
	SDL_Surface *e, *l, *zs, *zd;
	int pad = -1, i;
	const ds_theme *themes[2] = { light, dark };

	for (i = 0; i < 2; i++) {
		const ds_theme *t = themes[i];
		int c;

		e = noticeart_plain(84, t, false);
		l = noticeart_plain(84, t, true);
		CHECK(e && l && e->w == 84 && e->h == 84, "the plain badge is the size asked for");
		if (!e || !l) continue;
		CHECK((px(e, 0, 0) >> 24) == 0, "%s: the corner of the tile is round", t->name);
		c = (int)(px(e, 42, 4) >> 24);
		CHECK(c == 255, "%s: the tile is solid inside, alpha %d", t->name, c);
		CHECK(has_color(e, t->accent_ink.r, t->accent_ink.g, t->accent_ink.b, 1),
		      "%s: the earned trophy is in accent-ink", t->name);
		CHECK(!has_color(l, t->accent_ink.r, t->accent_ink.g, t->accent_ink.b, 1),
		      "%s: the locked one is not", t->name);
		CHECK((px(l, 42, 4) >> 24) < 255 && (px(l, 42, 4) >> 24) > 0,
		      "%s: the locked tile is faded, alpha %d", t->name, (int)(px(l, 42, 4) >> 24));
		CHECK(px(e, 42, 4) != px(l, 42, 4), "%s: earned and locked differ", t->name);
		SDL_FreeSurface(e);
		SDL_FreeSurface(l);
	}
	CHECK(!noticeart_plain(0, light, false), "no size, no badge");

	/* Zyper: his own colors, whichever theme he is on */
	zd = noticeart_zyper(84, false, dark, &pad);
	CHECK(zd && pad == 0, "without a shadow he has no margin: pad %d", pad);
	if (zd) {
		CHECK(zd->h >= 80 && zd->h <= 90 && zd->w >= 70 && zd->w <= 90,
		      "he is as tall as asked: %d x %d", zd->w, zd->h);
		CHECK(has_color(zd, 214, 118, 146, 2), "his tongue is out, pink");
		CHECK(has_color(zd, 17, 19, 16, 2), "his eyes and nose are ink");
		CHECK((px(zd, 0, 0) >> 24) == 0, "his corner is empty");
		CHECK(count_alpha(zd, 1, 254) < zd->w * zd->h / 5, "his edge is one pixel of blend, not a halo");
	}
	zs = noticeart_zyper(84, true, light, &pad);
	CHECK(zs && pad > 0, "with the shadow of Beard he has a margin for it: pad %d", pad);
	if (zs && zd) {
		CHECK(zs->w == zd->w + 2 * pad && zs->h == zd->h + 2 * pad,
		      "the margin is the shadow's reach: %d x %d for %d x %d and %d", zs->w, zs->h, zd->w, zd->h, pad);
		CHECK(count_alpha(zs, 1, 120) > count_alpha(zd, 1, 120) + 200, "the shadow is faint pixels under him");
		CHECK((px(zs, 0, 0) >> 24) == 0, "the far corner of the shadow's margin is clear");
		CHECK(has_color(zs, 214, 118, 146, 2), "he is as he was, over it");
	}
	if (zs) SDL_FreeSurface(zs);
	if (zd) SDL_FreeSurface(zd);
	CHECK(!noticeart_zyper(0, false, dark, NULL), "no height, no Zyper");
}

int main(int argc, char **argv)
{
	SDL_Surface *target;
	SDL_Renderer *r;

	if (argc == 3 && !strcmp(argv[1], "-K")) return standin(argc, argv);

	if (!realpath(argv[0], g_self)) snprintf(g_self, sizeof g_self, "%s", argv[0]);
	setenv("SDL_VIDEODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 2; }
	sh("rm -rf '%s'", ROOT);
	sh("rm -rf '%s'", NET_TMP);
	sh("mkdir -p '%s'", NET_TMP);
	write_set();
	CHECK(chv_load(g_set) && chv_count() == 3, "the set loads: %d", chv_count());
	CHECK(!strcmp(chv_at(0)->badge, "10001") && !strcmp(chv_at(2)->badge, "gone3"),
	      "the set carries each achievement's badge name");

	target = SDL_CreateRGBSurfaceWithFormat(0, 256, 256, 32, SDL_PIXELFORMAT_ARGB8888);
	r = SDL_CreateSoftwareRenderer(target);
	if (!r) { printf("no software renderer: %s\n", SDL_GetError()); return 2; }

	paths();
	textures(r, &DS_THEME[DS_LIGHT], &DS_THEME[DS_DARK]);
	art(&DS_THEME[DS_LIGHT], &DS_THEME[DS_DARK]);

	sh("rm -rf '%s'", ROOT);
	sh("rm -rf '%s'", NET_TMP);
	sh("rm -f '%s'", LOG);
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(target);
	SDL_Quit();
	if (failures) { printf("%d failed\n", failures); return 1; }
	printf("ok: badges are kept beside their set, fetched off the frame in one curl for what is missing,\n"
	       "and a badge that has not come is a plain one of the same size; a 404 page is never one\n");
	return 0;
}
