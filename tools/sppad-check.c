/* SPDX-License-Identifier: GPL-3.0-only */
/* The Smart Pro's controls on a computer (tools/sppad.h): frames found in a
 * stream however the reads cut it and whatever junk comes before them, every
 * press reported, even one that lasts a frame, the buttons as the keys the
 * launcher and Diatom read, and a stick's axis with its centre learned.
 *
 *   make check-sppad
 */
#include "sppad.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* A frame as a half sends it */
static void frame_bytes(uint8_t *b, uint8_t buttons, uint16_t x, uint16_t y)
{
	b[0] = SP_HEAD;
	b[1] = 0x00;
	b[2] = buttons;
	b[3] = (uint8_t)(x >> 8);
	b[4] = (uint8_t)x;
	b[5] = (uint8_t)(y >> 8);
	b[6] = (uint8_t)y;
	b[7] = SP_TAIL;
}

static bool is(const sp_frame *f, uint8_t buttons, uint16_t x, uint16_t y)
{
	return f->buttons == buttons && f->x == x && f->y == y;
}

static void frames(void)
{
	sp_reader r;
	sp_frame out[16];
	uint8_t b[64];
	int n, cut;

	printf("sppad-check: frames\n");
	sp_reader_init(&r);
	frame_bytes(b, 0x21, 2000, 2048);
	n = sp_feed(&r, b, 8, out, 16);
	CHECK(n == 1 && is(&out[0], 0x21, 2000, 2048), "a whole frame: its buttons, X and Y (high byte first)");
	CHECK(r.frames == 1 && r.skipped == 0, "one frame, nothing passed over");

	for (cut = 1; cut < SP_FRAME_LEN; cut++) {
		int got;

		sp_reader_init(&r);
		got = sp_feed(&r, b, (size_t)cut, out, 16);
		got += sp_feed(&r, b + cut, (size_t)(SP_FRAME_LEN - cut), out + got, 16 - got);
		CHECK(got == 1 && is(&out[0], 0x21, 2000, 2048), "a frame cut after byte %d: found once, whole", cut);
	}

	/* Junk first, a stray head among it */
	sp_reader_init(&r);
	b[0] = 0x12; b[1] = SP_HEAD; b[2] = 0x40; b[3] = 0x00;
	frame_bytes(b + 4, 0x80, 1, 2);
	n = sp_feed(&r, b, 12, out, 16);
	CHECK(n == 1 && is(&out[0], 0x80, 1, 2), "junk with a stray head before a frame: the frame (%d)", n);
	CHECK(r.skipped == 4, "the 4 bytes of junk passed over (%lu)", r.skipped);

	/* Head bytes inside a frame: every button down, an axis at 0xFF */
	sp_reader_init(&r);
	frame_bytes(b, 0xFF, 0x00FF, 0xFFFF);
	b[7] = SP_TAIL;
	n = sp_feed(&r, b, 8, out, 16);
	CHECK(n == 1 && is(&out[0], 0xFF, 0x00FF, 0xFFFF), "0xFF inside a frame is data");

	/* A frame cut short (its tail lost), then a whole one */
	sp_reader_init(&r);
	frame_bytes(b, 0x01, 10, 20);
	frame_bytes(b + 5, 0x02, 30, 40);
	n = sp_feed(&r, b, 13, out, 16);
	CHECK(n == 1 && is(&out[0], 0x02, 30, 40), "a frame cut short, then a whole one: the whole one (%d)", n);

	/* More frames than room: the room's worth, the rest counted */
	sp_reader_init(&r);
	for (n = 0; n < 4; n++) frame_bytes(b + n * 8, (uint8_t)n, 0, 0);
	n = sp_feed(&r, b, 32, out, 3);
	CHECK(n == 3 && out[2].buttons == 2 && r.lost == 1, "4 frames, room for 3: 3, and 1 lost (%lu)", r.lost);
}

/* 200 frames, junk between some, fed in reads of every size from 1 to 11:
 * every frame found, in order */
