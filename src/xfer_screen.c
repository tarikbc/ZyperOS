/* SPDX-License-Identifier: GPL-3.0-only */
#include "xfer_screen.h"
#include "radl.h"
#include "hwreport.h"
#include "ds/mark.h"   /* ds_bar_in */

#include "aout_route.h"
#include "app.h"
#include "bgm.h"
#include "bt.h"
#include "captive.h"
#include "ds/layout.h"
#include "ds/page.h"
#include "esmedia.h"
#include "favorites.h"
#include "hare.h"
#include "hareart.h"
#include "hotpanel.h"
#include "logpack.h"
#include "look.h"
#include "mdns.h"
#include "menu_panel.h"
#include "mp4_screen.h"
#include "mp4c.h"
#include "pace.h"
#include "panel.h"
#include "place.h"
#include "rafetch.h"
#include "sgdb.h"
#include "shelfbuild.h"
#include "signin_screen.h"
#include "ss.h"
#include "spawn.h"
#include "sshlink.h"
#include "taskhold.h"
#include "uisound.h"
#include "usbdrive_screen.h"
#include "usblink.h"
#include "xfer.h"
#include "zyperdraw.h"
#include "levels.h"
#include "wdog.h"

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

/* Wi-Fi File Transfer: the address, the PIN, and what is happening.
 *
 * The whole screen is a waiting room. It exists so somebody can read two
 * things off a handheld and type them into a laptop, and then watch enough to
 * know it is working. Everything real happens in hare.c and in a browser
 * somewhere else on the network.
 *
 * The server lives exactly as long as this screen. Closing it stops
 * listening, forgets the PIN and drops every session - which is the honest
 * answer to "how long is my ROM folder on the network for". */
/* The address and the PIN go in the HEADING, not in rows.
 *
 * They were four rows of equal weight, and rendered it was obvious that the
 * two things this screen exists to be read off were in the dimmest color the
 * launcher has: menu_draw paints a value UI_TEXT_DIM unless its row is both
 * live and selected, and nothing on a status screen is either. The heading is
 * UI_F_LABEL at UI_TEXT_SOFT - larger and brighter - and takes two lines when
 * it contains a newline, which is exactly two things worth copying.
 *
 * Everything else is status and belongs below the rule. */
/* With the handheld's name (zyperos.local, mdns.h): the address first, over
 * the cable as over Wi-Fi, and the name beside the PIN. Over the cable the
 * name went first until 2026-11-23, but Windows does not resolve it there
 * (Discord: Edge, Chrome and Firefox could not open http://zyperos.local
 * over the cable, the address could), and the address works on every
 * computer. The name is written bare here, as Wi-Fi's is: with http:// it
 * no longer fit beside the PIN in a heading's line, and slid; the way in,
 * which has the room, writes it with http:// over the cable (xs_way_set).
 * With no name (mDNS could not start, gave the name up, or the link is down
 * and `addr` says so), the address over the PIN. */
void hare_head(char *out, size_t n, bool cable, const char *name, const char *addr,
               const char *pin)
{
	/* A fixture's "http://192.168.1.42" is the address the screen shows */
	if (name && name[0] && !strncmp(addr, "http://", 7)) addr += 7;
	(void)cable;
	if (name && name[0]) snprintf(out, n, "%s\nPIN %s · %s", addr, pin, name);
	else snprintf(out, n, "%s\nPIN %s", addr, pin);
}

/* THE WAY IN (DESIGN.md 11.35, 2026-11-12: "while nobody inputted the PIN
 * yet ... show the URL/IP/PIN full size in the popup; only after someone
 * connected we show the stats"). Until someone has signed in, the panel is
 * the screen's name, the first thing to type as large as it fits whole, the
 * other way to reach it under that, and the PIN as four keys; under them the
 * step the link is on, the rule and the legend. The steps and their hint
 * (xs_step_rows) take its place only while a step before the browser stalls,
 * when the address cannot work yet. g_way holds what the header draws. */
static struct {
	const char *title;
	char        first[128], second[128], pin[16];
} g_way;

#define XS_WAY_H     352         /* the header's room, its gap under it included */
#define XS_WAY_KEY_W 80
#define XS_WAY_KEY_H 96

static void xs_way_header(app *a, const SDL_Rect *q)
{
	const ds_theme *t = theme();
	TTF_Font *fm = ui_font(UI_F_META), *fn = ui_font(UI_F_MENU);
	TTF_Font *big[3] = { ui_font(UI_F_TITLE), ui_font(UI_F_LABEL), ui_font(UI_F_MENU) };
	TTF_Font *fb;
	const int cx = q->x + q->w / 2, y = q->y;
	int n = (int)strlen(g_way.pin), i, kx, ky = y + 228;

	/* the screen's name, small, where a heading would be */
	ui_text(a->r, fm, g_way.title, cx, ui_cap_y(fm, y + 18), 0, UI_TEXT_DIM);
	/* the first thing to type: **title** where it fits whole, a step down
	 * where not (a port on the name), and sliding past that, never cut */
	for (i = 0; i < 2 && ui_text_width(big[i], g_way.first) > q->w; i++) ;
	fb = big[i];
	if (ui_text_width(fb, g_way.first) <= q->w)
		ui_text(a->r, fb, g_way.first, cx, ui_cap_y(fb, y + 84), 0, UI_TEXT);
	else
		ui_text_marquee(a->r, fb, g_way.first, q->x, ui_cap_y(fb, y + 84), q->w,
		                plat_now_ms(), UI_TEXT);
	if (g_way.second[0])
		ui_text(a->r, fn, g_way.second, cx, ui_cap_y(fn, y + 146), 0, UI_TEXT_SOFT);
	/* the PIN, a key a digit: **raised** in an **edge** frame, the digit in
	 * **title** **text**, as the page's four boxes take it */
	ui_text(a->r, fm, "PIN", cx, ui_cap_y(fm, y + 204), 0, UI_TEXT_DIM);
	kx = cx - (n * XS_WAY_KEY_W + (n - 1) * DS_GRID * 2) / 2;
	for (i = 0; i < n; i++, kx += XS_WAY_KEY_W + DS_GRID * 2) {
		SDL_Rect k = { kx, ky, XS_WAY_KEY_W, XS_WAY_KEY_H };
		char d[2] = { g_way.pin[i], '\0' };

		ui_round_rect(a->r, &k, DS_RADIUS_KEY, ui_col(t->edge, 255));
		ui_round_rect(a->r, &(SDL_Rect){ k.x + DS_FRAME, k.y + DS_FRAME, k.w - DS_FRAME * 2,
		                                 k.h - DS_FRAME * 2 }, DS_RADIUS_KEY - DS_FRAME,
		              ui_col(t->raised, 255));
		ui_text(a->r, big[0], d, k.x + k.w / 2, ui_cap_y(big[0], k.y + k.h / 2), 0, UI_TEXT);
	}
}

/* What the way in shows: the address first and the name under it, over the
 * cable as over Wi-Fi and a phone's tethering, as the heading orders them
 * (hare_head); over the cable the name with http:// */
void xs_way_set(bool cable, bool phone, const char *name, const char *addr, const char *pin)
{
	if (name && name[0] && !strncmp(addr, "http://", 7)) addr += 7;
	g_way.title = cable || phone ? XFER_NAME_CABLE : XFER_NAME_WIFI;
	snprintf(g_way.first, sizeof g_way.first, "%s", addr);
	/* a phone's: where to type it, as no name is shown there */
	if (name && name[0])
		snprintf(g_way.second, sizeof g_way.second, "%s%s", cable ? "http://" : "", name);
	else
		snprintf(g_way.second, sizeof g_way.second, "%s", phone ? T(S_XFER_PHONE_WHERE) : "");
	snprintf(g_way.pin, sizeof g_way.pin, "%s", pin);
}

/* The way in's rows: the step the link is on (Cable unplugged, Address or
 * Browser waiting, Wi-Fi not connected), or PIN waiting once a browser has
 * the page; the rule; the legend */
int xs_way_rows(menu_row *out, const link_steps *s, bool cable, const char *legend)
{
	int n = 0;

	if (s->step == LS_LINK && cable)
		out[n++] = (menu_row){ T(S_XFER_STEP_CABLE),
		                       s->sub == 0 ? T(S_XFER_STEP_UNPLUGGED) : T(S_XFER_WAITING), true };
	else if (s->step == LS_LINK)
		out[n++] = (menu_row){ T(S_XFER_STEP_WIFI), T(S_MENU_NOT_CONNECTED), true };
	else if (s->step == LS_ADDRESS)
		out[n++] = (menu_row){ T(S_XFER_STEP_ADDRESS), T(S_XFER_WAITING), true };
	else if (s->step == LS_BROWSER)
		out[n++] = (menu_row){ T(S_XFER_STEP_BROWSER), T(S_XFER_WAITING), true };
	else
		out[n++] = (menu_row){ "PIN", T(S_XFER_WAITING), true };
	out[n++] = MENU_RULE;
	out[n++] = MENU_NOTE(legend);
	return n;
}

/* Whether a stalled step's hint takes the way in's place: a hint for a step
 * before the browser, while the address cannot work yet */
bool xs_way_hint(int h)
{
	return h != LH_NONE && h != LH_NO_BROWSER;
}

