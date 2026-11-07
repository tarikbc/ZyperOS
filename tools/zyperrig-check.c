/* Zyper's moves beside the ZyperOS menu's heading: what src/zyperrig.c
 * answers, without a screen.
 *
 * The menu draws whatever pose it is handed, sixty times a second, and a
 * pose that is wrong for a few frames is a twitch nobody can point at on the
 * panel. So the motion is held here instead, at the moments it is owed
 * (DESIGN.md 8.3, and the Zyper lab I picked it in): still at rest but
 * for a 2-unit breath every 2.4 s and a blink every 3 to 6 s, shut for a
 * moment and open again in 134 ms; a hop 80 ms after the menu opens, up 24
 * and back in 280 ms with its squash at the landing; a hop and a pant for
 * something that worked; the tongue in and the head cocked to 18 for
 * something that failed, held, and back 860 ms on; a reaction that starts
 * from wherever he is rather than jumping; a press that drops what was
 * booked and starts him back at once; a move that does nothing; and all of
 * it across the clock's wrap.
 *
 * Links src/zyperrig.c and NOT SDL. */
#include "../src/design.h"
#include "../src/zyperrig.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The times, by their tokens (DESIGN.md 8.2) */
#define OPEN  DS_MS_HOLD_ZYPER_OPEN      /* 80: the menu's first frame to his hop */
#define HOLD  DS_MS_HOLD_ZYPER_FAIL      /* 860: a failure's cocked head */
#define PANT  DS_MS_ZYPER_PANT           /* 1000: three beats */
#define BEAT  (DS_MS_ZYPER_PANT / 3)
#define BLINK DS_MS_ZYPER_BLINK          /* 134 */
#define GAP   DS_MS_ZYPER_BLINK_GAP      /* 3000, to twice it */

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int near(float a, float b, float eps)
{
	return fabsf(a - b) <= eps;
}

/* The breath alone at `t`, as DESIGN.md 8.3 has it: 0 at the top of a loop,
 * 2 units up at its middle. */
static float breath(unsigned t)
{
	return -2.0f * (0.5f - 0.5f * cosf(2.0f * (float)M_PI *
	                                   (float)(t % DS_MS_ZYPER_BREATHE) /
	                                   DS_MS_ZYPER_BREATHE));
}

/* A rig started at `t0` and ticked at `t`, as the menu ticks it before it
 * asks. */
static zr_pose at(zr_rig *z, unsigned t)
{
	zr_tick(z, t);
	return zr_pose_at(z, t);
}

static int resting(zr_pose p, unsigned t)
{
	return near(p.tilt, 10.0f, 0.01f) && near(p.tongue, 1.0f, 0.001f) &&
	       near(p.ears, 0.0f, 0.001f) && near(p.squash, 1.0f, 0.0001f) &&
	       near(p.dy, breath(t), 0.01f);
}

/* No blink before a time this far on: the first is 1.2 s at the least. */
#define QUIET 1100

static void rest(unsigned t0)
{
	zr_rig z;
	zr_pose p;
	unsigned t;
	int ok = 1;

	zr_init(&z, t0, 7);
	for (t = t0; t - t0 < QUIET; t += 7) {
		p = at(&z, t);
		if (!resting(p, t) || p.blink != 0.0f) ok = 0;
	}
	ck(ok, "at rest he only breathes: head 10, tongue out, ears still, no squash");
	p = zr_pose_at(&z, t0 - t0 % DS_MS_ZYPER_BREATHE + DS_MS_ZYPER_BREATHE / 2);
	ck(near(p.dy, -2.0f, 0.01f), "the breath is 2 units up at the middle of its loop");
	ck(near(zr_pose_at(&z, t0 + 300).dy, zr_pose_at(&z, t0 + 300 + DS_MS_ZYPER_BREATHE).dy,
	        0.01f), "the breath is a loop of DS_MS_ZYPER_BREATHE");
}

