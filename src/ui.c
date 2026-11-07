/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "ui.h"
#include "roundrect.h"
#include <stddef.h>

#include "blur.h"     /* a shadow's blur and its nine pieces */
#include "cardgen.h"  /* the generated card's colors in each theme */
#include "sysmark.h"  /* a system's color as a mark */
#include "config.h"   /* CFG_NO_COLOR: a system with no color of its own */
#include "linebreak.h" /* where a name too wide for its box breaks or is cut */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLOW_SIZE 192      /* the glow is drawn stretched; small is enough */
/* Room for every string of the busiest screen. The keyboard draws about 60 -
 * forty keys, the action row and the line under it, the title, the field -
 * and at 12 each frame evicted what the next frame needed, so every frame
 * rendered all of them with SDL_ttf again, and typing lagged on the device.
 * 96 short strings are a few megabytes of texture at most. */
#define TEXT_CACHE 96

/* The type scale: one base size and a multiplier per role. The panel is
 * 1024x768 across about three inches, so the base is set for reading at arm's
 * length on a handheld rather than for a screen at desk distance, and the
 * ratios between the roles are what keeps a count from competing with a title.
 * UI_F_CARD is in card pixels, not screen pixels -- the card face is drawn at
 * 512 wide and shown at roughly two thirds of that. */
/* THE ONE SIZE THE LAUNCHER DRAWS AT, and 1.15 is here rather than 36.8 so it
 * stays legible as what it is: the largest of the three steps the Text Size row
 * used to offer, which was also the most readable of them.
 *
 * The row is gone. It ran 85% to 115%, and that band is too narrow to solve
 * anyone's problem - a player who cannot read the default is not rescued by
 * fifteen percent, and one who wants more games on screen is not served by
 * fifteen percent either. It changed how the launcher looked without changing
 * what anyone could do, while making every panel's fit a function of a
 * variable. The range once reached 1.50 and was cut to 1.15 because the
 * keyboard panel did not survive it: the panels size themselves from constants,
 * so a scale big enough to matter breaks their layout rather than stretching
 * it. That is what a large-text mode would have to fix first.
 *
 * Written as the same product it used to be evaluated as, so every font comes
 * out at exactly the point size 115% gave and nothing moves by a rounding
 * accident. */
#define FONT_BASE (32.0f * 1.15f)
static const float font_mul[UI_F_COUNT] = {
	[UI_F_TITLE] = 1.62f,   /* 60 */
	[UI_F_MENU]  = 1.34f,   /* 49 */
	[UI_F_LABEL] = 1.50f,   /* 55 */
	[UI_F_META]  = 1.00f,   /* 37 */
	[UI_F_CARD]  = 1.94f,   /* 71, in card pixels - the title IS the card */
};

/* The faces of the roles, a set a script: Lexend's (menu.ttf and title.ttf)
 * for every language but Chinese and Japanese, and tools/cjkfont.py's cuts
 * of Noto Sans SC and JP beside menu.ttf (DS_FONT_CJK..., DS_FONT_JA...),
 * each opened the first time its language or its text is drawn and kept from
 * then on: a language picker steps through them once a second, and a reopen
 * would cost those frames. `fonts` is the set the language that is on draws
 * in (ui_fonts_for); the button letters and the cards the launcher draws are
 * marks and stay Lexend's. */
enum { SET_LATIN, SET_SC, SET_JP, SETS };
static TTF_Font *font_sets[SETS][UI_F_COUNT];
static TTF_Font *const *const fonts_latin = font_sets[SET_LATIN];
static TTF_Font **fonts = font_sets[SET_LATIN];
static int set_on;                 /* the set `fonts` is, and the language's */
static bool set_tried[SETS];
static void set_open(int set);
static TTF_Font *f_mark;
/* The two faces of DESIGN.md 4: Regular, menu.ttf, the path ui_init is
 * handed, for every role but one; and SemiBold, title.ttf from the same
 * folder, for the title role and the letters button_semibold gives it (a
 * face button's, which DESIGN.md 11.9 sets in the title role, and the
 * keyboard's glyphs). On a card from before there was a title.ttf the title
 * path is menu.ttf's, so the title is Regular rather than missing. */
static char font_path_kept[512];
static char title_path_kept[512];
static SDL_Texture *glow_tex;
static void shadows_make(SDL_Renderer *r);
static void shadows_free(void);

/* One title is redrawn on every frame while the rest of the screen changes
 * around it. Rendering TTF text is milliseconds; blitting a texture is not.
 * A tiny most-recently-used cache turns the first into the second. */
struct text_entry {
	TTF_Font *font;
	char str[256];
	unsigned rgb;
	SDL_Texture *tex;
	int w, h;
	/* Down this far where the line was drawn in another face than `font`
	 * (face_for: Chinese in a Lexend role), so its capitals' middle is where
	 * the caller put `font`'s */
	int dy;
	unsigned used;
	/* A marquee's window, ended between letters (ui_text_marquee): how far
	 * in from its right end the last whole letter ends at the start, and
	 * from its left end the first whole letter starts at the far end, for a
	 * window mq_w wide; 0 there for none worked out yet. */
	int mq_w, mq_gr, mq_gl;
};
static struct text_entry cache[TEXT_CACHE];
static unsigned cache_clock;

static SDL_Texture *make_glow(SDL_Renderer *r)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, GLOW_SIZE, GLOW_SIZE, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	int x, y;
	if (!s) return NULL;
	for (y = 0; y < GLOW_SIZE; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
		for (x = 0; x < GLOW_SIZE; x++) {
			float dx = (x - GLOW_SIZE / 2.0f) / (GLOW_SIZE / 2.0f);
			float dy = (y - GLOW_SIZE / 2.0f) / (GLOW_SIZE / 2.0f);
			float d = sqrtf(dx * dx + dy * dy);
			float a = 1.0f - d;
			if (a < 0) a = 0;
			a = a * a * a;   /* a soft shoulder, no visible edge */
			row[x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(a * 255.0f));
		}
	}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	return t;
}


/* A role's size, in pixels at the 1024x768 the launcher draws at. */
static int role_pt(int role)
{
	return (int)(FONT_BASE * font_mul[role] + 0.5f);
}

/* DS_FONT_SEMIBOLD in the folder `menu` is in. */
static void title_path_beside(const char *menu, char *out, size_t n)
{
	const char *slash = strrchr(menu, '/');

	if (slash)
		snprintf(out, n, "%.*s/%s", (int)(slash - menu), menu, DS_FONT_SEMIBOLD);
	else
		snprintf(out, n, "%s", DS_FONT_SEMIBOLD);
}

/* ---- measured text, remembered ------------------------------------------ */

/* The same few strings are measured on every frame a screen draws: the
 * games shelf balances its title over two lines (4 to 25 TTF_SizeUTF8
 * calls, 21 to 293 us on an M3), the home fits the song's title (43 calls,
 * 1.85 ms for a 60-character one), a marquee measures its line. Each is a
 * pure function of the face, the text and the box, so the last answers are
 * kept here and handed back.
 *
 * The main thread's only, so nothing here is shared. Emptied whenever a face the main thread uses is closed, so a
 * new face that lands at a closed one's address is never answered for it. */
#define MEMO_N     64
#define MEMO_TEXT  256
#define MEMO_LINES 3

enum { MEMO_FREE, MEMO_WIDTH, MEMO_FIT, MEMO_BALANCE };

typedef struct {
	unsigned char kind;
	unsigned      used;          /* memo_clock when last handed back */
	TTF_Font     *f;
	int           maxw, cap;
	size_t        dstn;
	int           n;             /* the width, or the number of lines */
	char          text[MEMO_TEXT];
	char          out[MEMO_LINES][LB_LINE];
} memo_entry;

static memo_entry memo[MEMO_N];
static unsigned   memo_clock;
static SDL_threadID memo_thread;
static bool       memo_thread_set;

static void memo_clear(void)
{
	memset(memo, 0, sizeof memo);
}

/* On the main thread, and a text short enough to keep */
static bool memo_usable(const char *text)
{
	return memo_thread_set && SDL_ThreadID() == memo_thread &&
	       text && strlen(text) < MEMO_TEXT;
}

static memo_entry *memo_find(int kind, TTF_Font *f, const char *text,
                             int maxw, int cap, size_t dstn)
{
	int i;

	for (i = 0; i < MEMO_N; i++) {
		memo_entry *e = &memo[i];

		if (e->kind == kind && e->f == f && e->maxw == maxw && e->cap == cap &&
		    e->dstn == dstn && !strcmp(e->text, text)) {
			e->used = ++memo_clock;
			return e;
		}
	}
	return NULL;
}

/* The least recently used place, for a new answer */
static memo_entry *memo_slot(int kind, TTF_Font *f, const char *text,
                             int maxw, int cap, size_t dstn)
{
	memo_entry *e = &memo[0];
	int i;

	for (i = 1; i < MEMO_N && e->kind != MEMO_FREE; i++)
		if (memo[i].kind == MEMO_FREE || memo[i].used < e->used) e = &memo[i];
	e->kind = (unsigned char)kind;
	e->used = ++memo_clock;
	e->f = f;
	e->maxw = maxw;
	e->cap = cap;
	e->dstn = dstn;
	snprintf(e->text, sizeof e->text, "%s", text);
	return e;
}

/* Whether the renderer's presents wait for the panel: see ui_frame_rest */
static bool vsync_on;

void ui_frame_rest(Uint32 ms)
{
	if (!vsync_on) SDL_Delay(ms);
}

bool ui_init(SDL_Renderer *r, const char *font_path)
{
	SDL_RendererInfo ri;
	int i;

	/* The thread that draws, and the only one the memo answers */
	memo_thread = SDL_ThreadID();
	memo_thread_set = true;
	memo_clear();
	/* Granted, not asked for: a driver may decline PRESENTVSYNC in silence */
	vsync_on = SDL_GetRendererInfo(r, &ri) == 0 && (ri.flags & SDL_RENDERER_PRESENTVSYNC);

	if (TTF_Init() != 0) {
		fprintf(stderr, "ttf: %s\n", TTF_GetError());
		return false;
	}
	snprintf(font_path_kept, sizeof font_path_kept, "%s", font_path);
	title_path_beside(font_path, title_path_kept, sizeof title_path_kept);
	fonts = font_sets[SET_LATIN];
	for (i = 0; i < UI_F_COUNT; i++) {
		int pt = role_pt(i);
		const char *path = i == UI_F_TITLE ? title_path_kept : font_path;

		fonts[i] = TTF_OpenFont(path, pt);
		if (!fonts[i] && i == UI_F_TITLE) {
			/* Said once a run: ui_init runs again after every fallback
			 * game, and the button glyphs open the title path at their own
			 * sizes later, which is menu.ttf's from here on. */
			static bool said;

			if (!said)
				fprintf(stderr, "font: %s; the title is in %s\n",
				        TTF_GetError(), font_path);
			said = true;
			snprintf(title_path_kept, sizeof title_path_kept, "%s", font_path);
			fonts[i] = TTF_OpenFont(font_path, pt);
		}
		if (!fonts[i])
			fprintf(stderr, "font %s @%d: %s\n", path, pt, TTF_GetError());
	}
	glow_tex = make_glow(r);
	shadows_make(r);
	/* ui_init after ui_quit, a fallback game's, in 简体中文 or 日本語: its
	 * faces again */
	if (set_on != SET_LATIN) {
		set_open(set_on);
		if (font_sets[set_on][UI_F_MENU]) fonts = font_sets[set_on];
	}
	/* The title is a file of its own, so it opening says nothing of the
	 * others: menu.ttf missing, with title.ttf there, is a launcher with no
	 * menus, labels or counts. */
	return fonts[UI_F_TITLE] && fonts[UI_F_MENU];
}

static void glyphs_free(void);
static void digits_free(void);
static void button_fonts_free(void);

void ui_quit(void)
{
	memo_clear();
	glyphs_free();
	digits_free();
	shadows_free();
	for (int i = 0; i < TEXT_CACHE; i++)
		if (cache[i].tex) { SDL_DestroyTexture(cache[i].tex); cache[i].tex = NULL; }
	if (glow_tex) { SDL_DestroyTexture(glow_tex); glow_tex = NULL; }
	button_fonts_free();
	for (int k = 0; k < SETS; k++) {
		for (int i = 0; i < UI_F_COUNT; i++)
			if (font_sets[k][i]) { TTF_CloseFont(font_sets[k][i]); font_sets[k][i] = NULL; }
		set_tried[k] = false;
	}
	fonts = font_sets[SET_LATIN];
	if (f_mark) { TTF_CloseFont(f_mark); f_mark = NULL; }
	TTF_Quit();
}

TTF_Font *ui_font(ui_font_role role)
{
	if (role < 0 || role >= UI_F_COUNT) return NULL;
	return fonts[role];
}

/* A set's files beside menu.ttf: its title's and the rest's */
static const char *const SET_FILES[SETS][2] = {
	[SET_LATIN] = { DS_FONT_REGULAR, DS_FONT_SEMIBOLD },
	[SET_SC]    = { DS_FONT_CJK, DS_FONT_CJK_TITLE },
	[SET_JP]    = { DS_FONT_JA, DS_FONT_JA_TITLE },
};

static int set_of(lang l)
{
	return l == LANG_ZH_HANS ? SET_SC : l == LANG_JA ? SET_JP : SET_LATIN;
}

static void set_path(int set, bool title, char *out, size_t n)
{
	const char *slash = strrchr(font_path_kept, '/');
	int dir = slash ? (int)(slash - font_path_kept) + 1 : 0;

	snprintf(out, n, "%.*s%s", dir, font_path_kept, SET_FILES[set][title]);
}

/* A Chinese or Japanese set, every role at its size, once: a missing file is
 * a line in the log, and its language is then drawn in Lexend, which has
 * none of its characters, rather than not at all. */
static void set_open(int set)
{
	char menu[512], title[512];

	if (set == SET_LATIN || set_tried[set] || !font_path_kept[0]) return;
	set_tried[set] = true;
	set_path(set, false, menu, sizeof menu);
	set_path(set, true, title, sizeof title);
	for (int i = 0; i < UI_F_COUNT; i++) {
		font_sets[set][i] = TTF_OpenFont(i == UI_F_TITLE ? title : menu, role_pt(i));
		if (!font_sets[set][i]) font_sets[set][i] = TTF_OpenFont(menu, role_pt(i));
	}
	if (!font_sets[set][UI_F_MENU])
		fprintf(stderr, "font: %s: %s; drawn in Lexend\n", menu, TTF_GetError());
}

void ui_fonts_for(lang l)
{
	set_on = set_of(l);
	set_open(set_on);
	fonts = font_sets[set_on][UI_F_MENU] ? font_sets[set_on] : font_sets[SET_LATIN];
}

bool ui_fonts_cjk(void) { return fonts != font_sets[SET_LATIN]; }

int ui_font_set(void)
{
	return fonts == font_sets[SET_SC] ? 1 : fonts == font_sets[SET_JP] ? 2 : 0;
}

const char *ui_cjk_font_path(bool title, char *out, size_t n)
{
	set_path(set_on == SET_LATIN ? SET_SC : set_on, title, out, n);
	return out;
}

TTF_Font *ui_font_lang(ui_font_role role, lang l)
{
	int set = set_of(l);

	if (role < 0 || role >= UI_F_COUNT) return NULL;
	set_open(set);
	return font_sets[set][role] ? font_sets[set][role] : fonts[role];
}

int ui_font_line(ui_font_role role)
{
	return ui_font(role) ? role_pt(role) : 0;
}

int ui_line_y(ui_font_role role, int top)
{
	TTF_Font *f = ui_font(role);

	return f ? ui_cap_y(f, top + ui_font_line(role) * 2 / 5) : top;
}

