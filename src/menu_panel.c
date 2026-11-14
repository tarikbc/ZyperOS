/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu_panel.h"

#include "app.h"
#include "keyhints.h"
#include "keyhints_draw.h"
#include "look.h"
#include "panel.h"
#include "tween.h"
#include "ui.h"
#include "zyperdraw.h"
#include "zypermark.h"

#include <stdio.h>
#include <string.h>

/* ---------- menus --------------------------------------------------------- */

/* Both menus in ZyperOS are the same shape - a short list on a slab, over a
 * paused game frame or over the shelf - so they are drawn by one function and
 * cannot drift apart. The slab is sized to its own widest row with the same
 * padding on every side, rather than to a number picked once and left behind
 * by the next label someone adds. */
/* menu_row and the row kinds: src/menu.h. */

/* The unit every menu measurement is in. Row height, padding and the gap
 * between the two columns are all cut from it, so the whole panel scales with
 * the type rather than with a set of numbers that have to be retuned together. */
/* Two colors, `t` of the way from a to b. */
static SDL_Color col_mix(SDL_Color a, SDL_Color b, float t)
{
	if (t <= 0.0f) return a;
	if (t >= 1.0f) return b;
	return (SDL_Color){ (Uint8)(a.r + (b.r - a.r) * t),
	                    (Uint8)(a.g + (b.g - a.g) * t),
	                    (Uint8)(a.b + (b.b - a.b) * t),
	                    (Uint8)(a.a + (b.a - a.a) * t) };
}

/* DESIGN.md 5's one number, 1.5 menu lines. The line is the menu's em, 49
 * (ui_font_line): Lexend's line skip, 62, would make it 93 where the mock
 * kit's row is 77 and the panel two rows shorter than the rows it was drawn
 * with. */
int menu_row_h(void) { return ui_font_line(UI_F_MENU) * 3 / 2; }
/* The air between the panel edge and its content, on each side. menu_draw
 * draws it and menu_std_width subtracts it, and a panel width means nothing
 * unless both use the same number. */
int menu_pad(void)   { return menu_row_h() * 3 / 4; }

/* X's glyph at a row's right (MENU_KEY_X): the keyboard's action keys' 40,
 * DESIGN.md 11.9, which sits in a menu row as a hint's sits in its legend */
#define MENU_KEY_SIZE 40
/* Between On/Off and Y's glyph after it (MENU_KEY_Y_ON, MENU_KEY_Y_OFF) */
#define MENU_KEY_GAP 16

/* What a row's value takes at the right of the rows, for the measures:
 * its text, a tick (MENU_TICK) or X's glyph (MENU_KEY_X) */
int menu_value_w(TTF_Font *fm, const char *v)
{
	if (v == MENU_TICK) return ui_cap_height(fm) * 6 / 5;
	if (v == MENU_KEY_X) return ui_button_width("X", MENU_KEY_SIZE);
	if (v == MENU_KEY_Y_ON || v == MENU_KEY_Y_OFF)
		return ui_text_width(fm, T(v == MENU_KEY_Y_ON ? S_MENU_ON_TITLE : S_MENU_OFF_TITLE)) + MENU_KEY_GAP
		       + ui_button_width("Y", MENU_KEY_SIZE);
	return v ? ui_text_width(fm, v) : 0;
}

/* How many row_h rows menu_draw can show beside `fixed` other full rows,
 * `rules` rules and `notes` notes, before it starts windowing.
 *
 * It windows from the TOP of the list, which is right for a plain menu and
 * wrong for a screen whose last rows are a footer: Play Time overflowed by
 * 42px and menu_draw answered by scrolling the footer off the bottom, so the
 * totals and the key legend were never on screen together. A screen that
 * builds furniture around a list has to know how much list is left, and this
 * is that arithmetic in one place rather than a constant tuned to one set of
 * fonts - a face with other line heights makes a hardcoded row count wrong
 * again. */
int menu_list_fit(int fixed, int rules, int notes)
{
	int pad = menu_pad(), row_h = menu_row_h();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int avail = MENU_AVAIL_H
	          - (head_h + pad / 2)          /* heading down to the first row */
	          - pad;                        /* and the panel's bottom air */

	avail -= fixed * row_h;
	avail -= rules * (pad / 2 + 2);
	avail -= notes * (ui_font_line(UI_F_MENU) + pad / 3);
	return avail < row_h ? 1 : avail / row_h;
}

/* How many BODY lines fit in a panel that is nothing but body - a synopsis
 * card. The same arithmetic menu_list_fit does, asked the other way round: that
 * one reserves notes and answers in rows, and a page with no rows on it at all
 * would get the wrong answer from it.
 *
 * Here rather than in the caller because the two must agree about the heading,
 * the padding and the note height, and a screen that measured itself would
 * drift from the panel that draws it the first time either is retuned. */
int menu_note_h(void)
{
	return ui_font_line(UI_F_MENU) + menu_pad() / 3;
}

int menu_notes_fit(void)
{
	int pad = menu_pad();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int note_h = menu_note_h();
	int avail = MENU_AVAIL_H - (head_h + pad / 2) - pad;

	return note_h > 0 && avail > note_h ? avail / note_h : 1;
}

/* Zyper beside the heading, in the one menu that has him: set by
 * menu_run_body around that menu's own menu_draw and NULL everywhere else,
 * so a screen it opens - Wi-Fi, About, a sign-in - draws none, and neither
 * does a message drawn from its key handler. And by snap_draw, around that
 * menu's panel drawn again as it leaves (a copy of his rig, kept with it), so
 * he goes with the panel. `g_menu_zyper_at` is the moment his pose is for. */
const zr_rig *g_menu_zyper;
unsigned      g_menu_zyper_at;

static const char *mpt_name(int i) { return sys_menu_tab_name((pm_tab)i); }
menu_tabs        g_tabs = { .count = PT_TABS, .name = mpt_name };
const menu_tabs *g_menu_tabs;
/* The row each tab was left on, by identity (PM_ROWS: its first) */
pm_row           g_tab_row[PT_TABS] = { PM_ROWS, PM_ROWS, PM_ROWS,
                                        PM_ROWS, PM_ROWS, PM_ROWS };
/* Set by a key handler to move the runner's cursor, which it owns: MENU's
 * tabs, whose rows change under it. -1, none. */
int g_menu_sel_to = -1;

void menu_cursor_to(int sel)
{
	g_menu_sel_to = sel;
}
/* menu_style's row marks and header picture, set around the draw of the
 * menu that has them as g_menu_zyper is */
void (*g_row_mark)(app *a, const menu_row *row, int right, int cy, float on_fill);
void (*g_menu_header)(app *a, const SDL_Rect *r);
int    g_menu_header_h;
/* menu_style's picture in a column at the rows' left and its width, set
 * around the draw of the menu that has one as g_menu_header is; and whether
 * the panel being drawn is a copy (snap_draw), which the picture is told */
void (*g_menu_side)(app *a, const menu_side *at);
int    g_menu_side_w;
bool   g_snap_drawing;

#define MENU_ZYPER_H 64.0f

/* DESIGN.md 10: 64 units tall at the right end of the heading band, his
 * resting extent `pad` in from the panel's right edge like the rows' text,
 * and standing as high as he can and still hop inside the panel: his top
 * ZR_REACH under the frame's inner edge, so a hop at the top of a breath
 * brings his ears to the frame and no further. That is the Zyper lab's
 * place, his middle 94 on a panel at 33, a few units under the middle of
 * the heading's capitals and over all of them; centered on them, a hop put
 * an ear over the frame. His foot is 16 units above the rule, so he never
 * reaches a row. Drawn after the heading; his shadow first, in Beard, which
 * stays on the panel while he hops. The high brow follows his tilt, as
 * tools/zyperdef.py draws it. */
static void menu_zyper_draw(app *a, const SDL_Rect *panel, int pad)
{
	const float cx = panel->x + panel->w - pad - MENU_ZYPER_H * ZYPER_W / ZYPER_H / 2.0f;
	const float cy = panel->y + UI_PANEL_BORDER + ZR_REACH + MENU_ZYPER_H / 2.0f;
	zr_pose p;

	/* and none when the look has him off (DESIGN.md 3.5) */
	if (!g_menu_zyper || look_zyper_off()) return;
	p = zr_pose_at(g_menu_zyper, g_menu_zyper_at);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	zyper_shadow_draw(a->r, cx, cy, MENU_ZYPER_H, p.tilt, 1.0f);
	draw_zyper_pose(a->r, cx, cy, MENU_ZYPER_H, &p, zyper_brow_lift(p.tilt), 0.0f, 255);
}

