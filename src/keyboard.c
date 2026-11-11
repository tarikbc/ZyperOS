/* SPDX-License-Identifier: GPL-3.0-only */
/* The on-screen keyboard.
 *
 * Typing a WPA2 key on a d-pad is the worst text entry a handheld ever asks
 * for: long, mixed case, and full of symbols. Every decision here is about
 * cutting the number of button presses per character, because the naive
 * version - a grid with a shift KEY you navigate to - costs four to six moves
 * for every capital letter.
 *
 *   - The face buttons are verbs, not letters. B deletes, X shifts, Y goes to
 *     symbols. A capital costs one press instead of a round trip across the
 *     grid and back.
 *   - The digits row is permanent, on every layer. Passwords and usernames
 *     are full of digits, and putting them behind the symbols layer means a
 *     layer switch every few characters.
 *   - L1 and R1 move the cursor. Without them, fixing character 3 of a
 *     30-character key means 27 deletions and retyping the rest.
 *   - The grid wraps at every edge, which roughly halves worst-case travel.
 *
 * B is backspace rather than back, which is the one place this disagrees with
 * the rest of the UI. The reasoning is frequency: backspace is pressed dozens
 * of times per password and cancel once, so the cheap button belongs to the
 * common act. MENU cancels, and the line under the keys says so.
 *
 * Under the grid, the action row (DESIGN.md 11.5): shift, symbols, space,
 * delete and done as keys too, each carrying its button's glyph, so a player
 * who has not learned the verbs can reach them with the d-pad and learns the
 * button from the key. None of them costs anyone who knows the buttons a
 * press. Where the d-pad goes among them is src/kbnav.c's.
 */
#include <stdio.h>
#include <string.h>

#include "config.h"   /* CFG_NO_COLOR: the keyboard is no system's */
#include "kbnav.h"
#include "keyboard.h"
#include "platform.h"   /* PLAT_TARGET_FORMAT */
#include "ui.h"
#include "uisound.h"

/* Four rows of ten, three layers. Digits are row 0 of every layer so they
 * never cost a switch. The letters are QWERTY, the layout a hand already
 * looks for, so a letter is found rather than searched for. The tail of the
 * lower two rows carries the four symbols that turn up most in passwords and
 * mail addresses, so the common ones do not need the symbol layer either. */
static const char *LAYER[3] = {
	"1234567890"
	"qwertyuiop"
	"asdfghjkl-"
	"zxcvbnm_.@",

	"1234567890"
	"QWERTYUIOP"
	"ASDFGHJKL-"
	"ZXCVBNM_.@",

	"1234567890"
	"!@#$%^&*()"
	"=+[]{}\\|/~"
	";:'\",.<>?`",
};

/* One scratch texture for this prompt, reused by every moving key. A failed
 * allocation falls back to whole keys at rest, without retrying each frame. */
typedef struct {
	SDL_Texture *texture;
	bool tried;
} key_cache;

typedef struct {
	char buf[512];
	int  len, cur;      /* cursor sits BEFORE buf[cur] */
	int  layer;         /* 0 lower, 1 upper, 2 symbols */
	int  row, col;      /* row KB_ROW_ACTIONS is the action row, col its key */
	kb_pop pop[KB_ROWS + 1][KB_COLS]; /* one return per key, never a queue */
	key_cache keys;
	key_cache panel;
	/* Every key at rest, unselected, drawn once (keys_kept), and the layer
	 * and theme it holds them in when grid_ok */
	key_cache grid;
	bool grid_ok;
	int grid_layer, grid_theme, grid_y;
	SDL_Texture *field;
	char field_text[512];
	SDL_Color field_color;
	int field_w, field_h;
} kb_state;

/* Neither a retained panel nor the shared UI text cache owns typed text.
 * The caller owns the result; all private copies and textures end here. */
static void kb_clear(kb_state *k)
{
	volatile unsigned char *p = (volatile unsigned char *)k;
	size_t n = sizeof *k;

	if (k->keys.texture) SDL_DestroyTexture(k->keys.texture);
	if (k->panel.texture) SDL_DestroyTexture(k->panel.texture);
	if (k->grid.texture) SDL_DestroyTexture(k->grid.texture);
	if (k->field) SDL_DestroyTexture(k->field);
	while (n--) *p++ = 0;
}

/* Room for one more character, in the caller's buffer and this one. Its own
 * function so a press can be heard before it types. */
static bool kb_room(const kb_state *k, int cap)
{
	return k->len + 1 < cap && k->len + 1 < (int)sizeof k->buf;
}

static void ins(kb_state *k, char c, int cap)
{
	if (!kb_room(k, cap)) return;
	memmove(k->buf + k->cur + 1, k->buf + k->cur, (size_t)(k->len - k->cur) + 1);
	k->buf[k->cur] = c;
	k->cur++;
	k->len++;
}

static void del_left(kb_state *k)
{
	if (k->cur <= 0) return;
	memmove(k->buf + k->cur - 1, k->buf + k->cur, (size_t)(k->len - k->cur) + 1);
	k->cur--;
	k->len--;
}

/* What an action does, from its key or from its button: one path for both,
 * so a key can never do something its button does not. True for done. */
