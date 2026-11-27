/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's Player Looks on Now Playing: see nowplaying_looks.h, and
 * DESIGN.md 11.64 for what each is and why.
 *
 * Every look is the same frame - the top bar, the bar of the song's time
 * with the time played and the time left under its two ends, the keys at
 * the foot - and its own picture in the room between. The frame is laid out
 * from the foot up (src/ds/flex.h), so the six languages whose keys take two
 * rows give their look 48 units less and nothing meets. Every edge of the
 * frame and of each look is on the 8-unit grid, which is whole pixels on
 * the Pixel 2's 0.625 as on the Brick's 1: a bar 16 wide is 10 pixels there
 * and 8 between is 5. What moves - a bar's top, a cap - moves smoothly
 * between them.
 *
 * Drawn every frame while music plays (the bars are the sound), and not at
 * all once it is paused and every bar has come to rest: the run loop asks
 * npl_draw when next, and it says 0 only while something moves. */
#include "nowplaying_looks.h"

#include "app.h"
#include "cassette.h"
#include "config.h"
#include "covercolor.h"
#include "db.h"
#include "design.h"
#include "ds/flex.h"
#include "ds/glyph.h"
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/panel.h"
#include "home.h"
#include "i18n.h"
#include "keyhints_draw.h"
#include "mp4lib.h"
#include "nowplaying_screen.h"
#include "panel.h"
#include "platform.h"
#include "shots.h"
#include "theme.h"
#include "ui.h"
#include "vinyl.h"

#include <SDL_image.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- the setting ------------------------------------------------------------ */

static int g_look = -1;                  /* -1: not read yet */
static int g_look_shot = MP4L_COUNT;     /* --np-look's, never saved */

mp4l_id np_look(void)
{
	char k[16];

	if (g_look_shot < MP4L_COUNT) return (mp4l_id)g_look_shot;
	if (g_look < 0) {
		db_get_str(db_dev(), "mp4.look", k, sizeof k, mp4l_key(MP4L_COVER));
		g_look = mp4l_from_key(k);
	}
	return (mp4l_id)g_look;
}

void np_look_set(mp4l_id id)
{
	if (id < 0 || id >= MP4L_COUNT) return;
	g_look = id;
	db_set_str(db_dev(), "mp4.look", mp4l_key(id));
}

void np_look_shot(mp4l_id id) { g_look_shot = id; }

mp4l_id np_look_for(void)
{
	/* A book has its chapters and speed, a video its picture: Cover's
	 * screen, or the video player, says them; a look is for a song */
	if (mp4c_is_book() || mp4c_is_videos() || mp4c_video()) return MP4L_COVER;
	return np_look();
}

/* ---- the motion --------------------------------------------------------------- */

/* The bench's own clock (npl_bench), 0 for the real one */
static unsigned g_clock;

static unsigned clock_ms(void)
{
	return g_clock ? g_clock : plat_now_ms();
}

/* Levels older than this are not the sound any more: the daemon sends 30 a
 * second while it plays, and a gap this long is a pause or a stall */
#define BANDS_STALE 150

static struct {
	int  look;               /* the bars are this look's; -1 none yet */
	mp4l_bars_t bars;
	unsigned at;             /* the last step (plat_now_ms); 0 none */
	bool still;              /* under the lock */
	bool shot;               /* a shot's bars, never stepped */
} M = { .look = -1 };

/* The bars in show, for the list's picture with nothing playing: a beat
 * at 128 a minute in the low bands, the rest wandering, so the picture
 * moves the way a song would make it. Not the sound: no sound plays. */
static void show_bands(uint8_t *lv, unsigned t)
{
	float beat = fmodf((float)t / 469.0f, 1.0f), kick = expf(-beat * 7.0f);
	int i;

	for (i = 0; i < MP4L_BANDS; i++) {
		float x = (float)i / (MP4L_BANDS - 1);
		float v = 0.62f - 0.30f * x + kick * 0.34f * (1.0f - x) +
		          0.16f * sinf((float)t * 0.0023f + (float)i * 0.71f) +
		          0.11f * sinf((float)t * 0.0061f + (float)i * 1.93f);

		lv[i] = (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255.0f));
	}
}

/* One frame's step: `bands` the sound's levels, the show's, or NULL to fall */
static void motion_step(mp4l_id look, const uint8_t *bands)
{
	unsigned now = clock_ms();
	float dt = M.at ? (float)(now - M.at) : 0.0f;

	if ((int)look != M.look) {
		mp4l_bars_reset(&M.bars, mp4l_bars(look));
		M.look = look;
		M.shot = false;
		dt = 0;
	}
	M.at = now;
	if (M.shot) return;
	/* A long gap - a menu over it, the lock - starts from rest rather than
	 * from bars that were left in the air a second ago; under the lock they
	 * are at rest from its first frame */
	if (dt > 250.0f || M.still) {
		mp4l_bars_reset(&M.bars, M.bars.n);
		dt = 0;
	}
	mp4l_bars_step(&M.bars, dt, bands);
}

/* The sound's levels while playing and fresh, keeping the daemon measuring;
 * NULL otherwise */
static const uint8_t *sound_bands(bool playing)
{
	static uint8_t lv[MP4C_BANDS];
	unsigned age;

	if (!playing || M.still || !M.bars.n) return NULL;
	mp4c_bands_keep();
	return mp4c_bands(lv, &age) && age < BANDS_STALE ? lv : NULL;
}

void npl_still(bool still)
{
	M.still = still;
	/* Let go: from rest, not from the frame the lock froze */
	if (!still) M.at = 0;
}

static void shot_bars(mp4l_id look)
{
	int i;

	mp4l_bars_reset(&M.bars, mp4l_bars(look));
	M.look = look;
	M.shot = true;
	/* A song's shape, held: the bass high, the rest falling off with a
	 * rise in the middle, each cap a little over its bar or come down onto
	 * it, as they are a moment after a beat */
	for (i = 0; i < M.bars.n; i++) {
		float x = (float)i / (float)(M.bars.n > 1 ? M.bars.n - 1 : 1);
		float v = 0.78f - 0.42f * x + 0.16f * sinf(x * 9.0f + 0.6f) + 0.06f * sinf(x * 31.0f);

		M.bars.lv[i] = v < 0.04f ? 0.04f : v > 0.96f ? 0.96f : v;
		M.bars.cap[i] = M.bars.lv[i] + (i % 3 == 0 ? 0.0f : 0.05f + 0.07f * (float)(i % 5) / 4.0f);
		if (M.bars.cap[i] > 1.0f) M.bars.cap[i] = 1.0f;
	}
}

void npl_shot_bars(void) { shot_bars(np_look_for()); }

/* ---- the frame ------------------------------------------------------------------- */

typedef struct {
	SDL_Rect content;        /* the look's own room */
	SDL_Rect bar;            /* the song's time */
	SDL_Rect times;          /* the row under it: the time played, the time left */
	int keys_cy;             /* the keys' last row's middle */
} npl_frame;

/* From the foot up: the keys, as many rows as their words take, their box
 * rounded up to the grid; a section over them the times, the bar a related
 * step over those; a section over that the look's room, up to a section
 * under the header. */
