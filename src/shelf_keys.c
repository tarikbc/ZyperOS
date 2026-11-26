/* SPDX-License-Identifier: GPL-3.0-only */
#include "shelf_keys.h"

#include "app.h"
#include "ctl_channel.h"
#include "favorites.h"
#include "launch_screen.h"
#include "panel.h"
#include "shelf_draw.h"
#include "shelfbuild.h"
#include "shots_screen.h"
#include "sys_menu.h"
#include "uisound.h"

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* ---------- input --------------------------------------------------------- */

void enter_system(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	/* MP4 too: its cards are albums, and opening one is the games shelf's
	 * own A - see update_games. */
	if (v->list.count == 0) return;
	a->screen = SCREEN_GAMES;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	prime_window(a, a->sys_cursor);
}

/* Y on a card: a favorite added is a reward, one taken away goes back, and a
 * list already full (FAV_MAX) is a press nothing can act on. */
void fav_toggle_heard(const char *tag, const char *file)
{
	bool was = fav_is(tag, file), now = fav_toggle(tag, file);

	uisound_play(now ? DS_SOUND_REWARD : was ? DS_SOUND_BACK : DS_SOUND_DENY);
}

/* Start on a system's cards when the systems row moves onto it, not when A is
 * pressed.
 *
 * Entering used to be the first time anything asked for a system's covers, so
 * the games shelf drew with empty slots until they arrived. Measured
 * 2026-09-14, cold after a reboot, across six systems: the focused card 70-160ms
 * after A, the whole row 95-177ms. Short, and visible. Leaving a system keeps
 * the cards around its cursor, which is why only the first entry showed it.
 *
 * Starting here gives the decode however long it takes to reach the system and
 * press A, and that is enough: re-measured the same way, four systems entered
 * after a reboot all had every card already loaded on arrival.
 *
 * Whatever was queued for the system just passed is dropped first, so a
 * held direction does not decode a row for every system it crosses, and the one
 * the row stops on is not left waiting behind them.
 *
 * The visible row only; entering asks for two more either side. They stay after
 * the row moves on, which is less than already stays: a system entered and left
 * keeps nine. */
static void load_ahead(app *a)
{
	texload_forget();
	prime_toward(a, a->sys_cursor, a->view[a->sys_cursor].cursor);
}

/* The focused system's shelf, its covers asked for ahead of A, once the
 * home's cursor has rested on it HOME_PRIME_REST ms, not on every step
 * (2026-11-19: on each step the paths, file checks and MP4's cover asks of
 * seven games ran on the frame the step was drawn on, and their uploads on
 * the frames after, while the home showed none of them; on the Pixel 2 the
 * home's frames stalled to 50 ms and more as the cursor crossed systems). A
 * shelf opened sooner asks for its own covers as it draws. */
#define HOME_PRIME_REST 400
static void load_later(app *a)
{
	a->home_prime_at = (plat_now_ms() + HOME_PRIME_REST) | 1;
	redraw_at(a->home_prime_at);
}

/* Where up (-1) or down (+1) goes on the home with the order on A to Z */
static int home_letter_at(const app *a, int dir)
{
	unsigned char keys[CFG_MAX_SYSTEMS];
	int i, n = a->sys.count < CFG_MAX_SYSTEMS ? a->sys.count : CFG_MAX_SYSTEMS;

	for (i = 0; i < n; i++) keys[i] = home_group_key(a->sys.systems[i].tag, a->sys.systems[i].core,
		                         a->sys.systems[i].name);
	return home_letter_jump(keys, n, a->sys_cursor, dir);
}