/* ---- the MENU key in running text -----------------------------------------
 *
 * The Pixel 2's MENU is a round graphite key with three dimples in a line
 * down the device's height (tools/build-pixel2-model.py: radius 2.65 mm, the
 * dimples 0.3 mm across and 1.05 mm apart). 2026-11-05: wherever the
 * system says MENU it shows that key, not the word. So the word MENU, alone
 * or before +, >, a space or a stop, is drawn as a ring in the text's own
 * color around three dots, centered on the capitals ("Show Tutorial is in
 * MENU.", "MENU+VOL", "MENU > Controls"); "Menu" and "menus" stay words.
 * ui_text_width measures it the same way, so fitting, breaking and
 * centering all see the glyph. A key on its own is ui_button's round face. */
static bool word_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '_';
}

/* The next whole word MENU at or after p (s is the string's start), or NULL */
static const char *menu_word(const char *s, const char *p)
{
	for (; (p = strstr(p, "MENU")); p += 4)
		if ((p == s || !word_char(p[-1])) && !word_char(p[4])) return p;
	return NULL;
}

typedef struct {
	float d, ring, dot, gap, cy;  /* ring's outside, its width, a dot's radius,
	                                 * dot to dot, the middle in the line */
	int side, adv;                /* room either side; the whole width */
} menu_mark;

static menu_mark menu_mark_of(TTF_Font *f)
{
	float cap = (float)ui_cap_height(f);
	menu_mark m;

	m.d = roundf(cap * 1.3f);
	m.ring = fmaxf(1.25f, m.d * 0.1f);
	m.dot = fmaxf(1.0f, m.d * 0.095f);
	m.gap = m.d * 0.235f;
	m.side = (int)lroundf(cap * 0.12f);
	m.adv = (int)m.d + 2 * m.side;
	m.cy = (float)TTF_FontAscent(f) - cap / 2.0f;
	return m;
}

/* Whether s has a character Lexend lacks and the Chinese face has: the
 * ideographs, kana, CJK punctuation and the full-width forms */
static bool has_cjk(const char *s)
{
	const unsigned char *u = (const unsigned char *)s;

	for (; *u; u++) {
		unsigned c;

		if (*u < 0xE2 || *u > 0xEF || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80) continue;
		c = (*u & 0x0Fu) << 12 | (u[1] & 0x3Fu) << 6 | (u[2] & 0x3F);
		if ((c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF))
			return true;
	}
	return false;
}

/* Whether face f has every Chinese or Japanese character of s */
static bool covers(TTF_Font *f, const char *s)
{
	const unsigned char *u = (const unsigned char *)s;

	for (; *u; u++) {
		unsigned c;

		if (*u < 0xE2 || *u > 0xEF || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80) continue;
		c = (*u & 0x0Fu) << 12 | (u[1] & 0x3Fu) << 6 | (u[2] & 0x3F);
		if (c >= 0x2E80 && !TTF_GlyphIsProvided32(f, c)) return false;
		u += 2;
	}
	return true;
}

/* Whether s has kana: Japanese, whatever else is in it */
static bool has_kana(const char *s)
{
	const unsigned char *u = (const unsigned char *)s;

	for (; u[0] && u[1] && u[2]; u++)
		if (u[0] == 0xE3 && (u[1] == 0x81 || u[1] == 0x82 || u[1] == 0x83)) return true;
	return false;
}

/* The face s is drawn in when f is asked for: f, or where s has Chinese or
 * Japanese characters f has not, the first face of f's role that has them
 * all, the set that is on first, then Japanese's where s has kana and
 * Chinese's where it has none. So 简体中文 and 日本語 are themselves in MENU >
 * Language's panel in any language, and a game named in either is drawn
 * rather than boxes. Asked only where a line is measured or rendered afresh:
 * what ui_text has cached was drawn this way already. */
static TTF_Font *face_for(TTF_Font *f, const char *s)
{
	int role = -1, order[3];

	if (!has_cjk(s) || covers(f, s)) return f;
	for (int k = 0; k < SETS && role < 0; k++)
		for (int i = 0; i < UI_F_COUNT; i++)
			if (f == font_sets[k][i]) { role = i; break; }
	if (role < 0) return f;
	order[0] = set_on;
	/* Japanese where it has kana, or where it is Japanese's own name, which
	 * is kanji alone */
	order[1] = has_kana(s) || !strcmp(s, lang_name(LANG_JA)) ? SET_JP : SET_SC;
	order[2] = order[1] == SET_JP ? SET_SC : SET_JP;
	for (int k = 0; k < 3; k++) {
		if (order[k] == SET_LATIN) continue;
		set_open(order[k]);
		if (font_sets[order[k]][role] && covers(font_sets[order[k]][role], s))
			return font_sets[order[k]][role];
	}
	return f;
}

static int text_width(TTF_Font *f, const char *s)
{
	const char *p, *m;
	char part[512];
	int w = 0, pw;

	if (!f || !s) return 0;
	f = face_for(f, s);
	if (!menu_word(s, s)) {
		TTF_SizeUTF8(f, s, &w, NULL);
		return w;
	}
	for (p = s; *p; p = m + 4) {
		size_t n;

		m = menu_word(s, p);
		n = m ? (size_t)(m - p) : strlen(p);
		if (n) {
			snprintf(part, sizeof part, "%.*s", (int)n, p);
			pw = 0;
			TTF_SizeUTF8(f, part, &pw, NULL);
			w += pw;
		}
		if (!m) break;
		w += menu_mark_of(f).adv;
	}
	return w;
}

int ui_text_width(TTF_Font *f, const char *s)
{
	memo_entry *e;
	int w;

	if (!f || !s) return 0;
	if (!memo_usable(s)) return text_width(f, s);
	if ((e = memo_find(MEMO_WIDTH, f, s, 0, 0, 0))) return e->n;
	w = text_width(f, s);
	memo_slot(MEMO_WIDTH, f, s, 0, 0, 0)->n = w;
	return w;
}

/* The ring and its dots into an ARGB8888 surface at x, by 4 x 4 samples a
 * pixel, so the small sizes keep their round edge. */
static void menu_mark_put(SDL_Surface *dst, int x, menu_mark m, SDL_Color col)
{
	const float R = m.d / 2.0f, cx = (float)x + (float)m.side + R, cy = m.cy;
	int y0 = (int)floorf(cy - R - 1.0f), y1 = (int)ceilf(cy + R + 1.0f);

	if (y0 < 0) y0 = 0;
	if (y1 > dst->h) y1 = dst->h;
	for (int py = y0; py < y1; py++) {
		Uint32 *row = (Uint32 *)((Uint8 *)dst->pixels + (size_t)py * dst->pitch);

		for (int px = x; px < x + m.adv && px < dst->w; px++) {
			int hit = 0;

			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					float fx = (float)px + ((float)sx + 0.5f) / 4.0f - cx;
					float fy = (float)py + ((float)sy + 0.5f) / 4.0f - cy;
					float d = sqrtf(fx * fx + fy * fy);
					bool in = d <= R && d >= R - m.ring;

					for (int k = -1; k <= 1 && !in; k++) {
						float gy = fy - (float)k * m.gap;

						in = fx * fx + gy * gy <= m.dot * m.dot;
					}
					hit += in;
				}
			if (hit) row[px] = SDL_MapRGBA(dst->format, col.r, col.g, col.b,
			                                (Uint8)((hit * 255 + 8) / 16));
		}
	}
}

SDL_Surface *ui_text_surface(TTF_Font *f, const char *s, SDL_Color col)
{
	const char *p, *m;
	SDL_Surface *out;
	char part[512];
	int x = 0;

	if (!f || !s || !*s) return NULL;
	f = face_for(f, s);
	if (!menu_word(s, s)) return TTF_RenderUTF8_Blended(f, s, col);
	out = SDL_CreateRGBSurfaceWithFormat(0, ui_text_width(f, s), TTF_FontHeight(f),
	                                     32, SDL_PIXELFORMAT_ARGB8888);
	if (!out) return NULL;
	SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, col.r, col.g, col.b, 0));
	for (p = s; *p; p = m + 4) {
		size_t n;

		m = menu_word(s, p);
		n = m ? (size_t)(m - p) : strlen(p);
		if (n) {
			SDL_Surface *t;

			snprintf(part, sizeof part, "%.*s", (int)n, p);
			if ((t = TTF_RenderUTF8_Blended(f, part, col))) {
				SDL_SetSurfaceBlendMode(t, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(t, NULL, out, &(SDL_Rect){ x, 0, 0, 0 });
				x += t->w;
				SDL_FreeSurface(t);
			}
		}
		if (!m) break;
		if (SDL_LockSurface(out) == 0) {
			menu_mark_put(out, x, menu_mark_of(f), col);
			SDL_UnlockSurface(out);
		}
		x += menu_mark_of(f).adv;
	}
	return out;
}

/* ---- what does not fit, said on a host ------------------------------------
 *
 * A string from res/strings that a screen had to cut, break past its lines
 * or draw off the screen, in the language that is on: a line on stderr,
 * once a string, "i18n: cut: ..." or "i18n: off screen: ...", which
 * tools/i18n-shots.py fails on (make check-i18n-shots). Only where the
 * pseudo-locale is built in, which is the host's build: a device never
 * looks. A table string is known by where it lives (i18n_owns); in the
 * pseudo-locale, whose every string ends "·]", by that too, so a line
 * formatted from one is caught as well. */
#ifdef I18N_PSEUDO
static bool i18n_watched(const char *s)
{
	return s && (i18n_owns(s) || (i18n_lang() == LANG_PSEUDO && strstr(s, "\xc2\xb7]")));
}

static void i18n_note(const char *what, const char *s)
{
	static unsigned said[256];
	unsigned h = 5381;

	for (const char *p = s; *p; p++) h = h * 33 + (unsigned char)*p;
	for (int i = 0; i < 256 && said[i]; i++) if (said[i] == h) return;
	for (int i = 0; i < 256; i++) if (!said[i]) { said[i] = h; break; }
	fprintf(stderr, "i18n: %s: %s\n", what, s);
}

/* Text formatted from a string ("linked as %s") is not the table's, so
 * its cut is said as data: tools/i18n-shots.py fails on a screen whose
 * data cuts outnumber English's on the same screen, which is a cut the
 * translation made */
static void i18n_fit_seen(const char *src, const char *dst)
{
	if (src && strcmp(src, dst)) i18n_note(i18n_watched(src) ? "cut" : "cut data", src);
}
#else
#define i18n_fit_seen(src, dst) ((void)0)
#endif

static struct text_entry *text_get(SDL_Renderer *r, TTF_Font *f, const char *s,
                                  SDL_Color col)
{
	unsigned rgb = ((unsigned)col.r << 16) | ((unsigned)col.g << 8) | col.b;
	int i, oldest = 0;

	if (!f || !s || !*s) return NULL;
	for (i = 0; i < TEXT_CACHE; i++) {
		if (cache[i].tex && cache[i].font == f && cache[i].rgb == rgb &&
		    strcmp(cache[i].str, s) == 0) {
			cache[i].used = ++cache_clock;
			return &cache[i];
		}
		if (!cache[i].tex) { oldest = i; goto fill; }
		if (cache[i].used < cache[oldest].used) oldest = i;
	}
fill:
	if (cache[oldest].tex) SDL_DestroyTexture(cache[oldest].tex);
	memset(&cache[oldest], 0, sizeof cache[oldest]);
	{
		/* Opacity belongs to the draw, not the cached glyphs. The cache key
		 * holds RGB only; baking the first fade frame's alpha here would
		 * leave every later frame dim, even at full texture modulation. */
		col.a = 255;
		SDL_Surface *surf = ui_text_surface(f, s, col);
		TTF_Font *g = face_for(f, s);
		if (!surf) return NULL;
		if (g != f)
			cache[oldest].dy = (TTF_FontAscent(f) - TTF_FontAscent(g)) +
			                   (ui_cap_height(g) - ui_cap_height(f)) / 2;
		cache[oldest].tex = SDL_CreateTextureFromSurface(r, surf);
		cache[oldest].w = surf->w;
		cache[oldest].h = surf->h;
		SDL_FreeSurface(surf);
	}
	if (!cache[oldest].tex) return NULL;
	cache[oldest].font = f;
	cache[oldest].rgb = rgb;
	snprintf(cache[oldest].str, sizeof cache[oldest].str, "%s", s);
	cache[oldest].used = ++cache_clock;
	return &cache[oldest];
}

int ui_text(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
            int anchor, SDL_Color col)
{
	struct text_entry *e = text_get(r, f, s, col);
	SDL_Rect dst;
	if (!e) return 0;
	dst.w = e->w;
	dst.h = e->h;
	dst.x = anchor == 0 ? x - e->w / 2 : (anchor > 0 ? x - e->w : x);
	dst.y = y + e->dy;
#ifdef I18N_PSEUDO
	/* Past the screen's edge (the 1024 units' design space; an ink's
	 * width short of it, as a line's texture has room either side) */
	if ((dst.x < -4 || dst.x + e->w > 1024 + 4) && i18n_watched(s)) i18n_note("off screen", s);
#endif
	SDL_SetTextureAlphaMod(e->tex, col.a);
	SDL_RenderCopy(r, e->tex, NULL, &dst);
	return e->w;
}


int ui_cap_height(TTF_Font *f)
{
	int mnx, mxx, mny, mxy, adv;

	if (f && TTF_GlyphMetrics(f, 'H', &mnx, &mxx, &mny, &mxy, &adv) == 0)
		return mxy;
	return f ? TTF_FontAscent(f) : 0;
}

int ui_cap_y(TTF_Font *f, int mid)
{
	if (!f) return mid;
	return mid - TTF_FontAscent(f) + ui_cap_height(f) / 2;
}

/* ---- tabular digits ---------------------------------------------------------
 *
 * The digits of a font, each rendered once in white and tinted as it is
 * drawn, apart from the string cache: a clock would otherwise put a string a
 * second into a cache of twelve and push out the title it sits under. One slot
 * per font, and Now Playing uses one font.
 *
 * A digit is made the first time it is drawn, not all ten with the font's
 * first string: a texture is about 1.2 ms on the Pixel 2, and the first frame
 * drew the corner's clock and battery percentage in two fonts, 20 digits for
 * the six it showed - 24 ms of a 380 ms start (2026-11-07). */
#define DIGIT_FONTS 3
static struct {
	TTF_Font    *f;
	SDL_Texture *tex[10];
	int          w[10], h;
	bool         tried[10];          /* asked for: made, or failed for good */
} digits[DIGIT_FONTS];

static void digits_free(void)
{
	int i, d;

	for (i = 0; i < DIGIT_FONTS; i++) {
		for (d = 0; d < 10; d++)
			if (digits[i].tex[d]) SDL_DestroyTexture(digits[i].tex[d]);
		memset(&digits[i], 0, sizeof digits[i]);
	}
}

/* The width every digit is given: the widest one's advance. */
static int digit_cell(TTF_Font *f)
{
	int d, cell = 0, minx, maxx, miny, maxy, adv;

	for (d = 0; d < 10; d++)
		if (TTF_GlyphMetrics(f, (Uint16)('0' + d), &minx, &maxx, &miny, &maxy, &adv) == 0 &&
		    adv > cell)
			cell = adv;
	return cell;
}

/* The slot for `f`'s digits, claimed if it has none; digit_tex fills it */
static int digits_for(TTF_Font *f)
{
	int i, d, slot = -1;

	for (i = 0; i < DIGIT_FONTS; i++) {
		if (digits[i].f == f) return i;
		if (!digits[i].f && slot < 0) slot = i;
	}
	if (slot < 0) {                      /* all taken: the first makes room */
		for (d = 0; d < 10; d++)
			if (digits[0].tex[d]) SDL_DestroyTexture(digits[0].tex[d]);
		memset(&digits[0], 0, sizeof digits[0]);
		slot = 0;
	}
	digits[slot].f = f;
	return slot;
}

