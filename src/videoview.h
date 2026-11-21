/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_VIDEOVIEW_H
#define ZYPEROS_VIDEOVIEW_H

/* A shelf as its video snaps (MENU > View > Video, DESIGN.md 11.57): the
 * focused game's snap on the whole screen, its name in a card at the top
 * left, and its covers in a band at the foot with the keys. The fourth View,
 * after Covers, Grid and List; my pick in the Library lab, 2026-11-19: the
 * snap starts at once on every move, with no rest first, and "when rested
 * for more than 2s, the bottom bar goes down (into a compact mode) so you
 * can see the full preview".
 *
 * So after **hold-band** on a game whose snap plays, with no press, the band
 * slides down in **move** to its strip: a rail at the foot that says where
 * the game is in the shelf. Any press brings it back up in **quick**, and
 * does what it does. A game with no snap shows its card whole above the
 * band, which stays up.
 *
 * Left and Right step along the covers as the cover row's do, and the row
 * glides as the cover row does (games_row, the same motion). No I/O: the
 * caller hands it its facts and the snap's picture (src/snapshelf.h), as
 * games_draw's are handed. */
#include "coverflow.h"
#include "games.h"

#include "ds/layout.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* The band's top while it is up (a layout token, tools/designdef.py), and
 * the strip's while it is down: every screen's foot, the keys' line */
#define VV_BAND_TOP  DS_VIDEO_BAND_TOP
#define VV_STRIP_TOP DS_FOOT_Y

/* Where the band is, 0 up to 1 down, and where it is going: its slide
 * began at `t` from `from` and takes `ms`; the last press, for the rest */
typedef struct {
	bool     set;
	float    from, to;
	uint32_t t, ms, touched;
	/* The snap was playing the frame before: its start begins the rest
	 * again, so the band stays up hold-band once the snap shows (a game
	 * just ended, its snap starting again, keeps it up as long) */
	bool     was_playing;
} vv_motion;

typedef struct {
	const char *heading;          /* the system, and the folder it is in */
	const char *title;            /* the focused game's name */
	int count, cursor;
	unsigned accent;              /* the shelf's color, for the card's dot */
	/* The focused entry's facts, as the cover row has them */
	const char *played, *origin;
	bool has_achievements, resumable, install, inside;
	const char *cheats;   /* "2 cheats on" (src/cheats_screen.h), NULL or "" for none */
	/* Splore's entries (src/games.h): the offer to add PICO-8, and one that
	 * opens rather than plays; A says so and there is no Y, as on the others */
	bool howto, opens;
	int earned, total;
	int folder;                   /* the focused entry is a folder of this many */
	/* The word by START: the View it turns to (Covers) */
	const char *start_word;
	/* The focused game's snap (src/snapshelf.h): its picture, its shape,
	 * the part of its texture it is in, and how far the card has turned
	 * into it, 0 to 1; `playing` once it has, which starts the rest */
	SDL_Texture *snap;
	int snap_w, snap_h;
	SDL_Rect snap_src;
	float snap_k;
	bool playing;
	/* A shot's band, 0 up to 1 down; below 0 the band's own */
	float band;
	/* A shelf's own keys at the foot, A's among them (the Screenshots
	 * shelf's, DESIGN.md 11.68); NULL and 0 for a games shelf's */
	const key_hint *foot;
	int nfoot;
	/* A shot: at rest nothing covers the picture. The band goes down off
	 * the screen with no strip, and the name's card goes up off the top
	 * with it (2026-11-20: "the top card that contains the name and etc
	 * should animate and disappear as well. so we can see the whole image.
	 * also the collapsed footer can be nothing") */
	bool clear;
} vv_info;

/* Reset: the band up, rested from `now` */
void vv_reset(vv_motion *m, uint32_t now);

/* A press: the band back up in quick, the rest from `now` */
void vv_touch(vv_motion *m, uint32_t now);

/* At rest now: the band down in move at once, as the rest would take it
 * after hold-band (A in the Screenshots viewer, 2026-11-20: "it goes
 * straight to full without needing to wait for it to collapse"); the next
 * press brings it back up */
void vv_rest(vv_motion *m, uint32_t now);

/* Where the band is at `now`, 0 up to 1 down, eased */
float vv_band(const vv_motion *m, uint32_t now);

/* One frame, the covers placed by `row` (the cover row's motion, which the
 * caller steps as for the cover row). What it returns is when to draw
 * again, as games_draw's: the band's slide, or the end of its rest. */
uint32_t vv_draw(vv_motion *m, const games_row *row, SDL_Renderer *r, const vv_info *in,
                 cf_tex_fn texture, void *ctx, uint32_t now, uint32_t phase);

#endif
