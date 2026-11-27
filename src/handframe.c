/* SPDX-License-Identifier: GPL-3.0-only */
/* A handheld's frame around its game. See src/handframe.h. */
#include "handframe.h"

#include <math.h>
#include <string.h>

bool hf_place(const hf_model *m, hf_kind kind, int w, int h, int *scale, int *gx, int *gy, int *gw, int *gh)
{
	int s;

	if (!m || m->cols <= 0 || m->lines <= 0 || w <= 0 || h <= 0) return false;
	s = w / m->cols < h / m->lines ? w / m->cols : h / m->lines;
	/* Whole is the largest step under the full size where the whole lens
	 * and HF_WHOLE_BODY_MM of body round it are on the panel: one step on
	 * the Pixel 2 (2x), two on the Brick, where one step smaller (4x) still
	 * cuts a Game Boy's lens. Never down to 1x: a game a quarter of the
	 * screen is no game to play (the Advance on the Pixel 2) */
	if (kind == HF_WHOLE)
		for (s--; s >= 2; s--) {
			const float pw = (float)(m->cols * s) / m->lcd_w, ph = (float)(m->lines * s) / m->lcd_h;

			if ((m->lens.x1 - m->lens.x0 + 2 * HF_WHOLE_BODY_MM) * pw <= (float)w &&
			    (m->lens.y1 - m->lens.y0 + 2 * HF_WHOLE_BODY_MM) * ph <= (float)h)
				break;
		}
	if (s < (kind == HF_WHOLE ? 2 : 1)) return false;
	*scale = s;
	*gw = m->cols * s;
	*gh = m->lines * s;
	if (kind == HF_CUT) {
		/* Where Sharp Pixels puts it: the middle, rounded down, as
		 * diatom/src/scale.c's centred() */
		*gx = (w - *gw) / 2;
		*gy = (h - *gh) / 2;
	} else {
		/* The lens's middle on the panel's, the game where the lens has it,
		 * then held on the panel */
		const float px = (float)*gw / m->lcd_w, py = (float)*gh / m->lcd_h;
		const float cx = (m->lens.x0 + m->lens.x1) / 2.0f, cy = (m->lens.y0 + m->lens.y1) / 2.0f;

		*gx = (int)lroundf((float)w / 2.0f - cx * px);
		*gy = (int)lroundf((float)h / 2.0f - cy * py);
		if (*gx < 0) *gx = 0;
		if (*gy < 0) *gy = 0;
		if (*gx + *gw > w) *gx = w - *gw;
		if (*gy + *gh > h) *gy = h - *gh;
	}
	return true;
}

void hf_px_rect(const hf_model *m, const hf_rrect *r, int gx, int gy, int gw, int gh,
                int *x0, int *y0, int *x1, int *y1)
{
	const float px = (float)gw / m->lcd_w, py = (float)gh / m->lcd_h;

	*x0 = gx + (int)lroundf(r->x0 * px);
	*y0 = gy + (int)lroundf(r->y0 * py);
	*x1 = gx + (int)lroundf(r->x1 * px);
	*y1 = gy + (int)lroundf(r->y1 * py);
}

/* The signed distance, in pixels, from a point to a shape (negative inside):
 * a box with rounded corners (Inigo Quilez's rounded box, a radius a
 * corner), cut by an arc on its top or bottom where it has one, a fillet of
 * the corner's radius between the side and the arc */
typedef struct {
	float cx, cy, hx, hy;   /* the box's centre and half sizes */
	float r[4];             /* tl, tr, br, bl, pixels */
	float ax, at_y, at_r;   /* top arc's centre y and radius (at_r 0: none) */
	float ab_y, ab_r;       /* bottom arc's */
	float ft, fb;           /* fillets beside the top and bottom arcs */
} sdf_shape;

static float box_d(const sdf_shape *s, float px, float py)
{
	const float dx = px - s->cx, dy = py - s->cy;
	const float r = dx >= 0 ? (dy >= 0 ? s->r[2] : s->r[1]) : (dy >= 0 ? s->r[3] : s->r[0]);
	const float qx = fabsf(dx) - s->hx + r, qy = fabsf(dy) - s->hy + r;
	const float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;

	return fminf(fmaxf(qx, qy), 0.0f) + sqrtf(mx * mx + my * my) - r;
}

/* Two shapes' intersection with a fillet of radius r where they meet */
static float round_cut(float a, float b, float r)
{
	const float ux = r + a > 0 ? r + a : 0, uy = r + b > 0 ? r + b : 0;

	if (r <= 0) return fmaxf(a, b);
	return fminf(-r, fmaxf(a, b)) + sqrtf(ux * ux + uy * uy);
}

static float shape_d(const sdf_shape *s, float px, float py)
{
	float d = box_d(s, px, py);

	if (s->at_r > 0)   /* inside the disk under the top arc */
		d = round_cut(d, sqrtf((px - s->ax) * (px - s->ax) + (py - s->at_y) * (py - s->at_y)) - s->at_r, s->ft);
	if (s->ab_r > 0)
		d = round_cut(d, sqrtf((px - s->ax) * (px - s->ax) + (py - s->ab_y) * (py - s->ab_y)) - s->ab_r, s->fb);
	return d;
}

static hf_rgb paint_of(const hf_model *m, const hf_part *p, int color)
{
	if (p->paint == HF_BODY) return m->body[color];
	if (p->paint == HF_BODY_DARK) return m->body_dark[color];
	return p->color;
}

