/* SPDX-License-Identifier: GPL-3.0-only */
/* See listbox.h. */
#include "listbox.h"

#include "design.h"
#include "ds/flex.h"
#include "ds/layout.h"
#include "look.h"      /* look_ratio */

#include <math.h>

#define LB_TOP_GAP 28    /* the first row's top under the heading's line (200):
                          * room for the triangle that says more is above */
#define LB_GAP     48    /* the list's column to the media */
#define LB_A_GAP   48    /* the last fact's line to A's, as under the cover before */
#define LB_MEDIA_H (GL_ROWS * GL_ROW_H)

/* The box's width: square on the 4:3 canvas (1024 wide), the width past
 * it on a wider one, up to a 4:3 snap's at the box's height on the 8-unit
 * grid */
static int media_w(int screen_w)
{
	const int w = LB_MEDIA_H + screen_w - 1024, most = (LB_MEDIA_H * 4 / 3 + 4) / 8 * 8;

	return w < LB_MEDIA_H ? LB_MEDIA_H : w > most ? most : w;
}

gl_boxes gl_boxes_for(SDL_Rect sys, int screen_w, int screen_h)
{
	ds_flex F;
	gl_boxes b;
	const int top = sys.y + sys.h + LB_TOP_GAP;
	const int root = ds_flex_begin(&F, screen_w, screen_h,
	                               &(ds_box){ .dir = DS_ROW, .gap = LB_GAP,
	                                          .pad = { sys.x, top, screen_w - sys.x - sys.w,
	                                                   screen_h - top - LB_MEDIA_H } });
	const int list = ds_flex_add(&F, root, &(ds_box){ .grow = 1 });
	const int media = ds_flex_add(&F, root, &(ds_box){ .w = media_w(screen_w) });

	ds_flex_solve(&F);
	b.list = ds_flex_rect(&F, list);
	b.media = ds_flex_rect(&F, media);
	b.a_y = b.media.y + b.media.h - DS_INSET - DS_HINT_GLYPH_FOCUS / 2;
	b.a_gap = LB_A_GAP;
	return b;
}

SDL_FRect gl_media_fit(const gl_boxes *b, float w, float h)
{
	float f = w > 0 && h > 0 ? fminf((float)b->media.w / w, (float)b->media.h / h) : 0;

	return (SDL_FRect){ b->media.x + (b->media.w - w * f) / 2, (float)(b->media.y + b->media.h) - h * f,
	                    w * f, h * f };
}

void gl_details_col(SDL_FRect pic, int *x, int *w)
{
	const int l = (int)lroundf(pic.x), r = (int)lroundf(pic.x + pic.w);

	*x = l + DS_INSET;
	*w = r - l - 2 * DS_INSET;
}

/* The ground at `alpha` over the worst picture for `c`: black, or white,
 * blended as the renderer does in 8 bits, rounded toward the worse */
static float worst_ratio(ds_rgb ground, int alpha, ds_rgb c)
{
	const ds_rgb black = { (uint8_t)(ground.r * alpha / 255), (uint8_t)(ground.g * alpha / 255),
	                       (uint8_t)(ground.b * alpha / 255) };
	const ds_rgb white = { (uint8_t)(255 - (255 - ground.r) * alpha / 255),
	                       (uint8_t)(255 - (255 - ground.g) * alpha / 255),
	                       (uint8_t)(255 - (255 - ground.b) * alpha / 255) };
	const float a = look_ratio(c, black), b = look_ratio(c, white);

	return a < b ? a : b;
}

int gl_ground_alpha(ds_rgb ground, ds_rgb c)
{
	int lo = 0, hi = 255;

	if (worst_ratio(ground, hi, c) < GL_TEXT_OWED) return 255;
	/* the ratio grows with the alpha: the least that holds */
	while (lo < hi) {
		int mid = (lo + hi) / 2;

		if (worst_ratio(ground, mid, c) >= GL_TEXT_OWED) hi = mid;
		else lo = mid + 1;
	}
	return lo;
}

ds_rgb gl_line_on(ds_rgb ground, int alpha, ds_rgb c, ds_rgb text)
{
	for (int n = 0; n <= 20; n++) {
		const ds_rgb m = { (uint8_t)lroundf(c.r + (text.r - c.r) * n / 20.0f),
		                   (uint8_t)lroundf(c.g + (text.g - c.g) * n / 20.0f),
		                   (uint8_t)lroundf(c.b + (text.b - c.b) * n / 20.0f) };

		if (worst_ratio(ground, alpha, m) >= GL_TEXT_OWED) return m;
	}
	return text;
}
