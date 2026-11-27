/* SPDX-License-Identifier: GPL-3.0-only */
/* What an option list's cursor shows (src/ds/hover.h), its rules one case
 * each, against a list that records what it was told.
 *
 *   make check-hover
 *
 * A show once per choice the cursor comes to, never each pass; the kept one
 * put back with undo, at once; a rest held before a show and started again
 * by each move; undo once on close only when something was shown, so a
 * choice A takes is stepped from the kept one; no show at all from a list
 * with none; and own_sound quiets the list's moves only where a show plays
 * them; a tick on each pass, after the pass's show; and `at` told the
 * cursor on every pass, the kept one too, and -1 on close. */
#include "../src/ds/hover.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* What the list was told, in order: a show as its choice, an undo as U */
typedef struct {
	char said[64];
	int n;
} tape;

static void show(void *ctx, int k)
{
	tape *t = ctx;

	if (t->n < (int)sizeof t->said - 1) t->said[t->n++] = (char)('0' + k);
	t->said[t->n] = '\0';
}

static void undo(void *ctx, int kept)
{
	tape *t = ctx;

	(void)kept;

	if (t->n < (int)sizeof t->said - 1) t->said[t->n++] = 'U';
	t->said[t->n] = '\0';
}

static void tick(void *ctx)
{
	tape *t = ctx;

	if (t->n < (int)sizeof t->said - 1) t->said[t->n++] = 't';
	t->said[t->n] = '\0';
}

static const ds_hover AT_ONCE = { .show = show, .undo = undo };
static const ds_hover RESTS = { .show = show, .undo = undo, .rest_ms = 500 };
static const ds_hover SOUNDS = { .show = show, .undo = undo, .own_sound = true };

static void shows_once_per_choice(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &AT_ONCE, &t, 2, 2, 0);
	ds_hover_at(&r, 2, 0);                      /* opens on the kept one: nothing */
	ck(t.n == 0, "the kept one under the cursor as the list opens shows nothing");
	ds_hover_at(&r, 3, 16);
	ds_hover_at(&r, 3, 32);
	ds_hover_at(&r, 3, 48);
	ck(!strcmp(t.said, "3"), "a choice is shown once, not on every pass");
	ds_hover_at(&r, 4, 64);
	ck(!strcmp(t.said, "34"), "the next choice is shown when the cursor comes to it");
	ds_hover_at(&r, 2, 80);
	ck(!strcmp(t.said, "34U"), "back on the kept one: undo, at once");
	ds_hover_at(&r, 2, 96);
	ck(!strcmp(t.said, "34U"), "and only once");
	ds_hover_end(&r);
	ck(!strcmp(t.said, "34U"), "closed on the kept one: no undo, nothing was shown");
}

static void undo_on_close(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &AT_ONCE, &t, 0, 0, 0);
	ds_hover_at(&r, 1, 10);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "1U"), "closed on a shown choice: undo, before A's is applied");
	ds_hover_end(&r);
	ck(!strcmp(t.said, "1U"), "a spent run undoes nothing again");
}

static void starts_off_the_kept_one(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &AT_ONCE, &t, 0, 3, 0);
	ds_hover_at(&r, 3, 0);
	ck(!strcmp(t.said, "3"), "a list opening with its cursor off the kept one shows it");
	ds_hover_end(&r);
}

static void rests_before_a_show(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &RESTS, &t, 0, 0, 1000);
	ds_hover_at(&r, 1, 1000);
	ds_hover_at(&r, 1, 1400);
	ck(t.n == 0, "a choice passed in less than the rest is not shown");
	ds_hover_at(&r, 2, 1450);
	ds_hover_at(&r, 2, 1900);
	ck(t.n == 0, "a move starts the rest again");
	ds_hover_at(&r, 2, 1950);
	ck(!strcmp(t.said, "2"), "the choice the cursor rests on is shown once the rest is over");
	ds_hover_at(&r, 0, 1966);
	ck(!strcmp(t.said, "2U"), "back on the kept one: undo with no rest");
	ds_hover_at(&r, 1, 1982);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "2U"), "closed before the rest was over: nothing to undo");
}

static void none_is_on(void)
{
	tape t = { 0 };
	ds_hover_run r;

	/* A shader each system has its own of: none kept as a whole, so
	 * every choice is something to show */
	ds_hover_begin(&r, &AT_ONCE, &t, -1, 0, 0);
	ds_hover_at(&r, 0, 0);
	ck(!strcmp(t.said, "0"), "with none kept, the choice under the cursor is shown");
	ds_hover_end(&r);
	ck(!strcmp(t.said, "0U"), "and undone on close");
}

