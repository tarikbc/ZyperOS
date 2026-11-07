/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_PIXEL2_LEVELS_H
#define ZYPEROS_PIXEL2_LEVELS_H

/* The GKD Pixel 2's volume and brightness, as numbers: which control, which
 * window of it the twenty-one volume positions span, and the backlight's
 * rungs. Read by src/device/pixel2.c, which is the launcher's, and by
 * tools/portkeys.c, which holds the same keys while a port runs and the
 * launcher is stopped - so a level set in Stardew Valley is the level the
 * shelf comes back at, and both turn it into the same register value.
 *
 * No SDL here, and nothing of the launcher's: portkeys links only this, the
 * arithmetic in src/audioout.c, and src/osdbar.c. */

#include "../audioout.h"

/* The RK817's "Master Playback Volume": 0-255 on both channels, NOT inverted -
 * 255 is 0 dB and 0 is -95 dB, read with amixer 2026-10-01. It has no switch,
 * so 0 is the cut. Speaker or headphones is "Playback Mux", which the system's
 * jackswitch daemon drives.
 *
 * The windows were set by ear on 2026-10-02, the way the Brick's were: a game
 * playing, the register stepped over SSH until Eric called each end.
 *
 * Speaker: full volume is comfortable and does not distort, so position 20 is
 * raw 255. The floor was walked up from the old guess (raw 140, too quiet):
 * 148 and 156 too quiet, 164 too quiet beside 172, and 172 called right for
 * position 1. About 31 dB, so about 1.6 dB a press.
 *
 * Headphones (Apple EarPods): walked up from quiet to the most wanted - full
 * volume was a step too far, raw 244 is the top. The floor was walked down
 * from 172, 6 dB at a time, everything too loud until raw 76. About 63 dB,
 * 3.3 dB a press; EarPods are efficient, and a less sensitive pair will want
 * a different floor, as on the Brick.
 *
 * The constants are the windows that put positions 1 and 20 exactly on those
 * raw values with aout_level_to_raw's rounding: speaker 0-87, headphones
 * 11-188, in attenuation.
 *
 * Kept as ATTENUATION from 255, so the shared aout_level_to_raw - written for
 * the Brick's inverted register, loud end low - lays the positions out with
 * the same rounding. Diatom's Pixel port uses the same numbers and the same
 * arithmetic: a level that crosses the socket has to mean the same thing on
 * both sides. Change one, change the other. */
#define GAIN_CTL      "Master Playback Volume"
#define GAIN_RAW_MAX  255
#define SPK_ATT_TOP   0      /* attenuation at position 20 */
#define SPK_ATT_BOTTOM 87    /* and at position 0, which the cut replaces */
#define HP_ATT_TOP    11
#define HP_ATT_BOTTOM 188
#define VOL_MAX       20     /* PLAT_VOL_MAX: 21 positions, 0..20 */

/* A USB-C DAC's volume: a software one, "USB Playback Volume" on this card,
 * which plastron's ALSA config puts in front of the DAC (its pcm.usb), with
 * the DAC's own control set to full by the system when it appears. The DAC's
 * own comes and goes with it; this one is always here, so it is written with
 * every level, the codec's too, and plugging a DAC in needs nothing re-applied.
 *
 * 0-255 over -95.625 to 0 dB: 0.375 dB a step, the codec's step, so the
 * window is in the same units as the codec's. 0 is silence.
 *
 * The window is the EarPods' headphone window, a starting guess: the ear
 * sets it with a DAC playing, as it did the codec's. Diatom's Pixel port
 * writes the same control with the same numbers. Change one, change the
 * other, and asound.conf's min_dB with them. */
#define USB_CTL        "USB Playback Volume"
#define USB_RAW_MAX    255
#define USB_DB_MIN     (-9562)   /* hundredths of a dB, as alsa-lib rounds -95.625 */
#define USB_DB_STEP    37        /* and 0.375 */
#define USB_ATT_TOP    11
#define USB_ATT_BOTTOM 188

/* sysfs, 0-255, 0 dark. The device tree's brightness-levels table sends 1 to 17
 * all to the same output (a PWM duty of 1666 of 25000 ns) and is one-to-one
 * above that, so the Brick's ladder (2, 4, 8, 16, ...) spent its bottom four
 * rungs on one brightness - reported 2026-10-02 as the bottom of the scale
 * sitting still. Driving the PWM directly the same day, 1000 ns was black, so
 * 17 is the floor and stays: Eric wanted dimmer, but not black. The rungs are
 * equal ratios from 17 to 255, about 28% a step, since brightness reads in
 * ratios. Diatom's port holds the same rungs; change one, change the other. */
#define BACKLIGHT       "/sys/class/backlight/backlight/brightness"
#define BACKLIGHT_POWER "/sys/class/backlight/backlight/bl_power"
static const unsigned char bright_ladder[] __attribute__((unused)) = {
	17, 22, 28, 36, 46, 58, 74, 95, 122, 156, 199, 255
};
#define BRIGHT_MAX ((int)(sizeof bright_ladder / sizeof bright_ladder[0]) - 1)

/* The codec's raw gain for position v, by the window of the output in use. */
static inline long px2_gain_raw(int v, int headphones)
{
	return v == 0 ? 0
	     : GAIN_RAW_MAX - aout_level_to_raw(v, VOL_MAX,
	                                        headphones ? HP_ATT_TOP : SPK_ATT_TOP,
	                                        headphones ? HP_ATT_BOTTOM : SPK_ATT_BOTTOM);
}

/* And a USB-C DAC's software volume for the same position. */
static inline long px2_usb_raw(int v)
{
	return v == 0 ? 0
	     : USB_RAW_MAX - aout_level_to_raw(v, VOL_MAX, USB_ATT_TOP, USB_ATT_BOTTOM);
}

#endif
