/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_POWERSCREENS_H
#define ZYPEROS_POWERSCREENS_H

/* The two screens around the power: the start with a battery too low
 * (DESIGN.md 11.24, src/lowbatt.h's rules) and the charging screen
 * (DESIGN.md 11.25, src/charging.h's). Each is described in
 * src/powerscreens.c. */
#include "app.h"
#include "charging.h"
#include "lowbatt.h"
#include "zyperrig.h"

#include <stdbool.h>

/* The low-battery screen's state; the shots set it up (main.c) */
typedef struct {
	lowbatt  l;
	zr_rig   rig;
	zr_pose  pose;        /* the last drawn, where the send-off starts */
	unsigned cocked_at;   /* his head last cocked (zr_fails) */
	int      pct;         /* the reading the screen came up on */
} lowbatt_view;
extern lowbatt_view g_lb;

/* The charging screen's state; the shots set it up too */
typedef struct {
	ch_screen  s;
	ch_eta     eta;
	ch_reading r;
	ch_kind_wait kind;
	bool       valid;
	unsigned   read_at, shown_at;
	char       speed[96], when[96];
} charging_view;
extern charging_view g_chg;

/* One frame of the low-battery screen, and of its send-off */
void lowbatt_draw(app *a, unsigned now, const zr_pose *p, float dim, float k,
                  float bar_k);
void lowbatt_off_frame(app *a, const zr_pose *from, unsigned t, unsigned now);
/* Before the first frame of the start: true when it goes on */
bool lowbatt_gate(app *a);

/* One frame of the charging screen */
void chg_draw(app *a, unsigned now, float dim, float k);
/* A start the charger made: true when POWER held goes on with it */
bool charge_boot_gate(app *a);
/* At a power-off: the charging screen in place of black, on the charger */
bool charge_instead(app *a);

#endif
