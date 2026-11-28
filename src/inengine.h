/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_INENGINE_H
#define ZYPEROS_INENGINE_H

/* The one engine from what a handheld reports to the launcher's buttons
 * (DESIGN.md 11.9, "One input model"). Every device file gives it its raw
 * events (ie_key, ie_abs, ie_sync), a keyboard and desk mode their buttons
 * (ie_button), and it does the rest the same way on every handheld, from its
 * profile's table (src/device/devinput.h) and chords (dev_chords):
 *
 * - what the hand holds, each logical button (the sink's `hand`): a key, a
 *   stick's push, both keyboards; the glyphs light from this;
 * - MENU held is held, and opens nothing until it is let go; a button
 *   pressed under it is MENU's chord when the launcher takes it (src/
 *   menukeys.h), its press and its let-go the chord's alone, and MENU then
 *   opens nothing; the volume keys under it are brightness where the
 *   profile says so (DEV_BRIGHT_MENU_VOL);
 * - L2 and R2 as taps let go where the profile says so (lr2_taps), both
 *   together the battery's lights (battery_chord);
 * - a stick's four directions as buttons, at stickpad's lines past its dead
 *   middle (src/stickpad.h), the left one with the d-pad's keys where its
 *   table says, held while either holds.
 *
 * Before this, each device file did its own share of it, and three
 * handhelds drifted: the Brick's MENU skipped the chord rule (#496), the
 * Pixel 2's capture screen lost MENU's glyph (#464), and the R36S's L2,
 * a plain key there, skipped it too (FN+L2 was "That's L2." in the
 * tutorial, and no lock key in Hotkeys). Now no device file decides
 * anything about a button.
 *
 * Pure: no SDL. src/platform.c keeps the one engine of the launcher and
 * gives it in_state as its sink; tools/inmodel-check.c drives it for every
 * profile. */

#include <stdbool.h>
#include <stdint.h>

#include "device/profile.h"
#include "inbutton.h"
#include "stickpad.h"

/* Where a press comes from: the handheld's own keys, a keyboard or desk
 * mode, a stick */
enum { IE_SRC_PAD, IE_SRC_KEYS, IE_SRC_STICK_L, IE_SRC_STICK_R };

/* What the engine says, as it goes */
typedef struct {
	void *ctx;
	/* A button's edge, pressed as it goes down (plat_set_btn) */
	void (*set)(void *ctx, in_button b, bool down);
	/* MENU held or let go, no press: its press is `press`, as it is let go */
	void (*hold)(void *ctx, in_button b, bool down);
	void (*press)(void *ctx, in_button b);
	/* What the hand holds, whatever the button does with it */
	void (*hand)(void *ctx, in_button b, bool down);
} ie_sink;

typedef struct {
	const device_profile *p;
	/* MENU's chord takes `b` (src/menukeys.h); NULL takes none */
	bool (*take)(in_button b);
	/* L2 and R2 together, where battery_chord: the lights show the level */
	void (*battery)(void);

	uint8_t   src[IN_COUNT];        /* 1 << IE_SRC_* holding each */
	bool      menu, menu_chorded;   /* MENU held, and a chord made under it */
	in_button vol_as[2];            /* what VOL+ and VOL- went down as */
	bool      taken[IN_COUNT];      /* gone down as MENU's chord */
	in_button due;                  /* the chord to carry out, once */
	bool      lr2[2], lr2_chorded;  /* L2 and R2 held, as taps */
	bool      tap[IN_COUNT];        /* down for this poll alone */
	struct { int value, min, max, flat; bool known; } axis[DEV_STICKS][2];
	bool      moved;
	stickpad  stick[DEV_STICKS];
} inengine;

/* For `p`'s handheld, nothing held */
void ie_init(inengine *e, const device_profile *p);
/* The button `code` is on `p`, IN_NONE for a code it does not send */
in_button ie_code(const device_profile *p, int code);

/* An EV_KEY: 1 down, 0 up, 2 a repeat (nothing). False for a code the
 * profile has no button for, which did nothing */
bool ie_key(inengine *e, const ie_sink *s, int code, int value);
/* A button from a keyboard or desk mode, down or up */
void ie_button(inengine *e, const ie_sink *s, in_button b, bool down);

/* Whether one of the profile's sticks has the EV_ABS `code` */
bool ie_wants_axis(const device_profile *p, int code);
/* An axis's range and flat, as the kernel gives them (EVIOCGABS), and where
 * it is: before an axis is told, its stick moves nothing */
void ie_axis(inengine *e, int code, int value, int min, int max, int flat);
/* An EV_ABS where it is now, and at the report's end (SYN_REPORT, or the
 * read's) each stick taken as a whole */
void ie_abs(inengine *e, int code, int value);
void ie_sync(inengine *e, const ie_sink *s);

/* The buttons the hand holds for a press of `b` on `p`, into `out` (two at
 * most), how many: `b` itself, but for brightness made of MENU and a volume
 * key where the profile says so (DEV_BRIGHT_MENU_VOL). The glyphs light
 * these for that press (tools/inmodel-check.c holds them to it). */
int ie_made_of(const device_profile *p, in_button b, in_button out[2]);

/* At each poll's start: a tap of the last one ends */
void ie_poll_begin(inengine *e, const ie_sink *s);
/* At its end: the chord taken, to carry out, IN_NONE for none */
in_button ie_due(inengine *e);
/* Everything held forgotten, no edge said: a flush, whose let-gos are
 * never read. Where the axes are stays. */
void ie_forget(inengine *e);

#endif
