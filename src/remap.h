/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_REMAP_H
#define ZYPEROS_REMAP_H

#include <stdbool.h>
#include <stddef.h>

/* The rules of button remapping, pure: no SDL, no database. A table says what
 * each of the handheld's buttons gives the core, in Diatom's map language
 * (diatom/src/env.c, diatom_input_set_map): `from:to`, `none`, a turbo
 * `x:a~3`, and a layer `held+from:to`. Design:
 * docs/superpowers/specs/2026-11-13-button-remap-design.md, sections 3 and 6. */

/* The handheld's buttons, in the order the Controls tab lists them */
enum { RM_A, RM_B, RM_X, RM_Y, RM_L1, RM_R1, RM_L2, RM_R2, RM_START, RM_SELECT, RM_BUTTONS };

/* What a button can give: Diatom's canonical names in its order. The d-pad
 * is a target and never a button of the table. */
enum {
	RT_UP, RT_DOWN, RT_LEFT, RT_RIGHT, RT_A, RT_B, RT_X, RT_Y,
	RT_L1, RT_R1, RT_L2, RT_R2, RT_SELECT, RT_START, RT_COUNT
};

/* Nothing: the button gives the game no button */
#define RM_NONE (-1)

/* The half-period of a turbo, in frames, that every turbo choice uses */
#define RM_TURBO 3

typedef struct {
	signed char   to;      /* a target (RT_*), or RM_NONE */
	unsigned char turbo;   /* half-period in frames, 0 plain */
} rm_choice;

typedef struct {
	rm_choice b[RM_BUTTONS];
	/* The `held+from:to` pairs verbatim, joined by commas, never edited: the
	 * N64's C buttons live here */
	char layer[160];
} rm_table;

/* The target a button gives when nothing is changed */
int rm_own(int button);

/* A table of nothing changed */
void rm_identity(rm_table *t);

/* SETMAP's form read over the identity, a later pair for the same button
 * winning, as Diatom does. False, out untouched, on any bad pair. */
bool rm_parse(const char *spec, rm_table *out);

/* The whole table in SETMAP's form: the pairs that differ from the identity,
 * then the layer; "identity" when there is neither. */
void rm_format(const rm_table *t, char *out, size_t n);

/* What is kept for a scope: "1;" and the pairs where t differs from def, as
 * "a:b,x:a~3"; "" when t equals def. */
void rm_store(const rm_table *def, const rm_table *t, char *out, size_t n);

/* def with a stored string's pairs over it. False, out = def, for another
 * version or a bad pair. Nothing stored ("" or NULL) is def, and true. */
bool rm_load(const rm_table *def, const char *stored, rm_table *out);

bool rm_equal(const rm_table *a, const rm_table *b);

/* Whether a button gives something other than def's */
bool rm_changed(const rm_table *def, const rm_table *t, int button);

/* t becomes base with one change: button gives c. When c is plain and
 * another button Q gives c's target plainly in base, Q takes what button gave
 * in base (a swap, not a copy). Turbo and nothing never swap. Worked from
 * base, the table when the cursor came onto the row, so steps never pile up. */
void rm_set(rm_table *t, const rm_table *base, int button, rm_choice c);

/* The targets def reaches, plain or turbo, that t reaches nowhere, in target
 * order, at most max. The count. */
int rm_unreached(const rm_table *def, const rm_table *t, int *targets, int max);

/* Writes the four face buttons from layout and leaves the rest */
void rm_faces(rm_table *t, const rm_table *layout);

/* 0 when the faces are letters' (By Letter), 1 when they are the identity's
 * (By Place), 2 when they are neither (Mixed) */
int rm_faces_of(const rm_table *t, const rm_table *letters);

#endif
