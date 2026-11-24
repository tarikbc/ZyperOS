/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENUKEYS_SCREEN_H
#define ZYPEROS_MENUKEYS_SCREEN_H

/* MENU's chords on every screen, the launcher's half (src/menukeys.h has the
 * rules): the table kept on the card, the screenshot of the screen and its
 * notice, and the lock. */
#include "app.h"
#include "menukeys.h"

#include <SDL.h>
#include <stdbool.h>

/* Read the table from the device's database and give the platform the two
 * hooks (plat_set_menu_keys); `a` is the app the lock draws with */
void mk_init(app *a);

/* The table in force; and a new one, kept */
const mk_table *mk_now(void);
void mk_put(const mk_table *t);

/* Over every screen, under the bar (plat_set_overlay's, beside the replug
 * message): "Screenshot saved" once the picture is on the card */
void mk_overlay(SDL_Renderer *r);

/* The capture screen of MENU > System > Hotkeys: while it is open, every
 * button pressed with MENU held is taken, and none is carried out. The
 * button taken since the last mk_capture(true), as src/hotkeys.h's key
 * (HK_*), or -1 */
void mk_capture(bool on);
int mk_caught(void);

/* The button as src/hotkeys.h's key, -1 for one that is none; and back */
int mk_key_of(in_button b);
in_button mk_in_of(int key);

/* Locked until SELECT and START are held for Hold to Unlock: MENU+L2, on
 * any screen. The screen dark, MP4's lock's rules (src/mp4lock.h) and its
 * panel: a press shows how to open it, the volume keys get through. */
void mk_lock(app *a);

/* For a host shot: the lock's panel over the screen (`k`, 0 to 1, the
 * unlock's hold), and the notice as the screenshot leaves it */
void mk_lock_shot(app *a, float k);
void mk_notice_shot(app *a);

#endif
