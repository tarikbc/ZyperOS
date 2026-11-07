/* SPDX-License-Identifier: GPL-3.0-only */
#include "powerscreens.h"

#include "bgm.h"
#include "design.h"
#include "musec.h"
#include "place.h"
#include "status.h"
#include "theme.h"
#include "ui.h"
#include "zyperdraw.h"
#include "zypermark.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ---------- the start with a battery too low ------------------------------- */

/* The first screen of a start at BATT_EMPTY_PCT or under, off the charger
 * (DESIGN.md 11.24, src/lowbatt.h; my pick in the battery and heat lab,
 * 2026-11-06). Zyper where the send-off has him, LOWBATT_ZYPER_H tall, his head
 * cocked with the tongue in, breathing and blinking; under him the empty
 * battery mark (the status corner's, half again as large) before "Battery too
 * low" in **title**, "Plug in a charger." in **label** **text-soft**, and the
 * first start's bar (11.20) in **warning** on **track**, emptying from the
 * right over **hold-low-battery**. Then the send-off from where he stands:
 * the head straightens and he dims to the ground, as anim_poweroff's last
 * two parts. A charger meanwhile: the mark charging, "Charging.", Zyper's
 * hop and pant, the bar out over **fade-out**, and after **hold-short** the
 * start goes on. */
#define LOWBATT_ZYPER_H   230.0f
#define LOWBATT_ZYPER_CY  226.0f
#define LOWBATT_TITLE_TOP 418
#define LOWBATT_LINE_TOP  508
#define LOWBATT_BAR_TOP   640
#define LOWBATT_BAR_W     420
#define LOWBATT_MARK_K    1.5f
#define LOWBATT_MARK_GAP  22
#define LOWBATT_TITLE     T(S_LOWBATT_TITLE)
#define LOWBATT_LINE      T(S_LOWBATT_LINE)
#define LOWBATT_CHARGING  T(S_LOWBATT_CHARGING)

lowbatt_view g_lb;

/* The corner's battery mark at LOWBATT_MARK_K, its top left at (x, y), `pct`
 * full: low in **warning**, or charging. Here and on the charging screen. */
static void battery_mark_big(SDL_Renderer *r, int x, int y, int pct, bool charging,
                             bool held, int theme_ix, Uint8 alpha)
{
	static uint32_t      px[96 * 48];
	static SDL_Texture  *tex;
	static SDL_Renderer *tex_for;
	static int           drawn = -1;   /* what is painted: pct, charging, held, theme */
	status_marks m = { .wifi = STATUS_WIFI_NONE, .batt = pct, .charging = charging,
	                   .held = charging && held };
	int w, h, key = ((pct * 2 + (charging ? 1 : 0)) * 2 + (m.held ? 1 : 0)) * 2 + theme_ix;

	status_marks_size(&m, LOWBATT_MARK_K, &w, &h);
	if ((size_t)w * h > sizeof px / sizeof px[0]) return;
	if (tex_for != r || !tex) {
		if (tex) SDL_DestroyTexture(tex);
		tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		tex_for = r;
		drawn = -1;
	}
	if (drawn != key) {
		status_marks_render(px, w, h, w, LOWBATT_MARK_K, theme_ix, &m);
		SDL_UpdateTexture(tex, NULL, px, w * 4);
		drawn = key;
	}
	SDL_SetTextureAlphaMod(tex, alpha);
	SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){ x - (int)(STATUS_PAD * LOWBATT_MARK_K),
	                                          y - (int)(STATUS_PAD * LOWBATT_MARK_K), w, h });
}

/* One frame at `now`: Zyper in `p`, dimmed `dim` toward the ground, the words
 * at `k` of their opacity, the bar at `bar_k` */
