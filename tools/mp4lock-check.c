/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's lock rules (src/mp4lock.h), walked through what a pocket and a hand
 * actually do: every button blocked, power included, but the volume keys;
 * SELECT and START held together for hold-unlock unlock; a pair already held
 * when locking does not count until it is let go; the lock panel lit when it
 * locks, for a moment at every press of a blocked button, and while the pair
 * is held; the screen on for the volume bar's life after a volume key, and as
 * long as one is held. And the pair, unlocked: SELECT + START locks, and
 * SELECT alone closes MP4 as it is let go. */
#include <stdio.h>
#include <string.h>

#include "../src/mp4lock.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL: "); \
	printf(__VA_ARGS__); printf("\n        at %s:%d\n", __FILE__, __LINE__); } } while (0)

static bool down[IN_COUNT], pressed[IN_COUNT];

/* Steps from `from` to `to` ms in 10 ms passes, the buttons as they are.
 * True if any pass unlocked. */
static bool run(mp4lock *l, unsigned from, unsigned to)
{
	bool any = false;
	unsigned t;

	for (t = from; t <= to; t += 10) {
		any |= mp4lock_step(l, t, down, pressed);
		memset(pressed, 0, sizeof pressed);
	}
	return any;
}

static void press(in_button b) { down[b] = true; pressed[b] = true; }
static void let_go(in_button b) { down[b] = false; }