/* `fixed_w` is the content width to use, or 0 to size to these rows. The shelf
 * menus pass a width measured across both of them so the panel never resizes;
 * the in-game menu has no values to cycle and sizes to itself. */
/* `accent` is whose menu it is: a system's color, which is its heading rule
 * and the light it is lifted with in Salt & Pepper, or MENU_ZYPEROS for a menu
 * that is no system's. The shelf's own menu passes MENU_ZYPEROS because that
 * menu is ZyperOS, not whichever card is under the cursor; a menu that
 * belongs to a system passes that system's color, CFG_NO_COLOR for one whose
 * line names none. It is never the focus, which is lime on every menu
 * (DESIGN.md 3.3). */
/* `vcolors` is an optional array parallel to `rows`: a nonzero entry is one
 * of the VCOL_ roles above, which says what color that row is drawn in. A
 * ROLE and never a color: a color worked out before a screen's loop is the
 * theme of the moment the screen opened, and Auto turns the theme under a
 * screen that is still open - an earned value in Salt & Pepper's lime on
 * Beard's surface was 1.3:1. menu_draw_ex looks the role up in the theme on
 * every frame. It is not a field on menu_row because few screens have any
 * use for it, and putting it there made all 69 initializers in the launcher
 * declare that they do not care. NULL, which is what menu_draw passes, is
 * the ordinary rule. */
/* `visits_all` says the cursor can rest on EVERY row, whatever their `live`
 * says - which is only true of a list that uses `live` for something other
 * than selectability. The achievement list is the one: there `live` means
 * EARNED, and the cursor walks earned and unearned alike. Everywhere else the
 * two meanings coincide and this is false. */
/* `loop_at` is where the rows start repeating: rows[loop_at] is a second copy
 * of rows[0], laid out by the caller with a rule between the copies, so a body
 * too tall for its panel can scroll THROUGH and come round rather than running
 * backwards. 0 for every screen that does not do that, which is all but one.
 *
 * The caller passes it rather than this inferring it, because only the caller
 * knows the content is a repeat - and getting it wrong does not look like a
 * bug, it looks like a scroll that jumps. */
/* ui_text_marquee for a menu, which also says when the row travels. A
 * menu's loop draws every frame and asks the redraw clock for nothing, so
 * without this the status corner would not know a row was moving and could
 * stop one of its frames with a fork (frames_wanted): the shelf's marquee
 * says the same with its own redraw_at. */
static void menu_marquee(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
                         int w, unsigned phase, SDL_Color col)
{
	int over = f && s ? ui_text_width(f, s) - w : 0;

	ui_text_marquee(r, f, s, x, y, w, phase, col);
	if (over > 0) redraw_at(plat_now_ms() + ui_pingpong_wait(over, phase));
}

/* `pose` is where the panel is in its open or its close (src/menumotion.h):
 * at rest, {1, 1}, it is drawn straight on the screen as it always was;
 * anywhere else, through the panel pass below. menu_draw_ex and
 * menu_draw_caption are how every screen asks for one; this is the drawing.
 * `heading_note` is a caption at the heading's right, or NULL. */
/* A picture at the start of each two-column row, the label after it: the
 * achievement list's badges (My pick in the achievements lab,
 * 2026-11-05). Set by that screen around its own draw and NULL otherwise;
 * NULL for a row leaves its square empty, so the labels stay in one column. */
SDL_Texture *(*g_row_icon)(int row);

/* A body that holds still and is walked by hand (About's story): `>= 0` is the
 * pixels the caller has scrolled it by, in place of the ping-pong or the loop
 * the cursorless body runs by itself. Set around that screen's own draw and -1
 * everywhere else. Out come how far it can go (`g_menu_scroll_span`, from the
 * last draw) and the height of one line (`g_menu_scroll_line`), so the caller
 * stops at the ends and steps by a line without measuring anything. */
int g_menu_scroll_hold = -1;
int g_menu_scroll_span, g_menu_scroll_line;

/* A row's height in a panel: a rule's, a note's, or a row's (ROW_H) */
static int panel_row_h(menu_row r, int rule_h, int note_h, int row_h)
{
	return !r.label ? rule_h : (ROW_IS_NOTE(r) ? note_h : row_h);
}

/* A slot's (SLOT_H): in a list that goes round (menu_loop_*), slot `v`'s row
 * or the seam, which is a row high; otherwise row `v`'s */
static int panel_slot_h(const menu_row *rows, int n, bool loop, int v,
                        int rule_h, int note_h, int row_h)
{
	int r = loop ? menu_loop_row(n, v) : v;

	return r < 0 ? row_h : panel_row_h(rows[r], rule_h, note_h, row_h);
}

/* A new list in the same panel, which the window, the give and the plate
 * take as another menu even when it has as many rows as the last: MENU's
 * tabs (sys_menu.h). Their state is keyed on the row count otherwise. */
unsigned g_menu_epoch;

/* How far a tab move is, 0 to 1 on **out** in **move**, and whether it
 * still moves (asking for the next frame if it does) */
static float menu_tabs_p(const menu_tabs *t)
{
	unsigned now = anim_now(), el;

	if (!t || !t->t0) return 1.0f;
	el = now - t->t0;
	if (el >= DS_MS_MOVE) return 1.0f;
	redraw_now();
	return ds_out((float)el / DS_MS_MOVE);
}

/* How many tabs a tab row shows either side of the one that is on, as far as
 * they reach: the next and the one after, and the same back; with only a few
 * tabs, as many as fit without a name shown twice */
#define MENU_TAB_SIDE 2

/* One tab's row (DESIGN.md 11.3): the tab in **label** **text**, centered as
 * the heading was, and the tabs either side of it in **meta** **text-dim**,
 * going on round, laid out from it outward across the whole band between
 * the L1 and R1 glyphs (`lo` to `hi`); what reaches an end fades into it
 * over MENU_TAB_FADE, so a long name is shown as far as it fits and never
 * cut hard (2026-11-12: "use the full width up to the zyper icon (if
 * present) and kinda fade away on the extremes before the shoulder
 * buttons"). Returns the tab's name's width, for the bar under it. `dx` and
 * `op` draw it moved and faded, for a move. */
#define MENU_TAB_FADE 64
static int menu_tab_row(app *a, const menu_tabs *tabs, int at, int cx, int cy,
                        int lo, int hi, int dx, float op)
{
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_META);
	const char *cur = tabs->name(at);
	const int gap = 32, apart = 40;
	const int count = tabs->count;
	const int side = MENU_TAB_SIDE < (count - 1) / 2 ? MENU_TAB_SIDE : (count - 1) / 2;
	int aw = ui_text_width(fh, cur), cur_x = cx - aw / 2, k, x;
	SDL_Color dim = UI_TEXT_DIM, txt = UI_TEXT;

	dim.a = (Uint8)(dim.a * op);
	txt.a = (Uint8)(op * 255.0f + 0.5f);
	/* back: the tab before, ending a gap left of the name, then the one
	 * before that, while any of it is still inside */
	for (k = 1, x = cur_x - gap; k <= side && x + dx > lo; k++) {
		const char *t = tabs->name((at - k + 2 * count) % count);

		x -= ui_text_width(fm, t);
		ui_text_faded(a->r, fm, t, x + dx, ui_cap_y(fm, cy), lo, hi, MENU_TAB_FADE, dim);
		x -= apart;
	}
	/* on: the tab after, then the one after that */
	for (k = 1, x = cx + aw / 2 + gap; k <= side && x + dx < hi; k++) {
		const char *t = tabs->name((at + k) % count);

		ui_text_faded(a->r, fm, t, x + dx, ui_cap_y(fm, cy), lo, hi, MENU_TAB_FADE, dim);
		x += ui_text_width(fm, t) + apart;
	}
	ui_text_faded(a->r, fh, cur, cur_x + dx, ui_cap_y(fh, cy), lo, hi, MENU_TAB_FADE, txt);
	return aw;
}

/* MENU's tab row in the heading band, centered on the capitals at `cy`, the
 * L1 glyph at the band's left end (`zone_l`) and R1 at its right end
 * (`zone_r`, before Zyper, or the content's edge with him off), and the tabs
 * between them. Moving, the tab it came from slides out the way the press
 * went and the new one in from the other side, in **move** on **out**, and
 * the glyphs hold still. Returns the width of the bar under the name
 * (menu_tabs_bar), which takes the new name's width as it goes. */
