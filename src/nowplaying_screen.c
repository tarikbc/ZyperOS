/* SPDX-License-Identifier: GPL-3.0-only */
#include "nowplaying_screen.h"
#include "menukeys_screen.h"

#include "aout_route.h"
#include "app.h"
#include "blur.h"
#include "i18n.h"
#include "keyhints.h"
#include "keyhints_draw.h"
#include "keyrepeat.h"   /* repeat_stage: a second, three */
#include "mp4frame.h"    /* MPF_TOO_BIG, MPF_NO_PICTURE */
#include "mp4video.h"
#include "nowplaying_looks.h"
#include "panel.h"
#include "topbar.h"
#include "ui.h"
#include "ds/flex.h"     /* the band, laid out by rules */
#include "ds/layout.h"   /* DS_FOOT_Y */
#include "ds/mark.h"     /* ds_bar_in */
#include "uisound.h"
#include "usbdrive_screen.h"
#include "zyperdraw.h"
#include "levels.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- MP4: Now Playing ---------------------------------------------------- */

/* How far one step of left or right seeks: ten seconds a tap, and further the
 * longer it is held - a minute a step after a second, five after three.
 * Eric's, 2026-09-29, for books: at ten seconds a step, eleven steps a second,
 * crossing a seventeen-hour book took nine minutes of holding the d-pad, and
 * now takes about twenty seconds. A song is over long before the steps grow,
 * so music is as it was. */
double seek_step(const in_state *in, in_button b)
{
	static const double step[] = { 10, 60, 300 };
	Uint32 held = SDL_GetTicks() - in->down_since[b];

	return in->pressed[b] ? step[0] : step[repeat_stage(held)];
}

void mmss(char *out, size_t n, double sec)
{
	int t = sec > 0 ? (int)(sec + 0.5) : 0;

	/* Hours past the hour: a seventeen-hour book read "-1014:46" in minutes. */
	if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	else           snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* The cover where the shelf puts the card it is looking at, in the same kind
 * of light, and the words beside it. Its own screen and not a panel over the
 * shelf: it is the one screen left open for as long as an album lasts, and
 * what is behind a panel is a picture of games. */
#define NP_SIDE 432                              /* the cover, on screen */
#define NP_X    80
#define NP_Y    ((ZYPEROS_SCREEN_H - NP_SIDE) / 2 - 28)
#define NP_TX   (NP_X + NP_SIDE + 56)            /* the words beside it */
#define NP_TW   (ZYPEROS_SCREEN_W - NP_TX - 64)

/* Whether chapter `i` has a title worth reading, and not only its number -
 * Dungeon Crawler Carl's thirty-seven are "001" to "037" - or none. */
static bool chapter_titled(int i)
{
	const char *t = mp4c_chapter_title(i);
	const char *p;

	for (p = t; *p && (isdigit((unsigned char)*p) || *p == ' '); p++) { }
	return t[0] && *p;
}

/* A chapter as Now Playing names it: its own title when it has one worth
 * reading, "Chapter 3" when the title is only its number or missing. */
void chapter_label(int i, char *out, size_t n)
{
	if (chapter_titled(i)) snprintf(out, n, "%s", mp4c_chapter_title(i));
	else                   snprintf(out, n, T(S_MP4_CHAPTER), i + 1);
}

/* One line of the words, sliding when it is wider than the column. `y` is
 * the top of its line (ui_line_y). */
static void np_line(SDL_Renderer *r, ui_font_role role, const char *s, int y,
                    unsigned phase, SDL_Color col, unsigned *wait)
{
	TTF_Font *f = ui_font(role);
	unsigned w = ui_pingpong_wait(ui_text_width(f, s) - NP_TW, phase);

	ui_text_marquee(r, f, s, NP_TX, ui_line_y(role, y), NP_TW, phase, col);
	if (w < *wait) *wait = w;
}

static void np_hints(SDL_Renderer *r, bool playing, int kind, int count, int cy,
                     bool video, Uint8 alpha);

/* Returns how long until the picture would change by itself, in ms - 0 while
 * a line is sliding - so the loop can leave the screen alone until then. */
unsigned np_draw(app *a, const mu_now *mn, const char *next)
{
	SDL_Renderer *r = a->r;
	SDL_Rect cov = { NP_X, NP_Y, NP_SIDE, NP_SIDE };
	TTF_Font *fs = ui_font(UI_F_META);
	/* MP4's lime for everything that is not the cover or the words, the
	 * same its shelf and its menus wear: the light around the cover and along
	 * the foot, and as **accent-ink**, lime as a mark, the bar, the mode's
	 * mark and the word for a stopped track. It was the cover's own most
	 * vivid color once, which made one mark two colors a screen apart; Eric's
	 * call, 2026-09-19. */
	unsigned rgb = ZYPEROS_ACCENT, wait = (unsigned)-1, phase;
	SDL_Color acc = ui_col(theme()->accent_ink, 255);
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MP4_PAUSED) : T(S_MP4_STOPPED);
	int book = mp4c_is_book() ? ml_album_of(&g_mp4, mp4c_track(mn->index)) : -1;
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[200], fit[200];
	int y, ty, bar, fill, left, right;

	/* A book played to its end says so, rather than that it stopped. */
	if (mn->state == MU_STOPPED && book >= 0 && g_book_done && g_book_done[book])
		state = T(S_MP4_FINISHED);

	g_frame_no++;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The shelf's wash along the foot, and the lift a cover in MP4 gets:
	 * light in Salt & Pepper, and on Beard the wash laid on and a shadow
	 * under the cover (DESIGN.md 7, focus-small). */
	ui_lift(r, &(SDL_Rect){ 0, ZYPEROS_SCREEN_H - 240, ZYPEROS_SCREEN_W, 480 },
	        rgb, UI_LIFT_WASH);
	ui_lift(r, &cov, rgb, UI_LIFT_FOCUS_SMALL);
	if (g_np.tex) SDL_RenderCopy(r, g_np.tex, NULL, &cov);

	/* Title, artist and album slide together when they are too long, on one
	 * clock that starts over with each track. `y` is the top of each line,
	 * and every line is placed by it (ui_line_y). */
	phase = mq_phase(MQ_NP, mn->index, (int)strlen(mn->title));
	y = NP_Y + 36;
	{
		/* Where in the queue, and the mode it is playing in beside it. The
		 * count is the order being HEARD, so shuffled it still runs 1 to n.
		 *
		 * The mark half again the text's height, centered on it: at the
		 * text's own height the 1 in repeat one and shuffle's crossing were
		 * too small to read at arm's length. Eric, 2026-09-19.
		 *
		 * Centered on the ink, not the em box: the box keeps a descender's
		 * depth below the baseline that "1 of 12" never uses, so its middle
		 * sat the mark 3.5px low on the device. On the capitals' middle,
		 * which is where ui_line_y puts them in the line. */
		int th = ui_font_line(UI_F_META), gs = th * 3 / 2, gx = NP_TX;
		/* No mark on a book: it plays in order, whatever the mode. */
		int g = mp4c_is_book() ? -1 : MP4_MODES[mp4c_mode()].glyph;

		ty = ui_line_y(UI_F_META, y);

		/* A book with chapters counts those instead of its one file. */
		if (mp4c_is_book() && mp4c_chapters() > 1) {
			snprintf(line, sizeof line, T(S_MP4_CHAPTER_OF),
			         mp4c_chapter_now() + 1, mp4c_chapters());
			gx += ui_text(r, fs, line, NP_TX, ty, -1, UI_TEXT_DIM) + 18;
		} else if (mn->count > 1) {
			snprintf(line, sizeof line, "%d / %d", mn->index + 1, mn->count);
			/* Not tabular: it changes with the track, not every second,
			 * and a 1 in a fixed-width cell read as "1 of  14". */
			gx += ui_text(r, fs, line, NP_TX, ty, -1, UI_TEXT_DIM) + 18;
		}
		if (g >= 0) ui_glyph_draw(r, (ui_glyph)g, gx + gs / 2, y + th * 2 / 5, gs, acc);
		/* A book's speed where music's mode mark would be, and only when it
		 * is not the ordinary one. */
		if (mp4c_is_book() && fabs(mp4c_speed() - 1.0) > 0.01) {
			snprintf(line, sizeof line, "%gx", mp4c_speed());
			ui_text(r, fs, line, gx, ty, -1, acc);
		}
	}
	/* Room under it for the larger mark and for the title to stand clear of
	 * the line above - which read as one block with it at 6px. */
	y += ui_font_line(UI_F_META) + 24;
	{
		/* A chapter with a title of its own is what is playing; one that
		 * is only a number leaves the book's title there. */
		char chap[128];
		const char *top = mn->title;

		if (mp4c_is_book() && mp4c_chapters() > 1) {
			int ci = mp4c_chapter_now() < 0 ? 0 : mp4c_chapter_now();

			/* The book's own "Chapter 3" is only a number too */
			chapter_label(ci, chap, sizeof chap);
			if (chapter_titled(ci) && strncmp(chap, "Chapter ", 8) != 0) top = chap;
		}
		np_line(r, UI_F_TITLE, top, y, phase, UI_TEXT, &wait);
	}
	y += ui_font_line(UI_F_TITLE) + 10;
	np_line(r, UI_F_MENU, mn->artist, y, phase, UI_TEXT_SOFT, &wait);
	y += ui_font_line(UI_F_MENU);
	np_line(r, UI_F_MENU, mn->album, y, phase, UI_TEXT_DIM, &wait);

	/* Where in the track. What is left rather than the whole length on the
	 * right, because the question is usually how long until the next one. */
	bar = NP_Y + NP_SIDE - 132;
	fill = (int)(NP_TW * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	ds_bar_in(r, &(SDL_Rect){ NP_TX, bar, NP_TW, UI_BAR_H }, fill, ui_col(theme()->track, 255),
	          state ? ui_fade(acc, 0.45f) : acc);
	/* Tabular, so the clock ticking does not move what sits beside it. */
	mmss(t0, sizeof t0, mn->at);
	ty = ui_line_y(UI_F_META, bar + 16);
	left = NP_TX + ui_text_tabular(r, fs, t0, NP_TX, ty, -1, UI_TEXT_SOFT);
	right = NP_TX + NP_TW;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, NP_TX + NP_TW, ty, 1, UI_TEXT_SOFT);
	}
	/* Between the two times rather than on the column's middle, which a book's
	 * hours pushed them into: "16:54:47Stopped". */
	if (state) ui_text(r, fs, state, (left + right) / 2, ty, 0, acc);

	/* The last line, its line ending where the cover does. */
	if (next && next[0]) {
		snprintf(line, sizeof line, T(S_MP4_NEXT), next);
		ui_fit_text(fs, line, fit, sizeof fit, NP_TW);
		ui_text(r, fs, fit, NP_TX,
		        ui_line_y(UI_F_META, NP_Y + NP_SIDE - ui_font_line(UI_F_META)), -1,
		        UI_TEXT_DIM);
	}

	np_hints(r, state == NULL, mp4c_is_book() ? 1 : 0, 0, 0, false, 255);
	{
		/* The top bar, as on every MP4 screen (2026-11-09): the clock at
		 * the left, the marks and the battery's words at the right */
		unsigned corner = draw_topbar(r, theme_index(), UI_TEXT_SOFT,
		                              wait <= 50 || frames_wanted());
		if (corner < wait) wait = corner;
	}
	return wait;
}