static void frame_layout(npl_frame *F, const key_hint *h, int nh)
{
	static ds_flex L;
	int kh = keyhints_height(h, nh, ZYPEROS_SCREEN_W - 2 * DS_MARGIN);
	int root, content, prog, bar, times, keys;

	kh = (kh + DS_GRID - 1) / DS_GRID * DS_GRID;
	root = ds_flex_begin(&L, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
	                     &(ds_box){ .dir = DS_COL, .gap = DS_SECTION,
	                                .pad = { DS_MARGIN, DS_HEADER + DS_SECTION, DS_MARGIN,
	                                         DS_FOOT_INSET } });
	content = ds_flex_add(&L, root, &(ds_box){ .grow = 1 });
	prog = ds_flex_add(&L, root, &(ds_box){ .dir = DS_COL, .gap = DS_RELATED });
	bar = ds_flex_add(&L, prog, &(ds_box){ .h = UI_BAR_H });
	times = ds_flex_add(&L, prog, &(ds_box){ .h = DS_LINE_META });
	keys = ds_flex_add(&L, root, &(ds_box){ .h = kh });
	ds_flex_solve(&L);
	F->content = ds_flex_rect(&L, content);
	F->bar = ds_flex_rect(&L, bar);
	F->times = ds_flex_rect(&L, times);
	F->keys_cy = ds_flex_rect(&L, keys).y + ds_flex_rect(&L, keys).h - DS_HINT_GLYPH / 2;
}

/* The ground, the light along its foot, as Cover has them */
static void ground(SDL_Renderer *r)
{
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	ui_lift(r, &(SDL_Rect){ 0, ZYPEROS_SCREEN_H - 240, ZYPEROS_SCREEN_W, 480 },
	        ZYPEROS_ACCENT, UI_LIFT_WASH);
}

/* The song's time: the bar, its fill faded while it does not play; the time
 * played under its left end and the time left under its right, tabular; the
 * word for a pause or a stop between them, as on Cover, unless the look says
 * it itself (`said`: Classic's panel) */
static void frame_time(SDL_Renderer *r, const npl_frame *F, const mu_now *mn, bool said)
{
	TTF_Font *fs = ui_font(UI_F_META);
	SDL_Color acc = ui_col(theme()->accent_ink, 255);
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MP4_PAUSED) : T(S_MP4_STOPPED);
	char t0[16], t1[16], line[20];
	int ty = ui_line_y(UI_F_META, F->times.y), left, right;

	ds_bar_in(r, &F->bar, (int)(F->bar.w * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5),
	          ui_col(theme()->track, 255), state ? ui_fade(acc, 0.45f) : acc);
	mmss(t0, sizeof t0, mn->at);
	left = F->times.x + ui_text_tabular(r, fs, t0, F->times.x, ty, -1, UI_TEXT_SOFT);
	right = F->times.x + F->times.w;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, right, ty, 1, UI_TEXT_SOFT);
	}
	if (state && !said) ui_text(r, fs, state, (left + right) / 2, ty, 0, acc);
}

/* ---- the parts ---------------------------------------------------------------------- */

/* One line that slides when it is wider than `w`, on Now Playing's one
 * marquee clock; `top` is its line's top */
static void line(SDL_Renderer *r, ui_font_role role, const char *s, int x, int top, int w,
                 unsigned phase, SDL_Color col, unsigned *wait)
{
	TTF_Font *f = ui_font(role);
	unsigned wt = ui_pingpong_wait(ui_text_width(f, s) - w, phase);

	ui_text_marquee(r, f, s, x, ui_line_y(role, top), w, phase, col);
	if (wt < *wait) *wait = wt;
}

/* "3 / 12" and the play mode's mark after it, as Cover has them, on a meta
 * line whose top is `top`, from `x` (anchor -1) or to it (1); its width */
static int count_mark(SDL_Renderer *r, const mu_now *mn, int x, int top, int anchor)
{
	TTF_Font *fs = ui_font(UI_F_META);
	int th = ui_font_line(UI_F_META), gs = th * 3 / 2, g = MP4_MODES[mp4c_mode()].glyph;
	char s[32] = "";
	int tw = 0, w;

	if (mn->count > 1) {
		snprintf(s, sizeof s, "%d / %d", mn->index + 1, mn->count);
		tw = ui_text_width(fs, s);
	}
	w = tw + (g >= 0 ? (tw ? DS_LOOK_CAP * 4 : 0) + gs : 0);
	if (anchor > 0) x -= w;
	if (tw) ui_text(r, fs, s, x, ui_line_y(UI_F_META, top), -1, UI_TEXT_DIM);
	/* The mark half again the text's height, on its capitals' middle */
	if (g >= 0)
		ui_glyph_draw(r, (ui_glyph)g, x + w - gs / 2, top + th * 2 / 5, gs,
		              ui_col(theme()->accent_ink, 255));
	return w;
}

/* The bars from `x`, their feet on `foot`, `h` the most they reach with
 * their caps: accent-ink bars, text-dim caps (each a pill a cap's height,
 * a cap's height over its bar), and under the feet, `refl` tall, the bars
 * again in reflection, upside down and fading to nothing. */
static void bars(SDL_Renderer *r, const mp4l_bars_t *b, int x, int foot, int h, int refl)
{
	SDL_Color ink = ui_col(theme()->accent_ink, 255), cap = ui_col(theme()->text_dim, 255);
	int reach = h - 2 * DS_LOOK_CAP, i;

	for (i = 0; i < b->n; i++) {
		int bx = x + i * (DS_LOOK_BAR_W + DS_LOOK_BAR_GAP);
		int bh = (int)(b->lv[i] * (float)reach + 0.5f);
		int cy = foot - (int)(b->cap[i] * (float)reach + 0.5f) - 2 * DS_LOOK_CAP;

		/* Round at the top and flat on the foot, where the reflection
		 * goes on from it: the top's corners, then the rest square over
		 * the round rect's lower ones */
		if (bh > DS_RADIUS_BAR) {
			int th = bh < 2 * DS_RADIUS_BAR ? bh : 2 * DS_RADIUS_BAR;

			ui_round_rect(r, &(SDL_Rect){ bx, foot - bh, DS_LOOK_BAR_W, th }, DS_RADIUS_BAR, ink);
			SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, ink.a);
			SDL_RenderFillRect(r, &(SDL_Rect){ bx, foot - bh + DS_RADIUS_BAR, DS_LOOK_BAR_W,
			                                   bh - DS_RADIUS_BAR });
		} else if (bh > 0) {
			SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, ink.a);
			SDL_RenderFillRect(r, &(SDL_Rect){ bx, foot - bh, DS_LOOK_BAR_W, bh });
		}
		ui_round_rect(r, &(SDL_Rect){ bx, cy, DS_LOOK_BAR_W, DS_LOOK_CAP }, DS_LOOK_CAP / 2, cap);
		if (refl > 0 && bh > 0) {
			/* The bar's reflection: as tall a part of `refl` as the bar is
			 * of its reach, 22% at its foot and fading to nothing at
			 * `refl` (the lab's) */
			float k = b->lv[i], a0 = 0.22f * 255.0f, a1 = a0 * (1.0f - k);
			int rh = (int)(k * (float)refl + 0.5f);
			SDL_Vertex v[4] = {
				{ { (float)bx, (float)foot }, { ink.r, ink.g, ink.b, (Uint8)a0 }, { 0, 0 } },
				{ { (float)(bx + DS_LOOK_BAR_W), (float)foot }, { ink.r, ink.g, ink.b, (Uint8)a0 }, { 0, 0 } },
				{ { (float)(bx + DS_LOOK_BAR_W), (float)(foot + rh) }, { ink.r, ink.g, ink.b, (Uint8)a1 }, { 0, 0 } },
				{ { (float)bx, (float)(foot + rh) }, { ink.r, ink.g, ink.b, (Uint8)a1 }, { 0, 0 } },
			};
			static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

			SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
		}
	}
}

