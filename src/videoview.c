/* SPDX-License-Identifier: GPL-3.0-only */
/* The Video view. See src/videoview.h and DESIGN.md 11.57. */
#include "videoview.h"

#include "design.h"
#include "home.h"
#include "i18n.h"
#include "platform.h"
#include "keyhints_draw.h"
#include "theme.h"
#include "ui.h"
#include "ds/layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The band: its covers' boxes, the focused one's and its neighbours', their
 * feet on one line, a cover a step apart; the keys on every screen's foot */
#define VV_STEP       DS_VIDEO_STEP
#define VV_COVER_W    DS_VIDEO_COVER_W
#define VV_COVER_H    DS_VIDEO_COVER_H
#define VV_FOCUS_W    DS_VIDEO_FOCUS_W
#define VV_FOCUS_H    DS_VIDEO_FOCUS_H
#define VV_COVER_FOOT 692
#define VV_REACH        5     /* covers drawn either side of the focus */
/* The band's ground: surface at 92%, so the snap shows through a little */
#define VV_BAND_ALPHA 235
/* The rail in the strip: the page's margins, its middle down the strip's */
#define VV_RAIL_Y     (VV_STRIP_TOP + (ZYPEROS_SCREEN_H - VV_STRIP_TOP) / 2)
/* The name's card at the top left, under the header row, and the room its
 * name may take before it slides */
#define VV_CARD_X     DS_INSET
#define VV_CARD_Y     DS_VIDEO_CARD_Y
#define VV_CARD_PAD_X DS_VIDEO_CARD_PAD_X
#define VV_CARD_PAD_Y DS_VIDEO_CARD_PAD_Y
#define VV_CARD_TEXT  640
/* The ground's fade under the status corner while the snap plays */
#define VV_TOP_FADE   150

void vv_reset(vv_motion *m, uint32_t now)
{
	*m = (vv_motion){ .set = true, .from = 0, .to = 0, .t = now, .ms = DS_MS_QUICK, .touched = now };
}

float vv_band(const vv_motion *m, uint32_t now)
{
	float p;

	if (!m->set || m->ms == 0 || now - m->t >= m->ms) return m->set ? m->to : 0;
	p = ds_out((float)(now - m->t) / (float)m->ms);
	return m->from + (m->to - m->from) * p;
}

/* The band to `to` from wherever it is, over `ms`: a new press retargets */
static void slide(vv_motion *m, float to, uint32_t ms, uint32_t now)
{
	float at = vv_band(m, now);

	if (m->to == to && at == to) return;
	m->from = at;
	m->to = to;
	m->t = now;
	m->ms = ms;
}

void vv_touch(vv_motion *m, uint32_t now)
{
	if (!m->set) vv_reset(m, now);
	m->touched = now;
	slide(m, 0, DS_MS_QUICK, now);
}

void vv_rest(vv_motion *m, uint32_t now)
{
	if (!m->set) vv_reset(m, now);
	slide(m, 1, DS_MS_MOVE, now);
}

/* A picture whole inside `box`, at its own shape, its middle on the box's */
static SDL_FRect fit(float w, float h, SDL_FRect box)
{
	float f = fminf(box.w / w, box.h / h);

	return (SDL_FRect){ box.x + (box.w - w * f) / 2, box.y + (box.h - h * f) / 2, w * f, h * f };
}