/* Now Playing in the look that is on (MENU > Player Look, DESIGN.md 11.64):
 * Cover is np_draw above, as it always was; the others are
 * src/nowplaying_looks.c's. A book and a video are always Cover's. */
unsigned np_draw_now(app *a, const mu_now *mn, const char *next)
{
	mp4l_id look = np_look_for();

	return look == MP4L_COVER ? np_draw(a, mn, next) : npl_draw(a, look, mn);
}

/* ---- MP4: the lock screen -------------------------------------------------
 *
 * mine, 2026-11-05, in place of MP4's pocket lock (#51, Eric's design,
 * 2026-09-30), whose chord - F1+F2 held for a second, FUNCTION and both volume
 * keys on the Pixel 2 - it replaced. X on any MP4 screen while something
 * plays, SELECT and START pressed together there (2026-11-09), or MENU's Lock
 * Screen, locks. While locked every button but the volume keys is blocked,
 * POWER included, and SELECT and START held together for hold-unlock unlock:
 * the rules are src/mp4lock.h's, and why those.
 *
 * The lock screen - Now Playing under the scrim, and over it a panel saying
 * it is locked and how to open it, with a bar the hold fills - shows for
 * hold-lock-lit as it locks, again for as long at every press of a blocked
 * button (2026-11-09), and while the pair is held. Music: otherwise dark, the
 * backlight out (My pick, "dark until unlocked") - a book playing drew about
 * 470 mA with it on and 300 with it off, measured before #51 was built -
 * except the volume keys, which
 * light the lock screen with the volume bar over it for as long as the bar
 * shows, and then let it go dark again. A video keeps playing in sight,
 * since that is what it was locked to watch, and the volume bar comes over
 * the picture alone.
 *
 * A headset's play, pause and skip still work: mp4c_poll reads them, and it
 * runs here as everywhere. Auto Off still powers off a locked player once the
 * music has stopped long enough - it is no button. */

static bool g_lock_asked;                /* MENU's Lock Screen, for mp4_menu */

void mp4_ask_lock(void) { g_lock_asked = true; }

/* QUIT: MP4's MENU, its last row or X on any row, as the in-game menu's Quit
 * and its X (2026-11-18). MP4 closes as SELECT closes it - MP4_CLOSE up
 * through every screen of it to mp4_shelf_screen, which puts back what it
 * was opened over: the home, a menu, a paused game's menu. What plays keeps
 * playing, as it does after SELECT; A pauses it first for silence. */
static bool g_quit_asked;                /* MENU's Quit, for mp4_menu */

void mp4_ask_quit(void) { g_quit_asked = true; }

/* SHUFFLE ALL: every song on the card in a random order, from MENU on the
 * music shelf. mine, 2026-11-05: shuffle across the whole library, not one
 * album at a time. Music only - a book shuffled is not how anyone listens to
 * one, and a film is not a song - Singles with the rest.
 *
 * The mode becomes shuffle and is kept, as with Y on Now Playing: shuffle is
 * a switch on every player that has one, and the next album played is
 * shuffled too until Y moves it on. One queue of every track, so "3 / 1200"
 * counts the songs heard, and Now Playing names each song's own album and
 * artist (mp4_track_names) where an album's queue names its one. */
static bool g_shuffle_asked;             /* MENU's Shuffle All, for mp4_menu */

void mp4_ask_shuffle(void) { g_shuffle_asked = true; }

static bool mp4_shuffle_all(void)
{
	const char **paths = calloc((size_t)(g_mp4.ntracks > 0 ? g_mp4.ntracks : 1), sizeof *paths);
	int i, t, n = 0;

	if (!paths) return false;
	for (i = 0; i < g_mp4.nalbums; i++)
		if (g_mp4.albums[i].kind == ML_MUSIC)
			for (t = 0; t < g_mp4.albums[i].n; t++)
				paths[n++] = g_mp4.tracks[g_mp4.albums[i].first + t].path;
	if (n > 0) {
		mp4c_set_mode(MUQ_SHUFFLE);
		db_set_str(db_dev(), "mp4.mode", MP4_MODES[MUQ_SHUFFLE].key);
		/* Shuffled, the start plays first and the rest follow in a random
		 * order (mp4queue.h), so the start is drawn at random too. */
		mp4_play(paths, n, (int)((plat_now_ms() * 2654435761u >> 7) % (unsigned)n), 0,
		          false, 1.0, NULL, NULL);
		fprintf(stderr, "mp4: shuffle all, %d songs\n", n);
	}
	free(paths);
	return n > 0;
}

/* Whether a lock can happen here: something playing, and MP4 not opened over
 * a paused game, whose power rule belongs to the game. */
bool lock_allowed(void)
{
	return mp4c_playing() && !g_mp4_over;
}

/* SELECT and START on an MP4 screen, as mp4chord_step reads them (the rule
 * and why is src/mp4lock.h's): what this pass's press of the two came to.
 * Every MP4 loop reads its buttons with mp4_input, which says it, and asks
 * mp4_select for SELECT rather than its press. */
static mp4chord  g_mp4_chord;
static mp4c_said g_mp4_said;

