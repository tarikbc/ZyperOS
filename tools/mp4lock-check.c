/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's lock rules (src/mp4lock.h), walked through what a pocket and a hand
 * actually do: every button blocked, power included, but the volume keys;
 * SELECT and START held together for hold-unlock unlock; a pair already held
 * when locking does not count until it is let go; the lock panel lit when it
 * locks and while the pair is held; the screen on for the volume bar's life
 * after a volume key, and as long as one is held; dark for every other
 * press. */
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

	/* Every other button: blocked, and dark. */
	for (b = 0; b < IN_COUNT; b++) {
		if (b == MP4_UNLOCK_A || b == MP4_UNLOCK_B || mp4lock_passes((in_button)b)) continue;
		press((in_button)b);
		CHECK(!run(&l, 6000, 9000), "button %d held three seconds does not unlock", b);
		CHECK(l.locked && !mp4lock_awake(&l), "still locked and dark after button %d", b);
		let_go((in_button)b);
	}
	press(IN_POWER);
	run(&l, 10000, 10050);
	CHECK(l.locked && !mp4lock_awake(&l), "POWER is blocked, and lights nothing");
	let_go(IN_POWER);

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
	CHECK(!run(&l, 30200, 33000) && l.locked && !mp4lock_awake(&l), "L1+R1 held does nothing");
	let_go(IN_L1);
	let_go(IN_R1);

	/* One of the pair is not the pair. */
	press(MP4_UNLOCK_A);
	CHECK(!run(&l, 34000, 38000) && l.locked && !mp4lock_awake(&l), "SELECT alone does nothing");
	let_go(MP4_UNLOCK_A);
	press(MP4_UNLOCK_B);
	CHECK(!run(&l, 38010, 42000) && l.locked, "START alone does nothing");
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
	CHECK(mp4lock_progress(&l) == 0 && l.locked && !mp4lock_awake(&l),
	      "let go, the bar empties and the screen is dark again");

	/* Both, for the whole hold, a volume key held through it too. */
	press(IN_VOLDN);
	press(MP4_UNLOCK_B);
	CHECK(run(&l, 46000, 46000 + MP4_UNLOCK_MS) && !l.locked, "both for the whole hold unlocks");
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

	if (failures) {
		printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
