/* SPDX-License-Identifier: GPL-3.0-only */
/* The status marks, DESIGN.md 11.12: the right end of the top bar
 * (src/topbar.h), on the ground and in no shape. My pick in the status
 * lab, 2026-11-04, "the quiet corner".
 *
 * Right to left, all on one row 24 units tall: the battery, Wi-Fi, a
 * Bluetooth headset on the Brick, File Transfer. The top bar puts the row's
 * right end at x 960 and its top at y 52, and the clock at its left end;
 * STATUS_RIGHT and STATUS_TOP below are the row as this file measures it, at
 * the screen's edge less the inset. The marks are 12 apart and drawn here, as pixels, into one small buffer the
 * launcher turns into a texture when one of them changes - the clock goes
 * past every minute, a mark at most every couple of seconds - and blits each
 * frame. The clock is text and the launcher's (ui_text_tabular).
 *
 * The marks, in a 24-unit row, flat:
 *   Wi-Fi, 36 wide: a wedge of radius 8 and arcs at 14 and 22, +-45 degrees
 *     about straight up, 4-unit strokes as the volume bar's speaker waves.
 *     Lit parts **text-soft**, unlit **track**. On and not connected, the
 *     whole fan in **text-dim**, crossed by a 4-unit slash cut 3 units out of
 *     it. Off, or no radio: no mark.
 *   Bluetooth, 16 wide: the rune, a 3-unit stroke in **text-soft**.
 *   Battery, 44 wide: a 40x24 body with a 3-unit outline (**frame**) and
 *     **radius-chip** corners, a 3x8 tip, and a fill up to 28x12, at least 4
 *     wide while there is any charge. **text-soft**; **warning**, outline and
 *     fill, at STATUS_BATT_LOW and under off the charger; charging, the fill
 *     in **accent-ink** and a bolt over the body, cut out of what is behind it.
 *
 * Plain C, no SDL and no device: tools/status-check.c links this file alone,
 * for the rules (the battery's thresholds, the signal's steps) and for what
 * the marks look like in every state, in both themes. The shapes are drawn
 * by signed distance, antialiased the way src/osdbar.c draws the volume bar;
 * a copy of that idea rather than of its file, which Diatom and portkeys
 * share and which stays the bar's alone. */
#ifndef ZYPEROS_STATUS_H
#define ZYPEROS_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "design.h"

/* Where the corner stands, in units. */
#define STATUS_RIGHT     (1024 - DS_INSET)   /* the battery's right edge */
#define STATUS_TOP       DS_INSET            /* the row's top */
#define STATUS_ROW       24                  /* the row's height */
#define STATUS_MID       (STATUS_TOP + STATUS_ROW / 2)  /* the clock's capitals */
#define STATUS_GAP       12                  /* between two marks */

/* The buffer's margin around the marks, in units, so an edge's antialiasing
 * at the row's top or bottom is not cut off: the buffer's top left goes at
 * (left - STATUS_PAD, STATUS_TOP - STATUS_PAD). */
#define STATUS_PAD 1

/* The battery's warnings, in percent, off the charger: low at or under the
 * first, the fill blinking at or under the second. The same numbers as
 * Diatom's disc over a game, so the shelf and a game never disagree. At or
 * under BATT_EMPTY_PCT (below) the device powers off. */
#define STATUS_BATT_LOW      10
#define STATUS_BATT_CRITICAL 5

/* At or below this, off the charger, the device saves a running game and
 * powers off rather than let the PMIC cut it at its own limit, which loses the
 * game's progress since its last save and leaves the card dirty. A starting
 * point, short of measuring where each device's reading sits when it gives
 * out: both report 0% well before their PMIC's cutoff. The launcher's
 * battery_empty (src/main.c) and, over a port, tools/portkeys.c go by it. */
#define BATT_EMPTY_PCT  2

/* 0 fine or charging, 1 low, 2 critical: what Diatom is sent, and what the
 * battery mark is drawn in. */
int status_batt_level(int pct, bool charging);

/* The fill's width in units for a charge: 0 when there is none, else
 * 28 * pct / 100, rounded, and at least 4, so 1% still shows. */
int status_batt_fill(int pct);

/* Wi-Fi's mark. NONE draws nothing (off, or no radio), IDLE the crossed fan
 * (on, but not connected: no network, or one joined with no address yet),
 * and 1 to 3 that many parts of the fan lit. */