/* One frame of the way in, over what the caller drew under it */
void xs_way_draw(app *a, const menu_row *rows, int n)
{
	g_menu_header = xs_way_header;
	g_menu_header_h = XS_WAY_H;
	menu_draw(a, NULL, rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
	g_menu_header = NULL;
	g_menu_header_h = 0;
}

/* THE CABLE'S LOADING BAR (DESIGN.md 11.35, 2026-11-12: "usb file transfer
 * takes a while to start, we need a loading state"). Over the cable the
 * address and the PIN cannot work until the computer has taken the handheld,
 * opened its network on it and taken its address, a few seconds and more the
 * first time Windows sees it. Until then the way in's panel says so, in the
 * same place: the screen's name, "Connecting to the computer" in **label**,
 * and a **bar** on **track** filling with **accent-ink**, a third for each
 * of the three. While one is waited on, its third creeps toward three
 * quarters of the way (never the whole: the computer has not done it yet);
 * the step row under the rule says which. Once the address is in, the bar
 * runs to its end and the way in takes its place. A stalled step's hint
 * still takes the whole panel (xs_step_rows). g_load_k is what the header
 * draws. */
static float g_load_k;

#define XS_LOAD_BAR_W 420
#define XS_LOAD_CREEP_MS 3000.0f   /* a waited step's creep: 63% of its room in this */
#define XS_LOAD_EASE_MS  150.0f    /* the fill catching up with where it should be */

/* Where the bar should be: a third for each of the computer taking the
 * handheld, its network and its address, and the creep of the one waited
 * on. No computer (sub 0) or one waiting for power (sub 4) does not creep:
 * nothing is coming. */
float xs_load_target(const link_steps *s, unsigned now)
{
	float creep = 0.0f;
	int k;

	if (!s->started) return 0.0f;
	if (s->step >= LS_BROWSER) return 1.0f;
	k = s->step == LS_ADDRESS ? 2 : s->sub == 2 ? 1 : 0;
	if (s->step == LS_ADDRESS || s->sub == 1 || s->sub == 2)
		creep = 0.75f * (1.0f - expf(-(float)(now - s->since) / XS_LOAD_CREEP_MS));
	return ((float)k + creep) / 3.0f;
}

/* One pass of the bar (the session's fill `k` and the moment `ms` it last
 * moved): the fill eases up toward where it should be, and drops at once
 * when a step is lost (the cable out). True while it is short of its end,
 * when the loading view is the one up. */
static bool xs_load_step(const link_steps *s, float *k, unsigned *ms, unsigned now)
{
	float target = xs_load_target(s, now), dt;

	dt = *ms ? (float)(now - *ms) : 0.0f;
	if (dt > 100.0f) dt = 100.0f;
	*ms = now ? now : 1;
	if (target < *k) *k = target;
	else *k += (target - *k) * (dt >= XS_LOAD_EASE_MS ? 1.0f : dt / XS_LOAD_EASE_MS);
	if (target >= 1.0f && *k > 0.995f) *k = 1.0f;
	return *k < 1.0f;
}

static void xs_load_header(app *a, const SDL_Rect *q)
{
	const ds_theme *t = theme();
	const char *say = T(S_XFER_CONNECTING);
	TTF_Font *fm = ui_font(UI_F_META), *fl = ui_font(UI_F_LABEL);
	const int cx = q->x + q->w / 2, y = q->y;
	SDL_Rect bar = { cx - XS_LOAD_BAR_W / 2, y + 232, XS_LOAD_BAR_W, UI_BAR_H };
	int fill = (int)(XS_LOAD_BAR_W * (g_load_k < 0 ? 0 : g_load_k > 1 ? 1 : g_load_k) + 0.5f);

	ui_text(a->r, fm, g_way.title, cx, ui_cap_y(fm, y + 18), 0, UI_TEXT_DIM);
	/* **label**, or **menu** where a language's words do not fit it whole,
	 * and sliding past that, never cut */
	if (ui_text_width(fl, say) > q->w) fl = ui_font(UI_F_MENU);
	if (ui_text_width(fl, say) <= q->w)
		ui_text(a->r, fl, say, cx, ui_cap_y(fl, y + 160), 0, UI_TEXT);
	else
		ui_text_marquee(a->r, fl, say, q->x, ui_cap_y(fl, y + 160), q->w, plat_now_ms(), UI_TEXT);
	ds_bar_in(a->r, &bar, fill, ui_col(t->track, 255), ui_col(t->accent_ink, 255));
}

/* One frame of the loading view: the way in's panel, rows and all, with
 * the bar in place of what to type */
void xs_load_draw(app *a, const menu_row *rows, int n, float k)
{
	g_load_k = k;
	g_menu_header = xs_load_header;
	g_menu_header_h = XS_WAY_H;
	menu_draw(a, NULL, rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
	g_menu_header = NULL;
	g_menu_header_h = 0;
}

/* The hotspot's heading once someone came in (the news replaces its code):
 * its network and password, then the PIN, for another phone. Where the
 * network and the password do not fit one line of the panel's heading
 * (**label**, `room` wide; Chinese draws them wider and its panel is
 * narrower), the password moves down beside the PIN, so neither slides. */
void hot_head(char *out, size_t n, int room, const char *ssid, const char *pass,
              const char *pin)
{
	snprintf(out, n, "%s · %s", ssid, pass);
	if (ui_text_width(ui_font(UI_F_LABEL), out) <= room)
		snprintf(out, n, "%s · %s\nPIN %s", ssid, pass, pin);
	else
		snprintf(out, n, "%s\n%s · PIN %s", ssid, pass, pin);
}

int xs_step_rows(app *a, menu_row *out, char lines[3][LB_LINE], char *hint, size_t hn,
                 const link_steps *s, int h, bool cable, const char *legend)
{
	menu_row all[3];
	int n = 0, m = 0, k, i, at = 0;

	if (cable) {
		all[m++] = (menu_row){ T(S_XFER_STEP_CABLE),
		                       s->step > LS_LINK ? MENU_TICK
		                       : s->sub == 0 ? T(S_XFER_STEP_UNPLUGGED) : T(S_XFER_WAITING), true };
		all[m++] = (menu_row){ T(S_XFER_STEP_ADDRESS),
		                       s->step > LS_ADDRESS ? MENU_TICK
		                       : s->step == LS_ADDRESS ? T(S_XFER_WAITING) : "",
		                       s->step >= LS_ADDRESS };
		at = s->step < LS_BROWSER ? s->step : 2;
	} else {
		all[m++] = (menu_row){ T(S_XFER_STEP_WIFI),
		                       s->step > LS_LINK ? MENU_TICK : T(S_MENU_NOT_CONNECTED), true };
		at = s->step == LS_LINK ? 0 : 1;
	}
	all[m++] = (menu_row){ T(S_XFER_STEP_BROWSER),
	                       s->step > LS_BROWSER ? MENU_TICK
	                       : s->step == LS_BROWSER ? T(S_XFER_WAITING) : "",
	                       s->step >= LS_BROWSER };
	hint[0] = '\0';
	switch (h) {
	case LH_NO_COMPUTER: snprintf(hint, hn, "%s", T(S_XFER_HINT_NO_COMPUTER)); break;
	case LH_NO_ANSWER:   snprintf(hint, hn, "%s", T(S_XFER_HINT_NO_ANSWER));   break;
	case LH_NO_DRIVER:   snprintf(hint, hn, "%s", T(S_XFER_HINT_NO_DRIVER));   break;
	case LH_NO_ADDRESS:  snprintf(hint, hn, "%s", T(S_XFER_HINT_NO_ADDRESS));  break;
	case LH_NO_NETWORK:  snprintf(hint, hn, "%s", T(S_PANEL_NEEDS_WIFI));      break;
	case LH_REPLUG:      snprintf(hint, hn, "%s", T(S_XFER_HINT_REPLUG));      break;
	/* LH_NO_BROWSER has none: the way in (xs_way_rows) shows what to type
	 * at full size, which is what that hint said */
	default: break;
	}
	if (hint[0]) {
		out[n++] = all[at];
		k = ui_balance_lines(ui_font(UI_F_MENU), hint, menu_std_width(a), 3, lines);
		for (i = 0; i < k; i++) out[n++] = MENU_NOTE(lines[i]);
		ui_fit_drawn(hint);          /* drawn in pieces: the whole, for the check */
	} else {
		for (i = 0; i < m; i++) out[n++] = all[i];
	}
	out[n++] = MENU_RULE;
	out[n++] = MENU_NOTE(legend);
	return n;
}

/* Wi-Fi File Transfer as the handheld's own network (src/hotspot.h): a
 * screen of its own (src/hotpanel.c draws it), opened and closed as a panel
 * is, the whole screen being the panel: from 96% and clear over the shelf.
 * The rows carry its text so the copy kept as it
 * leaves (snap_record) draws it whole: the code's text as the first label,
 * then the network, the password, the PIN, the phones and the news. */
void hot_panel(app *a, const char *heading, const menu_row *rows, int n,
               mm_pose pose)
{
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	SDL_Rect screen = { 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H };
	hotpanel p;

	if (n < 6) return;
	p = (hotpanel){ heading, rows[0].label, rows[1].value, rows[2].value,
	                rows[3].value, rows[4].value, rows[5].value,
	                n > 6 ? rows[6].label : NULL };
	if (posed) posed = panel_pass_begin(a);
	hotpanel_draw(a->r, &p, plat_now_ms());
	if (posed) panel_pass_end(a, &screen, pose);
}

static void hot_rows(menu_row *rows, char *phones, size_t pn, bool starting,
                     int joined, const char *pin, const char *now)
{
	if (starting)         snprintf(phones, pn, "%s", T(S_HOTPANEL_STARTING));
	else if (joined <= 0) snprintf(phones, pn, "%s", T(S_XFER_WAITING));
	else                  snprintf(phones, pn, TN(S_HOTPANEL_JOINED, joined), joined);
	rows[0] = (menu_row){ hotspot_qr_text(),     NULL,               false };
	rows[1] = (menu_row){ T(S_HOTPANEL_NETWORK), hotspot_ssid(),     false };
	rows[2] = (menu_row){ T(S_PANEL_PASSWORD),   hotspot_password(), false };
	rows[3] = (menu_row){ "PIN",                 pin,                false };
	rows[4] = (menu_row){ T(S_HOTPANEL_PHONES),  phones,             false };
	rows[5] = (menu_row){ T(S_XFER_NOW),         now,                false };
}

static void hot_show(app *a, const char *name, const char *pin, bool starting,
                     const char *now, const char *legend)
{
	menu_row rows[7];
	char phones[32];

	hot_rows(rows, phones, sizeof phones, starting, hotspot_phones(), pin, now);
	rows[6] = MENU_NOTE(legend);
	panel_show(a, PANEL_HOT, name, rows, 7, -1, 0, 0u, NULL, false, 0, NULL);
}

/* One frame of it for the shot harness (--hotspot), from the rows the screen
 * builds, with a network and password given rather than made. */
void hot_preview(app *a, const char *ssid, const char *pass,
                 const char *phones, const char *now)
{
	char qr[160];
	menu_row rows[7];

	captive_wifi_qr(qr, sizeof qr, ssid, pass);
	rows[0] = (menu_row){ qr,         NULL,   false };
	rows[1] = (menu_row){ T(S_HOTPANEL_NETWORK), ssid,   false };
	rows[2] = (menu_row){ T(S_PANEL_PASSWORD),   pass,   false };
	rows[3] = (menu_row){ "PIN",                 "4071", false };
	rows[4] = (menu_row){ T(S_HOTPANEL_PHONES),  phones, false };
	rows[5] = (menu_row){ T(S_XFER_NOW),         now,    false };
	rows[6] = MENU_NOTE(b_legend(S_KEYHINT_CLOSE));
	draw_shelf(a);
	ui_scrim(a->r, 0);
	panel_show(a, PANEL_HOT, XFER_NAME_WIFI, rows, 7, -1, 0, 0u, NULL, false, 0, NULL);
}

/* Download logs, from Wi-Fi File Transfer: see logpack.h. The names to hide are
 * gathered here, at the moment of the download, because only the launcher
 * knows them - every saved network rather than just the one it is on, since
 * an older boot's log can name another; every paired headset; both account
 * names. about.txt carries the first questions asked about any report. */
app *g_logs_app;

static bool pack_logs(char *path, size_t pn, char *name, size_t nn)
{
	wifi_net   nets[WIFI_MAX_NETS];
	bt_device  bt[BT_MAX];
	const char *secrets[WIFI_MAX_NETS + BT_MAX + 5];
	char       now_ssid[WIFI_SSID_MAX] = "", ip[64], logs[CFG_STR * 2];
	char       folder[64], about[8192 + HWREPORT_KIT_ROOM], err[128], stamp[32], sysver[64] = "-",
	           hw[3072 + HWREPORT_KIT_ROOM];
	hw_facts   facts;
	int        n = 0, saved, nb, i;
	size_t     at = 0;
	time_t     t = time(NULL);
	struct tm  tm;
	struct statvfs vf;
	double     up = 0;
	FILE      *f;

	saved = wifi_known(nets, WIFI_MAX_NETS);
	for (i = 0; i < saved; i++) secrets[n++] = nets[i].ssid;
	if (wifi_status(now_ssid, sizeof now_ssid, ip, sizeof ip) != WIFI_OFF && now_ssid[0])
		secrets[n++] = now_ssid;
	nb = bt_bonded(bt, BT_MAX);
	for (i = 0; i < nb; i++) secrets[n++] = bt[i].name;
	if (ra_signed_in() && ra_user()) secrets[n++] = ra_user();
	if (ss_signed_in() && ss_user()) secrets[n++] = ss_user();
	/* The SteamGridDB key never goes into a log (src/sgdb.h); masked all the
	 * same, so a line nobody meant to write cannot carry it out. */
	if (sgdb_secret()) secrets[n++] = sgdb_secret();

	localtime_r(&t, &tm);
	strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tm);
	strftime(folder, sizeof folder, "zyperos-logs-%Y-%m-%d-%H%M", &tm);
	if ((f = fopen("/proc/uptime", "r"))) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }

#define ABOUT(...) do { \
		int k_ = snprintf(about + at, sizeof about - at, __VA_ARGS__); \
		if (k_ > 0 && (size_t)k_ < sizeof about - at) at += (size_t)k_; \
	} while (0)
	/* The system image's own version (its slot's), beside the launcher's */
	if ((f = fopen("/etc/zyperos/system", "r"))) {
		char l[128];

		while (fgets(l, sizeof l, f))
			if (!strncmp(l, "version ", 8)) snprintf(sysver, sizeof sysver, "%.*s", (int)strcspn(l + 8, "\n"), l + 8);
		fclose(f);
	}
	ABOUT("ZyperOS %s\n", ZYPEROS_VERSION);
	ABOUT("Device: %s (%s)\n", plat_profile()->name, plat_profile()->id);
	ABOUT("System: %s\n", sysver);
	ABOUT("Packed %s, up %.0f minutes\n", stamp, up / 60);
	if (statvfs(P_CARD, &vf) == 0)
		ABOUT("Card: %.1f GB free of %.1f GB\n",
		      (double)vf.f_bavail * vf.f_frsize / 1e9, (double)vf.f_blocks * vf.f_frsize / 1e9);
	ABOUT("Auto Off: %d s\n", g_logs_app ? g_logs_app->auto_off : -1);
	ABOUT("Wi-Fi: %s\n", now_ssid[0] ? now_ssid : "not connected");
	ABOUT("Paired headsets: %d\n", nb);
	/* What the handheld has (src/hwreport.h): its panel, inputs, GPU, Wi-Fi
	 * chip, closed files, and the start's marks */
	plat_hw_facts(&facts);
	hwreport_build(&facts, hw, sizeof hw);
	ABOUT("\n%s", hw);
	ABOUT("\nShelves:\n");
	for (i = 0; g_logs_app && i < g_logs_app->sys.count; i++) {
		const system_cfg *sc = &g_logs_app->sys.systems[i];

		if (is_mp4(sc))
			ABOUT("  %-22s %d albums, %d books, %d tracks\n", sc->name,
			      ml_count(&g_mp4, false), ml_count(&g_mp4, true), g_mp4.ntracks);
		else
			ABOUT("  %-22s %d\n", sc->name, sys_games(&g_logs_app->view[i])->count);
	}