/* Box `id` of a solved tree, moved by (dx, dy): a look lays out its room
 * from (0, 0) */
static SDL_Rect at(const ds_flex *L, int id, int dx, int dy)
{
	SDL_Rect q = ds_flex_rect(L, id);

	q.x += dx;
	q.y += dy;
	return q;
}

/* How wide `n` bars are */
static int bars_w(int n)
{
	return n > 0 ? n * (DS_LOOK_BAR_W + DS_LOOK_BAR_GAP) - DS_LOOK_BAR_GAP : 0;
}

/* The song a shot or the list's show plays, with no queue to ask: its
 * album and its place there (npl_song) */
static int g_song_al = -1, g_song_place = -1;

void npl_song(int album, int place)
{
	g_song_al = album;
	g_song_place = place;
}

/* The album the song is from and the song's place in it, for the looks
 * that show the album's tracks: -1 when the library has neither */
static int song_album(const mu_now *mn, int *place)
{
	const char *path;
	int al, t;

	if (g_song_al >= 0 && g_song_al < g_mp4.nalbums) {
		*place = g_song_place;
		return g_song_al;
	}
	path = mp4c_track(mn->index);
	al = path && path[0] ? ml_album_of(&g_mp4, path) : -1;
	*place = -1;
	if (al < 0 || al >= g_mp4.nalbums) return -1;
	for (t = 0; t < g_mp4.albums[al].n; t++)
		if (!strcmp(g_mp4.tracks[g_mp4.albums[al].first + t].path, path)) { *place = t; break; }
	return al;
}

/* ---- Classic ------------------------------------------------------------------------ */

/* The play state's mark, filled: the play triangle, two bars for a pause, a
 * square for a stop, `s` tall, centered on (cx, cy) */
static void state_mark(SDL_Renderer *r, mu_state st, int cx, int cy, int s, SDL_Color c)
{
	int bw = s / 3;

	if (st == MU_PLAYING) {
		/* Its middle at the triangle's centroid, a third of the way in */
		float h = (float)s, w = h * 0.87f, x0 = (float)cx - w / 3.0f;
		SDL_Vertex v[3] = {
			{ { x0, (float)cy - h / 2 }, c, { 0, 0 } },
			{ { x0 + w, (float)cy }, c, { 0, 0 } },
			{ { x0, (float)cy + h / 2 }, c, { 0, 0 } },
		};

		SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
	} else if (st == MU_PAUSED) {
		ui_round_rect(r, &(SDL_Rect){ cx - s / 2 + s / 12, cy - s / 2, bw, s }, bw / 3, c);
		ui_round_rect(r, &(SDL_Rect){ cx + s / 2 - s / 12 - bw, cy - s / 2, bw, s }, bw / 3, c);
	} else {
		ui_round_rect(r, &(SDL_Rect){ cx - s * 3 / 8, cy - s * 3 / 8, s * 3 / 4, s * 3 / 4 }, s / 8, c);
	}
}

/* A big clock, the bars and the track list (the lab's, my pick): on the
 * left the clock's panel - the play state and where in the queue over the
 * time played, the bars under it - with the song and its artist under the
 * panel; on the right the album's tracks in a panel of their own, the song
 * in the accent's fill. */
