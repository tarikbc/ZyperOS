/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "roundrect.h"
#include <stddef.h>

#include "blur.h"     /* a shadow's blur and its nine pieces */
#include "cardgen.h"  /* the generated card's colors in each theme */
#include "cardart.h"  /* a game's card: its ground and where its name breaks */
#include "cardart_draw.h"
#include "sysmark.h"  /* a system's color as a mark */
#include "config.h"   /* CFG_NO_COLOR: a system with no color of its own */
#include "linebreak.h" /* where a name too wide for its box breaks or is cut */
#include "look.h"      /* Corners: Square */
#include "padglyph.h"
#include "platform.h"  /* PLAT_TARGET_FORMAT, a render target's format */
#include "ds/button.h"  /* which glyph a button name draws on this handheld */
#include "folderart.h" /* a folder's card and row, in pixel art */  /* the PlayStation's shapes, drawn in running text */

#include <SDL_image.h>
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
/* See ui_face_open. Made by the first ui_init and kept for the life of the
 * process, never destroyed by ui_quit: a worker can be waiting on one then. */
static SDL_mutex *face_lock;
/* Every face ui_face_open opened and ui_face_close has not closed, with its
 * size and whether it is a title cut: so face_for finds Chinese and Japanese
 * faces of the same size and weight for a face of no role (home.c's page
 * faces, ui_face_at's), as it does for the role faces. Under face_lock, as
 * the opening and closing are. */
typedef struct { TTF_Font *f; int pt; bool title; } face_rec;
static face_rec *face_recs;
static int face_nrecs, face_caprecs;
/* The generated card's faces on the art workers (ui_card_paint's `away`):
 * sets of their own, so a face of the frame thread's is only ever the frame
 * thread's. One set for each of texload's two workers, so they draw two
 * cards at once as they decode two covers at once: a game's card (src/
 * cardart.h) is 40-50 ms on the Pixel 2, as much as a decode, and with one
 * set for both, a Grid of games with no art filled at half the rate of one
 * with art, and the rows a press brought into sight slid in empty. A set is
 * opened the first time a card is drawn in it and closed by ui_quit, both
 * under its own away_lock, which is held for the whole of a card.
 * `away_up` is from ui_init to ui_quit, the stretch the font library is up
 * in: outside it nothing opens them and nothing draws. */
#define AWAY_SETS 2                 /* src/texload.c's NWORKERS */
#define NAME_FACES (CA_SIZES * 2)   /* a game card's name: its sizes, then its rest's */
typedef struct {
	TTF_Font *title[SETS];      /* UI_F_CARD's face in each set */
	bool      title_tried[SETS];
	TTF_Font *mark;             /* the watermark's letter */
	bool      mark_tried;
	/* A game's card (src/cardart.h): the title face at the name's sizes in
	 * each set, and the last system art it read, kept for the next card */
	TTF_Font *name[SETS][NAME_FACES];
	bool      name_tried[SETS][NAME_FACES];
	char      art_path[UI_CARD_PATH];
	SDL_Surface *art;
} card_faces;
static SDL_mutex *away_lock[AWAY_SETS];
static card_faces away_faces[AWAY_SETS];
/* The frame thread's own copies of a game card's name faces and art (the
 * rest of a frame card is font_sets' and f_mark, as it always was) */
static card_faces frame_faces;
/* Every away_lock, in order, or let go of: ui_init and ui_quit change what
 * all the sets stand on, and wait out a card being drawn in any of them */
static void away_hold(bool hold)
{
	for (int i = 0; i < AWAY_SETS; i++) {
		if (!away_lock[i]) continue;
		if (hold) SDL_LockMutex(away_lock[i]);
		else SDL_UnlockMutex(away_lock[i]);
	}
}
static bool away_up;
static void card_faces_close(card_faces *k);
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

enum { MEMO_FREE, MEMO_WIDTH, MEMO_FIT, MEMO_BALANCE, MEMO_FILL };

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
	if (!face_lock) face_lock = SDL_CreateMutex();
	for (i = 0; i < AWAY_SETS; i++)
		if (!away_lock[i]) away_lock[i] = SDL_CreateMutex();

	/* No worker is in the library here: away_up is down until the end */
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

		fonts[i] = ui_face_open(path, pt);
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
			fonts[i] = ui_face_open(font_path, pt);
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
	/* Up for the workers' card faces too, which open from font_path_kept,
	 * set above: the lock is what makes it theirs to read */
	away_hold(true);
	away_up = true;
	away_hold(false);
	/* The title is a file of its own, so it opening says nothing of the
	 * others: menu.ttf missing, with title.ttf there, is a launcher with no
	 * menus, labels or counts. */
	return fonts[UI_F_TITLE] && fonts[UI_F_MENU];
}

/* Whether the file at path is one of the title cuts (DS_FONT_SEMIBOLD and
 * its Chinese and Japanese cuts) */
static bool title_cut(const char *path)
{
	const char *slash = strrchr(path, '/'), *name = slash ? slash + 1 : path;

	return !strcmp(name, DS_FONT_SEMIBOLD) || !strcmp(name, DS_FONT_CJK_TITLE) ||
	       !strcmp(name, DS_FONT_JA_TITLE);
}

TTF_Font *ui_face_open(const char *path, int pt)
{
	TTF_Font *f;

	if (face_lock) SDL_LockMutex(face_lock);
	f = TTF_OpenFont(path, pt);
	if (f) {
		/* A face with no record only goes without the Chinese and
		 * Japanese faces of its size */
		if (face_nrecs == face_caprecs) {
			int cap = face_caprecs ? face_caprecs * 2 : 32;
			face_rec *more = realloc(face_recs, (size_t)cap * sizeof *more);

			if (more) {
				face_recs = more;
				face_caprecs = cap;
			}
		}
		if (face_nrecs < face_caprecs)
			face_recs[face_nrecs++] = (face_rec){ f, pt, title_cut(path) };
	}
	if (face_lock) SDL_UnlockMutex(face_lock);
	return f;
}

void ui_face_close(TTF_Font *f)
{
	if (!f) return;
	if (face_lock) SDL_LockMutex(face_lock);
	for (int i = 0; i < face_nrecs; i++)
		if (face_recs[i].f == f) {
			face_recs[i] = face_recs[--face_nrecs];
			break;
		}
	TTF_CloseFont(f);
	if (face_lock) SDL_UnlockMutex(face_lock);
}

/* f's size and cut, as it was opened; false for a face ui_face_open did not
 * open */
static bool face_rec_of(TTF_Font *f, int *pt, bool *title)
{
	bool found = false;

	if (face_lock) SDL_LockMutex(face_lock);
	for (int i = 0; i < face_nrecs && !found; i++)
		if (face_recs[i].f == f) {
			*pt = face_recs[i].pt;
			*title = face_recs[i].title;
			found = true;
		}
	if (face_lock) SDL_UnlockMutex(face_lock);
	return found;
}

static void digits_free(void);
static void kin_free(void);
static void button_fonts_free(void);
static void bakes_free(void);