void lowbatt_draw(app *a, unsigned now, const zr_pose *p, float dim, float k,
                  float bar_k)
{
	const ds_theme *t = theme();
	bool charging = g_lb.l.charged;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fl = ui_font(UI_F_LABEL);
	const int mw = (int)(STATUS_BATT_W * LOWBATT_MARK_K + 0.5f), mh = (int)(STATUS_ROW * LOWBATT_MARK_K + 0.5f);
	int tw, x0, mid;

	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(a->r);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	zyper_shadow_draw(a->r, ZYPEROS_SCREEN_W / 2.0f, LOWBATT_ZYPER_CY, LOWBATT_ZYPER_H, p->tilt, 1.0f - dim);
	draw_zyper_pose(a->r, ZYPEROS_SCREEN_W / 2.0f, LOWBATT_ZYPER_CY, LOWBATT_ZYPER_H, p,
	                zyper_brow_lift(p->tilt), dim, 255);
	if (ft && fl && k > 0.0f) {
		/* The mark and the title as one row, centered, the mark on the
		 * title's capitals */
		tw = ui_text_width(ft, LOWBATT_TITLE);
		x0 = (ZYPEROS_SCREEN_W - (mw + LOWBATT_MARK_GAP + tw)) / 2;
		mid = LOWBATT_TITLE_TOP + ui_font_line(UI_F_TITLE) * 2 / 5;
		battery_mark_big(a->r, x0, mid - mh / 2, g_lb.pct, charging, false, theme_index(),
		                 (Uint8)(255.0f * (k > 1.0f ? 1.0f : k) + 0.5f));
		ui_text(a->r, ft, LOWBATT_TITLE, x0 + mw + LOWBATT_MARK_GAP, ui_line_y(UI_F_TITLE, LOWBATT_TITLE_TOP),
		        -1, ui_fade(ui_col(t->text, 255), k));
		ui_text(a->r, fl, charging ? LOWBATT_CHARGING : LOWBATT_LINE, ZYPEROS_SCREEN_W / 2,
		        ui_line_y(UI_F_LABEL, LOWBATT_LINE_TOP), 0, ui_fade(ui_col(t->text_soft, 255), k));
	}
	if (bar_k > 0.0f) {
		const int bx = (ZYPEROS_SCREEN_W - LOWBATT_BAR_W) / 2;
		int fill = (int)(LOWBATT_BAR_W * (1.0f - lowbatt_spent(&g_lb.l, now)) + 0.5f);

		ui_round_rect(a->r, &(SDL_Rect){ bx, LOWBATT_BAR_TOP, LOWBATT_BAR_W, DS_BAR }, DS_BAR / 2,
		              ui_fade(ui_col(t->track, 255), bar_k));
		if (fill >= DS_BAR)
			ui_round_rect(a->r, &(SDL_Rect){ bx, LOWBATT_BAR_TOP, fill, DS_BAR }, DS_BAR / 2,
			              ui_fade(ui_col(t->warning, 255), bar_k));
	}
}

/* The send-off from where he stands (power_off_with): the head straight on
 * **in** over POWEROFF_HEAD with the words fading, then dimmed to the ground
 * on **out** over POWEROFF_DIM, and no clear after, as anim_poweroff */
void lowbatt_off_frame(app *a, const zr_pose *from, unsigned t, unsigned now)
{
	zr_pose p = *from;
	float dim = 0.0f, k = 1.0f;

	if (t < POWEROFF_HEAD) {
		k = ds_in((float)t / POWEROFF_HEAD);
		p.tilt = from->tilt * (1.0f - k);
		p.dy = from->dy * (1.0f - k);
	} else {
		p.tilt = 0.0f;
		p.dy = 0.0f;
		dim = ds_out((float)(t - POWEROFF_HEAD) / POWEROFF_DIM);
	}
	p.blink = 0.0f;
	p.tongue = 0.0f;
	lowbatt_draw(a, now, &p, dim, 1.0f - k, 0.0f);
}

static void lowbatt_send_off(app *a)
{
	unsigned t0 = plat_now_ms(), t;
	const zr_pose from = g_lb.pose;

	for (;;) {
		t = plat_now_ms() - t0;
		if (t > POWEROFF_HEAD + POWEROFF_DIM) t = POWEROFF_HEAD + POWEROFF_DIM;
		lowbatt_off_frame(a, &from, t, t0 + t);
		plat_present();
		if (t >= POWEROFF_HEAD + POWEROFF_DIM) break;
		ui_frame_rest(6);
	}
}

/* Before the first frame of the start, once the boot's presses are thrown
 * away (plat_input_settle: the press that switched it on is not POWER here):
 * true when the start goes on, false when the handheld powered off. The
 * reading is the corner's (battery_now), which the first frame takes anyway,
 * so a normal start pays a compare. */
