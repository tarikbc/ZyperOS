/* SPDX-License-Identifier: GPL-3.0-only */
/* NES Player 2's page. See p2panel.h; the code's tile, the line over it and
 * the column beside it are Wi-Fi File Transfer's (src/hotpanel.c, DESIGN.md
 * 11.16), on a panel over the paused game as the in-game menu's pages are
 * (11.3). */
#include <stdio.h>
#include <string.h>

#include "design.h"
#include "hotpanel.h"
#include "keyhints.h"
#include "keyhints_draw.h"
#include "p2panel.h"
#include "platform.h"
#include "ui.h"

/* The panel's air and its gaps, on the grid. The code is 9 units a module:
 * the Wi-Fi's code (version 3, 29 modules, four of light each side) is 333
 * units, 4.1 cm on the Pixel 2, which a phone reads from arm's length, and
 * the panel with it and a line of two over it fits between the status
 * corner's row and the screen's foot. */
#define P2P_PAD      (4 * DS_GRID)
#define P2P_GAP      (2 * DS_GRID)
#define P2P_MODULE   9
#define P2P_COL_GAP  (6 * DS_GRID)       /* between the code and the column */
#define P2P_W_CODE   944                 /* with the code: the screen less 40 a side */
#define P2P_W        DS_PANEL_W          /* one column */
#define P2P_TOP      96                  /* a grid step under the status corner's row */
#define P2P_FOOT     24                  /* off the screen's foot */
#define P2P_HOW_LINES 3

/* Where everything goes, worked out once for the rect and the drawing */
typedef struct {
	SDL_Rect panel;
	bool     code;
	ui_font_role role;                  /* the line's and the players' */
	int      side, code_y, col_x;
	int      head_y, rule_y, how_y, net_y, pass_y, addr_y, who_y, who_step, rule2_y, legend_cy;
	char     how[P2P_HOW_LINES][LB_LINE];
	int      nhow;
} p2p_geom;

static void geom(const p2panel *p, p2p_geom *g)
{
	const int meta = ui_font_line(UI_F_META), menu = ui_font_line(UI_F_MENU);
	int y, line, inner, col_h, body_h;

	memset(g, 0, sizeof *g);
	g->code = p->qr && p->qr[0];
	g->panel.w = g->code ? P2P_W_CODE : P2P_W;
	inner = g->panel.w - 2 * P2P_PAD;
	/* With the code the line over it is the hot panel's, **meta**; alone it
	 * is what the page is for, at the rows' size */
	g->role = g->code ? UI_F_META : UI_F_MENU;
	line = ui_font_line(g->role);
	g->nhow = p->how && *p->how
	        ? ui_balance_lines(ui_font(g->role), p->how, inner, g->code ? 2 : P2P_HOW_LINES, g->how)
	        : 0;
	g->who_step = line * 3 / 2;

	/* Offsets from the panel's top first */
	g->head_y = P2P_PAD;
	g->rule_y = g->head_y + ui_font_line(UI_F_LABEL) + P2P_GAP;
	g->how_y = y = g->rule_y + DS_RULE + P2P_GAP;
	y += g->nhow * line + (g->nhow ? P2P_GAP * (g->code ? 1 : 2) : 0);
	if (g->code) {
		/* The code at the left; beside it the network and its password, as
		 * the hot panel's entries (what it is in **meta**, the value at menu
		 * size), the address under them in **meta**, and the players at the
		 * column's foot */
		g->side = hotpanel_code_side(p->qr, P2P_MODULE);
		g->code_y = y;
		g->col_x = P2P_PAD + g->side + P2P_COL_GAP;
		g->net_y = y;
		g->pass_y = g->net_y + meta + menu + P2P_GAP;
		g->addr_y = g->pass_y + meta + menu + P2P_GAP;
		col_h = g->addr_y + (p->addr && *p->addr ? meta : 0) - y + P2P_GAP +
		        p->nwho * g->who_step;
		body_h = g->side > col_h ? g->side : col_h;
		g->who_y = y + body_h - p->nwho * g->who_step;
	} else {
		g->col_x = P2P_PAD;
		g->who_y = y;
		body_h = p->nwho * g->who_step;
	}
	g->rule2_y = y + body_h + P2P_GAP;
	g->legend_cy = g->rule2_y + DS_RULE + P2P_GAP + meta / 2 + DS_GRID / 2;
	g->panel.h = g->legend_cy + meta / 2 + P2P_PAD - DS_GRID;

	g->panel.x = (ZYPEROS_SCREEN_W - g->panel.w) / 2;
	g->panel.y = (ZYPEROS_SCREEN_H - g->panel.h) / 2;
	if (g->panel.y < P2P_TOP) g->panel.y = P2P_TOP;
	if (g->panel.y + g->panel.h > ZYPEROS_SCREEN_H - P2P_FOOT)
		g->panel.y = ZYPEROS_SCREEN_H - P2P_FOOT - g->panel.h;
	g->col_x += g->panel.x;
	g->head_y += g->panel.y;
	g->rule_y += g->panel.y;
	g->how_y += g->panel.y;
	g->code_y += g->panel.y;
	g->net_y += g->panel.y;
	g->pass_y += g->panel.y;
	g->addr_y += g->panel.y;
	g->who_y += g->panel.y;
	g->rule2_y += g->panel.y;
	g->legend_cy += g->panel.y;
}

