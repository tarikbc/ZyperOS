/* SPDX-License-Identifier: GPL-3.0-only */
/* "USB Device Connected": the launcher's half of plastron's usbrole.
 *
 * The Pixel 2's one USB-C port has no Type-C controller, so something that
 * wants power and gives none is a DAC, a Wi-Fi adapter, a keyboard - or a
 * computer whose port took the sink side, which looks exactly the same and
 * must never be fed (2026-11-10: a Mac waited 10, 16 and 34 s for power).
 * So usbrole never makes the port a host on its own: it says in
 * /run/usbrole/state which plug may be asked about, the shelf asks (Power
 * It, or It's a Computer, DESIGN.md 11.36), and Power It writes that plug's
 * number to /run/usbrole/host, which usbrole reads once and acts on only
 * while its own safety rules hold. It's a Computer, or B, leaves the port
 * as it is and says to plug the cable in again, as a computer then gives
 * power. The directory is root's alone (usbrole makes it 0700); the launcher
 * runs as root.
 *
 * The parsing, the once-per-plug rule and the files are here, apart from
 * SDL, so tools/usbpower-check.c reaches them; the platform says where the
 * files are (plat_usb_power, usbpeer.h). */
#ifndef USBPOWER_H
#define USBPOWER_H

#include <stdbool.h>

/* usbrole's state line: "plug=3 ask=1 pin=1 vbus=0 powered=0 role=device" */
typedef struct {
	bool     known;       /* read and whole */
	unsigned plug;        /* the plug that wants power, 0 none yet */
	bool     ask;         /* it may be asked about now */
	int      pin, vbus;   /* 1, 0, or -1 unreadable */
	bool     powered;     /* this plug had power in, or a computer */
	bool     host;        /* the port is a host */
} usb_power;

bool usbpower_parse(const char *text, usb_power *out);

/* `dir`/state read, and the request for `plug` written to `dir`/host
 * (beside it, then renamed, so usbrole reads it whole) */
bool usbpower_read(const char *dir, usb_power *out);
bool usbpower_request(const char *dir, unsigned plug);

/* Something on the cable waits for power that the handheld is not giving:
 * a computer whose port took the sink side, as USB File Transfer's Cable
 * step sees it (linksteps.h) */
bool usbpower_waits(const usb_power *p);

/* How long a plug may be asked about before it is: a DAC settles, a plug
 * that is about to give power has done so */
#define USBPOWER_ASK_MS 2000u

typedef struct {
	unsigned plug;        /* the plug being watched */
	unsigned since;       /* since when it may be asked about, 0 not */
	unsigned asked;       /* the last plug asked about */
	unsigned wplug, wsince, told;   /* the same for a plug that waits (below) */
} usbpower_follow_state;

/* What the shelf does about the plug now */
enum { USBPOWER_NOTHING, USBPOWER_ASK, USBPOWER_REPLUG };

/* One pass: USBPOWER_ASK and the plug in *plug when it may have been asked
 * about for USBPOWER_ASK_MS (once per plug). USBPOWER_REPLUG when a plug
 * waits for power and may not be asked about because a computer's power
 * was on the port seconds before (usbrole's 5 s new-plug rule: it is that
 * computer, plugged in again too soon, its port on the sink side): the shelf
 * says to plug it in again, once per plug, after the same 2 s. */
int usbpower_follow(usbpower_follow_state *f, const usb_power *p, unsigned now_ms,
                    unsigned *plug);

#endif
