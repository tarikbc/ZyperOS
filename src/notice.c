/* SPDX-License-Identifier: GPL-3.0-only */
/* See notice.h for why the launcher draws this and Diatom shows it. */
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "notice.h"
#include "platform.h"
#include "ui.h"

#define NOTICE_MAX_W 1024        /* Diatom refuses anything wider */
#define NOTICE_MAX_H 256

/* One pixel of the destination, straight alpha, BGRA byte order - which is
 * what SDL_PIXELFORMAT_ARGB8888 already is in memory on a little-endian host,
 * and both ends of this are little-endian.
 *
 * Laid over what is there by how much of it there is: over nothing, the
 * color as it is at its own alpha. It used to mix the color with the
 * destination's bytes as if they were an opaque color, and the buffer starts
 * as transparent black, so the 85% slab was stored 15% of the way to black -
 * unseen on a dark slab, and a grey one where Beard's is surface. */
static void blend(uint8_t *dst, unsigned b, unsigned g, unsigned r, unsigned a)
{
	unsigned keep, out;

	if (a == 0) return;
	if (a == 255) {
		dst[0] = (uint8_t)b; dst[1] = (uint8_t)g;
		dst[2] = (uint8_t)r; dst[3] = 255;
		return;
	}
	keep = dst[3] * (255 - a);     /* the destination's share, times 255 */
	out = a * 255 + keep;          /* the alpha this leaves, times 255 */
	dst[0] = (uint8_t)((b * a * 255 + dst[0] * keep + out / 2) / out);
	dst[1] = (uint8_t)((g * a * 255 + dst[1] * keep + out / 2) / out);
	dst[2] = (uint8_t)((r * a * 255 + dst[2] * keep + out / 2) / out);
	dst[3] = (uint8_t)((out + 127) / 255);
}

static void draw_text(uint8_t *out, int ow, int oh, TTF_Font *f,
                      const char *s, int x, int y, SDL_Color col)
{
	SDL_Surface *t;
	int sx, sy;

	if (!f || !s || !*s) return;
	t = ui_text_surface(f, s, col);
	if (!t) return;

	/* Blended gives ARGB8888 with per-pixel alpha - the antialiasing is IN
	 * the alpha, so ignoring it would give the text hard jagged edges over
	 * the game. */
	for (sy = 0; sy < t->h; sy++) {
		const uint8_t *row = (const uint8_t *)t->pixels + (size_t)sy * t->pitch;
		int dy = y + sy;

		if (dy < 0 || dy >= oh) continue;
		for (sx = 0; sx < t->w; sx++) {
			int dx = x + sx;

			if (dx < 0 || dx >= ow) continue;
			blend(out + ((size_t)dy * ow + dx) * 4,
			      row[sx * 4 + 0], row[sx * 4 + 1],
			      row[sx * 4 + 2], row[sx * 4 + 3]);
		}
	}
	SDL_FreeSurface(t);
}

/* A badge, `size` pixels square, ARGB8888 pixels to read from, or NULL. Drawn
 * with the corners of a row (DS_RADIUS_ROW), as a picture laid on the slab. */
static SDL_Surface *badge_load(const char *path, int size)
{
	SDL_Surface *img, *argb, *out;

	if (!path || !*path || size <= 0) return NULL;
	if (!(img = IMG_Load(path))) return NULL;
	argb = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(img);
	if (!argb) return NULL;
	out = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
	if (out) {
		SDL_SetSurfaceBlendMode(argb, SDL_BLENDMODE_NONE);
		/* Linear where SDL has it: 64 pixels drawn at about 70 */
		if (SDL_BlitScaled(argb, NULL, out, NULL) != 0) {
			SDL_FreeSurface(out);
			out = NULL;
		}
	}
	SDL_FreeSurface(argb);
	return out;
}

static void draw_badge(uint8_t *px, int w, int h, const SDL_Surface *b, int bx, int by)
{
	int corner = DS_RADIUS_ROW < b->w / 2 ? DS_RADIUS_ROW : b->w / 2, x, y;

	for (y = 0; y < b->h; y++) {
		const uint8_t *row = (const uint8_t *)b->pixels + (size_t)y * b->pitch;

		if (by + y < 0 || by + y >= h) continue;
		for (x = 0; x < b->w; x++) {
			int dx = x < corner ? corner - x : (x >= b->w - corner ? x - (b->w - corner - 1) : 0);
			int dy = y < corner ? corner - y : (y >= b->h - corner ? y - (b->h - corner - 1) : 0);
			unsigned a = row[x * 4 + 3];

			if (bx + x < 0 || bx + x >= w) continue;
			if (dx || dy) {
				double d = sqrt((double)dx * dx + (double)dy * dy) - corner;

				if (d > 0.5) continue;
				if (d > -0.5) a = (unsigned)(a * (0.5 - d));
			}
			blend(px + ((size_t)(by + y) * w + bx + x) * 4,
			      row[x * 4 + 0], row[x * 4 + 1], row[x * 4 + 2], a);
		}
	}
}