SDL_Rect p2panel_rect(const p2panel *p)
{
	p2p_geom g;

	geom(p, &g);
	return g.panel;
}

/* One of the column's entries, as the hot panel's: what it is in **meta**
 * **text-dim**, and the value under it in **text** at menu size */
static void entry(SDL_Renderer *r, int x, int y, const char *what, const char *value)
{
	TTF_Font *fs = ui_font(UI_F_META), *fv = ui_font(UI_F_MENU);

	ui_text(r, fs, what, x, ui_line_y(UI_F_META, y), -1, UI_TEXT_DIM);
	ui_text_tabular(r, fv, value ? value : "", x,
	                ui_line_y(UI_F_MENU, y + ui_font_line(UI_F_META)), -1, UI_TEXT);
}

void p2panel_draw(SDL_Renderer *r, const p2panel *p, bool lift)
{
	const unsigned rule_rgb = ui_sys_mark(p->accent);
	const SDL_Color rule = { (Uint8)(rule_rgb >> 16), (Uint8)(rule_rgb >> 8), (Uint8)rule_rgb, 255 };
	TTF_Font *f;
	p2p_geom g;
	int i, x0, w, line;

	geom(p, &g);
	f = ui_font(g.role);
	line = ui_font_line(g.role);
	x0 = g.panel.x + P2P_PAD;
	w = g.panel.w - 2 * P2P_PAD;
	/* DESIGN.md 11.3: **surface** in an **edge** frame, lifted off the game */
	if (lift) ui_lift(r, &g.panel, p->accent, UI_LIFT_PANEL);
	ui_panel(r, &g.panel, DS_RADIUS_PANEL);
	ui_text(r, ui_font(UI_F_LABEL), p->heading ? p->heading : "", g.panel.x + g.panel.w / 2,
	        ui_line_y(UI_F_LABEL, g.head_y), 0, UI_TEXT_SOFT);
	/* The heading's rule in the system's color, as its menu's */
	SDL_SetRenderDrawColor(r, rule.r, rule.g, rule.b, 255);
	SDL_RenderFillRect(r, &(SDL_Rect){ x0, g.rule_y, w, DS_RULE });

	/* The line: what the page is for, under the heading as the hot panel's
	 * is, its whole for the overflow check when it is drawn in pieces */
	for (i = 0; i < g.nhow; i++)
		ui_text(r, f, g.how[i], x0, ui_line_y(g.role, g.how_y + i * line), -1,
		        g.code ? UI_TEXT_DIM : UI_TEXT);
	if (g.nhow > 1) ui_fit_drawn(p->how);
	if (g.code) {
		hotpanel_code(r, x0, g.code_y, P2P_MODULE, p->qr);
		entry(r, g.col_x, g.net_y, T(S_HOTPANEL_NETWORK), p->ssid);
		entry(r, g.col_x, g.pass_y, T(S_PANEL_PASSWORD), p->pass);
		if (p->addr && *p->addr)
			ui_text(r, ui_font(UI_F_META), p->addr, g.col_x, ui_line_y(UI_F_META, g.addr_y), -1,
			        UI_TEXT_DIM);
	}
	/* A line per player: the name in **text-soft**, the state at the right
	 * in **text-dim**, as the hot panel's status lines */
	for (i = 0; i < p->nwho && i < P2PANEL_STATUS; i++) {
		int y = ui_line_y(g.role, g.who_y + i * g.who_step);

		ui_text(r, f, p->who[i] ? p->who[i] : "", g.col_x, y, -1, UI_TEXT_SOFT);
		ui_text(r, f, p->state[i] ? p->state[i] : "", x0 + w, y, 1, UI_TEXT_DIM);
	}
	SDL_SetRenderDrawColor(r, rule.r, rule.g, rule.b, 255);
	SDL_RenderFillRect(r, &(SDL_Rect){ x0, g.rule2_y, w, DS_RULE });
	/* The button hints (DESIGN.md 11.4, keyhints.h), centered at the foot */
	if (p->legend && keyhints_is(p->legend))
		keyhints_draw_packed(r, p->legend, x0, x0 + w, g.legend_cy, 255);
	else if (p->legend)
		ui_text(r, ui_font(UI_F_META), p->legend, g.panel.x + g.panel.w / 2,
		        ui_cap_y(ui_font(UI_F_META), g.legend_cy), 0, UI_TEXT_DIM);
}
