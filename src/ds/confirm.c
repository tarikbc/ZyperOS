/* SPDX-License-Identifier: GPL-3.0-only */
/* A question with two answers (src/ds/confirm.h) */
#include "ds/confirm.h"

#include "config.h"      /* CFG_NO_COLOR: a lift in no system's color */
#include "design.h"
#include "ds/layout.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* `text` with the space between a number and its unit kept: "4.1 MB", "1,2 Go"
 * never broken across two lines (a no-break space, which Lexend and the CJK
 * faces have, as French's before "?" is) */
static const char *units_kept(const char *text)
{
	static char out[2048];
	size_t i, o = 0;

	for (i = 0; text[i] && o + 3 < sizeof out; i++) {
		if (text[i] == ' ' && i > 0 && text[i - 1] >= '0' && text[i - 1] <= '9') {
			size_t k = i + 1;

			while ((text[k] >= 'A' && text[k] <= 'Z') || (text[k] >= 'a' && text[k] <= 'z')) k++;
			if (k > i + 1 && k - i - 1 <= 2 && (text[i + 1] == 'K' || text[i + 1] == 'M' ||
			    text[i + 1] == 'G' || text[i + 1] == 'T' || text[i + 1] == 'k' || text[i + 1] == 'B') &&
			    !((text[k] >= 'A' && text[k] <= 'Z') || (text[k] >= 'a' && text[k] <= 'z'))) {
				out[o++] = (char)0xC2;
				out[o++] = (char)0xA0;
				continue;
			}
		}
		out[o++] = text[i];
	}
	out[o] = '\0';
	return out;
}

/* The lines `text` takes at `w` in `f`, balanced, as a centered paragraph is (lb_balance),
 * into `out`; more than DS_CONFIRM_LINES says DS_CONFIRM_LINES + 1 */
static int lines_of(TTF_Font *f, const char *text, int w, char out[DS_CONFIRM_LINES][LB_LINE])
{
	static char big[DS_CONFIRM_LINES + 1][LB_LINE];
	int n;

	if (!text || !*text) return 0;
	n = ui_balance_lines(f, units_kept(text), w, DS_CONFIRM_LINES + 1, big);
	if (n < 0) n = 0;
	memcpy(out, big, sizeof big[0] * (size_t)(n < DS_CONFIRM_LINES ? n : DS_CONFIRM_LINES));
	return n;
}

/* What an answer's words and B's glyph take, its slab's ends and all */
static int answer_need(TTF_Font *f, const char *word, unsigned mark)
{
	return ui_text_width(f, word ? word : "") + 2 * DS_CONFIRM_WORD_PAD +
	       (mark & DS_CONFIRM_B ? DS_CONFIRM_GLYPH + DS_CONFIRM_GLYPH_GAP : 0);
}

void ds_confirm_layout(const ds_confirm *c, int screen_w, int screen_h, int top, int avail,
                       ds_confirm_lay *l)
{
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	const int pad = DS_MENU_PAD, row = DS_MENU_ROW, rule_h = pad / 2 + DS_RULE;
	const int step_m = ui_font_line(UI_F_MENU) + pad / 3, step_s = ui_font_line(UI_F_META) + pad / 3;
	int content_off, room, ans_h, half, full, qh, n, fit, i, ay;
	const char *heading = c->heading ? c->heading : "";

	memset(l, 0, sizeof *l);
	l->whole = true;
	l->content_w = c->width;
	l->panel.w = c->width + 2 * pad;

	/* The heading: the action, on one line; a word too long is broken onto
	 * two, never slid (my pick, 2026-11-22) */
	l->head_lines = 1;
	snprintf(l->head[0], sizeof l->head[0], "%s", heading);
	if (ui_text_width(fh, heading) > c->width) {
		int k = ui_balance_lines(fh, heading, c->width, 2, l->head);

		l->head_lines = k > 1 ? 2 : 1;
		for (i = 0; i < l->head_lines; i++)
			if (ui_text_width(fh, l->head[i]) > c->width) l->whole = false;
	}
	l->head_h = ui_font_line(UI_F_LABEL) * l->head_lines + pad;
	content_off = l->head_h + pad / 2;

	/* The answers: side by side, each in its half, in **menu** when both
	 * fit theirs, in **meta** (the pair keeps one size) when one does not,
	 * stacked only when one does not fit its half even then */
	half = (l->panel.w - 2 * DS_CONFIRM_INSET - DS_CONFIRM_GAP) / 2;
	full = l->panel.w - 2 * DS_CONFIRM_INSET;
	l->side = true;
	for (i = 0; i < 2; i++)
		if (answer_need(fm, c->answer[i], c->mark[i]) > half) l->small = true;
	if (l->small)
		for (i = 0; i < 2; i++)
			if (answer_need(fs, c->answer[i], c->mark[i]) > half) l->side = false;
	if (!l->side) {
		l->small = false;
		for (i = 0; i < 2; i++)
			if (answer_need(fm, c->answer[i], c->mark[i]) > full) l->small = true;
		for (i = 0; i < 2; i++)
			if (answer_need(fs, c->answer[i], c->mark[i]) > full) l->whole = false;
	}
	ans_h = l->side ? row : 2 * row;

	/* The question: wrapped in **menu**; in **meta** when that does not
	 * fit over the answers; scrolling in its room only past that */
	room = avail - content_off - pad - ans_h;
	if (room < step_s) room = step_s;
	l->window = room;
	l->step = step_m;
	n = lines_of(fm, c->text, c->width, l->lines);
	fit = room / step_m;
	if (n > fit) {
		l->meta = true;
		l->step = step_s;
		n = lines_of(fs, c->text, c->width, l->lines);
		fit = room / step_s;
		if (n > fit) {
			l->scroll = true;
			l->whole = false;
			if (n > DS_CONFIRM_LINES) n = DS_CONFIRM_LINES;
		}
	}
	l->nlines = n;
	qh = l->scroll ? fit * l->step : n * l->step;
	/* A rule between the question and its answers where there is room, so
	 * a long question does not read on into Delete */
	l->rule = n > 0 && (l->scroll ? room - qh >= rule_h : (n < fit || room - n * l->step >= rule_h));
	if (l->scroll) l->window = qh;

	l->panel.h = content_off + qh + (l->rule ? rule_h : 0) + ans_h + pad;
	l->panel.x = (screen_w - l->panel.w) / 2;
	l->panel.y = (screen_h - l->panel.h) / 2;
	if (l->panel.y < top) l->panel.y = top;
	l->content_x = l->panel.x + pad;
	l->q_y = content_off;

	ay = l->panel.y + content_off + qh + (l->rule ? rule_h : 0);
	for (i = 0; i < 2; i++)
		l->ans[i] = l->side
		    ? (SDL_Rect){ l->panel.x + DS_CONFIRM_INSET + i * (half + DS_CONFIRM_GAP), ay, half, row }
		    : (SDL_Rect){ l->panel.x + DS_CONFIRM_INSET, ay + i * row, full, row };
}

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return (SDL_Color){ (Uint8)(a.r + (b.r - a.r) * t + 0.5f), (Uint8)(a.g + (b.g - a.g) * t + 0.5f),
	                    (Uint8)(a.b + (b.b - a.b) * t + 0.5f), 255 };
}