#undef ABOUT

	snprintf(logs, sizeof logs, "%s/logs", P_USERDATA);
	/* The kernel's log as it is now, into the pack too (masked as the rest):
	 * the panel, the GPU's and the Wi-Fi chip's own lines in full */
	{
		static char klog[1 << 18];
		char kpath[CFG_STR * 2 + 16];

		hw_klog_text(klog, sizeof klog);
		snprintf(kpath, sizeof kpath, "%s/kernel.log", logs);
		if (klog[0] && (f = fopen(kpath, "w"))) {
			fputs(klog, f);
			fclose(f);
		}
	}
	/* And the system log, the memory ring logread prints: on the Brick it
	 * runs while Bluetooth is on (/etc/init.d/bluetooth), and bluetoothd's
	 * and bluealsa's lines are only there, so a pairing that connects
	 * nothing says why (a tester's Brick, 2026-11-21, sent none). Read here
	 * and handed to the pack, which keeps those lines alone, masked; none of
	 * it is written to the card. None where no system logger runs */
	static char slog[1 << 16];
	{
		char *argv[] = { (char *)"/sbin/logread", NULL };

		if (spawn_capture(argv, slog, sizeof slog, 5) != 0) slog[0] = '\0';
	}
	snprintf(name, nn, "%s.tar.gz", folder);
	snprintf(path, pn, "/tmp/%s", name);
	/* tar, waited for as long as it takes (wdog.h) */
	wdog_hold("Download logs");
	if (!logpack_build(logs, about, slog, secrets, n, folder, path, err, sizeof err)) {
		wdog_release();
		fprintf(stderr, "logs: could not pack them: %s\n", err);
		return false;
	}
	wdog_release();
	fprintf(stderr, "logs: packed %s, %d names masked\n", name, n);
	return true;
}

/* A folder about to be deleted whole from Wi-Fi File Transfer, or a folder or file
 * about to be renamed there: if MP4 is playing or paused on that file or on
 * one in that folder, stop MP4 first. The shelf is rebuilt when the screen
 * closes, as for any change there; this is only so the player is not left
 * holding a file that is gone. */
void mp4_before_delete(const char *abs)
{
	const char *p = mp4c_path();
	char full[LIB_PATH * 2];
	size_t n = strlen(abs);

	if (!p[0]) return;
	snprintf(full, sizeof full, "%s/%s", P_CARD, p);
	if (!strncmp(full, abs, n) && (full[n] == '/' || !full[n])) {
		fprintf(stderr, "mp4: stopped, what it played is being moved or deleted\n");
		mp4c_stop();
	}
}

/* Rows of the settings keyed "<kind><folder>" or "<kind><folder>/...", which
 * a folder renamed with everything in it takes along. Gathered first and
 * changed after, so the rows are not rewritten under the read. */
typedef struct key_move { struct key_move *next; char *key, *val; } key_move;
typedef struct { size_t n; key_move *list; } key_moves;

static bool key_under(const char *key, const char *value, void *ctx)
{
	key_moves *m = ctx;
	key_move *k;

	/* "Album" and "Album/CD1", not "Album 2" */
	if (key[m->n] && key[m->n] != '/') return true;
	if (!(k = calloc(1, sizeof *k))) return false;
	k->key = strdup(key);
	k->val = strdup(value);
	k->next = m->list;
	m->list = k;
	return true;
}

static void keys_follow(db *d, const char *kind, const char *frel, const char *trel)
{
	char prefix[LIB_PATH + 16], nkey[LIB_PATH * 2 + 32];
	key_moves m = { 0, NULL };
	key_move *k;

	if (snprintf(prefix, sizeof prefix, "%s%s", kind, frel) >= (int)sizeof prefix) return;
	m.n = strlen(prefix);
	db_each_prefix(d, prefix, key_under, &m);
	while ((k = m.list)) {
		m.list = k->next;
		if (k->key && k->val &&
		    snprintf(nkey, sizeof nkey, "%s%s%s", kind, trel, k->key + m.n) < (int)sizeof nkey) {
			db_del(d, k->key);
			db_set_str(d, nkey, k->val);
		}
		free(k->key);
		free(k->val);
		free(k);
	}
}

/* A book's place names its file ("<seconds>\t<file>"): when that file was
 * renamed, the place follows. With `to` NULL the file's new name is the one
 * Tidy names would have given it, if that is there now. */
static void book_file_follows(const char *folder, const char *from, const char *to)
{
	char key[LIB_PATH + 300], val[LIB_PATH + 32], tidy[256], there[LIB_PATH * 2 + 300];
	char *tab;

	snprintf(key, sizeof key, "book.%s", folder);
	if (!db_get_str(db_dev(), key, val, sizeof val, "") || !(tab = strchr(val, '\t'))) return;
	if (from && strcmp(tab + 1, from)) return;
	if (!to) {
		if (!ml_tidy_file(tab + 1, tidy, sizeof tidy)) return;
		snprintf(there, sizeof there, "%s/%s/%s", P_CARD, folder, tidy);
		if (access(there, F_OK) != 0) return;
		to = tidy;
	}
	snprintf(tab + 1, sizeof val - (size_t)(tab + 1 - val), "%s", to);
	db_set_str(db_dev(), key, val);
}

/* The transfer page renamed a music or audiobook folder or file, by hand or
 * with Tidy names, which may also have renamed the files in the folder: what
 * is kept under the old names follows. An album's cover, from the .media
 * beside it; what Album Art remembers of it and a book's place, keyed by the
 * folder's path, for it and any folder in it; and the shelf held in memory,
 * until it is read again on the way out of the screen, so the page's lists
 * find the album's cover meanwhile. */
static void mp4_after_rename(const char *from, const char *to, bool tidied)
{
	size_t card = strlen(P_CARD), fl;
	const char *frel, *trel, *fbase = strrchr(from, '/'), *tbase = strrchr(to, '/');
	static const char *const EXT[] = { "jpg", "png" };
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 300];
	struct stat st;
	int k;

	if (strncmp(from, P_CARD, card) || from[card] != '/' ||
	    strncmp(to, P_CARD, card) || to[card] != '/' || !fbase || !tbase)
		return;
	frel = from + card + 1;
	trel = to + card + 1;
	fl = strlen(frel);
	/* A file renamed by hand: its track, and a book's place if it was there */
	if (stat(to, &st) == 0 && !S_ISDIR(st.st_mode)) {
		for (k = 0; k < g_mp4.ntracks; k++)
			if (!strcmp(g_mp4.tracks[k].path, frel))
				snprintf(g_mp4.tracks[k].path, sizeof g_mp4.tracks[k].path, "%s", trel);
		/* A rename names a sibling, so the folder is the same on both */
		snprintf(cfrom, sizeof cfrom, "%.*s", (int)(fbase - frel), frel);
		book_file_follows(cfrom, fbase + 1, tbase + 1);
		fprintf(stderr, "mp4: %s is now %s\n", frel, trel);
		return;
	}
	/* Each track at its new path. Tidy names renamed a file in the folder
	 * itself by ml_tidy_file unless that name was taken, so the same rule
	 * finds it again; anything else kept its name. */
	for (k = 0; k < g_mp4.ntracks; k++) {
		ml_track *t = &g_mp4.tracks[k];
		char tidy[256], np[LIB_PATH], there[LIB_PATH * 2 + 300];
		const char *file;

		if (strncmp(t->path, frel, fl) || t->path[fl] != '/') continue;
		file = t->path + fl + 1;
		if (tidied && !strchr(file, '/') && ml_tidy_file(file, tidy, sizeof tidy)) {
			snprintf(there, sizeof there, "%s/%s/%s", P_CARD, trel, tidy);
			if (access(there, F_OK) == 0) file = tidy;
		}
		if (snprintf(np, sizeof np, "%s/%s", trel, file) < (int)sizeof np)
			snprintf(t->path, sizeof t->path, "%s", np);
	}
	if (strcmp(frel, trel)) {
		for (k = 0; k < 2; k++) {
			snprintf(cfrom, sizeof cfrom, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         fbase + 1, EXT[k]);
			if (access(cfrom, F_OK) != 0) continue;
			snprintf(cto, sizeof cto, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         tbase + 1, EXT[k]);
			rename(cfrom, cto);
		}
		keys_follow(db_lib(), "mp4art.", frel, trel);
		keys_follow(db_dev(), "book.", frel, trel);
	}
	if (tidied) book_file_follows(trel, NULL, NULL);
	if (strcmp(frel, trel)) fprintf(stderr, "mp4: %s is now %s\n", frel, trel);
}

/* No network to be on, and an adapter that can be one: the screen makes the
 * handheld its own network (src/hotspot.h). On a network means an address,
 * as the screen needs it; a join still waiting for one is not. */
static bool xfer_wants_hotspot(void)
{
	char ip[64];

	if (!hotspot_available()) return false;
	return wifi_status(NULL, 0, ip, sizeof ip) != WIFI_CONNECTED || !ip[0];
}

/* The consoles as the transfer page sees their folders: every system the card
 * knows, from systems.cfg itself and not the launcher's shelves, which leave
 * out a system with no games - so an empty Atomiswave folder had no color on
 * the page (2026-11-06). Read when File Transfer opens. */
static systems_cfg g_xfer_sys;
static bool        g_xfer_sys_ok;

static const system_cfg *xfer_sys_of(const char *folder)
{
	int i;

	for (i = 0; g_xfer_sys_ok && i < g_xfer_sys.count; i++)
		if (!strcmp(g_xfer_sys.systems[i].folder, folder)) return &g_xfer_sys.systems[i];
	return NULL;
}

/* A system's folder, as xfer_set_systems asks: a console's own folder on
 * the page, which never moves, is renamed or is deleted. In any case, as
 * the card's folders are (vfat): "gba" is GBA's folder too. */
static bool xfer_folder_known(const char *folder)
{
	int i;

	for (i = 0; g_xfer_sys_ok && i < g_xfer_sys.count; i++)
		if (!strcasecmp(g_xfer_sys.systems[i].folder, folder)) return true;
	return false;
}

/* A console's color for its folder, as hare_set_folder_color asks: its
 * own, or Folder Color's for every folder (MENU > Display > Theme) */
static unsigned xfer_folder_color(const char *folder)
{
	const system_cfg *s = xfer_sys_of(folder);

	return s ? look_folder_rgb(s->accent) : CFG_NO_COLOR;
}

/* And its pixel art, as hare_set_folder_icon asks: the shelf's own for the
 * tag (home_art_key). Roms/BGM wore the cassette while it was menu music's;
 * the cassette is Menu Music's now, the card's folder in the sidebar */
static const char *xfer_folder_icon(const char *folder)
{
	const system_cfg *s = xfer_sys_of(folder);

	return s ? home_art_key(s->tag) : NULL;
}

/* The server, and over the cable SSH with it: both end with the screen, and
 * the cable goes back to charging only unless Developer Mode keeps it. Off
 * the cable this screen runs no SSH, and Developer Mode's over Wi-Fi, which
 * runs through it, goes on (dev_ssh_tick takes the name back). */
static void xfer_stop(bool cable)
{
	mdns_stop();
	if (cable) sshlink_stop();
	hare_set_ssh(NULL);
	hare_stop();
	hotspot_stop();     /* nothing when the screen was not the hotspot */
	if (cable) usblink_after_transfer(db_dev());
}

/* FILE TRANSFER'S TWO WAYS (DESIGN.md 11.35; 2026-11-13: "can't we use the
 * same upload/download popup design system in wifi file transfer as we do
 * for flash drive mode?"). Once someone came in, File Transfer's news is
 * Flash Drive's page with its two tiles, drawn by ud_tile: Download, to the
 * card (a browser's upload), and Upload, from it (a browser's download),
 * each with its speed, once a second, and its total since the screen
 * opened. A file's bytes only, as for the side lights: the page's own lists
 * and covers are not a transfer. A way that moved in the last 2 s is lit,
 * as on Flash Drive's page, and keeps its speed through a pause between
 * two files. Download's tile has the upload's bar at its foot while one is
 * on its way and its size is known. g_move is what the page draws. The
 * panel was the first try: at most 648 tall, its heading and rows over the
 * cable took 487 of it, and Flash Drive's tiles did not fit beside them. */
#define XS_FLOW_IDLE_MS 2000u   /* drivestat.h's DST_IDLE_MS */

/* A second's end for a way: its speed, lit or idle */
static void xs_flow_step(xs_flow *f, unsigned dt, unsigned now)
{
	if (f->win) {
		f->moved = now ? now : 1;
		f->active = true;
		f->rate = dt ? f->win * 1000ull / dt : 0;
	} else if (f->active && now - f->moved >= XS_FLOW_IDLE_MS) {
		f->active = false;
	}
	if (!f->active) f->rate = 0;
	f->win = 0;
}

static struct {
	bool  active[2];
	int   speed[2][16], ns[2], total[2][16], nt[2];
	float bar;                  /* the upload on its way, 0..1; below 0, none */
} g_move;

/* g_move from the two ways and the upload on its way, `got` of `of` bytes */
void xs_move_set(const xs_flow flow[2], long long got, long long of)
{
	int w;

	for (w = 0; w < 2; w++) {
		g_move.active[w] = flow[w].active;
		g_move.ns[w] = ud_rate(flow[w].rate, g_move.speed[w], 16);
		g_move.nt[w] = ud_total(flow[w].total, g_move.total[w], 16);
	}
	g_move.bar = of > 0 ? (float)((double)(got < 0 ? 0 : got) / (double)of) : -1.0f;
}