static void stream(void)
{
	static uint8_t s[200 * 12];
	sp_reader r;
	sp_frame out[16];
	size_t len = 0, at = 0;
	unsigned seed = 7;
	int i, found = 0, in_order = 1, step = 1;

	printf("sppad-check: a stream\n");
	for (i = 0; i < 200; i++) {
		seed = seed * 1103515245u + 12345u;
		if (seed >> 28 < 4) {   /* junk: a byte or three, heads among them */
			int k = 1 + (int)(seed >> 26 & 3) % 3;

			while (k--) s[len++] = (seed & 1) ? SP_HEAD : (uint8_t)(seed >> 8);
		}
		frame_bytes(s + len, (uint8_t)i, (uint16_t)(i * 7), (uint16_t)(i * 11));
		len += 8;
	}
	sp_reader_init(&r);
	while (at < len) {
		size_t take = (size_t)step < len - at ? (size_t)step : len - at;
		int n = sp_feed(&r, s + at, take, out, 16), k;

		for (k = 0; k < n; k++, found++)
			if (out[k].buttons != (uint8_t)found || out[k].x != (uint16_t)(found * 7)) in_order = 0;
		at += take;
		step = step % 11 + 1;
	}
	CHECK(found == 200 && in_order, "200 frames with junk between, reads of 1 to 11 bytes: all, in order (%d)",
	      found);
}

static int code_of(const char *name)
{
	int i;

	for (i = 0; i < sp_n_buttons; i++)
		if (!strcmp(sp_buttons[i].name, name)) return sp_buttons[i].code;
	return -1;
}

static void buttons(void)
{
	int codes[16], values[16], n, i, j;

	printf("sppad-check: the buttons\n");
	CHECK(sp_n_buttons == 15, "15 buttons: 7 on the left, 8 on the right (%d)", sp_n_buttons);
	for (i = 0; i < sp_n_buttons; i++)
		for (j = i + 1; j < sp_n_buttons; j++) {
			CHECK(sp_buttons[i].code != sp_buttons[j].code, "%s and %s send different keys",
			      sp_buttons[i].name, sp_buttons[j].name);
			CHECK(sp_buttons[i].half != sp_buttons[j].half || sp_buttons[i].bit != sp_buttons[j].bit,
			      "%s and %s are different bits", sp_buttons[i].name, sp_buttons[j].name);
		}
	/* The Pixel 2's codes, as brick-pad sends them: A, B, X, Y by their
	 * labels, the d-pad as keys, L2 and R2 as keys, MENU as BTN_MODE */
	CHECK(code_of("A") == BTN_SOUTH && code_of("B") == BTN_EAST && code_of("X") == BTN_NORTH &&
	      code_of("Y") == BTN_WEST, "A, B, X and Y as SOUTH, EAST, NORTH and WEST");
	CHECK(code_of("Up") == BTN_DPAD_UP && code_of("Down") == BTN_DPAD_DOWN &&
	      code_of("Left") == BTN_DPAD_LEFT && code_of("Right") == BTN_DPAD_RIGHT, "the d-pad as BTN_DPAD_*");
	CHECK(code_of("L1") == BTN_TL && code_of("R1") == BTN_TR && code_of("L2") == BTN_TL2 &&
	      code_of("R2") == BTN_TR2, "the shoulders as TL, TR, TL2 and TR2");
	CHECK(code_of("Select") == BTN_SELECT && code_of("Start") == BTN_START && code_of("Menu") == BTN_MODE,
	      "SELECT, START and MENU");

	n = sp_key_changes(SP_LEFT, 0x00, 0x04, codes, values, 16);
	CHECK(n == 1 && codes[0] == BTN_DPAD_UP && values[0] == 1, "left 0x04 down: up");
	n = sp_key_changes(SP_RIGHT, 0x00, 0x20, codes, values, 16);
	CHECK(n == 1 && codes[0] == BTN_SOUTH && values[0] == 1, "right 0x20 down: A");
	n = sp_key_changes(SP_RIGHT, 0x20, 0x00, codes, values, 16);
	CHECK(n == 1 && codes[0] == BTN_SOUTH && values[0] == 0, "and up again");
	n = sp_key_changes(SP_LEFT, 0x81, 0x22, codes, values, 16);
	CHECK(n == 4, "MENU and L1 up, L2 and down down: four changes (%d)", n);
	n = sp_key_changes(SP_LEFT, 0x00, 0x40, codes, values, 16);
	CHECK(n == 0, "a bit no button is (left 0x40): nothing");
	n = sp_key_changes(SP_LEFT, 0x13, 0x13, codes, values, 16);
	CHECK(n == 0, "nothing changed: nothing");

	/* A press one frame long: down, then up, both said */
	{
		sp_reader r;
		sp_frame out[4];
		uint8_t b[24];
		uint8_t was = 0;
		int down = 0, up = 0, k;

		frame_bytes(b, 0x00, 0, 0);
		frame_bytes(b + 8, 0x10, 0, 0);
		frame_bytes(b + 16, 0x00, 0, 0);
		sp_reader_init(&r);
		n = sp_feed(&r, b, 24, out, 4);
		for (k = 0; k < n; k++) {
			int m = sp_key_changes(SP_RIGHT, was, out[k].buttons, codes, values, 16);

			for (i = 0; i < m; i++)
				if (codes[i] == BTN_EAST) { if (values[i]) down++; else up++; }
			was = out[k].buttons;
		}
		CHECK(down == 1 && up == 1, "B held for one frame in one read: down once and up once");
	}
}

