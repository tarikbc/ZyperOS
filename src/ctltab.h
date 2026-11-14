/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CTLTAB_H
#define ZYPEROS_CTLTAB_H

#include <stdbool.h>
#include <stddef.h>

#include "menu.h"
#include "remap.h"
#include "tdkey.h"

/* The in-game menu's Controls tab, pure: its rows from what a game's buttons
 * are (a model the launcher gathers, src/ctltab_screen.c), which keys of the
 * handheld each row is about, and the steps a row's value takes. No SDL and
 * no database, so tools/menu-check.c holds it. Design:
 * docs/superpowers/specs/2026-11-13-button-remap-design.md, sections 2 to 7.
 *
 * The rows, in this order, those in brackets only where they apply:
 *
 *   Applies To        This Game, or the system (every game of it, by its
 *                     name, PlayStation); its value in accent-ink when this
 *                     game has buttons of its own
 *   [the note]        "This game keeps its own buttons.", dead, under it,
 *                     while it says the system and the game has its own
 *   [D-pad as Stick]  On or Off, always this game's
 *   [Pak]             an N64 game's, as before
 *   [Face Buttons]    By Letter, By Place, or Mixed
 *   A ... SELECT      the handheld's button, and what it gives the game;
 *                     a changed one in accent-ink. SELECT is a dead row on
 *                     the N64 ("C buttons") and on the Dreamcast with the
 *                     stick on ("Held: D-pad")
 *   [warnings]        "× has no button now", dead, right under the button
 *                     whose change left a game button with none, by what
 *                     the game is sent
 *   Reset Buttons     live only when the shown scope differs from its own
 *   Hotkeys           A opens the read-only pages */

/* A row's identity, whatever its place: a warning is CT_WARN plus the
 * target (RT_*) it is about */
typedef enum {
	CT_SCOPE, CT_OWN_NOTE, CT_STICK, CT_PAK, CT_FACE, CT_BUTTON,
	CT_WARN = CT_BUTTON + RM_BUTTONS, CT_RESET = CT_WARN + 16, CT_HOTKEYS, CT_MAX
} ct_row;

/* Each identity at most once, and room for the quiet rows' lines: the
 * screen wraps one too long for the column onto lines of its own
 * (src/ctltab_screen.c) */
#define CT_MAX_ROWS (CT_MAX + 16)

typedef struct {
	const char *tag, *ext;      /* the system's, and the game's extension */
	/* Applies To's value for every game of the system: its name, or its
	 * home label where the name does not fit beside Applies To (measured
	 * by the screen) */
	const char *sys_label;
	const char *sys_name;       /* its name, for a button's choices' heading */
	/* Applies To says the system; the game has a table of its own; and
	 * Applies To cannot change (a game's details, with no game running) */
	bool        scope_all, game_has_own, fixed_game;
	/* The shown scope's table, and its default: the system's table in force
	 * for This Game, ZyperOS's for the system */
	rm_table    cur, def;
	bool        stick_offered, stick_on;
	/* The Pak row's value, as gm_pak_name says it; NULL for no row */
	const char *pak;
	/* Face Buttons is offered (ctl_letter_layout), and its By Letter */
	bool        face_offered;
	rm_table    letters;
	/* SELECT is held for the N64's C buttons; for the Dreamcast's own d-pad
	 * while the stick is on */
	bool        select_held_c, select_held_dpad;
	/* What the design's model leaves to the screen: the system's map| spec,
	 * which orders the faces among the choices (ctl_remap_choices); the
	 * stick's default (a value off it is changed, and This Game's Reset puts
	 * it back); and `base`, the table as it was when the cursor came onto
	 * its row (rm_set's), so a button that differs from it besides the
	 * row's own is the row's swap partner. */
	const char *map;
	bool        stick_default;
	rm_table    base;
} ct_model;

/* What the rows point into: the caller's, as menu_row holds pointers */
typedef struct {
	char scope[64];
	char val[RM_BUTTONS][48];
	char warn[16][96];
} ct_bufs;

/* The tab's rows, their identities in `ids` and each value's color (0, or
 * VCOL_EARNED, 1: accent-ink) in `vcol`, each CT_MAX_ROWS long; the count */