void update_systems(app *a)
{
	int n = a->sys.count;

	/* No games: the empty home's keys (and MP4 alone is no shelf to move
	 * on). No systems at all also means the modulo below divides by zero:
	 * the first press of left or right took the launcher down with SIGFPE
	 * the moment empty systems started being hidden. */
	if (no_games(a)) { empty_keys(a); return; }
	if (a->home_prime_at && (Sint32)(plat_now_ms() - a->home_prime_at) >= 0) {
		a->home_prime_at = 0;
		load_ahead(a);
	}

	/* START turns the home's layout on, Spotlight to Gallery to Grid and
	 * round, as MENU > Home Screen's row does and as START turns a shelf's
	 * View (2026-11-17). Not with MENU or SELECT held, as on the shelf:
	 * MENU + START is the way out (tools/zyhome.c). */
	if (a->in.pressed[IN_START] && !a->in.down[IN_MENU] && !a->in.down[IN_SELECT]) {
		uisound_play(DS_SOUND_MOVE);
		home_turn(a, 1);
		return;
	}

	/* The direction is carried through, not inferred from the cursor: on a
	 * shelf of two, moving from either card to the other is one step in BOTH
	 * directions, and only the press says which. */
	in_button back = IN_LEFT;
	in_button fwd  = IN_RIGHT;
	in_button sb = IN_NONE;              /* the step's button, for its sound */
	int dir = 0, was = a->sys_cursor;

	/* On the Grid a held Left or Right stops at the first or the last
	 * system with the edge sound once, as a list's does, and a new press
	 * goes round (2026-11-19, from the home grid lab); a press goes round
	 * on every layout */
	bool grid = a->home_layout == HOME_GRID;

	if (in_repeat(&a->in, back)) {
		sb = back;
		if (!grid || a->sys_cursor > 0 || a->in.pressed[back]) {
			a->sys_cursor = (a->sys_cursor - 1 + n) % n; dir = -1;
		}
	}
	if (in_repeat(&a->in, fwd)) {
		sb = fwd;
		if (!grid || a->sys_cursor < n - 1 || a->in.pressed[fwd]) {
			a->sys_cursor = (a->sys_cursor + 1) % n; dir = +1;
		}
	}
	/* The Grid home (DESIGN.md 11.1): Up and Down a row, to the system
	 * nearest where the column started (Down then Up comes back), stopping
	 * at the top and the foot (the edge sound); L1 and R1 a screenful, or to
	 * the end, then round (gg_page, as the shelf grid's) */
	if (grid) {
		gh_geom g = home_grid_geom(n);
		int page = g.cols * g.win, to = a->sys_cursor;

		if (sb != IN_NONE) a->home.grid_ax = -1;
		/* Each button read once: a repeat is true once a frame, so a
		 * second read of a held Down was false and turned it round */
		int d = in_repeat_axis(&a->in, IN_UP, IN_DOWN);

		if (d) {
			int step;

			if (a->home.grid_ax < 0) {
				int x, y;

				gh_cell(&g, to, &x, &y);
				a->home.grid_ax = x + g.cw / 2;
			}
			step = gh_row_step(&g, to, d, a->home.grid_ax);
			if (step >= 0) to = step;
			sb = d > 0 ? IN_DOWN : IN_UP;
		}
		if (in_repeat(&a->in, IN_L1)) { to = gg_page(to, n, page, -1); sb = IN_L1; a->home.grid_ax = -1; }
		if (in_repeat(&a->in, IN_R1)) { to = gg_page(to, n, page, +1); sb = IN_R1; a->home.grid_ax = -1; }
		if (to != a->sys_cursor) dir = to > a->sys_cursor ? 1 : -1;
		a->sys_cursor = to;
		if (a->sys_cursor == was) dir = 0;
		if (dir) load_later(a);
		if (sb != IN_NONE) step_sound(a, sb, was, a->sys_cursor);
		if (a->in.pressed[IN_ACCEPT]) {
			if (is_mp4(&a->sys.systems[a->sys_cursor])) mp4_open(a, NULL, NULL);
			else { uisound_play(DS_SOUND_ACCEPT); enter_system(a); }
		}
		home_select(&a->home, a->sys_cursor, n, dir, plat_now_ms());
		return;
	}
	/* L1/R1 jump three systems, round the ends as a step goes (home_jump),
	 * the row moving all the way in the press's direction */
	if (in_repeat(&a->in, IN_L1)) { a->sys_cursor = home_jump(a->sys_cursor, n, -1); dir = -1; sb = IN_L1; }
	if (in_repeat(&a->in, IN_R1)) { a->sys_cursor = home_jump(a->sys_cursor, n, +1); dir = +1; sb = IN_R1; }
	/* Up and down do the same, one group at a time: three systems by release
	 * date, and an initial by A to Z (home_letter_jump). Built on the press,
	 * from the names in memory - nothing is read. */
	int ud = in_repeat_axis(&a->in, IN_UP, IN_DOWN);

	if (ud) {
		int d = ud;

		a->sys_cursor = g_order_release ? home_jump(a->sys_cursor, n, d)
		                                : home_letter_at(a, d);
		dir = d; sb = d > 0 ? IN_DOWN : IN_UP;
	}
	if (a->sys_cursor == was) dir = 0;
	if (dir) load_later(a);
	if (sb != IN_NONE) step_sound(a, sb, was, a->sys_cursor);
	/* MP4's card opens MP4, the one SELECT opens - see update_games. */
	if (a->in.pressed[IN_ACCEPT]) {
		if (is_mp4(&a->sys.systems[a->sys_cursor])) mp4_open(a, NULL, NULL);
		else { uisound_play(DS_SOUND_ACCEPT); enter_system(a); }
	}
	home_select(&a->home, a->sys_cursor, n, dir, plat_now_ms());
}