/* Digit `d` of slot `k`, made on its first draw; NULL if it cannot be */
static SDL_Texture *digit_tex(SDL_Renderer *r, int k, int d)
{
	static const SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface *s;

	if (digits[k].tried[d]) return digits[k].tex[d];
	digits[k].tried[d] = true;
	if (!(s = TTF_RenderGlyph_Blended(digits[k].f, (Uint16)('0' + d), white))) return NULL;
	digits[k].tex[d] = SDL_CreateTextureFromSurface(r, s);
	digits[k].w[d] = s->w;
	digits[k].h = s->h;
	SDL_FreeSurface(s);
	return digits[k].tex[d];
}

/* The next run of `s`: digits one at a time, anything else up to the next
 * digit. Returns its length, and the run in `out`. */
static size_t tab_run(const char *s, char *out, size_t n)
{
	size_t len = 1;

	if (!(*s >= '0' && *s <= '9'))
		while (s[len] && !(s[len] >= '0' && s[len] <= '9')) len++;
	if (len >= n) len = n - 1;
	memcpy(out, s, len);
	out[len] = '\0';
	return len;
}

int ui_text_tabular_width(TTF_Font *f, const char *s)
{
	char run[128];
	int w = 0, cell;

	if (!f || !s) return 0;
	cell = digit_cell(f);
	while (*s) {
		s += tab_run(s, run, sizeof run);
		w += run[0] >= '0' && run[0] <= '9' ? cell : ui_text_width(f, run);
	}
	return w;
}

int ui_text_tabular(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
                    int anchor, SDL_Color col)
{
	char run[128];
	int w, cell, at, k;

	if (!f || !s || !*s) return 0;
	w = ui_text_tabular_width(f, s);
	cell = digit_cell(f);
	k = digits_for(f);
	at = anchor == 0 ? x - w / 2 : (anchor > 0 ? x - w : x);
	while (*s) {
		s += tab_run(s, run, sizeof run);
		if (run[0] >= '0' && run[0] <= '9') {
			int d = run[0] - '0';
			SDL_Texture *t = digit_tex(r, k, d);

			if (t) {
				/* Centered. At the cell's right edge, tried 2026-09-29, a
				 * thin digit closed up on the digit after it and opened a
				 * gap on the other side instead: "- 1:00: 10". */
				SDL_Rect dst = { at + (cell - digits[k].w[d]) / 2, y,
				                 digits[k].w[d], digits[k].h };

				SDL_SetTextureColorMod(t, col.r, col.g, col.b);
				SDL_SetTextureAlphaMod(t, col.a);
				SDL_RenderCopy(r, t, NULL, &dst);
			}
			at += cell;
		} else {
			at += ui_text(r, f, run, at, y, -1, col);
		}
	}
	return w;
}

/* Tuned by eye, and the first is the one that matters - see ui.h.
 *
 * 70 px/s is slow enough to read at arm's length on a three-inch panel and
 * fast enough that a 300px overrun is done in four seconds. The pause at the
 * far end stops the reversal reading as jitter: without it the text arrives
 * and instantly leaves, which looks like a glitch rather than an end. */
#define MQ_FADE_PX     36          /* how far the edges dissolve */
#define MQ_FADE_STEP    3
#define MQ_HOLD_MS   1400          /* stillness before it starts */
#define MQ_END_MS     900          /* stillness at the far end */
#define MQ_SPEED_PXPS  70

/* How far into a ping-pong a given phase has got: still at 0 for a beat, out
 * to `over` at a walking pace, still again at the far end, then back.
 *
 * Shared rather than written twice. A long title scrolling sideways on the
 * shelf and a long description scrolling down a card are the same gesture, and
 * two copies of this arithmetic would drift the moment either was retuned. */
int ui_pingpong(int over, unsigned phase)
{
	int travel = over * 1000 / MQ_SPEED_PXPS;
	unsigned cycle, p;

	if (over <= 0) return 0;
	if (travel < 1) travel = 1;
	cycle = (unsigned)(MQ_HOLD_MS + travel + MQ_END_MS + travel);
	p     = phase % cycle;

	if (p < MQ_HOLD_MS) return 0;
	if (p < (unsigned)(MQ_HOLD_MS + travel))
		return (int)((p - MQ_HOLD_MS) * (unsigned)over / (unsigned)travel);
	if (p < (unsigned)(MQ_HOLD_MS + travel + MQ_END_MS)) return over;
	return over - (int)((p - MQ_HOLD_MS - travel - MQ_END_MS)
	                    * (unsigned)over / (unsigned)travel);
}

unsigned ui_pingpong_reach(int over)
{
	int travel = over * 1000 / MQ_SPEED_PXPS;

	if (over <= 0) return 0;
	return (unsigned)(MQ_HOLD_MS + (travel < 1 ? 1 : travel) + MQ_END_MS);
}

int ui_scrollthrough(int cycle, unsigned phase)
{
	int travel = cycle * 1000 / MQ_SPEED_PXPS;
	unsigned lap, p;

	if (cycle <= 0) return 0;
	if (travel < 1) travel = 1;
	lap = (unsigned)(MQ_HOLD_MS + travel);
	p   = phase % lap;
	if (p < MQ_HOLD_MS) return 0;
	return (int)((p - MQ_HOLD_MS) * (unsigned)cycle / (unsigned)travel);
}

/* The same cycle as ui_pingpong, phase for phase - it has to be, or a caller
 * would wake early, costing a frame, or late, holding back a scroll that should
 * already have started. */
unsigned ui_pingpong_wait(int over, unsigned phase)
{
	int travel = over * 1000 / MQ_SPEED_PXPS;
	unsigned cycle, p;

	if (over <= 0) return (unsigned)-1;         /* fits, so it never moves */
	if (travel < 1) travel = 1;
	cycle = (unsigned)(MQ_HOLD_MS + travel + MQ_END_MS + travel);
	p     = phase % cycle;

	if (p < MQ_HOLD_MS) return MQ_HOLD_MS - p;
	if (p < (unsigned)(MQ_HOLD_MS + travel)) return 0;
	if (p < (unsigned)(MQ_HOLD_MS + travel + MQ_END_MS))
		return (unsigned)(MQ_HOLD_MS + travel + MQ_END_MS) - p;
	return 0;
}

static int ttf_width(void *f, const char *s);

/* `c` as the clip, inside the caller's own when it has one. False when
 * nothing of it is left, and then nothing is to be drawn.
 *
 * The marquee used to replace the caller's clip with its own box. A scrolled
 * menu draws the row just above its window too, for the give (menu_draw_ex),
 * and relies on its clip to hide it - so a long value in that row, which
 * scrolls, drew over the heading: "not in this bu" under "ZyperOS", seen
 * 2026-11-06 once SteamGridDB's row put ScreenScraper's there. */
static bool clip_in(SDL_Renderer *r, const SDL_Rect *c, const SDL_Rect *outer)
{
	SDL_Rect in;

	if (c->w <= 0 || c->h <= 0) return false;
	if (!outer) { SDL_RenderSetClipRect(r, c); return true; }
	if (!SDL_IntersectRect(c, outer, &in)) return false;
	SDL_RenderSetClipRect(r, &in);
	return true;
}

void ui_text_marquee(SDL_Renderer *r, TTF_Font *f, const char *s,
                     int x, int y, int w, unsigned phase, SDL_Color col)
{
	int tw = ui_text_width(f, s);
	int over, off, i;
	SDL_Rect clip, was;
	SDL_bool had;

	if (!f || !s || w <= 0) return;
	if (tw <= w) { ui_text(r, f, s, x, y, -1, col); return; }
#ifdef I18N_PSEUDO
	/* A name slides because it is data (a game, an SSID); a string of the
	 * UI that slides was cut, only slowly: My rule wants it short
	 * (MENU's "Transferir arquivos por Wi-Fi" beside "requer Wi-Fi") */
	i18n_note(i18n_watched(s) ? "slides" : "slides data", s);
#endif

	over = tw - w;
	off  = ui_pingpong(over, phase);

	/* Nested clips: the caller may already have one, and dropping it would
	 * let this draw outside whatever panel it sits in. */
	had = SDL_RenderIsClipEnabled(r);
	if (had) SDL_RenderGetClipRect(r, &was);
	clip.x = x; clip.y = y;
	clip.w = w; clip.h = TTF_FontHeight(f);
	/* All of it outside the caller's clip: nothing to draw */
	if (!clip_in(r, &clip, had ? &was : NULL)) {
		SDL_RenderSetClipRect(r, had ? &was : NULL);
		return;
	}

	/* Faded at both edges, and faded in the TEXT rather than by laying a
	 * gradient of the background over it. The background here is a coverflow
	 * with a vignette, not a flat color, so anything painted on top would
	 * show as a band. Fading the glyphs works over whatever is behind them.
	 *
	 * Without it the text is chopped mid-stroke - and on the shelf the left
	 * chop lands right beside the heart, where it reads as damage rather than
	 * as more text.
	 *
	 * EACH SLICE IS DRAWN EXACTLY ONCE, at its own alpha, with the clip
	 * deciding which part of the text lands. The first attempt drew the whole
	 * string at full alpha and then re-drew the edges dimmer on top, which
	 * cannot subtract - blending only adds - and used BLENDMODE_NONE to try to
	 * force it, which replaces the destination wholesale and painted two solid
	 * white blocks where the fades should have been. */
	{
		struct text_entry *e = text_get(r, f, s, col);
		SDL_Rect dst;

		if (!e) { SDL_RenderSetClipRect(r, had ? &was : NULL); return; }
		dst.x = x - off; dst.y = y; dst.w = e->w; dst.h = e->h;

		/* THE WINDOW ENDS BETWEEN LETTERS WHERE THE TEXT RESTS. Its ends at
		 * x and x + w fall wherever the letters do, and at the start the
		 * right one went through a letter: "Potatoes," and the first stroke
		 * of the W after it, faded but there, which read as a broken letter
		 * and not as more text. So at the start the right end is pulled in to
		 * the end of the last whole letter, and at the far end the left one
		 * out to the start of the first (lb_edges), and each eases back to
		 * the window's own end as the text moves off its rest: over the
		 * pulled-in part's width of travel, the end moving out as fast as the
		 * text moves in, so the cut letter is uncovered from its left at
		 * twice the scroll's pace and not all at once; or, when the whole
		 * overflow is shorter than that part, over the whole travel, which is
		 * faster still. In motion a letter crossing an
		 * end is the text moving, which is what a fade is for. Worked out
		 * once per string and width, since the text_entry outlives the
		 * frame. */
		int li = 0, ri = 0, ww, lf, rf, sl, sr;

		if (e->mq_w != w) {
			int in, out;

			/* A window narrower than the first letter, or the last, would
			 * rest empty pulled in to a whole one; it keeps its own end and
			 * shows the cut letter, as before. */
			lb_edges(s, w, ttf_width, f, &in, &out);
			e->mq_gr = in > 0 ? w - in : 0;
			lb_edges(s, over, ttf_width, f, &in, &out);
			e->mq_gl = out - over < w ? out - over : 0;
			e->mq_w = w;
		}
		sl = e->mq_gl < over ? e->mq_gl : over;
		sr = e->mq_gr < over ? e->mq_gr : over;
		if (sl > 0 && over - off < sl) li = e->mq_gl * (sl - (over - off)) / sl;
		if (sr > 0 && off < sr) ri = e->mq_gr * (sr - off) / sr;
		ww = w - li - ri;

		/* A FADE MEANS "MORE TEXT THIS WAY", so each side only fades when
		 * there is something hidden on it. Both faded unconditionally at
		 * first, which put a soft left edge on a title resting at its
		 * beginning - nothing was hidden there, and it read as the name
		 * starting halfway through a word.
		 *
		 * The width is the amount hidden, capped, the letters the window's
		 * ends were pulled in past included. So it grows from nothing as the
		 * text pulls away and shrinks back to nothing as it returns, which
		 * also means neither end pops; and it runs into the window's ends
		 * where they are, so at rest the last whole letter is the faded one. */
		lf = off + li < MQ_FADE_PX ? off + li : MQ_FADE_PX;
		rf = over - off + ri < MQ_FADE_PX ? over - off + ri : MQ_FADE_PX;
		if (ww < 0) ww = 0;
		if (lf + rf > ww) {           /* a window narrower than its fades */
			lf = lf + rf > 0 ? lf * ww / (lf + rf) : 0;
			rf = ww - lf;
		}

		clip.x = x + li + lf;
		clip.w = ww - lf - rf;
		if (clip_in(r, &clip, had ? &was : NULL)) {
			SDL_SetTextureAlphaMod(e->tex, col.a);
			SDL_RenderCopy(r, e->tex, NULL, &dst);
		}

		for (i = 0; i < lf; i += MQ_FADE_STEP) {
			SDL_SetTextureAlphaMod(e->tex, (Uint8)(col.a * i / lf));
			clip.x = x + li + i;
			clip.w = lf - i < MQ_FADE_STEP ? lf - i : MQ_FADE_STEP;
			if (clip_in(r, &clip, had ? &was : NULL))
				SDL_RenderCopy(r, e->tex, NULL, &dst);
		}
		for (i = 0; i < rf; i += MQ_FADE_STEP) {
			int sw = rf - i < MQ_FADE_STEP ? rf - i : MQ_FADE_STEP;

			SDL_SetTextureAlphaMod(e->tex, (Uint8)(col.a * i / rf));
			clip.x = x + w - ri - sw - i;
			clip.w = sw;
			if (clip_in(r, &clip, had ? &was : NULL))
				SDL_RenderCopy(r, e->tex, NULL, &dst);
		}
		SDL_SetTextureAlphaMod(e->tex, 255);
	}
	SDL_RenderSetClipRect(r, had ? &was : NULL);
}

/* src/linebreak.c's measure, by SDL_ttf. Not remembered: a fit or a balance
 * measures a dozen pieces of a line once, and is remembered whole below. */
static int ttf_width(void *f, const char *s)
{
	return text_width(f, s);
}

void ui_fit_text(TTF_Font *f, const char *src, char *dst, size_t dstn,
                 int maxw)
{
	memo_entry *e;

	if (!f) {
		snprintf(dst, dstn, "%s", src ? src : "");
		return;
	}
	if (!src || !dstn || dstn > LB_LINE || !memo_usable(src)) {
		lb_fit(src, dst, dstn, maxw, ttf_width, f);
		if (src) i18n_fit_seen(src, dst);
		return;
	}
	if ((e = memo_find(MEMO_FIT, f, src, maxw, 0, dstn))) {
		memcpy(dst, e->out[0], dstn);
		return;
	}
	lb_fit(src, dst, dstn, maxw, ttf_width, f);
	i18n_fit_seen(src, dst);
	e = memo_slot(MEMO_FIT, f, src, maxw, 0, dstn);
	memcpy(e->out[0], dst, dstn);
}

int ui_balance_lines(TTF_Font *f, const char *text, int maxw, int cap,
                     char lines[][LB_LINE])
{
	memo_entry *e;
	int n;

	if (!f) return 0;
	if (cap < 1 || cap > MEMO_LINES || !memo_usable(text)) {
		n = lb_balance(text, maxw, cap, ttf_width, f, lines);
#ifdef I18N_PSEUDO
		if (n > 0 && strstr(lines[n - 1], "...") && !strstr(text, "..."))
			i18n_note(i18n_watched(text) ? "cut" : "cut data", text);
#endif
		return n;
	}
	if ((e = memo_find(MEMO_BALANCE, f, text, maxw, cap, 0))) {
		memcpy(lines, e->out, (size_t)e->n * LB_LINE);
		return e->n;
	}
	n = lb_balance(text, maxw, cap, ttf_width, f, lines);
#ifdef I18N_PSEUDO
	/* Every line full and the last cut: the text is longer than its box */
	if (n > 0 && strstr(lines[n - 1], "...") && !strstr(text, "..."))
		i18n_note(i18n_watched(text) ? "cut" : "cut data", text);
#endif
	if (n < 0 || n > cap) return n;
	e = memo_slot(MEMO_BALANCE, f, text, maxw, cap, 0);
	e->n = n;
	memcpy(e->out, lines, (size_t)n * LB_LINE);
	return n;
}

void ui_text_memo_clear(void)
{
	memo_clear();
}

