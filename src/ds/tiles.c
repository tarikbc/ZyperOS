/* SPDX-License-Identifier: GPL-3.0-only */
/* A question in tiles, or a list (src/ds/tiles.h). Moved whole from
 * src/main.c (tiles_panel, choice_list_panel and their helpers); the
 * caller's renderer and panel pass now come in as arguments, and its sizes
 * are src/ds/layout.h's. */
#include "ds/tiles.h"

#include "config.h"      /* CFG_NO_COLOR: a lift in no system's color */
#include "ds/layout.h"
#include "platform.h"    /* ZYPEROS_SCREEN_W and _H: the design space */
#include "keyhints.h"
#include "keyhints_draw.h"
#include "linebreak.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* Does the line after a hyphen go on with the same word: a lowercase
 * letter, ASCII or Latin-1's in UTF-8 (ß to ÿ, the accented lowercase
 * letters every language here uses)? Then the hyphen only broke the word */
static bool tile_break_hyphen(const char *next)
{
	const unsigned char *n = (const unsigned char *)next;

	return (n[0] >= 'a' && n[0] <= 'z') || (n[0] == 0xC3 && n[1] >= 0x9F && n[1] <= 0xBF);
}

/* Three choices side by side, DESIGN.md 11.18: a panel (11.3) holding the
 * heading, its rule, a tile per choice - **raised**, **radius.card**, its mark
 * and its name in two lines, the focused one the **accent** fill in
 * **on-accent** - and the key legend (11.4) under them. Tiles' names are
 * their labels, a newline between the two lines, and their marks are the
 * caller's (tiles_show), a ui_glyph a tile. Drawn at `pose` as every panel is.
 *
 * The tiles are the rows up to the first note (MENU_NOTE). Three notes after
 * them are File Transfer's question while files move (DESIGN.md 11.14,
 * My pick 2026-11-06): a line under the rule saying what moves, a line
 * under the tiles saying what the focused one does, and the legend. */
/* 272 holds "Developer" at the menu's size (242) with 12 either side, and
 * three of them with the gaps and pads are 944, inside the screen's insets */
#define TILE_W   DS_TILE_W         /* 272 */
#define TILE_W2  DS_TILE_W_WIDE    /* 408: two tiles, a name on one line, a line under it */
#define TILE_HG  DS_TILE_H_GRID    /* 164: a grid's tile (Computer Connected), a name on one line */
#define TILE_H   DS_TILE_H         /* 232 */
#define TILE_GAP DS_TILE_GAP       /* 24 */
#define TILE_PAD DS_TILE_PAD       /* 40 */

/* One of the question's lines of words, whole in the panel's inner width:
 * on one line, or balanced onto two when it is wider (DESIGN.md 1: nothing
 * is cut off; the hint of Computer Connected ran 125 units past its panel
 * in Portuguese, 2026-11-07). Whether it was whole is what fits says. */
typedef struct {
	char l[2][LB_LINE];
	int n;
	bool fits;
} tile_note;

/* A key legend ("Left/Right: move   A: choose   B: charge only") breaks
 * between its keys, where its three spaces are, and keeps them inside a line:
 * the break that leaves the narrowest widest line, or its words broken as
 * any text is when a key's own words are wider than the box */
static bool tile_note_keys(TTF_Font *f, const char *text, int maxw, tile_note *o)
{
	const char *at[8];
	int len[8], np = 0, k, best = -1, bestw = 0;
	char head[LB_LINE], tail[LB_LINE];
	const char *p = text, *gap;

	while (np < 8) {
		at[np] = p;
		gap = strstr(p, "   ");
		len[np++] = gap ? (int)(gap - p) : (int)strlen(p);
		if (!gap) break;
		p = gap + 3;
	}
	if (np < 2) return false;
	for (k = 1; k < np; k++) {
		int wh, wt;

		snprintf(head, sizeof head, "%.*s", (int)(at[k] - 3 - text), text);
		snprintf(tail, sizeof tail, "%s", at[k]);
		wh = ui_text_width(f, head);
		wt = ui_text_width(f, tail);
		if (best < 0 || (wh > wt ? wh : wt) < bestw) {
			best = k;
			bestw = wh > wt ? wh : wt;
		}
	}
	if (ui_text_width(f, text) <= maxw) {
		snprintf(o->l[0], sizeof o->l[0], "%s", text);
		o->n = 1;
		o->fits = true;
		return true;
	}
	if (bestw > maxw) return false;
	snprintf(o->l[0], sizeof o->l[0], "%.*s", (int)(at[best] - 3 - text), text);
	snprintf(o->l[1], sizeof o->l[1], "%s", at[best]);
	o->n = 2;
	o->fits = true;
	return true;
}

