/* SPDX-License-Identifier: GPL-3.0-only */
#include "clamp.h"
#include "coverflow.h"
#include "design.h"

#include <math.h>
#include <string.h>

/* End reflections one grid step above the count line. */
#define FLOOR_UNDER_COUNT ((690.0f - 8.0f) / 768.0f)

const cf_layout CF_LAYOUT_GAMES = {
	.size = 0.60f, .aspect = 0.72f, .step = 0.74f, .side_scale = 0.62f,
	.center_y = 0.47f, .tilt = 0.82f, .reflect = 1.52f,
	.side_alpha = 150, .strips = 16,
	.equal_area = true, .floor = FLOOR_UNDER_COUNT,
};

/* Muse's shelf: album covers, which are square, where the games layouts are
 * frames for tall boxes. A square contained in a 0.72 frame gets the frame's
 * WIDTH, 391px of a 461px-tall frame - covers came out smaller than the box
 * art around them, and Eric asked for them larger. A square frame the same
 * height gives them all of it, 461px, and the center moves down so the top
 * clears the artist line under the album's name. Otherwise the games rows'
 * own numbers, since a shelf of albums should move like a shelf of games. */
const cf_layout CF_LAYOUT_ALBUMS = {
	.size = 0.60f, .aspect = 1.00f, .step = 0.74f, .side_scale = 0.62f,
	.center_y = 0.54f, .tilt = 0.82f, .reflect = 1.52f,
	.side_alpha = 150, .strips = 16, .floor = FLOOR_UNDER_COUNT,
};


void cf_focus_rect(const cf_layout *lay, int screen_w, int screen_h, SDL_Rect *out)
{
	float ch = screen_h * lay->size;
	float cw = ch * lay->aspect;
	out->w = (int)cw;
	out->h = (int)ch;
	out->x = (int)(screen_w * 0.5f - cw * 0.5f);
	out->y = (int)(screen_h * lay->center_y - ch * 0.5f);
}

/* Every move takes the same time, whatever its distance: DESIGN.md 8.2's
 * **move**. Crossing the shelf is not a longer journey than stepping one card,
 * it is a faster one.
 *
 * This replaced an exponential ease toward the target with a speed cap over
 * it, and a teleport past six cards. The ease alone converged in time
 * proportional to log(distance), which was nearly constant already; the cap
 * made anything past a card and a half take time proportional to DISTANCE, so
 * a letter jump across a big shelf became a wait, and the teleport existed to
 * hide the worst of it. One duration removes the need for both. */
#define MOVE_MS ((float)DS_MS_MOVE)

/* The row glides only as far as the eye can follow in a move's time: at most
 * one card a frame at 60fps, on average. See CF_WARM_CARDS. */
_Static_assert(CF_WARM_CARDS * 1000 <= DS_MS_MOVE * 60,
               "a glide of CF_WARM_CARDS crosses more than a card a frame");

/* The clock every move is read from. See cf_clock. */
static Uint32 (*clock_fn)(void);

static Uint32 ticks(void) { return clock_fn ? clock_fn() : SDL_GetTicks(); }

void cf_clock(Uint32 (*now)(void)) { clock_fn = now; }

/* A row steps on **out** (ds_out), which spends most of its travel at once
 * and is why a single card reads as immediate though it takes as long as
 * forty.
 *
 * A cut's departure is the other, **in** (ds_in): it starts at rest and is at
 * its fastest when it ends. That is backwards for a step and exactly right for
 * a cut. The end of this move is where the discontinuity is, and a cut made
 * while the eye is tracking fast motion is close to invisible - it is the
 * ordinary cut on action - whereas one made at rest is the most visible cut
 * available, because the eye has settled and has nothing left to do but
 * notice it. */

static bool cf_loops(int count) { return count >= 2; }

static int cf_half(int count)
{
	int h = count - 1;
	return h < CF_HALF_WINDOW ? h : CF_HALF_WINDOW;
}

void cf_reset(coverflow *cf, int cursor)
{
	memset(cf, 0, sizeof *cf);
	cf->pos = cf->from = cf->target = (float)cursor;
	cf->last_cursor = cursor;
	cf->primed = true;
}

