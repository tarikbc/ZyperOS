/* SPDX-License-Identifier: GPL-3.0-only */
/* The Screenshots shelf's own words and keys. See src/shots_screen.h. */
#include "shots_screen.h"

#include "gamedel_screen.h"
#include "panel.h"
#include "screencard.h"
#include "shelf_draw.h"
#include "shelfbuild.h"
#include "shotname.h"
#include "sort.h"
#include "stats.h"
#include "texload.h"
#include "ui.h"
#include "uisound.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

bool shots_shelf(app *a, int s)
{
	return s >= 0 && s == shelf_shots_at(a);
}

void shots_png(const game_entry *g, char *out, size_t n)
{
	snprintf(out, n, "%s/%s", P_CARD, g->file);
}

/* ---- the line under a shot's name ---------------------------------------- */

const char *shots_facts(app *a, int o, const game_entry *g)
{
	static char line[CFG_STR + 96];
	char ago[48], clock[16];
	int y, mo, d, h, mi, s;
	time_t now = time(NULL), then;
	struct tm tm, nt;
	long days;

	/* When, by the calendar's days as Played says it (stats_ago), and the
	 * time it was taken for today and yesterday */
	shot_parts(g->added, &y, &mo, &d, &h, &mi, &s);
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	tm.tm_isdst = -1;
	then = mktime(&tm);
	stats_ago((long)then, (long)now, ago, sizeof ago);
	localtime_r(&now, &nt);
	days = shot_days_before(g->added, shot_secs(nt.tm_year + 1900, nt.tm_mon + 1, nt.tm_mday,
	                                           nt.tm_hour, nt.tm_min, nt.tm_sec));
	if (days <= 1) {
		strftime(clock, sizeof clock, g_clock24 ? "%H:%M" : "%I:%M %p", &tm);
		snprintf(line, sizeof line, "%s \xc2\xb7 %s %s", a->sys.systems[o].name, ago,
		         clock[0] == '0' && !g_clock24 ? clock + 1 : clock);
	} else {
		snprintf(line, sizeof line, "%s \xc2\xb7 %s", a->sys.systems[o].name, ago);
	}
	return line;
}

/* ---- the viewer --------------------------------------------------------- *
 *
 * A on a shot opens it in the Video view (DESIGN.md 11.57, 11.68) for as
 * long as it is open: the View the shelf keeps is not changed, and B goes
 * back to it. `g_back` is that View while the viewer A opened is up, and
 * none when the Video view is the shelf's own. */
enum { BACK_NONE, BACK_COVERS, BACK_GRID, BACK_LIST };
static int g_back;

static bool viewer_by_a(const sysview *v)
{
	return v->video && g_back != BACK_NONE;
}

void shots_hints(app *a, sysview *v, bool video, key_hint *foot, int *nfoot, const char **a_word)
{
	const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
	int n = 0;

	(void)a;
	*a_word = T(S_GAMES_VIEW);
	if (video) {
		/* No A: the viewer is for looking (pick 6) */
		foot[n++] = (key_hint){ "B", T(viewer_by_a(v) ? S_GAMES_BACK : S_GAMES_SYSTEMS) };
		*a_word = NULL;
		if (!viewer_by_a(v)) foot[n++] = (key_hint){ "START", T(S_GAMES_TO_COVERS) };
	} else {
		foot[n++] = (key_hint){ "B", T(S_GAMES_SYSTEMS) };
		foot[n++] = (key_hint){ "START", T(v->grid ? S_GAMES_TO_LIST : v->listed ? S_GAMES_TO_VIDEO
		                                  : S_GAMES_TO_GRID) };
	}
	if (SHOTS_DELETE_KEY == IN_X && g) foot[n++] = (key_hint){ "X", T(S_GAMEMENU_DELETE_YES) };
	if (SHOTS_DELETE_KEY == IN_Y && g) foot[n++] = (key_hint){ "Y", T(S_GAMEMENU_DELETE_YES) };
	*nfoot = n;
}

static SDL_Texture *g_view;
static int g_view_idx = -1, g_view_w, g_view_h;

