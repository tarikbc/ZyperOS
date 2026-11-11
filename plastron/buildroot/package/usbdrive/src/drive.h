/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbdrive's rules: the GKD Pixel 2's card as a USB drive (ZyperOS's
 * Computer Connected > USB Drive), so any computer opens it with no driver
 * (a Windows 10 player met File Transfer's "no driver" hint, 2026-11-11).
 *
 * The games partition is where ZyperOS itself lives, and two that write it
 * at once - the handheld and the computer - wreck it. So it is never
 * exported while mounted, and never mounted while exported:
 *
 *   prepare  ZyperOS has left (launch.sh hands over to usbdrive). What
 *            still holds the card is stopped, the card is synced and
 *            unmounted. Still mounted after that: FAILED, nothing exported,
 *            and the card stays as it was (fail closed).
 *   exported The partition is the computer's (configfs f_mass_storage,
 *            in NCM's place in the gadget). It ends when the computer ejects
 *            it (the LUN's file goes empty once it had one), the cable comes
 *            out (no power in for DRV_UNPLUG_MS), or POWER is pressed.
 *   return   Un-exported; checked if the computer left it dirty (datapart's
 *            repair) and mounted again; ZyperOS starts again (launch.sh), or
 *            for POWER the handheld turns off, the card un-exported first.
 *
 * Pure: no files, no clock of its own. tests/drive-check.c holds it.
 */
#ifndef USBDRIVE_DRIVE_H
#define USBDRIVE_DRIVE_H

#include <stdbool.h>

#define DRV_UNPLUG_MS 1500u    /* no power in this long: the cable is out */
#define DRV_NOHOST_MS 60000u   /* exported, and no computer took it: given up */

/* May the partition be exported now: unmounted, and nothing holds it */
bool drv_may_export(bool mounted, int holders);

/* Why the export ended */
typedef enum {
	DRV_STAY,        /* still the computer's */
	DRV_EJECTED,     /* the computer ejected it */
	DRV_UNPLUGGED,   /* the cable came out */
	DRV_POWER,       /* POWER: un-export, then off */
	DRV_NOHOST,      /* no computer took it (a charger, a cable for power only) */
} drv_end;

typedef struct {
	bool     started;
	unsigned start;       /* exported at */
	bool     had_file;    /* the LUN had the partition: what an eject clears */
	bool     configured;  /* a computer configured the gadget */
	unsigned unpowered;   /* since when no power comes in, 0 for power */
} drv_wait;

/* One look while exported: power in (VBUS), the LUN's file set, the gadget
 * configured by a computer, POWER pressed */
drv_end drv_wait_step(drv_wait *w, bool vbus, bool lun_file, bool configured, bool power,
                      unsigned now);

#endif
