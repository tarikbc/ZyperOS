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
static bool     g_held;            /* the link is on for this plug */
static bool     g_dev_answer;      /* ... because of Developer (SSH) */
static unsigned g_at;
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
	} else if (!g_held) {
		plat_usb_link(0);
	}
}

bool usblink_follow(db *dev, unsigned now_ms)
{
	usb_peer peer;

	/* Five looks a second: a quick replug is a new plug, and two small
	 * reads cost nothing beside a frame */
	if (g_peer != -2 && now_ms - g_at < 200u) return false;
	g_at = now_ms;
	peer = plat_usb_peer();
	if (peer == USB_PEER_UNKNOWN) return false;
	if (usblink_developer(dev)) {
		/* A computer plugged in again gets a fresh link. Kept bound across
		 * the unplug, the controller could come back from it - through the
		 * port's host role, which usbrole gives it while the plug settles -
		 * not offering itself, and the computer saw nothing until another
		 * replug (Pixel 2, 2026-11-05). File Transfer's screen makes a fresh
		 * one as it opens for the same reason (plat_cable_reconnect). */
		if (peer == USB_PEER_COMPUTER && g_peer != USB_PEER_COMPUTER && g_peer != -2) {
			plat_usb_link(2);
			fprintf(stderr, "usb: a computer is on the cable: a fresh link\n");
		} else if (!plat_usb_link(-1)) {
			plat_usb_link(1);
		}
		g_peer = peer;
		g_asked = peer == USB_PEER_COMPUTER;
		return false;
	}
	if (peer != USB_PEER_COMPUTER) {
		/* Out, or only a charger: whatever this plug had ends */
		if (g_held || plat_usb_link(-1)) plat_usb_link(0);
		if (g_peer == USB_PEER_COMPUTER) fprintf(stderr, "usb: the computer went away\n");
		g_held = g_dev_answer = g_asked = false;
		g_peer = peer;
		return false;
	}
	if (g_peer != USB_PEER_COMPUTER) fprintf(stderr, "usb: a computer is on the cable\n");
	g_peer = peer;
	if (g_asked) return false;
	g_asked = true;
	return true;
}

void usblink_hold(bool on)
{
	g_asked = true;
	g_held = on;
	g_dev_answer = on;
	plat_usb_link(on ? 1 : 0);
}

void usblink_for_transfer(void)
{
	g_asked = true;
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