/* Every blink over a minute: when each began and how it went. */
static void blinks(unsigned t0, unsigned seed)
{
	zr_rig z;
	unsigned t, last = 0, gap_lo = ~0u, gap_hi = 0;
	int n = 0, first_ok = 1, shape_ok = 1, was = 0;

	zr_init(&z, t0, seed);
	for (t = t0; t - t0 < 60000; t++) {
		zr_pose p = at(&z, t);
		int on = p.blink > 0.0f;

		if (on && !was) {
			unsigned since = t - t0;

			if (n == 0) {
				if (since < 1200 || since > 3701) first_ok = 0;
			} else {
				unsigned gap = t - last;

				if (gap < gap_lo) gap_lo = gap;
				if (gap > gap_hi) gap_hi = gap;
			}
			/* shut by 3/8 of it (50 ms), held to half (67), open again by
			 * its end (134); `t` is a millisecond after it began */
			if (!near(zr_pose_at(&z, t - 1 + BLINK * 3 / 8).blink, 1.0f, 0.0001f) ||
			    !near(zr_pose_at(&z, t - 1 + BLINK / 2 - 1).blink, 1.0f, 0.0001f) ||
			    zr_pose_at(&z, t - 1 + BLINK / 2 + 1).blink >= 1.0f ||
			    zr_pose_at(&z, t - 2 + BLINK).blink > 0.05f ||
			    zr_pose_at(&z, t - 1 + BLINK).blink != 0.0f)
				shape_ok = 0;
			last = t;
			n++;
		}
		if (p.blink < 0.0f || p.blink > 1.0f) shape_ok = 0;
		/* only the eyes move */
		if (p.blink > 0.0f && !resting(p, t)) shape_ok = 0;
		was = on;
	}
	ck(n >= 10 && n <= 20, "a minute at rest is 10 to 20 blinks");
	ck(first_ok, "the first blink 1.2 to 3.7 s in");
	ck(gap_lo >= GAP && gap_hi <= 2 * GAP, "and then one every 3 to 6 s");
	ck(shape_ok, "a blink shuts in 50 ms, holds to 67 and is open by 134, and moves only the eyes");
}

/* The first blink over many seeds: the window is the lab's whole 1.2 to
 * 3.7 s, no earlier and no later, and a blink ticked once, late, inside its
 * time - as a --zyper still ticks it - is where it would be had every
 * millisecond been drawn. */
static void first_blinks(unsigned t0)
{
	unsigned seed, lo = ~0u, hi = 0;
	int late_ok = 1;

	for (seed = 0; seed < 1000; seed++) {
		zr_rig z, once;
		unsigned t;

		zr_init(&z, t0, seed);
		for (t = t0; t - t0 < 4000 && !(at(&z, t).blink > 0.0f); t++) ;
		/* the first millisecond with the eyes moving, one after it began */
		if (t - t0 - 1 < lo) lo = t - t0 - 1;
		if (t - t0 - 1 > hi) hi = t - t0 - 1;
		zr_init(&once, t0, seed);
		if (!near(at(&once, t - 1 + BLINK / 2 - 8).blink, 1.0f, 0.0001f))
			late_ok = 0;
	}
	ck(lo >= 1200 && lo < 1210 && hi <= 3700 && hi > 3690,
	   "over 1000 seeds the first blink falls anywhere 1.2 to 3.7 s in, and nowhere else");
	ck(late_ok, "a blink is on its own time: ticked once, late, inside it, the eyes are shut");
}

