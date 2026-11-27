/* SPDX-License-Identifier: GPL-3.0-only */
/* See hotkeys_screen.h. The Hotkeys list, its capture screen and its swap
 * question; the rules are src/hotkeys.c's and what is kept src/hkstore.c's. */
#include "hotkeys_screen.h"

#include <stdio.h>
#include <string.h>

#include "aout_route.h"
#include "controls.h"
#include "design.h"
#include "device/profile.h"
#include "gamedel_screen.h"
#include "hkstore.h"
#include "home.h"
#include "hotkeys.h"
#include "menukeys_screen.h"
#include "i18n.h"
#include "menu.h"
#include "platform.h"
#include "storage_screen.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

/* The rows that are no action (the actions are 0 to HK_ACTS - 1); their
 * numbers are the shots' (--hotkeys), so a row gone keeps its number free */
enum { HKR_SCOPE = 100, HKR_HOME, HKR_FOOT_RULE, HKR_RESET, HKR_ALL,
       /* MENU's chords on every screen, MENU > System's alone (src/menukeys.h) */
       HKR_MK_RULE, HKR_FOOT, HKR_MK_SHOT, HKR_MK_LOCK, HKR_MK_HOLD };
#define HKR_MAX (HK_ACTS + 11)

/* The capture screen's glyphs, as the keyboard's big keys: 11.9's 112 */
#define HK_CAPTURE_GLYPH 112
/* ...and the room for them, a glyph and the grid's eight above and below */
#define HK_CAPTURE_H     (HK_CAPTURE_GLYPH + DS_GRID * 8)

/* Fast Forward's speed, the row under it (HKR_MAX had a row spare for it) */
enum { HKR_FF_SPEED = 120 };

typedef struct {
	app              *a;
	const system_cfg *s;          /* NULL: MENU > System's, Every Game */
	const char       *file, *title;
	bool              running;
	hks_scope         scope;
	/* the screen this opened over, and its context (a game's Controls tab) */
	const menu_style *over;
	void             *over_ctx;
	menu_style        st;         /* this list's, and its own screens' */
	int               ids[HKR_MAX];
	int               n;
	unsigned          vcol[HKR_MAX];
	unsigned char     sound[HKR_MAX];
	char              ff_val[16];
	char              val[HK_ACTS][CTL_KEYS_MAX];
	char              home[CTL_KEYS_MAX];
	char              scope_val[CFG_STR];
	char              mk_val[MK_ACTS][CTL_KEYS_MAX];
	char              hold_val[24];
	/* The footer's line, for the group the cursor is in (foot_for) */
	char              foot[160];
} hk_page;

static const str_id ACT_LABEL[HK_ACTS] = {
	S_HOTKEYS_QUICK_SAVE, S_HOTKEYS_QUICK_LOAD, S_CONTROLS_FASTER, S_CONTROLS_SLOWER,
	S_HOTKEYS_SHOT, S_HOTKEYS_SHADER, S_HOTKEYS_DISPLAY, S_HOTKEYS_RESET, S_GAMEMENU_QUIT,
	S_HOTKEYS_FFWD
};

/* The handheld's buttons a key is, as the capture screen reads them */
static const in_button KEY_IN[HK_KEYS] = {
	IN_ACCEPT, IN_BACK, IN_X, IN_Y, IN_L1, IN_R1, IN_L2, IN_R2, IN_START
};

/* ---- what is shown --------------------------------------------------------- */

void speed_said(int half, char *out, size_t n)
{
	if (half <= 0)    snprintf(out, n, "%s", T(S_POPUP_SPEED_MAX));
	else if (half % 2) snprintf(out, n, T(S_POPUP_SPEED_HALF), half / 2);
	else              snprintf(out, n, T(S_POPUP_SPEED_WHOLE), half / 2);
}

static void shown(const hk_page *p, hk_table *t, hk_table *back, bool *own)
{
	const char *tag = p->s ? p->s->tag : NULL;
	bool o = hks_get(p->scope, tag, p->file, t);

	hks_fallback(p->scope, tag, p->file, back);
	if (own) *own = o;
}

/* What the game under the list plays with now, sent at once while it is
 * paused there: the table in force for it (src/hkstore.h), whatever scope
 * the list shows */
