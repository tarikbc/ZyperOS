/* SPDX-License-Identifier: GPL-3.0-only */
/* The hotspot's screen. See hotpanel.h, and DESIGN.md 11.16 for the layout
 * I picked in the hotspot lab (docs/design/mock/hotspot-lab.html, "A whole
 * screen"). */
#include <stdio.h>
#include <string.h>

#include "design.h"
#include "hotpanel.h"
#include "platform.h"
#include "qr.h"
#include "ui.h"

/* Everything starts 6 grid steps in from the left, as the lab drew it */
#define HOT_LEFT     (6 * DS_GRID)
#define HOT_TITLE_Y  (7 * DS_GRID)       /* the title's line */
#define HOT_SUB_Y    (17 * DS_GRID)      /* the line under it */
#define HOT_CODE_Y   (26 * DS_GRID)      /* the code's tile */
/* One module of the code is 12 units: half again the grid step, so the code is
 * 444 units, 5.5 cm on the Pixel 2, which a phone reads from arm's length.
 * On the panel that is 7.5 pixels, so every other module's edge falls between
 * two pixels; tools/qr-check.c's reading is of a picture at the panel's own
 * size for that reason. Four modules of light around it, as the standard asks. */
#define HOT_MODULE   12
#define HOT_QUIET    4
/* The column beside the code: its gap from the code, where it starts, and
 * each of its three entries' step down it */
#define HOT_GAP      (8 * DS_GRID)
#define HOT_COL_Y    (27 * DS_GRID)
#define HOT_STEP     (15 * DS_GRID)
#define HOT_ROWS_Y   (73 * DS_GRID)      /* Phones, then Now under it */
#define HOT_ROW_STEP (6 * DS_GRID)

/* The code for `text`, made once and kept until the text changes: it changes
 * once a screen. */
static int code(const char *text, const unsigned char **mods)
{
	static unsigned char m[QR_SIZE_MAX * QR_SIZE_MAX];
	static char made[192];
	static int size;

	if (!text) text = "";
	if (strcmp(made, text) || !size) {
		snprintf(made, sizeof made, "%s", text);
		size = qr_encode(text, m);
	}
	*mods = m;
	return size;
}

/* The code on its tile: ink on Beard's surface in both themes, since a
 * phone's camera reads dark on light, framed in the theme's **edge** as every
 * surface is. Returns the tile's side. */
static int draw_code(SDL_Renderer *r, int x, int y, const char *qr)
{
	const ds_rgb paper = DS_THEME[DS_LIGHT].surface, ink = theme()->ink;
	const unsigned char *m;
	int n = code(qr, &m), side = (n + 2 * HOT_QUIET) * HOT_MODULE;
	SDL_Rect t = { x, y, side, side }, in = { x + DS_FRAME, y + DS_FRAME,
	                                         side - 2 * DS_FRAME, side - 2 * DS_FRAME };
	int i, j, k;

	ui_round_rect(r, &t, DS_RADIUS_ROW, ui_col(theme()->edge, 255));
	ui_round_rect(r, &in, DS_RADIUS_ROW - DS_FRAME, ui_col(paper, 255));
	if (!n) return side;
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, 255);
	/* A run of dark modules in a row is one rectangle */
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i = k) {
			if (!m[j * n + i]) { k = i + 1; continue; }
			for (k = i; k < n && m[j * n + k]; k++) { }
			SDL_RenderFillRect(r, &(SDL_Rect){ x + (HOT_QUIET + i) * HOT_MODULE,
			                                   y + (HOT_QUIET + j) * HOT_MODULE,
			                                   (k - i) * HOT_MODULE, HOT_MODULE });
		}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	return side;
}

/* One of the column's entries: what it is in **meta** **text-dim**, and under
 * it the value in **text**, at menu size or, for the PIN, title. */
static void entry(SDL_Renderer *r, int x, int y, int room, const char *what,
                  const char *value, ui_font_role role, unsigned since_ms)
{
	TTF_Font *fs = ui_font(UI_F_META), *fv = ui_font(role);
	int vy = ui_line_y(role, y + ui_font_line(UI_F_META));

	ui_text(r, fs, what, x, ui_line_y(UI_F_META, y), -1, UI_TEXT_DIM);
	if (!value) value = "";
	if (ui_text_tabular_width(fv, value) <= room)
		ui_text_tabular(r, fv, value, x, vy, -1, UI_TEXT);
	else
		ui_text_marquee(r, fv, value, x, vy, room, since_ms, UI_TEXT);
}

/* A status line under the column: the name in **text-soft**, the value
 * right-aligned in **text-dim**, both **meta**, the value scrolling when it is
 * wider than the name leaves it. */
static void status(SDL_Renderer *r, int x, int w, int y, const char *label,
                   const char *value, unsigned since_ms)
{
	TTF_Font *f = ui_font(UI_F_META);
	int ly = ui_line_y(UI_F_META, y);
	int lw = ui_text(r, f, label, x, ly, -1, UI_TEXT_SOFT);
	int room = w - lw - 2 * DS_GRID * 3;

	if (!value || !*value) return;
	if (ui_text_width(f, value) <= room)
		ui_text(r, f, value, x + w, ly, 1, UI_TEXT_DIM);
	else
		ui_text_marquee(r, f, value, x + w - room, ly, room, since_ms, UI_TEXT_DIM);
}

void hotpanel_draw(SDL_Renderer *r, const hotpanel *p, unsigned now_ms)
{
	static char last_now[128];
	static unsigned now_since;
	const ds_rgb g = theme()->ground;
	int side, kx, room;

	/* The whole screen is this one: the ground under everything */
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, g.r, g.g, g.b, 255);
	SDL_RenderFillRect(r, NULL);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

	ui_text(r, ui_font(UI_F_TITLE), p->heading, HOT_LEFT,
	        ui_line_y(UI_F_TITLE, HOT_TITLE_Y), -1, UI_TEXT);
	ui_text(r, ui_font(UI_F_META), T(S_HOTPANEL_SCAN),
	        HOT_LEFT, ui_line_y(UI_F_META, HOT_SUB_Y), -1, UI_TEXT_DIM);

	side = draw_code(r, HOT_LEFT, HOT_CODE_Y, p->qr);
	kx = HOT_LEFT + side + HOT_GAP;
	room = ZYPEROS_SCREEN_W - HOT_LEFT - kx;
	entry(r, kx, HOT_COL_Y, room, T(S_HOTPANEL_NETWORK), p->ssid, UI_F_MENU, now_ms);
	entry(r, kx, HOT_COL_Y + HOT_STEP, room, T(S_PANEL_PASSWORD), p->pass, UI_F_MENU, now_ms);
	entry(r, kx, HOT_COL_Y + 2 * HOT_STEP, room, "PIN", p->pin, UI_F_TITLE, now_ms);

	/* The Now line's marquee starts over when its news does */
	if (strcmp(last_now, p->now ? p->now : "")) {
		snprintf(last_now, sizeof last_now, "%s", p->now ? p->now : "");
		now_since = now_ms;
	}
	status(r, kx, room, HOT_ROWS_Y, T(S_HOTPANEL_PHONES), p->phones, 0);
	status(r, kx, room, HOT_ROWS_Y + HOT_ROW_STEP, T(S_XFER_NOW), p->now, now_ms - now_since);

	/* The key legend (DESIGN.md 11.4), centered at the foot */
	ui_text(r, ui_font(UI_F_META), p->legend ? p->legend : T(S_PANEL_B_CLOSE),
	        ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - DS_INSET - DS_GRID -
	                             ui_font_line(UI_F_META)), 0, UI_TEXT_DIM);
}
