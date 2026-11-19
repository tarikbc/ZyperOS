/* SPDX-License-Identifier: GPL-3.0-only */
/* A button as this handheld prints it (DESIGN.md 11.9, "Buttons by device"):
 * which glyph a hint, a lesson or a key row draws for a button, from the
 * device profile's table of its controls (src/device/profile.h, dev_button).
 *
 * Screens name a button by its id's name - "A", "SELECT", "L1", "F1", the
 * d-pad's "< >" and "^ v" - never by what one device prints on it. This says
 * what the device does print: the Pixel 2's SELECT is a round key with SE on
 * it, the Brick's a round key with a square on it; the Pixel 2's MENU is a round
 * key with three dots, the Brick's shows TrimUI's mark, so it is its word
 * (a vendor's logo is never a mark). src/ui.c draws the answer
 * (ui_button, ui_button_pressed, ui_button_width) in the theme's colors.
 *
 * Pure: no SDL, so tools/devices-check.c holds every device's answers. */
#ifndef ZYPEROS_DS_BUTTON_H
#define ZYPEROS_DS_BUTTON_H

#include <stdbool.h>
#include "../device/profile.h"

/* The glyph's shape: a circle as wide as it is tall (a face), a pill as
 * long as its word, the d-pad drawn as itself with the arms a hint means
 * lit (left and right, or up and down, or all four for the whole d-pad), a
 * stick's cap, a circle with its top's ring inside, or the volume keys'
 * rocker, VOL- and VOL+ as one pill */
typedef enum {
	DS_KEY_FACE, DS_KEY_PILL, DS_KEY_DPAD_LR, DS_KEY_DPAD_UD, DS_KEY_STICK,
	DS_KEY_DPAD, DS_KEY_ROCKER
} ds_key_shape;

/* What is on it instead of a word, from the design system's closed set */
typedef enum {
	DS_KEY_MARK_NONE,     /* its word (`text`) */
	DS_KEY_MARK_DOTS,     /* three dots down it: the Pixel 2's MENU */
	DS_KEY_MARK_SQUARE,   /* a square: the Brick's SELECT */
	DS_KEY_MARK_PLAY      /* a play triangle: the Brick's START */
} ds_key_mark;

typedef struct {
	ds_key_shape shape;
	ds_key_mark  mark;
	const char  *text;    /* the word drawn (a mark's key keeps its word here too) */
} ds_key;

/* The handheld's controls, its profile's table, as the launcher starts and
 * before anything is drawn (main.c). Until then, and with NULL, the glyphs
 * the launcher always drew: A B X Y round, MENU round with three dots, the
 * d-pad as itself, every other button a pill with its name. */
void ds_keys_set(const dev_button *table, int n);

/* The glyph for the button named `name` on this handheld. A name no button
 * has ("PIN", a word of a screen's own) is a pill with that word. "DPAD" is
 * the whole d-pad and "VOL" the volume keys as one rocker, as the Controls
 * pages name them (src/controls.c, ctl_keys_of). */
ds_key ds_key_of(const char *name);

/* `name`'s button id ("A" IN_ACCEPT, "F1" IN_BRIGHTDN), IN_NONE for none */
in_button ds_key_id(const char *name);

#endif
