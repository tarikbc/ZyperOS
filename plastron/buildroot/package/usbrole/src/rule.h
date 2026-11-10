/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The rule usbrole follows: when the one USB-C port may be a host.
 *
 * Host means the Pixel 2 puts power out on the port, so it must never happen
 * while anything puts power in: a computer or a charger on the other end can
 * damage the board. And a computer must find the handheld on the first plug.
 * On 2026-11-10 the port went host twice in the first 10 seconds of a boot
 * with a Mac on the cable: the pin said "wants power" with no power in at
 * those moments (a Mac's USB-C port tries both roles), the Mac then showed up
 * as a USB device ("Product: Mac"), and the cable had to be plugged in again
 * three times before the Mac saw the handheld. So:
 *
 *  - Nothing is known for the first UR_GRACE_MS after usbrole starts (the
 *    cable, the charger and a computer's port all settle at boot): device.
 *  - Power in now, a charger detection that saw a computer's port or a
 *    charger (SDP, CDP, DCP), a power reading that fails, or a computer
 *    seen as a USB device: device, and this plug is a powered one, device
 *    until the port has been empty for UR_UNPLUG_MS (a new plug).
 *  - Host only when the pin has said "wants power" with nothing coming in,
 *    unbroken, for UR_HOLD_MS. A partner that tries both roles shows power
 *    within that, and is a powered plug from then on.
 *  - Once host, the power reading is the port's own, so it is not asked:
 *    the pin going back, or a computer turning up on the bus, ends it.
 *
 * Pure: no files, no clock of its own. tests/rule-check.c replays plugs.
 */
#ifndef USBROLE_RULE_H
#define USBROLE_RULE_H

#define UR_GRACE_MS   3000u
#define UR_HOLD_MS    2000u
#define UR_UNPLUG_MS  2000u
#define UR_STEP_MS     100u

typedef struct {
	int pin;        /* the PHY's USB-HOST cable: 1 wants power, 0 not, -1 unreadable */
	int vbus;       /* power coming in (rk817-charger online): 1, 0, -1 unreadable */
	int charger;    /* the PHY saw a computer's port or a charger: SDP, CDP or DCP */
	int computer;   /* a computer on the bus as a USB device (only while host) */
} ur_sense;

typedef struct {
	unsigned start;        /* the first decision */
	unsigned since;        /* when the host condition began holding, 0 for not */
	unsigned quiet_since;  /* when the port went empty, 0 for not */
	int      powered;      /* this plug has had power in, or a computer */
	int      host;         /* what it asks for now */
	int      started;
} ur_state;

enum { UR_DEVICE = 0, UR_HOST = 1 };

/* The role the port should have now. *again_ms is when to look again with
 * no event from the kernel (a hold or the grace running out), 0 for not. */
int ur_decide(ur_state *s, const ur_sense *in, unsigned now_ms, unsigned *again_ms);

#endif