static void axes(void)
{
	sp_axis a;
	int i, v;

	printf("sppad-check: a stick\n");
	sp_axis_init(&a);
	for (i = 0; i < SP_CENTRE_READS - 1; i++)
		CHECK(sp_axis_value(&a, 2040 + i % 3 * 8) == 0, "while the centre is learned: 0");
	sp_axis_value(&a, 2048);
	CHECK(a.learned && a.centre >= 2040 && a.centre <= 2056, "the centre, learned from %d readings (%d)",
	      SP_CENTRE_READS, a.centre);
	CHECK(sp_axis_value(&a, a.centre) == 0, "at the centre: 0");
	v = SP_FIRST_REACH(a.centre) * SP_DEAD_PCT / 100;
	CHECK(sp_axis_value(&a, a.centre + v) == 0 && sp_axis_value(&a, a.centre - v) == 0,
	      "inside the dead zone either way: 0");
	v = sp_axis_value(&a, a.centre - SP_FIRST_REACH(a.centre));
	CHECK(v == SP_AXIS_MAX, "a reading a third of the centre under it: the end, turned to + (%d)", v);
	v = sp_axis_value(&a, a.centre + SP_FIRST_REACH(a.centre));
	CHECK(v == -SP_AXIS_MAX, "and over it: the other end (%d)", v);
	v = sp_axis_value(&a, a.centre - SP_FIRST_REACH(a.centre) / 2);
	CHECK(v > SP_AXIS_MAX / 3 && v < SP_AXIS_MAX * 2 / 3, "half way: about half (%d)", v);
	/* A stick that goes further than first taken: the end moves out to it */
	v = sp_axis_value(&a, 50);
	CHECK(v == SP_AXIS_MAX && a.lo == 50, "further than taken: the end, and the end is there now (%d)", a.lo);
	v = sp_axis_value(&a, a.centre - SP_FIRST_REACH(a.centre));
	CHECK(v < SP_AXIS_MAX, "so where the end was is short of it now (%d)", v);
	CHECK(sp_axis_value(&a, a.centre + 100000) == -SP_AXIS_MAX, "never past the end");

	/* TrimUI's own reader's factory travel when a stick has no calibration
	 * of its own: at rest 2150, from 1050 to 2900 (trimui_inputd, firmware
	 * 1.1.1). A stick like that reaches both ends at its first push, so the
	 * first reach must be inside the shorter side */
	sp_axis_init(&a);
	for (i = 0; i < SP_CENTRE_READS; i++) sp_axis_value(&a, 2150);
	CHECK(sp_axis_value(&a, 2900) == -SP_AXIS_MAX && sp_axis_value(&a, 1050) == SP_AXIS_MAX,
	      "TrimUI's factory travel (1050, 2150, 2900): both ends at the first push");

	/* A half that reads 0 at rest (a broken stick): no division by nothing */
	sp_axis_init(&a);
	for (i = 0; i < SP_CENTRE_READS; i++) sp_axis_value(&a, 0);
	CHECK(sp_axis_value(&a, 0) == 0 && sp_axis_value(&a, 5) <= 0, "a centre of 0: 0 at rest, and no crash");
}

static void silent(void)
{
	printf("sppad-check: a silent half\n");
	CHECK(!sp_silent(1000, 2999, 2000), "1999 ms since its last frame, 2000 allowed: not silent");
	CHECK(sp_silent(1000, 3000, 2000), "2000 ms: silent");
	CHECK(!sp_silent(5000, 4000, 2000), "a clock that went back: not silent");
}

int main(void)
{
	frames();
	stream();
	buttons();
	axes();
	silent();
	printf("sppad-check: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