static void without_a_show(void)
{
	static const ds_hover PICTURE = { .draw = NULL };
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, NULL, &t, 0, 0, 0);
	ds_hover_at(&r, 1, 0);
	ds_hover_end(&r);
	ds_hover_begin(&r, &PICTURE, &t, 0, 0, 0);
	ds_hover_at(&r, 1, 0);
	ds_hover_end(&r);
	ck(t.n == 0, "a list with no show, or no hover at all, is told nothing");
	ck(!ds_hover_quiet(NULL) && !ds_hover_quiet(&PICTURE) && !ds_hover_quiet(&AT_ONCE),
	   "a list's moves are quiet only where its show plays them");
	ck(ds_hover_quiet(&SOUNDS), "own_sound quiets the list's moves");
}

static void ends_once(void)
{
	static const ds_hover ENDS = { .show = show, .undo = undo, .end = tick };
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &ENDS, &t, 0, 0, 0);
	ds_hover_at(&r, 1, 16);
	ds_hover_end(&r);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "1Ut"), "end after the undo, once, whatever closed the list");
	ds_hover_begin(&r, &ENDS, &t, 0, 0, 0);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "1Utt"), "end with nothing shown too");
}

static void own_sound_heard_back(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &SOUNDS, &t, 0, 0, 0);
	ds_hover_at(&r, 1, 16);
	ds_hover_at(&r, 0, 32);
	ck(!strcmp(t.said, "10"), "a list that plays its moves shows the kept one coming back, to be heard");
	ck(!ds_hover_showing(), "and is not showing then");
	ds_hover_end(&r);
	ck(!strcmp(t.said, "10"), "nothing to undo on close");
}

static void showing_while_shown(void)
{
	tape t = { 0 };
	ds_hover_run r;

	ck(!ds_hover_showing(), "nothing showing before a list opens");
	ds_hover_begin(&r, &AT_ONCE, &t, 0, 0, 0);
	ds_hover_at(&r, 0, 0);
	ck(!ds_hover_showing(), "nothing showing on the kept one");
	ds_hover_at(&r, 2, 16);
	ds_hover_at(&r, 3, 32);
	ck(ds_hover_showing(), "showing once a choice is shown, through the next");
	ds_hover_at(&r, 0, 48);
	ck(!ds_hover_showing(), "not showing back on the kept one");
	ds_hover_at(&r, 1, 64);
	ds_hover_end(&r);
	ck(!ds_hover_showing(), "not showing once the list has closed");
}

static void ticks_each_pass(void)
{
	static const ds_hover TICKS = { .show = show, .undo = undo, .tick = tick };
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &TICKS, &t, 0, 0, 0);
	ds_hover_at(&r, 0, 0);
	ds_hover_at(&r, 1, 16);
	ds_hover_at(&r, 1, 32);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "t1ttU"), "tick runs each pass, after the pass's show");
}

/* `at`: the cursor each pass, a move back onto the kept one too (Rumble's
 * level felt), and -1 once the list has closed, before end */
static void at(void *ctx, int k)
{
	tape *t = ctx;

	if (t->n < (int)sizeof t->said - 1) t->said[t->n++] = k < 0 ? '-' : (char)('a' + k);
	t->said[t->n] = '\0';
}

static void told_the_cursor(void)
{
	static const ds_hover TOLD = { .at = at, .end = tick };
	tape t = { 0 };
	ds_hover_run r;

	ds_hover_begin(&r, &TOLD, &t, 0, 0, 0);
	ds_hover_at(&r, 0, 0);
	ds_hover_at(&r, 1, 16);
	ds_hover_at(&r, 0, 32);
	ds_hover_end(&r);
	ck(!strcmp(t.said, "aba-t"), "at is told the cursor each pass, the kept one too, then -1 before end");
	ck(!ds_hover_showing(), "and nothing is showing: at shows nothing");
}

int main(void)
{
	shows_once_per_choice();
	undo_on_close();
	starts_off_the_kept_one();
	rests_before_a_show();
	none_is_on();
	without_a_show();
	ticks_each_pass();
	showing_while_shown();
	own_sound_heard_back();
	ends_once();
	told_the_cursor();
	if (fails) { printf("\n%d hover check(s) failed\n", fails); return 1; }
	printf("ok: a list shows each choice once as its cursor comes to it, puts the kept one back with undo, "
	       "rests before a show where it asks, and undoes on close only what it showed\n");
	return 0;
}
