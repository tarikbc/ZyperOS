/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HOTKEYS_H
#define ZYPEROS_HOTKEYS_H

#include <stdbool.h>
#include <stddef.h>

/* The hotkeys a player sets (DESIGN.md 11.61), pure: no SDL, no
 * database. Every hotkey is SELECT held and one more button (my pick in the
 * Controls lab, 2026-11-19: MENU is never a holder, so MENU stays one press),
 * which Diatom's chord.h judges, its keys from RUN hotkeys= and SETHOTKEYS.
 * An action has one chord and a chord does one action: a clash is a swap.
 *
 * The first four are ZyperOS's from the start (SELECT+R1 quick save, +L1
 * quick load, +R2 faster, +L2 slower); the rest are None until set. Home
 * (MENU+START held) and POWER are no hotkeys here: they never change.
 *
 * Fast Forward (2026-11-23, asked for on the Discord) is last here, so the
 * actions keep their numbers, and listed after Slower (hk_order). */
typedef enum {
	HK_SAVE, HK_LOAD, HK_FASTER, HK_SLOWER,
	HK_SHOT, HK_SHADER, HK_DISPLAY, HK_RESET, HK_QUIT,
	HK_FFWD,
	HK_ACTS
} hk_act;

/* The actions in the order the lists show them: Fast Forward beside the
 * speed's steps */
extern const hk_act hk_order[HK_ACTS];

/* Fast Forward's speed, the one it turns on from 1x: 1.5x, 2x, 3x or Max
 * (Diatom's SPEED_HALVES past 1x), 2x from the start */
enum { HK_FF_15, HK_FF_2, HK_FF_3, HK_FF_MAX, HK_FF_STEPS };
#define HK_FF_DEFAULT HK_FF_2

/* The buttons SELECT can be held with, in the order the capture screen reads
 * them: never SELECT (the holder), MENU (one press, the menu) or the d-pad
 * (the game's, and the stick's SELECT rule) */
enum { HK_A, HK_B, HK_X, HK_Y, HK_L1, HK_R1, HK_L2, HK_R2, HK_START, HK_KEYS };

/* No key: the action is None */
#define HK_NONE (-1)

typedef struct {
	signed char key[HK_ACTS];     /* HK_A ... HK_START, or HK_NONE */
	signed char ff;               /* Fast Forward's speed, HK_FF_* */
} hk_table;

/* ZyperOS's own: the four with their shoulders, the rest None, Fast
 * Forward's speed 2x */
void hk_default(hk_table *t);

bool hk_equal(const hk_table *a, const hk_table *b);

/* A button as printed and as Diatom names it: "R1" and "r1"; NULL for none */
const char *hk_key_name(int key);
const char *hk_key_map(int key);

/* An action as Diatom names it ("save", "shot") */
const char *hk_act_name(hk_act a);

/* Fast Forward's speed as Diatom's RUN ffspeed= and SETHOTKEYS ffspeed=
 * and the store say it: "1.5", "2", "3", "max"; and in halves of the normal
 * speed as SPEED says it (3, 4, 6), 0 for Max */
const char *hk_ff_word(int ff);
int hk_ff_half(int ff);

/* The action key `key` is on in t, other than `not`; -1 for none */
int hk_act_on(const hk_table *t, int key, int not);

/* t with action a on key (HK_NONE for None), nothing else touched; a
 * clash is the caller's to ask about first (hk_act_on) */
void hk_set(hk_table *t, hk_act a, int key);

/* The swap: a takes key, and the action that had key takes a's old key
 * (None if a had none), so no hotkey is lost by accident */
void hk_swap(hk_table *t, hk_act a, int key);

/* What is kept for a scope: "1;" and the actions where t differs from
 * ZyperOS's own, "shot:y,save:none", and Fast Forward's speed where it does,
 * "ffspeed:max"; "" when t is ZyperOS's own */
void hk_store(const hk_table *t, char *out, size_t n);

/* ZyperOS's own with a stored string over it. False, out = ZyperOS's own,
 * for another version or a bad pair; nothing stored ("" or NULL) is true. A
 * key given twice keeps the first action's. */
bool hk_load(const char *stored, hk_table *out);

/* Diatom's RUN hotkeys= and SETHOTKEYS keys=: every action with a key,
 * "save:r1,load:l1,shot:y", or "none" for none at all */
void hk_run(const hk_table *t, char *out, size_t n);

/* "SELECT+R1", the action's chord as the pages and glyphs write it, into
 * out; "" for None */
void hk_chord(const hk_table *t, hk_act a, char *out, size_t n);

#endif