/* The glow texture stretched over `rect` times `spread`, in `rgb`, by `mode`.
 * The blend mode is set on every draw rather than once at load because the
 * one texture is two things: light ADDED to the ground in Salt & Pepper, and
 * on Beard the shelf's wash, the same shape laid on with an ordinary blend
 * (light added to a light ground is all but nothing to see).
 *
 * CFG_NO_COLOR, a system with no color and a menu that is no system's, is
 * lit in **text-dim**: a neutral light, as the shadow Beard lifts with is a
 * neutral dark. */
static void glow_draw(SDL_Renderer *r, const SDL_Rect *rect, unsigned rgb,
                      int alpha, float spread, SDL_BlendMode mode,
                      const ds_theme *t)
{
	SDL_Rect dst;
	int gw, gh;
	if (!glow_tex) return;
	if (rgb == CFG_NO_COLOR) rgb = ui_rgb(t->text_dim);
	gw = (int)(rect->w * spread);
	gh = (int)(rect->h * spread);
	dst.x = rect->x + rect->w / 2 - gw / 2;
	dst.y = rect->y + rect->h / 2 - gh / 2;
	dst.w = gw;
	dst.h = gh;
	SDL_SetTextureBlendMode(glow_tex, mode);
	SDL_SetTextureColorMod(glow_tex, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
	SDL_SetTextureAlphaMod(glow_tex, (Uint8)alpha);
	SDL_RenderCopy(r, glow_tex, NULL, &dst);
}

/* ---- lift -------------------------------------------------------------------
 *
 * DESIGN.md 7's table, one row per kind: the glow Salt & Pepper lifts with,
 * and the shadow Beard lifts with, whose blur and corner are src/blur.h's
 * BLUR_KIND (the one place they are paired, which blur-check tests). The
 * wash has no shadow: on Beard it is DS_WASH_LIGHT_A of the system's color,
 * normal blend, in the glow's shape. */
static const struct {
	int   glow_a;    /* Salt & Pepper: alpha 0-255 */
	float spread;    /* and how far past the rect, as a multiple of it */
	int   y;         /* Beard: drop, units */
	int   a;         /* ink opacity 0-255, or the wash's for UI_LIFT_WASH */
	int   shadow;    /* its BLUR_KIND: the blur and the corner; -1 for none */
} lift[UI_LIFT_KINDS] = {
	[UI_LIFT_FOCUS] = { DS_GLOW_FOCUS_A, DS_GLOW_FOCUS_SPREAD,
		DS_SHADOW_FOCUS_Y, DS_SHADOW_FOCUS_A, BLUR_FOCUS },
	[UI_LIFT_FOCUS_SMALL] = { DS_GLOW_FOCUS_SMALL_A, DS_GLOW_FOCUS_SMALL_SPREAD,
		DS_SHADOW_FOCUS_SMALL_Y, DS_SHADOW_FOCUS_SMALL_A, BLUR_FOCUS_SMALL },
	[UI_LIFT_PANEL] = { DS_GLOW_PANEL_A, DS_GLOW_PANEL_SPREAD,
		DS_SHADOW_PANEL_Y, DS_SHADOW_PANEL_A, BLUR_PANEL },
	[UI_LIFT_WASH] = { DS_GLOW_WASH_A, DS_GLOW_WASH_SPREAD,
		0, DS_WASH_LIGHT_A, -1 },
};

/* A shadow is one texture per kind, made by ui_init and kept until ui_quit:
 * white, its alpha a rounded rect blurred, tinted **ink** and faded to the
 * kind's opacity as it is drawn. Nothing of a theme is in its pixels, so a
 * change of theme has nothing here to drop - Auto turning to Salt & Pepper
 * only stops it being asked for.
 *
 * MADE AT START, NOT ON FIRST USE. The panel's is three blur passes each way
 * over 218 texels square, and made on first use it was made in the frame
 * that first opened a menu on Beard, which is the frame a press is answered
 * in. A kind that cannot be made then - no memory, no texture - is left
 * without one, says so once on stderr and draws no shadow: it is not tried
 * again on every frame that asks for it, which is when the same failure
 * would recur.
 *
 * AT HALF SIZE, a texel to BLUR_UNIT units, and drawn back up by the
 * texture's linear filtering: start pays for every texel of the three, at
 * every boot and after every fallback game, and a soft blur has nothing a
 * texel of two units loses. The panel's is 218 texels square where it was
 * 442 at a texel a unit, and the three are 315 KB to upload where they were
 * 1.3 MB; on the host they are made in 1.0 ms where at full size they took
 * 4.0 ms, and the handheld's Cortex-A53 is several times slower than the
 * host. The filter is set on each texture rather than left to
 * SDL_HINT_RENDER_SCALE_QUALITY, which every device file sets today and
 * under any other would draw the shadow in blocks of two units.
 * tools/blur-check.c draws them back up as the GPU does and holds them to
 * the shadow made at full size.
 *
 * ONE TEXTURE FOR EVERY SIZE, drawn as nine pieces. A shadow stretched whole
 * over a bigger card would stretch its blur with it, and the focused card
 * changes size on every move; cut into corners that are drawn as they are,
 * edges stretched along their length and a middle stretched both ways, the
 * blur stays DS_SHADOW's width under a thumbnail and under a whole panel.
 * The texture is drawn so the cut is exact: blur_shadow's `k`, the corner
 * piece, reaches far enough in that the two texels between the corners only
 * ever saw a straight edge through the blur, so stretching them is what a
 * longer rect would have been. src/blur.c has the numbers and the cut, and
 * tools/blur-check.c holds them to it. */
static struct {
	SDL_Texture *tex;
	blur_shadow size;   /* the texture's padding, corner piece and side */
} shadow[UI_LIFT_KINDS];

/* The pressed glyphs' halos (below, with the glyphs), made and freed with
 * the shadows. */
static void halos_make(SDL_Renderer *r);
static void halos_free(void);

static void shadows_free(void)
{
	int i;

	halos_free();

	for (i = 0; i < UI_LIFT_KINDS; i++) {
		if (shadow[i].tex) SDL_DestroyTexture(shadow[i].tex);
		memset(&shadow[i], 0, sizeof shadow[i]);
	}
}

/* The shadow of `kind`: a rect with the kind's corner, blurred three times
 * each way by boxes that together spread it as far as three as wide as
 * DS_SHADOW's blur (blur_shadow_fill, at BLUR_UNIT). Three such boxes come
 * close to a Gaussian of half the blur, which is what CSS draws a box-shadow
 * of the same numbers with: the mock in docs/design/mock is CSS, and the
 * shelf should look like it. */
static bool shadow_make(SDL_Renderer *r, int kind)
{
	const blur_kind *bk = &BLUR_KIND[lift[kind].shadow];
	const blur_shadow size = blur_shadow_size(bk->blur, bk->radius, BLUR_UNIT);
	const int n = size.n;
	float *a = malloc((size_t)n * n * sizeof *a);
	float *line = malloc((size_t)n * sizeof *line);
	SDL_Surface *s = NULL;
	int x, y;

	if (!a || !line) goto out;
	blur_shadow_fill(a, &size, bk->radius, line);
	s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) goto out;
	for (y = 0; y < n; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

		for (x = 0; x < n; x++) {
			float v = a[(size_t)y * n + x];

			v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			row[x] = (Uint32)(v * 255.0f + 0.5f) << 24 | 0x00FFFFFFu;
		}
	}
	shadow[kind].tex = SDL_CreateTextureFromSurface(r, s);
	if (shadow[kind].tex) {
		SDL_SetTextureBlendMode(shadow[kind].tex, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(shadow[kind].tex, SDL_ScaleModeLinear);
		shadow[kind].size = size;
	}
out:
	if (s) SDL_FreeSurface(s);
	free(line);
	free(a);
	return shadow[kind].tex != NULL;
}

/* Every kind that has a shadow - all but the wash, which on Beard is the
 * system's color laid on and not a shadow at all - and the pressed glyphs'
 * halos, made with them. */
static void shadows_make(SDL_Renderer *r)
{
	int i;

	halos_make(r);

	for (i = 0; i < UI_LIFT_KINDS; i++)
		if (lift[i].shadow >= 0 && !shadow[i].tex && !shadow_make(r, i))
			fprintf(stderr, "ui: no shadow for lift %d; drawn without\n", i);
}

/* The shadow under `q`, dropped by the kind's y: nine pieces of its texture
 * (see `shadow`) over `q` grown by blur_pad, cut by blur_slice, which says
 * what a rect too small for two whole corners each way gets. */
static void shadow_draw(SDL_Renderer *r, const SDL_Rect *q, int kind,
                        const ds_theme *th, int alpha)
{
	const ds_rgb ink = th->ink;
	const blur_shadow *size = &shadow[kind].size;
	const int pad = blur_pad(size);
	blur_rect src[9], dst[9];
	SDL_Texture *t;
	int i;

	if (!shadow[kind].tex) return;     /* not made at start: see `shadow` */
	t = shadow[kind].tex;
	blur_slice(size, q->x - pad, q->y + lift[kind].y - pad,
	           q->w + 2 * pad, q->h + 2 * pad, src, dst);

	SDL_SetTextureColorMod(t, ink.r, ink.g, ink.b);
	SDL_SetTextureAlphaMod(t, (Uint8)alpha);
	for (i = 0; i < 9; i++)
		if (dst[i].w > 0 && dst[i].h > 0)
			SDL_RenderCopy(r, t,
			               &(SDL_Rect){ src[i].x, src[i].y, src[i].w, src[i].h },
			               &(SDL_Rect){ dst[i].x, dst[i].y, dst[i].w, dst[i].h });
}

/* Every lift, at `k` of its strength: the alpha of the glow, or the opacity
 * of the shadow or the wash, times `k`. A shadow is cast in `shape`'s shape
 * when there is one, and in the rect's when not. */
static void lift_draw(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb, int kind, int theme_ix, float k)
{
	const ds_theme *t = theme_at(theme_ix);

	if (kind < 0 || kind >= UI_LIFT_KINDS || q->w <= 0 || q->h <= 0) return;
	if (!(k > 0.0f)) return;
	if (k > 1.0f) k = 1.0f;
	if (theme_ix == DS_DARK)
		glow_draw(r, q, sys_rgb, (int)(lift[kind].glow_a * k + 0.5f),
		          lift[kind].spread, SDL_BLENDMODE_ADD, t);
	else if (kind == UI_LIFT_WASH)
		glow_draw(r, q, sys_rgb, (int)(lift[kind].a * k + 0.5f),
		          lift[kind].spread, SDL_BLENDMODE_BLEND, t);
	else
		shadow_draw(r, q, kind, t, (int)(lift[kind].a * k + 0.5f));
}

void ui_lift_in(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb,
                int kind, int theme_ix)
{
	lift_draw(r, q, sys_rgb, kind, theme_ix, 1.0f);
}

/* By the theme's index and not its table's address: theme() points into
 * src/theme.c's own copy, never at DS_THEME. */
void ui_lift(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb, int kind)
{
	lift_draw(r, q, sys_rgb, kind, theme_index(), 1.0f);
}

void ui_lift_k(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb, int kind, float k)
{
	lift_draw(r, q, sys_rgb, kind, theme_index(), k);
}

void ui_scrim_k(SDL_Renderer *r, int game, float k)
{
	const ds_theme *t = theme();
	int alpha = game ? t->scrim_game : t->scrim_menu;

	if (!(k > 0.0f)) return;
	if (k < 1.0f) alpha = (int)(alpha * k + 0.5f);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, t->ink.r, t->ink.g, t->ink.b, (Uint8)alpha);
	SDL_RenderFillRect(r, NULL);
}

void ui_scrim(SDL_Renderer *r, int game)
{
	ui_scrim_k(r, game, 1.0f);
}

/* See ui.h. The rule is src/sysmark.c's, in plain C, so that
 * tools/sysmark-check.c holds it without a window. */
unsigned ui_sys_mark_in(unsigned rgb, int theme_ix)
{
	return ui_rgb(sysmark(rgb, theme_ix));
}

unsigned ui_sys_mark(unsigned rgb)
{
	return ui_sys_mark_in(rgb, theme_index());
}

/* How much of a band the boundary between two colors eats at its widest, in
 * band widths, half of it on each side.
 *
 * WIDEST IS HALFWAY THROUGH A MOVE, AND ZERO AT REST - see rail_blend. At 0
 * throughout, the bands are hard and a crisp edge wipes across the marker as it
 * moves; at 1 the strip is one long gradient. 0.40 gives the moving edge about
 * 30px of a 76px marker, which reads as a blend rather than as a seam. */
#define RAIL_BLEND 0.40f

/* The blend width for a marker `index` items along, which is RAIL_BLEND only
 * in the middle of a move and nothing at all at rest.
 *
 * A MARKER AT REST IS EXACTLY ONE BAND WIDE, SO ITS EDGES SIT ON THE
 * BOUNDARIES. A blend straddling those tints a resting marker with the system
 * before it and the system after it - about a pixel of each on the device,
 * which Eric spotted on 2026-09-17, and no width setting can fix it because the
 * contamination is where the marker ENDS, not how wide the ramp is.
 *
 * Nothing needs softening at rest either: the softening exists for the edge
 * that sweeps across during a move, and at rest there is no edge, only one
 * band filling the window. So the width follows the distance between bands -
 * triangular, zero at both ends, so the ramp hardens back to nothing as the
 * move lands, exactly as it grew out of nothing when it left. */
static float rail_blend(float index)
{
	float f = index - floorf(index);

	return RAIL_BLEND * (1.0f - fabsf(2.0f * f - 1.0f));
}

static unsigned rgb_mix(unsigned a, unsigned b, float t)
{
	unsigned out = 0;
	int i;

	for (i = 16; i >= 0; i -= 8) {
		float x = (float)((a >> i) & 0xff);
		float y = (float)((b >> i) & 0xff);

		out |= (unsigned)(int)(x + (y - x) * t + 0.5f) << i;
	}
	return out;
}

/* The mark the strip carries `x` items along it: each item's color as a mark
 * (ui_sys_mark), blended where two bands meet.
 *
 * THE STRIP IS FIXED TO THE TRACK AND THE MARKER IS A WINDOW ONTO IT. Eric's
 * idea, 2026-09-17: a line made of every system's color in order, invisible
 * except where the marker is. So the color is a function of WHERE ON THE TRACK
 * a pixel is, never of where the marker is - which is what makes the wrap come
 * out right for free. Mid-wrap the marker's remains at the far end still sit
 * over the last system's band while its copy at the near end sits over the
 * first system's, so the two stubs are correctly different colors without
 * anything being told that a wrap is happening.
 *
 * Bands are one item wide and the marker is one band wide, so at rest it
 * covers exactly its own system's band, pure to both edges. A rail whose marker
 * is WIDER than a band, which is any long list, covers several: that is only
 * ever the games rail, where every item carries its system's color and the
 * whole strip is one color anyway.
 *
 * `blend` is a width, not a switch: at zero the bands meet at a hard edge and
 * neither division below is reached. */
static unsigned rail_hue_at(float x, int count, float blend,
                            ui_rail_hue hue, void *ctx)
{
	int i = (int)floorf(x);
	float f = x - (float)i;

	i = ((i % count) + count) % count;
	/* Half the blend sits above the boundary and half below it, so the mix is
	 * exactly even where two bands meet.
	 *
	 * The MARKS are blended, not the colors. Beard's quarter toward **text**
	 * is a straight line, so for any two systems' colors it makes no
	 * difference which is done first; for a system with no color it does,
	 * since CFG_NO_COLOR is not a color to blend - and for Muse's lime, whose
	 * mark is another color, blending the two marks is what shows the marks
	 * at rest on either side. */
	if (f < blend * 0.5f)
		return rgb_mix(ui_sys_mark(hue(ctx, i)),
		               ui_sys_mark(hue(ctx, (i - 1 + count) % count)),
		               0.5f - f / blend);
	if (f > 1.0f - blend * 0.5f)
		return rgb_mix(ui_sys_mark(hue(ctx, i)),
		               ui_sys_mark(hue(ctx, (i + 1) % count)),
		               0.5f - (1.0f - f) / blend);
	return ui_sys_mark(hue(ctx, i));
}

