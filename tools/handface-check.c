/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's tape labels are handwritten (Caveat SemiBold, res/fonts/hand.ttf;
 * my pick in the Tape Styles labs, 2026-11-23), and Caveat has Latin and
 * Cyrillic alone: a song named in Chinese, Japanese or Greek must still be
 * itself on the label, never boxes. ui_hand_face draws a line run by run,
 * each run of characters Caveat lacks in Lexend or Noto, on Caveat's
 * baseline. Here:
 *
 *   - the runs: Latin and Cyrillic in the hand; Chinese in Noto Sans SC,
 *     a line with kana in Noto Sans JP; Greek in Lexend; a space keeps the
 *     face it follows;
 *   - the width: a mixed line measures as its runs measure in their own
 *     faces, not as the hand's boxes do;
 *   - the picture: a CJK line drawn in the hand has ink where each
 *     character is (no blank box outline), and a hand line is drawn on one
 *     baseline;
 *   - in English and in 日本語 alike.
 *
 *   make check-handface
 */
#include "../src/ui.h"
#include "../src/design.h"
#include "../src/i18n.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void fail(const char *what, const char *s)
{
	fprintf(stderr, "FAIL %s: \"%s\"\n", what, s);
	fails++;
}

/* The runs ui_hand_runs gives for s in hand f, as a string of digits
 * (0 Caveat, 1 Lexend, 2 Noto SC, 3 Noto JP) */
static void runs_are(TTF_Font *f, const char *s, const char *want)
{
	int faces[16], n = ui_hand_runs(f, s, faces, 16), i;
	char got[20] = "";

	for (i = 0; i < n && i < 16; i++) got[i] = (char)('0' + faces[i]);
	got[i] = '\0';
	if (strcmp(got, want)) {
		fprintf(stderr, "FAIL the runs of \"%s\": %s, not %s\n", s, got, want);
		fails++;
	}
}

static int width_of(const char *file, int pt, const char *s)
{
	char path[256];
	TTF_Font *f;
	int w = -1;

	snprintf(path, sizeof path, "res/fonts/%s", file);
	if (!(f = TTF_OpenFont(path, pt))) return -1;
	TTF_SizeUTF8(f, s, &w, NULL);
	TTF_CloseFont(f);
	return w;
}

static void pass(const char *lang_name, TTF_Font *f, int pt, bool bold)
{
	const int lex = (int)(pt * 0.76f + 0.5f);
	char what[128];
	int w, want;

	snprintf(what, sizeof what, "%s: no hand face", lang_name);
	if (!f) { fail(what, DS_FONT_HAND); return; }
	runs_are(f, "Harbour Lights", "0");
	runs_are(f, "Последний поезд", "0");
	runs_are(f, "Midnight 东京", "02");
	/* 東 is not in Chinese's cut: the line takes Japanese's, whole */
	runs_are(f, "Midnight 東京", "03");
	runs_are(f, "夜明けのメロディー", "3");
	runs_are(f, "爱情故事 (Live)", "20");
	runs_are(f, "Ω mega", "10");
	/* A mixed line's width: its runs' own */
	w = ui_text_width(f, "Midnight 东京");
	want = width_of(DS_FONT_HAND, pt, "Midnight ") + width_of(bold ? DS_FONT_CJK_TITLE : DS_FONT_CJK, lex, "东京");
	if (w != want) {
		fprintf(stderr, "FAIL %s: \"Midnight 东京\" measures %d, its runs %d\n", lang_name, w, want);
		fails++;
	}
	if (w == width_of(DS_FONT_HAND, pt, "Midnight 东京")) fail("measured as the hand's boxes", "Midnight 东京");
	/* The picture: each kanji of the line has ink inside its box's middle,
	 * where a missing glyph's box is empty */
	{
		SDL_Surface *s = ui_text_surface(f, "東京", (SDL_Color){ 0, 0, 0, 255 });
		int x, y, ink = 0, cw;

		if (!s) { fail("no picture", "東京"); return; }
		cw = s->w / 2;
		SDL_LockSurface(s);
		for (y = s->h / 3; y < s->h * 2 / 3; y++)
			for (x = cw / 3; x < cw * 2 / 3; x++) {
				Uint32 p = ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x];
				Uint8 r, g, b, a;

				SDL_GetRGBA(p, s->format, &r, &g, &b, &a);
				if (a > 128) ink++;
			}
		SDL_UnlockSurface(s);
		if (s->h != TTF_FontHeight(f)) fail("the line not the hand's height", "東京");
		SDL_FreeSurface(s);
		if (ink < 4) fail("a kanji drawn as an empty box", "東");
	}
}

int main(void)
{
	SDL_Surface *surface;
	SDL_Renderer *r;
	const int pt = (int)(DS_TYPE_TITLE * 1.3f + 0.5f);

	if (SDL_Init(0) != 0) return 1;
	surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
	r = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
	if (!r || !ui_init(r, "res/fonts/menu.ttf")) {
		fprintf(stderr, "FAIL the launcher's faces would not open\n");
		return 1;
	}
	pass("English", ui_hand_face(pt, true), pt, true);
	i18n_set(LANG_JA);
	ui_fonts_for(LANG_JA);
	pass("日本語", ui_hand_face(pt, true), pt, true);
	i18n_set(LANG_EN);
	ui_fonts_for(LANG_EN);
	ui_quit();
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(surface);
	SDL_Quit();
	if (fails) {
		fprintf(stderr, "check-handface: %d failed\n", fails);
		return 1;
	}
	printf("check-handface: the tape labels' handwriting draws what Caveat has not in Lexend or Noto, run by run, "
	       "never boxes, in English and in 日本語\n");
	return 0;
}
