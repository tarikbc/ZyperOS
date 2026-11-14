/* SPDX-License-Identifier: GPL-3.0-only */
/* See achtab.h. */
#include <stdio.h>
#include <string.h>

#include "achtab.h"

at_state at_state_of(const at_facts *f)
{
	/* A set on the card is the list, whatever else is true: a card can
	 * carry sets from tools/ra-sets.py with nobody signed in */
	if (f->count > 0) return AT_LIST;
	/* No hash is final, so said before anything a network could change */
	if (f->no_hash == RA_HASH_NO_SYSTEM) return AT_NOT_SYSTEM;
	if (f->no_hash == RA_HASH_NO_FILE) return AT_NOT_FILE;
	/* Then whether anybody could ask: chv_state_of says "none" with nobody
	 * signed in, which would be wrong here - nobody asked */
	if (!f->signed_in) return AT_SIGNED_OUT;
	if (f->fetching) return AT_WAITING;
	if (f->set == CHV_UNCHECKED) return AT_UNCHECKED;
	return AT_NONE;
}

str_id at_message(at_state s)
{
	switch (s) {
	case AT_NOT_SYSTEM: return S_ACHTAB_NOT_SYSTEM;
	case AT_NOT_FILE:   return S_ACHTAB_NOT_FILE;
	case AT_SIGNED_OUT: return S_ACHTAB_SIGNED_OUT;
	case AT_WAITING:    return S_ACHTAB_WAITING;
	case AT_UNCHECKED:  return S_CHEEVOS_UNCHECKED_SAID;
	default:            return S_ACHTAB_NONE;
	}
}

const char *at_counts(const at_facts *f, char *out, size_t n)
{
	if (f->pending > 0)
		snprintf(out, n, TN(S_ACHTAB_COUNTS_WAITING, f->pending),
		         f->earned, f->count, f->pending);
	else if (f->count > 0 && f->earned >= f->count)
		snprintf(out, n, T(S_CHEEVOS_MASTERED), f->points_total);
	else
		snprintf(out, n, T(S_ACHTAB_COUNTS), f->earned, f->count,
		         f->points, f->points_total);
	return out;
}

void at_row(const cheevo *c, menu_row *row, unsigned *vcol, char *val, size_t vn)
{
	bool got = c->earned || c->earned_now;
	const char *slash = strchr(c->progress, '/');

	/* A count moving, "74 / 100", where the points are, until it is earned
	 * (main.c's on_cheevo_progress) */
	if (!got && c->progress[0] && slash)
		snprintf(val, vn, "%.*s / %s", (int)(slash - c->progress), c->progress, slash + 1);
	else if (!got && c->progress[0])
		snprintf(val, vn, "%s", c->progress);
	else
		snprintf(val, vn, "%d", c->points);
	row->label = c->title;
	row->value = val;
	/* Said by color on EVERY earned row, not only the one under the cursor:
	 * live alone lifts the label one step, too little to scan a hundred by */
	row->live = got;
	if (vcol) *vcol = got ? AT_VCOL_EARNED : 0u;
}