/* One marker, cut down to its track and colored by what it is over.
 *
 * Clipped rather than kept inside, because the rail describes a RING: the shelf
 * wraps, so the marker has to be able to leave by one end while the copy a lap
 * behind it arrives at the other. Both are handed over as whole rectangles and
 * the track decides how much of each is seen.
 *
 * Runs of one color are filled as one rectangle, so a rail nobody is moving -
 * and every rail whose items share a color, which is every games rail - still
 * costs the single fill it always did. */
static void rail_seg(SDL_Renderer *r, const SDL_Rect *track, SDL_Rect seg,
                     float step, float blend, int count,
                     unsigned flat, ui_rail_hue hue, void *ctx)
{
	SDL_Rect q;
	int len, k, run = 0;
	unsigned held = ui_sys_mark(flat);

	if (!SDL_IntersectRect(track, &seg, &q)) return;
	len = q.w;
	for (k = 0; k <= len; k++) {
		unsigned rgb = held;

		if (k < len && hue) {
			int t = q.x + k - track->x;

			/* THE PIXEL'S CENTER, NOT ITS NEAR EDGE, and the half is
			 * load-bearing rather than tidy. The marker's rectangle is
			 * rounded to whole pixels and the bands are not, so wherever
			 * round(i * step) lands BELOW i * step - four of the eleven
			 * systems, and the second position once Favorites makes it
			 * twelve - the marker's first pixel starts just inside the
			 * previous band and comes out that system's color. One pixel,
			 * spotted on the device 2026-09-17. A pixel covers [t, t+1),
			 * so asking what is under its middle answers for the pixel
			 * rather than for the boundary it happens to start on. */
			rgb = rail_hue_at(((float)t + 0.5f) / step, count, blend,
			                  hue, ctx);
		}
		if (k == 0) { held = rgb; continue; }
		if (k < len && rgb == held) continue;
		SDL_SetRenderDrawColor(r, (Uint8)(held >> 16), (Uint8)(held >> 8),
		                       (Uint8)held, 235);
		SDL_RenderFillRect(r, &(SDL_Rect){ q.x + run, q.y, k - run, q.h });
		run = k;
		held = rgb;
	}
}

/* The rail's line, in the theme's **track**. */
static void rail_track(SDL_Renderer *r, const SDL_Rect *track)
{
	const ds_rgb t = theme()->track;

	SDL_SetRenderDrawColor(r, t.r, t.g, t.b, 255);
	SDL_RenderFillRect(r, track);
}

/* How far along a track of `track_len` the marker sits, in pixels from the
 * track's start, with one lap of the ring returned in the same units.
 *
 * `index` IS CONTINUOUS AND COMES FROM cf.pos, not from a cursor. The cursor
 * changes the instant a button goes down and the cards take the whole of a
 * move to follow, so a rail drawn from it does not merely jump - it jumps
 * EARLY, arriving while the shelf is still setting off. The same fault the
 * moving cards, so the rail reads cf.pos too.
 *
 * At whole numbers this is the integer version's own answer to within the
 * rounding - the first item against the near end, the last flush against the
 * far one - so nothing moved at rest when it changed.
 *
 * Taken modulo the lap because `pos` runs PAST the ends and is put back by
 * step_anim only once the move lands: a wrap carries the marker off the end it
 * left by, and the caller's copy one lap behind brings it back on at the other.
 * A lap is count steps, not the track: the last item is flush with the end, so
 * there is one more step to travel before the first item comes round again. */
static float rail_at(float index, int count, int track_len, int seg_len,
                     float *lap)
{
	float step = (float)(track_len - seg_len) / (float)(count - 1);
	float p = step * (float)count;
	float o = fmodf(index * step, p);

	if (o < 0.0f) o += p;
	*lap = p;
	return o;
}

void ui_rail(SDL_Renderer *r, int screen_w, int screen_h, float index, int count,
             unsigned rgb, ui_rail_hue hue, void *ctx)
{
	/* Grown upward from where the old 3px bar's bottom edge sat, so matching
	 * the settings line's weight did not also move the rail. */
	int h = UI_BAR_H, y = screen_h - 23 - h;
	int track_x = 90, track_w = screen_w - track_x * 2;
	SDL_Rect track;
	int seg_w, off, lap;
	float o, fl, step, blend;

	if (count <= 1) return;
	seg_w = track_w / count;
	if (seg_w < 18) seg_w = 18;
	o = rail_at(index, count, track_w, seg_w, &fl);
	step = fl / (float)count;
	blend = rail_blend(index);
	/* Rounded to whole pixels HERE rather than compared as floats below: at
	 * rest on the last item the offset is the track's far end to within a
	 * rounding error either way, and a float comparison would draw the wrapped
	 * copy on one side of that error and not the other - a second marker
	 * appearing at the near end of a long list for no reason. */
	off = (int)(o + 0.5f);
	lap = (int)(fl + 0.5f);
	track = (SDL_Rect){ track_x, y, track_w, h };

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* **track**, the role for the unfilled part of a bar or a rail, and
	 * opaque: it was white at 16 over the ground, a tint that only ever
	 * worked on a dark one - on Beard it would add white to near-white and
	 * the rail would have no line left to run along. */
	rail_track(r, &track);
	rail_seg(r, &track, (SDL_Rect){ track_x + off, y, seg_w, h },
	         step, blend, count, rgb, hue, ctx);
	/* Only while the marker overhangs the far end, which is the only time the
	 * copy has anything to show. On a shelf whose segments tile the track -
	 * eleven systems, where a segment is one step wide - the two are exactly
	 * complementary and the marker crosses the seam without a break. On a long
	 * shelf a segment is many steps wide, so a stub shows at each end for the
	 * length of that one move; that seam is a genuine discontinuity in the
	 * list, and showing where it goes beats teleporting across it. */
	if (off > track_w - seg_w)
		rail_seg(r, &track, (SDL_Rect){ track_x + off - lap, y, seg_w, h },
		         step, blend, count, rgb, hue, ctx);
}

_Static_assert(sizeof(rr_rect) == sizeof(SDL_Rect) &&
               offsetof(rr_rect, x) == offsetof(SDL_Rect, x) &&
               offsetof(rr_rect, y) == offsetof(SDL_Rect, y) &&
               offsetof(rr_rect, w) == offsetof(SDL_Rect, w) &&
               offsetof(rr_rect, h) == offsetof(SDL_Rect, h), "rr_rect is SDL_Rect");

/* The shape as rects a corner's size, every pixel in one of them once:
 * roundrect.h, which says why. A radius past ROUND_MAX (none is drawn) goes
 * row by row. */
void ui_round_rect(SDL_Renderer *r, const SDL_Rect *q, int radius, SDL_Color col)
{
	rr_rect rects[ROUND_RECTS];
	int y, n;

	if (q->w <= 0 || q->h <= 0) return;
	radius = round_rect_radius(q->w, q->h, radius);

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
	if (radius > ROUND_MAX) {
		SDL_RenderFillRect(r, &(SDL_Rect){ q->x, q->y + radius, q->w, q->h - radius * 2 });
		for (y = 0; y < radius; y++) {
			int dy = radius - y;
			int dx = radius - (int)(sqrt((double)(radius * radius - dy * dy)) + 0.5);
			SDL_RenderFillRect(r, &(SDL_Rect){ q->x + dx, q->y + y, q->w - dx * 2, 1 });
			SDL_RenderFillRect(r, &(SDL_Rect){ q->x + dx, q->y + q->h - 1 - y,
			                                   q->w - dx * 2, 1 });
		}
		return;
	}
	n = round_rect_rects(q->x, q->y, q->w, q->h, radius, rects);
	/* rr_rect is SDL_Rect's layout: four ints */
	SDL_RenderFillRects(r, (const SDL_Rect *)rects, n);
}

/* The frame is DESIGN.md's: 3 units of **edge**, in both themes. It was the
 * system's color, twelve pixels of it, because a thinner line of a dark
 * accent on a near-black panel read as gray; with lime as the focus fill, a
 * colored frame around it put two accents on one panel, and a quiet one lets
 * the focused row be the brightest thing there. A system's menu says whose it
 * is on its heading rule.
 *
 * Drawn as the whole rect in **edge** and the **surface** inset by the frame
 * over it, rather than as a ring, so the corners of the two can never leave
 * a gap of whatever is behind. Opaque: a card title behind a panel ghosted
 * through the list at 95%. And **surface**, not the ground: a panel in the
 * ground's own color is a hole in the screen rather than a surface on it. */
void ui_panel(SDL_Renderer *r, const SDL_Rect *q, int radius)
{
	const ds_theme *t = theme();
	const int bw = UI_PANEL_BORDER;
	SDL_Rect in = { q->x + bw, q->y + bw, q->w - bw * 2, q->h - bw * 2 };

	ui_round_rect(r, q, radius, ui_col(t->edge, 255));
	ui_round_rect(r, &in, radius - bw, ui_col(t->surface, 255));
}

unsigned ui_mix(unsigned a, unsigned b, float t)
{
	int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
	int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
	int rr = (int)(ar + (br - ar) * t);
	int rg = (int)(ag + (bg - ag) * t);
	int rb = (int)(ab + (bb - ab) * t);
	return ((unsigned)rr << 16) | ((unsigned)rg << 8) | (unsigned)rb;
}

/* ---- button glyphs ------------------------------------------------------ */

/* The faces at the sizes the glyphs ask for: the letter scales with the
 * glyph, and no role's font is the size of a 40-unit button's letter. Its
 * weight is button_semibold's, the mock kit's: the title's file, SemiBold,
 * or menu.ttf's Regular. Opened the first time a size and weight are asked
 * for and kept until ui_quit, never closed sooner: the text cache keys on a
 * font's address, and a font closed with another opened in its place could
 * be handed the first one's textures. A screen asks for one or two sizes, so
 * the table does not fill; if it did, the nearest size open in the weight
 * stands in. A size that would not open is not retried every frame - its slot
 * says so with a NULL. */
#define BUTTON_FONTS 8
static struct { int pt; bool semibold; TTF_Font *f; } button_fonts[BUTTON_FONTS];

static TTF_Font *button_font(int pt, bool semibold)
{
	const char *path = semibold ? title_path_kept : font_path_kept;
	int i, best = -1;

	if (pt < 1) pt = 1;
	/* Lexend's title face whatever the language: a button's letter is a mark */
	if (semibold && pt == DS_TYPE_TITLE && fonts_latin[UI_F_TITLE]) return fonts_latin[UI_F_TITLE];
	for (i = 0; i < BUTTON_FONTS; i++) {
		if (button_fonts[i].pt == pt && button_fonts[i].semibold == semibold)
			return button_fonts[i].f;
		if (!button_fonts[i].pt) {
			button_fonts[i].pt = pt;
			button_fonts[i].semibold = semibold;
			button_fonts[i].f = path[0] ? TTF_OpenFont(path, pt) : NULL;
			if (!button_fonts[i].f)
				fprintf(stderr, "font %s @%d: %s\n", path, pt, TTF_GetError());
			return button_fonts[i].f;
		}
		if (button_fonts[i].f && button_fonts[i].semibold == semibold &&
		    (best < 0 || abs(button_fonts[i].pt - pt) < abs(button_fonts[best].pt - pt)))
			best = i;
	}
	return best < 0 ? NULL : button_fonts[best].f;
}

static void button_fonts_free(void)
{
	int i;

	memo_clear();
	for (i = 0; i < BUTTON_FONTS; i++) {
		if (button_fonts[i].f) TTF_CloseFont(button_fonts[i].f);
		button_fonts[i].pt = 0;
		button_fonts[i].semibold = false;
		button_fonts[i].f = NULL;
	}
}

/* DESIGN.md 11.9's two glyphs at full size: the face button 96 across, the
 * pill 64 high. A pill is drawn from the pill's numbers and a face from the
 * face's, scaled by the height asked for. The mock kit draws the 64 pill
 * (.g-pill) with 30 units at each end of its word; the keyboard's pills
 * (.kg, 40 and 44 high) with 0.35 of the height. */
#define BUTTON_FACE_H   96
#define BUTTON_PILL_H   64
#define BUTTON_PILL_PAD 30
#define BUTTON_KEY_H    44

/* The face buttons, which are circles. */
static bool button_menu(const char *label)
{
	return label && !strcmp(label, "MENU");
}

/* A face: A, B, X and Y, and MENU, which is round on the Pixel 2 and drawn as
 * its three dots rather than a word (menu_key_dots). */
static bool button_round(const char *label)
{
	return (label && label[0] && !label[1] && strchr("ABXY", label[0])) ||
	       button_menu(label);
}

static void menu_key_dots(SDL_Renderer *r, TTF_Font *f, float cx, float cy,
                          float k, SDL_Color col);

/* The letter's point size, rounded: a face's is the title's at 96 units, a
 * pill's half that at 64. See ui.h. */
static int button_pt(bool round, int size)
{
	return round ? (DS_TYPE_TITLE * size + BUTTON_FACE_H / 2) / BUTTON_FACE_H
	             : (DS_TYPE_TITLE * size + BUTTON_PILL_H) / (BUTTON_PILL_H * 2);
}

/* The letter's weight, as the mock kit draws it: a face (.g-face) SemiBold at
 * any size, 11.9's letter in **title**; the keyboard's glyphs (.kg, 40 and 44
 * high, a face or a pill) SemiBold; and 11.9's pill at its 64 (.g-pill, the
 * tutorial's MENU, SELECT and START) Regular, the screen's own weight, which
 * .g-pill does not change. A pill between the two takes the one it is
 * nearer, as button_pad's padding runs from .kg's to .g-pill's. */
static bool button_semibold(bool round, int size)
{
	return round || size * 2 <= BUTTON_KEY_H + BUTTON_PILL_H;
}

/* The letter's face: button_pt's size in button_semibold's weight. */
static TTF_Font *button_letter(bool round, int size)
{
	return button_font(button_pt(round, size), button_semibold(round, size));
}

/* The ring: 11.9's 4 at either glyph's full height, rounded for a face and
 * cut down for a pill, which keeps the keyboard's 40 and 44 at the 2 the
 * keyboard lab draws them with (a pill rounded would be 3 there). */
static int button_ring(bool round, int size)
{
	int ring = round ? (DS_RING * size + BUTTON_FACE_H / 2) / BUTTON_FACE_H
	                 : DS_RING * size / BUTTON_PILL_H;

	return ring < 1 ? 1 : ring;
}

/* A pill's room at each end of its label, from the kit's two pills: .kg's
 * 0.35 of the height up to its 44, which leaves START's pill and "done" room
 * in the keyboard's done key, the tightest of the action keys; .g-pill's 30
 * in 64 from 11.9's full height up; and a straight line between the two, so
 * no height jumps from one to the other. */
static int button_pad(int size)
{
	const int key_pad = (BUTTON_KEY_H * 35 + 50) / 100;

	if (size <= BUTTON_KEY_H) return (size * 35 + 50) / 100;
	if (size >= BUTTON_PILL_H)
		return (BUTTON_PILL_PAD * size + BUTTON_PILL_H / 2) / BUTTON_PILL_H;
	return key_pad + (BUTTON_PILL_PAD - key_pad) * (size - BUTTON_KEY_H)
	                 / (BUTTON_PILL_H - BUTTON_KEY_H);
}

int ui_button_width(const char *label, int size)
{
	bool round = button_round(label);

	if (round) return size;
	return ui_text_width(button_letter(round, size), label)
	       + button_pad(size) * 2;
}

/* On the accent fill, the glyph's own face is the fill with this much
 * **on-accent** laid over it: the keyboard lab's 12%, enough to say "a
 * button" inside the selected key without a second color. */
#define BUTTON_ON_ACCENT_TINT 0.12f

