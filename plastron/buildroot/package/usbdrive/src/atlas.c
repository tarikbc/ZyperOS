/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Flash Drive page's live numbers: see atlas.h. */
#include "atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const SLOT[AT_SLOTS] = { "status", "speed", "written", "read" };
static const char *const GLYPH[AT_GLYPHS] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "dec", "sp", "B", "KB", "MB", "GB", "ps",
	"st_write", "st_read", "st_idle",
};

static bool inside(const at_layout *l, const at_rect *r)
{
	return r->w > 0 && r->h > 0 && r->x >= 0 && r->y >= 0 && r->x + r->w <= l->w && r->y + r->h <= l->h;
}

bool at_parse(const char *text, at_layout *l)
{
	bool got_slot[AT_SLOTS] = { false }, got_glyph[AT_GLYPHS] = { false }, head = false;
	const char *p = text;
	int i;

	memset(l, 0, sizeof *l);
	while (p && *p) {
		char line[160], a[16], b[16], side[8];
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		at_rect r;
		int v;

		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = '\0';
		p = nl ? nl + 1 : NULL;
		if (sscanf(line, "zyperos-drive-atlas %d", &v) == 1) head = v == 1;
		else if (sscanf(line, "size %d %d", &l->w, &l->h) == 2) continue;
		else if (sscanf(line, "slot %15s %d %d %d %d %7s", a, &r.x, &r.y, &r.w, &r.h, side) == 6) {
			/* X0 Y0 X1 Y1 */
			r.w -= r.x;
			r.h -= r.y;
			for (i = 0; i < AT_SLOTS; i++)
				if (!strcmp(a, SLOT[i])) {
					l->slot[i] = r;
					l->right[i] = !strcmp(side, "right");
					got_slot[i] = true;
				}
		} else if (sscanf(line, "glyph %15s %d %d %d %d", b, &r.x, &r.y, &r.w, &r.h) == 5) {
			for (i = 0; i < AT_GLYPHS; i++)
				if (!strcmp(b, GLYPH[i])) {
					l->glyph[i] = r;
					got_glyph[i] = true;
				}
		}
	}
	if (!head || l->w <= 0 || l->h <= 0 || l->w > 4096 || l->h > 4096) return false;
	for (i = 0; i < AT_SLOTS; i++)
		if (!got_slot[i] || !inside(l, &l->slot[i])) return false;
	for (i = 0; i < AT_GLYPHS; i++)
		if (!got_glyph[i] || !inside(l, &l->glyph[i]) || l->glyph[i].h > l->slot[AT_SLOT_SPEED].h)
			return false;
	return true;
}

void at_compose(const at_layout *l, int s, const int *g, int n, const uint32_t *atlas, uint32_t *out)
{
	const at_rect *sl = &l->slot[s];
	int i, x, y, width = 0;

	/* The slot cleared to the card's color, the atlas's own ground at its
	 * corner: what the page had there (its first numbers) is all gone, even
	 * where the new row is shorter */
	for (y = sl->y; y < sl->y + sl->h; y++)
		for (x = sl->x; x < sl->x + sl->w; x++) out[(size_t)y * l->w + x] = atlas[0];
	for (i = 0; i < n; i++)
		if (g[i] >= 0 && g[i] < AT_GLYPHS) width += l->glyph[g[i]].w;
	x = l->right[s] ? sl->x + sl->w - width : sl->x;
	for (i = 0; i < n; i++) {
		const at_rect *gr;
		int top, cx;

		if (g[i] < 0 || g[i] >= AT_GLYPHS) continue;
		gr = &l->glyph[g[i]];
		top = sl->y + (sl->h - gr->h) / 2;
		for (cx = 0; cx < gr->w; cx++) {
			int dx = x + cx;

			if (dx < sl->x || dx >= sl->x + sl->w) continue;
			for (y = 0; y < gr->h; y++)
				out[(size_t)(top + y) * l->w + dx] = atlas[(size_t)(gr->y + y) * l->w + gr->x + cx];
		}
		x += gr->w;
	}
}

bool at_load(const char *path, int w, int h, bool turned, uint32_t *out)
{
	size_t n = (size_t)w * h;
	uint32_t *in = malloc(n * 4);
	FILE *f = fopen(path, "rb");
	bool ok = in && f && fread(in, 4, n, f) == n;
	int x, y;

	if (f) fclose(f);
	if (ok) {
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++)
				out[(size_t)y * w + x] = turned ? in[(size_t)(w - 1 - x) * h + y] : in[(size_t)y * w + x];
	}
	free(in);
	return ok;
}