static void tile_note_set(TTF_Font *f, const char *text, int maxw, tile_note *o)
{
	size_t want = 0, got = 0;
	const char *p;
	int k;

	if (tile_note_keys(f, text, maxw, o)) return;
	o->n = ui_balance_lines(f, text, maxw, 2, o->l);
	o->fits = o->n > 0;
	for (p = text; *p; p++) want += *p != ' ';
	for (k = 0; k < o->n; k++) {
		for (p = o->l[k]; *p; p++) got += *p != ' ';
		if (ui_text_width(f, o->l[k]) > maxw) o->fits = false;
	}
	/* A cut line says so with its dots, and is short of the words */
	if (got != want) o->fits = false;
}

/* A tile with a small line under its name (What did you plug in?'s USB
 * Accessory): the name in the menu's size on one line, the smaller where it
 * does not fit, and on two lines at the smaller past that; the small line in
 * **meta**, up to three balanced lines (three tiles leave it 272 wide, where
 * "headphones, Wi-Fi, keyboard" takes three, 2026-11-13). `h` is the words'
 * height from the name's top. */
#define TILE_SUB_TOP DS_TILE_SUB_TOP   /* 92: the name's top in the tile, under the mark */
typedef struct {
	char name[2][LB_LINE];
	int  nn;
	bool small;
	char sub[3][LB_LINE];
	int  ns;
	int  h;
} tile_sub;

static void tile_sub_lay(const char *label, const char *value, int w, tile_sub *o)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);

	o->small = ui_text_width(fm, label) > w - 24;
	o->nn = 1;
	snprintf(o->name[0], sizeof o->name[0], "%s", label);
	if (o->small && ui_text_width(fs, label) > w - 24) {
		o->nn = ui_balance_lines(fs, label, w - 24, 2, o->name);
		if (o->nn < 1) {
			snprintf(o->name[0], sizeof o->name[0], "%s", label);
			o->nn = 1;
		}
	}
	o->ns = ui_balance_lines(fs, value, w - 32, 3, o->sub);
	if (o->ns < 0) o->ns = 0;
	o->h = o->nn * (o->small ? ls : lm) + 4 + o->ns * ls;
}

/* What did you plug in? as a list (DESIGN.md 11.36, 2026-11-13: "improve
 * the what did you plug in screen design"): three rows the panel's width,
 * as a phone's "Use USB for" is, each its mark at the left (64, in a 96
 * square), its name in the menu's size and under it, in **meta**, what it
 * does, on one line (two where a language needs them). The focused row in
 * **accent**, its words **on-accent**; the others **raised**, the line under
 * the name **text-dim**. Up and Down move (Left and Right too), so the hints
 * are A and B alone, as a menu's. Asked for as DS_TILES_LIST (tiles_show),
 * which the panel's copy keeps, so the panel drawn again leaving is the same
 * list. */
#define CHOICE_ROW_GAP DS_LIST_ROW_GAP   /* 12 */
#define CHOICE_PAD_Y   DS_LIST_PAD_Y     /* 14 */
#define CHOICE_MARK    DS_LIST_MARK      /* 64 */
/* MENU > Scraper's Settings is one too: a tile is 248 wide inside, and the
 * name alone takes 264 in meta, with "linked as" and the account under it
 * (2026-11-13). */