void mp4_input(app *a)
{
	plat_input_poll(&a->in);
	g_mp4_said = mp4chord_step(&g_mp4_chord, a->in.down, a->in.pressed);
}

/* SELECT alone, let go: MP4 closes */
bool mp4_select(void) { return g_mp4_said == MP4C_SELECT; }

/* START alone, let go: the screen turns - the shelf's View, Now Playing's
 * Player Look (DESIGN.md 11.67) */
bool mp4_start(void) { return g_mp4_said == MP4C_START; }

/* A screen of MP4's coming up: a SELECT down already is not its own */
void mp4_input_reset(void)
{
	g_mp4_chord = (mp4chord){ 0 };
	g_mp4_said = MP4C_NONE;
}

/* The volume and brightness keys on an MP4 screen, X, and SELECT with START.
 * True when the screen should lock. */
bool mp4_keys(app *a)
{
	in_state *in = &a->in;

	levels_keys(in, BOOST_OPEN);
	if (in->pressed[IN_X] || g_mp4_said == MP4C_LOCK) {
		if (lock_allowed()) { uisound_play(DS_SOUND_ACCEPT); return true; }
		uisound_play(DS_SOUND_DENY);       /* nothing playing to lock over */
	}
	return false;
}

/* The padlock, drawn: a ring for the shackle and a rounded body over its
 * lower half. On a panel of `ground`, which the ring is cut out with. */
#define LOCK_MARK 84
static void lock_mark(SDL_Renderer *r, int cx, int top, SDL_Color ink, SDL_Color ground)
{
	int s = LOCK_MARK, sw = s * 46 / 100, t = s / 9;
	SDL_Rect shackle = { cx - sw / 2, top, sw, s * 62 / 100 };
	SDL_Rect hole = { shackle.x + t, shackle.y + t, sw - 2 * t, shackle.h };
	SDL_Rect body = { cx - s * 36 / 100, top + s * 38 / 100, s * 72 / 100, s * 62 / 100 };

	ui_round_rect(r, &shackle, sw / 2, ink);
	ui_round_rect(r, &hole, hole.w / 2, ground);
	ui_round_rect(r, &body, DS_RADIUS_ROW * s / 96, ink);
}

/* "Hold (SELECT) + (START) to unlock" is three pieces - the words before the
 * pair, the pair drawn as buttons (DESIGN.md 11.9), the words after - and a
 * row of the lock panel is some of them in that order, centered. */
enum { LOCK_HOLD = 1, LOCK_PAIR = 2, LOCK_TAIL = 4 };
#define LOCK_GLYPH 52            /* the buttons' height, and a row's */
#define LOCK_GAP   14            /* between the words and the pair */

static int lock_row_width(TTF_Font *f, const char *hold, const char *tail, int parts)
{
	int w = 0;

	if ((parts & LOCK_HOLD) && hold[0]) w += ui_text_width(f, hold);
	if (parts & LOCK_PAIR) {
		if (w) w += LOCK_GAP;
		w += ui_button_width("SELECT", LOCK_GLYPH) + 12 + ui_text_width(f, "+") + 12 +
		     ui_button_width("START", LOCK_GLYPH);
	}
	if ((parts & LOCK_TAIL) && tail[0]) w += (w ? LOCK_GAP : 0) + ui_text_width(f, tail);
	return w;
}

static void lock_row(SDL_Renderer *r, TTF_Font *f, const char *hold, const char *tail,
                     int parts, int cx, int y, SDL_Color dim)
{
	int x = cx - lock_row_width(f, hold, tail, parts) / 2, gs = LOCK_GLYPH;
	int ty = ui_line_y(UI_F_META, y + (gs - ui_font_line(UI_F_META)) / 2), mid = y + gs / 2;
	bool any = false;

	if ((parts & LOCK_HOLD) && hold[0]) { x += ui_text(r, f, hold, x, ty, -1, dim); any = true; }
	if (parts & LOCK_PAIR) {
		if (any) x += LOCK_GAP;
		ui_button(r, "SELECT", x + ui_button_width("SELECT", gs) / 2, mid, gs, UI_BUTTON_PLAIN);
		x += ui_button_width("SELECT", gs) + 12;
		x += ui_text(r, f, "+", x, ty, -1, dim) + 12;
		ui_button(r, "START", x + ui_button_width("START", gs) / 2, mid, gs, UI_BUTTON_PLAIN);
		x += ui_button_width("START", gs);
		any = true;
	}
	if ((parts & LOCK_TAIL) && tail[0]) ui_text(r, f, tail, x + (any ? LOCK_GAP : 0), ty, -1, dim);
}

/* The panel over the dimmed screen: the mark, "Locked", how to open it, and
 * the bar the hold fills. In the theme's colors, over a video or not.
 *
 * How to open it is one row where it fits the panel, and two where it does
 * not - mine, 2026-11-09: in Portuguese "Segure SELECT + START para
 * desbloquear" ran out of the panel on both sides, and so did Spanish,
 * French and Italian. Broken where the two rows come out the most even:
 * "Segure SELECT + START" over "para desbloquear". The panel is 720 wide
 * and as tall as its rows, and wider only for a row no break makes fit
 * (none of the eight languages has one). check-i18n-shots holds it to its
 * box in every language (mp4-locked). */
#define LOCK_PANEL_W   720
#define LOCK_PANEL_PAD 48        /* the words' air at each side */

void lock_panel(app *a, const mp4lock *l)
{
	SDL_Renderer *r = a->r;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_META);
	SDL_Color ink = ui_col(theme()->accent_ink, 255), text = UI_TEXT, dim = UI_TEXT_DIM;
	SDL_Color panel = ui_col(theme()->surface, 255);
	/* "Hold %s to unlock": the words either side of the pair, which is
	 * drawn, their spaces off */
	const char *fmt = T(S_MP4_HOLD_UNLOCK), *at = strstr(fmt, "%s");
	const char *tail = at ? at + 2 : "";
	char hold[64];
	int hn = at ? (int)(at - fmt) : (int)strlen(fmt);
	int rows[2], nrows = 1, widest, pw, ph, px, py, y, cx = ZYPEROS_SCREEN_W / 2, i;
	float k = mp4lock_progress(l);

	while (hn > 0 && fmt[hn - 1] == ' ') hn--;
	snprintf(hold, sizeof hold, "%.*s", hn, fmt);
	while (*tail == ' ') tail++;

	rows[0] = LOCK_HOLD | LOCK_PAIR | LOCK_TAIL;
	widest = lock_row_width(fm, hold, tail, rows[0]);
	if (widest > LOCK_PANEL_W - 2 * LOCK_PANEL_PAD) {
		/* The words before over the pair and the rest, or the words before
		 * and the pair over the rest: whichever is the narrower */
		int a1 = lock_row_width(fm, hold, tail, LOCK_HOLD | LOCK_PAIR);
		int a2 = lock_row_width(fm, hold, tail, LOCK_TAIL);
		int b1 = lock_row_width(fm, hold, tail, LOCK_HOLD);
		int b2 = lock_row_width(fm, hold, tail, LOCK_PAIR | LOCK_TAIL);
		int wa = a1 > a2 ? a1 : a2, wb = b1 > b2 ? b1 : b2;

		if (tail[0] && (!hold[0] || wa <= wb)) {
			rows[0] = LOCK_HOLD | LOCK_PAIR;
			rows[1] = LOCK_TAIL;
			widest = wa;
			nrows = 2;
		} else if (hold[0]) {
			rows[0] = LOCK_HOLD;
			rows[1] = LOCK_PAIR | LOCK_TAIL;
			widest = wb;
			nrows = 2;
		}
	}
	pw = widest + 2 * LOCK_PANEL_PAD > LOCK_PANEL_W ? widest + 2 * LOCK_PANEL_PAD : LOCK_PANEL_W;
	if (pw > ZYPEROS_SCREEN_W - 2 * DS_INSET) pw = ZYPEROS_SCREEN_W - 2 * DS_INSET;
	/* 40 above the mark, 20 under it and under the title, the rows 8 apart,
	 * 28 to the bar and 40 under it: 360 with one row, as it always was */
	ph = 40 + LOCK_MARK + 20 + ui_font_line(UI_F_TITLE) + 20 + nrows * LOCK_GLYPH +
	     (nrows - 1) * 8 + 28 + UI_BAR_H + 40;
	px = (ZYPEROS_SCREEN_W - pw) / 2;
	py = (ZYPEROS_SCREEN_H - ph) / 2;
	y = py + 40;

	ui_panel(r, &(SDL_Rect){ px, py, pw, ph }, DS_RADIUS_PANEL);
	lock_mark(r, cx, y, ink, panel);
	y += LOCK_MARK + 20;
	ui_text(r, ft, T(S_MP4_LOCKED), cx, ui_line_y(UI_F_TITLE, y), 0, text);
	y += ui_font_line(UI_F_TITLE) + 20;
	for (i = 0; i < nrows; i++) {
		lock_row(r, fm, hold, tail, rows[i], cx, y, dim);
		y += LOCK_GLYPH + (i + 1 < nrows ? 8 : 0);
	}
	y += 28;
	/* The hold, as the one bar every hold in the system is (UI_BAR_H). */
	{
		ds_bar_in(r, &(SDL_Rect){ cx - 200, y, 400, UI_BAR_H }, (int)(400 * k + 0.5f), ui_col(theme()->track, 255),
		          ink);
	}
}