/* The page's rows (xs_page_draw reads them so; panel_show keeps them for
 * the page's way out): first the news, Now and what happened, or with no
 * `who` (Play after this file's wait) a line of its own; then Browsers and,
 * over the cable, SSH, on one line under it; and last the foot's hint, its
 * word as the label and its key as the value */
int xs_page_rows(menu_row *out, const char *who, const char *ssh, const char *now, str_id word)
{
	int n = 0;

	out[n++] = who ? (menu_row){ T(S_XFER_NOW), now, false } : MENU_NOTE(now);
	if (who) out[n++] = (menu_row){ T(S_XFER_BROWSERS), who, false };
	/* Over the cable, SSH beside the browsers (sshlink.h): who is signed in,
	 * or why nobody can be. */
	if (ssh) out[n++] = (menu_row){ "SSH", ssh, false };
	out[n++] = (menu_row){ T(word), "B", true };
	return n;
}

/* FILE TRANSFER'S NEWS ON FLASH DRIVE'S PAGE. Its top as Flash Drive's
 * (page_top): Zyper, and beside him what to type, the heading's first
 * line in **text** and its second in **text-soft**; the two tiles where
 * Flash Drive has them, the upload's bar at Download's foot; under them the
 * news in **meta** **text-soft** where Flash Drive's eject line is, and
 * under that Browsers (and SSH) in **meta**, the label **text-dim**; the
 * key's hint at the foot. */
static void xs_page_draw(app *a, const char *heading, const menu_row *rows, int n)
{
	ds_page_frame pf = page_top(a, 1.0f);
	int k;

	ds_page_head(a->r, &pf, DS_HEAD_TWO, heading, NULL, UI_GLYPH_COUNT, plat_now_ms());
	for (k = 0; k < 2; k++) {
		SDL_Rect q = ud_page_tile(k == 0);

		ud_tile(a->r, &q, &UD_PAGE, k == 0, g_move.active[k], g_move.speed[k], g_move.ns[k],
		        g_move.total[k], g_move.nt[k], k == 0 ? g_move.bar : -1.0f, NULL);
	}
	/* where Flash Drive's eject line is, the news, and under it the rest */
	if (n > 1) ds_page_meta(a->r, &pf, rows, 1, pf.news_y, plat_now_ms());
	if (n > 2) ds_page_meta(a->r, &pf, rows + 1, n - 2, pf.news_y + DS_PAGE_NEWS_STEP, plat_now_ms());
	if (n > 0) ud_page_foot(a->r, rows[n - 1].value, rows[n - 1].label, NULL, NULL);
}

/* The page as a panel kind (PANEL_MOVE): opened, closed and kept for its
 * way out as the hotspot's screen is, the whole screen being the panel */
void move_page(app *a, const char *heading, const menu_row *rows, int n, mm_pose pose)
{
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	SDL_Rect screen = { 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H };

	if (posed) posed = panel_pass_begin(a);
	xs_page_draw(a, heading, rows, n);
	if (posed) panel_pass_end(a, &screen, pose);
}

/* The page, through panel_show */
void xs_page_show(app *a, const char *heading, const menu_row *rows, int n)
{
	panel_show(a, PANEL_MOVE, heading, rows, n, -1, 0, 0u, NULL, false, 0, NULL);
}

/* One frame of it, for the shot harness, the tiles as the caller set them
 * (g_move). The rows come from the same function the screen uses, so a
 * picture of this cannot quietly stop being a picture of that. */
void hare_preview(app *a, const char *name, const char *addr, const char *pin,
                  const char *who, const char *ssh, const char *now, str_id word)
{
	menu_row rows[5];
	char head[192];
	int n = xs_page_rows(rows, who, ssh, now, word);

	hare_head(head, sizeof head, !strcmp(addr, "10.42.0.1"), name, addr, pin);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	xs_page_show(a, head, rows, n);
}

/* What the tiles say, for the screen's pace key: a change draws a frame */
static unsigned xs_move_key(unsigned key)
{
	int w, i;

	for (w = 0; w < 2; w++) {
		key = pace_int(key, g_move.active[w]);
		for (i = 0; i < g_move.ns[w]; i++) key = pace_int(key, g_move.speed[w][i]);
		for (i = 0; i < g_move.nt[w]; i++) key = pace_int(key, g_move.total[w][i]);
	}
	return pace_int(key, (int)(g_move.bar * 1000.0f));
}

/* The File Transfer page's SteamGridDB key (hare_set_sgdb). What goes back
 * is whether there is one, its last four, what is known about it, whether a
 * check is running, and the sentence for the last thing done - never the key.
 * The check runs in sgdb.c's thread, and xfer_screen's loop settles it. */
static void up_jstr(char *out, size_t n, const char *s);   /* with the updates, below */

static char *xfer_sgdb(int op, const char *body, size_t n, size_t *len,
                       char *why, size_t wn)
{
	char key[SGDB_KEY_MAX * 4], tail[8], *json, jsaid[400];
	const char *said, *outcome;
	sgdb_try t = SGDB_TRY_IDLE;
	int k;

	if (op == HARE_SGDB_KEY) {
		size_t m = n < sizeof key - 1 ? n : sizeof key - 1;

		memcpy(key, body, m);
		key[m] = '\0';
		t = sgdb_try_begin(key);
		memset(key, 0, sizeof key);
	} else if (op == HARE_SGDB_REMOVE) {
		sgdb_key_forget();
	}
	if (op != HARE_SGDB_REMOVE) t = sgdb_try_poll();
	said = op == HARE_SGDB_REMOVE ? T(S_XFER_KEY_REMOVED) : sgdb_try_said();
	outcome = t == SGDB_TRY_WORKS ? "works" : t == SGDB_TRY_KEPT ? "kept"
	        : t == SGDB_TRY_REFUSED ? "refused" : t == SGDB_TRY_BAD ? "bad" : "";
	sgdb_key_tail(tail, sizeof tail);
	if (!(json = malloc(512))) {
		snprintf(why, wn, "%s", T(S_XFER_ERR_MEMORY));
		return NULL;
	}
	/* Every value here is fixed text or letters and digits, but the sentence,
	 * which is in the language that is on: escaped, and room for it */
	up_jstr(jsaid, sizeof jsaid, said ? said : "");
	k = snprintf(json, 512,
	             "{\"key\":%s,\"tail\":\"%s\",\"state\":\"%s\",\"checking\":%s,"
	             "\"outcome\":\"%s\",\"said\":%s}",
	             sgdb_have_key() ? "true" : "false", tail, sgdb_status(),
	             t == SGDB_TRY_RUNNING ? "true" : "false", outcome, jsaid);
	if (k < 0 || k >= 512) {
		free(json);
		snprintf(why, wn, "%s", T(S_XFER_ERR_COULD_NOT_SAY));
		return NULL;
	}
	*len = (size_t)k;
	return json;
}

/* ---- File Transfer, open or behind the shelf ----------------------------
 *
 * 2026-11-06: "maybe instead of closing the channel it should
 * minimize? so transfer continues while we can operate the zyperOS? if gaming
 * affects it, prohibit gaming while transfering." So the server is a session
 * of its own and its screen is a view of it (DESIGN.md 11.14, my picks
 * in the lab, 2026-11-06): B closes it when nothing moves, and while files
 * move it asks - keep going behind the shelf, stop after the file on its way,
 * or cancel now, where the half-written file is a .part file deleted with its
 * connection and never a broken game on the shelf. MENU, while files move,
 * keeps them going and goes to the shelf, as MENU goes there from anything.
 *
 * Behind the shelf every screen's loop drives it through tick_theme, the
 * shelf's own on every pass, and the corner's mark says it is on (status.h).
 * A game cannot run beside it - a game takes the loop that moves the files,
 * and a port restarts the launcher - so launch asks first (xs_before_game).
 * Half an hour with no signed-in browser asking anything, behind the shelf,
 * ends it, and so do ten wrong PINs: a PIN that waits on the network for days
 * is a PIN somebody can work at. */
#define XS_QUIET_MS   (30u * 60u * 1000u)
#define XS_LINK_MS    10000u            /* the cable gone this long ends it */
#define XS_SHELF_MS   3000u             /* the files still this long: rescan */
#define XS_RESTART_MS 1500u             /* the page's restart answered first */
#define XS_WRONG_MAX  10                /* wrong PINs that end it behind the shelf */
#define XS_LEGEND     b_legend(S_KEYHINT_CLOSE)
#define XS_LEGEND_MOV b_legend(S_KEYHINT_KEEP_GOING_OR_STOP)
#define XS_LEGEND_END b_legend(S_KEYHINT_HIDE)

xfer_sess g_xs;

static void xs_stop(app *a, bool panels);

/* ---- System Update over the cable (hare_set_update) ----------------------
 *
 * The page brings a release's files from the computer's internet, where the
 * handheld on the cable has none (docs/updates.md, "Over the USB cable"): the
 * signed list and the pack, and for a card with two system slots the
 * system's list and pack too. They wait in <userdata>/update/cable/, and the
 * update program reads them there as it reads any release (--source
 * file://), so nothing is trusted for having come over the cable: the list's
 * signature is checked against the image's own keys, and every file's
 * SHA-256 against the list. An image with no release key takes none, as
 * System Update on Wi-Fi does, and the page says so. */
static void up_cable_dir(char *buf, size_t n) { up_path(buf, n, "cable"); }

static const char *up_device(void)
{
	const char *slash = strrchr(P_USERDATA, '/');

	return slash ? slash + 1 : P_USERDATA;
}

/* A release's file for this device, by the names docs/updates.md gives them:
 * the two lists exactly, and a pack with its version in its name */
static bool up_cable_name_ok(const char *name)
{
	const char *dev = up_device();
	char want[96];
	size_t n = strlen(name), sn;
	const char *p;

	snprintf(want, sizeof want, "ZyperOS-%s-update.txt", dev);
	if (!strcmp(name, want)) return true;
	snprintf(want, sizeof want, "ZyperOS-%s-system.txt", dev);
	if (!strcmp(name, want)) return true;
	if (strncmp(name, "ZyperOS-v", 9) || !xfer_name_ok(name)) return false;
	for (p = name + 9; *p; p++)
		if (!isalnum((unsigned char)*p) && !strchr(".-_+", *p)) return false;
	snprintf(want, sizeof want, "-%s-update.zpk", dev);
	sn = strlen(want);
	if (n > 9 + sn && !strcmp(name + n - sn, want)) return true;
	snprintf(want, sizeof want, "-%s-system.zpk", dev);
	sn = strlen(want);
	return n > 9 + sn && !strcmp(name + n - sn, want);
}

/* The update the page brought over the cable, once the update program is done
 * with it (xfer_update): a release is a few hundred MB of the card */
void up_cable_tidy(void)
{
	char dir[512], p[1024];
	struct dirent *e;
	DIR *d;

	if (up_running()) return;
	up_cable_dir(dir, sizeof dir);
	if (!(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p) remove(p);
	}
	closedir(d);
	rmdir(dir);
}

/* System Update's own rule (docs/updates.md, "Keys"): an image with no
 * /etc/zyperos/system, or no key to check a list with, takes no update. The
 * same overrides as the program's, for the checks and the VM. */
static bool up_supported(char *why, size_t wn)
{
	const char *keys = getenv("ZYPEROS_UPDATE_KEYS") ? getenv("ZYPEROS_UPDATE_KEYS")
	                 : "/etc/zyperos/update-keys";
	const char *sys = getenv("ZYPEROS_UPDATE_SYSTEM") ? getenv("ZYPEROS_UPDATE_SYSTEM")
	                : "/etc/zyperos/system";
	struct dirent *e;
	bool key = false;
	DIR *d;

	if (!up_have()) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_PROGRAM));
		return false;
	}
	if (access(sys, R_OK) != 0) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_SYSTEM_UPDATE));
		return false;
	}
	if ((d = opendir(keys))) {
		while (!key && (e = readdir(d))) {
			size_t n = strlen(e->d_name);

			key = n > 4 && !strcmp(e->d_name + n - 4, ".pem");
		}
		closedir(d);
	}
	if (!key) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_RELEASE_KEY));
		return false;
	}
	return true;
}

/* Start the program on the files the page brought */
static void up_spawn_cable(const char *cmd)
{
	char prog[512], dev[64], dir[512], src[600];
	char *argv[] = { prog, (char *)cmd, "--card", (char *)P_CARD, "--device", dev,
	                 "--source", src, NULL };

	snprintf(prog, sizeof prog, "%s/zyperos-update", P_ROOT);
	snprintf(dev, sizeof dev, "%s", up_device());
	up_cable_dir(dir, sizeof dir);
	snprintf(src, sizeof src, "file://%s/", dir);
	fprintf(stderr, "update: %s, the files from the cable\n", cmd);
	plat_spawn_detached(argv, NULL, P_ROOT);
	g_up_read_at = 0;
}

