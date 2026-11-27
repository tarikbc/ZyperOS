/* SPDX-License-Identifier: GPL-3.0-only */
/* The equalizer's arithmetic (src/eqdsp.h), measured on test tones:
 *
 *     make check-eqdsp
 *
 * Flat touches nothing; each band at +6 dB lifts its own tone by about 6 dB
 * and leaves a tone two octaves away within 1.5 dB; a step of a band moves
 * the sound over 20 ms with no jump bigger than the tone's own; the knee
 * keeps a boosted loud tone under full scale; the shared file reads back.
 * And what it costs: the time for a second of 48 kHz stereo through five
 * bands, against the second it lasts (a share of one core of this computer;
 * the Pixel 2's Cortex-A35 is slower, so the device's number is its own).
 *
 * And the volume boost's top cells (cells 4 to 6, EQ_COMP_TAB): cells 1 to
 * 3 the boost as it was before them, to the sample, against a copy of it
 * here; on PICO-8-like chip voices at fake08's levels and a loud film, each
 * top cell louder than the one under it, a three-voice tune about 9 dB and
 * all voices at full about 6 dB louder at cell 6, no sample past -1 dB of
 * full scale in any cell or on the way between them, a sine's THD+N cell by
 * cell, the compressor's swing on a held tune and on a run of notes,
 * silence after it silence (no subnormal, no NaN), and its cost. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/eqdsp.h"

#define RATE 48000

static int fails;

static void report(int ok, const char *name, double got)
{
	printf("  %s %s (%.3f)\n", ok ? "PASS" : "FAIL", name, got);
	if (!ok) fails++;
}

/* This thread's own CPU time, in seconds. The costs below are timed on it, the least of 20
   passes: wall time counted every moment another program held the core, and make check runs
   four at a time, on a Mac that may be loaded besides (2026-11-24). */