static void np_next_name(char *next, size_t n);

/* Locked, until SELECT and START are held, or Auto Off. See above. */
void mp4_locked(app *a)
{
	mp4lock l = { 0 };
	bool panel_was = true, dark = false;
	unsigned shown_serial = 0;
	int drawn_k = -1, drawn_sec = -1;
	Uint32 osd_due = UINT32_MAX;            /* the volume bar's next look */

	fprintf(stderr, "mp4: locked\n");
	/* Under the lock nothing moves: a look's bars rest, and with no frame
	 * of bars the daemon stops measuring (mp4c_bands_keep) */
	npl_still(true);
	/* Held as long as MENU > System > Hotkeys' Hold to Unlock says */
	l.hold_ms = mk_now()->hold_ms;
	mp4lock_engage(&l, plat_now_ms(), a->in.down[MP4_UNLOCK_A] && a->in.down[MP4_UNLOCK_B]);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!want_quit && a->running) {
		bool video, lit, awake, panel, fresh = false, vol = false;
		const mu_now *mn;

		mp4_screen_poll();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		/* Auto Off, once the music has stopped long enough: no button, so
		 * the lock does not stand in its way. The light first, so the
		 * power-off animation is seen. */
		if (idle_due(a)) {
			plat_backlight_dim(false);
			plat_backlight(true);
			mp4_power(a);
			break;
		}
		if (mp4lock_step(&l, plat_now_ms(), a->in.down, a->in.pressed)) break;
		/* The volume keys, the two that get through (mp4lock_passes): the
		 * same steps and the same bar as unlocked, repeating when held. */
		if (levels_keys(&a->in, BOOST_FILL)) vol = true;
		mn = mp4c_now();
		video = mp4c_video();
		lit = mp4lock_lit(&l);
		awake = mp4lock_awake(&l);
		/* The lock panel: over music whenever the screen is on, the volume
		 * bar over it; over a video only as it locks and while the pair is
		 * held, so a volume press brings the bar alone over the picture. */
		panel = video ? lit : awake;
		mp4c_video_show(video);
		if (video) fresh = mp4v_update(a->r);

		if (!video && !awake) {
			/* Dark: nothing drawn, a present would be work for nobody. */
			if (!dark) { plat_backlight(false); dark = true; }
			panel_was = false;
			osd_due = UINT32_MAX;
			SDL_Delay(30);
			continue;
		}
		/* Drawn for a new picture, the ring moving, the light coming on, a
		 * volume step or the bar moving, or a new second on the clock - not
		 * every pass of a lit screen. */
		if (fresh || vol || panel != panel_was || shown_serial == 0 ||
		    (osd_due != UINT32_MAX && (int)(plat_now_ms() - osd_due) >= 0) ||
		    (int)(mp4lock_progress(&l) * 100) != drawn_k || (int)(mn->at) != drawn_sec) {
			char next[128] = "";

			tick_theme(a);
			if (video) {
				np_draw_video(a, mn, 0, 1);
			} else {
				np_next_name(next, sizeof next);
				np_draw_now(a, mn, next);
			}
			if (panel) {
				ui_scrim(a->r, 0);
				lock_panel(a, &l);
			}
			/* Asked after the draw, which retires a bar whose time is up. */
			plat_draw_osd(a->r);
			osd_due = plat_osd_until();
			plat_present();
			shown_serial++;
			drawn_k = (int)(mp4lock_progress(&l) * 100);
			drawn_sec = (int)mn->at;
			/* The light after the frame, so it comes on to the lock screen
			 * rather than to whatever the panel held last; and at the lowest
			 * rung, since a press in a pocket or in the dark is what wakes
			 * it (2026-11-09). Unlocking puts the level back. */
			if (dark) { plat_backlight_dim(true); plat_backlight(true); dark = false; }
		}
		panel_was = panel;
		SDL_Delay(video || awake ? 8 : 30);
	}
	mp4c_video_show(false);
	npl_still(false);
	plat_backlight_dim(false);
	plat_backlight(true);
	fprintf(stderr, "mp4: unlocked\n");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	mp4_input_reset();
}

/* MENU, from any MP4 screen: MP4's own menu, the one its shelf has always
 * had. One button, one meaning, wherever in MP4 it is pressed - it used to
 * close MP4 from Now Playing and the tracks, and open this on the shelf.
 * Eric's call, 2026-09-23.
 *
 * Rescan Folder inside it rebuilds the library, and every album index with it,
 * so a screen holding one cannot carry on: MP4_REBUILT tells it to let go,
 * and the shelf finds itself again the way it does after its own MENU. */
static int g_np_depth;                   /* Now Playing is open, under whatever is */

mp4_exit mp4_menu(app *a)
{
	unsigned gen = g_mp4_gen;
	bool by_menu;

	zyperos_menu_for(a, SCREEN_GAMES);
	/* MENU inside it closed it, and only it: MP4 is still up, and may itself
	 * be over a menu SELECT opened it from, which has to stay open. So the
	 * screen under it comes back as after B. */
	by_menu = g_menu_closing;
	g_menu_closing = false;
	if (by_menu) panel_return();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* Lock Screen closed the menu to lock from the screen underneath. */
	if (g_lock_asked) {
		g_lock_asked = false;
		if (lock_allowed()) mp4_locked(a);
	}
	if (want_quit || !a->running || g_message_stopped) return MP4_CLOSE;
	if (g_quit_asked) {
		g_quit_asked = false;
		return MP4_CLOSE;
	}
	/* Shuffle All closed it to play: Now Playing, on whatever screen of
	 * MP4's MENU was pressed - on Now Playing itself, it just carries on
	 * with the new song (g_np_depth). */
	if (g_shuffle_asked) {
		g_shuffle_asked = false;
		if (mp4_shuffle_all() && !g_np_depth) {
			mp4_exit how = mp4_now_screen(a);

			if (how != MP4_BACK) return how;
		}
	}
	return g_mp4_gen != gen ? MP4_REBUILT : MP4_BACK;
}

/* What the mode will actually play next, for "Next:". Repeat one says so
 * with its mark, and "Next:" naming the same song reads as a mistake. In a
 * book with chapters, the next chapter, until the last. */
static void np_next_name(char *next, size_t n)
{
	const mu_now *mn = mp4c_now();
	const char *nt;

	next[0] = '\0';
	if (mp4c_is_book() && mp4c_chapters() > 1 &&
	    mp4c_chapter_now() + 1 < mp4c_chapters())
		chapter_label(mp4c_chapter_now() + 1, next, n);
	else if ((mp4c_is_book() || mp4c_is_videos() || mp4c_mode() != MUQ_REPEAT_ONE) &&
	         (nt = mp4c_upcoming()))
		ml_track_name(strrchr(nt, '/') ? strrchr(nt, '/') + 1 : nt, next, (int)n);
	(void)mn;
}

