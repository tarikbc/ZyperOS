/* SPDX-License-Identifier: GPL-3.0-only */
/* See mp4look.h. */
#include "mp4look.h"

#include "design.h"

#include <math.h>
#include <string.h>

static const struct { const char *key; int bars; } LOOKS[MP4L_COUNT] = {
	[MP4L_COVER]    = { "cover",    0 },
	/* None: the tape fills the screen and its reels are the motion (the
	 * Tape Styles lab, my pick); 25 under it until then */
	[MP4L_CASSETTE] = { "cassette", 0 },
	/* In the column beside the record, under the names */
	[MP4L_VINYL]    = { "vinyl",    15 },
	/* A Winamp's window of bars, under the clock: as many as fit the
	 * panel on the grid, 16 wide with 8 between (DESIGN.md 11.64) */
	[MP4L_CLASSIC]  = { "classic",  18 },
	/* One bar a band */
	[MP4L_SPECTRUM] = { "spectrum", 32 },
};

const char *mp4l_key(mp4l_id id)
{
	return id >= 0 && id < MP4L_COUNT ? LOOKS[id].key : LOOKS[MP4L_COVER].key;
}

mp4l_id mp4l_from_key(const char *key)
{
	int i;

	for (i = 0; key && i < MP4L_COUNT; i++)
		if (!strcmp(key, LOOKS[i].key)) return (mp4l_id)i;
	return MP4L_COVER;
}

int mp4l_bars(mp4l_id id)
{
	return id >= 0 && id < MP4L_COUNT ? LOOKS[id].bars : 0;
}

/* ---- the bars ---------------------------------------------------------------- */

/* Gravity, in heights a ms squared: from the top to the floor in
 * MP4L_CAP_DROP_MS, h = g t^2 / 2 */
#define CAP_G (2.0f / ((float)MP4L_CAP_DROP_MS * (float)MP4L_CAP_DROP_MS))
/* Under this a bar is at rest: less than a hundredth of a unit of its
 * height on the tallest bars drawn */
#define REST 0.002f

void mp4l_bars_reset(mp4l_bars_t *b, int n)
{
	memset(b, 0, sizeof *b);
	b->n = n < 0 ? 0 : n > MP4L_BARS_MAX ? MP4L_BARS_MAX : n;
}

/* Bar `i` of `n` over the 32 bands: the loudest of the bands under it, the
 * bands shared out in order, a bar taking one or two where n is not 32 */
static float target(const uint8_t *bands, int i, int n)
{
	int k0 = i * MP4L_BANDS / n, k1 = (i + 1) * MP4L_BANDS / n, k, top = 0;

	if (!bands) return 0.0f;
	if (k1 <= k0) k1 = k0 + 1;
	for (k = k0; k < k1 && k < MP4L_BANDS; k++)
		if (bands[k] > top) top = bands[k];
	return (float)top / 255.0f;
}

void mp4l_bars_step(mp4l_bars_t *b, float dt, const uint8_t *bands)
{
	/* follow's step for this frame, the bar's whole fall in one number */
	float fall = dt > 0 ? 1.0f - expf(-dt / (float)DS_MS_BAR_FALL) : 0.0f;
	int i;

	if (dt < 0) dt = 0;
	for (i = 0; i < b->n; i++) {
		float t = target(bands, i, b->n);

		/* Up at once; down on follow */
		if (t >= b->lv[i]) b->lv[i] = t;
		else b->lv[i] += (t - b->lv[i]) * fall;
		if (b->lv[i] < REST) b->lv[i] = 0.0f;

		/* The cap: on the bar wherever the bar reaches it, held there
		 * for peak-hold, then falling faster and faster until it is back
		 * on the bar */
		if (b->lv[i] >= b->cap[i]) {
			b->cap[i] = b->lv[i];
			b->hold[i] = (float)DS_MS_PEAK_HOLD;
			b->fall[i] = 0.0f;
		} else {
			float left = dt;

			if (b->hold[i] > 0) {
				float h = b->hold[i] < left ? b->hold[i] : left;

				b->hold[i] -= h;
				left -= h;
			}
			if (left > 0) {
				/* Exact for a fall that starts inside the frame */
				b->cap[i] -= b->fall[i] * left + 0.5f * CAP_G * left * left;
				b->fall[i] += CAP_G * left;
				if (b->cap[i] <= b->lv[i]) {
					b->cap[i] = b->lv[i];
					b->fall[i] = 0.0f;
				}
			}
		}
		if (b->cap[i] < REST) b->cap[i] = 0.0f;
	}
}