void cf_set_cursor_dir(coverflow *cf, int cursor, int count, int dir)
{
	if (!cf->primed) { cf_reset(cf, cursor); return; }
	if (cursor == cf->last_cursor) return;

	bool loops = cf_loops(count);
	int raw = cursor - cf->last_cursor;
	if (loops) {
		/* take the shortest way around the ring so an end<->beginning move
		 * is a single smooth step, not a slide across the whole list */
		while (raw > count / 2) raw -= count;
		while (raw < -count / 2) raw += count;
		/* A ring of TWO has both neighbors one step away, so "shortest way
		 * around" has no answer and the rule above returns the literal
		 * difference every time: +1, -1, +1, -1. The row then rocks right and
		 * left rather than turning, however the cards are drawn - which is
		 * why this looked like a drawing bug and is not one. When the caller
		 * knows which way the press was, believe it over the arithmetic. */
		if (count == 2 && dir) raw = dir;
	}
	if (raw) cf->last_dir = raw > 0 ? 1 : -1;
	cf->last_cursor = cursor;

	/* A press arriving while a cut is still being drawn: the departure on
	 * screen belongs to a jump that is already over. Land it before taking
	 * the new one, or the shelf creeps while the cursor races - **in** is
	 * cubic, so at the 90ms key repeat a 160ms departure has covered under a
	 * fifth of its travel, and holding the button would leave the cards
	 * crawling under a title running through the list. */
	if (cf->cutting) {
		cf->pos = cf->target;
		cf->cutting = false;
	}
	cf->target += (float)raw;
	/* Start the tween from where the cards ARE, not from where the last one
	 * was headed. Holding a direction retargets every key repeat, and taking
	 * the live position each time is what makes that one continuous glide
	 * rather than a stutter back to the old start. */
	cf->from = cf->pos;
	/* A MOVE LONGER THAN THE WARM WINDOW IS NOT WORTH DRAWING.
	 *
	 * Not the same claim this made when it was written. Then it said such a
	 * move could not be drawn at all, because decoding sat on the render path
	 * and seven cold cards in a frame meant the frame never arrived - a letter
	 * jump showed the card it started near, then the card it landed on, and
	 * nothing between. texload.c fixed that, and the frames arrive now.
	 *
	 * What is left is two things the worker does not change. The cards out
	 * there have no texture yet, so drawing the move would fly past empty
	 * slots; and a fixed duration turns distance into speed, so past about
	 * nine cards it crosses more than one per frame and stops being motion at
	 * all. Both say the same thing: show the part that means something and
	 * cut the rest.
	 *
	 * The part that means something is the departure, because the warm cards
	 * are all behind the cursor - the destination is by definition the half
	 * not decoded yet. So the motion accelerates rather than settles and the
	 * cut lands at full speed, where the eye is least able to see it, while
	 * the caller warms the destination underneath; see cf_landing. */
	cf->cutting = false;
	{
		float span = cf->target - cf->from;
		float g = (float)CF_WARM_CARDS;
		if (span > (float)CF_WARM_CARDS || span < -(float)CF_WARM_CARDS) {
			cf->cutting = true;
			cf->cut_to = cf->from + (span > 0.0f ? g : -g);
		}
	}
	cf->t0 = ticks();
	cf->active = true;
}

void cf_stage(coverflow *cf, float from, float to, float u)
{
	float ms = MOVE_MS;

	if (u < 0.0f) u = 0.0f;
	if (u > 1.0f) u = 1.0f;
	cf->from = from;
	cf->target = to;
	cf->pos = from + (to - from) * ds_out(u);
	/* Backdated, so the move is already this far along by the clock the label
	 * and step_anim both measure against. */
	cf->t0 = ticks() - (Uint32)(u * ms);
	cf->active = true;
	cf->cutting = false;
	cf->primed = true;
	cf->last_cursor = (int)floorf(to + 0.5f);
}

int cf_landing(const coverflow *cf, int count)
{
	int i;
	if (!cf->cutting || count <= 0) return -1;
	i = (int)floorf(cf->target + 0.5f) % count;
	if (i < 0) i += count;
	return i;
}

