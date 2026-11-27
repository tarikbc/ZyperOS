/* What does a menu contain, for a given state?
 *
 * This check is the point of the menu design. A screen's build function takes
 * state and produces rows, touching no renderer and no device, so the answer
 * can be had here rather than by looking at a handheld. Every menu defect this
 * project has had was some form of "nobody noticed the list was wrong", and
 * every one of them needed a device to see.
 *
 * If this file ever needs SDL to link, the menu design has failed. Do not add
 * it - reopen the design instead.
 */
#include "../src/wifi_menu.h"
#include "../src/sys_menu.h"
#include "../src/homesys.h"
#include "../src/game_menu.h"
#include "../src/controls.h"
#include "../src/ctltab.h"
#include "../src/padglyph.h"
#include "../src/sidelights.h"
#include "legend.h"   /* legend_form: DESIGN.md 11.4's one form */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/* MENU is in tabs (DESIGN.md 11.3): the checks below are of its rows, every
 * one, so they build it with no tab, which is every row in the enum's order
 * under the old heading. The tabs themselves are checked in menu_tabs(). */
static int build_every_row(sys_ui *u, menu_row *out, menu_bufs *b, const char **heading)
{
	if (!u->games) u->tab = PT_TABS;
	return sys_menu_build(u, out, b, heading);
}
#define sys_menu_build(u, out, b, heading) build_every_row(u, out, b, heading)

static int sm_at(const sys_ui *u, sm_row which);
static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int live_count(const menu_row *r, int n)
{
	int i, k = 0;
	for (i = 0; i < n; i++) if (r[i].live) k++;
	return k;
}

static int rule_at(const menu_row *r, int n)
{
	int i;
	for (i = 0; i < n; i++) if (!r[i].label) return i;
	return -1;
}

/* Every screen must be able to say "nothing here" without offering a row that
 * does nothing when pressed. The radio-off case is the one that used to draw a
 * selectable placeholder. */
static void off_state(void)
{
	wifi_ui w;
	menu_row rows[64];
	const char *heading;
	int n;

	memset(&w, 0, sizeof w);
	w.on = false;
	n = wifi_build(&w, rows, 64, &heading);

	printf("radio off:\n");
	ck(heading && !strcmp(heading, "Wi-Fi"), "the screen names itself");
	ck(n == 3, "switch, rule, note");
	ck(rows[0].live, "the switch is live");
	ck(!strcmp(rows[0].label, "Wi-Fi"), "row 0 is the switch");
	ck(!strcmp(rows[0].value, "Off"), "the switch reads Off");
	ck(rule_at(rows, n) == 1, "the rule separates the footer");
	ck(ROW_IS_NOTE(rows[2]), "the footer is a note");
	ck(!rows[2].live, "the footer is not selectable");
	ck(live_count(rows, n) == 1, "only the switch can be chosen");
}

/* The state the async scan produces on entry: saved networks up immediately,
 * the footer saying what is happening, nothing claiming a signal it has not
 * measured. */
static void scanning_state(void)
{
	wifi_ui w;
	menu_row rows[64];
	const char *heading;
	int n;

	memset(&w, 0, sizeof w);
	w.on = true;
	w.scanning = true;
	w.scanned = false;
	w.n = 2;
	snprintf(w.nets[0].ssid, sizeof w.nets[0].ssid, "ReinFi");
	w.nets[0].known = true;
	snprintf(w.nets[1].ssid, sizeof w.nets[1].ssid, "Transport");
	w.nets[1].known = true;
	wifi_label(&w);
	n = wifi_build(&w, rows, 64, &heading);

	printf("scanning, two saved networks:\n");
	ck(n == 5, "switch, two networks, rule, note");
	ck(live_count(rows, n) == 3, "switch and both networks are choosable");
	ck(!strcmp(rows[1].label, "ReinFi"), "the first saved network is listed");
	ck(!strcmp(rows[1].value, "Saved"), "no signal word before the scan lands");
	ck(rule_at(rows, n) == 3, "rule after the networks");
	ck(!strcmp(rows[4].label, "Scanning..."), "footer says what is happening");
	ck(ROW_IS_NOTE(rows[4]), "the progress line is a note");
}

/* After results land the footer becomes the key legend, and the legend only
 * offers forget when there is something to forget. */
static void scanned_state(void)
{
	wifi_ui w;
	menu_row rows[64];
	const char *heading;
	int n;

	memset(&w, 0, sizeof w);
	w.on = true;
	w.scanned = true;
	w.n = 1;
	snprintf(w.nets[0].ssid, sizeof w.nets[0].ssid, "Cafe");
	w.nets[0].known = false;
	w.nets[0].secured = true;
	w.nets[0].signal = -55;
	wifi_label(&w);
	n = wifi_build(&w, rows, 64, &heading);

	printf("scan done, one unsaved network:\n");
	ck(n == 4, "switch, network, rule, note");
	ck(!strcmp(rows[1].value, "Strong"), "-55 dBm reads as strong");
	ck(!strcmp(legend_text(rows[3].label), "Y: rescan"), "no forget offered with nothing saved");

	w.nets[0].known = true;
	wifi_label(&w);
	n = wifi_build(&w, rows, 64, &heading);
	ck(!strcmp(legend_text(rows[3].label), "Y: rescan   X: forget"), "forget offered once saved");
	ck(!strcmp(rows[1].value, "Strong - saved"), "a saved network in range shows both");
}

/* The Wi-Fi screen's footer, in every state it has: where it names a button
 * it is a key legend in DESIGN.md 11.4's one form, like the Bluetooth
 * screen's and MENU > Controls'. "Scanning..." and the two that say there is
 * nothing to list name none and are held to nothing. */
static void wifi_footer_is_a_legend(void)
{
	static const struct { bool on, scanning, scanned, saved; int n; } STATE[] = {
		{ false, false, false, false, 0 },   /* radio off */
		{ true, false, true, false, 0 },     /* no networks found */
		{ true, true, false, true, 1 },      /* scanning */
		{ true, false, true, false, 1 },     /* scanned, nothing saved */
		{ true, false, true, true, 1 },      /* scanned, one saved */
	};
	char msg[160];
	int k, legends = 0;

	printf("the Wi-Fi footer is a key legend:\n");
	for (k = 0; k < (int)(sizeof STATE / sizeof STATE[0]); k++) {
		wifi_ui w;
		menu_row rows[64];
		const char *heading, *note;
		int n;

		memset(&w, 0, sizeof w);
		w.on = STATE[k].on;
		w.scanning = STATE[k].scanning;
		w.scanned = STATE[k].scanned;
		w.n = STATE[k].n;
		if (w.n) {
			snprintf(w.nets[0].ssid, sizeof w.nets[0].ssid, "Cafe");
			w.nets[0].known = STATE[k].saved;
			w.nets[0].signal = -55;
		}
		wifi_label(&w);
		n = wifi_build(&w, rows, 64, &heading);
		ck(n > 0 && ROW_IS_NOTE(rows[n - 1]), "the screen ends in a footer");
		if (n <= 0 || !ROW_IS_NOTE(rows[n - 1])) continue;
		note = rows[n - 1].label;
		if (!strstr(legend_text(note), ": ")) continue;
		legends++;
		snprintf(msg, sizeof msg, "\"%s\" is not a legend", legend_text(note));
		ck(legend_form(note), msg);
	}
	ck(legends == 2, "both footers after a scan name buttons");
}

/* The bands are a judgment on a measured number and the boundary is not
 * validated. Pinned here so it cannot move by accident. */
static void strength_words(void)
{
	printf("signal words:\n");
	ck(!strcmp(wifi_strength(-40), "Strong"), "-40 strong");
	ck(!strcmp(wifi_strength(-60), "Strong"), "-60 is the strong boundary");
	ck(!strcmp(wifi_strength(-61), "Good"),   "-61 good");
	ck(!strcmp(wifi_strength(-72), "Good"),   "-72 is the good boundary");
	ck(!strcmp(wifi_strength(-73), "Weak"),   "-73 weak, the measured cliff");
}

/* A build must never write past what it was given. */
static void respects_max(void)
{
	wifi_ui w;
	menu_row rows[4];
	const char *heading;
	int n, i;

	memset(&w, 0, sizeof w);
	w.on = true;
	w.scanned = true;
	w.n = WIFI_MAX_NETS;
	for (i = 0; i < WIFI_MAX_NETS; i++)
		snprintf(w.nets[i].ssid, sizeof w.nets[i].ssid, "net%d", i);
	wifi_label(&w);
	n = wifi_build(&w, rows, 4, &heading);

	printf("a small buffer:\n");
	ck(n <= 4, "never returns more rows than it was offered");
}


/* ---------- the two MENU-button menus ------------------------------------- */

/* A row's value, "" for none and for a note, a body line or a rule inside a
 * body, whose `value` is a mark (menu.h) and no string */
static const char *val(const menu_row *r)
{
	if (ROW_IS_NOTE(*r) || ROW_IS_HR(*r)) return "";
	return r->value ? r->value : "";
}
/* A row's label, "" for a rule, which has none: every loop over a menu's
 * rows compares through this, so a rule in the list is not a NULL strcmp. */
static const char *lbl(const menu_row *r) { return r->label ? r->label : ""; }
static const char *val_of(const menu_row *rows, int n, const char *label);

/* A device with no network. Three rows depend on one, and all three have to
 * say so rather than silently doing nothing: Wi-Fi File Transfer, Box Art, and Box
 * Art's counterpart in the system menu. */
static void zyperos_menu_offline(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	int n;

	memset(&u, 0, sizeof u);
	u.wifi = WIFI_OFF;
	u.appearance = "Auto";
	u.auto_off = 120;
	u.clock = "Oct 3, 6:12 AM";
	u.update = "0.1.0";      /* a card with the update program */
	n = sys_menu_build(&u, rows, &b, &heading);

	printf("ZyperOS menu, radio off:\n");
	ck(n == PM_ROWS, "every row is filled");
	ck(!strcmp(heading, "ZyperOS"), "heading");
	ck(!strcmp(val(&rows[PM_WIFI]), "Off"), "Wi-Fi reads Off, its switch's value");
	ck(!strcmp(val(&rows[PM_XFER]), "Needs Wi-Fi"), "OTH says why it is dead");
	ck(!rows[PM_XFER].live, "OTH is not selectable offline");
	ck(!strcmp(rows[PM_SCRAPER].label, "Downloads") && rows[PM_SCRAPER].live,
	   "Downloads is live offline: its counts, Video Snaps and accounts need no network");
	ck(!strcmp(val(&rows[PM_SLEEP]), "2 min"), "120 s reads as 2 min");
	ck(!strcmp(rows[PM_CLOCK].label, "Date & Time") &&
	   !strcmp(val(&rows[PM_CLOCK]), "Oct 3, 6:12 AM"), "Date & Time reads the time");
	ck(rows[PM_CLOCK].live, "and opens with no network: it is how the Pixel gets one");
	ck(rows[PM_APPEARANCE].live, "and so is Appearance");
	/* Live since 2026-09-06, when the pairing screen landed. The row used to
	 * be dead and read "not yet", which was true of the screen and false of
	 * the feature - game audio had been going to a headset for three days. */
	ck(rows[PM_BT].live, "Bluetooth is reachable now that pairing exists");
	/* Always reachable, even with no radio and no cable: it is the row you go
	 * to in order to say "not Bluetooth", so it must not vanish with the thing
	 * it refuses. */
	ck(rows[PM_AUDIO].live, "Audio Output is reachable offline");
	ck(!strcmp(val(&rows[PM_AUDIO]), "Auto (Speaker)"),
	   "and Auto names where it landed rather than only saying Auto");
	/* Directly under it, and as reachable: someone who hears with one ear
	 * needs it with no radio and no cable as much as with both. */
	ck(PM_CHANNELS == PM_AUDIO + 1, "Audio Channels sits under Audio Output");
	ck(!strcmp(rows[PM_CHANNELS].label, "Audio Channels") && rows[PM_CHANNELS].live,
	   "Audio Channels is reachable offline");
	ck(!strcmp(val(&rows[PM_CHANNELS]), "Stereo"), "and reads stereo until it is set");
}

/* The row names the choice, not what a speaker makes of it: mono left stays
 * mono left with the cable out, where the speaker plays it as mono. */
static void channels_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	static const char *const want[AOUT_CHANNELS_COUNT] = {
		"Stereo", "Mono", "Mono left", "Mono right", "Auto (Stereo)",
	};
	int c;

	memset(&u, 0, sizeof u);
	printf("the Audio Channels row:\n");
	u.audio_dest = AOUT_SPK;
	for (c = 0; c < AOUT_CHANNELS_COUNT; c++) {
		u.audio_channels = (aout_channels)c;
		sys_menu_build(&u, rows, &b, &heading);
		ck(!strcmp(val(&rows[PM_CHANNELS]), want[c]), "each choice names itself");
	}
	/* Auto is a rule, so it says what it makes: mono on the Pixel 2's one
	 * speaker, as Audio Output's Auto says where the sound went */
	u.audio_channels = AOUT_CHANNELS_AUTO;
	u.audio_mix = AOUT_MONO;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_CHANNELS]), "Auto (Mono)"), "Auto names the mix it makes");
}

/* The row has to say a PLACE. "Auto" alone makes the player guess which of
 * three they are about to hear. */
/* The Disc row (issue #102): on the Game tab right after Quick Slot, only
 * for a game on more than one disc. Its value is the core's label when it
 * fits the row, else "Disc N of M". */
static bool disc_fits_short(const char *s) { return strlen(s) <= 12; }

static void gm_disc_row_only_with_discs(void)
{
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	int n, at, d;

	printf("in-game menu, Disc:\n");
	u.dmode = "Native"; u.earned = 12; u.total = 40; u.quick = 2;
	for (d = 0; d <= 1; d++) {
		u.discs = d;
		n = gm_rows(&u, GT_GAME, rows, &b);
		ck(gm_row_at(&u, GT_GAME, GM_DISC) == -1 && n == 7, "no Disc row with 0 or 1 disc, the menu as before");
	}
	u.discs = 3; u.disc = 1;
	n = gm_rows(&u, GT_GAME, rows, &b);
	at = gm_row_at(&u, GT_GAME, GM_DISC);
	ck(n == 8 && at == gm_row_at(&u, GT_GAME, GM_QUICK) + 1 &&
	   at + 1 == gm_row_at(&u, GT_GAME, GM_CHEEVOS), "with 3 discs: right after Quick Slot");
	ck(at >= 0 && !strcmp(rows[at].label, "Disc") && !strcmp(val(&rows[at]), "Disc 2 of 3") &&
	   rows[at].live && b.sound[at], "Disc says Disc 2 of 3, and steps round");
	u.player2 = true; u.player2_value = "Off";
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 9 && gm_row_at(&u, GT_GAME, GM_PLAYER2) == gm_row_at(&u, GT_GAME, GM_DISC) + 1,
	   "Player 2 follows Disc");
	u.player2 = false;
	u.port = true;
	ck(gm_row_at(&u, GT_GAME, GM_DISC) == -1, "none over a port");
	u.port = false;
	ck(gm_row_at(&u, GT_DISPLAY, GM_DISC) == -1, "and not on the Display tab");
}

/* Undo Load (1.0.6): the state from before the last load, kept once. A row
 * only while there is one, right under Load, never over a port and never on
 * the Display tab; a plain row A acts on */
static void gm_undo_row_only_with_undo(void)
{
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	int n, at;

	printf("in-game menu, Undo Load:\n");
	u.dmode = "Native"; u.earned = 12; u.total = 40; u.quick = 2;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(gm_row_at(&u, GT_GAME, GM_UNDO) == -1 && n == 7, "no Undo Load row with nothing to undo");
	u.undo = true;
	n = gm_rows(&u, GT_GAME, rows, &b);
	at = gm_row_at(&u, GT_GAME, GM_UNDO);
	ck(n == 8 && at == gm_row_at(&u, GT_GAME, GM_LOAD) + 1 &&
	   at + 1 == gm_row_at(&u, GT_GAME, GM_QUICK), "with one: right under Load, before Quick Slot");
	ck(at >= 0 && !strcmp(rows[at].label, "Undo Load") && !rows[at].value &&
	   rows[at].live && !b.sound[at], "Undo Load is a plain row");
	u.quick = 0;
	ck(gm_row_at(&u, GT_GAME, GM_UNDO) == gm_row_at(&u, GT_GAME, GM_LOAD) + 1,
	   "and with no Quick Slot, still under Load");
	u.quick = 2;
	u.port = true;
	ck(gm_row_at(&u, GT_GAME, GM_UNDO) == -1, "none over a port");
	u.port = false;
	ck(gm_row_at(&u, GT_DISPLAY, GM_UNDO) == -1, "and not on the Display tab");
	ck(SLOT_UNDO != SLOT_AUTO && (SLOT_UNDO < 1 || SLOT_UNDO > GM_SLOTS),
	   "SLOT_UNDO is none of the player's slots, nor Auto");
}

/* The in-game Display tab: Display, then Shader, no caption between them
 * (what a mode does is said in its list, beside a picture of it) */
static void gm_display_caption(void)
{
	gm_ui u = { 0 };
	gm_row ids[GM_MAX_ROWS];
	int n;

	printf("in-game menu, the Display tab:\n");
	u.dmode = "Full Screen"; u.quick = 2;
	n = gm_tab_rows(&u, GT_DISPLAY, ids);
	ck(n == 3 && ids[0] == GM_DISPLAY && ids[1] == GM_SHADER && ids[2] == GM_HUD,
	   "Display, Shader, then On-Screen Stats");
}

/* Screen Colors on the Display tab (1.1.0, DESIGN.md 11.42): where the
 * display has a table, after the picture's rows and right over On-Screen
 * Stats, its value the look, a plain row whose A opens MENU's page; on no
 * other tab, none without a table, and none over a port */
static void gm_screen_colors_row(void)
{
	gm_ui u = { 0 };
	gm_row ids[GM_MAX_ROWS];
	menu_row rows[GM_MAX_ROWS];
	gm_bufs b;
	int n, t, at;

	printf("in-game menu, Screen Colors on the Display tab:\n");
	u.dmode = "Full Screen"; u.quick = 2;
	n = gm_tab_rows(&u, GT_DISPLAY, ids);
	ck(n == 3 && gm_row_at(&u, GT_DISPLAY, GM_SCREEN_COLORS) < 0,
	   "no table (screen_colors NULL): no row");
	u.screen_colors = "Vivid";
	n = gm_tab_rows(&u, GT_DISPLAY, ids);
	ck(n == 4 && ids[0] == GM_DISPLAY && ids[1] == GM_SHADER && ids[2] == GM_SCREEN_COLORS &&
	   ids[3] == GM_HUD, "Display, Shader, Screen Colors, then On-Screen Stats");
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	at = gm_row_at(&u, GT_DISPLAY, GM_SCREEN_COLORS);
	ck(at == 2 && !strcmp(rows[at].label, "Screen Colors") && rows[at].value &&
	   !strcmp(rows[at].value, "Vivid") && rows[at].live,
	   "named as in MENU > Display, its value the look, a stop");
	ck(b.sound[at] == 0 && !b.choice[at].n, "a plain row: A opens the page, Left and Right do nothing");
	u.shader = "LCD Grid"; u.strength = "50%"; u.border = "Off"; u.screens = true;
	n = gm_tab_rows(&u, GT_DISPLAY, ids);
	ck(n == 9 && ids[6] == GM_STYLUS && ids[7] == GM_SCREEN_COLORS && ids[8] == GM_HUD,
	   "a DS game with Border and an old screen: after Stylus, over On-Screen Stats");
	for (t = 0; t < GT_TABS; t++)
		if (t != GT_DISPLAY && gm_row_at(&u, (gm_tab)t, GM_SCREEN_COLORS) >= 0) break;
	ck(t == GT_TABS, "on no other tab");
	u.port = true;
	ck(gm_row_at(&u, GT_GAME, GM_SCREEN_COLORS) < 0 && gm_row_at(&u, GT_DISPLAY, GM_SCREEN_COLORS) == 0,
	   "over a program of its own (OpenBOR, a port), its Display tab's one row");
	/* Over your PICO-8, its Display tab's, in the same place (1.1.0-rc.8) */
	u.port_display = u.port_hud = true;
	u.screens = false;
	n = gm_tab_rows(&u, GT_DISPLAY, ids);
	at = gm_row_at(&u, GT_DISPLAY, GM_SCREEN_COLORS);
	ck(at >= 0 && at == n - 2 && ids[n - 1] == GM_HUD && gm_row_at(&u, GT_GAME, GM_SCREEN_COLORS) < 0,
	   "over your PICO-8, on its Display tab right over On-Screen Stats");
}

/* Over a program still starting (PORT_MENU_WAITING, 1.0.6): the Game tab's
 * Continue named Keep Waiting, and its Controls tab as over it running */
static void gm_port_waiting(void)
{
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	int n;

	printf("in-game menu over a port still starting:\n");
	u.dmode = "Full Screen"; u.port = true;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 2 && !strcmp(rows[0].label, "Continue"), "over a running port: Continue");
	u.waiting = true;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 2 && !strcmp(rows[0].label, "Keep Waiting") && !strcmp(rows[1].label, "Quit"),
	   "over one still starting: Keep Waiting and Quit");
	n = gm_rows(&u, GT_CONTROLS, rows, &b);
	ck(n == 1 && !strcmp(rows[0].label, "All Controls"), "and its Controls tab: All Controls");
	u.port = false;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(!strcmp(rows[0].label, "Continue"), "and a game's menu is not touched by it");
}

static void disc_value_check(void)
{
	char out[64];

	ck(!strcmp(gm_disc_value(&(gm_ui){ .discs = 3, .disc = 1, .disc_label = "FFIX Disc 2" },
	                         out, sizeof out, disc_fits_short), "FFIX Disc 2"),
	   "the core's label when it fits");
	ck(!strcmp(gm_disc_value(&(gm_ui){ .discs = 3, .disc = 1,
	                                   .disc_label = "Final Fantasy IX (USA) (Disc 2)" },
	                         out, sizeof out, disc_fits_short), "Disc 2 of 3"),
	   "else Disc 2 of 3");
	ck(!strcmp(gm_disc_value(&(gm_ui){ .discs = 2, .disc = 0 }, out, sizeof out, disc_fits_short),
	           "Disc 1 of 2"), "no label: Disc 1 of 2");
	ck(!strcmp(gm_disc_value(&(gm_ui){ .discs = 2, .disc = 0, .disc_label = "" }, out, sizeof out, NULL),
	           "Disc 1 of 2"), "an empty label is no label");
	ck(gm_disc_step(0, 3, -1) == 2 && gm_disc_step(2, 3, 1) == 0 && gm_disc_step(1, 3, 1) == 2,
	   "the row steps round: Left on disc 1 is the last");
}

static void audio_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;

	memset(&u, 0, sizeof u);
	printf("the Audio Output row:\n");

	u.audio_policy = AOUT_AUTO;
	u.audio_dest = AOUT_WIRED;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "Auto (Wired)"), "auto, on a cable");

	u.audio_dest = AOUT_BT;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "Auto (Bluetooth)"), "auto, on a headset");

	/* Pinned says the place with no "auto", because there is no rule left to
	 * describe - it is just where the sound is. */
	u.audio_policy = AOUT_SPEAKER;
	u.audio_dest = AOUT_SPK;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "Speaker"), "pinned reads as the place");

	/* Pinned to Speaker with a cable in is still the cable: the pin refuses
	 * Bluetooth, never the jack. */
	u.audio_dest = AOUT_WIRED;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "Wired"),
	   "and a cable still shows through the Speaker pin");
}

/* Appearance is available without a radio. Removed selectors stay absent. */
static void look_rows(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	static const char *const names[] = { "Light", "Dark", "Auto" };
	int n, i;

	memset(&u, 0, sizeof u);
	u.wifi = WIFI_OFF;
	u.appearance = "Dark";
	n = sys_menu_build(&u, rows, &b, &heading);
	for (int i = 0; i < n; i++) {
		ck(strcmp(lbl(&rows[i]), "Card Style") != 0, "no card-style selector");
		ck(strcmp(lbl(&rows[i]), "UI Direction") != 0, "no direction selector");
	}

	printf("the Appearance row and the removed selectors:\n");
	ck(!strcmp(rows[PM_APPEARANCE].label, "Appearance"), "the new row is Appearance");
	ck(!strcmp(val(&rows[PM_APPEARANCE]), "Dark"), "Appearance's value is the setting's name");
	for (i = 0; i < n; i++)
		ck(!rows[i].label || strcmp(rows[i].label, "UI Theme"), "no row is still called UI Theme");

	for (i = 0; i < 3; i++) {
		u.appearance = names[i];
		sys_menu_build(&u, rows, &b, &heading);
		ck(!strcmp(val(&rows[PM_APPEARANCE]), names[i]), "each setting names itself");
	}

	/* A caller that says nothing gets the default, Light, as every other
	 * value that has one does */
	u.appearance = NULL;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_APPEARANCE]), "Light"), "and with none it reads Light, the default");

	/* Shader, ZyperOS's own screens' (1.0.6; every system's from
	 * 2026-11-12): on Display, the tab of how things look, after
	 * Screensaver. Off with none said; the shader's name as given. */
	printf("MENU's Shader row:\n");
	ck(sys_menu_tab_of(PM_SHADER) == PT_DISPLAY && PM_SHADER == PM_SAVER + 1,
	   "Shader is on Display, after Screensaver");
	ck(!strcmp(rows[PM_SHADER].label, "Shader") && !strcmp(val(&rows[PM_SHADER]), "Off") &&
	   rows[PM_SHADER].live, "Shader, Off when nothing is said, a stop");
	ck(b.sound[PM_SHADER] == MENU_SND_RING, "it steps round, on A too");
	u.shader_screens = "CRT Curved";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_SHADER]), "CRT Curved"), "and says what it is given");
	u.shader_screens = NULL;

	/* Strength, the screens' (1.0.6; every system's from 2026-11-14,
	 * src/strength.h): right under Shader, 100% with none said, stepped on
	 * left and right and stopping at the ends, so A is deny; gone while the
	 * screens' shader is not an old screen, and with Shader where the
	 * device draws none */
	printf("MENU's Strength row:\n");
	ck(sys_menu_tab_of(PM_STRENGTH) == PT_DISPLAY && PM_STRENGTH == PM_SHADER + 1,
	   "Strength is on Display, right under Shader");
	ck(!strcmp(rows[PM_STRENGTH].label, "Strength") && !strcmp(val(&rows[PM_STRENGTH]), "100%") &&
	   rows[PM_STRENGTH].live, "Strength, 100% when nothing is said, a stop");
	ck(b.sound[PM_STRENGTH] == MENU_SND_NO_A, "left and right step it, A does nothing");
	u.strength_screens = "40%";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_STRENGTH]), "40%"), "and says what it is given");
	u.strength_screens = NULL;
	{
		pm_row all[PM_ROWS];
		int before = sys_menu_zyperos_rows(&u, all), na, j, has = 0, hs = 0;

		u.no_strength = true;
		na = sys_menu_zyperos_rows(&u, all);
		for (j = 0; j < na; j++) has |= all[j] == PM_STRENGTH;
		ck(!has && na == before - 1, "no_strength takes the row and nothing else");
		u.no_strength = false;
		u.no_shader = true;
		na = sys_menu_zyperos_rows(&u, all);
		has = 0;
		for (j = 0; j < na; j++) {
			has |= all[j] == PM_STRENGTH;
			hs |= all[j] == PM_SHADER;
		}
		ck(!has && !hs && na == before - 2, "no_shader takes Shader and Strength and nothing else");
		u.no_shader = false;
	}
	u.no_strength = false;
	sys_menu_build(&u, rows, &b, &heading);

	/* The system menu is about a system, not about the look of the shelf */
	memset(&u, 0, sizeof u);
	u.games = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		ck(strcmp(lbl(&rows[i]), "Appearance") && strcmp(lbl(&rows[i]), "Card Style"),
		   "a system's menu has neither");
}

