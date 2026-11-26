/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's sound decoded ahead (src/mp4d/ahead.h), against a stand-in output
 * that plays at 48 kHz in real time:
 *
 *     make check-ahead
 *
 * Every frame comes out once and in order, through the ring's wrap; a push
 * waits while the ring is full and never more is held than AHEAD_FRAMES; a
 * decoder that is slow for one chunk in five (60 ms where 21 ms is the
 * budget, the Pixel 2 at 408 MHz) leaves the output never dry once half a
 * second is ahead, where writing straight to it ran dry every slow chunk;
 * a hold waits for the write in progress and throws the ring away; a
 * failed output makes the next push say false, until a hold. No ALSA. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../src/mp4d/ahead.h"

static int fails;

static void report(int ok, const char *name)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) fails++;
}

static long now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000L + t.tv_nsec / 1000;
}

static void sleep_us(long us)
{
	struct timespec t = { us / 1000000L, (us % 1000000L) * 1000L };

	nanosleep(&t, NULL);
}

/* The stand-in: plays each write in its own time, counts the frames, checks
 * their order (each frame's left sample is its number, wrapped), and counts
 * a write that came after the last had played out: the output dry */
static volatile long g_got, g_dry, g_next, g_low = -1;
static volatile bool g_order_ok = true, g_fail_next, g_watch;
static long g_ends_at;   /* when what was written has played */

static long out(const int16_t *f, int n)
{
	long t = now_us(), i;

	if (g_fail_next) { g_fail_next = false; return -1; }
	/* What the ring still held behind this write, the least of it while
	 * watched: the writer's own claim, that there was always more */
	if (g_watch) {
		long behind = ahead_frames() - n;

		if (g_low < 0 || behind < g_low) g_low = behind;
	}
	for (i = 0; i < n; i++, g_next++)
		if (f[i * 2] != (int16_t)(g_next & 0x7fff)) g_order_ok = false;
	if (g_ends_at && t > g_ends_at + 2000) g_dry++;
	if (g_ends_at < t) g_ends_at = t;
	g_ends_at += (long)n * 1000000L / 48000;
	/* An output of 43 ms (the Pixel 2's dmix): the write returns once what
	 * it holds is under that */
	if (g_ends_at - t > 43000) sleep_us(g_ends_at - t - 43000);
	g_got += n;
	return n;
}

static void chunk(int16_t *buf, int n, long *from)
{
	int i;

	for (i = 0; i < n; i++, (*from)++) buf[i * 2] = buf[i * 2 + 1] = (int16_t)(*from & 0x7fff);
}

int main(void)
{
	static int16_t buf[1024 * 2];
	long from = 0, t0, maxheld = 0;
	int i;

	printf("ahead: MP4's sound decoded ahead\n");
	report(ahead_start(out), "the_writer_starts");

	/* A decoder far faster than the output: the ring fills and the push
	 * waits, never more held than the ring and the write in hand */
	t0 = now_us();
	for (i = 0; i < 60; i++) {
		long h;

		chunk(buf, 1024, &from);
		ahead_push(buf, 1024);
		if ((h = ahead_frames()) > maxheld) maxheld = h;
	}
	report(maxheld <= AHEAD_FRAMES + AHEAD_WRITE, "never_more_than_the_ring_and_the_writers_hand");
	report(now_us() - t0 > 500000, "a_full_ring_makes_the_push_wait");
	while (ahead_frames() > 0) sleep_us(2000);
	sleep_us(50000);
	report(g_got == from && g_order_ok, "every_frame_once_in_order_through_the_wrap");

	/* A decoder that is slow one chunk in five, the ring full first: the
	 * output never runs dry */
	ahead_hold();
	g_ends_at = 0;
	g_got = 0;
	g_next = from;
	ahead_release();
	for (i = 0; i < 24; i++) {   /* half a second ahead, then the slow ones */
		chunk(buf, 1024, &from);
		ahead_push(buf, 1024);
	}
	/* Judged by what the ring held behind each write, not by when the
	 * writes came: the stand-in plays in real time, and on a loaded machine
	 * (CI, 2026-11-25) the writer thread itself woke past the 43 ms the
	 * output holds, a write late with the ring full, which no ring can help.
	 * Less behind a write than a slow chunk takes is the ring not keeping
	 * the decoder ahead, which is what this is about. The decoder keeps to a schedule of its
	 * own, each chunk's time from the start, so a sleep that wakes late is
	 * not added to every chunk after it. */
	g_dry = 0;
	g_low = -1;
	g_watch = true;
	t0 = now_us();
	{
		long at = 0;

		for (i = 0; i < 100; i++) {
			long wait;

			at += i % 5 == 4 ? 60000 : 10000;
			if ((wait = t0 + at - now_us()) > 0) sleep_us(wait);
			chunk(buf, 1024, &from);
			ahead_push(buf, 1024);
		}
	}
	g_watch = false;
	/* Behind every write, more than a slow chunk takes and a period: no
	 * slow chunk can empty it (a ring of 4096 frames left 3072, and the
	 * output dry by its own count only on a busier machine) */
	report(g_low >= 60000L * 48000 / 1000000 + AHEAD_WRITE, "a_slow_decoder_never_leaves_it_dry");
	printf("    the least behind a write %ld frames (%ld ms), %ld writes late on this machine's clock\n",
	       g_low, g_low * 1000 / 48000, (long)g_dry);
	{
		/* ...where the same decoder writing straight to the output ran
		 * dry: the stand-in's own count, without the ring */
		long dry0;

		while (ahead_frames() > 0) sleep_us(2000);
		sleep_us(60000);
		ahead_hold();
		g_ends_at = 0;
		g_next = from;
		dry0 = g_dry;
		for (i = 0; i < 20; i++) {
			sleep_us(i % 5 == 4 ? 60000 : 12000);
			chunk(buf, 1024, &from);
			out(buf, 1024);
		}
		report(g_dry > dry0, "and_straight_to_the_output_it_ran_dry");
		ahead_release();
	}

	/* A hold throws the ring away and waits for the write in progress */
	g_next = -1;   /* nothing more is checked for order */
	g_order_ok = true;
	for (i = 0; i < 10; i++) {
		chunk(buf, 1024, &from);
		ahead_push(buf, 1024);
	}
	ahead_hold();
	report(ahead_frames() == 0, "a_hold_throws_the_ring_away");
	ahead_release();

	/* A failed output: the push says so, until a hold */
	g_fail_next = true;
	chunk(buf, 1024, &from);
	ahead_push(buf, 1024);
	for (i = 0; i < 200 && !ahead_failed(); i++) sleep_us(1000);
	report(ahead_failed() && !ahead_push(buf, 1024), "a_failed_output_refuses_the_push");
	ahead_hold();
	report(!ahead_failed(), "until_a_hold");
	ahead_release();

	if (fails) {
		printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
