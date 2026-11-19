/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_REMAPSTORE_H
#define ZYPEROS_REMAPSTORE_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "remap.h"

/* What is kept on the card for button remapping, and what the game is sent.
 * It reads and writes the library database (db_lib) and nothing else: no
 * SDL. Design: DESIGN.md 11.45.
 *
 *     map.<TAG>          the system's pairs that differ from ZyperOS's default
 *     map.<TAG>.<file>   a game's pairs that differ from ZyperOS's default, the whole
 *                        table copied at its first change
 *     stick.<TAG>.<file> "1" or "0"; absent is the system's default
 *
 * A key never holds a table equal to its default, so undoing a change
 * undoes the key. */

/* ZyperOS's own table for a system: its `map|` line, then its turbo
 * (plat_turbo_map), a later pair for the same button winning, as the
 * launcher joined them before the table was sent whole */
void rms_default(const system_cfg *s, rm_table *out);

/* The default with the system's key over it. True when the key exists. */
bool rms_system(const system_cfg *s, rm_table *out);

/* The game's table: its own key over ZyperOS's default (true), the same
 * base as the system's, since its first change copied the whole table in;
 * else the system's table (false) */
bool rms_game(const system_cfg *s, const char *file, rm_table *out);

/* Keeps t for the game, or for the system when file is NULL, as pairs over
 * rms_default; the key is deleted when t equals what the scope falls back to
 * (the system's table for a game, rms_default for the system). */
void rms_put(const system_cfg *s, const char *file, const rm_table *t);

/* Deletes the scope's key: the game goes back to the system's table, the
 * system to ZyperOS's */
void rms_reset(const system_cfg *s, const char *file);

/* A game's key that has come to equal the system's table (the system's was
 * changed to match it later) is deleted, as a change that left it so would
 * have: the game has no buttons of its own then. A value this release cannot
 * read is left alone. True when it was deleted. */
bool rms_game_tidy(const system_cfg *s, const char *file);

/* D-pad as Stick. Offered where the system has a `stick|` line. */
bool rms_stick_offered(const system_cfg *s);
bool rms_stick_default(const system_cfg *s);
bool rms_stick_get(const system_cfg *s, const char *file);
/* Deletes the key when `on` is the default */
void rms_stick_put(const system_cfg *s, const char *file, bool on);

/* The RUN key for the stick: stick=dpad, stick=plain, stick=toggle (the
 * PSP's, on the d-pad's side until its Stick On/Off button turns it) or
 * stick=toggle-stick; "" for none. `keys`: the table sent gives a button the
 * stick (rm_has_stick), so a stick that is off is sent on the d-pad's side,
 * stick=dpad-pad or plain-pad, for that button to turn; with none it is ""
 * as ever. */
void rms_run_stick(const system_cfg *s, bool on, bool keys, char *key, size_t n);

/* The table a game is sent: rms_game, with one rule. Where the stick's own
 * rule takes SELECT (mode dpad: the N64 and the Dreamcast), a SELECT pair
 * the player changed is sent as the default's own (the N64's select:r2, the
 * Dreamcast's plain SELECT), the stick's rule winning. */
void rms_sent(const system_cfg *s, const char *file, bool stick_on, rm_table *out);

#endif