bool lowbatt_gate(app *a)
{
	int pct = -1;
	bool charging = false, valid = battery_now(&pct, &charging);
	unsigned now;

	if (!lowbatt_wanted(valid, pct, charging)) return true;
	fprintf(stderr, "battery: %d%% at start, off the charger: the low-battery screen\n", pct);
	now = plat_now_ms();
	g_lb.pct = pct;
	lowbatt_start(&g_lb.l, now);
	zr_init(&g_lb.rig, now, now ^ 0x10ba77u);
	zr_fails(&g_lb.rig, now);
	g_lb.cocked_at = now;
	/* A pose before the first pass, for a POWER press on it */
	g_lb.pose = zr_pose_at(&g_lb.rig, now);
	for (;;) {
		bool empty = false, *ep = NULL;
		lb_step was = g_lb.l.step, step;

		plat_input_poll(&a->in);
		if (a->in.quit_requested || want_quit) { a->running = false; return false; }
		now = plat_now_ms();
		battery_now(NULL, &charging);
		if (lowbatt_needs_reading(&g_lb.l, now)) {
			int p2 = -1;
			bool c2 = false, ok = plat_battery(&p2, &c2);

			empty = lowbatt_wanted(ok, p2, c2);
			ep = &empty;
			if (!empty)
				fprintf(stderr, "battery: %d%% now%s; going on\n", p2, c2 ? ", charging" : "");
		}
		step = lowbatt_tick(&g_lb.l, now, a->in.pressed[IN_POWER], charging, ep);
		if (step == LB_POWER_OFF) {
			fprintf(stderr, "battery: too low to start; powering off\n");
			power_off_with(a, lowbatt_send_off);
			return false;
		}
		if (step == LB_GO_ON) {
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
			return true;
		}
		if (step == LB_CHARGING && was != LB_CHARGING) {
			fprintf(stderr, "battery: a charger at the low-battery screen; going on\n");
			zr_press(&g_lb.rig, now);
			zr_works(&g_lb.rig, now);
		}
		/* Held cocked: again before the rig's own settle comes round */
		if (step == LB_WAIT && now - g_lb.cocked_at >= DS_MS_HOLD_ZYPER_FAIL / 2) {
			zr_fails(&g_lb.rig, now);
			g_lb.cocked_at = now;
		}
		zr_tick(&g_lb.rig, now);
		g_lb.pose = zr_pose_at(&g_lb.rig, now);
		{
			float bar_k = 1.0f;

			if (g_lb.l.charged) {
				unsigned since = now - g_lb.l.charged_at;
				bar_k = since >= DS_MS_FADE_OUT ? 0.0f : 1.0f - (float)since / DS_MS_FADE_OUT;
			}
			lowbatt_draw(a, now, &g_lb.pose, 0.0f, 1.0f, bar_k);
		}
		plat_present();
		ui_frame_rest(6);
	}
}

/* ---------- the charging screen ------------------------------------------- */

/* While the handheld is off and on a charger (DESIGN.md 11.25, src/charging.h;
 * My picks in the charging lab, 2026-11-06): Zyper asleep, his eyes
 * shut, breathing slowly, with his z's going up; under him the charging
 * battery mark before the percentage in **title**; "Charging · 7.4 W · wall
 * charger" in **label** **text-soft** and "Full in about 1 h 10 min" in
 * **meta** **text-dim**; the clock in its corner; and at the foot "Hold
 * (POWER) to start" in **meta** **text-dim** with the button drawn, and the
 * lock screen's bar (11.14) filling while POWER is held. Lit for
 * **hold-charge-lit** from the last button, then dark. */
