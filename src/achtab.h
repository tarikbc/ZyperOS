/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ACHTAB_H
#define ZYPEROS_ACHTAB_H

/* The in-game menu's Achievements tab, as words and rows (1.0.5): which state
 * the running game's set is in, the line of counts over its list, and each
 * achievement's row - its title, its points (or its count while one moves),
 * an earned one in the earned color. The list over the shelf (main.c's
 * cheevos_screen, from the game's details) builds its rows here too, so the
 * two cannot say different things about the same achievement.
 *
 * The tab never hides. A game with no list says why, in a sentence, where
 * the list would be: no hash for its system or for its kind of file
 * (src/rahash.h), nobody signed in, the set on its way, never played with a
 * network, or RetroAchievements has none.
 *
 * No SDL: tools/achtab-check.c holds every state and row to it.
 * src/achtab_screen.h draws it. */

#include <stdbool.h>
#include <stddef.h>

#include "cheevos.h"
#include "i18n.h"
#include "menu.h"
#include "rahash.h"

typedef enum {
	AT_LIST,          /* a set on the card: the list */
	AT_NOT_SYSTEM,    /* no hash for any file this system takes */
	AT_NOT_FILE,      /* no hash for this kind of file; another kind has one */
	AT_SIGNED_OUT,    /* nobody signed in, and no set on the card */
	AT_WAITING,       /* the set is being looked for, behind the game */
	AT_UNCHECKED,     /* never played with a network: not asked yet */
	AT_NONE,          /* asked: RetroAchievements has none for this copy */
	AT_STATES
} at_state;

/* What the tab needs to know, gathered by its caller (main.c: chv_*,
 * ra_signed_in, the fetch in flight, chv_state_of, ra_hash_support) */
typedef struct {
	int  count;            /* achievements in the set, 0 for no set */
	int  earned;           /* of them, earned (this session too) */
	int  points, points_total;
	int  pending;          /* earned offline, not sent yet */
	bool signed_in;
	bool fetching;         /* this game's set is being fetched now */
	chv_state set;         /* chv_state_of the game's set path */
	ra_hash_kind no_hash;  /* ra_hash_support of the game's file */
} at_facts;

at_state at_state_of(const at_facts *f);

/* The sentence said where a list would be, for each state but AT_LIST */
str_id at_message(at_state s);

/* The line over the list: "3 / 36 · 25 / 415 points", "3 / 36 · 2 waiting
 * to send" while some wait, "Mastered · 415 points" when all are earned and
 * sent. Into `out`, which it returns. */
const char *at_counts(const at_facts *f, char *out, size_t n);

/* The value color main.c's VCOL_EARNED is (src/panel.h, menu_style.vcolors) */
#define AT_VCOL_EARNED 1u

/* One achievement's row: its title, and in `val` (`vn` long) its points, or
 * its count while one moves ("74 / 100"); live and the earned color when
 * earned, before or during this game. The list is drawn with every row
 * reachable (visits_all), so live is about how it reads, not where the
 * cursor can go. */
void at_row(const cheevo *c, menu_row *row, unsigned *vcol, char *val, size_t vn);

#endif
