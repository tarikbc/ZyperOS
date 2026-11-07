/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Zyper's moves beside the ZyperOS menu's heading. See src/zyperrig.h.
 *
 * The Zyper lab's Rig (its lab.js), for the one place I picked: two
 * parts that ease from where they are to where they are sent - the head's
 * tilt and the tongue - and three moves on their own clocks that are added
 * on top, the hop, the pant and the blink, with the breath under all of it.
 * The parts ease on DESIGN.md 8.1's **out**, but for the failure's tilt,
 * which takes his own small overshoot (8.3: Zyper alone may overshoot and
 * settle). Every time is a token of src/design.h (DESIGN.md 8.2) but the
 * first blink's window, which is the lab's. */
#include "zyperrig.h"

#include <math.h>

#include "clamp.h"
#include "design.h"

/* At rest: ZYPER_TILT_DEG, src/zypermark.h's, which this cannot include
 * (it holds his vertices); tools/checkmark.py holds the two to one value */
#define REST_TILT    10.0f
/* The breath: this far up at the middle of DS_MS_ZYPER_BREATHE */
#define BREATH_UP    2.0f
/* A failure: the head this far, held DS_MS_HOLD_ZYPER_FAIL, then back */
#define FAIL_TILT    18.0f
/* The hop, DESIGN.md 8.3: up and back for the first LAND of it, then a
 * squash on the ground for the rest */
#define HOP_UP       24.0f
#define HOP_SQUASH   0.06f
#define HOP_LAND     0.72f
/* The boot loop's pant: the tongue this much in, three beats in
 * DS_MS_ZYPER_PANT */
#define PANT_DEPTH   0.18f
#define PANT_BEAT_MS ((float)DS_MS_ZYPER_PANT / 3.0f)
/* A blink, DS_MS_ZYPER_BLINK: shut in SHUT, held to HELD, open by its end;
 * one every DS_MS_ZYPER_BLINK_GAP to twice it, the first sooner, FIRST to
 * FIRST + FIRST_RND in, so he is seen to blink soon after the menu opens */
#define BLINK_SHUT   (DS_MS_ZYPER_BLINK * 3u / 8u)
#define BLINK_HELD   (DS_MS_ZYPER_BLINK / 2u)
#define FIRST_MS     1200u
#define FIRST_RND    2500u
/* What "never" is: this long before the rig was made */
#define LONG_AGO     0x10000u

enum { BOOK_NONE, BOOK_HOP, BOOK_SETTLE };

/* His overshoot: a back-out that goes about 9% past and comes back. Not one
 * of DESIGN.md 8.1's curves: those never overshoot, and this is Zyper's own,
 * the one thing in the interface that may (8.3). */
static float back(float t)
{
	const float s = 1.6f, u = t - 1.0f;

	return 1.0f + (s + 1.0f) * u * u * u + s * u * u;
}

float zr_overshoot(float t)
{
	return back(clamp01(t));
}

static float tween_at(const zr_tween *w, unsigned now)
{
	float k;

	if (w->ms == 0) return w->to;
	k = clamp01((float)(int)(now - w->t0) / (float)w->ms);
	return w->from + (w->to - w->from) * (w->back ? back(k) : ds_out(k));
}

/* From wherever it is at `now`. */
static void send(zr_rig *z, int part, unsigned now, float to, unsigned ms, bool bk)
{
	zr_tween *w = &z->part[part];

	w->from = tween_at(w, now);
	w->to = to;
	w->t0 = now;
	w->ms = ms;
	w->back = bk;
}

static void settle(zr_rig *z, unsigned now, unsigned ms)
{
	send(z, ZR_TILT, now, REST_TILT, ms, false);
	send(z, ZR_TONGUE, now, 1.0f, ms, false);
}

static unsigned dice(zr_rig *z, unsigned n)
{
	z->seed = z->seed * 1103515245u + 12345u;
	return (z->seed >> 8) % (n + 1);
}

void zr_init(zr_rig *z, unsigned now, unsigned seed)
{
	int i;

	for (i = 0; i < ZR_PARTS; i++) z->part[i] = (zr_tween){ 0 };
	settle(z, now, 0);
	z->hop_at = z->pant_at = z->blink_at = now - LONG_AGO;
	z->pant_ms = DS_MS_ZYPER_PANT;
	z->booked = BOOK_NONE;
	z->booked_at = now;
	z->seed = seed;
	z->next_blink = now + FIRST_MS + dice(z, FIRST_RND);
}

