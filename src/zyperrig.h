/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ZYPERRIG_H
#define ZYPEROS_ZYPERRIG_H

/* Zyper's moves beside the ZyperOS menu's heading (DESIGN.md 8.3 and 10), as
 * plain numbers: where his head and tongue are going, when he last
 * hopped, panted and blinked, and what is booked to happen next. src/main.c
 * draws the pose this answers (draw_zyper_pose); nothing here knows a
 * renderer or a clock, so tools/zyperrig-check.c holds the motion to its
 * times.
 *
 * As I picked it in the Zyper lab (2026-11-04): he hops as the menu
 * opens, hops and pants when something in it succeeds, and when something
 * fails cocks his head further with his tongue in, then settles; a move does
 * nothing to him. At rest he breathes, a 2-unit bob every 2.4 s, and blinks
 * every 3 to 6 s. A reaction starts from wherever he is, and a press drops
 * whatever was booked and starts him back from there, so nothing queues
 * (DESIGN.md 8.2).
 *
 * Times are milliseconds on any clock that only goes forward: plat_now_ms's
 * in the menu, the shot's chosen moment for --zyper. Every comparison is of
 * a difference, so the clock may wrap. */

#include <stdbool.h>

/* The highest he goes above where he stands, in units: a hop's 24 at the top
 * of a breath's 2. The menu stands him this far under its frame, so a hop
 * never crosses it. */
#define ZR_REACH 26.0f

/* One part's way from where it was to where it is going. */
typedef struct {
	float    from, to;
	unsigned t0, ms;      /* sent at t0, there ms later; 0 ms is there now */
	bool     back;        /* his small overshoot (DESIGN.md 8.3), not out */
} zr_tween;

enum { ZR_TILT, ZR_TONGUE, ZR_PARTS };

typedef struct {
	zr_tween part[ZR_PARTS];
	unsigned hop_at, pant_at, blink_at;   /* when each last began */
	unsigned pant_ms;                     /* and how long the pant runs */
	unsigned next_blink;
	/* The one thing booked for later, and when: a hop as the menu opens, or
	 * the way back from a failure. One, because nothing queues. */
	int      booked;
	unsigned booked_at;
	unsigned seed;                        /* the blinks' own dice */
} zr_rig;

/* What draw_zyper_pose is handed. */
typedef struct {
	float tilt;     /* degrees, ZYPER_TILT_DEG at rest; the high brow follows */
	float tongue;   /* 1 out, 0 in */
	float ears;     /* degrees each ear tip swings out about its fold: the
	                 * menu's rig leaves them still, at 0; draw_zyper_pose
	                 * swings them for whoever moves them (the tutorial) */
	float blink;    /* 0 open, 1 shut */
	float dy;       /* units, up negative: the hop and the breath */
	float squash;   /* his height about his chin, 1 at rest: a hop's landing */
	float mouth;    /* 0 open, 1 shut: asleep, on the charging screen; the
	                 * rig leaves it open */
} zr_pose;

/* At rest at `now`, nothing booked, the first blink 1.2 to 3.7 s on. `seed`
 * is any number; the same one blinks the same way. */
void zr_init(zr_rig *z, unsigned now, unsigned seed);
/* The menu's first frame: back at rest, and a hop DS_MS_HOLD_ZYPER_OPEN on,
 * as the panel arrives. */
void zr_open(zr_rig *z, unsigned now);
/* Any press in the menu. With something booked, the booking goes and he
 * starts back to rest from where he is; with nothing, it changes nothing:
 * a move is not news. Called before zr_works and zr_fails, which answer a
 * press. */
void zr_press(zr_rig *z, unsigned now);
/* Something in the menu succeeded, a setting changed or a sign-in: he hops
 * and pants. A hop under way runs on, and a pant under way goes on from
 * where it is to the end of the beat a second on, so a held Left or Right,
 * a success every 90 ms, starts nothing over: he hops again once he is
 * down, and pants until a second after the last. */
void zr_works(zr_rig *z, unsigned now);
/* Something failed: the tongue in and the head cocked to 18 degrees, held,
 * and back to rest DS_MS_HOLD_ZYPER_FAIL on unless a press comes first. */
void zr_fails(zr_rig *z, unsigned now);
/* Once a frame, before the pose is asked: runs what was booked for `now`
 * or earlier, and the blink that is due, each at the moment it was due. */
void zr_tick(zr_rig *z, unsigned now);
/* Where he is at `now`. */
zr_pose zr_pose_at(const zr_rig *z, unsigned now);

/* His overshoot, the one curve in the interface that goes past (DESIGN.md
 * 8.3): 0 at 0, about 9% past near the end, 1 at 1; clamped. The failure's
 * tilt eases on it, and the tutorial pops him in by scale on it. */
float zr_overshoot(float t);

#endif