/* ---- MP4: video -----------------------------------------------------------
 *
 * A file with a picture plays on the same Now Playing, in one view, as a
 * phone's or a TV's player does it (2026-11-15; there were two before, a
 * full screen and one with the picture smaller and its controls under it,
 * which Y turned between):
 *
 *   the picture   as wide as the screen at 16:9, under the header's band
 *                 (the clock, the title, the marks), the same in every
 *                 language: nothing of it is given up for the controls
 *   the controls  over the picture's foot on the theme's surface: the bar
 *                 with the time played and the time left at its two ends,
 *                 and the keys under it, wrapping onto a second row where a
 *                 language needs it, which only makes the band taller. Up
 *                 for hold-controls after any key and for as long as it is
 *                 paused, fading in over quick and out over fade-out; every
 *                 key also does what it does, the first one too. Y shows
 *                 them, or hides them at once
 *   the header    goes and comes with the controls, at the same pace, and
 *                 the ground around the picture goes to pure black with it
 *                 (2026-11-18: "when the footer times out and disappears
 *                 while video is playing, the top bar should too, and the
 *                 background should go pitch black"): hidden, there is
 *                 the picture and black, as a TV's player has it. A title
 *                 wider than the header's row slides there, as a long name
 *                 does everywhere (ui_text_marquee), from its start each
 *                 time the header comes back
 *   full screen   with the surround gone, the picture grows to the whole
 *                 screen, its shape kept, black only where its shape is not
 *                 the screen's: a 4:3 film fills the Pixel 2 (2026-11-19:
 *                 "if we could make the video go full size when the
 *                 surround collapses that's better"). Over turn, after the
 *                 surround has gone; back to its box over quick, before
 *                 the surround comes back, so the header never stands over
 *                 the picture. A 16:9 film is the same size either way (its
 *                 box under the header is the screen's middle) and does not
 *                 wait for it
 *   paused        a play disc in the picture's middle (ds_play_disc), the
 *                 bar's fill faded
 *
 * The picture itself is the GPU's: the daemon's newest frame uploaded as YUV
 * (src/mp4video.c) and drawn UNDER the screen at the panel's own resolution
 * (plat_video_under), so on the Pixel 2 a 640x360 video is pixel for pixel.
 * The screen is presented when a new frame arrives - the video's own rate,
 * 24 to 30 times a second - and otherwise when the controls change. */

/* The player's column: the margins its words keep, and the header's band
 * the picture starts under */
#define NPV_X 64
#define NPV_Y 96

/* The band: its top to the bar's row (a section), the bar's row, and the
 * row to the keys; all on the 8-unit grid */
#define NPV_BAND_PAD 24
#define NPV_BAR_ROW  32
#define NPV_BAR_GAP  16

/* What the buttons do on Now Playing, listed most needed first: A, B, X,
 * then Y and L1/R1, then the d-pad. `kind`: 0 music, 1 a book. On a video
 * Y is Repeat (MP4's MENU > Repeat: Off, One, All), its mark beside the
 * time when it is not Off. */
int np_hint_list(key_hint *h, bool playing, int kind, int count, bool video)
{
	int n = 0;

	h[n].key = "A";  h[n++].label = playing ? T(S_MP4_HINT_PAUSE) : T(S_MP4_HINT_PLAY);
	h[n].key = "B";  h[n++].label = T(S_CONTROLS_BACK);
	h[n].key = "X";  h[n++].label = T(S_MP4_HINT_LOCK);
	if (video) {
		h[n].key = "Y"; h[n++].label = T(S_SYSMENU_MP4_REPEAT);
		if (count > 1) { h[n].key = "L1/R1"; h[n++].label = T(S_MP4_HINT_VIDEO); }
	} else {
		h[n].key = "Y";     h[n++].label = kind == 1 ? T(S_MP4_HINT_SPEED) : T(S_MP4_HINT_MODE);
		h[n].key = "L1/R1"; h[n++].label = !mp4c_is_book() ? T(S_MP4_HINT_TRACK)
		                                  : mp4c_chapters() > 1 ? T(S_MP4_HINT_CHAPTER)
		                                                         : T(S_MP4_HINT_FILE);
	}
	h[n].key = KEYHINT_LEFT_RIGHT; h[n++].label = T(S_MP4_HINT_SEEK);
	return n;
}

/* The foot of the music view: the hints as the shelves' hints are drawn
 * (page_hint: a glyph and a word), spread from x 64 to 960, a row that does
 * not fit its language's words going on to a second one above it, never cut
 * (DESIGN.md 12). `cy` is the last row's middle (0 for a shelf's foot,
 * DS_FOOT_Y, every screen's), `alpha` how much of it shows. */
static void np_hints(SDL_Renderer *r, bool playing, int kind, int count, int cy,
                     bool video, Uint8 alpha)
{
	key_hint h[KEYHINTS_MAX];
	int n = np_hint_list(h, playing, kind, count, video);

	if (!cy) cy = DS_FOOT_Y;
	keyhints_draw(r, h, n, NPV_X, ZYPEROS_SCREEN_W - NPV_X, cy, KEYHINTS_SPREAD, true, alpha);
}

/* The bar from (x, y), `w` wide, in alpha `a`: its fill faded while paused
 * or stopped */
static void np_video_bar(SDL_Renderer *r, const mu_now *mn, int x, int y, int w, Uint8 a)
{
	SDL_Color acc = ui_col(theme()->accent_ink, a);
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	int fill = (int)(w * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);

	ds_bar_in(r, &(SDL_Rect){ x, y, w, UI_BAR_H }, fill, ui_col(theme()->track, a),
	          mn->state != MU_PLAYING ? ui_fade(acc, 0.45f) : acc);
}

/* The picture under the header, the screen's width at 16:9, and no taller
 * than the room under the header: on a 16:9 canvas the picture is the whole
 * screen's, so under the header it is a little smaller, in the middle */
static SDL_Rect np_video_boxed(void)
{
	const int h = ZYPEROS_SCREEN_W * 9 / 16, room = ZYPEROS_SCREEN_H - NPV_Y;

	return (SDL_Rect){ 0, NPV_Y, ZYPEROS_SCREEN_W, h < room ? h : room };
}

/* Where the player's parts go, laid out by rules from the canvas and what
 * it holds (np_player_layout): no y is typed. */
typedef struct {
	SDL_Rect pic;      /* the picture's 16:9 box */
	SDL_Rect band;     /* the controls' surface, from the bar's row to the screen's foot */
	SDL_Rect bar;      /* the bar between the two times */
	SDL_Rect t0, t1;   /* the time played, the time left */
	SDL_Rect mark;     /* Repeat's mark, before the time left; w 0 for none */
	int keys_cy;       /* the keys' last row's middle */
} np_player;

/* Repeat's mark in the bar's row (MP4's MENU > Repeat): music's marks at
 * music's size, half again the times' line, so repeat one's 1 reads at arm's
 * length as it does beside music's count */
#define NPV_MARK (ui_font_line(UI_F_META) * 3 / 2)

static void np_player_layout(np_player *L, const key_hint *h, int n, const char *t0, const char *t1,
                             bool mark)
{
	static ds_flex F;
	TTF_Font *fs = ui_font(UI_F_META);
	int root, band, row, keys, bar, c0, c1 = -1, mk = -1, i;

	/* The picture: the canvas's width at 16:9, under the header's band. It
	 * stands apart: the band floats over its foot and never moves it. */
	L->pic = np_video_boxed();
	/* The band, laid out by rules (src/ds/flex.h) from the screen's foot up:
	 * the keys on every screen's foot, as many rows as their words take,
	 * spread as keyhints_draw spreads them; the bar's row over them, the
	 * time played and the time left at its two ends and the bar between;
	 * and the band as tall as they need, which only grows over the picture */
	root = ds_flex_begin(&F, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
	                     &(ds_box){ .dir = DS_COL, .justify = DS_J_END });
	band = ds_flex_add(&F, root, &(ds_box){ .dir = DS_COL, .gap = NPV_BAR_GAP,
	                                        .pad = { NPV_X, NPV_BAND_PAD, NPV_X, DS_FOOT_INSET } });
	row = ds_flex_add(&F, band, &(ds_box){ .dir = DS_ROW, .h = NPV_BAR_ROW, .gap = NPV_BAR_GAP,
	                                       .align = DS_A_CENTER });
	c0 = ds_flex_add(&F, row, &(ds_box){ .w = ui_text_tabular_width(fs, t0), .h = NPV_BAR_ROW });
	bar = ds_flex_add(&F, row, &(ds_box){ .h = UI_BAR_H, .grow = 1 });
	if (mark) mk = ds_flex_add(&F, row, &(ds_box){ .w = NPV_MARK, .h = NPV_BAR_ROW });
	if (t1[0]) c1 = ds_flex_add(&F, row, &(ds_box){ .w = ui_text_tabular_width(fs, t1), .h = NPV_BAR_ROW });
	keys = ds_flex_add(&F, band, &(ds_box){ .dir = DS_ROW, .wrap = true, .gap = KEYHINTS_GAP_SPREAD,
	                                        .line_gap = KEYHINTS_ROW - DS_HINT_GLYPH });
	for (i = 0; i < n; i++)
		ds_flex_add(&F, keys, &(ds_box){ .w = keyhints_width(h[i].key, h[i].label), .h = DS_HINT_GLYPH });
	if (n == 0) ds_flex_add(&F, keys, &(ds_box){ .h = DS_HINT_GLYPH });
	ds_flex_solve(&F);
	L->band = ds_flex_rect(&F, band);
	L->t0 = ds_flex_rect(&F, c0);
	L->bar = ds_flex_rect(&F, bar);
	L->t1 = c1 >= 0 ? ds_flex_rect(&F, c1)
	                : (SDL_Rect){ ZYPEROS_SCREEN_W - NPV_X, L->t0.y, 0, NPV_BAR_ROW };
	L->mark = mk >= 0 ? ds_flex_rect(&F, mk) : (SDL_Rect){ 0, 0, 0, 0 };
	L->keys_cy = ds_flex_rect(&F, keys).y + ds_flex_rect(&F, keys).h - DS_HINT_GLYPH / 2;
}

