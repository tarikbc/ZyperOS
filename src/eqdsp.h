/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
#ifndef ZYPEROS_EQDSP_H
#define ZYPEROS_EQDSP_H

/* The equalizer's sound: five bands, one filter each, the same everywhere
 * ZyperOS makes or passes on sound - MP4 (src/mp4d/mp4d.c), the menu music
 * (src/bgm_out.c), a game (Diatom's src/audio.c, a copy of this file) and a
 * port (plastron's zyperos-eq LADSPA plugin, a copy too). The settings and
 * the presets are the launcher's (src/eq.h); this is the arithmetic alone,
 * header-only and pure C (libm), so each of those builds takes it as it is.
 *
 *   60 Hz    a low shelf      250 Hz, 1 kHz, 4 kHz   peaks, Q 1.1
 *   12 kHz   a high shelf     each -6 to +6 dB, in whole steps
 *
 * The filters are RBJ's cookbook biquads (Robert Bristow-Johnson's "Audio
 * EQ Cookbook"), the shelves at slope 1, in direct form I: a form whose
 * coefficients can move under a playing signal without a click, which is
 * how a change is made. Every gain moves to its new value over EQ_RAMP_MS
 * (20 ms), the coefficients worked out again each millisecond on the way;
 * so a step on the Equalizer page is heard as a turn, never a tick.
 *
 * Flat costs nothing: with every band at 0 and no move under way, eq_run
 * returns at once and the samples are not touched. At 0 dB each filter is
 * exactly the identity (its numerator and denominator are the same), so
 * the step from flat to a curve, and back, is seamless.
 *
 * A boost can take a loud song over full scale. Past 90% of it the samples
 * bend smoothly toward full scale (a soft knee) rather than being cut flat,
 * and only while a band is boosted: a cut never needs it. The level is not
 * pulled down for a boost, so Bass is more bass, not less of the rest.
 *
 * And the volume boost past the top (src/boost.h, DESIGN.md 11.6): a gain
 * of up to EQ_GAIN_MAX dB after the bands, moving over EQ_RAMP_MS as they
 * do, then a limiter that keeps every peak under EQ_LIM_T (-1 dB of full
 * scale): the gain comes down at once where a sample would pass it and
 * goes back up over EQ_LIM_REL_MS, both channels together. So a quiet film
 * gets louder and a loud one gets no louder than full scale, only flatter.
 * No gain, nothing of it runs. MP4 and a game take it; the menu music never
 * does (its eq_state's gain stays 0).
 *
 * In the top cells, above EQ_COMP_FROM (the three cells under it are the
 * gain and the limiter alone, as they always were), a compressor and a soft
 * clip come between the gain and the limiter, harder with each cell: a loud
 * game or cart, whose peaks the limiter already holds, gets louder too, by
 * giving up some of its cleanness (Tarik, 2026-11-20, on 1.1.0-rc.8, of a
 * loud PICO-8 cart: "Yes, compress harder"). See eq_comp_block. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#define EQ_BANDS    5
#define EQ_DB_MIN   (-6)
#define EQ_DB_MAX   6
#define EQ_RAMP_MS  20

static const float EQ_FREQ[EQ_BANDS] = { 60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f };
#define EQ_PEAK_Q   1.1f

/* One filter's coefficients, divided by its a0 */
typedef struct { float b0, b1, b2, a1, a2; } eq_coef;