/* Text for JSON: what the program says in its messages, escaped */
static void up_jstr(char *out, size_t n, const char *s)
{
	size_t o = 0;

	if (n < 3) { if (n) out[0] = '\0'; return; }
	out[o++] = '"';
	for (; *s && o + 7 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
		else if (c < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
		else out[o++] = (char)c;
	}
	out[o++] = '"';
	out[o] = '\0';
}

static const char *const SU_NAME[] = {
	"idle", "unsupported", "checking", "uptodate", "available", "needs-system",
	"copying", "downloading", "verifying", "installing", "ready", "stopped",
	"error", "battery", "space", "rollback", "too-big",
};

/* What the page shows: whether updates work here and why not, the versions,
 * the card's slots, what the program is doing, and the files waiting */
static char *up_state_json(size_t *len, char *why, size_t wn)
{
	const su_view *v = up_view(true);
	char sysv[64] = "", text[256] = "", dir[512], no[200] = "";
	char msg[400], res[200], files[900] = "[", cmd[1024] = "", nwhy[400];
	bool on = up_supported(no, sizeof no), slots;
	struct dirent *e;
	DIR *d;
	char *json;
	int k;
	FILE *f;

	if ((f = fopen(getenv("ZYPEROS_UPDATE_SYSTEM") ? getenv("ZYPEROS_UPDATE_SYSTEM")
	                                                : "/etc/zyperos/system", "r"))) {
		if (fgets(text, sizeof text, f)) sscanf(text, "version %63s", sysv);
		fclose(f);
	}
	if ((f = fopen("/proc/cmdline", "r"))) {
		if (!fgets(cmd, sizeof cmd, f)) cmd[0] = '\0';
		fclose(f);
	}
	slots = strstr(cmd, "zyperos.slot=") != NULL;
	up_cable_dir(dir, sizeof dir);
	if ((d = opendir(dir))) {
		while ((e = readdir(d))) {
			size_t used = strlen(files);

			if (!up_cable_name_ok(e->d_name)) continue;
			if (used + strlen(e->d_name) + 4 >= sizeof files) break;
			snprintf(files + used, sizeof files - used, "%s\"%s\"", used > 1 ? "," : "", e->d_name);
		}
		closedir(d);
	}
	strcat(files, "]");
	up_jstr(msg, sizeof msg, v->st.message);
	up_jstr(res, sizeof res, v->result);
	up_jstr(nwhy, sizeof nwhy, no);
	if (!(json = malloc(4096))) { snprintf(why, wn, "%s", T(S_XFER_ERR_MEMORY)); return NULL; }
	k = snprintf(json, 4096,
	             "{\"on\":%s,\"why\":%s,\"device\":\"%s\",\"installed\":\"%s\",\"system\":\"%s\","
	             "\"slots\":%s,\"running\":%s,\"state\":\"%s\",\"version\":\"%s\",\"kind\":\"%s\","
	             "\"download\":%llu,\"done\":%llu,\"total\":%llu,\"files_done\":%d,\"files_total\":%d,"
	             "\"message\":%s,\"result\":%s,\"time\":%lld,\"battery\":%d,\"charging\":%s,"
	             "\"files\":%s}",
	             on ? "true" : "false", nwhy, up_device(), v->installed, sysv,
	             slots ? "true" : "false", v->running ? "true" : "false",
	             (unsigned)v->st.state < sizeof SU_NAME / sizeof SU_NAME[0] ? SU_NAME[v->st.state] : "idle",
	             v->st.version, v->st.system ? "system" : "folder",
	             v->st.download, v->st.done, v->st.total, v->st.files_done, v->st.files_total,
	             msg, res, v->st.time, v->battery, v->charging ? "true" : "false", files);
	if (k < 0 || k >= 4096) {
		free(json);
		snprintf(why, wn, "%s", T(S_XFER_ERR_COULD_NOT_SAY));
		return NULL;
	}
	*len = (size_t)k;
	return json;
}

static char *up_dup(const char *s, size_t *len)
{
	char *p = strdup(s);

	if (p) *len = strlen(p);
	return p;
}

/* The page's update routes (hare_set_update). Every name it sends is checked
 * here, and every byte by the update program. */
static char *xfer_update(int op, const char *name, char *path, size_t pn,
                         size_t *len, char *why, size_t wn)
{
	char dir[512], list[700];

	switch (op) {
	case HARE_UPDATE_FILE:
		if (!up_supported(why, wn)) return NULL;
		if (up_running()) { snprintf(why, wn, "%s", T(S_SYSUPDATE_BUSY)); return NULL; }
		if (!up_cable_name_ok(name)) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NOT_UPDATE_FILE));
			return NULL;
		}
		up_path(dir, sizeof dir, "");
		mkdir(dir, 0755);
		up_cable_dir(dir, sizeof dir);
		mkdir(dir, 0755);
		if (snprintf(path, pn, "%s/%s", dir, name) >= (int)pn) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NAME_TOO_LONG));
			return NULL;
		}
		return up_dup("{}", len);
	case HARE_UPDATE_CHECK:
	case HARE_UPDATE_INSTALL:
		if (!up_supported(why, wn)) return NULL;
		if (up_running()) { snprintf(why, wn, "%s", T(S_SYSUPDATE_BUSY_ALREADY)); return NULL; }
		up_cable_dir(dir, sizeof dir);
		snprintf(list, sizeof list, "%s/ZyperOS-%s-update.txt", dir, up_device());
		if (access(list, R_OK) != 0) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_SEND_FILES_FIRST));
			return NULL;
		}
		up_spawn_cable(op == HARE_UPDATE_CHECK ? "check" : "download");
		return up_state_json(len, why, wn);
	case HARE_UPDATE_RESTART:
		if (up_view(true)->st.state != SU_READY) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NONE_READY));
			return NULL;
		}
		g_xs.restart_at = plat_now_ms() ? plat_now_ms() : 1;
		return up_dup("{\"restarting\":true}", len);
	default:
		return up_state_json(len, why, wn);
	}
}

/* Files on their way: uploads and downloads the server is in the middle of */
bool xs_moving(void)
{
	return g_xs.on && hare_inflight() > 0;
}

/* Every empty .media in the consoles' folders, gone: rmdir takes only an
 * empty folder. Box Art and the Scraper made one before each try, and the
 * card itself came with one in every console until mk/payload.sh stopped
 * (2026-11-06). As File Transfer ends, and once a start's shelf is up
 * (after_start_thread). */
static void media_tidy(void)
{
	DIR *d = opendir(P_ROMS);
	struct dirent *e;
	char p[LIB_PATH * 2];

	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s/.media", P_ROMS, e->d_name) < (int)sizeof p)
			rmdir(p);
	}
	if (d) closedir(d);
}

/* After a start's first frame, on a thread of their own: the card read again
 * against the shelves the saved library put up (lib_check, src/shelfbuild.h),
 * and media_tidy, which was before the first frame and is not wanted there:
 * each console's .media with box art in it is a folder the scan never reads,
 * and rmdir has to look inside it to find it is not empty, which after a cold
 * boot is a read off the card for each one, 150-195 ms on my card (35
 * folders, 2026-11-07). Both read the card, so both wait until the art the
 * first frame asked for is in (after_start_step). */
static struct {
	lib_check   *check;
	bool         begun, started;
	unsigned     at;
	SDL_atomic_t done;
} g_after;

static int after_start_thread(void *unused)
{
	(void)unused;
	if (g_after.check) lib_check_run(g_after.check);
	if (g_after.check) lib_check_import(g_after.check);
	media_tidy();
	/* A card's loose achievement badges into their sets' packs, once: a
	 * hundred thousand files made fsck.exfat take minutes (src/rapack.h) */
	radl_badges_migrate(P_ROMS);
	SDL_AtomicSet(&g_after.done, 1);
	return 0;
}

/* gamelist.xml's names and favorites, which lib_check_import just wrote to
 * the card's library, on the shelves at once: each game's title, its
 * favorite, and its shelf put in order again on the game it was on. `ctx`
 * says whether a favorite was added. */
typedef struct { app *a; bool fav, titles; } es_apply;

static void es_imported(const char *folder, const es_import_list *l, void *ctx)
{
	app *a = ((es_apply *)ctx)->a;
	bool *fav = &((es_apply *)ctx)->fav;

	for (int si = 0; si < a->sys.count; si++) {
		sysview *v = &a->view[si];
		const system_cfg *sc = &a->sys.systems[si];
		char file[LIB_PATH] = "";
		bool changed = false;
		int at;

		if (v->owner || is_mp4(sc) || strcmp(sc->folder, folder) || v->list.count <= 0) continue;
		snprintf(file, sizeof file, "%s", v->list.items[v->cursor].file);
		for (int i = 0; i < l->n; i++) {
			int k = lib_find_entry(&v->list, NULL, 0, l->got[i].file);
			game_entry *e;

			if (k < 0) continue;
			e = &v->list.items[k];
			if (l->got[i].title[0] && strcmp(e->title, l->got[i].title)) {
				snprintf(e->title, sizeof e->title, "%s", l->got[i].title);
				changed = true;
			}
			if (l->got[i].favorite && !fav_is(sc->tag, e->file)) {
				fav_toggle(sc->tag, e->file);
				*fav = true;
			}
		}
		if (!changed) continue;
		((es_apply *)ctx)->titles = true;
		free_view_textures(v);
		sort_shelf(a, si);
		if ((at = lib_find_entry(&v->list, NULL, 0, file)) >= 0) v->cursor = at;
		cf_reset(&v->cf, v->cursor);
		redraw_now();
	}
}

/* A shelf pass's share of it: begun at the first pass after the first frame,
 * started once texload is idle or 3 s on (art that never settles is no
 * reason to wait for ever), and the check applied here, on this thread, when
 * it is done. `drawn`: the first frame is up. */
void after_start_step(app *a, bool drawn)
{
	unsigned now;

	if (!drawn) return;
	now = plat_now_ms();
	if (!g_after.begun) {
		g_after.begun = true;
		g_after.at = now;
		g_after.check = lib_check_begin();
		return;
	}
	if (!g_after.started) {
		SDL_Thread *t;

		if (!texload_idle() && now - g_after.at < 3000) return;
		g_after.started = true;
		t = SDL_CreateThread(after_start_thread, "zyperos-after", NULL);
		if (t) SDL_DetachThread(t);
		else after_start_thread(NULL);
		return;
	}
	if (g_after.check && SDL_AtomicGet(&g_after.done)) {
		es_apply ea = { a, false, false };

		if (lib_check_apply(a, g_after.check)) redraw_now();
		lib_check_imported(g_after.check, es_imported, &ea);
		if (ea.fav) fav_save();
		/* its copies of the games, with their names and the new ones */
		if (ea.fav || ea.titles) refresh_favorites_shelf(a);
		lib_check_free(g_after.check);
		g_after.check = NULL;
	}
}

/* Open the server: the hooks, the hotspot when there is no network, the PIN,
 * SSH over the cable. False when it could not, having said why. */
/* The cable's SSH and its name, once usb0 has its address and the page's
 * first frame is up: not as the screen opens, when the link may still be on
 * its way (gadget.h) and dropbear would find no address to listen on, and
 * never between the press and the page. A Developer Mode SSH already there
 * on the cable takes File Transfer's PIN in place, its sessions kept
 * (sshlink_repin), rather than a stop and a start (which waited for the old
 * dropbear to end, and ended its sessions). */
static void xs_cable_services(const char *ip)
{
	char keys[600];
	unsigned t0 = plat_now_ms();

	snprintf(keys, sizeof keys, "%s/ssh", P_USERDATA);
	snprintf(g_xs.login, sizeof g_xs.login, "root@%s", ip);
	if (!sshlink_repin(ip, g_xs.pin)) sshlink_start(ip, g_xs.pin, keys);
	/* zyperos.local on the cable: only the computer is on this link, so
	 * the name is ours at once, with no probing (mdns.h) */
	if (!mdns_start("usb0", ip, false)) fprintf(stderr, "xfer: no zyperos.local on the cable\n");
	g_xs.services = true;
	fprintf(stderr, "xfer: the cable's SSH and name in %u ms\n", plat_now_ms() - t0);
}

