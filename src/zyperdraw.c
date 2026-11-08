/* SPDX-License-Identifier: GPL-3.0-only */
#include "zyperdraw.h"

#include "battery_eta.h"
#include "battery_poll.h"
#include "blur.h"
#include "design.h"
#include "musec.h"
#include "status.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- drawing ------------------------------------------------------- */

/* A heart, as a fan over the classic parametric curve:
 *
 *     x = 16 sin^3 t
 *     y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t
 *
 * Mirrors markdef.heart(), which is the definition. The Favorites card's
 * heart is the same curve, drawn from it once by the script that drew that
 * card. The same curve still marks favorite games: change the curve and that
 * mark is redrawn by hand to match. The zyper mark no longer needs this,
 * since src/zypermark.h is generated and checked (see draw_zyper).
 *
 * Fanned from a point BELOW the center rather than from the center itself. The
 * notch between the lobes is the one concave part of the shape, and a fan
 * apex level with it produces slivers that cross the notch and fill it in.
 * Dropping the apex puts every boundary point in view of it. A fan over
 * SDL_RenderGeometry because SDL has no polygon fill; it antialiases nothing
 * and so looks identical to the generators' rasterizer.
 */
#define HEART_N 48

void draw_heart(SDL_Renderer *r, float cx, float cy, float rad,
                SDL_Color col)
{
	SDL_Vertex v[HEART_N + 1];
	int idx[HEART_N * 3], i;

	v[0].position.x = cx;
	v[0].position.y = cy + rad * 0.25f;
	for (i = 0; i < HEART_N; i++) {
		float t = (float)(2.0 * M_PI * i / HEART_N);
		float x = 16.0f * powf(sinf(t), 3.0f);
		float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t)
		          - 2.0f * cosf(3 * t) - cosf(4 * t);

		/* The curve spans 32 wide and 30 tall; halving the wider axis makes
		 * `rad` a radius in the same sense the star's was. */
		v[i + 1].position.x = cx + x * rad / 16.0f;
		v[i + 1].position.y = cy - y * rad / 16.0f;
	}
	for (i = 0; i <= HEART_N; i++) {
		v[i].color = col;
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	for (i = 0; i < HEART_N; i++) {
		idx[i * 3 + 0] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % HEART_N;
	}
	SDL_RenderGeometry(r, NULL, v, HEART_N + 1, idx, HEART_N * 3);
}

#include "zypermark.h"

/* Zyper, drawn about his own center, in any pose the menu's rig gives him
 * (src/zyperrig.h).
 *
 * The triangles are tools/genzyper.py's, from tools/zyperdef.py, which is the
 * definition; tools/checkmark.py (in `make check`) fails when the header and
 * the definition disagree, and when its marks do not move him as the
 * definition does, so nothing here is hand-kept.
 *
 * `h` is his height in pixels at rest. The pose: `tilt` is the head's cock
 * in degrees, ZYPER_TILT_DEG at rest and 0 straight; `tongue` is how far the
 * tongue is out, 1 to 0, its vertices scaled toward the line it leaves the
 * mouth on, so at 0 it is gone behind the lip; `ears` swings each ear tip
 * out about its fold by that many degrees; `blink` shuts the eyes, 0 to 1,
 * onto ZYPER_EYE_Y; `squash` is his height about his chin, the foot of his
 * resting extent, his width going the other way so he keeps his size; and
 * `dy` moves the whole of him, after all of that. `brow` is how far the high
 * brow is lifted, ZYPER_BROW_LIFT as the header draws it: the menu's follows
 * the tilt, as tools/zyperdef.py's does, and draw_zyper keeps the header's.
 * `dim` fades the whole of him toward the ground, 0 to 1, by drawing him
 * again in the ground color at that alpha: the light going out, which is
 * how the shutdown animation ends. */
