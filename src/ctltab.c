/* SPDX-License-Identifier: GPL-3.0-only */
/* See ctltab.h. The Controls tab's rows and steps, and nothing else. */
#include "ctltab.h"

#include <stdio.h>
#include <string.h>

#include "controls.h"
#include "i18n.h"

/* The handheld's buttons as printed, in the tab's order (RM_*) */
static const char *const NAME[RM_BUTTONS] = {
	"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT"
};

/* ...and the key of the handheld each one is */
static const td_key KEY[RM_BUTTONS] = {
	TD_A, TD_B, TD_X, TD_Y, TD_L1, TD_R1, TD_L2, TD_R2, TD_START, TD_SELECT
};

const char *ct_button_name(int button)
{
	return button >= 0 && button < RM_BUTTONS ? NAME[button] : "";
}

td_key ct_key_of(int button)
{
	return button >= 0 && button < RM_BUTTONS ? KEY[button] : TD_KEYS;
}

int ct_button_of(ct_row id)
{
	return id >= CT_BUTTON && id < CT_BUTTON + RM_BUTTONS ? (int)(id - CT_BUTTON) : -1;
}

static bool same(rm_choice a, rm_choice b)
{
	return a.to == b.to && a.turbo == b.turbo;
}

bool ct_says_nothing(const ct_model *m, rm_choice c)
{
	char said[64];

	if (c.to == RM_NONE) return true;
	ctl_remap_say(m->tag, m->ext, c, said, sizeof said);
	return !said[0];
}

bool ct_select_held_c(const rm_table *t)
{
	return t && strstr(t->layer, "select+") != NULL;
}

void ct_sent(const ct_model *m, rm_table *out)
{
	*out = m->cur;
	if (m->select_held_dpad) out->b[RM_SELECT] = (rm_choice){ (signed char)rm_own(RM_SELECT), 0 };
}

/* A dead SELECT row: the stick's rule or the N64's C buttons hold it */
static bool select_dead(const ct_model *m)
{
	return m->select_held_c || m->select_held_dpad;
}

/* ---- the faces ------------------------------------------------------------- */

/* The faces By Letter moves: a Dreamcast's four, a Pocket's two (its X and
 * Y are ZyperOS's turbo, which no layout of letters touches) */
static unsigned face_mask(const ct_model *m)
{
	unsigned k = 0;
	int i;

	for (i = RM_A; i <= RM_Y; i++)
		if (m->letters.b[i].to != rm_own(i) || m->letters.b[i].turbo) k |= 1u << i;
	return k;
}

int ct_faces(const ct_model *m, const rm_table *t)
{
	const unsigned k = face_mask(m);
	rm_table u = *t;
	int i;

	/* Those it leaves out read as both layouts' own */
	for (i = RM_A; i <= RM_Y; i++)
		if (!(k >> i & 1u)) u.b[i] = m->letters.b[i];
	return rm_faces_of(&u, &m->letters);
}

void ct_faces_set(const ct_model *m, rm_table *t, int layout)
{
	const unsigned k = face_mask(m);
	int i;

	for (i = RM_A; i <= RM_Y; i++)
		if (k >> i & 1u)
			t->b[i] = layout == 0 ? m->letters.b[i] : (rm_choice){ (signed char)rm_own(i), 0 };
}

int ct_faces_next(int now)
{
	return now == 0 ? 1 : 0;
}

static const char *faces_said(int f)
{
	return T(f == 0 ? S_CONTROLS_BY_LETTER : f == 1 ? S_CONTROLS_BY_PLACE : S_CONTROLS_FACES_MIXED);
}

/* ---- the rows ---------------------------------------------------------------- */

bool ct_reset_live(const ct_model *m)
{
	if (!rm_equal(&m->cur, &m->def)) return true;
	return !m->scope_all && m->stick_offered && m->stick_on != m->stick_default;
}

/* Whether button `i` can be what lost a game button: a changed one that
 * gives no game button plainly in what is sent - Nothing, a turbo, one its
 * core names nothing - or a SELECT the stick's rule sends as SELECT. A
 * plain choice swaps, so it loses nothing, and a button as its default has
 * it lost nothing. */
static bool lossy(const ct_model *m, const rm_table *sent, int i)
{
	const rm_choice c = sent->b[i];

	if (!rm_changed(&m->def, &m->cur, i)) return false;
	return c.turbo || ct_says_nothing(m, c) || !same(c, m->cur.b[i]);
}

/* The row each lost game button's warning goes under: the button whose
 * change caused it. One the stick's rule sends otherwise, if it is what
 * gave it; else one that gives no game button now - the one that gave it
 * by default if that is one of them, else the first of them not taken yet;
 * failing those its default's giver, else A. */