static void send(const hk_page *p)
{
	hk_table t;
	char run[200];

	if (!p->running || !p->s) return;
	hks_get(HKS_GAME, p->s->tag, p->file, &t);
	hk_run(&t, run, sizeof run);
	fprintf(stderr, "hotkeys: %s %s ffspeed=%s\n", p->s->tag, run, hk_ff_word(t.ff));
	plat_resident_line("SETHOTKEYS\tkeys=%s\tffspeed=%s", run, hk_ff_word(t.ff));
}

static const char *scope_said(hk_page *p)
{
	if (p->scope == HKS_GAME) return T(S_GAMEMENU_THIS_GAME);
	if (p->scope == HKS_SYSTEM && p->s) {
		snprintf(p->scope_val, sizeof p->scope_val, "%s", home_label(p->s->tag, p->s->name));
		return p->scope_val;
	}
	return T(S_HOTKEYS_EVERY_GAME);
}

/* The footer under the list says what the row under the cursor is, or
 * nothing where there is nothing to say (the Pixel 2 test of 1.1.1-test.3:
 * it said "Home Screen is fixed" on every row, where Home is a quiet row
 * the cursor never lands on). The game's hotkeys work in a game, with
 * SELECT held; MENU's chords on every screen, in a game too; Hold to
 * Unlock is how long SELECT and START open the lock; Applies To, Reset
 * Hotkeys and All Controls say what they are themselves. One line under a
 * rule, pinned under the rows as an option list's note is, the same
 * height whatever it says (an empty line too), so the panel holds still
 * as the cursor moves. (2026-11-26, the test of 1.1.1-test.2: the quiet
 * lines between the rows read as observations among them, so they went
 * under the list.) */
static str_id foot_id(int id)
{
	if (id >= 0 && id < HK_ACTS) return S_HOTKEYS_GAME_NOTE;
	if (id == HKR_MK_SHOT || id == HKR_MK_LOCK) return S_HOTKEYS_MENU_NOTE;
	if (id == HKR_MK_HOLD) return S_HOTKEYS_HOLD_NOTE;
	return S_COUNT;
}

static void foot_for(hk_page *p, int id)
{
	const str_id f = foot_id(id);

	snprintf(p->foot, sizeof p->foot, "%s", f < S_COUNT ? T(f) : "");
	/* Said as it changes, for check-menukeys: the line each row has */
	{
		static char said[160] = "-";

		if (strcmp(said, p->foot)) {
			snprintf(said, sizeof said, "%s", p->foot);
			fprintf(stderr, "hotkeys: the footer says '%s'\n", p->foot);
		}
	}
}

/* The cursor's row each pass, before the frame: the footer follows it in
 * the frame it lands (the rows point at p->foot) */
static void at_row(app *a, void *ctx, int sel)
{
	hk_page *p = ctx;

	(void)a;
	if (sel >= 0 && sel < p->n) foot_for(p, p->ids[sel]);
}

