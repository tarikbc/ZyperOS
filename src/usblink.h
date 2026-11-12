/* SPDX-License-Identifier: GPL-3.0-only */
/* USB charges only until somebody says otherwise (2026-11-05).
 *
 * plastron's S40usbgadget makes the USB network gadget at boot but binds it
 * only in Developer Mode, so a computer sees no device and the handheld just
 * charges. When a computer is plugged in (the PHY tells its data port from a
 * wall charger: plat_usb_peer), the shelf asks once per plug, as three tiles
 * starting on the first (DESIGN.md 11.18): Charge Only,
 * File Transfer (binds the link and opens USB File Transfer), or, on a
 * development image, Developer (SSH) (binds the link until the cable comes
 * out). B, or no answer, keeps charging only; unplugging ends any choice.
 *
 * Developer Mode keeps the link bound whenever the cable is in, with no
 * question, so the development image's dropbear is always reachable at
 * 10.42.0.1. It is on by default in a development image (one that starts
 * dropbear at boot: /etc/init.d/S50dropbear) and does not exist in a release
 * image. It is kept in the device database and mirrored to a file on the
 * card, .userdata/<device>/developer ("1" or "0"), which a development
 * image's S40usbgadget reads at boot, before the launcher can, so the link
 * comes up without it. A release image binds nothing at boot, never reads
 * the file, and never writes it or the setting: the card can go back into a
 * development image, which must start from its own default.
 *
 * Nothing here is drawn: the shelf's loop asks usblink_follow and draws the
 * question itself. A device whose link is not the launcher's (the Brick:
 * plat_usb_peer says UNKNOWN) is never asked about and keeps its own. */
#ifndef USBLINK_H
#define USBLINK_H

#include <stdbool.h>

#include "db.h"

/* A development image: one that starts dropbear at boot. */
bool usblink_dev_image(void);

/* Developer Mode, as set, or the image's default. On a release image it is
 * false until usblink_release_dev says otherwise. */
bool usblink_developer(db *dev);

/* Developer Mode on a release image (src/devmode.h): the launcher tells this
 * whether it is on (as it starts, and when the player turns it on or off).
 * While on, usblink_developer is true there too: the link stays bound while
 * a computer is on the cable and nothing is asked, as on a development
 * image, so SSH (which the launcher starts then) is there for as long as the
 * cable is, and a launcher that restarts finds the link still up. Nothing is
 * written to the card, and a development image ignores this. */
void usblink_release_dev(bool on);

/* Set it: the database, the card's file for S40usbgadget, and the link now. */
void usblink_set_developer(db *dev, const char *userdata, bool on);

/* The card's file made to agree with the database, once as the launcher
 * starts: a database copied from another card, or a file deleted by hand. */
void usblink_sync(db *dev, const char *userdata);

/* One pass of the shelf's loop: binds or lets go as Developer Mode and the
 * cable say, and true exactly once per plug of a computer, when the question
 * is to be asked. Cheap: the port is read five times a second at most. */
bool usblink_follow(db *dev, unsigned now_ms);

/* The same look from anywhere else (the menus' loop), the question not
 * taken: true while a computer on the cable waits for it, so a menu can
 * close back to the shelf, whose usblink_follow then asks. It ends a plug
 * that went as usblink_follow does, so one replugged while a menu is open
 * is a new plug. */
bool usblink_wants_ask(db *dev, unsigned now_ms);

/* The link for this plug, from the question's answer: on until the cable
 * comes out (Developer (SSH)), or off (Charge Only). File Transfer's comes
 * and goes with its screen (usblink_for_transfer, usblink_after_transfer). */
void usblink_hold(bool on);

/* This plug was answered before the launcher started (USB Drive's: the
 * card came back with the cable still in): not asked again */
void usblink_skip_ask(void);

/* After USB File Transfer closes: off again unless Developer Mode or a
 * Developer (SSH) answer keeps it. */
void usblink_after_transfer(db *dev);

/* Before USB File Transfer opens from MENU: the link on for this plug. */
void usblink_for_transfer(void);

#endif