/* MENU > Clock: 12-hour or 24-hour, for the shelf's corner and the Date &
 * Time row both. My note in the status lab, 2026-11-04: "make 12-24
 * customizable in settings". Directly under Date & Time, live with no radio,
 * 12-hour until it is set, as the launcher has always been. */
static void clock_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	int n, i, found = 0;

	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	u.clock = "Nov 4, 22:08";
	n = sys_menu_build(&u, rows, &b, &heading);

	printf("the Clock row:\n");
	ck(PM_HOURS == PM_CLOCK + 1, "sits under Date & Time");
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Clock")) {
			found++;
			ck(rows[i].live, "is live on a device with no radio");
			ck(!strcmp(val(&rows[i]), "12-hour"), "and reads 12-hour until it is set");
			ck(i > 0 && !strcmp(rows[i - 1].label, "Date & Time"),
			   "directly under Date & Time there too");
			ck(i > 0 && !strcmp(val(&rows[i - 1]), "Nov 4, 22:08"),
			   "which shows the time as it is given");
		}
	ck(found == 1, "is there once");
	u.clock_24h = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Clock"))
			ck(!strcmp(val(&rows[i]), "24-hour"), "24-hour names itself");

	/* A system's menu is about the system. */
	memset(&u, 0, sizeof u);
	u.games = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		ck(!rows[i].label || strcmp(rows[i].label, "Clock"), "a system's menu has none");
}

/* Connected and signed in. The Wi-Fi row shows the network's NAME - a settings
 * row says what the setting is, and the address lives on the About page. */
/* The value of the row called `label`, "" for none: for a device whose rows
 * are fewer than the enum's, where a row's place is not its enum value */
static const char *val_of(const menu_row *rows, int n, const char *label)
{
	int i;

	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, label)) return val(&rows[i]);
	return "";
}

static bool has_label(const menu_row *rows, int n, const char *label)
{
	int i;

	for (i = 0; i < n; i++)
		if (!strcmp(lbl(&rows[i]), label)) return true;
	return false;
}

static void zyperos_menu_online(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	int n;

	memset(&u, 0, sizeof u);
	u.wifi = WIFI_CONNECTED;
	u.ssid = "kitchen";
	u.ra_in = true;
	u.ra_name = "eric";
	u.auto_off = 0;
	u.update = "0.2.0 available";
	u.storage = "7.4 GB free";
	n = sys_menu_build(&u, rows, &b, &heading);

	ck(n == PM_ROWS, "row count does not depend on the network");
	printf("ZyperOS menu, connected:\n");
	ck(!strcmp(val(&rows[PM_WIFI]), "kitchen"), "Wi-Fi shows the network name");
	ck(rows[PM_XFER].live && !rows[PM_XFER].value, "OTH is live and unqualified");
	ck(rows[PM_SCRAPER].live && !rows[PM_SCRAPER].value,
	   "Downloads is live, with no value before a count");
	ck(!strcmp(val(&rows[PM_SLEEP]), "Never"), "0s reads as never");

	/* ONE ROW, DOWNLOADS, where Scraper was (and Box Art before it): the
	 * runs, Video Snaps and the three accounts behind it (my pick in the
	 * downloads lab, 2026-11-19: one door; DESIGN.md 11.53). The account
	 * rows and the Achievements row it replaces are gone. */
	printf("the Downloads row and its screen:\n");
	ck(PM_SCRAPER == PM_RANDOM + 1 && sys_menu_tab_of(PM_SCRAPER) == PT_GAMES,
	   "Downloads is in Games, under Random Game");
	{
		int j;

		for (j = 0; j < n; j++)
			ck(strcmp(lbl(&rows[j]), "ScreenScraper") && strcmp(lbl(&rows[j]), "SteamGridDB") &&
			   strcmp(lbl(&rows[j]), "Box Art"), "no ScreenScraper, SteamGridDB or Box Art row in MENU");
	}
	u.scraper = "142 missing";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_SCRAPER]), "142 missing"), "its value is the count it is given");
	u.scraper = NULL;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == PM_ROWS, "and none of that changes the row count");
	{
		menu_row pr[SSP_ROWS];
		ssp_row pid[SSP_ROWS];
		int m = sys_menu_ss_rows(true, NULL, NULL, pr, pid), j, unlink = 0;

		ck(pid[0] == SSP_ACCOUNT && !strcmp(val(&pr[0]), "None") && pr[0].live,
		   "ScreenScraper's page: Account, none, live to link one");
		for (j = 0; j < m; j++) if (pid[j] == SSP_UNLINK) unlink++;
		ck(!unlink, "and nothing to unlink");
		m = sys_menu_ss_rows(true, "tarik", "works", pr, pid);
		for (j = 0; j < m; j++) if (pid[j] == SSP_UNLINK) unlink++;
		ck(!strcmp(val(&pr[0]), "tarik") && unlink == 1, "linked: the account, and Unlink");
		m = sys_menu_ss_rows(false, NULL, NULL, pr, pid);
		ck(!pr[0].live && !strcmp(val(&pr[0]), "Not in this build"), "a build without its key says so");
	}
	{
		menu_row sr[SG_ROWS];
		sg_row ids[SG_ROWS];
		const char *masked = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b";
		int m = sys_menu_sgdb_rows(NULL, NULL, sr, ids), j, notes = 0;

		ck(m <= SG_ROWS, "SteamGridDB's page fits SG_ROWS");
		ck(ids[0] == SG_KEY && !strcmp(sr[0].label, "Key") && !strcmp(val(&sr[0]), "None")
		   && sr[0].live, "SteamGridDB's page with no key: Key, none, live to type one");
		for (j = 0; j < m; j++) {
			ck(ids[j] != SG_REMOVE && ids[j] != SG_STATUS, "and nothing to unlink");
			if (sr[j].value == MENU_NOTE_MARK) notes++;
		}
		ck(notes == 2, "two notes say where a key comes from");
		m = sys_menu_sgdb_rows(masked, "Works", sr, ids);
		ck(m <= SG_ROWS && ids[1] == SG_STATUS && !strcmp(val(&sr[1]), "Works") &&
		   !sr[1].live, "with a key: its status, which is not a button");
		ck(ids[2] == SG_REMOVE && !strcmp(sr[2].label, "Unlink") && sr[2].live,
		   "and Unlink, live");
	}
	{
		snaps_console c[2] = { { "SNES", true, 212000000ull }, { "Game Boy", false, 0 } };
		snaps_ui vu = { .play = true, .ss_in = false, .ss_have = true,
		                .sound = SNAP_MUTED,
		                .c = c, .nc = 2, .total = 212000000ull };
		static snaps_bufs vb;
		menu_row vr[SN_ROWS];
		sn_row vid[SN_ROWS];
		int con[SN_ROWS], m = sys_menu_snaps_rows(&vu, &vb, vr, vid, con), j;

		printf("the Video Snaps page:\n");
		/* Getting them is MENU > Scraper's Systems grid now (2026-11-13) */
		for (j = 0; j < m; j++) ck(!vr[j].label || strcmp(vr[j].label, "Fetch with Box Art"),
		                            "no Fetch with Box Art row: the Systems grid gets them");
		ck(vid[0] == SN_PLAY && !strcmp(val(&vr[0]), "On") && vr[0].live, "playing is On to start");
		ck(vid[1] == SN_SOUND && !strcmp(val(&vr[1]), "Muted"), "and muted to start");
		ck(vid[3] == SN_CONSOLE && con[3] == 0 && !strcmp(vr[3].label, "SNES") &&
		   !strcmp(val(&vr[3]), "On \xc2\xb7 212 MB"), "a console with its room and its switch");
		ck(vid[4] == SN_CONSOLE && !strcmp(val(&vr[4]), "Off \xc2\xb7 0 KB"), "one turned off");
		ck(vid[m - 1] == SN_DELETE && !strcmp(val(&vr[m - 1]), "212 MB") && vr[m - 1].live,
		   "Delete All last, with what it gives back");
	}

	/* Controls needs nothing of the device - no network, no account, no
	 * card - so it is live in every state this menu can be in, and it sits
	 * with About at the end where the rows you only read live. */
	printf("the Controls row:\n");
	ck(!strcmp(rows[PM_CONTROLS].label, "Controls"), "it is there");
	ck(rows[PM_CONTROLS].live, "and live with the radio down and nobody signed in");
	ck(!rows[PM_CONTROLS].value, "and says nothing in the value column");
	ck(PM_CONTROLS == PM_TOUR - 1, "and sits just above Show Tutorial");

	/* The tutorial again (DESIGN.md 11.10): drawn from shapes, so it needs
	 * no library, no network and no account, and it sits between the two
	 * rows you only read. */
	printf("the Show Tutorial row:\n");
	ck(!strcmp(rows[PM_TOUR].label, "Show Tutorial"), "it is there");
	ck(rows[PM_TOUR].live, "and live with the radio down and nobody signed in");
	ck(!rows[PM_TOUR].value, "and says nothing in the value column");
	ck(PM_TOUR == PM_ROWS - 1 && sys_menu_tab_of(PM_TOUR) == PT_ABOUT,
	   "and closes About, the last tab");

	/* Storage (DESIGN.md 11.32): just above System Update, live on every
	 * device, its value the room left on the card */
	printf("the Storage row:\n");
	ck(!strcmp(rows[PM_STORAGE].label, "Storage"), "it is there");
	ck(rows[PM_STORAGE].live && !strcmp(val(&rows[PM_STORAGE]), "7.4 GB free"),
	   "live, and its value is the room left");
	ck(PM_STORAGE == PM_UPDATE - 1, "and sits just above System Update");

	/* System Update (DESIGN.md 11.17): just above About, its value the
	 * installed version or the news, on a card with the update program */
	printf("the System Update row:\n");
	ck(!strcmp(rows[PM_UPDATE].label, "System Update"), "it is there");
	ck(rows[PM_UPDATE].live && !strcmp(val(&rows[PM_UPDATE]), "0.2.0 available"),
	   "live, and its value says the news");
	ck(sys_menu_tab_of(PM_UPDATE) == PT_SYSTEM && PM_UPDATE == PM_ABOUT - 1,
	   "and closes System, the tab before About");
	u.update = NULL;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == PM_ROWS - 1 && !has_label(rows, n, "System Update") &&
	   !strcmp(rows[n - 1].label, "Show Tutorial"),
	   "a card without the program has no row, and Show Tutorial stays last");

	/* Developer Mode (src/usblink.h): a development image's, on or off,
	 * between Show Tutorial and System Update; a release image has none.
	 * Built without System Update from here, as the card above left it;
	 * Developer Mode is ahead of that row, so its index stands. */
	printf("the Developer Mode row:\n");
	ck(!strcmp(rows[PM_DEVELOPER].label, "Developer Mode"), "it is there");
	ck(rows[PM_DEVELOPER].live, "and live");
	ck(!strcmp(val(&rows[PM_DEVELOPER]), "Off"), "off says Off, a switch's value");
	u.developer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_DEVELOPER]), "On"), "on says On");
	u.no_developer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == PM_ROWS - 2 && !has_label(rows, n, "Developer Mode"),
	   "and a release image has none");
	u.no_developer = false;
	u.developer = false;

}

/* Connecting and idle are not the same as off, and the row must not flatten
 * them: "not connected" and "connecting" answer different questions. */
static void wifi_row_wording(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;

	memset(&u, 0, sizeof u);
	printf("the Wi-Fi row's three off states:\n");

	u.wifi = WIFI_CONNECTING;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_WIFI]), "Connecting"), "connecting");

	u.wifi = WIFI_IDLE;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_WIFI]), "Not connected"), "up but unassociated");

	/* Connected with no name yet: better to say nothing useful than to print
	 * an empty value where a network name belongs. */
	u.wifi = WIFI_CONNECTED;
	u.ssid = "";
	sys_menu_build(&u, rows, &b, &heading);
	ck(val(&rows[PM_WIFI])[0] != 0, "connected with no SSID still says something");
}

/* The system menu. Games and Core carry real values, so a wrong count here is
 * a row lying about the machine it describes. */
static void system_menu(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	int n;

	memset(&u, 0, sizeof u);
	u.games = true;
	u.wifi = WIFI_CONNECTED;
	u.sys_name = "Game Boy";
	u.sys_core = "gambatte";
	u.game_count = 412;
	u.dmode = "Sharp";
	u.border = "Blur";
	n = sys_menu_build(&u, rows, &b, &heading);

	printf("system menu:\n");
	ck(n == SM_ROWS - 4, "every row is filled, but Game Language, which only the Nintendo DS has, Super Game Boy, "
	   "Timing and Handheld Color");
	ck(!strcmp(heading, "Game Boy"), "heading is the system, not ZyperOS");
	ck(!strcmp(val(&rows[sm_at(&u, SM_GAMES)]), "412"), "the count is the real one");
	ck(!strcmp(val(&rows[sm_at(&u, SM_CORE)]), "gambatte"), "the core is the real one");
	ck(!strcmp(val(&rows[sm_at(&u, SM_DISPLAY)]), "Sharp"), "display mode is passed through");
	/* Border (src/border.h) right under Display, while the mode leaves room */
	ck(sm_at(&u, SM_BORDER) == sm_at(&u, SM_DISPLAY) + 1 && !strcmp(rows[sm_at(&u, SM_BORDER)].label, "Border") &&
	   !strcmp(val(&rows[sm_at(&u, SM_BORDER)]), "Blur") && rows[sm_at(&u, SM_BORDER)].live &&
	   menu_sound_at(b.sound, n, sm_at(&u, SM_BORDER)) == MENU_SND_RING,
	   "Border right under Display, its value, a row that steps round, on A too");
	{
		sys_ui nb = u;
		menu_row r2[MENU_MAX_ROWS];
		menu_bufs b2;
		const char *h2;

		nb.border = NULL;
		ck(sys_menu_build(&nb, r2, &b2, &h2) == n - 1 && !has_label(r2, n - 1, "Border"),
		   "and no Border row where the mode leaves no room");
	}
	/* Shader right under Display, no caption between (what a mode does is
	 * said in its list, beside a picture of it) */
	ck(sm_at(&u, SM_SHADER) == sm_at(&u, SM_BORDER) + 1 &&
	   !strcmp(rows[sm_at(&u, SM_SHADER)].label, "Shader") &&
	   !strcmp(val(&rows[sm_at(&u, SM_SHADER)]), "Off") && rows[sm_at(&u, SM_SHADER)].live,
	   "Shader right after Display and its Border, Off when nothing is said, a stop");
	ck(b.sound[sm_at(&u, SM_SHADER)] == MENU_SND_RING, "and it steps round, on A too");
	u.shader = "Sharp-Shimmerless";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_SHADER)]), "Sharp-Shimmerless"), "the shader is passed through");
	u.shader = NULL;
	/* Strength under it, for this system alone (src/strength.h) */
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(sm_at(&u, SM_STRENGTH) == sm_at(&u, SM_SHADER) + 1 &&
	   !strcmp(rows[sm_at(&u, SM_STRENGTH)].label, "Strength") &&
	   !strcmp(val(&rows[sm_at(&u, SM_STRENGTH)]), "100%") && rows[sm_at(&u, SM_STRENGTH)].live,
	   "Strength right under Shader, 100% when nothing is said");
	ck(b.sound[sm_at(&u, SM_STRENGTH)] == MENU_SND_NO_A, "left and right step it, A does nothing");
	u.strength = "40%";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_STRENGTH)]), "40%"), "the strength is passed through");
	u.strength = NULL;
	u.no_strength = true;
	ck(sys_menu_build(&u, rows, &b, &heading) == n - 1 && sm_at(&u, SM_STRENGTH) < 0 &&
	   !strcmp(rows[sm_at(&u, SM_SHADER) + 1].label, T(S_DOWNLOADS_HEADING)),
	   "and gone while the shader is not an old screen, Downloads moving up");
	u.no_strength = false;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(rows[sm_at(&u, SM_DISPLAY)].live && rows[sm_at(&u, SM_RESCAN)].live, "the two live rows are live");
	ck(!rows[sm_at(&u, SM_GAMES)].live && !rows[sm_at(&u, SM_CORE)].live, "reported facts are not rows to press");
	ck(rows[sm_at(&u, SM_BOXART)].live && !strcmp(rows[sm_at(&u, SM_BOXART)].label, T(S_DOWNLOADS_HEADING)),
	   "Box Art is Downloads, this console's page, live on a network");
	u.sys_downloads = "41 to fetch";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_BOXART)]), "41 to fetch"), "its value is the console's count");
	u.sys_downloads = NULL;
	sys_menu_build(&u, rows, &b, &heading);

	/* A shelf that lists two cores, the Dreamcast's: Core is a choice. */
	u.sys_core = "Flycast";
	u.core_choice = true;
	sys_menu_build(&u, rows, &b, &heading);
	ck(rows[sm_at(&u, SM_CORE)].live && !strcmp(val(&rows[sm_at(&u, SM_CORE)]), "Flycast"),
	   "two cores make Core a row to press, naming the one in use");
	u.core_choice = false;
	u.sys_core = "gambatte";

	/* The same menu with the radio down: Downloads stays live, its page
	 * counts and deletes with no network and says what Download needs */
	u.wifi = WIFI_OFF;
	sys_menu_build(&u, rows, &b, &heading);
	ck(rows[sm_at(&u, SM_BOXART)].live, "Downloads stays live with the radio down");
	ck(rows[sm_at(&u, SM_RESCAN)].live, "Rescan does not need a network");

	/* Favorites, which is not a system: no core and no folder, so four of the
	 * six rows have nothing to describe and one of them - Display Mode -
	 * would give a game a different aspect depending on the shelf it was
	 * launched from. */
	{
		sys_ui f = u;
		menu_row frows[MENU_MAX_ROWS];
		menu_bufs fb;
		const char *fhead = NULL;
		int fn, k;

		f.fav = true;   /* by what it has: no core, no folder */
		f.game_count = 0;
		f.sys_name = "Favorites";
		f.sys_core = "";
		fn = sys_menu_build(&f, frows, &fb, &fhead);
		printf("the Favorites shelf's menu:\n");
		ck(fn == SM_FAV_ROWS, "four rows, not eight");
		ck(!strcmp(frows[0].label, "Games"), "how many are on it");
		ck(!strcmp(frows[1].label, "Random Game"), "one of them, picked for you");
		ck(!frows[1].live, "which an empty Favorites cannot do");
		ck(!strcmp(frows[2].label, "Sort By"), "and what order they are in");
		ck(!strcmp(frows[3].label, "View") && frows[3].live, "and how the shelf shows them");
		f.game_count = 4;
		fn = sys_menu_build(&f, frows, &fb, &fhead);
		ck(frows[1].live, "and one with games can");
		for (k = 0; k < fn; k++) {
			ck(strcmp(lbl(&frows[k]), "Core") != 0, "no Core: it has none");
			ck(strcmp(lbl(&frows[k]), "Display Mode") != 0,
			   "no Display Mode: the owner's system decides that");
			ck(strcmp(lbl(&frows[k]), "Box Art") != 0, "no Box Art: no folder");
			ck(strcmp(lbl(&frows[k]), "Rescan Folder") != 0, "and nothing to rescan");
		}
	}

	/* MP4, which is not a system either: no core, no ROM folder, no display
	 * mode. What it does have is a folder of music, two orders to see it in,
	 * and Rescan Folder for music copied in. */
	{
		sys_ui m = u;
		menu_row mrows[MENU_MAX_ROWS];
		menu_bufs mb;
		const char *mhead = NULL;
		int mn, k;

		m.mp4 = true;
		m.sys_name = "MP4";
		m.sys_core = "";
		m.game_count = 7;
		m.sort = NULL;
		m.wifi = WIFI_CONNECTED;     /* the case above left the radio down */
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		printf("MP4's shelf menu:\n");
		ck(mn == 10, "ten rows with only music, Quit last");
		ck(!strcmp(mrows[0].label, "Albums") && !strcmp(val(&mrows[0]), "7"),
		   "how many albums");
		ck(!strcmp(mrows[1].label, "Sort By") && mrows[1].live &&
		   !strcmp(val(&mrows[1]), "Artist"),
		   "Sort By, and by artist when nothing says otherwise");
		ck(!strcmp(mrows[2].label, "View") && mrows[2].live && !strcmp(val(&mrows[2]), "Covers") &&
		   menu_sound_at(mb.sound, mn, 2) == MENU_SND_RING,
		   "View, Covers when nothing says otherwise, turning over (DESIGN.md 11.67)");
		ck(!strcmp(mrows[3].label, "Shuffle All") && mrows[3].live && !val(&mrows[3])[0],
		   "Shuffle All, live whether or not anything plays");
		ck(!strcmp(mrows[4].label, "Player Look") && mrows[4].live &&
		   !strcmp(val(&mrows[4]), "Cover"),
		   "Player Look, Cover when nothing says otherwise (DESIGN.md 11.64)");
		ck(!strcmp(mrows[5].label, "Equalizer") && mrows[5].live && !strcmp(val(&mrows[5]), "Off"),
		   "Equalizer, MENU's own row, Off until it is set");
		ck(!strcmp(mrows[6].label, "Lock Screen") && !mrows[6].live &&
		   !strcmp(val(&mrows[6]), "While playing"),
		   "Lock Screen, dead and saying why with nothing playing");
		ck(!strcmp(mrows[7].label, "Album Art") && mrows[7].live,
		   "covers to fetch, on a network");
		ck(!strcmp(mrows[8].label, "Rescan Folder") && mrows[8].live,
		   "and a rescan that works");
		m.grid = true;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[2]), "Grid"), "View says the Grid");
		m.grid = false;
		m.listed = true;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[2]), "List"), "and the List");
		m.listed = false;
		m.mp4_look = "Spectrum";
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[4]), "Spectrum"), "Player Look says the look that is on");
		m.mp4_look = NULL;
		m.eq = "Bass";
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[5]), "Bass"), "the Equalizer says the preset in use");
		m.eq = NULL;
		m.mp4_playing = true;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mrows[6].live && !val(&mrows[6])[0], "Lock Screen live while music plays");
		m.mp4_playing = false;
		m.sort = "Album";
		m.wifi = WIFI_OFF;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[1]), "Album"), "the order it is in");
		ck(!mrows[7].live && !strcmp(val(&mrows[7]), "Needs Wi-Fi"),
		   "Album Art says why it cannot, off the network");
		ck(mrows[1].live && mrows[2].live && mrows[3].live && mrows[4].live && mrows[5].live &&
		   mrows[8].live,
		   "and neither Sort By, View, Shuffle All, Player Look, Equalizer nor Rescan Folder needs one");
		for (k = 0; k < mn; k++) {
			ck(strcmp(lbl(&mrows[k]), "Core") != 0, "no Core: it has none");
			ck(strcmp(lbl(&mrows[k]), "Display Mode") != 0, "no Display Mode");
		}

		/* Music and books on one card: Show, between the count and the
		 * order, and on books no Album Art and the orders a book goes by. */
		m.sort = NULL;
		m.wifi = WIFI_CONNECTED;
		m.mp4_both = true;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == SM_MP4_ROWS - 1, "eleven rows with more than one kind on music (no Repeat)");
		ck(!strcmp(mrows[1].label, "Show") && mrows[1].live &&
		   !strcmp(val(&mrows[1]), "Music"), "Show, on music");
		ck(!strcmp(mrows[2].label, "Sort By") && !strcmp(mrows[3].label, "View") &&
		   !strcmp(mrows[4].label, "Shuffle All") &&
		   mrows[4].live && !strcmp(mrows[5].label, "Player Look") &&
		   !strcmp(mrows[6].label, "Equalizer") && !strcmp(mrows[7].label, "Lock Screen") &&
		   !strcmp(mrows[8].label, "Album Art"),
		   "Sort By, View, Shuffle All, Player Look, Equalizer, Lock Screen, then Album Art");
		m.mp4_books = true;
		m.game_count = 2;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == 8 && !strcmp(mrows[3].label, "View") && !strcmp(mrows[4].label, "Equalizer"),
		   "eight rows on books, View and the Equalizer among them");
		ck(!strcmp(mrows[0].label, "Books") && !strcmp(val(&mrows[0]), "2"),
		   "how many books");
		ck(!strcmp(val(&mrows[1]), "Audiobooks"), "Show, on books");
		ck(!strcmp(val(&mrows[2]), "Author"), "by author when nothing says otherwise");
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0 &&
			   strcmp(lbl(&mrows[k]), "Player Look") != 0,
			   "no Album Art on books (MusicBrainz knows records), no Shuffle All, no Player Look");
		/* Videos: their own count and orders, and like books no Album Art
		 * and no Shuffle All. */
		m.mp4_books = false;
		m.mp4_videos = true;
		m.game_count = 3;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == 9 && !strcmp(mrows[0].label, "Videos") && !strcmp(val(&mrows[0]), "3") &&
		   !strcmp(val(&mrows[1]), "Videos") && !strcmp(val(&mrows[2]), "Folder"),
		   "on videos: how many, Show, by folder");
		ck(!strcmp(mrows[4].label, "Repeat") && !strcmp(val(&mrows[4]), "Off") && mrows[4].live &&
		   menu_sound_at(mb.sound, mn, 4) == MENU_SND_RING,
		   "on videos: Repeat, Off to start with, steps round");
		m.mp4_repeat = "One";
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[4]), "One"), "Repeat says what it is");
		m.mp4_repeat = NULL;
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0 &&
			   strcmp(lbl(&mrows[k]), "Player Look") != 0,
			   "no Album Art, Shuffle All or Player Look on videos");
		/* All, where Show starts: the count is the Total, the orders the
		 * videos', and Shuffle All and Album Art when there are songs */
		m.mp4_videos = false;
		m.mp4_all = true;
		m.mp4_songs = true;
		m.game_count = 7;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == SM_MP4_ROWS && !strcmp(mrows[0].label, "Total") &&
		   !strcmp(val(&mrows[0]), "7") && !strcmp(val(&mrows[1]), "All") &&
		   !strcmp(val(&mrows[2]), "Folder") && !strcmp(mrows[3].label, "View") &&
		   !strcmp(mrows[4].label, "Shuffle All") &&
		   !strcmp(mrows[5].label, "Player Look") && !strcmp(mrows[6].label, "Repeat") &&
		   !strcmp(mrows[7].label, "Equalizer") && !strcmp(mrows[9].label, "Album Art"),
		   "on All: the Total, Show All, by folder, View, Shuffle All, Player Look, Repeat, Equalizer and Album Art");
		m.mp4_songs = false;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0 &&
			   strcmp(lbl(&mrows[k]), "Player Look") != 0,
			   "All with no songs: no Album Art, Shuffle All or Player Look");
		m.mp4_all = false;
		m.mp4_books = true;
		{
			sm_mp4_row ids[SM_MP4_ROWS];

			ck(sys_menu_mp4_rows(3, true, false, ids) == SM_MP4_ROWS && ids[3] == SMM_VIEW &&
			   ids[4] == SMM_SHUFFLE,
			   "All with songs has every row, View under Sort By");
			/* View under Sort By on every shelf of MP4's (DESIGN.md 11.67) */
			for (int kind = 0; kind <= 3; kind++)
				for (int both = 0; both <= 1; both++) {
					int q = sys_menu_mp4_rows(kind, both, false, ids), at = -1;

					for (int j = 0; j < q; j++) if (ids[j] == SMM_VIEW) at = j;
					ck(at > 0 && ids[at - 1] == SMM_SORT, "View under Sort By on every MP4 shelf");
				}

			/* Repeat on the shelves with videos (Videos, All), not on
			 * music or books, after Shuffle All where that is */
			for (int kind = 0; kind <= 3; kind++) {
				int q = sys_menu_mp4_rows(kind, true, false, ids), has = 0;

				for (int j = 0; j < q; j++) if (ids[j] == SMM_REPEAT) has = 1;
				ck(has == (kind == 2 || kind == 3), "Repeat on Videos and All alone");
			}
			ck(sys_menu_mp4_rows(true, false, false, ids) == 7 && ids[1] == SMM_SORT &&
			   ids[2] == SMM_VIEW && ids[3] == SMM_EQ && ids[4] == SMM_LOCK && ids[5] == SMM_RESCAN &&
			   ids[6] == SMM_QUIT,
			   "books alone: the count, Sort By, View, Equalizer, Lock Screen, Rescan Folder and Quit");
			/* Quit last on every shelf of MP4's, live, X's glyph its
			 * value: X does it from any row (DESIGN.md 11.14) */
			for (int kind = 0; kind <= 3; kind++)
				for (int both = 0; both <= 1; both++)
					for (int nw = 0; nw <= 1; nw++) {
						int q = sys_menu_mp4_rows(kind, both, nw, ids);
						ck(q > 0 && ids[q - 1] == SMM_QUIT,
						   "Quit is the last row on every MP4 shelf");
					}
		}
		{
			int q;

			m.mp4_books = false;
			mn = sys_menu_build(&m, mrows, &mb, &mhead);
			q = mn - 1;
			ck(mn > 0 && !strcmp(mrows[q].label, "Quit") && mrows[q].value == MENU_KEY_X &&
			   mrows[q].live && !menu_sound_at(mb.sound, mn, q),
			   "MP4's Quit: last, live, X's glyph at its right, plain to the ear");
		}
	}
}