static bool act(kb_state *k, kb_action a, int cap, unsigned now)
{
	/* Only actions that can act pop. The caller still plays the edge sound
	 * for a full field or a delete at the start of the text. */
	if (a == KB_ACT_SPACE && !kb_room(k, cap)) return false;
	if (a == KB_ACT_DELETE && k->cur <= 0) return false;
	if (a < 0 || a >= KB_ACTIONS) return false;
	kb_pop_press(&k->pop[KB_ROW_ACTIONS][a], now);

	switch (a) {
	case KB_ACT_SHIFT:
	case KB_ACT_SYMBOLS: k->layer = kb_layer_after(k->layer, a); return false;
	case KB_ACT_SPACE:   ins(k, ' ', cap); return false;
	case KB_ACT_DELETE:  del_left(k); return false;
	case KB_ACT_DONE:    return true;
	default:             return false;
	}
}

/* ---------- drawing ------------------------------------------------------- */

/* The keys' own sizes are src/kbnav.h's, and the panel around them is a
 * menu's (main.c's menu_draw): its padding at the sides, its heading in the
 * same band over the same rule, and its top where a tall menu's is. The field
 * is as wide as the keys, so their edges line up under it. The panel grows
 * with the fonts the language has, as a menu's does (kb_geo). */
#define FIELD_H       64
#define FIELD_PAD     16     /* the text from the field's ends */
#define ACT_GLYPH     40     /* an action key's button glyph */
/* A glyph to its label. The lab drew 10; Lexend's "unshift" beside X fills
 * all but 5 units of its key at that, so the grid's 8 gives it two more. */
#define ACT_GAP  DS_GRID
#define LINE_GLYPH    44     /* the line's glyphs */
#define LINE_SEP      28     /* between the line's two pairs */
/* Space is drawn as the space mark, a bracket open at the top: the lab's 56
 * by 16 in 4-unit strokes, at 80% of the label's color. */
#define SPACE_W       56
#define SPACE_H       16
#define SPACE_STROKE   4
#define SPACE_ALPHA  204
/* A tall menu's top: a grid step under the status corner's row, the same 96
 * as main.c's MENU_TOP */
#define KB_TOP        96

/* Where everything is, on the screen. pad is a menu's (main.c's menu_pad: three
 * quarters of a **menu** row), head the band its heading is centered in, down
 * to the rule. Under the rule half a pad to the field; the field and the keys
 * a key's gap apart, the grid and the action row as they always were; half a
 * pad, the quiet line, and half a pad to the bottom, as a menu's footer has. */
typedef struct {
	SDL_Rect panel;
	int pad, head;          /* from the panel's top: the rule's y */
	int field_y, grid_y, actions_y, line_cy;   /* on the screen */
} kb_geo;

static kb_geo kb_geometry(void)
{
	kb_geo g;
	int line_top, h;

	g.pad = ui_font_line(UI_F_MENU) * 3 / 2 * 3 / 4;
	g.head = ui_font_line(UI_F_LABEL) + g.pad;
	h = g.head + g.pad / 2 + FIELD_H + KB_KEY_GAP
	  + KB_ROWS * (KB_KEY_H + KB_KEY_GAP) + KB_KEY_H
	  + g.pad / 2 + LINE_GLYPH + g.pad / 2;
	g.panel.w = KB_GRID_W + 2 * g.pad;
	g.panel.h = h;
	g.panel.x = (ZYPEROS_SCREEN_W - g.panel.w) / 2;
	g.panel.y = (ZYPEROS_SCREEN_H - h) / 2;
	if (g.panel.y < KB_TOP) g.panel.y = KB_TOP;
	g.field_y = g.panel.y + g.head + g.pad / 2;
	g.grid_y = g.field_y + FIELD_H + KB_KEY_GAP;
	g.actions_y = g.grid_y + KB_ROWS * (KB_KEY_H + KB_KEY_GAP);
	line_top = g.actions_y + KB_KEY_H + g.pad / 2;
	g.line_cy = line_top + LINE_GLYPH / 2;
	return g;
}

/* Every line here is centered by its capitals, ui_cap_y, the rule ui_button
 * centers its letter by, so a glyph and the label beside it share a middle.
 * Half the descent, the rule before it, put Lexend's letters eight units low
 * in a key and its descenders out through the bottom of the field. */

/* The least room between an action key's edge and its glyph or label: what
 * "unshift" has on the host in Lexend, 3 to 4 at each side of its 178. */
#define ACT_EDGE 3

/* A button's glyph and a label beside it, the pair centered on (cx, cy): an
 * action key's, or one of the line's. `room` is the width the pair may take,
 * an action key's less ACT_EDGE at each side, or 0 for the line, which has
 * the panel's width. A pair wider than its room gives up half its gap first
 * and then the end of its label, never the key's edge: a face that sets the
 * label wider than the host's Lexend does would otherwise run it over the
 * key, and **meta** is already the least size text has (DESIGN.md 13). */