static void choice_list_panel(SDL_Renderer *r, const ds_pass *pass, const char *heading,
                              const menu_row *rows, const int *marks, int n, int sel,
                              const char *legend, bool hints_on, mm_pose pose)
{
	const ds_theme *thm = theme();
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	const int lh = ui_font_line(UI_F_LABEL), lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	const int pad = TILE_PAD, text_x = 24 + 96 + 16;
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	key_hint hints[KEYHINTS_MAX];
	char hbuf[512], head[2][LB_LINE], sub[3][2][LB_LINE];
	int nh, nhead, nsub[3], rh[3], i, k, y, inner, total = 0;
	SDL_Rect panel, rule;

	nh = keyhints_unpack(legend, hbuf, sizeof hbuf, hints, KEYHINTS_MAX);
	panel.w = ZYPEROS_SCREEN_W - 2 * 40;
	inner = panel.w - 2 * pad;
	nhead = ui_balance_lines(fh, heading, inner, 2, head);
	if (nhead < 1) nhead = 1;
	for (i = 0; i < n; i++) {
		nsub[i] = ui_balance_lines(fs, rows[i].value, inner - text_x - 24, 2, sub[i]);
		if (nsub[i] < 0) nsub[i] = 0;
		rh[i] = CHOICE_PAD_Y + lm + nsub[i] * ls + CHOICE_PAD_Y;
		if (rh[i] < 100) rh[i] = 100;
		total += rh[i] + (i ? CHOICE_ROW_GAP : 0);
	}
	panel.h = 32 + nhead * lh + 24 + DS_RULE + 32 + total + 24 +
	          (nh ? keyhints_height(hints, nh, inner) : ls) + 32;
	panel.x = (ZYPEROS_SCREEN_W - panel.w) / 2;
	panel.y = (ZYPEROS_SCREEN_H - panel.h) / 2;
	if (posed) {
		SDL_FRect fr = pass->scaled(&panel, pose.scale);
		SDL_Rect q = { (int)(fr.x + 0.5f), (int)(fr.y + 0.5f), (int)(fr.w + 0.5f), (int)(fr.h + 0.5f) };

		ui_lift_k(r, &q, CFG_NO_COLOR, UI_LIFT_PANEL, pose.alpha);
		posed = pass->begin(pass->ctx);
	} else {
		ui_lift(r, &panel, CFG_NO_COLOR, UI_LIFT_PANEL);
	}
	ui_panel(r, &panel, DS_RADIUS_PANEL);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	y = panel.y + 32;
	for (k = 0; k < nhead; k++)
		ui_text(r, fh, head[k], panel.x + panel.w / 2, ui_line_y(UI_F_LABEL, y + k * lh), 0, UI_TEXT_SOFT);
	rule = (SDL_Rect){ panel.x + pad, y + nhead * lh + 24, inner, DS_RULE };
	SDL_SetRenderDrawColor(r, thm->edge.r, thm->edge.g, thm->edge.b, 255);
	SDL_RenderFillRect(r, &rule);
	y = rule.y + DS_RULE + 32;
	for (i = 0; i < n; i++) {
		SDL_Rect t = { panel.x + pad, y, inner, rh[i] };
		bool on = i == sel;
		SDL_Color ink = on ? ui_col(thm->on_accent, 255) : UI_TEXT_SOFT;
		SDL_Color dim = on ? ink : UI_TEXT_DIM;
		const char *label = rows[i].label ? rows[i].label : "";
		int words = lm + nsub[i] * ls, ty = t.y + (t.h - words) / 2;
		TTF_Font *fn = ui_text_width(fm, label) <= inner - text_x - 24 ? fm : fs;

		ui_round_rect(r, &t, DS_RADIUS_CARD, ui_col(on ? thm->accent : thm->raised, 255));
		ui_glyph_draw(r, marks ? (ui_glyph)marks[i] : UI_GLYPH_COUNT, t.x + 24 + 48, t.y + t.h / 2,
		              CHOICE_MARK, ink);
		ui_text(r, fn, label, t.x + text_x, ui_line_y(fn == fm ? UI_F_MENU : UI_F_META, ty), -1,
		        on ? ink : UI_TEXT);
		ui_fit_drawn(label);
		for (k = 0; k < nsub[i]; k++)
			ui_text(r, fs, sub[i][k], t.x + text_x, ui_line_y(UI_F_META, ty + lm + k * ls), -1, dim);
		ui_fit_drawn(rows[i].value);
		y += rh[i] + CHOICE_ROW_GAP;
	}
	y += 24 - CHOICE_ROW_GAP;
	if (nh && hints_on)
		keyhints_draw(r, hints, nh, panel.x + pad, panel.x + panel.w - pad, y + KEYHINTS_ROW / 2,
		              KEYHINTS_CENTER, false, 255);
	else if (!nh)
		ui_text(r, fs, legend, panel.x + panel.w / 2, ui_line_y(UI_F_META, y), 0, UI_TEXT_DIM);
	if (posed) pass->end(pass->ctx, &panel, pose);
}