/* The labels, on their own. A row that reads "90s" for a minute and a half
 * would be wrong in a way no screenshot makes obvious. */
static void auto_off_words(void)
{
	char s[16];
	struct { int sec; const char *want; } t[] = {
		{ 0, "Never" }, { 30, "30 s" }, { 60, "1 min" },
		{ 120, "2 min" }, { 300, "5 min" }, { 600, "10 min" },
	};
	size_t i;

	printf("auto off labels:\n");
	for (i = 0; i < sizeof t / sizeof t[0]; i++) {
		sys_menu_auto_off_label(t[i].sec, s, sizeof s);
		ck(!strcmp(s, t[i].want), t[i].want);
	}
}


/* ---------- where the cursor can go --------------------------------------- */

/* Walk the whole menu and collect every row the cursor can actually rest on.
 * A row that is drawn but cannot be selected is fine; a row that CAN be
 * selected and then does nothing when pressed is the defect. */
/* Game Language (src/dslang.h): a row of the Nintendo DS shelf's menu, under
 * Core, and on no other shelf's menu. The DS has one core, DSperate, so its
 * Core row names it and is no row to press. */
static void dslang_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS], plain[MENU_MAX_ROWS];
	sm_row sm[SM_ROWS];
	const char *heading;
	int n, np, i, at;

	memset(&u, 0, sizeof u);
	u.games = true;
	u.wifi = WIFI_CONNECTED;
	u.sys_name = "Nintendo DS"; u.sys_core = "dsperate"; u.core_choice = false;
	u.game_count = 20; u.dmode = "Native"; u.border = "Off";
	np = sys_menu_build(&u, plain, &b, &heading);

	printf("system menu, Game Language:\n");
	ck(!has_label(plain, np, "Game Language"), "no Game Language row unless the shelf says so");
	ck(!has_label(plain, np, "Timing"), "nor Timing");
	ck(np == SM_ROWS - 4, "and every other row is there");
	ck(!plain[sm_at(&u, SM_CORE)].live && !strcmp(val(&plain[sm_at(&u, SM_CORE)]), "dsperate"),
	   "one core: the DS's Core row names DSperate and is no row to press");

	u.dslang = "Auto (English)";
	n = sys_menu_build(&u, rows, &b, &heading);
	at = sm_at(&u, SM_DSLANG);
	ck(n == np + 1 && n == SM_ROWS - 3, "one row more, every row but Super Game Boy, Timing and Handheld Color");
	ck(at == sm_at(&u, SM_CORE) + 1, "right under Core");
	ck(!strcmp(rows[at].label, "Game Language"), "named Game Language");
	ck(!strcmp(val(&rows[at]), "Auto (English)"), "saying what it is set to");
	ck(rows[at].live, "a row to press");
	ck(menu_sound_at(b.sound, n, at) == MENU_SND_RING, "that steps round, on A too");
	/* the rest in the order it was, each row as it was */
	ck(sys_menu_system_rows(&u, sm) == n && sm[at] == SM_DSLANG, "the build and the list agree");
	for (i = 0; i < n; i++) {
		int j = i < at ? i : i - 1;

		if (i == at) continue;
		if (strcmp(rows[i].label, plain[j].label) || strcmp(val(&rows[i]), val(&plain[j]))) {
			ck(false, "the other rows are as they were");
			break;
		}
	}
	u.dslang = "Español";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[at]), "Español"), "and a language by its name");

	/* No When Slow (1.0.6: the DS keeps its speed, frameskip=late, with no
	 * row for it) */
	ck(!has_label(rows, n, "When Slow"), "and no When Slow under it");

	/* Timing (src/pacing.h): under Game Language on the DS's shelf, under
	 * Core on a shelf without it (the GBA's) */
	printf("system menu, Timing:\n");
	u.pace = "Smooth";
	n = sys_menu_build(&u, rows, &b, &heading);
	{
		int gl = sm_at(&u, SM_DSLANG), tr = sm_at(&u, SM_PACE);

		ck(n == SM_ROWS - 2, "with Timing too, every row but the Game Boys' Super Game Boy and Handheld Color");
		ck(tr == gl + 1, "right under Game Language");
		ck(!strcmp(rows[tr].label, "Timing"), "named Timing");
		ck(!strcmp(val(&rows[tr]), "Smooth"), "saying what it is set to");
		ck(rows[tr].live, "a row to press");
		ck(menu_sound_at(b.sound, n, tr) == MENU_SND_RING, "that steps round, on A too");
		ck(sys_menu_system_rows(&u, sm) == n && sm[tr] == SM_PACE, "the build and the list agree");
		ck(!strcmp(rows[gl].label, "Game Language") && !strcmp(rows[tr + 1].label, plain[at].label),
		   "and the rows round it are as they were");
		u.pace = "Exact";
		sys_menu_build(&u, rows, &b, &heading);
		ck(!strcmp(val(&rows[tr]), "Exact"), "and Exact");
	}
	u.dslang = NULL;
	u.sys_name = "Game Boy Advance"; u.sys_core = "mgba";
	u.pace = "Smooth";
	n = sys_menu_build(&u, rows, &b, &heading);
	{
		int tr = sm_at(&u, SM_PACE);

		ck(n == SM_ROWS - 3, "on a shelf without the DS's row, one row more than its own");
		ck(tr == sm_at(&u, SM_CORE) + 1 && !strcmp(rows[tr].label, "Timing"), "Timing right under Core");
		ck(!has_label(rows, n, "Game Language"), "and no DS row with it");
		ck(!has_label(rows, n, "Super Game Boy"), "nor the Game Boys' Super Game Boy");
	}

	/* Super Game Boy (src/sgbmode.h): under Core on the Game Boy's and the
	 * Color's shelves, Timing under it */
	printf("system menu, Super Game Boy:\n");
	u.sys_name = "Game Boy";
	u.sgb = "Its Games";
	n = sys_menu_build(&u, rows, &b, &heading);
	{
		int sg = sm_at(&u, SM_SGB), tr = sm_at(&u, SM_PACE);

		ck(n == SM_ROWS - 2, "with Timing, every row but the DS's and Handheld Color");
		ck(sg == sm_at(&u, SM_CORE) + 1, "right under Core");
		ck(tr == sg + 1 && !strcmp(rows[tr].label, "Timing"), "Timing under it");
		ck(!strcmp(rows[sg].label, "Super Game Boy"), "named Super Game Boy");
		ck(!strcmp(val(&rows[sg]), "Its Games"), "saying what it is set to");
		ck(rows[sg].live, "a row to press");
		ck(menu_sound_at(b.sound, n, sg) == MENU_SND_RING, "that steps round, on A too");
		ck(sys_menu_system_rows(&u, sm) == n && sm[sg] == SM_SGB, "the build and the list agree");
	}
	/* Handheld Color (src/border_pic.h): right under Border, while it is a
	 * frame that comes in more than one color */
	printf("system menu, Handheld Color:\n");
	u.border = "Handheld";
	u.border_color = "Teal";
	n = sys_menu_build(&u, rows, &b, &heading);
	{
		int bo = sm_at(&u, SM_BORDER), co = sm_at(&u, SM_BORDER_COLOR);

		ck(n == SM_ROWS - 1 && co == bo + 1 && !strcmp(rows[co].label, "Handheld Color") &&
		   !strcmp(val(&rows[co]), "Teal") && rows[co].live &&
		   menu_sound_at(b.sound, n, co) == MENU_SND_RING, "right under Border, its color, a row that steps round");
		ck(sys_menu_system_rows(&u, sm) == n && sm[co] == SM_BORDER_COLOR, "the build and the list agree");
	}
	u.border_color = NULL;
	u.sgb = NULL;
	u.pace = NULL;
}

/* Where a console shelf's menu has `which`, or -1: the enum's numbers are the
 * full list's, and Game Language is only on the Nintendo DS's shelf (a Box
 * Art row is not there without a radio) */
static int sm_at(const sys_ui *u, sm_row which)
{
	sm_row ids[SM_ROWS];
	int n = sys_menu_system_rows(u, ids), k;

	for (k = 0; k < n; k++)
		if (ids[k] == which) return k;
	return -1;
}
static int reachable(const menu_row *rows, int n, int *out, int max)
{
	int sel = 0, k = 0, i;

	if (!rows[0].live) sel = menu_step_sel(rows, n, 0, +1);
	if (!rows[sel].live) return 0;      /* nothing live at all */
	for (i = 0; i < n && k < max; i++) {
		int next;

		out[k++] = sel;
		next = menu_step_sel(rows, n, sel, +1);
		if (next == sel) break;         /* the last stop: a menu does not wrap */
		sel = next;
	}
	return k;
}

static bool holds(const int *v, int n, int want)
{
	int i;
	for (i = 0; i < n; i++) if (v[i] == want) return true;
	return false;
}

/* The migration of the system menu onto the runner changed this on purpose:
 * the cursor used to walk every row, including the four that only report a
 * fact. Now it visits what can be pressed. */
static void cursor_reaches(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	int got[MENU_MAX_ROWS], n, k;

	memset(&u, 0, sizeof u);
	u.games = true;
	u.wifi = WIFI_CONNECTED;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	k = reachable(rows, n, got, MENU_MAX_ROWS);

	printf("system menu, what the cursor can reach:\n");
	ck(k == 9, "nine rows, not the facts (Strength among them, as a zeroed sys_ui shows it)");
	ck(holds(got, k, sm_at(&u, SM_RANDOM)), "Random Game, with games on the shelf");
	/* DESIGN.md 11.70: Favorites First, right under Sort By, Off as it
	 * ships, On when the shelf has it */
	ck(holds(got, k, sm_at(&u, SM_FAVFIRST)), "Favorites First");
	ck(sm_at(&u, SM_FAVFIRST) == sm_at(&u, SM_SORT) + 1 &&
	   !strcmp(rows[sm_at(&u, SM_FAVFIRST)].label, "Favorites First"), "Favorites First right under Sort By");
	ck(!strcmp(val(&rows[sm_at(&u, SM_FAVFIRST)]), "Off"), "Favorites First is Off, as it ships");
	u.fav_first = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_FAVFIRST)]), "On"), "or On");
	u.fav_first = false;
	n = sys_menu_build(&u, rows, &b, &heading);
	/* Not on the shelves with rows of their own: Favorites (all of it
	 * favorites), Recents, Collections and Screenshots */
	{
		sys_ui o = u;
		menu_row orow[MENU_MAX_ROWS];
		menu_bufs ob;
		const char *oh;
		int on, oi, j, has = 0;

		for (j = 0; j < 5; j++) {
			o = u;
			o.fav = j == 0 || j == 1;
			o.recents = j == 1;
			o.coll = j == 2 || j == 3;
			o.coll_inside = j == 3;
			o.shots = j == 4;
			on = sys_menu_build(&o, orow, &ob, &oh);
			for (oi = 0; oi < on; oi++)
				if (orow[oi].label && !strcmp(orow[oi].label, "Favorites First")) has++;
		}
		ck(!has, "no Favorites First on Favorites, Recents, Collections or Screenshots");
	}
	/* 1.0.5: the shelf's covers or a list, under Sort By (and Favorites
	 * First, since 11.70) */
	ck(holds(got, k, sm_at(&u, SM_VIEW)), "View");
	ck(sm_at(&u, SM_VIEW) == sm_at(&u, SM_FAVFIRST) + 1, "right under Sort By and Favorites First");
	ck(!strcmp(val(&rows[sm_at(&u, SM_VIEW)]), "Covers"), "the covers, as it always was");
	u.listed = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_VIEW)]), "List"), "or a list");
	u.listed = false;
	u.video = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_VIEW)]), "Video"), "or the Video view (DESIGN.md 11.57)");
	u.video = false;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(holds(got, k, sm_at(&u, SM_DISPLAY)), "Display Mode");
	ck(holds(got, k, sm_at(&u, SM_SHADER)), "Shader");
	/* Live since sorting stopped being alphabetical-only. It was a
	 * placeholder for long enough that this check counted three. */
	ck(holds(got, k, sm_at(&u, SM_SORT)), "Sort By");
	ck(holds(got, k, sm_at(&u, SM_BOXART)), "Downloads");
	ck(holds(got, k, sm_at(&u, SM_RESCAN)), "Rescan Folder");
	ck(!holds(got, k, sm_at(&u, SM_GAMES)) && !holds(got, k, sm_at(&u, SM_CORE)),
	   "the reported facts are not stops");

	memset(&u, 0, sizeof u);
	u.wifi = WIFI_OFF;
	u.update = "0.1.0";
	n = sys_menu_build(&u, rows, &b, &heading);
	k = reachable(rows, n, got, MENU_MAX_ROWS);

	/* The Pixel 2 with an adapter and no network: the transfer screen
	 * makes the handheld its own network, so the row is live and says so. */
	{
		sys_ui h;
		menu_row hr[MENU_MAX_ROWS];
		menu_bufs hb;
		const char *hh;

		memset(&h, 0, sizeof h);
		h.wifi = WIFI_IDLE;
		h.hotspot = true;
		sys_menu_build(&h, hr, &hb, &hh);
		printf("ZyperOS menu, no network, an adapter that can host one:\n");
		ck(hr[PM_XFER].live && !strcmp(val(&hr[PM_XFER]), "Hotspot"),
		   "Wi-Fi File Transfer is live and says it makes a hotspot");
		ck(hr[PM_SCRAPER].live, "Scraper opens: its fetch says it needs a real network");
		h.wifi = WIFI_CONNECTED;
		sys_menu_build(&h, hr, &hb, &hh);
		ck(hr[PM_XFER].live && !hr[PM_XFER].value, "on a network it is today's row");
		h.wifi = WIFI_OFF;
		h.cable_xfer = true;
		sys_menu_build(&h, hr, &hb, &hh);
		ck(!strcmp(hr[PM_XFER].label, "USB File Transfer") && !hr[PM_XFER].value,
		   "over the cable it is USB File Transfer, never a hotspot");
	}

	printf("ZyperOS menu offline, what the cursor can reach:\n");
	/* No network needed to pair a headset, so it stays reachable offline -
	 * unlike the three rows above, which do need one. */
	ck(holds(got, k, PM_BT), "Bluetooth is reachable with no network");
	ck(!holds(got, k, PM_XFER), "OTH is skipped with no network");
	ck(holds(got, k, PM_SCRAPER), "Scraper is reachable with no network");
	ck(holds(got, k, PM_WIFI), "Wi-Fi is reachable, which is how you fix that");
	ck(holds(got, k, PM_ABOUT), "About is reachable");
	/* Play Time reads what is already stored and asks nothing of the network,
	 * so it stays reachable when everything else is grayed out. */
	ck(holds(got, k, PM_STATS), "Play Time is reachable offline");
}


/* A device with no radio at all, which is not the same as one whose radio is
 * off. Off keeps every row and says "needs Wi-Fi", because turning it on is
 * the fix; with no radio there is no fix, so the rows go. And a device that
 * has both radios, which is the Brick, must get every row in today's order,
 * or the build and the key handler would be reading two different menus. */
static void radio_less(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	pm_row pm[PM_ROWS];
	sm_row sm[SM_ROWS];
	sm_mp4_row mu[SM_MP4_ROWS];
	const char *heading;
	int n, k, i;
	bool ok;

	memset(&u, 0, sizeof u);
	u.wifi = WIFI_OFF;
	u.update = "0.1.0";
	printf("a device with both radios:\n");
	k = sys_menu_zyperos_rows(&u, pm);
	for (i = 0, ok = true; i < k; i++) if (pm[i] != (pm_row)i) ok = false;
	ck(k == PM_ROWS && ok, "every ZyperOS row, in the enum's order");
	k = sys_menu_system_rows(&u, sm);
	{
		/* Every row but the four that only some shelves have, in order */
		int j = 0;

		ok = true;
		for (i = 0; i < SM_ROWS; i++) {
			if (i == SM_DSLANG || i == SM_SGB || i == SM_PACE || i == SM_BORDER || i == SM_BORDER_COLOR)
				continue;
			if (j >= k || sm[j] != (sm_row)i) ok = false;
			j++;
		}
	}
	ck(k == SM_ROWS - 5 && ok, "every system row but Game Language, Super Game Boy, Timing, Border and Handheld "
	   "Color, in the enum's order");

	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	printf("ZyperOS menu, no radio at all:\n");
	ck(n == k && n == PM_ROWS - 3, "three rows fewer, and the build agrees with the list");
	ck(!has_label(rows, n, "Wi-Fi") && !has_label(rows, n, "Bluetooth"),
	   "no Wi-Fi, no Bluetooth");
	ck(!has_label(rows, n, "Wi-Fi File Transfer") && has_label(rows, n, "Downloads"),
	   "no Wi-Fi File Transfer; Downloads stays, for its counts, Video Snaps and the accounts");
	ck(!has_label(rows, n, "Achievements"), "and no Achievements row");
	ck(has_label(rows, n, "Audio Output") && has_label(rows, n, "Play Time") &&
	   has_label(rows, n, "Controls") && has_label(rows, n, "About ZyperOS") &&
	   has_label(rows, n, "Date & Time"),
	   "the rest is all there, Date & Time with it");
	ck(!has_label(rows, n, "Card Style") && has_label(rows, n, "Appearance"),
	   "Appearance stays; Card Style is absent");
	ck(pm[n - 1] == PM_TOUR && !strcmp(rows[n - 1].label, "Show Tutorial") &&
	   pm[0] == PM_SEARCH && !strcmp(rows[0].label, "Search Games") &&
	   pm[1] == PM_STATS && !strcmp(rows[1].label, "Play Time"),
	   "and the list names the rows the build drew, first to last");

	/* The Pixel 2's USB rows: USB Device only while a plug waits, at the
	 * top; USB Accessories always where usbrole decides; neither elsewhere */
	printf("ZyperOS menu, the USB rows:\n");
	u.no_usb_wait = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && !has_label(rows, n, "USB Device") && has_label(rows, n, "USB Accessories") &&
	   pm[0] == PM_SEARCH, "no plug waiting: no USB Device, USB Accessories stays");
	u.usb_auto = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n && strcmp(rows[i].label, "USB Accessories"); i++) ;
	ck(i < n && rows[i].value && !strcmp(rows[i].value, "Automatic"), "USB Accessories reads Automatic");
	u.usb_auto = false;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n && strcmp(rows[i].label, "USB Accessories"); i++) ;
	ck(i < n && rows[i].value && !strcmp(rows[i].value, "Ask"), "and Ask, the default");
	u.no_usb_wait = false;
	u.no_usb_power = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && !has_label(rows, n, "USB Device") && !has_label(rows, n, "USB Accessories"),
	   "a device where nothing decides it: neither row");
	u.no_usb_power = false;

	/* The GKD Pixel 2: no radio, and its files go over the USB cable. */
	u.cable_xfer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	printf("ZyperOS menu, no radio, files over the cable:\n");
	ck(n == k && n == PM_ROWS - 2, "two rows fewer: the transfer row stays");
	for (i = 0; i < k && pm[i] != PM_XFER; i++) { }
	ck(i < k && !strcmp(rows[i].label, "USB File Transfer"),
	   "as USB File Transfer, where the list says it is");
	ck(i < k && rows[i].live && !rows[i].value, "live, and not asking for Wi-Fi");
	ck(!has_label(rows, n, "Wi-Fi File Transfer"), "never under its Wi-Fi name");

	/* The Pixel 2 with a phone's USB tethering on its port: no radio, but a
	 * way out to the internet (2026-11-13), so what fetches comes back */
	u.online = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	printf("ZyperOS menu, no radio, a phone's tethering:\n");
	ck(n == k && n == PM_ROWS - 2, "two rows fewer: Wi-Fi and Bluetooth");
	ck(has_label(rows, n, "Downloads") && has_label(rows, n, "USB File Transfer"),
	   "Downloads is there, beside USB File Transfer");
	ck(!has_label(rows, n, "Achievements"), "and no Achievements row: its page is behind Downloads");
	ck(!has_label(rows, n, "Wi-Fi") && !has_label(rows, n, "Wi-Fi File Transfer"),
	   "and the Wi-Fi rows stay away");
	for (i = 0; i < k && pm[i] != PM_SCRAPER; i++) { }
	ck(i < k && !strcmp(rows[i].label, "Downloads") && rows[i].live,
	   "Downloads live, where the list says it is");
	u.online = false;

	memset(&u, 0, sizeof u);
	u.games = true;
	u.no_wifi = true;
	u.online = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_system_rows(&u, sm);
	printf("system menu, no radio, a phone's tethering:\n");
	for (i = 0; i < k && sm[i] != SM_BOXART; i++) { }
	ck(n == k && i < k && !strcmp(rows[i].label, "Downloads") && rows[i].live && !rows[i].value,
	   "Downloads stays, live and not asking for Wi-Fi");

	memset(&u, 0, sizeof u);
	u.games = true;
	u.no_wifi = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native"; u.border = "Off";
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_system_rows(&u, sm);
	printf("system menu, no radio at all:\n");
	ck(n == k && n == SM_ROWS - 4, "Downloads stays: its counts, Systems and deletes need no network");
	ck(has_label(rows, n, "Downloads") && !has_label(rows, n, "Box Art"), "Downloads, no Box Art");
	ck(sm[n - 1] == SM_RESCAN && !strcmp(rows[n - 1].label, "Rescan Folder"),
	   "Rescan Folder last");

	k = sys_menu_mp4_rows(false, false, true, mu);
	for (i = 0, ok = true; i < k; i++) if (mu[i] == SMM_ART) ok = false;
	ck(ok, "MP4 loses Album Art too");
}

