/* SPDX-License-Identifier: GPL-3.0-only */
/* The Link Cable's page. See linkpanel.h; its slab, rules and legend are
 * the in-game menu's pages' (DESIGN.md 11.3), its large line and dots the
 * mock's (11.72). */
#include <stdio.h>
#include <string.h>

#include "config.h"   /* CFG_NO_COLOR: the neutral light */
#include "design.h"
#include "ds/layout.h"
#include "ds/panel.h"
#include "linebreak.h"
#include "linkpanel.h"
#include "platform.h"
#include "ui.h"

/* The panel's air and its gaps, on the grid. As wide as Player 2's page with
 * its code (the screen less a caption's inset a side, 944), from under the
 * header's band to a line's gap over the page's foot (576 tall, the mock's
 * 608 less the status corner's room), the same for every page of the link,
 * so nothing jumps as it goes on. */
#define LKP_PAD      (4 * DS_GRID)
#define LKP_GAP      (2 * DS_GRID)
#define LKP_W        (ZYPEROS_SCREEN_W - 2 * DS_CAPTION_INSET)
#define LKP_TOP      (DS_HEADER + DS_SECTION)
#define LKP_H        (DS_FOOT_Y - DS_PAGE_LINE_GAP - LKP_TOP)
#define LKP_BIG_LINES 2
#define LKP_LINE_LINES 2                 /* each line under it, balanced */
#define LKP_DOT      22                  /* the mock's three dots */
#define LKP_DOT_GAP  (2 * DS_GRID)
#define LKP_DOTS_MS  300                 /* the brightest moves on this often */
#define LKP_ROW_W    (2 * DS_TILE_W)     /* a host's row, centered: two tiles' width */

typedef struct {
	SDL_Rect panel;
	int  head_y, rule_y, rule2_y, legend_cy, x0, w;
	int  big_y, code_y, lines_y, dots_y, hosts_y, row_h;
	bool dots;                           /* drawn: there is room for them */
	char big[LKP_BIG_LINES][LB_LINE];
	int  nbig;
	char line[LINKPANEL_LINES][LKP_LINE_LINES][LB_LINE];
	int  nline[LINKPANEL_LINES];
} lkp_geom;

static void geom(const linkpanel *p, lkp_geom *g)
{
	const int big = ui_font_line(UI_F_TITLE), meta = ui_font_line(UI_F_META);
	int body_top, body_bot, h, i, y, nl;

	memset(g, 0, sizeof *g);
	g->panel.w = LKP_W;
	g->panel.h = LKP_H;
	g->panel.x = (ZYPEROS_SCREEN_W - LKP_W) / 2;
	g->panel.y = LKP_TOP;
	g->x0 = g->panel.x + LKP_PAD;
	g->w = LKP_W - 2 * LKP_PAD;
	g->head_y = g->panel.y + LKP_PAD;
	g->rule_y = g->head_y + ui_font_line(UI_F_LABEL) + LKP_GAP;
	g->legend_cy = g->panel.y + LKP_H - LKP_PAD - meta / 2 + DS_GRID / 2;
	g->rule2_y = g->legend_cy - meta / 2 - DS_GRID / 2 - LKP_GAP - DS_RULE;
	body_top = g->rule_y + DS_RULE + LKP_GAP;
	body_bot = g->rule2_y - LKP_GAP;

	/* What the body holds, and its height, to stand it in the middle */
	g->nbig = p->big && *p->big ? ui_balance_lines(ui_font(UI_F_TITLE), p->big, g->w, LKP_BIG_LINES, g->big) : 0;
	nl = 0;
	for (i = 0; i < LINKPANEL_LINES; i++) {
		g->nline[i] = p->lines[i] && *p->lines[i]
		            ? ui_balance_lines(ui_font(UI_F_META), p->lines[i], g->w, LKP_LINE_LINES, g->line[i]) : 0;
		nl += g->nline[i];
	}
	g->row_h = ui_font_line(UI_F_MENU) + 2 * DS_GRID;
	h = g->nbig * big + (g->nbig && nl ? 3 * DS_GRID : 0) + nl * meta;
	if (p->code && *p->code) h += 2 * DS_GRID + big;
	/* The dots only where they fit: a code under a large line of two (a
	 * long language's) takes their room, and the code is what matters */
	g->dots = p->dots && h + 5 * DS_GRID + LKP_DOT <= body_bot - body_top;
	if (g->dots) h += 5 * DS_GRID + LKP_DOT;
	if (p->nhosts) h += 4 * DS_GRID + p->nhosts * g->row_h + (p->nhosts - 1) * DS_GRID;

	y = body_top + (body_bot - body_top - h) / 2;
	if (y < body_top) y = body_top;
	g->big_y = y;
	y += g->nbig * big;
	/* The code, as large as the line over it: it is read off this screen
	 * and typed on the other one */
	if (p->code && *p->code) {
		g->code_y = y + 2 * DS_GRID;
		y = g->code_y + big;
	}
	if (g->nbig && nl) y += 3 * DS_GRID;
	g->lines_y = y;
	y += nl * meta;
	g->dots_y = y + 5 * DS_GRID;
	if (g->dots) y = g->dots_y + LKP_DOT;
	g->hosts_y = y + 4 * DS_GRID;
}

