/* SPDX-License-Identifier: GPL-3.0-only */
/* Badges, switch marks and the progress bar (src/ds/mark.h). The badge and
 * the switch were the Scraper page's scp_badge and scp_mark, and the bar its
 * scp_bar and Flash Drive's tile's, moved from src/main.c as they were. */
#include "ds/mark.h"

#include "ds/layout.h"
#include "ui.h"

void ds_badge(SDL_Renderer *r, int cx, int cy, int d, ui_glyph g, bool lime, Uint8 al)
{
	const ds_theme *t = theme();
	const int e = DS_BADGE_RING;

	ui_round_rect(r, &(SDL_Rect){ cx - d / 2 - e, cy - d / 2 - e, d + 2 * e, d + 2 * e }, d / 2 + e,
	              ui_col(t->edge, al));
	ui_round_rect(r, &(SDL_Rect){ cx - d / 2, cy - d / 2, d, d }, d / 2, ui_col(lime ? t->accent : t->surface, al));
	ui_glyph_draw(r, g, cx, cy, d * 64 / 100, ui_col(lime ? t->on_accent : t->accent_ink, al));
}

void ds_switch(SDL_Renderer *r, int cx, int cy, int d, ui_glyph g, bool on)
{
	const ds_theme *t = theme();
	const int e = DS_BADGE_RING;

	if (on) { ds_badge(r, cx, cy, d, g, true, 255); return; }
	ui_round_rect(r, &(SDL_Rect){ cx - d / 2, cy - d / 2, d, d }, d / 2, ui_col(t->edge, 255));
	ui_round_rect(r, &(SDL_Rect){ cx - d / 2 + e, cy - d / 2 + e, d - 2 * e, d - 2 * e }, d / 2 - e,
	              ui_col(t->raised, 255));
	ui_glyph_draw(r, g, cx, cy, d * 60 / 100, UI_TEXT_DIM);
}

void ds_bar(SDL_Renderer *r, int x, int y, int w, int fill)
{
	ui_round_rect(r, &(SDL_Rect){ x, y, w, UI_BAR_H }, UI_BAR_H / 2, ui_col(theme()->track, 255));
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ x, y, fill, UI_BAR_H }, UI_BAR_H / 2, ui_col(theme()->accent_ink, 255));
}