/* The bound is the point. An all-dead menu must terminate, not spin. */
static void step_terminates(void)
{
	menu_row dead[3];
	int i;

	for (i = 0; i < 3; i++) dead[i] = (menu_row){ "x", NULL, false };
	printf("a menu with nothing live:\n");
	ck(menu_step_sel(dead, 3, 1, +1) == 1, "forward stays put");
	ck(menu_step_sel(dead, 3, 1, -1) == 1, "backward stays put");
	ck(menu_step_sel(dead, 0, 0, +1) == 0, "an empty menu is survivable");
}

/* A menu does not wrap (2026-11-07: "when reaching the end of a menu, don't go
 * back up automatically"): Down on the last live row and Up on the first stay
 * where they are, however long the direction is held, and the dead rows at an
 * end are not stops. Walked a press at a time, as a held key repeats. */
static void step_does_not_wrap(void)
{
	menu_row rows[6];
	int sel, i;

	for (i = 0; i < 6; i++) rows[i] = (menu_row){ "row", NULL, true };
	rows[0].live = false;           /* a dead row above the first stop */
	rows[5].live = false;           /* and one below the last */

	printf("a held direction stops at the end (menu_step_sel):\n");
	sel = menu_step_sel(rows, 6, 1, -1);
	ck(sel == 1, "up on the first row stays on it");
	for (i = 0; i < 10; i++) sel = menu_step_sel(rows, 6, sel, +1);
	ck(sel == 4, "held down stops on the last row that can be selected");
	for (i = 0; i < 10; i++) sel = menu_step_sel(rows, 6, sel, +1);
	ck(sel == 4, "and keeps stopping there, however long it is held");
	for (i = 0; i < 10; i++) sel = menu_step_sel(rows, 6, sel, -1);
	ck(sel == 1, "held up stops on the first");
	ck(menu_step_sel(rows, 6, 2, +1) == 3 && menu_step_sel(rows, 6, 3, -1) == 2,
	   "and a step in the middle still steps");
	rows[2].live = false;
	ck(menu_step_sel(rows, 6, 1, +1) == 3, "over a dead row inside the list");
	ck(menu_step_sel(rows, 1, 0, +1) == 0 && menu_step_sel(rows, 1, 0, -1) == 0,
	   "a list of one has nowhere to go");
}

/* A PRESS goes round (DESIGN.md 11.3, my pick 2026-11-12): menu_step_round
 * for a list of rows, past dead rows at either end, and menu_step_index for a
 * plain index, which goes round on a press and stops on a hold. And a list
 * longer than its window goes on round: slots, the seam, the window. */
static void step_goes_round(void)
{
	menu_row rows[6], dead[3];
	int i, s, f;

	for (i = 0; i < 6; i++) rows[i] = (menu_row){ "row", NULL, true };
	rows[0].live = false;
	rows[5].live = false;
	printf("a press goes round (menu_step_round, menu_step_index):\n");
	ck(menu_step_round(rows, 6, 4, +1) == 1, "down on the last row that can be selected goes to the first");
	ck(menu_step_round(rows, 6, 1, -1) == 4, "up on the first goes to the last");
	ck(menu_step_round(rows, 6, 2, +1) == 3 && menu_step_round(rows, 6, 3, -1) == 2,
	   "and in the middle it steps");
	for (i = 0; i < 3; i++) dead[i] = (menu_row){ "row", NULL, false };
	ck(menu_step_round(dead, 3, 1, +1) == 1, "a list with nothing to select stays put");
	ck(menu_step_round(rows, 1, 0, +1) == 0, "a list of one has nowhere to go");
	ck(menu_step_index(5, 4, +1, true) == 0 && menu_step_index(5, 0, -1, true) == 4,
	   "an index goes round on a press");
	ck(menu_step_index(5, 4, +1, false) == 4 && menu_step_index(5, 0, -1, false) == 0,
	   "and stops at the end held");
	ck(menu_step_index(2, 0, +1, true) == 1 && menu_step_index(2, 1, +1, true) == 0,
	   "the yes/cancel pair turns over");

	printf("a long list goes on round (menu_loop_*):\n");
	ck(menu_loop_row(8, 0) == 0 && menu_loop_row(8, 7) == 7 && menu_loop_row(8, 8) == -1 &&
	   menu_loop_row(8, 9) == 0 && menu_loop_row(8, -1) == -1 && menu_loop_row(8, -2) == 7,
	   "the rows, a seam, the rows again, either way");
	ck(menu_loop_slot(8, 0, 7) == 9, "down from the last row is two slots on, over the seam");
	ck(menu_loop_slot(8, 7, 0) == -2, "up from the first is two slots back");
	ck(menu_loop_slot(8, 3, 2) == 3 && menu_loop_slot(8, 3, 13) == 12,
	   "a step lands on the next slot, on whichever lap");
	for (i = 0; i < 6; i++) rows[i].live = true;
	{
		menu_row r8[8];

		for (i = 0; i < 8; i++) r8[i] = (menu_row){ "row", NULL, true };
		f = menu_loop_first(r8, 8, 7, 6, 0);
		ck(f == 2, "the window follows the cursor to the last row");
		s = menu_loop_slot(8, 0, 7);
		f = menu_loop_first(r8, 8, s, 6, f);
		ck(s == 9 && f == 4 && menu_loop_row(8, f + 4) == -1,
		   "and on past it: the seam, then the first row, in sight");
		s = menu_loop_slot(8, 7, s);
		f = menu_loop_first(r8, 8, s, 6, f);
		ck(s == 7 && f == 4, "and back over the seam with no jump");
	}
	/* A row's caption under it (body lines, Display Mode's since 1.0.6)
	 * comes into view with the cursor: ten rows, the window six, a two-line
	 * caption under row 5 */
	{
		menu_row r10[10];

		for (i = 0; i < 10; i++) r10[i] = (menu_row){ "row", NULL, true };
		r10[6] = MENU_BODY("what row 5 does,");
		r10[7] = MENU_BODY("in two lines");
		f = menu_loop_first(r10, 10, 5, 6, 0);
		ck(f == 2, "down onto a row with a caption: the window shows its two lines too");
		f = menu_loop_first(r10, 10, 4, 6, 0);
		ck(f == 0, "the row above it moves nothing");
		r10[7] = (menu_row){ "row", NULL, true };
		r10[6] = MENU_NOTE("a note, not a caption");
		f = menu_loop_first(r10, 10, 5, 6, 0);
		ck(f == 0, "a plain note under a row is no caption: the window stays");
	}
}

/* MENU's tabs (DESIGN.md 11.3): every row is in one, in the enum's order
 * inside it, the tabs in a row are the whole menu, and a tab's build is
 * headed by its name */
static void menu_tabs(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	pm_row all[PM_ROWS], tab[PM_ROWS];
	int r, t, n, k, m, at = 0, n_all;
	bool ok = true;

	printf("MENU's tabs:\n");
	for (r = 0; r < PM_ROWS; r++)
		if (sys_menu_tab_of((pm_row)r) >= PT_TABS) {
			printf("  row %d is in no tab\n", r);
			ok = false;
		}
	ck(ok, "every row is in a tab");
	for (r = 1; r < PM_ROWS; r++)
		if (sys_menu_tab_of((pm_row)r) < sys_menu_tab_of((pm_row)(r - 1))) ok = false;
	ck(ok, "the enum is in the tabs' order, so a tab's rows are the enum's in order");
	memset(&u, 0, sizeof u);
	u.update = "0.1.0";
	n_all = sys_menu_zyperos_rows(&u, all);
	for (t = 0; t < PT_TABS; t++) {
		m = sys_menu_tab_rows(&u, (pm_tab)t, tab);
		for (k = 0; k < m; k++, at++)
			if (at >= n_all || all[at] != tab[k]) ok = false;
	}
	ck(ok && at == n_all, "the tabs, one after another, are every row the device has");
	m = sys_menu_tab_rows(&u, PT_DISPLAY, tab);
	ck(m == 12 && tab[0] == PM_APPEARANCE && tab[1] == PM_THEME &&
	   tab[2] == PM_HOME && tab[3] == PM_ORDER && tab[4] == PM_HOMESYS && tab[5] == PM_NAMES &&
	   tab[6] == PM_BATTERY && tab[7] == PM_SAVER && tab[8] == PM_SHADER && tab[9] == PM_STRENGTH &&
	   tab[10] == PM_SCREEN && tab[11] == PM_NIGHT,
	   "Display: Appearance, Theme, Home Screen, System Order, Home Systems, Game Names, Battery Icon, "
	   "Screensaver, Shader, Strength, Screen Colors, Night Mode (its page has the rest)");
	u.no_strength = true;
	m = sys_menu_tab_rows(&u, PT_DISPLAY, tab);
	ck(m == 11 && tab[8] == PM_SHADER && tab[9] == PM_SCREEN,
	   "and no Strength while no system draws an old screen");
	u.no_strength = false;
	u.no_screen_colors = u.no_night = true;
	m = sys_menu_tab_rows(&u, PT_DISPLAY, tab);
	ck(m == 10 && tab[9] == PM_STRENGTH,
	   "and no Screen Colors or Night Mode where the display has no table (the Brick)");
	u.no_screen_colors = u.no_night = false;
	m = sys_menu_tab_rows(&u, PT_GAMES, tab);
	ck(m == 4 && tab[0] == PM_SEARCH, "Games opens on Search Games");
	m = sys_menu_tab_rows(&u, PT_CONNECTIONS, tab);
	ck(m > 0 && tab[0] == PM_USB_WAIT, "Connections starts on USB Device while a plug waits");
	u.no_usb_wait = true;
	m = sys_menu_tab_rows(&u, PT_CONNECTIONS, tab);
	ck(m > 0 && tab[0] == PM_WIFI, "and on Wi-Fi when none does");
	u.tab = PT_SOUND;
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(n == 6 && !strcmp(heading, "Sound") && !strcmp(rows[0].label, "Audio Output") &&
	   !strcmp(rows[2].label, "Equalizer") && !strcmp(rows[4].label, "Menu Music") &&
	   !strcmp(rows[5].label, "Rumble"),
	   "a tab's build: its rows, headed by its name");
	u.tab = PT_DISPLAY;
	u.theme = "Lime";
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(n == 12 && !strcmp(rows[1].label, "Theme") && !strcmp(val(&rows[1]), "Lime") &&
	   !strcmp(rows[10].label, "Screen Colors") && !strcmp(val(&rows[10]), "Standard") &&
	   !strcmp(rows[11].label, "Night Mode") && !strcmp(val(&rows[11]), "Off"),
	   "Theme says its accent, Screen Colors its look and Night Mode its mode "
	   "(Standard and Off when none is said)");
	u.night = "By Hours";
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(n == 12 && !strcmp(val(&rows[11]), "By Hours"), "and says what it is given");
	ck(b.sound[10] == 0 && b.sound[11] == 0,
	   "Screen Colors and Night Mode open their pages, as Theme does: no row of steps");
	/* Game Names (DESIGN.md 11.71), under Home Systems: Short when nothing
	 * is said, as it ships, and stepped round like System Order */
	ck(!strcmp(rows[5].label, "Game Names") && !strcmp(val(&rows[5]), "Short") && rows[5].live &&
	   b.sound[5] == MENU_SND_RING, "Game Names reads Short when nothing is said, and steps round");
	u.names = "File Name";
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[5]), "File Name"), "and File Name when it is given");
	u.names = NULL;
	ck(!strcmp(sys_menu_tab_name(PT_CONNECTIONS), "Connections") &&
	   !strcmp(sys_menu_tab_name(PT_ABOUT), "About"), "the tabs' names");
}

/* The shared engine is the only place a menu list goes round by arithmetic:
 * every panel with rows goes through menu_step_round and menu_step_sel, and
 * the loops of their own through menu_step_index (list_step_heard). A `% n`
 * on the cursor in main.c, or a screen split from it (MAIN_SRC in
 * mk/native.mk), on the up or down press would be a second rule, so the
 * source is read for it. */
static void source_does_not_wrap(void)
{
	static const char *const bad[] = {
		"(sel + 1) % n", "(sel + n - 1) % n", "(sel + 1) % items",
		"(sel + items - 1) % items", "(sel == 1) ? 2 : 1",
	};
	static const char *const files[] = {
		"src/main.c", "src/aout_route.c", "src/bgm_screen.c",
		"src/bt_screen.c", "src/clock_screen.c", "src/ctl_channel.c",
		"src/ctltab_screen.c", "src/dl_screen.c", "src/gamedel_screen.c",
		"src/gamern_screen.c",
		"src/media_screen.c", "src/menu_panel.c", "src/nowplaying_screen.c",
		"src/place.c", "src/portload.c", "src/powerscreens.c", "src/raqueue.c",
		"src/search_screen.c", "src/shelfbuild.c", "src/stats_screen.c",
		"src/storage_screen.c", "src/update_screen.c", "src/zyperdraw.c",
	};
	char line[512];
	int hits = 0;
	size_t i, k;

	printf("no menu loop goes round by hand:\n");
	for (k = 0; k < sizeof files / sizeof files[0]; k++) {
		FILE *f = fopen(files[k], "r");
		int ln = 0;

		ck(f != NULL, files[k]);
		while (f && fgets(line, sizeof line, f)) {
			ln++;
			for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
				if (strstr(line, bad[i])) { printf("  %s:%d: %s", files[k], ln, line); hits++; }
		}
		if (f) fclose(f);
	}
	ck(hits == 0, "no up or down press steps a menu cursor round the end but the engine");
}

/* Walking down a list and back must end up looking at the top of it again.
 *
 * The list here is the game info screen as a scraped game builds it: Achievements
 * and Year lead, and neither is a stop for the cursor. On the device on
 * 2026-09-16, one walk down to Favorite scrolled both off the top and nothing
 * afterwards brought them back - the window moved only when the CURSOR would
 * leave it, and no cursor can ever be up there.
 *
 * Walked rather than asserted at one position, because every single frame of
 * that walk was correct on its own. */
static void window_returns(void)
{
	menu_row rows[9];
	int vis = 8, first = 0, sel, i;

	for (i = 0; i < 9; i++) rows[i] = (menu_row){ "row", NULL, false };
	rows[2].live = rows[7].live = rows[8].live = true;   /* Synopsis, art, fav */

	printf("a list whose first two rows cannot be selected:\n");
	sel = menu_step_sel(rows, 9, 0, +1);
	ck(sel == 2, "opens on the first row that can be");
	first = menu_window_first(rows, 9, sel, vis, first);
	ck(first == 0, "and shows the rows above it");

	sel = menu_step_sel(rows, 9, sel, +1);
	first = menu_window_first(rows, 9, sel, vis, first);
	sel = menu_step_sel(rows, 9, sel, +1);
	first = menu_window_first(rows, 9, sel, vis, first);
	ck(sel == 8 && first == 1, "walking to the bottom scrolls the top away");

	sel = menu_step_sel(rows, 9, sel, -1);
	first = menu_window_first(rows, 9, sel, vis, first);
	ck(sel == 7 && first == 1, "one step back does not move the window");

	sel = menu_step_sel(rows, 9, sel, -1);
	first = menu_window_first(rows, 9, sel, vis, first);
	ck(sel == 2 && first == 0, "reaching the top row brings the top back");

	/* The cursor still wins when what is above it cannot fit. */
	for (i = 0; i < 9; i++) rows[i].live = false;
	rows[8].live = true;
	ck(menu_window_first(rows, 9, 8, 4, 0) == 5, "a cursor is never pushed off");
	ck(menu_window_first(rows, 9, 8, 9, 0) == 0, "a list that fits never scrolls");
}


/* What scrolls, and what is cut short.
 *
 * The rule reads "the row under the cursor, and rows the cursor can never
 * reach" - and it used to infer the second half from `live`, which is right
 * everywhere except the one list that uses `live` for something else. There
 * `live` means EARNED, so a long achievement title scrolled or was cut
 * depending on whether the player had it. Reported from the device
 * 2026-09-16. */
static void what_scrolls(void)
{
	menu_row live_row = { "a long label", "v", true };
	menu_row dead_row = { "a long label", "v", false };

	printf("which rows scroll:\n");
	ck(menu_row_moves(live_row, true, false), "the row under the cursor does");
	ck(!menu_row_moves(live_row, false, false), "one the cursor left does not");
	ck(menu_row_moves(dead_row, false, false),
	   "a row the cursor cannot reach does, having no other way to be read");

	/* A list whose cursor visits every row: `live` is about color there, so
	 * only the cursor decides. */
	ck(menu_row_moves(live_row, true, true), "with visits_all, the cursor still does");
	ck(menu_row_moves(dead_row, true, true), "and an unearned row under it does too");
	ck(!menu_row_moves(dead_row, false, true),
	   "but an unearned row elsewhere is cut like any other");
	ck(!menu_row_moves(live_row, false, true), "exactly like an earned one");
}


/* ---------- the game info screen ------------------------------------------ */

/* Six rows at most: the three that describe the game, then the three that do
 * something about it, Delete Game last (DESIGN.md 11.31). The order reversed
 * on 2026-09-17 - it used to lead with what the cursor could act on - so it
 * is asserted rather than assumed, which is also how the old order got pinned
 * and had to be unpinned here.
 *
 * What is NOT here is as deliberate as what is: no Size, no Box Art byte
 * count, and no Saves, which the carousel shows with the frames themselves. A
 * row is easy to put back by habit. File came back on 2026-11-21 for the
 * brackets every title leaves out (DESIGN.md 11.69): an entry with no file
 * of the card's has none, and the rows below are such an entry's until
 * info_file_rows. */
/* Every row of a scraped game but Rename, Buttons, Add to Collection, Core,
 * File and Discs: GI_MAX less those six */
#define INFO_FULL (GI_MAX - 6)

static void info_rows(void)
{
	game_info gi;
	menu_row rows[GI_MAX];
	int got[GI_MAX], n, k, i;

	memset(&gi, 0, sizeof gi);
	snprintf(gi.cheevos, sizeof gi.cheevos, "12 of 78");
	gi.has_cheevos = true;
	gi.has_art = true;
	gi.can_delete = true;

	n = gi_rows(rows, &gi, true);
	printf("game info, nothing scraped, art present and a network:\n");
	ck(n == 3, "Achievements, the art action and Delete Game, and nothing else to say");
	ck(!strcmp(rows[0].label, "Achievements"), "Achievements leads when nothing describes the game");
	ck(!strcmp(val(&rows[0]), "12 of 78"), "and carries the count");
	ck(rows[0].live, "and opens the set, the way the in-game row does");
	ck(!strcmp(rows[1].label, "Cover and Video"), "art present: Cover and Video");
	ck(!strcmp(val(&rows[1]), "No video"), "says what is missing: the video");
	ck(rows[1].live, "and opens its page");
	ck(!strcmp(rows[2].label, "Delete Game"), "Delete Game last");
	ck(rows[2].live && !rows[2].value, "live, with no value: the question says what goes");
	for (i = 0; i < n; i++) {
		ck(strcmp(lbl(&rows[i]), "File") != 0, "no File row for an entry with no file");
		ck(strcmp(lbl(&rows[i]), "Discs") != 0, "and no Discs row");
		ck(strcmp(lbl(&rows[i]), "Size") != 0, "no Size row");
		ck(strcmp(lbl(&rows[i]), "Box Art") != 0, "no Box Art row");
		ck(strcmp(lbl(&rows[i]), "Favorite") != 0, "no Favorite row");
		ck(strcmp(lbl(&rows[i]), "Saves") != 0, "no Saves row");
	}
	k = reachable(rows, n, got, GI_MAX);
	ck(k == 3, "three stops: the set and the two actions");

	gi.has_cheevos = false;
	snprintf(gi.cheevos, sizeof gi.cheevos, "none");
	gi.has_art = false;
	n = gi_rows(rows, &gi, false);
	printf("game info, no set, no art and no network:\n");
	ck(!rows[0].live, "a game with no set has nothing to open");
	ck(!strcmp(rows[1].label, "Cover and Video"), "no art: the same row");
	ck(!strcmp(val(&rows[1]), "No cover"), "and says the cover is what is missing");
	ck(rows[1].live, "live with no network: the page shows what is on the card");
	ck(!strcmp(rows[2].label, "Delete Game") && rows[2].live,
	   "Delete Game needs no network");
	k = reachable(rows, n, got, GI_MAX);
	ck(k == 2, "Cover and Video and Delete Game are the things to choose");
	gi.has_art = true;
	gi.has_snap = true;
	n = gi_rows(rows, &gi, true);
	ck(!strcmp(rows[1].label, "Cover and Video") && !val(&rows[1])[0],
	   "a cover and a video: nothing is missing, no value");
	gi.has_snap = false;

	/* Scraped, which adds the three rows ABOVE Achievements and nowhere else. The
	 * shapes below are the whole of what a scrape can leave behind. */
	gi.has_cheevos = true;
	snprintf(gi.cheevos, sizeof gi.cheevos, "12 of 78");
	gi.has_art = true;
	gi.scraped = true;
	gi.has_synopsis = true;
	snprintf(gi.year, sizeof gi.year, "1995");
	snprintf(gi.genre, sizeof gi.genre, "Platform,Shoot'em Up");
	n = gi_rows(rows, &gi, true);
	printf("game info, scraped with a year, a genre and prose:\n");
	ck(n == INFO_FULL - 1, "three rows more than an unscraped game");
	{
		game_info fg = gi;
		menu_row fr[GI_MAX];
		int fn, j, at = -1;

		fg.can_favorite = true;
		fg.favorite = true;
		fn = gi_rows(fr, &fg, true);
		for (j = 0; j < fn; j++) if (!strcmp(fr[j].label, "Favorite")) at = j;
		ck(fn == INFO_FULL && at >= 0 && fr[at].live && fr[at].value == MENU_KEY_Y_ON,
		   "a game that can be a favorite has the Favorite row, live, with Y's glyph and On");
	}
	ck(!strcmp(rows[0].label, "Synopsis"), "what the game IS leads");
	ck(rows[0].live, "and opens, because the prose does not fit on a row");
	ck(!strcmp(rows[1].label, "Year") && !strcmp(val(&rows[1]), "1995"),
	   "then when it came out");
	ck(!strcmp(rows[2].label, "Genre") && !strcmp(val(&rows[2]), "Platform,Shoot'em Up"),
	   "then what kind, as the scrape joined it");
	ck(!strcmp(rows[3].label, "Achievements"),
	   "then what there is to shoot for, which is worth seeing before starting");
	ck(!strcmp(rows[4].label, "Cover and Video"), "then Cover and Video");
	ck(!strcmp(rows[5].label, "Delete Game"), "and Delete Game last");
	k = reachable(rows, n, got, GI_MAX);
	ck(k == 4, "four stops: the synopsis, the set and the two actions");

	gi.can_delete = false;
	n = gi_rows(rows, &gi, true);
	printf("game info on Ports, whose games are not deleted here:\n");
	ck(n == INFO_FULL - 2, "one row fewer");
	for (i = 0; i < n; i++)
		ck(strcmp(lbl(&rows[i]), "Delete Game") != 0, "no Delete Game row");
	ck(!strcmp(rows[n - 1].label, "Cover and Video"), "Cover and Video last");
	/* PortMaster's entry has its own cover, which no fetch replaces */
	gi.own_art = true;
	n = gi_rows(rows, &gi, true);
	printf("game info on PortMaster, whose cover is its own:\n");
	ck(n == INFO_FULL - 3, "no art action either");
	for (i = 0; i < n; i++)
		ck(strcmp(lbl(&rows[i]), "Cover and Video"), "no Cover and Video row");
	gi.own_art = false;
	gi.can_delete = true;

	gi.has_synopsis = false;
	n = gi_rows(rows, &gi, true);
	printf("game info, scraped with no prose:\n");
	ck(!strcmp(val(&rows[0]), "None"), "Synopsis says none");
	ck(!rows[0].live, "and does nothing, the way the in-game Achievements row does");

	gi.year[0] = '\0';
	gi.has_synopsis = true;
	n = gi_rows(rows, &gi, true);
	printf("game info, scraped with no year:\n");
	ck(n == INFO_FULL - 2, "the Year row is simply absent");
	ck(!strcmp(rows[0].label, "Synopsis"), "and Synopsis still leads");
	ck(!strcmp(rows[1].label, "Genre"), "with the genre where the year was");

	gi.genre[0] = '\0';
	n = gi_rows(rows, &gi, true);
	printf("game info, scraped with neither year nor genre:\n");
	ck(n == INFO_FULL - 3, "both are simply absent");
	ck(!strcmp(rows[1].label, "Achievements"), "leaving the prose and the two actions");

	/* Rename, where the game can take a name (src/gamern.h): right above
	 * Delete Game, which stays last, live, with no value */
	{
		game_info rg = gi;
		menu_row rr[GI_MAX];
		int rn, j;

		rg.can_rename = true;
		rg.can_favorite = true;
		rg.can_collect = true;
		snprintf(rg.collected, sizeof rg.collected, "In 2");
		snprintf(rg.core, sizeof rg.core, "gambatte");
		rg.own_buttons = true;
		rg.scraped = true;
		rg.has_synopsis = true;
		snprintf(rg.year, sizeof rg.year, "1995");
		snprintf(rg.genre, sizeof rg.genre, "Platform");
		snprintf(rg.file, sizeof rg.file, "Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1)");
		snprintf(rg.discs, sizeof rg.discs, "3");
		rn = gi_rows(rr, &rg, true);
		printf("game info, a game that can be renamed:\n");
		ck(rn == GI_MAX, "every row at once fits GI_MAX");
		for (j = 0; j < rn && strcmp(rr[j].label, "Rename"); j++) {}
		ck(j == rn - 2 && rr[j].live && !rr[j].value, "Rename, live, just above Delete Game");
		ck(!strcmp(rr[rn - 1].label, "Delete Game"), "and Delete Game still last");
		for (j = 0; j < rn && strcmp(rr[j].label, "Favorite"); j++) {}
		ck(j + 1 < rn && !strcmp(rr[j + 1].label, "Add to Collection") && rr[j + 1].live &&
		   !strcmp(val(&rr[j + 1]), "In 2"), "Add to Collection right after Favorite, live, its count");
		for (j = 0; j < rn && strcmp(rr[j].label, "Core"); j++) {}
		ck(j < rn && j > 0 && !strcmp(rr[j - 1].label, "Genre") && !rr[j].live && !strcmp(val(&rr[j]), "gambatte"),
		   "the core, a fact after the genre, where the shelf has two");
		rg.collected[0] = '\0';
		rn = gi_rows(rr, &rg, true);
		for (j = 0; j < rn && strcmp(rr[j].label, "Add to Collection"); j++) {}
		ck(j < rn && !rr[j].value, "in none: no value");
	}
}