static unsigned classic(SDL_Renderer *r, const npl_frame *F, const mu_now *mn, unsigned phase)
{
	static ds_flex L;
	TTF_Font *fs = ui_font(UI_F_META), *fc = ui_face_at(DS_TYPE_CLOCK, true);
	unsigned wait = (unsigned)-1;
	int root, left, panel, status, clock, barbox, words, title, artist, list;
	SDL_Rect pr, br, lr, sr, cr, tr, ar;
	char t[16];
	int al, place;

	root = ds_flex_begin(&L, F->content.w, F->content.h,
	                     &(ds_box){ .dir = DS_ROW, .gap = DS_SECTION });
	left = ds_flex_add(&L, root, &(ds_box){ .dir = DS_COL, .w = DS_LOOK_CLASSIC_W, .gap = DS_SECTION });
	panel = ds_flex_add(&L, left, &(ds_box){ .dir = DS_COL, .grow = 1, .gap = DS_RELATED,
	                                         .pad = { DS_LOOK_PANEL_PAD, DS_SECTION, DS_LOOK_PANEL_PAD,
	                                                  DS_LOOK_PANEL_PAD } });
	status = ds_flex_add(&L, panel, &(ds_box){ .h = DS_LINE_META });
	clock = ds_flex_add(&L, panel, &(ds_box){ .h = DS_LINE_CLOCK });
	barbox = ds_flex_add(&L, panel, &(ds_box){ .grow = 1 });
	words = ds_flex_add(&L, left, &(ds_box){ .dir = DS_COL });
	title = ds_flex_add(&L, words, &(ds_box){ .h = DS_LINE_TITLE });
	artist = ds_flex_add(&L, words, &(ds_box){ .h = DS_LINE_META });
	list = ds_flex_add(&L, root, &(ds_box){ .grow = 1 });
	ds_flex_solve(&L);
	pr = at(&L, panel, F->content.x, F->content.y);
	sr = at(&L, status, F->content.x, F->content.y);
	cr = at(&L, clock, F->content.x, F->content.y);
	br = at(&L, barbox, F->content.x, F->content.y);
	tr = at(&L, title, F->content.x, F->content.y);
	ar = at(&L, artist, F->content.x, F->content.y);
	lr = at(&L, list, F->content.x, F->content.y);

	/* The clock's panel */
	ds_panel_slab(r, &pr, CFG_NO_COLOR, true);
	{
		int th = ui_font_line(UI_F_META), ms = th * 3 / 5, x = sr.x;
		const char *word = mn->state == MU_PLAYING ? T(S_MP4_PLAYING)
		                 : mn->state == MU_PAUSED  ? T(S_MP4_PAUSED) : T(S_MP4_STOPPED);
		int cw = count_mark(r, mn, sr.x + sr.w, sr.y, 1);

		state_mark(r, mn->state, x + ms / 2, sr.y + th * 2 / 5, ms, ui_col(theme()->accent_ink, 255));
		x += ms + DS_LOOK_CAP * 4;
		{
			char fit[96];

			ui_fit_text(fs, word, fit, sizeof fit, sr.w - (x - sr.x) - cw - DS_SECTION);
			ui_text(r, fs, fit, x, ui_line_y(UI_F_META, sr.y), -1, UI_TEXT_DIM);
		}
	}
	/* The time played, as large as the panel holds it: a song past an hour
	 * that would not fit comes down to the title's size */
	mmss(t, sizeof t, mn->at);
	if (!fc || ui_text_tabular_width(fc, t) > cr.w) {
		ui_text_tabular(r, ui_font(UI_F_TITLE), t, cr.x, ui_cap_y(ui_font(UI_F_TITLE), cr.y + cr.h / 2),
		                -1, UI_TEXT);
	} else {
		ui_text_tabular(r, fc, t, cr.x, ui_cap_y(fc, cr.y + cr.h / 2), -1, UI_TEXT);
	}
	/* The bars on the panel's foot, the room over them theirs */
	bars(r, &M.bars, br.x, br.y + br.h, br.h, 0);

	/* The song and who it is by, under the panel */
	line(r, UI_F_TITLE, mn->title, tr.x, tr.y, tr.w, phase, UI_TEXT, &wait);
	line(r, UI_F_META, mn->artist, ar.x, ar.y, ar.w, phase, UI_TEXT_SOFT, &wait);

	/* The album's tracks: as many rows as the panel holds, the song's in
	 * view and as near the middle as the list allows, its row the
	 * accent's fill */
	ds_panel_slab(r, &lr, CFG_NO_COLOR, true);
	al = song_album(mn, &place);
	if (al >= 0) {
		const ml_album *A = &g_mp4.albums[al];
		int rows = (lr.h - 2 * DS_SECTION) / DS_LOOK_LIST_ROW, first, i;
		int top = lr.y + (lr.h - rows * DS_LOOK_LIST_ROW) / 2 / DS_GRID * DS_GRID;
		int x = lr.x + DS_SECTION, w = lr.w - 2 * DS_SECTION;

		if (rows > A->n) rows = A->n;
		first = place - rows / 2;
		if (first > A->n - rows) first = A->n - rows;
		if (first < 0) first = 0;
		for (i = 0; i < rows; i++) {
			int k = first + i, y = top + i * DS_LOOK_LIST_ROW;
			bool on = k == place;
			SDL_Color c = on ? ui_col(theme()->on_accent, 255) : UI_TEXT_SOFT;
			char num[12], fit[160];
			int ty = ui_cap_y(fs, y + DS_LOOK_LIST_ROW / 2);

			if (on)
				ui_round_rect(r, &(SDL_Rect){ lr.x + DS_RELATED, y, lr.w - 2 * DS_RELATED, DS_LOOK_LIST_ROW },
				              DS_RADIUS_ROW, ui_col(theme()->accent, 255));
			snprintf(num, sizeof num, "%d", k + 1);
			ui_text_tabular(r, fs, num, x + DS_LOOK_LIST_NUM - DS_GRID * 2, ty, 1,
			                on ? c : UI_TEXT_DIM);
			ui_fit_text(fs, g_mp4.tracks[A->first + k].name, fit, sizeof fit,
			            w - DS_LOOK_LIST_NUM);
			ui_text(r, fs, fit, x + DS_LOOK_LIST_NUM, ty, -1, c);
		}
	}
	return wait;
}

/* ---- Spectrum ------------------------------------------------------------------------ */

/* 32 bars across the screen, one a band, with their reflection under them,
 * under the cover and the names (the lab's, my pick). The bars are as wide
 * as 32 on the grid make them (760), and the cover's column is theirs, both
 * from the grid step nearest the middle. */
static unsigned spectrum(SDL_Renderer *r, const npl_frame *F, const mu_now *mn, unsigned phase)
{
	static ds_flex L;
	unsigned wait = (unsigned)-1;
	int w = bars_w(M.bars.n), x = (ZYPEROS_SCREEN_W - w) / 2 / DS_GRID * DS_GRID;
	int root, top, cover, names, count, title, artist, area;
	SDL_Rect cr, ct, tt, wr, ar;
	int bh, refl;

	root = ds_flex_begin(&L, w, F->content.h, &(ds_box){ .dir = DS_COL, .gap = DS_SECTION });
	top = ds_flex_add(&L, root, &(ds_box){ .dir = DS_ROW, .h = DS_LOOK_COVER, .gap = DS_SECTION + DS_GRID,
	                                       .align = DS_A_CENTER });
	cover = ds_flex_add(&L, top, &(ds_box){ .w = DS_LOOK_COVER, .h = DS_LOOK_COVER });
	names = ds_flex_add(&L, top, &(ds_box){ .dir = DS_COL, .grow = 1, .justify = DS_J_CENTER });
	count = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_META });
	title = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_TITLE });
	artist = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_MENU });
	area = ds_flex_add(&L, root, &(ds_box){ .grow = 1 });
	ds_flex_solve(&L);
	cr = at(&L, cover, x, F->content.y);
	ct = at(&L, count, x, F->content.y);
	tt = at(&L, title, x, F->content.y);
	wr = at(&L, artist, x, F->content.y);
	ar = at(&L, area, x, F->content.y);

	/* The cover in the light a cover in MP4 gets (DESIGN.md 7) */
	ui_lift(r, &cr, ZYPEROS_ACCENT, UI_LIFT_FOCUS_SMALL);
	if (g_np.tex) SDL_RenderCopy(r, g_np.tex, NULL, &cr);
	count_mark(r, mn, ct.x, ct.y, -1);
	line(r, UI_F_TITLE, mn->title, tt.x, tt.y, tt.w, phase, UI_TEXT, &wait);
	line(r, UI_F_MENU, mn->artist, wr.x, wr.y, wr.w, phase, UI_TEXT_SOFT, &wait);

	/* The bars over their reflection: the bars ten parts of the room in
	 * thirteen, on the grid, and the reflection the rest, about the
	 * lab's three tenths of the bars */
	bh = ar.h * 10 / 13 / DS_GRID * DS_GRID;
	refl = ar.h - bh;
	bars(r, &M.bars, ar.x, ar.y + bh, bh, refl);
	return wait;
}

/* ---- Cassette --------------------------------------------------------------------- */

/* Where the song is, as a fraction, for what moves with it every frame: the
 * daemon's live position for the song that plays (mp4c_at_live, between
 * its once-a-second reports), the given one for a shot's or the list's */
static float song_frac(const mu_now *mn)
{
	double at = mn == mp4c_now() ? mp4c_at_live() : mn->at;

	return mn->len > 0 ? (float)(at / mn->len) : 0.0f;
}

/* What turns: the reels' speed, and the tape (src/mp4look.h). Stepped once
 * a frame by turn_step for the look that has them. */
static mp4l_spin g_spin;
static mp4l_tape g_tape;
static mp4l_disc g_disc;
static cas_tex   g_cas;
static vin_tex   g_vin;

/* The label's words, and the last song's leaving as a new one's arrive:
 * they go in **exit** (in), up a grid step and out, and the new ones come
 * in **move** (out), up from a grid step under; the cassette rewinds over
 * both */
typedef struct {
	char title[128], under[260], place[24];
	char side;
} cas_words;