#define CHG_ZYPER_H    211.0f
#define CHG_ZYPER_CY   198.0f
#define CHG_TITLE_TOP  390
#define CHG_LINE1_TOP  506
#define CHG_LINE2_TOP  580
#define CHG_HINT_MID   680
#define CHG_BAR_TOP    720
#define CHG_BAR_W      400
#define CHG_GLYPH      52
#define CHG_BREATH_MS  4000u   /* a sleeper's breath, slower than awake */
#define CHG_Z_MS       3000u   /* each z's way up */
#define CHG_READ_MS    1000u   /* the gauge, read this often */
#define CHG_TEST_BOOT  "/tmp/zyperos_charge_boot"
#define CHG_RESTART    "/tmp/zyperos_restart"
#define CHG_TAKEN      "/tmp/zyperos_charge_taken"
#define CHG_OFF_PENDING "off-pending"   /* in P_USERDATA: launch.sh's, at a power-off */
/* After the send-off, dark, the cable still watched for this long before
 * the power-off goes on: back in by then, the screen again at once. 1.5 s
 * was shorter than a hand puts a cable back (seen on my Pixel 2) */
#define CHG_OFF_GRACE_MS 5000u

charging_view g_chg;

/* Held at the charge limit, the cable in (DESIGN.md 11.26): the plug in the
 * mark */
static bool chg_held(void)
{
	return g_chg.valid && g_chg.r.online && g_chg.r.held && g_chg.r.limit > 0 &&
	       g_chg.s.step != CS_UNPLUGGED && g_chg.s.step != CS_OFF;
}

/* And short of full: "Kept at 85%" and "Hold A for a full charge". A battery
 * charged full before the limit was set is held there too, and a full charge
 * is nothing to offer it (I saw it at 100% on a Pixel 2, 2026-11-06). */
static bool chg_kept(void)
{
	return chg_held() && !g_chg.r.full && g_chg.r.pct < 100;
}

/* Zyper asleep at `t` ms into the screen: eyes and mouth shut, at rest's
 * tilt, rising and falling with a slow breath */
