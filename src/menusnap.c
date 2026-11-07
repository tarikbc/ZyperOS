/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "menusnap.h"

#include <string.h>

/* `t` copied into the text; the row marks and NULL as they are. NULL, and
 * the copy no longer ok, when there is no room left for it. */
static const char *keep(menu_snap *s, const char *t)
{
	size_t len;
	char *d;

	if (!t || t == MENU_NOTE_MARK || t == MENU_BODY_MARK || t == MENU_HR_MARK || t == MENU_TICK)
		return t;
	len = strlen(t) + 1;
	if (len > sizeof s->text - s->used) { s->ok = false; return NULL; }
	d = s->text + s->used;
	memcpy(d, t, len);
	s->used += len;
	return d;
}

bool menusnap_take(menu_snap *s, const char *heading, const menu_row *rows,
                   int n, int sel, int fixed_w, unsigned accent,
                   const unsigned *vcolors, bool visits_all, int loop_at,
                   const zr_rig *zy, unsigned zy_ahead)
{
	int i;

	s->ok = n >= 0 && n <= MENUSNAP_ROWS;
	s->used = 0;
	if (!s->ok) return false;
	s->heading = keep(s, heading);
	s->note = NULL;
	for (i = 0; i < n && s->ok; i++) {
		s->rows[i].label = keep(s, rows[i].label);
		s->rows[i].value = keep(s, rows[i].value);
		s->rows[i].live  = rows[i].live;
		s->vcolors[i]    = vcolors ? vcolors[i] : 0u;
	}
	s->has_vcolors = vcolors != NULL;
	s->visits_all  = visits_all;
	s->n = n;
	s->sel = sel;
	s->fixed_w = fixed_w;
	s->loop_at = loop_at;
	s->accent = accent;
	s->zyper = zy != NULL;
	if (zy) s->zy = *zy;
	s->zy_ahead = zy_ahead;
	return s->ok;
}

bool menusnap_note(menu_snap *s, const char *note)
{
	if (!s->ok) return false;
	s->note = keep(s, note);
	return s->ok;
}