void draw_zyper_pose(SDL_Renderer *r, float cx, float cy, float h,
                     const zr_pose *p, float brow, float dim, Uint8 alpha)
{
	static SDL_Vertex v[ZYPER_NVERT];
	const float k = h / ZYPER_H;
	const float c = cosf(p->tilt * (float)M_PI / 180.0f);
	const float s = sinf(p->tilt * (float)M_PI / 180.0f);
	const float ec = cosf(p->ears * (float)M_PI / 180.0f);
	const float es = sinf(p->ears * (float)M_PI / 180.0f);
	const float shut = 1.0f - ZYPER_EYE_SHUT * (p->blink > 1.0f ? 1.0f : p->blink);
	const float mshut = 1.0f - ZYPER_MOUTH_SHUT * (p->mouth > 1.0f ? 1.0f : p->mouth < 0.0f ? 0.0f : p->mouth);
	const float chin = cy + h / 2.0f;
	const float wide = p->squash > 0.0f ? 1.0f / sqrtf(p->squash) : 1.0f;
	int i;

	for (i = 0; i < ZYPER_NVERT; i++) {
		float x = zyper_verts[i].x, y = zyper_verts[i].y, sx, sy;

		if (i >= ZYPER_TONGUE_FIRST)
			y = ZYPER_TONGUE_Y0 + (y - ZYPER_TONGUE_Y0) * p->tongue;
		if (i >= ZYPER_BROW_FIRST && i < ZYPER_BROW_END)
			y += ZYPER_BROW_LIFT - brow;
		if (i >= ZYPER_EYES_FIRST && i < ZYPER_EYES_END)
			y = ZYPER_EYE_Y + (y - ZYPER_EYE_Y) * shut;
		if (i >= ZYPER_MOUTH_FIRST && i < ZYPER_MOUTH_END)
			y = ZYPER_MOUTH_Y + (y - ZYPER_MOUTH_Y) * mshut;
		if (i >= ZYPER_EARS_FIRST && i < ZYPER_EARS_END && p->ears != 0.0f) {
			/* Each ear about its own fold, the far one the mirror way */
			const float m = x < 0.0f ? 1.0f : -1.0f;
			const float fx = ZYPER_EAR_FOLD_X * m, fy = ZYPER_EAR_FOLD_Y;
			const float ux = x - fx, uy = y - fy;

			x = fx + ux * ec - uy * es * m;
			y = fy + ux * es * m + uy * ec;
		}
		/* Tilt about his origin, then center on the tilted extent, the same
		 * order tools/zyperdef.py draws him in; then the squash about his
		 * chin and the move, the order the Zyper lab drew them in. */
		sx = cx + (x * c - y * s - ZYPER_CX) * k;
		sy = cy + (x * s + y * c - ZYPER_CY) * k;
		v[i].position.x = cx + (sx - cx) * wide;
		v[i].position.y = chin + (sy - chin) * p->squash + p->dy;
		v[i].color.r = zyper_verts[i].r;
		v[i].color.g = zyper_verts[i].g;
		v[i].color.b = zyper_verts[i].b;
		v[i].color.a = alpha;
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	/* The tongue all the way in is flat on its line: a GPU draws nothing of
	 * it, and SDL's software renderer, the shots', a pink row. Left out. */
	SDL_RenderGeometry(r, NULL, v, p->tongue > 0.0f ? ZYPER_NVERT : ZYPER_TONGUE_FIRST,
	                   NULL, 0);
	if (dim > 0.0f) {
		/* Toward the ground he is on, so at 1 he is gone into it. */
		const ds_rgb g = theme()->ground;

		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
		for (i = 0; i < ZYPER_NVERT; i++) {
			v[i].color.r = g.r; v[i].color.g = g.g; v[i].color.b = g.b;
			v[i].color.a = (Uint8)(alpha * (dim > 1.0f ? 1.0f : dim));
		}
		SDL_RenderGeometry(r, NULL, v, p->tongue > 0.0f ? ZYPER_NVERT : ZYPER_TONGUE_FIRST,
		                   NULL, 0);
	}
}

/* Zyper as the power-off screen and the sequence card draw him: a tilt and
 * a tongue and nothing else, the high brow where the header has it. */
void draw_zyper(SDL_Renderer *r, float cx, float cy, float h,
                float tilt, float tongue, float dim, Uint8 alpha)
{
	const zr_pose p = { tilt, tongue, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

	draw_zyper_pose(r, cx, cy, h, &p, ZYPER_BROW_LIFT, dim, alpha);
}

/* His shadow on Beard, DESIGN.md 10: there his beard is the ground's own
 * white, and drawn flat he loses his outline, so he casts the **focus-small**
 * shadow, 8 down, blurred 20, ink at 18%, the way everything in Beard is
 * lifted. Salt & Pepper casts none.
 *
 * In his own shape, his triangles at rest,
 * untilted and the tongue out, filled into a grid a texel to BLUR_UNIT units
 * at the size he is drawn (blur_fill_tris), blurred by focus-small's three
 * boxes as Beard's other shadows are (blur_shadow_size, blur_box3_by), and
 * kept as a white texture whose alpha is the shadow, tinted **ink** as it is
 * drawn. The tongue, the eyes and the brows are all inside his outline, so
 * his rest pose casts the shadow of every pose the menu and the power-off
 * give him but for the tilt, and that is drawn by turning the texture about
 * his origin as draw_zyper_pose turns him. Filled here rather than drawn
 * into a render target and read back: the triangles are at hand, and the
 * grid is the same on any GPU and before the first frame.
 *
 * Its blur is baked, so it is the shadow of one drawn size: one each for
 * the sizes he is drawn at on Beard, the menu's, the power-off's and the
 * tutorial's three (its welcome and finish, its corner, and the power step's
 * stage), made the first time each is asked for and dropped with the other textures,
 * before the renderer goes. The fill and the blur take 0.3 ms for the
 * menu's 59 x 58 texels and 2.2 ms for the power-off's 137 x 136 on the
 * host, a few times that on the handheld: the frame that first asks is
 * that much late, once. Not made at start, as src/ui.c's shadows are: the
 * menu's is a third of a millisecond on the host, and the power-off's is
 * asked for on the way out. A size that cannot be made, or a size past the
 * ZYPER_SHADOWS made, is drawn without one, said once on stderr. */
#define ZYPER_SHADOWS 5

static struct {
	float        h;        /* the height it was made for; 0 is a free slot */
	SDL_Texture *tex;      /* NULL when it could not be made */
	float        ox, oy;   /* his origin in it, units from its top left */
	float        w, hh;    /* its size in units */
} zyper_sh[ZYPER_SHADOWS];

void zyper_shadows_free(void)
{
	int i;

	for (i = 0; i < ZYPER_SHADOWS; i++) {
		if (zyper_sh[i].tex) SDL_DestroyTexture(zyper_sh[i].tex);
		memset(&zyper_sh[i], 0, sizeof zyper_sh[i]);
	}
}

static int zyper_shadow_for(SDL_Renderer *r, float h)
{
	static bool full_said;
	static float xy[ZYPER_NVERT * 2];
	const float k = h / ZYPER_H / BLUR_UNIT;      /* texels a design unit */
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	float *a = NULL, *line = NULL;
	SDL_Surface *sf = NULL;
	blur_shadow bs;
	int i, x, y, w, ht, slot = -1;

	for (i = 0; i < ZYPER_SHADOWS; i++) {
		if (zyper_sh[i].h == h) return i;
		if (slot < 0 && zyper_sh[i].h == 0.0f) slot = i;
	}
	if (slot < 0) {
		if (!full_said)
			fprintf(stderr, "zyper: no room for a shadow at %.0f tall, with %d "
			        "sizes made; drawn without\n", h, ZYPER_SHADOWS);
		full_said = true;
		return -1;
	}
	zyper_sh[slot].h = h;
	/* The blur alone: the corner is a rect's, and his shape is his own */
	bs = blur_shadow_size(DS_SHADOW_FOCUS_SMALL_BLUR, 0, BLUR_UNIT);

	for (i = 0; i < ZYPER_NVERT; i++) {
		x0 = fminf(x0, zyper_verts[i].x); x1 = fmaxf(x1, zyper_verts[i].x);
		y0 = fminf(y0, zyper_verts[i].y); y1 = fmaxf(y1, zyper_verts[i].y);
	}
	w = (int)ceilf((x1 - x0) * k) + 2 * bs.m;
	ht = (int)ceilf((y1 - y0) * k) + 2 * bs.m;
	for (i = 0; i < ZYPER_NVERT; i++) {
		xy[2 * i] = (zyper_verts[i].x - x0) * k + bs.m;
		xy[2 * i + 1] = (zyper_verts[i].y - y0) * k + bs.m;
	}
	a = malloc((size_t)w * ht * sizeof *a);
	line = malloc((size_t)(w > ht ? w : ht) * sizeof *line);
	if (!a || !line || !blur_fill_tris(a, w, ht, xy, ZYPER_NVERT)) goto out;
	blur_box3_by(a, w, ht, bs.rad, line);
	if (!(sf = SDL_CreateRGBSurfaceWithFormat(0, w, ht, 32, SDL_PIXELFORMAT_ARGB8888)))
		goto out;
	for (y = 0; y < ht; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)sf->pixels + (size_t)y * sf->pitch);

		for (x = 0; x < w; x++) {
			float t = a[(size_t)y * w + x];

			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			row[x] = (Uint32)(t * 255.0f + 0.5f) << 24 | 0x00FFFFFFu;
		}
	}
	if ((zyper_sh[slot].tex = SDL_CreateTextureFromSurface(r, sf))) {
		SDL_SetTextureBlendMode(zyper_sh[slot].tex, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(zyper_sh[slot].tex, SDL_ScaleModeLinear);
		zyper_sh[slot].ox = (bs.m - x0 * k) * BLUR_UNIT;
		zyper_sh[slot].oy = (bs.m - y0 * k) * BLUR_UNIT;
		zyper_sh[slot].w = (float)(w * BLUR_UNIT);
		zyper_sh[slot].hh = (float)(ht * BLUR_UNIT);
	}
out:
	if (!zyper_sh[slot].tex)
		fprintf(stderr, "zyper: no shadow at %.0f tall; drawn without\n", h);
	if (sf) SDL_FreeSurface(sf);
	free(line);
	free(a);
	return slot;
}

/* The shadow under Zyper drawn at (cx, cy), `h` tall and cocked `tilt`, at
 * `k` of its strength: 1, or 1 - dim as he goes into the ground. Drawn
 * before him, on Beard only. It stays where he stands: a hop or a breath
 * lifts him off it. */
void zyper_shadow_draw(SDL_Renderer *r, float cx, float cy, float h,
                       float tilt, float k)
{
	const ds_rgb ink = theme()->ink;
	const float u = h / ZYPER_H;
	int i;

	if (theme_index() != DS_LIGHT || !(k > 0.0f)) return;
	if ((i = zyper_shadow_for(r, h)) < 0 || !zyper_sh[i].tex) return;
	if (k > 1.0f) k = 1.0f;
	SDL_SetTextureColorMod(zyper_sh[i].tex, ink.r, ink.g, ink.b);
	SDL_SetTextureAlphaMod(zyper_sh[i].tex, (Uint8)(DS_SHADOW_FOCUS_SMALL_A * k + 0.5f));
	/* His origin is at (cx, cy) less ZYPER_CX, ZYPER_CY (draw_zyper_pose),
	 * and the texture is turned about it, then dropped */
	SDL_RenderCopyExF(r, zyper_sh[i].tex, NULL,
	                  &(SDL_FRect){ cx - ZYPER_CX * u - zyper_sh[i].ox,
	                                cy - ZYPER_CY * u - zyper_sh[i].oy
	                                + DS_SHADOW_FOCUS_SMALL_Y,
	                                zyper_sh[i].w, zyper_sh[i].hh },
	                  tilt, &(SDL_FPoint){ zyper_sh[i].ox, zyper_sh[i].oy },
	                  SDL_FLIP_NONE);
}

/* The corner, Diatom's warning and Auto Off share one reading. Capacity stays
 * at five seconds; the cable is read every 200 ms, including while no frame
 * is drawn (idle_due -> on_charger). Only a changed reading asks for a frame.
 * redraw_now also holds the status corner's radio forks back for that frame. */
static battery_poll g_battery;
static batt_eta     g_eta;

bool battery_now(int *pct, bool *charging)
{
	static bool was;
	uint32_t now = plat_now_ms();

	if (battery_poll_update(&g_battery, now, plat_battery)) redraw_now();
	if (was && !g_battery.charging) charge_full_end();
	was = g_battery.charging;
	/* Every reading goes to the time-left estimate, whatever draws or does
	 * not: the falls it learns from happen with the screen as it is. It
	 * compares one int and returns, until the percent changes. */
	batt_eta_note(&g_eta, now, g_battery.valid ? g_battery.pct : -1, g_battery.charging);
	if (pct) *pct = g_battery.pct;
	if (charging) *charging = g_battery.charging;
	return g_battery.valid;
}

int battery_minutes_left(void)
{
	battery_now(NULL, NULL);
	return batt_eta_minutes(&g_eta, plat_now_ms(), BATT_EMPTY_PCT);
}

/* Too low to go on, cached as battery_level is. Asked by every screen through
 * idle_due, and by the game's wait loop (plat_resident_on_battery_empty), so
 * an empty battery ends things the way the power button does: a game saves
 * on its way out, and power_off follows. Twice in a row, five seconds apart,
 * so one stray reading never powers the device off. */
bool battery_empty(void)
{
	static unsigned last;
	static int      seen;
	static bool     primed;
	unsigned now = plat_now_ms();

	if (!primed || now - last >= 5000) {
		int  pct;
		bool charging;

		primed = true;
		last = now;
		if (plat_battery(&pct, &charging) && !charging && pct <= BATT_EMPTY_PCT)
			seen++;
		else
			seen = 0;
		if (seen == 2)
			fprintf(stderr, "battery: %d%%, empty; powering off\n", pct);
	}
	return seen >= 2;
}

/* Has the player been away long enough to power the device off?
 *
 * The gathering half; the policy is idle_check, in src/idle.c, where a check
 * can reach it. This part is the three things only the launcher knows: the
 * setting, whether any button is down, and whether the charger is in.
 *
 * Every screen the launcher draws polls input in its own loop, and Auto Off
 * lived in exactly one of them: the shelf. The other seven simply held the
 * device on - both menus, Wi-Fi, About, Cheevos, the slot strip and the
 * keyboard. A menu left open is not evidence that somebody is there, it is
 * evidence somebody WAS; pausing because something else came up is the most
 * ordinary way there is to walk away from a device still switched on.
 *
 * Called beside each loop's existing power-button check, because that is what
 * the answer means. A device left alone does what the power button does -
 * already the rule for Diatom's IDLE during a game. `grep -n idle_due` is the
 * list of screens that honor it, and it should have no gaps. */
bool idle_due(app *a)
{
	int b;

	/* Every screen's loop comes through here, so the heat is read on all of
	 * them, and a warning that comes due over a menu waits for the home */
	heat_tick();
	if (battery_empty()) return true;
	a->idle.seconds = a->auto_off;
	for (b = 0; b < IN_COUNT; b++)
		if (a->in.pressed[b] || a->in.down[b]) break;

	/* Music playing HOLDS the clock the way the charger does, rather than
	 * counting as input: an album is somebody using the device with nobody
	 * touching it, and when it stops the countdown starts fresh. */
	return idle_check(&a->idle, plat_now_ms(), b < IN_COUNT,
	                  on_charger() || musec_playing());
}