void ui_button(SDL_Renderer *r, const char *label, int cx, int cy, int size,
               int style)
{
	const ds_theme *t = theme();
	bool round = button_round(label);
	TTF_Font *f;
	int w, ring;
	SDL_Color edge, fill, ink;
	SDL_Rect q, in;

	if (!label || size <= 0) return;
	f = button_letter(round, size);
	w = round ? size : ui_text_width(f, label) + button_pad(size) * 2;
	ring = button_ring(round, size);

	if (style == UI_BUTTON_ON_ACCENT) {
		unsigned face = ui_mix(ui_rgb(t->accent), ui_rgb(t->on_accent),
		                       BUTTON_ON_ACCENT_TINT);

		edge = ink = ui_col(t->on_accent, 255);
		fill = (SDL_Color){ (Uint8)(face >> 16), (Uint8)(face >> 8),
		                    (Uint8)face, 255 };
	} else {
		edge = ui_col(t->edge, 255);
		fill = ui_col(t->raised, 255);
		ink = ui_col(t->text, 255);
	}

	/* The ring as the whole shape in its color and the face inset over it,
	 * as ui_panel draws a frame: both opaque, so no gap of what is behind can
	 * show between them. */
	q = (SDL_Rect){ cx - w / 2, cy - size / 2, w, size };
	in = (SDL_Rect){ q.x + ring, q.y + ring, q.w - ring * 2, q.h - ring * 2 };
	ui_round_rect(r, &q, size / 2, edge);
	ui_round_rect(r, &in, size / 2 - ring, fill);

	/* Centered on the capitals: every label is capitals and digits, and the
	 * em box would hang a descender's depth of nothing under them and sit
	 * them high in the glyph. */
	if (button_menu(label)) menu_key_dots(r, f, (float)cx, (float)cy, 1.0f, ink);
	else ui_text(r, f, label, cx, ui_cap_y(f, cy), 0, ink);
}

/* ---- the glyph pressed, and the glyphs that are no letter -------------------
 *
 * DESIGN.md 11.9, as the tutorial lab draws them: a pressed glyph is 92% of
 * its size, its ring gone from **edge** to **accent** and a soft **accent**
 * halo round it, by `press` of the way (0 up, 1 down), which the caller
 * eases in **press**. `scale` draws the whole glyph that many times its size
 * about its center, for a glyph that pulses: the tutorial's answer to a
 * press of the wrong button. The d-pad's arm and the rocker's half that is
 * pressed fill with **accent** and their arrow or label turns **on-accent**,
 * so which direction shows.
 *
 * Drawn in float, from a fan of triangles per shape, where ui_round_rect is
 * whole units: a glyph at 92% or 106% of 96 is between units, and rounded
 * every frame of a press it would wobble by one. */
#define BUTTON_PRESSED   0.92f
/* The halo: the lab's drop-shadow, 16 units of blur in **accent** at three
 * quarters, around the shape and not dropped. Three textures, one per corner
 * the glyphs have - a face's or POWER's circle, a 64 pill's ends, a d-pad
 * arm's 10 - each drawn in nine pieces as Beard's shadows are, made with
 * them at start, white, and tinted as they are drawn. */
#define BUTTON_HALO_BLUR 16
#define BUTTON_HALO_A    0.75f
enum { HALO_FACE, HALO_PILL, HALO_ARM, HALOS };
static const int halo_radius[HALOS] = { BUTTON_FACE_H / 2, BUTTON_PILL_H / 2, 10 };
static struct {
	SDL_Texture *tex;
	blur_shadow size;
} halo[HALOS];

/* The opacity the glyph being drawn is at, which every color below takes:
 * set by each of the public calls, for a glyph that fades in with the line
 * it belongs to. */
static float glyph_op = 1.0f;

static void halos_free(void)
{
	int i;

	for (i = 0; i < HALOS; i++) {
		if (halo[i].tex) SDL_DestroyTexture(halo[i].tex);
		memset(&halo[i], 0, sizeof halo[i]);
	}
}

static void halos_make(SDL_Renderer *r)
{
	int i, x, y;

	for (i = 0; i < HALOS; i++) {
		const blur_shadow size = blur_shadow_size(BUTTON_HALO_BLUR, halo_radius[i], BLUR_UNIT);
		const int n = size.n;
		float *a, *line;
		SDL_Surface *s = NULL;

		if (halo[i].tex) continue;
		a = malloc((size_t)n * n * sizeof *a);
		line = malloc((size_t)n * sizeof *line);
		if (a && line) {
			blur_shadow_fill(a, &size, halo_radius[i], line);
			s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ARGB8888);
		}
		if (s) {
			for (y = 0; y < n; y++) {
				Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

				for (x = 0; x < n; x++) {
					float v = a[(size_t)y * n + x];

					v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
					row[x] = (Uint32)(v * 255.0f + 0.5f) << 24 | 0x00FFFFFFu;
				}
			}
			halo[i].tex = SDL_CreateTextureFromSurface(r, s);
			SDL_FreeSurface(s);
		}
		if (halo[i].tex) {
			SDL_SetTextureBlendMode(halo[i].tex, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(halo[i].tex, SDL_ScaleModeLinear);
			halo[i].size = size;
		} else {
			fprintf(stderr, "ui: no halo %d for a pressed glyph; drawn without\n", i);
		}
		free(line);
		free(a);
	}
}

/* The halo round a shape at (x, y), w x h units, `press` of the way on. */
static void halo_draw(SDL_Renderer *r, int kind, float x, float y, float w, float h,
                      float press)
{
	const ds_rgb acc = theme()->accent;
	const blur_shadow *size = &halo[kind].size;
	const int pad = blur_pad(size);
	blur_rect src[9], dst[9];
	int i;

	if (!halo[kind].tex || press <= 0.01f) return;
	blur_slice(size, (int)lroundf(x) - pad, (int)lroundf(y) - pad,
	           (int)lroundf(w) + 2 * pad, (int)lroundf(h) + 2 * pad, src, dst);
	SDL_SetTextureColorMod(halo[kind].tex, acc.r, acc.g, acc.b);
	SDL_SetTextureAlphaMod(halo[kind].tex,
	                       (Uint8)(255.0f * BUTTON_HALO_A * glyph_op
	                               * (press > 1.0f ? 1.0f : press) + 0.5f));
	for (i = 0; i < 9; i++)
		if (dst[i].w > 0 && dst[i].h > 0)
			SDL_RenderCopy(r, halo[kind].tex,
			               &(SDL_Rect){ src[i].x, src[i].y, src[i].w, src[i].h },
			               &(SDL_Rect){ dst[i].x, dst[i].y, dst[i].w, dst[i].h });
}

/* A convex shape as a fan from its first point. */
#define GLYPH_PTS 72
static void fan(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c)
{
	SDL_Vertex v[GLYPH_PTS];
	int idx[(GLYPH_PTS - 2) * 3], i;

	if (n < 3 || n > GLYPH_PTS) return;
	for (i = 0; i < n; i++)
		v[i] = (SDL_Vertex){ p[i], c, { 0, 0 } };
	for (i = 0; i < n - 2; i++) {
		idx[i * 3] = 0;
		idx[i * 3 + 1] = i + 1;
		idx[i * 3 + 2] = i + 2;
	}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(r, NULL, v, n, idx, (n - 2) * 3);
}

/* A rounded rect in float: each corner a quarter of 16 steps. */
static void rrect_f(SDL_Renderer *r, float x, float y, float w, float h, float rad,
                    SDL_Color c)
{
	SDL_FPoint p[64];
	int q, i, n = 0;

	if (w <= 0.0f || h <= 0.0f) return;
	if (rad * 2.0f > w) rad = w / 2.0f;
	if (rad * 2.0f > h) rad = h / 2.0f;
	if (rad < 0.0f) rad = 0.0f;
	for (q = 0; q < 4; q++) {
		const float ccx = q == 0 || q == 3 ? x + w - rad : x + rad;
		const float ccy = q < 2 ? y + h - rad : y + rad;

		for (i = 0; i < 16; i++) {
			float t = (float)M_PI / 2.0f * ((float)q + (float)i / 15.0f);

			p[n++] = (SDL_FPoint){ ccx + cosf(t) * rad, ccy + sinf(t) * rad };
		}
	}
	fan(r, p, n, c);
}

static void disc_f(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c)
{
	SDL_FPoint p[48];
	int i;

	for (i = 0; i < 48; i++) {
		float t = 2.0f * (float)M_PI * (float)i / 48.0f;

		p[i] = (SDL_FPoint){ cx + cosf(t) * rad, cy + sinf(t) * rad };
	}
	fan(r, p, 48, c);
}

/* MENU's face: three dots in a line down it, as the key's dimples run down
 * the Pixel 2's height, sized from the letter a face would carry so MENU
 * sits among A, B, X and Y at their weight. */
static void menu_key_dots(SDL_Renderer *r, TTF_Font *f, float cx, float cy,
                          float k, SDL_Color col)
{
	float cap = (float)ui_cap_height(f) * k;
	float rad = fmaxf(1.75f, cap * 0.15f), gap = cap * 0.5f;

	for (int i = -1; i <= 1; i++) disc_f(r, cx, cy + (float)i * gap, rad, col);
}

static SDL_Color gcol(ds_rgb c)
{
	return ui_col(c, (Uint8)(255.0f * glyph_op + 0.5f));
}

static SDL_Color col_toward(ds_rgb a, ds_rgb b, float t)
{
	unsigned m = ui_mix(ui_rgb(a), ui_rgb(b), t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));

	return (SDL_Color){ (Uint8)(m >> 16), (Uint8)(m >> 8), (Uint8)m,
	                    (Uint8)(255.0f * glyph_op + 0.5f) };
}

static float op01(float op)
{
	return op < 0.0f ? 0.0f : (op > 1.0f ? 1.0f : op);
}

/* A label `k` times its size, its capitals' middle on `cy`, centered on cx,
 * at `a` of its opacity. */
static void label_k(SDL_Renderer *r, TTF_Font *f, const char *s, float cx, float cy,
                    float k, SDL_Color col, float a)
{
	struct text_entry *e;
	float top;

	if (a <= 0.004f) return;
	if (button_menu(s)) {
		col.a = (Uint8)(col.a * glyph_op * (a > 1.0f ? 1.0f : a) + 0.5f);
		menu_key_dots(r, f, cx, cy, k, col);
		return;
	}
	if (!(e = text_get(r, f, s, col))) return;
	top = cy - (float)(TTF_FontAscent(f) - ui_cap_height(f) / 2) * k;
	SDL_SetTextureAlphaMod(e->tex, (Uint8)(col.a * glyph_op * (a > 1.0f ? 1.0f : a) + 0.5f));
	SDL_RenderCopyF(r, e->tex, NULL,
	                &(SDL_FRect){ cx - e->w * k / 2.0f, top, e->w * k, e->h * k });
}

/* A label that turns from **text** to **on-accent** as its part is pressed:
 * the two drawn over each other, each at its share, so the text cache holds
 * two textures rather than one for every step of the turn. */
static void label_turn(SDL_Renderer *r, TTF_Font *f, const char *s, float cx, float cy,
                       float k, float q)
{
	const ds_theme *t = theme();

	label_k(r, f, s, cx, cy, k, ui_col(t->text, 255), 1.0f - q);
	label_k(r, f, s, cx, cy, k, ui_col(t->on_accent, 255), q);
}

static float glyph_k(float press, float scale)
{
	if (press < 0.0f) press = 0.0f;
	if (press > 1.0f) press = 1.0f;
	return (scale > 0.0f ? scale : 1.0f) * (1.0f - (1.0f - BUTTON_PRESSED) * press);
}

void ui_button_pressed(SDL_Renderer *r, const char *label, int cx, int cy, int size,
                       int style, float press, float scale, float op)
{
	const ds_theme *t = theme();
	bool round = button_round(label);
	TTF_Font *f;
	float k, w, h, ring;

	if (!label || size <= 0 || op <= 0.0f) return;
	if (press <= 0.0f && (scale == 1.0f || scale <= 0.0f) && op >= 1.0f) {
		ui_button(r, label, cx, cy, size, style);
		return;
	}
	glyph_op = op01(op);
	f = button_letter(round, size);
	k = glyph_k(style == UI_BUTTON_ON_ACCENT ? 0.0f : press, scale);
	w = (float)(round ? size : ui_text_width(f, label) + button_pad(size) * 2) * k;
	h = (float)size * k;
	ring = (float)button_ring(round, size) * k;

	if (style == UI_BUTTON_ON_ACCENT) {
		/* A selected key's glyph, as ui_button draws it: the key presses */
		unsigned face = ui_mix(ui_rgb(t->accent), ui_rgb(t->on_accent),
		                       BUTTON_ON_ACCENT_TINT);
		ds_rgb fill = { (Uint8)(face >> 16), (Uint8)(face >> 8), (Uint8)face };

		rrect_f(r, cx - w / 2.0f, cy - h / 2.0f, w, h, h / 2.0f, gcol(t->on_accent));
		rrect_f(r, cx - w / 2.0f + ring, cy - h / 2.0f + ring, w - ring * 2.0f,
		        h - ring * 2.0f, h / 2.0f - ring, gcol(fill));
		label_k(r, f, label, (float)cx, (float)cy, k, ui_col(t->on_accent, 255), 1.0f);
	} else {
		halo_draw(r, round ? HALO_FACE : HALO_PILL, cx - w / 2.0f, cy - h / 2.0f, w, h, press);
		rrect_f(r, cx - w / 2.0f, cy - h / 2.0f, w, h, h / 2.0f,
		        col_toward(t->edge, t->accent, press));
		rrect_f(r, cx - w / 2.0f + ring, cy - h / 2.0f + ring, w - ring * 2.0f,
		        h - ring * 2.0f, h / 2.0f - ring, gcol(t->raised));
		label_k(r, f, label, (float)cx, (float)cy, k, ui_col(t->text, 255), 1.0f);
	}
	glyph_op = 1.0f;
}

/* ---- the d-pad ---------------------------------------------------------------
 * The lab's cross, 120 units: two arms of the ring 40 wide with a corner of
 * 10, the face inset 4 with a corner of 7, an arm per direction over it, an
 * **edge** dot at the middle and an arrow on each arm. Drawn at size / 120. */
#define DPAD_U 120.0f

static void dpad_rect(SDL_Renderer *r, float cx, float cy, float k, float x, float y,
                      float w, float h, float rad, SDL_Color c)
{
	rrect_f(r, cx + (x - DPAD_U / 2) * k, cy + (y - DPAD_U / 2) * k, w * k, h * k,
	        rad * k, c);
}

void ui_dpad(SDL_Renderer *r, int cx, int cy, int size, const float arm[UI_DPAD_ARMS],
             float scale, float op)
{
	/* Each arm's rect and its arrow's three points, in the cross's 120. */
	static const float A[UI_DPAD_ARMS][4] = {
		{ 44, 4, 32, 36 }, { 44, 80, 32, 36 }, { 4, 44, 36, 32 }, { 80, 44, 36, 32 } };
	static const float P[UI_DPAD_ARMS][6] = {
		{ 60, 13, 50, 27, 70, 27 }, { 60, 107, 70, 93, 50, 93 },
		{ 13, 60, 27, 70, 27, 50 }, { 107, 60, 93, 50, 93, 70 } };
	const ds_theme *t = theme();
	float pr = 0.0f, k;
	SDL_Color ring;
	int i;

	if (size <= 0 || op <= 0.0f) return;
	glyph_op = op01(op);
	for (i = 0; i < UI_DPAD_ARMS; i++)
		if (arm && arm[i] > pr) pr = arm[i];
	k = glyph_k(pr, scale) * (float)size / DPAD_U;
	ring = col_toward(t->edge, t->accent, pr);

	halo_draw(r, HALO_ARM, cx - 20 * k, cy - 60 * k, 40 * k, 120 * k, pr);
	halo_draw(r, HALO_ARM, cx - 60 * k, cy - 20 * k, 120 * k, 40 * k, pr);
	dpad_rect(r, cx, cy, k, 40, 0, 40, 120, 10, ring);
	dpad_rect(r, cx, cy, k, 0, 40, 120, 40, 10, ring);
	dpad_rect(r, cx, cy, k, 44, 4, 32, 112, 7, gcol(t->raised));
	dpad_rect(r, cx, cy, k, 4, 44, 112, 32, 7, gcol(t->raised));
	for (i = 0; i < UI_DPAD_ARMS; i++) {
		float q = arm ? arm[i] : 0.0f;
		SDL_FPoint p[3];
		int j;

		if (q > 0.004f)
			dpad_rect(r, cx, cy, k, A[i][0], A[i][1], A[i][2], A[i][3], 7,
			          col_toward(t->raised, t->accent, q));
		for (j = 0; j < 3; j++)
			p[j] = (SDL_FPoint){ cx + (P[i][j * 2] - DPAD_U / 2) * k,
			                     cy + (P[i][j * 2 + 1] - DPAD_U / 2) * k };
		fan(r, p, 3, col_toward(t->text, t->on_accent, q));
	}
	disc_f(r, (float)cx, (float)cy, 7 * k, gcol(t->edge));
	glyph_op = 1.0f;
}

