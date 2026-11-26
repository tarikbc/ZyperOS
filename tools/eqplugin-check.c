/* SPDX-License-Identifier: GPL-3.0-only */
/* The ports' equalizer plugin (plastron's zyperos-eq, a LADSPA plugin),
 * loaded on this computer the way alsa-lib's ladspa PCM loads it on the
 * handheld - dlopen, ladspa_descriptor, instantiate, connect, activate,
 * run - with the launcher's file stood in for by $ZYPEROS_EQ_FILE:
 *
 *     make check-eqplugin
 *
 * No file and a flat file pass the sound through untouched; Voice lifts a
 * 1 kHz tone by about 3 dB; a change to the file is followed within a
 * period and 20 ms; the file's cut (your PICO-8 quiet while MP4 plays) is
 * silence, faded, and cleared the sound as it came; and what it costs on a
 * second of 48 kHz stereo. */
#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../plastron/buildroot/package/zyperos-eq/src/ladspa.h"
#include "../src/eqdsp.h"

#define RATE   48000
#define PERIOD 512      /* the Pixel 2's dmix period */

static int fails;

static void report(int ok, const char *name, double got)
{
	printf("  %s %s (%.3f)\n", ok ? "PASS" : "FAIL", name, got);
	if (!ok) fails++;
}

static char share_file[64];

static void put_bands(int seq, const int8_t db[EQ_BANDS])
{
	eq_share s = { EQ_SHARE_MAGIC, (uint32_t)seq, { 0 }, { 0 } };
	FILE *f = fopen(share_file, "r+b");

	memcpy(s.db, db, EQ_BANDS);
	if (!f) f = fopen(share_file, "wb");
	if (!f) return;
	fwrite(&s, sizeof s, 1, f);
	fclose(f);
}

/* The cut byte, as MP4's follower writes it (eq_share_cut_put) */
static void set_cut(int cut)
{
	FILE *f = fopen(share_file, "r+b");

	if (!f) return;
	fseek(f, (long)offsetof(eq_share, cut), SEEK_SET);
	fputc(cut ? 1 : 0, f);
	fclose(f);
}

/* The volume boost's gain byte, as portkeys writes it (eq_share_put; one
 * byte, whole) */
static void set_gain(int db)
{
	FILE *f = fopen(share_file, "r+b");

	if (!f) return;
	fseek(f, (long)offsetof(eq_share, gain), SEEK_SET);
	fputc(db, f);
	fclose(f);
}

/* A second through the plugin, a period at a time */
static void run_through(const LADSPA_Descriptor *d, LADSPA_Handle h, float *il, float *ir, float *ol,
                        float *or_)
{
	int at;

	for (at = 0; at < RATE; at += PERIOD) {
		d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
		d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
		d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
	}
}

static void tone(float *l, float *r, int n, float hz, int from)
{
	int i;

	for (i = 0; i < n; i++)
		l[i] = r[i] = 0.25f * sinf(2.0f * 3.14159265f * hz * (float)(from + i) / RATE);
}

static double rms_db(const float *x, int n)
{
	double sq = 0;
	int i;

	for (i = 0; i < n; i++) sq += (double)x[i] * x[i];
	return 20.0 * log10(sqrt(sq / n) / (0.25 / sqrt(2.0)));
}