bool mp4l_bars_rest(const mp4l_bars_t *b)
{
	int i;

	for (i = 0; i < b->n; i++)
		if (b->lv[i] > 0.0f || b->cap[i] > 0.0f) return false;
	return true;
}

/* ---- turning ---------------------------------------------------------------- */

void mp4l_spin_to(mp4l_spin *s, bool on)
{
	float to = on ? 1.0f : 0.0f;

	if (s->to == to && (s->t < s->dur || s->v == to)) return;
	s->from = s->v;
	s->to = to;
	s->t = 0;
	s->dur = (float)(on ? DS_MS_SPIN_UP : DS_MS_SPIN_DOWN);
}

void mp4l_spin_step(mp4l_spin *s, float dt)
{
	if (s->t >= s->dur) { s->v = s->to; return; }
	s->t += dt;
	s->v = s->from + (s->to - s->from) * ds_out(s->t / s->dur);
	if (s->t >= s->dur) s->v = s->to;
}

bool mp4l_spin_moving(const mp4l_spin *s)
{
	return s->v > 0.0f || s->t < s->dur;
}

/* ---- the tape ----------------------------------------------------------------- */

float mp4l_tape_r(float f, int reel, float hub_r, float full_r)
{
	float k = reel ? f : 1.0f - f;

	if (k < 0) k = 0;
	if (k > 1) k = 1;
	return sqrtf(hub_r * hub_r + k * (full_r * full_r - hub_r * hub_r));
}

/* Under this the tape is where the song is */
#define TAPE_NEAR 0.0005f

void mp4l_tape_step(mp4l_tape *t, float dt, int song, float target, float spin, float speed,
                    float hub_r, float full_r, float wind)
{
	float f0, moved = 0;
	int k;

	if (target < 0) target = 0;
	if (target > 1) target = 1;
	if (!t->known) {
		t->known = true;
		t->song = song;
		t->f = target;
		t->t = -1;
	} else if (song != t->song) {
		/* A new song: back to the start, from wherever the tape is */
		t->song = song;
		t->from = t->f;
		t->t = 0;
	}
	f0 = t->f;
	if (t->t >= 0) {
		t->t += dt;
		if (t->t >= (float)DS_MS_REWIND) {
			t->t = -1;
			t->f = 0;
		} else {
			t->f = t->from * (1.0f - ds_turn(t->t / (float)DS_MS_REWIND));
		}
		moved = t->f - f0;
	}
	if (t->t < 0) {
		float gap = target - t->f;

		/* Playing on, the tape is where the song is; a seek's jump is wound
		 * on follow, which is all the tape the reels turn for past their
		 * own speed */
		if (fabsf(gap) < 0.002f) t->f = target;
		else t->f += gap * (1.0f - expf(-dt / (float)DS_MS_WIND));
		if (fabsf(target - t->f) < TAPE_NEAR) t->f = target;
		if (fabsf(gap) >= 0.002f) moved = t->f - f0;
	}
	for (k = 0; k < 2; k++) {
		float r = mp4l_tape_r(t->f, k, hub_r, full_r);
		/* Playing, the tape leaves the left pack down its outer side and
		 * climbs onto the right one: both turn counterclockwise, which is
		 * back on SDL's clockwise angle. Wound, the reels turn for the tape
		 * the wind moved, the same way for forward. */
		float turn = (spin * speed * dt / 1000.0f + moved * wind) / r;

		t->ang[k] = fmodf(t->ang[k] - turn * 57.29578f, 360.0f);
	}
}

bool mp4l_tape_moving(const mp4l_tape *t, float target)
{
	return t->t >= 0 || fabsf(target - t->f) >= TAPE_NEAR;
}

/* ---- the record ---------------------------------------------------------------- */

/* Under this the needle is where it goes: a tenth of a unit */
#define ARM_NEAR 0.1f

void mp4l_disc_step(mp4l_disc *d, float dt, float spin, float arm)
{
	if (!d->known) {
		d->known = true;
		d->arm = arm;
	}
	d->ang = fmodf(d->ang + spin * MP4L_RPM_DEG_S * dt / 1000.0f, 360.0f);
	d->arm += (arm - d->arm) * (1.0f - expf(-dt / (float)DS_MS_ARM_GLIDE));
	if (fabsf(arm - d->arm) < ARM_NEAR) d->arm = arm;
}