void zr_open(zr_rig *z, unsigned now)
{
	settle(z, now, 0);
	z->booked = BOOK_HOP;
	z->booked_at = now + DS_MS_HOLD_ZYPER_OPEN;
}

void zr_press(zr_rig *z, unsigned now)
{
	if (z->booked == BOOK_NONE) return;
	z->booked = BOOK_NONE;
	settle(z, now, DS_MS_ZYPER_TILT);
}

void zr_works(zr_rig *z, unsigned now)
{
	unsigned since = now - z->pant_at;

	/* A hop has no target to send it to, so one under way is the answer
	 * already: starting it over would drop him to the ground mid-air */
	if (now - z->hop_at >= DS_MS_ZYPER_HOP) z->hop_at = now;
	if (since >= z->pant_ms) {
		z->pant_at = now;
		z->pant_ms = DS_MS_ZYPER_PANT;
	} else {
		/* Whole beats, so it ends with the tongue out as a pant does */
		z->pant_ms = (unsigned)(ceilf((float)(since + DS_MS_ZYPER_PANT) / PANT_BEAT_MS) *
		                        PANT_BEAT_MS + 0.5f);
	}
}

void zr_fails(zr_rig *z, unsigned now)
{
	send(z, ZR_TONGUE, now, 0.0f, DS_MS_ZYPER_TONGUE, false);
	send(z, ZR_TILT, now, FAIL_TILT, DS_MS_ZYPER_TILT, true);
	z->booked = BOOK_SETTLE;
	z->booked_at = now + DS_MS_HOLD_ZYPER_FAIL;
}

void zr_tick(zr_rig *z, unsigned now)
{
	/* At the moment it was booked for, not the frame that noticed: a frame
	 * late is not a hop 16 ms late, and a still for --zyper is the same
	 * still at any frame rate. */
	if (z->booked != BOOK_NONE && (int)(now - z->booked_at) >= 0) {
		int what = z->booked;
		unsigned at = z->booked_at;

		z->booked = BOOK_NONE;
		if (what == BOOK_HOP) z->hop_at = at;
		else settle(z, at, DS_MS_ZYPER_TILT);
	}
	/* On its own time too, for the same reasons; one a gap that a slow
	 * frame or a screen over the menu let go by is long over, and only the
	 * last is kept */
	while ((int)(now - z->next_blink) >= 0) {
		z->blink_at = z->next_blink;
		z->next_blink = z->blink_at + DS_MS_ZYPER_BLINK_GAP + dice(z, DS_MS_ZYPER_BLINK_GAP);
	}
}

zr_pose zr_pose_at(const zr_rig *z, unsigned now)
{
	zr_pose p;
	unsigned since;

	p.tilt = tween_at(&z->part[ZR_TILT], now);
	p.tongue = tween_at(&z->part[ZR_TONGUE], now);
	p.ears = 0.0f;
	p.blink = 0.0f;
	p.dy = 0.0f;
	p.squash = 1.0f;
	p.mouth = 0.0f;

	since = now - z->pant_at;
	if (since < z->pant_ms)
		p.tongue *= 1.0f - PANT_DEPTH *
		            (0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)since / PANT_BEAT_MS));

	since = now - z->hop_at;
	if (since < DS_MS_ZYPER_HOP) {
		float u = (float)since / DS_MS_ZYPER_HOP;

		if (u < HOP_LAND)
			p.dy -= HOP_UP * sinf((float)M_PI * u / HOP_LAND);
		else
			p.squash = 1.0f - HOP_SQUASH *
			           sinf((float)M_PI * (u - HOP_LAND) / (1.0f - HOP_LAND));
	}

	since = now - z->blink_at;
	if (since < DS_MS_ZYPER_BLINK)
		p.blink = since < BLINK_SHUT ? ds_turn((float)since / BLINK_SHUT)
		        : since < BLINK_HELD ? 1.0f
		        : 1.0f - ds_turn((float)(since - BLINK_HELD) /
		                         (DS_MS_ZYPER_BLINK - BLINK_HELD));

	p.dy -= BREATH_UP * (0.5f - 0.5f * cosf(2.0f * (float)M_PI *
	                                        (float)(now % DS_MS_ZYPER_BREATHE) /
	                                        DS_MS_ZYPER_BREATHE));
	return p;
}