int main(void)
{
	mp4lock l;
	int b;

	printf("mp4lock: the lock screen's rules\n");

	memset(&l, 0, sizeof l);
	mp4lock_engage(&l, 1000, false);
	CHECK(l.locked && mp4lock_lit(&l) && mp4lock_awake(&l), "locking lights the screen with the way out");
	run(&l, 1000, 1000 + MP4_SHOW_MS + 20);
	CHECK(!mp4lock_lit(&l) && !mp4lock_awake(&l), "and it goes dark after a moment");

	/* Only the volume keys get through. */
	for (b = 0; b < IN_COUNT; b++)
		CHECK(mp4lock_passes((in_button)b) == (b == IN_VOLUP || b == IN_VOLDN),
		      "button %d %s", b, mp4lock_passes((in_button)b) ? "gets through" : "is blocked");

	/* Every other button: blocked, and the way out shown for a moment as it
	 * goes down - every time, not once - and dark after, held or not. */
	for (b = 0; b < IN_COUNT; b++) {
		if (b == MP4_UNLOCK_A || b == MP4_UNLOCK_B || mp4lock_passes((in_button)b)) continue;
		press((in_button)b);
		run(&l, 6000, 6000);
		CHECK(l.locked && mp4lock_lit(&l) && mp4lock_awake(&l),
		      "button %d shows the lock panel as it goes down", b);
		CHECK(mp4lock_next(&l) == MP4_SHOW_MS, "for a moment: %u ms", mp4lock_next(&l));
		CHECK(!run(&l, 6010, 10010), "button %d held four seconds does not unlock", b);
		CHECK(l.locked && !mp4lock_awake(&l), "still locked, and dark again, after button %d", b);
		let_go((in_button)b);
	}

	/* A volume tap: the screen on for the bar's whole life, then dark. */
	press(IN_VOLUP);
	run(&l, 10100, 10100);
	let_go(IN_VOLUP);
	CHECK(l.locked && mp4lock_awake(&l), "a volume press lights the screen");
	CHECK(!mp4lock_lit(&l), "for the volume bar, not the lock panel over a video");
	CHECK(mp4lock_next(&l) == MP4_VOLUME_LIT_MS, "and says when it goes dark: %u",
	      mp4lock_next(&l));
	run(&l, 10110, 10100 + MP4_VOLUME_LIT_MS - 10);
	CHECK(mp4lock_awake(&l), "lit while the bar holds and fades");
	run(&l, 10100 + MP4_VOLUME_LIT_MS, 10100 + MP4_VOLUME_LIT_MS);
	CHECK(l.locked && !mp4lock_awake(&l), "dark again once the bar is gone");
	CHECK(MP4_VOLUME_LIT_MS == DS_MS_HOLD_BAR + DS_MS_FADE_OUT,
	      "the light lasts exactly the bar's life: %u ms", MP4_VOLUME_LIT_MS);

	/* A tap let go inside one pass still counts. */
	pressed[IN_VOLDN] = true;
	run(&l, 12000, 12000);
	CHECK(mp4lock_awake(&l), "a quick tap of volume down lights it too");
	run(&l, 12010, 12000 + MP4_VOLUME_LIT_MS);
	CHECK(!mp4lock_awake(&l), "and goes dark after the bar");

	/* Held, it stays lit, and goes dark the bar's life after it is let go. */
	press(IN_VOLDN);
	run(&l, 14000, 18000);
	CHECK(mp4lock_awake(&l), "a volume key held four seconds keeps it lit");
	let_go(IN_VOLDN);
	run(&l, 18010, 18000 + MP4_VOLUME_LIT_MS - 10);
	CHECK(mp4lock_awake(&l), "still lit just inside the bar's life after letting go");
	run(&l, 18000 + MP4_VOLUME_LIT_MS, 18000 + MP4_VOLUME_LIT_MS + 10);
	CHECK(l.locked && !mp4lock_awake(&l), "dark after it, and still locked");

	/* Volume never unlocks, held with one of the pair or not. */
	press(IN_VOLUP);
	press(MP4_UNLOCK_A);
	CHECK(!run(&l, 20000, 24000) && l.locked, "volume with SELECT does not unlock");
	let_go(IN_VOLUP);
	let_go(MP4_UNLOCK_A);
	run(&l, 24010, 24010 + MP4_VOLUME_LIT_MS);
	CHECK(!mp4lock_awake(&l), "and the screen goes dark after");

	/* The old way out and the other shoulder pair are nothing now. */
	press(IN_L1);
	press(IN_R1);
	CHECK(!run(&l, 30200, 34210) && l.locked && !mp4lock_awake(&l),
	      "L1+R1 held does nothing but show the way out a moment");
	let_go(IN_L1);
	let_go(IN_R1);

	/* One of the pair is not the pair. */
	press(MP4_UNLOCK_A);
	CHECK(!run(&l, 34300, 38310) && l.locked && !mp4lock_awake(&l), "SELECT alone does nothing");
	let_go(MP4_UNLOCK_A);
	press(MP4_UNLOCK_B);
	CHECK(!run(&l, 38400, 42000) && l.locked, "START alone does nothing");
	let_go(MP4_UNLOCK_B);

	/* Both, let go too soon. */
	press(MP4_UNLOCK_A);
	press(MP4_UNLOCK_B);
	CHECK(!run(&l, 43000, 43000 + MP4_UNLOCK_MS * 3 / 4) && l.locked,
	      "both for three quarters of the hold does not unlock");
	CHECK(mp4lock_progress(&l) > 0.7f && mp4lock_progress(&l) < 0.8f,
	      "and the bar is three quarters full: %.2f", mp4lock_progress(&l));
	CHECK(mp4lock_lit(&l) && mp4lock_next(&l) == 0, "lit, and moving, while held");
	let_go(MP4_UNLOCK_B);
	run(&l, 45000, 45010);
	CHECK(mp4lock_progress(&l) == 0 && l.locked, "let go, the bar empties");
	run(&l, 45020, 47010);
	CHECK(l.locked && !mp4lock_awake(&l), "and the screen is dark again after its moment");

	/* Both, for the whole hold, a volume key held through it too. */
	press(IN_VOLDN);
	press(MP4_UNLOCK_B);
	CHECK(run(&l, 48000, 48000 + MP4_UNLOCK_MS) && !l.locked, "both for the whole hold unlocks");
	CHECK(!mp4lock_lit(&l) && !mp4lock_awake(&l), "and the lock screen is gone");
	let_go(IN_VOLDN);
	let_go(MP4_UNLOCK_A);
	let_go(MP4_UNLOCK_B);

	/* Locked with the pair still down: it has to be let go first. */
	press(MP4_UNLOCK_A);
	press(MP4_UNLOCK_B);
	mp4lock_engage(&l, 50000, true);
	CHECK(!run(&l, 50000, 54000) && l.locked, "a pair held through the lock does not unlock");
	let_go(MP4_UNLOCK_A);
	let_go(MP4_UNLOCK_B);
	run(&l, 54010, 54020);
	press(MP4_UNLOCK_A);
	press(MP4_UNLOCK_B);
	CHECK(run(&l, 54030, 54030 + MP4_UNLOCK_MS) && !l.locked, "let go and held again, it does");

	/* Unlocked, nothing happens. */
	press(IN_VOLUP);
	CHECK(!run(&l, 60000, 63000) && !l.locked && !mp4lock_awake(&l),
	      "unlocked, the rules stay out of the way, volume and all");
	let_go(IN_VOLUP);

	/* Every press of a blocked key shows the way out again, POWER too, and
	 * a tap let go inside one pass as well. */
	memset(&l, 0, sizeof l);
	memset(down, 0, sizeof down);
	memset(pressed, 0, sizeof pressed);
	mp4lock_engage(&l, 100000, false);
	run(&l, 100000, 100000 + MP4_SHOW_MS + 10);
	CHECK(l.locked && !mp4lock_awake(&l), "a fresh lock, dark");
	press(IN_POWER);
	run(&l, 110000, 110000);
	CHECK(l.locked && mp4lock_lit(&l), "POWER is blocked, and shows the way out");
	let_go(IN_POWER);
	run(&l, 110010, 110000 + MP4_SHOW_MS);
	CHECK(l.locked && !mp4lock_awake(&l), "then dark");
	press(IN_ACCEPT);
	run(&l, 120000, 120000);
	let_go(IN_ACCEPT);
	CHECK(mp4lock_lit(&l), "and a second press shows it again");
	run(&l, 120010, 120000 + MP4_SHOW_MS);
	CHECK(l.locked && !mp4lock_awake(&l), "and goes dark again");
	pressed[IN_BACK] = true;
	run(&l, 130000, 130000);
	CHECK(mp4lock_lit(&l), "a quick tap of B shows it");
	run(&l, 130010, 130000 + MP4_SHOW_MS);
	CHECK(l.locked && !mp4lock_awake(&l), "for the same moment");

	/* Unlocked: the pair locks, SELECT alone closes as it is let go */
	{
		mp4chord c = { 0 };

		memset(down, 0, sizeof down);
		memset(pressed, 0, sizeof pressed);
		press(IN_SELECT);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "SELECT down closes nothing yet");
		memset(pressed, 0, sizeof pressed);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "nor held");
		let_go(IN_SELECT);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_SELECT, "SELECT let go alone closes MP4");
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "once");

		press(IN_SELECT);
		mp4chord_step(&c, down, pressed);
		memset(pressed, 0, sizeof pressed);
		press(IN_START);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_LOCK, "START joining SELECT locks");
		memset(pressed, 0, sizeof pressed);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "once, held or not");
		let_go(IN_SELECT);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "and SELECT let go then closes nothing");
		let_go(IN_START);

		press(IN_START);
		mp4chord_step(&c, down, pressed);
		memset(pressed, 0, sizeof pressed);
		press(IN_SELECT);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_LOCK, "SELECT joining START locks");
		memset(pressed, 0, sizeof pressed);
		let_go(IN_SELECT);
		let_go(IN_START);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "and closes nothing let go");

		press(IN_SELECT);
		press(IN_START);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_LOCK, "both in one pass lock");
		memset(pressed, 0, sizeof pressed);
		let_go(IN_SELECT);
		let_go(IN_START);
		mp4chord_step(&c, down, pressed);

		/* A SELECT already down as the screen opens is the one that opened
		 * it, not this screen's */
		c = (mp4chord){ 0 };
		down[IN_SELECT] = true;
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "a SELECT held from before");
		let_go(IN_SELECT);
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_NONE, "closes nothing let go");

		/* A tap inside one pass */
		pressed[IN_SELECT] = true;
		CHECK(mp4chord_step(&c, down, pressed) == MP4C_SELECT, "a SELECT tap closes");
		memset(pressed, 0, sizeof pressed);
	}

	if (failures) {
		printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
