/* SPDX-License-Identifier: GPL-3.0-only */
/* A cached line must reach full opacity after its first fading frame. A
 * still drawn opaque cannot catch opacity baked into the cached glyphs.
 *
 * And the text measures ui.c remembers (ui_text_width, ui_fit_text,
 * ui_balance_lines) answer exactly what measuring again answers: the second
 * time, after many others pushed the first out, in another face, after the
 * memo is emptied, and from a thread that is not the main one. */
#include "../src/ui.h"
#include "../src/linebreak.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define W 640
#define H 128
static Uint32 reference[W * H], actual[W * H];

static int frame(SDL_Renderer *r, Uint8 alpha, Uint32 *pixels)
{
	const ds_theme *t = theme();
	SDL_Color c = ui_col(t->ground, alpha);
	SDL_SetRenderDrawColor(r, t->text.r, t->text.g, t->text.b, 255);
	SDL_RenderClear(r);
	if (!ui_text(r, ui_font(UI_F_TITLE), "Hi! I'm Zyper.", 24, 12, -1, c)) return 0;
	return SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, W * 4) == 0;
}

/* The same measure ui.c's linebreak calls make, straight from SDL_ttf (the
 * texts below carry no menu marks, where the two would differ) */
static int raw_width(void *f, const char *s)
{
	int w = 0;

	TTF_SizeUTF8(f, s, &w, NULL);
	return w;
}

static const char *texts[] = {
	"Contra", "The Legend of Zelda: A Link to the Past",
	"Castlevania: Symphony of the Night (Director's Cut, Special Edition)",
	"Pokemon Mystery Dungeon - Red Rescue Team", "Ys I & II", "",
	"A Very Long Song Title From An Album Nobody Has Heard Of, Remastered 2026",
	"Final Fantasy Tactics Advance", "WarioWare, Inc.: Mega Microgame$!",
};
static const int boxes[] = { 120, 256, 400, 656 };

static int fails;
static void ck(int ok, const char *what, const char *text, int box)
{
	if (!ok) { fprintf(stderr, "FAIL %s: \"%s\" in %d\n", what, text, box); fails++; }
}

/* Every text in every box, measured by ui.c and measured again here */
static void measures_agree(TTF_Font *f, const char *pass)
{
	size_t i, j;

	for (i = 0; i < sizeof texts / sizeof texts[0]; i++) {
		char want[LB_MAX_LINES][LB_LINE], got[LB_MAX_LINES][LB_LINE];
		char fw[96], fg[96];
		int cap;

		ck(ui_text_width(f, texts[i]) == raw_width(f, texts[i]), pass, texts[i], 0);
		for (j = 0; j < sizeof boxes / sizeof boxes[0]; j++) {
			for (cap = 1; cap <= 4; cap++) {
				int nw = lb_balance(texts[i], boxes[j], cap, raw_width, f, want);
				int ng = ui_balance_lines(f, texts[i], boxes[j], cap, got);
				int k, same = nw == ng;

				for (k = 0; same && k < nw; k++) same = !strcmp(want[k], got[k]);
				ck(same, pass, texts[i], boxes[j]);
			}
			lb_fit(texts[i], fw, sizeof fw, boxes[j], raw_width, f);
			ui_fit_text(f, texts[i], fg, sizeof fg, boxes[j]);
			ck(!strcmp(fw, fg), pass, texts[i], boxes[j]);
			/* A smaller buffer is another answer, not the same one cut */
			lb_fit(texts[i], fw, 12, boxes[j], raw_width, f);
			ui_fit_text(f, texts[i], fg, 12, boxes[j]);
			ck(!strcmp(fw, fg), pass, texts[i], boxes[j]);
		}
	}
}

static int on_a_thread(void *f)
{
	int k;

	for (k = 0; k < 200; k++)
		if (ui_text_width(f, texts[k % 9]) != raw_width(f, texts[k % 9])) return 1;
	return 0;
}

static void the_memo(SDL_Renderer *r)
{
	TTF_Font *a, *b;
	SDL_Thread *t;
	char many[16];
	int k, thread_fail = 1;
	clock_t t0;
	double first, again;

	if (!ui_init(r, "res/fonts/menu.ttf")) { ck(0, "ui_init", "", 0); return; }
	a = ui_font(UI_F_TITLE);
	b = ui_font(UI_F_META);
	measures_agree(a, "first time");
	measures_agree(a, "remembered");
	measures_agree(b, "another face");
	/* Two hundred others, more than the memo keeps, then the first again */
	for (k = 0; k < 200; k++) {
		snprintf(many, sizeof many, "Game %d", k);
		ui_text_width(a, many);
	}
	measures_agree(a, "after the memo was full");
	ui_text_memo_clear();
	measures_agree(a, "after it was emptied");
	t = SDL_CreateThread(on_a_thread, "memo-check", a);
	if (t) SDL_WaitThread(t, &thread_fail);
	ck(!thread_fail, "a thread's widths", "", 0);

	/* What it saves, on this machine: a title balanced 2000 times */
	t0 = clock();
	for (k = 0; k < 2000; k++) {
		char l[2][LB_LINE];
		lb_balance(texts[2], 656, 2, raw_width, a, l);
	}
	first = (double)(clock() - t0) / CLOCKS_PER_SEC;
	t0 = clock();
	for (k = 0; k < 2000; k++) {
		char l[2][LB_LINE];
		ui_balance_lines(a, texts[2], 656, 2, l);
	}
	again = (double)(clock() - t0) / CLOCKS_PER_SEC;
	printf("a title balanced: %.1f us measured, %.2f us remembered\n",
	       first / 2000 * 1e6, again / 2000 * 1e6);
	ui_quit();
}

int main(void)
{
	SDL_Surface *surface;
	SDL_Renderer *r;
	int th;
	const Uint8 starts[] = { 0, 32, 128 };

	if (SDL_Init(0) != 0) return 1;
	surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
	r = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
	if (!r) { fprintf(stderr, "textfade: %s\n", SDL_GetError()); return 1; }
	theme_set_file(NULL);
	for (th = 0; th < DS_THEMES; th++) {
		size_t i;
		theme_init(th == DS_LIGHT ? APPEAR_LIGHT : APPEAR_DARK);
		if (!ui_init(r, "res/fonts/menu.ttf") || !frame(r, 255, reference)) return 1;
		ui_quit();
		for (i = 0; i < sizeof starts / sizeof starts[0]; i++) {
			if (!ui_init(r, "res/fonts/menu.ttf") || !frame(r, starts[i], actual)) return 1;
			if (!memcmp(reference, actual, sizeof actual)) {
				fprintf(stderr, "FAIL theme %d: a fading line must start dimmer\n", th);
				fails++;
			}
			if (!frame(r, 255, actual)) return 1;
			if (memcmp(reference, actual, sizeof actual)) {
				fprintf(stderr, "FAIL theme %d: text cached at alpha %u stays dim\n", th, starts[i]);
				fails++;
			}
			ui_quit();
		}
	}
	the_memo(r);
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(surface);
	SDL_Quit();
	if (!fails) puts("ok: cached text reaches full opacity after every fade, in both themes, "
	                 "and remembered measures are the measures");
	return fails != 0;
}
