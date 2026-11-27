/* SPDX-License-Identifier: GPL-3.0-only */
/* A song, a video or a game named in Chinese or Japanese is drawn in a face
 * that has its characters, whatever face it was asked for: the role faces
 * (ui_font) and every other face the launcher opens through ui_face_open,
 * home.c's page faces above all (page_font), which the top bar's song, the
 * cassette's label, MP4's names and the grid's names are drawn in, and the
 * faces at sizes of their own (ui_face_at). face_for used to know only the
 * role faces, so a song title in one of the others was drawn in Lexend,
 * every Chinese or Japanese character a box (Discord, 2026-11-23).
 *
 * ui_text_width measures in the face ui_text draws in (face_for), so a line
 * measured in such a face must measure as the Chinese or Japanese cut of the
 * same size and weight measures it: in English, with the faces opened as
 * home.c opens them; again after they are closed and opened anew (new faces,
 * perhaps at the old ones' addresses); and in 日本語, where the Japanese
 * page face is asked for a line with a character only Chinese's may have. */
#include "../src/ui.h"
#include "../src/design.h"
#include "../src/i18n.h"
#include <stdio.h>
#include <string.h>

static int fails;

/* home.c's page faces, as page_fonts_init opens them (src/home.c) */
static const struct { int pt; bool title; const char *name; } PAGE[] = {
	{ DS_TYPE_CHROME, false, "the chrome's face (the bar's song)" },
	{ DS_TYPE_SUPPORT, false, "the action's and meta's face" },
	{ DS_TYPE_GAME_TITLE, true, "the game title's face (the cassette's title)" },
};
#define NPAGE (sizeof PAGE / sizeof PAGE[0])

/* A song with kana and one with simplified characters, with Latin letters
 * around them, as tags are */
static const char *const LINES[] = { "ゲーム～勇敢な恋の歌", "Ayaka - 爱情故事 (Live)" };

/* The cuts: [set][title], set 0 Lexend's, 1 Chinese's, 2 Japanese's, as
 * ui_font_set numbers them */
static const char *const CUTS[3][2] = {
	{ DS_FONT_REGULAR, DS_FONT_SEMIBOLD },
	{ DS_FONT_CJK, DS_FONT_CJK_TITLE },
	{ DS_FONT_JA, DS_FONT_JA_TITLE },
};

static TTF_Font *open_cut(int set, bool title, int pt)
{
	char path[256];

	snprintf(path, sizeof path, "res/fonts/%s", CUTS[set][title]);
	return ui_face_open(path, pt);
}

/* s's width in a cut at pt, or -1 when `need` and the cut lacks one of its
 * Chinese or Japanese characters. Opened with TTF_OpenFont, not through
 * ui.c, so nothing here is what is being checked. */
static int width_in(int set, bool title, int pt, const char *s, bool need)
{
	char path[256];
	TTF_Font *f;
	int w = -1;

	snprintf(path, sizeof path, "res/fonts/%s", CUTS[set][title]);
	if (!(f = TTF_OpenFont(path, pt))) {
		fprintf(stderr, "FAIL %s: %s\n", path, TTF_GetError());
		fails++;
		return -1;
	}
	for (const unsigned char *u = (const unsigned char *)s; need && *u; u++) {
		if (*u < 0xE2 || *u > 0xEF) continue;
		if (!TTF_GlyphIsProvided32(f, (Uint32)((*u & 0x0F) << 12 | (u[1] & 0x3F) << 6 | (u[2] & 0x3F)))) {
			TTF_CloseFont(f);
			return -1;
		}
		u += 2;
	}
	TTF_SizeUTF8(f, s, &w, NULL);
	TTF_CloseFont(f);
	return w;
}

/* The width s has in the first cut of the size that has all its characters:
 * the language's own (`first`, 1 or 2, 0 for Lexend's), then Japanese's
 * where s has kana and Chinese's where it has none, then the other (ui.c,
 * face_for) */
static int width_want(int first, bool title, int pt, const char *s)
{
	int kana = strstr(s, "ゲ") != NULL, order[3] = { first, kana ? 2 : 1, kana ? 1 : 2 };

	for (int k = 0; k < 3; k++) {
		int w;

		if (!order[k]) continue;
		if ((w = width_in(order[k], title, pt, s, true)) >= 0) return w;
	}
	return -1;
}

static void measured(const char *pass, const char *face, TTF_Font *f, int set, bool title, int pt)
{
	if (!f) {
		fprintf(stderr, "FAIL %s: %s would not open\n", pass, face);
		fails++;
		return;
	}
	for (size_t j = 0; j < sizeof LINES / sizeof LINES[0]; j++) {
		const char *s = LINES[j];
		int want = width_want(set, title, pt, s);
		int asked = width_in(set, title, pt, s, false);
		int got = ui_text_width(f, s);

		if (want < 0) {
			fprintf(stderr, "FAIL %s: no Chinese or Japanese cut has all of \"%s\"\n", pass, s);
			fails++;
		} else if (!set && want == asked) {
			/* The measure tells the faces apart only where they differ */
			fprintf(stderr, "FAIL %s: \"%s\" is %d wide in Lexend's boxes and in its own cut alike\n",
			        pass, s, want);
			fails++;
		} else if (got != want) {
			fprintf(stderr, "FAIL %s: \"%s\" in %s (%d pt) measures %d, %s; in the cut that has it, %d\n",
			        pass, s, face, pt, got,
			        got == asked ? "as in the face it was asked for, boxes and all" : "as in some other face",
			        want);
			fails++;
		}
	}
}

/* home.c's faces for a set, opened as page_fonts_init and
 * page_fonts_cjk_open open them, measured, and closed as page_fonts_quit
 * closes them */
static void page_faces(const char *pass, int set)
{
	TTF_Font *f[NPAGE];

	for (size_t i = 0; i < NPAGE; i++) f[i] = open_cut(set, PAGE[i].title, PAGE[i].pt);
	for (size_t i = 0; i < NPAGE; i++) measured(pass, PAGE[i].name, f[i], set, PAGE[i].title, PAGE[i].pt);
	/* As page_fonts_quit closes them: the measures of the faces forgotten
	 * first, since a face opened later may land at a closed one's address */
	ui_text_memo_clear();
	for (size_t i = 0; i < NPAGE; i++) ui_face_close(f[i]);
}

int main(void)
{
	SDL_Surface *surface;
	SDL_Renderer *r;

	if (SDL_Init(0) != 0) return 1;
	surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
	r = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
	if (!r || !ui_init(r, "res/fonts/menu.ttf")) {
		fprintf(stderr, "FAIL the launcher's faces would not open\n");
		return 1;
	}
	page_faces("English", 0);
	page_faces("English, the faces opened again", 0);
	/* A face at a size of its own, kept by ui.c (MP4's Classic clock's) */
	measured("English", "ui_face_at's face", ui_face_at(DS_TYPE_CLOCK, true), 0, true, DS_TYPE_CLOCK);
	i18n_set(LANG_JA);
	ui_fonts_for(LANG_JA);
	page_faces("日本語", 2);
	i18n_set(LANG_EN);
	ui_fonts_for(LANG_EN);
	ui_quit();
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(surface);
	SDL_Quit();
	if (fails) {
		fprintf(stderr, "check-cjkface: %d failed\n", fails);
		return 1;
	}
	printf("check-cjkface: Chinese and Japanese names are drawn in faces that have them, whatever face was asked for\n");
	return 0;
}
