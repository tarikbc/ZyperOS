/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PKFEEL_H
#define ZYPEROS_PKFEEL_H

/* What a screenshot over your PICO-8 is felt and heard as (DESIGN.md 9 and
 * 11.61), as a Diatom game's is: portkeys (tools/portkeys.c's shot_take)
 * buzzes the motor and plays the shutter as the frame is taken, before the
 * PNG is written. From the launcher's settings, read at each shot:
 *
 *   the buzz     MENU > Sound > Rumble's share (tools/pkkeys.h's
 *                pk_rumble_pct: Off none, Low 35%, Medium 65%, Full),
 *                PK_BUZZ_MS long, the boost's buzz
 *   the shutter  while MENU > UI Sounds is a voice (src/uisound.h's
 *                uisound_shot), that voice's res/sounds/<voice>/shutter.wav,
 *                none at Off; played on the mix PICO-8 plays into, beside
 *                it (tools/pkshutter.h): the system's "default", or "usb"
 *                with a USB-C DAC in, as the launcher's own sounds go
 *                (uisound_device), on the sides Audio Channels gives them
 *                there (uisound_sides)
 *
 * Pure but for the database: tools/pkkeys-check.c holds it with the
 * launcher's own words, written through src/db.c. */

#include <sqlite3.h>
#include <stdbool.h>

typedef struct {
	int         buzz_pct;   /* 0, no buzz */
	int         voice;      /* the shutter's voice, DS_VOICE_*; -1, no shutter */
	const char *device;     /* the ALSA device it plays on */
	bool        left, right;
} pk_feel;

/* `dac`: a USB-C DAC is in (/proc/asound/card1/usbid), where PICO-8's sound
 * goes (launch.sh); `wired`: headphones in the jack, which Audio Channels'
 * sides are for (a side on the speaker is plain mono, src/audioout.h) */
void pk_shot_feel(sqlite3 *db, bool dac, bool wired, pk_feel *f);

#endif