/* Whether r is inside the light's ring: a part of the light */
static bool in_light(const hf_model *m, const hf_rrect *r)
{
	const float e = 0.01f;

	return m->light.x1 > m->light.x0 && r->x0 >= m->light.x0 - e && r->x1 <= m->light.x1 + e &&
	       r->y0 >= m->light.y0 - e && r->y1 <= m->light.y1 + e;
}

int hf_light_dx(const hf_model *m, int w, int gx, int gy, int gw, int gh)
{
	const float px = (float)gw / m->lcd_w;
	const int room = (int)lroundf(HF_LIGHT_ROOM_MM * px);
	int x0, y0, x1, y1, dx;

	/* A light in the lens, beside the screen: the Game Boy's and the
	 * Color's. The Advance's is on the body past the lens's right end. */
	if (m->light.x1 <= m->light.x0 || m->light.x0 < m->lens.x0 || m->light.x1 > m->lens.x1 ||
	    m->light.y0 < m->lens.y0 || m->light.y1 > m->lens.y1)
		return 0;
	hf_px_rect(m, &m->light, gx, gy, gw, gh, &x0, &y0, &x1, &y1);
	if (x0 >= room && x1 <= w - room) return 0;
	if (x1 <= gx) {
		dx = room - x0;
		return x1 + dx + room <= gx ? dx : 0;
	}
	if (x0 >= gx + gw) {
		dx = w - room - x1;
		return x0 + dx - room >= gx + gw ? dx : 0;
	}
	return 0;
}

void hf_render(const hf_model *m, int color, int w, int h, int gx, int gy, int gw, int gh, uint8_t *bgra)
{
	const float px = (float)gw / m->lcd_w, py = (float)gh / m->lcd_h;
	/* The light where the panel shows it whole (hf_light_dx) */
	const int ldx = hf_light_dx(m, w, gx, gy, gw, gh);
	int i, x, y;

	if (color < 0 || color >= m->ncolors) color = 0;
	/* Black where the handheld is not: past its body's edge, as a handheld
	 * shot on black is. The body is a part, the first. */
	for (i = 0; i < w * h; i++) {
		bgra[i * 4] = bgra[i * 4 + 1] = bgra[i * 4 + 2] = 0;
		bgra[i * 4 + 3] = 255;
	}
	for (i = 0; i < m->nparts; i++) {
		const hf_part *p = &m->part[i];
		const hf_rgb c = paint_of(m, p, color);
		const float rs = px < py ? px : py;
		int x0, y0, x1, y1, k;
		sdf_shape sh;

		hf_px_rect(m, &p->shape, gx, gy, gw, gh, &x0, &y0, &x1, &y1);
		if (ldx && in_light(m, &p->shape)) {
			x0 += ldx;
			x1 += ldx;
		}
		if (x1 <= x0 || y1 <= y0) continue;
		sh.cx = (float)(x0 + x1) / 2.0f;
		sh.cy = (float)(y0 + y1) / 2.0f;
		sh.hx = (float)(x1 - x0) / 2.0f;
		sh.hy = (float)(y1 - y0) / 2.0f;
		sh.ax = sh.cx;
		/* A radius in pixels, at the smaller of the two scales (a corner is
		 * round on the face), no more than half the shorter side; none at a
		 * corner beside an arc, whose fillet it is instead */
		for (k = 0; k < 4; k++) {
			float r = p->shape.r[k] * rs;

			if (r > sh.hx) r = sh.hx;
			if (r > sh.hy) r = sh.hy;
			sh.r[k] = r < 0 ? 0 : r;
		}
		sh.at_r = sh.ab_r = 0;
		sh.ft = sh.fb = 0;
		if (p->shape.sag_top > 0) {
			const float sg = p->shape.sag_top * py, a = sh.hx;

			sh.at_r = (a * a + sg * sg) / (2.0f * sg);
			sh.at_y = (float)y0 + sh.at_r;
			sh.ft = fmaxf(sh.r[0], sh.r[1]);
			sh.r[0] = sh.r[1] = 0;
		}
		if (p->shape.sag_bot > 0) {
			const float sg = p->shape.sag_bot * py, a = sh.hx;

			sh.ab_r = (a * a + sg * sg) / (2.0f * sg);
			sh.ab_y = (float)y1 - sh.ab_r;
			sh.fb = fmaxf(sh.r[2], sh.r[3]);
			sh.r[2] = sh.r[3] = 0;
		}
		for (y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
			for (x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
				/* Coverage from the distance at the pixel's centre: a straight
				 * edge on a pixel boundary is 0 or 1, an arc is smoothed */
				const float dd = shape_d(&sh, (float)x + 0.5f, (float)y + 0.5f);
				const float a = 0.5f - dd < 0 ? 0 : 0.5f - dd > 1 ? 1 : 0.5f - dd;
				uint8_t *d = bgra + ((size_t)y * w + x) * 4;

				if (a <= 0.0f) continue;
				d[0] = (uint8_t)lroundf(c.b * a + d[0] * (1.0f - a));
				d[1] = (uint8_t)lroundf(c.g * a + d[1] * (1.0f - a));
				d[2] = (uint8_t)lroundf(c.r * a + d[2] * (1.0f - a));
			}
	}
}