#define STATUS_WIFI_NONE (-1)
#define STATUS_WIFI_IDLE 0
#define STATUS_WIFI_FULL 3

/* The parts lit for a signal in dBm: all three at -60 or better, two to -70,
 * one under. */
int status_wifi_steps(int dbm);

/* The signal in `text`, what `wpa_cli signal_poll` answers ("RSSI=-56" on a
 * line of its own, with LINKSPEED and the rest), in dBm. False when there is
 * none: FAIL from a driver that cannot say, or a level nobody heard. */
bool status_rssi_dbm(const char *text, int *dbm);

/* What the marks show. */
typedef struct {
	int  wifi;       /* STATUS_WIFI_NONE, STATUS_WIFI_IDLE, or 1 to 3 */
	bool bt;         /* a headset connected (the Brick) */
	int  batt;       /* percent, or -1 for no reading and no mark */
	bool charging;   /* on the cable */
	/* On the cable and held off at the charge limit (MENU > Charge Limit):
	 * a plug where the bolt goes. With charging. */
	bool held;
	bool blink_off;  /* critical, and in the second the fill is not drawn */
	/* Time Left with no time yet, one on its way: three dots in the number's
	 * place, calc_step (0 to 3) of them filled; pct_w is STATUS_CALC_W */
	bool calc;
	int  calc_step;
	/* Room left of the battery for its charge written out ("82%"), or the
	 * time left ("3 h 20 min"), in units: 0 for none (MENU > Battery Icon >
	 * Bars). The caller writes the text there, in the clock's font, as this
	 * file draws no text. */
	int  pct_w;
	/* File Transfer on behind the shelf (My pick, 2026-11-06): its mark,
	 * an arrow up and one down, left of the others; while files move a ring
	 * round it in **track**, filled clockwise from the top in **accent-ink**
	 * as far as the upload on its way (xfer_pct, 0 to 100). Off by default. */
	bool xfer;
	bool xfer_moving;
	int  xfer_pct;
	/* Headphones in the jack (plat_headphones_present, the codec's
	 * SW_HEADPHONE_INSERT), mine, 2026-11-09: a headband and two cups,
	 * 24 wide, a 3-unit stroke as the rune's, in **text-soft**, between the
	 * rune and the fan. Where they are, Audio Channels' Auto plays stereo. */
	bool phones;
} status_marks;

/* The battery mark's width and the space between its number and it */
#define STATUS_BATT_W   44
#define STATUS_PCT_GAP  8
/* The most room the number or the time left may take, in units: "100%" is
 * under 50, and "9 h 55 min" or "9 小时 55 分钟" the widest of the time */
#define STATUS_PCT_MAX  160
/* The room of the calculating dots: three of 6 units, 10.5 apart */
#define STATUS_CALC_W   27

/* The marks' width in units, gaps between them included: 0 for none, 120
 * for all three, 156 with File Transfer's 24, and 36 more with headphones'. */
int status_marks_width(const status_marks *m);

/* The buffer for the marks at `scale` pixels a unit, margin and all. */
void status_marks_size(const status_marks *m, float scale, int *w, int *h);

/* Draws the marks into px, w x h from status_marks_size, `stride` pixels a
 * row, clearing what is there: 0xAARRGGBB, straight alpha, as SDL's blending
 * wants. `theme` is DS_LIGHT or DS_DARK; anything else draws DS_LIGHT. */
void status_marks_render(uint32_t *px, int w, int h, int stride, float scale,
                         int theme, const status_marks *m);

/* The song's mark, which the top bar draws before the name of the menu
 * music or MP4's song while one plays (src/home.c's home_header): drawn as
 * the marks are, by signed distance at `scale` pixels a unit, into px,
 * STATUS_NOTE x STATUS_ROW units with STATUS_PAD round it, cleared first.
 * Playing, two eighth notes on one beam: heads of radius 3.5, stems and the
 * beam a 2.5-unit stroke as File Transfer's arrows; paused, the two bars of
 * a pause, 4.5 wide. In **accent-ink** (DESIGN.md 11.12). Mine, 2026-11-09:
 * the note of filled rectangles and discs it was "looks bad". */
#define STATUS_NOTE 24
void status_note_size(float scale, int *w, int *h);
void status_note_render(uint32_t *px, int w, int h, int stride, float scale,
                        int theme, bool paused);

#endif