static void opening(unsigned t0)
{
	zr_rig z;
	zr_pose p;
	unsigned t;
	float top = 0.0f, sq = 1.0f;
	int early = 0, after = 1;

	zr_init(&z, t0 - 5000, 3);
	zr_open(&z, t0);
	for (t = t0; t - t0 < OPEN; t++)
		if (!resting(at(&z, t), t)) early = 1;
	ck(!early, "opening, he holds still for 80 ms while the panel arrives");
	for (t = t0 + OPEN; t - t0 < OPEN + DS_MS_ZYPER_HOP; t++) {
		p = at(&z, t);
		if (p.dy - breath(t) < top) top = p.dy - breath(t);
		if (p.squash < sq) sq = p.squash;
	}
	ck(near(top, -24.0f, 0.05f), "the opening hop goes 24 units up");
	/* A hop at the top of a breath is the highest he goes, and ZR_REACH,
	 * which the menu places him by, is that */
	{
		zr_rig h;
		/* the hop's top, 100 ms in, on the breath's, 1200 into its loop */
		const unsigned top = DS_MS_ZYPER_HOP * 36 / 100, B = DS_MS_ZYPER_BREATHE;
		unsigned when = t0 + (B / 2 + B - (t0 + top) % B) % B;
		float hi = 0.0f;

		zr_init(&h, t0, 3);
		zr_tick(&h, when);
		zr_works(&h, when);
		for (t = when; t - when < DS_MS_ZYPER_HOP; t++)
			if (zr_pose_at(&h, t).dy < hi) hi = zr_pose_at(&h, t).dy;
		ck(hi >= -ZR_REACH - 0.001f && hi < -ZR_REACH + 0.1f,
		   "a hop at the top of a breath reaches ZR_REACH and no further");
	}
	ck(near(sq, 0.94f, 0.001f), "and lands with a 6% squash");
	p = zr_pose_at(&z, t0 + OPEN + DS_MS_ZYPER_HOP * 36 / 100);
	ck(near(p.squash, 1.0f, 0.0001f), "no squash in the air");
	p = zr_pose_at(&z, t0 + OPEN + DS_MS_ZYPER_HOP * 86 / 100);
	ck(p.dy - breath(t0 + OPEN + DS_MS_ZYPER_HOP * 86 / 100) > -0.001f && p.squash < 0.95f,
	   "the squash is on the ground, at the end of the hop");
	for (t = t0 + OPEN + DS_MS_ZYPER_HOP; t - t0 < 1100; t++)
		if (!resting(at(&z, t), t)) after = 0;
	ck(after, "after the hop he is at rest again");

	/* Ticked once, late, as a slow frame or a --zyper still ticks it: the
	 * hop is where it would be had every millisecond been drawn */
	{
		zr_rig once, each;
		unsigned u = t0 + OPEN + DS_MS_ZYPER_HOP * 36 / 100;

		zr_init(&once, t0 - 5000, 3);
		zr_init(&each, t0 - 5000, 3);
		zr_open(&once, t0);
		zr_open(&each, t0);
		for (t = t0; t != u; t++) zr_tick(&each, t);
		ck(near(at(&once, u).dy, at(&each, u).dy, 0.001f) &&
		   at(&once, u).dy - breath(u) < -23.0f,
		   "a hop booked for 80 ms on is at its top at 80 + 100 ms however late the tick");
	}
}

static void works(unsigned t0)
{
	zr_rig z;
	zr_pose p;

	zr_init(&z, t0 - 5000, 3);
	zr_tick(&z, t0);
	zr_press(&z, t0);
	zr_works(&z, t0);
	p = at(&z, t0 + DS_MS_ZYPER_HOP * 36 / 100);
	ck(near(p.dy - breath(t0 + DS_MS_ZYPER_HOP * 36 / 100), -24.0f, 0.5f),
	   "a success is a hop, at once");
	p = at(&z, t0 + BEAT / 2);
	ck(near(p.tongue, 0.82f, 0.01f), "and a pant: the tongue 18% in at the middle of a beat");
	p = at(&z, t0 + BEAT);
	ck(near(p.tongue, 1.0f, 0.01f), "and out again at its end, three beats a second");
	ck(near(p.tilt, 10.0f, 0.001f), "a success leaves his head as it was");
	p = at(&z, t0 + PANT);
	ck(resting(p, t0 + PANT), "the pant is over in a second");

	/* A held Left or Right changes a setting every 90 ms: each success
	 * starts nothing over, so nothing jumps */
	{
		zr_pose b;
		unsigned t;
		int smooth = 1;
		float last = 0.0f, tlast = 1.0f;

		zr_init(&z, t0 - 5000, 3);
		zr_tick(&z, t0);
		for (t = t0; t - t0 < 2400; t++) {
			zr_tick(&z, t);
			if ((t - t0) % 90 == 0 && t - t0 <= 450) {
				p = zr_pose_at(&z, t);
				zr_press(&z, t);
				zr_works(&z, t);
				b = zr_pose_at(&z, t);
				if (!near(p.dy, b.dy, 0.001f) || !near(p.tongue, b.tongue, 0.001f) ||
				    !near(p.squash, b.squash, 0.0001f))
					smooth = 0;
			}
			b = zr_pose_at(&z, t);
			if (t != t0 && (fabsf(b.dy - last) > 0.6f || fabsf(b.tongue - tlast) > 0.01f))
				smooth = 0;
			last = b.dy;
			tlast = b.tongue;
		}
		ck(smooth, "a success during a hop or a pant lets it run on: nothing jumps");
		/* the hop under way at 90, 180 and 270 ran on, and the success at
		 * 360, on the ground again, began another */
		p = zr_pose_at(&z, t0 + 300);
		ck(near(p.dy, breath(t0 + 300), 0.01f), "the first hop ends at 280 as it would have");
		p = zr_pose_at(&z, t0 + 360 + 100);
		ck(p.dy - breath(t0 + 360 + 100) < -23.0f, "a success after a hop hops again");
		/* the pant goes on a second past the last success, at 450, to the
		 * end of the beat it is in then: the fifth, 1667 ms from its start */
		p = zr_pose_at(&z, t0 + 4 * BEAT + BEAT / 2);
		ck(near(p.tongue, 0.82f, 0.01f), "the pant goes on a second past the last success");
		p = zr_pose_at(&z, t0 + 5 * BEAT + 2);
		ck(near(p.tongue, 1.0f, 0.0001f), "and ends on a whole beat");
	}
}