/* The first alphanumeric character of a name, upper-cased, or '\0' for a name
 * with none at all - which groups those few together rather than giving each
 * one a group of its own. Leading articles and punctuation are deliberately
 * NOT skipped: lib_scan sorts the shelf by its displayed title with strcasecmp,
 * so grouping that disagreed with the order the cards are drawn in would make
 * the jump land somewhere that looks arbitrary. "The Legend of Zelda" files
 * under T here because it sits under T on the shelf. */
static char shelf_initial(const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c >= 'a' && c <= 'z') return (char)(c - 32);
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (char)c;
	}
	return '\0';
}

/* The first game sharing idx's initial, walking backwards from it. */
/* What the letter jump reads: the title, or on MP4's shelf the artist. */
static const char *shelf_key(const sysview *v, int i)
{
	/* A film loose in Videos is under no name, and goes by its own: MP4's
	 * All in folder order puts it where its title does (mp4lib.h) */
	return v->jump_by_name && v->list.items[i].name[0] ? v->list.items[i].name
	                                                   : v->list.items[i].title;
}

/* The run game i is in: its initial, and with Favorites First on, which of
 * the two parts it is in (DESIGN.md 11.70). The favorites' last initial and
 * the other games' first are two stops even when they are one letter: Down
 * from "Zelda", the last favorite, lands on "Zero Wing", the first of the
 * others, and not past it. */
static int shelf_group(const sysview *v, int i)
{
	return (unsigned char)shelf_initial(shelf_key(v, i)) |
	       (v->nfav > 0 && i >= v->fav_from && i < v->fav_from + v->nfav ? 0x100 : 0);
}

static int shelf_group_start(sysview *v, int idx)
{
	int n = v->list.count, i = idx;
	int c = shelf_group(v, idx);

	for (;;) {
		int p = (i - 1 + n) % n;
		if (p == idx) return idx;             /* one initial, the whole shelf */
		if (shelf_group(v, p) != c) break;
		i = p;
	}
	return i;
}

/* Up and down cross the shelf one initial at a time, which on a long library
 * is the difference between forty presses and two. Down lands on the first
 * game of the next initial. Up lands on the first game of THIS one, and moves
 * to the previous initial only when the cursor is already there - so from any
 * group's first game, up and down are exact inverses. */
int shelf_letter_jump(sysview *v, int dir)
{
	int n = v->list.count, i, cur = v->cursor;
	int c0 = shelf_group(v, cur);

	if (n <= 1) return cur;
	if (dir > 0) {
		for (i = 1; i < n; i++) {
			int k = (cur + i) % n;
			if (shelf_group(v, k) != c0) return k;
		}
		return cur;                           /* every name starts alike */
	}
	i = shelf_group_start(v, cur);
	if (i != cur) return i;
	/* Already at the top of the group. If what precedes it shares the initial
	 * then the shelf is one group and there is nowhere to go - the same answer
	 * down gives, rather than shuffling back by one. */
	i = (cur - 1 + n) % n;
	if (shelf_group(v, i) == c0) return cur;
	return shelf_group_start(v, i);
}

/* A shelf's cursor moved by this pass's presses, in its View: along the row
 * of covers, the list or the grid (DESIGN.md 11.39, 11.47), with the step's
 * sound. The direction a step took, -1 or +1, and 0 for a jump or none. The
 * games shelf's and MP4's (src/mp4_screen.c, 11.67), so MP4's shelf moves
 * in each View exactly as a games shelf does. */
