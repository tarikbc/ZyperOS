/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The frame thread's time while a shelf lands, on the launcher's own objects
 * (make landbench). A measurement, not a check: it prints times and holds
 * nothing.
 *
 * A shelf of eleven games - the seven in view and two more each side, what
 * prime_window asks for - landed on a hundred times, each time with titles
 * not drawn before, and the frames that follow run at 60 Hz until every slot
 * holds its card. On each frame the frame thread does what src/main.c's does
 * with pictures: it takes what the workers finished (texload_deliver, with
 * src/main.c's shelf_put in miniature), then asks for the seven in view, as
 * cf_draw does through game_get_tex. Its time on that is the number; the
 * worst frame of a landing is the stutter, and the median of those over the
 * landings is what is printed, with the 90th centile and the worst.
 *
 * Three shelves: games with no art; games with box art, 359 x 512 PNGs as
 * art comes after the shrink (src/artshrink.c), written here at the start;
 * and "a crowd", two dozen cards with no art all finished before a frame
 * takes any, the most the workers can have waiting, which is a fast scroll's
 * worst. With --on-frame the frame works as it did before the workers drew
 * cards: asks with no card in them, every finished picture taken in one
 * frame, and the card drawn there for each game with no art. With --slow MS
 * every upload is made MS slower, as a slow GPU's are: the Pixel 2's Mali G31
 * is that, by how much this host cannot say.
 *
 * SDL's software renderer, no window: SDL_VIDEODRIVER=dummy.
 */
#include "texload.h"
#include "theme.h"
#include "ui.h"

#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SHELF 24
#define WINDOW 11
#define SYS 3
#define FRAME_MS (1000.0 / 60.0)
#define RUNS_MAX 1000

static SDL_Renderer *R;
static SDL_Texture *tex[SHELF];
static char titles[SHELF][UI_CARD_TITLE];
static char art[SHELF][512];
static bool on_frame;
static double slow_ms;

static double now_ms(void)
{
	return (double)SDL_GetPerformanceCounter() * 1000.0 /
	       (double)SDL_GetPerformanceFrequency();
}

static void ask(int i)
{
	if (on_frame) {
		texload_want(SYS, i, art[i], NULL);
	} else {
		ui_card_order o;

		ui_card_order_set(&o, titles[i], 0xE6312E, false);
		texload_want_or_draw(SYS, i, art[i], NULL, &o);
	}
}

/* shelf_put, for this one shelf */
static void put(void *ctx, texload_got *g)
{
	(void)ctx;
	if (g->sys != SYS || g->idx < 0 || g->idx >= SHELF || tex[g->idx]) {
		if (g->surf) SDL_FreeSurface(g->surf);
		return;
	}
	if (g->surf) {
		tex[g->idx] = g->card ? ui_card_upload(R, g->surf, g->theme)
		                      : SDL_CreateTextureFromSurface(R, g->surf);
		SDL_FreeSurface(g->surf);
	}
	if (!tex[g->idx]) {
		int w, h;

		tex[g->idx] = ui_make_card(R, titles[g->idx], 0xE6312E, &w, &h);
	}
	/* The upload made slower, whichever texture it was */
	{
		double t0 = now_ms();

		while (now_ms() - t0 < slow_ms) { }
	}
}

/* The frame's pictures, as it took them before: every one waiting */
static void take_all(void)
{
	texload_got g;

	while (texload_take(&g)) put(NULL, &g);
}

static void pictures(void)
{
	if (on_frame) take_all();
	else texload_deliver(theme_index(), put, NULL);
}

static void fresh(int run, int n, bool with_art)
{
	static const char *const words[] = {
		"Castlevania", "Contra", "Crystalis", "Double Dragon", "Earthbound",
		"Final Fantasy", "Galaga", "Kirby's Adventure", "Mega Man", "Metroid",
		"Punch-Out!!", "River City Ransom", "Super Mario Bros. 3", "Tetris",
		"The Legend of Zelda", "Zelda II" };

	texload_bump();
	for (int i = 0; i < SHELF; i++) {
		if (tex[i]) SDL_DestroyTexture(tex[i]);
		tex[i] = NULL;
		snprintf(titles[i], sizeof titles[i], "%s %d", words[(run * 7 + i) % 16],
		         run * SHELF + i);
		if (with_art && i < n)
			snprintf(art[i], sizeof art[i], "build-native/landbench-art/art%d.png", i);
		else
			snprintf(art[i], sizeof art[i], "/no/such/.media/%s.png", titles[i]);
	}
}

static int full(int n)
{
	for (int i = 0; i < n; i++)
		if (!tex[i]) return 0;
	return 1;
}

static int by_value(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static void report(const char *name, double *worst, int *frames, int runs)
{
	int most = 0;

	printf("%-9s first landing: worst frame %.2f ms\n", name, worst[0]);
	for (int k = 1; k < runs; k++) if (frames[k] > most) most = frames[k];
	qsort(worst + 1, (size_t)(runs - 1), sizeof worst[0], by_value);
	printf("%-9s worst frame of a landing: median %.2f, p90 %.2f, max %.2f ms; "
	       "all in within %d frames\n", name, worst[1 + (runs - 1) / 2],
	       worst[1 + (runs - 1) * 9 / 10], worst[runs - 1], most);
}

/* A hundred landings on a window of eleven */
static void landings(const char *name, bool with_art, int runs)
{
	static double worst[RUNS_MAX];
	static int frames[RUNS_MAX];

	for (int run = 0; run < runs; run++) {
		/* prime_around's order: farthest first, the focused card last */
		static const int order[WINDOW] = { 0, 10, 1, 9, 2, 8, 3, 7, 4, 6, 5 };
		double t0, t, w;
		int f = 0;

		fresh(run, WINDOW, with_art);
		t0 = now_ms();
		for (int k = 0; k < WINDOW; k++) ask(order[k]);
		w = now_ms() - t0;
		while (!full(WINDOW) && f < 600) {
			double a;

			f++;
			while (now_ms() - t0 < f * FRAME_MS) SDL_Delay(1);
			a = now_ms();
			pictures();
			for (int i = 2; i < 2 + 7; i++)
				if (!tex[i]) ask(i);
			t = now_ms() - a;
			if (t > w) w = t;
		}
		worst[run] = w;
		frames[run] = f;
	}
	report(name, worst, frames, runs);
}

/* Two dozen finished before a frame takes any */
static void crowds(int runs)
{
	static double worst[RUNS_MAX];
	static int frames[RUNS_MAX];

	for (int run = 0; run < runs; run++) {
		double t0, w = 0.0;
		Uint32 until;
		int f = 0;

		fresh(run, 0, false);
		for (int i = 0; i < SHELF; i++) ask(i);
		until = SDL_GetTicks() + 3000;
		while (texload_waiting() < SHELF && SDL_GetTicks() < until) SDL_Delay(1);
		t0 = now_ms();
		while (!full(SHELF) && f < 600) {
			double a, t;

			if (f) while (now_ms() - t0 < f * FRAME_MS) SDL_Delay(1);
			f++;
			a = now_ms();
			pictures();
			t = now_ms() - a;
			if (t > w) w = t;
		}
		worst[run] = w;
		frames[run] = f;
	}
	report("a crowd", worst, frames, runs);
}

/* Box art as a shelf has it after the shrink: 512 on its long edge, an NES
 * cover's shape, busy enough that its PNG is a real decode */
static void make_art(void)
{
	mkdir("build-native", 0755);
	mkdir("build-native/landbench-art", 0755);
	for (int i = 0; i < WINDOW; i++) {
		char p[512];
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 359, 512, 32,
		                                                SDL_PIXELFORMAT_ARGB8888);
		unsigned seed = 12345u + (unsigned)i;

		for (int y = 0; y < s->h; y++) {
			Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

			for (int x = 0; x < s->w; x++) {
				seed = seed * 1103515245u + 12345u;
				row[x] = 0xFF000000u | (Uint32)(((x * 3 + i * 20) & 255) << 16) |
				         (Uint32)(((y + (int)(seed >> 27)) & 255) << 8) |
				         (Uint32)((x ^ y) & 255);
			}
		}
		snprintf(p, sizeof p, "build-native/landbench-art/art%d.png", i);
		IMG_SavePNG(s, p);
		SDL_FreeSurface(s);
	}
}

int main(int argc, char **argv)
{
	SDL_Surface *screen;
	int runs = 100;

	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--on-frame")) on_frame = true;
		else if (!strcmp(argv[k], "--slow") && k + 1 < argc) slow_ms = atof(argv[++k]);
		else if (atoi(argv[k]) > 1) runs = atoi(argv[k]) < RUNS_MAX ? atoi(argv[k]) : RUNS_MAX;
	}
	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
	IMG_Init(IMG_INIT_PNG);
	screen = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
	R = SDL_CreateSoftwareRenderer(screen);
	make_art();
	theme_set_file(NULL);
	theme_init(APPEAR_LIGHT);
	if (!ui_init(R, "res/fonts/menu.ttf") || !texload_start()) {
		printf("no fonts or no workers\n");
		return 1;
	}
	printf("%s, %d landings, software renderer%s, a 60 Hz frame\n",
	       on_frame ? "cards drawn on the frame, every picture in one frame"
	                : "cards drawn on the workers, pictures a share of a frame",
	       runs, slow_ms > 0 ? ", every upload made slower" : "");
	if (slow_ms > 0) printf("each upload %.2f ms slower\n", slow_ms);
	landings("no art", false, runs);
	landings("box art", true, runs);
	crowds(runs);
	texload_stop();
	for (int i = 0; i < SHELF; i++) if (tex[i]) SDL_DestroyTexture(tex[i]);
	ui_quit();
	SDL_DestroyRenderer(R);
	SDL_FreeSurface(screen);
	IMG_Quit();
	SDL_Quit();
	return 0;
}