void ui_quit(void)
{
	/* The workers' card faces first, and the library marked down for them,
	 * before any of it goes: a worker drawing a card holds away_lock until
	 * the card is done, so this waits out that card, and the next one finds
	 * away_up false and leaves the card to the frame. Nothing draws text
	 * from here to the next ui_init, and nothing is left holding a face. */
	away_hold(true);
	away_up = false;
	for (int i = 0; i < AWAY_SETS; i++) card_faces_close(&away_faces[i]);
	away_hold(false);
	card_faces_close(&frame_faces);
	memo_clear();
	ds_glyphs_free();
	digits_free();
	bakes_free();
	shadows_free();
	for (int i = 0; i < TEXT_CACHE; i++)
		if (cache[i].tex) { SDL_DestroyTexture(cache[i].tex); cache[i].tex = NULL; }
	if (glow_tex) { SDL_DestroyTexture(glow_tex); glow_tex = NULL; }
	button_fonts_free();
	for (int k = 0; k < SETS; k++) {
		for (int i = 0; i < UI_F_COUNT; i++)
			if (font_sets[k][i]) { ui_face_close(font_sets[k][i]); font_sets[k][i] = NULL; }
		set_tried[k] = false;
	}
	fonts = font_sets[SET_LATIN];
	if (f_mark) { ui_face_close(f_mark); f_mark = NULL; }
	kin_free();
	if (face_lock) SDL_LockMutex(face_lock);
	TTF_Quit();
	if (face_lock) SDL_UnlockMutex(face_lock);
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
		font_sets[set][i] = ui_face_open(i == UI_F_TITLE ? title : menu, role_pt(i));
		if (!font_sets[set][i]) font_sets[set][i] = ui_face_open(menu, role_pt(i));
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

const char *ui_regular_font_path(char *out, size_t n)
{
	if (set_on == SET_LATIN || !font_path_kept[0]) snprintf(out, n, "%s", font_path_kept);
	else set_path(set_on, false, out, n);
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

/* ---- a marked key in running text ------------------------------------------
 *
 * The Pixel 2's MENU is a round graphite key with three dimples in a line
 * down the device's height (tools/build-pixel2-model.py: radius 2.65 mm, the
 * dimples 0.3 mm across and 1.05 mm apart). 2026-11-05: wherever the
 * system says MENU it shows that key, not the word. So the word MENU, alone
 * or before +, >, a space or a stop, is drawn as a ring in the text's own
 * color around three dots, centered on the capitals ("Show Tutorial is in
 * MENU.", "MENU+VOL", "MENU > Controls"); "Menu" and "menus" stay words.
 * The same for any key whose cap carries a mark and no word (src/ds/button.h):
 * the Brick's SELECT, a square, and START, a play triangle, which TrimUI's
 * manual calls SELECT and START, so "Press SELECT" there shows the key the
 * player sees (1.0.6). A key printed as its word (the Pixel 2's SELECT, the
 * Brick's MENU) stays a word. ui_text_width measures it the same way, so
 * fitting, breaking and centering all see the glyph. A key on its own is
 * ui_button's round face. */
static bool word_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '_';
}

/* The next whole word MENU, SELECT or START at or after p (s is the string's
 * start) whose key carries a mark on this handheld, or NULL: its bytes into
 * *len and its mark into *mark */
static const char *key_word(const char *s, const char *p, int *len, ds_key_mark *mark)
{
	static const char *const NAME[3] = { "MENU", "SELECT", "START" };
	const char *best = NULL;

	for (int i = 0; i < 3; i++) {
		const ds_key_mark m = ds_key_of(NAME[i]).mark;
		const size_t n = strlen(NAME[i]);
		const char *q = p;

		if (m == DS_KEY_MARK_NONE) continue;
		for (; (q = strstr(q, NAME[i])) && (!best || q < best); q += n)
			if ((q == s || !word_char(q[-1])) && !word_char(q[n])) {
				best = q;
				*len = (int)n;
				*mark = m;
				break;
			}
	}
	return best;
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

/* ---- the PlayStation's shapes in running text -----------------------------
 *
 * The cross, circle, triangle and square, where a string names them
 * (padglyph.h, 2026-11-07): drawn here as the shapes the pad has, in the
 * text's own color, a capital's height and a stroke as heavy as the face's,
 * so they read beside the letters in either theme. Made once per string like
 * the rest of it (text_get's cache), by 4 x 4 samples a pixel, so a shape
 * costs nothing a frame and stays crisp at the Pixel 2's size. Measured the
 * same way by width_in, so fitting and centering see them. */
typedef struct {
	float d, w, cy;    /* the shape's box, its stroke, the middle in the line */
	int side, adv;     /* room either side; the whole width */
} shape_mark;

static shape_mark shape_mark_of(TTF_Font *f)
{
	float cap = (float)ui_cap_height(f);
	shape_mark m;

	m.d = roundf(cap * 1.06f);
	m.w = fmaxf(1.5f, cap * 0.135f);
	m.side = (int)lroundf(cap * 0.10f);
	m.adv = (int)m.d + 2 * m.side;
	m.cy = (float)TTF_FontAscent(f) - cap / 2.0f;
	return m;
}

/* How far (px, py) is from the segment a-b */
static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
	float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
	float t = l2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / l2 : 0;

	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return hypotf(px - (ax + t * vx), py - (ay + t * vy));
}

/* Whether (x, y), from the shape's middle, is ink, for shape k (padglyph.h's
 * order: cross, circle, triangle, square) in a box d across, stroke w */
static bool shape_in(int k, float x, float y, float d, float w)
{
	const float hw = w / 2.0f;

	switch (k) {
	case 0: {   /* the cross: two strokes corner to corner */
		const float a = 0.36f * d;

		return seg_dist(x, y, -a, -a, a, a) <= hw || seg_dist(x, y, a, -a, -a, a) <= hw;
	}
	case 1:     /* the circle: a ring, the box across */
		return fabsf(hypotf(x, y) - (d / 2.0f - hw)) <= hw;
	case 2: {   /* the triangle: its point up, a little wider than the box
		     * and nearly as tall, so it looks the size of the circle beside
		     * it; equilateral, it looked a size smaller */
		const float top = -0.50f * d + hw, base = 0.40f * d - hw, half = 0.53f * d - hw;

		return seg_dist(x, y, 0, top, -half, base) <= hw ||
		       seg_dist(x, y, -half, base, half, base) <= hw ||
		       seg_dist(x, y, half, base, 0, top) <= hw;
	}
	default: {  /* the square: a little inside the box, as squares look larger */
		const float h = 0.40f * d;

		return fabsf(x) <= h + hw && fabsf(y) <= h + hw &&
		       !(fabsf(x) < h - hw && fabsf(y) < h - hw);
	}
	}
}

static void shape_put(SDL_Surface *dst, int x, shape_mark m, int k, SDL_Color col)
{
	const float cx = (float)x + (float)m.side + m.d / 2.0f, cy = m.cy;
	int y0 = (int)floorf(cy - m.d / 2.0f - 1.0f), y1 = (int)ceilf(cy + m.d / 2.0f + 1.0f);

	if (y0 < 0) y0 = 0;
	if (y1 > dst->h) y1 = dst->h;
	for (int py = y0; py < y1; py++) {
		Uint32 *row = (Uint32 *)((Uint8 *)dst->pixels + (size_t)py * dst->pitch);

		for (int px = x; px < x + m.adv && px < dst->w; px++) {
			int hit = 0;

			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
					hit += shape_in(k, (float)px + ((float)sx + 0.5f) / 4.0f - cx,
					                (float)py + ((float)sy + 0.5f) / 4.0f - cy, m.d, m.w);
			if (hit) row[px] = SDL_MapRGBA(dst->format, col.r, col.g, col.b,
			                                (Uint8)((hit * 255 + 8) / 16));
		}
	}
}

/* The next mark at or after p (s the string's start): the word MENU or a
 * shape. *len is its bytes, *kind -1 for MENU or the shape's number. NULL
 * when there is none. */
static const char *mark_next(const char *s, const char *p, int *len, int *kind)
{
	int klen = 0;
	ds_key_mark km = DS_KEY_MARK_NONE;
	const char *m = key_word(s, p, &klen, &km), *g = strchr(p, '\xEE');

	while (g && pad_glyph_at(g) < 0) g = strchr(g + 1, '\xEE');
	if (g && (!m || g < m)) {
		*len = 3;
		*kind = pad_glyph_at(g);
		return g;
	}
	/* A key: -1 MENU's dots, -2 a square, -3 a play triangle */
	*len = klen;
	*kind = km == DS_KEY_MARK_SQUARE ? -2 : km == DS_KEY_MARK_PLAY ? -3 : -1;
	return m;
}

static bool has_marks(const char *s)
{
	int len, kind;

	return mark_next(s, s, &len, &kind) != NULL;
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
		/* A shape (padglyph.h) is drawn, and no face is asked for it */
		if (c >= 0x2E80 && !(c >= 0xE000 && c <= 0xF8FF) && !TTF_GlyphIsProvided32(f, c))
			return false;
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

/* The Chinese and Japanese faces face_for found for faces of no role, by
 * set, size and cut: each opened the first time a line wants it, and kept
 * until ui_quit. The frame thread's, as face_for is. */
#define KIN_N 24
static struct { int set, pt; bool title, tried; TTF_Font *f; } kin[KIN_N];

static TTF_Font *kin_face(int set, int pt, bool title)
{
	char path[512];
	int i;

	for (i = 0; i < KIN_N && kin[i].tried; i++)
		if (kin[i].set == set && kin[i].pt == pt && kin[i].title == title) return kin[i].f;
	if (i == KIN_N || !font_path_kept[0]) return NULL;
	kin[i].set = set;
	kin[i].pt = pt;
	kin[i].title = title;
	kin[i].tried = true;
	set_path(set, title, path, sizeof path);
	kin[i].f = ui_face_open(path, pt);
	if (!kin[i].f && title) {
		set_path(set, false, path, sizeof path);
		kin[i].f = ui_face_open(path, pt);
	}
	return kin[i].f;
}

static void kin_free(void)
{
	for (int i = 0; i < KIN_N; i++) {
		if (kin[i].f) ui_face_close(kin[i].f);
		kin[i].f = NULL;
		kin[i].tried = false;
	}
}

/* The face s is drawn in when f is asked for: f, or where s has Chinese or
 * Japanese characters f has not, the first face of f's role that has them
 * all, the set that is on first, then Japanese's where s has kana and
 * Chinese's where it has none. So 简体中文 and 日本語 are themselves in MENU >
 * Language's panel in any language, and a game named in either is drawn
 * rather than boxes. A face of no role (home.c's page faces: the bar's
 * song, the cassette's label, the grid's names; ui_face_at's) looks the same
 * way among the Chinese and Japanese faces of its own size and cut; it used
 * to draw such a song title in Lexend, all boxes (Discord, 2026-11-23).
 * Asked only where a line is measured or rendered afresh: what
 * ui_text has cached was drawn this way already. */
static TTF_Font *face_for(TTF_Font *f, const char *s)
{
	int role = -1, order[3], pt = 0;
	bool title = false;

	if (!has_cjk(s) || covers(f, s)) return f;
	for (int k = 0; k < SETS && role < 0; k++)
		for (int i = 0; i < UI_F_COUNT; i++)
			if (f == font_sets[k][i]) { role = i; break; }
	if (role < 0 && !face_rec_of(f, &pt, &title)) return f;
	order[0] = set_on;
	/* Japanese where it has kana, or where it is Japanese's own name, which
	 * is kanji alone */
	order[1] = has_kana(s) || !strcmp(s, lang_name(LANG_JA)) ? SET_JP : SET_SC;
	order[2] = order[1] == SET_JP ? SET_SC : SET_JP;
	for (int k = 0; k < 3; k++) {
		TTF_Font *g;

		if (order[k] == SET_LATIN) continue;
		if (role >= 0) {
			set_open(order[k]);
			g = font_sets[order[k]][role];
		} else {
			g = kin_face(order[k], pt, title);
		}
		if (g && covers(g, s)) return g;
	}
	return f;
}

/* text_width in `f` when `f` is already the face `s` is drawn in (face_for's
 * answer for it): it reads nothing but the face, so a worker drawing a card
 * in faces of its own measures with it (card_face_for), and face_for, which
 * reads and opens the frame thread's sets, stays the frame thread's. */
static int width_in(TTF_Font *f, const char *s)
{
	const char *p = s, *m;
	char part[512];
	int w = 0, pw, len, kind;

	if (!f || !s) return 0;
	if (!has_marks(s)) {
		TTF_SizeUTF8(f, s, &w, NULL);
		return w;
	}
	while (*p) {
		size_t n;

		m = mark_next(s, p, &len, &kind);
		n = m ? (size_t)(m - p) : strlen(p);
		if (n) {
			snprintf(part, sizeof part, "%.*s", (int)n, p);
			pw = 0;
			TTF_SizeUTF8(f, part, &pw, NULL);
			w += pw;
		}
		if (!m) break;
		w += kind < 0 ? menu_mark_of(f).adv : shape_mark_of(f).adv;
		p = m + len;
	}
	return w;
}

static int text_width(TTF_Font *f, const char *s)
{
	if (!f || !s) return 0;
	return width_in(face_for(f, s), s);
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
/* Whether (fx, fy), from the key's middle, is in its mark: kind -1 MENU's
 * three dots, -2 a square, -3 a play triangle, in the proportions
 * ui_button's face gives them (the square 0.38 of the key across, the
 * triangle 0.49 of it tall, a little right of its box's middle) */
static bool key_mark_in(int kind, float fx, float fy, menu_mark m)
{
	if (kind == -2) {
		const float h = 0.19f * m.d;

		return fabsf(fx) <= h && fabsf(fy) <= h;
	}
	if (kind == -3) {
		const float q = 0.22f * m.d, x0 = -0.8f * q, x1 = 1.2f * q, hy = 1.1f * q;

		/* left edge x0; the two slopes meet at (x1, 0) */
		return fx >= x0 && fx <= x1 && fabsf(fy) <= hy * (x1 - fx) / (x1 - x0);
	}
	for (int k = -1; k <= 1; k++) {
		float gy = fy - (float)k * m.gap;

		if (fx * fx + gy * gy <= m.dot * m.dot) return true;
	}
	return false;
}

static void menu_mark_put(SDL_Surface *dst, int x, menu_mark m, int kind, SDL_Color col)
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
					bool in = (d <= R && d >= R - m.ring) || key_mark_in(kind, fx, fy, m);

					hit += in;
				}
			if (hit) row[px] = SDL_MapRGBA(dst->format, col.r, col.g, col.b,
			                                (Uint8)((hit * 255 + 8) / 16));
		}
	}
}

/* ui_text_surface in `f` when `f` is already the face `s` is drawn in, as
 * width_in is text_width's: the frame thread's sets are not read. */
static SDL_Surface *render_in(TTF_Font *f, const char *s, SDL_Color col)
{
	const char *p = s, *m;
	SDL_Surface *out;
	char part[512];
	int x = 0, len, kind;

	if (!f || !s || !*s) return NULL;
	if (!has_marks(s)) return TTF_RenderUTF8_Blended(f, s, col);
	out = SDL_CreateRGBSurfaceWithFormat(0, width_in(f, s), TTF_FontHeight(f),
	                                     32, SDL_PIXELFORMAT_ARGB8888);
	if (!out) return NULL;
	SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, col.r, col.g, col.b, 0));
	while (*p) {
		size_t n;

		m = mark_next(s, p, &len, &kind);
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
			if (kind < 0) menu_mark_put(out, x, menu_mark_of(f), kind, col);
			else          shape_put(out, x, shape_mark_of(f), kind, col);
			SDL_UnlockSurface(out);
		}
		x += kind < 0 ? menu_mark_of(f).adv : shape_mark_of(f).adv;
		p = m + len;
	}
	return out;
}

