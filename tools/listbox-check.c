/* SPDX-License-Identifier: GPL-3.0-only */
/* The List view's boxes (src/listbox.h, DESIGN.md 11.39).
 *
 *     make check-listbox
 *
 * My pick in the List View lab, 2026-11-23: the art and the video at the
 * full height of the rows, the details over the media's foot. On the 4:3
 * canvas (the GKD Pixel 2, the TrimUI Brick) and the Smart Pro's 1368: eight
 * rows of 56 from y 200 to 648; the media's box beside them as tall, square
 * at 448 on 4:3 and 600 wide on the Smart Pro (a 4:3 snap's width at 448),
 * its right edge the heading's; the names in their column with 320 units
 * on 4:3; every shape whole in the box with its foot on the box's foot, so
 * the details stand in one place; and the details inside the box. */
#include <math.h>
#include <stdio.h>

#include "../src/design.h"
#include "../src/ds/layout.h"
#include "../src/listbox.h"
#include "../src/look.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* The shelf heading's line, games_frame().sys: from the page's margin at
 * 64, its middle at 152, to the margin at the right */
static SDL_Rect heading(int screen_w)
{
	return (SDL_Rect){ DS_MARGIN, 132, screen_w - 2 * DS_MARGIN, 40 };
}

static void canvas(const char *name, int screen_w, int media_w, int name_room)
{
	const SDL_Rect sys = heading(screen_w);
	const gl_boxes b = gl_boxes_for(sys, screen_w, 768);
	/* the shapes a box comes in: a PSP case, a Genesis case, a square, a
	 * SNES box, a 4:3 snap, a 16:9 one */
	static const float shapes[][2] = { { 58, 100 }, { 70, 100 }, { 1, 1 }, { 138, 100 }, { 640, 480 }, { 1280, 720 } };
	int i;

	printf("  %s (%d x 768):\n", name, screen_w);
	CHECK(b.list.y == 200 && b.list.h == GL_ROWS * GL_ROW_H && GL_ROWS * GL_ROW_H == 448,
	      "the rows from 200, 448 tall (8 of 56): from %d, %d tall", b.list.y, b.list.h);
	CHECK(b.list.x == DS_MARGIN, "the list's column at the margin: %d", b.list.x);
	CHECK(b.media.y == b.list.y && b.media.h == b.list.h,
	      "the media's box as tall as the rows: %d..%d against %d..%d",
	      b.media.y, b.media.y + b.media.h, b.list.y, b.list.y + b.list.h);
	CHECK(b.media.w == media_w, "the media's box %d wide, not %d", media_w, b.media.w);
	CHECK(b.media.x + b.media.w == sys.x + sys.w,
	      "the box's right edge the heading's (%d): %d", sys.x + sys.w, b.media.x + b.media.w);
	CHECK(b.media.x - (b.list.x + b.list.w) == 48, "the list to the box 48 apart: %d",
	      b.media.x - (b.list.x + b.list.w));
	/* a row's name from 56 into the column to 24 short of its end */
	CHECK(b.list.w - 56 - 24 == name_room, "the names have %d units, not %d", name_room, b.list.w - 56 - 24);
	for (i = 0; i < (int)(sizeof shapes / sizeof shapes[0]); i++) {
		SDL_FRect f = gl_media_fit(&b, shapes[i][0], shapes[i][1]);
		float aspect = f.w / f.h, want = shapes[i][0] / shapes[i][1];

		CHECK(fabsf(f.y + f.h - (b.media.y + b.media.h)) < .01f,
		      "a %gx%g picture's foot on the box's foot (%d): %.2f", shapes[i][0], shapes[i][1],
		      b.media.y + b.media.h, f.y + f.h);
		CHECK(f.x >= b.media.x - .01f && f.x + f.w <= b.media.x + b.media.w + .01f && f.y >= b.media.y - .01f,
		      "a %gx%g picture inside the box: %.1f,%.1f %.1fx%.1f", shapes[i][0], shapes[i][1], f.x, f.y, f.w, f.h);
		CHECK(fabsf(f.w - b.media.w) < .01f || fabsf(f.h - b.media.h) < .01f,
		      "a %gx%g picture as large as the box holds it: %.1fx%.1f", shapes[i][0], shapes[i][1], f.w, f.h);
		CHECK(fabsf(aspect - want) < .01f, "a %gx%g picture at its own shape: %.3f", shapes[i][0], shapes[i][1], aspect);
		CHECK(fabsf(f.x + f.w / 2 - (b.media.x + b.media.w / 2.0f)) < .01f,
		      "a %gx%g picture's middle on the box's", shapes[i][0], shapes[i][1]);
	}
	{
		SDL_FRect snap = gl_media_fit(&b, 640, 480);

		CHECK(snap.w >= 448 && snap.h >= 336, "a 4:3 snap at least 448 x 336: %.0f x %.0f", snap.w, snap.h);
	}
	for (i = 0; i < (int)(sizeof shapes / sizeof shapes[0]); i++) {
		SDL_FRect f = gl_media_fit(&b, shapes[i][0], shapes[i][1]);
		int x, w;

		gl_details_col(f, &x, &w);
		CHECK(x == (int)lroundf(f.x) + DS_INSET && x + w == (int)lroundf(f.x + f.w) - DS_INSET,
		      "the details over a %gx%g picture inset %d from its sides: %d..%d over %.0f..%.0f",
		      shapes[i][0], shapes[i][1], DS_INSET, x, x + w, f.x, f.x + f.w);
	}
	{
		SDL_FRect snap = gl_media_fit(&b, 640, 480);
		int x, w;

		gl_details_col(snap, &x, &w);
		CHECK(w >= 400, "the details over a 4:3 snap at least 400 wide: %d", w);
	}
	CHECK(b.a_y + DS_HINT_GLYPH_FOCUS / 2 + DS_INSET == b.media.y + b.media.h,
	      "A's hint %d over the box's foot: its middle at %d", DS_INSET, b.a_y);
	/* three facts over A, the first's line in the box's lower half */
	CHECK(b.a_y - b.a_gap - 2 * 32 - 16 > b.media.y + b.media.h / 2,
	      "three facts and A fit in the lower half of the box: the first's top at %d",
	      b.a_y - b.a_gap - 2 * 32 - 16);
}

