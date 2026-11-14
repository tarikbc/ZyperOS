/* SPDX-License-Identifier: GPL-3.0-only */
#include "nowplaying_screen.h"

#include "aout_route.h"
#include "app.h"
#include "blur.h"
#include "i18n.h"
#include "keyhints.h"
#include "keyhints_draw.h"
#include "mp4video.h"
#include "panel.h"
#include "topbar.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <stdio.h>
#include <string.h>

/* ---- Muse: Now Playing ---------------------------------------------------- */

/* How far one step of left or right seeks: ten seconds a tap, and further the
 * longer it is held - a minute a step after a second, five after three.
 * Eric's, 2026-09-29, for books: at ten seconds a step, eleven steps a second,
 * crossing a seventeen-hour book took nine minutes of holding the d-pad, and
 * now takes about twenty seconds. A song is over long before the steps grow,
 * so music is as it was. */
double seek_step(const in_state *in, in_button b)
{
	Uint32 held = SDL_GetTicks() - in->down_since[b];

	if (in->pressed[b] || held < 1000) return 10;
	return held < 3000 ? 60 : 300;
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
	const char *t = musec_chapter_title(i);
	const char *p;

	for (p = t; *p && (isdigit((unsigned char)*p) || *p == ' '); p++) { }
	return t[0] && *p;
}

/* A chapter as Now Playing names it: its own title when it has one worth
 * reading, "Chapter 3" when the title is only its number or missing. */
void chapter_label(int i, char *out, size_t n)
{
	if (chapter_titled(i)) snprintf(out, n, "%s", musec_chapter_title(i));
	else                   snprintf(out, n, T(S_MUSE_CHAPTER), i + 1);
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
	/* Muse's lime for everything that is not the cover or the words, the
	 * same its shelf and its menus wear: the light around the cover and along
	 * the foot, and as **accent-ink**, lime as a mark, the bar, the mode's
	 * mark and the word for a stopped track. It was the cover's own most
	 * vivid color once, which made one mark two colors a screen apart; Eric's
	 * call, 2026-09-19. */
	unsigned rgb = ZYPEROS_ACCENT, wait = (unsigned)-1, phase;
	SDL_Color acc = ui_col(theme()->accent_ink, 255);
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);
	int book = musec_is_book() ? ml_album_of(&g_muse, musec_track(mn->index)) : -1;
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[200], fit[200];
	int y, ty, bar, fill, left, right;

	/* A book played to its end says so, rather than that it stopped. */
	if (mn->state == MU_STOPPED && book >= 0 && g_book_done && g_book_done[book])
		state = T(S_MUSE_FINISHED);

	g_frame_no++;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The shelf's wash along the foot, and the lift a cover in Muse gets:
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
		int g = musec_is_book() ? -1 : MUSE_MODES[musec_mode()].glyph;

		ty = ui_line_y(UI_F_META, y);

		/* A book with chapters counts those instead of its one file. */
		if (musec_is_book() && musec_chapters() > 1) {
			snprintf(line, sizeof line, T(S_MUSE_CHAPTER_OF),
			         musec_chapter_now() + 1, musec_chapters());
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
		if (musec_is_book() && fabs(musec_speed() - 1.0) > 0.01) {
			snprintf(line, sizeof line, "%gx", musec_speed());
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

		if (musec_is_book() && musec_chapters() > 1) {
			int ci = musec_chapter_now() < 0 ? 0 : musec_chapter_now();

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
	ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, NP_TW, UI_BAR_H }, UI_BAR_H / 2,
	              ui_col(theme()->track, 255));
	fill = (int)(NP_TW * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, fill, UI_BAR_H }, UI_BAR_H / 2,
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
		snprintf(line, sizeof line, T(S_MUSE_NEXT), next);
		ui_fit_text(fs, line, fit, sizeof fit, NP_TW);
		ui_text(r, fs, fit, NP_TX,
		        ui_line_y(UI_F_META, NP_Y + NP_SIDE - ui_font_line(UI_F_META)), -1,
		        UI_TEXT_DIM);
	}

	np_hints(r, state == NULL, musec_is_book() ? 1 : 0, 0, 0, false, 255);
	{
		/* The top bar, as on every MP4 screen (2026-11-09): the clock at
		 * the left, the marks and the battery's words at the right */
		unsigned corner = draw_topbar(r, theme_index(), UI_TEXT_SOFT,
		                              wait <= 50 || frames_wanted());
		if (corner < wait) wait = corner;
	}
	return wait;
}