/* The video's title in the header's row, between the clock and the marks, in
 * the clock's face: centered when it fits, else sliding in that room on
 * Now Playing's clock (MQ_NP, which the music view keeps and a video does
 * not share a frame with). How long until it moves by itself: 0 while it
 * slides, the rest of a hold at one end, never for one that fits. */
static unsigned np_video_title(SDL_Renderer *r, const char *title, int index)
{
	TTF_Font *fh = page_font(PAGE_ACTION);
	int half = header_half(), room = 2 * half, tw = ui_text_width(fh, title);
	int y = ui_cap_y(fh, TOPBAR_MID);
	unsigned phase;

	if (tw <= room) {
		ui_text(r, fh, title, ZYPEROS_SCREEN_W / 2, y, 0, UI_TEXT);
		return (unsigned)-1;
	}
	phase = mq_phase(MQ_NP, index, (int)strlen(title));
	ui_text_marquee(r, fh, title, ZYPEROS_SCREEN_W / 2 - half, y, room, phase, UI_TEXT);
	return ui_pingpong_wait(tw - room, phase);
}

/* The picture's box `grow` of the way from its place under the header to the
 * whole screen, on the turn curve, and the picture fitted in each */
static SDL_Rect np_video_box(const SDL_Rect *boxed, float grow, SDL_Rect *pic)
{
	SDL_Rect full = { 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H };
	SDL_Rect a = mp4v_fit(*boxed), b = mp4v_fit(full);
	float e = ds_turn(grow);
#define NPV_LERP(p, q) ((int)lroundf((p) + ((q) - (p)) * e))
	if (pic) *pic = (SDL_Rect){ NPV_LERP(a.x, b.x), NPV_LERP(a.y, b.y), NPV_LERP(a.w, b.w), NPV_LERP(a.h, b.h) };
	return (SDL_Rect){ NPV_LERP(boxed->x, full.x), NPV_LERP(boxed->y, full.y),
	                   NPV_LERP(boxed->w, full.w), NPV_LERP(boxed->h, full.h) };
#undef NPV_LERP
}

bool np_video_grows(void)
{
	SDL_Rect a, b, boxed = np_video_boxed();

	np_video_box(&boxed, 0, &a);
	np_video_box(&boxed, 1, &b);
	return !SDL_RectEquals(&a, &b);
}

/* A picture format's name as people know it, from FFmpeg's short one */
static void np_format_name(const char *ff, char *out, size_t n)
{
	static const struct { const char *ff, *name; } NAMES[] = {
		{ "h264", "H.264" }, { "hevc", "HEVC" }, { "av1", "AV1" }, { "vp8", "VP8" },
		{ "vp9", "VP9" }, { "mpeg4", "MPEG-4" }, { "mpeg2video", "MPEG-2" },
		{ "mpeg1video", "MPEG-1" }, { "prores", "ProRes" }, { "vc1", "VC-1" },
		{ "wmv3", "WMV" }, { "theora", "Theora" }, { "mjpeg", "Motion JPEG" },
	};
	size_t i;

	for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
		if (!strcmp(ff, NAMES[i].ff)) { snprintf(out, n, "%s", NAMES[i].name); return; }
	for (i = 0; i + 1 < n && ff[i]; i++) out[i] = (char)toupper((unsigned char)ff[i]);
	if (n) out[i] = 0;
}

/* What the daemon says of a picture this handheld cannot carry, in the
 * notice's slab (11.7) over the picture's foot, just over the band: what
 * the video is ("AV1 · 1920x1080 · 60 fps") in **meta** **text-dim** over
 * what that means in **label** **text**, on one line or two balanced, the
 * slab no wider than a menu panel. Too big (MPF_TOO_BIG) it comes and goes
 * with the controls at `al`; with no picture at all (MPF_NO_PICTURE) it
 * stands whole in the black, so the screen is never only black. */
static void np_video_notice(SDL_Renderer *r, const SDL_Rect *band, const mu_now *mn, Uint8 al)
{
	const ds_theme *t = theme();
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	const int padx = ui_font_line(UI_F_MENU) / 3, pady = ui_font_line(UI_F_MENU) / 6;
	const int l1 = ui_font_line(UI_F_META), l2 = ui_font_line(UI_F_LABEL);
	const int room = DS_PANEL_W - 2 * padx;
	char ff[16], name[24], head[96], lines[2][LB_LINE];
	const char *body, *nl;
	unsigned milli = 0;
	int trouble = mp4v_trouble(ff, sizeof ff, &milli), n, i, tw, w, h, x, y;

	if (trouble == MPF_FINE || !fh || !fb) return;
	if (trouble == MPF_NO_PICTURE) al = 255;
	if (al == 0) return;
	np_format_name(ff[0] ? ff : "?", name, sizeof name);
	n = snprintf(head, sizeof head, "%s", name);
	if (mn->vw > 0 && mn->vh > 0 && n > 0 && (size_t)n < sizeof head)
		n += snprintf(head + n, sizeof head - (size_t)n, " · %dx%d", mn->vw, mn->vh);
	if (milli > 0 && milli < 1000000 && n > 0 && (size_t)n < sizeof head) {
		double fps = milli / 1000.0;

		snprintf(head + n, sizeof head - (size_t)n, fabs(fps - round(fps)) < 0.01 ? " · %.0f fps" : " · %.2f fps", fps);
	}
	body = trouble == MPF_TOO_BIG ? T(S_MP4_TOO_BIG) : T(S_MP4_NO_PICTURE);
	ui_fit_drawn(body);
	if ((nl = strchr(body, '\n'))) {
		/* Chinese and Japanese say where their two lines part, which a
		 * break between any two characters would not */
		snprintf(lines[0], sizeof lines[0], "%.*s", (int)(nl - body), body);
		snprintf(lines[1], sizeof lines[1], "%s", nl + 1);
		n = 2;
	} else {
		n = ui_balance_lines(fb, body, room, 2, lines);
	}
	tw = ui_text_width(fh, head);
	for (i = 0; i < n; i++)
		if (ui_text_width(fb, lines[i]) > tw) tw = ui_text_width(fb, lines[i]);
	if (tw > room) tw = room;
	/* The notice's lines, and the quarter of the body's descender room taken
	 * off the bottom, as the menu music's pop-up has them */
	w = tw + 2 * padx;
	h = pady * 2 + l1 + n * l2 - l2 / 12;
	x = (ZYPEROS_SCREEN_W - w) / 2;
	y = band->y - DS_INSET - h;
	ui_round_rect(r, &(SDL_Rect){ x, y, w, h }, DS_RADIUS_PANEL, ui_col(t->surface, (Uint8)(217 * al / 255)));
	ui_text(r, fh, head, ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_META, y + pady), 0, ui_col(t->text_dim, al));
	for (i = 0; i < n; i++)
		ui_text(r, fb, lines[i], ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_LABEL, y + pady + l1 + i * l2), 0,
		        ui_col(t->text, al));
}