bool mp4l_disc_moving(const mp4l_disc *d, float arm)
{
	return fabsf(arm - d->arm) >= ARM_NEAR;
}

float mp4l_groove_r(int place, int n, float f, float out, float in)
{
	float k;

	if (n < 1) n = 1;
	if (place < 0) place = 0;
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	k = ((float)place + f) / (float)n;
	if (k > 1) k = 1;
	return out - (out - in) * k;
}

float mp4l_arm_angle(float r, float d, float pivot_deg, float len)
{
	const float deg = 180.0f / (float)M_PI;
	float pa = pivot_deg / deg, phi = atan2f(-sinf(pa), -cosf(pa)) * deg;
	float c = (len * len + d * d - r * r) / (2 * len * d);

	if (c > 1) c = 1;
	if (c < -1) c = -1;
	return phi - acosf(c) * deg;
}

/* ---- the cassette's styles ------------------------------------------------------ */

static const char *const TAPE_KEY[MP4T_COUNT] = {
	[MP4T_SMOKE] = "smoke", [MP4T_PINSTRIPE] = "pinstripe", [MP4T_BAND] = "band",
	[MP4T_FERRIC] = "ferric", [MP4T_METAL] = "metal", [MP4T_CLEAR] = "clear",
	[MP4T_RED] = "red", [MP4T_WORN] = "worn",
};
/* My picks: all eight kept (the Tape Styles lab, 2026-11-23). One left out
 * here leaves the row's list and Up and Down. */
static const bool TAPE_KEPT[MP4T_COUNT] = {
	[MP4T_SMOKE] = true, [MP4T_PINSTRIPE] = true, [MP4T_BAND] = true, [MP4T_FERRIC] = true,
	[MP4T_METAL] = true, [MP4T_CLEAR] = true, [MP4T_RED] = true, [MP4T_WORN] = true,
};

const char *mp4t_key(mp4t_id id)
{
	return id >= 0 && id < MP4T_COUNT ? TAPE_KEY[id] : TAPE_KEY[MP4T_SMOKE];
}

mp4t_id mp4t_from_key(const char *key)
{
	int i;

	for (i = 0; key && i < MP4T_COUNT; i++)
		if (!strcmp(key, TAPE_KEY[i])) return (mp4t_id)i;
	return MP4T_SMOKE;
}

bool mp4t_kept(mp4t_id id)
{
	return id >= 0 && id < MP4T_COUNT && TAPE_KEPT[id];
}

mp4t_id mp4t_step(mp4t_id id, int d)
{
	int i = mp4t_kept(id) ? (int)id : (d > 0 ? -1 : 0), k;

	d = d < 0 ? -1 : 1;
	for (k = 0; k < MP4T_COUNT; k++) {
		i = (i + d + MP4T_COUNT) % MP4T_COUNT;
		if (TAPE_KEPT[i]) return (mp4t_id)i;
	}
	return MP4T_SMOKE;
}

bool mp4t_pixel(mp4t_id id)
{
	return id != MP4T_SMOKE;
}

int mp4t_tape_no(double album_s)
{
	double m = album_s / 60.0;

	if (album_s <= 0) return 0;
	return m <= 46 ? 46 : m <= 60 ? 60 : m <= 90 ? 90 : 120;
}

char mp4t_side(int place, int n)
{
	if (n < 1) n = 1;
	return place < (n + 1) / 2 ? 'A' : 'B';
}

int mp4t_hub_frame(float ang)
{
	int f = (int)lroundf(ang / MP4T_HUB_STEP) % MP4T_HUB_FRAMES;

	return f < 0 ? f + MP4T_HUB_FRAMES : f;
}

int mp4t_pack_half(float r_cells)
{
	return (int)lroundf(r_cells * 2.0f);
}

int mp4t_cell(float px_per_unit)
{
	static const int CELLS[2] = { 8, 16 };
	int i;

	for (i = 0; i < 2; i++) {
		float px = (float)CELLS[i] * px_per_unit;

		if (px >= 1.0f && fabsf(px - roundf(px)) < 1e-3f) return CELLS[i];
	}
	return 0;
}

float mp4t_px_per_unit(int panel_w, int panel_h, int turn)
{
	int h = turn == 90 || turn == 270 ? panel_w : panel_h;

	return (float)h / 768.0f;
}
