/* SPDX-License-Identifier: GPL-3.0-only */
/* The Smart Pro's controls with no device in them (tools/sppad.h): frames,
 * buttons as keys, sticks as axes. tools/sppad.c reads the ports with them,
 * tools/sppad-check.c holds them on a computer. */
#include "sppad.h"

#include <string.h>

void sp_reader_init(sp_reader *r)
{
	memset(r, 0, sizeof *r);
}

/* What was held is no frame: its head goes, and what follows is kept from
 * the next head in it, if there is one */
static void resync(sp_reader *r)
{
	int i;

	for (i = 1; i < r->n && r->buf[i] != SP_HEAD; i++) ;
	r->skipped += (unsigned long)i;
	memmove(r->buf, r->buf + i, (size_t)(r->n - i));
	r->n -= i;
}

int sp_feed(sp_reader *r, const uint8_t *p, size_t n, sp_frame *out, int max)
{
	int got = 0;
	size_t i;

	for (i = 0; i < n; i++) {
		if (r->n == 0 && p[i] != SP_HEAD) {
			r->skipped++;
			continue;
		}
		r->buf[r->n++] = p[i];
		if (r->n < SP_FRAME_LEN) continue;
		if (r->buf[SP_FRAME_LEN - 1] != SP_TAIL) {
			resync(r);
			continue;
		}
		r->frames++;
		if (got < max) {
			out[got].buttons = r->buf[2];
			out[got].x = (uint16_t)(r->buf[3] << 8 | r->buf[4]);
			out[got].y = (uint16_t)(r->buf[5] << 8 | r->buf[6]);
			got++;
		} else {
			r->lost++;
		}
		r->n = 0;
	}
	return got;
}

/* The bits as Trimui_Inputd reads them; the keys the GKD Pixel 2's pad
 * sends, as the Brick's brick-pad does (plastron's patch 0039) */
const sp_button sp_buttons[] = {
	{ SP_LEFT,  0x01, BTN_TL,         "L1" },
	{ SP_LEFT,  0x02, BTN_TL2,        "L2" },
	{ SP_LEFT,  0x04, BTN_DPAD_UP,    "Up" },
	{ SP_LEFT,  0x08, BTN_DPAD_LEFT,  "Left" },
	{ SP_LEFT,  0x10, BTN_DPAD_RIGHT, "Right" },
	{ SP_LEFT,  0x20, BTN_DPAD_DOWN,  "Down" },
	{ SP_LEFT,  0x80, BTN_MODE,       "Menu" },
	{ SP_RIGHT, 0x01, BTN_TR,         "R1" },
	{ SP_RIGHT, 0x02, BTN_TR2,        "R2" },
	{ SP_RIGHT, 0x04, BTN_WEST,       "Y" },
	{ SP_RIGHT, 0x08, BTN_NORTH,      "X" },
	{ SP_RIGHT, 0x10, BTN_EAST,       "B" },
	{ SP_RIGHT, 0x20, BTN_SOUTH,      "A" },
	{ SP_RIGHT, 0x40, BTN_SELECT,     "Select" },
	{ SP_RIGHT, 0x80, BTN_START,      "Start" },
};
const int sp_n_buttons = (int)(sizeof sp_buttons / sizeof sp_buttons[0]);

int sp_key_changes(sp_half half, uint8_t was, uint8_t now, int *codes, int *values, int max)
{
	uint8_t changed = was ^ now;
	int i, n = 0;

	for (i = 0; i < sp_n_buttons && n < max; i++) {
		const sp_button *b = &sp_buttons[i];

		if (b->half != half || !(changed & b->bit)) continue;
		codes[n] = b->code;
		values[n] = (now & b->bit) ? 1 : 0;
		n++;
	}
	return n;
}

void sp_axis_init(sp_axis *a)
{
	memset(a, 0, sizeof *a);
}

int sp_axis_value(sp_axis *a, int raw)
{
	int d, span, dead, v;

	if (!a->learned) {
		a->sum += raw;
		if (++a->reads < SP_CENTRE_READS) return 0;
		a->centre = a->sum / a->reads;
		a->lo = a->centre - SP_FIRST_REACH(a->centre);
		a->hi = a->centre + SP_FIRST_REACH(a->centre);
		a->learned = true;
	}
	if (raw < a->lo) a->lo = raw;
	if (raw > a->hi) a->hi = raw;
	d = raw - a->centre;
	span = d < 0 ? a->centre - a->lo : a->hi - a->centre;
	if (span < 1) return 0;
	if (d < 0) d = -d;
	/* Thousandths of the way to the end, the dead zone taken off the start */
	dead = SP_DEAD_PCT * 10;
	v = (int)((long)d * 1000 / span);
	if (v <= dead) return 0;
	v = (int)((long)(v - dead) * SP_AXIS_MAX / (1000 - dead));
	if (v > SP_AXIS_MAX) v = SP_AXIS_MAX;
	/* The controller's axes run the other way */
	return raw < a->centre ? v : -v;
}

bool sp_silent(unsigned long last_frame_ms, unsigned long now_ms, unsigned long quiet_ms)
{
	return now_ms >= last_frame_ms && now_ms - last_frame_ms >= quiet_ms;
}