int shelf_move(app *a, sysview *v)
{
	int n = v->list.count;

	if (n <= 0) return 0;
	/* The two axes trade roles with the shelf rather than one of them being
	 * reassigned onto the other: stepping always runs along the row, and the
	 * letter jump always runs across it. Turning the shelf turns the d-pad
	 * with it and nothing is left doing two jobs. */
	in_button back = IN_LEFT, fwd = IN_RIGHT;
	in_button jup = IN_UP, jdn = IN_DOWN;
	in_button sb = IN_NONE;              /* the step's button, for its sound */
	int dir = 0, was = v->cursor;
	/* L1/R1's jump: a screenful of covers, or of the list's rows */
	int page = v->listed ? GL_ROWS : CF_WINDOW;

	/* The list (MENU > View) is the row turned: Up and Down step along it,
	 * Left and Right jump a letter (DESIGN.md 11.39) */
	if (v->listed) { back = IN_UP; fwd = IN_DOWN; jup = IN_LEFT; jdn = IN_RIGHT; }
	/* The grid (11.47): Left and Right step along its rows, round the ends
	 * as the row does; Up and Down a row, stopping at the top and the foot
	 * (the edge sound), and Down from over a short last row lands on its
	 * last cover; L1 and R1 a screenful. The four directions are the
	 * grid's, so it has no letter jump. */
	if (v->grid) {
		int cols = gg_cols(&v->gm), to = v->cursor;

		page = cols * GG_ROWS;
		jup = jdn = IN_NONE;
		if (in_repeat(&a->in, IN_UP)) {
			if (to >= cols) to -= cols;
			sb = IN_UP;
		}
		if (in_repeat(&a->in, IN_DOWN)) {
			if (to / cols < (n - 1) / cols) to = to + cols < n ? to + cols : n - 1;
			sb = IN_DOWN;
		}
		v->cursor = to;
	}

	if (in_repeat(&a->in, back)) { v->cursor = (v->cursor - 1 + n) % n; dir = -1; sb = back; }
	if (in_repeat(&a->in, fwd))  { v->cursor = (v->cursor + 1) % n; dir = +1; sb = fwd; }
	if (jdn != IN_NONE && in_repeat(&a->in, jdn))  { v->cursor = shelf_letter_jump(v, +1); sb = jdn; }
	if (jup != IN_NONE && in_repeat(&a->in, jup))  { v->cursor = shelf_letter_jump(v, -1); sb = jup; }
	/* L1/R1 jump a screenful, so a long shelf is crossable. Wrapped the long
	 * way round on purpose: C's % truncates toward zero, so on a shelf of
	 * three games the obvious `(cursor - CF_WINDOW + n*2) % n` lands on -1 and
	 * the draw walks off the front of the list.
	 * The grid's is a page's: on to the end, then round (gg_page). */
	if (in_repeat(&a->in, IN_L1)) {
		v->cursor = v->grid ? gg_page(v->cursor, n, page, -1) : ((v->cursor - page) % n + n) % n;
		sb = IN_L1;
	}
	if (in_repeat(&a->in, IN_R1)) {
		v->cursor = v->grid ? gg_page(v->cursor, n, page, +1) : (v->cursor + page) % n;
		sb = IN_R1;
	}
	if (sb != IN_NONE)
		step_sound(a, sb, was, v->cursor);
	return dir;
}