/* `q` drawn `k` of the way to its pressed size, about its middle */
static SDL_Rect pressed(SDL_Rect q, float k)
{
	float s = 1.0f - (1.0f - DS_CONFIRM_PRESS / 100.0f) * k;
	int w = (int)lroundf(q.w * s), h = (int)lroundf(q.h * s);

	return (SDL_Rect){ q.x + (q.w - w) / 2, q.y + (q.h - h) / 2, w, h };
}

void ds_confirm_draw(SDL_Renderer *r, const ds_confirm *c, int screen_w, int screen_h, int top,
                     int avail, mm_pose pose, const ds_pass *pass)
{
	const ds_theme *thm = theme();
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	const int pad = DS_MENU_PAD, line_head = ui_font_line(UI_F_LABEL);
	const SDL_Color on = ui_col(thm->on_accent, 255);
	static ds_confirm_lay l;
	bool posed;
	int i, cx, y;
	float fill = c->fill < 0.0f ? 0.0f : c->fill > 1.0f ? 1.0f : c->fill;

	ds_confirm_layout(c, screen_w, screen_h, top, avail, &l);
	posed = ds_panel_open(r, &l.panel, CFG_NO_COLOR, pose, pass);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	cx = l.panel.x + l.panel.w / 2;

	/* The heading, its ink centered in its band as every menu's is
	 * (src/menu_panel.c), over the rule in **edge** */
	{
		int asc = fh ? TTF_FontAscent(fh) : line_head;
		int cap = fh ? ui_cap_height(fh) : asc;
		int block = cap + (l.head_lines - 1) * line_head;
		int hy = l.panel.y + UI_PANEL_BORDER + (l.head_h - UI_PANEL_BORDER - block) / 2 - (asc - cap);

		for (i = 0; i < l.head_lines; i++)
			ui_text(r, fh, l.head[i], cx, hy + i * line_head, 0, UI_TEXT_SOFT);
		if (l.head_lines > 1) ui_fit_drawn(c->heading);
		ds_panel_rule(r, l.content_x, l.panel.y + l.head_h, l.content_w, ui_col(thm->edge, 255));
	}

	/* The question, its lines centered in **text-dim**: the answers are
	 * what the eye goes to */
	{
		TTF_Font *f = l.meta ? fs : fm;
		int qy = l.panel.y + l.q_y, off = 0, qh = l.scroll ? l.window : l.nlines * l.step;

		if (l.scroll) {
			off = ui_pingpong(l.nlines * l.step - l.window, c->phase);
			SDL_RenderSetClipRect(r, &(SDL_Rect){ l.panel.x, qy, l.panel.w, l.window });
		}
		for (i = 0; i < l.nlines; i++)
			ui_text(r, f, l.lines[i], cx, ui_cap_y(f, qy - off + i * l.step + l.step / 2), 0, UI_TEXT_DIM);
		if (l.scroll) SDL_RenderSetClipRect(r, NULL);
		/* drawn in lines, so said whole to the host's check */
		if (!l.scroll && l.nlines > 1) ui_fit_drawn(c->text);
		y = qy + qh;
		if (l.rule) ds_panel_rule(r, l.content_x, y + (pad / 2 - DS_RULE) / 2, l.content_w,
		                          ui_col(thm->edge, 255));
	}

	/* The answers: a raised slab each, radius.row, the focus fill gliding
	 * between them; the answer that loses something in **warning** at rest,
	 * every focused one **on-accent** on lime as every focus is */
	{
		SDL_Rect a = l.ans[0], b = l.ans[1], f;
		TTF_Font *fa = l.small ? fs : fm;

		for (i = 0; i < 2; i++) {
			SDL_Rect s = c->pressed == i ? pressed(l.ans[i], c->press) : l.ans[i];

			ui_round_rect(r, &s, DS_RADIUS_ROW, ui_col(thm->raised, 255));
		}
		f = (SDL_Rect){ (int)lroundf(a.x + (b.x - a.x) * fill), (int)lroundf(a.y + (b.y - a.y) * fill),
		                (int)lroundf(a.w + (b.w - a.w) * fill), a.h };
		if (c->pressed >= 0 && c->pressed == c->sel) f = pressed(f, c->press);
		ui_round_rect(r, &f, DS_RADIUS_ROW, ui_col(thm->accent, 255));

		for (i = 0; i < 2; i++) {
			const char *word = c->answer[i] ? c->answer[i] : "";
			float w = 1.0f - fabsf(fill - (float)i);
			SDL_Color rest = c->mark[i] & DS_CONFIRM_WARN ? ui_col(thm->warning, 255) : UI_TEXT_SOFT;
			SDL_Color ink = mix(rest, on, w);
			bool glyph = (c->mark[i] & DS_CONFIRM_B) != 0;
			int ww = ui_text_width(fa, word);
			int gs = DS_CONFIRM_GLYPH;
			int total = ww + (glyph ? gs + DS_CONFIRM_GLYPH_GAP : 0);
			SDL_Rect s = l.ans[i];
			int mid = s.y + s.h / 2, x = s.x + (s.w - total) / 2;

			if (c->pressed == i) gs = (int)lroundf(gs * (1.0f - (1.0f - DS_CONFIRM_PRESS / 100.0f) * c->press));
			if (glyph) {
				ui_button(r, "B", x + DS_CONFIRM_GLYPH / 2, mid, gs,
				          w > 0.5f ? UI_BUTTON_ON_ACCENT : UI_BUTTON_PLAIN);
				x += DS_CONFIRM_GLYPH + DS_CONFIRM_GLYPH_GAP;
			}
			ui_text(r, fa, word, x, ui_cap_y(fa, mid), -1, ink);
		}
	}
	ds_panel_close(&l.panel, pose, posed, pass);
}
