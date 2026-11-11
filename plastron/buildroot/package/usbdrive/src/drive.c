/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See drive.h. */
#include "drive.h"

bool drv_may_export(bool mounted, int holders)
{
	return !mounted && holders == 0;
}

drv_end drv_wait_step(drv_wait *w, bool vbus, bool lun_file, bool configured, bool power,
                      unsigned now)
{
	if (!w->started) {
		w->started = true;
		w->start = now;
		w->had_file = false;
		w->configured = false;
		w->unpowered = 0;
	}
	if (power) return DRV_POWER;
	if (lun_file) w->had_file = true;
	if (configured) w->configured = true;
	/* Ejected: the file it had is gone (the computer's SCSI eject clears
	 * a removable LUN's file) */
	if (w->had_file && !lun_file) return DRV_EJECTED;
	/* The cable out: no power in, for a while (a moment's dip is not it) */
	if (!vbus) {
		if (!w->unpowered) w->unpowered = now ? now : 1;
		if (now - w->unpowered >= DRV_UNPLUG_MS) return DRV_UNPLUGGED;
	} else {
		w->unpowered = 0;
	}
	/* Power in but nobody took it: a charger, or a cable that only charges */
	if (!w->configured && now - w->start >= DRV_NOHOST_MS) return DRV_NOHOST;
	return DRV_STAY;
}

drv_act drv_key(drv_keys *k, int key, bool down, unsigned now, bool wrote, unsigned last_write)
{
	if (key == DRV_K_FN) {
		k->fn = down;
		return DRV_A_NONE;
	}
	if (!down) return DRV_A_NONE;
	switch (key) {
	case DRV_K_VOLUP:
	case DRV_K_VOLDN: {
		int to = k->level + (key == DRV_K_VOLUP ? 1 : -1);

		/* Volume alone does nothing here: there is no sound */
		if (!k->fn || to < 0 || to > k->max) return DRV_A_NONE;
		k->level = to;
		return DRV_A_BRIGHT;
	}
	case DRV_K_B:
		if (k->asking) {
			k->asking = false;
			return DRV_A_KEEP;
		}
		if (wrote && now - last_write < DRV_ASK_MS) {
			k->asking = true;
			return DRV_A_ASK;
		}
		return DRV_A_EJECT;
	case DRV_K_A:
		if (!k->asking) return DRV_A_NONE;
		k->asking = false;
		return DRV_A_EJECT;
	default:
		return DRV_A_NONE;
	}
}