void shots_view_drop(void)
{
	if (g_view) SDL_DestroyTexture(g_view);
	g_view = NULL;
	g_view_idx = -1;
}

void shots_view_put(app *a, int idx, SDL_Surface *s, bool card)
{
	if (s && !card) {
		shots_view_drop();
		if ((g_view = SDL_CreateTextureFromSurface(a->r, s))) {
			g_view_idx = idx;
			SDL_QueryTexture(g_view, NULL, NULL, &g_view_w, &g_view_h);
		}
		redraw_now();
	}
	if (s) {
		if (card) ui_card_free(s);
		else SDL_FreeSurface(s);
	}
}

SDL_Texture *shots_view_tex(app *a, sysview *v, int *w, int *h)
{
	int i = v->cursor;
	char png[LIB_PATH * 2];

	if (i < 0 || i >= v->list.count) return NULL;
	if (g_view && g_view_idx == i) {
		*w = g_view_w;
		*h = g_view_h;
		return g_view;
	}
	/* Asked of the art workers until it is in, as a card is (asking again
	 * is ignored while it is on its way): the picture alone, scaled up
	 * whole by the nearest pixel (src/screencard.h) */
	shots_png(&v->list.items[i], png, sizeof png);
	if (!texload_want_screen(SHOTS_VIEW_SYS, i, png, NULL, NULL))
		shots_view_put(a, i, screen_card_make(png, NULL, NULL, false), false);   /* no workers: a shot */
	if (g_view && g_view_idx == i) {
		*w = g_view_w;
		*h = g_view_h;
		return g_view;
	}
	return NULL;
}

/* ---- the keys ------------------------------------------------------------ */

/* The next (`d` 1) or the last (-1) game's first shot from the cursor, as
 * the shelf's order has them; the cursor when every shot is of one game */
static int game_jump(const sysview *v, int d)
{
	int n = v->list.count, cur = v->cursor, k, j;
	const char *g = v->list.items[cur].name;

	for (k = 1; k < n; k++) {
		j = ((cur + d * k) % n + n) % n;
		if (strcmp(v->list.items[j].name, g)) break;
	}
	if (k >= n) return cur;
	if (d > 0) return j;
	/* back: to the first of that game's run */
	while (k + 1 < n) {
		int p = ((j - 1) % n + n) % n;

		if (p == cur || strcmp(v->list.items[p].name, v->list.items[j].name)) break;
		j = p;
		k++;
	}
	return j;
}

bool shots_jump(app *a, sysview *v)
{
	int d = 0, was = v->cursor;

	if (!v->video || v->list.count <= 1) return false;
	if (in_repeat(&a->in, IN_L1)) d = -1;
	if (in_repeat(&a->in, IN_R1)) d = 1;
	if (!d) return false;
	v->cursor = game_jump(v, d);
	step_sound(a, d > 0 ? IN_R1 : IN_L1, was, v->cursor);
	return true;
}

bool shots_delete_now(app *a, sysview *v)
{
	char png[LIB_PATH * 2];

	shots_png(&v->list.items[v->cursor], png, sizeof png);
	if (unlink(png) != 0 && errno != ENOENT) {
		fprintf(stderr, "shots: could not delete %s: %s\n", png, strerror(errno));
		return false;
	}
	fprintf(stderr, "shots: deleted %s\n", png);
	refresh_shots_shelf(a);
	/* The last shot gone: the viewer with it */
	if (!shots_shelf(a, a->sys_cursor)) g_back = BACK_NONE;
	return true;
}

/* X on a shot: the question, the cursor on Cancel, then the file. Its
 * game, its saves and its cover stay. The shelf is read again, on the shot
 * that took its place, or the home when it was the last. */
static void delete_shot(app *a, sysview *v)
{
	char q[LIB_NAME + 512];

	cq_text(CQ_SHOT_DELETE, q, sizeof q, v->list.items[v->cursor].title);
	if (!confirm_ask(a, CQ_SHOT_DELETE, q) || !a->running) return;
	if (!shots_delete_now(a, v)) result_message(a, T(S_SHOTS_DELETE), T(S_SHOTS_FAILED));
}