/* A bar of `color` from x to x+bw, `bh` tall, its ends round */
static void draw_bar(uint8_t *px, int w, int h, int x0, int y0, int bw, int bh, ds_rgb c)
{
	int x, y;
	double r = bh / 2.0;

	for (y = 0; y < bh; y++) {
		if (y0 + y < 0 || y0 + y >= h) continue;
		for (x = 0; x < bw; x++) {
			double ex = x < r ? r - x - 0.5 : (x >= bw - r ? x - (bw - r) + 0.5 : 0);
			double ey = y + 0.5 - r, d = sqrt(ex * ex + ey * ey) - r;
			unsigned a = 255;

			if (x0 + x < 0 || x0 + x >= w) continue;
			if (ex > 0) {
				if (d > 0.5) continue;
				if (d > -0.5) a = (unsigned)(255 * (0.5 - d));
			}
			blend(px + ((size_t)(y0 + y) * w + x0 + x) * 4, c.b, c.g, c.r, a);
		}
	}
}

/* Signed distances, in the mark's own units: to a disc, and to a capsule, the
 * segment a-b fattened by r */
static double sd_disc(double x, double y, double cx, double cy, double r)
{
	return sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}

static double sd_capsule(double x, double y, double ay, double by, double r)
{
	/* upright: a and b on x = 0 */
	double t = (y - ay) / (by - ay);

	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;
	return sd_disc(x, y, 0.0, ay + (by - ay) * t, r);
}

/* The thermometer, `size` pixels tall and half as wide, antialiased by its
 * distance as the slab's corners are: a stem and a bulb outlined in a
 * 3.4-unit stroke of `ink`, and the mercury, a disc in the bulb and a bar up
 * the stem, in `fill`, in a 28 x 56 box (the battery and heat lab's,
 * docs/design/mock). ARGB8888 pixels, straight alpha. */
