/* The menus' focus plate and the give of their list, without a screen: a
 * move of src/tween.c in DESIGN.md 8's **quick** on its **out** curve.
 *
 * Two claims, and both are about a held d-pad, which is where the plate went
 * wrong before. A press that comes while the plate is still moving takes it
 * on from where it is, and not from where it started or where it was going:
 * either of those is a jump on screen. And at the d-pad's repeat the plate is
 * on its row before the next press comes, within 1% of a row: a slide that is
 * still on its way at the next press trails the cursor, and the row before a
 * wrap, which snaps, is never seen lit.
 *
 * The repeat is src/keyrepeat.h's REPEAT_RATE_MS, read from there by the
 * Makefile, so a change to it is a change this sees.
 *
 * Links src/tween.c alone, and NOT SDL.
 */
#include "../src/tween.h"
#include "../src/design.h"

#include <math.h>
#include <stdio.h>

#ifndef REPEAT_RATE_MS
#error "the Makefile passes src/keyrepeat.h's REPEAT_RATE_MS"
#endif

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

int main(void)
{
	const float ROW = 74.0f;      /* a menu row, in units; any size will do */
	tween tw = { 0 };
	char what[160];
	float before, after, y;
	unsigned t;
	int i, ok;

	/* At rest where it is put, and there at once. */
	tween_snap(&tw, 3 * ROW);
	ck(tween_at(&tw, 0) == 3 * ROW && tween_at(&tw, 123456u) == 3 * ROW,
	   "a snapped plate is where it was put, at any time");
	ck(!tween_moving(&tw, 0), "and is not moving");

	/* A move: from where it starts, on the out curve, there at **quick**. */
	tween_snap(&tw, 0);
	tween_to(&tw, ROW, DS_MS_QUICK, 1000);
	ck(tween_at(&tw, 1000) == 0.0f, "a move starts where the plate is");
	ck(near(tween_at(&tw, 1000 + DS_MS_QUICK / 2), ROW * ds_out(0.5f), 0.01f),
	   "and runs on the out curve");
	ck(tween_at(&tw, 1000 + DS_MS_QUICK) == ROW && !tween_moving(&tw, 1000 + DS_MS_QUICK),
	   "and is on its row at quick, exactly, and still");
	ck(tween_moving(&tw, 1000 + DS_MS_QUICK - 1), "and not a millisecond before");
	ok = 1;
	for (t = 1000; t <= 1000 + DS_MS_QUICK; t++) {
		y = tween_at(&tw, t);
		if (y < 0.0f || y > ROW) ok = 0;
	}
	ck(ok, "never past either end: nothing in the interface overshoots");

	/* The same target again, as every frame asks: nothing starts over. */
	tween_snap(&tw, 0);
	tween_to(&tw, ROW, DS_MS_QUICK, 2000);
	tween_to(&tw, ROW, DS_MS_QUICK, 2000 + 60);
	ck(tween_at(&tw, 2000 + DS_MS_QUICK) == ROW,
	   "asked for the same row on a later frame, it lands when it would have");

	/* A press mid-move: on from where it is, with no jump. */
	tween_snap(&tw, 0);
	tween_to(&tw, ROW, DS_MS_QUICK, 3000);
	before = tween_at(&tw, 3000 + 40);
	tween_to(&tw, 2 * ROW, DS_MS_QUICK, 3000 + 40);
	after = tween_at(&tw, 3000 + 40);
	snprintf(what, sizeof what, "a press 40 ms into a move goes on from where "
	         "the plate is (it was at %.2f and went on from %.2f)", before, after);
	ck(after == before, what);
	ck(tween_at(&tw, 3000 + 40 + DS_MS_QUICK) == 2 * ROW,
	   "and lands on the new row quick after the press");
	ck(tween_at(&tw, 3000 + 41) > before, "and keeps going the way it was going");

	/* Back the other way mid-move, the same. */
	before = tween_at(&tw, 3000 + 70);
	tween_to(&tw, 0, DS_MS_QUICK, 3000 + 70);
	ck(tween_at(&tw, 3000 + 70) == before, "a press back the other way mid-move "
	   "turns it where it is");

	/* A held d-pad: a press every REPEAT_RATE_MS, each a row on. Before every
	 * one, the plate is within 1% of a row of the row it was sent to. */
	tween_snap(&tw, 0);
	ok = 1;
	for (i = 1, t = 5000; i <= 40; i++, t += REPEAT_RATE_MS) {
		float off;

		tween_to(&tw, i * ROW, DS_MS_QUICK, t);
		off = fabsf(tween_at(&tw, t + REPEAT_RATE_MS - 1) - i * ROW);
		if (off > 0.01f * ROW && ok) {
			snprintf(what, sizeof what, "held, the plate is within 1%% of its row "
			         "before the next repeat (press %d: %.1f%% of a row short)",
			         i, 100.0f * off / ROW);
			ck(0, what);
			ok = 0;
		}
	}
	snprintf(what, sizeof what, "quick on the out curve lands inside the %d ms "
	         "repeat (ds_out(%d/%d) = %.4f)", REPEAT_RATE_MS, REPEAT_RATE_MS,
	         DS_MS_QUICK, ds_out((float)REPEAT_RATE_MS / DS_MS_QUICK));
	ck(1.0f - ds_out((float)REPEAT_RATE_MS / DS_MS_QUICK) < 0.01f, what);

	/* The list's give: put back a row on a scroll, from where it is, and on
	 * to rest. Two in a row add up rather than starting over. */
	tween_snap(&tw, 0);
	tween_kick(&tw, ROW, DS_MS_QUICK, 7000);
	ck(tween_at(&tw, 7000) == ROW, "a give starts a whole row back, where the rows were");
	before = tween_at(&tw, 7000 + 30);
	tween_kick(&tw, ROW, DS_MS_QUICK, 7000 + 30);
	ck(near(tween_at(&tw, 7000 + 30), before + ROW, 0.001f),
	   "a second give mid-move adds a row to where the rows are");
	ck(tween_at(&tw, 7000 + 30 + DS_MS_QUICK) == 0.0f, "and the list comes to rest");
	tween_snap(&tw, 0);
	tween_kick(&tw, -ROW, DS_MS_QUICK, 7500);
	ck(tween_at(&tw, 7500) == -ROW && tween_at(&tw, 7500 + DS_MS_QUICK) == 0.0f,
	   "and the same scrolling up");

	/* A thumb faster than the repeat at the window's edge: a press every
	 * 30 ms, each scrolling the list a row. Where a row is drawn is its
	 * layout, a row further up at each press, plus the give. At a press it
	 * does not move at all, and from one millisecond to the next it moves no
	 * further than the out curve allows the move it is on: ds_out is
	 * steepest where it starts, 5 times its distance over its time. */
	{
		const unsigned T = 9000, EVERY = 30;
		float layout = 0.0f, prev = 0.0f, reach = 0.0f, jump = 0.0f;
		int fast = 0;

		tween_snap(&tw, 0);
		for (t = T; t < T + 20 * EVERY + DS_MS_QUICK; t++) {
			float p = layout + tween_at(&tw, t);
			float allow = 5.0f * fabsf(tw.from - tw.to) / DS_MS_QUICK + 0.001f;

			if (t > T && fabsf(p - prev) > allow) fast = 1;
			if ((t - T) % EVERY == 0 && t < T + 20 * EVERY) {
				layout -= ROW;
				tween_kick(&tw, ROW, DS_MS_QUICK, t);
				if (fabsf(layout + tween_at(&tw, t) - p) > jump)
					jump = fabsf(layout + tween_at(&tw, t) - p);
				p = layout + tween_at(&tw, t);
				if (fabsf(tw.from) > reach) reach = fabsf(tw.from);
			}
			prev = p;
		}
		snprintf(what, sizeof what, "pressed every %u ms at the window's edge, the "
		         "rows never jump (the most at a press: %.2f of a row)", EVERY,
		         jump / ROW);
		ck(jump < 0.001f, what);
		ck(!fast, "and between presses move no faster than the out curve allows");
		snprintf(what, sizeof what, "and the give stays within a row and a half "
		         "with nothing clipping it (%.2f rows)", reach / ROW);
		ck(reach < 1.5f * ROW, what);
		ck(tween_at(&tw, T + 20 * EVERY + DS_MS_QUICK) == 0.0f,
		   "and comes to rest when the presses stop");
	}

	/* A clock that wraps, as SDL_GetTicks does after 49 days. */
	tween_snap(&tw, 0);
	tween_to(&tw, ROW, DS_MS_QUICK, 0xFFFFFFFFu - 50);
	ck(tween_moving(&tw, 20) && tween_at(&tw, 20) > 0.0f && tween_at(&tw, 20) < ROW,
	   "a move across the clock's wrap is still moving");
	ck(tween_at(&tw, 0xFFFFFFFFu - 50 + DS_MS_QUICK) == ROW, "and lands");

	if (fails) {
		printf("tween: %d check(s) FAILED\n", fails);
		return 1;
	}
	printf("ok: the plate takes a press from where it is and lands inside the "
	       "%d ms repeat\n", REPEAT_RATE_MS);
	return 0;
}