static void failing(unsigned t0)
{
	zr_rig z;
	zr_pose p;
	unsigned t;
	float hi = 0.0f;
	int held = 1, back = 1;

	zr_init(&z, t0 - 5000, 3);
	zr_tick(&z, t0);
	zr_press(&z, t0);
	zr_fails(&z, t0);
	for (t = t0; t - t0 <= DS_MS_ZYPER_TILT; t++) {
		p = at(&z, t);
		if (p.tilt > hi) hi = p.tilt;
	}
	ck(hi > 18.2f && hi < 20.5f, "a failure cocks his head past 18 by a small overshoot");
	p = at(&z, t0 + DS_MS_ZYPER_TILT);
	ck(near(p.tilt, 18.0f, 0.001f) && near(p.tongue, 0.0f, 0.001f),
	   "and settles on 18 with the tongue in, in DS_MS_ZYPER_TILT");
	for (t = t0 + DS_MS_ZYPER_TILT; t - t0 < HOLD; t++) {
		p = at(&z, t);
		if (!near(p.tilt, 18.0f, 0.001f) || !near(p.tongue, 0.0f, 0.001f)) held = 0;
	}
	ck(held, "and holds there to 860 ms");
	p = at(&z, t0 + HOLD + DS_MS_ZYPER_TILT / 2);
	ck(p.tilt < 18.0f && p.tilt > 10.0f && p.tongue > 0.0f && p.tongue < 1.0f,
	   "then he comes back by himself");
	for (t = t0 + HOLD + DS_MS_ZYPER_TILT; t - t0 < 1100; t++)
		if (!resting(at(&z, t), t)) back = 0;
	ck(back, "and is at rest DS_MS_ZYPER_TILT later");

	/* On his way back nothing is booked, so a press then is a move like any
	 * other: it does not start the way back over */
	{
		zr_rig pressed, left;
		int same = 1;

		zr_init(&pressed, t0 - 5000, 3);
		zr_init(&left, t0 - 5000, 3);
		zr_tick(&pressed, t0);
		zr_tick(&left, t0);
		zr_fails(&pressed, t0);
		zr_fails(&left, t0);
		for (t = t0; t - t0 < 1200; t++) {
			zr_pose a = at(&pressed, t), b = at(&left, t);

			if (t - t0 == 900) zr_press(&pressed, t);
			if (memcmp(&a, &b, sizeof a)) same = 0;
		}
		ck(same, "a press on his way back from a failure changes nothing");
	}
}

/* A reaction starts from wherever he is: nothing jumps in the frame it is
 * sent, whatever was under way. */