typedef struct {
	int     rate;                   /* samples a second, a channel */
	int     tick;                   /* samples a millisecond: one ramp step */
	float   now[EQ_BANDS];          /* each gain as it is, dB */
	float   want[EQ_BANDS];         /* and as it is going to be */
	float   step[EQ_BANDS];         /* dB a step while it moves */
	int     steps;                  /* steps left in the move, 0 at rest */
	int     until;                  /* samples left in this step */
	eq_coef c[EQ_BANDS];
	float   cw[EQ_BANDS], sw[EQ_BANDS];   /* each centre's cosine and sine */
	/* Each filter's last two inputs and outputs, a channel each */
	float   x1[EQ_BANDS][2], x2[EQ_BANDS][2], y1[EQ_BANDS][2], y2[EQ_BANDS][2];
	int     boost;                  /* a band above 0 now or wanted: the knee */
	/* The volume boost: its gain, linear (1 none), moving a frame at a time */
	float   gain_now, gain_want, gain_step;
	int     gain_left;              /* frames left in the move, 0 at rest */
	float   lim;                    /* the limiter's envelope, 16-bit scale */
	float   lim_rel;                /* how much of it is left after a frame */
	/* The top cells' compressor and soft clip (eq_comp_block): where the gain
	 * is among them, 0 at EQ_COMP_FROM and under, 1 at cell 4, 3 at cell 6,
	 * moving with the gain */
	float   comp_s, comp_s_want, comp_s_step;
	float   comp_env;               /* the compressor's envelope, 16-bit scale */
	float   comp_att, comp_rel;     /* how much of a rise it takes, of a fall it keeps, a frame */
	float   comp_g, comp_g_step;    /* its gain (with the make-up) now, and its step a frame */
	float   clip_k;                 /* where the soft clip starts, 16-bit scale */
	int     comp_n;                 /* frames left until the gain computer looks again */
} eq_state;

#define EQ_GAIN_MAX    18           /* dB: src/boost.h's six cells of 3 dB */
#define EQ_LIM_T       (0.8913f * 32768.0f)   /* -1 dB of full scale */
#define EQ_LIM_REL_MS  100

/* The top cells (my pick, 2026-11-20: "compressing harder only in the top
 * cells, so cells 1 to 3 sound as clean as now, and cells 4 to 6 trade some
 * cleanness for loudness"). Above EQ_COMP_FROM dB of gain, src/boost.h's
 * first three cells of EQ_COMP_CELL dB, the gain goes through two more
 * stages before the limiter, each harder a cell (EQ_COMP_TAB):
 *
 *   a compressor  a threshold under the limiter's ceiling, a ratio, a soft
 *                 knee EQ_COMP_KNEE dB wide and a make-up gain; both
 *                 channels by the louder (stereo-linked); its envelope a
 *                 peak taken in EQ_COMP_ATT_MS and let go over
 *                 EQ_COMP_REL_MS, so the waves of a held note move it a
 *                 tenth of a dB and a run of notes about two
 *   a soft clip   from a point under the ceiling, what is past it bent into
 *                 the room left to the ceiling, its slope 1 where it starts
 *                 and 0 at the ceiling, which no sample passes (both
 *                 channels by the louder, again)
 *
 * The make-up and the clip are what make a loud cart louder: its peaks are
 * at the ceiling already, so only a flatter wave is louder. Measured on
 * PICO-8-like chip voices (tools/eqdsp-check.c), a three-voice tune is
 * about 6, 7.3 and 8.8 dB louder at cells 4, 5 and 6 (4.6 at cell 3), all
 * the voices at full about 3.5, 4.5 and 5.7 (1.6); a sine at -12 dB of full
 * scale comes out with about 3%, 11% and 32% THD+N. Between two cells (a
 * gain moving, or a GAIN of no cell's) the table is read in a straight
 * line. The gain computer looks every EQ_COMP_BLOCK frames (one log, two
 * powers), and the gain moves there in a straight line: no power or
 * exponential a sample. tools/eqdsp-check.c holds the cells 1 to 3 to the
 * sample, and the top cells to their loudness, their peaks, their pumping,
 * silence and their cost. */
#define EQ_COMP_CELL     3          /* dB: src/boost.h's BOOST_DB_CELL */
#define EQ_COMP_FROM     (3 * EQ_COMP_CELL)   /* cells 1 to 3: the gain and the limiter alone */
#define EQ_COMP_KNEE     6.0f       /* dB wide */
#define EQ_COMP_ATT_MS   5
#define EQ_COMP_REL_MS   300
#define EQ_COMP_BLOCK    16