static void causes(const ct_model *m, const rm_table *sent, const int *lost, int nlost, int *under)
{
	bool taken[RM_BUTTONS] = { false };
	int j, i;

	for (j = 0; j < nlost; j++) {
		under[j] = -1;
		for (i = 0; i < RM_BUTTONS && under[j] < 0; i++)
			if (!same(sent->b[i], m->cur.b[i]) && m->cur.b[i].to == lost[j]) {
				under[j] = i;
				taken[i] = true;
			}
	}
	for (j = 0; j < nlost; j++) {
		for (i = 0; i < RM_BUTTONS && under[j] < 0; i++)
			if (!taken[i] && lossy(m, sent, i) && m->def.b[i].to == lost[j]) {
				under[j] = i;
				taken[i] = true;
			}
	}
	for (j = 0; j < nlost; j++) {
		for (i = 0; i < RM_BUTTONS && under[j] < 0; i++)
			if (!taken[i] && lossy(m, sent, i)) {
				under[j] = i;
				taken[i] = true;
			}
		for (i = 0; i < RM_BUTTONS && under[j] < 0; i++)
			if (m->def.b[i].to == lost[j]) under[j] = i;
		if (under[j] < 0) under[j] = RM_A;
	}
}

int ct_rows(const ct_model *m, ct_row *ids, menu_row *out, unsigned *vcol, ct_bufs *b)
{
	int lost[RT_COUNT], under[RT_COUNT], nlost, n = 0, i, j;
	rm_table sent;

#define ROW(id_, r_, c_) do { ids[n] = (id_); out[n] = (r_); vcol[n] = (c_); n++; } while (0)
	/* Applies To: This Game, or the system by its name; a game with its own
	 * table says so in accent-ink, whichever it shows */
	snprintf(b->scope, sizeof b->scope, "%s",
	         !m->scope_all ? T(S_GAMEMENU_THIS_GAME) : m->sys_label ? m->sys_label : m->tag);
	ROW(CT_SCOPE, ((menu_row){ T(S_GAMEMENU_APPLIES_TO), b->scope, !m->fixed_game }),
	    m->game_has_own ? 1u : 0u);
	if (m->scope_all && m->game_has_own)
		ROW(CT_OWN_NOTE, ((menu_row){ T(S_GAMEMENU_OWN_BUTTONS), NULL, false }), 0u);
	if (m->stick_offered)
		ROW(CT_STICK, ((menu_row){ T(S_CONTROLS_DPAD_STICK_TITLE),
		                           T(m->stick_on ? S_MENU_ON_TITLE : S_MENU_OFF_TITLE), true }),
		    m->stick_on != m->stick_default ? 1u : 0u);
	if (m->pak) ROW(CT_PAK, ((menu_row){ T(S_GAMEMENU_PAK), m->pak, true }), 0u);
	if (m->face_offered) {
		const int f = ct_faces(m, &m->cur);

		ROW(CT_FACE, ((menu_row){ T(S_CONTROLS_FACE_BUTTONS), faces_said(f), true }),
		    f != ct_faces(m, &m->def) ? 1u : 0u);
	}

	/* The game buttons left with none by what is sent, each under the
	 * button whose change caused it */
	ct_sent(m, &sent);
	nlost = rm_unreached(&m->def, &sent, lost, RT_COUNT);
	causes(m, &sent, lost, nlost, under);
	for (i = 0; i < RM_BUTTONS; i++) {
		const rm_choice c = m->cur.b[i];

		if (i == RM_SELECT && (m->select_held_c || m->select_held_dpad)) {
			ROW(CT_BUTTON + i, ((menu_row){ NAME[i], T(m->select_held_c ? S_CONTROLS_SELECT_C_BUTTONS
			                                                       : S_CONTROLS_HELD_DPAD), false }), 0u);
		} else {
			if (ct_says_nothing(m, c)) snprintf(b->val[i], sizeof b->val[i], "%s", T(S_CONTROLS_NOTHING));
			else ctl_remap_say(m->tag, m->ext, c, b->val[i], sizeof b->val[i]);
			ROW(CT_BUTTON + i, ((menu_row){ NAME[i], b->val[i], true }),
			    rm_changed(&m->def, &m->cur, i) ? 1u : 0u);
		}
		for (j = 0; j < nlost; j++) {
			const int t = lost[j];
			char said[64];

			if (under[j] != i) continue;
			/* A target the core names nothing is no game button to lose */
			ctl_remap_say(m->tag, m->ext, (rm_choice){ (signed char)t, 0 }, said, sizeof said);
			if (!said[0]) continue;
			snprintf(b->warn[t], sizeof b->warn[t], T(S_CONTROLS_NO_BUTTON_NOW), said);
			ROW((ct_row)(CT_WARN + t), ((menu_row){ b->warn[t], NULL, false }), 0u);
		}
	}
	ROW(CT_RESET, ((menu_row){ T(S_GAMEMENU_RESET_BUTTONS), NULL, ct_reset_live(m) }), 0u);
	ROW(CT_HOTKEYS, ((menu_row){ ctl_page_name(CTL_HOTKEYS), NULL, true }), 0u);
#undef ROW
	return n;
}

