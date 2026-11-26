/* SPDX-License-Identifier: GPL-3.0-only */
/* The cassette's textures: see cassette.h. Drawn on the CPU once, as signed
 * distances, each part over the last with a one-unit soft edge
 * (src/lookart.h). */
#include "cassette.h"

#include "ds/sdf.h"
#include "lookart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The materials, the only colors the look has of its own besides the
 * cover's (DESIGN.md 11.64): the smoke-grey shell and its moulded edge, the
 * dark inside seen through the window, the screws, the hubs' white plastic,
 * the tape's brown */
static const la_rgba SMOKE     = { 214 / 255.f, 212 / 255.f, 203 / 255.f, 0.55f };
static const la_rgba MOULD     = { 156 / 255.f, 154 / 255.f, 146 / 255.f, 1.0f };
static const la_rgba INSIDE    = { 30 / 255.f, 32 / 255.f, 30 / 255.f, 0.62f };
static const la_rgba SLOT      = { 0.0f, 0.0f, 0.0f, 0.45f };
static const la_rgba HOLE      = { 18 / 255.f, 20 / 255.f, 19 / 255.f, 1.0f };
static const la_rgba TAPE      = { 70 / 255.f, 51 / 255.f, 42 / 255.f, 1.0f };
static const la_rgba HUB_DARK  = { 217 / 255.f, 216 / 255.f, 208 / 255.f, 1.0f };
static const la_rgba HUB_LIGHT = { 244 / 255.f, 243 / 255.f, 238 / 255.f, 1.0f };
static const la_rgba TOOTH     = { 154 / 255.f, 154 / 255.f, 146 / 255.f, 1.0f };
static const la_rgba SPINDLE   = { 30 / 255.f, 32 / 255.f, 31 / 255.f, 1.0f };
/* The pack, lighter where the tape is wound tight near the hub and darker
 * at its edge: 0.45, 0.8 and 1 of its radius */
static const la_rgba PACK_IN   = { 90 / 255.f, 66 / 255.f, 50 / 255.f, 1.0f };
static const la_rgba PACK_MID  = { 70 / 255.f, 51 / 255.f, 42 / 255.f, 1.0f };
static const la_rgba PACK_EDGE = { 46 / 255.f, 34 / 255.f, 28 / 255.f, 1.0f };

/* q: the label's box and corner, then the window's pill: the paper with the
 * window cut from it */
static float sd_paper(const float *q, float x, float y)
{
	float label = ds_sd_rbox(x, y, q[0], q[1], q[2], q[3], q[4]);
	float pill = ds_sd_rbox(x, y, q[5], q[6], q[7], q[8], (q[8] - q[6]) / 2);

	return fmaxf(label, -pill);
}

/* The pack's brown from its middle out (q: cx cy r) */
static la_rgba pack_col(const float *q, float x, float y)
{
	float t = hypotf(x - q[0], y - q[1]) / q[2];

	return t < 0.45f ? PACK_IN : t < 0.8f ? la_mix(PACK_IN, PACK_MID, (t - 0.45f) / 0.35f)
	                                      : la_mix(PACK_MID, PACK_EDGE, (t - 0.8f) / 0.2f);
}

/* The glass's light: white, 16% at the window's top to none at its middle
 * (q: the pill's box) */
static la_rgba glass_col(const float *q, float x, float y)
{
	float t = (y - q[1]) / ((q[3] - q[1]) / 2);

	(void)x;
	return (la_rgba){ 1, 1, 1, 0.16f * (1 - ds_sd_clamp(t, 0, 1)) };
}

/* A screw's head, 3.4 mm, sunk in its hole, its cross slot */
static void screw(la_canvas *c, float cx, float cy)
{
	const float d[3] = { cx, cy, 10 }, ring[4] = { cx, cy, 10, 1.5f };
	const float h[5] = { cx - 6, cy, cx + 6, cy, 2 }, v[5] = { cx, cy - 6, cx, cy + 6, 2 };

	la_paint(c, la_disc, NULL, d, MOULD, cx - 10, cy - 10, cx + 10, cy + 10);
	la_paint(c, la_ring, NULL, ring, SLOT, cx - 12, cy - 12, cx + 12, cy + 12);
	la_paint(c, la_seg, NULL, h, SLOT, cx - 7, cy - 2, cx + 7, cy + 2);
	la_paint(c, la_seg, NULL, v, SLOT, cx - 2, cy - 7, cx + 2, cy + 7);
}

