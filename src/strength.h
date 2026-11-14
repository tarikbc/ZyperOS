/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_STRENGTH_H
#define ZYPEROS_STRENGTH_H

/* How strongly an old screen's shader is drawn (CRT, CRT Curved, Scanlines,
 * LCD Grid: src/app.h SHADERS' gpu ones): the Strength row under each Shader
 * row, 10% to 100% in steps of 10, Left and Right stopping at the ends. 100%
 * is the shader as it was before the row, and the default, so nobody's
 * picture changes with the update. A player on Discord turned Scanlines and
 * the LCD grid off after a few minutes, "the effect is to strong", and edited
 * shader images down to about 15% by hand (2026-11-14).
 *
 * Sent to Diatom as SETDISPLAY strength= (and --strength), which mixes the
 * shader's lines and grille toward the plain picture under them (its
 * port/pixel2.c). Kept as the shader is: per system as shader_strength.<TAG>,
 * and for every system as shader_strength, what MENU's row sets. Not under
 * "shader.": MENU's Shader deletes every key that starts with that.
 *
 * No SDL, no app: the database and numbers, so tools/db-check.c holds it. */
#include "db.h"

#include <stdbool.h>
#include <stddef.h>

#define STRENGTH_MIN  10
#define STRENGTH_MAX  100
#define STRENGTH_STEP 10

/* A value read or kept to one the row can show: a step of 10 from 10 to 100,
 * rounded to the nearest; anything at or under 0 (a sysview never loaded) is
 * 100, the default */
int strength_clamp(int pct);
/* `pct` stepped by `d` (+1 or -1) steps, stopping at either end */
int strength_step(int pct, int d);
/* "50%" */
void strength_label(int pct, char *out, size_t n);

/* System `tag`'s own, else every system's (strength_read_all), else 100 */
int  strength_read(db *d, const char *tag);
int  strength_read_all(db *d);
/* System `tag`'s own, kept the moment it changes */
void strength_write(db *d, const char *tag, int pct);
/* MENU's row: `pct` for every system, those whose games are off the card
 * now too, so each system's own goes */
void strength_write_all(db *d, int pct);

#endif
