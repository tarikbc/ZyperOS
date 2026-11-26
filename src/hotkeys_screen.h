/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HOTKEYS_SCREEN_H
#define ZYPEROS_HOTKEYS_SCREEN_H

#include <stdbool.h>

#include "app.h"
#include "config.h"
#include "panel.h"

/* The Hotkeys list (DESIGN.md 11.61; my picks in the Controls
 * lab, 2026-11-19): a row an action, its keys at its right drawn as the
 * buttons (SELECT+R1) or None, A on a row the capture screen - SELECT held
 * and one more button, read as keys until let go - and a clash the swap
 * question, the cursor on Cancel. Home, fixed, says why; Reset Hotkeys asks
 * first.
 *
 * MENU > System > Hotkeys: Every Game's, over the shelf. */
void hotkeys_screen(app *a);

/* A game's Controls tab > Hotkeys: Applies To first (Every Game, the
 * system, This Game), the rows sent to the game at once while it is paused
 * under it (`running`, SETHOTKEYS), and last All Controls, the read-only
 * pages the row opened before. Over `over`'s backdrop with its power rule. */
void hotkeys_ingame(app *a, const menu_style *over, void *ctx, const system_cfg *s,
                    const char *file, const char *title, bool running);

/* A speed as the speed pop-up says it, in the language: "1.5x", "2x", "1x",
 * or "Max" for 0 (src/hotkeys.h's hk_ff_half, Diatom's SPEED x=max); the
 * Fast Forward Speed row says its speeds the same way */
void speed_said(int half, char *out, size_t n);

/* For the shots (src/shots.c): the list drawn once, the cursor on row
 * `sel`, in-game with `s` (NULL: MENU > System's); `capture` 0 none, 1 the
 * capture screen waiting, 2 with SELECT and `key` (HK_*) down; `ask` 1 the
 * swap question for that key, 2 Reset Hotkeys' for the scope, 3 Reset
 * Game's over the game */
void hotkeys_shot(app *a, const system_cfg *s, const char *file, int sel, int capture, int key,
                  int ask, int scope);

#endif