SDL_Surface *notice_heat_mark(int size, ds_rgb ink, ds_rgb fill)
{
	SDL_Surface *s;
	int w = (size + 1) / 2, x, y;
	double k = 56.0 / size;   /* units a pixel */

	if (size <= 0) return NULL;
	s = SDL_CreateRGBSurfaceWithFormat(0, w, size, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	for (y = 0; y < size; y++) {
		uint8_t *row = (uint8_t *)s->pixels + (size_t)y * s->pitch;

		for (x = 0; x < w; x++) {
			double ux = (x + 0.5) * k - 14.0, uy = (y + 0.5) * k - 26.0;
			double body = fmin(sd_capsule(ux, uy, -18.0, 9.0, 6.0),
			                   sd_disc(ux, uy, 0.0, 17.0, 10.0));
			double merc = fmin(sd_disc(ux, uy, 0.0, 17.0, 6.0),
			                   sd_capsule(ux, uy, -12.0, 16.0, 2.4)) / k;
			double ring = (fabs(body) - 1.7) / k;   /* in pixels */
			double am = 0.5 - merc, ar = 0.5 - ring, a;
			const ds_rgb *c;

			am = am < 0.0 ? 0.0 : am > 1.0 ? 1.0 : am;
			ar = ar < 0.0 ? 0.0 : ar > 1.0 ? 1.0 : ar;
			/* The two never meet (the mercury is inside the stroke's gap),
			 * so a pixel is one or the other */
			c = am >= ar ? &fill : &ink;
			a = am >= ar ? am : ar;
			row[x * 4 + 0] = c->b;
			row[x * 4 + 1] = c->g;
			row[x * 4 + 2] = c->r;
			row[x * 4 + 3] = (uint8_t)(a * 255.0 + 0.5);
		}
	}
	return s;
}

static bool render(const char *heading, const char *body, const char *badge,
                   notice_mark mark, int value, int target, const char *path,
                   int theme_ix);

bool notice_render(const char *heading, const char *body, const char *path,
                   int theme_ix)
{
	return render(heading, body, NULL, NOTICE_MARK_NONE, 0, 0, path, theme_ix);
}

bool notice_render_ex(const char *heading, const char *body, const char *badge,
                      int value, int target, const char *path, int theme_ix)
{
	return render(heading, body, badge, NOTICE_MARK_NONE, value, target, path, theme_ix);
}

bool notice_render_mark(const char *heading, const char *body, notice_mark mark,
                        const char *path, int theme_ix)
{
	return render(heading, body, NULL, mark, 0, 0, path, theme_ix);
}

static bool render(const char *heading, const char *body, const char *badge,
                   notice_mark mark, int value, int target, const char *path,
                   int theme_ix)
{
	/* The NAME is the line worth reading, so it gets the larger face and the
	 * brighter ink; "Achievement unlocked" is context and sits above it,
	 * smaller and dimmer. These were the other way round in the first
	 * version, which put the least interesting words in the biggest type -
	 * obvious the moment it was rendered and looked at, and invisible while
	 * it was only being reasoned about. DESIGN.md 11.7: the heading in
	 * **meta** and **text-dim**, the body in **label** and **text**. */
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	int padx, pady, w, h, x, y, corner, maxw;
	char headfit[192], bodyfit[192];
	uint8_t *px;
	FILE *f;
	unsigned char hdr[8];
	/* The badge and the strip (notice_render_ex): the badge as tall as the
	 * two lines, a gap of the horizontal padding after it */
	bool prog = target > 0;
	int bsize = 0, bgap = 0, tx0, th, l1, l2, sy = 0, sh = DS_BAR;
	SDL_Surface *bs = NULL;
	/* The theme the caller says, never one read once and kept: see notice.h */
	const ds_theme *t = theme_at(theme_ix);
	const ds_rgb slab = t->surface;

	if (!fb) return false;
	if (!fh) fh = fb;

	/* Tighter than square. A notice sits over someone's game for four
	 * seconds, so it should be the words and little else - the first version
	 * used one padding value everywhere and read as a slab with text in the
	 * middle of it.
	 *
	 * The vertical is smaller than the horizontal because the two lines carry
	 * their own leading already, and a third of the body's descender room -
	 * the quarter of its line under the baseline (ui_line_y) - comes off the
	 * bottom: without it the gap under the last line is a descender deep even
	 * when nothing descends. From the line and not from TTF_FontDescent, so
	 * the slab is the shape it was whichever face is on the card. */
	padx = ui_font_line(UI_F_MENU) / 3;
	pady = ui_font_line(UI_F_MENU) / 6;

	/* A margin off the panel edge as well as off Diatom's hard limit: a
	 * notice that runs the full width of the screen stops reading as a thing
	 * laid over the game and starts reading as part of it. */
	maxw = ZYPEROS_SCREEN_W * 5 / 6;
	if (maxw > NOTICE_MAX_W) maxw = NOTICE_MAX_W;
	maxw -= padx * 2;
	/* The text block: two lines, or for progress the name, the strip with a
	 * padding over and under it, and the count in the small face */
	l1 = ui_font_line(UI_F_META);
	l2 = prog ? ui_font_line(UI_F_META) : ui_font_line(UI_F_LABEL);
	th = l1 + l2 + (prog ? sh + pady * 2 : 0);
	/* A mark is drawn here, as tall as a badge and as narrow as it is */
	if ((badge && (bs = badge_load(badge, th - pady))) ||
	    (mark == NOTICE_MARK_HEAT && (bs = notice_heat_mark(th - pady, t->warning, t->warning)))) {
		bsize = bs->w;
		bgap = padx;
		maxw -= bsize + bgap;
	}
	/* Not optional. Diatom REFUSES an overlay wider than the panel rather than
	 * clipping it, which is the right call there and means a long name would
	 * silently show nothing at all. 144 of the achievement titles in the
	 * 180-ROM test library are over 40 characters and the longest is 64 -
	 * "From Johnny, Harris, Brooklyn Bob, and Reggie! Yeah Even Reggie!" - so
	 * this is the common case, not the edge. */
	/* The count is in the small face, as the heading is */
	if (prog) fb = fh;
	ui_fit_text(fb, body, bodyfit, sizeof bodyfit, maxw);
	ui_fit_text(fh, heading, headfit, sizeof headfit, maxw);
	body = bodyfit;
	heading = headfit;

	w = ui_text_width(fb, body);
	x = ui_text_width(fh, heading);
	if (x > w) w = x;
	/* A strip as wide as the name, and never a stub under a short one */
	if (prog && w < ui_font_line(UI_F_LABEL) * 4) w = ui_font_line(UI_F_LABEL) * 4;
	tx0 = padx + bsize + bgap;
	w += padx * 2 + bsize + bgap;
	h = th + pady * 2 - (prog ? 0 : ui_font_line(UI_F_LABEL) / 12);
	if (w > NOTICE_MAX_W) w = NOTICE_MAX_W;
	if (h > NOTICE_MAX_H) h = NOTICE_MAX_H;
	if (w <= 0 || h <= 0) { if (bs) SDL_FreeSurface(bs); return false; }

	px = calloc((size_t)w * (size_t)h, 4);
	if (!px) { if (bs) SDL_FreeSurface(bs); return false; }

	/* A slab of the theme's **surface** at 85%, rounded the way every menu
	 * panel in the launcher is rounded, DS_RADIUS_PANEL (DESIGN.md 11.7). The
	 * first version chamfered the corners - one comparison per pixel - and
	 * the comment justified it as reading the same from a meter away. It does
	 * not read the same. It reads as a different piece of software borrowing
	 * the screen for a moment. Never more than half the slab's height, so a
	 * short one is a pill and not a shape the corners overrun. */
	corner = DS_RADIUS_PANEL < h / 2 ? DS_RADIUS_PANEL : h / 2;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int dx = x < corner ? corner - x : (x >= w - corner ? x - (w - corner - 1) : 0);
			int dy = y < corner ? corner - y : (y >= h - corner ? y - (h - corner - 1) : 0);
			unsigned a = 217;   /* 85% */

			if (dx || dy) {
				/* Coverage from the distance to the arc, so the curve is
				 * antialiased rather than staircased. More than half a pixel
				 * outside is nothing; more than half inside is solid. */
				double d = sqrt((double)dx * dx + (double)dy * dy) - corner;

				if (d > 0.5) continue;
				if (d > -0.5) a = (unsigned)(a * (0.5 - d));
			}
			blend(px + ((size_t)y * w + x) * 4, slab.b, slab.g, slab.r, a);
		}
	}

	/* With a badge: the badge in the middle of the slab's height at the
	 * left, and the lines left-aligned beside it, as the achievements lab
	 * drew it. A progress notice is the name, the strip and the count. */
	if (bs) {
		SDL_LockSurface(bs);
		draw_badge(px, w, h, bs, padx, (h - bs->h) / 2);
		SDL_UnlockSurface(bs);
		SDL_FreeSurface(bs);
	}
	if (prog) {
		int sw = w - tx0 - padx;
		int fill = value <= 0 ? 0 : value >= target ? sw
		         : (int)((long long)sw * value / target);

		sy = pady + l1 + pady;
		draw_text(px, w, h, fh, heading, tx0, ui_line_y(UI_F_META, pady),
		          ui_col(t->text_dim, 255));
		draw_bar(px, w, h, tx0, sy, sw, sh, t->track);
		if (fill >= sh) draw_bar(px, w, h, tx0, sy, fill, sh, t->accent_ink);
		draw_text(px, w, h, fb, body, tx0, ui_line_y(UI_F_META, sy + sh + pady),
		          ui_col(t->text, 255));
	} else if (bsize) {
		draw_text(px, w, h, fh, heading, tx0, ui_line_y(UI_F_META, pady),
		          ui_col(t->text_dim, 255));
		draw_text(px, w, h, fb, body, tx0,
		          ui_line_y(UI_F_LABEL, pady + ui_font_line(UI_F_META)),
		          ui_col(t->text, 255));
	} else {
		/* Both lines centered, not left-aligned. The slab is as wide as its
		 * wider line, so the shorter one sat against the left edge with a
		 * gap after it - which reads as text that failed to fill rather
		 * than as a caption. It is also what menu_draw does with a heading,
		 * so the two agree. */
		draw_text(px, w, h, fh, heading,
		          (w - ui_text_width(fh, heading)) / 2,
		          ui_line_y(UI_F_META, pady),
		          ui_col(t->text_dim, 255));
		draw_text(px, w, h, fb, body,
		          (w - ui_text_width(fb, body)) / 2,
		          ui_line_y(UI_F_LABEL, pady + ui_font_line(UI_F_META)),
		          ui_col(t->text, 255));
	}

	f = fopen(path, "wb");
	if (!f) { free(px); return false; }
	memcpy(hdr, "DTOV", 4);
	hdr[4] = (unsigned char)w;  hdr[5] = (unsigned char)(w >> 8);
	hdr[6] = (unsigned char)h;  hdr[7] = (unsigned char)(h >> 8);
	if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr ||
	    fwrite(px, 4, (size_t)w * (size_t)h, f) != (size_t)w * (size_t)h) {
		fclose(f);
		free(px);
		return false;
	}
	free(px);
	/* The close is where a buffered write lands, and where a full /tmp says
	 * so. A file cut short is not one to hand Diatom: false, and the caller
	 * sends no OVERLAY line for it. */
	return fclose(f) == 0;
}
