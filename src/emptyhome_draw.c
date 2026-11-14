/* SPDX-License-Identifier: GPL-3.0-only */
/* The empty home drawn: see emptyhome.h. Its words are emptyhome.c's, which
 * needs no screen, so tools/emptyhome-check.c holds them on their own. */
#include "emptyhome.h"
#include "design.h"
#include "home.h"       /* page_font, page_hint: the home's own type and keys */
#include "theme.h"
#include "ui.h"

#include <stdio.h>

/* One way's card at x: its mark and its button on the first line, its name,
 * the three steps, and the reason it cannot start now. A way that cannot is
 * all **text-dim**, its button plain, the reason in **text-soft**: it is the
 * line to read. */
static void card(SDL_Renderer *r, const eh_state *s, eh_way w, int x)
{
	const ds_theme *t = theme();
	TTF_Font *fn = page_font(PAGE_GAME_TITLE), *fs = page_font(PAGE_META);
	SDL_Rect q = { x, EH_CARD_Y, EH_CARD_W, EH_CARD_H };
	char addr[64], fit[96];
	eh_text tx;
	bool ok = s->ok[w];
	SDL_Color name = ok ? UI_TEXT : UI_TEXT_DIM, step = ok ? UI_TEXT_SOFT : UI_TEXT_DIM;
	int i, room = EH_CARD_W - 2 * EH_PAD_X;

	eh_words(s, w, &tx, addr, sizeof addr);
	ui_round_rect(r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
	ui_glyph_draw(r, w == EH_USB ? UI_GLYPH_USB : UI_GLYPH_WIFI,
	              x + EH_PAD_X + 28, EH_CARD_Y + 54, 56,
	              ok ? ui_col(t->accent_ink, 255) : UI_TEXT_DIM);
	ui_button(r, tx.key, x + EH_CARD_W - EH_PAD_X - 24, EH_CARD_Y + 54, 48,
	          ok ? UI_BUTTON_ON_ACCENT : UI_BUTTON_PLAIN);
	ui_fit_text(fn, tx.name, fit, sizeof fit, room);
	ui_text(r, fn, fit, x + EH_PAD_X, ui_cap_y(fn, EH_CARD_Y + 117), -1, name);
	for (i = 0; i < 3; i++) {
		char num[4];
		int cy = EH_CARD_Y + 174 + 46 * i;

		snprintf(num, sizeof num, "%d", i + 1);
		ui_text(r, fs, num, x + EH_PAD_X, ui_cap_y(fs, cy), -1, UI_TEXT_DIM);
		ui_fit_text(fs, tx.step[i], fit, sizeof fit, room - 36);
		ui_text(r, fs, fit, x + EH_PAD_X + 36, ui_cap_y(fs, cy), -1, step);
	}
	/* At the card's foot: what is missing, or else the other way on the
	 * same cable, quieter, since nothing waits on it */
	if (tx.why || tx.also) {
		ui_fit_text(fs, tx.why ? tx.why : tx.also, fit, sizeof fit, room);
		ui_text(r, fs, fit, x + EH_PAD_X, ui_cap_y(fs, EH_CARD_Y + EH_CARD_H - 42), -1,
		        tx.why ? UI_TEXT_SOFT : UI_TEXT_DIM);
	}
}

void eh_draw(SDL_Renderer *r, const eh_state *s, eh_zyper_fn zyper)
{
	TTF_Font *ft = ui_font(UI_F_TITLE), *fs = page_font(PAGE_META);

	/* Zyper at the left of the heading, and the heading with its smaller
	 * line under it, where every page has them (src/ds/page.h's frame,
	 * DS_HEAD_SUB's places; its smaller line in the home's own face) */
	if (zyper) zyper(r, DS_PAGE_ZYPER_X, DS_PAGE_ZYPER_Y, DS_PAGE_ZYPER_H);
	ui_text(r, ft, T(S_EMPTYHOME_NO_GAMES_YET), DS_PAGE_HEAD_X, ui_cap_y(ft, DS_PAGE_TITLE_Y), -1, UI_TEXT);
	ui_text(r, fs, T(S_EMPTYHOME_ADD_THEM), DS_PAGE_HEAD_X, ui_cap_y(fs, DS_PAGE_SUB_Y), -1,
	        UI_TEXT_SOFT);
	card(r, s, EH_USB, EH_CARD_X0);
	card(r, s, EH_WIFI, EH_CARD_X1);
	ui_text(r, fs, T(S_EMPTYHOME_BIOS), 512, ui_cap_y(fs, EH_BIOS_CY), 0,
	        UI_TEXT_DIM);
	page_hint(r, "MENU", T(S_EMPTYHOME_SETTINGS), 64, DS_PAGE_FOOT_Y, false, false);
	if (s->music) page_hint(r, "SELECT", "MP4", 784, DS_PAGE_FOOT_Y, false, false);
}

/* The cheer's times, on DESIGN.md's tokens: in on **move**, the tutorial's
 * reward's hold, out on **fade-out** */
unsigned eh_cheer_ms(void)
{
	return DS_MS_TOUR_REWARD + DS_MS_FADE_OUT;
}

/* A stroke from (x0, y0) to (x1, y1), `w` wide, as two triangles */
static void stroke(SDL_Renderer *r, float x0, float y0, float x1, float y1, float w, SDL_Color c)
{
	float dx = x1 - x0, dy = y1 - y0, l = SDL_sqrtf(dx * dx + dy * dy);
	float nx = l > 0 ? -dy / l * w / 2 : 0, ny = l > 0 ? dx / l * w / 2 : 0;
	SDL_Vertex v[4] = {
		{ { x0 + nx, y0 + ny }, c, { 0, 0 } }, { { x1 + nx, y1 + ny }, c, { 0, 0 } },
		{ { x1 - nx, y1 - ny }, c, { 0, 0 } }, { { x0 - nx, y0 - ny }, c, { 0, 0 } },
	};
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

	SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

/* A filled disc about (cx, cy) as a fan */
static void disc(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c)
{
	SDL_Vertex v[49];
	int idx[48 * 3], i;

	v[0] = (SDL_Vertex){ { cx, cy }, c, { 0, 0 } };
	for (i = 0; i < 48; i++) {
		float a = (float)i / 48.0f * 6.2831853f;

		v[i + 1] = (SDL_Vertex){ { cx + SDL_cosf(a) * rad, cy + SDL_sinf(a) * rad }, c, { 0, 0 } };
		idx[i * 3] = 0;
		idx[i * 3 + 1] = i + 1;
		idx[i * 3 + 2] = i == 47 ? 1 : i + 2;
	}
	SDL_RenderGeometry(r, NULL, v, 49, idx, 48 * 3);
}

void eh_cheer_draw(SDL_Renderer *r, int games, unsigned t, eh_pose_fn zyper, void *ctx)
{
	const ds_theme *th = theme();
	TTF_Font *fb = ui_font(UI_F_META);
	float k = t < DS_MS_TOUR_REWARD ? ds_out((float)t / DS_MS_MOVE)
	        : 1.0f - ds_in((float)(t - DS_MS_TOUR_REWARD) / DS_MS_FADE_OUT);
	float pop = ds_out((float)t / DS_MS_ZYPER_POP), rad = 38.0f * pop;
	SDL_Color ground = ui_col(th->ground, (Uint8)(165 * k + 0.5f));
	SDL_Color fill = ui_col(th->accent, (Uint8)(255 * k + 0.5f));
	SDL_Color ink = ui_col(th->on_accent, (Uint8)(255 * k + 0.5f));
	char say[32];
	int w, bh = 68;
	SDL_Rect b;

	if (!(k > 0.0f)) return;
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The home faded back, so the cheer is what is read */
	SDL_SetRenderDrawColor(r, ground.r, ground.g, ground.b, ground.a);
	SDL_RenderFillRect(r, NULL);
	if (zyper) zyper(r, 510.0f, 326.0f, 232.0f, k, ctx);
	/* The check, the tutorial's: lime with its tick in **on-accent** */
	if (rad > 0.5f) {
		float s = rad / 38.0f;

		disc(r, 650.0f, 234.0f, rad, fill);
		stroke(r, 650 - 15 * s, 235 + 1 * s, 650 - 5 * s, 245 + 1 * s, 7 * s, ink);
		stroke(r, 650 - 7 * s, 247 * 1.0f - 2 * (1 - s), 650 + 16 * s, 224 + 10 * (1 - s), 7 * s, ink);
	}
	/* His bubble: **text** with the words in the **ground**, as the
	 * tutorial's cheer */
	snprintf(say, sizeof say, TN(S_EMPTYHOME_CHEER, games), games);
	w = ui_text_width(fb, say) + 56;
	b = (SDL_Rect){ 620, 364 - bh / 2, w, bh };
	ui_round_rect(r, &b, bh / 2, ui_col(th->text, (Uint8)(255 * k + 0.5f)));
	ui_text(r, fb, say, b.x + 28, ui_cap_y(fb, 364), -1, ui_col(th->ground, (Uint8)(255 * k + 0.5f)));
}