static void glyph_label(SDL_Renderer *r, const char *btn, int gsize, int style,
                        const char *label, SDL_Color col, int cx, int cy,
                        int room)
{
	TTF_Font *fm = ui_font(UI_F_META);
	int gw = ui_button_width(btn, gsize);
	int gap = ACT_GAP;
	char fit[32];
	int tw, x;

	snprintf(fit, sizeof fit, "%s", label);
	tw = ui_text_width(fm, fit);
	if (room > 0 && gw + gap + tw > room) {
		gap = ACT_GAP - (gw + gap + tw - room);
		if (gap < ACT_GAP / 2) gap = ACT_GAP / 2;
		if (gw + gap + tw > room) {
			ui_fit_text(fm, label, fit, sizeof fit, room - gw - gap);
			tw = ui_text_width(fm, fit);
		}
	}
	x = cx - (gw + gap + tw) / 2;
	ui_button(r, btn, x + gw / 2, cy, gsize, style);
	ui_text(r, fm, fit, x + gw + gap, ui_cap_y(fm, cy), -1, col);
}

/* The space mark, centered on (cx, cy). Three rects that do not overlap, so
 * the translucent color is laid down once everywhere. */
static void space_mark(SDL_Renderer *r, int cx, int cy, SDL_Color col)
{
	int x = cx - SPACE_W / 2, y = cy - SPACE_H / 2;
	int side = SPACE_H - SPACE_STROKE;

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
	SDL_RenderFillRect(r, &(SDL_Rect){ x, y, SPACE_STROKE, side });
	SDL_RenderFillRect(r, &(SDL_Rect){ x + SPACE_W - SPACE_STROKE, y,
	                                   SPACE_STROKE, side });
	SDL_RenderFillRect(r, &(SDL_Rect){ x, y + side, SPACE_W, SPACE_STROKE });
}

/* Rasterize at unit scale, then shrink the complete key. Scaling primitive
 * drawing directly loses the one-unit rows of ui_round_rect on SDL's
 * software renderer, removing both the key's caps and the glyph's ring.
 * Padding also keeps the text texture's descent outside the key if needed. */
#define KEY_PASS_PAD DS_TYPE_MENU

typedef struct {
	SDL_Texture *target;
	SDL_Rect viewport, clip, source;
	SDL_FRect destination;
	int logical_w, logical_h;
	float x, y;
	SDL_Color color;
	SDL_BlendMode blend;
	bool clipped;
	bool changed;
} key_pass;

static key_pass key_save(SDL_Renderer *r)
{
	key_pass saved = {0};

	saved.target = SDL_GetRenderTarget(r);
	SDL_RenderGetLogicalSize(r, &saved.logical_w, &saved.logical_h);
	SDL_RenderGetScale(r, &saved.x, &saved.y);
	SDL_RenderGetViewport(r, &saved.viewport);
	saved.clipped = SDL_RenderIsClipEnabled(r);
	SDL_RenderGetClipRect(r, &saved.clip);
	SDL_GetRenderDrawColor(r, &saved.color.r, &saved.color.g,
	                       &saved.color.b, &saved.color.a);
	SDL_GetRenderDrawBlendMode(r, &saved.blend);
	return saved;
}

/* A key may be drawn inside another target. Restore that exact target and
 * its coordinate state, including on setup failure; never assume screen. */
static bool key_restore(SDL_Renderer *r, const key_pass *saved)
{
	int error = SDL_SetRenderTarget(r, saved->target);

	error |= SDL_RenderSetLogicalSize(r, saved->logical_w, saved->logical_h);
	error |= SDL_RenderSetScale(r, saved->x, saved->y);
	error |= SDL_RenderSetViewport(r, &saved->viewport);
	error |= SDL_RenderSetClipRect(r, saved->clipped ? &saved->clip : NULL);
	error |= SDL_SetRenderDrawColor(r, saved->color.r, saved->color.g,
	                                saved->color.b, saved->color.a);
	error |= SDL_SetRenderDrawBlendMode(r, saved->blend);
	return error == 0;
}

static key_pass key_begin(SDL_Renderer *r, key_cache *cache, SDL_Rect *q,
                          float scale, SDL_Color fill)
{
	key_pass saved = {0};

	if (scale >= 1.0f) return saved;
	if (!cache->tried) {
		int a, x, w, widest = KB_KEY_W;

		cache->tried = true;
		for (a = 0; a < KB_ACTIONS; a++) {
			kb_action_span(a, &x, &w);
			if (w > widest) widest = w;
		}
		cache->texture = SDL_CreateTexture(r, PLAT_TARGET_FORMAT,
		                                    SDL_TEXTUREACCESS_TARGET,
		                                    widest + 2 * KEY_PASS_PAD,
		                                    KB_KEY_H + 2 * KEY_PASS_PAD);
		if (cache->texture &&
		    (SDL_SetTextureBlendMode(cache->texture, SDL_BLENDMODE_BLEND) != 0 ||
		     SDL_SetTextureScaleMode(cache->texture, SDL_ScaleModeLinear) != 0)) {
			SDL_DestroyTexture(cache->texture);
			cache->texture = NULL;
		}
	}
	if (!cache->texture) return saved;

	saved = key_save(r);

	/* Clear to the key's color at zero alpha, so linear filtering at its
	 * transparent outline does not introduce a black fringe. */
	if (SDL_SetRenderTarget(r, cache->texture) != 0 ||
	    SDL_RenderSetLogicalSize(r, 0, 0) != 0 ||
	    SDL_RenderSetScale(r, 1.0f, 1.0f) != 0 ||
	    SDL_RenderSetViewport(r, NULL) != 0 ||
	    SDL_RenderSetClipRect(r, NULL) != 0 ||
	    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 0) != 0 ||
	    SDL_RenderClear(r) != 0) {
		key_restore(r, &saved);
		return saved;
	}
	saved.source = (SDL_Rect){0, 0, q->w + 2 * KEY_PASS_PAD,
	                                q->h + 2 * KEY_PASS_PAD};
	saved.destination = (SDL_FRect){
		q->x + q->w * (1.0f - scale) / 2.0f - KEY_PASS_PAD * scale,
		q->y + q->h * (1.0f - scale) / 2.0f - KEY_PASS_PAD * scale,
		saved.source.w * scale, saved.source.h * scale
	};
	saved.changed = true;
	q->x = q->y = KEY_PASS_PAD;
	return saved;
}