static zr_pose chg_pose(unsigned t)
{
	zr_pose p = { ZYPER_TILT_DEG, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
	float ph = (float)(t % CHG_BREATH_MS) / (float)CHG_BREATH_MS;

	p.dy = -3.0f * (0.5f - 0.5f * cosf(2.0f * (float)M_PI * ph));
	return p;
}

/* His z's: three, each up its own way from his head, larger as they go,
 * fading in and out, a third of the way apart */
static void chg_zs(SDL_Renderer *r, unsigned t, float k)
{
	static const ui_font_role role[3] = { UI_F_META, UI_F_MENU, UI_F_TITLE };
	const ds_theme *th = theme();
	int i;

	for (i = 0; i < 3; i++) {
		float ph = (float)((t + (unsigned)i * CHG_Z_MS / 3u) % CHG_Z_MS) / (float)CHG_Z_MS;
		float a = sinf((float)M_PI * ph) * k;
		TTF_Font *f = ui_font(role[i]);

		if (!f || a <= 0.0f) continue;
		ui_text(r, f, i == 2 ? "Z" : "z", 628 + i * 28 + (int)(ph * 12.0f),
		        150 - i * 34 - (int)(ph * 28.0f), -1, ui_fade(ui_col(th->text_dim, 255), a));
	}
}

/* A hint with a button drawn in it: the string is its words with %s where
 * the button goes ("Hold %s to start"), so a language puts it where its
 * sentence has it. The words before it, their spaces off, into `pre`, and
 * where the words after it start into `post` (a no-break space off too:
 * French's before a colon, which the glyph's gap stands for). */
static void hint_split(const char *fmt, char *pre, size_t pn, const char **post)
{
	const char *at = strstr(fmt, "%s");
	size_t n = at ? (size_t)(at - fmt) : strlen(fmt);

	while (n > 0 && fmt[n - 1] == ' ') n--;
	snprintf(pre, pn, "%.*s", (int)n, fmt);
	*post = at ? at + 2 : "";
	for (;;) {
		if (**post == ' ') (*post)++;
		else if (!strncmp(*post, "\xc2\xa0", 2)) *post += 2;
		else break;
	}
}

/* The gap from the glyph to the words after it: a word's 14, or a colon's
 * 6, which sits against what it follows ("Segure (A): carga total") */
static int hint_gap(const char *post)
{
	return *post == ':' ? 6 : 14;
}

/* Its width: the words either side of the button's glyph, `bw` wide, and the gaps */
static int hint_width(TTF_Font *f, const char *pre, int bw, const char *post)
{
	return (pre[0] ? ui_text_width(f, pre) + 14 : 0) + bw +
	       (post[0] ? hint_gap(post) + ui_text_width(f, post) : 0);
}

/* Drawn from `x`, the words at `ty` and the button centered on `mid`; how
 * far it went */
static int hint_draw(SDL_Renderer *r, TTF_Font *f, const char *pre, const char *button, int bw,
                     const char *post, int x, int ty, int mid, SDL_Color c)
{
	int x0 = x;

	if (pre[0]) x += ui_text(r, f, pre, x, ty, -1, c) + 14;
	ui_button(r, button, x + bw / 2, mid, CHG_GLYPH, UI_BUTTON_PLAIN);
	x += bw;
	if (post[0]) x += hint_gap(post) + ui_text(r, f, post, x + hint_gap(post), ty, -1, c);
	return x - x0;
}

/* The screen at `now`: `dim` toward the ground (the send-off), the words at
 * `k` */
void chg_draw(app *a, unsigned now, float dim, float k)
{
	const ds_theme *t = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fl = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_META);
	const int mw = (int)(STATUS_BATT_W * LOWBATT_MARK_K + 0.5f), mh = (int)(STATUS_ROW * LOWBATT_MARK_K + 0.5f);
	/* CS_OFF comes only from the cable out: its send-off dims "Unplugged",
	 * not the charging screen it was before (I saw "Charging" come back
	 * for a moment on a Pixel 2, 2026-11-06) */
	const bool unplugged = g_chg.s.step == CS_UNPLUGGED || g_chg.s.step == CS_OFF;
	const unsigned since = now - g_chg.shown_at;
	zr_pose p = chg_pose(since);
	char pct[8], clk[16];
	int tw, x0, mid;

	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(a->r);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	zyper_shadow_draw(a->r, ZYPEROS_SCREEN_W / 2.0f, CHG_ZYPER_CY, CHG_ZYPER_H, p.tilt, 1.0f - dim);
	draw_zyper_pose(a->r, ZYPEROS_SCREEN_W / 2.0f, CHG_ZYPER_CY, CHG_ZYPER_H, &p,
	                zyper_brow_lift(p.tilt), dim, 255);
	if (!ft || !fl || !fm || k <= 0.0f) return;
	chg_zs(a->r, since, k * (1.0f - dim));
	if (status_clock(time(NULL), clk, sizeof clk))
		ui_text(a->r, fm, clk, STATUS_RIGHT, ui_cap_y(fm, STATUS_MID), 1,
		        ui_fade(ui_col(t->text_soft, 255), k));
	/* The mark and the percentage as one row, centered */
	snprintf(pct, sizeof pct, "%d%%", g_chg.valid ? g_chg.r.pct : 0);
	tw = ui_text_tabular_width(ft, pct);
	x0 = (ZYPEROS_SCREEN_W - (mw + LOWBATT_MARK_GAP + tw)) / 2;
	mid = CHG_TITLE_TOP + ui_font_line(UI_F_TITLE) * 2 / 5;
	battery_mark_big(a->r, x0, mid - mh / 2, g_chg.valid ? g_chg.r.pct : 0, !unplugged,
	                 chg_held(), theme_index(), (Uint8)(255.0f * k + 0.5f));
	ui_text_tabular(a->r, ft, pct, x0 + mw + LOWBATT_MARK_GAP, ui_line_y(UI_F_TITLE, CHG_TITLE_TOP),
	                -1, ui_fade(ui_col(t->text, 255), k));
	if (unplugged || g_chg.speed[0])
		ui_text(a->r, fl, unplugged ? T(S_CHARGING_UNPLUGGED) : g_chg.speed, ZYPEROS_SCREEN_W / 2,
		        ui_line_y(UI_F_LABEL, CHG_LINE1_TOP), 0, ui_fade(ui_col(t->text_soft, 255), k));
	if (!unplugged && g_chg.when[0])
		ui_text(a->r, fm, g_chg.when, ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_META, CHG_LINE2_TOP),
		        0, ui_fade(ui_col(t->text_dim, 255), k));
	if (unplugged || dim > 0.0f) return;
	/* "Hold (POWER) to start", the button drawn as the lock screen draws
	 * its pair (DESIGN.md 11.9, 11.14); held at the charge limit, "Hold (A)
	 * for a full charge" before it (11.26), and the bar under them fills
	 * for whichever is held */
	{
		char start_pre[64], full_pre[64];
		const char *start_post, *full_post, *dot = "·";
		const bool kept = chg_kept();
		SDL_Color dimc = ui_fade(ui_col(t->text_dim, 255), k);
		int gw = ui_button_width("POWER", CHG_GLYPH), ga = ui_button_width("A", CHG_GLYPH);
		int x, w, ty = ui_cap_y(fm, CHG_HINT_MID);
		float held = ch_screen_hold(&g_chg.s, now);
		bool kept_split = false;

		hint_split(T(S_CHARGING_HOLD_START), start_pre, sizeof start_pre, &start_post);
		hint_split(T(S_CHARGING_HOLD_FULL), full_pre, sizeof full_pre, &full_post);
		w = hint_width(fm, start_pre, gw, start_post);
		/* The two in a language whose words do not fit one line (French):
		 * a line each, the full charge's first, both centered, never cut */
		if (kept && w + hint_width(fm, full_pre, ga, full_post) + 44 + ui_text_width(fm, dot) >
		            ZYPEROS_SCREEN_W - 2 * DS_INSET) {
			const int fw = hint_width(fm, full_pre, ga, full_post);
			float fh = ch_screen_full_hold(&g_chg.s, now);

			hint_draw(a->r, fm, full_pre, "A", ga, full_post, (ZYPEROS_SCREEN_W - fw) / 2,
			          ui_cap_y(fm, CHG_HINT_MID - 30), CHG_HINT_MID - 30, dimc);
			hint_draw(a->r, fm, start_pre, "POWER", gw, start_post, (ZYPEROS_SCREEN_W - w) / 2,
			          ui_cap_y(fm, CHG_HINT_MID + 26), CHG_HINT_MID + 26, dimc);
			if (fh > held) held = fh;
			kept_split = true;
		}
		if (kept && !kept_split)
			w += hint_width(fm, full_pre, ga, full_post) +
			     22 + ui_text_width(fm, dot) + 22;
		x = (ZYPEROS_SCREEN_W - w) / 2;
		if (kept_split) {
			/* drawn above */
		} else if (kept) {
			float fh = ch_screen_full_hold(&g_chg.s, now);

			x += hint_draw(a->r, fm, full_pre, "A", ga, full_post, x, ty, CHG_HINT_MID, dimc);
			x += 22;
			x += ui_text(a->r, fm, dot, x, ty, -1, dimc) + 22;
			if (fh > held) held = fh;
		}
		if (!kept_split)
			hint_draw(a->r, fm, start_pre, "POWER", gw, start_post, x, ty, CHG_HINT_MID, dimc);
		if (held > 0.0f) {
			SDL_Rect bar = { (ZYPEROS_SCREEN_W - CHG_BAR_W) / 2, CHG_BAR_TOP, CHG_BAR_W, UI_BAR_H };

			ui_round_rect(a->r, &bar, UI_BAR_H / 2, ui_col(t->track, 255));
			bar.w = (int)(CHG_BAR_W * held + 0.5f);
			if (bar.w >= UI_BAR_H) ui_round_rect(a->r, &bar, UI_BAR_H / 2, ui_col(t->accent_ink, 255));
		}
	}
}

