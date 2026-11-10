/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The rule usbrole follows: when the one USB-C port may be a host.
 *
 * Host means the Pixel 2 puts power out on the port, so it must never happen
 * while anything puts power in (a computer or a charger can damage the
 * board), and it must never feed a computer. The Pixel 2 has no Type-C port
 * controller: the "pin" is the USB PHY's ID input, and a Mac's port that
 * takes the sink side looks exactly like a DAC that wants power. Measured on
 * 2026-11-10 (usbrole's data build, 9 plugs of a Mac): a Mac on its sink
 * side waits 10, 16, 34 seconds and more for power, the ID line shows one
 * edge for it as for a DAC, and the Mac shows itself only once it is fed
 * (as a USB device, "Product: Mac", 1.9 s after host). Waiting cannot tell
 * them apart and feeding is the only test, so the port is never a host on
 * its own: the launcher asks the player ("USB Device Connected": Power It,
 * or It's a Computer), and only "Power It" for this plug lets it be one.
 *
 *  - Nothing is known for the first UR_GRACE_MS after usbrole starts:
 *    device.
 *  - Power in now, a charger detection (SDP, CDP, DCP), a reading that
 *    fails, or a computer seen as a USB device: device, and this plug is a
 *    powered one until the port has been empty for UR_UNPLUG_MS, counted
 *    from when the power went (a detection that lingers after it is the
 *    last plug's).
 *  - Each time the pin goes to "wants power", that is a new plug, with a
 *    number. It may be asked about (ur_can_ask) while the pin says so with
 *    nothing coming in, on a plug that is not a powered one. Host only when
 *    the launcher said Power It for that number (ur_grant), and only while
 *    all of that still holds.
 *  - Once host, the power reading is the port's own, so it is not asked:
 *    the pin going back, or a computer turning up on the bus, ends it, and
 *    a computer makes the plug a powered one.
 *
 * Pure: no files, no clock of its own. tests/rule-check.c replays plugs.
 */
#ifndef USBROLE_RULE_H
#define USBROLE_RULE_H

#define UR_GRACE_MS   3000u
#define UR_UNPLUG_MS  5000u

typedef struct {
	int pin;        /* the PHY's USB-HOST cable: 1 wants power, 0 not, -1 unreadable */
	int vbus;       /* power coming in (rk817-charger online): 1, 0, -1 unreadable */
	int charger;    /* the PHY saw a computer's port or a charger: SDP, CDP or DCP */
	int computer;   /* a computer on the bus as a USB device (only while host) */
} ur_sense;

typedef struct {
	unsigned start;        /* the first decision */
	unsigned quiet_since;  /* when the port went empty, 0 for not */
	unsigned plug;         /* the number of the plug that wants power, 0 none yet */
	unsigned granted;      /* the plug the launcher said Power It for, 0 none */
	int      pin_was;      /* the pin at the last decision */
	int      powered;      /* this plug has had power in, or a computer */
	int      can_ask;      /* the plug may be asked about now */
	int      host;         /* what it asks for now */
	int      started;
} ur_state;

enum { UR_DEVICE = 0, UR_HOST = 1 };

/* The role the port should have now. *again_ms is when to look again with
 * no event from the kernel (the grace running out), 0 for not. */
int ur_decide(ur_state *s, const ur_sense *in, unsigned now_ms, unsigned *again_ms);

/* The launcher's Power It for plug `plug`: kept only when it is the plug
 * that may be asked about now; the next ur_decide makes it host. True when
 * kept. */
int ur_grant(ur_state *s, unsigned plug);

#endif