bool cf_cutting(const coverflow *cf) { return cf->cutting; }

static void step_anim(coverflow *cf, int count)
{
	bool loops = cf_loops(count);
	if (loops) {
		/* `from` rides along with pos and target through a wrap, or the
		 * interpolation below would be measuring against a point that is now
		 * a whole revolution away. */
		while (cf->pos >= (float)count) {
			cf->pos -= count; cf->target -= count; cf->from -= count;
			if (cf->cutting) cf->cut_to -= count;
		}
		while (cf->pos < 0.0f) {
			cf->pos += count; cf->target += count; cf->from += count;
			if (cf->cutting) cf->cut_to += count;
		}
	}
	if (!cf->active) return;
	{
		float ms = MOVE_MS;
		float u = (float)(ticks() - cf->t0) / ms;
		if (u >= 1.0f) {
			/* Whether or not this was a cut, the move ends where the cursor
			 * has been since the press. `target` was never moved. */
			cf->cutting = false;
			cf->pos = cf->target;
			cf->active = false;
			return;
		}
		cf->pos = cf->cutting
			? cf->from + (cf->cut_to - cf->from) * ds_in(u)
			: cf->from + (cf->target - cf->from) * ds_out(u);
	}
}

/* Weak-perspective projection of a card-local point. The card is yawed
 * about its vertical axis; x rotates into depth and everything is scaled
 * by F/(F+z), which squashes the far edge horizontally and vertically. */
typedef struct {
	float ox, oy;   /* card center on screen */
	float cosa, sina;
	float focal;
} cf_proj;

static void proj_point(const cf_proj *p, float lx, float ly, float *sx, float *sy)
{
	float xr = lx * p->cosa;
	float zr = lx * p->sina;
	float s = p->focal / (p->focal + zr);
	*sx = p->ox + xr * s;
	*sy = p->oy + ly * s;
}

static void render_quad(SDL_Renderer *r, SDL_Texture *tex,
                        const float xy[8], const float uv[8],
                        const SDL_Color col[4])
{
	SDL_Vertex v[4];
	for (int i = 0; i < 4; i++) {
		v[i].position.x = xy[i * 2];
		v[i].position.y = xy[i * 2 + 1];
		v[i].tex_coord.x = uv[i * 2];
		v[i].tex_coord.y = uv[i * 2 + 1];
		v[i].color = col[i];
	}
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	SDL_RenderGeometry(r, tex, v, 4, idx, 6);
}

/* The art's own half width and height, in a card frame of half size hw x hh. */
static void art_fit(const cf_layout *lay, int tw, int th, float hw, float hh,
                    float *out_hw, float *out_hh)
{
	/* Fit the texture to the card.
	 *
	 * CONTAIN gives every cover the frame's proportions and nobody else's, so
	 * only art shaped like the frame ever fills it. Measured on this card
	 * 2026-09-12, on one shelf at one setting: an NES cover is 0.70, close to
	 * the frame's 0.72, and draws 323x461. A US SNES cover is 1.41 - those
	 * cases were wider than they were tall - and draws 332x236, barely half
	 * the area. Square Game Boy and TurboGrafx art draws 332x331, about three
	 * quarters. The shelf looked inconsistent because it was.
	 *
	 * EQUAL AREA solves ahw/ahh = tex_ar against ahw*ahh = hw*hh: the card
	 * keeps the frame's area and takes the art's shape. Art already shaped
	 * like the frame barely moves, which is why NES is unchanged and SNES
	 * grows to 464x330.
	 *
	 * It reads each cover rather than each system, which matters more than it
	 * sounds: 14 of 314 SNES covers here are Japanese, and a Super Famicom box
	 * is TALL - 0.53, not 1.41. Widening the SNES frame would have helped 300
	 * covers and cut those 14 to a quarter of their area. This rule never sees
	 * the difference because it never asks what system it is looking at. */
	float ahw = hw, ahh = hh;
	if (tw > 0 && th > 0) {
		float tex_ar = (float)tw / (float)th;
		if (lay->equal_area) {
			float area = hw * hh;
			ahw = sqrtf(area * tex_ar);
			ahh = sqrtf(area / tex_ar);
		} else {
			float frame_ar = hw / hh;
			if (tex_ar > frame_ar) ahh = hw / tex_ar;
			else ahw = hh * tex_ar;
		}
	}
	*out_hw = ahw;
	*out_hh = ahh;
}