static double cpu_s(void)
{
	struct timespec t;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static void tone(int16_t *buf, int n, float hz, float amp)
{
	int i;

	for (i = 0; i < n; i++) {
		const int16_t v = (int16_t)lrintf(amp * 32767.0f * sinf(2.0f * 3.14159265f * hz * (float)i / RATE));

		buf[i * 2] = buf[i * 2 + 1] = v;
	}
}

/* The level of the second half of `buf` (the filters settled), dB re `amp` */
static double level_db(const int16_t *buf, int n, float amp)
{
	double sq = 0;
	int i;

	for (i = n / 2; i < n; i++) sq += (double)buf[i * 2] * buf[i * 2];
	return 20.0 * log10(sqrt(sq / (n - n / 2)) / (amp * 32767.0 / sqrt(2.0)));
}

static double gain_at(int band, int db, float hz)
{
	static int16_t buf[RATE * 2];
	int8_t g[EQ_BANDS] = { 0 };
	eq_state e;

	eq_init(&e, RATE);
	g[band] = (int8_t)db;
	eq_jump(&e, g);
	tone(buf, RATE, hz, 0.1f);
	eq_run(&e, buf, RATE);
	return level_db(buf, RATE, 0.1f);
}

/* ---- the volume boost's top cells (src/eqdsp.h's EQ_COMP_TAB) ------------ */

/* The boost as it was before the top cells came (src/eqdsp.h's eq_gain and
 * eq_gain_run at 1.1.0-rc.8, word for word): cells 1 to 3 must still be it,
 * to the sample */
typedef struct {
	int   tick;
	float gain_now, gain_want, gain_step;
	int   gain_left;
	float lim, lim_rel;
} ref_boost;

static void ref_init(ref_boost *r, int rate)
{
	memset(r, 0, sizeof *r);
	r->tick = rate / 1000;
	r->gain_now = r->gain_want = 1.0f;
	r->lim_rel = expf(-1000.0f / ((float)EQ_LIM_REL_MS * (float)rate));
}

static void ref_gain(ref_boost *e, int db)
{
	const int d = db < 0 ? 0 : db > EQ_GAIN_MAX ? EQ_GAIN_MAX : db;
	const float want = d ? powf(10.0f, (float)d / 20.0f) : 1.0f;
	const int frames = e->tick * EQ_RAMP_MS;

	if (want == e->gain_want) return;
	e->gain_want = want;
	e->gain_step = (want - e->gain_now) / (float)frames;
	e->gain_left = frames;
}

static void ref_run(ref_boost *e, float *f, int n)
{
	float g = e->gain_now, env = e->lim;
	const float rel = e->lim_rel;
	int i;

	if (!(e->gain_left > 0 || e->gain_now != 1.0f)) return;   /* eq_run_f's eq_active, flat bands */
	for (i = 0; i < n; i++) {
		float l, r, p;

		if (e->gain_left > 0) g = --e->gain_left ? g + e->gain_step : e->gain_want;
		l = f[i * 2] * g;
		r = f[i * 2 + 1] * g;
		p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
		env *= rel;
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

/* PICO-8-like chip voices at fake08's levels (its synth's amplitudes, which
 * it took from PICO-8's own exports: a square +-0.25, a triangle +-0.5, a
 * saw +-0.33, at a note's volume v/7), and a loud film or game: filtered
 * noise in syllables at 4 a second with a loud hit every 0.7 s, its peak at
 * -1 dB of full scale. Mono, in both channels, float at 16-bit's scale. */
enum { SIG_QUIET, SIG_TUNE, SIG_FULL, SIG_FILM, SIG_SINE, SIG_NOTES, SIGS };
static const char *const SIG_NAME[SIGS] = {
	"one quiet voice (square at 5)", "a three-voice tune (square 5, triangle 5, saw 4)",
	"all voices full (square, triangle, saw at 7)", "a loud film or game (noise, peaks at -1 dB)",
	"a 1 kHz sine at -12 dB of full scale", "a run of notes (8 a second, each fading)",
};

#define SIG_FRAMES (RATE * 2)
/* The limiter's ceiling, to a float's last bit: a sample scaled to it
 * rounds a bit-step either way (0.002 at 16-bit's scale) */
#define CEILING (EQ_LIM_T * (1.0f + 1e-6f))

static float sq_w(double t, double hz) { return fmod(t * hz, 1.0) < 0.5 ? 0.25f : -0.25f; }
static float tri_w(double t, double hz)
{
	const double p = fmod(t * hz, 1.0);

	return (float)((p < 0.5 ? 4 * p - 1 : 3 - 4 * p) * 0.5);
}
static float saw_w(double t, double hz) { return (float)((fmod(t * hz, 1.0) * 2 - 1) * 0.33); }

static void signal_of(int kind, float *f, int n)
{
	unsigned rng = 1;
	float lp = 0, lp2 = 0, peak = 0;
	int i;

	for (i = 0; i < n; i++) {
		const double t = (double)i / RATE;
		float v = 0;

		switch (kind) {
		case SIG_QUIET: v = 5 / 7.f * sq_w(t, 220); break;
		case SIG_TUNE: v = 5 / 7.f * sq_w(t, 220) + 5 / 7.f * tri_w(t, 110) + 4 / 7.f * saw_w(t, 330); break;
		case SIG_FULL: v = sq_w(t, 220) + tri_w(t, 110) + saw_w(t, 330); break;
		case SIG_FILM: {
			float w;
			double env = 0.25 + 0.2 * sin(2 * 3.14159265358979 * 4 * t);

			rng = rng * 1664525u + 1013904223u;
			w = (float)((int)(rng >> 8) - (1 << 23)) / (float)(1 << 23);
			lp += 0.15f * (w - lp);
			lp2 += 0.15f * (lp - lp2);
			if (fmod(t, 0.7) < 0.05) env = 1.0;
			v = (float)(lp2 * env * 3.0);
			break;
		}
		case SIG_SINE: v = (float)(0.25 * sin(2 * 3.14159265358979 * 1000.0 * t)); break;
		default: {
			const double a = exp(-fmod(t * 8, 1.0) * 4);

			v = (float)(a * (5 / 7.f * sq_w(t, 220 * (1 + (int)(t * 8) % 3 * 0.25)) + 4 / 7.f * tri_w(t, 110)));
		}
		}
		if (v > 1) v = 1;
		if (v < -1) v = -1;
		f[i * 2] = f[i * 2 + 1] = v * 32768.0f;
		if (fabsf(v) > peak) peak = fabsf(v);
	}
	if (kind == SIG_FILM)
		for (i = 0; i < n * 2; i++) f[i] *= 0.8913f / peak;
}

/* RMS of the first channel over frames [a, b) */
static double rms_of(const float *f, int a, int b)
{
	double s = 0;
	int i;

	for (i = a; i < b; i++) s += (double)f[i * 2] * f[i * 2];
	return sqrt(s / (b - a));
}

/* THD+N of a sine at `hz` over frames [a, b): what is left after the best
 * sine and offset, against that sine, in percent */
static double thdn_of(const float *f, int a, int b, double hz)
{
	double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, y0 = 0, det, A, B, dc, res = 0, fit = 0;
	int i;

	for (i = a; i < b; i++) {
		const double w = 2 * 3.14159265358979 * hz * i / RATE, s = sin(w), c = cos(w), y = f[i * 2];

		ss += s * s; cc += c * c; sc += s * c; ys += y * s; yc += y * c; y0 += y;
	}
	det = ss * cc - sc * sc;
	A = (ys * cc - yc * sc) / det;
	B = (yc * ss - ys * sc) / det;
	dc = y0 / (b - a);
	for (i = a; i < b; i++) {
		const double w = 2 * 3.14159265358979 * hz * i / RATE, m = A * sin(w) + B * cos(w) + dc, e = f[i * 2] - m;

		res += e * e;
		fit += (m - dc) * (m - dc);
	}
	return 100.0 * sqrt(res / fit);
}

/* `kind` through the boost at `cell`, a stream that starts there, in the
 * plugin's runs of 256 frames: the dB heard over the second second, the
 * output's peak, and the compressor's gain at each run's end over it, its
 * swing in dB (lowest to highest) */
static double through(int kind, int cell, float *out, float *peak, double *swing)
{
	static float in[SIG_FRAMES * 2];
	double lo = 1e9, hi = -1e9;
	eq_state e;
	int i;

	signal_of(kind, in, SIG_FRAMES);
	memcpy(out, in, sizeof in);
	eq_init(&e, RATE);
	eq_gain_jump(&e, cell * EQ_COMP_CELL);
	for (i = 0; i < SIG_FRAMES; i += 256) {
		eq_run_f(&e, out + i * 2, 256);
		if (i >= RATE && e.comp_g > 0) {
			const double d = 20.0 * log10(e.comp_g);

			if (d < lo) lo = d;
			if (d > hi) hi = d;
		}
	}
	*peak = 0;
	for (i = 0; i < SIG_FRAMES * 2; i++)
		if (fabsf(out[i]) > *peak || out[i] != out[i]) *peak = out[i] != out[i] ? INFINITY : fabsf(out[i]);
	if (swing) *swing = cell > 3 ? hi - lo : 0;
	return 20.0 * log10(rms_of(out, RATE, SIG_FRAMES) / rms_of(in, RATE, SIG_FRAMES));
}

/* src/boost.h's number named `name`, -1 when it has none */
static long boost_define(const char *name)
{
	FILE *f = fopen("src/boost.h", "r");
	char line[256], key[64];
	long v;

	if (!f) return -1;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "#define %63s %ld", key, &v) == 2 && !strcmp(key, name)) {
			fclose(f);
			return v;
		}
	fclose(f);
	return -1;
}

static void top_cells(void)
{
	static float out[SIG_FRAMES * 2], ref[SIG_FRAMES * 2];
	static const int steps[][2] = {   /* frames, then the gain asked, dB */
		{ 4800, 3 }, { 9000, 9 }, { 300, 6 }, { 24000, 0 }, { 960, 9 }, { 12000, 6 },
		{ 700, 3 }, { 30000, 9 }, { 5000, 0 },
	};
	double heard[SIGS][7], swing[SIGS][7], thd[7];
	float peak, worst = 0;
	int kind, cell, i, exact = 1, rising = 1, k;

	printf("eqdsp: the boost's top cells\n");
	report(boost_define("BOOST_DB_CELL") == EQ_COMP_CELL &&
	       boost_define("BOOST_VOL_CELLS") * EQ_COMP_CELL == EQ_GAIN_MAX &&
	       (int)(sizeof EQ_COMP_TAB / sizeof EQ_COMP_TAB[0]) == boost_define("BOOST_VOL_CELLS") - 2,
	       "the_table_has_a_row_for_cell_3_and_each_top_cell_of_src_boost_h", (double)EQ_COMP_CELL);

	/* Cells 1 to 3, to the sample: the gain and the limiter alone, as
	 * before, through moves between them, on every signal */
	for (kind = 0; kind < SIGS && exact; kind++) {
		eq_state e;
		ref_boost r;
		int at = 0;

		signal_of(kind, out, SIG_FRAMES);
		for (i = 0; i < SIG_FRAMES * 2; i++) out[i] *= kind == SIG_SINE ? 3.5f : 1.0f;
		memcpy(ref, out, sizeof out);
		eq_init(&e, RATE);
		ref_init(&r, RATE);
		for (k = 0; k < (int)(sizeof steps / sizeof steps[0]) && at < SIG_FRAMES; k++) {
			int left = steps[k][0];

			eq_gain(&e, steps[k][1]);
			ref_gain(&r, steps[k][1]);
			while (left > 0 && at < SIG_FRAMES) {
				const int n = left < 256 ? left : 256;

				eq_run_f(&e, out + at * 2, n < SIG_FRAMES - at ? n : SIG_FRAMES - at);
				ref_run(&r, ref + at * 2, n < SIG_FRAMES - at ? n : SIG_FRAMES - at);
				at += n;
				left -= n;
			}
		}
		if (memcmp(out, ref, sizeof out)) exact = 0;
	}
	report(exact, "cells_1_to_3_and_their_moves_are_the_boost_as_it_was_to_the_sample", 0);

	/* The top cells: louder with each, every signal; the targets for the
	 * loud carts at cell 6; no sample past the ceiling */
	for (kind = 0; kind < SIGS; kind++) {
		printf("  %s:", SIG_NAME[kind]);
		for (cell = 3; cell <= 6; cell++) {
			heard[kind][cell] = through(kind, cell, out, &peak, &swing[kind][cell]);
			if (peak > worst) worst = peak;
			if (kind == SIG_SINE) thd[cell] = thdn_of(out, RATE, SIG_FRAMES, 1000.0);
			printf(" cell %d %+.1f dB", cell, heard[kind][cell]);
			if (kind == SIG_SINE) printf(" (THD+N %.1f%%)", thd[cell]);
			if (cell > 3 && heard[kind][cell] < heard[kind][cell - 1] + 0.3) rising = 0;
		}
		printf("\n");
	}
	report(rising, "each_top_cell_is_louder_than_the_one_under_it_on_every_signal", 0);
	report(heard[SIG_TUNE][6] > 8.0 && heard[SIG_TUNE][6] < 9.5, "cell_6_a_three_voice_tune_about_9_db_louder",
	       heard[SIG_TUNE][6]);
	report(heard[SIG_FULL][6] > 5.0 && heard[SIG_FULL][6] < 6.5, "cell_6_all_voices_full_about_6_db_louder",
	       heard[SIG_FULL][6]);
	report(heard[SIG_FULL][3] < 2.5 && heard[SIG_TUNE][3] < 5.5, "where_cell_3_left_them_the_limiter_alone",
	       heard[SIG_FULL][3]);
	report(worst <= CEILING, "no_sample_past_minus_1_db_in_any_cell", worst / 32768.0);

	/* What they trade: a sine's THD+N, cell by cell */
	report(thd[3] < 0.01 && thd[4] < 5.0 && thd[5] < 15.0 && thd[6] < 40.0,
	       "a_sines_thd_n_cell_3_none_then_under_5_15_and_40_percent", thd[6]);

	/* Pumping: a held tune moves the compressor a tenth of a dB, a run of
	 * fading notes about two */
	report(swing[SIG_TUNE][4] < 0.3 && swing[SIG_TUNE][5] < 0.3 && swing[SIG_TUNE][6] < 0.3,
	       "a_held_tune_moves_the_compressor_under_0_3_db", swing[SIG_TUNE][6]);
	report(swing[SIG_NOTES][6] < 3.5, "a_run_of_notes_moves_it_under_3_5_db", swing[SIG_NOTES][6]);
	printf("  the compressor's swing at cells 4, 5, 6: held tune %.2f, %.2f, %.2f dB; notes %.2f, %.2f, %.2f dB\n",
	       swing[SIG_TUNE][4], swing[SIG_TUNE][5], swing[SIG_TUNE][6], swing[SIG_NOTES][4], swing[SIG_NOTES][5],
	       swing[SIG_NOTES][6]);

	/* Moving through the top cells and out again under a loud film: nothing
	 * past the ceiling on the way, no NaN */
	{
		static const int moves[] = { 9, 18, 12, 15, 0, 18, 9, 12, 3 };
		eq_state e;
		int bad = 0;

		signal_of(SIG_FILM, out, SIG_FRAMES);
		eq_init(&e, RATE);
		for (i = 0; i < SIG_FRAMES; i += 256) {
			int j;

			if (i % (RATE / 5) == 0) eq_gain(&e, moves[(i / (RATE / 5)) % 9]);
			eq_run_f(&e, out + i * 2, 256);
			for (j = 0; j < 512; j++)
				if (!(fabsf(out[i * 2 + j]) <= CEILING)) bad++;
		}
		report(!bad, "moving_through_the_top_cells_and_out_nothing_past_the_ceiling", bad);
	}

	/* Silence after a loud cart at cell 6: silence out, every state 0 or
	 * normal, never a subnormal or a NaN; a near-silence of tiny samples
	 * the same */
	{
		eq_state e;
		int bad = 0;

		signal_of(SIG_FULL, out, SIG_FRAMES);
		eq_init(&e, RATE);
		eq_gain_jump(&e, EQ_GAIN_MAX);
		for (i = 0; i < SIG_FRAMES; i += 256) eq_run_f(&e, out + i * 2, 256);
		for (k = 0; k < 3; k++) {
			for (i = 0; i < SIG_FRAMES * 2; i++) out[i] = k == 1 ? 1e-30f * (float)(i & 1 ? 1 : -1) : 0.0f;
			for (i = 0; i < SIG_FRAMES; i += 256) eq_run_f(&e, out + i * 2, 256);
			for (i = 0; i < SIG_FRAMES * 2; i++)
				if (out[i] != out[i] || fpclassify(out[i]) == FP_SUBNORMAL || (k != 1 && out[i] != 0.0f)) bad++;
		}
		report(!bad && e.comp_env == 0.0f && e.lim == 0.0f && isfinite(e.comp_g) &&
		       fpclassify(e.comp_g) != FP_SUBNORMAL && isfinite(e.clip_k),
		       "silence_after_cell_6_is_silence_no_subnormal_no_nan", bad);
	}

	/* What it costs: a second of stereo at cell 6, and at cell 3. This thread's CPU time, the
	   least of 20 passes (cpu_s() says why). */
	{
		const int reps = 20;
		double s6 = 1e9, s3 = 1e9, t0, one;
		eq_state e;
		int c;

		for (c = 3; c <= 6; c += 3) {
			signal_of(SIG_TUNE, ref, RATE);
			eq_init(&e, RATE);
			eq_gain_jump(&e, c * EQ_COMP_CELL);
			for (k = 0; k < reps; k++) {
				memcpy(out, ref, sizeof(float) * RATE * 2);
				t0 = cpu_s();
				for (i = 0; i < RATE; i += 256) eq_run_f(&e, out + i * 2, 256);
				one = cpu_s() - t0;
				if (one < *(c == 3 ? &s3 : &s6)) *(c == 3 ? &s3 : &s6) = one;
			}
		}
		printf("  cost: %.1f ns a stereo frame at cell 6, %.1f at cell 3: %.3f%% and %.3f%% of one core here\n",
		       s6 * 1e9 / RATE, s3 * 1e9 / RATE, s6 * 100.0, s3 * 100.0);
		report(s6 < 0.01, "cell_6_costs_under_1_percent_of_a_core_here", s6 * 100.0);
	}
}

int main(void)
{
	static int16_t a[RATE * 2], b[RATE * 2];
	int8_t g[EQ_BANDS] = { 0 }, back[EQ_BANDS];
	eq_state e;
	int i, k, same;
	double worst, d;

	printf("eqdsp: the five bands\n");
	/* Flat: nothing at all */
	eq_init(&e, RATE);
	tone(a, RATE, 440.0f, 0.5f);
	memcpy(b, a, sizeof a);
	eq_run(&e, b, RATE);
	report(!memcmp(a, b, sizeof a) && !eq_active(&e), "flat_touches_nothing", 0);
	eq_jump(&e, g);
	eq_run(&e, b, RATE);
	report(!memcmp(a, b, sizeof a), "all_zero_touches_nothing", 0);

	/* Each band at +6 lifts its own tone, and not one two octaves away */
	for (k = 0; k < EQ_BANDS; k++) {
		const float f = EQ_FREQ[k];
		const float far = k < 2 ? f * 4.0f : f / 4.0f;
		char name[64];

		/* A shelf's own tone is at its middle, half its gain; a tone past
		 * it gets all of it */
		d = gain_at(k, 6, k == 0 ? f / 4.0f : k == EQ_BANDS - 1 ? 18000.0f : f);
		snprintf(name, sizeof name, "band_%d_plus6_lifts_its_tone", (int)f);
		report(d > 5.0 && d < 6.6, name, d);
		d = gain_at(k, 6, far);
		snprintf(name, sizeof name, "band_%d_plus6_leaves_%d_hz", (int)f, (int)far);
		report(fabs(d) < 1.5 + (k == 0 || k == EQ_BANDS - 1 ? 1.5 : 0), name, d);
		d = gain_at(k, -6, k == 0 ? f / 4.0f : k == EQ_BANDS - 1 ? 18000.0f : f);
		snprintf(name, sizeof name, "band_%d_minus6_cuts_its_tone", (int)f);
		report(d < -5.0 && d > -6.6, name, d);
	}

	/* A step mid-tone: no jump bigger than the tone's own steepest */
	eq_init(&e, RATE);
	tone(a, RATE, 1000.0f, 0.3f);
	eq_run(&e, a, RATE / 2);
	g[2] = 6;
	eq_set(&e, g);
	eq_run(&e, a + RATE, RATE / 2);
	worst = 0;
	for (i = 1; i < RATE; i++) {
		d = fabs((double)a[i * 2] - a[(i - 1) * 2]);
		if (d > worst) worst = d;
	}
	/* 0.3 at 1 kHz moves at most 2 pi 1000 / 48000 * 0.3 * 32767 a sample,
	 * and twice that after +6 dB */
	report(worst < 2.0 * 2.0 * 3.14159 * 1000.0 / RATE * 0.3 * 32767.0 * 1.05, "a_step_has_no_click", worst);
	report(eq_active(&e) && e.steps == 0 && e.now[2] == 6.0f, "the_move_ends_in_20_ms", e.now[2]);

	/* Back to flat: at rest it stops touching the samples again */
	memset(g, 0, sizeof g);
	eq_set(&e, g);
	eq_run(&e, a, 48 * EQ_RAMP_MS + 48);
	report(!eq_active(&e), "back_to_flat_rests", 0);

	/* A boosted loud tone stays under full scale, bent rather than cut */
	eq_init(&e, RATE);
	g[0] = 6;
	eq_jump(&e, g);
	tone(a, RATE, 40.0f, 0.95f);
	eq_run(&e, a, RATE);
	k = 0;
	for (i = 0; i < RATE * 2; i++) if (a[i] == 32767 || a[i] == -32768) k++;
	report(k == 0, "the_knee_never_cuts_flat", k);

	/* The volume boost: a quiet tone made louder by the gain, a loud one held
	 * under -1 dB of full scale by the limiter, no gain touching nothing */
	{
		int16_t peak = 0;

		eq_init(&e, RATE);
		eq_gain_jump(&e, 0);
		report(!eq_active(&e), "no_gain_touches_nothing", 0);
		eq_gain_jump(&e, 12);
		tone(a, RATE, 440.0f, 0.1f);
		eq_run(&e, a, RATE);
		for (i = RATE; i < RATE * 2; i++) if (abs(a[i]) > peak) peak = (int16_t)abs(a[i]);
		report(peak > 0.1f * 32767 * 3.8f && peak < 0.1f * 32767 * 4.1f, "twelve_db_is_four_times_a_quiet_tone", peak / 3276.7);
		eq_init(&e, RATE);
		eq_gain_jump(&e, 12);
		tone(a, RATE, 440.0f, 0.9f);
		eq_run(&e, a, RATE);
		peak = 0;
		for (i = 0; i < RATE * 2; i++) if (abs(a[i]) > peak) peak = (int16_t)abs(a[i]);
		report(peak <= (int)EQ_LIM_T + 1, "the_limiter_holds_a_loud_tone_under_minus_1_db", peak / 32767.0);
		eq_init(&e, RATE);
		g[0] = 6; g[1] = 6; g[2] = 6; g[3] = 6; g[4] = 6;
		eq_jump(&e, g);
		eq_gain_jump(&e, 12);
		tone(a, RATE, 60.0f, 0.95f);
		eq_run(&e, a, RATE);
		peak = 0;
		for (i = 0; i < RATE * 2; i++) if (abs(a[i]) > peak) peak = (int16_t)abs(a[i]);
		report(peak <= (int)EQ_LIM_T + 1, "and_with_every_band_boosted_too", peak / 32767.0);
		eq_init(&e, RATE);
		eq_gain_jump(&e, 6);
		eq_gain(&e, 0);
		tone(a, RATE, 440.0f, 0.1f);
		eq_run(&e, a, RATE);
		report(!eq_active(&e), "back_to_no_gain_in_20_ms_rests", e.gain_now);
	}

	/* The shared file, and its gain */
	{
		eq_share s = { EQ_SHARE_MAGIC, 2, { 5, 2, 0, 0, 1 }, 8, { 0 } };
		int8_t gain = -1;

		same = eq_share_read(&s, back, &gain) && back[0] == 5 && back[4] == 1 && gain == 8;
		s.seq = 3;
		report(same && !eq_share_read(&s, back, NULL), "the_shared_file_reads_whole_or_not", 0);
		s.seq = 2;
		eq_share_put(&s, NULL, 4);
		report(s.seq == 4 && s.gain == 4 && s.db[0] == 5, "the_gain_written_alone_keeps_the_bands", s.gain);

		/* Your PICO-8's cut: a byte of its own, out of seq's count, so a
		 * writer of the gain and a writer of the cut never leave seq odd */
		report(!eq_share_cut_get(&s), "a_file_from_before_has_no_cut", 0);
		eq_share_cut_put(&s, 1);
		report(eq_share_cut_get(&s) && s.seq == 4 && s.gain == 4 && s.db[0] == 5,
		       "the_cut_written_touches_nothing_else", s.seq);
		s.seq = 5;
		report(eq_share_cut_get(&s), "and_reads_while_the_bands_are_being_written", 0);
		s.seq = 4;
		eq_share_put(&s, NULL, 6);
		report(eq_share_cut_get(&s), "the_gain_written_keeps_the_cut", 0);
		eq_share_cut_put(&s, 0);
		report(!eq_share_cut_get(&s), "cleared", 0);
		s.magic = 0;
		eq_share_cut_put(&s, 1);
		report(!eq_share_cut_get(&s), "none_from_a_file_that_is_not_one", 0);
	}

	/* The cut, faded: down to silence in 20 ms, never louder, and back */
	{
		static float fl[RATE], fr[RATE];
		const int ramp = RATE / 1000 * EQ_RAMP_MS;
		eq_fade f;
		int i, ok = 1;

		eq_fade_init(&f, RATE);
		report(!eq_fade_active(&f), "the_fade_starts_heard_at_rest", f.now);
		for (i = 0; i < RATE; i++) fl[i] = fr[i] = 0.5f;
		eq_fade_cut(&f, 1, 0);
		eq_fade_run(&f, fl, fr, RATE);
		for (i = 1; i < ramp && ok; i++)
			if (!(fl[i] < fl[i - 1]) || fl[i] < 0.0f) ok = 0;
		report(ok && fl[ramp - 1] == 0.0f && fl[RATE - 1] == 0.0f && fr[ramp] == 0.0f,
		       "down_to_silence_in_20_ms_falling_all_the_way", fl[ramp / 2]);
		for (i = 0; i < RATE; i++) fl[i] = fr[i] = 0.5f;
		eq_fade_cut(&f, 0, 0);
		eq_fade_run(&f, fl, fr, RATE);
		report(fl[0] < 0.01f && fl[ramp - 1] == 0.5f && fl[RATE - 1] == 0.5f && !eq_fade_active(&f),
		       "back_in_20_ms_then_at_rest_untouched", fl[ramp / 2]);
		eq_fade_cut(&f, 1, 1);
		report(f.now == 0.0f && eq_fade_active(&f), "a_jump_is_at_once", f.now);
	}

	/* What it costs: a second of stereo through five boosted bands. This thread's CPU time, the
	   least of 20 passes (cpu_s() says why). */
	{
		const int reps = 20;
		double s = 1e9, t0, one;

		eq_init(&e, RATE);
		g[0] = 5; g[1] = 2; g[2] = -2; g[3] = 3; g[4] = 1;
		eq_jump(&e, g);
		tone(a, RATE, 440.0f, 0.3f);
		for (k = 0; k < reps; k++) {
			t0 = cpu_s();
			eq_run(&e, a, RATE);
			one = cpu_s() - t0;
			if (one < s) s = one;
		}
		printf("  cost: %.3f ms for a second of 48 kHz stereo, five bands: %.3f%% of one core here\n",
		       s * 1000.0, s * 100.0);
		report(s < 0.02, "costs_under_2_percent_of_a_core_here", s * 100.0);
	}

	top_cells();

	if (fails) {
		printf("\n%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
