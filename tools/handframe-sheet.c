/* SPDX-License-Identifier: GPL-3.0-only */
/* The handheld frames' check sheet (src/handframe.h): each frame drawn at
 * each placement on each handheld's screen, a stand-in game in its window,
 * and over it, one pixel wide, the outlines the model's numbers give - the
 * lens, its window, the light, and the LCD's published size in the middle -
 * drawn on their own from the millimetres, not from the drawing, so an edge
 * of the drawing that is not on its line shows. Writes PPM files into the
 * folder it is given: tools/handframe-sheet.sh makes them a sheet. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/handframe.h"

static void put(uint8_t *px, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, float a)
{
	uint8_t *d;

	if (x < 0 || y < 0 || x >= w || y >= h) return;
	d = px + ((size_t)y * w + x) * 4;
	d[2] = (uint8_t)lroundf(r * a + d[2] * (1 - a));
	d[1] = (uint8_t)lroundf(g * a + d[1] * (1 - a));
	d[0] = (uint8_t)lroundf(b * a + d[0] * (1 - a));
}

/* An outline from millimetres, as the drawing places them: straight edges
 * on the rounded pixel edges, arcs and corners by their centres */
static void outline(const hf_model *m, const hf_rrect *r, uint8_t *px, int w, int h, int gx, int gy, int gw,
                    int gh, int dx)
{
	const float sx = (float)gw / m->lcd_w, sy = (float)gh / m->lcd_h;
	int x0, y0, x1, y1, t;

	hf_px_rect(m, r, gx, gy, gw, gh, &x0, &y0, &x1, &y1);
	x0 += dx;
	x1 += dx;
	for (t = 0; t < 4000; t++) {
		/* walk the rect's edges, the corner radii ignored: the check is the
		 * straight runs and the arcs' middles */
		const float u = (float)t / 4000.0f;
		int x, y;

		x = x0 + (int)lroundf(u * (x1 - x0 - 1));
		y = r->sag_top > 0 ? y0 + (int)lroundf(r->sag_top * sy * (1 - powf(2 * u - 1, 2))) * 0 : y0;
		if (u > 0.08f && u < 0.92f) put(px, w, h, x, y, 0, 255, 255, 0.6f);
		y = y1 - 1;
		if (u > 0.12f && u < 0.88f) put(px, w, h, x, y, 0, 255, 255, 0.6f);
		y = y0 + (int)lroundf(u * (y1 - y0 - 1));
		if (u > 0.12f && u < 0.85f) { put(px, w, h, x0, y, 0, 255, 255, 0.6f); put(px, w, h, x1 - 1, y, 0, 255, 255, 0.6f); }
	}
	(void)sx;
}

int main(int argc, char **argv)
{
	static const char *const TAGS[] = { "GB", "GBC", "GBA" };
	static const int SIZES[2][2] = { { 640, 480 }, { 1024, 768 } };
	const char *dir = argc > 1 ? argv[1] : ".";
	int t, s, k, c;

	for (t = 0; t < 3; t++)
		for (s = 0; s < 2; s++)
			for (k = 0; k < 2; k++)
				for (c = 0; c < (t == 1 && k == 1 && s == 1 ? 6 : 1); c++) {
					const hf_model *m = hf_model_for(TAGS[t]);
					const int w = SIZES[s][0], h = SIZES[s][1];
					int scale, gx, gy, gw, gh, x, y;
					uint8_t *px = malloc((size_t)w * h * 4);
					char path[512];
					FILE *f;

					if (!m || !px) return 1;
					if (!hf_place(m, k ? HF_WHOLE : HF_CUT, w, h, &scale, &gx, &gy, &gw, &gh)) {
						printf("%s %dx%d %s: no placement\n", TAGS[t], w, h, k ? "whole" : "cut");
						free(px);
						continue;
					}
					hf_render(m, c, w, h, gx, gy, gw, gh, px);
					/* A stand-in game: a checker of its own pixels */
					for (y = gy; y < gy + gh; y++)
						for (x = gx; x < gx + gw; x++) {
							const int on = (((x - gx) / scale / 8) + ((y - gy) / scale / 8)) & 1;
							uint8_t *d = px + ((size_t)y * w + x) * 4;

							d[0] = on ? 120 : 200; d[1] = on ? 160 : 210; d[2] = on ? 120 : 200;
						}
					if (argc > 2 && !strcmp(argv[2], "--outline")) {
						int i;

						/* the light where the drawing puts it (hf_light_dx) */
						const int ldx = hf_light_dx(m, w, gx, gy, gw, gh);

						for (i = 0; i < m->nparts; i++) {
							const hf_rrect *r = &m->part[i].shape;
							const int lit = r->x0 >= m->light.x0 - 0.01f && r->x1 <= m->light.x1 + 0.01f &&
							                r->y0 >= m->light.y0 - 0.01f && r->y1 <= m->light.y1 + 0.01f;

							outline(m, r, px, w, h, gx, gy, gw, gh, lit ? ldx : 0);
						}
					}
					snprintf(path, sizeof path, "%s/%s-%d-%s-%d.ppm", dir, TAGS[t], w, k ? "whole" : "cut", c);
					f = fopen(path, "wb");
					if (!f) return 1;
					fprintf(f, "P6\n%d %d\n255\n", w, h);
					for (y = 0; y < w * h; y++) { fputc(px[y * 4 + 2], f); fputc(px[y * 4 + 1], f); fputc(px[y * 4], f); }
					fclose(f);
					printf("%s %dx%d %s: %dx game %dx%d at %d,%d, %.2f x %.2f px/mm\n", TAGS[t], w, h, k ? "whole" : "cut",
					       scale, gw, gh, gx, gy, gw / m->lcd_w, gh / m->lcd_h);
					free(px);
				}
	return 0;
}