/* The projection a card centered at (ox, oy), of half width hw, yawed by
 * `ang`, is drawn through. */
static cf_proj card_proj(float ox, float oy, float hw, float ang)
{
	return (cf_proj){
		.ox = ox, .oy = oy,
		.cosa = cosf(ang), .sina = sinf(ang),
		.focal = hw * 6.0f + 1.0f,
	};
}

/* The rect on screen the art of a card covers down to `cb` of its height
 * (1 for the whole texture, content_bottom for where its pixels stop): the
 * bounds of the four corners as they are projected, so a card turned away is
 * as narrow as it looks. */
static void art_rect(const cf_layout *lay, int tw, int th, float ox, float oy,
                     float hw, float hh, float ang, float cb, SDL_Rect *out)
{
	float ahw, ahh, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	cf_proj p;
	int k;

	art_fit(lay, tw, th, hw, hh, &ahw, &ahh);
	p = card_proj(ox, oy, hw, ang);
	for (k = 0; k < 4; k++) {
		float sx, sy;

		proj_point(&p, k & 1 ? ahw : -ahw, k & 2 ? -ahh + 2.0f * ahh * cb : -ahh,
		           &sx, &sy);
		if (sx < x0) x0 = sx;
		if (sx > x1) x1 = sx;
		if (sy < y0) y0 = sy;
		if (sy > y1) y1 = sy;
	}
	out->x = (int)floorf(x0);
	out->y = (int)floorf(y0);
	out->w = (int)ceilf(x1) - out->x;
	out->h = (int)ceilf(y1) - out->y;
}