/* Each top cell's stages: the compressor's threshold (dB under the
 * ceiling), its ratio, its make-up (dB), and where the soft clip starts (dB
 * under the ceiling); the first row is none, cell 3's */
static const float EQ_COMP_TAB[4][4] = {
	{  0.0f, 1.0f,  0.0f,   0.0f },     /* cell 3 and under: none */
	{ -3.0f, 2.0f,  0.0f,  -4.5f },     /* cell 4 */
	{ -4.0f, 2.5f,  3.5f,  -5.0f },     /* cell 5 */
	{ -5.0f, 3.0f, 12.0f, -10.0f },     /* cell 6 */
};

/* Band `band`'s filter at `db`, its centre's cosine and sine at the rate
 * given (eq_init works them out once) */
static inline void eq_coef_at(eq_coef *c, int band, float db, float cw, float sw)
{
	const float A = powf(10.0f, db / 40.0f);
	float b0, b1, b2, a0, a1, a2;

	if (band == 0 || band == EQ_BANDS - 1) {
		/* A shelf at slope 1: alpha = sin(w0)/2 * sqrt(2) */
		const float alpha = sw * 0.70710678f, sa = 2.0f * sqrtf(A) * alpha;

		if (band == 0) {
			b0 = A * ((A + 1) - (A - 1) * cw + sa);
			b1 = 2 * A * ((A - 1) - (A + 1) * cw);
			b2 = A * ((A + 1) - (A - 1) * cw - sa);
			a0 = (A + 1) + (A - 1) * cw + sa;
			a1 = -2 * ((A - 1) + (A + 1) * cw);
			a2 = (A + 1) + (A - 1) * cw - sa;
		} else {
			b0 = A * ((A + 1) + (A - 1) * cw + sa);
			b1 = -2 * A * ((A - 1) + (A + 1) * cw);
			b2 = A * ((A + 1) + (A - 1) * cw - sa);
			a0 = (A + 1) - (A - 1) * cw + sa;
			a1 = 2 * ((A - 1) - (A + 1) * cw);
			a2 = (A + 1) - (A - 1) * cw - sa;
		}
	} else {
		const float alpha = sw / (2.0f * EQ_PEAK_Q);

		b0 = 1 + alpha * A;
		b1 = -2 * cw;
		b2 = 1 - alpha * A;
		a0 = 1 + alpha / A;
		a1 = -2 * cw;
		a2 = 1 - alpha / A;
	}
	c->b0 = b0 / a0;
	c->b1 = b1 / a0;
	c->b2 = b2 / a0;
	c->a1 = a1 / a0;
	c->a2 = a2 / a0;
}

static inline void eq_coef_of(eq_coef *c, int band, float db, int rate)
{
	const float w0 = 2.0f * 3.14159265358979f * EQ_FREQ[band] / (float)rate;

	eq_coef_at(c, band, db, cosf(w0), sinf(w0));
}

/* Flat, at `rate` (48000 for MP4, the menu music and the dmix) */
static inline void eq_init(eq_state *e, int rate)
{
	int b;

	memset(e, 0, sizeof *e);
	e->rate = rate > 0 ? rate : 48000;
	e->tick = e->rate / 1000 > 0 ? e->rate / 1000 : 1;
	e->gain_now = e->gain_want = 1.0f;
	e->lim_rel = expf(-1000.0f / ((float)EQ_LIM_REL_MS * (float)e->rate));
	e->comp_att = 1.0f - expf(-1000.0f / ((float)EQ_COMP_ATT_MS * (float)e->rate));
	e->comp_rel = expf(-1000.0f / ((float)EQ_COMP_REL_MS * (float)e->rate));
	e->comp_g = 1.0f;
	e->clip_k = EQ_LIM_T;
	for (b = 0; b < EQ_BANDS; b++) {
		const float w0 = 2.0f * 3.14159265358979f * EQ_FREQ[b] / (float)e->rate;

		e->cw[b] = cosf(w0);
		e->sw[b] = sinf(w0);
		eq_coef_at(&e->c[b], b, 0.0f, e->cw[b], e->sw[b]);
	}
}