/* The ratio of `c` over the ground at `alpha` laid on a picture of `art`,
 * blended in 8 bits as the renderer does */
static float over(ds_rgb ground, int alpha, ds_rgb art, ds_rgb c)
{
	const ds_rgb m = { (uint8_t)((ground.r * alpha + art.r * (255 - alpha)) / 255),
	                   (uint8_t)((ground.g * alpha + art.g * (255 - alpha)) / 255),
	                   (uint8_t)((ground.b * alpha + art.b * (255 - alpha)) / 255) };

	return look_ratio(c, m);
}

/* The least of `c`'s ratios over every gray and every pure and half
 * channel's picture under the ground at `alpha` */
static float worst(ds_rgb ground, int alpha, ds_rgb c)
{
	float w = 21;

	for (int v = 0; v < 256; v += 5) {
		ds_rgb arts[] = { { (uint8_t)v, (uint8_t)v, (uint8_t)v }, { (uint8_t)v, 0, 0 }, { 0, (uint8_t)v, 0 },
		                  { 0, 0, (uint8_t)v }, { (uint8_t)v, (uint8_t)v, 0 }, { 0, (uint8_t)v, (uint8_t)v },
		                  { (uint8_t)v, 0, (uint8_t)v }, { 255, (uint8_t)v, 0 }, { 0, 255, (uint8_t)v } };

		for (int i = 0; i < (int)(sizeof arts / sizeof arts[0]); i++) {
			float r = over(ground, alpha, arts[i], c);

			if (r < w) w = r;
		}
	}
	return w;
}

/* Every theme, tone and accent: text-soft over any picture at the least
 * ground it asks, 4.5:1, and no more than 80% of ground for it; the
 * warning line at its ground (up to 92%) in its walked color, 4.5:1 */
static void contrast(void)
{
	static const char *const NAME[DS_THEMES] = { "Beard", "Salt & Pepper" };
	float least_soft = 21, least_warn = 21;
	int most_soft = 0, most_warn = 0, steps = 0;

	printf("  the details' lines over any picture, every theme, tone and accent:\n");
	for (int b = 0; b < DS_THEMES; b++)
		for (int t = 0; t < DS_TONES; t++)
			for (int a = 0; a < DS_ACCENTS; a++) {
				ds_theme th;
				int soft, warn;
				ds_rgb w;
				float rs, rw;

				look_theme(b, DS_TONE[b][t], DS_ACCENT[a], &th);
				soft = gl_ground_alpha(th.ground, th.text_soft);
				warn = gl_ground_alpha(th.ground, th.warning);
				if (warn < soft) warn = soft;
				if (warn > 235) warn = 235;
				w = gl_line_on(th.ground, warn, th.warning, th.text);
				rs = worst(th.ground, soft, th.text_soft);
				rw = worst(th.ground, warn, w);
				CHECK(rs >= GL_TEXT_OWED, "%s, tone %d: text-soft %.2f:1 over the worst picture at %d",
				      NAME[b], t, rs, soft);
				CHECK(soft <= 204, "%s, tone %d: text-soft asks %d of ground, more than 80%%", NAME[b], t, soft);
				CHECK(soft == 0 || worst(th.ground, soft - 1, th.text_soft) < GL_TEXT_OWED + .05f,
				      "%s, tone %d: %d is not the least ground text-soft needs", NAME[b], t, soft);
				CHECK(rw >= GL_TEXT_OWED, "%s, tone %d: the warning line %.2f:1 over the worst picture at %d",
				      NAME[b], t, rw, warn);
				if (rs < least_soft) least_soft = rs;
				if (rw < least_warn) least_warn = rw;
				if (soft > most_soft) most_soft = soft;
				if (warn > most_warn) most_warn = warn;
				if (w.r != th.warning.r || w.g != th.warning.g || w.b != th.warning.b) steps++;
			}
	printf("    text-soft at %.2f:1 or more with at most %d of ground; the warning line at %.2f:1 or more "
	       "with at most %d, walked toward text in %d of %d looks\n",
	       least_soft, most_soft, least_warn, most_warn, steps, DS_THEMES * DS_TONES * DS_ACCENTS);
}

int main(void)
{
	printf("listbox: the List view's boxes\n");
	canvas("the 4:3 canvas (Pixel 2, Brick)", 1024, 448, 320);
	canvas("the Smart Pro's canvas", 1368, 600, 512);
	contrast();
	printf("listbox: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