static void retarget(unsigned t0)
{
	unsigned when[] = { 40, 120, 500, 900, 960 };
	int ok = 1;
	size_t i;

	for (i = 0; i < sizeof when / sizeof *when; i++) {
		zr_rig z;
		zr_pose a, b;
		unsigned t = t0 + when[i];

		zr_init(&z, t0 - 5000, 3);
		zr_tick(&z, t0);
		zr_fails(&z, t0);
		a = at(&z, t);
		zr_press(&z, t);
		zr_fails(&z, t);
		b = zr_pose_at(&z, t);
		if (!near(a.tilt, b.tilt, 0.001f) || !near(a.tongue, b.tongue, 0.001f)) ok = 0;
		/* and goes on to the new target from there */
		b = at(&z, t + DS_MS_ZYPER_TILT);
		if (!near(b.tilt, 18.0f, 0.001f) || !near(b.tongue, 0.0f, 0.001f)) ok = 0;
	}
	ck(ok, "a failure during a failure, or during its way back, starts from where he is");
}

/* A press drops what was booked and starts him back now, from where he is. */
static void press(unsigned t0)
{
	zr_rig z, still;
	zr_pose a, b;
	unsigned t;
	int moved = 0, same = 1;

	zr_init(&z, t0 - 5000, 3);
	zr_tick(&z, t0);
	zr_press(&z, t0);
	zr_fails(&z, t0);
	a = at(&z, t0 + 300);
	zr_press(&z, t0 + 300);
	b = zr_pose_at(&z, t0 + 300);
	ck(near(a.tilt, b.tilt, 0.001f), "a press during a failure starts him back from where he is");
	b = at(&z, t0 + 300 + DS_MS_ZYPER_TILT);
	ck(resting(b, t0 + 300 + DS_MS_ZYPER_TILT),
	   "and he is at rest DS_MS_ZYPER_TILT after the press, not 860 ms after the failure");

	/* The hop booked by the opening: a move inside its 80 ms drops it */
	zr_init(&z, t0 - 5000, 3);
	zr_open(&z, t0);
	zr_tick(&z, t0 + 40);
	zr_press(&z, t0 + 40);
	for (t = t0 + 40; t - t0 < 600; t++)
		if (!resting(at(&z, t), t)) moved = 1;
	ck(!moved, "a press before the opening hop drops it");

	/* With nothing booked a press is nothing */
	zr_init(&z, t0 - 5000, 3);
	zr_init(&still, t0 - 5000, 3);
	zr_tick(&z, t0);
	zr_tick(&still, t0);
	zr_press(&z, t0);
	for (t = t0; t - t0 < 3000; t += 3) {
		a = at(&z, t);
		b = at(&still, t);
		if (memcmp(&a, &b, sizeof a)) same = 0;
	}
	ck(same, "a move with nothing booked changes nothing");

	/* and a press during a success does not cut the hop or the pant short */
	zr_init(&z, t0 - 5000, 3);
	zr_init(&still, t0 - 5000, 3);
	zr_tick(&z, t0);
	zr_tick(&still, t0);
	zr_works(&z, t0);
	zr_works(&still, t0);
	zr_press(&z, t0 + 100);
	a = at(&z, t0 + 200);
	b = at(&still, t0 + 200);
	ck(!memcmp(&a, &b, sizeof a), "a move during a hop lets it finish");
}

int main(void)
{
	/* Twice: from a clock near its start, and across its wrap */
	const unsigned starts[] = { 10000u, 0xFFFFF000u };
	size_t i;

	for (i = 0; i < sizeof starts / sizeof *starts; i++) {
		printf("from %u\n", starts[i]);
		rest(starts[i]);
		blinks(starts[i], 1);
		blinks(starts[i], 0xC0FFEEu);
		first_blinks(starts[i]);
		opening(starts[i]);
		works(starts[i]);
		failing(starts[i]);
		retarget(starts[i]);
		press(starts[i]);
	}
	if (fails) {
		printf("zyperrig: %d FAILED\n", fails);
		return 1;
	}
	printf("ok: Zyper's moves keep their times\n");
	return 0;
}