/* Whether the bands would touch the samples: one off 0, or a move */
static inline int eq_bands_active(const eq_state *e)
{
	int b;

	if (e->steps > 0) return 1;
	for (b = 0; b < EQ_BANDS; b++)
		if (e->now[b] != 0.0f) return 1;
	return 0;
}

/* Whether the boost's gain would: some gain, or a move (the top cells'
 * strength moves with the gain, so it is on only while this is) */
static inline int eq_gain_active(const eq_state *e)
{
	return e->gain_left > 0 || e->gain_now != 1.0f;
}

/* Where `d` dB of gain is among the top cells: 0 to EQ_COMP_FROM, 1 at
 * cell 4, up to 3 at cell 6 (EQ_GAIN_MAX) */
static inline float eq_comp_of(int d)
{
	return d <= EQ_COMP_FROM ? 0.0f : (float)(d - EQ_COMP_FROM) / (float)EQ_COMP_CELL;
}

/* Whether eq_run would touch the samples at all */
static inline int eq_active(const eq_state *e)
{
	return eq_bands_active(e) || eq_gain_active(e);
}

/* The boost's gain to go to, dB (0..EQ_GAIN_MAX), over EQ_RAMP_MS */
static inline void eq_gain(eq_state *e, int db)
{
	const int d = db < 0 ? 0 : db > EQ_GAIN_MAX ? EQ_GAIN_MAX : db;
	const float want = d ? powf(10.0f, (float)d / 20.0f) : 1.0f;
	const int frames = e->tick * EQ_RAMP_MS;

	if (want == e->gain_want) return;
	e->gain_want = want;
	e->gain_step = (want - e->gain_now) / (float)frames;
	e->gain_left = frames;
	/* The top cells' strength over the same frames, in step with it */
	e->comp_s_want = eq_comp_of(d);
	e->comp_s_step = (e->comp_s_want - e->comp_s) / (float)frames;
}

/* The same at once: for a stream that is starting */
static inline void eq_gain_jump(eq_state *e, int db)
{
	eq_gain(e, db);
	e->gain_now = e->gain_want;
	e->gain_left = 0;
	e->comp_s = e->comp_s_want;
	e->comp_n = 0;
}

static inline void eq_boost_of(eq_state *e)
{
	int b;

	e->boost = 0;
	for (b = 0; b < EQ_BANDS; b++)
		if (e->now[b] > 0.0f || e->want[b] > 0.0f) e->boost = 1;
}

/* The bands to go to, dB, each held to -6..+6: from where they are now,
 * over EQ_RAMP_MS. The same again changes nothing. */
static inline void eq_set(eq_state *e, const int8_t db[EQ_BANDS])
{
	int b, same = 1;

	for (b = 0; b < EQ_BANDS; b++) {
		const int d = db[b] < EQ_DB_MIN ? EQ_DB_MIN : db[b] > EQ_DB_MAX ? EQ_DB_MAX : db[b];

		if ((float)d != e->want[b]) same = 0;
		e->want[b] = (float)d;
	}
	if (same) return;
	for (b = 0; b < EQ_BANDS; b++) e->step[b] = (e->want[b] - e->now[b]) / (float)EQ_RAMP_MS;
	e->steps = EQ_RAMP_MS;
	e->until = 0;
	eq_boost_of(e);
}

/* The same at once, with no move: for a stream that is starting */
static inline void eq_jump(eq_state *e, const int8_t db[EQ_BANDS])
{
	int b;

	eq_set(e, db);
	for (b = 0; b < EQ_BANDS; b++) {
		e->now[b] = e->want[b];
		eq_coef_at(&e->c[b], b, e->now[b], e->cw[b], e->sw[b]);
	}
	e->steps = 0;
	eq_boost_of(e);
}

