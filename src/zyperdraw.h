/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ZYPERDRAW_H
#define ZYPEROS_ZYPERDRAW_H

/* Zyper and the heart as the launcher draws them, Zyper's shadow on Beard,
 * and the battery's reading with Auto Off's question that every screen
 * asks. Each is described in src/zyperdraw.c. */
#include "app.h"
#include "status.h"
#include "zyperrig.h"

#include <SDL.h>
#include <stdbool.h>

/* The heart, about (cx, cy), `rad` its radius */
void draw_heart(SDL_Renderer *r, float cx, float cy, float rad,
                SDL_Color col);
/* Zyper about his center, `h` tall, in the rig's pose `p` */
void draw_zyper_pose(SDL_Renderer *r, float cx, float cy, float h,
                     const zr_pose *p, float brow, float dim, Uint8 alpha);
/* ...in a tilt and a tongue alone */
void draw_zyper(SDL_Renderer *r, float cx, float cy, float h,
                float tilt, float tongue, float dim, Uint8 alpha);
/* His shadow, on Beard only, drawn before him; and its textures let go */
void zyper_shadow_draw(SDL_Renderer *r, float cx, float cy, float h,
                       float tilt, float k);
void zyper_shadows_free(void);

/* The battery, as the corner, Diatom's disc and Auto Off read it */
bool battery_now(int *pct, bool *charging);
bool battery_empty(void);
/* Minutes of use left before the battery reaches BATT_EMPTY_PCT, where the
 * handheld saves and powers off, from how fast it has been falling
 * (src/battery_eta.h); -1 while there is none to give: off the cable, fewer
 * than two falls seen since the start or the cable came out. */
int battery_minutes_left(void);
/* Has the player been away long enough to power the device off? */
bool idle_due(app *a);

/* 0 when the battery is fine or charging, 1 when it is low, 2 when it is
 * critical: status_batt_level's, for Diatom's disc over a game. */
static inline int battery_level(void)
{
	int pct;
	bool charging;

	return battery_now(&pct, &charging) ? status_batt_level(pct, charging) : 0;
}

/* Both halves of Auto Off poll the same cable state the corner shows. The
 * shelf asks even on idle passes, so plugging in does not wait for a draw. */
static inline bool on_charger(void)
{
	bool charging;

	battery_now(NULL, &charging);
	return charging;
}

#endif