/* A game's file, whole, on its details (DESIGN.md 11.69): the last fact, after
 * the core and before Achievements, a fact the cursor never stops on (so a
 * long name slides rather than being cut, menu_row_moves); a set's Discs
 * under it, and no Discs for a game of one disc. The value is the file as
 * the gatherer gives it (lib_file_shown, check-titles), never cut here. */
static void info_file_rows(void)
{
	static const char LONG[] = "Final Fantasy VII (USA) (Rev 1) (Disc 1) [!] (v1.1)";
	game_info gi;
	menu_row rows[GI_MAX];
	int got[GI_MAX], n, k, j, base;

	memset(&gi, 0, sizeof gi);
	snprintf(gi.cheevos, sizeof gi.cheevos, "12 of 78");
	gi.has_cheevos = true;
	gi.has_art = true;
	gi.can_delete = true;
	gi.can_favorite = true;
	base = gi_rows(rows, &gi, true);
	k = reachable(rows, base, got, GI_MAX);

	snprintf(gi.file, sizeof gi.file, "%s", LONG);
	n = gi_rows(rows, &gi, true);
	printf("game info, a game's file:\n");
	ck(n == base + 1, "one row more: File");
	ck(!strcmp(rows[0].label, "File") && !strcmp(val(&rows[0]), LONG),
	   "File leads an unscraped game, its name whole, every bracket in it");
	ck(!rows[0].live, "a fact: the cursor never stops on it");
	ck(menu_row_moves(rows[0], false, false), "so a name too long for the row slides, never cut");
	ck(!strcmp(rows[1].label, "Achievements"), "Achievements after it");
	for (j = 0; j < n; j++) ck(strcmp(lbl(&rows[j]), "Discs") != 0, "one disc: no Discs row");
	ck(reachable(rows, n, got, GI_MAX) == k, "and nothing more to stop on");

	snprintf(gi.discs, sizeof gi.discs, "3");
	n = gi_rows(rows, &gi, true);
	printf("game info, a set of three discs:\n");
	ck(n == base + 2, "two rows more: File and Discs");
	ck(!strcmp(rows[1].label, "Discs") && !strcmp(val(&rows[1]), "3") && !rows[1].live,
	   "Discs right under File, how many, a fact");
	ck(reachable(rows, n, got, GI_MAX) == k, "still nothing more to stop on");

	gi.scraped = true;
	gi.has_synopsis = true;
	snprintf(gi.year, sizeof gi.year, "1997");
	snprintf(gi.genre, sizeof gi.genre, "Role-playing");
	snprintf(gi.core, sizeof gi.core, "gambatte");
	n = gi_rows(rows, &gi, true);
	printf("game info, scraped, on a shelf of two cores:\n");
	for (j = 0; j < n && strcmp(rows[j].label, "File"); j++) {}
	ck(j < n && j > 0 && !strcmp(rows[j - 1].label, "Core"), "File after the core");
	ck(j + 2 < n && !strcmp(rows[j + 1].label, "Discs") && !strcmp(rows[j + 2].label, "Achievements"),
	   "then Discs, then Achievements: the last of the facts");
	ck(!strcmp(rows[0].label, "Synopsis") && !strcmp(rows[1].label, "Year") &&
	   !strcmp(rows[2].label, "Genre"), "the scrape's facts still lead");

	/* An entry that is no file of the card's (Install PortMaster, Splore):
	 * no File, and so no Discs, whatever else is set */
	gi.file[0] = '\0';
	n = gi_rows(rows, &gi, true);
	for (j = 0; j < n; j++)
		ck(strcmp(lbl(&rows[j]), "File") && strcmp(lbl(&rows[j]), "Discs"),
		   "no file: neither File nor Discs");
}

/* A game with a key of its own (rms_game) shows its buttons on its details:
 * Buttons after Favorite, valued This Game, and no other game has the row. */
static void gi_buttons_row_only_when_own(void)
{
	game_info gi;
	menu_row rows[GI_MAX];
	int n, i, at = -1, fav = -1, base;

	memset(&gi, 0, sizeof gi);
	snprintf(gi.cheevos, sizeof gi.cheevos, "none");
	gi.can_favorite = true;
	gi.can_delete = true;
	gi.has_art = true;
	n = gi_rows(rows, &gi, true);
	base = n;
	printf("game info, a game on the system's buttons:\n");
	for (i = 0; i < n; i++)
		ck(strcmp(lbl(&rows[i]), "Buttons") != 0, "no Buttons row");

	gi.own_buttons = true;
	n = gi_rows(rows, &gi, true);
	printf("game info, a game with its own buttons:\n");
	ck(n == base + 1, "one row more");
	for (i = 0; i < n; i++) {
		if (!strcmp(lbl(&rows[i]), "Buttons")) at = i;
		if (!strcmp(lbl(&rows[i]), "Favorite")) fav = i;
	}
	ck(at >= 0 && fav >= 0 && at == fav + 1, "Buttons right after Favorite");
	ck(at >= 0 && !strcmp(val(&rows[at]), "This Game"), "valued This Game");
	ck(at >= 0 && rows[at].live, "and live: A opens it");
	ck(!strcmp(rows[n - 1].label, "Delete Game"), "Delete Game still last");
}


/* ---------- the in-game menu ---------------------------------------------- */

/* The in-game menu is three tabs (DESIGN.md 11.45): Game, Display and Controls, MENU's tab row. gm_tab_rows says
 * which rows a tab has, gm_rows builds them, and a port's menu has no tabs.
 * Achievements is the only row that can be dead, and it is dead exactly when
 * the game has no set. Everything else is always something A does. */
static int labels_are(const menu_row *rows, int n, const char *const *want, int k)
{
	int i;

	if (n != k) return 0;
	for (i = 0; i < k; i++)
		if (strcmp(lbl(&rows[i]), want[i])) return 0;
	return 1;
}

static int row_of(const gm_ui *u, gm_tab t, gm_row id)
{
	return gm_row_at(u, t, id);
}

static void ingame_rows(void)
{
	static const char *const game[] = { "Continue", "Save", "Load", "Quick Slot", "Player 2",
	                                    "Achievements", "Reset", "Quit" };
	static const char *const dsp[] = { "Display", "Shader", "Screens", "Swap Screens", "Stylus",
	                                   "On-Screen Stats" };
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	gm_row ids[GM_MAX_ROWS];
	int got[GM_MAX_ROWS], n, k, t;

	u.dmode = "Native"; u.earned = 12; u.total = 40; u.quick = 2;

	printf("in-game menu, the tabs:\n");
	ck(GT_TABS == 4 && GT_GAME == 0 && GT_DISPLAY == 1 && GT_CONTROLS == 2 &&
	   GT_ACHIEVEMENTS == 3, "four tabs, Game first, Achievements last (L1 from Game)");
	ck(!strcmp(gm_tab_name(GT_GAME), "Game") && !strcmp(gm_tab_name(GT_DISPLAY), "Display") &&
	   !strcmp(gm_tab_name(GT_CONTROLS), "Controls") &&
	   !strcmp(gm_tab_name(GT_ACHIEVEMENTS), "Achievements"),
	   "named Game, Display, Controls and Achievements");
	ck(gm_tab_rows(&u, GT_ACHIEVEMENTS, ids) == 0,
	   "the Achievements tab's rows are src/achtab.c's, none from here");
	ck(!strcmp(gm_tab_name(-1), "Game") && !strcmp(gm_tab_name(GT_TABS), "Game"),
	   "a tab past the ends reads as the first");

	/* An NES game: its Player 2, and no screens, shader or display on this tab */
	u.player2 = true; u.player2_value = gm_player2_value(P2_OFF);
	n = gm_rows(&u, GT_GAME, rows, &b);
	printf("in-game menu, Game tab:\n");
	ck(labels_are(rows, n, game, 8), "gm_tabs_game_rows: Continue Save Load Quick Slot Player 2 Achievements Reset Quit");
	ck(gm_tab_rows(&u, GT_GAME, ids) == 8 && ids[0] == GM_CONTINUE && ids[4] == GM_PLAYER2 &&
	   ids[5] == GM_CHEEVOS && ids[7] == GM_QUIT, "and the ids say the same");
	ck(!strcmp(val(&rows[3]), "2") && rows[3].live && !strcmp(val(&rows[5]), "12 / 40"),
	   "Quick Slot says its slot, Achievements counts the set");
	ck(!strcmp(val(&rows[4]), "Off") && rows[4].live && !b.sound[4], "Player 2 says off, a plain stop");
	ck(reachable(rows, n, got, GM_MAX_ROWS) == 8, "every row is a stop");
	/* X from anywhere is Quit's, and Quit alone carries X's glyph */
	ck(gm_x_row() == GM_QUIT && rows[7].value == MENU_KEY_X, "X is Quit's, and Quit draws X's glyph");
	for (k = 0; k < n; k++)
		if (k != 7 && rows[k].value == MENU_KEY_X) break;
	ck(k == n, "and no other row does");
	ck(row_of(&u, GT_GAME, GM_QUIT) == 7 && row_of(&u, GT_GAME, GM_SHADER) == -1,
	   "gm_row_at is a place on that tab, -1 for a row it has not");
	u.player2 = false;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 7 && !strcmp(rows[4].label, "Achievements"), "a game with no Player 2 has seven");
	u.quick = 0;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 6 && row_of(&u, GT_GAME, GM_QUICK) == -1 && !strcmp(rows[2].label, "Load") &&
	   !strcmp(rows[3].label, "Achievements"), "no save states: no Quick Slot");
	ck(ctl_has_states("SFC") && ctl_has_states("PS1") && ctl_has_states("N64") &&
	   !ctl_has_states("SCUMMVM") && !ctl_has_states("J2ME"),
	   "every Diatom system keeps states but ScummVM and J2ME");
	u.quick = 2;

	/* Achievements says none when the game has no set, and is still a stop:
	 * A on it turns to the Achievements tab, which says why (1.0.5) */
	u.earned = 0; u.total = 0;
	n = gm_rows(&u, GT_GAME, rows, &b);
	printf("in-game menu, a game with no set:\n");
	ck(!strcmp(val(&rows[gm_row_at(&u, GT_GAME, GM_CHEEVOS)]), "None"), "Achievements says none");
	ck(rows[gm_row_at(&u, GT_GAME, GM_CHEEVOS)].live, "and is a stop, A turning to its tab");
	k = reachable(rows, n, got, GM_MAX_ROWS);
	ck(k == n && holds(got, k, gm_row_at(&u, GT_GAME, GM_CHEEVOS)),
	   "so the cursor rests on it");
	u.earned = 12; u.total = 40;

	/* Display: a DS game adds the screens' three, in this order */
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	printf("in-game menu, Display tab:\n");
	ck(n == 3 && !strcmp(rows[0].label, "Display") && !strcmp(rows[1].label, "Shader") &&
	   !strcmp(rows[2].label, "On-Screen Stats"),
	   "a game that is not a DS one: Display, Shader and On-Screen Stats");
	/* On-Screen Stats, the tab's last row (src/hudmenu.h): On or Off, left
	 * and right turn it over, A opens its page */
	ck(!strcmp(val(&rows[2]), "Off") && rows[2].live && b.sound[2] == MENU_SND_RING &&
	   gm_row_at(&u, GT_DISPLAY, GM_HUD) == 2 && gm_row_at(&u, GT_GAME, GM_HUD) < 0,
	   "On-Screen Stats says Off, steps round, and is on the Display tab only");
	u.hud = true;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(!strcmp(val(&rows[2]), "On"), "and On when it is on");
	u.hud = false;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(!strcmp(val(&rows[0]), "Native") && b.sound[0] == MENU_SND_RING && b.sound[1] == MENU_SND_RING,
	   "Display carries the mode, and both step round");
	u.shader = "Sharp-Shimmerless";
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(!strcmp(val(&rows[1]), "Sharp-Shimmerless"), "Shader says the shader it is given");
	u.shader = NULL;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(!strcmp(val(&rows[1]), "Off") && rows[1].live, "and Off when none is, a stop");
	/* Strength right under Shader while the shader is an old screen */
	u.shader = "LCD Grid"; u.strength = "50%";
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 4 && !strcmp(rows[2].label, "Strength") && !strcmp(val(&rows[2]), "50%") &&
	   rows[2].live && gm_row_at(&u, GT_DISPLAY, GM_STRENGTH) == 2,
	   "an old screen: Strength under Shader, saying its percent");
	ck(b.sound[2] == MENU_SND_NO_A, "left and right step it, A does nothing");
	ck(gm_row_at(&u, GT_GAME, GM_STRENGTH) < 0, "and it is on no other tab");
	u.screens = true;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 7 && !strcmp(rows[2].label, "Strength") && !strcmp(rows[3].label, "Screens") &&
	   !strcmp(rows[6].label, "On-Screen Stats"),
	   "a DS game's three come after it, On-Screen Stats last");
	u.screens = false; u.shader = NULL; u.strength = NULL;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	u.screens = true; u.layout = 1; u.swap = true; u.stylus = false;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(labels_are(rows, n, dsp, 6),
	   "gm_tabs_display_ds: Display Shader Screens Swap Screens Stylus On-Screen Stats");
	ck(gm_tab_rows(&u, GT_DISPLAY, ids) == 6 && ids[2] == GM_SCREENS && ids[3] == GM_SWAP &&
	   ids[4] == GM_STYLUS && ids[5] == GM_HUD, "and the ids");
	ck(!strcmp(val(&rows[2]), "Top Focus") && !strcmp(val(&rows[3]), "On") && !strcmp(val(&rows[4]), "Off"),
	   "Screens says the layout, Swap Screens and Stylus say On or Off");
	ck(reachable(rows, n, got, GM_MAX_ROWS) == 6, "every row is a stop");
	ck(gm_layout_index("single") == 2 && gm_layout_index("nonsense") == 0,
	   "a layout name Diatom did not send reads as Stacked");
	ck(!strcmp(gm_layout_names[GM_LAYOUT_DEFAULT], "single"),
	   "a system with no layout on record starts on one screen alone, the top one");
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 7, "and the DS game's Game tab is the same as any other's");
	u.screens = false;

	/* No GM_CONTROLS row in any tab of a game's menu, and no Pak: the
	 * Controls tab's rows are src/ctltab.c's, from task 7 */
	u.pak = gm_pak_name("rumble");
	u.player2 = true; u.screens = true;
	for (t = 0; t < GT_TABS; t++) {
		n = gm_tab_rows(&u, (gm_tab)t, ids);
		for (k = 0; k < n; k++)
			if (ids[k] == GM_CONTROLS || ids[k] == GM_PAK) break;
		if (k < n) break;
	}
	ck(t == GT_TABS, "gm_no_controls_row: no Controls or Pak row in any tab of a game");
	ck(gm_tab_rows(&u, GT_CONTROLS, ids) == 0 && gm_rows(&u, GT_CONTROLS, rows, &b) == 0,
	   "the Controls tab has no rows of this file's until task 7");
	u.pak = NULL; u.player2 = false; u.screens = false;

	/* Over a program of its own (OpenBOR, a port, PortMaster,
	 * src/portmenu.h): a game's tabs and places, with what a stopped
	 * program can still be asked (1.1.0-rc.8: "openbor is also missing the
	 * menu as we have in other systems", "yeah have the same menu in ports
	 * as well"), every one a stop */
	u.port = true;
	printf("in-game menu over a program of its own (OpenBOR, a port):\n");
	ck(gm_tab_count(&u) == 3 && gm_tab_count(&u) == GT_ACHIEVEMENTS && !gm_ctltab(&u),
	   "a game's tabs, Game, Display and Controls, no Achievements tab, and no buttons to change");
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 2 && gm_tab_rows(&u, GT_GAME, ids) == 2 && ids[0] == GM_CONTINUE && ids[1] == GM_QUIT,
	   "the Game tab: Continue and Quit");
	ck(!strcmp(rows[0].label, "Continue") && !strcmp(rows[1].label, "Quit"), "labeled as over a game");
	ck(rows[1].value == MENU_KEY_X && gm_row_at(&u, GT_GAME, gm_x_row()) == 1,
	   "X is its Quit too, with the glyph");
	ck(reachable(rows, n, got, GM_MAX_ROWS) == 2, "every row a stop");
	ck(gm_tab_rows(&u, GT_DISPLAY, ids) == 0, "the Display tab with no table: nothing of the program's picture");
	u.screen_colors = "Standard";
	u.shader = "CRT"; u.strength = "70%"; u.border = "Blur";
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 1 && gm_tab_rows(&u, GT_DISPLAY, ids) == 1 && ids[0] == GM_SCREEN_COLORS &&
	   !strcmp(rows[0].label, "Screen Colors") && rows[0].live,
	   "with a table: Screen Colors alone, the panel's; no Display, Border, Shader, Strength or "
	   "On-Screen Stats, its picture being its own");
	u.screen_colors = NULL;
	u.shader = NULL; u.strength = NULL; u.border = NULL;
	n = gm_rows(&u, GT_CONTROLS, rows, &b);
	ck(n == 1 && gm_tab_rows(&u, GT_CONTROLS, ids) == 1 && ids[0] == GM_CONTROLS &&
	   !strcmp(rows[0].label, "All Controls") && rows[0].live && !b.sound[0],
	   "the Controls tab: All Controls, a plain stop, its read-only pages");
	for (t = 0; t < GT_TABS; t++) {
		static const gm_row none[] = { GM_SAVE, GM_LOAD, GM_UNDO, GM_QUICK, GM_DISC, GM_PLAYER2,
		                               GM_CHEATS, GM_CHEEVOS, GM_RESET, GM_PAK, GM_DISPLAY,
		                               GM_SHADER, GM_HUD };
		size_t z;

		for (z = 0; z < sizeof none / sizeof none[0]; z++)
			if (gm_row_at(&u, (gm_tab)t, none[z]) >= 0) break;
		if (z < sizeof none / sizeof none[0]) break;
	}
	ck(t == GT_TABS, "nothing it cannot do on any tab: hidden, not greyed");
	/* Over your PICO-8 (src/portmenu.h's port_menu_display): a game's tabs
	 * and places, with what a stopped PICO-8 can answer to (1.1.0-rc.8:
	 * "we should use the same component as in other systems"). Display,
	 * its value the mode, A its list and Left and Right nothing; then the
	 * Display tab's picture rows its shim draws (src/p8fx.h): Shader
	 * always, Border where the mode leaves room, Strength under an old
	 * screen; Screen Colors where the display has a table; On-Screen Stats
	 * where portkeys draws it */
	printf("in-game menu over your PICO-8:\n");
	u.port_display = true;
	u.port_buttons = true;
	u.dmode = "Actual Size";
	ck(gm_ctltab(&u) && gm_tab_count(&u) == 3 && gm_tab_count(&u) == GT_ACHIEVEMENTS,
	   "a game's tabs, Game, Display and Controls, its buttons a game's Controls tab, "
	   "and no Achievements tab (PICO-8 earns none)");
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 2 && gm_tab_rows(&u, GT_GAME, ids) == 2 && ids[0] == GM_CONTINUE && ids[1] == GM_QUIT,
	   "the Game tab before it has drawn: Continue and Quit");
	ck(!strcmp(rows[0].label, "Continue") && !strcmp(rows[1].label, "Quit") && rows[1].value == MENU_KEY_X,
	   "labeled as over a game, Quit with X's glyph");
	for (t = 0; t < GT_TABS; t++) {
		static const gm_row none[] = { GM_SAVE, GM_LOAD, GM_UNDO, GM_QUICK, GM_DISC, GM_PLAYER2,
		                               GM_CHEATS, GM_CHEEVOS, GM_CONTROLS, GM_PAK, GM_SCREENS,
		                               GM_SWAP, GM_STYLUS };
		size_t z;

		for (z = 0; z < sizeof none / sizeof none[0]; z++)
			if (gm_row_at(&u, (gm_tab)t, none[z]) >= 0) break;
		if (z < sizeof none / sizeof none[0]) break;
	}
	ck(t == GT_TABS, "no Save, Load, Undo Load, Quick Slot, Disc, Player 2, Cheats, Achievements, "
	   "Controls row, Pak or DS row on any tab: hidden, not greyed");
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 2 && gm_tab_rows(&u, GT_DISPLAY, ids) == 2 && ids[0] == GM_DISPLAY && ids[1] == GM_SHADER,
	   "the Display tab: Display and Shader");
	ck(!strcmp(rows[0].label, "Display") && rows[0].value && !strcmp(rows[0].value, "Actual Size"),
	   "Display, its value the mode it draws");
	ck(!(b.sound[0] & MENU_SND_RING), "Left and Right step nothing on it (no ring)");
	ck(!strcmp(rows[1].label, "Shader") && rows[1].value && !strcmp(rows[1].value, "Off") &&
	   (b.sound[1] & MENU_SND_RING), "Shader, Off, stepping round as over a game");
	ck(reachable(rows, n, got, GM_MAX_ROWS) == 2, "every row a stop");
	ck(gm_tab_rows(&u, GT_CONTROLS, ids) == 0 && gm_tab_rows(&u, GT_ACHIEVEMENTS, ids) == 0,
	   "the Controls tab's rows are src/ctltab.c's, as a game's");
	/* And On-Screen Stats, the Display tab's last row, as over a game: its
	 * words, On or Off, Left and Right turning it */
	u.port_hud = true;
	u.hud = true;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 3 && gm_tab_rows(&u, GT_DISPLAY, ids) == 3 && ids[2] == GM_HUD,
	   "Display, Shader and On-Screen Stats");
	ck(!strcmp(rows[2].label, "On-Screen Stats") && rows[2].value && !strcmp(rows[2].value, "On") &&
	   (b.sound[2] & MENU_SND_RING), "On-Screen Stats, On, Left and Right turning it, as over a game");
	u.hud = false;
	gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(rows[2].value && !strcmp(rows[2].value, "Off"), "Off when it is off");
	/* With room round the picture and an old screen: Border under Display
	 * and Strength under Shader, as in a game's Display tab; Screen Colors
	 * right over On-Screen Stats */
	u.border = "Blur";
	u.shader = "CRT";
	u.strength = "70%";
	u.screen_colors = "Vivid";
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 6 && gm_tab_rows(&u, GT_DISPLAY, ids) == 6 && ids[0] == GM_DISPLAY && ids[1] == GM_BORDER &&
	   ids[2] == GM_SHADER && ids[3] == GM_STRENGTH && ids[4] == GM_SCREEN_COLORS && ids[5] == GM_HUD,
	   "with room, an old screen and a table: Display, Border, Shader, Strength, Screen Colors, "
	   "On-Screen Stats, a game's order");
	ck(!strcmp(rows[1].value, "Blur") && !strcmp(rows[2].value, "CRT") && !strcmp(rows[3].value, "70%") &&
	   !strcmp(rows[4].value, "Vivid"), "each its value");
	ck(b.sound[4] == 0 && !b.choice[4].n && rows[4].live,
	   "Screen Colors a plain stop, A its page, as over a game");
	{
		gm_ui g = u;
		gm_row gi[GM_MAX_ROWS];
		int gn, k2;

		/* The same rows in the same places as a game's Display tab */
		g.port = g.port_display = g.port_hud = g.port_buttons = false;
		gn = gm_tab_rows(&g, GT_DISPLAY, gi);
		n = gm_tab_rows(&u, GT_DISPLAY, ids);
		for (k2 = 0; k2 < n && k2 < gn && ids[k2] == gi[k2]; k2++) ;
		ck(gn == n && k2 == n, "the very rows of a game's Display tab, in its order");
	}
	ck(reachable(rows, n, got, GM_MAX_ROWS) == 6, "every row a stop");
	u.port_hud = false;
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	ck(n == 5 && gm_row_at(&u, GT_DISPLAY, GM_HUD) < 0, "with no On-Screen Stats: none");
	/* ...and Reset before Quit on the Game tab, once it has drawn
	 * (port_resets), asked first, as a game's Game tab has it */
	u.port_hud = u.port_reset = true;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 3 && gm_tab_rows(&u, GT_GAME, ids) == 3 && ids[0] == GM_CONTINUE && ids[1] == GM_RESET &&
	   ids[2] == GM_QUIT, "the Game tab once it has drawn: Continue, Reset and Quit");
	ck(!strcmp(rows[1].label, "Reset") && rows[1].live, "Reset, as a game's Game tab says it");
	ck(gm_row_at(&u, GT_GAME, gm_x_row()) == 2 && reachable(rows, n, got, GM_MAX_ROWS) == 3,
	   "X still Quit, every row a stop, Reset too");
	/* Still starting: Continue says Keep Waiting, and no Reset */
	u.waiting = true;
	u.port_reset = false;
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(n == 2 && !strcmp(rows[0].label, "Keep Waiting") && !strcmp(rows[1].label, "Quit"),
	   "still starting: Keep Waiting and Quit");
	u.waiting = false;
	u.port_hud = u.port_reset = false;
	u.border = NULL;
	u.shader = NULL;
	u.strength = NULL;
	u.screen_colors = NULL;
	u.port_display = false;
	u.port_buttons = false;
	ck(gm_tab_rows(&u, GT_GAME, ids) == 2 && gm_tab_rows(&u, GT_DISPLAY, ids) == 0,
	   "and no Display, Border, Shader or Reset row over any other program");
	u.port = false;
}