SDL_Surface *ui_text_surface(TTF_Font *f, const char *s, SDL_Color col)
{
	if (!f || !s || !*s) return NULL;
	return render_in(face_for(f, s), s, col);
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
/* Drawing a picture of a screen into a texture, not the screen (ui_fit_away):
 * its words are that screen's, which its own shots hold, and are not held
 * against the screen it is drawn on */
static int fit_away;

void ui_fit_away(bool on) { fit_away += on ? 1 : -1; }

#ifdef I18N_PSEUDO
static bool i18n_watched(const char *s)
{
	return s && (i18n_owns(s) || (i18n_lang() == LANG_PSEUDO && strstr(s, "\xc2\xb7]")));
}

static void i18n_note(const char *what, const char *s)
{
	if (fit_away) return;
	static unsigned said[256];
	unsigned h = 5381;

	for (const char *p = what; *p; p++) h = h * 33 + (unsigned char)*p;
	for (const char *p = s; *p; p++) h = h * 33 + (unsigned char)*p;
	for (int i = 0; i < 256 && said[i]; i++) if (said[i] == h) return;
	for (int i = 0; i < 256; i++) if (!said[i]) { said[i] = h; break; }
	fprintf(stderr, "i18n: %s: %s\n", what, s);
}

/* A row's value that fits its room by less than 2% of the row: the Pixel
 * 2's fonts measure a little wider than the host's, and "auto
 * (alto-falante)", 3 units to spare in MENU on the host, was cut there
 * (2026-11-10). "i18n: tight: ..." (or "tight data"), which
 * tools/i18n-shots.py holds as it holds a cut. Only for a menu row's value,
 * where the room is what the label leaves: a fitted line elsewhere is tight
 * by design (a balanced line, a button's word). */
void ui_fit_tight(const char *s, int w, int room, int row_w)
{
	int spare = row_w / 100;

	if (s && w <= room && w > room - spare)
		i18n_note(i18n_watched(s) ? "tight" : "tight data", s);
}

/* ---- the box a string must stay inside ---------------------------------
 *
 * A string that is wider than the panel, tile or plate it is drawn on is as
 * broken as one past the screen, and the screen's edge says nothing of it
 * (the Computer Connected question's hint ran 125 units past its two-tile
 * panel, 2026-11-07). So the rects a frame fills (ui_round_rect, which
 * ui_panel and the tiles, cards and plates are made of) are kept, and each
 * string drawn is held to the smallest of them its anchor and its middle are
 * in, less a margin: text touching an edge is overflowing it too. And two
 * strings of a line must not meet. Every string, not only the table's: a
 * label copied into a row's buffer or formatted around a name is no longer
 * the table's pointer, and it is the one that overflows.
 *
 * "fit: overflow: PX|BOX|TEXT" and, once a string, "fit: drawn: TEXT" (not
 * "i18n: ", which other checks read as a string that did not fit;
 * tools/i18n-shots.py names the screen, the language and the string ID, and
 * from the drawn ones what no shot shows). Only on a host. */
#define FIT_RECTS 128
#define FIT_MARGIN 3                /* units of air a string keeps from its box: a key's are 3 to 4 */
static SDL_Rect fit_rect[FIT_RECTS];
static int fit_n;
static struct { SDL_Rect r; unsigned h; char text[32]; } fit_line[FIT_RECTS];
static int fit_lines;
/* The keys and the marks: every button glyph and more mark drawn, and every
 * string's ink (its capitals' top to its baseline: a descender is one
 * letter's, and taken across the whole string it met a key under the "an"
 * of "Japan"), so that a string meeting a key or a mark
 * is said, whichever is drawn first. A button's own letter is not a string
 * that meets it (fit_quiet). */
static SDL_Rect fit_mark[FIT_RECTS];
static int fit_marks;
static struct { SDL_Rect r, dst; TTF_Font *f; char s[256]; char text[32]; } fit_ink[FIT_RECTS];
static int fit_inks, fit_quiet;

void ui_fit_frame(void)
{
	if (fit_away) return;
	fit_n = 0; fit_lines = 0; fit_marks = 0; fit_inks = 0;
}

/* Do `a` and `b` come within `m` of each other? How far, top to bottom */
static bool fit_meet(const SDL_Rect *a, const SDL_Rect *b, int m, int *px)
{
	int top = a->y > b->y - m ? a->y : b->y - m;
	int bot = a->y + a->h < b->y + b->h + m ? a->y + a->h : b->y + b->h + m;
	int left = a->x > b->x - m ? a->x : b->x - m;
	int right = a->x + a->w < b->x + b->w + m ? a->x + a->w : b->x + b->w + m;

	if (bot <= top || right <= left) return false;
	*px = bot - top;
	return true;
}

static void fit_box(const SDL_Rect *q)
{
	if (fit_away) return;
	if (fit_n < FIT_RECTS) fit_rect[fit_n++] = *q;
}

/* The text with its line ends as \n, on one line of the log */
static void fit_say(const char *what, const char *head, const char *s)
{
	if (fit_away) return;
	static unsigned said[4096];
	char line[1100], *o = line;
	unsigned h = 5381;
	int i;

	for (const char *p = what; *p; p++) h = h * 33 + (unsigned char)*p;
	for (const char *p = head; *p; p++) h = h * 33 + (unsigned char)*p;
	for (const char *p = s; *p; p++) h = h * 33 + (unsigned char)*p;
	if (!h) h = 1;
	for (i = (int)(h % 4096); said[i] && said[i] != h; i = (i + 1) % 4096) {}
	if (said[i]) return;
	said[i] = h;
	o += snprintf(line, sizeof line, "%s", head);
	for (; *s && o < line + sizeof line - 3; s++) {
		if (*s == '\n') { *o++ = '\\'; *o++ = 'n'; }
		else *o++ = *s;
	}
	*o = '\0';
	fprintf(stderr, "fit: %s: %s\n", what, line);
}

void ui_fit_drawn(const char *s)
{
	if (s && *s) fit_say("drawn", "", s);
}

/* A menu row's value, "fit: value: TEXT" once a string: tools/i18n-shots.py
 * fails on one of the table's that begins with a small letter (a row's value
 * begins with a capital, res/strings/GLOSSARY.md) */
void ui_fit_value(const char *s)
{
	if (s && *s) fit_say("value", "", s);
}

/* Where the rough boxes come close, the string's own ink: rendered as
 * ui_text draws it (dst is where it was drawn), the rows of it with a pixel
 * of ink within `m` of `mark`. A line of lowercase under a key keeps its
 * x-height's air, which its capitals' box does not (Storage's caption under
 * the sort key, 14 units clear, read as touching). */
static int fit_ink_rows(TTF_Font *f, const char *s, const SDL_Rect *dst, const SDL_Rect *mark, int m)
{
	SDL_Rect q = { mark->x - m, mark->y - m, mark->w + 2 * m, mark->h + 2 * m }, hit;
	SDL_Surface *surf;
	int x, y, rows = 0;

	if (!SDL_IntersectRect(dst, &q, &hit)) return 0;
	if (!(surf = ui_text_surface(f, s, (SDL_Color){ 255, 255, 255, 255 }))) return 1;
	SDL_LockSurface(surf);
	for (y = hit.y; y < hit.y + hit.h; y++) {
		int sy = y - dst->y;
		bool any = false;

		if (sy < 0 || sy >= surf->h) continue;
		for (x = hit.x; x < hit.x + hit.w && !any; x++) {
			int sx = x - dst->x;
			Uint8 cr, cg, cb, ca;
			Uint32 px;

			if (sx < 0 || sx >= surf->w) continue;
			px = ((Uint32 *)((Uint8 *)surf->pixels + (size_t)sy * surf->pitch))[sx];
			SDL_GetRGBA(px, surf->format, &cr, &cg, &cb, &ca);
			any = ca > 64;
		}
		rows += any;
	}
	SDL_UnlockSurface(surf);
	SDL_FreeSurface(surf);
	return rows;
}

void ui_fit_mark(const SDL_Rect *q)
{
	char head[96];
	int i, px, rows;

	if (fit_away) return;
	for (i = 0; i < fit_inks; i++)
		if (fit_meet(&fit_ink[i].r, q, FIT_MARGIN, &px) &&
		    (rows = fit_ink_rows(fit_ink[i].f, fit_ink[i].s, &fit_ink[i].dst, q, FIT_MARGIN)) > 0) {
			snprintf(head, sizeof head, "%d|keys %d..%d|", rows, q->x, q->x + q->w);
			fit_say("overflow", head, fit_ink[i].text);
		}
	if (fit_marks < FIT_RECTS) fit_mark[fit_marks++] = *q;
}

/* A string's ink against the keys and marks drawn before it, then kept for
 * the ones after */
static void fit_ink_check(TTF_Font *f, const char *s, const SDL_Rect *dst, const SDL_Rect *ink)
{
	char head[96];
	int i, px, rows;

	if (fit_away) return;
	for (i = 0; i < fit_marks; i++)
		if (fit_meet(ink, &fit_mark[i], FIT_MARGIN, &px) &&
		    (rows = fit_ink_rows(f, s, dst, &fit_mark[i], FIT_MARGIN)) > 0) {
			snprintf(head, sizeof head, "%d|keys %d..%d|", rows, fit_mark[i].x, fit_mark[i].x + fit_mark[i].w);
			fit_say("overflow", head, s);
		}
	if (fit_inks < FIT_RECTS) {
		fit_ink[fit_inks].r = *ink;
		fit_ink[fit_inks].dst = *dst;
		fit_ink[fit_inks].f = f;
		snprintf(fit_ink[fit_inks].s, sizeof fit_ink[fit_inks].s, "%s", s);
		snprintf(fit_ink[fit_inks].text, sizeof fit_ink[fit_inks].text, "%.30s", s);
		for (char *c = fit_ink[fit_inks].text; *c; c++) if (*c == '\n' || *c == '|') *c = ' ';
		fit_inks++;
	}
}

/* `dst` is where `s` is drawn, at anchor x `ax` */
static void fit_check(SDL_Renderer *r, const char *s, const SDL_Rect *dst, int ax)
{
	const SDL_Rect *box = NULL;
	char head[128];
	unsigned hash = 5381;
	int i, cy = dst->y + dst->h / 2, over;
	long best = 0;

	if (fit_away) return;
	for (const char *p = s; *p; p++) hash = hash * 33 + (unsigned char)*p;

	fit_say("drawn", "", s);
	if (SDL_RenderIsClipEnabled(r)) return;
	for (i = 0; i < fit_n; i++) {
		const SDL_Rect *q = &fit_rect[i];
		long area = (long)q->w * q->h;

		if (ax < q->x || ax >= q->x + q->w || cy < q->y || cy >= q->y + q->h) continue;
		if (q->h < dst->h / 2 || q->w < 24) continue;
		if (!box || area <= best) { box = q; best = area; }
	}
	if (box) {
		over = box->x + FIT_MARGIN - dst->x;
		if (dst->x + dst->w - (box->x + box->w - FIT_MARGIN) > over)
			over = dst->x + dst->w - (box->x + box->w - FIT_MARGIN);
		if (over > 0) {
			snprintf(head, sizeof head, "%d|box %d..%d|", over, box->x, box->x + box->w);
			fit_say("overflow", head, s);
		}
	}
	/* Two strings of a line must not meet: a label and its value, a
	 * footer's two ends */
	for (i = 0; i < fit_lines; i++) {
		const SDL_Rect *o = &fit_line[i].r;
		int top = dst->y > o->y ? dst->y : o->y;
		int bot = dst->y + dst->h < o->y + o->h ? dst->y + dst->h : o->y + o->h;
		int left = dst->x > o->x ? dst->x : o->x;
		int right = dst->x + dst->w < o->x + o->w ? dst->x + dst->w : o->x + o->w;

		/* The same string drawn twice (a plate's focused copy) is not a meeting,
		 * nor is one drawn over most of the other (an animation's two frames) */
		if (fit_line[i].h != hash && bot - top > dst->h / 2 && right - left > 2 &&
		    (right - left) * 5 < (dst->w < o->w ? dst->w : o->w) * 3) {
			snprintf(head, sizeof head, "%d|meets %d..%d %s|", right - left, o->x, o->x + o->w,
			         fit_line[i].text);
			fit_say("overflow", head, s);
		}
	}
	if (fit_lines < FIT_RECTS) {
		fit_line[fit_lines].r = *dst;
		fit_line[fit_lines].h = hash;
		snprintf(fit_line[fit_lines].text, sizeof fit_line[fit_lines].text, "%.30s", s);
		for (char *c = fit_line[fit_lines].text; *c; c++) if (*c == '\n' || *c == '|') *c = ' ';
		fit_lines++;
	}
	/* Past the screen's edge (the 1024 units' design space) */
	over = FIT_MARGIN - dst->x;
	if (dst->x + dst->w - (1024 - FIT_MARGIN) > over) over = dst->x + dst->w - (1024 - FIT_MARGIN);
	if (over > 0) {
		snprintf(head, sizeof head, "%d|screen 0..1024|", over);
		fit_say("overflow", head, s);
	}
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
void ui_fit_frame(void) {}
void ui_fit_tight(const char *s, int w, int room, int row_w) { (void)s; (void)w; (void)room; (void)row_w; }
void ui_fit_drawn(const char *s) { (void)s; }
void ui_fit_value(const char *s) { (void)s; }
void ui_fit_mark(const SDL_Rect *q) { (void)q; }
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
	fit_check(r, s, &dst, x);
	if (!fit_quiet && !SDL_RenderIsClipEnabled(r)) {
		int asc = TTF_FontAscent(f), cap = ui_cap_height(f);
		SDL_Rect ink = { dst.x, y + asc - cap, dst.w, cap };

		fit_ink_check(f, s, &dst, &ink);
	}
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

/* ---- parts drawn once -------------------------------------------------
 * The Pixel 2's GPU takes each one-pixel line in a new color as a draw call of
 * its own (panfrost): the five graded bars of Screen Colors, a line for each
 * half of each column, made the page draw 13 frames a second while Down was
 * held, 66.6 ms a frame against the Display tab's 22 (2026-11-16). So such a
 * part is drawn once into a texture and copied, one draw a frame. Only what is
 * opaque and the same in both themes goes in, and it is copied 1:1 where it was
 * drawn, so the copy puts the very pixels the drawing did and leaves the rest
 * (tools/scale-check.c compares the two). Everything the renderer had - its
 * target, viewport, clip, scale, blend and color - is put back after. */
#define BAKES 32
static struct {
	SDL_Renderer *r;
	const char   *what;
	int           k, w, h;
	SDL_Texture  *t;
} bakes[BAKES];
static int bake_next;
bool ui_bake_on = true;
unsigned long ui_calls;

SDL_Texture *ui_bake(SDL_Renderer *r, const char *what, int k, int w, int h,
                     void (*draw)(SDL_Renderer *r, int k, int w, int h))
{
	SDL_Texture *t, *was;
	SDL_Rect view, clip;
	SDL_bool clipped;
	SDL_BlendMode mode;
	Uint8 cr, cg, cb, ca;
	float sx, sy;
	int i;

	if (!ui_bake_on || !r || w <= 0 || h <= 0) return NULL;
	for (i = 0; i < BAKES; i++)
		if (bakes[i].t && bakes[i].r == r && bakes[i].what == what && bakes[i].k == k &&
		    bakes[i].w == w && bakes[i].h == h)
			return bakes[i].t;
	if (!(t = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET, w, h)))
		return NULL;
	was = SDL_GetRenderTarget(r);
	SDL_RenderGetViewport(r, &view);
	clipped = SDL_RenderIsClipEnabled(r);
	SDL_RenderGetClipRect(r, &clip);
	SDL_RenderGetScale(r, &sx, &sy);
	SDL_GetRenderDrawBlendMode(r, &mode);
	SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
	if (SDL_SetRenderTarget(r, t) != 0) {
		SDL_DestroyTexture(t);
		return NULL;
	}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
	SDL_RenderClear(r);
	draw(r, k, w, h);
	SDL_SetRenderTarget(r, was);
	SDL_RenderSetScale(r, sx, sy);
	SDL_RenderSetViewport(r, &view);
	SDL_RenderSetClipRect(r, clipped ? &clip : NULL);
	SDL_SetRenderDrawBlendMode(r, mode);
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
	if (bakes[bake_next].t) SDL_DestroyTexture(bakes[bake_next].t);
	bakes[bake_next].r = r;
	bakes[bake_next].what = what;
	bakes[bake_next].k = k;
	bakes[bake_next].w = w;
	bakes[bake_next].h = h;
	bakes[bake_next].t = t;
	bake_next = (bake_next + 1) % BAKES;
	return t;
}

static void bakes_free(void)
{
	for (int i = 0; i < BAKES; i++) {
		if (bakes[i].t) SDL_DestroyTexture(bakes[i].t);
		bakes[i].t = NULL;
	}
	bake_next = 0;
}

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

void ui_text_faded(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
                   int lo, int hi, int fade, SDL_Color col)
{
	struct text_entry *e;
	SDL_Rect clip, was, dst;
	SDL_bool had;
	int i, lf, rf;

	if (!f || !s || !*s || hi <= lo || col.a == 0) return;
	e = text_get(r, f, s, col);
	if (!e) return;
	dst = (SDL_Rect){ x, y, e->w, e->h };
	if (x >= hi || x + e->w <= lo) return;              /* all outside */
	/* Wholly inside and clear of both fades: as ui_text draws it */
	if (x >= lo + fade && x + e->w <= hi - fade) {
		ui_text(r, f, s, x, y, -1, col);
		return;
	}
	/* The fades are only as wide as there is room for, between the ends */
	lf = rf = fade;
	if (lf + rf > hi - lo) lf = rf = (hi - lo) / 2;

	had = SDL_RenderIsClipEnabled(r);
	if (had) SDL_RenderGetClipRect(r, &was);
	clip.y = y;
	clip.h = e->h;
	/* Each slice drawn once, at its own alpha, the clip choosing which part
	 * of the text lands (the marquee's rule, below): the middle whole, then
	 * the two fades a step at a time toward their ends */
	clip.x = lo + lf;
	clip.w = hi - rf - clip.x;
	if (clip.w > 0 && clip_in(r, &clip, had ? &was : NULL)) {
		SDL_SetTextureAlphaMod(e->tex, col.a);
		SDL_RenderCopy(r, e->tex, NULL, &dst);
	}
	for (i = 0; i < lf; i += MQ_FADE_STEP) {
		clip.x = lo + i;
		clip.w = lf - i < MQ_FADE_STEP ? lf - i : MQ_FADE_STEP;
		SDL_SetTextureAlphaMod(e->tex, (Uint8)(col.a * i / lf));
		if (clip_in(r, &clip, had ? &was : NULL)) SDL_RenderCopy(r, e->tex, NULL, &dst);
	}
	for (i = 0; i < rf; i += MQ_FADE_STEP) {
		int sw = rf - i < MQ_FADE_STEP ? rf - i : MQ_FADE_STEP;

		clip.x = hi - i - sw;
		clip.w = sw;
		SDL_SetTextureAlphaMod(e->tex, (Uint8)(col.a * i / rf));
		if (clip_in(r, &clip, had ? &was : NULL)) SDL_RenderCopy(r, e->tex, NULL, &dst);
	}
	SDL_SetTextureAlphaMod(e->tex, 255);
	SDL_RenderSetClipRect(r, had ? &was : NULL);
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

#ifdef I18N_PSEUDO
/* A balanced text cut: every line full and the last cut (the text is longer
 * than its box), or a word longer than a line cut where it stands, in any
 * line ("Röhrenfernseh..." in a narrow note, which the last line's dots
 * alone did not say) */
static void balance_cut_note(const char *text, char lines[][LB_LINE], int n)
{
	int k;

	if (strstr(text, "...")) return;
	for (k = 0; k < n; k++)
		if (strstr(lines[k], "...")) {
			i18n_note(i18n_watched(text) ? "cut" : "cut data", text);
			return;
		}
}
#endif

/* `text` broken by `brk` (lb_balance or lb_fill), measured in `f`, and
 * remembered under `kind` when it is short enough to keep */
typedef int (*lines_fn)(const char *text, int maxw, int cap, lb_width_fn width,
                        void *ctx, char lines[][LB_LINE]);

static int break_lines(int kind, lines_fn brk, TTF_Font *f, const char *text, int maxw,
                       int cap, char lines[][LB_LINE])
{
	memo_entry *e;
	int n;

	if (!f) return 0;
	if (cap < 1 || cap > MEMO_LINES || !memo_usable(text)) {
		n = brk(text, maxw, cap, ttf_width, f, lines);
#ifdef I18N_PSEUDO
		balance_cut_note(text, lines, n);
#endif
		return n;
	}
	if ((e = memo_find(kind, f, text, maxw, cap, 0))) {
		memcpy(lines, e->out, (size_t)e->n * LB_LINE);
		return e->n;
	}
	n = brk(text, maxw, cap, ttf_width, f, lines);
#ifdef I18N_PSEUDO
	balance_cut_note(text, lines, n);
#endif
	if (n < 0 || n > cap) return n;
	e = memo_slot(kind, f, text, maxw, cap, 0);
	e->n = n;
	memcpy(e->out, lines, (size_t)n * LB_LINE);
	return n;
}

int ui_balance_lines(TTF_Font *f, const char *text, int maxw, int cap,
                     char lines[][LB_LINE])
{
	return break_lines(MEMO_BALANCE, lb_balance, f, text, maxw, cap, lines);
}

int ui_fill_lines(TTF_Font *f, const char *text, int maxw, int cap,
                  char lines[][LB_LINE])
{
	return break_lines(MEMO_FILL, lb_fill, f, text, maxw, cap, lines);
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
static void shadow_tint(SDL_Renderer *r, const SDL_Rect *q, int kind, ds_rgb ink, int alpha);

static void shadow_draw(SDL_Renderer *r, const SDL_Rect *q, int kind,
                        const ds_theme *th, int alpha)
{
	shadow_tint(r, q, kind, th->ink, alpha);
}

/* A shadow of `kind` in any color: Highlight's lift leans toward the accent's
 * ink in Beard, and is ink black under Salt & Pepper's light */
static void shadow_tint(SDL_Renderer *r, const SDL_Rect *q, int kind, ds_rgb ink, int alpha)
{
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

/* See ui.h. Highlight's lift (the focus lab, 2026-11-19): soft and tinted,
 * never grey haze on Beard's light ground and never gone on Salt & Pepper's
 * dark one. Beard: a short shadow of ink under the item, and for a high lift
 * a long one leaning a third of the way to the accent's ink; Salt & Pepper:
 * a light in the accent and an ink-black shadow under it. The shadows are
 * the lift's own textures (UI_LIFT_FOCUS_SMALL's and UI_LIFT_FOCUS's),
 * tinted: nothing is made for it. */
void ui_lift_focus(SDL_Renderer *r, const SDL_Rect *q, bool high, float k)
{
	const ds_theme *t = theme();

	if (!(k > 0.0f) || q->w <= 0 || q->h <= 0) return;
	if (k > 1.0f) k = 1.0f;
	if (theme_index() == DS_DARK) {
		glow_draw(r, q, ui_rgb(t->accent), (int)((high ? 96 : 66) * k + 0.5f),
		          high ? DS_GLOW_FOCUS_SPREAD : DS_GLOW_FOCUS_SMALL_SPREAD, SDL_BLENDMODE_ADD, t);
		shadow_tint(r, q, high ? UI_LIFT_FOCUS : UI_LIFT_FOCUS_SMALL, (ds_rgb){ 0, 0, 0 },
		            (int)((high ? 150 : 120) * k + 0.5f));
		return;
	}
	{
		const ds_rgb a = t->ink, b = t->accent_ink;
		const ds_rgb tint = { (Uint8)(a.r + (b.r - a.r) * 2 / 5), (Uint8)(a.g + (b.g - a.g) * 2 / 5),
		                      (Uint8)(a.b + (b.b - a.b) * 2 / 5) };

		if (high) {
			shadow_tint(r, q, UI_LIFT_FOCUS_SMALL, a, (int)(36 * k + 0.5f));
			shadow_tint(r, q, UI_LIFT_FOCUS, tint, (int)(76 * k + 0.5f));
		} else {
			shadow_tint(r, q, UI_LIFT_FOCUS_SMALL, tint, (int)(62 * k + 0.5f));
		}
	}
}

void ui_scrim_k(SDL_Renderer *r, int game, float k)
{
	const ds_theme *t = theme();
	int alpha = game ? t->scrim_game : t->scrim_menu;

	if (!(k > 0.0f)) return;
	ui_fit_frame();   /* what is under a scrim is another layer's */
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
	 * since CFG_NO_COLOR is not a color to blend - and for MP4's lime, whose
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
#ifdef I18N_PSEUDO
	fit_box(q);
#endif
	radius = round_rect_radius(q->w, q->h, radius);
	/* Corners: Square (DESIGN.md 3.5): every corner under a pill's goes
	 * square - a panel, the focus fill, a key, a chip - and a pill, whose
	 * radius is half its height, stays a pill, as a circle stays a circle */
	if (look_square() && radius * 2 < (q->w < q->h ? q->w : q->h)) radius = 0;

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
			button_fonts[i].f = path[0] ? ui_face_open(path, pt) : NULL;
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

TTF_Font *ui_face_at(int pt, bool semibold)
{
	return button_font(pt, semibold);
}

static void button_fonts_free(void)
{
	int i;

	memo_clear();
	for (i = 0; i < BUTTON_FONTS; i++) {
		if (button_fonts[i].f) ui_face_close(button_fonts[i].f);
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

/* The d-pad's hint (keyhints.h): "< >" its left and right, "^ v" its up and
 * down. Drawn as the d-pad itself (dpad_key), the arms it means lit, not as a
 * pill with the signs on it (1.0.6: Tarik, "fix the arrow buttons hint design.
 * Check the pixel 2 3d model for reference on how they look"). Which arms:
 * 1 left and right, 2 up and down, 0 for any other glyph. */
static int key_dpad(ds_key k)
{
	return k.shape == DS_KEY_DPAD_LR ? 1 : k.shape == DS_KEY_DPAD_UD ? 2 : k.shape == DS_KEY_DPAD ? 3 : 0;
}

static void dpad_key(SDL_Renderer *r, float cx, float cy, float size, int arms, SDL_Color edge,
                     SDL_Color face, SDL_Color arm, SDL_Color ink);
/* A d-pad's key is this much a button glyph's size: a cross has less ink
 * than a face's disc, and at the same size it read small beside them */
#define DPAD_KEY_K 1.25f

/* Round: a face (A, B, X and Y; the Pixel 2's MENU, drawn as its three dots
 * rather than a word, menu_key_dots; the Brick's SELECT and START, their
 * square and play triangle, key_mark) or a stick's cap. Which glyph a name
 * is comes from the handheld's table (src/ds/button.h). */
static bool key_round(ds_key k)
{
	return k.shape == DS_KEY_FACE || k.shape == DS_KEY_STICK;
}

static void menu_key_dots(SDL_Renderer *r, TTF_Font *f, float cx, float cy,
                          float k, SDL_Color col);
static void key_mark(SDL_Renderer *r, TTF_Font *f, float cx, float cy, float k, ds_key_mark m,
                     SDL_Color col);

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

/* A face's letters: one at the face's size; two (the Pixel 2's SE and ST,
 * a stick's L3) at 5/8 of it, as the model engraves them (1.75 mm to a face
 * letter's 2.8, tools/build-pixel2-model.py) */
static TTF_Font *key_letters(ds_key k, bool round, int size)
{
	if (round && k.mark == DS_KEY_MARK_NONE && k.text && k.text[0] && k.text[1])
		return button_font((button_pt(true, size) * 5 + 4) / 8, true);
	return button_letter(round, size);
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

/* A chord: buttons named with "/" between them for either (L1/R1), "+"
 * for together (SELECT+START) and ", " for one after the other (MENU, X),
 * each its own glyph with the sign between them (DESIGN.md 11.9), as the
 * device-modular spec's seam says. A sign with nothing on one side is part
 * of a name (VOL+). Up to CHORD_MAX names; the pieces into `part`, the signs
 * into `sign`; the count, 1 for no chord. */
#define CHORD_MAX  4
#define CHORD_NAME 16
static int chord_split(const char *label, char part[CHORD_MAX][CHORD_NAME], char sign[CHORD_MAX])
{
	int n = 0, len = 0;
	const char *p;

	if (!label) return 0;
	for (p = label; *p; p++) {
		if ((*p == '/' || *p == '+') && len > 0 && p[1] && p[1] != '/' && p[1] != '+' && n < CHORD_MAX - 1) {
			part[n][len] = '\0';
			sign[n++] = *p;
			len = 0;
			continue;
		}
		if (*p == ',' && p[1] == ' ' && p[2] && len > 0 && n < CHORD_MAX - 1) {
			part[n][len] = '\0';
			sign[n++] = ',';
			len = 0;
			p++;                      /* and its space */
			continue;
		}
		if (len >= CHORD_NAME - 1) return 1;
		part[n][len++] = *p;
	}
	part[n][len] = '\0';
	return n + 1;
}

/* The air either side of a chord's sign, on the 4-unit grid: 4 at the
 * hints' 32 to the keyboard's 44, 8 at the tutorial's 64, so a chord reads
 * as one hint and not two */
static int chord_gap(int size)
{
	return (size + 16) / 32 * 4;
}

/* The sign in the hints' words, **text-dim** */
static TTF_Font *chord_font(void)
{
	return ui_font(UI_F_META);
}

/* The air before and after sign `s`: a chord's own either side, and a
 * comma close to the press before it and twice as far from the next, as a
 * comma sits in a sentence */
static int sign_before(char s, int size) { return s == ',' ? chord_gap(size) / 2 : chord_gap(size); }
static int sign_after(char s, int size)  { return s == ',' ? chord_gap(size) * 2 : chord_gap(size); }

static int chord_width(char part[CHORD_MAX][CHORD_NAME], const char sign[CHORD_MAX], int n, int size)
{
	int w = 0, i;

	for (i = 0; i < n; i++) {
		w += ui_button_width(part[i], size);
		if (i + 1 < n) {
			const char s[2] = { sign[i], '\0' };

			w += sign_before(sign[i], size) + sign_after(sign[i], size) + ui_text_width(chord_font(), s);
		}
	}
	return w;
}

int ui_button_width(const char *label, int size)
{
	char part[CHORD_MAX][CHORD_NAME], sign[CHORD_MAX];
	const int n = chord_split(label, part, sign);
	const ds_key k = ds_key_of(label);
	bool round = key_round(k);

	if (n > 1) return chord_width(part, sign, n, size);
	if (k.shape == DS_KEY_ROCKER) return ui_rocker_width(size);
	if (round) return size;
	if (key_dpad(k)) return (int)((float)size * DPAD_KEY_K + 0.5f);
	return ui_text_width(button_letter(round, size), k.text)
	       + button_pad(size) * 2;
}

/* Each of a chord's glyphs, left to right from its whole width's left edge,
 * its signs between them; `pressed` draws each as ui_button_pressed would */
static void chord_draw(SDL_Renderer *r, char part[CHORD_MAX][CHORD_NAME], const char sign[CHORD_MAX],
                       int n, int cx, int cy, int size, int style, bool pressed, float press,
                       float scale, float op)
{
	int x = cx - chord_width(part, sign, n, size) / 2, i;

	for (i = 0; i < n; i++) {
		const int w = ui_button_width(part[i], size);

		if (pressed) ui_button_pressed(r, part[i], x + w / 2, cy, size, style, press, scale, op);
		else ui_button(r, part[i], x + w / 2, cy, size, style);
		x += w;
		if (i + 1 < n) {
			const char s[2] = { sign[i], '\0' };
			TTF_Font *f = chord_font();
			SDL_Color c = style == UI_BUTTON_ON_ACCENT ? ui_col(theme()->on_accent, 255)
			                                          : ui_col(theme()->text_dim, 255);

			if (pressed) c.a = (Uint8)(255.0f * (op < 0.0f ? 0.0f : op > 1.0f ? 1.0f : op) + 0.5f);
			x += sign_before(sign[i], size);
			ui_text(r, f, s, x + ui_text_width(f, s) / 2, ui_cap_y(f, cy), 0, c);
			x += ui_text_width(f, s) + sign_after(sign[i], size);
		}
	}
}

/* On the accent fill, the glyph's own face is the fill with this much
 * **on-accent** laid over it: the keyboard lab's 12%, enough to say "a
 * button" inside the selected key without a second color. */
#define BUTTON_ON_ACCENT_TINT 0.12f

void ui_button(SDL_Renderer *r, const char *label, int cx, int cy, int size,
               int style)
{
	const ds_theme *t = theme();
	const ds_key k = ds_key_of(label);
	bool round = key_round(k);
	TTF_Font *f;
	int w, ring;
	SDL_Color edge, fill, ink;
	SDL_Rect q, in;
	char part[CHORD_MAX][CHORD_NAME], sign[CHORD_MAX];
	int parts;

	if (!label || size <= 0) return;
	if ((parts = chord_split(label, part, sign)) > 1) {
		chord_draw(r, part, sign, parts, cx, cy, size, style, false, 0.0f, 1.0f, 1.0f);
		return;
	}
	if (k.shape == DS_KEY_ROCKER) {
		ui_rocker(r, cx, cy, size, 0.0f, 0.0f, 1.0f, 1.0f);
		return;
	}
	if (key_dpad(k)) {
		const float ds = (float)size * DPAD_KEY_K;
		const int dw = (int)(ds + 0.5f);

		if (style == UI_BUTTON_ON_ACCENT) {
			unsigned face = ui_mix(ui_rgb(t->accent), ui_rgb(t->on_accent), BUTTON_ON_ACCENT_TINT);
			unsigned arm = ui_mix(face, ui_rgb(t->on_accent), 0.25f);

			dpad_key(r, (float)cx, (float)cy, ds, key_dpad(k), ui_col(t->on_accent, 255),
			         (SDL_Color){ (Uint8)(face >> 16), (Uint8)(face >> 8), (Uint8)face, 255 },
			         (SDL_Color){ (Uint8)(arm >> 16), (Uint8)(arm >> 8), (Uint8)arm, 255 },
			         ui_col(t->on_accent, 255));
		} else {
			unsigned arm = ui_mix(ui_rgb(t->raised), ui_rgb(t->text_dim), 0.35f);

			dpad_key(r, (float)cx, (float)cy, ds, key_dpad(k), ui_col(t->edge, 255), ui_col(t->raised, 255),
			         (SDL_Color){ (Uint8)(arm >> 16), (Uint8)(arm >> 8), (Uint8)arm, 255 }, ui_col(t->text, 255));
		}
		ui_fit_mark(&(SDL_Rect){ cx - dw / 2, cy - dw / 2, dw, dw });
		return;
	}
	f = key_letters(k, round, size);
	w = round ? size : ui_text_width(f, k.text) + button_pad(size) * 2;
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
	/* A stick's cap: its top's ring inside, under the word */
	if (k.shape == DS_KEY_STICK) {
		const int sd = size * 7 / 10;
		SDL_Rect s = { cx - sd / 2, cy - sd / 2, sd, sd };
		SDL_Rect si = { s.x + ring, s.y + ring, sd - ring * 2, sd - ring * 2 };

		ui_round_rect(r, &s, sd / 2, edge);
		ui_round_rect(r, &si, sd / 2 - ring, fill);
	}

	/* Centered on the capitals: every label is capitals and digits, and the
	 * em box would hang a descender's depth of nothing under them and sit
	 * them high in the glyph. */
#ifdef I18N_PSEUDO
	fit_quiet++;
#endif
	if (k.mark == DS_KEY_MARK_DOTS) menu_key_dots(r, f, (float)cx, (float)cy, 1.0f, ink);
	else if (k.mark != DS_KEY_MARK_NONE) key_mark(r, f, (float)cx, (float)cy, 1.0f, k.mark, ink);
	else ui_text(r, f, k.text, cx, ui_cap_y(f, cy), 0, ink);
#ifdef I18N_PSEUDO
	fit_quiet--;
#endif
	ui_fit_mark(&q);
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

/* The Brick's SELECT square and START play triangle, as its handheld draws
 * them (tourdevice.c, bk_icon), sized from the letter's capitals as MENU's
 * dots are, so a mark weighs what a face's letter does: the triangle a
 * capital tall and a little right of its box's middle, so it looks centred,
 * the square 0.72 of a capital across, as a solid square carries more ink
 * than a letter's strokes; in the word's color, as the dots are */
static void key_mark(SDL_Renderer *r, TTF_Font *f, float cx, float cy, float k, ds_key_mark m,
                     SDL_Color col)
{
	const float q = (float)ui_cap_height(f) * k * 0.42f;

	if (m == DS_KEY_MARK_SQUARE) {
		const float s = q * (0.36f / 0.42f);
		const SDL_FPoint p[4] = { { cx - s, cy - s }, { cx + s, cy - s }, { cx + s, cy + s }, { cx - s, cy + s } };

		fan(r, p, 4, col);
	} else if (m == DS_KEY_MARK_PLAY) {
		const SDL_FPoint p[3] = { { cx - 0.8f * q, cy - 1.1f * q }, { cx + 1.2f * q, cy },
		                          { cx - 0.8f * q, cy + 1.1f * q } };

		fan(r, p, 3, col);
	}
}

/* A stick's cap's top: a ring 0.7 of the cap across, inside it */
static void stick_ring(SDL_Renderer *r, float cx, float cy, float h, float ring, SDL_Color edge,
                       SDL_Color face)
{
	const float sd = h * 0.7f;

	rrect_f(r, cx - sd / 2.0f, cy - sd / 2.0f, sd, sd, sd / 2.0f, edge);
	rrect_f(r, cx - sd / 2.0f + ring, cy - sd / 2.0f + ring, sd - ring * 2.0f, sd - ring * 2.0f,
	        sd / 2.0f - ring, face);
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

/* A glyph's word or mark `k` times its size, its capitals' middle on `cy`,
 * centered on cx, at `a` of its opacity. */
static void label_k(SDL_Renderer *r, TTF_Font *f, ds_key g, float cx, float cy,
                    float k, SDL_Color col, float a)
{
	const char *s = g.text;
	struct text_entry *e;
	float top;

	if (a <= 0.004f) return;
	if (g.mark != DS_KEY_MARK_NONE) {
		col.a = (Uint8)(col.a * glyph_op * (a > 1.0f ? 1.0f : a) + 0.5f);
		if (g.mark == DS_KEY_MARK_DOTS) menu_key_dots(r, f, cx, cy, k, col);
		else key_mark(r, f, cx, cy, k, g.mark, col);
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
	const ds_key w = { DS_KEY_PILL, DS_KEY_MARK_NONE, s };

	label_k(r, f, w, cx, cy, k, ui_col(t->text, 255), 1.0f - q);
	label_k(r, f, w, cx, cy, k, ui_col(t->on_accent, 255), q);
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
	const ds_key g = ds_key_of(label);
	bool round = key_round(g);
	TTF_Font *f;
	float k, w, h, ring;
	char part[CHORD_MAX][CHORD_NAME], sign[CHORD_MAX];
	int parts;

	if (!label || size <= 0 || op <= 0.0f) return;
	if ((parts = chord_split(label, part, sign)) > 1) {
		chord_draw(r, part, sign, parts, cx, cy, size, style, true, press, scale, op);
		return;
	}
	if (g.shape == DS_KEY_ROCKER) {
		ui_rocker(r, cx, cy, size, press, press, scale, op);
		return;
	}
	if (press <= 0.0f && (scale == 1.0f || scale <= 0.0f) && op >= 1.0f) {
		ui_button(r, label, cx, cy, size, style);
		return;
	}
	glyph_op = op01(op);
	f = key_letters(g, round, size);
	k = glyph_k(style == UI_BUTTON_ON_ACCENT ? 0.0f : press, scale);
	if (key_dpad(g)) {
		/* the d-pad's key, faded, pressed or pulsing as any glyph is */
		const float ds = (float)size * DPAD_KEY_K * k;

		if (style == UI_BUTTON_ON_ACCENT) {
			unsigned m = ui_mix(ui_rgb(t->accent), ui_rgb(t->on_accent), BUTTON_ON_ACCENT_TINT);
			unsigned a = ui_mix(m, ui_rgb(t->on_accent), 0.25f);
			ds_rgb face = { (Uint8)(m >> 16), (Uint8)(m >> 8), (Uint8)m };
			ds_rgb arm = { (Uint8)(a >> 16), (Uint8)(a >> 8), (Uint8)a };

			dpad_key(r, (float)cx, (float)cy, ds, key_dpad(g), gcol(t->on_accent), gcol(face), gcol(arm),
			         gcol(t->on_accent));
		} else {
			unsigned a = ui_mix(ui_rgb(t->raised), ui_rgb(t->text_dim), 0.35f);
			ds_rgb arm = { (Uint8)(a >> 16), (Uint8)(a >> 8), (Uint8)a };

			dpad_key(r, (float)cx, (float)cy, ds, key_dpad(g), col_toward(t->edge, t->accent, press),
			         gcol(t->raised), gcol(arm), gcol(t->text));
		}
		glyph_op = 1.0f;
		return;
	}
	w = (float)(round ? size : ui_text_width(f, g.text) + button_pad(size) * 2) * k;
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
		if (g.shape == DS_KEY_STICK) stick_ring(r, (float)cx, (float)cy, h, ring, gcol(t->on_accent), gcol(fill));
		label_k(r, f, g, (float)cx, (float)cy, k, ui_col(t->on_accent, 255), 1.0f);
	} else {
		halo_draw(r, round ? HALO_FACE : HALO_PILL, cx - w / 2.0f, cy - h / 2.0f, w, h, press);
		rrect_f(r, cx - w / 2.0f, cy - h / 2.0f, w, h, h / 2.0f,
		        col_toward(t->edge, t->accent, press));
		rrect_f(r, cx - w / 2.0f + ring, cy - h / 2.0f + ring, w - ring * 2.0f,
		        h - ring * 2.0f, h / 2.0f - ring, gcol(t->raised));
		if (g.shape == DS_KEY_STICK)
			stick_ring(r, (float)cx, (float)cy, h, ring, col_toward(t->edge, t->accent, press), gcol(t->raised));
		label_k(r, f, g, (float)cx, (float)cy, k, ui_col(t->text, 255), 1.0f);
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

/* The d-pad as a key's hint: the tutorial's d-pad below (the lab's cross, as
 * the handheld's in src/tourdevice.c is), `size` across - DPAD_KEY_K of a
 * button glyph's, as a cross has less ink than a face - its ring in `edge`
 * and its face in `face`; the arms it means a step darker (`arm`), as the
 * handheld lights a pressed arm, each with its arrow in `ink`, as a face's
 * letter is; the others plain; and the edge's dot at its middle. */
static void dpad_key(SDL_Renderer *r, float cx, float cy, float size, int arms, SDL_Color edge,
                     SDL_Color face, SDL_Color arm, SDL_Color ink)
{
	static const float A[UI_DPAD_ARMS][4] = {
		{ 44, 4, 32, 36 }, { 44, 80, 32, 36 }, { 4, 44, 36, 32 }, { 80, 44, 36, 32 } };
	static const float P[UI_DPAD_ARMS][6] = {
		{ 60, 13, 50, 27, 70, 27 }, { 60, 107, 70, 93, 50, 93 },
		{ 13, 60, 27, 70, 27, 50 }, { 107, 60, 93, 50, 93, 70 } };
	const float k = size / DPAD_U;
	int i, j;

	dpad_rect(r, cx, cy, k, 40, 0, 40, 120, 10, edge);
	dpad_rect(r, cx, cy, k, 0, 40, 120, 40, 10, edge);
	dpad_rect(r, cx, cy, k, 44, 4, 32, 112, 7, face);
	dpad_rect(r, cx, cy, k, 4, 44, 112, 32, 7, face);
	for (i = 0; i < UI_DPAD_ARMS; i++) {
		SDL_FPoint q[3];

		/* up and down are arms 0 and 1, left and right 2 and 3 */
		if (!(arms & (i < 2 ? 2 : 1))) continue;
		dpad_rect(r, cx, cy, k, A[i][0], A[i][1], A[i][2], A[i][3], 7, arm);
		for (j = 0; j < 3; j++)
			q[j] = (SDL_FPoint){ cx + (P[i][j * 2] - DPAD_U / 2) * k, cy + (P[i][j * 2 + 1] - DPAD_U / 2) * k };
		fan(r, q, 3, ink);
	}
	disc_f(r, cx, cy, 7 * k, edge);
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

#define CARD_W CA_CARD_W   /* src/cardart.h: the card's width, its name's places are in */
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

/* ---- the faces a card is drawn in
 *
 * One recipe, two sets of faces: `k` NULL is the frame thread's own - the
 * card role of its sets, which face_for would answer with, and f_mark - and
 * `k` a card_faces is a set of copies, for a worker (away_faces). The same
 * files at the same sizes, so the same pixels; only whose they are differs,
 * since a face may be used by one thread at a time. */

/* UI_F_CARD's face in set `set`, opened the first time it is wanted: on the
 * frame, set_open's, which opens the set's every role as face_for always
 * did; away, that one face, from the file set_open would have read. */
static TTF_Font *card_title_face(card_faces *k, int set)
{
	char path[512];

	if (!k) {
		set_open(set);
		return font_sets[set][UI_F_CARD];
	}
	if (!k->title_tried[set]) {
		k->title_tried[set] = true;
		if (set == SET_LATIN) snprintf(path, sizeof path, "%s", font_path_kept);
		else set_path(set, false, path, sizeof path);
		k->title[set] = path[0] ? ui_face_open(path, role_pt(UI_F_CARD)) : NULL;
	}
	return k->title[set];
}

/* The watermark's face. The frame's is tried again at every card that would
 * show one, as it always was; a worker's once, after which its cards are the
 * frame's to make (ui_card_paint). */
static TTF_Font *card_mark_face(card_faces *k)
{
	if (!k) {
		if (!f_mark && font_path_kept[0])
			f_mark = ui_face_open(font_path_kept, WATERMARK_PT);
		return f_mark;
	}
	if (!k->mark_tried) {
		k->mark_tried = true;
		k->mark = font_path_kept[0] ? ui_face_open(font_path_kept, WATERMARK_PT) : NULL;
	}
	return k->mark;
}

static void card_faces_close(card_faces *k)
{
	for (int i = 0; i < SETS; i++) {
		ui_face_close(k->title[i]);
		k->title[i] = NULL;
		k->title_tried[i] = false;
	}
	ui_face_close(k->mark);
	k->mark = NULL;
	k->mark_tried = false;
	for (int i = 0; i < SETS; i++)
		for (int j = 0; j < NAME_FACES; j++) {
			ui_face_close(k->name[i][j]);
			k->name[i][j] = NULL;
			k->name_tried[i][j] = false;
		}
	if (k->art) SDL_FreeSurface(k->art);
	k->art = NULL;
	k->art_path[0] = '\0';
}

/* face_for for a line of a card, `f` its Latin face: the same choice, made
 * among `k`'s faces, with the language's set the order carried rather than
 * the one on now. */
static TTF_Font *card_face_for(card_faces *k, int lang_set, TTF_Font *f,
                               const char *s)
{
	int order[3];

	if (!has_cjk(s) || covers(f, s)) return f;
	order[0] = lang_set;
	order[1] = has_kana(s) || !strcmp(s, lang_name(LANG_JA)) ? SET_JP : SET_SC;
	order[2] = order[1] == SET_JP ? SET_SC : SET_JP;
	for (int n = 0; n < 3; n++) {
		TTF_Font *g;

		if (order[n] == SET_LATIN) continue;
		g = card_title_face(k, order[n]);
		if (g && covers(g, s)) return g;
	}
	return f;
}

/* Where a card's lines are set, and in which faces */
typedef struct {
	card_faces *k;
	int         lang_set;
	TTF_Font   *f;        /* the Latin face: the em and the capitals' place */
} card_pen;

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
static void blit_line(SDL_Surface *dst, const card_pen *pen, const char *line,
                      int *y, int x, SDL_Color col)
{
	const int em = role_pt(UI_F_CARD);
	SDL_Surface *t = render_in(card_face_for(pen->k, pen->lang_set, pen->f, line),
	                           line, col);
	if (!t) return;
	SDL_BlitSurface(t, NULL, dst,
	                &(SDL_Rect){ x < 0 ? (dst->w - t->w) / 2 : x,
	                             ui_cap_y(pen->f, *y + em * 2 / 5), 0, 0 });
	*y += em + 6;
	SDL_FreeSurface(t);
}

/* Break a title across lines that fit the card. `x` is where each line starts,
 * or -1 to center them. The line is built by appending in place and undoing the
 * append when it no longer fits, so there is no second buffer that could
 * truncate the first. Returns the y below the last line, which is where a rule
 * under the title goes. */
static int draw_wrapped(SDL_Surface *dst, const card_pen *pen, const char *title,
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
			if (len) { blit_line(dst, pen, line, &y, x, col); lines++; }
			snprintf(line, sizeof line, "%s", word);
			len = n;
			continue;
		}
		if (len) line[len++] = ' ';
		memcpy(line + len, word, n);
		len += n;
		line[len] = '\0';

		w = width_in(card_face_for(pen->k, pen->lang_set, pen->f, line), line);
		if (w > box_w && keep) {
			line[keep] = '\0';                /* undo the append */
			blit_line(dst, pen, line, &y, x, col);
			lines++;
			snprintf(line, sizeof line, "%s", word);
			len = n;
		}
	}
	if (line[0] && lines < 4) blit_line(dst, pen, line, &y, x, col);
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
static void draw_watermark(SDL_Surface *dst, card_faces *k, const char *title,
                           unsigned rgb)
{
	char ch[2] = { 0, 0 };
	const char *p = title;
	TTF_Font *mark;
	SDL_Surface *t;

	while (*p && (unsigned char)*p <= ' ') p++;
	if (!*p) return;
	ch[0] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
	if ((unsigned char)ch[0] > 127) return;   /* one glyph, and an ASCII one */

	if (!(mark = card_mark_face(k))) return;

	t = TTF_RenderUTF8_Blended(mark, ch, (SDL_Color){
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
		dst->h - WATERMARK_PT / 20 - TTF_FontAscent(mark), 0, 0 });
	SDL_FreeSurface(t);
}

/* ---- a folder's card (src/shelfdir.h, src/folderart.h) ----
 *
 * The folder itself, as pixel art in its system's color, a cell four units
 * wide, papers standing out of it, with how many games it holds on a paper
 * label and its name on its front. No slab: a folder on the shelf is a
 * folder, not a card with a folder on it (2026-11-13). */
/* A system with no color of its own is drawn in its marks' **text-dim** */
static unsigned folder_rgb(const ui_card_order *o)
{
	return o->rgb == CFG_NO_COLOR ? ui_rgb(theme_at(o->theme_ix)->text_dim) : o->rgb;
}

static SDL_Surface *folder_card_surface(card_faces *k, const ui_card_order *o)
{
	int gh = FA_CARD_H;
	SDL_Surface *grid = SDL_CreateRGBSurfaceWithFormat(0, FA_CARD_W, gh, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, FA_CARD_W * FA_CARD_SCALE, gh * FA_CARD_SCALE,
	                                                32, SDL_PIXELFORMAT_ARGB8888);
	card_pen pen = { k, o->lang_set, NULL };
	unsigned rgb = folder_rgb(o), ink;
	SDL_Rect nb;

	if (!grid || !s) {
		if (grid) SDL_FreeSurface(grid);
		if (s) SDL_FreeSurface(s);
		return NULL;
	}
	SDL_FillRect(s, NULL, 0);
	fa_paint(grid, rgb, o->folder, true);
	SDL_SetSurfaceBlendMode(grid, SDL_BLENDMODE_NONE);
	/* Nearest, whole cells: SDL_BlitScaled is nearest */
	SDL_BlitScaled(grid, NULL, s, NULL);
	SDL_FreeSurface(grid);
	if ((pen.f = card_title_face(k, SET_LATIN))) {
		fa_name_box(rgb, &nb, &ink);
		draw_wrapped(s, &pen, o->title, nb.w * FA_CARD_SCALE, nb.y * FA_CARD_SCALE,
		             nb.x * FA_CARD_SCALE, ui_col((ds_rgb){ (Uint8)(ink >> 16), (Uint8)(ink >> 8),
		                                                   (Uint8)ink }, 255));
	}
	return s;
}

SDL_Texture *ui_make_folder_icon(SDL_Renderer *r, unsigned rgb, int *w, int *h)
{
	SDL_Surface *grid = SDL_CreateRGBSurfaceWithFormat(0, FA_ICON_W, FA_ICON_H, 32,
	                                                   SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t = NULL;

	if (!grid) return NULL;
	if (rgb == CFG_NO_COLOR) rgb = ui_rgb(theme()->text_dim);
	fa_paint(grid, rgb, 0, false);
	if ((t = SDL_CreateTextureFromSurface(r, grid))) {
		SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
		*w = grid->w;
		*h = grid->h;
	}
	SDL_FreeSurface(grid);
	return t;
}

/* ---- a game's card (src/cardart.h, DESIGN.md 11.1) ----
 *
 * Its own box, the same in both themes: a frame of play when the game has
 * one, or else a ground of the system's era, and across it the name, set the
 * same way on every card so a shelf, the Grid and the list are read alike:
 * the title face, hard left at CA_NAME_X, the first line's capitals at
 * CA_NAME_TOP% of the card, at cardart_name's one size, near-white on a dark
 * band, and the system's rule under it. */
#define NAME_INK  ((SDL_Color){ 0xF6, 0xF5, 0xF1, 255 })   /* tools/cardart-check.c's NAME_INK */
#define NAME_INK2 ((SDL_Color){ 0xE2, 0xE0, 0xDA, 255 })   /* the part after " - " */

/* A name size (one of CA_SIZE_STEPS, or 6/10 of one) as its place among a
 * card_faces' name faces */
static int name_ix(int size)
{
	for (int i = 0; i < CA_SIZES; i++) {
		if (size == CA_SIZE_STEPS[i]) return i;
		if (size == CA_SIZE_STEPS[i] * 6 / 10) return CA_SIZES + i;
	}
	return 0;
}

/* The title face of set `set` at `size`, opened the first time it is wanted */
static TTF_Font *name_face(card_faces *k, int set, int size)
{
	int i = name_ix(size);
	char path[512];

	if (!k->name_tried[set][i]) {
		k->name_tried[set][i] = true;
		if (set != SET_LATIN) set_path(set, true, path, sizeof path);
		else snprintf(path, sizeof path, "%s", title_path_kept[0] ? title_path_kept : font_path_kept);
		k->name[set][i] = path[0] ? ui_face_open(path, size) : NULL;
	}
	return k->name[set][i];
}

/* card_face_for, among the name's faces */
static TTF_Font *name_face_for(card_faces *k, int lang_set, int size, const char *s)
{
	TTF_Font *f = name_face(k, SET_LATIN, size);
	int order[3];

	if (!f || !has_cjk(s) || covers(f, s)) return f;
	order[0] = lang_set;
	order[1] = has_kana(s) || !strcmp(s, lang_name(LANG_JA)) ? SET_JP : SET_SC;
	order[2] = order[1] == SET_JP ? SET_SC : SET_JP;
	for (int n = 0; n < 3; n++) {
		TTF_Font *g;

		if (order[n] == SET_LATIN) continue;
		g = name_face(k, order[n], size);
		if (g && covers(g, s)) return g;
	}
	return f;
}

typedef struct { card_faces *k; int lang_set; } name_pen;

static int name_width(void *ctx, int size, const char *s)
{
	name_pen *p = ctx;
	TTF_Font *f = name_face_for(p->k, p->lang_set, size, s);
	int w = 0;

	if (f) TTF_SizeUTF8(f, s, &w, NULL);
	return w;
}

/* The Latin face's capital height at `size` */
static int name_cap(card_faces *k, int size)
{
	TTF_Font *f = name_face(k, SET_LATIN, size);
	int minx, maxx, miny, maxy, adv;

	if (!f || TTF_GlyphMetrics(f, 'H', &minx, &maxx, &miny, &maxy, &adv)) return size * 7 / 10;
	return maxy;
}

/* One line, its capitals' top at `cap_top`, over a soft shadow 3 below */
static void name_line(SDL_Surface *s, card_faces *k, int lang_set, int size, const char *line,
                      int cap_top, SDL_Color col)
{
	TTF_Font *f = name_face_for(k, lang_set, size, line), *latin = name_face(k, SET_LATIN, size);
	SDL_Surface *t;
	int top;

	if (!f || !latin) return;
	top = cap_top - (TTF_FontAscent(latin) - name_cap(k, size));
	/* rendered once, in white, and put down twice through its color: the
	 * shadow black at 110, then the line in its ink */
	if ((t = render_in(f, line, (SDL_Color){ 255, 255, 255, 255 }))) {
		SDL_SetSurfaceBlendMode(t, SDL_BLENDMODE_BLEND);
		SDL_SetSurfaceColorMod(t, 0, 0, 0);
		SDL_SetSurfaceAlphaMod(t, 110);
		SDL_BlitSurface(t, NULL, s, &(SDL_Rect){ CA_NAME_X, top + 3, 0, 0 });
		SDL_SetSurfaceColorMod(t, col.r, col.g, col.b);
		SDL_SetSurfaceAlphaMod(t, col.a);
		SDL_BlitSurface(t, NULL, s, &(SDL_Rect){ CA_NAME_X, top, 0, 0 });
		SDL_FreeSurface(t);
	}
}

/* The name on its band, and the system's rule under it */
static void card_name(SDL_Surface *s, card_faces *k, const ui_card_order *o)
{
	name_pen pen = { k, o->lang_set };
	unsigned rule = ui_sys_mark_in(o->rgb, DS_DARK);
	int y0 = s->h * CA_NAME_TOP / 100, y, cap, capg = 0, foot;
	ca_name n;

	if (!cardart_name(o->title, s->w - 2 * CA_NAME_X, CA_LINES, name_width, &pen, &n)) return;
	cap = name_cap(k, n.size);
	if (n.sub_n) capg = name_cap(k, n.sub_size);
	/* the foot of the block: its last capitals, then the rule 18 under */
	foot = y0 + n.n * cap + (n.n - 1) * (cap * 6 / 10);
	if (n.sub_n) foot += cap / 2 + n.sub_n * capg + (n.sub_n - 1) * (capg * 6 / 10);
	cardart_paint_band(s, y0 - 30, foot + 18 + 4 + 30, 22);
	y = y0;
	for (int i = 0; i < n.n; i++, y += cap + cap * 6 / 10)
		name_line(s, k, o->lang_set, n.size, n.line[i], y, NAME_INK);
	y -= cap * 6 / 10;
	if (n.sub_n) {
		y += cap / 2;
		for (int i = 0; i < n.sub_n; i++, y += capg + capg * 6 / 10)
			name_line(s, k, o->lang_set, n.sub_size, n.sub[i], y, NAME_INK2);
		y -= capg * 6 / 10;
	}
	SDL_FillRect(s, &(SDL_Rect){ CA_NAME_X, y + 18, 72, 4 },
	             SDL_MapRGBA(s->format, (Uint8)(rule >> 16), (Uint8)(rule >> 8), (Uint8)rule, 255));
}

/* The system's still art at `path`, kept for the next card: a shelf asks
 * for one system's cards together */
static SDL_Surface *card_art(card_faces *k, const char *path)
{
	SDL_Surface *raw;

	if (!path || !path[0]) return NULL;
	if (k->art && !strcmp(k->art_path, path)) return k->art;
	if (k->art) SDL_FreeSurface(k->art);
	k->art = NULL;
	snprintf(k->art_path, sizeof k->art_path, "%s", path);
	if ((raw = IMG_Load(path))) {
		k->art = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
		SDL_FreeSurface(raw);
	}
	return k->art;
}

/* The ground a game's card keeps for the Grid, a quarter of its size, with
 * no name: on the card's userdata, marked as one by its own userdata, until
 * ui_card_ground_take takes it (or ui_card_free frees it with the card) */
static char ground_mark;

SDL_Surface *ui_card_ground_take(SDL_Surface *card)
{
	SDL_Surface *g = card ? card->userdata : NULL;

	if (!g || g->userdata != &ground_mark) return NULL;
	card->userdata = NULL;
	g->userdata = NULL;
	return g;
}

void ui_card_free(SDL_Surface *s)
{
	SDL_Surface *g = ui_card_ground_take(s);

	if (g) SDL_FreeSurface(g);
	if (s) SDL_FreeSurface(s);
}

/* `from`: the ground as painted, the card's own or its half-size paint */
static void keep_ground(SDL_Surface *s, SDL_Surface *from)
{
	SDL_Surface *g = SDL_CreateRGBSurfaceWithFormat(0, s->w / 4, s->h / 4, 32, SDL_PIXELFORMAT_ARGB8888);

	if (!g) return;
	SDL_SetSurfaceBlendMode(from, SDL_BLENDMODE_NONE);
	if (SDL_SoftStretchLinear(from, NULL, g, NULL)) {
		SDL_FreeSurface(g);
		return;
	}
	g->userdata = &ground_mark;
	s->userdata = g;
}

/* What a card costs to paint, said in the log at the 1st, 2nd, 4th... 64th
 * card, and at the 1st, 2nd, 4th... 64th sharp one (src/focusfx.h): grounds,
 * blurred frames of play and sharp ones apart, mean and worst, in ms. From
 * any thread, so counted under its own lock. */
enum { COST_GROUND, COST_FRAME, COST_SHARP, COSTS };
static void card_cost(int kind, Uint64 t0)
{
	static SDL_SpinLock lock;
	static int n[COSTS];
	static double sum[COSTS], worst[COSTS];
	double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
	int all;

	SDL_AtomicLock(&lock);
	n[kind]++;
	sum[kind] += ms;
	if (ms > worst[kind]) worst[kind] = ms;
	all = n[COST_GROUND] + n[COST_FRAME];
	if ((kind != COST_SHARP && all <= 64 && !(all & (all - 1))) ||
	    (kind == COST_SHARP && n[kind] <= 64 && !(n[kind] & (n[kind] - 1))))
		fprintf(stderr, "cardart: %d cards painted: %d grounds %.1f ms (worst %.1f), %d frames of play %.1f ms "
		        "(worst %.1f); %d sharp %.1f ms (worst %.1f)\n",
		        all, n[0], n[0] ? sum[0] / n[0] : 0, worst[0], n[1], n[1] ? sum[1] / n[1] : 0, worst[1],
		        n[2], n[2] ? sum[2] / n[2] : 0, worst[2]);
	SDL_AtomicUnlock(&lock);
}

static SDL_Surface *game_art_card(card_faces *k, const ui_card_order *o)
{
	Uint64 t0 = SDL_GetPerformanceCounter();
	/* An album's is square, as its cover is (MP4's shelf, DESIGN.md 11.67):
	 * the same ground and name, the name's place by the card's height */
	const int ch = o->album ? CARD_W : CARD_H;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, CARD_W, ch, 32, SDL_PIXELFORMAT_ARGB8888);
	/* a system with no color: its marks' text-dim, of the dark theme the
	 * card always is */
	unsigned rgb = o->rgb == CFG_NO_COLOR ? ui_rgb(theme_at(DS_DARK)->text_dim) : o->rgb;
	bool played = false;

	if (!s) return NULL;
	for (int i = 0; i < UI_CARD_FRAMES && !played; i++) {
		SDL_Surface *f;

		if (!o->frame[i][0] || !(f = IMG_Load(o->frame[i]))) continue;
		played = cardart_paint_frame(s, f, rgb, o->sharp);
		SDL_FreeSurface(f);
	}
	if (!played) {
		/* painted at half the size and stretched: on the Pixel 2 a whole
		 * one took 134 ms on the worker, and shown at a third of its size
		 * nothing of it is lost */
		SDL_Surface *half = SDL_CreateRGBSurfaceWithFormat(0, CARD_W / 2, ch / 2, 32,
		                                                   SDL_PIXELFORMAT_ARGB8888);
		ca_ground g;

		cardart_ground(rgb, cardart_hash(o->title), &g);
		if (half) {
			cardart_paint_ground(half, rgb, cardart_texture(o->tag), &g, card_art(k, o->art));
			SDL_SetSurfaceBlendMode(half, SDL_BLENDMODE_NONE);
			SDL_BlitScaled(half, NULL, s, NULL);
			keep_ground(s, half);
			SDL_FreeSurface(half);
		} else {
			cardart_paint_ground(s, rgb, cardart_texture(o->tag), &g, card_art(k, o->art));
			keep_ground(s, s);
		}
	} else if (!o->sharp) {
		/* the Grid's ground; the sharp card is the cover row's alone */
		keep_ground(s, s);
	}
	card_name(s, k, o);
	round_corners(s, CARD_RADIUS);
	card_cost(!played ? COST_GROUND : o->sharp ? COST_SHARP : COST_FRAME, t0);
	return s;
}

/* An album's card with no cover, 512 square (a game's is game_art_card's
 * since 2026-11-15; this drew it at 512 x 656 before, and still can). Its
 * corners square: it stands among square sleeves.
 *
 * For the order's theme, DS_LIGHT or DS_DARK, and only from it: DESIGN.md
 * 11.1 draws a card for each theme, a light **surface** slab with a charcoal
 * title in Beard and a dark one with a light title in Salt & Pepper
 * (src/cardgen.h has both, and the check that the title reads on every row of
 * either). The order names the theme; nothing here reads theme(), so a
 * worker can draw it. In `k`'s faces (card_title_face). */
static SDL_Surface *game_card_surface(card_faces *k, const ui_card_order *o)
{
	const int cw = CARD_W, ch = o->album ? CARD_W : CARD_H;
	const int theme_ix = o->theme_ix;
	const char *title = o->title;
	unsigned rgb = o->rgb;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, cw, ch, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	const ds_theme *t = theme_at(theme_ix);
	/* The rule under the title is a mark of the system's color on the slab,
	 * so it is drawn as every such mark is (ui_sys_mark_in): a quarter of the
	 * way to **text** on Beard's light slab, where a yellow like Game Boy
	 * Color's would not read, and lime as **accent-ink**. The band across the
	 * top stays the system's own color, as the mock kit's card has it. */
	unsigned rule = ui_sys_mark_in(rgb, theme_ix);
	card_pen pen = { k, o->lang_set, NULL };
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

	draw_watermark(s, k, title, rgb);

	/* The system's color as a band rather than a wash: a generated card
	 * should read as "this system, no art" at a glance in the row. */
	SDL_FillRect(s, &(SDL_Rect){ 0, 0, cw, 6 },
	             SDL_MapRGBA(s->format, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8),
	                         (Uint8)rgb, 255));

	/* The title at the top and hard left, because that is the edge the eye
	 * runs down when the row is moving. A rule under it rather than across
	 * the card: it is the end of the name, not a divider between halves. */
	if ((pen.f = card_title_face(k, SET_LATIN))) {
		/* Not at the top edge. Started at 76 it left the bottom half of the
		 * card empty for the one-line titles that are most of a shelf, and the
		 * bleed does not fill it - the face darkens toward the foot and takes
		 * the letter's tail with it. Four lines still fit below this. */
		int y = draw_wrapped(s, &pen, title, cw - 96,
		                     (int)(ch * 0.38f), 48,
		                     ui_col(cardgen_title(theme_ix), 255));

		SDL_FillRect(s, &(SDL_Rect){ 48, y + 12, 90, 3 },
		             SDL_MapRGBA(s->format, (Uint8)(rule >> 16),
		                         (Uint8)(rule >> 8), (Uint8)rule, 220));
	}

	if (!o->album) round_corners(s, CARD_RADIUS);
	return s;
}

/* The card an order describes: a folder's (folder_card_surface) or a game's
 * or an album's */
static SDL_Surface *card_surface(card_faces *k, const ui_card_order *o)
{
	if (o->folder > 0) return folder_card_surface(k, o);
	/* a game's is its own (game_art_card); an album's too, square, when its
	 * order names its system (MP4's shelf, ui_card_order_game), and
	 * otherwise today's */
	return o->album && !o->tag[0] ? game_card_surface(k, o) : game_art_card(k ? k : &frame_faces, o);
}

void ui_card_order_set(ui_card_order *o, const char *title, unsigned rgb,
                       bool album)
{
	snprintf(o->title, sizeof o->title, "%s", title ? title : "");
	o->rgb = rgb;
	o->album = album;
	o->theme_ix = theme_index();
	o->lang_set = set_on;
	o->folder = 0;
	o->tag[0] = o->art[0] = '\0';
	for (int i = 0; i < UI_CARD_FRAMES; i++) o->frame[i][0] = '\0';
	o->sharp = false;
}

void ui_card_order_game(ui_card_order *o, const char *tag, const char *art,
                        const char *const frames[UI_CARD_FRAMES])
{
	snprintf(o->tag, sizeof o->tag, "%s", tag ? tag : "");
	snprintf(o->art, sizeof o->art, "%s", art ? art : "");
	for (int i = 0; i < UI_CARD_FRAMES; i++)
		snprintf(o->frame[i], sizeof o->frame[i], "%s", frames && frames[i] ? frames[i] : "");
}

void ui_card_order_folder(ui_card_order *o, const char *title, unsigned rgb,
                          int games)
{
	ui_card_order_set(o, title, rgb, false);
	o->folder = games > 0 ? games : 1;
}

SDL_Surface *ui_card_paint(const ui_card_order *o, bool away)
{
	SDL_Surface *s = NULL;
	card_faces *k;
	int i;

	if (!away) return card_surface(NULL, o);
	for (i = 0; i < AWAY_SETS; i++)
		if (!away_lock[i]) return NULL;
	/* A set no one is drawing in, or, with both in use (the frame's card
	 * beside two workers'), the first, when it is done */
	for (i = 0; i < AWAY_SETS && SDL_TryLockMutex(away_lock[i]) != 0; i++)
		;
	if (i == AWAY_SETS) SDL_LockMutex(away_lock[i = 0]);
	k = &away_faces[i];
	/* Both faces open before a pixel is drawn, or none of it: a card drawn
	 * here without a face the frame would have drawn it with would be
	 * another card, so a face that will not open leaves every card to the
	 * frame, which draws it as it always did, with or without. */
	if (away_up && card_title_face(k, SET_LATIN) && card_mark_face(k))
		s = card_surface(k, o);
	SDL_UnlockMutex(away_lock[i]);
	return s;
}

/* Which theme a card's texture was made for, kept on the texture itself
 * (SDL's user data, SDL 2.0.18, which SDL_RenderGeometry already needs): a
 * pointer to one of these, by index, and any other pointer is not a card. On
 * the texture and not in a table beside it, because a card's texture goes
 * into slots that are allocated, reordered, moved and freed in a dozen places
 * in src/main.c, and a mark that lives with the texture cannot be left behind
 * by any of them. */
/* And by the look it was made in (src/look.h's look_id): a card made in
 * another look is made again, in its new colors, as one of the other theme
 * is. By the look itself and not a count of changes, so a list's cursor
 * going through accents (src/ds/hover.h) and back leaves the kept look's
 * cards current and no other: a count kept in four could come round to a
 * shown look's cards. */
#define CARD_LOOKS LOOK_IDS
static char card_made_for[DS_THEMES][CARD_LOOKS];

SDL_Texture *ui_card_upload(SDL_Renderer *r, SDL_Surface *s, int theme_ix)
{
	SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);

	if (!t) return NULL;
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureUserData(t, &card_made_for[theme_clamp(theme_ix)][look_id()]);
	return t;
}

/* On the frame, so for the theme that is on. In the workers' faces when they
 * can be had - the same pixels - and the frame's own when not: a card made
 * here is Now Playing's, a port's loading screen's, a shot's or one a worker
 * could not draw, and a second 560-point face with its glyphs cached, kept
 * for those alone, would be megabytes for a card or two. */
static SDL_Texture *make_card(SDL_Renderer *r, const char *title, unsigned rgb,
                              bool album, int *w, int *h)
{
	ui_card_order o;
	SDL_Surface *s;
	SDL_Texture *t;

	ui_card_order_set(&o, title, rgb, album);
	if (!(s = ui_card_paint(&o, true)) && !(s = card_surface(NULL, &o)))
		return NULL;
	t = ui_card_upload(r, s, o.theme_ix);
	if (t) { *w = s->w; *h = s->h; }
	ui_card_free(s);
	return t;
}

int ui_card_theme(SDL_Texture *t)
{
	uintptr_t p = (uintptr_t)(t ? SDL_GetTextureUserData(t) : NULL);
	uintptr_t base = (uintptr_t)&card_made_for[0][0];
	int i, g;

	/* One of the marks, by its place among them (a table of every look
	 * since Folder Color, too long to walk for each card a frame) */
	if (p < base || p >= base + sizeof card_made_for) return -1;
	i = (int)((p - base) / CARD_LOOKS);
	g = (int)((p - base) % CARD_LOOKS);
	/* A card of an older look reads as made for the other theme, which is
	 * what makes it stale (tex_stale) and made again */
	return g == look_id() ? i : (i == DS_DARK ? DS_LIGHT : DS_DARK);
}

SDL_Texture *ui_make_card_for(SDL_Renderer *r, const ui_card_order *o, int *w, int *h,
                              SDL_Surface **ground)
{
	SDL_Surface *s;
	SDL_Texture *t;

	if (ground) *ground = NULL;
	if (!(s = ui_card_paint(o, true)) && !(s = card_surface(NULL, o)))
		return NULL;
	t = ui_card_upload(r, s, o->theme_ix);
	if (t) { *w = s->w; *h = s->h; }
	if (ground) *ground = ui_card_ground_take(s);
	ui_card_free(s);
	return t;
}

SDL_Texture *ui_make_card(SDL_Renderer *r, const char *title, unsigned rgb,
                          int *w, int *h)
{
	return make_card(r, title, rgb, false, w, h);
}

SDL_Texture *ui_make_cover(SDL_Renderer *r, const char *title, unsigned rgb,
                           int *w, int *h)
{
	return make_card(r, title, rgb, true, w, h);
}

SDL_Texture *ui_make_folder_card(SDL_Renderer *r, const char *title, unsigned rgb,
                                 int games, int *w, int *h)
{
	ui_card_order o;
	SDL_Surface *s;
	SDL_Texture *t;

	ui_card_order_folder(&o, title, rgb, games);
	if (!(s = ui_card_paint(&o, true)) && !(s = card_surface(NULL, &o)))
		return NULL;
	t = ui_card_upload(r, s, o.theme_ix);
	if (t) { *w = s->w; *h = s->h; }
	SDL_FreeSurface(s);
	return t;
}