static void tiles_panel(SDL_Renderer *r, const ds_pass *pass, ds_tiles_style kind,
                        const char *heading, const menu_row *tiles, const int *marks, int n,
                        int sel, const char *legend, bool hints_on, mm_pose pose)
{
	const char *sub = NULL, *what = NULL;
	key_hint hints[KEYHINTS_MAX];
	char hbuf[512];
	int nh;
	const ds_theme *thm = theme();
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int lh = ui_font_line(UI_F_LABEL), lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	SDL_Rect panel, rule;
	tile_note nsub = { .n = 0 }, nwhat = { .n = 0 }, nleg;
	int i, k, top, tiles_y, all = n, tiles_w, inner, widest, tw;
	int tile_gap = TILE_GAP, tile_pad = TILE_PAD, cols, trows, th, tiles_h;
	bool g_tiles_grid;
	char head[2][LB_LINE];
	int nhead;

	for (n = 0; n < all && !ROW_IS_NOTE(tiles[n]); n++) {}
	if (all - n == 3) {
		sub = tiles[n].label;
		what = tiles[n + 1].label;
		legend = tiles[n + 2].label;
	} else if (all - n == 1) {
		legend = tiles[n].label;
	}
	if (n < 1) return;
	if (kind == DS_TILES_LIST) {
		choice_list_panel(r, pass, heading, tiles, marks, n, sel, legend, hints_on, pose);
		return;
	}
	/* The legend: button hints (keyhints.h), or a line of text as before */
	nh = keyhints_unpack(legend, hbuf, sizeof hbuf, hints, KEYHINTS_MAX);
	/* The tiles' width, or the title's if that is wider, and where the lines
	 * under them do not fit in two lines of it, the screen's inset width: the
	 * panel is as wide as its words need within the screen, never more */
	/* Two tiles are wider, room for a name on one line and the small line
	 * under it (What did you plug in?, DESIGN.md 11.36) */
	/* Computer Connected's tiles two to a row, TILE_W2 by TILE_HG
	 * (2026-11-11, with Flash Drive: four in a row left the longer names
	 * no room); asked for as DS_TILES_GRID, which the panel's copy keeps,
	 * so the panel drawn again leaving is the same grid. Two are a row. */
	g_tiles_grid = kind == DS_TILES_GRID && n >= 3;
	tw = n == 2 || g_tiles_grid ? TILE_W2 : TILE_W;
	cols = g_tiles_grid && n > 2 ? 2 : n;
	trows = (n + cols - 1) / cols;
	th = g_tiles_grid && n > 2 ? TILE_HG : TILE_H;
	/* A small line that takes a third line, or a name two, makes every
	 * tile of the row as much taller, the room under them kept */
	if (th == TILE_H)
		for (i = 0; i < n; i++)
			if (tiles[i].value && !ROW_IS_NOTE(tiles[i])) {
				tile_sub ts;
				int room = TILE_H - TILE_SUB_TOP - (lm + 4 + 2 * ls);

				tile_sub_lay(tiles[i].label ? tiles[i].label : "", tiles[i].value, tw, &ts);
				if (TILE_SUB_TOP + ts.h + room > th) th = TILE_SUB_TOP + ts.h + room;
			}
	tiles_w = cols * tw + (cols - 1) * tile_gap;
	tiles_h = trows * th + (trows - 1) * tile_gap;
	widest = ZYPEROS_SCREEN_W - 2 * 40;
	panel.w = tiles_w + 2 * tile_pad;
	if (ui_text_width(fh, heading) + 2 * tile_pad > panel.w)
		panel.w = ui_text_width(fh, heading) + 2 * tile_pad;
	if (panel.w > widest) panel.w = widest;
	for (;;) {
		inner = panel.w - 2 * tile_pad;
		if (sub) tile_note_set(fs, sub, inner, &nsub);
		if (what) tile_note_set(fs, what, inner, &nwhat);
		if (nh) nleg = (tile_note){ .n = 0, .fits = true };
		else tile_note_set(fs, legend, inner, &nleg);
		if (panel.w >= widest || ((!sub || nsub.fits) && (!what || nwhat.fits) && nleg.fits)) break;
		panel.w = widest;
	}
	/* A title wider than the panel is broken onto a second line, never cut */
	nhead = ui_balance_lines(fh, heading, inner, 2, head);
	if (nhead < 1) nhead = 0;
	panel.h = 32 + (nhead > 1 ? nhead : 1) * lh + 24 + DS_RULE + (sub ? 20 + nsub.n * ls : 0) + 32 + tiles_h +
	          (what ? 24 + nwhat.n * ls : 0) + 24 +
	          (nh ? keyhints_height(hints, nh, inner) : nleg.n * ls) + 32;
	panel.x = (ZYPEROS_SCREEN_W - panel.w) / 2;
	panel.y = (ZYPEROS_SCREEN_H - panel.h) / 2;
	if (posed) {
		SDL_FRect fr = pass->scaled(&panel, pose.scale);
		SDL_Rect q = { (int)(fr.x + 0.5f), (int)(fr.y + 0.5f),
		               (int)(fr.w + 0.5f), (int)(fr.h + 0.5f) };

		ui_lift_k(r, &q, CFG_NO_COLOR, UI_LIFT_PANEL, pose.alpha);
		posed = pass->begin(pass->ctx);
	} else {
		ui_lift(r, &panel, CFG_NO_COLOR, UI_LIFT_PANEL);
	}
	ui_panel(r, &panel, DS_RADIUS_PANEL);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

	top = panel.y + 32;
	for (k = 0; k < nhead; k++)
		ui_text(r, fh, head[k], panel.x + panel.w / 2, ui_line_y(UI_F_LABEL, top + k * lh), 0,
		        UI_TEXT_SOFT);
	rule = (SDL_Rect){ panel.x + tile_pad, top + (nhead > 1 ? nhead : 1) * lh + 24,
	                   panel.w - 2 * tile_pad, DS_RULE };
	SDL_SetRenderDrawColor(r, thm->edge.r, thm->edge.g, thm->edge.b, 255);
	SDL_RenderFillRect(r, &rule);
	tiles_y = rule.y + DS_RULE;
	/* What moves, in **meta** **text-dim** under the rule */
	if (sub) {
		tiles_y += 20;
		for (k = 0; k < nsub.n; k++)
			ui_text(r, fs, nsub.l[k], panel.x + panel.w / 2,
			        ui_line_y(UI_F_META, tiles_y + k * ls), 0, UI_TEXT_DIM);
		tiles_y += nsub.n * ls;
	}
	tiles_y += 32;

	for (i = 0; i < n; i++) {
		/* By its row and column; a last row short of a whole one centered */
		int row = i / cols, col = i % cols;
		int in_row = row == trows - 1 ? n - row * cols : cols;
		int row_w = in_row * tw + (in_row - 1) * tile_gap;
		SDL_Rect t = { panel.x + (panel.w - row_w) / 2 + col * (tw + tile_gap),
		               tiles_y + row * (th + tile_gap), tw, th };

		/* A grid's last tile alone on its row (a release image's Flash
		 * Drive) spans the grid's width rather than sitting small in the
		 * middle of empty room */
		if (cols == 2 && trows > 1 && in_row == 1) {
			t.x = panel.x + (panel.w - tiles_w) / 2;
			t.w = tiles_w;
		}
		bool on = i == sel;
		SDL_Color ink = on ? ui_col(thm->on_accent, 255) : UI_TEXT_SOFT;
		const char *label = tiles[i].label ? tiles[i].label : "";
		const char *nl = strchr(label, '\n');
		ui_glyph g = marks ? (ui_glyph)marks[i] : UI_GLYPH_COUNT;
		char one[64], two[64];
		TTF_Font *f = fm;
		int lf = lm;

		ui_round_rect(r, &t, DS_RADIUS_CARD, ui_col(on ? thm->accent : thm->raised, 255));
		if (tiles[i].value && !ROW_IS_NOTE(tiles[i])) {
			/* A name and the small line under it: the mark higher, the
			 * words as tile_sub_lay lays them */
			tile_sub ts;
			int y;
			SDL_Color soft = on ? ink : UI_TEXT_DIM;

			tile_sub_lay(label, tiles[i].value, t.w, &ts);
			if (ts.small) { f = fs; lf = ls; }
			ui_glyph_draw(r, g, t.x + t.w / 2, t.y + 48, 64, ink);
			y = t.y + TILE_SUB_TOP;
			for (k = 0; k < ts.nn; k++, y += lf)
				ui_text(r, f, ts.name[k], t.x + t.w / 2, ui_line_y(f == fs ? UI_F_META : UI_F_MENU, y), 0, ink);
			if (ts.nn > 1) ui_fit_drawn(label);
			y += 4;
			for (k = 0; k < ts.ns; k++)
				ui_text(r, fs, ts.sub[k], t.x + t.w / 2, ui_line_y(UI_F_META, y + k * ls), 0, soft);
			ui_fit_drawn(tiles[i].value);
			continue;
		}
		if (th == TILE_HG) {
			/* A grid's tile: the mark, and the name on one line under it
			 * (its two lines joined), the smaller size where it does not
			 * fit, on two lines only past that */
			char joined[128];
			char *c;

			/* A line broken at a hyphen joins with none between; a
			 * hyphen that only broke a word ("Datei-\nübertragung", a
			 * lowercase letter after it) goes too, "Dateiübertragung",
			 * where one that is part of a name stays ("USB-\nStick").
			 * Any other break joins with a space. */
			snprintf(joined, sizeof joined, "%s", label);
			for (c = joined; *c; c++)
				if (*c == '\n') {
					if (c > joined && c[-1] == '-') {
						bool word = tile_break_hyphen(c + 1);

						memmove(c, c + 1, strlen(c));
						if (word) {
							memmove(c - 1, c, strlen(c) + 1);
							c--;
						}
						c--;
					} else {
						*c = ' ';
					}
				}
			ui_glyph_draw(r, g, t.x + t.w / 2, t.y + 54, 72, ink);
			if (ui_text_width(fm, joined) <= t.w - 24 || ui_text_width(fs, joined) <= t.w - 24) {
				f = ui_text_width(fm, joined) <= t.w - 24 ? fm : fs;
				ui_text(r, f, joined, t.x + t.w / 2,
				        ui_line_y(f == fs ? UI_F_META : UI_F_MENU, t.y + 110 + (f == fs ? (lm - ls) / 2 : 0)),
				        0, ink);
				ui_fit_drawn(label);
				continue;
			}
		}
		ui_glyph_draw(r, g, t.x + t.w / 2, t.y + 56, 72, ink);
		snprintf(one, sizeof one, "%.*s", nl ? (int)(nl - label) : (int)strlen(label), label);
		snprintf(two, sizeof two, "%s", nl ? nl + 1 : "");
		/* The menu's size where both lines fit the tile, else the least
		 * there is: nothing is cut off (DESIGN.md 1) */
		if (ui_text_width(f, one) > t.w - 24 || ui_text_width(f, two) > t.w - 24) {
			f = fs;
			lf = ls;
		}
		{
			/* Two lines under the mark, the smaller size centered where
			 * the menu's two would be */
			int y = t.y + 104 + (lm - lf);

			ui_text(r, f, one, t.x + t.w / 2, ui_line_y(f == fs ? UI_F_META : UI_F_MENU, y),
			        0, ink);
			if (two[0])
				ui_text(r, f, two, t.x + t.w / 2,
				        ui_line_y(f == fs ? UI_F_META : UI_F_MENU, y + lf), 0, ink);
		}
	}
	tiles_y += tiles_h;
	/* What the focused one does, in **meta** **text-soft** under the tiles */
	if (what) {
		tiles_y += 24;
		for (k = 0; k < nwhat.n; k++)
			ui_text(r, fs, nwhat.l[k], panel.x + panel.w / 2,
			        ui_line_y(UI_F_META, tiles_y + k * ls), 0, UI_TEXT_SOFT);
		tiles_y += nwhat.n * ls;
	}
	tiles_y += 24;
	/* Not while the question takes no button yet (usbpower_takes_input):
	 * its room kept, so nothing moves when they come */
	if (nh && hints_on)
		keyhints_draw(r, hints, nh, panel.x + tile_pad, panel.x + panel.w - tile_pad,
		              tiles_y + KEYHINTS_ROW / 2, KEYHINTS_CENTER, false, 255);
	else
		for (k = 0; k < nleg.n; k++)
			ui_text(r, fs, nleg.l[k], panel.x + panel.w / 2,
			        ui_line_y(UI_F_META, tiles_y + k * ls), 0, UI_TEXT_DIM);
	if (posed) pass->end(pass->ctx, &panel, pose);
}

void ds_tiles_draw(SDL_Renderer *r, const ds_tiles *t, mm_pose pose, const ds_pass *pass)
{
	tiles_panel(r, pass, t->style, t->heading, t->rows, t->marks, t->n, t->sel, t->legend, t->hints,
	            pose);
}