static bool xs_open(app *a, int way)
{
	char        ip[64], path[512];
	unsigned    t0 = plat_now_ms(), t_link, t_art, t_sys;
	/* A phone's tethering (way 2, or whichever: the port a host with a
	 * phone's network on it, which the cable cannot be then) */
	const bool  phone = way == 2 || (way < 0 && !plat_cable_link(NULL, 0) &&
	                                 plat_phone_link(false, NULL, 0, ip, sizeof ip) && ip[0]);
	const bool  cable = !phone && (way < 0 ? plat_cable_link(NULL, 0) : way == 1);
	const char *name  = cable || phone ? XFER_NAME_CABLE : XFER_NAME_WIFI;
	const bool  hot   = !cable && !phone && xfer_wants_hotspot();
	bool        due   = g_xs.rescan_due;

	memset(&g_xs, 0, sizeof g_xs);
	g_xs.rescan_due = due;
	g_xs.cable = cable;
	g_xs.phone = phone;
	g_xs.hot = hot;
	g_xs.name = name;
	g_xs.hs = HOTSPOT_OFF;
	g_logs_app = a;
	/* The link on, since USB charges only until asked (usblink.h); and a
	 * fresh one when it needs it, since a computer can drop it while the
	 * device believes it is still up: not whole, or the computer not
	 * answering over it (plat_cable_reconnect, cablefresh.h). Neither waits
	 * on the controller for long (hwput.h). */
	if (cable) {
		usblink_for_transfer();
		plat_cable_reconnect();
	}
	t_link = plat_now_ms();
	hare_set_logs(pack_logs);
	hare_set_before_delete(mp4_before_delete);
	hare_set_after_rename(mp4_after_rename);
	/* Box art fetched by the page is USB File Transfer's alone (hareart.h);
	 * a cover uploaded into a shelf's .media is shrunk on every device. */
	hareart_init(&a->sys, P_ROMS);
	singles_cover();                /* back, if it was taken off the card */
	t_art = plat_now_ms();
	/* the phone's browser has the phone's internet, as a computer's has */
	if (cable || phone) {
		hare_set_art(hareart_wanted, hareart_match, hareart_crc, hareart_nointro);
		hare_set_albums(albums_wanted, album_pick);
	} else {
		hare_set_art(NULL, NULL, NULL, NULL);
		hare_set_albums(NULL, NULL);
	}
	hare_set_after_write(xfer_after_write);
	/* The SteamGridDB key, pasted on the page: over Wi-Fi it is checked at
	 * once; over the cable, which has no internet, it is kept unchecked */
	hare_set_sgdb(xfer_sgdb);
	/* Updates through the computer, where the handheld has no internet of
	 * its own: over the cable (docs/updates.md) */
	hare_set_update(cable || phone ? xfer_update : NULL);
	/* The page's name, the one this screen shows: USB File Transfer over
	 * the cable and a phone's tethering, Wi-Fi File Transfer over Wi-Fi and
	 * the hotspot */
	hare_set_cable(cable || phone);
	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	g_xfer_sys_ok = cfg_load_systems(path, &g_xfer_sys);
	/* The folders the shelves read, EmulationStation's where they chose one */
	if (g_xfer_sys_ok) shelf_folders_apply(&g_xfer_sys);
	hare_set_folder_color(xfer_folder_color);
	hare_set_folder_icon(xfer_folder_icon);
	/* A console's own folder stays put (xfer_console); without systems.cfg,
	 * every folder at the top of ROMs is taken for one */
	xfer_set_systems(g_xfer_sys_ok ? xfer_folder_known : NULL);
	snprintf(path, sizeof path, "%s/system-icons", P_ROOT);
	hare_set_icons(path);
	t_sys = plat_now_ms();
	if (hot) {
		wait_panel(a, name, T(S_XFER_STARTING_HOTSPOT));
		/* Up before the code is shown, so a phone that scans it at once can
		 * join: a second or two, and hotspot_poll gives up at fifteen */
		if (hotspot_start())
			while ((g_xs.hs = hotspot_poll()) == HOTSPOT_STARTING) SDL_Delay(50);
		if (g_xs.hs != HOTSPOT_UP) {
			menu_row row = { T(S_XFER_HOTSPOT_FAILED), NULL, false };

			hotspot_stop();
			timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
			return false;
		}
	}
	if (!hare_start(P_ROMS, P_CARD, P_SHARED, P_WEB)) {
		menu_row row = { T(S_XFER_COULD_NOT_START), NULL, false };

		if (hot) hotspot_stop();
		/* Over **scrim.menu**, as every message is (DESIGN.md 11.8). */
		timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
		return false;
	}
	/* Every name a phone looks up is the handheld on its own network, so a
	 * request for another host is its internet check (src/captive.h). */
	if (hot) {
		/* The redirect names the port too when the server is not on 80 (a
		 * phone's check still goes to 80, but a hand-typed address may not) */
		char self[32];

		if (hare_port() == 80) snprintf(self, sizeof self, "%s", hotspot_addr());
		else snprintf(self, sizeof self, "%s:%d", hotspot_addr(), hare_port());
		hare_set_captive(self);
		/* zyperos.local there too, for a phone that asks by mDNS rather
		 * than the hotspot's DNS (which answers every name with itself) */
		mdns_start("wlan0", hotspot_addr(), true);
	}
	snprintf(g_xs.pin, sizeof g_xs.pin, "%s", hare_pin());
	/* SSH over the same cable, behind the same PIN (sshlink.h), and its
	 * name: once the page is up and usb0 has its address (xs_tick) */
	g_xs.on = true;
	/* Where the opening's time went: the link is asked for, never waited
	 * on (gadget.h), so it is a few ms; the rest is the card's */
	fprintf(stderr, "xfer: opened in %u ms (link %u, art %u, systems %u, server and SSH %u)\n",
	        plat_now_ms() - t0, t_link - t0, t_art - t_link, t_sys - t_art, plat_now_ms() - t_sys);
	return true;
}

/* The server's slice of a frame, whichever screen is up: bounded, like
 * everything in a frame (httpd.h). */
void xs_tick(app *a)
{
	unsigned now = plat_now_ms();
	char ssid[WIFI_SSID_MAX], ip[64];

	if (!g_xs.on) return;
	g_xs.busy = hare_poll();
	hare_status(&g_xs.st);
	if (hare_signed_once() || (g_xs.cable && sshlink_sessions() > 0)) g_xs.met = true;
	/* A batch of files in: the side lights say so (src/sidelights.h) */
	if (side_lights_batch_done(&g_xs.lit, g_xs.st.uploads > 0, now))
		plat_side_lights_say(SIDE_LIGHTS_DONE);
	/* Files moving: the side lights' wave, down while a file comes in and
	 * up while one goes out; a file's bytes only (side_lights_move_files) */
	{
		char line[64];

		if (side_lights_move_files(&g_xs.mv, g_xs.st.uploads, g_xs.st.sends, g_xs.st.in, g_xs.st.out,
		                           now, line, sizeof line))
			plat_side_lights_say(line);
	}
	/* A key the page sent, settled the frame its check ends */
	sgdb_try_poll();
	mdns_poll(now);
	if (g_xs.cable && g_xs.drawn && !g_xs.services && sshlink_if_addr("usb0", ip, sizeof ip))
		xs_cable_services(ip);
	/* The link, step by step (linksteps.h): the cable's facts five times a
	 * second; Wi-Fi's from the address check below */
	if (g_xs.cable) {
		if (!g_xs.sense_at || now - g_xs.sense_at >= 200u) {
			int was = g_xs.steps.step, sub = g_xs.steps.sub;

			g_xs.sense_at = now ? now : 1;
			g_xs.sensed = plat_cable_sense(&g_xs.sense);
			if (g_xs.sensed) {
				link_steps_cable(&g_xs.steps, &g_xs.sense, g_xs.st.requests, now);
				if (g_xs.steps.step != was || g_xs.steps.sub != sub)
					fprintf(stderr, "xfer: link step %d.%d (computer %d configured %d carrier %d address %d)\n",
					        g_xs.steps.step, g_xs.steps.sub, g_xs.sense.computer,
					        g_xs.sense.configured, g_xs.sense.carrier, g_xs.sense.address);
			}
		}
	} else if (!g_xs.hot && g_xs.addr[0]) {
		link_steps_wifi(&g_xs.steps, g_xs.joined, g_xs.st.requests, now);
	}
	if (g_xs.cable) {
		sshlink_note_web(hare_wrong_pins());
		sshlink_poll();
		/* The page shows the command only while SSH listens */
		if (g_xs.ssh_shown != sshlink_on()) {
			g_xs.ssh_shown = sshlink_on();
			hare_set_ssh(g_xs.ssh_shown ? g_xs.login : NULL);
		}
	}
	g_xs.in  += g_xs.st.in;
	g_xs.out += g_xs.st.out;
	/* The tiles' ways: a file's bytes, their speed once a second */
	{
		unsigned long long fin = g_xs.st.uploads > 0 || g_xs.flow_up ? g_xs.st.in : 0;
		unsigned long long fout = g_xs.st.sends > 0 || g_xs.flow_send ? g_xs.st.out : 0;

		g_xs.flow_up = g_xs.st.uploads > 0;
		g_xs.flow_send = g_xs.st.sends > 0;
		g_xs.flow[0].total += fin;
		g_xs.flow[0].win += fin;
		g_xs.flow[1].total += fout;
		g_xs.flow[1].win += fout;
		if (!g_xs.flow_at) {
			g_xs.flow_at = now ? now : 1;
		} else if (now - g_xs.flow_at >= 1000u) {
			xs_flow_step(&g_xs.flow[0], now - g_xs.flow_at, now);
			xs_flow_step(&g_xs.flow[1], now - g_xs.flow_at, now);
			g_xs.flow_at = now ? now : 1;
		}
	}
	if (g_xs.st.in || g_xs.st.out || hare_inflight() > 0) g_xs.moved_at = now ? now : 1;
	/* The hotspot's DNS answers and its start, every frame (bounded) */
	if (g_xs.hot && (g_xs.hs = hotspot_poll()) == HOTSPOT_FAILED) g_xs.hot_failed = true;

	/* Two seconds, because wifi_status forks wpa_cli and this runs every
	 * frame. Same cache the About screen uses. Over a cable the address
	 * never changes, so it is always shown, and the browser row says
	 * whether anyone is on the other end. */
	if (g_xs.hot) {
		/* Its own address, which never changes; port 80 needs no colon */
		if (hare_port() == 80) snprintf(g_xs.addr, sizeof g_xs.addr, "%s", hotspot_addr());
		else snprintf(g_xs.addr, sizeof g_xs.addr, "%s:%d", hotspot_addr(), hare_port());
	} else if (!g_xs.addr[0] || now - g_xs.addr_at > 2000u) {
		bool up = g_xs.cable ? plat_cable_link(ip, sizeof ip)
		        : g_xs.phone ? plat_phone_link(false, NULL, 0, ip, sizeof ip) && ip[0]
		        : wifi_status(ssid, sizeof ssid, ip, sizeof ip) == WIFI_CONNECTED && ip[0];

		g_xs.addr_at = now;
		/* On a phone's tethering, joined while the phone gives an address;
		 * no name there (a phone's browser does not look names up by mDNS),
		 * the address shown alone (xs_way_set) */
		if (g_xs.phone) g_xs.joined = up;
		/* Over Wi-Fi the name follows the network: on it (a new address
		 * starts it again there), or off with it */
		else if (!g_xs.cable) {
			g_xs.joined = up;
			if (up) mdns_start("wlan0", ip, true);
			else mdns_stop();
		}
		if (up) {
			/* Port 80 needs no colon, and the address is being copied by
			 * hand off a three-inch screen - so it is only shown when it is
			 * not the one everybody assumes. */
			if (hare_port() == 80) snprintf(g_xs.addr, sizeof g_xs.addr, "%s", ip);
			else snprintf(g_xs.addr, sizeof g_xs.addr, "%s:%d", ip, hare_port());
			g_xs.link_lost_at = 0;
		} else {
			snprintf(g_xs.addr, sizeof g_xs.addr, "%s",
			         g_xs.cable || g_xs.phone ? T(S_XFER_CABLE_UNPLUGGED) : T(S_XFER_WIFI_GONE));
			if (!g_xs.link_lost_at) g_xs.link_lost_at = now ? now : 1;
		}
	}

	/* Auto Off must not shut the device down in the middle of a 900MB
	 * upload, and it also must not shut it down while somebody is reading
	 * a listing on the other side of the room. Bytes moving is obvious
	 * activity; a connected browser is a person at the other end of it,
	 * which is the same claim a button press makes and no weaker for
	 * arriving over a network. Neither suspends Auto Off - close the tab
	 * and the countdown resumes. Said as every long task says it
	 * (src/taskhold.h), where idle_due reads it. */
	if (g_xs.busy || g_xs.st.clients > 0 || sshlink_sessions() > 0 ||
	    (g_xs.hot && hotspot_phones() > 0))
		task_note(TASK_TRANSFER, now);

	/* The page asked for a restart to install an update (xfer_update): a
	 * moment for its answer to go out, then as System Update's Restart */
	if (g_xs.restart_at && now - g_xs.restart_at > XS_RESTART_MS) {
		g_xs.restart_at = 0;
		xfer_stop(g_xs.cable);
		g_xs.on = false;
		uisound_play(DS_SOUND_ACCEPT);
		remember_place(a);
		book_keep(true);
		plat_request_reboot();
		plat_backlight(false);
		a->running = false;
		return;
	}

	/* Behind the shelf: the stop the question asked for, once the files on
	 * their way are in; the hotspot gone; the cable out; and half an hour of
	 * nobody asking anything. On its screen, the screen does these. */
	if (!g_xs.shown) {
		bool quiet = hare_inflight() == 0 && sshlink_sessions() == 0 &&
		             !(g_xs.hot && hotspot_phones() > 0);

		/* Nobody is at the screen to read the PIN behind the shelf, so a
		 * run of wrong ones is somebody else guessing: it ends */
		bool guessed = hare_wrong_pins() >= XS_WRONG_MAX;

		if ((hare_closing() && hare_inflight() == 0) || g_xs.hot_failed || guessed ||
		    /* the link gone, or with an upload paused (hare_paused) as long
		     * as the page waits for it to come back */
		    (g_xs.link_lost_at && now - g_xs.link_lost_at > (hare_paused() ? HARE_PAUSE_MS : XS_LINK_MS)) ||
		    (quiet && hare_quiet_ms() > XS_QUIET_MS)) {
			fprintf(stderr, "xfer: closed behind the shelf (%s)\n",
			        hare_closing() ? "after the files" : g_xs.hot_failed ? "the hotspot stopped"
			        : guessed ? "wrong PINs" : g_xs.link_lost_at ? "the link went"
			        : "nobody asked for 30 min");
			xs_stop(a, false);
		}
	}
}