static void draw_card(SDL_Renderer *r, SDL_Texture *tex, int tw, int th,
                      float cb, float ox, float oy, float hw, float hh,
                      float ang, Uint8 alpha, const cf_layout *lay,
                      float floor_y)
{
	float ahw, ahh;

	art_fit(lay, tw, th, hw, hh, &ahw, &ahh);

	cf_proj p = card_proj(ox, oy, hw, ang);

	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

	int n = lay->strips > 0 ? lay->strips : 16;
	/* The reflection reaches a common baseline (lay->reflect * hh below the
	 * card center) no matter how tall the fitted art is: short art gets a
	 * longer reflection so it reflects to the same depth as tall art. The
	 * art itself stays vertically centered. */
	/* Where the art's opaque pixels stop, in local units. Reflecting from the
	 * card's bottom edge instead put the mirror below transparent padding: the
	 * console art is squared and centered, so a Genesis carries 102 of its 384
	 * rows empty underneath and its reflection began a further 102 away again
	 * - a gap of about 196px on screen, against 10px for a Game Boy, which
	 * fills its canvas. Same reflection setting, wildly different result.
	 * Measured 2026-09-08. */
	float y_cb = -ahh + cb * 2.0f * ahh;
	/* How far past the mirror the reflection runs, as a fraction of the art's
	 * full height, so that it still reaches the common baseline the layout
	 * asks for however high the mirror sits. With cb = 1 this is exactly what
	 * it was before: art that fills its canvas is unchanged. */
	float f = lay->reflect;
	if (ahh > 0.001f) {
		f = (lay->reflect * hh - y_cb) / (2.0f * ahh);
		if (f < 0.02f) f = 0.02f;
		/* Cap at what is actually above the mirror: sampling past the top of
		 * the texture gives a negative texcoord, which the Mali GLES driver
		 * drops entirely - no reflection at all rather than a short one. */
		if (f > cb) f = cb;
		/* And ended by the floor, the text under the row: a reflection that
		 * would reach past it is shortened to end there, so it has faded
		 * out before the first word, steeper the shorter it is. In the
		 * card's unturned units, which is exact for the card in focus, the
		 * one under the words. A turned card's near edge is nearer, so the
		 * perspective scales it up, and mid-move its reflection can end about
		 * 20 units below the floor. Short of nothing, none at all. */
		if (floor_y > 0.0f) {
			float lim = (floor_y - oy - y_cb) / (2.0f * ahh);

			if (f > lim) f = lim;
		}
	}
	/* The reflection is the art itself, untinted - white is no tint, here and
	 * on the body - at 90/255 under the card and fading to nothing at its
	 * foot, DESIGN.md 7. To nothing and not to a color, so what it fades into
	 * is whatever is under it, which is the theme's ground and the system's
	 * wash along it: Beard's light ground and Salt & Pepper's dark one, with
	 * no color of the reflection's own to be wrong in either. */
	Uint8 ra = (Uint8)(alpha * 90 / 255);
	SDL_Color body[4] = {
		{ 255, 255, 255, alpha }, { 255, 255, 255, alpha },
		{ 255, 255, 255, alpha }, { 255, 255, 255, alpha },
	};

	for (int i = 0; i < n; i++) {
		float u0 = (float)i / n, u1 = (float)(i + 1) / n;
		float lx0 = -ahw + u0 * 2.0f * ahw;
		float lx1 = -ahw + u1 * 2.0f * ahw;

		float tlx, tly, trx, try_, brx, bry, blx, bly;
		proj_point(&p, lx0, -ahh, &tlx, &tly);
		proj_point(&p, lx1, -ahh, &trx, &try_);
		proj_point(&p, lx1, +ahh, &brx, &bry);
		proj_point(&p, lx0, +ahh, &blx, &bly);

		float xy[8] = { tlx, tly, trx, try_, brx, bry, blx, bly };
		float uv[8] = { u0, 0, u1, 0, u1, 1, u0, 1 };
		render_quad(r, tex, xy, uv, body);

		if (f > 0.0f) {
			/* Mirror from where the CONTENT ends, extended along the card's
			 * own (already foreshortened) vertical direction. The mirror
			 * corners are found by walking cb of the way down the strip,
			 * which is exact: only x rotates into depth, so the depth scale
			 * is constant across a strip's height and the interpolation is
			 * linear. */
			/* Down cb of the strip to where the art stops, then the gap. The
			 * texture still starts at cb: the reflection is displaced, not
			 * cropped, so the first thing it shows is still the last thing
			 * the art showed. */
			float m = cb;
			float mlx = tlx + m * (blx - tlx), mly = tly + m * (bly - tly);
			float mrx = trx + m * (brx - trx), mry = try_ + m * (bry - try_);
			float rblx = mlx + f * (blx - tlx);
			float rbly = mly + f * (bly - tly);
			float rbrx = mrx + f * (brx - trx);
			float rbry = mry + f * (bry - try_);
			float vb = cb - f;
			float rxy[8] = { mlx, mly, mrx, mry, rbrx, rbry, rblx, rbly };
			float ruv[8] = { u0, cb, u1, cb, u1, vb, u0, vb };
			SDL_Color rcol[4] = {
				{ 255, 255, 255, ra }, { 255, 255, 255, ra },
				{ 255, 255, 255, 0 }, { 255, 255, 255, 0 },
			};
			render_quad(r, tex, rxy, ruv, rcol);
		}
	}
}

