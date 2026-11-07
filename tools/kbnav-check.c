/* The on-screen keyboard's keys, without a screen: where the d-pad goes from
 * each one, and what the action row says on each layer.
 *
 * DESIGN.md 11.5 puts an action row under the grid - shift, symbols, space,
 * delete, done - and the d-pad has to reach it and leave it without losing
 * the player: down from the grid's last row lands on the key under the
 * column, up from a key lands on the column under its center, left and right
 * walk the five keys, and the whole thing still wraps at every edge, which is
 * what keeps the worst trip short. Every one of those is a number that looks
 * right and is off by a column, and on a d-pad an off-by-one is a press that
 * goes somewhere nobody pointed. So the moves are held here, key by key,
 * along with the layout they are worked out from: the action keys tile the
 * grid's width in the keyboard lab's proportions, and each column and key is
 * found under its own center.
 *
 * And the labels: X's key says "unshift" on the capitals and Y's says "abc"
 * on the symbols, because a key's label is what pressing it does next.
 *
 * Links src/kbnav.c alone, and NOT SDL.
 */
#include "../src/kbnav.h"
#include "../src/design.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* One move from (row, col), and whether it landed on (er, ec). */
static int moves_to(int row, int col, kb_dir d, int er, int ec)
{
	kb_move(&row, &col, d);
	return row == er && col == ec;
}

static int streq(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

static int near(float got, float want)
{
	float d = got - want;

	return d > -0.000001f && d < 0.000001f;
}

/* These values are hand-derived: quintic out leaves 1/32 of the shrink
 * halfway through. A constant scale, linear curve, early completion, stale
 * start on repeat, or signed time subtraction must break these checks. */
static void check_pop(void)
{
	kb_pop p = {0}, other = {0};
	unsigned start = 1000;
	float before;
	int i;

	printf("key press pop:\n");
	ck(kb_pop_scale(&p, 0) == 1.0f, "a zeroed pop rests at full size at clock zero");
	ck(kb_pop_scale(&p, UINT_MAX) == 1.0f, "a resting pop stays full size at any time");
	kb_pop_press(&p, start);
	ck(near(kb_pop_scale(&p, start), 0.92f), "a press starts at 92 percent");
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS / 4), 0.981015625f),
	   "the first quarter uses quintic out, not a linear return");
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS / 2), 0.9975f),
	   "the midpoint leaves one thirty-second of the shrink");
	ck(kb_pop_scale(&p, start + DS_MS_PRESS) == 1.0f,
	   "the pop completes at press duration");
	ck(kb_pop_scale(&p, start + DS_MS_PRESS + 900) == 1.0f,
	   "a completed pop stays at full size");
	ck(kb_pop_scale(&p, start) == 1.0f,
	   "a completed pop does not restart when the counter wraps to its old start");
	kb_pop_press(&p, start + 1000);
	ck(near(kb_pop_scale(&p, start + 1000), 0.92f),
	   "a new press after completion starts a new pop");
	kb_pop_press(&p, start + 2000);
	ck(near(kb_pop_scale(&p, start + 2000), 0.92f),
	   "a late press starts fresh even without a completion frame");

	p = (kb_pop){0};
	kb_pop_press(&p, start);
	before = 0.92f;
	for (i = 0; i <= DS_MS_PRESS; i++) {
		float scale = kb_pop_scale(&p, start + (unsigned)i);

		ck(scale >= before && scale >= 0.92f && scale <= 1.0f,
		   "every millisecond returns toward full size without overshoot");
		before = scale;
	}

	p = (kb_pop){0};
	kb_pop_press(&p, start);
	before = kb_pop_scale(&p, start + DS_MS_PRESS / 2);
	kb_pop_press(&p, start + DS_MS_PRESS / 2);
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS / 2), before),
	   "a repeated press continues from the current scale without a jump");
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS), 0.999921875f),
	   "the repeated pop returns from its sampled scale on quintic out");
	ck(kb_pop_scale(&p, start + DS_MS_PRESS + DS_MS_PRESS / 2) == 1.0f,
	   "a repeat completes one press duration after the latest press");

	p = (kb_pop){0};
	kb_pop_press(&p, start);
	for (i = 0; i < 1000; i++) kb_pop_press(&p, start);
	ck(near(kb_pop_scale(&p, start), 0.92f),
	   "same-frame repeats do not compound the shrink");
	ck(kb_pop_scale(&p, start + DS_MS_PRESS) == 1.0f,
	   "many repeated presses do not build a queue");

	p = (kb_pop){0};
	kb_pop_press(&p, 0);
	ck(near(kb_pop_scale(&p, 0), 0.92f), "a press at clock zero is active");
	ck(kb_pop_scale(&other, 0) == 1.0f, "a press leaves another key at rest");
	kb_pop_press(&other, DS_MS_PRESS / 2);
	ck(near(kb_pop_scale(&p, DS_MS_PRESS / 2), 0.9975f) &&
	   near(kb_pop_scale(&other, DS_MS_PRESS / 2), 0.92f),
	   "different keys keep independent return times");

	p = (kb_pop){0};
	start = UINT_MAX - DS_MS_PRESS / 4;
	kb_pop_press(&p, start);
	ck(near(kb_pop_scale(&p, start), 0.92f), "a press starts before counter wrap");
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS / 2), 0.9975f),
	   "the midpoint is correct across counter wrap");
	kb_pop_press(&p, start + DS_MS_PRESS / 2);
	ck(near(kb_pop_scale(&p, start + DS_MS_PRESS), 0.999921875f),
	   "a repeated press samples correctly across counter wrap");
	ck(kb_pop_scale(&p, start + DS_MS_PRESS + DS_MS_PRESS / 2) == 1.0f,
	   "a repeated pop completes after counter wrap");
}

