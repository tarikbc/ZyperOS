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
#include "../src/game_menu.h"
#include "../src/controls.h"
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
	ck(!strcmp(rows[0].value, "off"), "the switch reads off");
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
	ck(!strcmp(rows[1].value, "saved"), "no signal word before the scan lands");
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
	ck(!strcmp(rows[1].value, "strong"), "-55 dBm reads as strong");
	ck(!strcmp(legend_text(rows[3].label), "Y: rescan"), "no forget offered with nothing saved");

	w.nets[0].known = true;
	wifi_label(&w);
	n = wifi_build(&w, rows, 64, &heading);
	ck(!strcmp(legend_text(rows[3].label), "Y: rescan   X: forget"), "forget offered once saved");
	ck(!strcmp(rows[1].value, "strong - saved"), "a saved network in range shows both");
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
	ck(!strcmp(wifi_strength(-40), "strong"), "-40 strong");
	ck(!strcmp(wifi_strength(-60), "strong"), "-60 is the strong boundary");
	ck(!strcmp(wifi_strength(-61), "good"),   "-61 good");
	ck(!strcmp(wifi_strength(-72), "good"),   "-72 is the good boundary");
	ck(!strcmp(wifi_strength(-73), "weak"),   "-73 weak, the measured cliff");
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

static const char *val(const menu_row *r) { return r->value ? r->value : ""; }
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
	ck(!strcmp(val(&rows[PM_WIFI]), "off"), "Wi-Fi reads off");
	ck(!strcmp(val(&rows[PM_XFER]), "needs Wi-Fi"), "OTH says why it is dead");
	ck(!rows[PM_XFER].live, "OTH is not selectable offline");
	ck(!strcmp(rows[PM_SCRAPER].label, "Scraper") && rows[PM_SCRAPER].live,
	   "Scraper is live offline: its Video Snaps and sources need no network");
	ck(!strcmp(val(&rows[PM_ACHIEVEMENTS]), "sign in"), "Achievements invites a sign in");
	ck(rows[PM_ACHIEVEMENTS].live, "Achievements is reachable signed out");
	ck(!strcmp(val(&rows[PM_SLEEP]), "2m"), "120s reads as 2m");
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
	ck(!strcmp(val(&rows[PM_AUDIO]), "auto (speaker)"),
	   "and Auto names where it landed rather than only saying Auto");
	/* Directly under it, and as reachable: someone who hears with one ear
	 * needs it with no radio and no cable as much as with both. */
	ck(PM_CHANNELS == PM_AUDIO + 1, "Audio Channels sits under Audio Output");
	ck(!strcmp(rows[PM_CHANNELS].label, "Audio Channels") && rows[PM_CHANNELS].live,
	   "Audio Channels is reachable offline");
	ck(!strcmp(val(&rows[PM_CHANNELS]), "stereo"), "and reads stereo until it is set");
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
		"stereo", "mono", "mono left", "mono right", "auto (stereo)",
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
	ck(!strcmp(val(&rows[PM_CHANNELS]), "auto (mono)"), "Auto names the mix it makes");
}

