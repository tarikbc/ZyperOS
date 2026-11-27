/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SPPAD_H
#define ZYPEROS_SPPAD_H

/* The TrimUI Smart Pro's controls (tools/sppad.c): every button and both
 * sticks come from two small controllers, one in each half of the body, on
 * serial ports (the left on /dev/ttyS4, the right on /dev/ttyS3, 19200 baud,
 * 8N1). Each sends 8-byte frames: 0xFF, a byte not read, the buttons (a bit
 * a button), the stick's X and Y (each high byte first), 0xFE. The format and
 * the bits are from Jpe230's Trimui_Inputd (GPL-3.0, as ZyperOS).
 *
 * The part here has no device in it, so tools/sppad-check.c holds it on a
 * computer: frames found in a stream of bytes however the reads cut it, the
 * buttons as the GKD Pixel 2's key codes (as the Brick's brick-pad sends
 * them, so the launcher and Diatom read all three alike), and a stick's raw
 * reading as an axis, its centre learned at rest. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __linux__
#include <linux/input.h>
#else
/* The codes as linux/input-event-codes.h has them, for the check on a
 * computer that has no such header */
#define BTN_SOUTH      0x130
#define BTN_EAST       0x131
#define BTN_NORTH      0x133
#define BTN_WEST       0x134
#define BTN_TL         0x136
#define BTN_TR         0x137
#define BTN_TL2        0x138
#define BTN_TR2        0x139
#define BTN_SELECT     0x13a
#define BTN_START      0x13b
#define BTN_MODE       0x13c
#define BTN_DPAD_UP    0x220
#define BTN_DPAD_DOWN  0x221
#define BTN_DPAD_LEFT  0x222
#define BTN_DPAD_RIGHT 0x223
#endif

#define SP_FRAME_LEN 8
#define SP_HEAD      0xFF
#define SP_TAIL      0xFE

typedef enum { SP_LEFT, SP_RIGHT, SP_HALVES } sp_half;

/* One frame of a half: its buttons and its stick's raw reading */
typedef struct {
	uint8_t  buttons;
	uint16_t x, y;
} sp_frame;

/* A half's stream, read as it comes */
typedef struct {
	uint8_t       buf[SP_FRAME_LEN];
	int           n;         /* bytes of the frame so far */
	unsigned long frames;    /* frames found */
	unsigned long skipped;   /* bytes passed over finding them */
	unsigned long lost;      /* frames found with no room in `out` */
} sp_reader;

void sp_reader_init(sp_reader *r);

/* `n` bytes from a half's port: each whole frame among them, in order, into
 * `out` (at most `max`, the rest counted in r->lost), and their count. A
 * frame cut across two reads is kept until its end comes. Bytes
 * before a frame's head, and a run of 8 that does not end in the tail, are
 * passed over and the next head is looked for in what was held, so the junk
 * before a frame never costs the frame. */
int sp_feed(sp_reader *r, const uint8_t *p, size_t n, sp_frame *out, int max);

/* A button: which half, which bit of its frame, the key it is sent as, and
 * its name as printed */
typedef struct {
	sp_half     half;
	uint8_t     bit;
	int         code;
	const char *name;
} sp_button;

extern const sp_button sp_buttons[];
extern const int sp_n_buttons;

/* The keys that changed between a half's buttons `was` and `now`: each
 * one's code into codes[] and 1 (down) or 0 (up) into values[], at most
 * `max`; their count */
int sp_key_changes(sp_half half, uint8_t was, uint8_t now, int *codes, int *values, int max);

/* A stick's axis: its raw reading to -SP_AXIS_MAX..SP_AXIS_MAX, the
 * controller's direction turned (its axes run the other way), with a dead
 * zone round the centre.
 *
 * The centre is learned from the first readings, the stick at rest as the
 * handheld starts. How far it goes each way is not known before a stick has
 * gone there: until then it is taken as a third of the centre's own value,
 * and it grows to the furthest reading seen, so a push to the end is always
 * the end. A third is inside the shorter side of TrimUI's own reader's
 * factory travel (at rest 2150, from 1050 to 2900, in trimui_inputd of
 * firmware 1.1.1, for a stick with no calibration of its own): a first reach
 * further out than the stick goes would leave its end out of reach. */
#define SP_AXIS_MAX     32767
#define SP_CENTRE_READS 8      /* readings averaged for the centre */
#define SP_DEAD_PCT     10     /* the dead zone, percent of the way to the end */
#define SP_FIRST_REACH(centre) ((centre) / 3)   /* each way, raw, until seen */

typedef struct {
	int  sum, reads;     /* the centre, while it is learned */
	int  centre;
	int  lo, hi;         /* the furthest seen each way, raw */
	bool learned;
} sp_axis;

void sp_axis_init(sp_axis *a);
int sp_axis_value(sp_axis *a, int raw);

/* Whether a half has gone silent: no frame for `quiet_ms` and more */
bool sp_silent(unsigned long last_frame_ms, unsigned long now_ms, unsigned long quiet_ms);

#endif