bool shots_keys(app *a, sysview *v)
{
	const in_state *in = &a->in;
	const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;

	/* The viewer A opened, gone some other way (MENU > View): the shelf's
	 * own View from here */
	if (!v->video) g_back = BACK_NONE;
	if (!g) return false;
	if (SHOTS_DELETE_KEY != IN_NONE && in->pressed[SHOTS_DELETE_KEY]) {
		uisound_play(DS_SOUND_ACCEPT);
		delete_shot(a, v);
		return true;
	}
	/* A shot has no details and nothing to start over */
	if (in->pressed[IN_X] || in->pressed[IN_Y]) {
		uisound_play(DS_SOUND_DENY);
		return true;
	}
	if (viewer_by_a(v) && in->pressed[IN_START] && !in->down[IN_MENU] && !in->down[IN_SELECT]) {
		uisound_play(DS_SOUND_DENY);
		return true;
	}
	/* B in the viewer A opened: back to the View it was opened from */
	if (viewer_by_a(v) && in->pressed[IN_BACK]) {
		uisound_play(DS_SOUND_BACK);
		v->video = false;
		v->grid = g_back == BACK_GRID;
		v->listed = g_back == BACK_LIST;
		g_back = BACK_NONE;
		v->lm.set = false;
		v->gm.set = false;
		games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
		return true;
	}
	if (!in->pressed[IN_ACCEPT]) return false;
	if (!v->video) {
		/* A on a shot in Covers, Grid or List: the shot on the whole screen,
		 * as the Video view draws a game's snap; not kept as the shelf's View */
		uisound_play(DS_SOUND_ACCEPT);
		g_back = v->grid ? BACK_GRID : v->listed ? BACK_LIST : BACK_COVERS;
		v->video = true;
		v->grid = v->listed = false;
		vv_reset(&v->vm, plat_now_ms());
		games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
		return true;
	}
	/* A in the viewer: the whole shot at once, the band and the name's
	 * card off the screen without waiting for the rest (2026-11-20: "when
	 * clicking A while in play mode, it goes straight to full without
	 * needing to wait for it to collapse"); the next press brings them
	 * back. The shot's game still starts only from its own shelf (pick 6) */
	uisound_play(DS_SOUND_ACCEPT);
	vv_rest(&v->vm, plat_now_ms());
	return true;
}

/* ---- the rest ------------------------------------------------------------ */

static bool g_taken;

void shots_taken(void)
{
	g_taken = true;
}

bool shots_take_new(void)
{
	bool was = g_taken;

	g_taken = false;
	return was;
}

void shots_delete_preview(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	char q[LIB_NAME + 512];

	if (!shots_shelf(a, a->sys_cursor) || v->list.count == 0) return;
	cq_text(CQ_SHOT_DELETE, q, sizeof q, v->list.items[v->cursor].title);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	confirm_preview(a, CQ_SHOT_DELETE, q);
}

/* MENU > Sort By's three on this shelf (sort.h's SORT_SHOTS_ORDERS), each
 * one's newest first (src/shotname.h's shot_cmp); a shot and its owner move
 * together */
struct shot_pair { game_entry e; int owner; };
static shot_order g_order;

static int pair_cmp(const void *x, const void *y)
{
	const struct shot_pair *p = x, *q = y;
	shot_key a = { p->e.title, p->e.added, p->owner }, b = { q->e.title, q->e.added, q->owner };

	return shot_cmp(&a, &b, g_order);
}

void shots_sort(app *a, int s)
{
	sysview *v = &a->view[s];
	struct shot_pair *p;
	int i, n = v->list.count;

	if (n <= 1 || !v->owner || !(p = malloc((size_t)n * sizeof *p))) return;
	g_order = v->sort == sort_index("name") ? SHOT_ORDER_NAME
	        : v->sort == sort_index("system") ? SHOT_ORDER_SYSTEM : SHOT_ORDER_ADDED;
	for (i = 0; i < n; i++) { p[i].e = v->list.items[i]; p[i].owner = v->owner[i]; }
	qsort(p, (size_t)n, sizeof *p, pair_cmp);
	for (i = 0; i < n; i++) { v->list.items[i] = p[i].e; v->owner[i] = p[i].owner; }
	free(p);
}