/* The row has to say a PLACE. "Auto" alone makes the player guess which of
 * three they are about to hear. */
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
	ck(!strcmp(val(&rows[PM_AUDIO]), "auto (wired)"), "auto, on a cable");

	u.audio_dest = AOUT_BT;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "auto (bluetooth)"), "auto, on a headset");

	/* Pinned says the place with no "auto", because there is no rule left to
	 * describe - it is just where the sound is. */
	u.audio_policy = AOUT_SPEAKER;
	u.audio_dest = AOUT_SPK;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "speaker"), "pinned reads as the place");

	/* Pinned to Speaker with a cable in is still the cable: the pin refuses
	 * Bluetooth, never the jack. */
	u.audio_dest = AOUT_WIRED;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_AUDIO]), "wired"),
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

	/* Shader, every system's (2026-11-12): on Display, the tab of how
	 * things look, last, after Screensaver. Off with none said; the
	 * shader's name, or "per system", as given. */
	printf("MENU's Shader row:\n");
	ck(sys_menu_tab_of(PM_SHADER) == PT_DISPLAY && PM_SHADER == PM_SAVER + 1,
	   "Shader is on Display, after Screensaver");
	ck(!strcmp(rows[PM_SHADER].label, "Shader") && !strcmp(val(&rows[PM_SHADER]), "Off") &&
	   rows[PM_SHADER].live, "Shader, Off when nothing is said, a stop");
	ck(b.sound[PM_SHADER] == MENU_SND_RING, "it steps round, on A too");
	u.shader_all = "per system";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_SHADER]), "per system"), "and says what it is given");
	u.shader_all = NULL;

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
	   "Scraper is live, with no value before a count");
	ck(!strcmp(val(&rows[PM_ACHIEVEMENTS]), "eric"), "Achievements shows the account");
	ck(!strcmp(val(&rows[PM_SLEEP]), "never"), "0s reads as never");

	/* ONE ROW, SCRAPER, where Box Art was: the run, Video Snaps and the
	 * three sources behind it (My picks in the Scraper lab, 2026-11-06;
	 * DESIGN.md 11.19). The two account rows it replaces are gone. */
	printf("the Scraper row and its screen:\n");
	ck(PM_SCRAPER == PM_ACHIEVEMENTS + 1 && sys_menu_tab_of(PM_SCRAPER) == PT_GAMES,
	   "Scraper is in Games, under Achievements");
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
		scraper_ui su;
		scraper_bufs sb;
		menu_row sr[SC_ROWS];
		sc_row ids[SC_ROWS];
		int m, j, src = 0;

		memset(&su, 0, sizeof su);
		su.net = true;
		su.missing = 142;
		su.snaps = "off \xc2\xb7 0 KB";
		m = sys_menu_scraper_rows(&su, &sb, sr, ids);
		ck(m <= SC_ROWS, "the screen fits SC_ROWS");
		ck(m == 6, "six rows and a rule, what the panel holds at 640x480");
		ck(ids[0] == SC_FETCH && !strcmp(sr[0].label, "Fetch Missing Art") &&
		   !strcmp(val(&sr[0]), "142 missing") && sr[0].live, "Fetch Missing Art first, with the count");
		ck(ids[1] == SC_SNAPS && !strcmp(sr[1].label, "Video Snaps") && sr[1].live,
		   "Video Snaps under it");
		for (j = 0; j < m; j++) {
			if (ids[j] == SC_SS) ck(src++ == 0, "ScreenScraper is asked first");
			if (ids[j] == SC_LIBRETRO) ck(src++ == 1 && !sr[j].live &&
			                              !strcmp(val(&sr[j]), "no account"),
			                              "then libretro, which needs nothing");
			if (ids[j] == SC_SGDB) ck(src++ == 2 && sr[j].live && !strcmp(val(&sr[j]), "link"),
			                          "then SteamGridDB, which invites a link");
			if (ids[j] == SC_SS) ck(!sr[j].live && !strcmp(val(&sr[j]), "not in this build"),
			                        "a build without ScreenScraper's key says so");
		}
		ck(src == 3, "all three sources are there");
		su.ss_have = su.ss_in = true;
		su.ss_name = "tarik";
		su.sgdb = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b";
		m = sys_menu_scraper_rows(&su, &sb, sr, ids);
		for (j = 0; j < m; j++) {
			if (ids[j] == SC_SS) ck(!strcmp(val(&sr[j]), "linked as tarik") && sr[j].live,
			                        "a linked account says who, and opens its page");
			if (ids[j] == SC_SGDB) ck(!strcmp(val(&sr[j]), su.sgdb), "the key, masked as given");
		}
		su.net = false;
		m = sys_menu_scraper_rows(&su, &sb, sr, ids);
		ck(!strcmp(val(&sr[0]), "needs Wi-Fi") && !sr[0].live && sr[1].live,
		   "offline the fetch says why, and Video Snaps still opens");
		su.net = true;
		su.running = true;
		su.done = 31;
		su.total = 142;
		su.ss_found = 12; su.lr_found = 17; su.sg_found = 2;
		m = sys_menu_scraper_rows(&su, &sb, sr, ids);
		ck(!strcmp(sr[0].label, "Fetching") && !strcmp(val(&sr[0]), "31 / 142"),
		   "running, the first row is its progress");
		for (j = 0; j < m; j++) {
			if (ids[j] == SC_SS) ck(!strcmp(val(&sr[j]), "12 covers") && !sr[j].live,
			                        "and each source counts what it found");
			if (ids[j] == SC_SGDB) ck(!strcmp(val(&sr[j]), "2 covers") && !sr[j].live, "SteamGridDB too");
			ck(sr[j].value != MENU_NOTE_MARK, "no notes: six rows are what the panel holds");
		}
	}
	{
		menu_row pr[SSP_ROWS];
		ssp_row pid[SSP_ROWS];
		int m = sys_menu_ss_rows(true, NULL, NULL, pr, pid), j, unlink = 0;

		ck(pid[0] == SSP_ACCOUNT && !strcmp(val(&pr[0]), "none") && pr[0].live,
		   "ScreenScraper's page: Account, none, live to link one");
		for (j = 0; j < m; j++) if (pid[j] == SSP_UNLINK) unlink++;
		ck(!unlink, "and nothing to unlink");
		m = sys_menu_ss_rows(true, "tarik", "works", pr, pid);
		for (j = 0; j < m; j++) if (pid[j] == SSP_UNLINK) unlink++;
		ck(!strcmp(val(&pr[0]), "tarik") && unlink == 1, "linked: the account, and Unlink");
		m = sys_menu_ss_rows(false, NULL, NULL, pr, pid);
		ck(!pr[0].live && !strcmp(val(&pr[0]), "not in this build"), "a build without its key says so");
	}
	{
		menu_row sr[SG_ROWS];
		sg_row ids[SG_ROWS];
		const char *masked = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b";
		int m = sys_menu_sgdb_rows(NULL, NULL, sr, ids), j, notes = 0;

		ck(m <= SG_ROWS, "SteamGridDB's page fits SG_ROWS");
		ck(ids[0] == SG_KEY && !strcmp(sr[0].label, "Key") && !strcmp(val(&sr[0]), "none")
		   && sr[0].live, "SteamGridDB's page with no key: Key, none, live to type one");
		for (j = 0; j < m; j++) {
			ck(ids[j] != SG_REMOVE && ids[j] != SG_STATUS, "and nothing to unlink");
			if (sr[j].value == MENU_NOTE_MARK) notes++;
		}
		ck(notes == 2, "two notes say where a key comes from");
		m = sys_menu_sgdb_rows(masked, "works", sr, ids);
		ck(m <= SG_ROWS && ids[1] == SG_STATUS && !strcmp(val(&sr[1]), "works") &&
		   !sr[1].live, "with a key: its status, which is not a button");
		ck(ids[2] == SG_REMOVE && !strcmp(sr[2].label, "Unlink") && sr[2].live,
		   "and Unlink, live");
	}
	{
		snaps_console c[2] = { { "SNES", true, 212000000ull }, { "Game Boy", false, 0 } };
		snaps_ui vu = { .fetch = false, .play = true, .ss_in = false, .ss_have = true,
		                .sound = SNAP_MUTED,
		                .c = c, .nc = 2, .total = 212000000ull };
		static snaps_bufs vb;
		menu_row vr[SN_ROWS];
		sn_row vid[SN_ROWS];
		int con[SN_ROWS], m = sys_menu_snaps_rows(&vu, &vb, vr, vid, con);

		printf("the Video Snaps page:\n");
		ck(vid[0] == SN_FETCH && !vr[0].live && !strcmp(val(&vr[0]), "needs ScreenScraper"),
		   "fetching needs ScreenScraper, and says so");
		ck(vid[1] == SN_PLAY && !strcmp(val(&vr[1]), "on") && vr[1].live, "playing is on to start");
		ck(vid[2] == SN_SOUND && !strcmp(val(&vr[2]), "muted"), "and muted to start");
		ck(vid[4] == SN_CONSOLE && con[4] == 0 && !strcmp(vr[4].label, "SNES") &&
		   !strcmp(val(&vr[4]), "on \xc2\xb7 212 MB"), "a console with its room and its switch");
		ck(vid[5] == SN_CONSOLE && !strcmp(val(&vr[5]), "off \xc2\xb7 0 KB"), "one turned off");
		ck(vid[m - 1] == SN_DELETE && !strcmp(val(&vr[m - 1]), "212 MB") && vr[m - 1].live,
		   "Delete All last, with what it gives back");
		vu.ss_in = true;
		m = sys_menu_snaps_rows(&vu, &vb, vr, vid, con);
		ck(vr[0].live && !strcmp(val(&vr[0]), "off"), "with ScreenScraper, fetching is a switch, off to start");
		vu.ss_have = false;
		m = sys_menu_snaps_rows(&vu, &vb, vr, vid, con);
		ck(!vr[0].live && !strcmp(val(&vr[0]), "not in this build"),
		   "a build without ScreenScraper's developer pair says so, plainly");
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
	ck(!strcmp(val(&rows[PM_DEVELOPER]), "off"), "off says off");
	u.developer = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_DEVELOPER]), "on"), "on says on");
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
	ck(!strcmp(val(&rows[PM_WIFI]), "connecting"), "connecting");

	u.wifi = WIFI_IDLE;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[PM_WIFI]), "not connected"), "up but unassociated");

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
	n = sys_menu_build(&u, rows, &b, &heading);

	printf("system menu:\n");
	ck(n == SM_ROWS - 1, "every row is filled, but Game Language, which only the Nintendo DS has");
	ck(!strcmp(heading, "Game Boy"), "heading is the system, not ZyperOS");
	ck(!strcmp(val(&rows[sm_at(&u, SM_GAMES)]), "412"), "the count is the real one");
	ck(!strcmp(val(&rows[sm_at(&u, SM_CORE)]), "gambatte"), "the core is the real one");
	ck(!strcmp(val(&rows[sm_at(&u, SM_DISPLAY)]), "Sharp"), "display mode is passed through");
	ck(sm_at(&u, SM_SHADER) == sm_at(&u, SM_DISPLAY) + 1 &&
	   !strcmp(rows[sm_at(&u, SM_SHADER)].label, "Shader") &&
	   !strcmp(val(&rows[sm_at(&u, SM_SHADER)]), "Off") && rows[sm_at(&u, SM_SHADER)].live,
	   "Shader right after Display Mode, Off when nothing is said, a stop");
	ck(b.sound[sm_at(&u, SM_SHADER)] == MENU_SND_RING, "and it steps round, on A too");
	u.shader = "Sharp-Shimmerless";
	sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_SHADER)]), "Sharp-Shimmerless"), "the shader is passed through");
	u.shader = NULL;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(rows[sm_at(&u, SM_DISPLAY)].live && rows[sm_at(&u, SM_RESCAN)].live, "the two live rows are live");
	ck(!rows[sm_at(&u, SM_GAMES)].live && !rows[sm_at(&u, SM_CORE)].live, "reported facts are not rows to press");
	ck(rows[sm_at(&u, SM_BOXART)].live, "Box Art is live on a network");

	/* A shelf that lists two cores, the Dreamcast's: Core is a choice. */
	u.sys_core = "Flycast";
	u.core_choice = true;
	sys_menu_build(&u, rows, &b, &heading);
	ck(rows[sm_at(&u, SM_CORE)].live && !strcmp(val(&rows[sm_at(&u, SM_CORE)]), "Flycast"),
	   "two cores make Core a row to press, naming the one in use");
	u.core_choice = false;
	u.sys_core = "gambatte";

	/* The same menu with the radio down. Only Box Art changes. */
	u.wifi = WIFI_OFF;
	sys_menu_build(&u, rows, &b, &heading);
	ck(!rows[sm_at(&u, SM_BOXART)].live, "Box Art dies with the radio");
	ck(!strcmp(val(&rows[sm_at(&u, SM_BOXART)]), "needs Wi-Fi"), "and says why");
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

	/* Muse, which is not a system either: no core, no ROM folder, no display
	 * mode. What it does have is a folder of music, two orders to see it in,
	 * and Rescan Folder for music copied in. */
	{
		sys_ui m = u;
		menu_row mrows[MENU_MAX_ROWS];
		menu_bufs mb;
		const char *mhead = NULL;
		int mn, k;

		m.muse = true;
		m.sys_name = "MP4";
		m.sys_core = "";
		m.game_count = 7;
		m.sort = NULL;
		m.wifi = WIFI_CONNECTED;     /* the case above left the radio down */
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		printf("MP4's shelf menu:\n");
		ck(mn == 6, "six rows with only music");
		ck(!strcmp(mrows[0].label, "Albums") && !strcmp(val(&mrows[0]), "7"),
		   "how many albums");
		ck(!strcmp(mrows[1].label, "Sort By") && mrows[1].live &&
		   !strcmp(val(&mrows[1]), "Artist"),
		   "Sort By, and by artist when nothing says otherwise");
		ck(!strcmp(mrows[2].label, "Shuffle All") && mrows[2].live && !val(&mrows[2])[0],
		   "Shuffle All, live whether or not anything plays");
		ck(!strcmp(mrows[3].label, "Lock Screen") && !mrows[3].live &&
		   !strcmp(val(&mrows[3]), "while playing"),
		   "Lock Screen, dead and saying why with nothing playing");
		ck(!strcmp(mrows[4].label, "Album Art") && mrows[4].live,
		   "covers to fetch, on a network");
		ck(!strcmp(mrows[5].label, "Rescan Folder") && mrows[5].live,
		   "and a rescan that works");
		m.muse_playing = true;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mrows[3].live && !val(&mrows[3])[0], "Lock Screen live while music plays");
		m.muse_playing = false;
		m.sort = "Album";
		m.wifi = WIFI_OFF;
		sys_menu_build(&m, mrows, &mb, &mhead);
		ck(!strcmp(val(&mrows[1]), "Album"), "the order it is in");
		ck(!mrows[4].live && !strcmp(val(&mrows[4]), "needs Wi-Fi"),
		   "Album Art says why it cannot, off the network");
		ck(mrows[1].live && mrows[2].live && mrows[5].live,
		   "and neither Sort By, Shuffle All nor Rescan Folder needs one");
		for (k = 0; k < mn; k++) {
			ck(strcmp(lbl(&mrows[k]), "Core") != 0, "no Core: it has none");
			ck(strcmp(lbl(&mrows[k]), "Display Mode") != 0, "no Display Mode");
		}

		/* Music and books on one card: Show, between the count and the
		 * order, and on books no Album Art and the orders a book goes by. */
		m.sort = NULL;
		m.wifi = WIFI_CONNECTED;
		m.muse_both = true;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == SM_MUSE_ROWS, "seven rows with more than one kind");
		ck(!strcmp(mrows[1].label, "Show") && mrows[1].live &&
		   !strcmp(val(&mrows[1]), "Music"), "Show, on music");
		ck(!strcmp(mrows[2].label, "Sort By") && !strcmp(mrows[3].label, "Shuffle All") &&
		   mrows[3].live && !strcmp(mrows[4].label, "Lock Screen") &&
		   !strcmp(mrows[5].label, "Album Art"),
		   "Sort By, Shuffle All, Lock Screen, then Album Art");
		m.muse_books = true;
		m.game_count = 2;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == 5, "five rows on books");
		ck(!strcmp(mrows[0].label, "Books") && !strcmp(val(&mrows[0]), "2"),
		   "how many books");
		ck(!strcmp(val(&mrows[1]), "Audiobooks"), "Show, on books");
		ck(!strcmp(val(&mrows[2]), "Author"), "by author when nothing says otherwise");
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0,
			   "no Album Art on books (MusicBrainz knows records), no Shuffle All");
		/* Videos: their own count and orders, and like books no Album Art
		 * and no Shuffle All. */
		m.muse_books = false;
		m.muse_videos = true;
		m.game_count = 3;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == 5 && !strcmp(mrows[0].label, "Videos") && !strcmp(val(&mrows[0]), "3") &&
		   !strcmp(val(&mrows[1]), "Videos") && !strcmp(val(&mrows[2]), "Folder"),
		   "on videos: how many, Show, by folder");
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0,
			   "no Album Art and no Shuffle All on videos");
		/* All, where Show starts: the count is the Total, the orders the
		 * videos', and Shuffle All and Album Art when there are songs */
		m.muse_videos = false;
		m.muse_all = true;
		m.muse_songs = true;
		m.game_count = 7;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		ck(mn == SM_MUSE_ROWS && !strcmp(mrows[0].label, "Total") &&
		   !strcmp(val(&mrows[0]), "7") && !strcmp(val(&mrows[1]), "All") &&
		   !strcmp(val(&mrows[2]), "Folder") && !strcmp(mrows[3].label, "Shuffle All") &&
		   !strcmp(mrows[5].label, "Album Art"),
		   "on All: the Total, Show All, by folder, Shuffle All and Album Art");
		m.muse_songs = false;
		mn = sys_menu_build(&m, mrows, &mb, &mhead);
		for (k = 0; k < mn; k++)
			ck(strcmp(lbl(&mrows[k]), "Album Art") != 0 &&
			   strcmp(lbl(&mrows[k]), "Shuffle All") != 0,
			   "All with no songs: no Album Art and no Shuffle All");
		m.muse_all = false;
		m.muse_books = true;
		{
			sm_muse_row ids[SM_MUSE_ROWS];

			ck(sys_menu_muse_rows(3, true, false, ids) == SM_MUSE_ROWS && ids[3] == SMM_SHUFFLE,
			   "All with songs has every row");

			ck(sys_menu_muse_rows(true, false, false, ids) == 4 && ids[1] == SMM_SORT &&
			   ids[2] == SMM_LOCK,
			   "books alone: the count, Sort By, Lock Screen and Rescan Folder");
		}
	}
}