/* The shell, its screws and its head end, the label with the window cut
 * from it, and the dark inside the window: everything that does not move */
static SDL_Texture *shell(SDL_Renderer *r, const cas_tex *t)
{
	la_canvas c;
	const float body[5] = { 1.5f, 1.5f, CAS_W - 1.5f, CAS_H - 1.5f, 16 };
	const float rim[6] = { 1.5f, 1.5f, CAS_W - 1.5f, CAS_H - 1.5f, 16, 3 };
	/* The head end, a trapezoid 54.3 mm along the foot and 40.7 along
	 * its top, 16.3 high */
	const float head[8] = { CAS_WIN_X0, CAS_H, CAS_WIN_X1, CAS_H, 416, 280, 176, 280 };
	/* Its openings: the pinch rollers' either side, the head's between */
	const float hole_l[5] = { 192, 320, 232, 352, 4 }, hole_r[5] = { 360, 320, 400, 352, 4 },
	            hole_m[5] = { 272, 328, 320, 352, 4 };
	const float tape[5] = { 180, 354, 412, 354, 4 };
	const float inside[5] = { CAS_WIN_X0, CAS_WIN_Y0, CAS_WIN_X1, CAS_WIN_Y1, (CAS_WIN_Y1 - CAS_WIN_Y0) / 2.0f };
	const float paper[9] = { CAS_LABEL_X0, CAS_LABEL_Y0, CAS_LABEL_X1, CAS_LABEL_Y1, 12,
	                         CAS_WIN_X0, CAS_WIN_Y0, CAS_WIN_X1, CAS_WIN_Y1 };

	if (!la_new_scaled(&c, t->w, t->h, (float)t->w / CAS_W, (float)t->h / CAS_H)) return NULL;
	la_paint(&c, la_rbox, NULL, body, SMOKE, 0, 0, CAS_W, CAS_H);
	la_paint(&c, la_rbox_line, NULL, rim, MOULD, 0, 0, CAS_W, CAS_H);
	screw(&c, 24, 24);
	screw(&c, CAS_W - 24, 24);
	screw(&c, 24, CAS_H - 24);
	screw(&c, CAS_W - 24, CAS_H - 24);
	la_paint(&c, la_poly4, NULL, head, MOULD, CAS_WIN_X0, 280, CAS_WIN_X1, CAS_H);
	screw(&c, CAS_W / 2, 300);
	la_paint(&c, la_rbox, NULL, hole_l, HOLE, 192, 320, 232, 352);
	la_paint(&c, la_rbox, NULL, hole_r, HOLE, 360, 320, 400, 352);
	la_paint(&c, la_rbox, NULL, hole_m, HOLE, 272, 328, 320, 352);
	la_paint(&c, la_seg, NULL, tape, TAPE, 176, 350, 416, 358);
	la_paint(&c, la_rbox, NULL, inside, INSIDE, CAS_WIN_X0, CAS_WIN_Y0, CAS_WIN_X1, CAS_WIN_Y1);
	la_paint(&c, sd_paper, NULL, paper, la_role(t->paper), CAS_LABEL_X0, CAS_LABEL_Y0, CAS_LABEL_X1,
	      CAS_LABEL_Y1);
	return la_texture(r, &c);
}

/* The window's box over the reels: the label's paper in its corners round
 * the pill, the pill's moulded edge, and the light on its glass */
static SDL_Texture *glass(SDL_Renderer *r, const cas_tex *t)
{
	const float w = CAS_WIN_X1 - CAS_WIN_X0, h = CAS_WIN_Y1 - CAS_WIN_Y0;
	/* The paper: the box less the pill (the label's corner is far away) */
	const float paper[9] = { -40, -40, w + 40, h + 40, 0, 0, 0, w, h };
	const float pill[5] = { 0, 0, w, h, h / 2 }, edge[6] = { 0, 0, w, h, h / 2, 2 };
	la_canvas c;

	if (!la_new_scaled(&c, (int)lroundf(w * t->w / CAS_W), (int)lroundf(h * t->h / CAS_H), (float)t->w / CAS_W,
	                   (float)t->h / CAS_H))
		return NULL;
	la_paint(&c, la_rbox, glass_col, pill, (la_rgba){ 0 }, 0, 0, w, h);
	la_paint(&c, sd_paper, NULL, paper, la_role(t->paper), 0, 0, w, h);
	la_paint(&c, la_rbox_line, NULL, edge, MOULD, 0, 0, w, h);
	return la_texture(r, &c);
}