SDL_Rect linkpanel_rect(const linkpanel *p)
{
	lkp_geom g;

	geom(p, &g);
	return g.panel;
}

void linkpanel_draw(SDL_Renderer *r, const linkpanel *p, bool lift, unsigned now_ms)
{
	const ds_theme *th = theme();
	const SDL_Color rule = ui_col(th->edge, 255);
	const int big = ui_font_line(UI_F_TITLE), meta = ui_font_line(UI_F_META);
	const int cx = ZYPEROS_SCREEN_W / 2;
	lkp_geom g;
	int i, k, y;

	geom(p, &g);
	/* DESIGN.md 11.3: **surface** in an **edge** frame, lifted off the game */
	ds_panel_slab(r, &g.panel, CFG_NO_COLOR, lift);
	ui_text(r, ui_font(UI_F_LABEL), p->heading ? p->heading : "", cx,
	        ui_line_y(UI_F_LABEL, g.head_y), 0, UI_TEXT_SOFT);
	ds_panel_rule(r, g.x0, g.rule_y, g.w, rule);

	/* The large line in **title** **text**, centered, as the mock's */
	for (i = 0; i < g.nbig; i++)
		ui_text(r, ui_font(UI_F_TITLE), g.big[i], cx, ui_line_y(UI_F_TITLE, g.big_y + i * big), 0, UI_TEXT);
	if (g.nbig > 1) ui_fit_drawn(p->big);
	/* The code in **title** **accent-ink**, its letters tabular */
	if (p->code && *p->code)
		ui_text_tabular(r, ui_font(UI_F_TITLE), p->code, cx, ui_line_y(UI_F_TITLE, g.code_y), 0,
		                ui_col(th->accent_ink, 255));
	/* What to do, in **meta** **text-soft**, centered under it */
	y = g.lines_y;
	for (i = 0; i < LINKPANEL_LINES; i++) {
		for (k = 0; k < g.nline[i]; k++, y += meta)
			ui_text(r, ui_font(UI_F_META), g.line[i][k], cx, ui_line_y(UI_F_META, y), 0, UI_TEXT_SOFT);
		if (g.nline[i] > 1) ui_fit_drawn(p->lines[i]);
	}
	/* Three dots in **accent-ink**, the brightest going round while it
	 * waits (the mock's 1, 0.6 and 0.3) */
	if (g.dots) {
		static const float A[3] = { 1.0f, 0.6f, 0.3f };
		const int step = (int)(now_ms / LKP_DOTS_MS % 3);
		const int x = cx - (3 * LKP_DOT + 2 * LKP_DOT_GAP) / 2;

		for (i = 0; i < 3; i++) {
			SDL_Rect d = { x + i * (LKP_DOT + LKP_DOT_GAP), g.dots_y, LKP_DOT, LKP_DOT };

			ui_round_rect(r, &d, LKP_DOT / 2,
			              ui_col(th->accent_ink, (Uint8)(255.0f * A[(i - step + 3) % 3] + 0.5f)));
		}
	}
	/* The hosts as rows, each a **raised** tile and the one under the
	 * cursor the **accent** fill with its name in **on-accent**, as a
	 * menu's focus */
	for (i = 0; i < p->nhosts && i < LINKPANEL_HOSTS; i++) {
		SDL_Rect q = { cx - LKP_ROW_W / 2, g.hosts_y + i * (g.row_h + DS_GRID), LKP_ROW_W, g.row_h };
		bool on = i == p->sel;

		ui_round_rect(r, &q, DS_RADIUS_ROW, ui_col(on ? th->accent : th->raised, 255));
		ui_text(r, ui_font(UI_F_MENU), p->hosts[i] ? p->hosts[i] : "", cx,
		        ui_line_y(UI_F_MENU, q.y + DS_GRID), 0,
		        on ? ui_col(th->on_accent, 255) : UI_TEXT);
	}
	ds_panel_rule(r, g.x0, g.rule2_y, g.w, rule);
	/* The button hints (DESIGN.md 11.4, keyhints.h), centered at the foot */
	ds_panel_legend(r, p->legend, g.x0, g.x0 + g.w, g.legend_cy);
}