int main(int argc, char **argv)
{
	static float il[RATE], ir[RATE], ol[RATE], or_[RATE];
	const LADSPA_Descriptor *(*get)(unsigned long);
	const LADSPA_Descriptor *d;
	LADSPA_Handle h;
	const int8_t voice[EQ_BANDS] = { -2, 0, 3, 3, 0 }, flat[EQ_BANDS] = { 0 };
	void *so;
	int at, same;
	double g;

	if (argc < 2 || !(so = dlopen(argv[1], RTLD_NOW))) {
		printf("eqplugin: cannot load %s: %s\n", argc > 1 ? argv[1] : "(none)", dlerror());
		return 1;
	}
	snprintf(share_file, sizeof share_file, "/tmp/eqplugin-check-%d", (int)getpid());
	unlink(share_file);
	setenv("ZYPEROS_EQ_FILE", share_file, 1);
	printf("eqplugin: zyperos_eq as alsa-lib loads it\n");
	get = (const LADSPA_Descriptor *(*)(unsigned long))dlsym(so, "ladspa_descriptor");
	d = get ? get(0) : NULL;
	report(d && !strcmp(d->Label, "zyperos_eq") && d->PortCount == 4 && get(1) == NULL &&
	       LADSPA_IS_PORT_INPUT(d->PortDescriptors[0]) && LADSPA_IS_PORT_OUTPUT(d->PortDescriptors[3]) &&
	       !strcmp(d->PortNames[1], "Input R") && !strcmp(d->PortNames[2], "Output L"),
	       "one_stereo_plugin_labelled_zyperos_eq", 0);
	if (!d) return 1;
	h = d->instantiate(d, RATE);
	d->connect_port(h, 0, il);
	d->connect_port(h, 1, ir);
	d->connect_port(h, 2, ol);
	d->connect_port(h, 3, or_);

	/* No file: the sound as it came */
	d->activate(h);
	tone(il, ir, RATE, 1000.0f, 0);
	for (at = 0; at < RATE; at += PERIOD) {
		d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
		d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
		d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
	}
	report(!memcmp(il, ol, sizeof il) && !memcmp(ir, or_, sizeof ir), "no_file_touches_nothing", 0);
	d->deactivate(h);

	/* Flat in the file: the same */
	put_bands(2, flat);
	d->activate(h);
	for (at = 0; at < RATE; at += PERIOD) {
		d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
		d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
		d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
	}
	report(!memcmp(il, ol, sizeof il), "flat_touches_nothing", 0);

	/* Voice written while it runs: followed at the next period, its 1 kHz
	 * about +3 dB once the move is over */
	put_bands(4, voice);
	for (at = 0; at < RATE; at += PERIOD) {
		d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
		d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
		d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
	}
	g = rms_db(ol + RATE / 2, RATE / 2);
	report(g > 2.4 && g < 3.6, "voice_written_while_it_runs_lifts_1_khz_3_db", g);
	same = !memcmp(ol, or_, sizeof ol);
	report(same, "both_channels_alike_for_a_mono_tone", 0);

	/* A file half written (an odd sequence) is not taken: the bands stay */
	put_bands(5, flat);
	for (at = 0; at < RATE; at += PERIOD) {
		d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
		d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
		d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
	}
	g = rms_db(ol + RATE / 2, RATE / 2);
	report(g > 2.4, "a_file_half_written_is_not_taken", g);

	/* Your PICO-8's quiet while MP4 plays (the file's `cut`, eqdsp.h): set
	 * while it runs, silence from the next period, faded over 20 ms; cleared,
	 * the sound back, faded the same; a stream that starts cut starts
	 * silent; and flat with no cut, the sound as it came, to the bit */
	{
		static float fl[RATE], fr[RATE];
		const int ramp = RATE / 1000 * EQ_RAMP_MS;
		double before, during, tail;
		int i, peak_ok = 1, quiet_ok = 1;

		put_bands(8, flat);
		run_through(d, h, il, ir, ol, or_);
		set_cut(1);
		run_through(d, h, il, ir, fl, fr);
		before = rms_db(ol + RATE / 2, RATE / 2);
		for (i = PERIOD + ramp; i < RATE && quiet_ok; i++)
			if (fl[i] != 0.0f || fr[i] != 0.0f) quiet_ok = 0;
		report(quiet_ok, "cut_written_while_it_runs_is_silence_after_a_period_and_20_ms", before);
		for (i = 1; i < PERIOD + ramp && peak_ok; i++)
			if (fabsf(fl[i]) > fabsf(il[i]) + 1e-6f) peak_ok = 0;
		report(peak_ok && fl[12] != 0.0f && fabsf(fl[12]) < fabsf(il[12]), "faded_down_never_louder_than_it_came", 0);
		set_cut(0);
		run_through(d, h, il, ir, fl, fr);
		during = rms_db(fl + RATE / 2, RATE / 2);
		report(fabs(during) < 0.05 && !memcmp(fl + PERIOD + ramp, il + PERIOD + ramp,
		                                    (size_t)(RATE - PERIOD - ramp) * sizeof *fl),
		       "cleared_the_sound_back_as_it_came_after_the_fade", during);
		set_cut(1);
		d->deactivate(h);
		d->activate(h);
		run_through(d, h, il, ir, fl, fr);
		tail = 0;
		for (i = 0; i < RATE; i++) tail += fabsf(fl[i]) + fabsf(fr[i]);
		report(tail == 0.0, "a_stream_starting_cut_starts_silent", tail);
		set_cut(0);
		d->deactivate(h);
		d->activate(h);
		run_through(d, h, il, ir, fl, fr);
		report(!memcmp(fl, il, sizeof fl), "no_cut_flat_touches_nothing_again", 0);
	}

	/* The volume boost's gain (the file's `gain`, which portkeys writes while
	 * a port runs, src/boost.h): written while it runs, the tone 6 dB louder
	 * once its 20 ms move is over; at cell 3, the last before the top cells,
	 * 9 dB louder, the gain alone (the tone, a quarter of full scale, stays
	 * under the ceiling); at the most, cell 6, louder again through the top
	 * cells' compressor and soft clip (src/eqdsp.h's EQ_COMP_TAB), and every
	 * peak still under -1 dB of full scale; back to 0, the sound as it came,
	 * to the bit.
	 * The plugin's part of the boost over a port, your PICO-8's included
	 * (1.1.0-rc.8: "volume boost while in pico 8 is not working") */
	{
		static float fl[RATE], fr[RATE];
		const int ramp = RATE / 1000 * EQ_RAMP_MS;
		float peak = 0;
		int i;

		put_bands(12, flat);
		run_through(d, h, il, ir, fl, fr);
		set_gain(6);
		run_through(d, h, il, ir, fl, fr);
		g = rms_db(fl + RATE / 2, RATE / 2);
		report(g > 5.95 && g < 6.05, "gain_written_while_it_runs_6_db_louder", g);
		set_gain(EQ_COMP_FROM);
		run_through(d, h, il, ir, fl, fr);
		g = rms_db(fl + RATE / 2, RATE / 2);
		report(g > 8.95 && g < 9.05, "cell_3_the_gain_alone_9_db_louder", g);
		set_gain(EQ_GAIN_MAX);
		run_through(d, h, il, ir, fl, fr);
		for (i = 0; i < RATE; i++)
			if (fabsf(fl[i]) > peak) peak = fabsf(fl[i]);
		g = rms_db(fl + RATE / 2, RATE / 2);
		report(peak <= EQ_LIM_T / 32768.0f + 1e-6f && g > 12.0 && g < 15.0,
		       "the_most_gain_the_top_cells_louder_still_and_every_peak_under_minus_1_db", g);
		set_gain(0);
		run_through(d, h, il, ir, fl, fr);
		report(!memcmp(fl + PERIOD + ramp, il + PERIOD + ramp, (size_t)(RATE - PERIOD - ramp) * sizeof *fl),
		       "gain_back_to_0_the_sound_as_it_came_after_the_move", rms_db(fl + RATE / 2, RATE / 2));
	}

	/* What it costs: Voice over a second, a period at a time. Timed as this thread's own CPU
	   time, the least of 20 passes: wall time counted every moment another program held the
	   core, and with make check four at a time on a loaded Mac the mean of 20 passes on the
	   wall clock went past 2% once though the plugin was the same (2026-11-24). */
	{
		const int reps = 20;
		struct timespec t0, t1;
		double s = 1e9, one;
		int k;

		put_bands(6, voice);
		for (k = 0; k < reps; k++) {
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
			for (at = 0; at < RATE; at += PERIOD) {
				d->connect_port(h, 0, il + at); d->connect_port(h, 1, ir + at);
				d->connect_port(h, 2, ol + at); d->connect_port(h, 3, or_ + at);
				d->run(h, (unsigned long)(RATE - at < PERIOD ? RATE - at : PERIOD));
			}
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
			one = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
			if (one < s) s = one;
		}
		printf("  cost: %.3f ms for a second of 48 kHz stereo in %d-frame periods: %.3f%% of one core here\n",
		       s * 1000.0, PERIOD, s * 100.0);
		report(s < 0.02, "costs_under_2_percent_of_a_core_here", s * 100.0);
	}
	d->deactivate(h);
	d->cleanup(h);
	unlink(share_file);
	if (fails) {
		printf("\n%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