/* The labels, on their own. A row that reads "90s" for a minute and a half
 * would be wrong in a way no screenshot makes obvious. */
static void auto_off_words(void)
{
	char s[16];
	struct { int sec; const char *want; } t[] = {
		{ 0, "never" }, { 30, "30s" }, { 60, "1m" },
		{ 120, "2m" }, { 300, "5m" }, { 600, "10m" },
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
	u.game_count = 20; u.dmode = "Native";
	np = sys_menu_build(&u, plain, &b, &heading);

	printf("system menu, Game Language:\n");
	ck(!has_label(plain, np, "Game Language"), "no Game Language row unless the shelf says so");
	ck(np == SM_ROWS - 1, "and every other row is there");
	ck(!plain[sm_at(&u, SM_CORE)].live && !strcmp(val(&plain[sm_at(&u, SM_CORE)]), "dsperate"),
	   "one core: the DS's Core row names DSperate and is no row to press");

	u.dslang = "Auto (English)";
	n = sys_menu_build(&u, rows, &b, &heading);
	at = sm_at(&u, SM_DSLANG);
	ck(n == np + 1 && n == SM_ROWS, "one row more");
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
	ck(k == 7, "seven rows, not the facts");
	ck(holds(got, k, sm_at(&u, SM_RANDOM)), "Random Game, with games on the shelf");
	/* 1.0.5: the shelf's covers or a list, under Sort By */
	ck(holds(got, k, sm_at(&u, SM_VIEW)), "View");
	ck(sm_at(&u, SM_VIEW) == sm_at(&u, SM_SORT) + 1, "right under Sort By");
	ck(!strcmp(val(&rows[sm_at(&u, SM_VIEW)]), "Covers"), "the covers, as it always was");
	u.listed = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(!strcmp(val(&rows[sm_at(&u, SM_VIEW)]), "List"), "or a list");
	u.listed = false;
	n = sys_menu_build(&u, rows, &b, &heading);
	ck(holds(got, k, sm_at(&u, SM_DISPLAY)), "Display Mode");
	ck(holds(got, k, sm_at(&u, SM_SHADER)), "Shader");
	/* Live since sorting stopped being alphabetical-only. It was a
	 * placeholder for long enough that this check counted three. */
	ck(holds(got, k, sm_at(&u, SM_SORT)), "Sort By");
	ck(holds(got, k, sm_at(&u, SM_BOXART)), "Box Art");
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
		ck(hr[PM_XFER].live && !strcmp(val(&hr[PM_XFER]), "hotspot"),
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
	sm_muse_row mu[SM_MUSE_ROWS];
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
	for (i = 0, ok = true; i < k; i++) if (sm[i] != (sm_row)(i + (i >= SM_DSLANG))) ok = false;
	ck(k == SM_ROWS - 1 && ok, "every system row but Game Language, in the enum's order");

	u.no_wifi = true;
	u.no_bt = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	printf("ZyperOS menu, no radio at all:\n");
	ck(n == k && n == PM_ROWS - 4, "four rows fewer, and the build agrees with the list");
	ck(!has_label(rows, n, "Wi-Fi") && !has_label(rows, n, "Bluetooth"),
	   "no Wi-Fi, no Bluetooth");
	ck(!has_label(rows, n, "Wi-Fi File Transfer") && has_label(rows, n, "Scraper"),
	   "no Wi-Fi File Transfer; Scraper stays, for Video Snaps and the sources");
	ck(!has_label(rows, n, "Achievements"), "and no Achievements sign-in");
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
	ck(n == k && n == PM_ROWS - 3, "three rows fewer: the transfer row stays");
	for (i = 0; i < k && pm[i] != PM_XFER; i++) { }
	ck(i < k && !strcmp(rows[i].label, "USB File Transfer"),
	   "as USB File Transfer, where the list says it is");
	ck(i < k && rows[i].live && !rows[i].value, "live, and not asking for Wi-Fi");
	ck(!has_label(rows, n, "Wi-Fi File Transfer"), "never under its Wi-Fi name");

	memset(&u, 0, sizeof u);
	u.games = true;
	u.no_wifi = true;
	u.sys_name = "NES"; u.sys_core = "nestopia";
	u.game_count = 3; u.dmode = "Native";
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_system_rows(&u, sm);
	printf("system menu, no radio at all:\n");
	ck(n == k && n == SM_ROWS - 2, "Box Art goes and nothing else");
	ck(!has_label(rows, n, "Box Art"), "no Box Art");
	ck(sm[n - 1] == SM_RESCAN && !strcmp(rows[n - 1].label, "Rescan Folder"),
	   "Rescan Folder moves up into its place");

	k = sys_menu_muse_rows(false, false, true, mu);
	for (i = 0, ok = true; i < k; i++) if (mu[i] == SMM_ART) ok = false;
	ck(ok, "Muse loses Album Art too");
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
	ck(m == 8 && tab[0] == PM_APPEARANCE && tab[1] == PM_AUTOHOURS && tab[2] == PM_THEME &&
	   tab[3] == PM_HOME && tab[4] == PM_ORDER && tab[5] == PM_BATTERY &&
	   tab[6] == PM_SAVER && tab[7] == PM_SHADER,
	   "Display: Appearance, Auto Hours, Theme, Home Screen, System Order, Battery Icon, Screensaver, Shader");
	m = sys_menu_tab_rows(&u, PT_GAMES, tab);
	ck(m == 5 && tab[0] == PM_SEARCH, "Games opens on Search Games");
	m = sys_menu_tab_rows(&u, PT_CONNECTIONS, tab);
	ck(m > 0 && tab[0] == PM_USB_WAIT, "Connections starts on USB Device while a plug waits");
	u.no_usb_wait = true;
	m = sys_menu_tab_rows(&u, PT_CONNECTIONS, tab);
	ck(m > 0 && tab[0] == PM_WIFI, "and on Wi-Fi when none does");
	u.tab = PT_SOUND;
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(n == 4 && !strcmp(heading, "Sound") && !strcmp(rows[0].label, "Audio Output") &&
	   !strcmp(rows[3].label, "Menu Music"), "a tab's build: its rows, headed by its name");
	u.tab = PT_DISPLAY;
	u.auto_hours = "7:00 AM to 7:00 PM";
	u.theme = "Lime";
	n = (sys_menu_build)(&u, rows, &b, &heading);
	ck(n == 8 && !strcmp(rows[1].label, "Auto Hours") && !strcmp(val(&rows[1]), "7:00 AM to 7:00 PM") &&
	   !strcmp(rows[2].label, "Theme") && !strcmp(val(&rows[2]), "Lime"),
	   "Auto Hours says its hours, Theme its accent");
	ck(!strcmp(sys_menu_tab_name(PT_CONNECTIONS), "Connections") &&
	   !strcmp(sys_menu_tab_name(PT_ABOUT), "About"), "the tabs' names");
}

/* The shared engine is the only place a menu list goes round by arithmetic:
 * every panel with rows goes through menu_step_round and menu_step_sel, and
 * the loops of their own through menu_step_index (list_step_heard). A `% n`
 * on the cursor in main.c on the up or down press would be a second rule, so
 * the source is read for it. */
static void source_does_not_wrap(void)
{
	static const char *const bad[] = {
		"(sel + 1) % n", "(sel + n - 1) % n", "(sel + 1) % items",
		"(sel + items - 1) % items", "(sel == 1) ? 2 : 1",
	};
	FILE *f = fopen("src/main.c", "r");
	char line[512];
	int ln = 0, hits = 0;
	size_t i;

	printf("no menu loop goes round by hand:\n");
	ck(f != NULL, "src/main.c is there to read");
	while (f && fgets(line, sizeof line, f)) {
		ln++;
		for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
			if (strstr(line, bad[i])) { printf("  src/main.c:%d: %s", ln, line); hits++; }
	}
	if (f) fclose(f);
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
 * What is NOT here is as deliberate as what is: no File, no Size, no Box Art
 * byte count, no Favorite, which Y does from the shelf, and no Saves, which
 * the carousel shows with the frames themselves. A row is easy to put back by
 * habit. */
/* Every row of a scraped game but Rename: GI_MAX less that one */
#define INFO_FULL (GI_MAX - 1)

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
	ck(!strcmp(val(&rows[1]), "no video"), "says what is missing: the video");
	ck(rows[1].live, "and opens its page");
	ck(!strcmp(rows[2].label, "Delete Game"), "Delete Game last");
	ck(rows[2].live && !rows[2].value, "live, with no value: the question says what goes");
	for (i = 0; i < n; i++) {
		ck(strcmp(lbl(&rows[i]), "File") != 0, "no File row");
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
	ck(!strcmp(val(&rows[1]), "no cover"), "and says the cover is what is missing");
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
	ck(!strcmp(val(&rows[0]), "none"), "Synopsis says none");
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
		rg.scraped = true;
		rg.has_synopsis = true;
		snprintf(rg.year, sizeof rg.year, "1995");
		snprintf(rg.genre, sizeof rg.genre, "Platform");
		rn = gi_rows(rr, &rg, true);
		printf("game info, a game that can be renamed:\n");
		ck(rn == GI_MAX, "every row at once fits GI_MAX");
		for (j = 0; j < rn && strcmp(rr[j].label, "Rename"); j++) {}
		ck(j == rn - 2 && rr[j].live && !rr[j].value, "Rename, live, just above Delete Game");
		ck(!strcmp(rr[rn - 1].label, "Delete Game"), "and Delete Game still last");
	}
}


/* ---------- the in-game menu ---------------------------------------------- */

/* Achievements is the only row that can be dead, and it is dead exactly when the
 * game has no set. Everything else is always something A does. */
static void ingame_rows(void)
{
	gm_ui u = { 0 };
	gm_bufs b;
	menu_row rows[GM_MAX_ROWS];
	gm_row ids[GM_MAX_ROWS];
	int got[GM_MAX_ROWS], n, k;

	u.dmode = "Native"; u.earned = 12; u.total = 40; u.quick = 2;
	n = gm_rows(&u, rows, &b);

	printf("in-game menu, a game with a set:\n");
	ck(n == GM_ROWS, "ten rows");
	ck(!strcmp(rows[GM_CONTINUE].label, "Continue"), "Continue leads");
	/* The quick slot right after Load (2026-11-07): the slot SELECT+R1 and
	 * SELECT+L1 use, its number at the right, stepped round like Display */
	ck(!strcmp(rows[GM_QUICK].label, "Quick Slot") && GM_QUICK == GM_LOAD + 1,
	   "Quick Slot right after Load");
	ck(!strcmp(val(&rows[GM_QUICK]), "2") && rows[GM_QUICK].live,
	   "and says the slot, a stop");
	/* Shader right after Display (2026-11-12), Off with no shader said,
	 * stepped round like Display; then Controls (2026-11-06), a page to
	 * read, so it is always a stop and says nothing at the right */
	ck(!strcmp(rows[GM_DISPLAY + 1].label, "Shader") && GM_SHADER == GM_DISPLAY + 1,
	   "Shader right after Display");
	ck(!strcmp(val(&rows[GM_SHADER]), "Off") && rows[GM_SHADER].live,
	   "and says Off when no shader is given, a stop");
	ck(!strcmp(rows[GM_SHADER + 1].label, "Controls") && GM_CONTROLS == GM_SHADER + 1,
	   "Controls right after Shader");
	ck(rows[GM_CONTROLS].live && !rows[GM_CONTROLS].value, "and opens with A, no value");
	ck(!strcmp(val(&rows[GM_DISPLAY]), "Native"), "Display carries the mode");
	ck(!strcmp(val(&rows[GM_CHEEVOS]), "12 / 40"), "Achievements counts the set");
	ck(rows[GM_CHEEVOS].live, "and is reachable");
	k = reachable(rows, n, got, GM_ROWS);
	ck(k == GM_ROWS, "every row is a stop");
	/* X from anywhere on the page is Quit (2026-11-08), and Quit alone
	 * carries X's glyph to say so */
	ck(gm_x_row() == GM_QUIT && rows[GM_QUIT].value == MENU_KEY_X,
	   "X is Quit's, and Quit draws X's glyph");
	for (k = 0; k < n; k++)
		if (k != GM_QUIT && rows[k].value == MENU_KEY_X) break;
	ck(k == n, "and no other row does");

	/* Over a port (OpenBOR, src/portmenu.h): what a stopped program can
	 * still be asked, every one a stop, in the game menu's order */
	u.port = true;
	n = gm_rows(&u, rows, &b);
	printf("in-game menu over a port:\n");
	ck(n == 3 && gm_row_ids(&u, ids) == 3 && ids[0] == GM_CONTINUE && ids[1] == GM_CONTROLS &&
	   ids[2] == GM_QUIT, "Continue, Controls and Quit");
	ck(!strcmp(rows[0].label, "Continue") && !strcmp(rows[1].label, "Controls") &&
	   !strcmp(rows[2].label, "Quit"), "labeled as over a game");
	ck(rows[2].value == MENU_KEY_X && gm_row_at(&u, gm_x_row()) == 2,
	   "X is its Quit too, with the glyph");
	ck(reachable(rows, n, got, GM_ROWS) == 3, "every row a stop");
	u.port = false;

	u.earned = 0; u.total = 0;
	n = gm_rows(&u, rows, &b);
	printf("in-game menu, a game with no set:\n");
	ck(!strcmp(val(&rows[GM_CHEEVOS]), "none"), "Achievements says none");
	ck(!rows[GM_CHEEVOS].live, "and does nothing");
	k = reachable(rows, n, got, GM_ROWS);
	ck(k == GM_ROWS - 1, "so the cursor steps over it");
	ck(!holds(got, k, GM_CHEEVOS), "and never rests on it");
	ck(holds(got, k, GM_QUIT) && holds(got, k, GM_CONTINUE),
	   "the rows either side of it still work");

	/* A system whose core keeps no state (ScummVM, J2ME): no quick slot,
	 * so no row, and every other row a place up from its id */
	u.earned = 12; u.total = 40; u.quick = 0;
	n = gm_rows(&u, rows, &b);
	printf("in-game menu, a system with no save states:\n");
	ck(n == GM_ROWS - 1, "nine rows, no Quick Slot");
	ck(gm_row_at(&u, GM_QUICK) == -1 && gm_row_at(&u, GM_DISPLAY) == GM_DISPLAY - 1 &&
	   !strcmp(rows[gm_row_at(&u, GM_DISPLAY)].label, "Display"),
	   "Display a row up, where gm_row_at says");
	gm_row_ids(&u, ids);
	ck(ids[2] == GM_LOAD && ids[3] == GM_DISPLAY && ids[n - 1] == GM_QUIT,
	   "Load, then Display, Quit last");
	ck(ctl_has_states("SFC") && ctl_has_states("PS1") && ctl_has_states("N64") &&
	   !ctl_has_states("SCUMMVM") && !ctl_has_states("J2ME"),
	   "every Diatom system keeps states but ScummVM and J2ME");

	/* A DS game on DSperate: its two screens' three rows, after Display,
	 * and every other row still where gm_row_ids says it is. */
	u.quick = 1;
	u.screens = true; u.layout = 1; u.swap = true; u.stylus = false;
	n = gm_rows(&u, rows, &b);
	printf("in-game menu, a game with two screens:\n");
	ck(n == GM_MAX_ROWS - 1, "thirteen rows: every row but the N64's Pak");
	ck(gm_row_ids(&u, ids) == n && ids[3] == GM_QUICK && ids[4] == GM_DISPLAY &&
	   ids[5] == GM_SHADER && ids[6] == GM_SCREENS && ids[7] == GM_SWAP &&
	   ids[8] == GM_STYLUS && ids[9] == GM_CONTROLS && ids[10] == GM_CHEEVOS &&
	   ids[12] == GM_QUIT,
	   "Screens, Swap Screens and Stylus right after Display and Shader, then Controls");
	ck(!strcmp(rows[6].label, "Screens") && !strcmp(val(&rows[6]), "Top Focus"),
	   "Screens says the layout");
	ck(!strcmp(val(&rows[7]), "on") && !strcmp(val(&rows[8]), "off"),
	   "Swap Screens and Stylus say on or off, lowercase like any status");
	ck(!strcmp(rows[10].label, "Achievements") && !strcmp(val(&rows[10]), "12 / 40"),
	   "Achievements moves down with its value");
	k = reachable(rows, n, got, GM_MAX_ROWS - 1);
	ck(k == GM_MAX_ROWS - 1, "every row is a stop");
	ck(gm_layout_index("single") == 2 && gm_layout_index("nonsense") == 0,
	   "a layout name Diatom did not send reads as Stacked");
	ck(!strcmp(gm_layout_names[GM_LAYOUT_DEFAULT], "single"),
	   "a system with no layout on record starts on one screen alone, the top one");

	u.screens = false;
	ck(gm_row_ids(&u, ids) == GM_ROWS && ids[GM_CHEEVOS] == GM_CHEEVOS,
	   "and any other game keeps ten, each its own id");

	/* A Nintendo 64 game: its Pak, right before Controls, saying which */
	u.pak = gm_pak_name("rumble");
	n = gm_rows(&u, rows, &b);
	printf("in-game menu, a Nintendo 64 game:\n");
	ck(n == GM_ROWS + 1, "eleven rows");
	ck(gm_row_at(&u, GM_PAK) == gm_row_at(&u, GM_SHADER) + 1 &&
	   gm_row_at(&u, GM_CONTROLS) == gm_row_at(&u, GM_PAK) + 1, "Pak between Shader and Controls");
	u.shader = "Sharp-Shimmerless";
	n = gm_rows(&u, rows, &b);
	ck(!strcmp(val(&rows[gm_row_at(&u, GM_SHADER)]), "Sharp-Shimmerless"),
	   "Shader says the shader it is given");
	u.shader = NULL;
	ck(!strcmp(rows[gm_row_at(&u, GM_PAK)].label, "Pak") &&
	   !strcmp(val(&rows[gm_row_at(&u, GM_PAK)]), "Rumble Pak"), "Pak says Rumble Pak");
	k = reachable(rows, n, got, GM_ROWS + 1);
	ck(k == GM_ROWS + 1, "every row is a stop");
	u.pak = NULL;
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
	ck(PM_SOUNDS == PM_CHANNELS + 1, "sits under Audio Channels");
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "UI Sounds")) {
			found++;
			ck(rows[i].live, "is live on a device with no radio");
			ck(!strcmp(val(&rows[i]), "Wood"), "and with no setting reads Wood, the default");
			ck(i > 0 && !strcmp(rows[i - 1].label, "Audio Channels"),
			   "directly under Audio Channels there too");
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
		{ 0, "off" }, { 30, "30s" }, { 60, "1m" }, { 120, "2m" }, { 300, "5m" }, { 600, "10m" },
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
			ck(!strcmp(val(&rows[i]), "1m"), "reads its delay");
			ck(menu_sound_at(b.sound, n, i) == MENU_SND_NO_A,
			   "steps with left and right and is heard after, as Auto Off is");
		}
	ck(found == 1, "is there once");
	u.saver = 0;
	n = sys_menu_build(&u, rows, &b, &heading);
	for (i = 0, found = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, "Screensaver"))
			found += !strcmp(val(&rows[i]), "off");
	ck(found == 1, "and off when it is off");
	u.no_lights = true;
	u.no_charge_limit = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	k = sys_menu_zyperos_rows(&u, pm);
	ck(n == k && has_label(rows, n, "Screensaver"), "a device without lights has it too");
	for (i = 0, found = 0; i < k; i++) found += pm[i] == PM_SAVER;
	ck(found == 1, "and the list names it");
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

	/* The page: the five parts, as I picked them in the side lights lab */
	n = sys_menu_lights_rows(LIGHTS_ALL, page, ids);
	ck(n == 5, "the page has five rows");
	ck(!strcmp(lbl(&page[0]), "Charging Bar") && !strcmp(lbl(&page[1]), "Low Battery") &&
	   !strcmp(lbl(&page[2]), "Volume and Brightness") &&
	   !strcmp(lbl(&page[3]), "Files and Covers") && !strcmp(lbl(&page[4]), "Achievements"),
	   "named in the lab's order");
	for (i = 0; i < n; i++)
		ck(page[i].live && !strcmp(val(&page[i]), "On") && ids[i] == (side_light_part)i,
		   "each On, live, and its part");
	n = sys_menu_lights_rows(side_lights_flip(LIGHTS_ALL, LP_DONE), page, ids);
	ck(!strcmp(val(&page[3]), "Off") && !strcmp(val(&page[2]), "On"), "a part off says Off");
	ck(!strcmp(side_lights_value(LIGHTS_ALL, buf, sizeof buf), "All on") &&
	   !strcmp(side_lights_value(0, buf, sizeof buf), "Off") &&
	   !strcmp(side_lights_value((1u << LP_BAR) | (1u << LP_CHEER), buf, sizeof buf), "2 on"),
	   "the row's words");
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
	sm_muse_row mids[SM_MUSE_ROWS];
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

	memset(&u, 0, sizeof u);
	u.games = true; u.muse = true; u.muse_both = true;
	n = sys_menu_build(&u, rows, &b, &heading);
	sys_menu_muse_rows(false, true, false, mids);
	for (i = 0; i < n; i++) {
		if (mids[i] == SMM_SHOW)
			ck(b.sound[i] == MENU_SND_RING, "Muse's Show turns over");
		if (mids[i] == SMM_SORT)
			ck(b.sound[i] == (MENU_SND_RING | MENU_SND_NO_A),
			   "Muse's Sort By steps round and does nothing with A");
		if (mids[i] == SMM_LOCK)
			ck(!b.sound[i], "Lock Screen is plain");
	}

	/* The in-game menu's Display steps on A as well as left and right
	 * (gm_key's GM_DISPLAY), so A is accept there, not deny. Shader too. */
	g.dmode = "Native"; g.screens = true; g.quick = 3;
	n = gm_rows(&g, gr, &gb);
	gm_row_ids(&g, ids);
	for (i = 0; i < n; i++) {
		unsigned want = ids[i] == GM_DISPLAY || ids[i] == GM_SHADER || ids[i] == GM_QUICK ||
		                ids[i] == GM_SCREENS || ids[i] == GM_SWAP || ids[i] == GM_STYLUS ||
		                ids[i] == GM_PAK
		                ? MENU_SND_RING : 0;

		ck(menu_sound_at(gb.sound, n, i) == want,
		   "in-game: Display, Shader, Quick Slot and the screens' rows step round, on A too; the rest plain");
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

int main(void)
{
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
	ingame_rows();
	quick_slot();
	n64_pak();
	audio_row();
	channels_row();
	sounds_row();
	charge_row();
	lights_row();
	row_sounds();
	if (fails) { printf("\n%d menu check(s) failed\n", fails); return 1; }
	printf("\nok: menus contain what they should\n");
	return 0;
}
