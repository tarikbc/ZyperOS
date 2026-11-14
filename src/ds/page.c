/* SPDX-License-Identifier: GPL-3.0-only */
/* A page's parts (src/ds/page.h). Moved from src/main.c as they were: the
 * headings from Flash Drive's page (usb_drive_draw), File Transfer's
 * (xs_page_line) and the Scraper's (scp_head); the line over the keys
 * (scp_line); the lines of words (xs_page_meta); the foot (ud_page_foot and
 * scp_foot, one now). */
#include "ds/page.h"

#include "ds/mark.h"
#include "home.h"        /* page_hint: a button glyph and its word */
#include "linebreak.h"
#include "platform.h"    /* ZYPEROS_SCREEN_W: the design space */
#include "ui.h"

#include <stdio.h>
#include <string.h>

ds_page_frame ds_page_frame_of(bool zyper)
{
	return (ds_page_frame){
		.x0 = DS_INSET,
		.x1 = ZYPEROS_SCREEN_W - DS_INSET,
		.head_x = zyper ? DS_PAGE_HEAD_X : DS_INSET,
		.body_y = DS_PAGE_BODY_Y,
		.line_y = DS_PAGE_LINE_Y,
		.foot_y = DS_PAGE_FOOT_Y,
	};
}

/* A heading's line beside Zyper: **title** where it fits whole, **label**
 * where not, and sliding past that, never cut */
static void head_line(SDL_Renderer *r, const char *s, int x, int cy, int room, SDL_Color c, unsigned now)
{
	TTF_Font *f = ui_font(UI_F_TITLE);

	if (ui_text_width(f, s) > room) f = ui_font(UI_F_LABEL);
	if (ui_text_width(f, s) <= room) ui_text(r, f, s, x, ui_cap_y(f, cy), -1, c);
	else ui_text_marquee(r, f, s, x, ui_cap_y(f, cy), room, now, c);
}

void ds_page_head(SDL_Renderer *r, const ds_page_frame *f, ds_head_style style, const char *title,
                  const char *sub, ui_glyph mark, unsigned now)
{
	TTF_Font *ft = ui_font(UI_F_TITLE);
	const int hx = f->head_x, room = f->x1 - hx;

	if (!title) title = "";
	if (style == DS_HEAD_BALANCED) {
		char lines[2][LB_LINE];
		int n = ui_balance_lines(ft, title, room, 2, lines), k;

		if (n > 1) ui_fit_drawn(title);
		for (k = 0; k < n; k++)
			ui_text(r, ft, lines[k], hx, ui_cap_y(ft, DS_PAGE_HEAD_Y + (2 * k - (n - 1)) * DS_PAGE_HEAD_STEP),
			        -1, UI_TEXT);
		return;
	}
	if (style == DS_HEAD_TWO) {
		const char *nl = strchr(title, '\n');
		char first[192];

		snprintf(first, sizeof first, "%.*s", nl ? (int)(nl - title) : (int)strlen(title), title);
		head_line(r, first, hx, nl ? DS_PAGE_HEAD_Y - DS_PAGE_HEAD_STEP : DS_PAGE_HEAD_Y, room, UI_TEXT, now);
		if (nl) head_line(r, nl + 1, hx, DS_PAGE_HEAD_Y + DS_PAGE_HEAD_STEP, room, UI_TEXT_SOFT, now);
		return;
	}
	{
		TTF_Font *fs = ui_font(UI_F_META);
		bool two = sub && *sub;
		int sx = hx;
		char fit[192];

		ui_fit_text(ft, title, fit, sizeof fit, room);
		ui_text(r, ft, fit, hx, ui_cap_y(ft, two ? DS_PAGE_TITLE_Y : DS_PAGE_HEAD_Y), -1, UI_TEXT);
		if (two) {
			if (mark != UI_GLYPH_COUNT) {
				ui_glyph_draw(r, mark, hx + DS_PAGE_SUB_MARK / 2, DS_PAGE_SUB_Y, DS_PAGE_SUB_MARK,
				              ui_col(theme()->accent_ink, 255));
				sx = hx + DS_PAGE_SUB_MARK + DS_HINT_GAP;
			}
			ui_fit_text(fs, sub, fit, sizeof fit, f->x1 - sx);
			ui_text(r, fs, fit, sx, ui_cap_y(fs, DS_PAGE_SUB_Y), -1, UI_TEXT_SOFT);
		}
	}
}