/* Past 90% of full scale, bent smoothly toward it: f(0.9) = 0.9 with a
 * slope of 1, f reaching 1 as x goes on. Only above the knee. */
static inline float eq_knee(float v)
{
	const float k = 0.9f * 32768.0f, room = 32767.0f - k;
	const float a = v < 0 ? -v : v;
	float over, out;

	if (a <= k) return v;
	over = (a - k) / room;
	out = k + room * (over / (1.0f + over));
	return v < 0 ? -out : out;
}

static inline int16_t eq_s16(float v)
{
	return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int16_t)lrintf(v);
}

/* One band over `n` frames of `f` (interleaved stereo, float), in place,
 * its coefficients fixed: the two channels' recursions side by side, the
 * state in locals, so the loop is the filter's arithmetic alone */
static inline void eq_band_run(eq_state *e, int b, float *f, int n)
{
	const eq_coef c = e->c[b];
	float lx1 = e->x1[b][0], lx2 = e->x2[b][0], ly1 = e->y1[b][0], ly2 = e->y2[b][0];
	float rx1 = e->x1[b][1], rx2 = e->x2[b][1], ry1 = e->y1[b][1], ry2 = e->y2[b][1];
	int i;

	for (i = 0; i < n; i++) {
		const float l = f[i * 2], r = f[i * 2 + 1];
		const float yl = c.b0 * l + c.b1 * lx1 + c.b2 * lx2 - c.a1 * ly1 - c.a2 * ly2;
		const float yr = c.b0 * r + c.b1 * rx1 + c.b2 * rx2 - c.a1 * ry1 - c.a2 * ry2;

		lx2 = lx1; lx1 = l; ly2 = ly1; ly1 = yl;
		rx2 = rx1; rx1 = r; ry2 = ry1; ry1 = yr;
		f[i * 2] = yl;
		f[i * 2 + 1] = yr;
	}
	/* Denormals: a filter ringing down to nothing would run its state into
	 * numbers the FPU takes many cycles over */
	if (fabsf(ly1) < 1e-15f) ly1 = 0.0f;
	if (fabsf(ly2) < 1e-15f) ly2 = 0.0f;
	if (fabsf(ry1) < 1e-15f) ry1 = 0.0f;
	if (fabsf(ry2) < 1e-15f) ry2 = 0.0f;
	e->x1[b][0] = lx1; e->x2[b][0] = lx2; e->y1[b][0] = ly1; e->y2[b][0] = ly2;
	e->x1[b][1] = rx1; e->x2[b][1] = rx2; e->y1[b][1] = ry1; e->y2[b][1] = ry2;
}

/* The top cells' gain computer, every EQ_COMP_BLOCK frames: the
 * compressor's gain for its envelope now, at the cell e->comp_s is at, with
 * the make-up (a soft knee: Giannoulis, Massberg and Reiss, "Digital
 * Dynamic Range Compressor Design", 2012), which the frames until the next
 * look move to in a straight line; and where the soft clip starts. */
static inline void eq_comp_block(eq_state *e)
{
	const float at = e->comp_s < 0.0f ? 0.0f : e->comp_s > 3.0f ? 3.0f : e->comp_s;
	const int row = at >= 3.0f ? 2 : (int)at;
	const float w = at - (float)row;
	const float *a = EQ_COMP_TAB[row], *b = EQ_COMP_TAB[row + 1];
	const float th = a[0] + w * (b[0] - a[0]), ratio = a[1] + w * (b[1] - a[1]);
	const float makeup = a[2] + w * (b[2] - a[2]), clip = a[3] + w * (b[3] - a[3]);
	const float x = e->comp_env > 1.0f ? 20.0f * log10f(e->comp_env / EQ_LIM_T) : -120.0f;
	const float over = x - th;
	float gr = 0.0f;

	if (2.0f * over > EQ_COMP_KNEE) {
		gr = (1.0f / ratio - 1.0f) * over;
	} else if (2.0f * over > -EQ_COMP_KNEE) {
		const float k = over + EQ_COMP_KNEE / 2.0f;

		gr = (1.0f / ratio - 1.0f) * k * k / (2.0f * EQ_COMP_KNEE);
	}
	e->comp_g_step = (powf(10.0f, (gr + makeup) / 20.0f) - e->comp_g) / (float)EQ_COMP_BLOCK;
	e->clip_k = clip < 0.0f ? EQ_LIM_T * powf(10.0f, clip / 20.0f) : EQ_LIM_T;
	e->comp_n = EQ_COMP_BLOCK;
}