void update_games(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	int n = v->list.count;
	if (n <= 0) { a->screen = SCREEN_SYSTEMS; return; }
	/* MP4 HAS ONE DOOR. Its card used to open its shelf HERE, in the main
	 * loop, as if it were a console: a second MP4 beside the one SELECT
	 * opens, so SELECT on it opened that one on top, and the two looked
	 * alike. Eric's call, 2026-09-23: the card opens the same MP4 SELECT
	 * does. Whatever lands the main loop on MP4's shelf - A on its card, a
	 * jump to it, a screen put back at boot - hands over to mp4_open, which
	 * comes back to the card on the systems row. */
	if (is_mp4(&a->sys.systems[a->sys_cursor])) {
		a->screen = SCREEN_SYSTEMS;
		mp4_open(a, NULL, NULL);
		return;
	}
	/* The Video view's band (src/videoview.h): any press, or a button
	 * held, brings it back up and starts its rest again, and then does
	 * what it does */
	if (v->video) {
		bool any = false;
		/* but not A in the Screenshots viewer: A puts the band down there
		 * (shots_keys), and A still held the next pass would bring it
		 * straight back up */
		in_button skip = shots_shelf(a, a->sys_cursor) ? IN_ACCEPT : IN_NONE;

		for (int b = 0; b < IN_COUNT && !any; b++)
			any = b != (int)skip && (a->in.pressed[b] || a->in.down[b]);
		if (any) vv_touch(&v->vm, plat_now_ms());
	}
	/* The Screenshots shelf (DESIGN.md 11.68, src/shots_screen.h): in its
	 * viewer L1 and R1 go to the next or the last game's shots, read before
	 * the shelf's own L1 and R1 */
	bool shots = shots_shelf(a, a->sys_cursor);
	/* (a press reads true however often it is read: a jump is this pass's
	 * move, not the shelf's screenful as well) */
	int dir = shots && shots_jump(a, v) ? 0 : shelf_move(a, v);

	/* HERE, AND NOT AT THE END OF THE PASS, because four of the handlers below
	 * return without reaching the end.
	 *
	 * A repeat fires from the clock every REPEAT_RATE_MS while a direction is
	 * held, and the loop passes at about 60Hz while any button is down, so
	 * roughly one pass in five moves the cursor. Press A on one of those passes
	 * and the shelf was never told: `target` stays a card behind `cursor`, the
	 * game runs, the render on the way back snaps the cards to that stale
	 * target, and the NEXT pass through here finally notices and animates the
	 * difference. Which is exactly the report - the shelf comes back showing a
	 * game off to the side and then slides across to the right one - and the
	 * one-in-five is why it only happened sometimes.
	 *
	 * X and Y are the same shape: the info screen and a favorite toggle both
	 * return from here too. B is the only one that got away with it, because
	 * enter_system resets this coverflow on the way back in.
	 *
	 * The call is unconditional and `dir` may be 0: a letter jump and L1/R1
	 * move the cursor without a direction, and cf_set_cursor_dir works out the
	 * rest itself. It returns immediately when the cursor has not moved. */
	games_select(&v->row, v->cursor, n, dir, plat_now_ms());
	/* ...and its A, B, X, Y and START where they are its own: A opens a
	 * shot on the whole screen, B closes it, X deletes it */
	if (shots && shots_keys(a, v)) return;
	/* START turns the shelf's View on, Covers to Grid to List and round, as
	 * MENU's View row does (2026-11-13: "make clicking start in a system
	 * change view mode to list and vice versa"; the grid between them,
	 * 2026-11-15). Not with MENU or SELECT held: MENU + START is the way
	 * out (tools/zyhome.c), SELECT + START MP4's lock. */
	if (a->in.pressed[IN_START] && !a->in.down[IN_MENU] && !a->in.down[IN_SELECT]) {
		uisound_play(DS_SOUND_MOVE);
		view_turn(a, a->sys_cursor, 1);
		return;
	}
	/* B in a folder goes up one (src/shelfdir.h), onto the folder it left;
	 * at the top, to the systems */
	if (a->in.pressed[IN_BACK] && v->dir[0]) {
		uisound_play(DS_SOUND_BACK);
		shelf_up(a, v);
		return;
	}
	if (a->in.pressed[IN_BACK]) {
		uisound_play(DS_SOUND_BACK);
		a->screen = SCREEN_SYSTEMS;
		evict_far(v, TEX_KEEP_FAR);
		return;
	}
	/* A folder has no details and nothing to start over: a press nobody can
	 * act on still gets its sound (DESIGN.md 1) */
	if ((a->in.pressed[IN_X] || a->in.pressed[IN_Y]) && v->list.items[v->cursor].folder) {
		uisound_play(DS_SOUND_DENY);
		return;
	}
	/* A on a folder opens it */
	if (a->in.pressed[IN_ACCEPT] && v->list.items[v->cursor].folder) {
		uisound_play(DS_SOUND_ACCEPT);
		shelf_into(a, v);
		return;
	}
	/* X opens the game's details, where Y marks it a favorite. */
	if (a->in.pressed[IN_X] && n > 0) { uisound_play(DS_SOUND_ACCEPT); game_info_screen(a); return; }
	/* Y starts the game under the cursor fresh, without its resume point
	 * (2026-11-08): A continues it, Y starts over. A game with nothing to
	 * resume has nothing to start over from, so Y does nothing there.
	 * Favorites moved to the details page (X, then Y). */
	if (a->in.pressed[IN_Y] && n > 0) {
		if (!(g_game_page.valid && g_game_page.resume)) { uisound_play(DS_SOUND_DENY); return; }
		g_ctl_fresh = true;
		launch(a);
		g_ctl_fresh = false;
		return;
	}
	if (a->in.pressed[IN_ACCEPT]) { launch(a); return; }
}