void ds_page_line(SDL_Renderer *r, const ds_page_frame *f, const ds_line_item *it, int n)
{
	const ds_theme *t = theme();
	TTF_Font *fs = ui_font(UI_F_META);
	const int d = DS_MARK_LINE, pad = DS_MARK_LINE_PAD;
	int w[4], i, sum = 0, gap = DS_PAGE_LINE_GAP, x;

	if (n > 4) n = 4;
	for (i = 0; i < n; i++) sum += (w[i] = d + pad + ui_text_width(fs, it[i].text));
	if (sum + gap * (n - 1) > f->x1 - f->x0) gap = DS_PAGE_LINE_GAP_TIGHT;
	x = (ZYPEROS_SCREEN_W - sum - gap * (n - 1)) / 2;
	for (i = 0; i < n; i++) {
		SDL_Color c = it[i].tone == DS_TONE_WARN ? ui_col(t->warning, 255)
		            : it[i].tone == DS_TONE_OFF ? UI_TEXT_DIM : it[i].tone == DS_TONE_INFO ? UI_TEXT_SOFT : UI_TEXT;

		if (it[i].tone == DS_TONE_ON || it[i].tone == DS_TONE_OFF)
			ds_switch(r, x + d / 2, f->line_y, d, it[i].g, it[i].tone == DS_TONE_ON);
		else
			ui_glyph_draw(r, it[i].g, x + d / 2, f->line_y, d,
			              it[i].tone == DS_TONE_WARN ? c : ui_col(t->accent_ink, 255));
		ui_text(r, fs, it[i].text, x + d + pad, ui_cap_y(fs, f->line_y), -1, c);
		ui_fit_drawn(it[i].text);
		x += w[i] + gap;
	}
}

#define DS_PAGE_SEP "   ·   "

void ds_page_meta(SDL_Renderer *r, const ds_page_frame *f, const menu_row *rows, int n, int cy, unsigned now)
{
	TTF_Font *ft = ui_font(UI_F_META);
	const int room = f->x1 - f->x0, y = ui_cap_y(ft, cy);
	char line[512];
	size_t at = 0;
	int k, w = 0, x;

	/* the whole, for its width and for the check that it fits */
	line[0] = '\0';
	for (k = 0; k < n && at < sizeof line; k++) {
		bool note = ROW_IS_NOTE(rows[k]);

		at += (size_t)snprintf(line + at, sizeof line - at, "%s%s%s%s", k ? DS_PAGE_SEP : "", rows[k].label,
		                       note ? "" : "  ", note ? "" : rows[k].value);
	}
	w = ui_text_width(ft, line);
	if (w > room) {
		ui_text_marquee(r, ft, line, f->x0, y, room, now, UI_TEXT_SOFT);
		return;
	}
	x = (ZYPEROS_SCREEN_W - w) / 2;
	for (k = 0; k < n; k++) {
		if (k) {
			ui_text(r, ft, DS_PAGE_SEP, x, y, -1, UI_TEXT_DIM);
			x += ui_text_width(ft, DS_PAGE_SEP);
		}
		if (ROW_IS_NOTE(rows[k])) {
			ui_text(r, ft, rows[k].label, x, y, -1, UI_TEXT_SOFT);
			x += ui_text_width(ft, rows[k].label);
			continue;
		}
		ui_text(r, ft, rows[k].label, x, y, -1, UI_TEXT_DIM);
		x += ui_text_width(ft, rows[k].label) + ui_text_width(ft, "  ");
		ui_text(r, ft, rows[k].value, x, y, -1, UI_TEXT_SOFT);
		x += ui_text_width(ft, rows[k].value);
	}
}

void ds_page_foot(SDL_Renderer *r, const ds_page_frame *f, const char *const *keys,
                  const char *const *words, int n, int gap, int tight)
{
	int w[8], i, sum = 0, x;

	if (n > 8) n = 8;
	for (i = 0; i < n; i++) sum += (w[i] = page_hint_width(keys[i], words[i], false));
	if (sum + gap * (n - 1) > f->x1 - f->x0) gap = tight;
	x = (ZYPEROS_SCREEN_W - sum - gap * (n - 1)) / 2;
	for (i = 0; i < n; i++) {
		page_hint(r, keys[i], words[i], x, f->foot_y, false, false);
		x += w[i] + gap;
	}
}
