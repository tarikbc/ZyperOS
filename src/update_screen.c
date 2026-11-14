/* SPDX-License-Identifier: GPL-3.0-only */
#include "update_screen.h"

#include "aout_route.h"
#include "app.h"
#include "design.h"
#include "linebreak.h"
#include "panel.h"
#include "place.h"
#include "theme.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"
#include "zypermark.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* MENU > System Update (src/sysupdate.h, docs/updates.md): the full page,
 * My pick in the update lab (2026-11-05), DESIGN.md 11.17. Zyper at the
 * left, the versions in **title**, the line under them in **meta**, the
 * release's What's New, one bar (or one line where it goes), and the
 * buttons at the foot. The work is the update program's, which goes on when
 * B leaves; this page starts it and draws what it says. */
#define UP_X       90                         /* the page's left and right edge */
#define UP_W       (ZYPEROS_SCREEN_W - 2 * UP_X)

/* What's New: the release's own points, the list's "note" lines, signed with
 * it; the first UP_NOTES, each whole (a list from tools/update-pack.py since
 * 2026-11-11 carries them uncut; one before, cut at 110 with "...") */
int up_notes(char out[UP_NOTES][UP_NOTE_W])
{
	char p[512], line[UP_NOTE_W + 8];
	FILE *f;
	int n = 0;

	/* A system update's notes are in its own list */
	up_path(p, sizeof p, "latest-system.txt");
	if (access(p, R_OK)) up_path(p, sizeof p, "latest.txt");
	f = fopen(p, "r");
	while (f && n < UP_NOTES && fgets(line, sizeof line, f)) {
		if (strncmp(line, "note ", 5)) continue;
		line[strcspn(line, "\n")] = 0;
		snprintf(out[n++], UP_NOTE_W, "%s", line + 5);
	}
	if (f) fclose(f);
	return n;
}

/* What's New's "more above" (up) or "more below" mark: a small triangle in
 * **text-dim**, centered on x, its flat side at y */
