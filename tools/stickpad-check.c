/* SPDX-License-Identifier: GPL-3.0-only */
/* A stick as the d-pad (src/stickpad.h), on a computer: half way holds,
 * under a third lets go, one axis at a time, never two directions.
 *
 *   make check-stickpad
 */
#include "../src/stickpad.h"

#include <stdio.h>

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define MAX 32767

static int held_count(const stickpad *s)
{
	int i, n = 0;

	for (i = 0; i < STICK_DIRS; i++) n += s->held[i];
	return n;
}

static bool only(const stickpad *s, int dir)
{
	return s->held[dir] && held_count(s) == 1;
}

int main(void)
{
	stickpad s = { { false } };
	int x, y, a;

	printf("stickpad-check: the rule\n");
	stickpad_update(&s, 0, 0, MAX);
	CHECK(held_count(&s) == 0, "at rest: nothing");
	stickpad_update(&s, MAX * 49 / 100, 0, MAX);
	CHECK(held_count(&s) == 0, "under half way right: nothing");
	stickpad_update(&s, MAX / 2 + 1, 0, MAX);
	CHECK(only(&s, STICK_RIGHT), "half way right: right");
	stickpad_update(&s, MAX * 40 / 100, 0, MAX);
	CHECK(only(&s, STICK_RIGHT), "back to 40%%: still right (no flutter)");
	stickpad_update(&s, MAX * 32 / 100, 0, MAX);
	CHECK(held_count(&s) == 0, "under a third: let go");
	stickpad_update(&s, -MAX, 0, MAX);
	CHECK(only(&s, STICK_LEFT), "all the way left: left");
	stickpad_update(&s, 0, -MAX, MAX);
	CHECK(only(&s, STICK_UP), "straight up: up, left let go");
	stickpad_update(&s, 0, MAX, MAX);
	CHECK(only(&s, STICK_DOWN), "straight down: down (+ is down, as evdev has it)");

	/* Up and a little right is up; the further axis wins, one direction */
	stickpad_update(&s, 0, 0, MAX);
	stickpad_update(&s, MAX * 55 / 100, -MAX * 80 / 100, MAX);
	CHECK(only(&s, STICK_UP), "up and a little right: up alone");
	stickpad_update(&s, MAX * 80 / 100, -MAX * 55 / 100, MAX);
	CHECK(only(&s, STICK_RIGHT), "rolled round to right and a little up: right alone");

	/* Pushed corner-ways: the direction held keeps its axis through a wobble
	 * past 45 degrees, and gives it up well past */
	stickpad_update(&s, 0, 0, MAX);
	stickpad_update(&s, MAX * 70 / 100, -MAX * 68 / 100, MAX);
	CHECK(only(&s, STICK_RIGHT), "corner-ways, a hair more right: right");
	stickpad_update(&s, MAX * 68 / 100, -MAX * 70 / 100, MAX);
	CHECK(only(&s, STICK_RIGHT), "a wobble a hair past the corner: still right");
	stickpad_update(&s, MAX * 50 / 100, -MAX * 70 / 100, MAX);
	CHECK(only(&s, STICK_UP), "well past the corner: up");

	printf("stickpad-check: every way round\n");
	/* Round the circle at the end, and at 40%: never two directions, and
	 * never one at 40% from rest */
	for (a = 0; a < 360; a += 3) {
		stickpad r = { { false } };
		static const int c[] = { 1000, 999, 995, 988, 978, 966, 951, 934, 914, 891, 866, 839, 809, 777,
		                         743, 707, 669, 629, 588, 545, 500, 454, 407, 358, 309, 259, 208, 156,
		                         105, 52, 0 };
		int q = a / 90, k = a % 90, cx, cy;

		cx = c[k / 3];
		cy = c[30 - k / 3];
		x = q == 0 ? cx : q == 1 ? -cy : q == 2 ? -cx : cy;
		y = q == 0 ? cy : q == 1 ? cx : q == 2 ? -cy : -cx;
		stickpad_update(&s, x * MAX / 1000, y * MAX / 1000, MAX);
		CHECK(held_count(&s) == 1, "at %d degrees, at the end: one direction (%d)", a, held_count(&s));
		stickpad_update(&r, x * MAX / 1000 * 40 / 100, y * MAX / 1000 * 40 / 100, MAX);
		CHECK(held_count(&r) == 0, "at %d degrees, 40%% from rest: nothing", a);
	}
	stickpad_update(&s, 0, 0, 0);
	CHECK(held_count(&s) == 0, "a stick with no range: nothing");

	printf("stickpad-check: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
