/* SPDX-License-Identifier: GPL-3.0-only */
/* See usblink.h. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "usblink.h"
#include "usbpeer.h"

/* What makes a development image (plastron's release build takes it out);
 * tools/usblink-check.c points it at a file of its own */
#ifndef DEV_SCRIPT
#define DEV_SCRIPT "/etc/init.d/S50dropbear"
#endif
#define KEY        "developer"     /* the device database: -1 image's default, 0, 1 */

static int      g_peer = -2;       /* what the port had at the last look */
static bool     g_asked;           /* this plug has had its question */
static bool     g_want;            /* the last look: a computer on the cable, not asked yet */
static bool     g_held;            /* the link is on for this plug */
static bool     g_dev_answer;      /* ... because of Developer (SSH) */
static unsigned g_at;
/* Developer Mode's new plug: when it came, and whether its link is still to
 * be looked at (USBLINK_SETTLE_MS) */
static unsigned g_plug_at;
static bool     g_plug_check;
/* The setting as the database has it (-1 its default, 0, 1), or -2 before it
 * is read: usblink_follow asks five times a second, and only
 * usblink_set_developer writes it */
static int      g_dev_value = -2;
/* Developer Mode on a release image (devmode.h), which the launcher says is
 * on: the link is kept like a development image's, with no question */
static bool     g_release_dev;

bool usblink_dev_image(void)
{
	return access(DEV_SCRIPT, F_OK) == 0;
}

bool usblink_developer(db *dev)
{
	int v;

	if (!usblink_dev_image()) return g_release_dev;
	if (g_dev_value == -2) g_dev_value = dev ? db_get_int(dev, KEY, -1) : -1;
	v = g_dev_value;
	return v < 0 ? true : v != 0;
}

static void write_flag(const char *userdata, bool on)
{
	char path[600], tmp[620];
	int fd;

	if (!userdata || !userdata[0]) return;
	snprintf(path, sizeof path, "%s/developer", userdata);
	snprintf(tmp, sizeof tmp, "%s.new", path);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)) < 0) return;
	if (write(fd, on ? "1\n" : "0\n", 2) != 2) { close(fd); unlink(tmp); return; }
	fsync(fd);
	close(fd);
	if (rename(tmp, path) != 0) unlink(tmp);
}

void usblink_sync(db *dev, const char *userdata)
{
	/* A release image's S40usbgadget never binds at boot and never reads the
	 * file, which a development image may have left on the card: left alone */
	if (usblink_dev_image()) write_flag(userdata, usblink_developer(dev));
}

void usblink_set_developer(db *dev, const char *userdata, bool on)
{
	/* A release image has no Developer Mode, and writes no answer for one:
	 * the database is on the card, and a development image reading it later
	 * must start from its own default, on, or the sessions lose the device */
	if (!usblink_dev_image()) return;
	if (dev) db_set_int(dev, KEY, on ? 1 : 0);
	g_dev_value = on ? 1 : 0;
	write_flag(userdata, on);
	if (plat_usb_peer() == USB_PEER_UNKNOWN) return;
	if (on) {
		plat_usb_link(1);
		g_asked = true;                   /* nothing left to ask this plug */
		g_want = false;
	} else if (!g_held) {
		plat_usb_link(0);
	}
	fprintf(stderr, "usb: Developer Mode %s\n", on ? "on" : "off");
}

void usblink_release_dev(bool on)
{
	if (usblink_dev_image() || g_release_dev == on) return;
	g_release_dev = on;
	if (plat_usb_peer() == USB_PEER_UNKNOWN) return;
	if (on) {
		plat_usb_link(1);
		g_asked = true;                   /* nothing left to ask this plug */
		g_want = false;
	} else if (!g_held) {
		plat_usb_link(0);
	}
}

void usblink_skip_ask(void)
{
	g_asked = true;
	g_want = false;
	g_peer = USB_PEER_COMPUTER;
}

/* One look at the cable (five a second at most; between, the last look
 * stands): binds or lets go as Developer Mode says, ends a plug that went,
 * and says whether a computer waits for its question; `take` asks it */
