/* A menu's panel opening and closing, without a screen: src/menumotion.c's
 * stack of open screens and the frame it gives each, held to DESIGN.md 8.2.
 *
 * What it holds, each against the times and curves of src/design.h:
 *   - a menu opened on the shelf arrives in **quick** on **out**, from 96% and
 *     clear, its scrim with it, and is at rest from then on;
 *   - a press while it opens puts it at rest at once;
 *   - a submenu over it waits for its panel to leave in **exit** on **in**,
 *     then arrives, the scrim whole all the while, and never are two panels
 *     on screen at once;
 *   - going back is the same the other way, and B before the submenu had
 *     come leaves the menu where it was, with no journey;
 *   - the last menu closed onto the shelf leaves over it with the scrim;
 *   - MENU deep in a submenu unwinds every level with nothing arriving on the
 *     way, and the panel on screen leaves once;
 *   - a press on a screen still waiting for the panel before it to leave
 *     ends that leave, and its own panel, at rest, is then the one on
 *     screen: what leaves next is the panel the press left there;
 *   - Muse's shelf takes a menu's panel off over itself and gives it back,
 *     and MENU in Muse's menu, which closes only that menu, brings the
 *     screen under it back;
 *   - a held d-pad, a press every REPEAT_RATE_MS (src/keyrepeat.h's, read by
 *     the Makefile), puts the panel at rest once and starts nothing over;
 *   - and in src/main.c, read as text with its comments taken out: the
 *     in-game menu's close (panel_leave_run) reads input every frame, a press
 *     ends it before the frame is drawn, and power or Auto Off in it stop the
 *     game as in the menu; game_menu draws it, then drops what is left of it,
 *     then hands the pages back, then sends RESUME, and draws nothing after:
 *     Diatom owns the display from there. make check-menushots looks at the
 *     pictures of the motion itself.
 *
 * Links src/menumotion.c alone, and NOT SDL.
 */
#include "../src/menumotion.h"
#include "../src/design.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REPEAT_RATE_MS
#error "the Makefile passes src/keyrepeat.h's REPEAT_RATE_MS"
#endif

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int near(float a, float b) { return fabsf(a - b) <= 0.0005f; }

static int at_rest(mm_frame f)
{
	return f.before.alpha == 0.0f && f.panel.alpha == 1.0f &&
	       f.panel.scale == 1.0f && f.scrim == 1.0f && !f.moving;
}

/* Arriving `e` of the way, on out */
static int arriving(mm_pose p, float u)
{
	float e = ds_out(u);

	return near(p.alpha, e) && near(p.scale, MM_SCALE_FROM + (1.0f - MM_SCALE_FROM) * e);
}

/* Leaving `u` of the way, on in */
static int leaving(mm_pose p, float u)
{
	float e = ds_in(u);

	return near(p.alpha, 1.0f - e) &&
	       near(p.scale, 1.0f - (1.0f - MM_SCALE_FROM) * e);
}

/* From `t0` for `ms`: never two panels at once, and the scrim never other
 * than `scrim` (a negative one: not checked). */
static int one_at_a_time(int lv, unsigned t0, unsigned ms, float scrim)
{
	unsigned t;

	for (t = t0; t != t0 + ms; t++) {
		mm_frame f = mm_at(lv, t);

		if (f.before.alpha > 0.0f && f.panel.alpha > 0.0f) return 0;
		if (scrim >= 0.0f && f.scrim != scrim) return 0;
	}
	return 1;
}

/* `src` with its comments taken out, so a comment cannot say what the code
 * does not; string literals are kept whole. */
static void uncomment(char *src)
{
	char *r = src, *w = src, q = 0;

	while (*r) {
		if (q) {
			if (*r == '\\' && r[1]) { *w++ = *r++; *w++ = *r++; continue; }
			if (*r == q) q = 0;
			*w++ = *r++;
		} else if (r[0] == '/' && r[1] == '*') {
			char *e = strstr(r + 2, "*/");

			r = e ? e + 2 : r + strlen(r);
			*w++ = ' ';
		} else if (r[0] == '/' && r[1] == '/') {
			while (*r && *r != '\n') r++;
		} else {
			if (*r == '"' || *r == '\'') q = *r;
			*w++ = *r++;
		}
	}
	*w = '\0';
}