static struct {
	cas_words now, was;
	unsigned  t0;            /* the change, while `moving` */
	bool      moving;
	int       song;
	bool      known;
} g_lab;

/* The tape's wind: as much tape as a whole song is, in units, for the
 * reels' turn as it winds or rewinds. Not the song's real tape - 10 m at
 * 4.76 cm a second, which would spin the hubs too fast to see a tooth -
 * but enough that a rewind from a song's end turns the empty hub 4.3 times
 * a second at its fastest, 26 degrees a frame at 60: under the 30 at which
 * six teeth would seem to stand or turn back. */
#define CAS_WIND 800.0f

static int song_album(const mu_now *mn, int *place);

/* The tonearm's needle for the song: its place on the album's grooves */
static float arm_target(const mu_now *mn)
{
	int place, al = song_album(mn, &place), n = al >= 0 ? g_mp4.albums[al].n : mn->count;

	if (place < 0) place = mn->index;
	return vin_groove_r(place, n, song_frac(mn));
}

/* What turns, for this frame: the reels and the tape, or the record and its
 * arm. `gap` says the last frame was long ago (a menu, the lock, Now
 * Playing opening), and then the tape and the needle are where the song is
 * with no rewind or glide to get there */
static void turn_step(mp4l_id look, const mu_now *mn, float dt, bool gap)
{
	float target = song_frac(mn);

	if (gap) {
		g_tape.known = false;
		g_disc.known = false;
		g_spin = (mp4l_spin){ .v = mn->state == MU_PLAYING ? 1.0f : 0.0f,
		                      .to = mn->state == MU_PLAYING ? 1.0f : 0.0f };
		dt = 0;
	}
	mp4l_spin_to(&g_spin, mn->state == MU_PLAYING);
	mp4l_spin_step(&g_spin, dt);
	if (look == MP4L_CASSETTE)
		mp4l_tape_step(&g_tape, dt, mn->index, target, g_spin.v, CAS_TAPE_SPEED, CAS_HUB_R,
		               CAS_PACK_R, CAS_WIND);
	else
		mp4l_disc_step(&g_disc, dt, g_spin.v, arm_target(mn));
}

static bool turn_moving(mp4l_id look, const mu_now *mn)
{
	float target = song_frac(mn);

	if (mp4l_spin_moving(&g_spin)) return true;
	if (look == MP4L_CASSETTE) return mp4l_tape_moving(&g_tape, target) || g_lab.moving;
	return mp4l_disc_moving(&g_disc, arm_target(mn));
}

/* The label's words for the song: the side its place in the album puts it
 * on (the first half A, the rest B, as an album went onto a tape), the
 * song, its place, and who and what album under the window */
static void cas_words_of(const mu_now *mn, cas_words *w)
{
	int place, al = song_album(mn, &place), n = al >= 0 ? g_mp4.albums[al].n : mn->count;

	if (place < 0) place = mn->index;
	snprintf(w->title, sizeof w->title, "%s", mn->title);
	if (mn->artist[0] && mn->album[0] && strcmp(mn->artist, mn->album))
		snprintf(w->under, sizeof w->under, "%s \xc2\xb7 %s", mn->artist, mn->album);
	else
		snprintf(w->under, sizeof w->under, "%s", mn->artist[0] ? mn->artist : mn->album);
	w->place[0] = '\0';
	if (n > 1) snprintf(w->place, sizeof w->place, "%d / %d", place + 1, n);
	w->side = place < (n + 1) / 2 ? 'A' : 'B';
}

/* One set of the label's words at `alpha`, `dy` off their places */
static void cas_words_draw(SDL_Renderer *r, const cas_words *w, int x, int y, float alpha, int dy,
                           unsigned phase, unsigned *wait)
{
	const ds_theme *th = theme();
	TTF_Font *ft = page_font(PAGE_GAME_TITLE), *fs = page_font(PAGE_CHROME), *fl = ui_face_at(DS_TYPE_SUPPORT, true);
	SDL_Color text = ui_col(th->text, (Uint8)(255 * alpha)), dim = ui_col(th->text_dim, (Uint8)(255 * alpha));
	int mid = y + CAS_ROW_MID + dy, pw = 0, tx = x + CAS_TITLE_X0, tw;
	char side[2] = { w->side, 0 }, fit[260];

	if (alpha <= 0) return;
	/* The side's letter in its box: text's ink, the paper's letter */
	ui_round_rect(r, &(SDL_Rect){ x + CAS_SIDE_X0, y + CAS_SIDE_Y0 + dy, CAS_SIDE, CAS_SIDE }, DS_RADIUS_CHIP,
	              text);
	if (fl) ui_text(r, fl, side, x + CAS_SIDE_X0 + CAS_SIDE / 2, ui_cap_y(fl, mid), 0,
	                ui_col(th->surface, (Uint8)(255 * alpha)));
	/* Its place and the play mode's mark, at the label's right */
	if (w->place[0]) {
		int g = MP4_MODES[mp4c_mode()].glyph, gs = DS_HINT_GLYPH * 3 / 4;
		int right = x + CAS_LABEL_X1 - CAS_LABEL_PAD;

		if (g >= 0) {
			ui_glyph_draw(r, (ui_glyph)g, right - gs / 2, mid, gs, ui_col(th->accent_ink, (Uint8)(255 * alpha)));
			right -= gs + DS_RELATED;
		}
		pw = x + CAS_LABEL_X1 - CAS_LABEL_PAD - right + ui_text(r, fs, w->place, right, ui_cap_y(fs, mid), 1, dim);
	}
	/* The song, sliding when it is longer than the label */
	tw = x + CAS_LABEL_X1 - CAS_LABEL_PAD - pw - DS_SECTION - tx;
	{
		unsigned wt = ui_pingpong_wait(ui_text_width(ft, w->title) - tw, phase);

		ui_text_marquee(r, ft, w->title, tx, ui_cap_y(ft, mid), tw, phase, text);
		if (wt < *wait) *wait = wt;
	}
	/* Who and the album, under the window */
	ui_fit_text(fs, w->under, fit, sizeof fit, CAS_LABEL_X1 - CAS_LABEL_X0 - 2 * CAS_LABEL_PAD);
	ui_text(r, fs, fit, x + CAS_W / 2, ui_cap_y(fs, y + CAS_UNDER_MID + dy), 0, dim);
}

/* A C-cassette at its real proportions in a clear smoke-grey shell (the
 * lab's, my pick), its reels turning through the window, the label saying
 * the side, the song and its place, the cover's stripe across it; the bars
 * under it, as wide as it. */