static void key_end(SDL_Renderer *r, const key_cache *cache, key_pass saved)
{
	if (saved.changed && key_restore(r, &saved))
		SDL_RenderCopyF(r, cache->texture, &saved.source, &saved.destination);
}

/* A key of the grid, at `scale` of its size (a press's pop), as it always
 * was: **raised**, or the **accent** fill with its character in **on-accent**
 * when selected. */
static void grid_key(SDL_Renderer *r, kb_state *k, const ds_theme *t, TTF_Font *fk,
                     int gx, int gy, int i, int c, bool sel, float scale)
{
	char lbl[2] = { LAYER[k->layer][i * KB_COLS + c], '\0' };
	SDL_Rect q = { gx + c * (KB_KEY_W + KB_KEY_GAP),
	               gy + i * (KB_KEY_H + KB_KEY_GAP), KB_KEY_W, KB_KEY_H };
	SDL_Color fill = ui_col(sel ? t->accent : t->raised, 255);
	key_pass saved = key_begin(r, &k->keys, &q, scale, fill);

	ui_round_rect(r, &q, DS_RADIUS_KEY, fill);
	ui_text(r, fk, lbl, q.x + q.w / 2,
	        ui_cap_y(fk, q.y + q.h / 2), 0,
	        ui_col(sel ? t->on_accent : t->text_soft, 255));
	key_end(r, &k->keys, saved);
}

/* An action key: its button's glyph beside its label in **meta**, space its
 * mark. On the selected key the glyph is outlined in **on-accent**, so it is
 * still a button on the fill. */
static void action_key(SDL_Renderer *r, kb_state *k, const ds_theme *t,
                       int gx, int top, int i, bool sel, float scale)
{
	int x, w;
	SDL_Rect q;
	SDL_Color ink, fill;
	key_pass saved;
	const char *btn = kb_action_button((kb_action)i);

	kb_action_span(i, &x, &w);
	q = (SDL_Rect){ gx + x, top, w, KB_KEY_H };
	ink = ui_col(sel ? t->on_accent : t->text, 255);
	fill = ui_col(sel ? t->accent : t->raised, 255);
	saved = key_begin(r, &k->keys, &q, scale, fill);

	ui_round_rect(r, &q, DS_RADIUS_KEY, fill);
	if (btn)
		glyph_label(r, btn, ACT_GLYPH,
		            sel ? UI_BUTTON_ON_ACCENT : UI_BUTTON_PLAIN,
		            kb_action_label((kb_action)i, k->layer), ink,
		            q.x + q.w / 2, q.y + q.h / 2, q.w - ACT_EDGE * 2);
	else
		space_mark(r, q.x + q.w / 2, q.y + q.h / 2,
		           ui_fade(ink, SPACE_ALPHA / 255.0f));
	key_end(r, &k->keys, saved);
}

/* The grid and the action row's rect on the screen */
static SDL_Rect keys_rect(int gx, int gy, const kb_geo *g)
{
	return (SDL_Rect){ gx, gy, KB_GRID_W, g->actions_y + KB_KEY_H - gy };
}

/* Every key at rest and unselected, drawn once into a screen-sized texture
 * for this layer and theme and copied from there every frame, so a frame
 * draws one or two keys and not 45. Drawn every frame, the keys cost the
 * Pixel 2's GPU most of a frame: the search keyboard ran at 18 fps with the
 * cursor moving, and at 60 with the keys left out (2026-11-05).
 *
 * The same pixels: a key is opaque where it is drawn and the texture clear
 * everywhere else, its label blended onto its fill there as it would be on
 * the screen, so the copy puts each key's pixels down as they were and
 * leaves the panel between them. The texture is copied 1:1 and nearest.
 * False when there is no texture, and every key is drawn as before. */
