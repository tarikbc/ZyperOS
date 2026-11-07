/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The on-screen keyboard's keys and the d-pad's moves between them. See
 * kbnav.h; tools/kbnav-check.c holds every move. */
#include "kbnav.h"
#include "design.h"
#include "i18n.h"

#include <stddef.h>

void kb_pop_press(kb_pop *p, unsigned now)
{
	float current = kb_pop_scale(p, now);

	p->from = p->active ? current : 0.92f;
	p->started = now;
	p->active = true;
}

float kb_pop_scale(kb_pop *p, unsigned now)
{
	unsigned elapsed = now - p->started;

	if (!p->active) return 1.0f;
	if (elapsed >= DS_MS_PRESS) {
		p->active = false;
		return 1.0f;
	}
	return p->from + (1.0f - p->from) * ds_out((float)elapsed / DS_MS_PRESS);
}

/* The action keys' widths, in tenths of the row less its gaps: the keyboard
 * lab's 2.2 / 1.7 / 1.4 / 2.2 / 2.5. Each is as wide as a button glyph and its
 * label at **meta** need (DESIGN.md 11.5), "unshift" and "done" beside START
 * the widest, and space, which carries only its mark, the narrowest. */
static const int FLEX[KB_ACTIONS] = { 22, 17, 14, 22, 25 };

#define PITCH (KB_KEY_W + KB_KEY_GAP)

/* Laid out by the running sum of the widths, each edge rounded once, so the
 * five keys and their four gaps come to exactly the grid's width and no
 * rounding is left over to land on the last key. */
void kb_action_span(int a, int *x, int *w)
{
	const int room = KB_GRID_W - (KB_ACTIONS - 1) * KB_KEY_GAP;
	int i, sum = 0, e0, e1;

	if (a < 0) a = 0;
	if (a >= KB_ACTIONS) a = KB_ACTIONS - 1;
	for (i = 0; i < a; i++) sum += FLEX[i];
	e0 = (room * sum + 50) / 100;
	e1 = (room * (sum + FLEX[a]) + 50) / 100;
	*x = e0 + a * KB_KEY_GAP;
	*w = e1 - e0;
}

/* Each key owns half of the gap on either side of it. */
int kb_col_under(int x)
{
	int c = (x + KB_KEY_GAP / 2) / PITCH;

	if (x + KB_KEY_GAP / 2 < 0 || c < 0) return 0;
	return c < KB_COLS ? c : KB_COLS - 1;
}

int kb_action_under(int x)
{
	int a, ax, aw;

	for (a = 0; a < KB_ACTIONS - 1; a++) {
		kb_action_span(a, &ax, &aw);
		if (x < ax + aw + KB_KEY_GAP / 2) return a;
	}
	return KB_ACTIONS - 1;
}

/* The center of grid column `c`, and of action key `a`. */
static int col_mid(int c) { return c * PITCH + KB_KEY_W / 2; }

static int act_mid(int a)
{
	int x, w;

	kb_action_span(a, &x, &w);
	return x + w / 2;
}

/* The keys in row `row`: ten on the grid, five on the action row. */
static int row_keys(int row)
{
	return row == KB_ROW_ACTIONS ? KB_ACTIONS : KB_COLS;
}

void kb_clamp(int *row, int *col)
{
	if (*row < 0) *row = 0;
	if (*row > KB_ROW_ACTIONS) *row = KB_ROW_ACTIONS;
	if (*col < 0) *col = 0;
	if (*col >= row_keys(*row)) *col = row_keys(*row) - 1;
}

/* Cleared before it is set, so a press and its release inside one poll
 * still count as a press on the key. */
bool kb_hold_armed(bool armed, bool on, bool pressed, bool down)
{
	if (!on || !down) armed = false;
	if (on && pressed) armed = true;
	return armed;
}

void kb_move(int *row, int *col, kb_dir d)
{
	int n;

	kb_clamp(row, col);
	n = row_keys(*row);

	/* Along a row, round and round: on the grid ten keys, on the action row
	 * five. */
	if (d == KB_LEFT)  { *col = (*col + n - 1) % n; return; }
	if (d == KB_RIGHT) { *col = (*col + 1) % n; return; }

	/* Out of the action row, up onto the last row or round the wrap down
	 * onto the digits, at the column under the key's center: the key a
	 * thumb was on is the place it expects to come out above or below. */
	if (*row == KB_ROW_ACTIONS) {
		*col = kb_col_under(act_mid(*col));
		*row = d == KB_UP ? KB_ROWS - 1 : 0;
		return;
	}

	/* Within the grid. */
	if (d == KB_UP && *row > 0)            { (*row)--; return; }
	if (d == KB_DOWN && *row < KB_ROWS - 1) { (*row)++; return; }

	/* Off the grid's last row going down, or off the digits going up round
	 * the wrap: onto the action key under the column's center. */
	*col = kb_action_under(col_mid(*col));
	*row = KB_ROW_ACTIONS;
}

/* Sticky, not held: a face button cannot comfortably be held while the
 * d-pad works on this shell. Pressing the layer you are on goes back to the
 * letters. */
int kb_layer_after(int layer, kb_action a)
{
	if (a == KB_ACT_SHIFT)   return layer == 1 ? 0 : 1;
	if (a == KB_ACT_SYMBOLS) return layer == 2 ? 0 : 2;
	return layer;
}

/* Copy as DESIGN.md 12 has it: lowercase verbs, as the action row of the
 * keyboard lab says them. Shift's and symbols' name the layer the press goes
 * to, so they are worked out from kb_layer_after and cannot disagree with
 * what the key does. */
const char *kb_action_label(kb_action a, int layer)
{
	switch (a) {
	case KB_ACT_SHIFT:
		return T(kb_layer_after(layer, a) == 0 ? S_KEYBOARD_UNSHIFT : S_KEYBOARD_SHIFT);
	case KB_ACT_SYMBOLS:
		return kb_layer_after(layer, a) == 0 ? "abc" : "#+=";
	case KB_ACT_DELETE:
		return T(S_KEYBOARD_DELETE);
	case KB_ACT_DONE:
		return T(S_KEYBOARD_DONE);
	case KB_ACT_SPACE:
	default:
		return "";
	}
}

const char *kb_action_button(kb_action a)
{
	switch (a) {
	case KB_ACT_SHIFT:   return "X";
	case KB_ACT_SYMBOLS: return "Y";
	case KB_ACT_DELETE:  return "B";
	case KB_ACT_DONE:    return "START";
	case KB_ACT_SPACE:
	default:             return NULL;
	}
}