/* ---- the volume rocker -------------------------------------------------------
 * One pill, a half for each key with VOL- on the left, 24 units at each end
 * of each half's label at the 64 pill, and a 2-unit **edge** line between
 * the halves that stops 4 short of the ring at each end. */
static int rocker_pad(int size)
{
	return (24 * size + BUTTON_PILL_H / 2) / BUTTON_PILL_H;
}

int ui_rocker_width(int size)
{
	TTF_Font *f = button_letter(false, size);
	int a = ui_text_width(f, "VOL-"), b = ui_text_width(f, "VOL+");

	return 2 * ((a > b ? a : b) + 2 * rocker_pad(size));
}

/* Draw `what` clipped to the half of the screen left (or right) of x, inside
 * whatever clip the caller has set. */
static void clip_half(SDL_Renderer *r, float x, bool left, SDL_Rect *was, bool had)
{
	SDL_Rect half = left ? (SDL_Rect){ -0x4000, -0x4000, (int)lroundf(x) + 0x4000, 0x8000 }
	                     : (SDL_Rect){ (int)lroundf(x), -0x4000, 0x4000, 0x8000 };
	SDL_Rect in;

	if (had && SDL_IntersectRect(&half, was, &in)) half = in;
	else if (had) half = (SDL_Rect){ 0, 0, 0, 0 };
	SDL_RenderSetClipRect(r, &half);
}

void ui_rocker(SDL_Renderer *r, int cx, int cy, int size, float down, float up,
               float scale, float op)
{
	const ds_theme *t = theme();
	TTF_Font *f;
	float pr = down > up ? down : up, k = glyph_k(pr, scale);
	float w, h, ring, x, y;
	bool had = SDL_RenderIsClipEnabled(r);
	SDL_Rect was = { 0, 0, 0, 0 };
	int half;

	if (size <= 0 || op <= 0.0f) return;
	glyph_op = op01(op);
	f = button_letter(false, size);
	w = (float)ui_rocker_width(size) * k;
	h = (float)size * k;
	ring = (float)button_ring(false, size) * k;
	x = cx - w / 2.0f;
	y = cy - h / 2.0f;
	if (had) SDL_RenderGetClipRect(r, &was);
	halo_draw(r, HALO_PILL, x, y, w, h, pr);
	rrect_f(r, x, y, w, h, h / 2.0f, col_toward(t->edge, t->accent, pr));
	for (half = 0; half < 2; half++) {
		clip_half(r, (float)cx, half == 0, &was, had);
		rrect_f(r, x + ring, y + ring, w - ring * 2.0f, h - ring * 2.0f, h / 2.0f - ring,
		        col_toward(t->raised, t->accent, half == 0 ? down : up));
	}
	SDL_RenderSetClipRect(r, had ? &was : NULL);
	rrect_f(r, cx - DS_RULE * k / 2.0f, y + ring, DS_RULE * k, h - ring * 2.0f, 0.0f,
	        gcol(t->edge));
	label_turn(r, f, "VOL-", cx - w / 4.0f, (float)cy, k, down);
	label_turn(r, f, "VOL+", cx + w / 4.0f, (float)cy, k, up);
	glyph_op = 1.0f;
}

/* ---- POWER ---------------------------------------------------------------------
 * A face's circle with the power mark in **text**: a ring of 20 open at the
 * top, a stroke of 6 with round ends, and a stroke down into it from 23. */
void ui_power(SDL_Renderer *r, int cx, int cy, int size, float press, float scale,
              float op)
{
	const ds_theme *t = theme();
	SDL_Color ink;
	/* The open ring runs from 51.4 degrees right of the top, clockwise, to
	 * as far left of it: (59.47, 33.62) to (36.53, 33.62) about (48, 48). */
	const float a0 = atan2f(-14.38f, 11.47f), sweep = 2.0f * (float)M_PI - 2.0f * (a0 + (float)M_PI / 2.0f);
	float k = glyph_k(press, scale) * (float)size / BUTTON_FACE_H, ring;
	SDL_Vertex v[2 * 33];
	int idx[32 * 6], i;

	if (size <= 0 || op <= 0.0f) return;
	glyph_op = op01(op);
	ink = gcol(t->text);
	ring = (float)button_ring(true, size) * glyph_k(press, scale);
	halo_draw(r, HALO_FACE, cx - 48 * k, cy - 48 * k, 96 * k, 96 * k, press);
	disc_f(r, (float)cx, (float)cy, 48 * k, col_toward(t->edge, t->accent, press));
	disc_f(r, (float)cx, (float)cy, 48 * k - ring, gcol(t->raised));
	for (i = 0; i <= 32; i++) {
		float a = a0 + sweep * (float)i / 32.0f, c = cosf(a), s = sinf(a);

		v[i * 2]     = (SDL_Vertex){ { cx + c * 17 * k, cy + s * 17 * k }, ink, { 0, 0 } };
		v[i * 2 + 1] = (SDL_Vertex){ { cx + c * 23 * k, cy + s * 23 * k }, ink, { 0, 0 } };
		if (i < 32) {
			int *q = &idx[i * 6];

			q[0] = i * 2; q[1] = i * 2 + 1; q[2] = i * 2 + 2;
			q[3] = i * 2 + 1; q[4] = i * 2 + 3; q[5] = i * 2 + 2;
		}
	}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(r, NULL, v, 2 * 33, idx, 32 * 6);
	disc_f(r, cx + cosf(a0) * 20 * k, cy + sinf(a0) * 20 * k, 3 * k, ink);
	disc_f(r, cx + cosf(a0 + sweep) * 20 * k, cy + sinf(a0 + sweep) * 20 * k, 3 * k, ink);
	rrect_f(r, cx - 3 * k, cy - 25 * k, 6 * k, 25 * k, 0.0f, ink);
	disc_f(r, (float)cx, cy - 25 * k, 3 * k, ink);
	disc_f(r, (float)cx, (float)cy, 3 * k, ink);
	glyph_op = 1.0f;
}

/* ---- the generated card ------------------------------------------------- */

#define CARD_W 512
#define CARD_H 656
#define CARD_RADIUS 22
#define WATERMARK_PT 560   /* the title's first letter, enormous and faint */

static void round_corners(SDL_Surface *s, int radius)
{
	int x, y;
	for (y = 0; y < radius; y++) {
		Uint32 *top = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
		Uint32 *bot = (Uint32 *)((Uint8 *)s->pixels + (size_t)(s->h - 1 - y) * s->pitch);
		for (x = 0; x < radius; x++) {
			int dx = radius - x, dy = radius - y;
			if (dx * dx + dy * dy > radius * radius) {
				top[x] = 0; top[s->w - 1 - x] = 0;
				bot[x] = 0; bot[s->w - 1 - x] = 0;
			}
		}
	}
}

/* In the color the card's theme gives its title (card_surface), handed down
 * from there and never read from theme().
 *
 * A line of the card role is laid out as ui_line_y lays out a line on the
 * screen: `*y` is the top of its line, an em (UI_F_CARD's 71) tall, and its
 * capitals centered two fifths of the way down; the next line starts an em
 * and 6 lower. Not by the texture's height, which is the face's ascent and
 * descent: Lexend's is 89, and stacked by it four lines ran off an album's
 * square cover and the first sat 19 lower on the card than the Josefin Sans
 * before it, whose 71 to 73 and 4 of air the em and 6 keep. */
static void blit_line(SDL_Surface *dst, TTF_Font *f, const char *line, int *y,
                      int x, SDL_Color col)
{
	const int em = role_pt(UI_F_CARD);
	SDL_Surface *t = ui_text_surface(f, line, col);
	if (!t) return;
	SDL_BlitSurface(t, NULL, dst,
	                &(SDL_Rect){ x < 0 ? (dst->w - t->w) / 2 : x,
	                             ui_cap_y(f, *y + em * 2 / 5), 0, 0 });
	*y += em + 6;
	SDL_FreeSurface(t);
}

/* Break a title across lines that fit the card. `x` is where each line starts,
 * or -1 to center them. The line is built by appending in place and undoing the
 * append when it no longer fits, so there is no second buffer that could
 * truncate the first. Returns the y below the last line, which is where a rule
 * under the title goes. */
static int draw_wrapped(SDL_Surface *dst, TTF_Font *f, const char *title,
                        int box_w, int top_y, int x, SDL_Color col)
{
	char line[256];
	char word[96];
	const char *p = title;
	size_t len = 0;
	int y = top_y;
	int lines = 0;

	line[0] = '\0';
	while (*p && lines < 4) {
		size_t n = 0, keep;
		int w = 0;

		while (*p == ' ') p++;
		while (*p && *p != ' ' && n + 1 < sizeof word) word[n++] = *p++;
		while (*p && *p != ' ') p++;          /* drop the tail of a huge word */
		word[n] = '\0';
		if (!n) break;

		keep = len;
		if (len + (len ? 1 : 0) + n + 1 > sizeof line) {
			/* the line cannot hold another word at all */
			if (len) { blit_line(dst, f, line, &y, x, col); lines++; }
			snprintf(line, sizeof line, "%s", word);
			len = n;
			continue;
		}
		if (len) line[len++] = ' ';
		memcpy(line + len, word, n);
		len += n;
		line[len] = '\0';

		w = ui_text_width(f, line);
		if (w > box_w && keep) {
			line[keep] = '\0';                /* undo the append */
			blit_line(dst, f, line, &y, x, col);
			lines++;
			snprintf(line, sizeof line, "%s", word);
			len = n;
		}
	}
	if (line[0] && lines < 4) blit_line(dst, f, line, &y, x, col);
	return y;
}

/* The first letter of the title, enormous and barely there, running off the
 * bottom-right corner.
 *
 * It used to sit centered and upright at a third of the way down, which made it
 * the largest thing on the card - and on an alphabetized shelf it is the least
 * distinguishing: Castlevania, Contra and Crystalis sit next to each other and
 * were three identical Cs with the titles that tell them apart set small
 * underneath. Bled off the corner it is what it always was, a texture in the
 * system's color, and the title can have the space. */
static void draw_watermark(SDL_Surface *dst, const char *title, unsigned rgb)
{
	char ch[2] = { 0, 0 };
	const char *p = title;
	SDL_Surface *t;

	while (*p && (unsigned char)*p <= ' ') p++;
	if (!*p) return;
	ch[0] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
	if ((unsigned char)ch[0] > 127) return;   /* one glyph, and an ASCII one */

	if (!f_mark && font_path_kept[0])
		f_mark = TTF_OpenFont(font_path_kept, WATERMARK_PT);
	if (!f_mark) return;

	t = TTF_RenderUTF8_Blended(f_mark, ch, (SDL_Color){
		(Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 55 });
	if (!t) return;
	SDL_SetSurfaceBlendMode(t, SDL_BLENDMODE_BLEND);
	/* Deliberately past both edges: what is wanted is the shoulder of the
	 * letter, not the letter. Its baseline a twentieth of its size above the
	 * card's foot, by the face's ascent: by a fraction of the texture's
	 * height, as it was, the letter moved with the face's ascent, and with
	 * Lexend's whole em its top came 41 lower than with Josefin Sans's. */
	SDL_BlitSurface(t, NULL, dst, &(SDL_Rect){
		dst->w - (int)(t->w * 0.62f),
		dst->h - WATERMARK_PT / 20 - TTF_FontAscent(f_mark), 0, 0 });
	SDL_FreeSurface(t);
}

/* The card at any size: ui_make_card is a game's, ui_make_cover an album's.
 * One recipe, so an album with no art reads as the same kind of thing as a
 * game with none - except the corners, which an album's does not round: it
 * stands among square sleeves, where a game's stands among cards.
 *
 * For theme `theme_ix`, DS_LIGHT or DS_DARK, and only from it: DESIGN.md 11.1
 * draws a card for each theme, a light **surface** slab with a charcoal title
 * in Beard and a dark one with a light title in Salt & Pepper (src/cardgen.h
 * has both, and the check that the title reads on every row of either). The
 * caller names the theme. */
static SDL_Surface *card_surface(const char *title, unsigned rgb,
                                 int cw, int ch, bool round, int theme_ix)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, cw, ch, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	const ds_theme *t = theme_at(theme_ix);
	/* The rule under the title is a mark of the system's color on the slab,
	 * so it is drawn as every such mark is (ui_sys_mark_in): a quarter of the
	 * way to **text** on Beard's light slab, where a yellow like Game Boy
	 * Color's would not read, and lime as **accent-ink**. The band across the
	 * top stays the system's own color, as the mock kit's card has it. */
	unsigned rule = ui_sys_mark_in(rgb, theme_ix);
	int y;
	if (!s) return NULL;

	/* A system with no color has its band and letter in **text-dim**, as its
	 * marks are, in the card's theme. */
	if (rgb == CFG_NO_COLOR) rgb = ui_rgb(t->text_dim);

	/* A vertical gradient, darker at the foot, so a wall of generated cards
	 * still has some depth to it: the theme's surface, falling toward its
	 * ground (cardgen_slab). */
	for (y = 0; y < ch; y++) {
		ds_rgb c = cardgen_slab(theme_ix, (float)y / ch);
		SDL_FillRect(s, &(SDL_Rect){ 0, y, cw, 1 },
		             SDL_MapRGBA(s->format, c.r, c.g, c.b, 255));
	}

	draw_watermark(s, title, rgb);

	/* The system's color as a band rather than a wash: a generated card
	 * should read as "this system, no art" at a glance in the row. */
	SDL_FillRect(s, &(SDL_Rect){ 0, 0, cw, 6 },
	             SDL_MapRGBA(s->format, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8),
	                         (Uint8)rgb, 255));

	/* The title at the top and hard left, because that is the edge the eye
	 * runs down when the row is moving. A rule under it rather than across
	 * the card: it is the end of the name, not a divider between halves. */
	if (fonts_latin[UI_F_CARD]) {
		/* Not at the top edge. Started at 76 it left the bottom half of the
		 * card empty for the one-line titles that are most of a shelf, and the
		 * bleed does not fill it - the face darkens toward the foot and takes
		 * the letter's tail with it. Four lines still fit below this. */
		int y = draw_wrapped(s, fonts_latin[UI_F_CARD], title, cw - 96,
		                     (int)(ch * 0.38f), 48,
		                     ui_col(cardgen_title(theme_ix), 255));

		SDL_FillRect(s, &(SDL_Rect){ 48, y + 12, 90, 3 },
		             SDL_MapRGBA(s->format, (Uint8)(rule >> 16),
		                         (Uint8)(rule >> 8), (Uint8)rule, 220));
	}

	if (round) round_corners(s, CARD_RADIUS);
	return s;
}

/* Which theme a card's texture was made for, kept on the texture itself
 * (SDL's user data, SDL 2.0.18, which SDL_RenderGeometry already needs): a
 * pointer to one of these, by index, and any other pointer is not a card. On
 * the texture and not in a table beside it, because a card's texture goes
 * into slots that are allocated, reordered, moved and freed in a dozen places
 * in src/main.c, and a mark that lives with the texture cannot be left behind
 * by any of them. */