/* One frame of the video, the controls and the header at `k` (0 hidden, 1
 * shown), the ground around the picture the theme's at 1 and black at 0, and
 * the picture `grow` of the way to the whole screen (drawn only with the
 * header gone, k 0). The run loop keeps the controls up while paused; the
 * lock draws them hidden and the picture whole, since no key they name does
 * anything while it is locked. When the frame changes by itself, the title's
 * slide: 0 while it moves. */
unsigned np_draw_video(app *a, const mu_now *mn, float k, float grow)
{
	SDL_Renderer *r = a->r;
	SDL_Rect src;
	SDL_Texture *tex = mp4v_texture(&src);
	TTF_Font *fs = ui_font(UI_F_META);
	ds_rgb g = theme()->ground;
	SDL_Color soft = UI_TEXT_SOFT, bg;
	key_hint h[KEYHINTS_MAX];
	char t0[16], t1[24], left[16];
	np_player L;
	SDL_Rect pic, box;
	unsigned wait = (unsigned)-1;
	int n, mg;
	Uint8 al;

	k = k < 0 ? 0 : k > 1 ? 1 : k;
	al = (Uint8)(255 * k + 0.5f);
	/* The ground: the theme's with the controls, and pure black without, the
	 * one eased to the other as they come and go (2026-11-18) */
	bg = (SDL_Color){ (Uint8)(g.r * k + 0.5f), (Uint8)(g.g * k + 0.5f), (Uint8)(g.b * k + 0.5f), 255 };
	n = np_hint_list(h, mn->state == MU_PLAYING, 0, mn->count, true);
	mmss(t0, sizeof t0, mn->at);
	t1[0] = 0;
	if (mn->len > 0) {
		mmss(left, sizeof left, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(t1, sizeof t1, "-%s", left);
	}
	/* Repeat's mark (MP4's MENU > Repeat), music's own, when it is not Off */
	mg = MP4_MODES[mp4c_video_mode()].glyph;
	np_player_layout(&L, h, n, t0, t1, mg >= 0);

	/* The header stands over the box's band: the picture grows only once
	 * it has gone (the run loop's order; held here too) */
	if (al > 0) grow = 0;
	box = np_video_box(&L.pic, grow < 0 ? 0 : grow > 1 ? 1 : grow, &pic);
	plat_video_under(tex, &src, &pic, bg);
	ui_fit_frame();   /* the picture covers what was drawn (the host's fit check) */
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* Before the first picture: its box, empty, so nothing jumps. */
	if (!tex) ui_round_rect(r, &box, 0, (SDL_Color){ 0, 0, 0, 255 });

	/* The home's header, as on every MP4 screen (2026-11-09): the clock at
	 * the left, the marks at the right, and the title between them. It goes
	 * with the controls: above the picture's box there is only the ground,
	 * so the ground laid over it at what is gone of `k` fades all of it as
	 * one, on every device (over the Pixel 2's clear screen too: the blend
	 * is the same there, premultiplied), and hidden it is not drawn. */
	if (al > 0) {
		draw_topbar(r, theme_index(), UI_TEXT_SOFT, true);
		wait = np_video_title(r, mn->title, mn->index);
		if (al < 255)
			ui_round_rect(r, &(SDL_Rect){ 0, 0, ZYPEROS_SCREEN_W, L.pic.y }, 0,
			              (SDL_Color){ bg.r, bg.g, bg.b, (Uint8)(255 - al) });
	}

	if (mn->state != MU_PLAYING) {
		int d = 120;

		ds_play_disc(r, pic.x + (pic.w - d) / 2, pic.y + (pic.h - d) / 2, d, 255);
	}
	np_video_notice(r, &L.band, mn, al);
	if (al == 0) return wait;
	ui_round_rect(r, &L.band, 0, ui_col(theme()->surface, (Uint8)(al * 0.92f)));
	soft.a = al;
	np_video_bar(r, mn, L.bar.x, L.bar.y, L.bar.w, al);
	ui_text_tabular(r, fs, t0, L.t0.x, ui_cap_y(fs, L.t0.y + L.t0.h / 2), -1, soft);
	if (t1[0]) ui_text_tabular(r, fs, t1, L.t1.x + L.t1.w, ui_cap_y(fs, L.t1.y + L.t1.h / 2), 1, soft);
	if (L.mark.w > 0)
		ui_glyph_draw(r, (ui_glyph)mg, L.mark.x + L.mark.w / 2, L.mark.y + L.mark.h / 2, L.mark.w,
		              ui_col(theme()->accent_ink, al));
	keyhints_draw(r, h, n, NPV_X, ZYPEROS_SCREEN_W - NPV_X, L.keys_cy, KEYHINTS_SPREAD, true, al);
	return wait;
}

/* The tape's own click with the voice's sound, on the Cassette look alone
 * (my pick in the Tape Styles lab: a click on A and on a track change, no
 * hiss): heard as UI Sounds are, so Off is silent */
static void tape_click(ds_sound s, bool video)
{
	if (!video && np_look_for() == MP4L_CASSETTE) uisound_play(s);
}

/* MP4_CLOSE for SELECT and the power button, which leave MP4 altogether;
 * MP4_BACK for B, which is the album's tracks underneath; MP4_REBUILT when
 * MENU's Rescan Folder replaced the library. MENU otherwise comes back here. */
static mp4_exit mp4_now_screen_run(app *a)
{
	struct {
		mu_state st;
		int at, len, index, count, mode, chapter, speed;
		int battery_pct;
		bool charging, video;
		char title[128], artist[128], album[128];
		SDL_Texture *tex;
	} shown, drawn;
	unsigned due = 0, ctl_until = 0, ctl_at = 0;
	float ctl = 1, ctl_drawn = -1, grow = 0, grow_drawn = -1;
	int trouble_seen = MPF_FINE;
	mp4_exit how = MP4_BACK;

	mq_reset(MQ_NP);
	memset(&drawn, 0, sizeof drawn);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	mp4_input_reset();
	ctl_until = plat_now_ms() + DS_MS_HOLD_CONTROLS;
	ctl_at = plat_now_ms();

	while (!want_quit && a->running) {
		const mu_now *mn;
		char next[128] = "";
		unsigned now;
		bool touched = false, video, fresh = false;
		int b;

		mp4_screen_poll();
		cover_answers();
		mn = mp4c_now();
		video = mp4c_video();
		/* Pictures are decoded only while somebody looks at them. */
		mp4c_video_show(video);
		mp4_input(a);
		if (a->in.quit_requested) { a->running = false; how = MP4_CLOSE; break; }
		/* A plug on the USB port asks here too, as at the shelf: a DAC
		 * plugged in to listen gets its question (usb_questions) */
		if (usb_questions(a)) {
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
			mp4_input_reset();
			redraw_now();
			continue;
		}
		if (a->in.pressed[IN_POWER] || idle_due(a)) { mp4_power(a); how = MP4_CLOSE; break; }
		if (a->in.pressed[IN_BACK] || mp4_select()) uisound_play(DS_SOUND_BACK);
		if (a->in.pressed[IN_BACK]) break;
		if (mp4_select()) { how = MP4_CLOSE; break; }
		if (a->in.pressed[IN_MENU]) {
			mp4c_video_show(false);
			how = mp4_menu(a);
			if (how != MP4_BACK) break;
			memset(&drawn, 0, sizeof drawn);     /* the menu drew over it */
			continue;
		}

		/* A, and only A. X did the same thing here and pausing in the list,
		 * which made one button mean two things a screen apart; X is the lock
		 * now, on every MP4 screen. Eric's call, 2026-09-19; X, 2026-11-05. */
		if (a->in.pressed[IN_ACCEPT])       { uisound_play(DS_SOUND_ACCEPT); tape_click(DS_SOUND_TAPE_KEY, video); mp4c_toggle(); }
		/* The mode, on music; on a book, which plays in order whatever the
		 * mode says, its speed; on a video, Repeat: Off, One, All and round,
		 * as MP4's MENU > Repeat turns it, kept, its mark beside the time as
		 * the controls come up (2026-11-19: "Make the y button change it") */
		if (a->in.pressed[IN_Y]) {
			uisound_play(DS_SOUND_ACCEPT);
			if (video)                 video_cycle_repeat();
			else if (mp4c_is_book())  book_cycle_speed();
			else if (!mp4c_is_videos()) mp4_cycle_mode();
		}
		/* START turns the Player Look, round as MENU's row does, kept at
		 * once (DESIGN.md 11.67); as it is let go, since SELECT with START
		 * locks (mp4_input). A song's alone: a book shows its chapters
		 * and a video its picture whatever the look (np_look_for). */
		if (mp4_start()) {
			if (mp4c_is_book() || mp4c_is_videos() || video) {
				uisound_play(DS_SOUND_DENY);
			} else {
				uisound_play(DS_SOUND_MOVE);
				np_look_set((mp4l_id)((np_look() + 1) % MP4L_COUNT));
				memset(&drawn, 0, sizeof drawn);
			}
		}
		/* Up and Down step the tape's style on Cassette, round the kept
		 * ones, kept at once, its name said a moment (my pick in the Tape
		 * Styles lab, 2026-11-23); nothing on another look, a book or a
		 * video */
		if ((a->in.pressed[IN_UP] || a->in.pressed[IN_DOWN]) && !video && np_look_for() == MP4L_CASSETTE) {
			uisound_play(DS_SOUND_MOVE);
			np_tape_set(mp4t_step(np_tape(), a->in.pressed[IN_DOWN] ? 1 : -1));
			npl_tape_said();
			memset(&drawn, 0, sizeof drawn);
		}
		if (in_repeat(&a->in, IN_L1))       { moved_sound(a, IN_L1); tape_click(DS_SOUND_TAPE_WIND, video); mp4c_prev(); }
		if (in_repeat(&a->in, IN_R1))       { moved_sound(a, IN_R1); tape_click(DS_SOUND_TAPE_WIND, video); mp4c_next(); }
		if (in_repeat(&a->in, IN_LEFT))     { moved_sound(a, IN_LEFT); mp4c_seek_by(-seek_step(&a->in, IN_LEFT)); }
		if (in_repeat(&a->in, IN_RIGHT))    { moved_sound(a, IN_RIGHT); mp4c_seek_by(+seek_step(&a->in, IN_RIGHT)); }
		if (mp4_keys(a)) {
			mp4_locked(a);
			if (!a->running || g_mp4_gone) { how = MP4_CLOSE; break; }
			memset(&drawn, 0, sizeof drawn);   /* the screen is back: draw it */
			ctl_drawn = -1;
			grow_drawn = -1;
			continue;
		}

		for (b = 0; b < IN_COUNT && !touched; b++)
			touched = a->in.pressed[b] || a->in.down[b];
		/* A new theme is a change like any other here: every pixel of it. */
		if (tick_theme(a)) touched = true;
		now = plat_now_ms();

		if (video) {
			/* The controls: up for hold-controls after any press, and for
			 * as long as it is paused; in over quick, out over fade-out.
			 * The picture grows to the whole screen over turn once they
			 * have gone, and is back in its box, over quick, before they
			 * come again; a picture that does not grow waits for nothing. */
			float dt;
			bool want, grows;
			int tr;

			fresh = mp4v_update(a->r);
			/* The daemon finding the file too big for this CPU: the
			 * controls come up as on a key, so the notice with them is
			 * read; it, or no picture at all, drawn as it changes */
			tr = mp4v_trouble(NULL, 0, NULL);
			if (tr != trouble_seen) {
				if (tr == MPF_TOO_BIG) ctl_until = now + DS_MS_HOLD_CONTROLS;
				trouble_seen = tr;
				ctl_drawn = -1;
			}
			/* Any key shows them and does what it does: Y too, which
			 * turns Repeat, whose mark is in them */
			for (b = 0; b < IN_COUNT; b++)
				if (a->in.pressed[b]) { ctl_until = now + DS_MS_HOLD_CONTROLS; break; }
			want = mn->state != MU_PLAYING || (int)(ctl_until - now) > 0;
			grows = np_video_grows();
			dt = (float)(now - ctl_at);
			if (want) {
				if (grows && grow > 0) grow = grow - dt / DS_MS_QUICK > 0 ? grow - dt / DS_MS_QUICK : 0;
				else { grow = 0; ctl = ctl + dt / DS_MS_QUICK < 1 ? ctl + dt / DS_MS_QUICK : 1; }
			} else if (ctl > 0) {
				ctl = ctl - dt / DS_MS_FADE_OUT > 0 ? ctl - dt / DS_MS_FADE_OUT : 0;
			} else {
				grow = !grows ? 1 : grow + dt / DS_MS_TURN < 1 ? grow + dt / DS_MS_TURN : 1;
			}
			ctl_at = now;
			if (fresh || touched || ctl != ctl_drawn || grow != grow_drawn ||
			    drawn.at != (int)(mn->at + 0.5) ||
			    drawn.st != mn->state || !drawn.video || (int)(now - due) >= 0) {
				Uint32 osd;
				unsigned wait;

				/* The header coming back: its title reads from the start */
				if (ctl > 0 && ctl_drawn == 0) mq_reset(MQ_NP);
				wait = np_draw_video(a, mn, ctl, grow);
				plat_draw_osd(a->r);
				osd = plat_osd_until();
				plat_present();
				ctl_drawn = ctl;
				grow_drawn = grow;
				memset(&drawn, 0, sizeof drawn);
				drawn.video = true;
				drawn.at = (int)(mn->at + 0.5);
				drawn.st = mn->state;
				/* A sliding title is drawn as it moves, paused too, when no
				 * new picture would; its holds are slept through */
				due = now + (wait < 1000 ? wait : 1000);
				if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			}
			/* A frame is due every 33 to 42 ms; looking every 4 catches it
			 * within a few, which LEAD in src/mp4d/vid.c allows for. */
			SDL_Delay(4);
			continue;
		}

		/* The queue's track, not mp4c_path: that goes blank when an album
		 * ends, and the screen should still show the album that just did. */
		np_cover(a, ml_album_of(&g_mp4, mp4c_track(mn->index)));
		np_next_name(next, sizeof next);

		/* Drawn when what it shows has changed, not every pass. This is the
		 * screen left open for the length of an album, and a present a frame
		 * for forty minutes of one still picture is battery spent on nothing.
		 * The clock is kept in whole seconds, which is all it shows. */
		memset(&shown, 0, sizeof shown);
		shown.st = mn->state;
		shown.at = (int)(mn->at + 0.5);
		shown.len = (int)(mn->len + 0.5);
		shown.index = mn->index;
		shown.count = mn->count;
		memcpy(shown.title, mn->title, sizeof shown.title);
		memcpy(shown.artist, mn->artist, sizeof shown.artist);
		memcpy(shown.album, mn->album, sizeof shown.album);
		shown.tex = g_np.tex;
		shown.mode = mp4c_is_book() ? -1 : (int)mp4c_mode();
		shown.chapter = mp4c_chapter_now();
		shown.speed = (int)(mp4c_speed() * 100 + 0.5);
		/* Now Playing has its own draw clock, separate from g_redraw_at.
		 * Include the battery even when paused and no playback time moves. */
		if (!battery_now(&shown.battery_pct, &shown.charging)) shown.battery_pct = -1;

		if (touched || memcmp(&shown, &drawn, sizeof shown) || (int)(now - due) >= 0) {
			unsigned wait = np_draw_now(a, mn, next);
			Uint32 osd;

			/* Asked after the draw, which is what retires a line whose
			 * time is up - asked before, it names a moment already past. */
			plat_draw_osd(a->r);
			osd = plat_osd_until();
			plat_present();
			drawn = shown;
			/* At least once a second whatever happens, which is also how
			 * often the clock changes. The volume line needs one more frame
			 * at the moment it is due to go. */
			due = now + (wait < 1000 ? wait : 1000);
			if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			/* A look moving draws every frame, paced by the panel: a sleep
			 * on top of the present's wait would halve it (ui_frame_rest) */
			if (wait == 0) ui_frame_rest(16);
			else SDL_Delay(8);
		} else {
			SDL_Delay(16);
		}
	}

	mp4c_video_show(false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* Now Playing, counted while it is open, so MENU's Shuffle All knows to
 * carry on in it rather than open another on top (mp4_menu). */
mp4_exit mp4_now_screen(app *a)
{
	mp4_exit how;

	g_np_depth++;
	how = mp4_now_screen_run(a);
	g_np_depth--;
	return how;
}
