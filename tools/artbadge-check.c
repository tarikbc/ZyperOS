/* SPDX-License-Identifier: GPL-3.0-only */
/* The art badge (src/artbadge.h), drawn alone: every badge in both themes at
 * every size a row draws one, for what it must be wherever it lands.
 *
 *   - its size follows the card's, a whole unit at a time, AB_D at rest and
 *     none under AB_MIN_D; its center AB_INSET + AB_D / 2 in from the art's
 *     corner, scaled the same;
 *   - the glyph's color on the disc meets 3:1, a mark's contrast, in both
 *     themes, so it reads on any art, the disc being the glyph's ground;
 *   - the glyph is inside the disc, the disc is whole surface round it, and
 *     what is past the disc and its shadow is clear;
 *   - the shadow falls below the disc, not above it;
 *   - the heart is the same both sides of its middle;
 *   - the four badges are four different pictures at every size.
 *
 * Plain C: links src/artbadge.c and nothing else. */
#include "../src/artbadge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static double lin(int v)
{
	double c = v / 255.0;

	return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double lum(ds_rgb c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }

static double contrast(ds_rgb a, ds_rgb b)
{
	double la = lum(a), lb = lum(b);

	return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

static unsigned alpha_of(uint32_t p) { return p >> 24; }

static int rgb_near(uint32_t p, ds_rgb c, int tol)
{
	int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;

	return abs(r - c.r) + abs(g - c.g) + abs(b - c.b) <= tol;
}

static const char *const NAME[AB_KINDS] = { "favorite", "album", "book", "video" };

int main(void)
{
	static uint32_t px[AB_KINDS][(AB_D * 2 + AB_PAD * 2) * (AB_D * 2 + AB_PAD * 2)];
	int theme, kind, d;
	float cx, cy;

	/* Its size and place, with the card */
	CHECK(ab_diameter(1.0f) == AB_D, "a focused card's badge is %d, not %d", ab_diameter(1.0f), AB_D);
	CHECK(ab_diameter(0.72f) == 29, "a side card's badge on a games shelf is %d, not 29", ab_diameter(0.72f));
	CHECK(ab_diameter(0.45f) == 0, "a badge %d across on a card under half its size", ab_diameter(0.45f));
	CHECK(ab_diameter(0.55f) == 22, "MP4's turned side card's badge is %d, not 22", ab_diameter(0.55f));
	for (d = 0; d < 100; d++) {
		float k = d / 100.0f;

		CHECK(ab_diameter(k + 0.01f) >= ab_diameter(k), "the badge shrinks as its card grows at %.2f", k);
	}
	ab_center(100, 200, 1.0f, &cx, &cy);
	CHECK(cx == 100 + AB_INSET + AB_D / 2 && cy == 200 + AB_INSET + AB_D / 2,
	      "a focused card's badge centered at (%.1f, %.1f) of its corner (100, 200)", cx, cy);
	ab_center(0, 0, 0.5f, &cx, &cy);
	CHECK(fabsf(cx - (AB_INSET + AB_D / 2) * 0.5f) < 0.001f, "the inset does not scale with the card");

	for (theme = 0; theme < DS_THEMES; theme++) {
		ds_rgb disc = ab_disc_color(theme);

		for (kind = 0; kind < AB_KINDS; kind++) {
			double c = contrast(ab_glyph_color(kind, theme), disc);

			CHECK(c >= 3.0, "%s's glyph on its disc in %s is %.2f:1, under a mark's 3:1",
			      NAME[kind], DS_THEME[theme].name, c);
		}
		for (d = AB_MIN_D; d <= AB_D + 4; d++) {
			int side = ab_buf_side(d), x, y;
			float c = side / 2.0f, rad = d / 2.0f;

			for (kind = 0; kind < AB_KINDS; kind++) {
				uint32_t *p = px[kind];
				ds_rgb glyph = ab_glyph_color(kind, theme);
				int ink = 0, wild = 0, ring_bad = 0, corner = 0, mirror = 0;
				unsigned above = 0, below = 0;

				ab_render(p, side, side, d, theme, kind);
				for (y = 0; y < side; y++)
					for (x = 0; x < side; x++) {
						uint32_t q = p[y * side + x];
						float fx = x + 0.5f - c, fy = y + 0.5f - c, r = sqrtf(fx * fx + fy * fy);

						if (rgb_near(q, glyph, 60) && alpha_of(q) == 255) {
							ink++;
							/* The glyph keeps a margin inside the disc */
							if (r > rad - 2) wild++;
						}
						/* Just inside the edge, the disc's own color */
						if (r > rad - 1.75f && r < rad - 1.0f &&
						    (!rgb_near(q, disc, 6) || alpha_of(q) != 255))
							ring_bad++;
						/* Past the disc and the shadow's reach, nothing */
						if (r > rad + (AB_SHADOW_Y + AB_SHADOW_BLUR) * d / (float)AB_D + 1 && alpha_of(q))
							corner++;
						if (r > rad + 1 && fy < 0) above += alpha_of(q);
						if (r > rad + 1 && fy > 0) below += alpha_of(q);
						if (kind == AB_FAVORITE) {
							uint32_t o = p[y * side + (side - 1 - x)];

							if (abs((int)alpha_of(q) - (int)alpha_of(o)) > 2 ||
							    rgb_near(q, glyph, 24) != rgb_near(o, glyph, 24))
								mirror++;
						}
					}
				CHECK(ink >= d * d / 16, "%s at %d in %s: %d pixels of its glyph, too few to read",
				      NAME[kind], d, DS_THEME[theme].name, ink);
				CHECK(!wild, "%s at %d in %s: %d glyph pixels at the disc's edge",
				      NAME[kind], d, DS_THEME[theme].name, wild);
				CHECK(!ring_bad, "%s at %d in %s: %d pixels inside the disc's edge not its surface",
				      NAME[kind], d, DS_THEME[theme].name, ring_bad);
				CHECK(!corner, "%s at %d in %s: %d pixels drawn past the disc and its shadow",
				      NAME[kind], d, DS_THEME[theme].name, corner);
				CHECK(below > above * 2, "%s at %d in %s: the shadow is not below the disc (%u above, %u below)",
				      NAME[kind], d, DS_THEME[theme].name, above, below);
				CHECK(!mirror, "the heart at %d in %s is not the same both sides (%d pixels)",
				      d, DS_THEME[theme].name, mirror);
			}
			for (kind = 0; kind < AB_KINDS; kind++) {
				int other;

				for (other = kind + 1; other < AB_KINDS; other++) {
					int diff = 0, i;

					for (i = 0; i < side * side; i++)
						if (rgb_near(px[kind][i], ab_glyph_color(kind, theme), 24) !=
						    rgb_near(px[other][i], ab_glyph_color(other, theme), 24))
							diff++;
					CHECK(diff > d * d / 24, "%s and %s at %d in %s differ in only %d pixels",
					      NAME[kind], NAME[other], d, DS_THEME[theme].name, diff);
				}
			}
		}
	}
	/* A theme that is not one draws Beard's, and nothing for no size */
	ab_render(px[0], ab_buf_side(AB_D), ab_buf_side(AB_D), AB_D, 7, AB_FAVORITE);
	ab_render(px[1], ab_buf_side(AB_D), ab_buf_side(AB_D), AB_D, DS_LIGHT, AB_FAVORITE);
	CHECK(!memcmp(px[0], px[1], sizeof(uint32_t) * ab_buf_side(AB_D) * ab_buf_side(AB_D)),
	      "an unknown theme does not draw Beard's badge");
	ab_render(px[0], ab_buf_side(0), ab_buf_side(0), 0, DS_LIGHT, AB_FAVORITE);
	for (d = 0; d < ab_buf_side(0) * ab_buf_side(0); d++)
		CHECK(!px[0][d], "a badge of no size drew something");

	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("ok: badges %d to %d across, both themes, %d kinds: sized and placed with the card, "
	       "3:1 on their disc, inside it, shadowed below, the heart symmetric, each its own picture\n",
	       AB_MIN_D, AB_D + 4, AB_KINDS);
	return 0;
}
