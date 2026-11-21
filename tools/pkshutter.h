/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PKSHUTTER_H
#define ZYPEROS_PKSHUTTER_H

/* The shutter over your PICO-8 (DESIGN.md 9 and 11.61): what portkeys plays
 * as the Screenshot hotkey takes a shot, the launcher being gone.
 *
 * The port owns the sound: PICO-8 plays through its own chain (launch.sh,
 * sd/zyperos/eq-port.sh: plug, the equalizer's plugin with the boost's gain
 * and MP4's cut, plug) into the system's dmix. The shutter is a second
 * player on that same dmix, opened by name ("default", or "usb" with a DAC:
 * tools/pkfeel.h), as the launcher's interface sounds open it beside a
 * paused game (src/uisound_out.c, src/pcm_out.c): never the card itself and
 * never the port's chain, so nothing of PICO-8's sound is taken, cut, paused
 * or shaped by it, and it is heard while MP4 plays over a quiet PICO-8. The
 * volume and the mute switch sit below the mix and set it too.
 *
 * On a thread of its own, so a shot never waits on the output and the keys
 * PICO-8 reads go on: pkshutter_play returns at once. One at a time: a shot
 * while the last shutter is still sounding (175 ms) plays none. The files are
 * read once, before the first shot (pkshutter_load), so a press reads no
 * card either. */

#include <stdbool.h>

/* The shutter of every voice, from `dir` (res/sounds); false when none of
 * them could be read */
bool pkshutter_load(const char *dir);

/* `voice`'s shutter on `device`, on the sides asked for; returns at once.
 * False when there is nothing to play, or one is still playing. */
bool pkshutter_play(int voice, const char *device, bool left, bool right);

/* Whether one is playing, and for a check, a wait of up to `ms` for it to
 * end: true when it has */
bool pkshutter_busy(void);
bool pkshutter_wait(int ms);

#endif
