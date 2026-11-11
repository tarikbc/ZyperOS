/* SPDX-License-Identifier: GPL-3.0-only */
/* See keyhints_draw.h. */
#include "keyhints_draw.h"

#include "home.h"      /* page_hint_fade, page_hint_width: the shelves' own */

int keyhints_width(const char *key, const char *label)
{
	return page_hint_width(key, label, false);
}

int keyhints_line_width(const char *packed)
{
	key_hint h[KEYHINTS_MAX];
	char buf[512];
	int n = keyhints_unpack(packed, buf, sizeof buf, h, KEYHINTS_MAX), i, w = 0;

	for (i = 0; i < n; i++) w += keyhints_width(h[i].key, h[i].label) + (i ? KEYHINTS_GAP : 0);
	return w;
}

int keyhints_draw_packed(SDL_Renderer *r, const char *packed, int x0, int x1, int cy, Uint8 alpha)
{
	key_hint h[KEYHINTS_MAX];
	char buf[512];
	int n = keyhints_unpack(packed, buf, sizeof buf, h, KEYHINTS_MAX);

	return n ? keyhints_draw(r, h, n, x0, x1, cy, KEYHINTS_CENTER, false, alpha) : 0;
}

int keyhints_height(const key_hint *h, int n, int maxw)
{
	int start[KEYHINTS_MAX + 1];

	return keyhints_rows(h, n, maxw, KEYHINTS_GAP, keyhints_width, start) * KEYHINTS_ROW;
}

int keyhints_draw(SDL_Renderer *r, const key_hint *h, int n, int x0, int x1, int cy,
                  int align, bool up, Uint8 alpha)
{
	int start[KEYHINTS_MAX + 1], rows, row, i;

	rows = keyhints_rows(h, n, x1 - x0, align == KEYHINTS_SPREAD ? KEYHINTS_GAP_SPREAD : KEYHINTS_GAP,
	                     keyhints_width, start);
	for (row = 0; row < rows; row++) {
		int a = start[row], b = start[row + 1], total = 0, x, sp, y;

		y = up ? cy - (rows - 1 - row) * KEYHINTS_ROW : cy + row * KEYHINTS_ROW;
		for (i = a; i < b; i++) total += keyhints_width(h[i].key, h[i].label);
		if (align == KEYHINTS_SPREAD && b - a > 1) {
			sp = (x1 - x0 - total) / (b - a - 1);
			x = x0;
		} else if (align == KEYHINTS_SPREAD) {
			sp = 0;
			x = x0;
		} else {
			sp = KEYHINTS_GAP;
			total += (b - a - 1) * sp;
			x = align == KEYHINTS_SPREAD ? x0 : x0 + (x1 - x0 - total) / 2;
		}
		for (i = a; i < b; i++) {
			page_hint_fade(r, h[i].key, h[i].label, x, y, false, false, alpha);
			x += keyhints_width(h[i].key, h[i].label) + sp;
		}
	}
	return rows;
}