/* The top cells over `n` frames of `f`, between the gain and the limiter:
 * the compressor, then the soft clip, both channels by the louder. Where it
 * is among the cells moves as the gain does (eq_gain), each frame. */
static inline void eq_comp_run(eq_state *e, float *f, int n)
{
	float env = e->comp_env, g = e->comp_g;
	const float att = e->comp_att, rel = e->comp_rel;
	int i;

	for (i = 0; i < n; i++) {
		float l = f[i * 2], r = f[i * 2 + 1], p, q;

		if (e->comp_s != e->comp_s_want) {
			e->comp_s += e->comp_s_step;
			if ((e->comp_s_step > 0) == (e->comp_s > e->comp_s_want)) e->comp_s = e->comp_s_want;
		}
		p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
		env = p > env ? env + att * (p - env) : env * rel;
		if (e->comp_n <= 0) {
			e->comp_env = env;
			e->comp_g = g;
			eq_comp_block(e);
		}
		e->comp_n--;
		g += e->comp_g_step;
		l *= g;
		r *= g;
		/* The soft clip: past clip_k, u rooms in, out as u - u*u/4, a
		 * room at u = 2 and on, its slope 1 at the start and 0 there; with
		 * no clip (cell 3's row), none, the limiter's */
		p *= g;
		if (p > e->clip_k && e->clip_k < EQ_LIM_T) {
			const float room = EQ_LIM_T - e->clip_k, u = (p - e->clip_k) / room;

			q = (u >= 2.0f ? EQ_LIM_T : e->clip_k + room * (u - 0.25f * u * u)) / p;
			l *= q;
			r *= q;
		}
		f[i * 2] = l;
		f[i * 2 + 1] = r;
	}
	/* Silence lets the envelope fall towards 0: no denormal is kept */
	e->comp_env = env < 1e-3f ? 0.0f : env;
	e->comp_g = g;
}

/* The boost's gain and the limiter over `n` frames of `f`, in place; in the
 * top cells, the compressor and the soft clip between the two (eq_comp_run,
 * which the frames under EQ_COMP_FROM never reach: they are the gain and
 * the limiter alone, to the sample, as before the top cells had them) */
static inline void eq_gain_run(eq_state *e, float *f, int n)
{
	float g = e->gain_now, env = e->lim;
	const float rel = e->lim_rel;
	int i;

	if (e->comp_s > 0.0f || e->comp_s_want > 0.0f) {
		/* The gain first, then the top cells, then the limiter, a run at a
		 * time: each frame's gain as the loop below gives it */
		for (i = 0; i < n; i++) {
			if (e->gain_left > 0) g = --e->gain_left ? g + e->gain_step : e->gain_want;
			f[i * 2] *= g;
			f[i * 2 + 1] *= g;
		}
		e->gain_now = g;
		eq_comp_run(e, f, n);
		for (i = 0; i < n; i++) {
			float l = f[i * 2], r = f[i * 2 + 1], p;

			p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
			env *= rel;
			if (p > env) env = p;
			if (env > EQ_LIM_T) {
				const float k = EQ_LIM_T / env;

				f[i * 2] = l * k;
				f[i * 2 + 1] = r * k;
			}
		}
		e->lim = env < 1.0f ? 0.0f : env;
		return;
	}
	for (i = 0; i < n; i++) {
		float l, r, p;

		if (e->gain_left > 0) g = --e->gain_left ? g + e->gain_step : e->gain_want;
		l = f[i * 2] * g;
		r = f[i * 2 + 1] * g;
		p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
		env *= rel;               /* never under the sample: |out| <= EQ_LIM_T */
		if (p > env) env = p;
		if (env > EQ_LIM_T) {
			const float k = EQ_LIM_T / env;

			l *= k;
			r *= k;
		}
		f[i * 2] = l;
		f[i * 2 + 1] = r;
	}
	e->gain_now = g;
	e->lim = env < 1.0f ? 0.0f : env;
}