static int build(void *ctx, menu_row *rows, int max, const char **heading)
{
	hk_page *p = ctx;
	hk_table t, back;
	const char *home = dev_role_keys(&plat_profile()->chords, DEV_ROLE_HOME);
	bool own;
	int n = 0, j;

#define ROW(id_, r_, c_, snd_) do { if (n < max && n < HKR_MAX) { p->ids[n] = (id_); rows[n] = (r_); \
	p->vcol[n] = (c_); p->sound[n] = (snd_); n++; } } while (0)
	shown(p, &t, &back, &own);
	*heading = ctl_page_name(CTL_HOTKEYS);
	/* Applies To, in a game: Every Game, the system, This Game, round; its
	 * value in accent-ink when the scope shown has hotkeys of its own */
	if (p->s)
		ROW(HKR_SCOPE, ((menu_row){ T(S_GAMEMENU_APPLIES_TO), scope_said(p), true }),
		    own && p->scope != HKS_EVERY ? 1u : 0u, MENU_SND_RING);
	for (j = 0; j < HK_ACTS; j++) {
		const hk_act i = hk_order[j];
		char chord[24];

		hk_chord(&t, i, chord, sizeof chord);
		if (chord[0]) snprintf(p->val[i], sizeof p->val[i], "%s%s", MENU_KEYS_MARK, chord);
		else snprintf(p->val[i], sizeof p->val[i], "%s", T(S_HOTKEYS_NONE));
		ROW(i, ((menu_row){ T(ACT_LABEL[i]), p->val[i], true }),
		    t.key[i] != back.key[i] ? 1u : 0u, 0);
		/* Fast Forward's speed under it, round on Left and Right as
		 * Applies To: 1.5x, 2x, 3x, Max, as the speed pop-up says them */
		if (i == HK_FFWD) {
			speed_said(hk_ff_half(t.ff), p->ff_val, sizeof p->ff_val);
			ROW(HKR_FF_SPEED, ((menu_row){ T(S_HOTKEYS_FF_SPEED), p->ff_val, true }),
			    t.ff != back.ff ? 1u : 0u, MENU_SND_RING);
		}
	}
	/* Home, the way out of anything, the same on every card: fixed, a quiet
	 * row the cursor passes */
	if (home) {
		snprintf(p->home, sizeof p->home, "%s%s", MENU_KEYS_MARK, home);
		ROW(HKR_HOME, ((menu_row){ T(S_CONTROLS_HOME), p->home, false }), 0u, 0);
	}
	ROW(HKR_RESET, ((menu_row){ T(S_HOTKEYS_RESET_ALL), NULL, own }), 0u, own ? 0 : MENU_SND_NO_A);
	if (p->s) ROW(HKR_ALL, ((menu_row){ T(S_HOTKEYS_ALL_CONTROLS), NULL, true }), 0u, 0);
	/* MENU held with a button, on every screen and in a game (2026-11-22):
	 * a screenshot, the lock, and how long SELECT+START is held to open it.
	 * The launcher's own, so only on MENU > System's list, not a game's */
	if (!p->s) {
		const mk_table *m = mk_now();
		mk_table def;
		int i;

		mk_default(&def);
		ROW(HKR_MK_RULE, MENU_RULE, 0u, 0);
		for (i = 0; i < MK_ACTS; i++) {
			char chord[24];

			mk_chord(m, (mk_act)i, chord, sizeof chord);
			snprintf(p->mk_val[i], sizeof p->mk_val[i], "%s%s", MENU_KEYS_MARK, chord);
		}
		ROW(HKR_MK_SHOT, ((menu_row){ T(S_HOTKEYS_SHOT), p->mk_val[MK_SHOT], true }),
		    m->key[MK_SHOT] != def.key[MK_SHOT] ? 1u : 0u, 0);
		ROW(HKR_MK_LOCK, ((menu_row){ T(S_SYSMENU_MP4_LOCK), p->mk_val[MK_LOCK], true }),
		    m->key[MK_LOCK] != def.key[MK_LOCK] ? 1u : 0u, 0);
		{
			/* "0.5 s" or "0,5 s", the language's decimal mark */
			char secs[12];

			mk_secs(m->hold_ms, i18n_decimal_comma(), secs, sizeof secs);
			snprintf(p->hold_val, sizeof p->hold_val, T(S_HOTKEYS_UNLOCK_SECS), secs);
		}
		ROW(HKR_MK_HOLD, ((menu_row){ T(S_HOTKEYS_UNLOCK_HOLD), p->hold_val, true }),
		    m->hold_ms != def.hold_ms ? 1u : 0u, MENU_SND_RING);
	}
	ROW(HKR_FOOT_RULE, MENU_RULE, 0u, 0);
	ROW(HKR_FOOT, MENU_NOTE(p->foot), 0u, 0);
#undef ROW
	p->n = n;
	return n;
}

/* ---- the screens it opens -------------------------------------------------- */