int main(void)
{
	/* The action key under each grid column's center, and the column under
	 * each action key's center. Worked out by hand from the lab's layout,
	 * so a change to the layout or to the rule shows up here as a change
	 * somebody has to agree with. */
	static const int under_col[KB_COLS] = {
		KB_ACT_SHIFT, KB_ACT_SHIFT, KB_ACT_SYMBOLS, KB_ACT_SYMBOLS,
		KB_ACT_SPACE, KB_ACT_DELETE, KB_ACT_DELETE, KB_ACT_DELETE,
		KB_ACT_DONE, KB_ACT_DONE,
	};
	static const int over_act[KB_ACTIONS] = { 1, 3, 4, 6, 8 };
	static const int flex[KB_ACTIONS] = { 22, 17, 14, 22, 25 };   /* tenths */
	char what[128];
	int a, c, r, x, w, nx, nw, row, col;

	printf("layout:\n");
	ck(KB_GRID_W == 834, "the grid is ten 78-unit keys and nine 6-unit gaps");
	kb_action_span(0, &x, &w);
	ck(x == 0, "the action row starts where the grid does");
	for (a = 0; a < KB_ACTIONS; a++) {
		int want = (KB_GRID_W - (KB_ACTIONS - 1) * KB_KEY_GAP) * flex[a] / 100;

		kb_action_span(a, &x, &w);
		snprintf(what, sizeof what, "action %d is %d wide, the lab's %d.%d of ten",
		         a, w, flex[a] / 10, flex[a] % 10);
		ck(abs(w - want) <= 1, what);
		if (a + 1 < KB_ACTIONS) {
			kb_action_span(a + 1, &nx, &nw);
			snprintf(what, sizeof what, "action %d starts one gap after %d", a + 1, a);
			ck(nx == x + w + KB_KEY_GAP, what);
		} else {
			ck(x + w == KB_GRID_W, "the action row ends where the grid does");
		}
	}

	printf("what is under what:\n");
	for (c = 0; c < KB_COLS; c++) {
		int mid = c * (KB_KEY_W + KB_KEY_GAP) + KB_KEY_W / 2;

		snprintf(what, sizeof what, "column %d is under its own center", c);
		ck(kb_col_under(mid) == c, what);
		snprintf(what, sizeof what, "action %d is under column %d's center",
		         under_col[c], c);
		ck(kb_action_under(mid) == under_col[c], what);
	}
	for (a = 0; a < KB_ACTIONS; a++) {
		kb_action_span(a, &x, &w);
		snprintf(what, sizeof what, "action %d is under its own center", a);
		ck(kb_action_under(x + w / 2) == a, what);
		snprintf(what, sizeof what, "column %d is under action %d's center",
		         over_act[a], a);
		ck(kb_col_under(x + w / 2) == over_act[a], what);
	}
	ck(kb_col_under(-40) == 0 && kb_col_under(KB_GRID_W + 40) == KB_COLS - 1,
	   "a point off either end is the column at that end");
	ck(kb_action_under(-40) == 0 &&
	   kb_action_under(KB_GRID_W + 40) == KB_ACTIONS - 1,
	   "and the action key at that end");

	printf("the grid:\n");
	for (r = 0; r < KB_ROWS; r++) {
		snprintf(what, sizeof what, "row %d wraps left from the first column", r);
		ck(moves_to(r, 0, KB_LEFT, r, KB_COLS - 1), what);
		snprintf(what, sizeof what, "row %d wraps right from the last column", r);
		ck(moves_to(r, KB_COLS - 1, KB_RIGHT, r, 0), what);
		snprintf(what, sizeof what, "row %d steps right", r);
		ck(moves_to(r, 4, KB_RIGHT, r, 5), what);
		snprintf(what, sizeof what, "row %d steps left", r);
		ck(moves_to(r, 4, KB_LEFT, r, 3), what);
	}
	for (c = 0; c < KB_COLS; c++) {
		snprintf(what, sizeof what, "column %d steps down the grid", c);
		ck(moves_to(1, c, KB_DOWN, 2, c), what);
		snprintf(what, sizeof what, "column %d steps up the grid", c);
		ck(moves_to(2, c, KB_UP, 1, c), what);
	}

	printf("into the action row:\n");
	for (c = 0; c < KB_COLS; c++) {
		snprintf(what, sizeof what,
		         "down from the last row's column %d is action %d", c, under_col[c]);
		ck(moves_to(KB_ROWS - 1, c, KB_DOWN, KB_ROW_ACTIONS, under_col[c]), what);
		snprintf(what, sizeof what,
		         "up from the digits' column %d wraps to action %d", c, under_col[c]);
		ck(moves_to(0, c, KB_UP, KB_ROW_ACTIONS, under_col[c]), what);
	}

	printf("along and out of the action row:\n");
	for (a = 0; a < KB_ACTIONS; a++) {
		int l = (a + KB_ACTIONS - 1) % KB_ACTIONS, rt = (a + 1) % KB_ACTIONS;

		snprintf(what, sizeof what, "left from action %d is action %d", a, l);
		ck(moves_to(KB_ROW_ACTIONS, a, KB_LEFT, KB_ROW_ACTIONS, l), what);
		snprintf(what, sizeof what, "right from action %d is action %d", a, rt);
		ck(moves_to(KB_ROW_ACTIONS, a, KB_RIGHT, KB_ROW_ACTIONS, rt), what);
		snprintf(what, sizeof what,
		         "up from action %d is the last row's column %d", a, over_act[a]);
		ck(moves_to(KB_ROW_ACTIONS, a, KB_UP, KB_ROWS - 1, over_act[a]), what);
		snprintf(what, sizeof what,
		         "down from action %d wraps to the digits' column %d", a, over_act[a]);
		ck(moves_to(KB_ROW_ACTIONS, a, KB_DOWN, 0, over_act[a]), what);

		/* Up and straight back down is where you were: the column a key
		 * sends you to is one whose center is over that same key. */
		row = KB_ROW_ACTIONS; col = a;
		kb_move(&row, &col, KB_UP);
		kb_move(&row, &col, KB_DOWN);
		snprintf(what, sizeof what, "up and back down from action %d is action %d", a, a);
		ck(row == KB_ROW_ACTIONS && col == a, what);
	}

	printf("labels:\n");
	ck(streq(kb_action_label(KB_ACT_SHIFT, 0), "shift"), "X's key is shift on the letters");
	ck(streq(kb_action_label(KB_ACT_SHIFT, 1), "unshift"), "and unshift on the capitals");
	ck(streq(kb_action_label(KB_ACT_SHIFT, 2), "shift"), "and shift on the symbols");
	ck(streq(kb_action_label(KB_ACT_SYMBOLS, 0), "#+="), "Y's key is #+= on the letters");
	ck(streq(kb_action_label(KB_ACT_SYMBOLS, 1), "#+="), "and on the capitals");
	ck(streq(kb_action_label(KB_ACT_SYMBOLS, 2), "abc"), "and abc on the symbols");
	for (r = 0; r < 3; r++) {
		snprintf(what, sizeof what, "space has no word on layer %d: it is the mark", r);
		ck(streq(kb_action_label(KB_ACT_SPACE, r), ""), what);
		snprintf(what, sizeof what, "delete and done are the same on layer %d", r);
		ck(streq(kb_action_label(KB_ACT_DELETE, r), "delete") &&
		   streq(kb_action_label(KB_ACT_DONE, r), "done"), what);
	}
	ck(streq(kb_action_button(KB_ACT_SHIFT), "X"), "shift is X");
	ck(streq(kb_action_button(KB_ACT_SYMBOLS), "Y"), "symbols is Y");
	ck(kb_action_button(KB_ACT_SPACE) == NULL, "space is no button's");
	ck(streq(kb_action_button(KB_ACT_DELETE), "B"), "delete is B");
	ck(streq(kb_action_button(KB_ACT_DONE), "START"), "done is START");

	printf("layers:\n");
	ck(kb_layer_after(0, KB_ACT_SHIFT) == 1, "shift on the letters is the capitals");
	ck(kb_layer_after(1, KB_ACT_SHIFT) == 0, "unshift is the letters");
	ck(kb_layer_after(2, KB_ACT_SHIFT) == 1, "shift on the symbols is the capitals");
	ck(kb_layer_after(0, KB_ACT_SYMBOLS) == 2, "#+= on the letters is the symbols");
	ck(kb_layer_after(1, KB_ACT_SYMBOLS) == 2, "and on the capitals");
	ck(kb_layer_after(2, KB_ACT_SYMBOLS) == 0, "abc is the letters");
	for (r = 0; r < 3; r++) {
		snprintf(what, sizeof what, "space, delete and done keep layer %d", r);
		ck(kb_layer_after(r, KB_ACT_SPACE) == r &&
		   kb_layer_after(r, KB_ACT_DELETE) == r &&
		   kb_layer_after(r, KB_ACT_DONE) == r, what);
	}

	printf("a selection out of range:\n");
	{
		/* { row, col in } -> { row, col out } */
		static const int cl[][4] = {
			{ 1, 3, 1, 3 },                                  /* in range: kept */
			{ KB_ROW_ACTIONS, 4, KB_ROW_ACTIONS, 4 },        /* done: kept */
			{ -1, 3, 0, 3 },                                 /* above the digits */
			{ -50, -50, 0, 0 },
			{ 2, -1, 2, 0 },                                 /* left of the grid */
			{ 2, KB_COLS, 2, KB_COLS - 1 },                  /* right of it */
			{ 2, 99, 2, KB_COLS - 1 },
			{ KB_ROW_ACTIONS + 1, 2, KB_ROW_ACTIONS, 2 },    /* below the actions */
			{ 99, 0, KB_ROW_ACTIONS, 0 },
			{ KB_ROW_ACTIONS, KB_ACTIONS, KB_ROW_ACTIONS, KB_ACTIONS - 1 },
			{ KB_ROW_ACTIONS, 7, KB_ROW_ACTIONS, KB_ACTIONS - 1 },  /* a grid column */
			{ KB_ROW_ACTIONS, -1, KB_ROW_ACTIONS, 0 },
			{ 99, 99, KB_ROW_ACTIONS, KB_ACTIONS - 1 },
		};
		size_t n;

		for (n = 0; n < sizeof cl / sizeof cl[0]; n++) {
			row = cl[n][0]; col = cl[n][1];
			kb_clamp(&row, &col);
			snprintf(what, sizeof what, "(%d, %d) is (%d, %d)",
			         cl[n][0], cl[n][1], cl[n][2], cl[n][3]);
			ck(row == cl[n][2] && col == cl[n][3], what);
		}
		for (a = 0; a < KB_ACTIONS; a++) {
			row = KB_ROW_ACTIONS; col = a;
			kb_clamp(&row, &col);
			snprintf(what, sizeof what, "action %d is kept", a);
			ck(row == KB_ROW_ACTIONS && col == a, what);
		}
		row = 7; col = 7;
		kb_move(&row, &col, KB_LEFT);
		ck(row == KB_ROW_ACTIONS && col == KB_ACTIONS - 2,
		   "a move clamps first: left from (7, 7) is from done");
	}

	/* A held down repeats on delete as B does, but only a hold that began
	 * with a press there: a thumb resting on A while the d-pad walks onto
	 * delete must not start deleting. Frame by frame: whether A may repeat,
	 * from the frame before, whether the selection is on delete, and A's
	 * pressed (this frame) and down. */
	printf("A held onto delete:\n");
	{
		bool armed = false;

		armed = kb_hold_armed(armed, false, true, true);    /* A on a letter */
		armed = kb_hold_armed(armed, false, false, true);   /* held, moving */
		armed = kb_hold_armed(armed, true, false, true);    /* onto delete */
		ck(!armed, "a hold that began elsewhere does not repeat on delete");
		armed = kb_hold_armed(armed, true, false, true);
		ck(!armed, "nor a frame later");
		armed = kb_hold_armed(armed, true, false, false);   /* let go */
		armed = kb_hold_armed(armed, true, true, true);     /* pressed there */
		ck(armed, "a press on delete repeats");
		armed = kb_hold_armed(armed, true, false, true);
		ck(armed, "and keeps repeating while held");
		armed = kb_hold_armed(armed, false, false, true);   /* off, still held */
		ck(!armed, "moving off stops it");
		armed = kb_hold_armed(armed, true, false, true);    /* back, still held */
		ck(!armed, "and coming back with it still held does not restart it");
		armed = kb_hold_armed(false, true, true, true);
		armed = kb_hold_armed(armed, true, false, false);
		ck(!armed, "letting go stops it");
		ck(kb_hold_armed(false, true, true, false),
		   "a press let go within one frame still deletes once");
	}

	check_pop();
	if (fails) { printf("kbnav: %d FAILED\n", fails); return 1; }
	printf("kbnav: ok\n");
	return 0;
}