/* From tick_theme: behind the shelf only, since the screen drives it itself */
void xs_tick_behind(app *a)
{
	if (g_xs.on && !g_xs.shown) xs_tick(a);
}

/* The shelf's turn: read again once the files that arrived behind it have
 * been still a moment, and once a session that ended behind it changed it.
 * Not as each file lands: a batch of twenty would rescan twenty times, and
 * every one would stall the loop that is still receiving. */
bool xs_shelf_due(void)
{
	if (g_xs.rescan_due) {
		g_xs.rescan_due = false;
		return true;
	}
	if (g_xs.on && !g_xs.shown && hare_shelf_changed() && hare_inflight() == 0 &&
	    plat_now_ms() - g_xs.moved_at > XS_SHELF_MS) {
		hare_shelf_seen();
		return true;
	}
	return false;
}

bool xs_row_cable(void) { return g_xs.cable || g_xs.phone; }

/* The corner's mark: on behind the shelf, and the ring as far as the
 * upload on its way (status.h). Not while its own screen is up. */
void xs_mark(status_marks *m)
{
	m->xfer = g_xs.on && !g_xs.shown;
	m->xfer_moving = m->xfer && xs_moving();
	m->xfer_pct = m->xfer_moving && g_xs.st.up_total > 0
	            ? (int)(g_xs.st.up_got * 100 / g_xs.st.up_total) : 0;
}

/* MENU's row while it runs: what it is doing */
const char *xs_row_value(void)
{
	if (!g_xs.on) return NULL;
	if (hare_closing()) return T(S_XFER_ROW_CLOSING);
	if (g_xs.st.uploads > 0 || g_xs.st.paused > 0) return T(S_XFER_ROW_RECEIVING);
	if (g_xs.st.sends > 0) return T(S_XFER_ROW_SENDING);
	return T(S_XFER_ROW_ON);
}

/* End it: the server, SSH, the hotspot; the empty .media its covers left;
 * and the shelf read again if anything on it moved - now, with `panels`, or
 * on the shelf's next pass (xs_shelf_due) when it ends behind another
 * screen, which a rescan must not change under. */
static void xs_stop(app *a, bool panels)
{
	bool changed;

	if (!g_xs.on) return;
	fprintf(stderr, "xfer: closed\n");
	/* Asked before hare_stop, which is what owning the server means: after
	 * it, there is nobody left to ask. */
	changed = hare_shelf_changed();
	if (panels && g_xs.hot) {
		if (g_xs.hot_failed) {
			menu_row row = { T(S_XFER_HOTSPOT_STOPPED), NULL, false };

			timed_message(a, g_xs.name, &row, 1, 0, MENU_ZYPEROS);
		}
		/* The hotspot down and the Wi-Fi the player had back up, which
		 * takes the supplicant a second or two to answer (xfer_stop) */
		wait_panel(a, g_xs.name, T(S_XFER_CLOSING_HOTSPOT));
	}
	xfer_stop(g_xs.cable);
	g_xs.on = false;
	g_xs.shown = false;
	{
		char line[64];

		/* A file cut off on its way: the side lights rest at once */
		if (side_lights_move_end(&g_xs.mv, line, sizeof line)) plat_side_lights_say(line);
	}
	media_tidy();
	up_cable_tidy();
	/* Menu music read again, for what came into Menu Music: on its own
	 * thread, and quick, so whatever the session changed */
	bgm_rescan();
	/* The point of the whole feature is getting ROMs onto the card, and the
	 * shelf is scanned once at startup - so without this the file lands,
	 * the screen says it landed, and the game is not there. */
	if (changed) {
		if (panels) {
			wait_panel(a, g_xs.name, T(S_XFER_SCANNING));
			rescan_all(a);
		} else {
			g_xs.rescan_due = true;
		}
	}
}

/* The power going, or the launcher: whatever is open goes with it, the
 * hotspot too, or Wi-Fi stays off under a hostapd nobody stops */
void xs_end(void)
{
	if (!g_xs.on) return;
	xfer_stop(g_xs.cable);
	g_xs.on = false;
	g_xs.shown = false;
}

/* The tiles (g_move) from the session as it is now */
static void xs_move_now(void)
{
	/* a paused upload's bar where it stopped (hare_status) */
	bool up = g_xs.st.uploads > 0 || g_xs.st.paused > 0;

	xs_move_set(g_xs.flow, up ? g_xs.st.up_got : 0, up ? g_xs.st.up_total : 0);
}

/* Play after this file's wait (xs_before_game), on the page: its line,
 * saying which file, where the news is, and B's hint */
int xs_finish_rows(menu_row *rows, char *line, size_t ln, const char *file)
{
	if (file && file[0]) snprintf(line, ln, T(S_XFER_FINISHING), file);
	else snprintf(line, ln, "%s", T(S_XFER_FINISHING_THE_FILE));
	return xs_page_rows(rows, NULL, NULL, line, S_KEYHINT_BACK_TO_SHELF);
}

/* The two questions' words (DESIGN.md 11.14): the tiles, two lines each, and
 * what each does, said under them while it is focused. The shot harness's
 * --xfer-ask draws these same ones. Each is three IDs, read through T() as
 * it is asked, so XS_STOP_L and the rest are arrays of strings as they were. */
static const str_id XS_WORDS[4][3] = {
	{ S_XFER_TILE_KEEP_GOING, S_XFER_TILE_STOP_AFTER, S_XFER_TILE_CANCEL_NOW },
	{ S_XFER_KEEP_GOING_SAYS, S_XFER_STOP_AFTER_SAYS, S_XFER_HALF_DELETED },
	{ S_XFER_TILE_NOT_NOW, S_XFER_TILE_PLAY_AFTER, S_XFER_TILE_CANCEL_PLAY },
	{ S_XFER_NOT_NOW_SAYS, S_XFER_PLAY_AFTER_SAYS, S_XFER_HALF_DELETED },
};

/* Their marks, a tile each: two arrows for going on, a page with a tick for
 * after this file, a play triangle for play after it, a ring crossed for
 * cancel */
const int XS_MARKS[2][3] = {
	{ UI_GLYPH_ARROWS, UI_GLYPH_FINISH, UI_GLYPH_CANCEL },
	{ UI_GLYPH_ARROWS, UI_GLYPH_PLAY, UI_GLYPH_CANCEL },
};

const char *const *xs_words(int which)
{
	static const char *w[4][3];
	int i;

	for (i = 0; i < 3; i++) w[which][i] = T(XS_WORDS[which][i]);
	return w[which];
}
/* A question while files move (DESIGN.md 11.14): three tiles over the shelf,
 * as Computer Connected's (My pick in the lab, 2026-11-06), a line under
 * the rule saying what moves, and under the tiles what the focused one does.
 * The server keeps moving the files all the while. Returns the choice, -1
 * for B, or -2 when the files finished while it was up and it no longer has
 * anything to ask about. `what` may hold one %s, the file on its way. */
static int xs_ask(app *a, const char *heading, const char *const *labels, const int *marks,
                  const char *const *what, int n, const char *legend)
{
	menu_row rows[7];
	char line[160], sub[192];
	int sel = 0, choice = -3, lv, i;
	float k;

	if (n > 4) n = 4;
	lv = panel_open();
	while (choice == -3) {
		xs_tick(a);
		if (!g_xs.on || !xs_moving()) { choice = -2; break; }
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; choice = -1; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			panel_close(a, lv, true);
			xs_end();
			power_off(a);
			return -1;
		}
		/* B: the first answer, which loses nothing; MENU: the same, and on
		 * to the shelf */
		if (menu_leaving(a)) { choice = -1; break; }
		/* each button read once a frame: a second read of a held Left
		 * was false and turned it Right (in_repeat_axis) */
		int lr = in_repeat_axis(&a->in, IN_LEFT, IN_RIGHT);

		if (lr) {
			in_button b = lr < 0 ? IN_LEFT : IN_RIGHT;

			moved_sound(a, b);
			sel = (sel + lr + n) % n;
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			choice = sel;
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		for (i = 0; i < n; i++) rows[i] = (menu_row){ labels[i], NULL, true };
		if (g_xs.st.file[0])
			snprintf(sub, sizeof sub,
			         T(g_xs.st.uploads > 0 || g_xs.st.paused > 0 ? S_XFER_RECEIVING : S_XFER_SENDING),
			         g_xs.st.file);
		else
			snprintf(sub, sizeof sub, "%s",
			         T(g_xs.st.uploads > 0 || g_xs.st.paused > 0 ? S_XFER_RECEIVING_A_FILE
			                                                         : S_XFER_SENDING_A_FILE));
		snprintf(line, sizeof line, what[sel],
		         g_xs.st.file[0] ? g_xs.st.file : T(S_XFER_THE_FILE));
		rows[n]     = MENU_NOTE(sub);
		rows[n + 1] = MENU_NOTE(line);
		rows[n + 2] = MENU_NOTE(legend);
		tiles_show(a, PANEL_TILES, heading, rows, n + 3, marks, n, sel);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(4);
	}
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return choice;
}

/* B while files move: keep going behind the shelf, stop after the file on
 * its way, or cancel now. 0, 1, 2; -1 for B again, which keeps going too. */
static int xs_stop_ask(app *a)
{
	return xs_ask(a, T(S_XFER_STILL_MOVING), XS_STOP_L, XS_STOP_M, XS_STOP_W, 3, XS_STOP_LEGEND);
}

/* A game asked for while File Transfer is on (My note: "if gaming affects it,
 * prohibit gaming while transfering"; it does - the game takes the loop that
 * moves the files, and a port restarts the launcher). With nothing moving,
 * it closes for the game and says so; with files on their way, the player
 * says: not now, after the file on its way, or cancel and play. True when
 * the game may start. */
bool xs_before_game(app *a)
{
	const char *name = g_xs.name;
	int c;

	if (!g_xs.on) return true;
	c = xs_moving() ? xs_ask(a, T(S_XFER_STILL_MOVING), XS_GAME_L, XS_GAME_M, XS_GAME_W, 3, XS_GAME_LEGEND)
	                 : -2;
	if (!a->running) return false;
	if (c == 0 || c == -1) return false;
	if (c == 1) {
		/* After the file on its way: the page is told nothing new starts,
		 * and this waits for it, B going back with it still to close; on
		 * the news's page, the tiles over its line */
		menu_row rows[3];
		char line[192];
		int lv = panel_open();
		float k;

		hare_close_after();
		while (g_xs.on && xs_moving()) {
			xs_tick(a);
			plat_input_poll(&a->in);
			ui_sounds_follow();
			if (a->in.quit_requested) { a->running = false; break; }
			if (menu_leaving(a)) break;
			if (!panel_frame(a, lv, &k)) continue;
			tick_tint(a);
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
			xs_move_now();
			xs_page_show(a, name, rows, xs_finish_rows(rows, line, sizeof line, g_xs.st.file));
			plat_draw_osd(a->r);
			plat_present();
			ui_frame_rest(4);
		}
		panel_close(a, lv, !a->running);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		if (!a->running || xs_moving()) return false;
		xs_stop(a, false);
		return true;
	}
	/* Cancel and play (2), or nothing was moving (-2): closed for the game,
	 * the shelf read again after it if anything arrived */
	if (c == 2) hare_drop_paused();
	xs_stop(a, false);
	if (c == -2) {
		menu_row row = { T(S_XFER_CLOSED_FOR_GAME), NULL, false };

		timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
	}
	return true;
}

/* The steps instead of the usual rows (linksteps.h): until a browser asked,
 * over the cable where the device can tell (plat_cable_sense) and over Wi-Fi
 * (never the hotspot, whose screen says what it says), and nothing moving */
static bool xs_steps_shown(void)
{
	if (g_xs.hot || !g_xs.steps.started || g_xs.steps.step == LS_DONE || xs_moving()) return false;
	/* A phone's tethering opens File Transfer only once it has an address,
	 * so no step before the browser is the player's to fix: none shown */
	if (g_xs.phone) return false;
	return g_xs.cable ? g_xs.sensed : true;
}

