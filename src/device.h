/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DEVICE_H
#define ZYPEROS_DEVICE_H

#include "platform.h"

/* The seam inside the platform layer, between the parts every device shares
 * (platform.c, resident.c) and the one device being built (src/device/<name>.c,
 * chosen by DEVICE= at build time). Nothing outside the platform layer
 * includes this: the rest of ZyperOS sees platform.h only.
 *
 * A device file defines everything platform.h declares that touches hardware -
 * video, input, levels, the backlight, LEDs, the battery, where things live on
 * the card - plus the two functions below. Kept this short on purpose: each
 * line here is something a second device has to answer. */

/* Drain the power key's queue and say whether a press was in it. Called while
 * a child or a game owns the screen, so it must not block. */
bool device_power_pressed(void);

/* A game just handed input back. Forget whatever was remembered about the
 * hardware while something else was driving: the headphone jack, a mute
 * switch. */
void device_levels_forget(void);

/* The display, around a game. Release is called just before Diatom is told to
 * present (RUN, and the wait that follows a RESUME), take once it has handed
 * the screen back (PAUSED, EXIT, or the game gone). Nothing on a device where
 * both can draw to one framebuffer by turns, which is the Brick; passing DRM
 * master on one that drives its display through KMS. */
void device_display_release(void);
void device_display_take(void);

/* How long the launcher may wait for Diatom's next line while a game runs, ms
 * the launcher would wait. A device whose panel Diatom draws on returns them;
 * the host's desk mode, which shows the game's frames in its own window
 * (src/device/desk.h), shows the newest one here, takes the keys, and asks
 * for a few ms only. */
unsigned device_game_wait(unsigned ms);

/* Shared by the portable half, for the device file. */
void plat_set_btn(in_state *st, in_button b, bool down);   /* a button's edge */
/* The one engine (src/inengine.h), which every device file gives what its
 * handheld reports, from the profile in force: */
/* An EV_KEY (1 down, 0 up, 2 a repeat); false for a code the profile has no
 * button for */
bool plat_in_key(in_state *st, int code, int value);
/* A button from a keyboard or desk mode */
void plat_key(in_state *st, in_button b, bool down);
/* A stick's axis: whether the profile reads `code`, its range and flat as
 * the kernel says them, where it is, and each report's end */
bool plat_in_wants_axis(int code);
void plat_in_axis(int code, int value, int min, int max, int flat);
void plat_in_abs(int code, int value);
void plat_in_sync(in_state *st);
/* At each poll's start, every device: a tap of the last poll ends */
void plat_in_begin(in_state *st);
/* L2 and R2 together, on a handheld whose lights show the battery
 * (battery_chord): what flashes them */
void plat_in_battery(void (*fn)(void));
/* Everything held forgotten: a flush, whose let-gos are never read */
void plat_menu_forget(void);
/* At each poll's end, every device: a chord taken is carried out */
void plat_menu_after(in_state *st);
/* At each present's start, every device, before the bar over it: the frame
 * read back for a screenshot when one is asked (plat_grab_next) */
void plat_grab_frame(SDL_Renderer *r);
bool plat_terminating(void);                    /* SIGTERM arrived */
void plat_set_power_pressed(bool on);           /* the last game ended on power */
/* One line to a running game, on the connection already open; connects for
 * nothing. */
bool plat_resident_tell(const char *fmt, ...);

#endif