static int menu_tabs_draw(app *a, const menu_tabs *t, int cx, int cy,
                          int zone_l, int zone_r)
{
	const int size = 40, gpad = 12;
	int lw = ui_button_width("L1", size), rw = ui_button_width("R1", size);
	int lo = zone_l + lw + gpad, hi = zone_r - rw - gpad;
	float p = menu_tabs_p(t);
	int aw, w;

	ui_button(a->r, "L1", zone_l + lw / 2, cy, size, UI_BUTTON_PLAIN);
	ui_button(a->r, "R1", zone_r - rw / 2, cy, size, UI_BUTTON_PLAIN);
	if (p < 1.0f && t->from != t->at) {
		/* the old one gone in the first two fifths, as its rows are */
		float op = 1.0f - p * 2.5f;
		int ow = op > 0.0f ? menu_tab_row(a, t, t->from, cx, cy, lo, hi,
		                                  (int)(-t->dir * 96 * p), op)
		                   : ui_text_width(ui_font(UI_F_LABEL),
		                                   t->name(t->from));

		aw = menu_tab_row(a, t, t->at, cx, cy, lo, hi, (int)(t->dir * 96 * (1.0f - p)), p);
		w = (int)(ow + (aw - ow) * p + 0.5f);
	} else {
		w = aw = menu_tab_row(a, t, t->at, cx, cy, lo, hi, 0, 1.0f);
	}
	(void)aw;
	return w;
}

/* Turn the tabs one way, +1 for R1 and -1 for L1, round the end: the one it
 * came from and which way are kept, for the move between them */
void tabs_turn(menu_tabs *t, int dir)
{
	t->from = t->at;
	t->at = (t->at + dir + t->count) % t->count;
	t->dir = dir;
	t->t0 = anim_now();
	if (!t->t0) t->t0 = 1;
	g_menu_epoch++;
}

/* The bar on the heading's rule at `rule_y` under the tab's name: the home's
 * underline, **accent-ink**, the bar's 6 high (DESIGN.md 11.3) */
static void menu_tabs_bar(app *a, int cx, int rule_y, int w)
{
	ui_round_rect(a->r, &(SDL_Rect){ cx - w / 2, rule_y + DS_RULE / 2 - DS_BAR / 2, w, DS_BAR },
	              DS_BAR / 2, ui_col(theme()->accent_ink, 255));
}

/* menu_style's picture at the rows' left (g_menu_side), told where it is
 * and in which pass (menu_side) */
static void menu_side_call(app *a, const SDL_Rect *panel, int content_x, int content_y,
                           const menu_row *rows, int n, int sel, menu_side_stage stage)
{
	menu_side at;

	if (!g_menu_side) return;
	memset(&at, 0, sizeof at);
	at.panel = *panel;
	at.content_x = content_x;
	at.content_y = content_y;
	at.rows = rows;
	at.n = n;
	at.sel = sel;
	at.live = !g_snap_drawing;
	at.tab_at = g_menu_tabs ? g_menu_tabs->at : -1;
	at.tab_from = g_menu_tabs ? g_menu_tabs->from : -1;
	at.tab_dir = g_menu_tabs ? g_menu_tabs->dir : 0;
	at.tab_t0 = g_menu_tabs ? g_menu_tabs->t0 : 0;
	at.now = anim_now();
	at.stage = stage;
	g_menu_side(a, &at);
}