/* The gauge, read every CHG_READ_MS, and the two lines made from it */
static void chg_read(unsigned now, bool force)
{
	int eta;

	if (!force && now - g_chg.read_at < CHG_READ_MS) return;
	g_chg.read_at = now;
	g_chg.valid = plat_charge_read(&g_chg.r);
	if (!g_chg.valid) return;
	if (!g_chg.r.online) charge_full_end();
	g_chg.r.limit = charge_limit_now();
	ch_kind_settle(&g_chg.kind, now, &g_chg.r);
	ch_eta_note(&g_chg.eta, now, &g_chg.r);
	eta = ch_eta_minutes(&g_chg.eta, now, &g_chg.r);
	ch_words(&g_chg.r, eta, g_chg.eta.primed && eta < 0, g_chg.speed, sizeof g_chg.speed,
	         g_chg.when, sizeof g_chg.when);
}

/* The cable in, read now (not battery_now's, which is a few seconds old) */
static bool chg_cable_in(void)
{
	ch_reading r;

	return plat_charge_read(&r) && r.online;
}

/* What ends the send-off early, if anything, at `t0`'s send-off: POWER
 * pressed (CS_START), the cable back (CS_LIT), or neither (CS_OFF) */
static cs_step chg_send_off_check(app *a, unsigned t0)
{
	plat_input_poll(&a->in);
	if (a->in.pressed[IN_POWER]) {
		fprintf(stderr, "charging: POWER %u ms into the send-off, starting\n", plat_now_ms() - t0);
		return CS_START;
	}
	if (chg_cable_in()) {
		fprintf(stderr, "charging: the cable back %u ms into the send-off\n", plat_now_ms() - t0);
		return CS_LIT;
	}
	return CS_OFF;
}

