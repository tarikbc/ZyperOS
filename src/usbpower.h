/* SPDX-License-Identifier: GPL-3.0-only */
/* "What did you plug in?": the launcher's half of plastron's usbrole.
 *
 * The Pixel 2's one USB-C port has no Type-C controller, so something that
 * wants power and gives none is a DAC, a Wi-Fi adapter, a keyboard - or a
 * computer whose port took the sink side, which looks exactly the same and
 * must never be fed (2026-11-10: a Mac waited 10, 16 and 34 s for power).
 * So usbrole never makes the port a host on its own: it says in
 * /run/usbrole/state which plug may be asked about, the shelf asks (USB
 * Accessory, or Computer, DESIGN.md 11.36), and USB Accessory writes that plug's
 * number to /run/usbrole/host, which usbrole reads once and acts on only
 * while its own safety rules hold. Computer, or B, leaves the port
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

/* usbrole's state line:
 * "plug=3 ask=1 pin=1 vbus=0 computer=0 role=device auto=0 paused=0"
 * (paused= is read as 0 when a usbrole from before it leaves it out) */
typedef struct {
	bool     known;       /* read and whole */
	unsigned plug;        /* the plug that wants power, 0 none yet */
	bool     ask;         /* it may be asked about now */
	int      pin, vbus;   /* 1, 0, or -1 unreadable */
	bool     computer;    /* a computer showed up on the bus during this plug */
	bool     host;        /* the port is a host */
	bool     automatic;   /* usbrole powers accessories by itself (the setting) */
	bool     paused;      /* ...but not now: a computer was just on the port,
	                       * so a plug is asked about (rule.h) */
} usb_power;

bool usbpower_parse(const char *text, usb_power *out);

/* `dir`/state read, and the request for `plug` written to `dir`/host
 * (beside it, then renamed, so usbrole reads it whole) */
bool usbpower_read(const char *dir, usb_power *out);
bool usbpower_request(const char *dir, unsigned plug);
/* The setting for usbrole, MENU > USB Accessories: "auto" or "ask" in
 * `dir`/mode, written beside and renamed as the request is */
bool usbpower_mode(const char *dir, bool automatic);

/* Something on the cable waits for power that the handheld is not giving:
 * a computer whose port took the sink side, as USB File Transfer's Cable
 * step sees it (linksteps.h) */
bool usbpower_waits(const usb_power *p);

/* The message an answer leaves up, over every screen but a game, until its
 * plug ends (DESIGN.md 11.36): Computer's, to plug the cable in again, as a
 * computer's port takes either side at random; or Phone's, what the phone
 * must do. A phone takes the side that waits for power at every plug (a
 * phone's port prefers it, so a replug never helps: 2026-11-13, an iPhone
 * and an Android phone asked again and again), and the handheld, with no
 * Type-C controller and no Power Delivery, cannot ask it to swap. So Phone
 * powers it: the handheld is the USB host and the phone the device, as on
 * the bus on 2026-11-13 (the iPhone, "iPhone" by Apple, and an AYN Thor), and
 * the message asks for the phone's USB tethering, which shares its network
 * over the cable; USB File Transfer then opens on it. An iPhone shares its
 * network only with a computer it trusts (paired), which the handheld cannot
 * be: once it is seen on the bus, its own message (Wi-Fi File Transfer, or
 * an adapter and a USB-A to USB-C cable) takes the place of Phone's. */
enum { USBPOWER_SAY_NONE, USBPOWER_SAY_COMPUTER, USBPOWER_SAY_PHONE, USBPOWER_SAY_IPHONE };

/* The message `say` for plug `plug` is over: the cable out (the pin idle),
 * another plug or usbrole unread; Computer's also with power in (the
 * computer connected) or the port a host (USB Accessory after all). The
 * phone's are for a phone the handheld powers, so the port a host is their
 * point and its power reading then the port's own: power in ends them only
 * while the port is not a host. */
bool usbpower_said_ends(int say, unsigned plug, const usb_power *p);

/* How long a plug may be asked about before it is: a DAC settles, a plug
 * that is about to give power has done so */
#define USBPOWER_ASK_MS 2000u

typedef struct {
	unsigned plug;        /* the plug being watched */
	unsigned since;       /* since when it may be asked about, 0 not */
	unsigned asked;       /* the last plug asked about */
	unsigned told;        /* the last plug told to plug in again */
} usbpower_follow_state;

/* What the shelf does about the plug now */
enum { USBPOWER_NOTHING, USBPOWER_ASK, USBPOWER_REPLUG };

/* One pass: USBPOWER_ASK and the plug in *plug when it may have been asked
 * about for USBPOWER_ASK_MS (once per plug; never while usbrole powers
 * accessories by itself). USBPOWER_REPLUG when a plug waits and a computer
 * has shown up on the bus during it (fed by a wrong answer, or by Power
 * Automatically): it is a computer for certain, and the launcher says to
 * plug it in again, at once, once per plug. Having had power a moment before says
 * nothing about what is plugged in now (2026-11-10), so nothing else is
 * taken for a computer. */
int usbpower_follow(usbpower_follow_state *f, const usb_power *p, unsigned now_ms,
                    unsigned *plug);

/* Back on after the handheld was turned off (its sleep: off, and the game
 * where it was at the next start) with an accessory powered (Discord,
 * metior, 2026-11-11: the Wi-Fi adapter asked about again at every start,
 * just as the player was in the menus). The launcher keeps "an accessory
 * was powered" when it turns off (`kept`), and the first plug of this start,
 * there from the start (askable within USBPOWER_RESUME_MS of the launcher's
 * start), is that plug again: powered with no question. The handheld cannot
 * tell what happened while it was off, so a computer put in its place gets
 * the same 2 s Automatic gives one before usbrole sees it on the bus and
 * stops. Never on Automatic, which powers it anyway. */
#define USBPOWER_RESUME_MS 8000u
bool usbpower_resume(const usb_power *p, bool kept, unsigned since_start_ms);

/* A question that comes by itself (What did you plug in?, Computer
 * Connected) came while the player was pressing through the menus, and a
 * press meant for them answered it (the same report). So a button that was
 * down when it appeared counts only once it has been let go and pressed
 * again, and one pressed in its first USBPOWER_TAKES_MS counts the same
 * way; after that, a press answers at once. Its button hints show from
 * USBPOWER_TAKES_MS. Buttons are bits (1 << button); the caller keeps POWER
 * out of it, which always works. */
#define USBPOWER_TAKES_MS 500u

typedef struct {
	unsigned opened;      /* when it appeared */
	unsigned held;        /* buttons that count only after a release */
} usbpower_guard;

void usbpower_guard_start(usbpower_guard *g, unsigned now_ms, unsigned down);

/* One pass: the buttons down and pressed now; the presses that count */
unsigned usbpower_guard_pass(usbpower_guard *g, unsigned now_ms, unsigned down, unsigned pressed);

/* Its button hints shown */
bool usbpower_guard_hints(const usbpower_guard *g, unsigned now_ms);

#endif