unsigned ct_lit(const ct_model *m, ct_row id)
{
	const int bt = ct_button_of(id);
	unsigned k = 0;
	int i;

	if (bt >= 0) {
		/* Its button, and while the cursor stays on the row the one a swap
		 * gave its old value to */
		k = 1u << KEY[bt];
		for (i = 0; i < RM_BUTTONS; i++)
			if (i != bt && !same(m->cur.b[i], m->base.b[i])) k |= 1u << KEY[i];
		return k;
	}
	switch (id) {
	case CT_STICK:
		/* SELECT where the stick's rule takes it: then the d-pad held
		 * under it is the system's own d-pad */
		k = 1u << TD_DPAD;
		if (m->stick_on && (m->select_held_c || m->select_held_dpad)) k |= 1u << TD_SELECT;
		return k;
	case CT_FACE:
		for (i = RM_A; i <= RM_Y; i++)
			if (face_mask(m) >> i & 1u) k |= 1u << KEY[i];
		return k;
	case CT_RESET:
		for (i = 0; i < RM_BUTTONS; i++)
			if (rm_changed(&m->def, &m->cur, i)) k |= 1u << KEY[i];
		return k;
	default:
		return 0;
	}
}

/* ---- the steps ------------------------------------------------------------- */

int ct_choice_at(const ct_model *m, const rm_choice *ch, int n, rm_choice c)
{
	const bool none = ct_says_nothing(m, c);
	int i;

	for (i = 0; i < n; i++)
		if (none ? ch[i].to == RM_NONE : same(ch[i], c)) return i;
	return -1;
}

rm_choice ct_next(const ct_model *m, const rm_choice *ch, int n, int button, int dir)
{
	int at;

	if (n <= 0) return m->cur.b[button];
	at = ct_choice_at(m, ch, n, m->cur.b[button]);
	if (at < 0) return ch[dir > 0 ? 0 : n - 1];
	return ch[(at + (dir > 0 ? 1 : n - 1)) % n];
}

rm_choice ct_taken(const ct_model *m, int button, rm_choice c)
{
	if (c.to == RM_NONE && ct_says_nothing(m, m->def.b[button])) return m->def.b[button];
	return c;
}

void ct_pick(ct_model *m, int button, rm_choice c)
{
	rm_table from = m->base;
	const bool dead = select_dead(m) && button != RM_SELECT;

	/* A held SELECT is no button to swap with: it gives nothing a swap
	 * could find, and keeps what it gave */
	if (dead) from.b[RM_SELECT] = (rm_choice){ RM_NONE, 0 };
	rm_set(&m->cur, &from, button, ct_taken(m, button, c));
	if (dead) m->cur.b[RM_SELECT] = m->base.b[RM_SELECT];
}

bool ct_step(ct_model *m, ct_row id, int dir)
{
	const int bt = ct_button_of(id);
	rm_choice ch[32];
	int n;

	if (bt < 0 || (bt == RM_SELECT && select_dead(m))) return false;
	n = ctl_remap_choices(m->tag, m->ext, m->map, ch, 32);
	if (n <= 0) return false;
	ct_pick(m, bt, ct_next(m, ch, n, bt, dir));
	return true;
}

/* ---- a button's choices ---------------------------------------------------------- */

int ct_pick_rows(const ct_model *m, int button, const rm_choice *ch, int n,
                 menu_row *out, ct_pick_bufs *b, int *sel)
{
	const int now = ct_choice_at(m, ch, n, m->cur.b[button]);
	const int def = ct_choice_at(m, ch, n, m->def.b[button]);
	int i;

	snprintf(b->head, sizeof b->head, T(S_CONTROLS_PICK_HEADING), ct_button_name(button),
	         m->sys_name ? m->sys_name : m->sys_label ? m->sys_label : m->tag);
	if (n > 32) n = 32;
	for (i = 0; i < n; i++) {
		if (ch[i].to == RM_NONE) snprintf(b->say[i], sizeof b->say[i], "%s", T(S_CONTROLS_NOTHING));
		else ctl_remap_say(m->tag, m->ext, ch[i], b->say[i], sizeof b->say[i]);
		out[i] = (menu_row){ b->say[i], i == now ? MENU_TICK : i == def ? T(S_CONTROLS_DEFAULT) : NULL,
		                     true };
	}
	*sel = now >= 0 ? now : 0;
	return n;
}