static void backdrop(app *a, void *ctx, float k)
{
	hk_page *p = ctx;

	if (p->over && p->over->backdrop) {
		p->over->backdrop(a, p->over_ctx, k);
		return;
	}
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

static menu_result power(app *a, void *ctx)
{
	hk_page *p = ctx;

	if (p->over && p->over->on_power) return p->over->on_power(a, p->over_ctx);
	power_off(a);
	return MENU_DONE;
}

/* The capture screen's keys, for its header (menu_style.header has no
 * context): SELECT (or MENU) while held, and the button with it. Kept as
 * they were when the capture ends, until the next one starts: the panel's
 * copy draws the header again as it leaves (snap_draw), and a MENU chord's
 * glyph turned into SELECT's in that leave when `menu` was cleared at the
 * end (the Pixel 2 test of 1.1.1-test.3) */
static struct { bool sel; int key; bool menu; } g_cap;

static void capture_header(app *a, const SDL_Rect *r)
{
	const int cx = r->x + r->w / 2, cy = r->y + r->h / 2;
	char keys[24];

	if (!g_cap.sel) {
		TTF_Font *f = ui_font(UI_F_LABEL);

		ui_text(a->r, f, T(S_HOTKEYS_CAPTURE_WAIT), cx, ui_cap_y(f, cy), 0, UI_TEXT_DIM);
		return;
	}
	if (g_cap.key >= 0) snprintf(keys, sizeof keys, "%s+%s", g_cap.menu ? "MENU" : "SELECT", hk_key_name(g_cap.key));
	else snprintf(keys, sizeof keys, "%s", g_cap.menu ? "MENU" : "SELECT");
	/* Said when it changes, for check-menukeys: the keys never turn into
	 * another's, the leave included */
	{
		static char said[24];

		if (strcmp(said, keys)) {
			snprintf(said, sizeof said, "%s", keys);
			fprintf(stderr, "hotkeys: the capture shows %s\n", keys);
		}
	}
	/* lit as they go down, the tutorial's pressed look */
	ui_button_pressed(a->r, keys, cx, cy, HK_CAPTURE_GLYPH, UI_BUTTON_PLAIN, 1.0f, 1.0f, 1.0f);
}

static void capture_draw_as(app *a, hk_page *p, const char *heading, float k)
{
	menu_row rows[2];
	menu_style st = p->st;
	static char hint[128];

	/* MENU's as the handheld prints it: FN on the R36S */
	if (g_cap.menu) snprintf(hint, sizeof hint, T(S_HOTKEYS_MENU_CAPTURE_HINT), dev_menu_word(plat_profile()));
	rows[0] = MENU_NOTE(g_cap.menu ? hint : T(S_HOTKEYS_CAPTURE_HINT));
	rows[1] = MENU_NOTE(T(g_cap.key >= 0 ? S_HOTKEYS_CAPTURE_KEEP : S_HOTKEYS_CAPTURE_BACK));
	/* the widest panel, the lab's 800, so the line under the keys stays
	 * whole in every language */
	st.fixed_w = menu_most_w();
	st.header = capture_header;
	st.header_h = HK_CAPTURE_H;
	st.sound = NULL;
	st.vcolors = NULL;
	st.at = NULL;
	st.pin_foot = false;
	backdrop(a, p, k);
	menu_draw_styled(a, &st, heading, rows, 2, -1);
}

static void capture_draw(app *a, hk_page *p, hk_act act, float k)
{
	capture_draw_as(a, p, T(ACT_LABEL[act]), k);
}

/* A MENU chord's row's name: Screenshot or Lock Screen */
static const char *mk_label(mk_act m)
{
	return T(m == MK_SHOT ? S_HOTKEYS_SHOT : S_SYSMENU_MP4_LOCK);
}

/* A on Screenshot or Lock Screen under MENU's rule: MENU held, then a
 * button, taken by the platform as a chord would be (mk_capture) so no
 * screenshot or lock comes of it; MENU let go keeps it. MENU tapped alone
 * or B with nothing held leaves with no change. The key (HK_*) or -1. */
static int capture_menu(app *a, hk_page *p, mk_act m)
{
	int lv = panel_open(), key = -1;
	bool done = false;
	float k;

	g_cap.sel = false;
	g_cap.key = -1;
	g_cap.menu = true;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	mk_capture(true);
	while (!done && !want_quit && a->running) {
		bool held;

		plat_input_poll(&a->in);
		ui_sounds_follow();
		held = plat_menu_held() || a->in.down[IN_MENU];
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power(a, p); break; }
		if (mk_caught() >= 0 && mk_caught() != g_cap.key) {
			g_cap.key = mk_caught();
			uisound_play(DS_SOUND_ACCEPT);
		}
		if (held && !g_cap.sel) {
			g_cap.sel = true;
			uisound_play(DS_SOUND_MOVE);
		}
		if (!held && g_cap.sel && g_cap.key >= 0) {
			key = g_cap.key;
			done = true;
		} else if (a->in.pressed[IN_MENU]) {
			break;                      /* MENU alone: no change */
		} else if (!g_cap.sel && a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		capture_draw_as(a, p, mk_label(m), k);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	mk_capture(false);
	/* g_cap stays MENU's: the panel leaving draws it again */
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return key;
}

/* Screenshot or Lock Screen given `key`: the other's already, the swap
 * question first, as a game's hotkeys ask it */
/* The swap question's words for a MENU chord, as a game's hotkeys say it
 * (CQ_HOTKEYS_SWAP): MENU+L2 to this one? the other has it and moves to
 * this one's keys */
static void mk_swap_text(const mk_table *t, mk_act m, int key, int other, char *q, size_t n)
{
	char chord[24], was[24];

	/* the keys in words, MENU as the handheld prints it (FN on the R36S) */
	snprintf(chord, sizeof chord, "%s+%s", dev_menu_word(plat_profile()), hk_key_name(key));
	if (hk_key_name(t->key[m])) snprintf(was, sizeof was, "%s+%s", dev_menu_word(plat_profile()), hk_key_name(t->key[m]));
	else was[0] = '\0';
	cq_text(CQ_HOTKEYS_SWAP, q, n, chord, mk_label(m), mk_label((mk_act)other), was);
}

static void mk_take_key(app *a, hk_page *p, mk_act m, int key)
{
	mk_table t = *mk_now();
	int other = mk_act_on(&t, key);

	if (t.key[m] == key) return;
	if (other >= 0) {
		char q[320];

		mk_swap_text(&t, m, key, other, q, sizeof q);
		if (!confirm_ask_over(a, CQ_HOTKEYS_SWAP, q, &p->st, p)) return;
	}
	mk_swap(&t, m, key);
	mk_put(&t);
}

/* A on an action's row: every button read as a key until SELECT is let go,
 * A and B too. The key with SELECT (HK_*), or -1 for none: B with nothing
 * held, MENU, POWER */
static int capture(app *a, hk_page *p, hk_act act)
{
	int lv = panel_open(), key = -1, i;
	bool done = false;
	float k;

	g_cap.sel = false;
	g_cap.key = -1;
	g_cap.menu = false;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!done && !want_quit && a->running) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power(a, p); break; }
		if (a->in.pressed[IN_MENU]) break;
		if (!g_cap.sel) {
			if (a->in.down[IN_SELECT]) {
				g_cap.sel = true;
				uisound_play(DS_SOUND_MOVE);
			} else if (a->in.pressed[IN_BACK]) {
				uisound_play(DS_SOUND_BACK);
				break;
			}
		} else {
			for (i = 0; i < HK_KEYS; i++)
				if (a->in.pressed[KEY_IN[i]]) {
					g_cap.key = i;
					uisound_play(DS_SOUND_ACCEPT);
				}
			if (!a->in.down[IN_SELECT]) {
				/* let go: kept, or SELECT alone and back to waiting */
				if (g_cap.key >= 0) {
					key = g_cap.key;
					done = true;
				} else {
					g_cap.sel = false;
				}
			}
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		capture_draw(a, p, act, k);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return key;
}

/* The swap question: `key` is another action's; true for Swap */
static bool ask_swap(app *a, hk_page *p, const hk_table *t, hk_act act, int key, int other)
{
	char q[320], chord[24], was[24];

	snprintf(chord, sizeof chord, "SELECT+%s", hk_key_name(key));
	hk_chord(t, act, was, sizeof was);
	cq_text(CQ_HOTKEYS_SWAP, q, sizeof q, chord, T(ACT_LABEL[act]), T(ACT_LABEL[other]),
	        was[0] ? was : T(S_HOTKEYS_NONE));
	return confirm_ask_over(a, CQ_HOTKEYS_SWAP, q, &p->st, p);
}

static void reset_ask(app *a, hk_page *p)
{
	char q[CFG_STR * 3];
	cq_id which = p->scope == HKS_GAME && p->s ? CQ_HOTKEYS_RESET_GAME
	            : p->scope == HKS_SYSTEM && p->s ? CQ_HOTKEYS_RESET_SYSTEM : CQ_HOTKEYS_RESET_EVERY;

	cq_text(which, q, sizeof q, p->s ? p->s->name : "", p->title ? p->title : "");
	if (!confirm_ask_over(a, which, q, &p->st, p)) return;
	hks_reset(p->scope, p->s ? p->s->tag : NULL, p->file);
	send(p);
}

static menu_result key(app *a, void *ctx, in_button k, int sel)
{
	hk_page *p = ctx;
	const int id = sel >= 0 && sel < p->n ? p->ids[sel] : -1;
	const bool side = k == IN_LEFT || k == IN_RIGHT;

	if (id < 0 || (!side && k != IN_ACCEPT)) return MENU_STAY;
	if (id == HKR_MK_HOLD) {
		mk_table t = *mk_now();

		t.hold_ms = mk_hold_step(t.hold_ms, k == IN_LEFT ? -1 : 1);
		mk_put(&t);
		return MENU_STAY;
	}
	if (!side && (id == HKR_MK_SHOT || id == HKR_MK_LOCK)) {
		const mk_act m = id == HKR_MK_SHOT ? MK_SHOT : MK_LOCK;
		int got = capture_menu(a, p, m);

		if (got >= 0 && a->running) mk_take_key(a, p, m, got);
		return MENU_STAY;
	}
	if (id == HKR_SCOPE) {
		const int d = k == IN_LEFT ? -1 : 1;

		p->scope = (hks_scope)((p->scope + 3 + d) % 3);
		return MENU_STAY;
	}
	if (id == HKR_FF_SPEED) {
		hk_table t, back;

		shown(p, &t, &back, NULL);
		t.ff = (signed char)((t.ff + HK_FF_STEPS + (k == IN_LEFT ? -1 : 1)) % HK_FF_STEPS);
		hks_put(p->scope, p->s ? p->s->tag : NULL, p->file, &t);
		send(p);
		return MENU_STAY;
	}
	if (side) return MENU_STAY;
	if (id == HKR_RESET) {
		bool own;
		hk_table t, back;

		shown(p, &t, &back, &own);
		if (own) reset_ask(a, p);
		return MENU_STAY;
	}
	if (id == HKR_ALL) {
		game_controls_pages(a, p->over, p->over_ctx);
		return MENU_STAY;
	}
	if (id >= 0 && id < HK_ACTS) {
		hk_table t, back;
		int got = capture(a, p, (hk_act)id), other;

		if (got < 0 || !a->running) return MENU_STAY;
		shown(p, &t, &back, NULL);
		if (t.key[id] == got) return MENU_STAY;
		other = hk_act_on(&t, got, id);
		/* One action a chord: a clash offers the swap, the cursor on Cancel */
		if (other >= 0) {
			if (!ask_swap(a, p, &t, (hk_act)id, got, other)) return MENU_STAY;
			hk_swap(&t, (hk_act)id, got);
		} else {
			hk_set(&t, (hk_act)id, got);
		}
		hks_put(p->scope, p->s ? p->s->tag : NULL, p->file, &t);
		send(p);
	}
	return MENU_STAY;
}

static void page_init(hk_page *p, app *a, const menu_style *over, void *ctx, const system_cfg *s,
                      const char *file, const char *title, bool running)
{
	memset(p, 0, sizeof *p);
	p->a = a;
	p->s = s;
	p->file = file;
	p->title = title;
	p->running = running;
	p->over = over;
	p->over_ctx = ctx;
	/* In a game, This Game first, as the Controls tab opens on it */
	p->scope = s ? HKS_GAME : HKS_EVERY;
	/* The widest panel, as the lab drew it: a row's name and its keys side
	 * by side, and Home's line whole, in every language */
	p->st.fixed_w = menu_most_w();
	p->st.accent = over ? over->accent : MENU_ZYPEROS;
	p->st.follow_tint = over ? over->follow_tint : false;
	p->st.backdrop = backdrop;
	p->st.on_power = power;
	p->st.sound = p->sound;
	p->st.vcolors = p->vcol;
	p->st.at = at_row;
	p->st.pin_foot = true;
}

void hotkeys_screen(app *a)
{
	hk_page p;

	page_init(&p, a, NULL, NULL, NULL, NULL, NULL, false);
	menu_run(a, &p.st, build, key, &p);
}

void hotkeys_ingame(app *a, const menu_style *over, void *ctx, const system_cfg *s,
                    const char *file, const char *title, bool running)
{
	hk_page p;

	page_init(&p, a, over, ctx, s, file, title, running);
	menu_run(a, &p.st, build, key, &p);
}

void hotkeys_shot(app *a, const system_cfg *s, const char *file, int sel, int capture_at, int k,
                  int ask, int scope)
{
	static menu_row rows[HKR_MAX];
	static hk_page p;
	const char *heading;
	int n;

	page_init(&p, a, NULL, NULL, s, file, file, false);
	if (s && scope >= 0 && scope <= HKS_GAME) p.scope = (hks_scope)scope;
	n = build(&p, rows, HKR_MAX, &heading);
	if (capture_at) {
		g_cap.sel = capture_at == 2;
		g_cap.key = capture_at == 2 ? k : -1;
		/* A MENU chord's row's capture (sel its row's id) */
		g_cap.menu = sel == HKR_MK_SHOT || sel == HKR_MK_LOCK;
		if (g_cap.menu) capture_draw_as(a, &p, mk_label(sel == HKR_MK_SHOT ? MK_SHOT : MK_LOCK), 1.0f);
		else capture_draw(a, &p, sel >= 0 && sel < HK_ACTS ? (hk_act)sel : HK_SHOT, 1.0f);
		g_cap.menu = false;
		return;
	}
	if (ask == 2 || ask == 3) {
		/* Reset Hotkeys' question for the scope shown (2), or Reset
		 * Game's over the game (3) */
		char q[CFG_STR * 3];
		cq_id which = ask == 3 ? CQ_GAME_RESET
		            : p.scope == HKS_GAME && s ? CQ_HOTKEYS_RESET_GAME
		            : p.scope == HKS_SYSTEM && s ? CQ_HOTKEYS_RESET_SYSTEM : CQ_HOTKEYS_RESET_EVERY;

		cq_text(which, q, sizeof q, s ? s->name : "", file ? file : "");
		backdrop(a, &p, 1.0f);
		confirm_preview(a, which, q);
		return;
	}
	if (ask == 1 && (sel == HKR_MK_SHOT || sel == HKR_MK_LOCK)) {
		/* A MENU chord given the other's key (--hk-key) */
		const mk_act m = sel == HKR_MK_SHOT ? MK_SHOT : MK_LOCK;
		mk_table t;
		char q[320];

		mk_default(&t);
		mk_swap_text(&t, m, t.key[m == MK_SHOT ? MK_LOCK : MK_SHOT], m == MK_SHOT ? MK_LOCK : MK_SHOT,
		             q, sizeof q);
		backdrop(a, &p, 1.0f);
		confirm_preview(a, CQ_HOTKEYS_SWAP, q);
		return;
	}
	if (ask) {
		hk_table t, back;
		char q[320], chord[24], was[24];
		int other;
		const hk_act act = sel >= 0 && sel < HK_ACTS ? (hk_act)sel : HK_SHOT;
		shown(&p, &t, &back, NULL);
		other = hk_act_on(&t, k, act);
		if (other < 0) other = HK_FASTER;
		snprintf(chord, sizeof chord, "SELECT+%s", hk_key_name(k));
		hk_chord(&t, act, was, sizeof was);
		cq_text(CQ_HOTKEYS_SWAP, q, sizeof q, chord, T(ACT_LABEL[act]), T(ACT_LABEL[other]),
		        was[0] ? was : T(S_HOTKEYS_NONE));
		backdrop(a, &p, 1.0f);
		confirm_preview(a, CQ_HOTKEYS_SWAP, q);
		return;
	}
	{
		int i = 0;

		/* the row whose id is sel's place in the list */
		for (i = 0; i < n && p.ids[i] != sel; i++) ;
		if (i >= n) i = 0;
		if (!rows[i].live) i = menu_step_sel(rows, n, i, +1);
		foot_for(&p, p.ids[i]);
		backdrop(a, &p, 1.0f);
		menu_draw_styled(a, &p.st, heading, rows, n, i);
	}
}