/* The N64's Pak as the row steps it and the library keeps it */
static void n64_pak(void)
{
	char key[96];

	printf("the N64's Pak:\n");
	ck(!strcmp(gm_pak_value(NULL), "rumble") && !strcmp(gm_pak_value("nonsense"), "rumble"),
	   "the Rumble Pak until a game takes the other, and for a value it does not know");
	ck(!strcmp(gm_pak_value("memory"), "memory"), "the Controller Pak is both cores' memory");
	ck(!strcmp(gm_pak_other("rumble"), "memory") && !strcmp(gm_pak_other("memory"), "rumble"),
	   "the row turns between the two");
	ck(!strcmp(gm_pak_name("rumble"), "Rumble Pak") &&
	   !strcmp(gm_pak_name("memory"), "Controller Pak"), "each by its product's name");
	gm_pak_key("N64", "Mario Kart 64 (USA).z64", key, sizeof key);
	ck(!strcmp(key, "pak.N64.Mario Kart 64 (USA).z64"), "kept per game, as pak.<TAG>.<file>");
	ck(!strcmp(gm_pak_option("parallel_n64"), "parallel-n64-pak1"), "ParallelN64's player 1 pak option");
	ck(!strcmp(gm_pak_option("mupen64plus_next"), "mupen64plus-pak1"), "Mupen64Plus-Next's");
	ck(!gm_pak_option("flycast2021") && !gm_pak_option(NULL), "no pak on any other core");
}

/* ---- the Controls tab (src/ctltab.h) ------------------------------------------
 *
 * Models as src/ctltab_screen.c gathers them, the tables written by hand from
 * config/systems.cfg's map| lines: the PlayStation has none, so its table is
 * the identity; the N64's and the Dreamcast's are theirs. */
#define CT_N64_MAP "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y,select+x:x"
#define CT_DC_MAP  "a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1"

static void ct_ps1(ct_model *m)
{
	memset(m, 0, sizeof *m);
	m->tag = "PS1"; m->ext = "chd"; m->sys_label = "PlayStation"; m->sys_name = "PlayStation";
	m->map = "";
	rm_identity(&m->def);
	m->cur = m->base = m->def;
	m->stick_offered = true;
}

static void ct_table(rm_table *t, const char *spec)
{
	rm_identity(t);
	rm_parse(spec, t);
}

/* Whether the handheld's pose for `keys` is `yaw` and `pitch` off the front's */
static bool lean_is(unsigned keys, float yaw, float pitch)
{
	float y = -99.0f, p = -99.0f;

	ct_lean(&td_model_pixel2, keys, &y, &p);
	return y == yaw && p == pitch;
}

/* ...on the Brick, whose shoulders are on its back ledge */
static bool brick_lean_is(unsigned keys, float yaw, float pitch)
{
	float y = -99.0f, p = -99.0f;

	ct_lean(&td_model_brick, keys, &y, &p);
	return y == yaw && p == pitch;
}

/* Where row `id` is, -1 for nowhere */
static int ct_at(const ct_row *ids, int n, ct_row id)
{
	int i;

	for (i = 0; i < n; i++) if (ids[i] == id) return i;
	return -1;
}

static void ctl_tab(void)
{
	static const char *const ps1[] = { "Applies To", "D-pad as Stick", "A", "B", "X", "Y", "L1",
	                                   "R1", "L2", "R2", "START", "SELECT", "Reset Buttons", "Hotkeys" };
	static const char *const n64[] = { "Applies To", "D-pad as Stick", "Pak", "A", "B", "X", "Y", "L1",
	                                   "R1", "L2", "R2", "START", "SELECT", "Reset Buttons", "Hotkeys" };
	ct_model m;
	ct_bufs b;
	menu_row rows[CT_MAX_ROWS];
	ct_row ids[CT_MAX_ROWS];
	unsigned vcol[CT_MAX_ROWS];
	rm_choice ch[32];
	int n, k, i, nch;

	printf("the Controls tab:\n");
	/* ct_rows_ps1_default */
	ct_ps1(&m);
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(labels_are(rows, n, ps1, 14), "ct_rows_ps1_default: Applies To, D-pad as Stick, A to SELECT, Reset Buttons, Hotkeys");
	ck(ids[0] == CT_SCOPE && ids[1] == CT_STICK && ids[2] == CT_BUTTON + RM_A &&
	   ids[11] == CT_BUTTON + RM_SELECT && ids[12] == CT_RESET && ids[13] == CT_HOTKEYS,
	   "and the ids say the same");
	ck(!strcmp(val(&rows[0]), "This Game") && rows[0].live && !vcol[0], "Applies To: This Game, plain");
	ck(!strcmp(val(&rows[1]), "Off") && rows[1].live, "D-pad as Stick: Off");
	ck(!strcmp(val(&rows[2]), PAD_CIRCLE) && !strcmp(val(&rows[3]), PAD_CROSS) &&
	   !strcmp(val(&rows[4]), PAD_TRIANGLE) && !strcmp(val(&rows[5]), PAD_SQUARE),
	   "the faces by the core's own names, the PlayStation's shapes");
	for (k = 2; k <= 11 && rows[k].live && !vcol[k]; k++) ;
	ck(k == 12, "every button live and plain");
	ck(!rows[12].live && !rows[12].value, "Reset Buttons is dead: nothing to put back");
	ck(rows[13].live && !rows[13].value, "Hotkeys opens the pages");
	ck(ct_lit(&m, CT_SCOPE) == 0 && ct_lit(&m, CT_HOTKEYS) == 0, "Applies To and Hotkeys light nothing");
	ck(ct_lit(&m, CT_BUTTON + RM_R1) == 1u << TD_R1 && ct_lit(&m, CT_BUTTON + RM_SELECT) == 1u << TD_SELECT,
	   "a button row lights its button");
	ck(ct_lit(&m, CT_STICK) == 1u << TD_DPAD, "D-pad as Stick lights the d-pad, no SELECT on the PlayStation");
	/* All PlayStation, and a stick off its default */
	m.scope_all = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(!strcmp(val(&rows[0]), "PlayStation") && ct_at(ids, n, CT_OWN_NOTE) < 0,
	   "every game of the system: PlayStation, by its name, and no note with no own buttons");
	m.scope_all = false;
	m.stick_on = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(!strcmp(val(&rows[1]), "On") && vcol[1] && rows[n - 2].live,
	   "the stick off its default: accent-ink, and This Game's Reset puts it back");

	/* ct_rows_n64 */
	memset(&m, 0, sizeof m);
	m.tag = "N64"; m.ext = "z64"; m.sys_label = "N64"; m.map = CT_N64_MAP;
	ct_table(&m.def, CT_N64_MAP);
	m.cur = m.base = m.def;
	m.stick_offered = m.stick_on = m.stick_default = true;
	m.pak = "Rumble Pak";
	m.select_held_c = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(labels_are(rows, n, n64, 15), "ct_rows_n64: the Pak after D-pad as Stick, SELECT held for the C buttons");
	ck(ids[2] == CT_PAK && !strcmp(val(&rows[2]), "Rumble Pak") && rows[2].live, "Pak says what is in");
	ck(!strcmp(val(&rows[1]), "On") && !vcol[1], "the stick On, its default");
	ck(!rows[12].live && !strcmp(val(&rows[12]), "C buttons"), "SELECT is dead: C buttons");
	ck(ct_select_held_c(&m.def), "the N64's SELECT layer (select+a:a) holds SELECT for the C buttons");
	ck(ct_lit(&m, CT_STICK) == (1u << TD_DPAD | 1u << TD_SELECT), "the stick lights SELECT, the way to the D-pad");
	ck(ct_lit(&m, CT_PAK) == 0, "Pak lights nothing");

	/* ct_rows_n64_rumble: a core that asked for the motor, on a handheld
	 * with one (src/rumble.h) */
	m.rumble = "Low";
	m.rumble_own = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(n == 16 && ids[3] == CT_RUMBLE && !strcmp(rows[3].label, "Rumble") && !strcmp(val(&rows[3]), "Low") &&
	   rows[3].live && vcol[3], "ct_rows_n64_rumble: Rumble under the Pak, its own level in accent-ink");
	ck(!strcmp(rows[4].label, "A") && ct_button_of(ids[4]) >= 0, "and the buttons after it");
	ck(ct_lit(&m, CT_RUMBLE) == 0, "Rumble lights nothing");
	m.rumble = "Full";
	m.rumble_own = false;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(!strcmp(val(&rows[3]), "Full") && !vcol[3], "MENU's level, the scope's none: plain");
	m.rumble = NULL;

	/* ct_rows_dc_face_stick_on */
	memset(&m, 0, sizeof m);
	m.tag = "DC"; m.ext = "chd"; m.sys_label = "Dreamcast"; m.map = CT_DC_MAP;
	ct_table(&m.def, CT_DC_MAP);
	m.cur = m.base = m.zdef = m.def;
	m.stick_offered = m.stick_on = true;
	m.face_offered = ctl_letter_layout("DC", &m.letters);
	m.select_held_dpad = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_FACE);
	ck(m.face_offered && k == 2 && !strcmp(val(&rows[k]), "By Letter") && rows[k].live,
	   "ct_rows_dc_face_stick_on: Face Buttons after the stick, By Letter as ZyperOS has it");
	k = ct_at(ids, n, CT_BUTTON + RM_SELECT);
	ck(k > 0 && !rows[k].live && !strcmp(val(&rows[k]), "Held: D-pad"), "SELECT is dead: Held: D-pad");
	ck(vcol[1], "the stick On off its default (Off): accent-ink");
	ck(ct_lit(&m, CT_STICK) == (1u << TD_DPAD | 1u << TD_SELECT), "the stick lights SELECT too");
	ck(ct_lit(&m, CT_FACE) == (1u << TD_A | 1u << TD_B | 1u << TD_X | 1u << TD_Y), "Face Buttons the four faces");
	ck(ct_faces(&m, &m.cur) == 0, "the default reads By Letter");
	ct_faces_set(&m, &m.cur, 1);
	ck(ct_faces(&m, &m.cur) == 1 && m.cur.b[RM_A].to == RT_A && m.cur.b[RM_L1].to == RT_L2,
	   "By Place writes the faces alone: the shoulders stay the Dreamcast's");
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_FACE);
	ck(!strcmp(val(&rows[k]), "By Place") && vcol[k], "By Place, off the default: accent-ink");
	m.cur.b[RM_A] = (rm_choice){ RT_X, 0 };
	ck(ct_faces(&m, &m.cur) == 2 && ct_faces_next(2) == 0, "one face changed is Mixed, and a step from it By Letter");
	m.stick_on = false; m.select_held_dpad = false;
	ck(ct_lit(&m, CT_STICK) == 1u << TD_DPAD, "the stick off: the d-pad alone");

	/* ct_scope_accent_when_own */
	ct_ps1(&m);
	m.game_has_own = true;
	ct_table(&m.cur, "b:y,y:b");
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(vcol[0] && !strcmp(val(&rows[0]), "This Game"), "ct_scope_accent_when_own: Applies To in accent-ink");
	k = ct_at(ids, n, CT_BUTTON + RM_B);
	ck(vcol[k] && !strcmp(val(&rows[k]), PAD_SQUARE) && vcol[k + 2] && !vcol[k - 1],
	   "the changed buttons in accent-ink, the rest plain");
	ck(rows[ct_at(ids, n, CT_RESET)].live, "and Reset Buttons is live");
	m.game_has_own = false;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(!vcol[0], "no own buttons: Applies To plain");

	/* ct_own_note_when_all */
	ct_ps1(&m);
	m.scope_all = m.game_has_own = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(ids[1] == CT_OWN_NOTE && !rows[1].live && !strcmp(lbl(&rows[1]), "This game keeps its own buttons."),
	   "ct_own_note_when_all: the note right under Applies To");
	m.scope_all = false;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(ct_at(ids, n, CT_OWN_NOTE) < 0, "not in This Game");

	/* ct_warn_under_cause */
	ct_ps1(&m);
	m.cur.b[RM_B] = (rm_choice){ RM_NONE, 0 };
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_BUTTON + RM_B);
	ck(k > 0 && ids[k + 1] == CT_WARN + RT_B && !rows[k + 1].live &&
	   !strcmp(lbl(&rows[k + 1]), PAD_CROSS " has no button now"),
	   "ct_warn_under_cause: \"× has no button now\" right under B");
	ck(!strcmp(val(&rows[k]), "Nothing") && vcol[k], "B says Nothing, in accent-ink");
	m.cur.b[RM_X] = (rm_choice){ RT_B, RM_TURBO };
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(ct_at(ids, n, CT_WARN + RT_B) < 0 && ct_at(ids, n, CT_WARN + RT_X) == ct_at(ids, n, CT_BUTTON + RM_X) + 1,
	   "a turbo of it is a button; triangle is the one with none now, under X");

	/* ct_warn_cause: Y given cross, so B gives square; then B on Nothing:
	 * square has no button now, under B, whose change did it, not under Y,
	 * which gave it by default */
	ct_ps1(&m);
	rm_set(&m.cur, &m.base, RM_Y, (rm_choice){ RT_B, 0 });
	m.base = m.cur;
	m.cur.b[RM_B] = (rm_choice){ RM_NONE, 0 };
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_WARN + RT_Y);
	ck(k > 0 && ids[k - 1] == CT_BUTTON + RM_B && !strcmp(lbl(&rows[k]), PAD_SQUARE " has no button now"),
	   "ct_warn_cause: \"□ has no button now\" under B, the row that did it");
	m.cur.b[RM_A] = (rm_choice){ RM_NONE, 0 };
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(ct_at(ids, n, CT_WARN + RT_A) == ct_at(ids, n, CT_BUTTON + RM_A) + 1 &&
	   ct_at(ids, n, CT_WARN + RT_Y) == ct_at(ids, n, CT_BUTTON + RM_B) + 1,
	   "and with A on Nothing too, circle's under A and square's still under B");

	/* ct_dead_select: the Dreamcast with the stick on sends SELECT as SELECT,
	 * so what a SELECT changed before gave is lost, and said, under SELECT;
	 * and a swap never lands on the dead row */
	memset(&m, 0, sizeof m);
	m.tag = "DC"; m.ext = "chd"; m.sys_label = "Dreamcast"; m.map = CT_DC_MAP;
	ct_table(&m.def, CT_DC_MAP);
	m.cur = m.base = m.zdef = m.def;
	m.cur.b[RM_SELECT] = (rm_choice){ RT_START, 0 };
	m.cur.b[RM_START] = (rm_choice){ RT_SELECT, 0 };
	m.stick_offered = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	ck(ct_at(ids, n, CT_WARN + RT_START) < 0, "the stick off: SELECT gives Start, nothing lost");
	m.stick_on = m.select_held_dpad = true;
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_WARN + RT_START);
	ck(k > 0 && ids[k - 1] == CT_BUTTON + RM_SELECT, "the stick on: Start has no button now, under SELECT");
	m.cur = m.base = m.def;
	ct_pick(&m, RM_A, (rm_choice){ RT_SELECT, 0 });
	ck(m.cur.b[RM_SELECT].to == RT_SELECT && m.cur.b[RM_A].to == RT_SELECT,
	   "a choice SELECT gives is no swap with the dead SELECT: it keeps it");
	/* ct_sent_select_default: the stick's rule sends ZyperOS's own SELECT,
	 * not a plain one and not the scope's default. This Game under a
	 * Dreamcast whose SELECT and START were swapped for every game: the
	 * game is sent a plain SELECT, so its Start is lost, and said */
	m.def.b[RM_SELECT] = (rm_choice){ RT_START, 0 };
	m.def.b[RM_START] = (rm_choice){ RT_SELECT, 0 };
	m.cur = m.base = m.def;
	{
		rm_table sent;

		ct_sent(&m, &sent);
		ck(sent.b[RM_SELECT].to == RT_SELECT, "ct_sent_select_default: ZyperOS's SELECT, as rms_sent sends it");
		n = ct_rows(&m, ids, rows, vcol, &b);
		k = ct_at(ids, n, CT_WARN + RT_START);
		ck(k > 0 && ids[k - 1] == CT_BUTTON + RM_SELECT, "and Start has no button now, under SELECT");
		m.zdef.b[RM_SELECT] = (rm_choice){ RT_R2, 0 };
		ct_sent(&m, &sent);
		ck(sent.b[RM_SELECT].to == RT_R2 && sent.b[RM_SELECT].turbo == 0,
		   "a default whose SELECT is a pair of its own (the N64's select:r2) has it put back");
	}
	memset(&m, 0, sizeof m);
	m.tag = "N64"; m.ext = "z64"; m.map = CT_N64_MAP;
	ct_table(&m.def, CT_N64_MAP);
	m.cur = m.base = m.def;
	m.select_held_c = true;
	ct_pick(&m, RM_Y, (rm_choice){ RT_R2, 0 });
	ck(m.cur.b[RM_SELECT].to == RT_R2 && m.cur.b[RM_Y].to == RT_R2,
	   "the N64's SELECT keeps R2, the C buttons' own, when another button is given it");
	ck(!ct_step(&m, CT_BUTTON + RM_SELECT, +1), "and its dead row has no step");

	/* ct_stick_psp: the PSP's R2 is a button like any other now, its
	 * default Stick On/Off (map|PSP|r2:stick), which turns the d-pad into
	 * the stick; on a system with a stick every button but SELECT is
	 * offered Stick On/Off and Stick While Held, last; they swap as a game
	 * button does, and a game button left with none still warns */
	memset(&m, 0, sizeof m);
	m.tag = "PSP"; m.ext = "iso"; m.sys_label = "PSP"; m.sys_name = "PSP"; m.map = "r2:stick";
	ct_table(&m.def, "r2:stick");
	m.cur = m.base = m.zdef = m.def;
	m.stick_offered = true;
	m.held = ct_held_of("PSP");
	ck(m.held == 0 && ct_held_of("PS1") == 0 && ct_held_of("DC") == 0,
	   "ct_held_of: no button of the PSP's is Diatom's now, nor of any system but the DS");
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_BUTTON + RM_R2);
	ck(k > 0 && rows[k].live && !strcmp(val(&rows[k]), "Stick On/Off") && !vcol[k],
	   "ct_stick_psp: R2 is a live row, Stick On/Off, its default and so not changed");
	nch = ct_choices(&m, RM_R2, ch, 32);
	ck(nch == 15 && ch[13].to == RM_STICK_TOGGLE && ch[14].to == RM_STICK_HOLD && ch[12].to == RM_NONE,
	   "R2's choices: four faces, L, R, Start, Select, four turbos, Nothing, then the stick's two");
	ck(ct_choices(&m, RM_SELECT, ch, 32) == 13, "SELECT is offered no stick: it holds the hotkeys");
	m.stick_offered = false;
	ck(ct_choices(&m, RM_R2, ch, 32) == 13, "and a system with no stick offers none");
	m.stick_offered = true;
	ck(ct_step(&m, CT_BUTTON + RM_R2, +1) && m.cur.b[RM_R2].to == RM_STICK_HOLD,
	   "Right on R2: Stick While Held");
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_BUTTON + RM_R2);
	ck(k > 0 && !strcmp(val(&rows[k]), "Stick While Held") && vcol[k] == 1u, "said, and changed");
	m.cur = m.base = m.def;
	ct_pick(&m, RM_Y, (rm_choice){ RM_STICK_TOGGLE, 0 });
	ck(m.cur.b[RM_Y].to == RM_STICK_TOGGLE && m.cur.b[RM_R2].to == RT_Y && m.cur.b[RM_R2].turbo == 0,
	   "Y given Stick On/Off: R2 takes square, a swap, so one button turns the stick");
	n = ct_rows(&m, ids, rows, vcol, &b);
	for (k = 0; k < n && !(ids[k] >= CT_WARN && ids[k] < CT_RESET); k++) ;
	ck(k == n, "and nothing has no button: square is on R2");
	m.cur = m.base = m.def;
	ct_pick(&m, RM_R2, (rm_choice){ RM_NONE, 0 });
	n = ct_rows(&m, ids, rows, vcol, &b);
	for (k = 0; k < n && !(ids[k] >= CT_WARN && ids[k] < CT_RESET); k++) ;
	ck(k == n && m.cur.b[RM_R2].to == RM_NONE,
	   "R2 given Nothing: no game button is lost, the stick is none of the game's");
	m.cur = m.base = m.def;
	ct_pick(&m, RM_Y, (rm_choice){ RM_STICK_HOLD, 0 });
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_WARN + RT_Y);
	ck(m.cur.b[RM_R2].to == RM_STICK_TOGGLE && k > 0 && ids[k - 1] == CT_BUTTON + RM_Y,
	   "Y given Stick While Held: R2 keeps Stick On/Off (another choice), and square has no button now, under Y");

	/* ct_share: two buttons on one game button (issue #305). On a PS1
	 * game: Y's list on cross, which B gives, is one Y shares (A swaps and
	 * Y gives both); Y takes it as well; B's row and Y's carry the other's
	 * glyph after the value; the handheld lights both; the warning row is
	 * the game button no button gives; Nothing, a turbo and the row's own
	 * are A's alone */
	memset(&m, 0, sizeof m);
	m.tag = "PS1"; m.ext = "chd"; m.sys_label = "PS1"; m.sys_name = "PlayStation"; m.map = "";
	rm_identity(&m.def);
	m.cur = m.base = m.zdef = m.def;
	{
		char la[192], lb[192];

		ck(ct_can_share(&m, RM_Y, (rm_choice){ RT_B, 0 }) && !ct_can_share(&m, RM_Y, (rm_choice){ RT_Y, 0 }) &&
		   !ct_can_share(&m, RM_Y, (rm_choice){ RM_NONE, 0 }) &&
		   !ct_can_share(&m, RM_Y, (rm_choice){ RT_B, RM_TURBO }),
		   "ct_can_share: on cross (B's), not on its own, Nothing or a turbo");
		ct_pick_lines(&m, RM_Y, (rm_choice){ RT_B, 0 }, la, sizeof la, lb, sizeof lb);
		ck(!strcmp(la, "A: B takes " PAD_SQUARE ".") && !strcmp(lb, "Y: Y and B give " PAD_CROSS "."),
		   "the foot says what A and Y do: B takes square; Y and B give cross");
		ct_pick_lines(&m, RM_Y, (rm_choice){ RT_B, RM_TURBO }, la, sizeof la, lb, sizeof lb);
		ck(!strcmp(lb, "") && !strcmp(la, "Y gives Turbo " PAD_CROSS "."),
		   "a choice of A's alone (a turbo never swaps): one line, Y gives it");
	}
	ct_share(&m, RM_Y, (rm_choice){ RT_B, 0 });
	ck(m.cur.b[RM_Y].to == RT_B && m.cur.b[RM_B].to == RT_B, "Y takes cross as well: both give it");
	ck(ct_partners(&m, RM_Y) == 1u << RM_B && ct_partners(&m, RM_B) == 1u << RM_Y && ct_partners(&m, RM_A) == 0,
	   "ct_partners: B and Y each other's, A none");
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_BUTTON + RM_Y);
	ck(k > 0 && !strcmp(val(&rows[k]), PAD_CROSS MENU_PARTNER_MARK "B") && vcol[k] == 1u,
	   "Y's row: cross, then B's glyph (MENU_PARTNER_MARK), changed");
	k = ct_at(ids, n, CT_BUTTON + RM_B);
	ck(k > 0 && !strcmp(val(&rows[k]), PAD_CROSS MENU_PARTNER_MARK "Y") && vcol[k] == 0,
	   "B's row: cross, then Y's glyph, not changed");
	k = ct_at(ids, n, CT_WARN + RT_Y);
	ck(k > 0 && ids[k - 1] == CT_BUTTON + RM_Y, "square has no button now, under Y");
	m.base = m.cur;
	ck(ct_lit(&m, CT_BUTTON + RM_Y) == (1u << TD_Y | 1u << TD_B), "the handheld lights both, on Y's row");
	/* removing one: B given L1, Y keeps cross alone */
	ct_pick(&m, RM_B, (rm_choice){ RT_L1, 0 });
	ck(m.cur.b[RM_B].to == RT_L1 && m.cur.b[RM_Y].to == RT_B && m.cur.b[RM_L1].to == RT_B &&
	   ct_partners(&m, RM_Y) == 1u << RM_L1, "B given L1 swaps as ever: L1 takes cross, beside Y");

	/* ct_held_nds: a DS game's L2 and R2 are Diatom's, the other screen and
	 * the stylus */
	memset(&m, 0, sizeof m);
	m.tag = "NDS"; m.ext = "nds"; m.sys_label = "NDS"; m.sys_name = "Nintendo DS"; m.map = "";
	rm_identity(&m.def);
	m.cur = m.base = m.zdef = m.def;
	m.held = ct_held_of("NDS");
	n = ct_rows(&m, ids, rows, vcol, &b);
	k = ct_at(ids, n, CT_BUTTON + RM_L2);
	ck(m.held == (1u << RM_L2 | 1u << RM_R2) && k > 0 && !rows[k].live &&
	   !strcmp(val(&rows[k]), "Other screen") && ids[k + 1] == CT_BUTTON + RM_R2 && !rows[k + 1].live &&
	   !strcmp(val(&rows[k + 1]), "Stylus on/off"),
	   "ct_held_nds: L2 and R2 are dead rows: Other screen, Stylus on/off");
	ck(!ct_step(&m, CT_BUTTON + RM_L2, -1) && !ct_step(&m, CT_BUTTON + RM_R2, +1) &&
	   rm_equal(&m.cur, &m.base), "no step on either");
	ct_pick(&m, RM_A, (rm_choice){ RT_L2, 0 });
	ck(m.cur.b[RM_L2].to == RT_L2 && m.cur.b[RM_R2].to == RT_R2,
	   "a swap never lands on them: A given L2 leaves L2 giving its own");
	nch = ctl_remap_choices("NDS", "nds", "", ch, 32);
	for (i = 0; i < nch && ch[i].to != RT_L2 && ch[i].to != RT_R2; i++) ;
	ck(i == nch, "and neither is a choice");

	/* ct_lit_swap */
	ct_ps1(&m);
	rm_set(&m.cur, &m.base, RM_Y, (rm_choice){ RT_B, 0 });
	ck(m.cur.b[RM_B].to == RT_Y, "Y given cross: B gives square");
	ck(ct_lit(&m, CT_BUTTON + RM_Y) == (1u << TD_Y | 1u << TD_B), "ct_lit_swap: the Y row lights Y and B");
	m.base = m.cur;
	ck(ct_lit(&m, CT_BUTTON + RM_Y) == 1u << TD_Y, "once the cursor comes back, Y alone");
	ck(ct_lit(&m, CT_RESET) == (1u << TD_Y | 1u << TD_B), "Reset Buttons lights every changed button");

	/* ct_lean: the handheld turns toward the side the focused row lights, the
	 * d-pad's further, and tilts back to show its top for a shoulder */
	ck(CT_LEAN_DPAD_DEG >= 20.0f && CT_LEAN_DPAD_DEG <= 25.0f && CT_LEAN_DPAD_DEG > CT_LEAN_DEG,
	   "ct_lean: the d-pad leans 20 to 25 degrees, further than a side's keys");
	ck(CT_PITCH_DEG > 0.0f, "ct_lean: a shoulder's pitch shows the top");
	ct_ps1(&m);
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_Y), CT_LEAN_DEG, 0.0f), "ct_lean: the Y row leans right, no pitch");
	ck(lean_is(ct_lit(&m, CT_STICK), -CT_LEAN_DPAD_DEG, 0.0f),
	   "ct_lean: D-pad as Stick on the PlayStation leans further left, no pitch");
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_L1), -CT_LEAN_DEG, CT_PITCH_DEG), "ct_lean: the L1 row leans left and shows the top");
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_L2), -CT_LEAN_DEG, CT_PITCH_DEG), "ct_lean: the L2 row leans left and shows the top");
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_R2), CT_LEAN_DEG, CT_PITCH_DEG), "ct_lean: the R2 row leans right and shows the top");
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_R1), CT_LEAN_DEG, CT_PITCH_DEG), "ct_lean: the R1 row leans right and shows the top");
	ck(lean_is(ct_lit(&m, CT_BUTTON + RM_SELECT), 0.0f, 0.0f), "ct_lean: the SELECT row, front");
	ck(lean_is(ct_lit(&m, CT_SCOPE), 0.0f, 0.0f) && lean_is(ct_lit(&m, CT_HOTKEYS), 0.0f, 0.0f),
	   "ct_lean: Applies To and Hotkeys, front");
	ck(lean_is(1u << TD_START, 0.0f, 0.0f) && lean_is(0, 0.0f, 0.0f), "ct_lean: START alone and no keys, front");
	ck(lean_is(1u << TD_L1 | 1u << TD_R1, 0.0f, 0.0f), "ct_lean: both sides' shoulders, front");
	ck(lean_is(1u << TD_MENU, 0.0f, 0.0f) && lean_is(1u << TD_F1 | 1u << TD_F2, 0.0f, 0.0f),
	   "ct_lean: a side face's key, and keys the Pixel 2 has not, front");
	{
		float y = -99.0f, p = -99.0f;

		ct_lean(NULL, 1u << TD_A, &y, &p);
		ck(y == 0.0f && p == 0.0f, "ct_lean: no model, front");
	}
	ck(lean_is(1u << TD_L1 | 1u << TD_SELECT, -CT_LEAN_DEG, CT_PITCH_DEG),
	   "ct_lean: a shoulder with a centre key leans to its side and shows the top");
	ck(lean_is(1u << TD_A | 1u << TD_R2, CT_LEAN_DEG, 0.0f), "ct_lean: a face with a right shoulder, no pitch");
	ck(lean_is(1u << TD_DPAD | 1u << TD_L2, -CT_LEAN_DPAD_DEG, 0.0f), "ct_lean: the d-pad with a left shoulder, the d-pad's");
	rm_set(&m.cur, &m.base, RM_B, (rm_choice){ RT_L1, 0 });
	ck(lean_is(ct_lit(&m, CT_RESET), 0.0f, 0.0f), "ct_lean: Reset Buttons with B and L1 changed, front");
	ct_ps1(&m);
	rm_set(&m.cur, &m.base, RM_B, (rm_choice){ RT_R1, 0 });
	ck(lean_is(ct_lit(&m, CT_RESET), CT_LEAN_DEG, 0.0f), "ct_lean: Reset Buttons with B and R1 changed, right, no pitch");
	/* The N64's stick lights the d-pad and SELECT: left and centre is left */
	memset(&m, 0, sizeof m);
	m.tag = "N64"; m.ext = "z64"; m.sys_label = "N64"; m.map = CT_N64_MAP;
	ct_table(&m.def, CT_N64_MAP);
	m.cur = m.base = m.def;
	m.stick_offered = m.stick_on = m.stick_default = true;
	m.select_held_c = true;
	ck(ct_lit(&m, CT_STICK) == (1u << TD_DPAD | 1u << TD_SELECT) && lean_is(ct_lit(&m, CT_STICK), -CT_LEAN_DPAD_DEG, 0.0f),
	   "ct_lean: the stick on the N64 (d-pad and SELECT) leans as the d-pad does");
	/* The Dreamcast's Face Buttons light the four faces */
	memset(&m, 0, sizeof m);
	m.tag = "DC"; m.ext = "chd"; m.sys_label = "Dreamcast"; m.map = CT_DC_MAP;
	ct_table(&m.def, CT_DC_MAP);
	m.cur = m.base = m.zdef = m.def;
	m.face_offered = ctl_letter_layout("DC", &m.letters);
	ck(lean_is(ct_lit(&m, CT_FACE), CT_LEAN_DEG, 0.0f), "ct_lean: Face Buttons lean right, no pitch");

	/* ct_lean on the Brick: its front's keys as the Pixel 2's; its shoulders
	 * alone, on the back ledge no front pose shows, turn it round to its back
	 * view (off the tab's front pose), whichever side they are on; with other
	 * keys, the others' lean, turning to the back and back again (ct_spin) */
	{
		const td_model *b = &td_model_brick;
		const float back = b->back.deg - b->tab.deg, tilt = b->back.pitch;
		float sy = 0.0f, sp = 0.0f;

		ck(back > 140.0f && tilt > 0.0f, "ct_lean: the Brick's back view is turned round and tilted");
		ck(brick_lean_is(1u << TD_DPAD, -CT_LEAN_DPAD_DEG, 0.0f) && brick_lean_is(1u << TD_Y, CT_LEAN_DEG, 0.0f) &&
		   brick_lean_is(1u << TD_A, CT_LEAN_DEG, 0.0f), "ct_lean: the Brick's d-pad left, Y and A right, level");
		ck(brick_lean_is(1u << TD_SELECT, 0.0f, 0.0f) && brick_lean_is(1u << TD_START, 0.0f, 0.0f) &&
		   brick_lean_is(1u << TD_MENU, 0.0f, 0.0f), "ct_lean: the Brick's SELECT, START and MENU, front");
		ck(brick_lean_is(1u << TD_L1, back, tilt) && brick_lean_is(1u << TD_L2, back, tilt) &&
		   brick_lean_is(1u << TD_R2, back, tilt) && brick_lean_is(1u << TD_R1, back, tilt),
		   "ct_lean: each of the Brick's shoulders, the back view");
		ck(brick_lean_is(1u << TD_L1 | 1u << TD_R1, back, tilt),
		   "ct_lean: the Brick's shoulders on both sides, the back view");
		ck(!ct_spin(b, 1u << TD_L1 | 1u << TD_R1, &sy, &sp) && !ct_spin(b, 1u << TD_A, &sy, &sp),
		   "ct_spin: shoulders alone, or front keys alone, do not spin");
		ck(brick_lean_is(1u << TD_L2 | 1u << TD_SELECT, 0.0f, 0.0f) &&
		   ct_spin(b, 1u << TD_L2 | 1u << TD_SELECT, &sy, &sp) && sy == back && sp == tilt,
		   "ct_lean: a shoulder with a centre key, the front, spinning round to the back");
		ck(brick_lean_is(1u << TD_A | 1u << TD_R2, CT_LEAN_DEG, 0.0f) &&
		   brick_lean_is(1u << TD_DPAD | 1u << TD_L1, -CT_LEAN_DPAD_DEG, 0.0f) &&
		   brick_lean_is(1u << TD_B | 1u << TD_L1, CT_LEAN_DEG, 0.0f) &&
		   ct_spin(b, 1u << TD_A | 1u << TD_R2, &sy, &sp) && ct_spin(b, 1u << TD_DPAD | 1u << TD_L1, &sy, &sp),
		   "ct_lean: a front key with the Brick's shoulders leans as the front key, level, and spins");
		ck(!ct_spin(&td_model_pixel2, 1u << TD_A | 1u << TD_R2, &sy, &sp),
		   "ct_spin: the Pixel 2's shoulders show from the front: no spin");
	}

	/* The steps, round the choices, and Nothing */
	ct_ps1(&m);
	nch = ctl_remap_choices("PS1", "chd", "", ch, 32);
	ck(nch == 15, "the PlayStation's choices: four faces, six more, four turbos, Nothing");
	ck(ct_next(&m, ch, nch, RM_A, +1).to == RT_B && ct_next(&m, ch, nch, RM_A, -1).to == RM_NONE,
	   "Right on A is cross, Left goes round to Nothing");
	ck(!ct_select_held_c(&m.def), "the PlayStation's SELECT is its own");
	/* A step works from the table as it was when the cursor came onto the
	 * row: a second step undoes the first one's swap */
	ck(ct_step(&m, CT_BUTTON + RM_Y, -1) && m.cur.b[RM_Y].to == RT_X && m.cur.b[RM_X].to == RT_Y,
	   "Left on Y: triangle, and X takes square");
	ck(ct_step(&m, CT_BUTTON + RM_Y, -1) && m.cur.b[RM_Y].to == RT_B && m.cur.b[RM_B].to == RT_Y &&
	   m.cur.b[RM_X].to == RT_X, "Left again: cross, B takes square, and X has triangle back");
	ck(ct_step(&m, CT_BUTTON + RM_Y, +1) && ct_step(&m, CT_BUTTON + RM_Y, +1) && rm_equal(&m.cur, &m.base),
	   "and two Rights put the table back as it was");
	ck(!ct_step(&m, CT_SCOPE, +1) && rm_equal(&m.cur, &m.base), "no step on a row that is no button");
	i = ct_choice_at(&m, ch, nch, m.cur.b[RM_START]);
	ck(i >= 0 && ch[i].to == RT_START, "START is among them");
	m.cur.b[RM_A] = (rm_choice){ RT_L1, 0 };
	ck(ct_choice_at(&m, ch, nch, m.cur.b[RM_A]) == 4, "L1 is the first after the faces");
	{
		ct_model nes;
		rm_choice nc[32];
		int nn;

		memset(&nes, 0, sizeof nes);
		nes.tag = "NES"; nes.ext = "nes"; nes.map = "";
		ct_table(&nes.def, "x:a~3,y:b~3");
		nes.cur = nes.base = nes.def;
		nn = ctl_remap_choices("NES", "nes", "", nc, 32);
		ck(ct_says_nothing(&nes, nes.cur.b[RM_L1]) && ct_choice_at(&nes, nc, nn, nes.cur.b[RM_L1]) == nn - 1,
		   "an NES game's L1 gives nothing: it reads Nothing");
		ck(ct_taken(&nes, RM_L1, (rm_choice){ RM_NONE, 0 }).to == RT_L1,
		   "and Nothing taken there is its own, so nothing is kept");
		n = ct_rows(&nes, ids, rows, vcol, &b);
		k = ct_at(ids, n, CT_BUTTON + RM_L1);
		ck(!strcmp(val(&rows[k]), "Nothing") && !vcol[k], "its row says Nothing, plain");
		nes.cur.b[RM_L1] = (rm_choice){ RT_A, RM_TURBO };
		n = ct_rows(&nes, ids, rows, vcol, &b);
		for (k = 0; k < n && !(ids[k] >= CT_WARN && ids[k] < CT_RESET); k++) ;
		ck(k == n, "no warning for a button the core names nothing");
	}

	/* A button's choices */
	{
		ct_pick_bufs pb;
		int sel = -1;

		ct_ps1(&m);
		rm_set(&m.cur, &m.base, RM_Y, (rm_choice){ RT_B, 0 });
		nch = ctl_remap_choices("PS1", "chd", "", ch, 32);
		n = ct_pick_rows(&m, RM_Y, ch, nch, rows, &pb, &sel);
		ck(n == nch && !strcmp(pb.head, "Y: PlayStation"), "the choices of Y, under \"Y: PlayStation\"");
		ck(sel == 1 && rows[1].value == MENU_TICK && !strcmp(lbl(&rows[1]), PAD_CROSS),
		   "a tick on cross, what it gives now, the cursor on it");
		ck(!strcmp(val(&rows[3]), "Default") && !strcmp(lbl(&rows[3]), PAD_SQUARE), "default on square");
		for (k = 0, i = 0; k < n; k++) i += rows[k].live;
		ck(i == n, "every choice can be taken");
		n = ct_pick_rows(&m, RM_A, ch, nch, rows, &pb, &sel);
		ck(sel == 0 && rows[0].value == MENU_TICK, "A at its default: the tick alone");
	}

	/* The Hotkeys row's pages, beside the handheld: none of the system's
	 * buttons (the tab shows them), SELECT alone a row */
	{
		ctl_game g;
		ctl_buf cb;
		menu_row pr[CTL_MAX_ROWS];
		int np, pages;

		memset(&g, 0, sizeof g);
		g.tag = "PS1"; g.name = "PlayStation"; g.short_name = "PS1"; g.map = ""; g.ext = "chd";
		pages = ctl_game_pages(&g);
		g.no_buttons = true;
		ck(ctl_game_pages(&g) == 2 && pages > 2, "the PlayStation's pages less its buttons: Hotkeys, In a game");
		np = ctl_game_rows(&g, 0, pr, &cb);
		ck(!strcmp(cb.head, "Controls: Hotkeys") && np == 5 && ROW_IS_NOTE(pr[4]),
		   "Hotkeys first, as ever, SELECT alone its note");
		memset(&g, 0, sizeof g);
		g.tag = "N64"; g.name = "Nintendo 64"; g.short_name = "N64"; g.ext = "z64";
		g.map = CT_N64_MAP; g.accessory = "Rumble Pak"; g.no_buttons = true;
		np = ctl_game_rows(&g, 0, pr, &cb);
		ck(!strcmp(cb.head, "Controls: C buttons") && np > 0 && ROW_IS_NOTE(pr[np - 1]) &&
		   !strcmp(lbl(&pr[np - 1]), "Pak: Rumble Pak"),
		   "the N64's C buttons first, and which Pak is in, not where (the tab's Pak row is)");
		/* ...and SELECT then the d-pad only while the d-pad is the stick */
		g.stick = CTL_STICK_ON;
		np = ctl_game_rows(&g, 0, pr, &cb);
		for (k = 0; k < np && strcmp(lbl(&pr[k]), "SELECT+D-pad"); k++) ;
		ck(np == 6 && k < np && !strcmp(val(&pr[k]), "N64 D-pad"),
		   "the stick on: SELECT then the d-pad is the N64's D-pad");
		g.stick = CTL_STICK_OFF;
		np = ctl_game_rows(&g, 0, pr, &cb);
		for (k = 0; k < np && strcmp(lbl(&pr[k]), "SELECT+D-pad"); k++) ;
		ck(np == 5 && k == np && ROW_IS_NOTE(pr[np - 1]),
		   "the stick off: no such row, the d-pad being the N64's D-pad already");
	}
}