static bool look(db *dev, unsigned now_ms, bool take)
{
	usb_peer peer;

	/* Five looks a second: a quick replug is a new plug, and two small
	 * reads cost nothing beside a frame */
	if (g_peer == -2 || now_ms - g_at >= 200u) {
		g_at = now_ms;
		peer = plat_usb_peer();
		if (peer == USB_PEER_UNKNOWN) {
			g_want = false;
			return false;
		}
		if (usblink_developer(dev)) {
			/* A computer plugged in again gets a fresh link if it needs
			 * one. Kept bound across the unplug, the controller could come
			 * back from it - through the port's host role, which usbrole
			 * gives it while the plug settles - not offering itself, and
			 * the computer saw nothing until another replug (Pixel 2,
			 * 2026-11-05). So a link the computer has not configured
			 * USBLINK_SETTLE_MS after the plug is made fresh; one it took
			 * is left alone, as a rebind at every plug made the computer
			 * meet the handheld three times over and held the screen
			 * (2026-11-24). Where the controller cannot tell (the Brick),
			 * the fresh link comes at once, as before. */
			if (peer == USB_PEER_COMPUTER && g_peer != USB_PEER_COMPUTER && g_peer != -2) {
				if (plat_usb_configured() < 0) {
					plat_usb_link(2);
					fprintf(stderr, "usb: a computer is on the cable: a fresh link\n");
				} else {
					if (!plat_usb_link(-1)) plat_usb_link(1);
					g_plug_at = now_ms;
					g_plug_check = true;
					fprintf(stderr, "usb: a computer is on the cable: its link looked at in %u s\n",
					        USBLINK_SETTLE_MS / 1000);
				}
			} else if (!plat_usb_link(-1)) {
				plat_usb_link(1);
			}
			if (peer != USB_PEER_COMPUTER) {
				g_plug_check = false;
			} else if (g_plug_check && now_ms - g_plug_at >= USBLINK_SETTLE_MS) {
				g_plug_check = false;
				if (plat_usb_configured() == 1) {
					fprintf(stderr, "usb: the computer took the link: left as it is\n");
				} else {
					plat_usb_link(2);
					fprintf(stderr, "usb: the computer did not take the link in %u s: a fresh link\n",
					        USBLINK_SETTLE_MS / 1000);
				}
			}
			g_peer = peer;
			g_asked = peer == USB_PEER_COMPUTER;
			g_want = false;
			return false;
		}
		if (peer != USB_PEER_COMPUTER) {
			/* Out, or only a charger: whatever this plug had ends */
			if (g_held || plat_usb_link(-1)) plat_usb_link(0);
			if (g_peer == USB_PEER_COMPUTER) fprintf(stderr, "usb: the computer went away\n");
			g_held = g_dev_answer = g_asked = false;
			g_peer = peer;
			g_want = false;
			return false;
		}
		if (g_peer != USB_PEER_COMPUTER) fprintf(stderr, "usb: a computer is on the cable\n");
		g_peer = peer;
		g_want = !g_asked;
	}
	if (!g_want) return false;
	if (take) {
		g_asked = true;
		g_want = false;
	}
	return true;
}

bool usblink_follow(db *dev, unsigned now_ms)
{
	return look(dev, now_ms, true);
}

bool usblink_wants_ask(db *dev, unsigned now_ms)
{
	return look(dev, now_ms, false);
}

void usblink_hold(bool on)
{
	g_asked = true;
	g_want = false;
	g_held = on;
	g_dev_answer = on;
	plat_usb_link(on ? 1 : 0);
}

void usblink_for_transfer(void)
{
	g_asked = true;
	g_want = false;
	if (plat_usb_peer() == USB_PEER_UNKNOWN) return;
	plat_usb_link(1);
}

void usblink_after_transfer(db *dev)
{
	if (plat_usb_peer() == USB_PEER_UNKNOWN) return;
	if (usblink_developer(dev) || g_dev_answer) return;
	g_held = false;
	plat_usb_link(0);
}
