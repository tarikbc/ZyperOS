/* SPDX-License-Identifier: GPL-3.0-only */
/* See achtab_screen.h. */
#include <stdio.h>
#include <string.h>

#include "achtab_screen.h"
#include "design.h"
#include "rabadge.h"
#include "theme.h"
#include "ui.h"

/* The counts line's place under the tab row: the meta line's height and a
 * grid step under it, so the first row keeps its usual distance from it */
#define AT_HEADER_H (3 * DS_GRID + 32)

/* What the header and the badges draw from: the rows last built. The panel
 * calls them with no context of their own. */
static const achtab_bufs *g_at;
static at_source          g_at_src;
static SDL_Renderer      *g_at_r;

static void at_header(app *a, const SDL_Rect *q)
{
	TTF_Font *fm = ui_font(UI_F_META);
	const char *s = g_at ? g_at->counts : "";
	/* All of them earned: the line in accent-ink, as an earned row's value */
	SDL_Color col = g_at && g_at->mastered ? ui_col(theme()->accent_ink, 255)
	                                       : UI_TEXT_SOFT;
	int cy = q->y + q->h / 2 - DS_GRID;

	if (!*s) return;
	if (ui_text_width(fm, s) <= q->w)
		ui_text(a->r, fm, s, q->x + q->w / 2, ui_cap_y(fm, cy), 0, col);
	else
		ui_text_marquee(a->r, fm, s, q->x, ui_cap_y(fm, cy), q->w, SDL_GetTicks(), col);
}

/* Row `row`'s badge: the achievement's, greyed until earned, or the plain one
 * where the card has none (rb_icon). Every row has one, so nothing moves
 * when a badge lands: the next frame reads the file. */
static SDL_Texture *at_badge(int row)
{
	const cheevo *c;

	if (!g_at || g_at->state != AT_LIST || !g_at_src || row < 0 || row >= g_at->n) return NULL;
	c = g_at_src(row);
	return c ? rb_icon(g_at_r, chv_source(), c->badge, !(c->earned || c->earned_now),
	                   theme()) : NULL;
}

int achtab_rows(app *a, const at_facts *f, at_source at, achtab_bufs *b,
                menu_row *rows, int max, int w, menu_style *st)
{
	int n = 0, i;

	b->state = at_state_of(f);
	g_at = b;
	g_at_src = at;
	g_at_r = a->r;
	st->side_w = 0;
	st->sound = b->sound;
	st->vcolors = b->vcol;

	if (b->state == AT_LIST) {
		/* A badge that landed since the last frame is read on this one */
		rb_landed();
		at_counts(f, b->counts, sizeof b->counts);
		b->mastered = f->pending == 0 && f->earned >= f->count;
		for (i = 0; i < f->count && n < max && n < CHV_MAX; i++) {
			const cheevo *c = at(i);

			if (!c) break;
			at_row(c, &rows[n], &b->vcol[n], b->vals[n], sizeof b->vals[n]);
			b->sound[n] = 0;
			n++;
		}
		st->header = at_header;
		st->header_h = AT_HEADER_H;
		st->row_icon = at_badge;
		st->visits_all = true;
	} else {
		const char *text = T(at_message(b->state));
		int k = ui_balance_lines(ui_font(UI_F_MENU), text, w, AT_LINES, b->lines);

		b->counts[0] = '\0';
		b->mastered = false;
		/* Drawn whole, on lines: said so to the host's fit check */
		ui_fit_drawn(text);
		for (i = 0; i < k && n < max; i++) {
			rows[n] = (menu_row){ b->lines[i], NULL, false };
			b->vcol[n] = 0;
			/* A does nothing on a sentence: deny */
			b->sound[n] = MENU_SND_NO_A;
			n++;
		}
		st->header = NULL;
		st->header_h = 0;
		st->row_icon = NULL;
		st->visits_all = false;
	}
	b->n = n;
	return n;
}

const cheevo *achtab_pick(const achtab_bufs *b, at_source at, int sel)
{
	if (!b || b->state != AT_LIST || !at || sel < 0 || sel >= b->n) return NULL;
	return at(sel);
}

void achtab_close(void)
{
	rb_textures_free();
	g_at = NULL;
	g_at_src = NULL;
}

/* ---------------------------------------------------------------- shots -- */

/* Real titles, and the long ones are the point, as the list's own shot says
 * (main.c, --cheevos-screen): a set's names run past the panel often */
static cheevo g_shot_set[8];
static int    g_shot_earned = 3;

static void shot_fill(void)
{
	static const struct { const char *t; int p; } sample[8] = {
		{ "The Path to Disaster", 5 },
		{ "Hold On To Your Potatoes, We're In For A Bumpy Ride", 10 },
		{ "Violated Heavens", 10 },
		{ "I Am Not Left Handed Either, But I Can Still Beat You", 25 },
		{ "Koma Faction's Fall", 25 },
		{ "Not So Disaster", 10 },
		{ "Storming The Fortress", 10 },
		{ "Blazing through the Skies", 10 },
	};
	int i;

	for (i = 0; i < 8; i++) {
		memset(&g_shot_set[i], 0, sizeof g_shot_set[i]);
		g_shot_set[i].id = 1000 + i;
		g_shot_set[i].points = sample[i].p;
		g_shot_set[i].earned = i < g_shot_earned;
		snprintf(g_shot_set[i].title, sizeof g_shot_set[i].title, "%s", sample[i].t);
	}
	/* One count moving, where its points are (the fifth, when not earned) */
	if (g_shot_earned <= 4) snprintf(g_shot_set[4].progress, sizeof g_shot_set[4].progress, "74/100");
}

void achtab_shot_earned(int earned)
{
	g_shot_earned = earned < 0 ? 3 : earned > 8 ? 8 : earned;
	shot_fill();
}

const cheevo *achtab_shot_at(int i)
{
	if (!g_shot_set[0].id) shot_fill();
	return i >= 0 && i < 8 ? &g_shot_set[i] : NULL;
}

int achtab_shot_count(void) { return 8; }