/* The way the device has now: over the cable when it links, else over the
 * air, as MENU's row says - or the one already open */
void xfer_screen(app *a)
{
	xfer_screen_way(a, -1);
}

/* `way` 1 over the cable, 0 over the air, -1 whichever the device has: the
 * empty home starts the one its button names (src/emptyhome.h). Already on,
 * behind the shelf, it is that one, shown again with the same PIN. */
void xfer_screen_way(app *a, int way)
{
	char       who[64], head[192], now_line[160], name[96];
	char       hint[320], lines[3][LB_LINE];
	menu_row   rows[STEP_ROWS > 7 ? STEP_ROWS : 7];
	int        nrows, lv;
	/* which view is up: the way in, the cable's loading bar, the steps, the
	 * news, the hotspot's */
	enum { WAY_IN, WAY_LOAD, WAY_STEPS, WAY_NEWS, WAY_HOT } view;
	unsigned   cover_at = 0, cover_next = 0;   /* the embedded covers, below */
	int        cover_left = -1, cover_albums = -1;
	unsigned long long cover_moved = 0;
	bool       done = false, stop = false;
	float      k;
	pace       pc;
	/* Left alone on the cable, Auto Off's time turns the screen off, or
	 * only dims it while files move (src/pace.h, pace_screen_step) */
	pace_screen scr;
	unsigned   asked_at = plat_now_ms(), drawn_at;
	bool       first = true;

	if (!g_xs.on && !xs_open(a, way)) return;
	g_xs.shown = true;
	lv = panel_open();
	pace_init(&pc, plat_now_ms());
	pace_screen_init(&scr, plat_now_ms());

	while (!done && !want_quit && a->running && g_xs.on) {
		unsigned now = plat_now_ms();

		/* The launcher's slice of the transfer. Bounded, like everything else
		 * in a frame - see httpd.h. */
		xs_tick(a);
		if (!a->running || !g_xs.on) break;

		/* Every album's embedded cover copied out to .media, where the page
		 * can show it and Album Art can judge it: an album the shelf never
		 * drew had none there, so the page showed nothing and asked
		 * MusicBrainz over a cover the files already carried (Eric,
		 * 2026-10-05). cover_file asks MP4 as the shelf does, one album
		 * each tenth of a second, again for any MP4 has not answered. */
		mp4_poll();
		cover_answers();
		mp4_file_albums();             /* read again on the way out, if moved */
		/* One round of the albums as the screen opens, and another after
		 * files have moved or the library changed, then still: going round
		 * for as long as the screen was open asked MP4 about every album
		 * ten times a second on a screen nobody touched (2026-11-10) */
		if (cover_albums != g_mp4.nalbums ||
		    cover_moved != (unsigned long long)g_xs.in + (unsigned long long)g_xs.out) {
			cover_albums = g_mp4.nalbums;
			cover_moved = (unsigned long long)g_xs.in + (unsigned long long)g_xs.out;
			cover_left = g_mp4.nalbums;
		}
		if (g_cov && g_mp4.nalbums > 0 && cover_left > 0 && now - cover_at >= 100u) {
			char pic[LIB_PATH * 2 + 8];

			int al = (int)(cover_next++ % (unsigned)g_mp4.nalbums);

			cover_left--;

			cover_at = now;
			cover_recheck(al);
			cover_file(al, pic, sizeof pic);
		}

		if (g_xs.st.clients == 0) snprintf(who, sizeof who, "%s", T(S_XFER_WAITING));
		else snprintf(who, sizeof who, TN(S_XFER_CONNECTED, g_xs.st.clients), g_xs.st.clients);

		/* Closing after the file on its way: the Now row says which */
		if (hare_closing() && g_xs.st.file[0])
			snprintf(now_line, sizeof now_line, T(S_XFER_CLOSING_AFTER), g_xs.st.file);
		else if (hare_closing())
			snprintf(now_line, sizeof now_line, "%s", T(S_XFER_CLOSING_AFTER_THIS_FILE));
		else
			snprintf(now_line, sizeof now_line, "%s",
			         g_xs.st.last[0] ? g_xs.st.last : T(S_XFER_WAITING));

		/* The name, with the port when the server is not on 80, while the
		 * link is there to reach it by */
		name[0] = '\0';
		if (mdns_name() && !g_xs.link_lost_at) {
			if (hare_port() == 80) snprintf(name, sizeof name, "%s", mdns_name());
			else snprintf(name, sizeof name, "%s:%d", mdns_name(), hare_port());
		}
		hare_head(head, sizeof head, g_xs.cable, name, g_xs.addr, g_xs.pin);
		/* Until someone comes in, the way in, or the steps while one before
		 * the browser stalls; after, the news, whoever leaves (DESIGN.md
		 * 11.35). The hotspot's way in is its own screen (hot_show). */
		view = WAY_NEWS;
		if (!g_xs.met && !g_xs.hot) {
			int h = link_steps_hint(&g_xs.steps, now);

			view = xs_steps_shown() && xs_way_hint(h) ? WAY_STEPS : WAY_IN;
			if (view == WAY_STEPS)
				nrows = xs_step_rows(a, rows, lines, hint, sizeof hint, &g_xs.steps, h,
				                     g_xs.cable, XS_LEGEND);
			else {
				xs_way_set(g_xs.cable, g_xs.phone, name, g_xs.addr, g_xs.pin);
				nrows = xs_way_rows(rows, &g_xs.steps, g_xs.cable || g_xs.phone, XS_LEGEND);
				/* Over a cable the device can read, the loading bar until
				 * the computer has its address and the bar its end */
				if (g_xs.cable && g_xs.sensed
				    && xs_load_step(&g_xs.steps, &g_xs.load_k, &g_xs.load_ms, now))
					view = WAY_LOAD;
			}
		} else if (!g_xs.met) {
			view = WAY_HOT;
			nrows = 0;
		}
		if (view == WAY_NEWS) {
			if (g_xs.hot)
				hot_head(head, sizeof head, menu_std_width(a), hotspot_ssid(),
				         hotspot_password(), g_xs.pin);
			nrows = xs_page_rows(rows, who, g_xs.cable ? sshlink_status() : NULL, now_line,
			                     hare_closing() ? S_KEYHINT_HIDE
			                     : xs_moving() ? S_KEYHINT_KEEP_GOING_OR_STOP : S_KEYHINT_CLOSE);
			xs_move_now();
		}

		plat_input_poll(&a->in);
		ui_sounds_follow();
		/* The launcher stopping: the hotspot too (xfer_stop), or Wi-Fi stays
		 * off under a hostapd nobody stops until the next transfer screen */
		if (a->in.quit_requested) { xs_end(); a->running = false; return; }

		/* The screen's light, ahead of every press: a press that lights a
		 * dark or dim screen is the light's alone, POWER's too, as on the
		 * shelf (src/chargedark.h) */
		{
			pace_light was = scr.light, light;
			bool press = false, spent;
			int b;

			for (b = 0; b < IN_COUNT && !press; b++) press = a->in.pressed[b];
			light = pace_screen_step(&scr, plat_now_ms(), a->auto_off, on_charger(),
			                         mp4c_playing(), press, xs_moving(), g_xs.st.clients, &spent);
			if (light != was) {
				if (was == PACE_OFF) plat_backlight(true);
				if (was == PACE_DIM) plat_backlight_dim(false);
				if (light == PACE_OFF) { snap_off(); plat_backlight(false); }
				if (light == PACE_DIM) plat_backlight_dim(true);
				fprintf(stderr, "xfer: screen %s\n", light == PACE_OFF ? "off, nothing moving"
				        : light == PACE_DIM ? "dimmed, files moving" : "lit");
				pc.drawn = false;           /* a whole frame when it lights */
			}
			if (spent) {
				plat_input_flush();
				memset(&a->in, 0, sizeof a->in);
				continue;
			}
			if (light == PACE_OFF) {
				/* Dark: still the server, and nothing drawn */
				ram_idle_set(!xs_moving());
				plat_input_wait(xs_moving() ? 4 : 50);
				continue;
			}
		}

		/* Powering off stops the hotspot as every way out does (xfer_stop):
		 * on the hotspot, a second or two for the supplicant to come back */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			xs_end();
			power_off(a);
			return;
		}
		/* The files the question let finish are in: closed */
		if (hare_closing() && hare_inflight() == 0) { stop = true; done = true; }
		/* B: closed when nothing moves; while files move, the question.
		 * MENU keeps them moving and goes to the shelf. Once it is closing
		 * after the file on its way, B hides it and it closes behind the
		 * shelf when that file is in. */
		if (!done && menu_leaving(a)) {
			done = true;
			if (g_menu_closing || hare_closing()) {
				/* hidden, still running */
			} else if (!xs_moving()) {
				stop = true;
			} else {
				int c = xs_stop_ask(a);

				if (!a->running || !g_xs.on) return;
				if (c == 1) { hare_close_after(); done = false; }  /* the screen closes it */
				else if (c == 2 || c == -2) {
					/* Cancel now: a paused upload's part with the rest */
					if (c == 2) hare_drop_paused();
					stop = true;
				}
				/* 0 or B: keep going, hidden */
			}
		}

		levels_keys(&a->in, BOOST_HIDE);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		{
			/* What the frame would show, and whether it must come
			 * (src/pace.h): files moving and the hotspot coming up draw
			 * every pass, as before */
			unsigned nowp = plat_now_ms();
			const char *legend;

			power_follow();             /* as in the menus' loop */
			legend = hare_closing() ? XS_LEGEND_END : xs_moving() ? XS_LEGEND_MOV : XS_LEGEND;
			unsigned key = view == WAY_HOT
			    ? pace_str(pace_str(pace_str(pace_str(pace_int(pace_rows(NULL, NULL, 0, -1, k, a->tint),
			               g_xs.hs), g_xs.name), g_xs.pin), now_line), legend)
			    : view == WAY_IN
			    ? pace_str(pace_str(pace_rows(NULL, rows, nrows, -1, k, a->tint), g_way.first),
			               g_way.second)
			    : view == WAY_LOAD
			    ? pace_int(pace_rows(NULL, rows, nrows, -1, k, a->tint), (int)(g_xs.load_k * 1000.0f))
			    : xs_move_key(pace_rows(head, rows, nrows, -1, k, a->tint));
			/* the bar catching up draws every pass; its creep, slow, by its key */
			bool moving = xs_moving() || (g_xs.hot && g_xs.hs == HOTSPOT_STARTING)
			              || (view == WAY_LOAD
			                  && xs_load_target(&g_xs.steps, nowp) - g_xs.load_k > 0.002f);
			bool busy = moving || pace_busy(a);

			if (!pace_due(&pc, key, nowp, g_redraw_at, busy)) {
				/* Nothing to draw: the memory's slow clock once still for
				 * 2 s, and a wait for a button. The server is served each
				 * pass, so its rest is short while a browser is there. */
				ram_idle_set(pace_ram_idle(&pc, nowp));
				plat_input_wait(pace_rest(&pc, nowp, g_redraw_at,
				                          moving ? 4 : g_xs.st.clients ? 10 : 50));
				continue;
			}
			ram_idle_set(false);
			g_redraw_at = REDRAW_NEVER;
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
			if (view == WAY_HOT) hot_show(a, g_xs.name, g_xs.pin, g_xs.hs == HOTSPOT_STARTING, now_line, legend);
			else if (view == WAY_IN) xs_way_draw(a, rows, nrows);
			else if (view == WAY_LOAD) xs_load_draw(a, rows, nrows, g_xs.load_k);
			else xs_page_show(a, head, rows, nrows);
			plat_draw_osd(a->r);
			drawn_at = plat_now_ms();
			plat_present();
			pace_drawn(&pc, key, nowp);
			/* The press to the page's first frame, for the log: the link is
			 * never waited on (gadget.h), so this is a frame or two. Its
			 * present apart: on the handheld a flip, in desk mode the
			 * window's face drawn around it */
			if (first) {
				first = false;
				g_xs.drawn = true;
				fprintf(stderr, "xfer: the page's first frame %u ms after it was asked for (%s; "
				        "drawn by %u ms, its present %u ms)\n",
				        plat_now_ms() - asked_at,
				        view == WAY_LOAD ? "the loading bar" : view == WAY_STEPS ? "the steps"
				        : view == WAY_IN ? "the way in" : view == WAY_HOT ? "the hotspot" : "the page",
				        drawn_at - asked_at, plat_now_ms() - drawn_at);
			}
		}
		/* Shorter than the usual 8ms: this loop is also the server's, and a
		 * transfer moves POLL_BUDGET per pass. */
		ui_frame_rest(4);
	}
	if (scr.light == PACE_OFF) plat_backlight(true);
	if (scr.light == PACE_DIM) plat_backlight_dim(false);
	ram_idle_set(false);
	panel_close(a, lv, false);
	g_xs.shown = false;
	/* The hotspot failing ends it as Y does; anything else that ended the
	 * loop left it running behind the shelf */
	if (g_xs.on && (stop || g_xs.hot_failed)) xs_stop(a, true);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}