bool cf_draw(coverflow *cf, SDL_Renderer *r, int screen_w, int screen_h,
             int count, cf_tex_fn get_tex, cf_lift_fn lift, void *ctx,
             const cf_layout *lay)
{
	if (count <= 0) return false;
	step_anim(cf, count);

	bool loops = cf_loops(count);
	int half = cf_half(count);
	float ch = screen_h * lay->size;
	float cw = ch * lay->aspect;
	/* Spacing counts the card's extent along the axis it is stacked on. */
	float step = cw * lay->step;
	float cx = screen_w * 0.5f;
	float cy = screen_h * lay->center_y;

	int base = (int)floorf(cf->pos + 0.5f);

	/* collect visible slots, then draw far-to-near so the center card wins */
	struct slot { int item; float d; } slots[CF_WINDOW];
	int ns = 0;
	for (int k = -half; k <= half; k++) {
		int i = base + k;
		float d = (float)i - cf->pos;
		if (fabsf(d) > half + 0.5f) continue;
		int item = i;
		if (loops) {
			item = i % count;
			if (item < 0) item += count;
		} else if (i < 0 || i >= count) {
			continue;
		}
		/* One slot per item, nearest wins. With the window now allowed to be
		 * wider than the list, wrapping offers the same item on both sides -
		 * two items put the other one at -1 and +1 - and drawing it twice
		 * would be worse than the bug this replaced. */
		{
			int dup = -1, q;
			for (q = 0; q < ns; q++)
				if (slots[q].item == item) { dup = q; break; }
			if (dup >= 0) {
				float held = slots[dup].d;
				/* Nearest wins. On an exact tie - which is every resting frame
				 * of a two-item ring, where both copies sit one step out - put
				 * the card BEHIND the direction of travel, so the one you just
				 * moved past is the one you see. Choosing arbitrarily instead
				 * meant it sat left whichever way you went, and so had to jump
				 * across at the end of every leftward move. */
				if (fabsf(d) < fabsf(held) ||
				    (fabsf(d) == fabsf(held) && cf->last_dir &&
				     (d < 0) == (cf->last_dir > 0)))
					slots[dup].d = d;
				continue;
			}
		}
		slots[ns].item = item;
		slots[ns].d = d;
		ns++;
	}
	/* insertion sort by |d| descending */
	for (int a = 1; a < ns; a++) {
		struct slot s = slots[a];
		int b = a - 1;
		while (b >= 0 && fabsf(slots[b].d) < fabsf(s.d)) {
			slots[b + 1] = slots[b];
			b--;
		}
		slots[b + 1] = s;
	}

	/* Each card's place, once: the lifts below need it before any card is
	 * drawn, and the cards after. */
	struct card {
		SDL_Texture *tex;
		int tw, th;
		float cb, c, ox, oy, hw, hh, ang;
		Uint8 alpha;
	} cards[CF_WINDOW];
	for (int a = 0; a < ns; a++) {
		struct card *k = &cards[a];
		float d = slots[a].d;
		float ad = fabsf(d);
		float scale;

		k->c = 1.0f - clampf(ad, 0.0f, 1.0f);
		scale = lay->side_scale + (1.0f - lay->side_scale) * k->c;
		k->ang = -lay->tilt * clampf(d, -1.0f, 1.0f);
		k->alpha = (Uint8)(lay->side_alpha + (255 - lay->side_alpha) * k->c);
		k->tw = k->th = 0;
		k->cb = 1.0f;
		k->tex = get_tex(ctx, slots[a].item, &k->tw, &k->th, &k->cb);
		k->ox = cx + d * step;
		k->oy = cy;
		k->hw = cw * 0.5f * scale;
		k->hh = ch * 0.5f * scale;
	}
	/* The lifts first, under every card and not only under their own: a
	 * glow is added light, and laid over a neighbor already drawn it would
	 * light that card up too, where under it, as it always was, it shows
	 * only round and through it. */
	for (int a = 0; lift && a < ns; a++) {
		const struct card *k = &cards[a];
		SDL_Rect q, c;

		if (!k->tex || k->c <= 0.0f) continue;
		art_rect(lay, k->tw, k->th, k->ox, k->oy, k->hw, k->hh, k->ang, 1.0f, &q);
		art_rect(lay, k->tw, k->th, k->ox, k->oy, k->hw, k->hh, k->ang, k->cb, &c);
		lift(ctx, slots[a].item, &q, &c, k->c);
	}
	for (int a = 0; a < ns; a++) {
		const struct card *k = &cards[a];

		if (!k->tex) continue;
		draw_card(r, k->tex, k->tw, k->th, k->cb, k->ox, k->oy, k->hw, k->hh,
		          k->ang, k->alpha, lay, lay->floor * screen_h);
	}
	return cf->active;
}