static char card_made_for[DS_THEMES];

/* A card's surface as its texture: blended for its corners, and marked with
 * the theme it was drawn for. */
static SDL_Texture *card_texture(SDL_Renderer *r, SDL_Surface *s, int theme_ix)
{
	SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);

	if (!t) return NULL;
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureUserData(t, &card_made_for[theme_clamp(theme_ix)]);
	return t;
}

/* On the main thread, so for the theme that is on. */
static SDL_Texture *make_card(SDL_Renderer *r, const char *title, unsigned rgb,
                              int cw, int ch, bool round, int *w, int *h)
{
	const int ix = theme_index();
	SDL_Surface *s;
	SDL_Texture *t;

	if (!(s = card_surface(title, rgb, cw, ch, round, ix)))
		return NULL;
	t = card_texture(r, s, ix);
	SDL_FreeSurface(s);
	if (t) { *w = cw; *h = ch; }
	return t;
}

int ui_card_theme(SDL_Texture *t)
{
	void *p = t ? SDL_GetTextureUserData(t) : NULL;
	int i;

	for (i = 0; i < DS_THEMES; i++)
		if (p == &card_made_for[i]) return i;
	return -1;
}

SDL_Texture *ui_make_card(SDL_Renderer *r, const char *title, unsigned rgb,
                          int *w, int *h)
{
	return make_card(r, title, rgb, CARD_W, CARD_H, true, w, h);
}

SDL_Texture *ui_make_cover(SDL_Renderer *r, const char *title, unsigned rgb,
                           int *w, int *h)
{
	return make_card(r, title, rgb, CARD_W, CARD_W, false, w, h);
}


/* ---- the play modes' marks ---------------------------------------------- */

/* Each mark is a few strokes and a few arrowheads in a unit square, y down,
 * rendered into a white texture with 4x4 supersampling - which is the whole of
 * the antialiasing - and tinted when drawn. Strokes are segments with round
 * ends; a curve is a few of them. */

typedef struct { float x0, y0, x1, y1; } g_seg;
typedef struct { float ax, ay, bx, by, cx, cy; } g_tri;

#define G_W   0.055f            /* half a stroke */
#define G_MAX 32

typedef struct {
	g_seg seg[G_MAX];
	g_tri tri[4];
	int   nseg, ntri;
	float w[G_MAX];              /* each stroke's half width */
} g_shape;

static void g_line(g_shape *s, float x0, float y0, float x1, float y1, float w)
{
	if (s->nseg == G_MAX) return;
	s->w[s->nseg] = w;
	s->seg[s->nseg++] = (g_seg){ x0, y0, x1, y1 };
}

/* A quarter of a circle as six strokes, from angle a0 to a1 (radians, y down). */
static void g_arc(g_shape *s, float cx, float cy, float r, float a0, float a1)
{
	int i;

	for (i = 0; i < 6; i++) {
		float t0 = a0 + (a1 - a0) * (float)i / 6.0f;
		float t1 = a0 + (a1 - a0) * (float)(i + 1) / 6.0f;

		g_line(s, cx + r * cosf(t0), cy + r * sinf(t0),
		       cx + r * cosf(t1), cy + r * sinf(t1), G_W);
	}
}

/* An arrowhead with its tip at (tx, ty), pointing along (dx, dy), a unit
 * vector. */
static void g_head(g_shape *s, float tx, float ty, float dx, float dy)
{
	float bx = tx - dx * 0.17f, by = ty - dy * 0.17f;
	float px = -dy * 0.13f, py = dx * 0.13f;

	if (s->ntri == 4) return;
	s->tri[s->ntri++] = (g_tri){ tx, ty, bx + px, by + py, bx - px, by - py };
}

static void g_build(ui_glyph g, g_shape *s)
{
	const float pi = 3.14159265f;

	memset(s, 0, sizeof *s);
	if (g == UI_GLYPH_CHARGE) {
		/* A bolt, outlined: down from the top right, a step across, and
		 * down again to the point, from the USB lab's mark. */
		static const float p[][2] = {
			{ 0.563f, 0.083f }, { 0.208f, 0.583f }, { 0.479f, 0.583f },
			{ 0.417f, 0.917f }, { 0.792f, 0.396f }, { 0.521f, 0.396f },
		};
		int i;

		for (i = 0; i < 6; i++)
			g_line(s, p[i][0], p[i][1], p[(i + 1) % 6][0], p[(i + 1) % 6][1], G_W);
		return;
	}
	if (g == UI_GLYPH_TRANSFER) {
		/* A folder, its tab at the top left, and an arrow up out of it. */
		g_line(s, 0.10f, 0.80f, 0.10f, 0.22f, G_W);
		g_line(s, 0.10f, 0.22f, 0.36f, 0.22f, G_W);
		g_line(s, 0.36f, 0.22f, 0.44f, 0.31f, G_W);
		g_line(s, 0.44f, 0.31f, 0.90f, 0.31f, G_W);
		g_line(s, 0.90f, 0.31f, 0.90f, 0.80f, G_W);
		g_line(s, 0.90f, 0.80f, 0.10f, 0.80f, G_W);
		g_line(s, 0.50f, 0.70f, 0.50f, 0.52f, G_W);
		g_head(s, 0.50f, 0.40f, 0.0f, -1.0f);
		return;
	}
	if (g == UI_GLYPH_TERMINAL) {
		/* A screen, a prompt's chevron and the cursor's line. */
		g_line(s, 0.10f, 0.17f, 0.90f, 0.17f, G_W);
		g_line(s, 0.90f, 0.17f, 0.90f, 0.83f, G_W);
		g_line(s, 0.90f, 0.83f, 0.10f, 0.83f, G_W);
		g_line(s, 0.10f, 0.83f, 0.10f, 0.17f, G_W);
		g_line(s, 0.27f, 0.37f, 0.42f, 0.50f, G_W);
		g_line(s, 0.42f, 0.50f, 0.27f, 0.63f, G_W);
		g_line(s, 0.50f, 0.63f, 0.72f, 0.63f, G_W);
		return;
	}
	if (g == UI_GLYPH_USB) {
		/* The USB trident (2026-11-06): an arrow up the stem, a
		 * disc at its foot, a branch bending out to a disc on the left and
		 * one to a square on the right. Discs are strokes of no length. */
		const float w = 0.045f;

		s->tri[s->ntri++] = (g_tri){ 0.50f, 0.03f, 0.39f, 0.23f, 0.61f, 0.23f };
		g_line(s, 0.50f, 0.20f, 0.50f, 0.85f, w);
		g_line(s, 0.50f, 0.86f, 0.50f, 0.86f, 0.09f);
		g_line(s, 0.27f, 0.41f, 0.27f, 0.41f, 0.085f);
		g_line(s, 0.27f, 0.41f, 0.27f, 0.55f, w);
		g_line(s, 0.27f, 0.55f, 0.30f, 0.61f, w);
		g_line(s, 0.30f, 0.61f, 0.38f, 0.655f, w);
		g_line(s, 0.38f, 0.655f, 0.50f, 0.70f, w);
		s->tri[s->ntri++] = (g_tri){ 0.63f, 0.27f, 0.79f, 0.27f, 0.79f, 0.43f };
		s->tri[s->ntri++] = (g_tri){ 0.63f, 0.27f, 0.79f, 0.43f, 0.63f, 0.43f };
		g_line(s, 0.71f, 0.43f, 0.71f, 0.50f, w);
		g_line(s, 0.71f, 0.50f, 0.68f, 0.55f, w);
		g_line(s, 0.68f, 0.55f, 0.60f, 0.585f, w);
		g_line(s, 0.60f, 0.585f, 0.50f, 0.62f, w);
		return;
	}
	if (g == UI_GLYPH_ARROWS) {
		/* Files both ways: an arrow up, and beside it one down */
		g_line(s, 0.34f, 0.86f, 0.34f, 0.30f, G_W);
		g_head(s, 0.34f, 0.13f, 0.0f, -1.0f);
		g_line(s, 0.66f, 0.14f, 0.66f, 0.70f, G_W);
		g_head(s, 0.66f, 0.87f, 0.0f, 1.0f);
		return;
	}
	if (g == UI_GLYPH_FINISH) {
		/* A page, its corner turned, and a tick on it */
		g_line(s, 0.20f, 0.08f, 0.58f, 0.08f, G_W);
		g_line(s, 0.58f, 0.08f, 0.80f, 0.30f, G_W);
		g_line(s, 0.80f, 0.30f, 0.80f, 0.92f, G_W);
		g_line(s, 0.80f, 0.92f, 0.20f, 0.92f, G_W);
		g_line(s, 0.20f, 0.92f, 0.20f, 0.08f, G_W);
		g_line(s, 0.33f, 0.60f, 0.45f, 0.72f, G_W);
		g_line(s, 0.45f, 0.72f, 0.68f, 0.46f, G_W);
		return;
	}
	if (g == UI_GLYPH_CHECK) {
		/* A tick, the finish's own, alone and a stroke heavier */
		g_line(s, 0.14f, 0.54f, 0.38f, 0.78f, G_W * 1.4f);
		g_line(s, 0.38f, 0.78f, 0.86f, 0.24f, G_W * 1.4f);
		return;
	}
	if (g == UI_GLYPH_CANCEL) {
		/* A ring, crossed */
		g_arc(s, 0.50f, 0.50f, 0.40f, 0.0f, 0.5f * pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, 0.5f * pi, pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, pi, 1.5f * pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, 1.5f * pi, 2.0f * pi);
		g_line(s, 0.36f, 0.36f, 0.64f, 0.64f, G_W);
		g_line(s, 0.64f, 0.36f, 0.36f, 0.64f, G_W);
		return;
	}
	if (g == UI_GLYPH_PLAY) {
		/* A play triangle, outlined, its point to the right */
		g_line(s, 0.27f, 0.14f, 0.27f, 0.86f, G_W);
		g_line(s, 0.27f, 0.86f, 0.84f, 0.50f, G_W);
		g_line(s, 0.84f, 0.50f, 0.27f, 0.14f, G_W);
		return;
	}
	if (g == UI_GLYPH_WIFI) {
		/* Three arcs over a dot, the widest a quarter turn across */
		g_arc(s, 0.50f, 0.80f, 0.56f, 1.25f * pi, 1.75f * pi);
		g_arc(s, 0.50f, 0.80f, 0.37f, 1.25f * pi, 1.75f * pi);
		g_arc(s, 0.50f, 0.80f, 0.18f, 1.25f * pi, 1.75f * pi);
		g_line(s, 0.50f, 0.80f, 0.50f, 0.80f, 0.075f);
		return;
	}
	if (g == UI_GLYPH_SHUFFLE) {
		/* Two paths that cross, both arriving on the right. */
		g_line(s, 0.10f, 0.32f, 0.30f, 0.32f, G_W);
		g_line(s, 0.30f, 0.32f, 0.60f, 0.68f, G_W);
		g_line(s, 0.60f, 0.68f, 0.72f, 0.68f, G_W);
		g_head(s, 0.89f, 0.68f, 1.0f, 0.0f);
		g_line(s, 0.10f, 0.68f, 0.30f, 0.68f, G_W);
		g_line(s, 0.30f, 0.68f, 0.60f, 0.32f, G_W);
		g_line(s, 0.60f, 0.32f, 0.72f, 0.32f, G_W);
		g_head(s, 0.89f, 0.32f, 1.0f, 0.0f);
		return;
	}
	/* The loop: two arrows chasing each other round a rounded rectangle, one
	 * up the left and along the top, one down the right and along the
	 * bottom. */
	g_line(s, 0.12f, 0.60f, 0.12f, 0.42f, G_W);
	g_arc(s, 0.30f, 0.42f, 0.18f, pi, 1.5f * pi);
	g_line(s, 0.30f, 0.24f, 0.68f, 0.24f, G_W);
	g_head(s, 0.86f, 0.24f, 1.0f, 0.0f);
	g_line(s, 0.88f, 0.40f, 0.88f, 0.58f, G_W);
	g_arc(s, 0.70f, 0.58f, 0.18f, 0.0f, 0.5f * pi);
	g_line(s, 0.70f, 0.76f, 0.32f, 0.76f, G_W);
	g_head(s, 0.14f, 0.76f, -1.0f, 0.0f);
	if (g == UI_GLYPH_REPEAT_ONE) {
		/* A 1 in the middle, lighter than the loop and clear of it. */
		g_line(s, 0.50f, 0.38f, 0.50f, 0.62f, 0.04f);
		g_line(s, 0.50f, 0.38f, 0.445f, 0.43f, 0.04f);
	}
}

static int g_inside(const g_shape *s, float x, float y)
{
	int i;

	for (i = 0; i < s->nseg; i++) {
		const g_seg *g = &s->seg[i];
		float vx = g->x1 - g->x0, vy = g->y1 - g->y0;
		float len2 = vx * vx + vy * vy;
		float t = len2 > 0 ? ((x - g->x0) * vx + (y - g->y0) * vy) / len2 : 0;
		float dx, dy;

		if (t < 0) t = 0;
		if (t > 1) t = 1;
		dx = x - (g->x0 + t * vx);
		dy = y - (g->y0 + t * vy);
		if (dx * dx + dy * dy <= s->w[i] * s->w[i]) return 1;
	}
	for (i = 0; i < s->ntri; i++) {
		const g_tri *t = &s->tri[i];
		float d1 = (x - t->bx) * (t->ay - t->by) - (t->ax - t->bx) * (y - t->by);
		float d2 = (x - t->cx) * (t->by - t->cy) - (t->bx - t->cx) * (y - t->cy);
		float d3 = (x - t->ax) * (t->cy - t->ay) - (t->cx - t->ax) * (y - t->ay);
		int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;

		if (!(neg && pos)) return 1;
	}
	return 0;
}

static SDL_Texture *g_render(SDL_Renderer *r, ui_glyph g, int size)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	g_shape shape;
	int x, y, i, j;

	if (!s) return NULL;
	g_build(g, &shape);
	for (y = 0; y < size; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

		for (x = 0; x < size; x++) {
			int hit = 0;

			for (j = 0; j < 4; j++)
				for (i = 0; i < 4; i++)
					hit += g_inside(&shape, ((float)x + ((float)i + 0.5f) / 4.0f) / (float)size,
					                ((float)y + ((float)j + 0.5f) / 4.0f) / (float)size);
			row[x] = (Uint32)(hit * 255 / 16) << 24 | 0x00FFFFFFu;
		}
	}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

/* A handful of (mark, size) pairs is all a screen asks for. */
static struct { ui_glyph g; int size; SDL_Texture *tex; } g_cache[8];

static void glyphs_free(void)
{
	int i;

	for (i = 0; i < 8; i++)
		if (g_cache[i].tex) { SDL_DestroyTexture(g_cache[i].tex); g_cache[i].tex = NULL; }
}

void ui_glyph_draw(SDL_Renderer *r, ui_glyph g, int cx, int cy, int size,
                   SDL_Color col)
{
	SDL_Texture *t = NULL;
	int i, free_slot = -1;

	if (g < 0 || g >= UI_GLYPH_COUNT || size <= 0) return;
	for (i = 0; i < 8; i++) {
		if (g_cache[i].tex && g_cache[i].g == g && g_cache[i].size == size) {
			t = g_cache[i].tex;
			break;
		}
		if (!g_cache[i].tex && free_slot < 0) free_slot = i;
	}
	if (!t) {
		if (free_slot < 0) { glyphs_free(); free_slot = 0; }
		t = g_render(r, g, size);
		if (!t) return;
		g_cache[free_slot].g = g;
		g_cache[free_slot].size = size;
		g_cache[free_slot].tex = t;
	}
	SDL_SetTextureColorMod(t, col.r, col.g, col.b);
	SDL_SetTextureAlphaMod(t, col.a);
	SDL_RenderCopy(r, t, NULL, &(SDL_Rect){ cx - size / 2, cy - size / 2, size, size });
}