/* ---- MP4: the lock screen -------------------------------------------------
 *
 * mine, 2026-11-05, in place of Muse's pocket lock (#51, Eric's design,
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
 * A headset's play, pause and skip still work: musec_poll reads them, and it
 * runs here as everywhere. Auto Off still powers off a locked player once the
 * music has stopped long enough - it is no button. */

static bool g_lock_asked;                /* MENU's Lock Screen, for muse_menu */

void muse_ask_lock(void) { g_lock_asked = true; }

/* SHUFFLE ALL: every song on the card in a random order, from MENU on the
 * music shelf. mine, 2026-11-05: shuffle across the whole library, not one
 * album at a time. Music only - a book shuffled is not how anyone listens to
 * one, and a film is not a song - Singles with the rest.
 *
 * The mode becomes shuffle and is kept, as with Y on Now Playing: shuffle is
 * a switch on every player that has one, and the next album played is
 * shuffled too until Y moves it on. One queue of every track, so "3 / 1200"
 * counts the songs heard, and Now Playing names each song's own album and
 * artist (muse_track_names) where an album's queue names its one. */
static bool g_shuffle_asked;             /* MENU's Shuffle All, for muse_menu */

void muse_ask_shuffle(void) { g_shuffle_asked = true; }

static bool muse_shuffle_all(void)
{
	const char **paths = calloc((size_t)(g_muse.ntracks > 0 ? g_muse.ntracks : 1), sizeof *paths);
	int i, t, n = 0;

	if (!paths) return false;
	for (i = 0; i < g_muse.nalbums; i++)
		if (g_muse.albums[i].kind == ML_MUSIC)
			for (t = 0; t < g_muse.albums[i].n; t++)
				paths[n++] = g_muse.tracks[g_muse.albums[i].first + t].path;
	if (n > 0) {
		musec_set_mode(MUQ_SHUFFLE);
		db_set_str(db_dev(), "muse.mode", MUSE_MODES[MUQ_SHUFFLE].key);
		/* Shuffled, the start plays first and the rest follow in a random
		 * order (musequeue.h), so the start is drawn at random too. */
		muse_play(paths, n, (int)((plat_now_ms() * 2654435761u >> 7) % (unsigned)n), 0,
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
	return musec_playing() && !g_muse_over;
}

/* SELECT and START on an MP4 screen, as mp4chord_step reads them (the rule
 * and why is src/mp4lock.h's): what this pass's press of the two came to.
 * Every MP4 loop reads its buttons with muse_input, which says it, and asks
 * muse_select for SELECT rather than its press. */
static mp4chord  g_muse_chord;
static mp4c_said g_muse_said;

void muse_input(app *a)
{
	plat_input_poll(&a->in);
	g_muse_said = mp4chord_step(&g_muse_chord, a->in.down, a->in.pressed);
}

/* SELECT alone, let go: MP4 closes */
bool muse_select(void) { return g_muse_said == MP4C_SELECT; }

/* A screen of MP4's coming up: a SELECT down already is not its own */
void muse_input_reset(void)
{
	g_muse_chord = (mp4chord){ 0 };
	g_muse_said = MP4C_NONE;
}

/* The volume and brightness keys on an MP4 screen, X, and SELECT with START.
 * True when the screen should lock. */
bool muse_keys(app *a)
{
	in_state *in = &a->in;

	if (in_repeat(in, IN_VOLUP)) plat_volume_nudge(+1);
	if (in_repeat(in, IN_VOLDN)) plat_volume_nudge(-1);
	if (in_repeat(in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
	if (in_repeat(in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
	if (in->pressed[IN_X] || g_muse_said == MP4C_LOCK) {
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
		SDL_Rect bar = { cx - 200, y, 400, UI_BAR_H };

		ui_round_rect(r, &bar, UI_BAR_H / 2, ui_col(theme()->track, 255));
		bar.w = (int)(400 * k + 0.5f);
		if (bar.w > 0) ui_round_rect(r, &bar, UI_BAR_H / 2, ink);
	}
}

static void np_next_name(char *next, size_t n);

/* Locked, until SELECT and START are held, or Auto Off. See above. */
void muse_locked(app *a)
{
	mp4lock l = { 0 };
	bool panel_was = true, dark = false;
	unsigned shown_serial = 0;
	int drawn_k = -1, drawn_sec = -1;
	Uint32 osd_due = UINT32_MAX;            /* the volume bar's next look */

	fprintf(stderr, "mp4: locked\n");
	mp4lock_engage(&l, plat_now_ms(), a->in.down[MP4_UNLOCK_A] && a->in.down[MP4_UNLOCK_B]);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!want_quit && a->running) {
		bool video, lit, awake, panel, fresh = false, vol = false;
		const mu_now *mn;

		muse_screen_poll();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		/* Auto Off, once the music has stopped long enough: no button, so
		 * the lock does not stand in its way. The light first, so the
		 * power-off animation is seen. */
		if (idle_due(a)) {
			plat_backlight_dim(false);
			plat_backlight(true);
			muse_power(a);
			break;
		}
		if (mp4lock_step(&l, plat_now_ms(), a->in.down, a->in.pressed)) break;
		/* The volume keys, the two that get through (mp4lock_passes): the
		 * same steps and the same bar as unlocked, repeating when held. */
		if (in_repeat(&a->in, IN_VOLUP)) { plat_volume_nudge(+1); vol = true; }
		if (in_repeat(&a->in, IN_VOLDN)) { plat_volume_nudge(-1); vol = true; }
		mn = musec_now();
		video = musec_video();
		lit = mp4lock_lit(&l);
		awake = mp4lock_awake(&l);
		/* The lock panel: over music whenever the screen is on, the volume
		 * bar over it; over a video only as it locks and while the pair is
		 * held, so a volume press brings the bar alone over the picture. */
		panel = video ? lit : awake;
		musec_video_show(video);
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
				np_draw_video(a, mn, 0);
			} else {
				np_next_name(next, sizeof next);
				np_draw(a, mn, next);
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
	musec_video_show(false);
	plat_backlight_dim(false);
	plat_backlight(true);
	fprintf(stderr, "mp4: unlocked\n");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	muse_input_reset();
}

/* MENU, from any Muse screen: Muse's own menu, the one its shelf has always
 * had. One button, one meaning, wherever in Muse it is pressed - it used to
 * close Muse from Now Playing and the tracks, and open this on the shelf.
 * Eric's call, 2026-09-23.
 *
 * Rescan Folder inside it rebuilds the library, and every album index with it,
 * so a screen holding one cannot carry on: MUSE_REBUILT tells it to let go,
 * and the shelf finds itself again the way it does after its own MENU. */
static int g_np_depth;                   /* Now Playing is open, under whatever is */

muse_exit muse_menu(app *a)
{
	unsigned gen = g_muse_gen;
	bool by_menu;

	zyperos_menu_for(a, SCREEN_GAMES);
	/* MENU inside it closed it, and only it: Muse is still up, and may itself
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
		if (lock_allowed()) muse_locked(a);
	}
	if (want_quit || !a->running || g_message_stopped) return MUSE_CLOSE;
	/* Shuffle All closed it to play: Now Playing, on whatever screen of
	 * MP4's MENU was pressed - on Now Playing itself, it just carries on
	 * with the new song (g_np_depth). */
	if (g_shuffle_asked) {
		g_shuffle_asked = false;
		if (muse_shuffle_all() && !g_np_depth) {
			muse_exit how = muse_now_screen(a);

			if (how != MUSE_BACK) return how;
		}
	}
	return g_muse_gen != gen ? MUSE_REBUILT : MUSE_BACK;
}

/* What the mode will actually play next, for "Next:". Repeat one says so
 * with its mark, and "Next:" naming the same song reads as a mistake. In a
 * book with chapters, the next chapter, until the last. */
static void np_next_name(char *next, size_t n)
{
	const mu_now *mn = musec_now();
	const char *nt;

	next[0] = '\0';
	if (musec_is_book() && musec_chapters() > 1 &&
	    musec_chapter_now() + 1 < musec_chapters())
		chapter_label(musec_chapter_now() + 1, next, n);
	else if ((musec_is_book() || musec_is_videos() || musec_mode() != MUQ_REPEAT_ONE) &&
	         (nt = musec_upcoming()))
		ml_track_name(strrchr(nt, '/') ? strrchr(nt, '/') + 1 : nt, next, (int)n);
	(void)mn;
}

/* ---- MP4: video -----------------------------------------------------------
 *
 * A file with a picture plays on the same Now Playing, in one of two views
 * Y turns between, kept across restarts (`mp4.view`):
 *
 *   full screen   the picture as large as the screen takes it, on black,
 *                 and the controls over its foot only for hold-controls
 *                 after a press, or while paused - fading in over quick and
 *                 out over fade-out
 *   with controls the picture in a 16:9 frame on the theme's ground, the
 *                 title above it and the bar and keys below, always there
 *
 * The picture itself is the GPU's: the daemon's newest frame uploaded as YUV
 * (src/mp4video.c) and drawn UNDER the screen at the panel's own resolution
 * (plat_video_under), so on the Pixel 2 a 640x360 video is pixel for pixel.
 * The screen is presented when a new frame arrives - the video's own rate,
 * 24 to 30 times a second - and otherwise when the controls change. */

int g_np_view = -1;               /* -1 until read from the settings */

static int np_view(void)
{
	if (g_np_view < 0) {
		char v[16];

		db_get_str(db_dev(), "mp4.view", v, sizeof v, "full");
		g_np_view = !strcmp(v, "controls") ? NPV_CONTROLS : NPV_FULL;
	}
	return g_np_view;
}

static void np_view_toggle(void)
{
	g_np_view = np_view() == NPV_FULL ? NPV_CONTROLS : NPV_FULL;
	db_set_str(db_dev(), "mp4.view", g_np_view == NPV_FULL ? "full" : "controls");
}

/* The 16:9 frame of the view with controls, and the column its words share. */
#define NPV_X 64
#define NPV_W (ZYPEROS_SCREEN_W - 2 * NPV_X)
#define NPV_Y 96
#define NPV_H (NPV_W * 9 / 16)

/* The foot: what the buttons do on Now Playing, as the shelves' hints are
 * drawn (page_hint: a glyph and a word), in a row from x 64 to 960 with the
 * room left over shared between them. Listed most needed first; a row that
 * does not fit its language's words goes on to a second one above it, never
 * cut (DESIGN.md 12). `kind`: 0 music, 1 a book, 2 a video in full screen, 3
 * a video with its controls. `count` is a video's folder, `cy` the last
 * row's middle (0 for the standard 720), `alpha` how much of it shows. */
static void np_hints(SDL_Renderer *r, bool playing, int kind, int count, int cy,
                     bool video, Uint8 alpha)
{
	key_hint h[7];
	int n = 0;

	if (!cy) cy = 720;
	h[n].key = "A";  h[n++].label = playing ? T(S_MUSE_HINT_PAUSE) : T(S_MUSE_HINT_PLAY);
	h[n].key = "B";  h[n++].label = T(S_CONTROLS_BACK);
	if (video) {
		h[n].key = "X";     h[n++].label = T(S_MP4_HINT_LOCK);
		h[n].key = "Y";     h[n++].label = kind == 2 ? T(S_MP4_HINT_CONTROLS) : T(S_MP4_HINT_FULL_SCREEN);
		if (count > 1) { h[n].key = "L1/R1"; h[n++].label = T(S_MP4_HINT_VIDEO); }
	} else {
		h[n].key = "X";     h[n++].label = T(S_MP4_HINT_LOCK);
		h[n].key = "Y";     h[n++].label = kind == 1 ? T(S_MUSE_HINT_SPEED) : T(S_MUSE_HINT_MODE);
		h[n].key = "L1/R1"; h[n++].label = !musec_is_book() ? T(S_MUSE_HINT_TRACK)
		                                  : musec_chapters() > 1 ? T(S_MUSE_HINT_CHAPTER)
		                                                         : T(S_MUSE_HINT_FILE);
	}
	h[n].key = KEYHINT_LEFT_RIGHT; h[n++].label = T(S_MUSE_HINT_SEEK);
	/* The button hints' component (keyhints.h), spread from x 64 to 960,
	 * the last row on its line and any more above it */
	keyhints_draw(r, h, n, 64, ZYPEROS_SCREEN_W - 64, cy, KEYHINTS_SPREAD, true, alpha);
}

/* The bar, and with `times` the times under it - "1:26", what is left, and
 * the state when it is not playing - from y, `w` wide at x, in alpha `a`.
 * Without, the state alone, over the bar's right end. */
static void np_video_bar(SDL_Renderer *r, const mu_now *mn, int x, int y, int w, Uint8 a,
                         bool times)
{
	TTF_Font *fs = ui_font(UI_F_META);
	SDL_Color acc = ui_col(theme()->accent_ink, a), soft = UI_TEXT_SOFT;
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[24];
	int fill, ty, left, right;

	soft.a = a;
	ui_round_rect(r, &(SDL_Rect){ x, y, w, UI_BAR_H }, UI_BAR_H / 2, ui_col(theme()->track, a));
	fill = (int)(w * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ x, y, fill, UI_BAR_H }, UI_BAR_H / 2,
		              state ? ui_fade(acc, 0.45f) : acc);
	if (!times) return;
	mmss(t0, sizeof t0, mn->at);
	ty = ui_line_y(UI_F_META, y + 16);
	left = x + ui_text_tabular(r, fs, t0, x, ty, -1, soft);
	right = x + w;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, x + w, ty, 1, soft);
	}
	if (state) ui_text(r, fs, state, (left + right) / 2, ty, 0, acc);
}

/* One frame of the video view, the controls at `k` (0 hidden, 1 shown; the
 * view with controls always has them). */
void np_draw_video(app *a, const mu_now *mn, float k)
{
	SDL_Renderer *r = a->r;
	SDL_Texture *tex = mp4v_texture();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	bool full = np_view() == NPV_FULL;
	SDL_Rect box = full ? (SDL_Rect){ 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H }
	                    : (SDL_Rect){ NPV_X, NPV_Y, NPV_W, NPV_H };
	SDL_Rect pic = mp4v_fit(box);
	ds_rgb g = full ? DS_THEME[DS_DARK].ink : theme()->ground;
	char fit[200];
	Uint8 al;

	if (!full) k = 1;
	al = (Uint8)(255 * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5f);
	plat_video_under(tex, &pic, (SDL_Color){ g.r, g.g, g.b, 255 });
	ui_fit_frame();   /* the picture covers what was drawn (the host's fit check) */
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);


	if (full) {
		/* The controls over the picture's foot, on the theme's surface, all
		 * of it fading as one. */
		SDL_Rect band = { 0, ZYPEROS_SCREEN_H - 200, ZYPEROS_SCREEN_W, 200 };
		SDL_Color text = UI_TEXT, dim = UI_TEXT_DIM;

		SDL_Color soft = UI_TEXT_SOFT;
		char t0[16], t1[16], when[40];
		int tw;

		if (al == 0) return;
		ui_round_rect(r, &band, 0, ui_col(theme()->surface, (Uint8)(al * 0.92f)));
		text.a = dim.a = soft.a = al;
		/* The title and, at its right, where in the video: "1:26 / 2:32:38". */
		mmss(t0, sizeof t0, mn->at);
		mmss(t1, sizeof t1, mn->len);
		snprintf(when, sizeof when, mn->len > 0 ? "%s / %s" : "%s", t0, t1);
		tw = ui_text_tabular(r, fs, when, NPV_X + NPV_W,
		                     ui_line_y(UI_F_META, band.y + 30), 1, soft);
		/* Paused, or stopped at the end, before the time in accent-ink. */
		if (mn->state != MU_PLAYING) {
			SDL_Color acc = ui_col(theme()->accent_ink, al);
			const char *st = mn->state == MU_PAUSED ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);

			tw += 24 + ui_text(r, fs, st, NPV_X + NPV_W - tw - 24,
			                   ui_line_y(UI_F_META, band.y + 30), 1, acc);
		}
		ui_fit_text(fm, mn->title, fit, sizeof fit, NPV_W - tw - 32);
		ui_text(r, fm, fit, NPV_X, ui_line_y(UI_F_MENU, band.y + 22), -1, text);
		np_video_bar(r, mn, NPV_X, band.y + 88, NPV_W, al, false);
		np_hints(r, mn->state == MU_PLAYING, 2, mn->count, 730, true, al);
		return;
	}
	/* With controls: the home's header, as on every MP4 screen (2026-11-09),
	 * the clock at the left and the marks at the right, and the title
	 * between them in its row, centered; the frame, then the bar and the
	 * keys. The title was at the left, where the clock is now. */
	draw_topbar(r, theme_index(), UI_TEXT_SOFT, true);
	{
		/* In the clock's face, which the row is made for: the menu's, over
		 * the frame's top edge, its descenders touched the picture */
		TTF_Font *fh = page_font(PAGE_ACTION);

		ui_fit_text(fh, mn->title, fit, sizeof fit, 2 * header_half());
		ui_text(r, fh, fit, ZYPEROS_SCREEN_W / 2, ui_cap_y(fh, TOPBAR_MID), 0,
		        UI_TEXT);
	}
	if (!tex) {
		/* Before the first picture: the frame, empty, so nothing jumps. */
		SDL_Color ink = { 0, 0, 0, 255 };

		ui_round_rect(r, &box, 0, ink);
	}
	np_video_bar(r, mn, NPV_X, NPV_Y + NPV_H + 24, NPV_W, 255, true);
	np_hints(r, mn->state == MU_PLAYING, 3, mn->count, 730, true, 255);
}

/* MUSE_CLOSE for SELECT and the power button, which leave Muse altogether;
 * MUSE_BACK for B, which is the album's tracks underneath; MUSE_REBUILT when
 * MENU's Rescan Folder replaced the library. MENU otherwise comes back here. */
static muse_exit muse_now_screen_run(app *a)
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
	float ctl = 1, ctl_drawn = -1;
	muse_exit how = MUSE_BACK;

	mq_reset(MQ_NP);
	memset(&drawn, 0, sizeof drawn);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	muse_input_reset();
	ctl_until = plat_now_ms() + DS_MS_HOLD_CONTROLS;
	ctl_at = plat_now_ms();

	while (!want_quit && a->running) {
		const mu_now *mn;
		char next[128] = "";
		unsigned now;
		bool touched = false, video, fresh = false;
		int b;

		muse_screen_poll();
		cover_answers();
		mn = musec_now();
		video = musec_video();
		/* Pictures are decoded only while somebody looks at them. */
		musec_video_show(video);
		muse_input(a);
		if (a->in.quit_requested) { a->running = false; how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_BACK] || muse_select()) uisound_play(DS_SOUND_BACK);
		if (a->in.pressed[IN_BACK]) break;
		if (muse_select()) { how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_MENU]) {
			musec_video_show(false);
			how = muse_menu(a);
			if (how != MUSE_BACK) break;
			memset(&drawn, 0, sizeof drawn);     /* the menu drew over it */
			continue;
		}

		/* A, and only A. X did the same thing here and pausing in the list,
		 * which made one button mean two things a screen apart; X is the lock
		 * now, on every MP4 screen. Eric's call, 2026-09-19; X, 2026-11-05. */
		if (a->in.pressed[IN_ACCEPT])       { uisound_play(DS_SOUND_ACCEPT); musec_toggle(); }
		/* The mode, on music; on a book, which plays in order whatever the
		 * mode says, its speed; on a video, the view. */
		if (a->in.pressed[IN_Y]) {
			uisound_play(DS_SOUND_ACCEPT);
			if (video)                 np_view_toggle();
			else if (musec_is_book())  book_cycle_speed();
			else if (!musec_is_videos()) muse_cycle_mode();
		}
		if (in_repeat(&a->in, IN_L1))       { moved_sound(a, IN_L1); musec_prev(); }
		if (in_repeat(&a->in, IN_R1))       { moved_sound(a, IN_R1); musec_next(); }
		if (in_repeat(&a->in, IN_LEFT))     { moved_sound(a, IN_LEFT); musec_seek_by(-seek_step(&a->in, IN_LEFT)); }
		if (in_repeat(&a->in, IN_RIGHT))    { moved_sound(a, IN_RIGHT); musec_seek_by(+seek_step(&a->in, IN_RIGHT)); }
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) { how = MUSE_CLOSE; break; }
			memset(&drawn, 0, sizeof drawn);   /* the screen is back: draw it */
			ctl_drawn = -1;
			continue;
		}

		for (b = 0; b < IN_COUNT && !touched; b++)
			touched = a->in.pressed[b] || a->in.down[b];
		/* A new theme is a change like any other here: every pixel of it. */
		if (tick_theme(a)) touched = true;
		now = plat_now_ms();

		if (video) {
			/* The controls: up for hold-controls after any press, and for
			 * as long as it is paused; in over quick, out over fade-out. */
			float want, step;

			fresh = mp4v_update(a->r);
			for (b = 0; b < IN_COUNT; b++)
				if (a->in.pressed[b]) { ctl_until = now + DS_MS_HOLD_CONTROLS; break; }
			want = mn->state != MU_PLAYING || (int)(ctl_until - now) > 0 ? 1.0f : 0.0f;
			step = (float)(now - ctl_at) / (want > ctl ? DS_MS_QUICK : DS_MS_FADE_OUT);
			ctl = want > ctl ? (ctl + step > want ? want : ctl + step)
			                 : (ctl - step < want ? want : ctl - step);
			ctl_at = now;
			if (fresh || touched || ctl != ctl_drawn || drawn.at != (int)(mn->at + 0.5) ||
			    drawn.st != mn->state || !drawn.video || (int)(now - due) >= 0) {
				Uint32 osd;

				np_draw_video(a, mn, ctl);
				plat_draw_osd(a->r);
				osd = plat_osd_until();
				plat_present();
				ctl_drawn = ctl;
				memset(&drawn, 0, sizeof drawn);
				drawn.video = true;
				drawn.at = (int)(mn->at + 0.5);
				drawn.st = mn->state;
				due = now + 1000;
				if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			}
			/* A frame is due every 33 to 42 ms; looking every 4 catches it
			 * within a few, which LEAD in src/muse/vid.c allows for. */
			SDL_Delay(4);
			continue;
		}

		/* The queue's track, not musec_path: that goes blank when an album
		 * ends, and the screen should still show the album that just did. */
		np_cover(a, ml_album_of(&g_muse, musec_track(mn->index)));
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
		shown.mode = musec_is_book() ? -1 : (int)musec_mode();
		shown.chapter = musec_chapter_now();
		shown.speed = (int)(musec_speed() * 100 + 0.5);
		/* Now Playing has its own draw clock, separate from g_redraw_at.
		 * Include the battery even when paused and no playback time moves. */
		if (!battery_now(&shown.battery_pct, &shown.charging)) shown.battery_pct = -1;

		if (touched || memcmp(&shown, &drawn, sizeof shown) || (int)(now - due) >= 0) {
			unsigned wait = np_draw(a, mn, next);
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
			SDL_Delay(8);
		} else {
			SDL_Delay(16);
		}
	}

	musec_video_show(false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* Now Playing, counted while it is open, so MENU's Shuffle All knows to
 * carry on in it rather than open another on top (muse_menu). */
muse_exit muse_now_screen(app *a)
{
	muse_exit how;

	g_np_depth++;
	how = muse_now_screen_run(a);
	g_np_depth--;
	return how;
}