/* A reel's hub: the white plastic the tape winds on, the clamp that holds
 * the tape's end in its rim, and the six teeth round the spindle's hole */
static SDL_Texture *hub(SDL_Renderer *r, const cas_tex *t)
{
	const float sc = (float)t->h / CAS_H;
	const int side = (int)ceilf(CAS_HUB_R * sc) * 2;
	const float m = (float)side / 2 / sc;
	const float disc[3] = { m, m, CAS_HUB_R - 0.5f }, rim[4] = { m, m, CAS_HUB_R - 2, 2 };
	const float spindle[3] = { m, m, 24 }, clamp[5] = { m + 46, m, m + 58, m, 6 };
	la_canvas c;
	int k;

	if (!la_new_scaled(&c, side, side, sc, sc)) return NULL;
	la_paint(&c, la_disc, NULL, disc, t->dark ? HUB_DARK : HUB_LIGHT, 0, 0, 2 * m, 2 * m);
	la_paint(&c, la_ring, NULL, rim, MOULD, 0, 0, 2 * m, 2 * m);
	la_paint(&c, la_seg, NULL, clamp, TOOTH, m + 40, m - 4, m + 62, m + 4);
	la_paint(&c, la_disc, NULL, spindle, SPINDLE, m - 24, m - 24, m + 24, m + 24);
	for (k = 0; k < 6; k++) {
		/* The hub's own plastic, from the hole's wall in toward the
		 * spindle, between the clamp's way round */
		float a = (float)k * (float)M_PI / 3 + (float)M_PI / 6, ca = cosf(a), sa = sinf(a);
		const float tooth[5] = { m + ca * 26, m + sa * 26, m + ca * 16, m + sa * 16, 7 };

		la_paint(&c, la_seg, NULL, tooth, t->dark ? HUB_DARK : HUB_LIGHT, m - 30, m - 30, m + 30, m + 30);
	}
	return la_texture(r, &c);
}

/* A full pack of tape, its brown darker toward its edge */
static SDL_Texture *pack(SDL_Renderer *r, const cas_tex *t)
{
	const float k = (float)t->h / CAS_H;
	const int side = (int)ceilf(CAS_PACK_R * k) * 2;
	const float m = (float)side / 2 / k;
	const float q[3] = { m, m, CAS_PACK_R - 0.5f / k };
	la_canvas c;

	if (!la_new_scaled(&c, side, side, k, k)) return NULL;
	la_paint(&c, la_disc, pack_col, q, (la_rgba){ 0 }, 0, 0, 2 * m, 2 * m);
	return la_texture(r, &c);
}

void cas_free(cas_tex *t)
{
	if (t->shell) SDL_DestroyTexture(t->shell);
	if (t->glass) SDL_DestroyTexture(t->glass);
	if (t->hub) SDL_DestroyTexture(t->hub);
	if (t->pack) SDL_DestroyTexture(t->pack);
	t->shell = t->glass = t->hub = t->pack = NULL;
}

bool cas_ready(SDL_Renderer *r, cas_tex *t, const ds_theme *th, bool dark, int w, int h)
{
	if (t->shell && t->glass && t->hub && t->pack && !memcmp(&t->paper, &th->surface, sizeof t->paper) &&
	    t->dark == dark && t->w == w && t->h == h)
		return true;
	{
		Uint32 t0 = SDL_GetTicks();

		cas_free(t);
		t->paper = th->surface;
		t->dark = dark;
		t->w = w;
		t->h = h;
		t->shell = shell(r, t);
		t->glass = glass(r, t);
		t->hub = hub(r, t);
		t->pack = pack(r, t);
		/* Once a theme: what it costs, for the device's log */
		{
			SDL_Texture *all[4] = { t->shell, t->glass, t->hub, t->pack };
			long bytes = 0;
			int k, tw, th2;

			for (k = 0; k < 4; k++)
				if (all[k] && SDL_QueryTexture(all[k], NULL, NULL, &tw, &th2) == 0) bytes += (long)tw * th2 * 4;
			fprintf(stderr, "mp4: the cassette drawn in %u ms, %ld KB of textures\n",
			        (unsigned)(SDL_GetTicks() - t0), bytes / 1024);
		}
	}
	return t->shell && t->glass && t->hub && t->pack;
}