/* NES Player 2 (src/p2.h): its row right after Shader's group, on a game
 * whose core the system table offers it on (gm_gather asks p2_offered), and
 * on no other game's menu; A opens its page, so it is a stop and plain. Its
 * value is the state: off, waiting for a player, or one connected. */
static void player2_row_on_nes_only(void)
{
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	int n, at, i;

	printf("in-game menu, NES Player 2:\n");
	u.dmode = "Native"; u.earned = 12; u.total = 40; u.quick = 2;
	ck(gm_row_at(&u, GT_GAME, GM_PLAYER2) == -1 && gm_tab_rows(&u, GT_GAME, (gm_row[GM_MAX_ROWS]){ 0 }) == 7,
	   "no Player 2 row where it is not offered");
	u.player2 = true;
	u.player2_value = gm_player2_value(P2_OFF);
	n = gm_rows(&u, GT_GAME, rows, &b);
	at = gm_row_at(&u, GT_GAME, GM_PLAYER2);
	ck(n == 8, "eight rows with it");
	ck(at == gm_row_at(&u, GT_GAME, GM_QUICK) + 1 && at + 1 == gm_row_at(&u, GT_GAME, GM_CHEEVOS),
	   "right after Quick Slot, before Achievements");
	ck(at >= 0 && !strcmp(rows[at].label, "Player 2") && !strcmp(val(&rows[at]), "Off"),
	   "Player 2, off until it is started");
	ck(at >= 0 && rows[at].live && !b.sound[at], "a stop, and plain: A opens its page");
	ck(!strcmp(gm_player2_value(P2_OFF), "Off") &&
	   !strcmp(gm_player2_value(P2_WAITING), "Waiting") &&
	   !strcmp(gm_player2_value(P2_CONNECTED), "Connected"),
	   "its three values: off, waiting, connected");
	ck(!strcmp(gm_player2_value(P2_FAILED), "Off"), "and a start that failed reads off");
	u.player2_value = gm_player2_value(P2_CONNECTED);
	n = gm_rows(&u, GT_GAME, rows, &b);
	ck(!strcmp(val(&rows[gm_row_at(&u, GT_GAME, GM_PLAYER2)]), "Connected"), "the row says the value it is given");
	/* Over a port, none */
	u.port = true;
	ck(gm_row_at(&u, GT_GAME, GM_PLAYER2) == -1, "and none over a port");
	u.port = false;
	u.player2 = false;
	n = gm_rows(&u, GT_GAME, rows, &b);
	for (i = 0; i < n; i++)
		if (!strcmp(lbl(&rows[i]), "Player 2")) break;
	ck(i == n && n == 7, "without it, every other game keeps its seven");
}

/* The quick slot as the device's database keeps it: per system, slot 1
 * until one is picked, anything that is no slot read as 1, and the row's
 * step round the six */
static void quick_slot(void)
{
	char key[48];

	printf("the quick slot:\n");
	gm_quick_key("SFC", key, sizeof key);
	ck(!strcmp(key, "quickslot.SFC"), "kept per system, as quickslot.<TAG>");
	ck(gm_quick_slot(NULL) == 1, "slot 1 until one is picked");
	ck(gm_quick_slot("4") == 4 && gm_quick_slot("6") == 6, "a slot that was picked");
	ck(gm_quick_slot("0") == 1 && gm_quick_slot("7") == 1 && gm_quick_slot("auto") == 1 &&
	   gm_quick_slot("") == 1, "and 1 for anything that is no slot, Auto included");
	ck(gm_quick_step(1, 1) == 2 && gm_quick_step(6, 1) == 1 && gm_quick_step(1, -1) == 6,
	   "right and left step round 1 to 6");
	ck(gm_quick_step(9, 1) == 2, "from a slot that is none, as from slot 1");
}

/* The save slots, and the one constant that survived a rename by being written
 * out as a number.
 *
 * The carousel's Auto entry mapped to a hardcoded 9 - MinUI's number for the
 * resume slot. When it was renamed to "auto" on 2026-08-27 every use of the
 * CONSTANT was updated and the literal was missed, so Load -> Auto asked for a
 * `.9.state` that could not exist. Ten days later that turned into a wedged
 * device, because the rejection it caused was misread as a dead emulator.
 *
 * Cheap to assert, and it is the assertion that was missing. */
static void slots(void)
{
	int i;

	printf("save slots:\n");
	ck(gm_slot_at(0) == SLOT_AUTO, "the carousel's first entry is the resume slot");
	ck(gm_slot_at(0) != 9, "and is not MinUI's number for it");
	for (i = 1; i <= GM_SLOTS; i++)
		ck(gm_slot_at(i) == i, "a numbered slot maps to itself");

	/* The resume slot must not collide with one the player can pick, or a
	 * save to the last slot would overwrite the state the exit funnel owns. */
	ck(SLOT_AUTO > GM_SLOTS, "the resume slot is outside the numbered range");
}

/* Where Save lands. A double-tap of A used to overwrite slot 1 every time,
 * which is the slot most likely to hold something wanted. */
static void save_slot(void)
{
	bool have[GM_SLOTS + 1] = { false };
	bool ok;
	int  i;

	printf("which slot Save opens on:\n");
	ck(gm_save_slot(have) == 1, "nothing saved yet: the first slot");

	have[1] = true;
	ck(gm_save_slot(have) == 2, "slot 1 taken: the next empty one");

	have[2] = have[3] = true;
	ck(gm_save_slot(have) == 4, "three taken: still the first empty");

	/* A gap in the middle is where a new save belongs - it is empty, and
	 * nothing is lost by using it. */
	for (i = 1; i <= GM_SLOTS; i++) have[i] = true;
	have[3] = false;
	ck(gm_save_slot(have) == 3, "a freed middle slot is used before any reuse");
	have[3] = true;

	/* An empty last slot is reached the ordinary way, not as the fallback -
	 * the two rules agree here, which is what makes the full case readable. */
	have[GM_SLOTS] = false;
	ck(gm_save_slot(have) == GM_SLOTS, "the last slot, while it is still empty");
	have[GM_SLOTS] = true;

	printf("and when every slot is taken:\n");
	ck(gm_save_slot(have) == GM_SLOTS, "the last slot, so the target never moves");

	/* The case the rule exists for. Slot 1 is where a finished playthrough
	 * or a point-of-no-return save tends to sit, and it is the slot a blind
	 * double-tap used to land on. */
	ck(gm_save_slot(have) != 1, "never slot 1, whatever else is on the card");

	/* Every arrangement of the card, not a sampled few: 64 is small enough to
	 * enumerate, and a rule this cheap has no excuse for an untested corner. */
	printf("and it never aims at Auto:\n");
	for (i = 0, ok = true; i < (1 << GM_SLOTS); i++) {
		int j, got;

		for (j = 1; j <= GM_SLOTS; j++) have[j] = (i >> (j - 1)) & 1;
		got = gm_save_slot(have);
		if (got < 1 || got > GM_SLOTS) ok = false;
	}
	ck(ok, "a numbered slot, for every arrangement of the card");
	ck(SLOT_AUTO > GM_SLOTS, "so the resume slot stays with the exit funnel");
}


static void sounds_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	static const char *const names[] = { "Wood", "Glass", "Bubble", "Off" };
	int n, i, found = 0;

	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);

	printf("the UI Sounds row:\n");
	ck(PM_SOUNDS == PM_EQ + 1 && PM_EQ == PM_CHANNELS + 1, "sits under the Equalizer, under Audio Channels");
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "UI Sounds")) {
			found++;
			ck(rows[i].live, "is live on a device with no radio");
			ck(!strcmp(val(&rows[i]), "Wood"), "and with no setting reads Wood, the default");
			ck(i > 0 && !strcmp(rows[i - 1].label, "Equalizer"),
			   "directly under the Equalizer there too");
		}
	ck(found == 1, "is there once");
	for (i = 0; i < 4; i++) {
		u.ui_sounds = names[i];
		n = sys_menu_build(&u, rows, &b, &heading);
		ck(!strcmp(val_of(rows, n, "UI Sounds"), names[i]), "each setting names itself");
	}

	printf("the Menu Music row:\n");
	ck(PM_MUSIC == PM_SOUNDS + 1, "sits under UI Sounds");
	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0, found = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Menu Music")) {
			found++;
			ck(rows[i].live, "is live on a device with no radio");
			ck(!strcmp(val(&rows[i]), "Off"), "and with no value reads Off");
			ck(i > 0 && !strcmp(rows[i - 1].label, "UI Sounds"),
			   "directly under UI Sounds there too");
		}
	ck(found == 1, "is there once");
	u.menu_music = "no tracks";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val_of(rows, n, "Menu Music"), "no tracks"), "says why it is silent");

	/* A system's menu is about the system. */
	memset(&u, 0, sizeof u);
	u.games = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		ck(strcmp(rows[i].label, "UI Sounds"), "a system's menu has none");
}

/* MENU > Sound > Rumble (src/rumble.h): the Sound tab's last row, under Menu
 * Music, Full until it is set, on a handheld with a motor ZyperOS runs; on
 * one without (the Brick's waits for its tester), not there. */
static void rumble_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	pm_row ids[MENU_MAX_ROWS];
	const char *heading;
	int n, i, found = 0;

	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	printf("the Rumble row:\n");
	ck(PM_RUMBLE == PM_MUSIC + 1 && sys_menu_tab_of(PM_RUMBLE) == PT_SOUND, "sits under Menu Music, on Sound");
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Rumble")) {
			found++;
			ck(rows[i].live && !strcmp(val(&rows[i]), "Full"), "is live, and reads Full until it is set");
			ck(i > 0 && !strcmp(rows[i - 1].label, "Menu Music"), "directly under Menu Music there too");
		}
	ck(found == 1, "is there once");
	u.rumble = "Low";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val_of(rows, n, "Rumble"), "Low"), "says the level");
	u.tab = PT_SOUND;
	n = sys_menu_tab_rows(&u, PT_SOUND, ids);
	ck(n > 0 && ids[n - 1] == PM_RUMBLE, "the Sound tab's last row");
	u.no_rumble = true;
	u.tab = PT_TABS;
	n = sys_menu_zyperos_rows(&u, ids);
	for (i = 0, found = 0; i < n; i++) found += ids[i] == PM_RUMBLE;
	ck(!found, "no motor: no row");
}

/* MENU > Side Lights (src/sidelights.h): on a device with lights of its own,
 * under Charge Limit, saying how many parts are on, opening the page of five
 * switches; on one without, not there at all. The setting's words are
 * tools/sidelights-check.c's. */
/* The Charge Limit row (DESIGN.md 11.26): on a charger that can stop short,
 * under Auto Off, naming the setting, Off by default, steps round; on one that
 * cannot, not there at all. */