/* Off, from the charging screen: it dims to the ground as the send-off's
 * last part, and no clear after; then dark for CHG_OFF_GRACE_MS. CS_OFF for
 * off; CS_LIT when the cable comes back meanwhile, for the charging screen
 * again; CS_START when POWER is pressed, for ZyperOS as from off. The Pixel
 * 2's PMIC starts the handheld when a cable comes while it is off, and only
 * then, so a cable back during the power-off woke nothing (
 * 2026-11-06); later than this, plastron's last step of the power-off
 * restarts the handheld for it (launch.sh, CHG_OFF_PENDING). And POWER did
 * nothing until the power-off had ended. */
static cs_step chg_send_off(app *a)
{
	unsigned t0 = plat_now_ms(), t;
	cs_step end;

	for (;;) {
		t = plat_now_ms() - t0;
		if (t > POWEROFF_DIM) t = POWEROFF_DIM;
		if ((end = chg_send_off_check(a, t0)) != CS_OFF) return end;
		chg_draw(a, t0 + t, ds_out((float)t / POWEROFF_DIM), 1.0f - (float)t / POWEROFF_DIM);
		plat_present();
		if (t >= POWEROFF_DIM) break;
		ui_frame_rest(6);
	}
	plat_backlight(false);
	while (plat_now_ms() - t0 < POWEROFF_DIM + CHG_OFF_GRACE_MS) {
		if ((end = chg_send_off_check(a, t0)) != CS_OFF) {
			plat_backlight(true);
			return end;
		}
		plat_input_wait(50);
	}
	return CS_OFF;
}

/* The charging screen until it ends in a start (CS_START, the screen's own
 * or POWER during the send-off), off with its send-off done (CS_OFF), or -1
 * for TERM; the cable back during the send-off is the screen again */
static int charging_run(app *a);

static int charging_until(app *a)
{
	int st;

	for (;;) {
		st = charging_run(a);
		if (st != CS_OFF) return st;
		if ((st = chg_send_off(a)) != CS_LIT) return st;
	}
}

/* Nothing: the send-off has played and the screen is dark */
static void chg_dark(app *a)
{
	(void)a;
}

/* The screen until it ends: CS_START, CS_OFF, or -1 for the launcher told to
 * go (TERM). Lit and drawn while lit; dark, the backlight off and the loop
 * waiting on input, as the charger's dark shelf does. */
static int charging_run(app *a)
{
	unsigned now = plat_now_ms();
	bool dark = false;

	memset(&g_chg, 0, sizeof g_chg);
	g_chg.shown_at = now;
	ch_screen_start(&g_chg.s, now);
	chg_read(now, true);
	fprintf(stderr, "charging: the charging screen, %d%%%s\n", g_chg.valid ? g_chg.r.pct : -1,
	        g_chg.valid ? "" : " (no reading)");
	plat_backlight(true);
	for (;;) {
		bool any = false;
		cs_step step;
		int b;

		plat_input_poll(&a->in);
		if (a->in.quit_requested || want_quit) return -1;
		now = plat_now_ms();
		chg_read(now, false);
		for (b = 0; b < IN_COUNT; b++) any = any || a->in.pressed[b];
		step = ch_screen_tick(&g_chg.s, now, any, a->in.down[IN_POWER],
		                      g_chg.valid ? g_chg.r.online : on_charger());
		if (step == CS_START || step == CS_OFF) {
			fprintf(stderr, "charging: %s\n", step == CS_START ? "POWER, starting"
			                                                   : "unplugged, turning off");
			return step;
		}
		/* "Hold A for a full charge", at the limit: this charge to 100% */
		if (ch_screen_full(&g_chg.s, now, chg_kept() && a->in.down[IN_ACCEPT])) {
			charge_full_once();
			chg_read(now, true);
		}
		if (step == CS_DARK) {
			if (!dark) { plat_backlight(false); dark = true; }
			plat_input_wait(100);
			continue;
		}
		if (dark) { plat_backlight(true); dark = false; }
		chg_draw(a, now, 0.0f, 1.0f);
		plat_present();
		ui_frame_rest(16);
	}
}