/* The float copy eq_run works through, in frames */
#define EQ_SEG 256

/* `frames` interleaved stereo frames of `f`, float at 16-bit's scale (full
 * scale 32768), in place: in runs over which the coefficients hold still (a
 * whole buffer at rest, a millisecond while a gain moves), each band over
 * the run in turn, then the knee while a band is boosted. A port's plugin
 * calls it on its own floats; eq_run on 16-bit samples. */
static inline void eq_run_f(eq_state *e, float *f, int frames)
{
	int at = 0, i, b;

	if (!eq_active(e)) return;
	while (at < frames && eq_bands_active(e)) {
		int n = frames - at;

		if (e->steps > 0 && e->until <= 0) {
			/* The next step of a move: the gains and their filters */
			e->steps--;
			for (b = 0; b < EQ_BANDS; b++) {
				e->now[b] = e->steps ? e->now[b] + e->step[b] : e->want[b];
				eq_coef_at(&e->c[b], b, e->now[b], e->cw[b], e->sw[b]);
			}
			if (!e->steps) eq_boost_of(e);
			e->until = e->tick;
		}
		if (e->steps > 0 && n > e->until) n = e->until;
		for (b = 0; b < EQ_BANDS; b++) eq_band_run(e, b, f + at * 2, n);
		if (e->boost)
			for (i = at * 2; i < (at + n) * 2; i++) f[i] = eq_knee(f[i]);
		if (e->steps > 0) e->until -= n;
		at += n;
	}
	if (eq_gain_active(e)) eq_gain_run(e, f, frames);
}

/* `frames` interleaved stereo frames of `buf`, in place, through a float
 * copy a run at a time */
static inline void eq_run(eq_state *e, int16_t *buf, int frames)
{
	float f[EQ_SEG * 2];
	int at, i;

	if (!eq_active(e)) return;
	for (at = 0; at < frames; ) {
		const int n = frames - at < EQ_SEG ? frames - at : EQ_SEG;

		for (i = 0; i < n * 2; i++) f[i] = (float)buf[at * 2 + i];
		eq_run_f(e, f, n);
		for (i = 0; i < n * 2; i++) buf[at * 2 + i] = eq_s16(f[i]);
		at += n;
	}
}

/* ---- the gains a port's plugin reads, from the launcher ------------------- */

/* The file the launcher keeps the bands in force for the output in use, for
 * the processes that cannot be told over a socket: a port's ALSA plugin
 * (plastron's zyperos-eq) maps it and reads it once a period. Fixed size,
 * written in place: `seq` is odd while the launcher writes and even after,
 * so a reader that sees it change, or odd, reads again. All bands 0 is
 * flat. $ZYPEROS_EQ_FILE stands for the path on a computer.
 *
 * `gain` is the volume boost's, dB (eq_gain): the launcher writes it with
 * every change and portkeys while a port runs, the launcher being gone
 * then. A file from before the boost has 0 there, its padding.
 *
 * `cut` is your PICO-8's quiet while MP4 plays (DESIGN.md 11.55), as a
 * Diatom game's QUIET: 1, and the port's plugin plays silence, faded over
 * EQ_RAMP_MS; 0, its sound as before. One byte, stored and read whole, out
 * of `seq`'s count: MP4's follower and the in-game menu over PICO-8 write it
 * while portkeys writes the gain, and two writers of `seq` could leave it
 * odd. The launcher sets it to 0 as it starts and as it hands over to any
 * other program. A file from before has 0 there; a plugin from before reads
 * none, and PICO-8 is heard, as before. */
