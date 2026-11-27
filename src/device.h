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
/* A button's edge on a device whose MENU is a button of its own (the Brick,
 * a keyboard, desk mode): MENU said as it is let go, and a button pressed
 * while it is held MENU's chord when the launcher takes it (src/menukeys.h),
 * as the Pixel 2's FUNCTION does. Everything else as plat_set_btn. */
void plat_key(in_state *st, in_button b, bool down);
/* For a device that keeps MENU itself (the Pixel 2's FUNCTION): `b` went
 * down or up, MENU held or not; true when MENU's chord took it, and nothing
 * else may see it, its let-go too */
bool plat_menu_with(in_state *st, in_button b, bool down, bool menu_held);
/* What MENU's chords keep between polls, forgotten: a flush, whose let-gos
 * are never read */
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