static bool keys_kept(SDL_Renderer *r, kb_state *k, const ds_theme *t, TTF_Font *fk,
                      int gx, int gy, const kb_geo *g)
{
	key_pass saved;
	bool drawn = false;
	int i, c;

	if (!k->grid.tried) {
		k->grid.tried = true;
		k->grid.texture = SDL_CreateTexture(r, PLAT_TARGET_FORMAT,
		                                     SDL_TEXTUREACCESS_TARGET,
		                                     ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		if (k->grid.texture &&
		    (SDL_SetTextureBlendMode(k->grid.texture, SDL_BLENDMODE_BLEND) != 0 ||
		     SDL_SetTextureScaleMode(k->grid.texture, SDL_ScaleModeNearest) != 0)) {
			SDL_DestroyTexture(k->grid.texture);
			k->grid.texture = NULL;
		}
	}
	if (!k->grid.texture) return false;
	if (k->grid_ok && k->grid_layer == k->layer && k->grid_theme == theme_index() &&
	    k->grid_y == gy) return true;

	saved = key_save(r);
	if (SDL_SetRenderTarget(r, k->grid.texture) == 0 &&
	    SDL_RenderSetLogicalSize(r, 0, 0) == 0 &&
	    SDL_RenderSetScale(r, 1.0f, 1.0f) == 0 &&
	    SDL_RenderSetViewport(r, NULL) == 0 && SDL_RenderSetClipRect(r, NULL) == 0 &&
	    SDL_SetRenderDrawColor(r, 0, 0, 0, 0) == 0 && SDL_RenderClear(r) == 0) {
		for (i = 0; i < KB_ROWS; i++)
			for (c = 0; c < KB_COLS; c++)
				grid_key(r, k, t, fk, gx, gy, i, c, false, 1.0f);
		for (i = 0; i < KB_ACTIONS; i++)
			action_key(r, k, t, gx, g->actions_y, i, false, 1.0f);
		drawn = true;
	}
	k->grid_ok = false;
	if (!key_restore(r, &saved) || !drawn) return false;
	k->grid_ok = true;
	k->grid_layer = k->layer;
	k->grid_theme = theme_index();
	k->grid_y = gy;
	return true;
}

/* Every color is a role of the theme that is on, asked for here, on every
 * frame: the loop that draws this ticks the theme (the caller's backdrop),
 * and Auto can turn Beard into Salt & Pepper under an open keyboard. */
static void kb_draw(SDL_Renderer *r, kb_state *k, const char *title, unsigned now,
                     bool lift)
{
	const ds_theme *t = theme();
	const kb_geo g = kb_geometry();
	SDL_Rect panel = g.panel;
	int gx = panel.x + g.pad;
	int gy = g.grid_y;
	TTF_Font *fk = ui_font(UI_F_MENU);
	TTF_Font *fl = ui_font(UI_F_LABEL);
	TTF_Font *fm = ui_font(UI_F_META);
	int i, c;
	char shown[sizeof k->buf];

	/* DESIGN.md 11.3's panel, as every menu over the shelf is: **surface** in
	 * an **edge** frame, lifted by a shadow on Beard and a neutral light in
	 * Salt & Pepper, since the keyboard is ZyperOS's and no system's. */
	if (lift) ui_lift(r, &panel, CFG_NO_COLOR, UI_LIFT_PANEL);
	ui_panel(r, &panel, DS_RADIUS_PANEL);

	/* The title, centered by its capitals in the band between the panel's
	 * inner edge and the rule, over a rule in **edge**: a menu's heading */
	ui_text(r, fl, title, panel.x + panel.w / 2,
	        ui_cap_y(fl, panel.y + (UI_PANEL_BORDER + g.head) / 2), 0,
	        ui_col(t->text_soft, 255));
	SDL_SetRenderDrawColor(r, t->edge.r, t->edge.g, t->edge.b, 255);
	SDL_RenderFillRect(r, &(SDL_Rect){ panel.x + g.pad, panel.y + g.head,
	                                   KB_GRID_W, DS_RULE });

	/* The field: **ground** inside an **edge** line. Always in clear - see
	 * the note in keyboard.h. */
	{
		SDL_Rect f = { gx, g.field_y, KB_GRID_W, FIELD_H };
		SDL_Rect in = { f.x + DS_RULE, f.y + DS_RULE,
		                f.w - DS_RULE * 2, f.h - DS_RULE * 2 };
		/* Inside the line across, and the whole field down: a descender
		 * of g or y may brush the line, and cutting it off there is the flat
		 * cut that was reported from the device on 2026-08-30. */
		SDL_Rect clip = { in.x, f.y, in.w, f.h };
		int ty = ui_cap_y(fk, f.y + f.h / 2);
		int base = ty + (fk ? TTF_FontAscent(fk) : 0);
		int room = f.w - FIELD_PAD * 2 - DS_CARET;
		int caret, pre, shift = 0;

		ui_round_rect(r, &f, DS_RADIUS_KEY, ui_col(t->edge, 255));
		ui_round_rect(r, &in, DS_RADIUS_KEY - DS_RULE, ui_col(t->ground, 255));

		/* Width of everything left of the cursor, which is both where the
		 * caret goes and what the scroll is computed from. A WPA key runs to
		 * 63 characters and will not fit; without this the text simply walks
		 * out of the panel and the caret goes with it. */
		memcpy(shown, k->buf, (size_t)k->cur);
		shown[k->cur] = '\0';
		pre = ui_text_width(fk, shown);
		if (pre > room) shift = pre - room;

		memcpy(shown, k->buf, (size_t)k->len);
		shown[k->len] = '\0';
		SDL_RenderSetClipRect(r, &clip);
		/* The same rasterization as ui_text, with prompt-scoped storage:
		 * a password must not remain in its process-wide text cache. */
		{
			SDL_Color ink = ui_col(t->text, 255);
			if (strcmp(k->field_text, shown) ||
			    memcmp(&k->field_color, &ink, sizeof ink) || (!k->field && *shown)) {
				SDL_Surface *s = fk && *shown ? TTF_RenderUTF8_Blended(fk, shown, ink) : NULL;
				if (k->field) SDL_DestroyTexture(k->field);
				k->field = s ? SDL_CreateTextureFromSurface(r, s) : NULL;
				k->field_w = s ? s->w : 0;
				k->field_h = s ? s->h : 0;
				if (s) SDL_FreeSurface(s);
				snprintf(k->field_text, sizeof k->field_text, "%s", shown);
				k->field_color = ink;
			}
			if (k->field) {
				SDL_Rect dst = {f.x + FIELD_PAD - shift, ty, k->field_w, k->field_h};
				SDL_RenderCopy(r, k->field, NULL, &dst);
			}
		}
		SDL_RenderSetClipRect(r, NULL);

		/* Caret at the measured pixel width rather than a fixed advance,
		 * which a proportional face would not honor, in **accent-ink**: lime
		 * as a mark, since the fill's lime is 1.3:1 on Beard's ground. It
		 * runs from just over the capitals to two thirds of the way down the
		 * descenders, the lab's 44 for a 49-unit line. */
		caret = f.x + FIELD_PAD + pre - shift;
		{
			int cap = ui_cap_height(fk);
			int below = fk ? -TTF_FontDescent(fk) * 2 / 3 : 0;

			SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(r, t->accent_ink.r, t->accent_ink.g,
			                       t->accent_ink.b, 255);
			SDL_RenderFillRect(r, &(SDL_Rect){ caret, base - cap - 1,
			                                   DS_CARET, cap + 1 + below });
		}
	}

	/* The keys: the kept ones at rest, then over them the selected key and
	 * any key popping from a press. A popping key is drawn smaller than the
	 * kept one, so its rect is given back to the panel's **surface** first,
	 * which is what is under every key. Without a texture, every key. */
	{
		SDL_Rect kr = keys_rect(gx, gy, &g);
		bool kept = keys_kept(r, k, t, fk, gx, gy, &g);

		if (kept) SDL_RenderCopy(r, k->grid.texture, &kr, &kr);
		for (i = 0; i <= KB_ROWS; i++) {
			int cols = i < KB_ROWS ? KB_COLS : KB_ACTIONS;

			for (c = 0; c < cols; c++) {
				float scale = kb_pop_scale(&k->pop[i][c], now);
				bool sel = k->row == i && k->col == c;

				if (kept && !sel && scale >= 1.0f) continue;
				if (kept && scale < 1.0f) {
					SDL_Rect q;

					if (i < KB_ROWS) {
						q = (SDL_Rect){ gx + c * (KB_KEY_W + KB_KEY_GAP),
						                gy + i * (KB_KEY_H + KB_KEY_GAP), KB_KEY_W, KB_KEY_H };
					} else {
						int x, w;

						kb_action_span(c, &x, &w);
						q = (SDL_Rect){ gx + x, g.actions_y, w, KB_KEY_H };
					}
					SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
					SDL_SetRenderDrawColor(r, t->surface.r, t->surface.g, t->surface.b, 255);
					SDL_RenderFillRect(r, &q);
				}
				if (i < KB_ROWS) grid_key(r, k, t, fk, gx, gy, i, c, sel, scale);
				else action_key(r, k, t, gx, g.actions_y, c, sel, scale);
			}
		}
	}

	/* One quiet line, **meta** **text-dim**: the two verbs that have no key.
	 * A has no caption - it is OK everywhere. */
	{
		static const char *btn[2] = { "L1/R1", "MENU" };
		const char *lbl[2] = { T(S_KEYBOARD_CURSOR), T(S_KEYBOARD_CANCEL) };
		int wd[2], total = LINE_SEP, x;
		int cy = g.line_cy;

		for (i = 0; i < 2; i++) {
			wd[i] = ui_button_width(btn[i], LINE_GLYPH) + ACT_GAP
			        + ui_text_width(fm, lbl[i]);
			total += wd[i];
		}
		x = panel.x + (panel.w - total) / 2;
		for (i = 0; i < 2; i++) {
			glyph_label(r, btn[i], LINE_GLYPH, UI_BUTTON_PLAIN, lbl[i],
			            ui_col(t->text_dim, 255), x + wd[i] / 2, cy, 0);
			x += wd[i] + LINE_SEP;
		}
	}
}

/* A whole keyboard pose, using the owner's shared motion clock. Its private
 * pass includes all text and key pops, but its lift stays on the destination.
 * No screen-size password texture survives kb_prompt's return. */
static void kb_draw_pose(SDL_Renderer *r, kb_state *k, const char *title,
                          unsigned now, kb_pose pose)
{
	SDL_Rect q = kb_geometry().panel;
	SDL_FRect dst = {q.x + q.w * (1 - pose.scale) / 2,
	                 q.y + q.h * (1 - pose.scale) / 2,
	                 q.w * pose.scale, q.h * pose.scale};
	key_pass saved;
	bool ready = false;
	const ds_rgb edge = theme()->edge;

	if (!(pose.alpha > 0)) return;
	if (pose.alpha == 1 && pose.scale == 1) {
		kb_draw(r, k, title, now, true);
		return;
	}
	saved = key_save(r);
	if (!k->panel.tried) {
		k->panel.tried = true;
		k->panel.texture = SDL_CreateTexture(r, PLAT_TARGET_FORMAT,
		                                      SDL_TEXTUREACCESS_TARGET,
		                                      ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		if (k->panel.texture &&
		    (SDL_SetTextureBlendMode(k->panel.texture, SDL_BLENDMODE_BLEND) != 0 ||
		     SDL_SetTextureScaleMode(k->panel.texture, SDL_ScaleModeLinear) != 0)) {
			SDL_DestroyTexture(k->panel.texture);
			k->panel.texture = NULL;
		}
	}
	if (k->panel.texture && SDL_SetRenderTarget(r, k->panel.texture) == 0 &&
	    SDL_RenderSetLogicalSize(r, 0, 0) == 0 &&
	    SDL_RenderSetScale(r, 1, 1) == 0 &&
	    SDL_RenderSetViewport(r, NULL) == 0 && SDL_RenderSetClipRect(r, NULL) == 0 &&
	    SDL_SetRenderDrawColor(r, edge.r, edge.g, edge.b, 0) == 0 &&
	    SDL_RenderClear(r) == 0) {
		kb_draw(r, k, title, now, false);
		ready = true;
	}
	if (!key_restore(r, &saved)) return;
	if (ready) {
		SDL_Rect lift = {(int)(dst.x + .5f), (int)(dst.y + .5f),
		                 (int)(dst.w + .5f), (int)(dst.h + .5f)};
		ui_lift_k(r, &lift, CFG_NO_COLOR, UI_LIFT_PANEL, pose.alpha);
		SDL_SetTextureAlphaMod(k->panel.texture, (Uint8)(pose.alpha * 255 + .5f));
		SDL_RenderCopyF(r, k->panel.texture, &q, &dst);
	} else {
		kb_draw(r, k, title, now, true);
	}
	key_restore(r, &saved);
}

static void preview(SDL_Renderer *r, const char *title, const char *text,
                    int layer, int row, int col, bool pressed, unsigned now,
                    const kb_pose *pose)
{
	kb_state k;

	memset(&k, 0, sizeof k);
	snprintf(k.buf, sizeof k.buf, "%s", text ? text : "");
	k.len = (int)strlen(k.buf);
	k.cur = k.len;
	k.layer = layer < 0 || layer > 2 ? 0 : layer;
	k.row = row;
	k.col = col;
	kb_clamp(&k.row, &k.col);
	if (pressed) kb_pop_press(&k.pop[k.row][k.col], 0);
	if (pose) kb_draw_pose(r, &k, title, now, *pose);
	else kb_draw(r, &k, title, now, true);
	kb_clear(&k);
}

void kb_preview(SDL_Renderer *r, const char *title, const char *text,
                int layer, int row, int col)
{
	preview(r, title, text, layer, row, col, false, 0, NULL);
}

void kb_preview_press(SDL_Renderer *r, const char *title, const char *text,
                      int layer, int row, int col, unsigned elapsed_ms)
{
	preview(r, title, text, layer, row, col, true, elapsed_ms, NULL);
}

void kb_preview_panel(SDL_Renderer *r, const char *title, const char *text,
                      int layer, int row, int col, kb_pose pose)
{
	preview(r, title, text, layer, row, col, false, 0, &pose);
}

/* ---------- the loop ------------------------------------------------------ */

/* The sounds (DESIGN.md 9): A types and plays accept, B deletes and plays
 * back, and a step on the grid or along the text plays move, including a wrap. Edge means it
 * cannot go on: the cursor at an end of the text, a
 * delete with nothing left, a key with no room for it. Each is asked for on
 * the first line of its press, before the press changes anything, so it is
 * worked out from what the press is about to do. */
static uis_steps kb_steps;

static void kb_heard(const in_state *in, in_button b, bool moved)
{
	uisound_cue(uisound_step(&kb_steps, moved, !in->pressed[b]));
}

/* A grid step, including a wrap, is normal movement. */
static void kb_step_sound(const in_state *in, in_button b, const kb_state *k, kb_dir d)
{
	int row = k->row, col = k->col;

	kb_move(&row, &col, d);
	kb_heard(in, b, row != k->row || col != k->col);
}

/* A delete, by B or by A on delete: back while there is something before the
 * cursor, softer on a repeat, and the end once when there is not. */
static void kb_delete_sound(const in_state *in, in_button b, const kb_state *k)
{
	if (k->cur <= 0) { kb_heard(in, b, false); return; }
	kb_steps.edged = false;
	uisound_cue((uis_cue){ DS_SOUND_BACK, in->pressed[b] ? 1.0f : DS_SOUND_HELD_LEVEL });
}

/* A, on a key or an action key: what it is about to do, heard. */
static void kb_press_sound(const in_state *in, const kb_state *k, int cap)
{
	if (k->row == KB_ROW_ACTIONS && k->col == KB_ACT_DELETE)
		kb_delete_sound(in, IN_ACCEPT, k);
	else if ((k->row != KB_ROW_ACTIONS || k->col == KB_ACT_SPACE) && !kb_room(k, cap))
		kb_heard(in, IN_ACCEPT, false);
	else
		uisound_play(DS_SOUND_ACCEPT);
}

kb_result kb_prompt(SDL_Renderer *r, in_state *in, const char *title,
                    char *buf, int cap, kb_panel panel, kb_idle idle,
                    void *ctx)
{
	kb_state k;
	kb_result result = KB_CANCEL;
	bool done = false;
	bool closing = false;
	bool a_armed = false;   /* A was pressed on delete and is still held there */

	memset(&k, 0, sizeof k);
	snprintf(k.buf, sizeof k.buf, "%s", buf ? buf : "");
	k.len = (int)strlen(k.buf);
	k.cur = k.len;
	k.row = 1;              /* open on the letters, not the digits */
	if (panel) panel(ctx, KB_PANEL_OPEN, false);

	for (;;) {
		unsigned now;
		bool pressed = false;
		int b;
		kb_pose pose = {1, 1, false};

		plat_input_poll(in);
		now = plat_now_ms();
		if (in->quit_requested) {
			result = KB_CANCEL;
			if (panel) panel(ctx, KB_PANEL_CUT, false);
			goto out;
		}
		/* Typing a password is the longest anyone stares at this device
		 * without pressing anything, and it was the one screen where that
		 * could go on forever. */
		if (in->pressed[IN_POWER] || (idle && idle(ctx))) {
			result = KB_POWER;
			if (panel) panel(ctx, KB_PANEL_CUT, false);
			goto out;
		}
		for (b = 0; b < IN_COUNT; b++)
			if (in->pressed[b]) { pressed = true; break; }
		if (in_repeat(in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		if (closing) goto draw;

		/* Wrapping in both axes, and through the action row: the grid is a
		 * torus, which is what keeps the worst case from being a trip
		 * across ten columns. src/kbnav.c says where each press lands. */
		if (in_repeat(in, IN_LEFT)) {
			kb_step_sound(in, IN_LEFT, &k, KB_LEFT);
			kb_move(&k.row, &k.col, KB_LEFT);
		}
		if (in_repeat(in, IN_RIGHT)) {
			kb_step_sound(in, IN_RIGHT, &k, KB_RIGHT);
			kb_move(&k.row, &k.col, KB_RIGHT);
		}
		if (in_repeat(in, IN_UP)) {
			kb_step_sound(in, IN_UP, &k, KB_UP);
			kb_move(&k.row, &k.col, KB_UP);
		}
		if (in_repeat(in, IN_DOWN)) {
			kb_step_sound(in, IN_DOWN, &k, KB_DOWN);
			kb_move(&k.row, &k.col, KB_DOWN);
		}

		/* Cursor, on the shoulders. Repeating, because walking a long key one
		 * press at a time is the thing this exists to avoid. */
		if (in_repeat(in, IN_L1)) {
			kb_heard(in, IN_L1, k.cur > 0);
			if (k.cur > 0) k.cur--;
		}
		if (in_repeat(in, IN_R1)) {
			kb_heard(in, IN_R1, k.cur < k.len);
			if (k.cur < k.len) k.cur++;
		}

		/* A types the key it is on, and on an action key does what that
		 * key's button does. B repeats, so A on delete repeats as well:
		 * holding either clears a field. But only a hold that began with a
		 * press on delete: A held since another key has an old down_since,
		 * and in_repeat alone would start deleting the moment the d-pad
		 * reached delete, at the repeat rate, with nothing pressed there. */
		a_armed = kb_hold_armed(a_armed,
		                        k.row == KB_ROW_ACTIONS && k.col == KB_ACT_DELETE,
		                        in->pressed[IN_ACCEPT], in->down[IN_ACCEPT]);
		if (k.row == KB_ROW_ACTIONS) {
			bool a = k.col == KB_ACT_DELETE
			         ? a_armed && in_repeat(in, IN_ACCEPT)
			         : in->pressed[IN_ACCEPT];
			if (a) {
				kb_press_sound(in, &k, cap);
				done |= act(&k, (kb_action)k.col, cap, now);
			}
		} else if (in->pressed[IN_ACCEPT]) {
			kb_press_sound(in, &k, cap);
			if (kb_room(&k, cap)) kb_pop_press(&k.pop[k.row][k.col], now);
			ins(&k, LAYER[k.layer][k.row * KB_COLS + k.col], cap);
		}
		/* Repeating, so holding it clears a field instead of asking for
		 * thirty presses. */
		if (in_repeat(in, IN_BACK)) {
			kb_delete_sound(in, IN_BACK, &k);
			act(&k, KB_ACT_DELETE, cap, now);
		}
		if (in->pressed[IN_X]) {
			uisound_play(DS_SOUND_ACCEPT);
			act(&k, KB_ACT_SHIFT, cap, now);
		}
		if (in->pressed[IN_Y]) {
			uisound_play(DS_SOUND_ACCEPT);
			act(&k, KB_ACT_SYMBOLS, cap, now);
		}
		if (in->pressed[IN_START]) {
			uisound_play(DS_SOUND_ACCEPT);
			done |= act(&k, KB_ACT_DONE, cap, now);
		}

		if (done) {
			result = KB_ACCEPT;
			closing = true;
		} else if (in->pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			closing = true;
		}
		if (closing) {
			if (!panel) goto out;
			panel(ctx, KB_PANEL_LEAVE, false);
			pressed = false; /* This press starts the leave; the next cuts it. */
		}
draw:
		if (panel) pose = panel(ctx, KB_PANEL_FRAME, pressed);
		if (closing && !pose.moving) goto out;
		kb_draw_pose(r, &k, title, plat_now_ms(), pose);
		plat_draw_osd(r);
		plat_present();
		ui_frame_rest(8);
	}
out:
	if (result == KB_ACCEPT) snprintf(buf, (size_t)cap, "%s", k.buf);
	kb_clear(&k);
	if (panel) panel(ctx, KB_PANEL_FINISH, false);
	return result;
}