static unsigned cassette(SDL_Renderer *r, const npl_frame *F, const mu_now *mn, unsigned phase)
{
	static ds_flex L;
	const ds_theme *th = theme();
	unsigned wait = (unsigned)-1, now = clock_ms();
	int root, cas, area, x, y, k;
	SDL_Rect cr, ar, win;
	unsigned stripe = g_np.rgb != CFG_NO_COLOR ? g_np.rgb : ui_rgb(th->accent);

	root = ds_flex_begin(&L, F->content.w, F->content.h,
	                     &(ds_box){ .dir = DS_COL, .gap = DS_SECTION, .align = DS_A_CENTER });
	cas = ds_flex_add(&L, root, &(ds_box){ .w = DS_LOOK_CASSETTE_W, .h = CAS_H });
	area = ds_flex_add(&L, root, &(ds_box){ .w = DS_LOOK_CASSETTE_W, .grow = 1 });
	ds_flex_solve(&L);
	cr = at(&L, cas, F->content.x, F->content.y);
	ar = at(&L, area, F->content.x, F->content.y);
	/* On the grid, whatever the column's middle came to */
	cr.x = cr.x / DS_GRID * DS_GRID;
	ar.x = cr.x;
	x = cr.x;
	y = cr.y;
	if (!cas_ready(r, &g_cas, th, theme_index() == DS_DARK)) return wait;

	SDL_RenderCopy(r, g_cas.shell, NULL, &(SDL_Rect){ x, y, CAS_W, CAS_H });
	/* The cover's stripe across the label */
	SDL_SetRenderDrawColor(r, (Uint8)(stripe >> 16), (Uint8)(stripe >> 8), (Uint8)stripe, 255);
	SDL_RenderFillRect(r, &(SDL_Rect){ x + CAS_LABEL_X0, y + CAS_STRIPE_Y0, CAS_LABEL_X1 - CAS_LABEL_X0,
	                                   CAS_STRIPE_Y1 - CAS_STRIPE_Y0 });
	/* The reels through the window: each pack its radius, each hub its
	 * angle, the window's box their clip and its glass over them */
	win = (SDL_Rect){ x + CAS_WIN_X0, y + CAS_WIN_Y0, CAS_WIN_X1 - CAS_WIN_X0, CAS_WIN_Y1 - CAS_WIN_Y0 };
	SDL_RenderSetClipRect(r, &win);
	for (k = 0; k < 2; k++) {
		float cx = (float)x + CAS_W / 2.0f + (k ? CAS_REEL_DX : -CAS_REEL_DX), cy = (float)y + CAS_REEL_Y;
		float pr = mp4l_tape_r(g_tape.f, k, CAS_HUB_R, CAS_PACK_R);
		float hr = (float)((int)ceilf(CAS_HUB_R));

		SDL_RenderCopyF(r, g_cas.pack, NULL, &(SDL_FRect){ cx - pr, cy - pr, 2 * pr, 2 * pr });
		SDL_RenderCopyExF(r, g_cas.hub, NULL, &(SDL_FRect){ cx - hr, cy - hr, 2 * hr, 2 * hr },
		                  (double)g_tape.ang[k], NULL, SDL_FLIP_NONE);
	}
	SDL_RenderSetClipRect(r, NULL);
	SDL_RenderCopy(r, g_cas.glass, NULL, &win);

	/* The label's words: a new song's coming in after the last's left */
	{
		cas_words w;
		unsigned t;

		cas_words_of(mn, &w);
		if (!g_lab.known || M.shot) {
			g_lab.now = w;
			g_lab.song = mn->index;
			g_lab.known = true;
			g_lab.moving = false;
		} else if (mn->index != g_lab.song) {
			g_lab.was = g_lab.now;
			g_lab.song = mn->index;
			g_lab.t0 = now;
			g_lab.moving = true;
		}
		g_lab.now = w;
		t = g_lab.moving ? now - g_lab.t0 : DS_MS_EXIT + DS_MS_MOVE;
		if (t >= DS_MS_EXIT + DS_MS_MOVE) {
			g_lab.moving = false;
			cas_words_draw(r, &g_lab.now, x, y, 1.0f, 0, phase, &wait);
		} else if (t < DS_MS_EXIT) {
			float k = ds_in((float)t / DS_MS_EXIT);

			cas_words_draw(r, &g_lab.was, x, y, 1.0f - k, -(int)(DS_GRID * k + 0.5f), phase, &wait);
		} else {
			float k = ds_out((float)(t - DS_MS_EXIT) / DS_MS_MOVE);

			cas_words_draw(r, &g_lab.now, x, y, k, (int)(DS_GRID * (1.0f - k) + 0.5f), phase, &wait);
		}
	}

	/* The bars under it, as wide as it, their room theirs */
	bars(r, &M.bars, ar.x, ar.y + ar.h, ar.h, 0);
	return wait;
}

/* ---- Vinyl ------------------------------------------------------------------------ */

/* The label: the cover itself as a disc of triangles round (cx, cy), its
 * picture turned `ang` degrees clockwise - the record's angle - by turning
 * where each edge point reads it from, inside the picture's square so its
 * corners never show. The light's shadow round the label's edge, over it,
 * softens the triangles' straight sides. */
#define LABEL_STEPS 96

static void label_draw(SDL_Renderer *r, SDL_Texture *cover, float cx, float cy, float rad, float ang)
{
	SDL_Vertex v[LABEL_STEPS + 1];
	int idx[LABEL_STEPS * 3], i;
	float turn = ang * (float)M_PI / 180.0f;
	const SDL_Color white = { 255, 255, 255, 255 };

	v[0] = (SDL_Vertex){ { cx, cy }, white, { 0.5f, 0.5f } };
	for (i = 0; i < LABEL_STEPS; i++) {
		float a = 2.0f * (float)M_PI * (float)i / LABEL_STEPS;

		v[i + 1] = (SDL_Vertex){ { cx + rad * cosf(a), cy + rad * sinf(a) }, white,
		                         { 0.5f + 0.5f * cosf(a - turn), 0.5f + 0.5f * sinf(a - turn) } };
		idx[i * 3] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % LABEL_STEPS;
	}
	SDL_RenderGeometry(r, cover, v, LABEL_STEPS + 1, idx, LABEL_STEPS * 3);
}

/* A record turning at 33 1/3, pressed in the cover's main color and
 * darkened, the cover its label, the album's songs its rings; the tonearm's
 * needle on the song's place among them; the names and the bars beside it
 * (the lab's, my pick). */