/* The body of the function of src/main.c that starts `head`, its comments
 * taken out; NULL when there is none */
static char *main_fn(const char *head)
{
	FILE *f = fopen("src/main.c", "rb");
	char *all, *at, *end, *out = NULL;
	long n;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	all = malloc((size_t)n + 1);
	if (all && fread(all, 1, (size_t)n, f) == (size_t)n) {
		all[n] = '\0';
		if ((at = strstr(all, head)) && (end = strstr(at, "\n}\n"))) {
			*end = '\0';
			out = strdup(at);
			if (out) uncomment(out);
		}
	}
	free(all);
	fclose(f);
	return out;
}

/* Where `what` first is in `body`, as an offset; -1 if nowhere */
static long first(const char *body, const char *what)
{
	const char *p = strstr(body, what);

	return p ? (long)(p - body) : -1;
}

int main(void)
{
	const unsigned Q = DS_MS_QUICK, X = DS_MS_EXIT;
	unsigned t;
	mm_frame f;
	int top, sub, deep, muse, ok;

	/* ---- opened on the shelf ---- */
	mm_reset();
	top = mm_open(1000, false);
	ck(top == 1 && mm_depth() == 1, "the first menu opened on the shelf is level 1");
	f = mm_at(top, 1000);
	ck(f.panel.alpha == 0.0f && near(f.panel.scale, MM_SCALE_FROM) && f.scrim == 0.0f &&
	   f.before.alpha == 0.0f && f.moving,
	   "it starts clear, at 96%, with no scrim and nothing before it");
	f = mm_at(top, 1000 + Q / 2);
	ck(arriving(f.panel, 0.5f), "and arrives on the out curve");
	ck(near(f.scrim, f.panel.alpha), "with the scrim coming in with it");
	ck(mm_at(top, 1000 + Q - 1).moving, "still arriving a millisecond before quick");
	ck(at_rest(mm_at(top, 1000 + Q)), "and at rest at quick, exactly");
	ck(at_rest(mm_at(top, 1000 + 60000)), "and at rest from then on");
	ok = 1;
	for (t = 1000; t < 1000 + Q; t++)
		if (mm_at(top, t + 1).panel.alpha < mm_at(top, t).panel.alpha) ok = 0;
	ck(ok, "never going back the way it came");

	/* ---- a press while it opens ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	ck(at_rest(mm_at(top, 1040)), "a press 40 ms into the open puts it at rest at once");

	/* ---- a submenu over it ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(5000, true);
	ck(sub == 2 && mm_depth() == 2, "a submenu is the next level");
	f = mm_at(sub, 5000);
	ck(f.before.alpha == 1.0f && f.before.scale == 1.0f && f.panel.alpha == 0.0f &&
	   f.scrim == 1.0f && f.moving,
	   "a submenu starts with the menu's panel whole and its own not there, the scrim whole");
	f = mm_at(sub, 5000 + X / 2);
	ck(leaving(f.before, 0.5f) && f.panel.alpha == 0.0f,
	   "the menu's panel leaves on the in curve, the submenu's waiting");
	f = mm_at(sub, 5000 + X);
	ck(f.before.alpha == 0.0f && f.panel.alpha == 0.0f && f.moving,
	   "the menu's panel is gone at exit, and the submenu's starts");
	ck(arriving(mm_at(sub, 5000 + X + Q / 2).panel, 0.5f),
	   "then the submenu's arrives on the out curve");
	ck(at_rest(mm_at(sub, 5000 + X + Q)), "and is at rest at exit and quick");
	ck(one_at_a_time(sub, 5000, X + Q, 1.0f),
	   "never two panels on screen at once, and the scrim whole all the while");

	/* With nothing on screen to leave: it arrives at once, the scrim as it is */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(5000, false);
	f = mm_at(sub, 5000 + Q / 2);
	ck(f.before.alpha == 0.0f && arriving(f.panel, 0.5f) && f.scrim == 1.0f,
	   "a submenu with no panel on screen before it arrives at once");

	/* A press while the menu's panel is still leaving */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(5000, true);
	mm_press(sub);
	ck(at_rest(mm_at(sub, 5000 + X / 2)),
	   "a press while the menu's panel leaves puts the submenu's at rest at once");

	/* ---- back ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(5000, true);
	mm_press(sub);
	mm_close(sub, 9000, sub, false);
	ck(mm_depth() == 1, "B closes the submenu");
	f = mm_at(top, 9000);
	ck(f.before.alpha == 1.0f && f.panel.alpha == 0.0f && f.scrim == 1.0f,
	   "back, the submenu's panel is on screen and the menu's is not");
	ck(leaving(mm_at(top, 9000 + X / 2).before, 0.5f), "the submenu's panel leaves on in");
	ck(arriving(mm_at(top, 9000 + X + Q / 2).panel, 0.5f), "then the menu's arrives on out");
	ck(at_rest(mm_at(top, 9000 + X + Q)), "and is at rest at exit and quick");
	ck(one_at_a_time(top, 9000, X + Q, 1.0f),
	   "never two panels on screen at once going back either, the scrim whole");
	ck(mm_leaving(9000).moving == false, "and nothing leaves over the shelf");

	/* B before the submenu's panel had come: the menu's never left */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(5000, true);
	mm_close(sub, 5030, top, false);
	ck(at_rest(mm_at(top, 5030)),
	   "B while the menu's panel was leaving puts it back where it belongs, at once");

	/* ---- closed onto the shelf ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	mm_close(top, 9000, top, false);
	ck(mm_depth() == 0, "the last menu closes onto the shelf");
	f = mm_leaving(9000);
	ck(f.before.alpha == 1.0f && f.before.scale == 1.0f && f.scrim == 1.0f && f.moving,
	   "its panel and the scrim are whole as it starts to leave");
	f = mm_leaving(9000 + X / 2);
	ck(leaving(f.before, 0.5f) && near(f.scrim, f.before.alpha),
	   "and leave together on the in curve");
	f = mm_leaving(9000 + X);
	ck(f.before.alpha == 0.0f && f.scrim == 0.0f && !f.moving, "gone at exit, exactly");
	mm_reset();
	ck(mm_leaving(9000 + X / 2).moving, "the shelf's own reset does not stop it");
	mm_leaving_cut();
	ck(!mm_leaving(9000 + X / 2).moving, "a press on the shelf does: it has gone");

	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	mm_close(top, 9000, top, false);
	top = mm_open(9050, false);
	ck(!mm_leaving(9060).moving && mm_at(top, 9050).scrim == 0.0f,
	   "a menu opened again while the last one leaves takes over from nothing");

	mm_reset();
	top = mm_open(1000, false);
	mm_close(top, 1001, 0, false);
	ck(!mm_leaving(1001).moving, "a menu closed with nothing on screen leaves nothing");

	/* ---- MENU deep in a submenu ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(2000, true);
	mm_press(sub);
	deep = mm_open(3000, true);
	mm_press(deep);
	mm_close(deep, 9000, deep, true);
	ck(at_rest(mm_at(sub, 9000 + X / 2)), "MENU: the level under is not brought back");
	mm_close(sub, 9000, deep, true);
	ck(at_rest(mm_at(top, 9000 + X / 2)), "nor the one under that");
	ck(!mm_leaving(9000).moving, "and nothing leaves until the last one has closed");
	mm_close(top, 9000, deep, true);
	ck(mm_depth() == 0, "every level unwinds");
	{
		int starts = 0, was_on = 0;
		float last = 2.0f;

		ok = 1;
		for (t = 9000; t < 9000 + 4 * X; t++) {
			f = mm_leaving(t);
			if (f.moving && !was_on) starts++;
			was_on = f.moving;
			if (f.before.alpha > last) ok = 0;
			last = f.before.alpha;
		}
		ck(starts == 1 && mm_leaving(9000).before.alpha == 1.0f,
		   "the panel on screen leaves over the shelf once, from whole");
		ck(ok, "and only ever goes");
	}
	/* B the same three times: each level comes back, so the difference is MENU */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(2000, true);
	mm_press(sub);
	deep = mm_open(3000, true);
	mm_press(deep);
	mm_close(deep, 9000, deep, false);
	ck(mm_at(sub, 9000 + X / 2).moving, "where B brings the level under back");

	/* ---- a press while a screen waits for the panel before it ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(2000, true);
	mm_press(sub);
	mm_close(sub, 9000, sub, false);
	ck(mm_settle(top, 9030, sub) == top && at_rest(mm_at(top, 9030)),
	   "A in the menu while its submenu's panel leaves: the menu's panel is the one on "
	   "screen, at rest");
	sub = mm_open(9030, true);
	f = mm_at(sub, 9030);
	ck(f.before.alpha == 1.0f && f.panel.alpha == 0.0f,
	   "and it is the one the next submenu waits for, whole");

	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(2000, true);
	mm_press(sub);
	mm_close(sub, 9000, sub, false);
	ck(mm_settle(top, 9030, sub) == top, "MENU over the paused game while its "
	   "achievements' panel leaves: the in-game menu's is the one on screen");
	mm_close(top, 9030, top, false);
	f = mm_leaving(9030);
	ck(f.moving && f.before.alpha == 1.0f, "and it is what leaves, from whole");

	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	sub = mm_open(2000, true);
	ck(mm_settle(sub, 2030, top) == sub && at_rest(mm_at(sub, 2030)),
	   "a press that opens over a submenu still waiting for its menu to leave: the "
	   "submenu's panel is the one on screen");

	mm_reset();
	top = mm_open(1000, false);
	ck(mm_settle(top, 1030, top) == top && at_rest(mm_at(top, 1030)),
	   "a press on a panel arriving: it is at rest, and on screen as it was");
	ck(mm_settle(0, 1030, 3) == 3, "a press on the shelf changes nothing on screen");

	/* ---- Muse's shelf ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_press(top);
	muse = mm_base(5000, true);
	ck(muse == 2, "Muse's shelf is a level of its own");
	f = mm_leaving(5000 + X / 2);
	ck(leaving(f.before, 0.5f) && near(f.scrim, f.before.alpha),
	   "and takes the menu's panel off over itself, with the scrim");
	sub = mm_open(5100, false);
	f = mm_at(sub, 5100);
	ck(!mm_leaving(5100).moving && f.scrim == 0.0f && f.before.alpha == 0.0f,
	   "a panel opened on it brings the scrim in, the menu's cut");
	mm_press(sub);
	mm_close(sub, 6000, sub, false);
	ck(mm_leaving(6000).moving, "and leaves over it with the scrim");
	mm_close(muse, 7000, 0, false);
	f = mm_at(top, 7000);
	ck(mm_depth() == 1 && f.panel.alpha == 0.0f && f.scrim == 0.0f && f.before.alpha == 0.0f &&
	   !mm_leaving(7000).moving,
	   "closed, it gives the menu back as it opened, scrim and all");
	ck(at_rest(mm_at(top, 7000 + Q)), "which is at rest at quick");

	/* MENU in Muse's menu, over its tracks, closes that menu alone */
	mm_reset();
	muse = mm_base(1000, false);
	top = mm_open(1100, false);
	mm_press(top);
	sub = mm_open(2000, true);
	mm_press(sub);
	mm_close(sub, 3000, sub, true);
	ck(at_rest(mm_at(top, 3000 + X / 2)), "MENU in Muse's menu brings nothing back by itself");
	mm_return(top, 3000, sub);
	f = mm_at(top, 3000);
	ck(mm_depth() == top && f.before.alpha == 1.0f && f.panel.alpha == 0.0f && f.scrim == 1.0f,
	   "but where it stops, the tracks come back after the menu's panel leaves");
	ck(at_rest(mm_at(top, 3000 + X + Q)), "at rest at exit and quick");
	mm_return(muse, 4000, 0);
	ck(mm_depth() == muse && !mm_leaving(4000).moving,
	   "and stopping at Muse's shelf brings nothing back");

	/* ---- the stack's edges ---- */
	mm_reset();
	top = mm_open(1000, false);
	mm_reset();
	mm_close(top, 2000, top, false);
	ck(mm_depth() == 0 && !mm_leaving(2000).moving,
	   "a level closed after the shelf put the stack down does nothing");
	ok = 1;
	mm_reset();
	for (t = 0; t < MM_LEVELS + 4; t++) {
		int lv = mm_open(1000, true);

		if (lv) mm_press(lv);
		if (t >= MM_LEVELS - 1 && lv != 0) ok = 0;
		if (!at_rest(mm_at(0, 1000 + t))) ok = 0;
	}
	ck(ok, "past the deepest level a screen is level 0, which is always at rest");

	/* ---- a held d-pad ---- */
	mm_reset();
	top = mm_open(1000, false);
	sub = 0;
	ok = 1;
	for (t = 1000 + REPEAT_RATE_MS / 3; t < 1000 + 20 * REPEAT_RATE_MS; t += REPEAT_RATE_MS) {
		mm_press(top);
		if (!at_rest(mm_at(top, t)) || !at_rest(mm_at(top, t + 1))) ok = 0;
	}
	sub = mm_open(5000, true);
	for (t = 5000 + REPEAT_RATE_MS / 3; t < 5000 + 20 * REPEAT_RATE_MS; t += REPEAT_RATE_MS) {
		mm_press(sub);
		if (!at_rest(mm_at(sub, t)) || !at_rest(mm_at(sub, t + 1))) ok = 0;
	}
	ck(ok, "a press every repeat through an open puts the panel at rest once and "
	       "starts nothing over");

	/* ---- the in-game menu, in src/main.c ---- */
	{
		char *gm = main_fn("static void game_menu(app *a)\n{");
		char *pl = main_fn("static leave_end panel_leave_run(");

		ck(gm && pl, "src/main.c has game_menu and panel_leave_run (run from the top "
		             "of the tree)");
		if (pl) {
			long poll = first(pl, "plat_input_poll(&a->in)");
			long power = first(pl, "a->in.pressed[IN_POWER] || idle_due(a)");
			long cut = first(pl, "mm_leaving_cut()");
			long ask = first(pl, "mm_leaving(anim_now())");
			long draw = first(pl, "plat_present()");

			ck(poll >= 0 && power > poll && cut > power && ask > cut && draw > ask,
			   "the in-game menu's close reads input every frame, hands power and "
			   "Auto Off back, and a press ends it before the frame is drawn");
			ck(first(pl, "LEAVE_POWER") > poll, "and says when power came");
		}
		if (gm) {
			long leave = first(gm, "panel_leave_run(");
			long power = first(gm, "LEAVE_POWER");
			long stop = first(gm, "gm_power(");
			long cut = first(gm, "mm_leaving_cut();");
			long pages = first(gm, "present_clear(a, DIATOM_PAGE);");
			long resume = first(gm, "plat_resident_line(\"RESUME\");");
			const char *after = resume >= 0 ? gm + resume : "";
			const char *no = leave >= 0 ? strstr(gm + leave, "c.resume = false") : NULL;

			ck(leave >= 0 && power > leave && stop > power && cut > stop &&
			   pages > cut && resume > pages,
			   "game_menu draws its close, stops the game on power in it, then drops "
			   "what is left of it, then hands the pages back, then sends RESUME");
			ck(no && no - gm < pages, "and does not resume a game power stopped");
			ck(!strstr(after, "plat_present") && !strstr(after, "present_clear") &&
			   !strstr(after, "panel_leave") && !strstr(after, "menu_draw") &&
			   !strstr(after, "render("),
			   "and draws nothing after RESUME");
		}
		free(gm);
		free(pl);
	}

	/* A clock that wraps, as SDL_GetTicks does after 49 days */
	mm_reset();
	top = mm_open(0xFFFFFFFFu - 50, false);
	f = mm_at(top, 10);
	ck(f.moving && f.panel.alpha > 0.0f && f.panel.alpha < 1.0f,
	   "an open across the clock's wrap is still arriving");
	ck(at_rest(mm_at(top, 0xFFFFFFFFu - 50 + Q)), "and lands");

	if (fails) {
		printf("menumotion: %d check(s) FAILED\n", fails);
		return 1;
	}
	printf("ok: a panel opens in %u ms and closes in %u, one at a time, a press puts "
	       "it at rest, and MENU's unwind leaves once\n", Q, X);
	return 0;
}
