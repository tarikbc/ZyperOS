/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENUKEYS_H
#define ZYPEROS_MENUKEYS_H

#include <stdbool.h>
#include <stddef.h>

/* MENU held with one more button, on every screen of the launcher (DESIGN.md
 * 11.61): MENU+L1 takes a screenshot of the screen, MENU+L2 locks the
 * handheld (2026-11-22: "Ability to screenshot on all screens, maybe
 * menu+l1", "Ability to lock device on all screens, maybe menu+l2"). The
 * keys and how long the unlock is held are MENU > System > Hotkeys' last
 * rows. Pure: no SDL, no database; src/main.c keeps the table and acts.
 *
 * MENU opens on its release on both handhelds (the Pixel 2's has since
 * 2026-10-01, its FUNCTION key being the brightness chord's too, and the
 * Brick's since these), so a button pressed while MENU is held is MENU's
 * chord, and MENU then opens nothing. Only the buttons these take are
 * chords: the rest go through as they always did.
 *
 * In a game too: there MENU is Diatom's, and RUN menukeys= gives it these
 * two (mk_run; its chord.h's mchord), the screenshot the game's frame as
 * SELECT's hotkey takes it, the lock a pause (PAUSED for=lock) the launcher
 * locks over (game_lock). Never START,
 * which is Home with MENU (MENU+START held, tools/zyhome.c), nor SELECT,
 * the d-pad or a volume key (MENU+volume is the Pixel 2's brightness). The
 * buttons are src/hotkeys.h's, by the same names. */
#include "hotkeys.h"

typedef enum { MK_SHOT, MK_LOCK, MK_ACTS } mk_act;

/* How long SELECT+START is held to unlock, the choices, in ms; 2 s the one
 * from the start, MP4's lock's as it was (DS_MS_HOLD_UNLOCK). Half a second
 * the shortest (the Pixel 2 test of 1.1.1-test.3: "add a 0.5 s option") */
#define MK_HOLDS      5
extern const unsigned MK_HOLD_MS[MK_HOLDS];
#define MK_HOLD_DEFAULT 2000

typedef struct {
	signed char key[MK_ACTS];     /* HK_A ... HK_R2 */
	unsigned    hold_ms;          /* one of MK_HOLD_MS */
} mk_table;

/* ZyperOS's own: MENU+L1 a screenshot, MENU+L2 the lock, 2 s to unlock */
void mk_default(mk_table *t);

/* Whether `key` (HK_*) may go with MENU: A, B, X, Y and the four shoulders */
bool mk_key_ok(int key);

/* The action `key` is on, or -1 */
int mk_act_on(const mk_table *t, int key);

/* a takes key; the other action, if it had key, takes a's old one, so
 * neither is lost (src/hotkeys.h's swap). A key that cannot go with MENU
 * changes nothing. */
void mk_swap(mk_table *t, mk_act a, int key);

/* The next hold, round: dir +1 or -1 */
unsigned mk_hold_step(unsigned ms, int dir);

/* What is kept: "1;shot:l1,lock:l2,hold:2000", "" when t is ZyperOS's own;
 * and back, ZyperOS's own for "" or anything it cannot read (false then) */
void mk_store(const mk_table *t, char *out, size_t n);
bool mk_load(const char *stored, mk_table *out);

/* A hold's seconds as the pages write them: "0.5", "2"; with `comma` the
 * decimal comma ("0,5") of the languages that write one */
void mk_secs(unsigned ms, bool comma, char *out, size_t n);

/* What RUN says to Diatom for a game (menukeys=, its chord.h's mchord):
 * "shot:l1,lock:l2", the keys in force, so they work in a game too */
void mk_run(const mk_table *t, char *out, size_t n);

/* "MENU+L1", the action's chord as the pages write it */
void mk_chord(const mk_table *t, mk_act a, char *out, size_t n);

#endif
