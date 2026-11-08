/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-px2-perf: px2-perf's arithmetic and parsing (tools/px2-perf.h)
 * on a computer. The percentiles as a spreadsheet gives them, the frame-time
 * spread from the times new frames were seen, the CPU ticks from a
 * /proc/PID/stat line whose name has spaces and parentheses in it, and the
 * speed against the clock from two of Diatom's now files, refused for two
 * writings of different games or of a game that did not run between. */
#include <math.h>
#include <stdio.h>

#include "px2-perf.h"

static int fails;

static void near(const char *what, double got, double want)
{
	int ok = fabs(got - want) < 1e-6;

	printf("  %s %s", ok ? "ok  " : "FAIL", what);
	if (!ok) printf(": %.6f, not %.6f", got, want);
	printf("\n");
	fails += !ok;
}

static void yes(const char *what, int ok)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(void)
{
	const double one[] = { 7.0 };
	const double four[] = { 1.0, 2.0, 3.0, 4.0 };
	const double ten[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	struct perf_frames f;
	unsigned long long ticks = 0;

	/* Percentiles: interpolated between the nearest ranks */
	near("the median of an even count is the mean of the middle two", perf_percentile(four, 4, 50), 2.5);
	near("the 0th is the least", perf_percentile(four, 4, 0), 1.0);
	near("the 100th is the most", perf_percentile(four, 4, 100), 4.0);
	near("the 95th of 1..10 is 9.55, between the 9th and the 10th", perf_percentile(ten, 10, 95), 9.55);
	near("the 25th of 1..4 is 1.75", perf_percentile(four, 4, 25), 1.75);
	near("one value is every percentile", perf_percentile(one, 1, 95), 7.0);
	near("no values are 0", perf_percentile(one, 0, 50), 0.0);

	/* A 60 Hz panel showing every frame but two that stayed up two blanks
	 * and one three: 20 frames, 19 frame times */
	{
		uint64_t t[20], at = 1000000;
		int i;

		for (i = 0; i < 20; i++) {
			t[i] = at;
			at += i == 4 || i == 11 ? 33333 : i == 16 ? 50000 : 16667;
		}
		perf_frame_stats(t, 20, 0.5, &f);
		yes("20 frames give 19 frame times", f.frames == 20 && f.times == 19);
		near("40 fps over half a second", f.fps, 40.0);
		near("the least frame time is a blank's", f.min_ms, 16.667);
		near("the median is a blank's", f.median_ms, 16.667);
		near("the 95th falls between the two-blank frame and the three-blank one",
		     f.p95_ms, 33.333 + 0.1 * (50.0 - 33.333));
		near("the most is three blanks'", f.max_ms, 50.0);
	}
	perf_frame_stats(NULL, 0, 10.0, &f);
	yes("no frames: 0 fps and no spread", f.frames == 0 && f.fps == 0.0 && f.times == 0);
	{
		uint64_t t1[] = { 5 };

		perf_frame_stats(t1, 1, 2.0, &f);
		yes("one frame: half a frame a second, no frame times", f.fps == 0.5 && f.times == 0);
	}

	/* /proc/PID/stat: utime and stime counted from the last ')' */
	yes("utime + stime of a plain name",
	    perf_stat_ticks("111 (diatom) S 100 111 100 0 -1 4194560 21053 0 12 0 4521 377 0 0 20 0 9 0 300 1",
	                    &ticks) && ticks == 4898);
	yes("and of a name with spaces and parentheses",
	    perf_stat_ticks("42 (a (b) c) R 1 42 42 0 -1 0 0 0 0 0 10 5 0 0 20 0 1 0 9 1", &ticks) &&
	    ticks == 15);
	yes("a line cut short does not read", !perf_stat_ticks("42 (x) R 1 2 3", &ticks));
	yes("nor one with no name", !perf_stat_ticks("garbage", &ticks));

	/* Diatom's now file */
	{
		const char *a = "pid=111\ncore=/c/fceumm_libretro.so\nrom=/r/NES/A Game.nes\nfps=60.0998\n"
		                "seq=4\nt_us=100\nframes=1000\nrun_us=16639000\nspeed=1\ndropped=12\n";
		const char *b = "pid=111\ncore=/c/fceumm_libretro.so\nrom=/r/NES/A Game.nes\nfps=60.0998\n"
		                "seq=44\nt_us=200\nframes=1450\nrun_us=26639000\nspeed=1\ndropped=20\n";
		const char *other = "pid=111\ncore=/c/x.so\nrom=/r/NES/Another.nes\nfps=60.0998\n"
		                    "seq=2\nt_us=200\nframes=1450\nrun_us=26639000\nspeed=1\ndropped=20\n";
		const char *restarted = "pid=222\ncore=/c/fceumm_libretro.so\nrom=/r/NES/A Game.nes\nfps=60.0998\n"
		                        "seq=44\nt_us=200\nframes=1450\nrun_us=26639000\nspeed=1\ndropped=20\n";
		char v[64];
		double dropped = -1;

		yes("a key's value, spaces and all",
		    perf_kv(a, "rom", v, sizeof v) && !strcmp(v, "/r/NES/A Game.nes"));
		yes("a key that is a prefix of another is not it", !perf_kv(a, "ro", v, sizeof v));
		yes("an absent key is absent", !perf_kv(a, "nothing", v, sizeof v));
		near("450 frames of a 60.0998 fps game in 10 s: 0.749x of the clock",
		     perf_speed(a, b, &dropped), 450 * 1e6 / 60.0998 / 10e6);
		near("and the 8 audio frames dropped in between", dropped, 8.0);
		near("another game between the two says nothing", perf_speed(a, other, NULL), 0.0);
		near("nor a Diatom started again", perf_speed(a, restarted, NULL), 0.0);
		near("nor the same writing twice", perf_speed(a, a, NULL), 0.0);
	}

	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("ok: px2-perf's percentiles, frame times, CPU ticks and speed against the clock\n");
	return 0;
}