/* zycharge=1 on the kernel's line: a charger woke the handheld, and
 * plastron's bootloader started it to say so (a word without a dot, which
 * the kernel hands init's environment too, where plastron's splash reads
 * it). Or the test file, gone once read, so the start it asks for is one
 * start. Taken once a boot (CHG_TAKEN, in /tmp, which a boot empties): the
 * line stays the same for the whole boot, and a launcher started again in
 * it - the charging screen's own Hold POWER after a power-off on a charger,
 * a crash, a deploy - is not a start the charger made (seen on a Pixel 2,
 * 2026-11-06). */
static bool charge_boot(void)
{
	char cmd[1024] = "";
	bool pending;
	FILE *f;

	if (access(CHG_TEST_BOOT, F_OK) == 0) {
		remove(CHG_TEST_BOOT);
		return true;
	}
	if (access(CHG_TAKEN, F_OK) == 0) return false;
	{
		/* A power-off a charger came into while it ran: plastron's last
		 * step restarts the handheld for it, and launch.sh left this at
		 * the power-off. Taken at any start, so a start from POWER after a
		 * power-off that went through finds it gone. */
		char p[512];

		snprintf(p, sizeof p, "%s/%s", P_USERDATA, CHG_OFF_PENDING);
		pending = access(p, F_OK) == 0 && remove(p) == 0;
	}
	if ((f = fopen("/proc/cmdline", "r"))) {
		if (!fgets(cmd, sizeof cmd, f)) cmd[0] = '\0';
		fclose(f);
	}
	{
		/* the word, not one that starts with it */
		const char *w = strstr(cmd, "zycharge=1");

		if ((!w || (w != cmd && w[-1] != ' ') || (w[10] != '\0' && w[10] != ' ' && w[10] != '\n')) &&
		    !(pending && chg_cable_in()))
			return false;
	}
	if ((f = fopen(CHG_TAKEN, "w"))) fclose(f);
	return true;
}

/* A start the charger made: the charging screen first, and POWER held goes on
 * with the start (true); unplugged turns the handheld off (false) */
bool charge_boot_gate(app *a)
{
	int st;

	if (!charge_boot()) return true;
	fprintf(stderr, "charging: started by a charger\n");
	st = charging_until(a);
	if (st == CS_START) {
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return true;
	}
	if (st == CS_OFF) power_off_with(a, chg_dark);
	a->running = false;
	return false;
}

/* POWER, or Auto Off, on a charger: what power_off does - the game saved, the
 * place kept, the send-off - and then the charging screen in place of black.
 * ZyperOS is off: the music too. POWER held starts it afresh, as from off:
 * this process ends and launch.sh starts a new one at once (CHG_RESTART),
 * which goes back into a game as any start does. Unplugged, off. False off
 * the charger, or with no gauge to read (the Brick), for the power-off as
 * ever. */
bool charge_instead(app *a)
{
	ch_reading r;
	int st;

	if (want_quit || !on_charger() || !plat_charge_read(&r)) return false;
	snap_off();
	remember_place(a);
	book_keep(true);
	if (musec_playing()) musec_stop();
	bgm_hold(BGM_HOLD_START, true);
	anim_poweroff(a);
	st = charging_until(a);
	if (st == CS_START) {
		FILE *f = fopen(CHG_RESTART, "w");

		if (f) fclose(f);
	} else if (st == CS_OFF)
		plat_request_poweroff();
	a->running = false;
	return true;
}