void menu_panel(app *a, const char *heading, const menu_row *rows,
                int n, int sel, int fixed_w, unsigned accent,
                const unsigned *vcolors, bool visits_all, int loop_at,
                const char *heading_note, mm_pose pose)
{
	const ds_theme *thm = theme();
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	/* The focused row's text, DESIGN.md 11.3: **on-accent**, its label whole
	 * and its value at the 72% the mock draws a focused value at
	 * (docs/design/mock/kit.css, .mrow.sel span), which on the lime still
	 * clears 6:1. */
	const SDL_Color on = ui_col(thm->on_accent, 255);
	const SDL_Color on_quiet = ui_col(thm->on_accent, 184);
	/* The heading's rule and the list's: **edge**, or in a system's menu that
	 * system's color as a mark (ui_sys_mark), which for a system with no
	 * color is **text-dim**, as its other marks are. */
	const unsigned rule_rgb = accent == MENU_ZYPEROS ? ui_rgb(thm->edge)
	                                                 : ui_sys_mark(accent);
	const ds_rgb rule = { (Uint8)(rule_rgb >> 16), (Uint8)(rule_rgb >> 8),
	                      (Uint8)rule_rgb };
	TTF_Font *fm = ui_font(UI_F_MENU), *fh = ui_font(UI_F_LABEL);
	int row_h = menu_row_h();
	int pad = menu_pad();
	/* A rule is 2px of bar plus the gap the heading's rule gets below it, so
	 * the footer sits off the list by the same amount the first row sits off
	 * the heading. Giving it a whole row_h left it swimming.
	 *
	 * A note is one line of text and nothing else. row_h is 1.5x the line, so a
	 * note in a full row carried half a row of air underneath it and then the
	 * panel's own pad on top of that, which read as the footer floating off the
	 * bottom border. */
	int rule_h = pad / 2 + 2;
	int gap = row_h;                 /* between the label and value columns */
	/* ...the air a panel is sized with. A row that measures past it on a
	 * device whose fonts measure a little wider than the host's is not cut
	 * for that: it may close the gap to half a row before anything is cut
	 * ("auto (alto-falante)" in MENU, 3 units over on the Pixel 2,
	 * 2026-11-10) */
	int fit_gap = row_h / 2;
	int note_h = ui_font_line(UI_F_MENU) + pad / 3;
	/* A row's height. A function under the macro, so its row is read once:
	 * as a macro that read `r` up to four times, the scroll below wrote
	 * ROW_H(rows[--lo]) and stepped lo three rows a pass, to rows[-2] -
	 * which crashed the launcher whenever DOWN was held in MENU while the
	 * list still eased (2026-11-05). tools/rowmacro-check.py holds it. */
#define ROW_H(r) panel_row_h((r), rule_h, note_h, row_h)
#define SLOT_H(v) panel_slot_h(rows, n, loop, (v), rule_h, note_h, row_h)
	/* A row's text is centered on its capitals (ui_cap_y), not on its box.
	 * The box reserves a descender's depth below the baseline that labels
	 * like "Wi-Fi" and "Bluetooth" never use, so centering the box leaves
	 * the visible line riding high in its row and reads as a highlight
	 * sitting too low. That was put right by half the descent, which held
	 * for a face whose ascent was three quarters of an em; Lexend's is a
	 * whole one, and the capitals are the same middle in any face. */
	/* The heading band runs from the panel's top edge down to the rule, and
	 * the heading is centered inside it rather than hung a fixed distance from
	 * the top - otherwise retuning the heading's size moves it off center,
	 * which is exactly what happened when it grew. `content_off` is the panel
	 * top to the first row, so a panel with no heading just pads instead. */
	int notes_drawn = 0;   /* which marquee slot the next long note takes */
	int scroll = 0;        /* pixels the body is lifted by, cursorless */
	int scroll_h = 0;      /* the body viewport's height, 0 if nothing scrolls */
	int scroll_span = 0;
	int scroll_split = 0;  /* first pinned row: the rule, or n if there is none */
	int scroll_foot = 0;   /* the pinned rows' total height */
	/* THE LIST GIVES WHEN THE WINDOW SCROLLS: the rows start where they
	 * were and slide the row's height into place in **quick**, on **out**.
	 * See the give below. */
	float win_off = 0.0f;

	/* Where the highlight actually is. The text colors are weighed by how
	 * much of it covers each row rather than by `i == sel`, so a row lights
	 * as the plate arrives and dims as it leaves, and the two rows either
	 * side of a move cross over correctly without anyone tracking which row
	 * was left. */
	float plate_y = -1e9f, plate_h = 0.0f;
	int line_head   = ui_font_line(UI_F_LABEL);
	/* A heading may carry a second line, separated by a newline. Nothing else
	 * passes one; the achievements screen does, because its heading is a game
	 * title AND two counts, and on one line that was wider than the panel and
	 * got both its ends clipped. Splitting keeps both rather than choosing. */
	const char *head2 = heading ? strchr(heading, '\n') : NULL;
	char head1[192];
	int head_lines = head2 ? 2 : 1;
	int head_h      = heading ? line_head * head_lines + pad : 0;
	int content_off = (heading ? head_h + pad / 2 : pad)
	                  + (g_menu_header ? g_menu_header_h : 0);
	bool two_col = false;
	int content_w = 0, i, k, rows_h = 0;
	SDL_Rect panel;
	int cx, content_x, content_y;
	/* The list can outgrow the screen from either end - a menu is meant to
	 * gain rows - so the panel is capped to the screen and the rows window
	 * around the selection when they do not all fit. A menu that runs off the
	 * top is worse than one that scrolls. */
	const int margin = MENU_MARGIN;
	int vis = n, first = 0;
	/* A list too long for its window goes round without end (menu_loop_*,
	 * DESIGN.md 11.3): `first` and `vsel` are then slots, not rows */
	bool loop = false;
	int vsel = sel, v;
	/* MENU's tabs: the bar's width under the name, and the rows of a tab
	 * just come sliding in the way the press went and fading up, in
	 * **move** on **out** as the tab row (menu_tabs_draw) */
	int tab_bar_w = 0;
	float tab_p = g_menu_tabs ? menu_tabs_p(g_menu_tabs) : 1.0f;
	bool tab_moving = tab_p < 1.0f;
	int tab_dx = tab_moving ? (int)(g_menu_tabs->dir * 64 * (1.0f - tab_p)) : 0;
	/* menu_style's picture at the rows' left: the rows and the focus fill
	 * start `side_w` in from the content's left edge, the heading and its
	 * rule do not */
	const int side_w = g_menu_side ? g_menu_side_w : 0;
	int row_cx;

	/* A rule's sentinel is no value: a list with a rule in it and no values,
	 * Delete Game's question over its answers, stays a centered list */
	for (i = 0; i < n; i++)
		if (rows[i].value && !ROW_IS_NOTE(rows[i]) && !ROW_IS_HR(rows[i])) two_col = true;
	/* MENU's tabs are one list in pieces: a tab whose rows have no values
	 * (About) is laid out as the others are, not centered */
	if (g_menu_tabs) two_col = true;
	for (i = 0; i < n; i++) {
		int w;

		if (!rows[i].label) continue;          /* a rule measures nothing */
		/* A legend of button hints (keyhints.h) on its one row */
		w = keyhints_is(rows[i].label) ? keyhints_line_width(rows[i].label)
		                               : ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			w += gap + menu_value_w(fm, rows[i].value);
		if (w > content_w) content_w = w;
	}
	if (head2) {
		size_t n1 = (size_t)(head2 - heading);

		if (n1 >= sizeof head1) n1 = sizeof head1 - 1;
		memcpy(head1, heading, n1);
		head1[n1] = '\0';
		head2++;                                    /* past the newline */
	} else if (heading) {
		snprintf(head1, sizeof head1, "%s", heading);
	}

	if (heading) {
		int w = ui_text_width(fh, head1);
		int w2 = head2 ? ui_text_width(fh, head2) : 0;

		if (w2 > w) w = w2;
		if (w > content_w) content_w = w;
	}
	if (fixed_w > 0) content_w = fixed_w;
	else content_w += side_w;
	if (content_w > ZYPEROS_SCREEN_W - margin * 2 - pad * 2)
		content_w = ZYPEROS_SCREEN_W - margin * 2 - pad * 2;
	/* The most a row can have: the fixed width, or the screen's. A row
	 * that sizes a free panel is as wide as the panel by its own measure,
	 * so only this cap says a row is near being cut (ui_fit_tight) */
	int cap_w = fixed_w > 0 && fixed_w < ZYPEROS_SCREEN_W - margin * 2 - pad * 2
	          ? fixed_w : ZYPEROS_SCREEN_W - margin * 2 - pad * 2;

	{
		int sum = 0;
		for (i = 0; i < n; i++) sum += ROW_H(rows[i]);
		rows_h = sum;
	}
	if (content_off + rows_h + pad > MENU_AVAIL_H) {
		int avail = MENU_AVAIL_H - content_off - pad;

		if (sel >= 0) {
			/* THE WINDOW ONLY MOVES WHEN THE CURSOR WOULD LEAVE IT.
			 *
			 * It used to center itself on the selection, which meant almost
			 * every press scrolled the list: the highlight stayed put in the
			 * middle of the panel and the rows jumped under it. That is the
			 * wrong thing to move. The cursor is what the player is aiming,
			 * so the cursor travels and the list holds still until it runs
			 * out of window.
			 *
			 * Static, and keyed on `n` like the highlight's own state, since
			 * menu_draw keeps nothing between frames and every screen shares
			 * it. A different row count is a different menu and starts at the
			 * top rather than inheriting someone else's scroll. */
			static int win_n = -1, win_first, win_slot;
			static unsigned win_epoch;

			vis = avail / row_h;
			if (vis < 1) vis = 1;
			if (vis > n) vis = n;

			if (win_n != n || win_epoch != g_menu_epoch) { win_first = 0; win_slot = sel; }
			win_n = n;
			win_epoch = g_menu_epoch;
			/* AND IT GOES ROUND. Past the last row the list goes on: a seam,
			 * then its first row, and on (menu_loop_*). The cursor's slot is
			 * the copy of its row nearest the last one, so a step past the
			 * end is a step, two slots over the seam, and the window follows
			 * it as it follows any step. (2026-11-12: lists go round, and
			 * the list I see moves on rather than jumping back to its top.) */
			loop = true;
			vsel = menu_loop_slot(n, sel, win_slot);
			win_slot = vsel;
			win_first = menu_loop_first(rows, n, vsel, vis, win_first);
			first = win_first;
		} else {
			/* NO CURSOR MEANS NOTHING CAN SCROLL IT, so it scrolls itself.
			 * Dropping rows off the bottom is what a cursor makes safe: you
			 * can always walk down to them. A card has no walk, so the rows
			 * all stay and the window moves over them instead, on the same
			 * ping-pong a long title runs sideways.
			 *
			 * ONLY THE BODY MOVES. Past the rule is the footer, and on the
			 * achievement card that is the points and whether it is earned -
			 * the two facts a reader wants WHILE reading the description, not
			 * after waiting out a scroll to reach them. The rule is already
			 * where this menu divides a list from its footer, so it is the
			 * split here too and nothing new has to be declared. */
			int body = 0, j;

			/* MENU_RULE only: a MENU_HR is punctuation inside the body,
			 * and treating it as the footer split would pin half a synopsis
			 * to the bottom of the panel. */
			scroll_split = n;
			for (j = 0; j < n; j++)
				if (!rows[j].label && !ROW_IS_HR(rows[j])) {
					scroll_split = j;
					break;
				}
			for (j = scroll_split; j < n; j++) scroll_foot += ROW_H(rows[j]);
			for (j = 0; j < scroll_split; j++) body += ROW_H(rows[j]);

			scroll_h = avail - scroll_foot;
			if (scroll_h < row_h) scroll_h = row_h;
			scroll_span = body - scroll_h;
			if (scroll_span < 0) scroll_span = 0;
			g_menu_scroll_span = scroll_span;
			g_menu_scroll_line = note_h;
			/* Walked by hand: nothing travels, so nothing asks for a frame.
			 * The soft edges below say which way there is more. */
			if (g_menu_scroll_hold >= 0) {
				scroll = g_menu_scroll_hold < scroll_span ? g_menu_scroll_hold : scroll_span;
			} else {
			/* Moving while it travels, for the status corner's forks
			 * (frames_wanted): a looping card always, a ping-pong out of
			 * its holds */
			if (loop_at > 0 && loop_at < n) redraw_now();
			else redraw_at(plat_now_ms() +
			               ui_pingpong_wait(scroll_span, mq_phase(MQ_VSCROLL, n, rows_h)));
			if (loop_at > 0 && loop_at < n) {
				/* One lap is everything before the repeat, so the wrap lands
				 * on the same picture the lap started from. What keeps the
				 * bottom of the viewport covered through a whole lap is that
				 * the second copy is as tall as the first, which is only
				 * worth scrolling at all when it is taller than the panel. */
				int lap = 0;

				for (j = 0; j < loop_at; j++) lap += ROW_H(rows[j]);
				scroll = ui_scrollthrough(lap,
				                          mq_phase(MQ_VSCROLL, n, rows_h));
			} else {
				scroll = ui_pingpong(scroll_span,
				                     mq_phase(MQ_VSCROLL, n, rows_h));
			}
			}
		}
	}

	panel.w = content_w + pad * 2;
	{   /* the heights actually on screen, not vis * row_h */
		int sum = 0, j, last = first + vis - 1;

		for (j = first; j < first + vis && (loop || j < n); j++) sum += SLOT_H(j);
		/* A scrolling card is as tall as its viewport plus its pinned footer.
		 * Summing the rows would size the panel to text that is deliberately
		 * not all on it. */
		panel.h = content_off + (scroll_h ? scroll_h + scroll_foot : sum) + pad;
		/* A panel that ends in a footer gets a smaller bottom pad. The full
		 * pad is there to keep list ITEMS off the border; a note is already
		 * held off the list by its rule and is meant to sit low.
		 *
		 * The gap under the footer text is pad/6 from centering plus whatever
		 * bottom pad is left. Set by eye on the device 2026-09-04, not derived:
		 * the full pad gives 1.17 and floats, 0.50 is too tight, and 0.75 is
		 * where Eric called it. 5/12 is the subtraction that leaves 0.75.
		 *
		 * A body row is not a footer. A synopsis short enough to fit without
		 * scrolling ends in prose, which is the panel's content and wants the
		 * same pad under it that a list does. */
		if (!scroll_h && !loop && last >= 0 && last < n && rows[last].label &&
		    ROW_IS_NOTE(rows[last]) && !ROW_IS_BODY(rows[last]))
			panel.h -= pad * 5 / 12;
	}
	/* MENU's tabs: as tall as the screen allows whichever tab is on, so the
	 * panel holds still as L1 and R1 go round (DESIGN.md 11.3); and a panel
	 * with a picture beside its rows, so the picture holds still as the
	 * pages beside it change (the Controls tab's handheld) */
	if (g_menu_tabs || g_menu_side) {
		int most = (MENU_AVAIL_H - content_off - pad) / row_h;
		int tall = content_off + most * row_h + pad;

		if (panel.h < tall) panel.h = tall;
	}
	panel.x = (ZYPEROS_SCREEN_W - panel.w) / 2;
	panel.y = (ZYPEROS_SCREEN_H - panel.h) / 2;
	if (panel.y < MENU_TOP) panel.y = MENU_TOP;
	g_menu_panel = panel;   /* for a shot's check of the corner */
	cx = panel.x + panel.w / 2;
	content_x = panel.x + pad;
	content_y = panel.y + content_off;
	g_menu_body = (SDL_Rect){ content_x, content_y, panel.w - pad * 2,
	                          panel.y + panel.h - pad - content_y };

	/* DESIGN.md 11.3: **surface** in an **edge** frame, lifted off what is
	 * behind it - a shadow on Beard, light in the system's color in Salt &
	 * Pepper, or a neutral light for a menu that is no system's. The frame is
	 * never the system's color: a menu says whose it is on its heading rule. */
	/* Moving, the lift goes straight on the screen at the panel's size and
	 * its strength, and the panel into the pass: a shadow or a glow drawn
	 * into a clear texture and copied out with its alpha would be faded
	 * twice over. */
	if (posed) {
		SDL_FRect f = panel_scaled(&panel, pose.scale);
		SDL_Rect q = { (int)(f.x + 0.5f), (int)(f.y + 0.5f),
		               (int)(f.w + 0.5f), (int)(f.h + 0.5f) };

		ui_lift_k(a->r, &q, accent == MENU_ZYPEROS ? CFG_NO_COLOR : accent,
		          UI_LIFT_PANEL, pose.alpha);
		posed = panel_pass_begin(a);
	} else {
		ui_lift(a->r, &panel, accent == MENU_ZYPEROS ? CFG_NO_COLOR : accent,
		        UI_LIFT_PANEL);
	}
	ui_panel(a->r, &panel, DS_RADIUS_PANEL);

	if (heading) {
		/* Centered in the band by its ink, on the same reasoning as the rows:
		 * the em box carries descender depth that "ZyperOS" and "NES" do not use.
		 * This read "mostly do not" while the heading was "PlayOS", whose y was
		 * the exception; the shift is by font metrics rather than by the string,
		 * so nothing here changed with the name, it just got exactly true. */
		/* Center the INK between the panel's top and the rule, computed from
		 * the font rather than nudged by a fraction of the descent.
		 *
		 * The heading is capitals and lowercase with nothing below the
		 * baseline, so what should sit in the middle of that band is cap-top
		 * to baseline - not the em box, which carries a descender's depth of
		 * empty space at the bottom. Centering the box left "Wi-Fi" visibly
		 * high on the scanning screen.
		 *
		 * cap is ui_cap_height's, the height of an 'H' above the baseline,
		 * and a line's height when there is no font to ask. */
		int asc = fh ? TTF_FontAscent(fh) : line_head;
		int cap = fh ? ui_cap_height(fh) : asc;
		int block, hy;

		/* cap-to-baseline for the first line, plus a whole line for a second */
		block = cap + (head_lines - 1) * line_head;
		/* From the panel's INNER edge, not panel.y. The border is a visible
		 * frame and the eye reads the space inside it, so centering against
		 * the outer edge is arithmetically right and looks high by exactly
		 * the border's width - measured on the Wi-Fi panel as 18px above the
		 * ink against 32 below, which is what Eric saw. */
		hy = panel.y + UI_PANEL_BORDER
		     + (head_h - UI_PANEL_BORDER - block) / 2 - (asc - cap);

		/* Same rule for the heading, and for the same reason: art_screen and
		 * notice.c each fit their own before handing it over, which worked
		 * and meant two callers had to remember. */
		{
			/* A HEADING THAT DOES NOT FIT SCROLLS, like every other line in
			 * this launcher that does not fit. It used to be cut with an
			 * ellipsis and left there, which on the achievement list meant a
			 * game's own name was the one thing on screen you could not read:
			 * "Castlevania III - Dracula's C...". The panel is a fixed width
			 * shared by every menu, and a game title is whatever it is.
			 *
			 * Left-aligned while it moves, for the reason a long note is:
			 * text wider than the box has nothing left to center.
			 *
			 * Both lines share one slot, so when a heading has two they move
			 * together rather than taking turns resetting each other's clock -
			 * the same lesson the value marquee learned on Wi-Fi File Transfer. */
			char h1[192], h2[192];
			TTF_Font *meta = ui_font(UI_F_META);
			int inset = heading_note ? ui_text_width(meta, heading_note) + 2 * DS_GRID : 0;
			int title_room = content_w - 2 * inset;
			int title_x = content_x + inset;
			int title_cx = cx;
			int hw = ui_text_width(fh, head1);
			unsigned ph;

			/* Centered with the same room either side of it, so the count
			 * cannot touch it; when a longer heading (Commandes : Game Boy,
			 * 2026-11-07) does not fit that, it gives up the centering and
			 * takes all the room the count leaves, centered in what is
			 * left of the panel, before it has to scroll */
			if (inset && (hw > title_room || (head2 && ui_text_width(fh, head2) > title_room))) {
				title_room = content_w - inset;
				title_x = content_x;
				title_cx = content_x + title_room / 2;
			}
			if (title_room < 1) title_room = 1;
			ph = mq_phase(MQ_HEAD, (int)strlen(head1), title_room);
			/* Page information belongs to the panel */
			if (heading_note)
				ui_text(a->r, meta, heading_note, content_x + content_w,
				        ui_cap_y(meta, hy + asc - cap / 2), 1, UI_TEXT_DIM);

			if (g_menu_tabs) {
				/* MENU's tab row, the whole band from the content's inset
				 * to Zyper, a grid step and a half clear of him, or to the
				 * content's edge with him off */
				int zr = content_x + content_w;

				if (g_menu_zyper && !look_zyper_off())
					zr = panel.x + panel.w - pad
					     - (int)(MENU_ZYPER_H * ZYPER_W / ZYPER_H) - DS_GRID * 3 / 2;
				tab_bar_w = menu_tabs_draw(a, g_menu_tabs, cx, hy + asc - cap / 2,
				                           content_x, zr);
			} else if (hw <= title_room) {
				ui_fit_text(fh, head1, h1, sizeof h1, title_room);
				ui_text(a->r, fh, h1, title_cx, hy, 0, UI_TEXT_SOFT);
			} else {
				menu_marquee(a->r, fh, head1, title_x, hy, title_room,
				                ph, UI_TEXT_SOFT);
			}
			if (head2) {
				if (ui_text_width(fh, head2) <= title_room) {
					ui_fit_text(fh, head2, h2, sizeof h2, title_room);
					ui_text(a->r, fh, h2, title_cx, hy + line_head, 0, UI_TEXT_SOFT);
				} else {
					menu_marquee(a->r, fh, head2, title_x,
					                hy + line_head, title_room, ph,
					                UI_TEXT_SOFT);
				}
			}
		}
		SDL_SetRenderDrawColor(a->r, rule.r, rule.g, rule.b, 255);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x, panel.y + head_h,
		                                      content_w, DS_RULE });
		if (g_menu_tabs) menu_tabs_bar(a, cx, panel.y + head_h, tab_bar_w);
		menu_zyper_draw(a, &panel, pad);
	}
	/* the screen's picture under the rule, its gap under it, or at the top
	 * of a panel with no heading (File Transfer's way in) */
	if (g_menu_header)
		g_menu_header(a, &(SDL_Rect){ content_x,
		                              heading ? panel.y + head_h + pad / 2 : panel.y + pad,
		                              content_w, g_menu_header_h - pad / 2 });
	/* the picture at the rows' left, before the rows and with nothing
	 * clipped: in the pass that moves the panel, or at rest */
	menu_side_call(a, &panel, content_x, content_y, rows, n, sel,
	               posed ? MENU_SIDE_PASS : MENU_SIDE_REST);

	/* THE HIGHLIGHT SLIDES. It used to be drawn inside the row loop at
	 * i == sel, which put it wherever the cursor was that frame and made it
	 * snap. Here it is a rect of its own, moving from where it was toward
	 * where it belongs, and drawn BEFORE the rows so it stays under all of
	 * them - inside the loop, a plate that had moved up would cover the text
	 * of the row already drawn above it.
	 *
	 * The state is static because menu_draw has none and every screen shares
	 * it. `n` is the key: a different row count is a different menu, and the
	 * plate snaps rather than travelling across from wherever the last menu
	 * left it. Two menus of the SAME length in a row will slide between
	 * themselves, which is wrong but invisible - both are lists of the same
	 * shape and the plate is already close to where it should be. */
	if (vis < n) {
		/* THE GIVE: a scroll of one row starts the rows where they were, a
		 * row back, and slides them into place in **quick** on **out**, from
		 * wherever they are when it comes (tween_kick). Two chained decays
		 * did this before, with the give cut to 0.55 of a row so a held
		 * button never left the list far behind the cursor; a move in
		 * **quick** that takes over from where the rows are is in place
		 * before the next repeat (tools/tween-check.c), so it starts from
		 * where the rows really were.
		 *
		 * And nothing clips it. A clip to a row, so that one row past each
		 * end of the window would be enough to draw, jumped the rows by
		 * whatever it clipped: a quarter of a row at a press every 30 ms. The
		 * rows past the window are drawn as far as the give reaches instead
		 * (below), and the give stays within a row and a half even then: at
		 * the d-pad's repeat it is a row and a thousandth. */
		static int   wn = -1, wfirst = -1;
		static unsigned wep;
		static tween give;
		unsigned nw = anim_now();

		if (wn != n || wep != g_menu_epoch) tween_snap(&give, 0.0f);
		else if (wfirst != first) {
			int a0 = first < wfirst ? first : wfirst;
			int b0 = first < wfirst ? wfirst : first;
			float d = 0.0f;
			int j2;

			/* A STEP GIVES, A JUMP DOES NOT. Wrapping from the last row to
			 * the first moves the window the whole length of the list, and a
			 * shoulder button pages it by eight; easing across either drags
			 * rows in from the far end and reads as the list skipping. Only
			 * a single row's move is softened. */
			/* Going round is two slots, the seam and the row: a step */
			if (b0 - a0 > (loop ? 2 : 1)) {
				tween_snap(&give, 0.0f);
			} else {
				for (j2 = a0; j2 < b0 && (loop || j2 < n); j2++) d += SLOT_H(j2);
				tween_kick(&give, (first > wfirst) ? d : -d, DS_MS_QUICK, nw);
			}
		}
		wn = n; wfirst = first; wep = g_menu_epoch;
		win_off = tween_at(&give, nw);
		if (tween_moving(&give, nw)) redraw_now();    /* the list still easing: moving */
	}

	/* No plate on a note nothing can land on: a list that is only that (the
	 * in-game menu's Controls tab before it has rows) has no cursor to show */
	if (sel >= 0 && sel < n && rows[sel].label &&
	    !(ROW_IS_NOTE(rows[sel]) && !rows[sel].live)) {
		static int   pl_n = -1;
		static unsigned pl_ep;
		static tween pl_y, pl_h;
		/* A MOVE IN **QUICK** THAT TAKES OVER FROM WHERE THE PLATE IS,
		 * DESIGN.md 8.2's focus fill (src/tween.h). REPEAT_RATE_MS is 90 and
		 * a timed slide used to take 150, so holding the d-pad meant the plate
		 * never finished a move: it trailed a row behind, and on the wrap -
		 * which snaps - it jumped from halfway to the top. The last row before
		 * the wrap was being selected and never visibly highlighted, which is
		 * what it looked like from the outside. Two chained decays replaced
		 * it, at 96% of the way within one repeat.
		 *
		 * This is on its row within one repeat, 99.9% of the way
		 * (tools/tween-check.c), and a press mid-move goes on from where the
		 * plate is rather than starting over, so under repeat it keeps up and
		 * after a single press it eases.
		 *
		 * It goes where its row will REST, not where the row is drawn this
		 * frame: on a scroll the list gives under it (win_off above), and a
		 * plate chasing a row that is still sliding would be sent somewhere
		 * new every frame. At the window's edge it holds still and the rows
		 * slide into it. */
		int ty_sel = content_y - scroll, jj;
		unsigned nowp = anim_now();
		float tgt_h = (float)ROW_H(rows[sel]);
		SDL_Rect plate;

		for (jj = first; jj < vsel && (loop || jj < n); jj++) ty_sel += SLOT_H(jj);

		/* Another menu, or another tab of MENU: no journey. Anything else
		 * glides, a wrap too: in a list that fits, the fill goes back up
		 * (or down) to the other end in **quick** as any step does, and in
		 * a list that goes round it is the next slot anyway (DESIGN.md
		 * 11.3, 2026-11-12; until then a wrap snapped, and from 1.0.1 there
		 * was none). */
		if (pl_n != n || pl_ep != g_menu_epoch) {
			tween_snap(&pl_y, (float)ty_sel);
			tween_snap(&pl_h, tgt_h);
		}
		pl_n = n; pl_ep = g_menu_epoch;
		tween_to(&pl_y, (float)ty_sel, DS_MS_QUICK, nowp);
		tween_to(&pl_h, tgt_h, DS_MS_QUICK, nowp);
		/* the plate still on its way: moving */
		if (tween_moving(&pl_y, nowp) || tween_moving(&pl_h, nowp))
			redraw_now();

		/* The **accent** fill, DESIGN.md 11.3: lime on every menu, whoever's
		 * it is, and never the system's color. On a dark red system a plate
		 * in the system's color read as a stain on the row rather than a
		 * highlight under it, and with the system's color on the heading rule
		 * the two competed; lime is ZyperOS's, and it means the focus
		 * wherever it is.
		 *
		 * Flat, with no radial glow under it. The glow was brightest at the
		 * row's midpoint and fell off toward both ends, which put a soft blob
		 * behind the middle of every highlighted row and read as a smudge
		 * rather than as a selection. */
		plate_y = tween_at(&pl_y, nowp);
		plate_h = tween_at(&pl_h, nowp);
		plate.x = panel.x + pad / 2 + tab_dx + side_w;
		plate.y = (int)(plate_y + 0.5f);
		plate.w = panel.w - pad - side_w;
		plate.h = (int)(plate_h + 0.5f);
		if (tab_moving)
			SDL_RenderSetClipRect(a->r, &(SDL_Rect){ panel.x + UI_PANEL_BORDER, panel.y,
			                                         panel.w - UI_PANEL_BORDER * 2, panel.h });
		ui_round_rect(a->r, &plate, DS_RADIUS_ROW,
		              ui_col(thm->accent, (Uint8)(255.0f * tab_p + 0.5f)));
	}

	notes_drawn = 0;
	if (scroll_h)
		SDL_RenderSetClipRect(a->r, &(SDL_Rect){ panel.x, content_y,
		                                         panel.w, scroll_h });
	/* Clipped to the panel, and ONE ROW BEYOND THE WINDOW at each end, or as
	 * many as the give reaches past it. Offsetting the rows opens a gap at
	 * one edge and pushes the far row past the other; without the extra rows
	 * the gap is empty, and without the clip the overflow draws outside the
	 * panel. Leaving either out is what made the first attempt at this look
	 * like the list was wrapping. */
	int lo = first > 0 || loop ? first - 1 : first, hi = first + vis;

	if (vis < n) {
		SDL_Rect rc = { panel.x + UI_PANEL_BORDER, content_y,
		                panel.w - UI_PANEL_BORDER * 2,
		                panel.y + panel.h - pad - content_y };
		float cover;

		SDL_RenderSetClipRect(a->r, &rc);
		cover = lo < first ? SLOT_H(lo) : 0;
		while ((loop || lo > 0) && cover < win_off) {
			lo--;
			cover += SLOT_H(lo);
		}
		cover = loop || hi < n ? SLOT_H(hi) : 0;
		while ((loop || hi + 1 < n) && cover < -win_off) {
			hi++;
			cover += SLOT_H(hi);
		}
	}
	if (tab_moving) {
		if (vis >= n)
			SDL_RenderSetClipRect(a->r, &(SDL_Rect){ panel.x + UI_PANEL_BORDER, panel.y,
			                                         panel.w - UI_PANEL_BORDER * 2, panel.h });
		content_x += tab_dx;
	}
	/* Set in by the picture beside them, and centered in what is left */
	content_x += side_w;
	content_w -= side_w;
	cap_w -= side_w;
	row_cx = side_w ? content_x + content_w / 2 : cx;
	/* `v` walks the slots on screen and `i` is the row each holds: one and
	 * the same unless the list goes round */
	for (k = 0, v = lo; (loop || v < n) && v <= hi; k++, v++) {
		int y, ty, j;
		float sel_w;
		SDL_Color lc, vc;

		i = loop ? menu_loop_row(n, v) : v;
		if (scroll_h && i >= scroll_split) {
			/* Pinned. Measured from the bottom of the viewport rather than
			 * from the top of the rows, so the footer does not move when the
			 * body does. The clip comes off here and stays off. */
			if (i == scroll_split) SDL_RenderSetClipRect(a->r, NULL);
			y = content_y + scroll_h;
			for (j = scroll_split; j < i; j++) y += ROW_H(rows[j]);
		} else {
			y = content_y - scroll + (int)win_off;
			if (v >= first) for (j = first; j < v; j++) y += SLOT_H(j);
			else            for (j = v; j < first; j++) y -= SLOT_H(j);
		}
		/* The seam, where a list that goes round starts again: the
		 * heading's rule, across the middle of a row's height */
		if (i < 0) {
			SDL_SetRenderDrawColor(a->r, rule.r, rule.g, rule.b, 255);
			SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x,
			                                      y + (row_h - DS_RULE) / 2,
			                                      content_w, DS_RULE });
			continue;
		}
		/* Centered in ITS OWN row, not in row_h: a note is shorter. */
		ty = ui_cap_y(fm, y + ROW_H(rows[i]) / 2);

		/* The same bar as the heading's, in the same color and width. If
		 * one is ever retuned the other follows, which is the point. */
		if (!rows[i].label) {
			SDL_SetRenderDrawColor(a->r, rule.r, rule.g, rule.b, 255);
			SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x,
			                                      y + rule_h - DS_RULE,
			                                      content_w, DS_RULE });
			continue;
		}

		/* A placeholder row still highlights - it is a real place on the list -
		 * and stays one step quieter than a working row, which is the whole
		 * signal that it does nothing yet. One step, though, not two: most of
		 * this list is placeholders, and ranking them against an unselected
		 * working row as well left the entire menu reading as grayed out. */
		/* `w` is how selected this row is RIGHT NOW, not whether it is the
		 * one the cursor points at: 1 once the plate has arrived, 0 before it
		 * sets off, and the two rows crossfade past each other in between. */
		{
			/* How much of the plate is over this row, 0 to 1. */
			float top = y > plate_y ? (float)y : plate_y;
			float bot = (float)(y + ROW_H(rows[i])) < plate_y + plate_h
			            ? (float)(y + ROW_H(rows[i])) : plate_y + plate_h;
			float w = (bot - top) / (float)ROW_H(rows[i]);

			if (w < 0.0f) w = 0.0f;
			if (w > 1.0f) w = 1.0f;

			/* Off the fill a working row is **text-soft** and a placeholder
			 * one step quieter, and a message is **text-soft** either way;
			 * on the fill every label is **on-accent**, whole (DESIGN.md
			 * 11.3: the focused row's label and value are on-accent, and the
			 * mock draws a focused placeholder no quieter). */
			lc = col_mix(rows[i].live || (vcolors && vcolors[i] == VCOL_MESSAGE)
			             ? UI_TEXT_SOFT : UI_TEXT_DIM, on, w);
			sel_w = w;
		}
		/* A value is **text-dim** off the fill, or **accent-ink** for an
		 * earned achievement, and on the fill it is **on-accent**, whatever
		 * color it had: accent-ink on lime is a darker lime on lime, which
		 * says nothing. It used to take the panel's accent as the plate
		 * arrived, and a system's red on the lime would be no better. */
		vc = col_mix(vcolors && vcolors[i] == VCOL_EARNED
		             ? ui_col(thm->accent_ink, 255) : UI_TEXT_DIM,
		             on_quiet, sel_w);
		/* a tab's rows fade up as they slide in */
		if (tab_moving) {
			lc.a = (Uint8)(lc.a * tab_p);
			vc.a = (Uint8)(vc.a * tab_p);
		}

		/* Trimmed to the panel, HERE, rather than by every caller.
		 *
		 * content_w is capped to the screen above, but nothing used to fit
		 * the text to it - the label was drawn from the left edge and the
		 * value right-aligned to the right, so a row wider than the cap drew
		 * them straight through each other and out past the border. Seen on
		 * the game info screen: "File" and "Alex Kidd in Miracle World
		 * (World) (Sega Ages).zip" overlapping in the middle of the panel.
		 *
		 * Callers were each remembering to call ui_fit_text, or forgetting.
		 * A rule about how text fits a panel belongs in the thing that draws
		 * the panel, where a screen written next year gets it for free.
		 *
		 * The VALUE yields first: labels are short and fixed ("File", "Size"),
		 * values are whatever the card happens to hold. */
		if (keyhints_is(rows[i].label)) {
			/* Button hints: a glyph and a word each, centered */
			keyhints_draw_packed(a->r, rows[i].label, content_x, content_x + content_w,
			                     y + ROW_H(rows[i]) / 2, lc.a);
		} else if (ROW_IS_NOTE(rows[i])) {
			char note[192];

			/* A note that fits is centered, which is what a caption wants.
			 * One that does not SCROLLS rather than being cut: a note is a
			 * sentence, and Play Time's footer lost the last of four facts
			 * to an ellipsis - "longest 56m 30s · t..." - where the reader
			 * cannot even tell which word went. Same ping-pong and the same
			 * faded edges a long title gets on the shelf.
			 *
			 * Left-aligned in that case, not centered: the text is wider
			 * than the box, so centering it has nothing left to center.
			 *
			 * A body row is left-aligned whether it fits or not. It is one
			 * line of a paragraph, and a paragraph needs one left edge. */
			if (ui_text_width(fm, rows[i].label) <= content_w) {
				bool body = ROW_IS_BODY(rows[i]);

				ui_fit_text(fm, rows[i].label, note, sizeof note, content_w);
				ui_text(a->r, fm, note, body ? content_x : row_cx, ty,
				        body ? -1 : 0, lc);
			} else {
				int slot = MQ_NOTE0 + (notes_drawn < MQ_NOTES - 1
				                       ? notes_drawn : MQ_NOTES - 1);

				menu_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w, mq_phase(slot, sel, i), lc);
			}
			notes_drawn++;
		} else if (two_col) {
			char lbl[192], val[192];
			/* g_row_icon's square, three quarters of the row, and the row
			 * drawn after it as if it started there; put back below */
			int icon_x0 = content_x, icon_w0 = content_w;

			if (g_row_icon) {
				int isz = row_h * 3 / 4;
				SDL_Texture *it = g_row_icon(i);

				if (it) SDL_RenderCopy(a->r, it, NULL, &(SDL_Rect){
					content_x, y + (ROW_H(rows[i]) - isz) / 2, isz, isz });
				content_x += isz + pad / 2;
				content_w -= isz + pad / 2;
			}
			int lw = ui_text_width(fm, rows[i].label);
			int vw = menu_value_w(fm, rows[i].value);
			int room;

			/* WHICHEVER IS LONGER IS THE CONTENT, and the content is what
			 * moves. The other one is a caption, and a caption keeps its
			 * natural width.
			 *
			 * On the play-time list the label is a game name and the value a
			 * duration, so the name scrolls and the number stays put; a
			 * duration pushed off the row it exists to report is no use to
			 * anyone. On the game info screen it is the other way round -
			 * "File" is the caption and the filename is the content - and
			 * the same rule sends the filename to the marquee without
			 * needing to know which screen it is on.
			 *
			 * The condition used to ask whether the label fit in what the
			 * value left, which answers a different question: with a long
			 * value that subtraction goes negative, so a two-letter label
			 * failed it and took the long-LABEL path. The filename was then
			 * drawn ellipsized across the full width with "File" printed on
			 * top of it, which is what sent me looking.
			 *
			 * Only the selected row moves, OR one the cursor can never reach
			 * - menu_run_body steps off any row whose `live` is false, so on
			 * a screen of facts, waiting for selection waits forever. Rows
			 * long enough to move are rare either way, and a page where
			 * everything is moving will not sit still to be read. */
			/* See menu_row_moves. The rule lives in menu.c because the
			 * last time it quietly meant something else, nothing could see
			 * it: the achievement list scrolled a title or cut it depending
			 * on whether the player had earned it. */
			bool moves = menu_row_moves(rows[i], i == sel, visits_all);

			/* X's glyph where the value goes, on the fill as a selected
			 * key's (UI_BUTTON_ON_ACCENT) once the plate is mostly there,
			 * and the label given what is left */
			if (rows[i].value == MENU_KEY_Y_ON || rows[i].value == MENU_KEY_Y_OFF) {
				/* Y's glyph at the right, as X's is, and the state before it */
				int bw = ui_button_width("Y", MENU_KEY_SIZE);

				ui_button(a->r, "Y", content_x + content_w - bw / 2,
				          y + ROW_H(rows[i]) / 2, MENU_KEY_SIZE,
				          sel_w >= 0.5f ? UI_BUTTON_ON_ACCENT : UI_BUTTON_PLAIN);
				ui_text(a->r, fm, T(rows[i].value == MENU_KEY_Y_ON ? S_MENU_ON_TITLE : S_MENU_OFF_TITLE),
				        content_x + content_w - bw - MENU_KEY_GAP, ty, 1, vc);
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w - vw - gap);
				ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
			} else if (rows[i].value == MENU_KEY_X) {
				ui_button(a->r, "X", content_x + content_w - vw / 2,
				          y + ROW_H(rows[i]) / 2, MENU_KEY_SIZE,
				          sel_w >= 0.5f ? UI_BUTTON_ON_ACCENT : UI_BUTTON_PLAIN);
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w - vw - gap);
				ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
			} else if (lw + (vw ? fit_gap + vw : 0) <= content_w) {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				if (rows[i].value)
					ui_text(a->r, fm, rows[i].value,
					        content_x + content_w, ty, 1, vc);
				/* The screen's mark before the value (menu_style's
				 * row_mark): Theme's dot and chips */
				if (g_row_mark && rows[i].value && rows[i].value != MENU_TICK)
					g_row_mark(a, &rows[i], content_x + content_w - vw - DS_GRID * 2,
					           y + ROW_H(rows[i]) / 2, sel_w);
				/* Both fit, but by how much against the most the row can
				 * have: the device measures a little wider */
				if (rows[i].value && rows[i].value != MENU_TICK)
					ui_fit_tight(rows[i].value, lw + fit_gap + vw, content_w + (cap_w - icon_w0), cap_w);
				/* The choice that is on: a tick, a capital's height and a
				 * fifth, where the value's right edge is */
				if (rows[i].value == MENU_TICK) {
					int ts = ui_cap_height(fm) * 6 / 5;

					ui_glyph_draw(a->r, UI_GLYPH_CHECK, content_x + content_w - ts / 2,
					              y + ROW_H(rows[i]) / 2, ts, vc);
				}
			} else if (vw > lw) {
				/* The VALUE is the long one. The label keeps its width and
				 * the value takes everything left of the right edge. */
				ui_text(a->r, fm, rows[i].label, content_x, ty, -1, lc);
				room = content_w - lw - fit_gap;
				if (room <= 0) {
					/* No room for it at all; the label alone is the row. */
				} else if (moves) {
					/* One slot for every long value on the screen, never one
					 * per row: keyed per row, a screen of facts with two of
					 * them reset the clock twice a frame and neither ever
					 * moved. Wi-Fi File Transfer showed it 2026-09-14 - "Transferred"
					 * and "Now" both too long, and two captures a second apart
					 * identical. Sharing the clock, they all move.
					 *
					 * WHICH clock depends on why this row moves, not on what
					 * is in it. See MQ_FIXED. */
					menu_marquee(a->r, fm, rows[i].value,
					                content_x + content_w - room, ty, room,
					                mq_row_phase(i == sel, heading, n, sel), vc);
				} else {
					ui_fit_text(fm, rows[i].value, val, sizeof val, room);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
			} else {
				/* The LABEL is the long one: a game's name, an SSID. */
				room = content_w - (vw ? vw + fit_gap : 0);
				if (rows[i].value) {
					ui_fit_text(fm, rows[i].value, val, sizeof val, content_w);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
				if (room <= 0) {
					/* Nothing left for a name. Better a cut label than none. */
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				} else if (moves) {
					menu_marquee(a->r, fm, rows[i].label, content_x, ty,
					                room,
					                mq_row_phase(i == sel, heading, n, sel), lc);
				} else {
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, room);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				}
			}
			content_x = icon_x0;
			content_w = icon_w0;
		} else {
			char lbl[192];

			/* A centered list's row, under the two-column rule: the row that
			 * moves slides when it does not fit, rather than losing its end
			 * to an ellipsis. Left-aligned while it slides, the way a long
			 * note is - wider than the box, it has nothing to be centered in.
			 *
			 * This branch only ever cut. Found on Muse's track list
			 * 2026-09-19: "Bird Dream Of The Olympus Mons" ended in "M..."
			 * - and slid the moment a track of that album was playing,
			 * because the "playing" value put the list in two columns. */
			if (ui_text_width(fm, rows[i].label) > content_w &&
			    menu_row_moves(rows[i], i == sel, visits_all)) {
				menu_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w,
				                mq_row_phase(i == sel, heading, n, sel), lc);
			} else {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, row_cx, ty, 0, lc);
			}
		}
	}
	if (vis < n || tab_moving) SDL_RenderSetClipRect(a->r, NULL);
	content_x -= tab_dx;
	content_x -= side_w;
	content_w += side_w;
	cap_w += side_w;

	if (scroll_h) {
		/* Faded where the text runs out of viewport, and faded by laying the
		 * PANEL'S OWN FILL over it, which is the opposite of what the shelf
		 * does. There the background is a coverflow with a vignette, so a
		 * gradient painted on top would show as a band and the glyphs have to
		 * be faded instead. Here the surface under the text is one flat color,
		 * ui_panel's **surface**, so the cheap way is also the right one - as
		 * long as it is the surface of the theme that is on: the old panel's
		 * own color, kept here, was a dark band across a Beard panel.
		 *
		 * A side fades only while something is hidden on it, so a card sitting
		 * at the top of its travel has a hard top edge and a soft bottom one,
		 * and says by that which way it is about to move. */
		const int fade = row_h;
		int e;

		SDL_RenderSetClipRect(a->r, NULL);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		for (e = 0; e < fade; e++) {
			Uint8 al = (Uint8)(252 - 252 * e / fade);

			SDL_SetRenderDrawColor(a->r, thm->surface.r, thm->surface.g,
			                       thm->surface.b, al);
			if (scroll > 0)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
			if (scroll < scroll_span)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + scroll_h - 1 - e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
		}
		SDL_RenderSetClipRect(a->r, NULL);
	}

	/* A small triangle in **text-dim** where the list carries on (DESIGN.md
	 * 11.3). Hung just off the rows rather than centered in the padding: with
	 * a heading above, the padding is already spoken for by the separator, and
	 * the indicator belongs to the list in any case. */
	if (vis < n && !scroll_h) {
		/* It points the way the list continues, which three dots could not
		 * say. Drawn as rows because there is no filled-triangle primitive
		 * and this needs no texture. */
		const int tw = row_h / 3, th = tw / 2, off = 8;

		SDL_SetRenderDrawColor(a->r, thm->text_dim.r, thm->text_dim.g,
		                       thm->text_dim.b, 255);
		for (k = 0; k < th; k++) {
			int up_w   = tw * (k + 1) / th;      /* apex at the top */
			int down_w = tw * (th - k) / th;     /* apex at the bottom */

			/* a list that goes round carries on both ways, always */
			if (first > 0 || loop)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - up_w / 2,
				                   content_y - off - th + k, up_w, 1 });
			if (first + vis < n || loop)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - down_w / 2,
				                   content_y + vis * row_h + off + k, down_w, 1 });
		}
	}
	if (posed) {
		panel_pass_end(a, &panel, pose);
		/* and a picture that holds still, over it at its place at rest */
		menu_side_call(a, &panel, content_x, content_y, rows, n, sel, MENU_SIDE_OVER);
	}
}