static void up_arrow_v(SDL_Renderer *r, int x, int y, bool up)
{
	SDL_Color c = UI_TEXT_DIM;
	SDL_Vertex v[3];
	int i, hw = 12, h = 10;

	v[0].position = (SDL_FPoint){ (float)(x - hw), (float)(up ? y + h : y) };
	v[1].position = (SDL_FPoint){ (float)(x + hw), (float)(up ? y + h : y) };
	v[2].position = (SDL_FPoint){ (float)x, (float)(up ? y : y + h) };
	for (i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
	SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

/* The arrow between the versions, drawn: the faces have no U+2192. A shaft
 * and a head, `w` long, centered on (x, mid), in **text-dim**. */
static void up_arrow(SDL_Renderer *r, int x, int mid, int w)
{
	SDL_Color c = UI_TEXT_DIM;
	SDL_Vertex v[3];
	int t = 6, hw = 16, hh = 15, i;
	SDL_Rect shaft = { x, mid - t / 2, w - hw + 2, t };

	ui_round_rect(r, &shaft, t / 2, c);
	v[0].position = (SDL_FPoint){ (float)(x + w - hw), (float)(mid - hh) };
	v[1].position = (SDL_FPoint){ (float)(x + w), (float)mid };
	v[2].position = (SDL_FPoint){ (float)(x + w - hw), (float)(mid + hh) };
	for (i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
	SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

/* The page's heading is the caller's: System Update's, or Install
 * PortMaster's, which is this page with its own words (src/pminstall.h).
 * `notes` are the points under the line, What's New on System Update's.
 * `icon`, PortMaster's (pm_icon_tex), stands where Zyper does, NULL for him. */
/* What's New's lines, from its first at y 348 every UP_NOTE_STEP, as many
 * as reach UP_NOTE_END: a point's lines balanced in **meta**, its lead
 * ("**USB Accessories:**" in the release notes, the words before its first
 * ": ") in **text** and the rest in **text-soft**, a dot at its first line,
 * and air between points. Scrolled a line at a time (up and down); an
 * arrow at the right says there is more above or below. */
#define UP_NOTE_STEP 42
#define UP_NOTE_GAP  14
#define UP_NOTE_END  548
#define UP_NOTE_LINES 8

typedef struct { const char *text; int len, lead, first; } up_note_line;

int g_up_scroll;           /* What's New's first line on screen */
static bool g_up_more;            /* ...and lines below the last on screen */

/* `text` filled onto lines of `maxw` a word at a time, as a page of text
 * is set (a balanced line, ui_balance_lines, is for a title or a message
 * of two or three: over seven lines it set a point narrow and ragged). A
 * word wider than a line goes on one of its own, cut there with the
 * fitter's ellipsis, which says so on a host (the fit check). The count. */
static int up_fill_lines(TTF_Font *f, const char *text, int maxw, int cap, char lines[][LB_LINE])
{
	char line[LB_LINE] = "", tryl[LB_LINE];
	const char *p = text;
	int n = 0;

	while (*p && n < cap) {
		const char *e;
		size_t wl;

		while (*p == ' ') p++;
		if (!*p) break;
		e = strchr(p, ' ');
		wl = e ? (size_t)(e - p) : strlen(p);
		snprintf(tryl, sizeof tryl, "%s%s%.*s", line, line[0] ? " " : "", (int)wl, p);
		/* A "word" wider than the line: Chinese or Japanese, set without
		 * spaces, which the line breaker breaks between characters */
		if (!line[0] && ui_text_width(f, tryl) > maxw)
			return ui_balance_lines(f, text, maxw, cap, lines);
		if (!line[0] || ui_text_width(f, tryl) <= maxw) {
			snprintf(line, sizeof line, "%s", tryl);
			p += wl;
			continue;
		}
		ui_fit_text(f, line, lines[n++], LB_LINE, maxw);
		line[0] = '\0';
	}
	if (line[0] && n < cap) ui_fit_text(f, line, lines[n++], LB_LINE, maxw);
	else if (*p) ui_fit_text(f, text, lines[n - 1], LB_LINE, 1);   /* more than cap lines: said */
	return n;
}

/* Every line of the points, in order, into out (up to cap), with where
 * each starts; the count */
static int up_note_lines(TTF_Font *fs, const char *const *notes, int nnotes,
                         char buf[][LB_LINE], up_note_line *out, int cap)
{
	int i, k, n = 0;

	for (i = 0; i < nnotes && n < cap; i++) {
		char lines[UP_NOTE_LINES][LB_LINE];
		const char *colon = strstr(notes[i], ": ");
		int lead = colon && colon - notes[i] <= 60 ? (int)(colon - notes[i]) + 1 : 0;
		int m = up_fill_lines(fs, notes[i], UP_W - 40, UP_NOTE_LINES, lines);

		for (k = 0; k < m && n < cap; k++, n++) {
			snprintf(buf[n], LB_LINE, "%s", lines[k]);
			out[n].text = buf[n];
			out[n].len = (int)strlen(buf[n]);
			out[n].first = k == 0;
			/* The lead on the first line: the whole line when it is longer */
			out[n].lead = k == 0 ? (lead < out[n].len ? lead : out[n].len) : 0;
		}
	}
	return n;
}

void update_page_draw(app *a, const char *heading, const su_page *p,
                             const char *const *notes, int nnotes, SDL_Texture *icon)
{
	const ds_theme *thm = theme();
	SDL_Renderer *r = a->r;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fl = ui_font(UI_F_LABEL),
	         *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	SDL_Color warn = ui_col(thm->warning, 255);
	char buf[160];
	int x, i, w, y;

	draw_background(a);
	ui_text(r, fl, heading, ZYPEROS_SCREEN_W / 2, ui_cap_y(fl, 66), 0, UI_TEXT_SOFT);
	if (icon) {
		/* Its 256 square drawn at 0.8, about Zyper's size */
		int iw, ih;

		SDL_QueryTexture(icon, NULL, NULL, &iw, &ih);
		iw = iw * 4 / 5;
		ih = ih * 4 / 5;
		/* A little over Zyper's middle, its quay clear of the points */
		SDL_RenderCopy(r, icon, NULL, &(SDL_Rect){ 158 - iw / 2, 216 - ih / 2, iw, ih });
	} else {
		draw_zyper(r, 158.0f, 232.0f, 150.0f, ZYPER_TILT_DEG, 1.0f, 0.0f, 255);
	}

	/* 0.1.0 → 0.2.0, or the installed version alone */
	x = 270;
	x += ui_text(r, ft, p->from, x, ui_cap_y(ft, 200), -1, UI_TEXT);
	if (p->to[0]) {
		up_arrow(r, x + 24, 200, 52);
		ui_text(r, ft, p->to, x + 100, ui_cap_y(ft, 200), -1, UI_TEXT);
	}
	/* The line under the versions wraps to a second line before it is cut
	 * (a long sentence in French or Portuguese); the second sits under it,
	 * clear of What's New at 348 */
	int sub_lines;
	{
		char sl[2][LB_LINE];
		int n = ui_balance_lines(fs, p->sub, ZYPEROS_SCREEN_W - 272 - UP_X, 2, sl), k;

		sub_lines = n;
		if (n > 1) ui_fit_drawn(p->sub);        /* drawn whole, in pieces */
		for (k = 0; k < n; k++)
			ui_text(r, fs, sl[k], 272, ui_cap_y(fs, 270 + k * 38), -1, p->sub_warn ? warn : UI_TEXT_DIM);
	}

	/* A problem, in full: **menu** **text**, up to three balanced lines from
	 * y 348 every 52, a **warning** bar beside them, where What's New goes */
	if (p->warn[0]) {
		char lines[4][LB_LINE];
		int n = ui_balance_lines(fm, p->warn, UP_W - 34, 4, lines), k;
		SDL_Rect mark = { UP_X, 348 - 30, 8, (n - 1) * 52 + 60 };

		ui_round_rect(r, &mark, 4, warn);
		if (n > 1) ui_fit_drawn(p->warn);       /* drawn whole, in pieces */
		for (k = 0; k < n; k++)
			ui_text(r, fm, lines[k], UP_X + 34, ui_cap_y(fm, 348 + k * 52), -1, UI_TEXT);
	}
	g_up_more = false;
	/* What's New (UP_NOTE_STEP above) */
	if (p->notes && !p->warn[0] && nnotes > 0) {
		static char buf[UP_NOTES * UP_NOTE_LINES][LB_LINE];
		static up_note_line ln[UP_NOTES * UP_NOTE_LINES];
		/* Down to the bar's room, or to the buttons' when there is no bar
		 * and no line where it goes */
		int note_end = p->bar || p->line[0] ? UP_NOTE_END : 640;
		/* Under a two-line sub, a little lower, so the two read apart */
		int top = sub_lines > 1 ? 362 : 348;
		int n = up_note_lines(fs, notes, nnotes, buf, ln, UP_NOTES * UP_NOTE_LINES), fits, k;

		/* Each point drawn whole, in pieces when it wraps */
		for (k = 0; k < nnotes; k++) ui_fit_drawn(notes[k]);

		/* Each line's place from the first point's top, the air before
		 * each point after the first counted; the scroll held to what
		 * shows the last line */
		static int at[UP_NOTES * UP_NOTE_LINES];

		for (k = 0, y = 0; k < n; k++) {
			if (k > 0 && ln[k].first) y += UP_NOTE_GAP;
			at[k] = y;
			y += UP_NOTE_STEP;
		}
		for (fits = 0; fits < n && at[n - 1] - at[fits] > note_end - top; fits++) {}
		if (g_up_scroll > fits) g_up_scroll = fits;
		if (g_up_scroll < 0) g_up_scroll = 0;
		for (k = g_up_scroll; k < n; k++) {
			y = top + at[k] - at[g_up_scroll];
			if (y > note_end) break;
			/* A drawn dot, not the "·" glyph: Noto Sans SC's is full
			 * width, and in Chinese it ran into the point's first
			 * character */
			if (ln[k].first)
				ui_round_rect(r, &(SDL_Rect){ UP_X + 4, y - 5, 10, 10 }, 5, UI_TEXT_DIM);
			if (ln[k].lead > 0) {
				char lead[LB_LINE];

				snprintf(lead, sizeof lead, "%.*s", ln[k].lead, ln[k].text);
				x = UP_X + 40 + ui_text(r, fs, lead, UP_X + 40, ui_cap_y(fs, y), -1, UI_TEXT);
				if (ln[k].text[ln[k].lead])
					ui_text(r, fs, ln[k].text + ln[k].lead, x, ui_cap_y(fs, y), -1, UI_TEXT_SOFT);
			} else {
				ui_text(r, fs, ln[k].text, UP_X + 40, ui_cap_y(fs, y), -1, UI_TEXT_SOFT);
			}
		}
		/* More above, more below: the menus' arrows, at the right */
		if (g_up_scroll > 0) up_arrow_v(r, UP_X + UP_W - 8, top - 30, true);
		if (k < n) up_arrow_v(r, UP_X + UP_W - 8, note_end + 4, false);
		g_up_more = k < n;
	}

	/* The bar, track with accent-ink over it, and its two words under it */
	if (p->bar) {
		SDL_Rect tr = { UP_X, 572, UP_W, 18 };
		w = (int)((float)UP_W * p->frac + 0.5f);
		ui_round_rect(r, &tr, 9, ui_col(thm->track, 255));
		if (w > 0) {
			SDL_Rect fi = { UP_X, 572, w < 18 ? 18 : w, 18 };
			ui_round_rect(r, &fi, 9, ui_col(thm->accent_ink, 255));
		}
		ui_text(r, fs, p->bar_left, UP_X, ui_cap_y(fs, 626), -1, UI_TEXT_DIM);
		ui_text(r, fs, p->bar_right, UP_X + UP_W, ui_cap_y(fs, 626), 1, UI_TEXT_DIM);
	} else if (p->line[0]) {
		/* Up to three lines, centered, the last where the one line stood */
		char ll[3][LB_LINE];
		int n = ui_balance_lines(fs, p->line, UP_W, 3, ll), k;
		for (k = 0; k < n; k++)
			ui_text(r, fs, ll[k], ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, 600 - (n - 1 - k) * 38), 0,
			        p->line_warn ? warn : UI_TEXT_DIM);
	}

	/* The buttons, glyph and word, centered as a row at the foot */
	{
		int gs = 48, gap = 12, between = 40, total = 0, cx;
		for (i = 0; i < p->nkeys; i++)
			total += ui_button_width(p->keys[i].button, gs) + gap + ui_text_width(fs, p->keys[i].label);
		total += between * (p->nkeys > 1 ? p->nkeys - 1 : 0);
		cx = (ZYPEROS_SCREEN_W - total) / 2;
		for (i = 0; i < p->nkeys; i++) {
			int bw = ui_button_width(p->keys[i].button, gs);
			ui_button(r, p->keys[i].button, cx + bw / 2, 714, gs, UI_BUTTON_PLAIN);
			cx += bw + gap;
			cx += ui_text(r, fs, p->keys[i].label, cx, ui_cap_y(fs, 714), -1, UI_TEXT_SOFT) + between;
		}
	}
}

/* The button's action on this page, or SUA_NONE */
su_action update_page_key(const su_page *p, const char *button)
{
	int i;
	for (i = 0; i < p->nkeys; i++)
		if (!strcmp(p->keys[i].button, button)) return p->keys[i].act;
	return SUA_NONE;
}

static void update_screen_body(app *a);

void update_screen(app *a)
{
	g_up_shown = true;
	update_screen_body(a);
	g_up_shown = false;
}

static void update_screen_body(app *a)
{
	const su_view *v = up_view(true);

	char notes[UP_NOTES][UP_NOTE_W];
	const char *np[UP_NOTES];
	int nnotes = up_notes(notes), i;

	for (i = 0; i < UP_NOTES; i++) np[i] = notes[i];
	g_up_scroll = 0;
	unsigned notes_at = plat_now_ms();

	/* Opened with a network and nothing in hand: it looks now, one small
	 * request (a look within the last ten minutes stands) */
	if (!v->running && v->wifi &&
	    (v->st.state == SU_IDLE || v->st.state == SU_UPTODATE || v->st.state == SU_ERROR) &&
	    (!v->st.time || time(NULL) - (time_t)v->st.time > 600))
		up_spawn("check");
	while (!want_quit && a->running) {
		su_page p;
		su_action act = SUA_NONE;

		v = up_view(false);
		su_page_build(v, &p);
		/* What's New comes with the list a look keeps: read again now and
		 * then, not every frame */
		if (plat_now_ms() - notes_at > 1000) { nnotes = up_notes(notes); notes_at = plat_now_ms(); }

		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		/* What's New, a line at a time: up and down (held, it goes on) */
		if (in_repeat(&a->in, IN_UP) && g_up_scroll > 0) { g_up_scroll--; moved_sound(a, IN_UP); }
		if (in_repeat(&a->in, IN_DOWN) && g_up_more) { g_up_scroll++; moved_sound(a, IN_DOWN); }
		if (a->in.pressed[IN_ACCEPT])      act = update_page_key(&p, "A");
		else if (a->in.pressed[IN_Y])      act = update_page_key(&p, "Y");
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}
		if ((a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_Y]) && act == SUA_NONE)
			uisound_play(DS_SOUND_DENY);
		switch (act) {
		case SUA_CHECK:
			uisound_play(DS_SOUND_ACCEPT);
			up_spawn("check");
			break;
		case SUA_DOWNLOAD:
			uisound_play(DS_SOUND_ACCEPT);
			up_spawn("download");
			break;
		case SUA_ROLLBACK: {
			char msg[192], prog[512], dev[64];
			const char *slash = strrchr(P_USERDATA, '/');
			char *argv[] = { prog, "rollback", "--card", (char *)P_CARD, "--device", dev, NULL };

			uisound_play(DS_SOUND_ACCEPT);
			snprintf(msg, sizeof msg, T(S_UPDATEPAGE_ROLLBACK_ASK), v->prev);
			if (confirm_panel(a, T(S_UPDATEPAGE_ROLLBACK), msg, T(S_UPDATEPAGE_ROLLBACK))) {
				snprintf(prog, sizeof prog, "%s/zyperos-update", P_ROOT);
				snprintf(dev, sizeof dev, "%s", slash ? slash + 1 : P_USERDATA);
				plat_run(argv, NULL, P_ROOT);
				g_up_read_at = 0;
			}
			break;
		}
		case SUA_RESTART:
			/* As power_off, but the device comes back: launch.sh reboots,
			 * and the image's switch puts the new version in before
			 * anything runs */
			uisound_play(DS_SOUND_ACCEPT);
			remember_place(a);
			book_keep(true);
			plat_request_reboot();
			plat_backlight(false);
			a->running = false;
			return;
		case SUA_BACK:
			uisound_play(DS_SOUND_BACK);
			return;
		default:
			break;
		}

		tick_tint(a);
		tick_theme(a);
		update_page_draw(a, T(S_UPDATEPAGE_TITLE), &p, np, nnotes, NULL);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
}