int ct_rows(const ct_model *m, ct_row *ids, menu_row *out, unsigned *vcol, ct_bufs *b);

/* The keys of the handheld row `id` is about, 1u << TD_*: a button its own
 * and its swap partner's (the buttons that differ from `base` besides its
 * own); D-pad as Stick the d-pad, and SELECT where the stick's rule takes it
 * (the N64's and the Dreamcast's own d-pad); Face Buttons the faces it
 * sets; Reset Buttons every changed button; the rest none. */
unsigned ct_lit(const ct_model *m, ct_row id);

/* The button a button row is about (RM_*), -1 for another row */
int ct_button_of(ct_row id);

/* A button as printed on the handheld: "A", "L1", "SELECT"; and its key */
const char *ct_button_name(int button);
td_key ct_key_of(int button);

/* Reset Buttons' row is live: the shown table differs from its default, or
 * in This Game the stick does from its own */
bool ct_reset_live(const ct_model *m);

/* Whether a table's held layer is SELECT's: the N64's C buttons, which make
 * its SELECT row a dead one (ct_model.select_held_c) */
bool ct_select_held_c(const rm_table *t);

/* What the game is sent of the shown table: `cur`, but where the stick's
 * rule takes SELECT (select_held_dpad) a SELECT the player changed is
 * plain SELECT, as rms_sent sends it */
void ct_sent(const ct_model *m, rm_table *out);

/* ---- the steps -------------------------------------------------------------- */

/* Whether `c` gives this game nothing: Nothing, or a target its core names
 * nothing (an NES game's L1) */
bool ct_says_nothing(const ct_model *m, rm_choice c);

/* Where `c` is among the system's choices `ch` (ctl_remap_choices), a button
 * that gives nothing being Nothing; -1 for none */
int ct_choice_at(const ct_model *m, const rm_choice *ch, int n, rm_choice c);

/* The choice `dir` (+1 Right, -1 Left) steps to from what `button` gives in
 * `cur`, round the list; from a value not in it, Right is the first and
 * Left the last */
rm_choice ct_next(const ct_model *m, const rm_choice *ch, int n, int button, int dir);

/* What giving `button` choice `c` writes: `c`, but Nothing on a button whose
 * default gives this game nothing is the default's own, so stepping onto
 * Nothing there changes nothing and keeps no key */
rm_choice ct_taken(const ct_model *m, int button, rm_choice c);

/* Left or Right (`dir`) on button row `id`: `cur` becomes `base` (the table
 * when the cursor came onto the row) with the button stepped on from what
 * it gives in `cur`, so steps never pile up swaps and a step back undoes
 * one. False, `cur` untouched, for a row with no step (another row, or a
 * dead SELECT). */
bool ct_step(ct_model *m, ct_row id, int dir);

/* A choice taken from a button's list (A on it): `cur` becomes `base` with
 * `button` given `c`, one swap at most. Either way the swap's partner is
 * never a dead row: a held SELECT keeps what it gives, and the two give the
 * same. */
void ct_pick(ct_model *m, int button, rm_choice c);

/* Face Buttons' value: 0 By Letter, 1 By Place, 2 Mixed, over the faces
 * By Letter moves (a Pocket's two, a Dreamcast's four), the rest left out */
int ct_faces(const ct_model *m, const rm_table *t);

/* Writes `layout` (0 By Letter, 1 By Place) into the faces it moves, and
 * leaves the rest; a step from Mixed is to By Letter (ct_faces_next) */
void ct_faces_set(const ct_model *m, rm_table *t, int layout);
int ct_faces_next(int now);

/* ---- a button's choices (A on its row) ----------------------------------------- */

typedef struct {
	char head[96];
	char say[32][48];
} ct_pick_bufs;

/* The choices of `button` as rows: the core's name for each, a tick on what
 * it gives now and "default" on its default's (only the tick when they are
 * one); the heading "Y: PlayStation" in b->head, the system's name (its
 * label without one). `*sel` is the row of what
 * it gives now (the first when none). The count, at most 32. */
int ct_pick_rows(const ct_model *m, int button, const rm_choice *ch, int n,
                 menu_row *out, ct_pick_bufs *b, int *sel);

#endif