static void charge_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	pm_row pm[PM_ROWS];
	const char *heading;
	int n, k, i, found = 0;

	printf("the Charge Limit row:\n");
	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	u.cable_xfer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Charge Limit")) {
			found++;
			ck(rows[i].live, "is live on the Pixel 2");
			ck(!strcmp(val(&rows[i]), "Off"), "and Off with no setting, a full charge");
			ck(i > 0 && !strcmp(lbl(&rows[i - 1]), "Auto Off"), "directly under Auto Off");
			ck(menu_sound_at(b.sound, n, i) == MENU_SND_RING, "steps round, heard on the press");
		}
	ck(found == 1, "is there once");
	u.charge_limit = 85;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(has_label(rows, n, "Charge Limit") && !strcmp(val_of(rows, n, "Charge Limit"), "85%"),
	   "a limit names itself in percent");
	u.charge_limit = 80;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val_of(rows, n, "Charge Limit"), "80%"), "80% too");

	u.no_charge_limit = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && !has_label(rows, n, "Charge Limit"), "a charger that cannot stop short has no row");
	for (i = 0; i < k; i++) ck(pm[i] != PM_CHARGE, "and the list does not name it");
}

/* The Screensaver row (src/saver.h): on Display, after Battery Icon, on every device, its
 * value the delay in Auto Off's words or off, stepped round with left and
 * right the way Auto Off is. */
static void saver_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	pm_row pm[PM_ROWS];
	const char *heading;
	char s[16];
	struct { int sec; const char *want; } t[] = {
		{ 0, "Off" }, { 30, "30 s" }, { 60, "1 min" }, { 120, "2 min" }, { 300, "5 min" }, { 600, "10 min" },
	};
	int n, k, i, found = 0;
	size_t j;

	printf("the Screensaver row:\n");
	ck(sys_menu_tab_of(PM_SAVER) == PT_DISPLAY && PM_SAVER == PM_BATTERY + 1,
	   "is on Display, after Battery Icon");
	for (j = 0; j < sizeof t / sizeof t[0]; j++) {
		sys_menu_saver_label(t[j].sec, s, sizeof s);
		ck(!strcmp(s, t[j].want), t[j].want);
	}
	/* The Pixel 2, then the Brick: a row on both, with or without lights */
	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	u.cable_xfer = true;
	u.saver = 60;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Screensaver")) {
			found++;
			ck(rows[i].live, "is live");
			ck(!strcmp(val(&rows[i]), "1 min"), "reads its delay");
			ck(menu_sound_at(b.sound, n, i) == MENU_SND_NO_A,
			   "steps with left and right and is heard after, as Auto Off is");
		}
	ck(found == 1, "is there once");
	u.saver = 0;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0, found = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Screensaver"))
			found += !strcmp(val(&rows[i]), "Off");
	ck(found == 1, "and off when it is off");
	u.no_lights = true;
	u.no_charge_limit = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && has_label(rows, n, "Screensaver"), "a device without lights has it too");
	for (i = 0, found = 0; i < k; i++) found += pm[i] == PM_SAVER;
	ck(found == 1, "and the list names it");
}

/* Power Button (1.0.6, src/powerhold.h): on System, over Auto Off, Press
 * as it ships and Hold when it is set, and A switches it, as Left and Right
 * do: two choices, heard as a ring */
static void power_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	pm_row tab[PM_ROWS];
	int n, k;

	printf("the Power Button row:\n");
	ck(sys_menu_tab_of(PM_POWER) == PT_SYSTEM && PM_SLEEP == PM_POWER + 1,
	   "Power Button is on System, over Auto Off");
	memset(&u, 0, sizeof u);
	u.tab = PT_TABS;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(rows[PM_POWER].label, "Power Button") && rows[PM_POWER].live,
	   "it is there and live on every device");
	ck(!strcmp(val(&rows[PM_POWER]), "Press"), "Press when nothing is set, as it ships");
	ck(b.sound[PM_POWER] == MENU_SND_RING, "switched on A too, heard as a ring");
	u.power_hold = true;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_POWER]), "Hold"), "Hold when it is set");
	u.tab = PT_SYSTEM;
	n = sys_menu_tab_rows(&u, PT_SYSTEM, tab);
	for (k = 0; k < n && tab[k] != PM_POWER; k++) ;
	ck(k < n && k + 1 < n && tab[k + 1] == PM_SLEEP && k > 0 && tab[k - 1] == PM_HOTKEYS &&
	   k > 1 && tab[k - 2] == PM_LANGUAGE,
	   "on System's page between Hotkeys (after Language) and Auto Off");
	ck(!strcmp(rows[PM_HOTKEYS].label, "Hotkeys") && rows[PM_HOTKEYS].live && !rows[PM_HOTKEYS].value,
	   "Hotkeys: a row that opens its list");
}

/* Home Systems (1.0.6, src/homesys.h): on Display under System Order, All
 * when every shelf is on, and its page: Favorites and Recents, a rule, every
 * system with games On or Off, the note; the last system on cannot go, and a
 * second note says so */
static void homesys_rows(void)
{
	static const char *const names[3] = { "PlayStation", "Ports", "Game Boy" };
	static const char *const tags[3] = { "PS1", "PRT", "GB" };
	homesys_ui hu = { names, tags, 3, true };
	menu_row rows[HS_ROWS], all[MENU_MAX_ROWS];
	const char *ids[HS_ROWS];
	menu_bufs b;
	const char *heading;
	sys_ui u;
	int n;

	printf("the Home Systems row and its page:\n");
	ck(sys_menu_tab_of(PM_HOMESYS) == PT_DISPLAY && PM_HOMESYS == PM_ORDER + 1,
	   "Home Systems is on Display, under System Order");
	memset(&u, 0, sizeof u);
	u.tab = PT_TABS;
	sys_menu_build(&u, all, &b, &heading);
	ck(!strcmp(all[PM_HOMESYS].label, "Home Systems") && !strcmp(val(&all[PM_HOMESYS]), "All") &&
	   all[PM_HOMESYS].live, "Home Systems reads All when nothing is said");
	u.homesys = "4 of 5";
	sys_menu_build(&u, all, &b, &heading);
	ck(!strcmp(val(&all[PM_HOMESYS]), "4 of 5"), "and what it is given");

	homesys_parse("");
	n = sys_menu_homesys_rows(&hu, rows, ids);
	ck(n == 9 && !strcmp(rows[0].label, "Favorites") && !strcmp(rows[1].label, "Recents") &&
	   !strcmp(rows[2].label, "Collections") && !rows[3].label && !strcmp(rows[4].label, "PlayStation") &&
	   !strcmp(rows[6].label, "Game Boy") && !rows[7].label && ROW_IS_NOTE(rows[8]),
	   "Favorites, Recents, Collections, a rule, the three systems, a rule, the note");
	ck(!strcmp(ids[0], "FAV") && !strcmp(ids[1], "RECENT") && !strcmp(ids[2], "COLL") && !ids[3] &&
	   !strcmp(ids[5], "PRT") && !ids[7] && !ids[8],
	   "each switch names its tag; the rule and the note none");
	ck(!strcmp(val(&rows[0]), "On") && !strcmp(val(&rows[2]), "On") && !strcmp(val(&rows[5]), "On") &&
	   rows[5].live, "all On as they ship");
	homesys_parse("PRT,RECENT,COLL");
	n = sys_menu_homesys_rows(&hu, rows, ids);
	ck(!strcmp(val(&rows[1]), "Off") && !strcmp(val(&rows[2]), "Off") && !strcmp(val(&rows[5]), "Off") &&
	   !strcmp(val(&rows[4]), "On"), "Ports, Recents and Collections Off, as the word says");
	homesys_parse("PRT,GB");
	n = sys_menu_homesys_rows(&hu, rows, ids);
	ck(n == 10 && !rows[4].live && rows[5].live && ROW_IS_NOTE(rows[9]),
	   "one system left on: its row is dead, and a second note says why");
	/* With no collection on the card, no Collections row, as a system with
	 * no games has none */
	hu.coll = false;
	homesys_parse("");
	n = sys_menu_homesys_rows(&hu, rows, ids);
	ck(n == 8 && !strcmp(rows[1].label, "Recents") && !rows[2].label && !strcmp(ids[1], "RECENT") &&
	   !strcmp(rows[3].label, "PlayStation"), "no collection: Favorites, Recents, a rule, the systems");
	/* A shot on the card (DESIGN.md 11.68): Screenshots' row after
	 * Collections, as Collections' after Recents */
	hu.coll = true;
	hu.shots = true;
	homesys_parse("SHOT");
	n = sys_menu_homesys_rows(&hu, rows, ids);
	ck(n == 10 && !strcmp(rows[3].label, "Screenshots") && !strcmp(ids[3], "SHOT") && rows[3].live &&
	   !strcmp(val(&rows[3]), "Off") && !rows[4].label && !strcmp(rows[5].label, "PlayStation"),
	   "a shot on the card: Screenshots after Collections, Off as the word says");
	homesys_parse("");
}

/* The Screenshots shelf's menu (DESIGN.md 11.68): Sort By, Recently Added
 * as it ships, and View; no count and no Random Game */
static void shots_menu(void)
{
	sys_ui u;
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs b;
	const char *heading;
	int n;

	printf("the Screenshots shelf's menu:\n");
	memset(&u, 0, sizeof u);
	u.games = true;
	u.shots = true;
	u.sys_name = "Screenshots";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == SM_SHOT_ROWS && n == 2 && !strcmp(heading, "Screenshots"), "two rows under its name");
	ck(!strcmp(rows[0].label, "Sort By") && !strcmp(val(&rows[0]), "Recently Added") && rows[0].live,
	   "Sort By first, Recently Added as it ships");
	ck(!strcmp(rows[1].label, "View") && !strcmp(val(&rows[1]), "Covers") && rows[1].live, "then View, on Covers");
	u.sort = "Name";
	u.video = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[0]), "Name") && !strcmp(val(&rows[1]), "Video"), "each says what it is on");
}

static void lights_row(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS], page[LP_COUNT];
	side_light_part ids[LP_COUNT];
	pm_row pm[PM_ROWS];
	const char *heading;
	char buf[16];
	int n, k, i, found = 0;

	printf("the Side Lights row:\n");
	ck(PM_CHARGE == PM_SLEEP + 1 && PM_LIGHTS == PM_CHARGE + 1,
	   "sits under Charge Limit, under Auto Off");

	/* The GKD Pixel 2: no radio, files over the cable, four lights */
	memset(&u, 0, sizeof u);
	u.no_wifi = true;
	u.no_bt = true;
	u.cable_xfer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Side Lights")) {
			found++;
			ck(rows[i].live, "is live on the Pixel 2");
			ck(!strcmp(val(&rows[i]), "All on"), "and with no setting reads All on, the default");
			ck(i > 0 && !strcmp(lbl(&rows[i - 1]), "Charge Limit"),
			   "directly under Charge Limit there too");
			ck(menu_sound_at(b.sound, n, i) == 0,
			   "opens a page on A, so it does not step round");
		}
	ck(found == 1, "is there once");
	u.lights = side_lights_value(1u << LP_LOW, buf, sizeof buf);
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val_of(rows, n, "Side Lights"), "1 on"), "the row counts the parts on");

	/* The Brick: no lights, no row, and the list agrees with the build */
	u.no_lights = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && !has_label(rows, n, "Side Lights"), "a device without lights has no row");
	for (i = 0; i < k; i++) ck(pm[i] != PM_LIGHTS, "and the list does not name it");

	/* The page: the five parts, as I picked them in the side lights lab, and
	 * Battery Level with the battery's two (my ask after testing 1.0.6) */
	n = sys_menu_lights_rows(LIGHTS_ALL, page, ids);
	ck(n == 6, "the page has six rows");
	ck(!strcmp(lbl(&page[0]), "Charging Bar") && !strcmp(lbl(&page[1]), "Low Battery") &&
	   !strcmp(lbl(&page[2]), "Battery Level") && !strcmp(lbl(&page[3]), "Volume and Brightness") &&
	   !strcmp(lbl(&page[4]), "Files and Covers") && !strcmp(lbl(&page[5]), "Achievements"),
	   "named in the lab's order, Battery Level third");
	{
		static const side_light_part want[6] = { LP_BAR, LP_LOW, LP_BATTERY, LP_LEVEL, LP_DONE, LP_CHEER };

		for (i = 0; i < n; i++)
			ck(page[i].live && !strcmp(val(&page[i]), "On") && ids[i] == want[i],
			   "each On, live, and its part");
	}
	n = sys_menu_lights_rows(LIGHTS_DEFAULT, page, ids);
	for (i = 0; i < n; i++)
		ck(!strcmp(val(&page[i]), i == 2 ? "Off" : "On"), "a new card: Battery Level Off, the rest On");
	n = sys_menu_lights_rows(side_lights_flip(LIGHTS_ALL, LP_DONE), page, ids);
	ck(!strcmp(val(&page[4]), "Off") && !strcmp(val(&page[3]), "On"), "a part off says Off");
	ck(!strcmp(side_lights_value(LIGHTS_ALL, buf, sizeof buf), "All on") &&
	   !strcmp(side_lights_value(0, buf, sizeof buf), "Off") &&
	   !strcmp(side_lights_value((1u << LP_BAR) | (1u << LP_CHEER), buf, sizeof buf), "2 on"),
	   "the row's words");
	ck(!strcmp(side_lights_value(LIGHTS_DEFAULT, buf, sizeof buf), "5 on"),
	   "a new card, Battery Level off to start, reads 5 on, not All on");
	ck(!strcmp(side_lights_value(1u << LP_BATTERY, buf, sizeof buf), "1 on") &&
	   !strcmp(side_lights_value((1u << LP_BAR) | (1u << LP_CHEER) | (1u << LP_BATTERY), buf, sizeof buf), "3 on") &&
	   !strcmp(side_lights_value(LIGHTS_DEFAULT & ~(1u << LP_DONE), buf, sizeof buf), "4 on") &&
	   !strcmp(side_lights_value(LIGHTS_ALL & ~(1u << LP_DONE), buf, sizeof buf), "5 on"),
	   "Battery Level counts like every other part");
}

/* How each row sounds (DESIGN.md 9): the class each build sets beside its
 * rows, by the row's id, and that the runner reads by place (menu_sound_at).
 * A value that steps round is heard on the press, before its handler, which
 * on the in-game menu's Display waits on Diatom; one that stops at its ends
 * (Auto Off) is heard after, by whether it moved; A on a row that does
 * nothing with it is deny; UI Sounds plays its own. Held to the real builds,
 * and never taken from a row's text: a network or a headset named like a
 * marked row is plain. */
static void row_sounds(void)
{
	sys_ui u;
	menu_bufs b;
	menu_row rows[MENU_MAX_ROWS];
	const char *heading;
	gm_ui g = { 0 };
	gm_bufs gb;
	menu_row gr[GM_MAX_ROWS];
	gm_row ids[GM_MAX_ROWS];
	sm_mp4_row mids[SM_MP4_ROWS];
	wifi_ui w;
	menu_row wr[64];
	int n, i;

	printf("how rows sound:\n");
	memset(&u, 0, sizeof u);
	u.wifi = WIFI_OFF;
	u.update = "0.1.0";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == PM_ROWS, "the whole ZyperOS menu");
	{
		static const pm_row ring[] = { PM_AUDIO, PM_CHANNELS, PM_APPEARANCE };

		for (i = 0; i < (int)(sizeof ring / sizeof ring[0]); i++)
			ck(menu_sound_at(b.sound, n, ring[i]) == MENU_SND_RING,
			   "a value that steps round, and A steps it too: heard on the press");
	}
	ck(menu_sound_at(b.sound, n, PM_SLEEP) == MENU_SND_NO_A,
	   "Auto Off stops at its ends, so it is heard after, and A does nothing on it");
	ck(menu_sound_at(b.sound, n, PM_SOUNDS) == MENU_SND_OWN, "UI Sounds plays its own");
	ck(!menu_sound_at(b.sound, n, PM_WIFI) && !menu_sound_at(b.sound, n, PM_STATS) &&
	   !menu_sound_at(b.sound, n, PM_ABOUT), "a row that opens a screen is plain");
	ck(!menu_sound_at(b.sound, n, n) && !menu_sound_at(b.sound, n, -1),
	   "and so is a place past the rows");

	/* On a device with no radio the rows move up; the classes move with them. */
	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0; i < n; i++)
		if (!strcmp(rows[i].label, "UI Sounds"))
			ck(b.sound[i] == MENU_SND_OWN, "a row's class goes where the row goes");

	/* The rows that carry a user's text keep their own class. */
	memset(&u, 0, sizeof u);
	u.wifi = WIFI_CONNECTED;
	u.ssid = "Display Mode";
	u.bt_name = "UI Sounds";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!menu_sound_at(b.sound, n, PM_WIFI) && !menu_sound_at(b.sound, n, PM_BT),
	   "a network named Display Mode and a headset named UI Sounds leave their rows plain");

	memset(&u, 0, sizeof u);
	u.games = true;
	u.sys_name = "DC"; u.sys_core = "Flycast"; u.core_choice = true;
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(menu_sound_at(b.sound, n, sm_at(&u, SM_CORE)) == MENU_SND_RING, "a system's Core steps round, on A too");
	ck(menu_sound_at(b.sound, n, sm_at(&u, SM_FAVFIRST)) == MENU_SND_RING,
	   "Favorites First turns over on A too, as Look's two-way rows");
	ck(menu_sound_at(b.sound, n, sm_at(&u, SM_SORT)) == (MENU_SND_RING | MENU_SND_NO_A),
	   "Sort By steps round and does nothing with A");
	ck(menu_sound_at(b.sound, n, sm_at(&u, SM_DISPLAY)) == (MENU_SND_RING | MENU_SND_NO_A),
	   "and Display Mode the same");
	ck(!menu_sound_at(b.sound, n, sm_at(&u, SM_GAMES)) && !menu_sound_at(b.sound, n, sm_at(&u, SM_RESCAN)),
	   "the count and Rescan are plain");

	u.fav = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == SM_FAV_ROWS && menu_sound_at(b.sound, n, 2) == (MENU_SND_RING | MENU_SND_NO_A) &&
	   !menu_sound_at(b.sound, n, 0) && menu_sound_at(b.sound, n, 1) == MENU_SND_OWN,
	   "Favorites' Sort By too, its count plain, Random Game heard by its pick");
	/* Recents' fifth, Card Picture (DESIGN.md 11.48): Cover as it ships */
	u.recents = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == SM_RECENTS_ROWS && !strcmp(rows[4].label, "Card Picture") && !strcmp(val(&rows[4]), "Cover") &&
	   rows[4].live && menu_sound_at(b.sound, n, 4) == MENU_SND_RING,
	   "Recents has Card Picture under View, Cover as it ships, stepped round");
	u.recents_screen = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[4]), "Last Screen"), "and Last Screen when it is on");
	u.recents = u.recents_screen = false;

	/* Collections (src/collect.h): under the collection's name, its top's
	 * four, and in one its count, Random Game and Remove from Collection */
	memset(&u, 0, sizeof u);
	u.games = true; u.coll = true; u.coll_name = "Couch Co-op"; u.sys_name = "Collections";
	u.game_count = 3;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == 4 && !strcmp(heading, "Couch Co-op") && !strcmp(rows[0].label, "Sort By") &&
	   !strcmp(val(&rows[0]), "Added") && !strcmp(rows[1].label, "View") && !strcmp(rows[2].label, "Rename") &&
	   !strcmp(rows[3].label, "Delete Collection") && rows[3].live,
	   "Collections' top: Sort By (Added), View, Rename, Delete Collection, under the collection's name");
	ck(menu_sound_at(b.sound, n, 0) == (MENU_SND_RING | MENU_SND_NO_A) && !menu_sound_at(b.sound, n, 2),
	   "its Sort By steps round as any shelf's, Rename is plain");
	u.coll_inside = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(n == SM_COLL_ROWS && !strcmp(rows[0].label, "Games") && !strcmp(val(&rows[0]), "3") && !rows[0].live &&
	   !strcmp(rows[1].label, "Random Game") && !strcmp(rows[5].label, "Remove from Collection") &&
	   rows[5].live && !strcmp(rows[6].label, "Delete Collection"),
	   "in one: its count, Random Game, Sort By, View, Rename, Remove from Collection, Delete Collection");
	u.game_count = 0;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!rows[1].live && !rows[5].live, "with no game, nothing to pick or take out");

	memset(&u, 0, sizeof u);
	u.games = true; u.mp4 = true; u.mp4_both = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	sys_menu_mp4_rows(false, true, false, mids);
	for (i = 0; i < n; i++) {
		if (mids[i] == SMM_SHOW)
			ck(b.sound[i] == MENU_SND_RING, "MP4's Show turns over");
		if (mids[i] == SMM_SORT)
			ck(b.sound[i] == (MENU_SND_RING | MENU_SND_NO_A),
			   "MP4's Sort By steps round and does nothing with A");
		if (mids[i] == SMM_VIEW)
			ck(b.sound[i] == MENU_SND_RING, "MP4's View turns over, on A too");
		if (mids[i] == SMM_LOCK)
			ck(!b.sound[i], "Lock Screen is plain");
	}

	/* The in-game menu's Display steps on A as well as left and right
	 * (gm_key's GM_DISPLAY), so A is accept there, not deny. Shader too. */
	g.dmode = "Native"; g.screens = true; g.quick = 3;
	for (int t = 0; t < GT_TABS; t++) {
		n = gm_rows(&g, (gm_tab)t, gr, &gb);
		gm_tab_rows(&g, (gm_tab)t, ids);
		for (i = 0; i < n; i++) {
			unsigned want = ids[i] == GM_DISPLAY || ids[i] == GM_SHADER || ids[i] == GM_QUICK ||
			                ids[i] == GM_SCREENS || ids[i] == GM_SWAP || ids[i] == GM_STYLUS ||
			                ids[i] == GM_PAK || ids[i] == GM_HUD
			                ? MENU_SND_RING : 0;

			ck(menu_sound_at(gb.sound, n, i) == want,
			   "in-game: Display, Shader, Quick Slot, the screens' rows and On-Screen Stats step round; the rest plain");
		}
	}

	/* The Wi-Fi screen's rows are networks, named by whoever runs them, and
	 * its build sets no classes: a network called Core is a network. */
	memset(&w, 0, sizeof w);
	w.on = true;
	w.scanned = true;
	w.n = 2;
	snprintf(w.nets[0].ssid, sizeof w.nets[0].ssid, "Core");
	snprintf(w.nets[1].ssid, sizeof w.nets[1].ssid, "Sort By");
	wifi_label(&w);
	n = wifi_build(&w, wr, 64, &heading);
	ck(!strcmp(wr[1].label, "Core") && !strcmp(wr[2].label, "Sort By"),
	   "networks named Core and Sort By are listed");
	ck(!menu_sound_at(NULL, n, 1) && !menu_sound_at(NULL, n, 2),
	   "and play as any network does: the Wi-Fi screen has no classes to read");
}

/* An option row's choice is reached by its own Left and Right, straight
 * there and never round past an end (menu_choice_steps), and a build's
 * table is copied to its rows' places (sys_menu_build) */
static void option_choices(void)
{
	static const char *const lab[4] = { "A", "B", "C", "D" };
	menu_choice c = { .n = 4, .cur = 1, .labels = lab }, t[3] = { { 0 } };
	menu_choice pm[PM_ROWS];
	menu_bufs b;
	menu_row out[MENU_MAX_ROWS];
	const char *heading;
	sys_ui u = { 0 };
	bool right;
	int i, n, found = 0;

	ck(menu_choice_steps(&c, 3, &right) == 2 && right, "two to the right, straight there");
	ck(menu_choice_steps(&c, 0, &right) == 1 && !right, "one to the left, never round past the end");
	ck(menu_choice_steps(&c, 1, &right) == 0, "the one that is on: nothing to send");
	c.none = true;
	ck(menu_choice_steps(&c, 1, &right) == 2, "none on as a whole: right and back sets it for all");
	ck(menu_choice_steps(&c, 9, &right) == 0, "a choice past the end: nothing");
	t[1] = c;
	ck(menu_choice_at(t, 3, 1) == &t[1] && !menu_choice_at(t, 3, 0) && !menu_choice_at(t, 3, 5) &&
	   !menu_choice_at(NULL, 3, 1), "a row's choices by place, none for a row with no table");
	memset(pm, 0, sizeof pm);
	pm[PM_SAVER] = (menu_choice){ .n = 4, .cur = 2, .labels = lab };
	u.pm_choice = pm;
	n = sys_menu_build(&u, out, &b, &heading);
	for (i = 0; i < n; i++)
		if (b.choice[i].n == 4 && b.choice[i].cur == 2) found = !strcmp(out[i].label, T(S_SYSMENU_SCREENSAVER));
	ck(found, "MENU's table reaches the Screensaver row's place");
}

int main(void)
{
	option_choices();
	off_state();
	scanning_state();
	scanned_state();
	wifi_footer_is_a_legend();
	strength_words();
	respects_max();
	zyperos_menu_offline();
	look_rows();
	clock_row();
	zyperos_menu_online();
	wifi_row_wording();
	system_menu();
	auto_off_words();
	saver_row();
	cursor_reaches();
	radio_less();
	dslang_row();
	slots();
	save_slot();
	step_terminates();
	step_does_not_wrap();
	step_goes_round();
	menu_tabs();
	source_does_not_wrap();
	window_returns();
	what_scrolls();
	info_rows();
	info_file_rows();
	gi_buttons_row_only_when_own();
	ingame_rows();
	quick_slot();
	n64_pak();
	ctl_tab();
	player2_row_on_nes_only();
	gm_disc_row_only_with_discs();
	gm_undo_row_only_with_undo();
	gm_display_caption();
	gm_screen_colors_row();
	gm_port_waiting();
	disc_value_check();
	audio_row();
	channels_row();
	sounds_row();
	rumble_row();
	charge_row();
	power_row();
	homesys_rows();
	shots_menu();
	lights_row();
	row_sounds();
	if (fails) { printf("\n%d menu check(s) failed\n", fails); return 1; }
	printf("\nok: menus contain what they should\n");
	return 0;
}
