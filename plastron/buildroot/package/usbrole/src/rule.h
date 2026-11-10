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
 * its own: the launcher asks the player (What did you plug in?: USB
 * Accessory, or Computer), and only USB Accessory for this plug lets it be
 * one; or the setting Automatic, after the plug's settle.
 *
 *  - Nothing is known for the first UR_GRACE_MS after usbrole starts:
 *    device.
 *  - Each time the pin goes to "wants power", that is a new plug, with a
 *    number. Nothing about the plug before it carries over: "it had power a
 *    moment ago" says nothing about what is plugged in now (a cable swapped
 *    for an adapter in a second, a wall charger), measured 2026-11-10.
 *  - A plug may be asked about (can_ask) while the pin says so, no power
 *    comes in, no charger detection (SDP, CDP, DCP) is up, every reading was
 *    made, and no computer has shown up on the bus during it. Host only
 *    then, and only on the launcher's answer for that plug (ur_grant), or,
 *    with the setting Power Automatically (ur_state.auto_mode), once it has
 *    held UR_SETTLE_MS.
 *  - Once host, the power reading is the port's own, so it is not asked:
 *    the pin going back, or a computer turning up on the bus, ends it, and a
 *    computer keeps that plug device until it ends.
 *
 * Pure: no files, no clock of its own. tests/rule-check.c replays plugs.
 */
#ifndef USBROLE_RULE_H
#define USBROLE_RULE_H

#define UR_GRACE_MS   3000u
#define UR_SETTLE_MS  2000u     /* Power Automatically: a plug's wait first */

typedef struct {
	int pin;        /* the PHY's USB-HOST cable: 1 wants power, 0 not, -1 unreadable */
	int vbus;       /* power coming in (rk817-charger online): 1, 0, -1 unreadable */
	int charger;    /* the PHY saw a computer's port or a charger: SDP, CDP or DCP */
	int computer;   /* a computer on the bus as a USB device (only while host) */
} ur_sense;

typedef struct {
	unsigned start;        /* the first decision */
	unsigned plug;         /* the number of the plug that wants power, 0 none yet */
	unsigned granted;      /* the plug the launcher said USB Accessory for, 0 none */
	unsigned since;        /* since when the plug may be asked about, 0 not */
	int      pin_was;      /* the pin at the last decision */
	int      computer;     /* a computer showed up on the bus during this plug */
	int      can_ask;      /* the plug may be asked about now */
	int      host;         /* what it asks for now */
	int      auto_mode;    /* Power Automatically, as the launcher says */
	int      started;
} ur_state;

enum { UR_DEVICE = 0, UR_HOST = 1 };

/* The role the port should have now. *again_ms is when to look again with
 * no event from the kernel (the grace or a settle running out), 0 for not. */
int ur_decide(ur_state *s, const ur_sense *in, unsigned now_ms, unsigned *again_ms);

/* The launcher's USB Accessory for plug `plug`: kept only when it is the
 * plug that may be asked about now; the next ur_decide makes it host. True
 * when kept. */
int ur_grant(ur_state *s, unsigned plug);

#endif