static unsigned vinyl(SDL_Renderer *r, const npl_frame *F, const mu_now *mn, unsigned phase)
{
	static ds_flex L;
	const ds_theme *th = theme();
	unsigned wait = (unsigned)-1, rgb = g_np.rgb != CFG_NO_COLOR ? g_np.rgb : ui_rgb(th->accent);
	int root, rec, col, names, count, title, artist, area, place, al, n;
	SDL_Rect rr, cr, ct, tt, wr, ar;
	float cx, cy, px, py, a = VIN_PIVOT_A * (float)M_PI / 180.0f;
	int col_x, ox, room;
	const int plate = (int)ceilf(VIN_PLATTER_R) * 2 + 4, face = (int)ceilf(VIN_R) * 2 + 4;

	al = song_album(mn, &place);
	n = al >= 0 ? g_mp4.albums[al].n : mn->count;
	/* The record left in the room and on its middle; the column from a
	 * section past the tonearm's base, on the grid. The room is a 4:3
	 * canvas's, in the middle of a wider one (src/canvas.h), so the record
	 * and its words stand together as the cassette's do. */
	ox = (ZYPEROS_SCREEN_W - canvas_panel_room()) / 2;
	room = F->content.w - 2 * ox;
	root = ds_flex_begin(&L, room, F->content.h, &(ds_box){ .dir = DS_ROW, .align = DS_A_CENTER });
	rec = ds_flex_add(&L, root, &(ds_box){ .w = DS_LOOK_RECORD, .h = DS_LOOK_RECORD });
	ds_flex_solve(&L);
	rr = at(&L, rec, F->content.x + ox, F->content.y);
	rr.y = rr.y / DS_GRID * DS_GRID;
	cx = (float)rr.x + DS_LOOK_RECORD / 2.0f;
	cy = (float)rr.y + DS_LOOK_RECORD / 2.0f;
	px = cx + VIN_PIVOT_D * cosf(a);
	py = cy + VIN_PIVOT_D * sinf(a);
	col_x = ((int)ceilf(px) + 38 + DS_SECTION + DS_GRID - 1) / DS_GRID * DS_GRID;

	root = ds_flex_begin(&L, F->content.x + ox + room - col_x, DS_LOOK_RECORD,
	                     &(ds_box){ .dir = DS_COL, .justify = DS_J_BETWEEN,
	                                .pad = { 0, DS_SECTION, 0, DS_SECTION } });
	names = ds_flex_add(&L, root, &(ds_box){ .dir = DS_COL });
	count = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_META });
	title = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_TITLE });
	artist = ds_flex_add(&L, names, &(ds_box){ .h = DS_LINE_MENU });
	area = ds_flex_add(&L, root, &(ds_box){ .h = DS_LOOK_LIST_ROW * 2 });
	ds_flex_solve(&L);
	col = 0;
	(void)col;
	cr = at(&L, root, col_x, rr.y);
	ct = at(&L, count, col_x, rr.y);
	tt = at(&L, title, col_x, rr.y);
	wr = at(&L, artist, col_x, rr.y);
	ar = at(&L, area, col_x, rr.y);
	(void)cr;

	if (!vin_ready(r, &g_vin, th, theme_index() == DS_DARK, cover_rgb_darken(rgb, 0.20f, 0.55f), n))
		return wait;
	/* The platter, the record's face, its label turning, the light that
	 * does not, the spindle */
	SDL_RenderCopyF(r, g_vin.deck, NULL, &(SDL_FRect){ cx - plate / 2.0f, cy - plate / 2.0f, plate, plate });
	SDL_RenderCopyF(r, g_vin.face, NULL, &(SDL_FRect){ cx - face / 2.0f, cy - face / 2.0f, face, face });
	if (g_np.tex) label_draw(r, g_np.tex, cx, cy, VIN_LABEL_R, g_disc.ang);
	SDL_RenderCopyF(r, g_vin.light, NULL, &(SDL_FRect){ cx - face / 2.0f, cy - face / 2.0f, face, face });
	/* The tonearm on its base, turned to put its needle on the song */
	{
		int bw, bh, aw, ah;

		SDL_QueryTexture(g_vin.base, NULL, NULL, &bw, &bh);
		SDL_QueryTexture(g_vin.arm, NULL, NULL, &aw, &ah);
		SDL_RenderCopyF(r, g_vin.base, NULL, &(SDL_FRect){ px - bw / 2.0f, py - bh / 2.0f, bw, bh });
		SDL_RenderCopyExF(r, g_vin.arm, NULL, &(SDL_FRect){ px - VIN_ARM_PX, py - VIN_ARM_PY, aw, ah },
		                  vin_arm_angle(g_disc.arm), &(SDL_FPoint){ VIN_ARM_PX, VIN_ARM_PY }, SDL_FLIP_NONE);
	}

	/* The names, and the bars at the column's foot */
	count_mark(r, mn, ct.x, ct.y, -1);
	line(r, UI_F_TITLE, mn->title, tt.x, tt.y, tt.w, phase, UI_TEXT, &wait);
	line(r, UI_F_MENU, mn->artist, wr.x, wr.y, wr.w, phase, UI_TEXT_SOFT, &wait);
	bars(r, &M.bars, ar.x, ar.y + ar.h, ar.h, 0);
	return wait;
}

/* ---- the look --------------------------------------------------------------------- */

/* Now Playing in `look`, its bars stepped to `bands` (NULL: falling) */
static unsigned look_draw(app *a, mp4l_id look, const mu_now *mn, const uint8_t *bands)
{
	SDL_Renderer *r = a->r;
	key_hint h[KEYHINTS_MAX];
	int nh = np_hint_list(h, mn->state == MU_PLAYING, 0, mn->count, false);
	npl_frame F;
	unsigned wait, phase = mq_phase(MQ_NP, mn->index, (int)strlen(mn->title)), corner;

	g_frame_no++;
	{
		/* What turns, before the bars' step moves M.at on */
		unsigned now = clock_ms();
		float dt = M.at ? (float)(now - M.at) : 0.0f;
		bool gap = !M.at || dt > 250.0f || M.still || (int)look != M.look;

		/* A shot's tape where its song is, the hubs at rest */
		if (look == MP4L_CASSETTE || look == MP4L_VINYL)
			turn_step(look, mn, gap || M.shot ? 0 : dt, gap || M.still || M.shot);
	}
	motion_step(look, bands);
	frame_layout(&F, h, nh);
	ground(r);
	wait = look == MP4L_CASSETTE ? cassette(r, &F, mn, phase)
	     : look == MP4L_VINYL ? vinyl(r, &F, mn, phase)
	     : look == MP4L_CLASSIC ? classic(r, &F, mn, phase)
	     : look == MP4L_SPECTRUM ? spectrum(r, &F, mn, phase) : (unsigned)-1;
	frame_time(r, &F, mn, look == MP4L_CLASSIC);
	keyhints_draw(r, h, nh, DS_MARGIN, ZYPEROS_SCREEN_W - DS_MARGIN, F.keys_cy, KEYHINTS_SPREAD,
	              true, 255);
	/* Music playing moves every frame; paused, until the bars are down */
	if (!M.still && !M.shot && (mn->state == MU_PLAYING || !mp4l_bars_rest(&M.bars) ||
	                            ((look == MP4L_CASSETTE || look == MP4L_VINYL) && turn_moving(look, mn))))
		wait = 0;
	corner = draw_topbar(r, theme_index(), UI_TEXT_SOFT, wait <= 50 || frames_wanted());
	return corner < wait ? corner : wait;
}

unsigned npl_draw(app *a, mp4l_id look, const mu_now *mn)
{
	return look_draw(a, look, mn, sound_bands(mn->state == MU_PLAYING));
}

/* ---- the bench ---------------------------------------------------------------------- */

/* The scene it plays, by its own clock at 60 frames a second: the song
 * playing on, the next one at 2 s (the cassette rewinds, the needle glides,
 * the label's words change), a pause at 3.5 s (what turns spins down, the
 * bars fall), playing again at 5 s. With ZYPEROS_NP_FRAMES naming a folder,
 * every second frame is written there as frame-NNNN.png, 30 a second, for
 * a film of the motion. */