/* The ground's color fading down from the top, under the status corner */
static void top_fade(SDL_Renderer *r, float k)
{
	const ds_rgb g = theme()->ground;
	const SDL_Color top = { g.r, g.g, g.b, (Uint8)lroundf(204 * k) };
	const SDL_Color bottom = { g.r, g.g, g.b, 0 };
	const SDL_Vertex v[4] = {
		{ { 0, 0 }, top, { 0, 0 } },
		{ { ZYPEROS_SCREEN_W, 0 }, top, { 0, 0 } },
		{ { ZYPEROS_SCREEN_W, VV_TOP_FADE }, bottom, { 0, 0 } },
		{ { 0, VV_TOP_FADE }, bottom, { 0, 0 } },
	};
	const int idx[6] = { 0, 1, 2, 0, 2, 3 };

	if (k <= 0) return;
	SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

/* The name's card: the shelf and the place in it, the name, its facts;
 * `gone` 0 in its place up to 1 off the top of the screen (a shot's) */
static uint32_t name_card(SDL_Renderer *r, const vv_info *in, uint32_t phase, float gone)
{
	TTF_Font *meta = page_font(PAGE_META), *title = page_font(PAGE_GAME_TITLE);
	TTF_Font *chrome = page_font(PAGE_CHROME);
	char head[512], facts[256] = "";
	const char *name = in->count > 0 && in->title && in->title[0] ? in->title : T(S_SHELF_NO_GAMES);
	uint32_t wait = UINT32_MAX;
	int dot = 14, w, tw, x, y, h, dy;
	SDL_Rect q;

	snprintf(head, sizeof head, "%s \xc2\xb7 %d / %d", in->heading ? in->heading : "",
	         in->count > 0 ? in->cursor + 1 : 0, in->count);
	if (in->folder > 0) {
		snprintf(facts, sizeof facts, TN(S_SHELF_GAMES, in->folder), in->folder);
	} else {
		size_t at = 0;

		if (in->origin && *in->origin) at += (size_t)snprintf(facts + at, sizeof facts - at, "%s", in->origin);
		if ((in->install || in->howto) && at < sizeof facts)
			at += (size_t)snprintf(facts + at, sizeof facts - at, "%s%s", at ? "  \xc2\xb7  " : "",
			                       T(S_PORTMASTER_NOT_INSTALLED));
		if (in->played && *in->played && at < sizeof facts) {
			char p[96];

			snprintf(p, sizeof p, T(S_GAMES_PLAYED), in->played);
			at += (size_t)snprintf(facts + at, sizeof facts - at, "%s%s", at ? "  \xc2\xb7  " : "", p);
		}
		if (in->has_achievements && at < sizeof facts) {
			char p[96];

			snprintf(p, sizeof p, T(S_GAMES_ACHIEVEMENTS), in->earned, in->total);
			at += (size_t)snprintf(facts + at, sizeof facts - at, "%s%s", at ? "  \xc2\xb7  " : "", p);
		}
		/* and how many of its cheats are on (DESIGN.md 11.66) */
		if (in->cheats && *in->cheats && at < sizeof facts)
			snprintf(facts + at, sizeof facts - at, "%s%s", at ? "  \xc2\xb7  " : "", in->cheats);
	}
	tw = ui_text_width(title, name);
	if (tw > VV_CARD_TEXT) tw = VV_CARD_TEXT;
	w = ui_text_width(meta, head) + dot + 12;
	if (tw > w) w = tw;
	if (facts[0] && ui_text_width(chrome, facts) > w)
		w = ui_text_width(chrome, facts) < VV_CARD_TEXT ? ui_text_width(chrome, facts) : VV_CARD_TEXT;
	h = VV_CARD_PAD_Y * 2 + DS_LINE_META + 12 + ui_font_line(UI_F_TITLE) + (facts[0] ? DS_LINE_META : 0);
	if (gone >= 1) return wait;
	dy = -(int)lroundf((float)(VV_CARD_Y + h) * gone);
	q = (SDL_Rect){ VV_CARD_X, VV_CARD_Y + dy, w + 2 * VV_CARD_PAD_X, h };
	ui_round_rect(r, &q, DS_RADIUS_CARD, ui_col(theme()->edge, VV_BAND_ALPHA));
	q = (SDL_Rect){ q.x + UI_PANEL_BORDER, q.y + UI_PANEL_BORDER, q.w - 2 * UI_PANEL_BORDER, q.h - 2 * UI_PANEL_BORDER };
	ui_round_rect(r, &q, DS_RADIUS_CARD - UI_PANEL_BORDER, ui_col(theme()->surface, VV_BAND_ALPHA));
	x = VV_CARD_X + VV_CARD_PAD_X;
	y = VV_CARD_Y + dy + VV_CARD_PAD_Y + DS_LINE_META / 2;
	/* The shelf's color as a dot before its name, as the lab drew it: its
	 * mark, which meets its contrast on the card (ui_sys_mark) */
	{
		unsigned mark = ui_sys_mark(in->accent);
		SDL_Color mc = { (Uint8)(mark >> 16), (Uint8)(mark >> 8), (Uint8)mark, 255 };

		ui_round_rect(r, &(SDL_Rect){ x, y - dot / 2, dot, dot }, dot / 2, mc);
	}
	ui_text(r, meta, head, x + dot + 12, ui_cap_y(meta, y), -1, UI_TEXT_SOFT);
	y += DS_LINE_META / 2 + 12 + ui_font_line(UI_F_TITLE) / 2;
	if (ui_text_width(title, name) <= VV_CARD_TEXT) {
		ui_text(r, title, name, x, ui_cap_y(title, y), -1, UI_TEXT);
	} else {
		ui_text_marquee(r, title, name, x, ui_cap_y(title, y), VV_CARD_TEXT, phase, UI_TEXT);
		wait = ui_pingpong_wait(ui_text_width(title, name) - VV_CARD_TEXT, phase);
	}
	if (facts[0]) {
		y += ui_font_line(UI_F_TITLE) / 2 + DS_LINE_META / 2;
		if (ui_text_width(chrome, facts) <= VV_CARD_TEXT) {
			ui_text(r, chrome, facts, x, ui_cap_y(chrome, y), -1, UI_TEXT_DIM);
		} else {
			uint32_t next;

			ui_text_marquee(r, chrome, facts, x, ui_cap_y(chrome, y), VV_CARD_TEXT, phase, UI_TEXT_DIM);
			next = ui_pingpong_wait(ui_text_width(chrome, facts) - VV_CARD_TEXT, phase);
			if (next < wait) wait = next;
		}
	}
	return wait;
}

/* The ring round the focused cover: accent-ink, a gap of the band's ground
 * inside it, its corners nesting round the cover's */
static void ring(SDL_Renderer *r, SDL_FRect f, float fade)
{
	int out = DS_FOCUS_RING + DS_FOCUS_GAP;
	SDL_Rect q = { (int)lroundf(f.x), (int)lroundf(f.y), (int)lroundf(f.w), (int)lroundf(f.h) };
	SDL_Rect o = { q.x - out, q.y - out, q.w + 2 * out, q.h + 2 * out };
	SDL_Rect g = { q.x - DS_FOCUS_GAP, q.y - DS_FOCUS_GAP, q.w + 2 * DS_FOCUS_GAP, q.h + 2 * DS_FOCUS_GAP };

	ui_round_rect(r, &o, DS_RADIUS_CELL + out, ui_col(theme()->accent_ink, (Uint8)lroundf(255 * fade)));
	ui_round_rect(r, &g, DS_RADIUS_CELL + DS_FOCUS_GAP, ui_col(theme()->surface, (Uint8)lroundf(255 * fade)));
}

/* The band, its top at `top`: the covers in a row, the focused one ringed,
 * and the keys; the strip's rail in it at `strip` (0 to 1), none for a
 * shot's, whose band goes off the screen (`in->clear`) */
static void band(SDL_Renderer *r, const games_row *row, const vv_info *in, cf_tex_fn texture,
                 void *ctx, uint32_t now, int top, float strip)
{
	const int dy = top - VV_BAND_TOP;
	/* The covers and the keys go as the band goes down, the rail comes:
	 * the strip holds the rail alone */
	const float fade = 1 - strip;
	games_slot slots[GAMES_WINDOW];
	int n, i;

	ui_round_rect(r, &(SDL_Rect){ 0, top, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H - top + DS_RADIUS_PANEL },
	              0, ui_col(theme()->surface, VV_BAND_ALPHA));
	n = in->count > 0 && fade > 0 ? games_slots(row, now, slots) : 0;
	for (i = 0; i < n; i++) {
		float off = slots[i].offset, a = fabsf(off), near = a < 1 ? 1 - a : 0;
		float bw = VV_COVER_W + (VV_FOCUS_W - VV_COVER_W) * near;
		float bh = VV_COVER_H + (VV_FOCUS_H - VV_COVER_H) * near;
		float cx = ZYPEROS_SCREEN_W / 2.0f + off * VV_STEP;
		SDL_FRect box = { cx - bw / 2, VV_COVER_FOOT + dy - bh, bw, bh }, dst;
		int w = 0, h = 0;
		float cb = 1;
		SDL_Texture *t;
		Uint8 was;

		if (a > VV_REACH + 0.5f || box.y > ZYPEROS_SCREEN_H) continue;
		if (!(t = texture(ctx, slots[i].item, &w, &h, &cb)) || w <= 0 || h <= 0) continue;
		dst = fit((float)w, (float)h, box);
		/* the feet of every cover on one line */
		dst.y = box.y + box.h - dst.h;
		SDL_GetTextureAlphaMod(t, &was);
		SDL_SetTextureAlphaMod(t, (Uint8)lroundf(was * slots[i].alpha * fade * (a < 0.5f ? 1.0f : 0.8f)));
		if (a < 0.5f) ring(r, dst, fade);
		SDL_RenderCopyF(r, t, NULL, &dst);
		SDL_SetTextureAlphaMod(t, was);
	}
	/* The keys, as every shelf's foot (DESIGN.md 11.4), with A's among them */
	if (fade > 0 && in->foot) {
		shelf_frame sf = games_frame();

		keyhints_draw(r, in->foot, in->nfoot, sf.keys.x, sf.keys.x + sf.keys.w, sf.keys.y + sf.keys.h / 2 + dy,
		              KEYHINTS_SPREAD, true, (Uint8)lroundf(255 * fade));
	} else if (fade > 0) {
		key_hint foot[5];
		int nf = 0;
		shelf_frame sf = games_frame();

		foot[nf++] = (key_hint){ "B", T(in->inside ? S_GAMES_BACK : S_GAMES_SYSTEMS) };
		if (in->count > 0)
			foot[nf++] = (key_hint){ "A", T(in->folder > 0 || in->opens ? S_GAMES_OPEN
			                                : in->howto ? S_PICO8_HOW_TO_ADD
			                                : in->install ? S_PORTMASTER_INSTALL
			                                : in->resumable ? S_GAMES_RESUME : S_GAMES_PLAY) };
		if (in->count > 0 && in->start_word) foot[nf++] = (key_hint){ "START", in->start_word };
		if (in->count > 0 && in->resumable && !in->install && !in->howto && !in->opens && !in->folder)
			foot[nf++] = (key_hint){ "Y", T(S_GAMES_FRESH) };
		if (in->count > 0 && !in->folder) foot[nf++] = (key_hint){ "X", T(S_GAMES_DETAILS) };
		keyhints_draw(r, foot, nf, sf.keys.x, sf.keys.x + sf.keys.w, sf.keys.y + sf.keys.h / 2 + dy,
		              KEYHINTS_SPREAD, true, (Uint8)lroundf(255 * fade));
	}
	/* The strip's rail: where the game is in the shelf (DESIGN.md 11.2) */
	if (strip > 0 && in->count > 1 && !in->clear) {
		const int x0 = DS_MARGIN, tw = ZYPEROS_SCREEN_W - 2 * DS_MARGIN;
		int mw = tw / in->count, mx;
		Uint8 al = (Uint8)lroundf(255 * strip);

		if (mw < DS_RAIL_MIN) mw = DS_RAIL_MIN;
		mx = x0 + (int)lroundf((float)(tw - mw) * in->cursor / (in->count - 1));
		ui_round_rect(r, &(SDL_Rect){ x0, top + (VV_RAIL_Y - VV_STRIP_TOP) - DS_BAR / 2, tw, DS_BAR },
		              DS_BAR / 2, ui_col(theme()->track, al));
		ui_round_rect(r, &(SDL_Rect){ mx, top + (VV_RAIL_Y - VV_STRIP_TOP) - DS_BAR / 2, mw, DS_BAR },
		              DS_BAR / 2, ui_col(theme()->accent_ink, al));
	}
}

uint32_t vv_draw(vv_motion *m, const games_row *row, SDL_Renderer *r, const vv_info *in,
                 cf_tex_fn texture, void *ctx, uint32_t now, uint32_t phase)
{
	SDL_BlendMode blend;
	Uint8 red, green, blue, alpha;
	uint32_t wait = UINT32_MAX, next;
	float k = in->snap && in->snap_w > 0 && in->snap_h > 0 ? fminf(fmaxf(in->snap_k, 0), 1) : 0;
	float down;
	int top;

	SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
	SDL_GetRenderDrawBlendMode(r, &blend);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	if (!m->set) vv_reset(m, now);
	if (in->playing && !m->was_playing && now > m->touched) m->touched = now;
	m->was_playing = in->playing;
	/* The rest: down to the strip once the snap has played hold-band
	 * after the last press; no snap, no strip */
	if (!in->playing && m->to > 0) slide(m, 0, DS_MS_QUICK, now);
	if (in->playing && m->to == 0 && now - m->touched >= DS_MS_HOLD_BAND)
		slide(m, 1, DS_MS_MOVE, now);
	down = in->band >= 0 ? fminf(in->band, 1) : vv_band(m, now);
	if (in->band < 0) {
		if (now - m->t < m->ms) wait = 0;
		else if (in->playing && m->to == 0) wait = m->touched + DS_MS_HOLD_BAND - now;
	}
	/* The covers' glide, a frame each until it lands, as the cover row's
	 * (games_draw). Without it the shelf drew the glide only while the
	 * band slid or a button was down, and its last frame stayed up until
	 * the snap's next picture or the heartbeat: a cover left at the side of
	 * the band, half faded, for up to a second after the press (2026-11-19,
	 * on the 1.1.0 test build: "a black box stays on the side") */
	if (row_moving(&row->motion, now)) wait = 0;
	top = (int)lroundf(VV_BAND_TOP + ((in->clear ? ZYPEROS_SCREEN_H : VV_STRIP_TOP) - VV_BAND_TOP) * down);

	/* The focused game's card, whole above the band, until its snap is in */
	if (k < 1 && in->count > 0) {
		int w = 0, h = 0;
		float cb = 1;
		SDL_Texture *t = texture(ctx, in->cursor, &w, &h, &cb);

		if (t && w > 0 && h > 0) {
			SDL_FRect box = { DS_INSET, VV_CARD_Y, ZYPEROS_SCREEN_W - 2 * DS_INSET,
			                  (float)(top - DS_INSET - VV_CARD_Y) };
			SDL_FRect dst = fit((float)w, (float)h, box);
			Uint8 was;

			SDL_GetTextureAlphaMod(t, &was);
			SDL_SetTextureAlphaMod(t, (Uint8)lroundf(was * (1 - k)));
			SDL_RenderCopyF(r, t, NULL, &dst);
			SDL_SetTextureAlphaMod(t, was);
		}
	}
	/* The snap, whole on the screen at its own shape, on ink */
	if (k > 0) {
		SDL_FRect dst = fit((float)in->snap_w, (float)in->snap_h,
		                    (SDL_FRect){ 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H });
		const ds_rgb ink = theme()->ink;

		SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, (Uint8)lroundf(255 * k));
		SDL_RenderFillRect(r, NULL);
		SDL_SetTextureAlphaMod(in->snap, (Uint8)lroundf(255 * k));
		SDL_RenderCopyF(r, in->snap, in->snap_src.w > 0 ? &in->snap_src : NULL, &dst);
		SDL_SetTextureAlphaMod(in->snap, 255);
		top_fade(r, in->clear ? k * (1 - down) : k);
	}
	next = name_card(r, in, phase, in->clear ? down : 0);
	if (next < wait) wait = next;
	if (top < ZYPEROS_SCREEN_H) band(r, row, in, texture, ctx, now, top, down);
	SDL_SetRenderDrawColor(r, red, green, blue, alpha);
	SDL_SetRenderDrawBlendMode(r, blend);
	return wait;
}