#define EQ_SHARE_PATH  "/run/zyperos/eq"
#define EQ_SHARE_MAGIC 0x3151455aU    /* "ZEQ1" */

typedef struct {
	uint32_t magic;
	uint32_t seq;
	int8_t   db[EQ_BANDS];
	int8_t   gain;
	uint8_t  cut;
	uint8_t  pad;
} eq_share;

/* `s`'s bands into `db` and its gain into `*gain` (when not NULL), as a
 * reader takes them: 0 (and nothing touched) while it is being written, or
 * for a file that is not one */
static inline int eq_share_read(const volatile eq_share *s, int8_t db[EQ_BANDS], int8_t *gain)
{
	uint32_t a, z;
	int8_t got[EQ_BANDS], g;
	int b;

	if (!s || s->magic != EQ_SHARE_MAGIC) return 0;
	a = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
	if (a & 1) return 0;
	for (b = 0; b < EQ_BANDS; b++) got[b] = s->db[b];
	g = s->gain;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	z = __atomic_load_n(&s->seq, __ATOMIC_RELAXED);
	if (z != a) return 0;
	memcpy(db, got, sizeof got);
	if (gain) *gain = g;
	return 1;
}

/* Written in place as a reader expects (seq odd, the change, seq even):
 * the bands from `db` when not NULL, the gain when `gain` is 0 or more.
 * For a mapped file of one writer at a time. */
static inline void eq_share_put(volatile eq_share *s, const int8_t *db, int gain)
{
	int b;

	__atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (db)
		for (b = 0; b < EQ_BANDS; b++) s->db[b] = db[b];
	if (gain >= 0) s->gain = (int8_t)(gain > EQ_GAIN_MAX ? EQ_GAIN_MAX : gain);
	__atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELEASE);
}

/* The cut, as a reader takes it, and written: one byte, whole (see above) */
static inline int eq_share_cut_get(const volatile eq_share *s)
{
	return s && s->magic == EQ_SHARE_MAGIC && __atomic_load_n(&s->cut, __ATOMIC_ACQUIRE) == 1;
}

static inline void eq_share_cut_put(volatile eq_share *s, int cut)
{
	__atomic_store_n(&s->cut, (uint8_t)(cut ? 1 : 0), __ATOMIC_RELEASE);
}

/* ---- the cut, faded: a port's plugin playing silence while MP4 plays ---- */

/* 1 heard, 0 silence, moving over EQ_RAMP_MS a frame at a time, so a cut is
 * no click */
typedef struct {
	float now, want, step;
	int   left;              /* frames left in the move, 0 at rest */
	int   frames;            /* a move's frames, at the stream's rate */
} eq_fade;

static inline void eq_fade_init(eq_fade *f, int rate)
{
	f->now = f->want = 1.0f;
	f->step = 0.0f;
	f->left = 0;
	f->frames = (rate > 0 ? rate : 48000) / 1000 * EQ_RAMP_MS;
	if (f->frames < 1) f->frames = 1;
}

/* Cut (silence) or not, faded to; at once with `jump`, for a stream that is
 * starting */
static inline void eq_fade_cut(eq_fade *f, int cut, int jump)
{
	const float want = cut ? 0.0f : 1.0f;

	if (jump) {
		f->now = f->want = want;
		f->left = 0;
		return;
	}
	if (want == f->want) return;
	f->want = want;
	f->step = (want - f->now) / (float)f->frames;
	f->left = f->frames;
}

/* Whether it does anything: a move, or silence */
static inline int eq_fade_active(const eq_fade *f)
{
	return f->left > 0 || f->now != 1.0f;
}

/* Two channels of `n` frames, each its own buffer, in place */
static inline void eq_fade_run(eq_fade *f, float *l, float *r, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (f->left > 0) f->now = --f->left ? f->now + f->step : f->want;
		l[i] *= f->now;
		r[i] *= f->now;
	}
}

#endif