void npl_bench(app *a, const mu_now *mn0, int frames)
{
	mp4l_id look = np_look_for();
	const char *dir = getenv("ZYPEROS_NP_FRAMES");
	static double ms[4096];
	double sum = 0, worst = 0;
	Uint64 f = SDL_GetPerformanceFrequency();
	uint8_t lv[MP4L_BANDS];
	int i, n = frames > 4096 ? 4096 : frames, k;
	bool changed = false;
	mu_now mn = *mn0;

	if (look == MP4L_COVER || n <= 0) return;
	M.shot = false;
	M.at = 0;
	for (i = 0; i < n; i++) {
		unsigned t = (unsigned)i * 1000 / 60;
		Uint64 t0;

		g_clock = 100000 + t;
		if (t >= 2000 && !changed) {
			/* The next song, from its start */
			changed = true;
			mn.index++;
			mn.at = 0;
			if (g_song_al >= 0 && g_song_al < g_mp4.nalbums) {
				const ml_album *A = &g_mp4.albums[g_song_al];

				g_song_place = (g_song_place + 1) % (A->n > 0 ? A->n : 1);
				snprintf(mn.title, sizeof mn.title, "%s", g_mp4.tracks[A->first + g_song_place].name);
			}
		}
		mn.state = t >= 3500 && t < 5000 ? MU_PAUSED : MU_PLAYING;
		if (mn.state == MU_PLAYING) mn.at += 1.0 / 60;
		show_bands(lv, t);
		t0 = SDL_GetPerformanceCounter();
		look_draw(a, look, &mn, mn.state == MU_PLAYING ? lv : NULL);
		SDL_RenderFlush(a->r);
		ms[i] = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)f;
		sum += ms[i];
		if (ms[i] > worst) worst = ms[i];
		if (dir && i % 2 == 0) {
			SDL_Surface *o = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, 32,
			                                                SDL_PIXELFORMAT_RGBA32);
			char path[1024];

			if (o && SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, o->pixels, o->pitch) == 0) {
				snprintf(path, sizeof path, "%s/frame-%04d.png", dir, i / 2);
				IMG_SavePNG(o, path);
			}
			if (o) SDL_FreeSurface(o);
		}
	}
	g_clock = 0;
	/* The 95th percentile: sorted by insertion, n is small */
	for (i = 1; i < n; i++) {
		double v = ms[i];

		for (k = i - 1; k >= 0 && ms[k] > v; k--) ms[k + 1] = ms[k];
		ms[k + 1] = v;
	}
	fprintf(stderr, "np-bench: %s, %d frames: mean %.2f ms, p95 %.2f ms, worst %.2f ms\n",
	        mp4l_key(look), n, sum / n, ms[n * 95 / 100], worst);
}

/* ---- the list's picture ------------------------------------------------------------- */

/* MENU > Player Look's picture: Now Playing drawn whole, at the screen's
 * own size, into a texture, so the picture is the screen and not a drawing
 * of it; halved into a second one, where the GPU's bilinear sample at each
 * texel's corner is the average of four, and that drawn into the box. Drawn
 * straight at the box's scale the panels' round corners, a row of units
 * each, came apart at a third of a unit, and once shrunk 3.4 times in one
 * copy the words' thin strokes dropped out. 3 MB and 0.75 MB of textures,
 * made the first time the list opens and kept.
 *
 * The song that plays, live; with none, the shelf's album's first song in
 * show - its clock running from when the list opened and the bars moving
 * to show_bands - so the picture moves as the look will. Cover's is the
 * cover look itself. */
void npl_preview(app *a, mp4l_id look, int x, int y, int w, int h)
{
	static SDL_Texture *full, *half;
	static unsigned since, seen;
	SDL_Renderer *r = a->r;
	SDL_Texture *was = SDL_GetRenderTarget(r);
	const mu_now *live = mp4c_now();
	bool playing_music = (live->state == MU_PLAYING || live->state == MU_PAUSED) &&
	                     !mp4c_is_book() && !mp4c_is_videos() && !mp4c_video();
	mu_now show;
	/* A shot's moment, held (src/shots.h) */
	unsigned now = shot_path ? 0 : plat_now_ms();
	float sx, sy;
	const mu_now *mn = live;

	if (!full) full = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
	                                    ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
	if (!half) half = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
	                                    ZYPEROS_SCREEN_W / 2, ZYPEROS_SCREEN_H / 2);
	if (!full || !half) return;
	SDL_SetTextureBlendMode(full, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(half, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(full, SDL_ScaleModeLinear);
	SDL_SetTextureScaleMode(half, SDL_ScaleModeLinear);
	/* A list opened again starts its show again */
	if (now - seen > 500) since = now;
	seen = now;
	if (!playing_music) {
		int s = a->sys_cursor, al = -1, i;
		const sysview *v = &a->view[s];

		if (v->album && v->cursor >= 0 && v->cursor < v->list.count) al = v->album[v->cursor];
		if (al < 0 || al >= g_mp4.nalbums || g_mp4.albums[al].kind != ML_MUSIC)
			for (i = 0, al = -1; i < g_mp4.nalbums && al < 0; i++)
				if (g_mp4.albums[i].kind == ML_MUSIC && g_mp4.albums[i].n > 0) al = i;
		memset(&show, 0, sizeof show);
		if (al >= 0) {
			const ml_album *A = &g_mp4.albums[al];
			int ar;

			for (ar = 0; ar < g_mp4.nartists - 1; ar++)
				if (al < g_mp4.artists[ar].first + g_mp4.artists[ar].n) break;
			snprintf(show.title, sizeof show.title, "%s", g_mp4.tracks[A->first].name);
			snprintf(show.artist, sizeof show.artist, "%s", g_mp4.artists[ar].name);
			snprintf(show.album, sizeof show.album, "%s", A->name);
			show.count = A->n;
			np_cover(a, al);
			npl_song(al, 0);
		}
		show.state = MU_PLAYING;
		show.len = 214.0;
		show.at = shot_path ? 83.0 : (double)(now - since) / 1000.0;
		mn = &show;
	} else {
		np_cover(a, ml_album_of(&g_mp4, mp4c_track(live->index)));
	}

	SDL_RenderGetScale(r, &sx, &sy);
	if (SDL_SetRenderTarget(r, full) != 0) return;
	ui_fit_away(true);
	/* At 1, whatever the target it was drawing to was at; the viewport
	 * then the whole texture's */
	SDL_RenderSetScale(r, 1.0f, 1.0f);
	SDL_RenderSetViewport(r, NULL);
	if (look == MP4L_COVER) {
		np_draw(a, mn, "");
	} else if (playing_music) {
		npl_draw(a, look, mn);
	} else {
		/* In show: the bars to show_bands, the rest as Now Playing */
		uint8_t lv[MP4L_BANDS];

		show_bands(lv, now - since);
		if (shot_path) shot_bars(look);
		look_draw(a, look, mn, shot_path ? NULL : lv);
	}
	ui_fit_away(false);
	if (SDL_SetRenderTarget(r, half) == 0) SDL_RenderCopy(r, full, NULL, NULL);
	SDL_SetRenderTarget(r, was);
	SDL_RenderSetScale(r, sx, sy);
	npl_song(-1, -1);
	SDL_RenderCopy(r, half, NULL, &(SDL_Rect){ x, y, w, h });
	/* It moves while it is up: the next frame soon */
	if (!shot_path) redraw_at(now + 16);
}
