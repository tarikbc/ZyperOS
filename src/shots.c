/* SPDX-License-Identifier: GPL-3.0-only */
#include "ds/hover.h"
#include "shots.h"

#include "about_screen.h"
#include "app.h"
#include "bgm.h"
#include "bgm_screen.h"
#include "clock.h"
#include "clock_screen.h"
#include "ctltab.h"
#include "ctltab_screen.h"
#include "db.h"
#include "devmode_screen.h"
#include "dl_screen.h"
#include "ds/page.h"
#include "emptyhome.h"
#include "gamedel_screen.h"
#include "gameinfo_screen.h"
#include "ingame_screen.h"
#include "hudmenu_screen.h"
#include "launch_screen.h"
#include "lights_screen.h"
#include "media_screen.h"
#include "menu_panel.h"
#include "menu_pick.h"
#include "menu_runner.h"
#include "mp4lock.h"
#include "mp4video.h"
#include "musec.h"
#include "notice.h"
#include "nowplaying_screen.h"
#include "homesys_screen.h"
#include "osdbar.h"
#include "panel.h"
#include "player2_screen.h"
#include "pminstall.h"
#include "portload.h"
#include "powerscreens.h"
#include "rabadge.h"
#include "remapstore.h"
#include "saver_screen.h"
#include "radl_screen.h"
#include "scraper_screen.h"
#include "shelf_draw.h"
#include "storage_screen.h"
#include "sysmenu_screen.h"
#include "devmode_screen.h"
#include "topbar.h"
#include "tour_screen.h"
#include "tourdevice.h"
#include "tourdraw.h"
#include "update_screen.h"
#include "usbdrive_screen.h"
#include "xfer_screen.h"

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

/* ---------- main ---------------------------------------------------------- */

/* --shot <file.png> [--screen games|systems] draws one frame, writes it out
 * and exits. This is how the shelf gets looked at without a device in hand:
 * the host build renders exactly what the handheld renders. */
const char *shot_path;
int shot_screen = -1;
/* --folder DIR: the shelf on that folder of its games (src/shelfdir.h);
 * --game FILE: its cursor on that game or folder, opening the folder it is
 * in; --view list|covers: MENU > View for the shot, whatever is saved */
const char *shot_folder, *shot_game, *shot_view;
/* --shelf-press KEYS: the shelf's own presses before the shot, comma
 * separated - a, b, x, y, up, down, left, right, l1, r1, start - each one pass of
 * update_games, as the device's loop makes them (never A on a game: that
 * starts it) */
const char *shot_shelf_press;
int shot_menu, shot_menu_sel;
/* --menu-steps N: N presses of Down (Up when N is negative) on --menu's
 * list before the shot, a second apart on a held clock, so a list that
 * goes round can be looked at past its end (DESIGN.md 11.3) */
int shot_menu_steps;
/* --menu-tab-from M: the tab MENU's move comes from, or -1 */
int shot_tab_from = -1, shot_tab_ms;
/* --look ACCENT,LIGHT,DARK,CORNERS,ZYPER: the look for the shot; and
 * --look-page theme|night|night-list|screen|screen-looks [row]: Theme's
 * page, Night Mode's, a Night Mode row's list (--pick-at for its cursor),
 * Screen Colors' or its Look's list (DESIGN.md 11.42), the cursor on `row` */
const char *shot_look, *shot_look_page;
int shot_homesys = -1;     /* --homesys-page ROW: MENU > Home Systems' page */
int shot_lights = -1;      /* --lights-page ROW: MENU > Side Lights' page */
int shot_look_row;
int shot_kb, shot_kb_layer;
static keyboard_transition shot_keyboard;
static bool shot_keyboard_panel;
/* --kbsel ROW COL: the keyboard's selected key, rows 0-3 the grid and row 4
 * the action row, with COL its key (src/kbnav.h). It opens on the letters'
 * first key, as kb_prompt does. */
int shot_kb_row = 1, shot_kb_col;
const char *shot_notice;
const char *shot_notice_head = "Unlocked · 5 points";
/* --notice-badge PNG and --notice-progress VALUE TARGET: the same notice with
 * a badge, or as a progress notice (notice_render_ex) */
const char *shot_notice_badge;
int shot_notice_value, shot_notice_target;
/* --notice-kind unlock|progress|set: what the notice is about, which says
 * what it carries (notice_look): an unlock has the plain badge where no
 * --notice-badge is given, and Zyper; progress, the plain badge; set, neither */
nq_kind shot_notice_kind = NQ_SET;
/* --notice-heat: the notice with the thermometer where a badge goes, as
 * "Getting hot" is over a game (DESIGN.md 11.23) */
int shot_notice_mark;
/* --popup saved|loaded|save_failed|load_failed|slot_empty|no_states|dslang|corestate|stick_on|stick_off|speed N:
 * the pop-up of the SELECT hotkeys, as on_quick and on_speed put it over a
 * game (notice_render, no badge, no Zyper); N is the step for speed, in
 * halves (3 is 1.5x) */
const char *shot_popup;
int shot_popup_half = 2;
/* --lowbatt MS [charging|off]: the low-battery start screen MS ms in
 * (DESIGN.md 11.24); with charging, a charger plugged in at 2.4 s; with off,
 * MS ms into its send-off */
int shot_lowbatt = -1, shot_lowbatt_kind;
/* --heatpop MS: Zyper's "Getting hot" on the home, MS ms in (DESIGN.md 11.23) */
int shot_heatpop = -1;
/* --ranews N: Zyper's bubble saying N achievements earned offline went */
int shot_ranews;
/* --cheevos-waiting N: the list's heading with N of its unlocks waiting */
int shot_cheevos_waiting;
/* --upnews available|ready: his bubble with System Update's news instead */
const char *shot_upnews;
/* --charging MS [hold|unplugged|off|pending|kept|limit|keptfull|soon]: the
 * charging screen MS ms in (DESIGN.md 11.25), a wall charger putting 1480 mA
 * into a 62% battery; with hold, POWER held half way; unplugged, the cable
 * out; off, MS ms into its send-off; pending, the charger not named yet;
 * kept, held at an 85% charge limit (11.26); limit, charging toward it;
 * keptfull, held at 100%, charged full before the limit was set; soon, a
 * 92% battery, full in minutes */
int shot_charging = -1, shot_charging_kind;
int shot_cheevos;
int shot_cheevos_sel;
/* --ingame: --cheevos-screen as the in-game menu opens it, over a paused
 * game (a made-up frame), rather than over the shelf */
int shot_ingame;
/* How far between two systems the shelf is caught, 0 at rest. The shot is one
 * settled frame, so anything that only happens DURING a move - the label
 * crossfade, a card mid-slide - cannot be looked at without this. */
float shot_sysmove;
/* The same for the games shelf, which is the one with a long list under it.
 * Eleven systems make a rail whose segments tile the track; three hundred games
 * make an 18px segment moving two and a half pixels a card, and the two do not
 * look alike at any fraction of a move. */
float shot_gamemove;
/* --shot-at MS: a motion started for the shot and drawn MS milliseconds in,
 * on a clock held still for it (anim_now, cf_clock), the one clock of every
 * shot that moves: with --menu, the focus plate moving onto the menu's row
 * from the row above it (the list gives too when the window scrolls), or with
 * --open, --close or --swap the top menu opening on the shelf, closing onto
 * it, or giving way to MENU > Controls; with --zyper, Zyper in the top menu
 * that long after it opened; with --launch, the zoom into the focused game;
 * with --poweroff, the power-off; otherwise the shelf on screen stepping one
 * card forward. Frame strips are made from these, a shot for each time. -1,
 * without it, is a shot at rest, drawn as it always was. Unlike --sysmove
 * and --gamemove, which stage a fraction of a move, this presses: the plate,
 * the menu and the shelf are told to move as the buttons tell them, and the
 * frames between are drawn as the device draws them, so whatever follows
 * over time - the plate, the list's give, the label's crossfade, the
 * background tint - is where it is that far in. */
int shot_at = -1;
/* --launch: the launch zoom at --shot-at's time (0 without it), of the
 * focused game of the focused system, from either screen: the systems screen
 * has the same game focused that its shelf would open on, and the shot reads
 * its card on the frame, as a shot reads every card. */
int shot_launch;
const char *shot_cheevos_game = "Hagane: The Final Conflict";
/* --cheevos-badge SET NAME: every row with that badge (src/rabadge.h), the
 * ones not earned greyed, as the list draws them; a '#' in NAME is the row's
 * number, 1 to 8, so each row can have a picture of its own. Without it every
 * row has the plain badge, as a set the card has no art for does. */
const char *shot_badge_set, *shot_badge_name;
static bool shot_badge_got[8];

static SDL_Texture *shot_row_badge(int row)
{
	char name[64], *hash;

	if (row < 0 || row >= 8) return NULL;
	snprintf(name, sizeof name, "%s", shot_badge_name ? shot_badge_name : "");
	if ((hash = strchr(name, '#'))) *hash = (char)('1' + row);
	return rb_icon(g_badge_r, shot_badge_set, name, !shot_badge_got[row], theme());
}
int shot_syn;       /* --synopsis: the card a scraped game opens */
int shot_info;
int shot_delete_ask, shot_delete_now;
/* --gm-said K: one of Delete Game's and Rename's messages (GM_SAID) */
int shot_gm_said = -1;

/* Delete Game's and Rename's messages, each as its screen words it
 * (src/gamedel_screen.c, src/gamern_screen.c): first the frame a delete runs
 * under, then Delete Game's results, then Rename's. A port's is about the
 * game under the cursor (a port, on the shot's card), a BIOS's about neogeo,
 * the one result_message's lines were made for. */
static const str_id GM_SAID[] = {
	S_GAMEMENU_DELETING, S_GAMEMENU_DELETE_PARTLY, S_GAMEMENU_DELETE_FAILED,
	S_GAMEMENU_DELETE_OUTSIDE, S_GAMEMENU_DELETE_RUNTIME, S_GAMEMENU_DELETE_PORT_UNKNOWN,
	S_GAMEMENU_DELETE_BIOS,
	S_GAMEMENU_RENAME_INVALID, S_GAMEMENU_RENAME_TAKEN, S_GAMEMENU_RENAME_FAILED,
	S_GAMEMENU_RENAME_PARTLY,
};
#define GM_SAID_RENAME 7    /* GM_SAID's first of Rename's */

static void gm_said_preview(app *a, int k)
{
	sysview *v = &a->view[a->sys_cursor];
	const char *heading = T(k >= GM_SAID_RENAME ? S_GAMEMENU_RENAME : S_GAMEMENU_DELETE);
	char said[LIB_NAME + 256];
	str_id id;

	if (k < 0 || k >= (int)(sizeof GM_SAID / sizeof GM_SAID[0]) || v->list.count == 0) return;
	id = GM_SAID[k];
	if (id == S_GAMEMENU_DELETING) {
		/* wait_panel's one frame, before the delete runs */
		menu_row row = { T(id), NULL, false };

		draw_shelf(a);
		ui_scrim(a->r, 0);
		menu_message(a, heading, row, MENU_ZYPEROS);
		return;
	}
	/* The table's own string where nothing is put in it, which the host's
	 * check holds as a string (a copy of it is data, whose cut English is
	 * let off) */
	if (id == S_GAMEMENU_DELETE_PORT_UNKNOWN)
		snprintf(said, sizeof said, T(id), v->list.items[v->cursor].title);
	else if (id == S_GAMEMENU_DELETE_BIOS)
		snprintf(said, sizeof said, T(id), "neogeo");
	else {
		result_preview(a, heading, T(id));
		return;
	}
	result_preview(a, heading, said);
}

int shot_controls = -1;
/* --sgdb [0|1|2]: MENU > SteamGridDB with no key, a key that works, or one
 * they refused - a stand-in key, masked as a real one is */
int shot_sgdb = -1;
/* --scraper [3|4]: the Video Snaps page; ScreenScraper's page. MENU >
 * Scraper itself is --scraper-page (scraper_page_shot). */
int shot_scraper = -1;
int shot_about = -1;
/* --about-scroll F: the story page scrolled F (0 to 1) of the way down, as
 * Up and Down would leave it. Left alone it is the top, where it opens. */
float shot_about_scroll = -1;
/* --language [ROW]: MENU > Language's panel over the shelf, the cursor on
 * ROW (the language that is on when none is given) */
int shot_language = -2;
/* --music-page [ROW [STEPS]]: MENU > Menu Music's page, the cursor on ROW,
 * then STEPS presses of Down, a frame each, so the list goes round and the
 * note under it comes into the window; `--music-page foot` draws it with no
 * cursor, the note pinned under the rows as a card's footer is, so a check
 * can measure it whatever the window shows */
int shot_music_page = -2, shot_music_steps;
/* --update: MENU > System Update's page as the status file in
 * ZYPEROS_USERDATA/update says (the update lab's states) */
int shot_update = -1;
int shot_hare;
int shot_usb = -1;   /* --usb-ask [N]: the question, tile N focused */
/* --datetime [N]: Date & Time's page, Time Zone on Auto with nothing found
 * yet (0), on Auto with the zone it found (1), with a long city (2), on a
 * zone of the list (3), or Auto's message as it is chosen (4); Time Zone's
 * list open on its first zone (5), on its longest city in the middle (6), on
 * Auto with the zone it found (7), and with a long city (8) */
int shot_datetime = -1, shot_datetime_mon = 10;
int shot_dev = -1;   /* --dev-page [N]: Developer Mode's page on the cable with 1, 12 or no keys (0..2), or the message that
                      * says it is on (3); over Wi-Fi with one key signed in (4); SSH over Wi-Fi's message as it is turned
                      * on (5); over Wi-Fi with no key (6); Wi-Fi up with SSH over Wi-Fi off (7); the cursor on
                      * Developer Mode (8) */
bool shot_usb_dev = true;   /* --usb-ask N plain: without the development image's tile */
/* --usb-power [N]: What did you plug in?, row N focused (1, Computer,
 * where it starts; 2, Phone); --usb-power said: the message after
 * Computer; --usb-power phone: the message after Phone; --usb-power
 * iphone: an iPhone's */
int shot_power = -1;
int shot_usbdrive = -1;   /* --usb-drive N */
/* --xfer-ask [N] [game]: File Transfer's question while files move, tile N
 * focused; with "game", the one a game asked for gets */
int shot_xfer_ask = -1;
bool shot_xfer_game;
int shot_hot, shot_hot_in;
const char *shot_hot_ssid   = "ZyperOS-7A2B";
const char *shot_hot_pass   = "q7mx-3kpa";
const char *shot_hot_phones = "1 joined";
const char *shot_hot_now    = "receiving Contra (USA).zip";
const char *shot_hare_addr = "192.168.1.42";
const char *shot_hare_who  = "1 connected";
const char *shot_hare_head = "Contra (USA).zip";   /* the file the Now row names */
const char *shot_hare_move;                         /* --hare-move: the tiles' state */
/* --hare-steps usb|wifi STATE: File Transfer's steps (linksteps.h) at one
 * state, its hint shown where the state has one: unplugged, nocomputer,
 * noanswer, nodriver, address, noaddress, browser, nobrowser, nonetwork */
const char *shot_steps_way, *shot_steps_state, *shot_steps_addr;
bool shot_steps_noname;      /* ... noname: as when mDNS could not start */
const char *shot_wait, *shot_wait_msg;   /* NULL: library.scanning's */
int shot_osd_kind, shot_osd_val; /* --osd volume|brightness|power LEVEL */
/* --tour STEP [MS [DONE]]: one frame of the tutorial, src/tour.h's step by
 * its name (or "skip", the shelf with the message a skip leaves), MS into
 * it, its press landed DONE ms ago or not at all (-1). --tour-wrong KEY [MS]
 * a press of the wrong button KEY, MS ago; --fn-volume the Pixel 2's keys,
 * whose brightness is MENU+VOL. --tour-pick N [MS]: in a choice step, the
 * focus moved to choice N MS ago (default long since); --tour-music a card
 * with menu music, so the strip counts its step. */
int shot_tour = -2;          /* -2 none, -1 the skip message */
int shot_tour_ms = 2000, shot_tour_done = -1;
int shot_tour_wrong = -1, shot_tour_wrong_ms = 300;
int shot_tour_pick = -1, shot_tour_pick_ms = 5000;
bool shot_tour_music;
int shot_fn_volume;
/* The chords a shot draws with: the profile's, or with --fn-volume the
 * Pixel 2's brightness chord, MENU on release and Home over them */
static dev_chords shot_chords(void)
{
	return shot_fn_volume ? dev_chords_fn_volume(plat_profile()->chords) : plat_profile()->chords;
}
/* --theme light|dark: the theme the shot is drawn in, whatever the saved
 * Appearance is; -1 for the saved one. */
int shot_theme = -1;
int shot_home = -1;
/* --lang CODE: the language the shot is drawn in (src/i18n.h; xx, the
 * pseudo-locale, on a host), whatever MENU > Language saved; NULL for it */
const char *shot_lang;
/* --cheer MS: the cheer for the first games, MS into it, over the home */
int shot_cheer = -1;
/* --songpop "HEAD|BODY" [MS]: Zyper's pop-up with a song (DESIGN.md 11.22),
 * MS into its life, 1500 by default: in, his hop and pant over, held */
const char *shot_songpop;
int shot_songpop_ms = 1500;
const char *shot_song;
bool shot_song_paused;
/* --status's words into shot_st, the corner's stand-ins; false on a word it
 * does not know. Wi-Fi is off and the battery has no reading until a word
 * says otherwise, as on a host. */
bool shot_status_parse(const char *spec)
{
	char buf[128], *w, *save;

	shot_st.set = true;
	shot_st.m = (status_marks){ .wifi = STATUS_WIFI_NONE, .batt = -1 };
	snprintf(buf, sizeof buf, "%s", spec);
	for (w = strtok_r(buf, " ,", &save); w; w = strtok_r(NULL, " ,", &save)) {
		int h, mi;

		if (sscanf(w, "%d:%d", &h, &mi) == 2 && h >= 0 && h < 24 && mi >= 0 && mi < 60)
			shot_st.minutes = h * 60 + mi;
		else if (!strcmp(w, "24h")) shot_st.h24 = 1;
		else if (!strcmp(w, "12h")) shot_st.h24 = 0;
		else if (!strcmp(w, "wifi=off")) shot_st.m.wifi = STATUS_WIFI_NONE;
		else if (!strcmp(w, "wifi=idle")) shot_st.m.wifi = STATUS_WIFI_IDLE;
		else if (!strncmp(w, "wifi=", 5) && w[5] >= '1' && w[5] <= '3' && !w[6])
			shot_st.m.wifi = w[5] - '0';
		else if (!strcmp(w, "bt")) shot_st.m.bt = true;
		/* the day it shows (MENU's Date & Time row), date=2026-01-15; today
		 * without it */
		else if (!strncmp(w, "date=", 5)) {
			int y, mo, d;

			if (sscanf(w + 5, "%d-%d-%d", &y, &mo, &d) != 3 || y < 1971 || mo < 1 || mo > 12 || d < 1 || d > 31)
				return false;
			shot_st.year = y, shot_st.mon = mo, shot_st.mday = d;
		}
		else if (!strncmp(w, "batt=", 5)) shot_st.m.batt = atoi(w + 5);
		else if (!strcmp(w, "chg")) shot_st.m.charging = true;
		/* on the cable, held at the charge limit: the plug */
		else if (!strcmp(w, "held")) shot_st.m.charging = shot_st.m.held = true;
		else if (!strcmp(w, "blinkoff")) shot_st.m.blink_off = true;
		/* what MENU > Battery Icon > Time Left writes: eta=200 is 3 h 20 min,
		 * on the cable the time to full; without it, the percentage */
		else if (!strncmp(w, "eta=", 4) && w[4] >= '0' && w[4] <= '9') shot_st.eta = atoi(w + 4);
		/* Time Left before it has a time: calc is the dots two filled, calc=N
		 * N of them (0 to 3); full is Charged, on the cable */
		else if (!strcmp(w, "calc")) shot_st.calc = 2;
		else if (!strncmp(w, "calc=", 5) && w[5] >= '0' && w[5] <= '3' && !w[6]) shot_st.calc = w[5] - '0';
		else if (!strcmp(w, "full")) shot_st.m.charging = shot_st.full = true;
		/* MENU > Battery Icon for this shot, not the saved one: icon=bars,
		 * icon=percentage or icon=time */
		else if (!strncmp(w, "icon=", 5)) {
			int k;

			for (k = 0; k < BATT_MODES && strcmp(w + 5, BATT_IDS[k]); k++) ;
			if (k == BATT_MODES) return false;
			shot_st.icon = k;
		}
		/* File Transfer behind the shelf: xfer=on, nothing moving; xfer=62,
		 * files moving and the ring that far round */
		else if (!strcmp(w, "xfer=on")) shot_st.m.xfer = true;
		else if (!strcmp(w, "usb=on")) shot_st.m.usb = true;
		/* System Update downloading behind: update=42, the ring that far */
		else if (!strncmp(w, "update=", 7) && w[7] >= '0' && w[7] <= '9') {
			shot_st.m.update = true;
			shot_st.m.update_pct = atoi(w + 7);
		}
		/* headphones in the jack: their mark */
		else if (!strcmp(w, "phones=on")) shot_st.m.phones = true;
		else if (!strncmp(w, "xfer=", 5) && w[5] >= '0' && w[5] <= '9') {
			shot_st.m.xfer = shot_st.m.xfer_moving = true;
			shot_st.m.xfer_pct = atoi(w + 5);
		}
		else return false;
	}
	return true;
}

/* --flip: the shot drawn whole in the theme --theme starts it in, so every
 * card on it is made in that theme, and then the theme turned to the other
 * one the way Auto turns it (shot_flip_theme), and the shot taken there.
 * `make check-flip` holds it against a shot started in the second theme: a
 * card the turn left in the old theme, and the settled frame did not replace,
 * is the difference. */
int shot_flip;
/* Set by take_shot when the frame it drew fails a check of its own (see
 * shot_cards_have_feet), and the run then exits 3: a shot is also how
 * `make check-flip` looks at the systems row, the games shelf and Now
 * Playing's cover, and a picture that is wrong in a way a byte comparison of
 * two wrong pictures cannot see is one it must not pass. */
int shot_failed;
/* --portload NAME: the still a port's loading screen leaves for splash, for
 * the game called NAME, drawn to the shot's file in place of a shelf. */
const char *shot_portload;
/* --poweroff: the power-off animation's frame --shot-at's time in (0
 * without it), in place of a shelf */
int shot_poweroff;
const char *shot_kb_text = "correct horse";
/* The title was hardcoded to "Wi-Fi password", so the one other thing that
 * uses this keyboard - signing in to RetroAchievements - could not be
 * rendered at all. A harness narrower than the thing it checks. */
const char *shot_kb_title = "Wi-Fi password";
/* --kb-ss-user: the title is the ScreenScraper sign-in's, in the shot's
 * language, so check-i18n-shots measures it in every one */
int shot_kb_ss_user;

static const char *kb_title_shown(void)
{
	return shot_kb_ss_user ? T(S_SCRAPER_SS_USER) : shot_kb_title;
}
int shot_slots, shot_slot_sel;
int shot_jump;               /* letter-jumps to apply before drawing */
/* --nowplaying ALBUM [TRACK [SECONDS]], and --paused with it. */
int   shot_np = -1, shot_np_track, shot_np_paused;
/* --locked: the lock screen over it, its hold three fifths done. --video
 * [full|controls]: Now Playing as a video, a test picture for the frame. */
int   shot_locked, shot_video = -1;
float shot_np_at = 83.0f;
/* --mode NAME plays whatever the shot shows in that play mode: the mark beside
 * the track count is drawn from it. */
const char *shot_mode;
float shot_slot_aspect = 4.0f / 3.0f;
/* --slot-when MONTH HOUR: the slot in view saved in that month (1 to 12) at
 * that hour, not August's afternoon; --slot-save: the strip as Save asks
 * where to ("Save to") */
int shot_slot_mon, shot_slot_hour, shot_slot_save;

/* A stand-in for a paused game frame, at whatever shape was asked for: the
 * slot carousel frames the picture to its own aspect, so looking at that
 * without a device means being able to hand it one. */
static SDL_Texture *fake_frame(SDL_Renderer *r, float aspect)
{
	int h = 224, w = (int)(224 * aspect + 0.5f);
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	int x, y;

	if (!s) return NULL;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
			int checker = ((x / 16) + (y / 16)) & 1;
			px[x] = SDL_MapRGBA(s->format,
			                    (Uint8)(30 + 180 * x / w),
			                    (Uint8)(40 + 150 * y / h),
			                    (Uint8)(checker ? 150 : 90), 255);
		}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	return t;
}

static void shot_draw_slots(app *a)
{
	slot_view sv = { .aspect = shot_slot_aspect, .saving = shot_slot_save };
	int i;

	for (i = 0; i <= GM_SLOTS; i++) {
		/* An afternoon in August, a slot an hour apart */
		struct tm tm = { .tm_mon = 7, .tm_mday = 20 + i, .tm_hour = 13 + i,
		                 .tm_min = i * 7, .tm_sec = i * 9 };

		if (i == shot_slot_sel && shot_slot_mon) tm.tm_mon = shot_slot_mon - 1, tm.tm_hour = shot_slot_hour;
		/* Slot 5 left empty, so the empty state is in the picture too. */
		if (i == 5) continue;
		sv.have[i] = 1;
		slot_when(sv.when[i], sizeof sv.when[i], &tm);
		sv.thumb[i] = fake_frame(a->r, shot_slot_aspect);
	}
	ui_scrim(a->r, 1);
	slot_draw(a, &sv, shot_slot_sel);
	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
}

/* --gamemenu: the menu over a paused game, as gm_backdrop and the runner draw
 * it, on a made-up frame: the one place the status corner is drawn in Salt &
 * Pepper whatever the theme. On a games shelf, whose system and game it
 * builds its rows for. */
/* --gamemenu [row] port: the menu over a port instead (src/portmenu.h), as
 * port_menu shows it over OpenBOR; --backdrop PNG puts a picture of a real
 * game under it in place of the made-up frame, at the screen's size. */
int shot_gamemenu, shot_gamemenu_sel, shot_gamemenu_tab;
const char *shot_backdrop;
/* The Controls tab's (src/ctltab_screen.h), with --gm-tab 2: --gm-all,
 * Applies To on All <System>; --gm-pick, the choices of the button under
 * the cursor; --gm-ask, Reset Buttons' question; --gm-keys N, page N of the
 * Hotkeys row's pages. */
bool shot_gm_all, shot_gm_pick, shot_gm_ask;
int  shot_gm_keys = -1;
/* With --shot-at, its motion: --gm-tab-from N, L1 or R1 from tab N to
 * --gm-tab's; --gm-press KEYS, the Controls tab's presses (up, down, left,
 * right, comma separated) from the row --gamemenu names. The tab's move,
 * when one was pressed (shot_motion); and whether the handheld was lit
 * yet, so the frames of a motion go on from the one before. */
int  shot_gm_from = -1;
const char *shot_gm_press;
static unsigned shot_gm_t0;
static bool shot_gm_open;

/* --gamemenu's Controls tab, beside the handheld, or a page it opens */
static void shot_draw_ctltab(app *a, menu_tabs *tabs)
{
	static menu_row rows[CT_MAX_ROWS];
	unsigned vcol[CT_MAX_ROWS];
	int ids[CT_MAX_ROWS], n, sel;
	menu_style over = { .fixed_w = gm_width(a), .follow_tint = true };
	menu_style st = over;
	ctlscr_game g;

	gm_ctl_game(a, NULL, &g);
	g.over = &over;
	g_gm_mem.scope_all = shot_gm_all;
	if (!shot_gm_open) ctlscr_open();
	shot_gm_open = true;
	n = ctlscr_rows(a, &g, rows, vcol, NULL, ids);
	sel = shot_gamemenu_sel < n ? shot_gamemenu_sel : n - 1;
	/* Off a quiet row, as the runner steps off one */
	if (!rows[sel].live) {
		sel = menu_step_sel(rows, n, sel, +1);
		if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, -1);
	}
	if (shot_gm_pick) {
		ctlscr_shot_pick(a, &g, ids[sel]);
		return;
	}
	if (shot_gm_ask) {
		char q[LIB_NAME + 256], lines[DEL_ASK_LINES][CHV_WRAP_COLS];
		menu_row ask[DEL_ASK_LINES + 2];
		int nq;

		ctlscr_question(&g, q, sizeof q);
		nq = store_ask_rows(a, q, lines, ask);
		ask[nq] = (menu_row){ T(S_GAMEMENU_RESET_BUTTONS_YES), NULL, true };
		ask[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
		menu_draw(a, T(S_GAMEMENU_RESET_BUTTONS), ask, nq + 2, nq + 1, menu_std_width(a),
		          MENU_ZYPEROS);
		return;
	}
	if (shot_gm_keys >= 0) {
		ctl_game cg = game_controls_of(a);
		const menu_row *pr;
		ctl_buf cb;
		char count[16];
		int pages, np, loop_at, width;

		cg.no_buttons = true;
		{ const dev_chords c = shot_chords(); ctl_set_chords(&c); }
		pages = ctl_game_pages(&cg);
		width = game_controls_width(a, &cg);
		np = controls_page_rows(&cg, shot_gm_keys, width, &cb, &pr, &loop_at);
		snprintf(count, sizeof count, "%d / %d", shot_gm_keys < pages ? shot_gm_keys + 1 : 1, pages);
		menu_draw_caption(a, cb.head, pr, np, -1, width, a->tint, NULL, false, loop_at,
		                  pages > 1 ? count : NULL);
		return;
	}
	st.tabs = tabs;
	st.side = ctlscr_side;
	st.side_w = CTLSCR_SIDE_W;
	st.vcolors = vcol;
	menu_draw_styled(a, &st, gm_tab_name(tabs->at), rows, n, sel);
}

/* --pick-at K: the list's cursor on choice K, not on the one that is on */
int shot_pick_at = -1;

/* An option row's list as menu_pick draws it: its choices, the one on
 * ticked, and what the one under the cursor does - in a picture column
 * beside them with its preview (menu_pick_preview), or under them */
static const menu_choice *g_shot_list;
static int g_shot_list_at, g_shot_list_side;
/* What the list's cursor shows (src/ds/hover.h): shown in the first
 * compose, the shot then composed again over it (shot_compose), the tick
 * staying on the choice kept before the show */
static int g_shot_hover_kept = -2;
static bool g_shot_hover_again;

static void shot_list_side(app *a, const menu_side *at)
{
	const menu_choice *c = g_shot_list;

	if (!c || at->stage == MENU_SIDE_OVER) return;
	menu_pick_side_draw(a, at->content_x, at->content_y, g_shot_list_side,
	                    menu_pick_note_room(c->notes, c->n, g_shot_list_side), c->notes ? c->notes[g_shot_list_at] : NULL, c->preview, g_shot_list_at);
}

static void shot_list_draw(app *a, const char *heading, const menu_choice *ch, int width,
                           unsigned accent)
{
	menu_row pr[MENU_CHOICES_MAX + 3];
	static char pw[MENU_CHOICES_MAX][64];
	static char pn[2][CHV_WRAP_COLS];
	const int at = shot_pick_at >= 0 && shot_pick_at < ch->n ? shot_pick_at : ch->cur;
	int k, np = ch->n, kept = ch->none ? -1 : ch->cur;

	/* The choice under the cursor shown, as the list shows it: once, and
	 * the shot composed again with it on (the tick on the one kept) */
	if (g_shot_hover_kept == -2) {
		g_shot_hover_kept = kept;
		if (ch->hover && ch->hover->show && at != kept) {
			ch->hover->show(a, at);
			g_shot_hover_again = true;
		}
	} else {
		kept = g_shot_hover_kept;
	}
	for (k = 0; k < ch->n; k++) {
		menu_pick_word(ch->labels[k], pw[k], (int)sizeof pw[k]);
		pr[k] = (menu_row){ pw[k], k == kept ? MENU_TICK : NULL, true };
	}
	if (ch->notes && !ch->preview) {   /* what the choice under the cursor does */
		int nl = menu_pick_note(ch->notes[at], pn);

		/* drawn in lines, said whole to the host's check, as menu_pick says it */
		if (nl > 1) ui_fit_drawn(ch->notes[at]);
		pr[np++] = MENU_RULE;
		pr[np++] = MENU_BODY(nl > 0 ? pn[0] : "");
		pr[np++] = MENU_BODY(nl > 1 ? pn[1] : "");
	}
	g_menu_epoch++;   /* another menu: a list opens at rest on its choice, as menu_run marks it */
	g_menu_tabs = NULL;   /* a list of its own: the row's name, no tabs */
	g_shot_list = ch;
	g_shot_list_at = at;
	g_menu_side = ch->preview ? shot_list_side : NULL;
	width = menu_pick_width(a, width, ch->labels, ch->n, ch->none ? -1 : ch->cur, ch->preview != NULL,
	                        &g_shot_list_side);
	g_menu_side_w = g_shot_list_side;
	g_menu_pin_foot = !ch->preview;   /* as menu_pick's style asks */
	menu_draw(a, heading, pr, np, at, width, accent);
	g_menu_pin_foot = false;
	g_menu_side = NULL;
	g_menu_side_w = 0;
	g_shot_list = NULL;
}

static void shot_draw_gamemenu(app *a)
{
	menu_row rows[GM_MAX_ROWS];
	gm_bufs b;
	SDL_Texture *bg = shot_backdrop ? IMG_LoadTexture(a->r, shot_backdrop)
	                                : fake_frame(a->r, 4.0f / 3);
	gm_ui u;
	menu_tabs tabs = { .count = GT_TABS, .name = gm_tab_name_i };
	int n;

	gm_gather(a, &u);
	tabs.at = tabs.from = shot_gamemenu_tab % GT_TABS;
	/* The tab move a shot pressed (shot_motion), from where it was */
	if (shot_gm_t0 && shot_gm_from >= 0) {
		tabs.from = shot_gm_from % GT_TABS;
		tabs.dir = (tabs.at - tabs.from + GT_TABS) % GT_TABS == 1 ? +1 : -1;
		tabs.t0 = shot_gm_t0;
	}
	/* The On-Screen Stats page, which A on the Display tab's row opens */
	if (shot_hud_page >= 0 && !u.port) {
		hud_page_shot(a, bg, shot_hud_page, shot_hud_items, shot_hud_pick, shot_hud_shows);
		if (bg) SDL_DestroyTexture(bg);
		return;
	}
	gm_backdrop(a, &(gm_ctx){ .a = a, .bg = bg }, 1.0f);
	gm_preview_frame(bg);
	if (!u.port && tabs.at == GT_CONTROLS) {
		shot_draw_ctltab(a, &tabs);
		if (bg) SDL_DestroyTexture(bg);
		return;
	}
	/* The Achievements tab, as gm_menu_build makes it and menu_run draws it:
	 * the set on the card's game, or --achtab's state with a made-up set */
	if (!u.port && tabs.at == GT_ACHIEVEMENTS) {
		static achtab_bufs ab;
		static menu_row arows[MENU_RUN_ROWS];
		menu_style st = { 0 };
		at_facts f;
		int an, asel;

		if (shot_badge_set) achtab_shot_badges(shot_badge_set, shot_badge_name);
		gm_at_facts(&f);
		st.fixed_w = gm_width(a);
		st.follow_tint = true;
		st.tabs = &tabs;
		an = achtab_rows(a, &f, gm_at_source(), &ab, arows, MENU_RUN_ROWS, st.fixed_w, &st);
		asel = shot_gamemenu_sel < an ? shot_gamemenu_sel : an - 1;
		/* Where the runner puts the cursor (menu_run_body): off a dead row
		 * onto a live one, or, with none (a sentence in place of the list),
		 * still on the dead row, which the panel draws no plate on
		 * (menu_rows_landable). Hiding it here, as the shot did, kept the
		 * plate the device drew on that sentence out of every shot (1.0.6) */
		if (!st.visits_all && !arows[asel].live) {
			asel = menu_step_sel(arows, an, asel, +1);
			if (!arows[asel].live) asel = menu_step_sel(arows, an, asel, -1);
		}
		menu_draw_styled(a, &st, gm_tab_name(tabs.at), arows, an, asel);
		if (bg) SDL_DestroyTexture(bg);
		return;
	}
	n = gm_rows(&u, (gm_tab)tabs.at, rows, &b);
	if (n == 0) rows[n++] = MENU_NOTE("");
	g_menu_tabs = u.port ? NULL : &tabs;
	/* The handheld leaving with the Controls tab's name */
	g_menu_side = u.port ? NULL : ctlscr_side;
	/* --gm-choose: A on the row, as the runner answers an option row of
	 * more than two (menu_choose): its list over the paused game */
	if (shot_gm_choose && !u.port) {
		gm_row ids[GM_MAX_ROWS];
		int ni = gm_tab_rows(&u, (gm_tab)tabs.at, ids), sel = shot_gamemenu_sel < n ? shot_gamemenu_sel : n - 1;
		const menu_choice *ch = sel >= 0 && sel < ni ? &gm_choices(a, &u)[ids[sel]] : NULL;

		if (ch && (ch->n > 2 || ch->list)) {
			shot_list_draw(a, rows[sel].label, ch, gm_width(a), a->tint);
			if (bg) SDL_DestroyTexture(bg);
			return;
		}
	}
	menu_draw(a, u.port ? NULL : gm_tab_name(tabs.at), rows, n,
	          shot_gamemenu_sel < n ? shot_gamemenu_sel : n - 1,
	          gm_width(a), a->tint);
	g_menu_side = NULL;
	g_menu_tabs = NULL;
	if (bg) SDL_DestroyTexture(bg);
}

/* --player2 KIND: NES Player 2's page over a paused game (a made-up frame),
 * in a state made up as the hot panel's shot makes its network (--hotspot):
 * hotspot, its code with nobody yet; connected, player 2 playing; cable, a
 * computer on the cable; off and off-cable, before A on each link; none,
 * neither plugged in; failed; starting, the hotspot's start; closing and
 * closing-cable, the stop on each link. With --gamemenu the in-game menu
 * instead, its Player 2 row saying that state. */
static void shot_draw_player2(app *a)
{
	SDL_Texture *bg = shot_backdrop ? IMG_LoadTexture(a->r, shot_backdrop)
	                                : fake_frame(a->r, 4.0f / 3);

	paused_backdrop(a, bg, 1.0f);
	p2_show(a, &g_shot_p2_view);
	if (bg) SDL_DestroyTexture(bg);
}

/* --gamecontrols [page] [TAG]: the in-game menu's Controls page over a
 * paused game, page `page`, for the game under the cursor on the shelf of
 * the system tagged TAG (the shelf the shot is on when none is given), as
 * game_controls_screen draws it. --fn-volume names the Pixel 2's
 * brightness chord. */
int shot_gamecontrols = -1;
const char *shot_gamecontrols_tag;

static void shot_draw_gamecontrols(app *a)
{
	SDL_Texture *bg = shot_backdrop ? IMG_LoadTexture(a->r, shot_backdrop)
	                                : fake_frame(a->r, 4.0f / 3);
	const menu_row *rows;
	ctl_buf b;
	ctl_game g;
	char count[16];
	int i, n, pages, width, loop_at;

	for (i = 0; shot_gamecontrols_tag && i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, shot_gamecontrols_tag)) {
			a->sys_cursor = i;
			a->tint = a->sys.systems[i].accent;
			break;
		}
	g = game_controls_of(a);
	{ const dev_chords c = shot_chords(); ctl_set_chords(&c); }
	width = game_controls_width(a, &g);
	n = controls_page_rows(&g, shot_gamecontrols, width, &b, &rows, &loop_at);
	pages = ctl_game_pages(&g);
	snprintf(count, sizeof count, "%d / %d",
	         shot_gamecontrols < pages ? shot_gamecontrols + 1 : 1, pages);
	paused_backdrop(a, bg, 1.0f);
	menu_draw_caption(a, b.head, rows, n, -1, width, a->tint, NULL, false, loop_at,
	                  pages > 1 ? count : NULL);
	if (bg) SDL_DestroyTexture(bg);
}

/* --handheld-box [tab|tour]: the handheld measured in every pose the
 * Controls tab (ctlscr_box_check) and the tutorial (tourdev_box_check) give
 * it, every frame of every turn, against its kept texture's box: a texture
 * too small cuts the figure off on the device and on the host alike. A
 * "HANDHELD-BOX:" line on stderr for each pose out of its box, and a count
 * (tools/controls-shots-check.sh, tools/tour-shot-check.py). */
const char *shot_handheld_box;

static void handheld_box_report(void)
{
	int tab = 0, tour = 0, bad = 0;

	if (strcmp(shot_handheld_box, "tour")) bad += ctlscr_box_check(&tab);
	if (strcmp(shot_handheld_box, "tab")) bad += tourdev_box_check(plat_profile()->model, &tour);
	fprintf(stderr, "handheld-box: %d poses of the Controls tab and %d of the tutorial measured, "
	        "%d out of their box\n", tab, tour, bad);
}

/* --controls-fit [game|home]: every Controls page measured with the font
 * against the panel that draws it - the home's five on both devices, and
 * every system's in a game (an .fds game's too), or one of the two sets. A
 * row wider than the panel is cut or slides; a page taller than it scrolls
 * itself, which is wrong for a table read a button at a time. A
 * "CONTROLS-FIT:" line on stderr for each page that does not fit, and a count
 * of the pages read (tools/controls-shots-check.sh). */
const char *shot_controls_fit;

static int controls_fit_one(app *a, const char *where, const char *head,
                            const char *count, const menu_row *rows, int n)
{
	int w = menu_std_width(a), mw = menu_measure(rows, n, head), i;
	int rws = 0, rules = 0, notes = 0, room, bad = 0;
	/* the heading is centered with the count's room kept free either side
	 * of it (panel_show) */
	int inset = count ? ui_text_width(ui_font(UI_F_META), count) + 2 * DS_GRID : 0;
	int hw = ui_text_width(ui_font(UI_F_LABEL), head);

	if (hw > w - 2 * inset) {
		fprintf(stderr, "CONTROLS-FIT: %s: heading \"%s\" %d wide, room for %d beside \"%s\"\n",
		        where, head, hw, w - 2 * inset, count ? count : "");
		bad = 1;
	}

	for (i = 0; i < n; i++) {
		if (!rows[i].label) rules++;
		else if (ROW_IS_NOTE(rows[i])) notes++;
		else rws++;
	}
	room = menu_list_fit(0, rules, notes);
	if (mw > w) {
		fprintf(stderr, "CONTROLS-FIT: %s: %d wide, the panel holds %d\n", where, mw, w);
		bad = 1;
	}
	if (rws > room) {
		fprintf(stderr, "CONTROLS-FIT: %s: %d rows and %d notes, room for %d rows\n",
		        where, rws, notes, room);
		bad = 1;
	}
	return bad;
}

/* The Ports shelf's: PortMaster's own pages, on the card or not, and every
 * port on this card's (src/portdoc.h), but a port's About, which is prose
 * and scrolls as a synopsis does. The pages read, into *pages. */
static int controls_fit_ports(app *a, const char *dev, int *pages)
{
	static port_doc doc;
	char dir[CFG_STR * 2], path[CFG_STR * 3], where[160], count[16];
	DIR *d;
	struct dirent *e = NULL;
	int bad = 0, p;
	bool pm = true;

	snprintf(dir, sizeof dir, "%s/Ports", P_ROMS);
	d = opendir(dir);
	while (pm || (d && (e = readdir(d)))) {
		const char *name = pm ? PM_SCRIPT : e->d_name;
		size_t l = strlen(name);
		ctl_game g;
		int np;

		if (!pm && (name[0] == '.' || l < 4 || strcasecmp(name + l - 3, ".sh") ||
		            !strcasecmp(name, PM_SCRIPT)))
			continue;
		pm = false;
		snprintf(path, sizeof path, "%s/%s", dir, name);
		port_doc_read(path, &doc);
		g = (ctl_game){ "", "Ports", NULL, "", NULL, "sh", ctl_head_fits, a, true, NULL, &doc, false,
		                CTL_STICK_TABLE };
		np = ctl_game_pages(&g);
		for (p = 0; p < np; p++) {
			menu_row rows[CTL_MAX_ROWS];
			char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
			ctl_buf b;
			int n = ctl_game_rows(&g, p, rows, &b);

			if (ctl_game_prose(&g, p)) continue;
			ctl_glyph_rows(rows, n, keys);
			snprintf(where, sizeof where, "%s, the port %s page %d", dev, doc.title, p);
			snprintf(count, sizeof count, "%d / %d", p + 1, np);
			bad += controls_fit_one(a, where, b.head, np > 1 ? count : NULL, rows, n);
			(*pages)++;
		}
	}
	if (d) closedir(d);
	return bad;
}

static void controls_fit_report(app *a)
{
	static systems_cfg all;
	char path[CFG_STR * 2], where[96], head[80], count[16];
	menu_row rows[CTL_MAX_ROWS];
	bool home = strcmp(shot_controls_fit, "game") != 0;
	bool game = strcmp(shot_controls_fit, "home") != 0;
	const device_profile *pd;
	int k, p, i, e, pages = 0, bad = 0;

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &all)) all.count = 0;
	/* Every handheld linked (each on the host, the device's own on a device),
	 * each set of chords once: the host's desktop has the Brick's */
	for (k = 0; (pd = plat_profile_at(k)); k++) {
		bool seen = false;

		for (int j = 0; j < k; j++)
			if (!memcmp(&plat_profile_at(j)->chords, &pd->chords, sizeof pd->chords)) seen = true;
		if (seen) continue;
		ctl_set_chords(&pd->chords);
		for (p = 0; home && p < CTL_PAGES; p++) {
			char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
			int n = ctl_rows((ctl_page)p, rows);

			/* measured as drawn, the buttons as glyphs */
			ctl_glyph_rows(rows, n, keys);

			snprintf(head, sizeof head, "Controls: %s", ctl_page_name((ctl_page)p));
			snprintf(count, sizeof count, "%d / %d", p + 1, CTL_PAGES);
			snprintf(where, sizeof where, "%s, the home's %s", pd->name,
			         ctl_page_name((ctl_page)p));
			bad += controls_fit_one(a, where, head, count, rows, n);
			pages++;
		}
		for (i = 0; game && i < all.count; i++) {
			const system_cfg *s = &all.systems[i];

			/* e: an .fds game too; st: each D-pad as Stick a game can have
			 * (CTL_STICK_*), on a system whose stick is one switch a game */
			for (e = 0; e < 2; e++) for (int st = CTL_STICK_TABLE; st <= CTL_STICK_ON; st++) {
				ctl_game g = { s->tag, s->name, home_label(s->tag, s->name), s->map,
				               plat_turbo_map(s->tag), e ? "fds" : "", ctl_head_fits, a,
				               is_port(s), NULL, NULL, false, CTL_STICK_TABLE };
				int np;

				if (e && strcmp(s->tag, "NES")) continue;
				if (st && (!rms_stick_offered(s) || !strcmp(s->stick, "toggle"))) continue;
				g.stick = st;
				np = ctl_game_known(s->tag) ? ctl_game_pages(&g) : 0;
				for (p = 0; p < np; p++) {
					char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
					ctl_buf b;
					int n = ctl_game_rows(&g, p, rows, &b);

					if (!ctl_game_prose(&g, p)) ctl_glyph_rows(rows, n, keys);

					snprintf(where, sizeof where, "%s, %s%s%s page %d",
					         pd->name, s->tag, e ? " (fds)" : "",
					         st == CTL_STICK_ON ? " (stick on)" : st == CTL_STICK_OFF ? " (stick off)" : "",
					         p);
					snprintf(count, sizeof count, "%d / %d", p + 1, np);
					bad += controls_fit_one(a, where, b.head, np > 1 ? count : NULL, rows, n);
					pages++;
				}
			}
		}
		if (game) bad += controls_fit_ports(a, pd->name, &pages);
	}
	fprintf(stderr, "controls-fit: %d pages measured, %d do not fit\n", pages, bad);
}

/* --press left|right|a: that key on --menu's row before the menu is drawn,
 * as the player would press it, through the menu's own handler - for a row
 * that changes a setting (Clock, Appearance, Auto Off), not one that opens a
 * screen. The setting is saved as it would be, in the card the shot runs on. */
int shot_key = -1;   /* not shot_press, which is --shot-at's */
static menu_choice shot_pick;          /* --press a on an option row: its list */
static const char *shot_pick_heading;
int shot_gm_choose;     /* --gm-choose: an option row's list, A pressed on it */
int shot_hud_page = -1; /* --hud-page ROW: the On-Screen Stats page */
bool shot_hud_items;    /* --hud-items: its Shows list */
int  shot_hud_pick = -1; /* --hud-pick ROW: that row's list of choices */
const char *shot_hud_shows; /* --hud-shows WORDS: the items it shows */
bool shot_hud_alt;      /* --hud-alt: Shadow and Small, the two-choice rows' other values */
int shot_rows_report;   /* --rows-report: every option page's rows (tools/menu-rows-check.py) */
int shot_hover_sweep;   /* --hover-sweep: Theme's lists show every choice, writing nothing (check-hover-sweep) */
/* --menu's row by its label, found when the menu is built */
const char *shot_menu_label;

/* The status corner clear of what stands near it on the frame just drawn
 * (DESIGN.md 11.12): drawn when --status gives it something to show, and
 * no clock unless --status gives one; its clock 16 from
 * its marks and its marks at x 1000;
 * the home sprite below it; and a games title's
 * capitals and a panel's top a grid step under its row. The corner's box is
 * its clock's text box and its marks, a little wider than their ink. NULL
 * when it is clear, else what it meets. */
static const char *shot_corner_clear(app *a)
{
	static char why[200];
	const SDL_Rect c = g_corner_box;
	const int under = g_corner_box.y + g_corner_box.h + DS_GRID;
	SDL_Rect card;

	if (g_clock_box.w && !shot_st.set) {
		snprintf(why, sizeof why, "a clock drawn with no --status to give one: "
		         "the host's, which changes from one minute to the next");
		return why;
	}
	if (c.w <= 0) {
		/* --status gave it something to show, and every screen a shot can
		 * draw stands under the corner */
		if (shot_st.set && (shot_st.minutes >= 0 || status_marks_width(&shot_st.m)))
			return "no corner drawn, though --status gave it something to show";
		return NULL;
	}
	if (g_clock_box.w && g_clock_box.x != TOPBAR_CLOCK_X) {
		snprintf(why, sizeof why, "the top bar's clock starts at x %d, not at %d",
		         g_clock_box.x, TOPBAR_CLOCK_X);
		return why;
	}
	if (c.x + c.w != TOPBAR_RIGHT) {
		snprintf(why, sizeof why, "the top bar's marks end at x %d, not at %d",
		         c.x + c.w, TOPBAR_RIGHT);
		return why;
	}
#ifdef I18N_PSEUDO
	/* The pseudo-locale is 40% longer on purpose, and meant to overflow */
	if (i18n_lang() != LANG_PSEUDO)
#endif
	if (g_pct_natural > STATUS_PCT_MAX) {
		snprintf(why, sizeof why, "the battery's text is %d units wide, over the %d "
		         "its room holds: it would run into the marks", g_pct_natural, STATUS_PCT_MAX);
		return why;
	}
	if (a->sys.count > 0 && a->screen == SCREEN_SYSTEMS) {
		/* Grid's sprites start under the top bar's band (96): none meets it */
		card = a->home_layout == HOME_GALLERY ? (SDL_Rect){320,128,384,384}
		     : a->home_layout == HOME_GRID ? (SDL_Rect){0,0,0,0}
		                                   : (SDL_Rect){536,112,448,448};
		if (SDL_HasIntersection(&c, &card)) {
			snprintf(why, sizeof why, "the corner (x %d..%d) meets the focused card "
			         "(x %d..%d)", c.x, c.x + c.w, card.x, card.x + card.w);
			return why;
		}
	}
	if (g_title_caps.w > 0 && g_title_caps.x < c.x + c.w &&
	    g_title_caps.x + g_title_caps.w > c.x && g_title_caps.y < under) {
		snprintf(why, sizeof why, "the title's capitals start at y %d, under %d",
		         g_title_caps.y, under);
		return why;
	}
	/* And the header's clock at the left: MP4's album title stands in its
	 * row, between the clock and the marks (header_half) */
	if (g_title_caps.w > 0 && g_clock_box.w &&
	    g_title_caps.x < g_clock_box.x + g_clock_box.w + DS_GRID &&
	    g_title_caps.y < g_clock_box.y + g_clock_box.h + DS_GRID) {
		snprintf(why, sizeof why, "the title's capitals (x %d..%d) meet the clock (x %d..%d)",
		         g_title_caps.x, g_title_caps.x + g_title_caps.w, g_clock_box.x,
		         g_clock_box.x + g_clock_box.w);
		return why;
	}
	if (g_menu_panel.w > 0 && g_menu_panel.x < c.x + c.w &&
	    g_menu_panel.x + g_menu_panel.w > c.x && g_menu_panel.y < under) {
		snprintf(why, sizeof why, "a panel's top is at y %d, under %d", g_menu_panel.y,
		         under);
		return why;
	}
	return NULL;
}

/* Now Playing without a daemon, for --shot. A cover not yet on the card is
 * drawn as missing rather than asked for: there is nobody to ask. */
static void np_shot(app *a)
{
	mu_now mn = { 0 };
	const ml_album *al;
	char next[128] = "";
	int ar, t;

	if (shot_np < 0 || shot_np >= g_muse.nalbums) return;
	al = &g_muse.albums[shot_np];
	t = shot_np_track < al->n ? shot_np_track : al->n - 1;
	for (ar = 0; ar < g_muse.nartists - 1; ar++)
		if (shot_np < g_muse.artists[ar].first + g_muse.artists[ar].n) break;
	mn.state = shot_np_paused ? MU_PAUSED : MU_PLAYING;
	snprintf(mn.title, sizeof mn.title, "%s", g_muse.tracks[al->first + t].name);
	snprintf(mn.artist, sizeof mn.artist, "%s", g_muse.artists[ar].name);
	snprintf(mn.album, sizeof mn.album, "%s", al->name);
	mn.at = shot_np_at;
	mn.len = 236.0;
	mn.index = t;
	mn.count = al->n;
	if (t + 1 < al->n)
		snprintf(next, sizeof next, "%s", g_muse.tracks[al->first + t + 1].name);

	{
		char base[LIB_PATH * 2], path[LIB_PATH * 2 + 8];

		ml_cover_base(g_muse_root, &g_muse, shot_np, base, sizeof base);
		snprintf(path, sizeof path, "%s.jpg", base);
		if (!file_nonempty(path)) {
			snprintf(path, sizeof path, "%s.png", base);
			if (!file_nonempty(path) && g_cov)
				g_cov[shot_np].st = ml_folder_image(g_muse_root, &g_muse, shot_np,
				                                    path, sizeof path)
				                  ? COV_FOLDER : COV_NONE;
		}
	}
	np_cover(a, shot_np);
	if (shot_video >= 0) {
		mp4v_test_picture(a->r, 640, 360);
		mn.vw = 640;
		mn.vh = 360;
		np_draw_video(a, &mn, (float)shot_video);
	} else {
		np_draw(a, &mn, next);
	}
	if (shot_locked) {
		mp4lock l = { 0 };

		mp4lock_engage(&l, 1000, false);
		l.held_from = 1000;
		l.now = 1000 + DS_MS_HOLD_UNLOCK * 3 / 5;
		ui_scrim(a->r, 0);
		lock_panel(a, &l);
	}
}

/* For a shot: every cover not on the card is a cover the music does not
 * carry, since there is no daemon to ask - so those albums draw their
 * generated card rather than an empty slot. */
static void muse_covers_settle(void)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	int i;

	for (i = 0; g_cov && i < g_muse.nalbums; i++) {
		ml_cover_base(g_muse_root, &g_muse, i, base, sizeof base);
		snprintf(p, sizeof p, "%s.jpg", base);
		if (file_nonempty(p)) continue;
		snprintf(p, sizeof p, "%s.png", base);
		if (!file_nonempty(p))
			g_cov[i].st = ml_folder_image(g_muse_root, &g_muse, i, p, sizeof p)
			            ? COV_FOLDER : COV_NONE;
	}
}

/* --look-page: Theme's page, Night Mode's or Screen Colors', over the shelf,
 * as menu_run draws them, the cursor on --look-page's row */
static void shot_draw_look(app *a)
{
	menu_row rows[LK_ROWS];
	const char *heading;
	night_view nv = { .a = a };
	bool night_list = !strcmp(shot_look_page, "night-list");
	bool night = night_list || !strcmp(shot_look_page, "night"), screen = !strcmp(shot_look_page, "screen");
	int n = night ? night_page_build(&nv, rows, NP_ROWS, &heading)
	      : screen ? sc_build(NULL, rows, LK_ROWS, &heading) : look_build(NULL, rows, LK_ROWS, &heading);

	ui_scrim(a->r, 0);
	/* --look-page theme-list ROW: Theme's row ROW's list, Accent's or a
	 * ground's (--pick-at for the cursor, the whole screen showing it) */
	if (!strcmp(shot_look_page, "theme-list")) {
		const menu_choice *ch;
		int row = shot_look_row >= 0 && shot_look_row < n ? shot_look_row : 0;

		ch = menu_choice_at(theme_choices(), n, row);
		if (ch) shot_list_draw(a, rows[row].label, ch, menu_std_width(a), MENU_ZYPEROS);
		return;
	}
	if (night_list) {
		/* A Night Mode row's list, as A opens it (menu_pick) */
		const int row = shot_look_row >= 0 && shot_look_row < n ? shot_look_row : 0;
		const menu_choice *ch = menu_choice_at(night_page_choices(), n, row);

		if (ch) shot_list_draw(a, rows[row].label, ch, menu_std_width(a), MENU_ZYPEROS);
		return;
	}
	if (!strcmp(shot_look_page, "screen-looks")) {
		/* The Look's list, its picture beside it (--pick-at for the cursor) */
		menu_choice ch;

		sc_look_choice(&ch);
		shot_list_draw(a, T(S_SCREEN_LOOK), &ch, menu_most_w(), MENU_ZYPEROS);
		return;
	}
	if (screen) {
		g_menu_side = sc_side;
		g_menu_side_w = SC_SIDE_W;
	} else if (!night) {
		g_row_mark = look_row_mark;
		g_menu_header = look_header;
		g_menu_header_h = 150;
	}
	menu_draw(a, heading, rows, n, shot_look_row < n ? shot_look_row : 0,
	          screen ? menu_most_w() : menu_std_width(a), MENU_ZYPEROS);
	g_row_mark = NULL;
	g_menu_header = NULL;
	g_menu_side = NULL;
	g_menu_side_w = 0;
}

/* Everything a shot draws, in the theme that is on. */
int   g_shot_saver = -1;          /* --saver [cover]: 0 video, 1 cover */
float g_shot_saver_alpha = 1;     /* --saver-alpha F: how far it has faded up */
const char *g_shot_saver_pic;     /* --saver-pic FILE: its picture, a screenshot */

static void shot_compose_once(app *a);

/* A shot's picture; composed again where an open list's cursor showed its
 * choice on the whole screen in the first (shot_list_draw) */
static void shot_compose(app *a)
{
	shot_compose_once(a);
	if (g_shot_hover_again) {
		g_shot_hover_again = false;
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
		SDL_RenderClear(a->r);
		shot_compose_once(a);
	}
}

static void shot_compose_once(app *a)
{
	drawn_forget();
	ui_fit_frame();
	if (g_shot_saver >= 0) {
		saver_shot(a);
		return;
	}
	if (shot_keyboard_panel) {
		kb_pose pose = keyboard_panel(&shot_keyboard, KB_PANEL_FRAME, false);

		if (shot_keyboard.closed && !pose.moving) {
			draw_shelf(a);
			zyperos_menu_draw(a, shot_menu_sel);
		} else {
			kb_preview_panel(a->r, kb_title_shown(), shot_kb_text,
			                 shot_kb_layer, shot_kb_row, shot_kb_col, pose);
		}
		return;
	}
	if (shot_poweroff) {
		poweroff_frame(a, shot_at > 0 ? (unsigned)shot_at : 0u);
		return;
	}
	muse_covers_settle();
	if (shot_menu && shot_menu_label) {
		menu_row rows[MENU_MAX_ROWS];
		menu_bufs b;
		const char *heading;
		lang was = i18n_lang();
		int n, k;

		/* The name as typed is English's, and the row is the same row in
		 * every language: found in English, drawn in the shot's */
		i18n_set(LANG_EN);
		n = menu_build(a, a->screen, a->sys_cursor, rows, &b, &heading);
		for (k = 0; k < n && !(rows[k].label && !strcmp(rows[k].label, shot_menu_label)); k++) ;
		/* In MENU, on whichever tab has it (sys_menu.h) */
		if (k == n && a->screen != SCREEN_GAMES) {
			int t, at = g_tabs.at;

			for (t = 0; t < PT_TABS && k == n; t++) {
				g_tabs.at = t;
				n = menu_build(a, a->screen, a->sys_cursor, rows, &b, &heading);
				for (k = 0; k < n && !(rows[k].label && !strcmp(rows[k].label, shot_menu_label)); k++) ;
			}
			if (k == n) {
				g_tabs.at = at;
				n = menu_build(a, a->screen, a->sys_cursor, rows, &b, &heading);
				k = n;
			}
		}
		i18n_set(was);
		if (k == n) {
			char all[32], own[32], key[CFG_STR + 16];
			const char *tag = a->sys.count > 0 ? a->sys.systems[a->sys_cursor].tag : "";

			fprintf(stderr, "--menu: no row called %s\n", shot_menu_label);
			/* What the rows were built from: a row that comes and goes with
			 * a setting (Strength with the Shader) says whether the setting
			 * was there to be read */
			snprintf(key, sizeof key, "shader.%s", tag);
			db_get_str(db_dev(), "launcher_shader", all, sizeof all, "(none)");
			db_get_str(db_dev(), key, own, sizeof own, "(none)");
			fprintf(stderr, "--menu: built with the settings %s, launcher_shader=%s, %s=%s, "
			        "the shelf's shader %d, %d systems\n",
			        db_dev() ? "open" : "NOT OPEN", all, key, own,
			        a->sys.count > 0 ? a->view[a->sys_cursor].shader : -1, a->sys.count);
			shot_failed = 1;
		}
		shot_menu_sel = k < n ? k : 0;
		shot_menu_label = NULL;   /* found once, for --flip's second draw too */
	}
	if (shot_menu && shot_key >= 0) {
		sysmenu_ctx c;
		const menu_choice *ch = NULL;

		memset(&c, 0, sizeof c);
		c.a = a;
		c.screen = a->screen;
		/* A on an option row, as the runner answers it (menu_choose): its
		 * list over the shelf for more than two, the other one for two */
		if (shot_key == IN_ACCEPT) {
			static menu_row mr[MENU_MAX_ROWS];
			const char *mh;
			int mn = menu_build(a, a->screen, a->sys_cursor, mr, &c.bufs, &mh);

			ch = menu_choice_at(c.bufs.choice, mn, shot_menu_sel);
			if (ch && !ch->own && (ch->n > 2 || ch->list)) {
				shot_pick = *ch;
				shot_pick_heading = mr[shot_menu_sel].label;
			} else if (ch && !ch->own && ch->n == 2) {
				sysmenu_key(a, &c, ch->cur == 0 ? IN_RIGHT : IN_LEFT, shot_menu_sel);
			} else {
				ch = NULL;
			}
		}
		if (!ch) sysmenu_key(a, &c, (in_button)shot_key, shot_menu_sel);
		shot_key = -1;   /* once: --flip composes the shot more than once */
		/* L1 or R1 moved MENU's tab and its cursor, as the runner would;
		 * the shot is the tab at rest */
		if (g_menu_sel_to >= 0) {
			shot_menu_sel = g_menu_sel_to;
			g_menu_sel_to = -1;
			g_tabs.t0 = 0;
		}
	}
	draw_shelf(a);
	if (shot_music_page > -2) {
		bgm_view v;
		menu_row rows[BGM_ROWS + 3];
		const char *heading;
		int n, k, sel = shot_music_page;

		memset(&v, 0, sizeof v);
		bgm_scan_now(&v.list);
		n = bgm_build(&v, rows, BGM_ROWS + 3, &heading);
		/* Each step a frame, the window following the cursor as it does
		 * on the device; the last one is the shot, drawn over the shelf
		 * again */
		for (k = 0; k <= shot_music_steps; k++) {
			if (k) draw_shelf(a);
			ui_scrim(a->r, 0);
			menu_draw(a, heading, rows, n, sel, menu_std_width(a), MENU_ZYPEROS);
			if (sel >= 0) sel = menu_step_round(rows, n, sel, 1);
		}
		bgm_free(&v.list);
		return;
	}
	if (shot_language > -2) {
		static unsigned char snd[LANG_COUNT];
		menu_row rows[LANG_COUNT];
		const char *heading;
		int n = language_build(snd, rows, LANG_COUNT, &heading);

		ui_scrim(a->r, 0);
		menu_draw(a, heading, rows, n, shot_language >= 0 ? shot_language : (int)i18n_lang(),
		          menu_std_width(a), MENU_ZYPEROS);
		return;
	}
	if (shot_media) {
		media_shot(a);
		return;
	}
	if (shot_storage) {
		storage_shot(a);
		return;
	}
	if (shot_update >= 0) {
		su_page p;
		char notes[UP_NOTES][UP_NOTE_W];
		const char *np[UP_NOTES];
		int nn = up_notes(notes), i;

		for (i = 0; i < UP_NOTES; i++) np[i] = notes[i];

		su_page_build(up_view(true), &p);
		update_page_draw(a, T(S_UPDATEPAGE_TITLE), &p, np, nn, NULL);
		return;
	}
	if (g_pm_shot >= 0) {
		static system_cfg ports = { .folder = "Ports", .core = "port" };
		char notes[PM_NOTES][120];
		su_page p;
		int nn;

		SDL_Texture *icon = pm_icon_tex(a);
		const pm_view *v = pm_view_read(&ports, true);

		pm_page_build(v, &p, notes, &nn);
		{
			const char *np[PM_NOTES];
			int i;

			for (i = 0; i < PM_NOTES; i++) np[i] = notes[i];
			update_page_draw(a, T(v->repair ? S_PORTMASTER_REINSTALL_TITLE : S_PORTMASTER_TITLE),
			                 &p, np, nn, icon);
		}
		if (icon) SDL_DestroyTexture(icon);
		return;
	}
	if (shot_about >= 0) {
		static about_view v;
		static bool made;

		/* Once: a shot that draws more than one frame keeps the photo */
		if (!made) {
			made = true;
			about_refresh(&v, 2700000u);
			about_pages(a, &v);
			v.width = about_width(a, &v);
			if (shot_about_scroll >= 0) {
				int over = v.nstory - menu_notes_fit();

				v.story_scroll = over > 0 ? (int)(shot_about_scroll * over * menu_note_h()) : 0;
			}
		}
		ui_scrim(a->r, 0);
		/* The thanks, --about-scroll of the way down: as a share of their
		 * span, which is the panel's to measure */
		if (shot_about == 3 && shot_about_scroll >= 0) g_menu_scroll_frac = shot_about_scroll;
		about_draw(a, &v, shot_about);
		g_menu_scroll_frac = -1;
		return;
	}
	if (shot_menu && shot_tab_from >= 0) {
		if (!g_clock_held) { g_clock_held = true; g_clock_at = plat_now_ms(); }
		g_tabs.from = shot_tab_from;
		g_tabs.dir = (g_tabs.at - shot_tab_from + PT_TABS) % PT_TABS == 1 ? +1 : -1;
		g_tabs.t0 = g_clock_at - (unsigned)shot_tab_ms;
		if (!g_tabs.t0) g_tabs.t0 = 1;
		g_menu_epoch++;
		shot_tab_from = -1;
	}
	if (shot_menu && shot_menu_steps) {
		menu_row rows[MENU_MAX_ROWS];
		menu_bufs b;
		const char *h;
		int n = menu_build(a, a->screen, a->sys_cursor, rows, &b, &h);
		int k, d = shot_menu_steps > 0 ? 1 : -1;

		if (!g_clock_held) { g_clock_held = true; g_clock_at = plat_now_ms(); }
		for (k = 0; k < (shot_menu_steps > 0 ? shot_menu_steps : -shot_menu_steps); k++) {
			zyperos_menu_draw(a, shot_menu_sel);
			g_clock_at += 1000;
			shot_menu_sel = menu_step_round(rows, n, shot_menu_sel, d);
		}
		/* and the last press drawn, then let settle: the shot is the list
		 * at rest after it */
		zyperos_menu_draw(a, shot_menu_sel);
		g_clock_at += 1000;
		shot_menu_steps = 0;    /* once: --flip composes the shot twice */
		draw_shelf(a);
	}
	/* After --close's press the menu is the shelf's to draw leaving, and
	 * after --swap's it has given way to the page below */
	if (shot_menu) {
		if (shot_pressed && shot_press == SHOT_CLOSE) panel_leave_draw(a, 0);
		else if (!shot_pressed || shot_press != SHOT_SWAP) {
			if (!shot_pick.n) zyperos_menu_draw(a, shot_menu_sel);
		}
		/* --press a on an option row of more than two: its list, which
		 * opens over the shelf as MENU > Language does (menu_pick) */
		if (shot_pick.n) {
			ui_scrim(a->r, 0);
			shot_list_draw(a, shot_pick_heading, &shot_pick, menu_std_width(a), MENU_ZYPEROS);
		}
	}
	if (shot_look_page) shot_draw_look(a);
	if (shot_homesys >= 0) homesys_shot_draw(a, shot_homesys);
	if (shot_lights >= 0) lights_shot_draw(a, shot_lights);
	if (shot_slots) shot_draw_slots(a);
	if (shot_gamemenu && a->sys.count > 0) shot_draw_gamemenu(a);
	else if (g_shot_p2 && a->sys.count > 0) shot_draw_player2(a);
	if (shot_gamecontrols >= 0 && a->sys.count > 0) shot_draw_gamecontrols(a);
	if (shot_controls_fit) controls_fit_report(a);
	if (shot_rows_report) {
		sysmenu_rows_report(a);
		gm_rows_report(a);
		snaps_rows_report(a);
		downloads_rows_report(a);
		bgm_rows_report(a);
		hud_rows_report(a);
		shot_rows_report = 0;   /* once: --flip composes the shot twice */
	}
	if (shot_hover_sweep) {
		sysmenu_hover_sweep(a);
		shot_hover_sweep = 0;
	}
	if (shot_handheld_box) handheld_box_report();
	/* Over **scrim.menu**, as the screens they picture are: kb_prompt's
	 * backdrop, wait_panel and the achievement list opened over the shelf. */
	if (shot_cheevos && shot_ingame) {
		/* as opened from the in-game menu: over a paused game */
		SDL_Texture *bg = fake_frame(a->r, 4.0f / 3);

		paused_backdrop(a, bg, 1.0f);
		if (bg) SDL_DestroyTexture(bg);
	} else if (shot_kb || shot_wait || shot_cheevos) {
		ui_scrim(a->r, 0);
	}
	if (shot_cheer >= 0) {
		cheer_ctx c;

		zr_init(&c.z, 0, 0);
		zr_works(&c.z, 0);
		c.now = (unsigned)shot_cheer;
		eh_cheer_draw(a->r, games_on_card(a), (unsigned)shot_cheer, cheer_zyper, &c);
	}
	if (shot_kb) {
		if (shot_at >= 0)
			kb_preview_press(a->r, kb_title_shown(), shot_kb_text,
			                 shot_kb_layer, shot_kb_row, shot_kb_col, (unsigned)shot_at);
		else kb_preview(a->r, kb_title_shown(), shot_kb_text,
		                shot_kb_layer, shot_kb_row, shot_kb_col);
	}
	/* The in-game notice is not drawn by this process - Diatom composites it
	 * over the game - so it cannot be screenshotted like the rest. Rendered
	 * to its wire format and read straight back, which is the same pixels
	 * Diatom will show and therefore the thing worth looking at. */
	/* The achievements list, with sample data. It is only reachable from the
	 * in-game menu, so without this it can only be judged on a device with a
	 * game running - and it took two rounds of that to notice its heading was
	 * clipped at both ends. */
	/* The one-line panel behind "Scanning...", "Connecting...", "Signing
	 * in..." and "Looking up this game...". Five flows use it and none of them
	 * could be rendered. */
	if (shot_wait) {
		menu_row row = { shot_wait_msg ? shot_wait_msg : T(S_LIBRARY_SCANNING), NULL, false };

		menu_message(a, shot_wait, row, MENU_ZYPEROS);
	}
	if (shot_info == 4) info_buttons_preview(a, shot_gm_pick);
	else if (shot_info) info_preview(a, true, shot_info == 2 ? T(S_GAMEMENU_DELETE)
	                                     : shot_info == 3 ? T(S_GAMEMENU_RENAME) : NULL);
	if (shot_delete_ask) delete_ask_preview(a);
	if (shot_gm_said >= 0) gm_said_preview(a, shot_gm_said);
	if (shot_controls >= 0 && (shot_press != SHOT_SWAP || shot_pressed)) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80], count[16];
		ctl_page p = (ctl_page)(shot_controls % CTL_PAGES);
		float k = 1.0f;
		int n;

		char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];

		ctl_set_chords(&plat_profile()->chords);
		n = ctl_rows(p, rows);
		ctl_glyph_rows(rows, n, keys);

		snprintf(head, sizeof head, T(S_CONTROLS_HEADING), ctl_page_name(p));
		snprintf(count, sizeof count, "%d / %d", (int)p + 1, CTL_PAGES);
		/* --swap's page, arriving over the menu that gave way to it */
		if (shot_press == SHOT_SWAP && shot_lv) panel_frame(a, shot_lv, &k);
		ui_scrim_k(a->r, 0, k);
		menu_draw_caption(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS, NULL, false,
		                  0, count);
	}
	if (shot_sgdb >= 0) {
		static const char *const st[] = { NULL, "works", "refused" };
		menu_row rows[SG_ROWS];
		sg_row ids[SG_ROWS];
		int n = sys_menu_sgdb_rows(shot_sgdb ? "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b"
		                                     : NULL, st[shot_sgdb], rows, ids);

		ui_scrim(a->r, 0);
		menu_draw(a, "SteamGridDB", rows, n, 0, menu_std_width(a), MENU_ZYPEROS);
	}
	if (shot_scraper == 3) {
		static snaps_bufs b;
		snaps_console c[5] = { { "SNES", true, 212000000ull }, { "Game Boy", true, 96000000ull },
		                       { "PlayStation", false, 0 }, { "Genesis", true, 154000000ull },
		                       { "NES", true, 188000000ull } };
		snaps_ui u = { .play = true, .ss_in = true, .ss_have = true,
		               .sound = SNAP_MUTED,
		               .c = c, .nc = 5, .total = 650000000ull };
		menu_row rows[SN_ROWS];
		sn_row ids[SN_ROWS];
		int con[SN_ROWS], n = sys_menu_snaps_rows(&u, &b, rows, ids, con);

		ui_scrim(a->r, 0);
		menu_draw(a, T(S_SNAPS_HEADING), rows, n, 4, menu_std_width(a), MENU_ZYPEROS);
	} else if (shot_scraper == 4) {
		menu_row rows[SSP_ROWS];
		ssp_row ids[SSP_ROWS];
		int n = sys_menu_ss_rows(true, "tarik", "19796 left today", rows, ids);

		ui_scrim(a->r, 0);
		menu_draw(a, "ScreenScraper", rows, n, 0, menu_std_width(a), MENU_ZYPEROS);
	}
	if (shot_np >= 0) np_shot(a);
	if (shot_usb >= 0) {
		/* The question a computer gets, at rest, as usb_ask asks it: all three tiles,
		 * or with --usb-ask N plain the two a card without the development image has. In the language that is on, since the
		 * overflow is in the translation */
		menu_row tiles[4];
		int what[4], marks[4];
		int nt = usb_ask_tiles(tiles, what, marks, shot_usb_dev);

		ui_scrim(a->r, 0);
		tiles_panel(a, PANEL_TILES_GRID, T(S_XFER_COMPUTER_CONNECTED), tiles, marks, nt, shot_usb % nt,
		            PANEL_REST.panel);
	}
	if (shot_scraper_page >= 0) {
		scraper_page_shot(a);
		return;
	}
	/* MENU > Downloads (--downloads STATE) */
	if (shot_downloads >= 0) {
		downloads_shot(a);
		return;
	}
	/* MENU > Achievements' page (--radl-page STATE) */
	if (shot_radl_page >= 0) {
		radl_page_shot(a);
		return;
	}
	if (shot_usbdrive >= 0 && shot_usbdrive < UD_FRAMES) {
		usb_drive_draw(a, shot_usbdrive);
		return;
	}
	/* usbdrive's atlas, and where its glyphs are, on stdout */
	if (shot_usbdrive == UD_FRAMES) {
		usb_drive_atlas(a, NULL, NULL, stdout);
		return;
	}
	if (shot_power >= 0) {
		menu_row rows[UP_TILES + 1];

		ui_scrim(a->r, 0);
		if (shot_power < UP_TILES) {
			int nt = usb_power_rows(rows);

			tiles_panel(a, PANEL_LIST, T(S_XFER_USB_DEVICE_CONNECTED), rows, UP_MARK, nt, shot_power,
			            PANEL_REST.panel);
		} else {
			/* The answer's message over the shelf, as the overlay draws it */
			g_replug.say = shot_power == UP_TILES ? USBPOWER_SAY_COMPUTER
			             : shot_power == UP_TILES + 1 ? USBPOWER_SAY_PHONE : USBPOWER_SAY_IPHONE;
			replug_draw_at(a->r);
		}
	}
	if (shot_usb_said) {
		/* USB Accessories turned to Automatic: its message, as
		 * usb_auto_message lays it out */
		char lines[4][LB_LINE];
		menu_row rows[4];
		int n;

		ui_scrim(a->r, 0);
		n = usb_auto_lines(a, lines, rows);
		menu_draw(a, T(S_SYSMENU_USB_ACCESSORIES), rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
	}
	if (shot_usb_offer) {
		/* Zyper's offer of Automatic, as usb_offer_show lays it out, the
		 * cursor on Keep Asking */
		char lines[USB_OFFER_LINES][LB_LINE];
		menu_row rows[USB_OFFER_LINES + 3];
		int n;

		ui_scrim(a->r, 0);
		n = usb_offer_rows(a, lines, rows);
		rows[n]     = (menu_row){ T(S_SYSMENU_USB_AUTO), NULL, true };
		rows[n + 1] = (menu_row){ T(S_XFER_USB_OFFER_KEEP), NULL, true };
		menu_draw(a, T(S_XFER_USB_OFFER), rows, n + 2, n + 1, menu_std_width(a), MENU_ZYPEROS);
	}
	if (shot_datetime >= 0) {
		/* Date & Time's page as clock_build builds it, at a time of its own,
		 * in the language that is on */
		static const char *const ids[4] = { "America/New_York", "America/Sao_Paulo",
		                                     "America/Bahia_Banderas", "America/New_York" };
		struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = shot_datetime_mon - 1, .tm_mday = 3,
		                 .tm_hour = 18, .tm_min = 5, .tm_isdst = -1 };
		menu_row rows[CLOCK_ZONE_LIST_MAX];
		clock_ui c;
		int k = shot_datetime % 9, n;

		memset(&c, 0, sizeof c);
		ui_scrim(a->r, 0);
		if (k >= 5) {
			static const char *const list_ids[4] = { "Pacific/Honolulu", "Africa/Johannesburg",
			                                         "America/Sao_Paulo", "America/Bahia_Banderas" };
			static zone_list_ui z;
			const char *zid = list_ids[k - 5];
			bool autoz = k >= 7;

			n = zone_list_rows(&z, zid, autoz, autoz ? zid : "", rows, CLOCK_ZONE_LIST_MAX);
			menu_draw(a, T(S_CLOCK_TIME_ZONE), rows, n, clock_zone_list_row(zid, autoz),
			          menu_std_width(a), MENU_ZYPEROS);
		} else if (k == 4) {
			char lines[2][LB_LINE];

			n = zone_auto_lines(a, lines, rows);
			menu_draw(a, T(S_CLOCK_TIME_ZONE), rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
		} else {
			n = clock_page_rows(&c, mktime(&tm), ids[k], k < 3,
			                    k == 1 || k == 2 ? ids[k] : "", true, rows, CLOCK_ROWS + 4);
			menu_draw(a, T(S_DATETIME_TITLE), rows, n, CLOCK_ROW_ZONE, menu_std_width(a),
			          MENU_ZYPEROS);
		}
	}
	if (shot_dev >= 0) {
		/* Developer Mode's page, as dev_screen builds it, the message the
		 * unlock leaves and SSH over Wi-Fi's, in the language that is on.
		 * The cursor rests on SSH over Wi-Fi, as the page opens. */
		char lines[DEV_LINES][DEV_LINE], said[32];
		menu_row rows[DEV_ROWS];
		int n;

		ui_scrim(a->r, 0);
		snprintf(said, sizeof said, TN(S_SSH_SIGNED_IN, 1), 1);
		if (shot_dev < 3) {
			n = dev_rows(rows, lines, false, false, true, "10.42.0.1", NULL, "x7k4mq2d",
			             shot_dev == 0 ? 1 : shot_dev == 1 ? 12 : 0, T(S_SSH_WAITING));
			menu_draw(a, T(S_XFER_DEV_HEADING), rows, n, DEV_ROW_WIFI, menu_std_width(a), MENU_ZYPEROS);
		} else if (shot_dev == 4) {
			/* over Wi-Fi, one key, somebody signed in */
			n = dev_rows(rows, lines, true, true, true, "192.168.110.68", "zyperos.local", "",
			             1, said);
			menu_draw(a, T(S_XFER_DEV_HEADING), rows, n, DEV_ROW_WIFI, menu_std_width(a), MENU_ZYPEROS);
		} else if (shot_dev == 5) {
			char said_lines[4][LB_LINE];

			n = dev_wifi_lines(a, said_lines, rows);
			menu_draw(a, T(S_DEVMODE_WIFI), rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
		} else if (shot_dev == 6) {
			/* Wi-Fi up, SSH over Wi-Fi on, and no key on the card */
			n = dev_rows(rows, lines, true, true, false, "", NULL, "", 0, T(S_SSH_NEEDS_KEY));
			menu_draw(a, T(S_XFER_DEV_HEADING), rows, n, DEV_ROW_WIFI, menu_std_width(a), MENU_ZYPEROS);
		} else if (shot_dev == 7) {
			/* Wi-Fi up, SSH over Wi-Fi off: SSH stops, as it always has */
			n = dev_rows(rows, lines, false, false, false, "", NULL, "", 1, T(S_SSH_NOT_WITH_WIFI));
			menu_draw(a, T(S_XFER_DEV_HEADING), rows, n, DEV_ROW_WIFI, menu_std_width(a), MENU_ZYPEROS);
		} else if (shot_dev == 8) {
			/* the cursor on Developer Mode */
			n = dev_rows(rows, lines, false, false, true, "10.42.0.1", NULL, "x7k4mq2d", 1,
			             T(S_SSH_WAITING));
			menu_draw(a, T(S_XFER_DEV_HEADING), rows, n, DEV_ROW_MODE, menu_std_width(a), MENU_ZYPEROS);
		} else {
			rows[0] = (menu_row){ T(S_DEVMODE_IS_ON), NULL, false };
			rows[1] = (menu_row){ T(S_DEVMODE_WHERE), NULL, false };
			menu_draw(a, T(S_SYSMENU_DEVELOPER), rows, 2, -1, 0, MENU_ZYPEROS);
		}
	}
	if (shot_xfer_ask >= 0) {
		/* The question while files move, from the same words xs_ask shows */
		const char *const *l = shot_xfer_game ? XS_GAME_L : XS_STOP_L;
		const char *const *w = shot_xfer_game ? XS_GAME_W : XS_STOP_W;
		int k = shot_xfer_ask % 3;
		char said[128];
		menu_row rows[6] = {
			{ l[0], NULL, true }, { l[1], NULL, true }, { l[2], NULL, true },
			MENU_NOTE(said), MENU_NOTE(w[k]),
			MENU_NOTE(shot_xfer_game ? XS_GAME_LEGEND : XS_STOP_LEGEND),
		};

		snprintf(said, sizeof said, T(S_XFER_RECEIVING), "Tobu Tobu Girl.gb");
		ui_scrim(a->r, 0);
		tiles_panel(a, PANEL_TILES, T(S_XFER_STILL_MOVING), rows, shot_xfer_game ? XS_GAME_M : XS_STOP_M, 6, k,
		            PANEL_REST.panel);
	}
	if (shot_steps_way) {
		static const struct { const char *name; int step, sub, hint; } ST[] = {
			{ "unplugged",  LS_LINK,    0, LH_NONE },
			/* a computer that has not taken the handheld yet, and one
			 * that took it and has not opened its network: the loading bar */
			{ "taking",     LS_LINK,    1, LH_NONE },
			{ "opening",    LS_LINK,    2, LH_NONE },
			{ "nocomputer", LS_LINK,    0, LH_NO_COMPUTER },
			{ "noanswer",   LS_LINK,    1, LH_NO_ANSWER },
			{ "nodriver",   LS_LINK,    2, LH_NO_DRIVER },
			{ "address",    LS_ADDRESS, 0, LH_NONE },
			{ "noaddress",  LS_ADDRESS, 0, LH_NO_ADDRESS },
			{ "browser",    LS_BROWSER, 0, LH_NONE },
			{ "nobrowser",  LS_BROWSER, 0, LH_NO_BROWSER },
			{ "nonetwork",  LS_LINK,    3, LH_NO_NETWORK },
			{ "waits",      LS_LINK,    4, LH_NONE },
			{ "replug",     LS_LINK,    4, LH_REPLUG },
			/* the page open in a browser, its PIN not in yet */
			{ "pin",        LS_DONE,    0, LH_NONE },
		};
		const bool phone = !strcmp(shot_steps_way, "phone");
		const bool cable = strcmp(shot_steps_way, "wifi") != 0 && !phone;
		const char *addr = cable ? "10.42.0.1" : phone ? "192.168.42.37" : "192.168.1.42";
		link_steps st = { 0 };
		menu_row rows[STEP_ROWS];
		char head[192], hint[320], lines[3][LB_LINE];
		int k, n, h = LH_NONE;

		st.started = true;
		for (k = 0; k < (int)(sizeof ST / sizeof ST[0]); k++)
			if (!strcmp(ST[k].name, shot_steps_state)) {
				st.step = ST[k].step;
				st.sub = ST[k].sub;
				h = ST[k].hint;
			}
		/* Off Wi-Fi the heading says so, with no name, as the screen does */
		const char *name = shot_steps_noname || phone ? "" : "zyperos.local";

		if (!cable && st.step == LS_LINK) hare_head(head, sizeof head, false, NULL, T(S_XFER_WIFI_GONE), "4071");
		else hare_head(head, sizeof head, cable, name, addr, "4071");
		draw_shelf(a);
		ui_scrim(a->r, 0);
		/* As the screen chooses (xfer_screen_way): the way in, or the steps
		 * while one before the browser stalls */
		if (xs_way_hint(h)) {
			n = xs_step_rows(a, rows, lines, hint, sizeof hint, &st, h, cable,
			                 b_legend(S_KEYHINT_CLOSE));
			menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
		} else {
			xs_way_set(cable, phone, name, shot_steps_addr ? shot_steps_addr : addr, "4071");
			n = xs_way_rows(rows, &st, cable || phone, b_legend(S_KEYHINT_CLOSE));
			/* over the cable, before the address: the loading bar, its
			 * step waited on for a second and a half */
			if (cable && st.step < LS_BROWSER)
				xs_load_draw(a, rows, n, xs_load_target(&st, 1500));
			else
				xs_way_draw(a, rows, n);
		}
	}
	if (shot_hare || (shot_hot && shot_hot_in)) {
		/* The tiles as the screen sets them (xs_move_set), from a
		 * session's numbers over Wi-Fi (--hare-move): nothing moving after
		 * a file came in, a file coming in (its bar at 62%), one going
		 * out, both, closing after the one coming in, Play after this
		 * file's wait, Wi-Fi gone with the totals kept, and Wi-Fi gone in
		 * the middle of a file, paused with its part kept (its bar at 58%,
		 * hare_paused). `now` is the Now row's words for the file
		 * (shot_hare_head). */
		static const struct {
			const char *state;
			bool on[2];
			unsigned long long rate[2];
			int bar;                /* per mille of the upload; -1 none */
			str_id now;
		} MV[] = {
			{ "idle",    { false, false }, { 0, 0 },                   -1,  S_HARE_RECEIVED },
			{ "in",      { true, false },  { 2400000ull, 0 },          620, S_HARE_RECEIVING },
			{ "out",     { false, true },  { 0, 3100000ull },          -1,  S_HARE_SENDING },
			{ "both",    { true, true },   { 1800000ull, 1200000ull }, 350, S_HARE_RECEIVING },
			{ "closing", { true, false },  { 2400000ull, 0 },          840, S_XFER_CLOSING_AFTER },
			{ "finish",  { true, false },  { 2400000ull, 0 },          840, S_XFER_FINISHING },
			{ "gone",    { false, false }, { 0, 0 },                   -1,  S_HARE_RECEIVED },
			{ "paused",  { false, false }, { 0, 0 },                   580, S_HARE_PAUSED },
		};
		int m = 1, k;
		xs_flow flow[2] = { { 186000000ull, 0, 0, 0, false }, { 41000000ull, 0, 0, 0, false } };
		char now[192];

		for (k = 0; k < (int)(sizeof MV / sizeof MV[0]); k++)
			if (shot_hare_move && !strcmp(shot_hare_move, MV[k].state)) m = k;
		for (k = 0; k < 2; k++) {
			flow[k].active = MV[m].on[k];
			flow[k].rate = MV[m].rate[k];
		}
		xs_move_set(flow, MV[m].bar, MV[m].bar >= 0 ? 1000 : 0);
		snprintf(now, sizeof now, T(MV[m].now), shot_hare_head);
		if (shot_hot && shot_hot_in) {
			/* the hotspot once someone came in: the news, its network,
			 * password and PIN in the heading (hot_head) */
			menu_row rows[5];
			char head[192];
			int n = xs_page_rows(rows, shot_hot_phones, NULL, shot_hot_now, S_KEYHINT_CLOSE);

			hot_head(head, sizeof head, menu_std_width(a), shot_hot_ssid, shot_hot_pass, "4071");
			draw_shelf(a);
			ui_scrim(a->r, 0);
			xs_page_show(a, head, rows, n);
		} else if (m == 5) {
			/* Play after this file's wait, as xs_before_game draws it */
			menu_row rows[3];

			draw_shelf(a);
			ui_scrim(a->r, 0);
			xs_page_show(a, XFER_NAME_WIFI, rows, xs_finish_rows(rows, now, sizeof now, shot_hare_head));
		} else {
			const bool gone = m == 6 || m == 7;

			hare_preview(a, gone ? "" : "zyperos.local", gone ? T(S_XFER_WIFI_GONE) : shot_hare_addr,
			             "4071", shot_hare_who,
			             /* the cable's address is USB File Transfer, with SSH */
			             !strcmp(shot_hare_addr, "10.42.0.1") ? "1 signed in" : NULL, now,
			             m == 4 ? S_KEYHINT_HIDE
			             : MV[m].on[0] || MV[m].on[1] || m == 7 ? S_KEYHINT_KEEP_GOING_OR_STOP
			             : S_KEYHINT_CLOSE);
		}
	} else if (shot_hot)
		hot_preview(a, shot_hot_ssid, shot_hot_pass, shot_hot_phones, shot_hot_now);
	/* The synopsis card, from the card's own games table - so a shot of it is a
	 * picture of real scraped text rather than a fixture. */
	if (shot_syn) {
		sysview *v = &a->view[a->sys_cursor];
		const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)];
		game_meta m;

		if (v->list.count > 0 &&
		    db_game_get(db_lib(), s->folder, v->list.items[v->cursor].file, &m) &&
		    m.synopsis[0]) {
			char lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
			menu_row rows[SYN_ROWS];
			int fixed = menu_std_width(a);
			int loop_at = 0;
			int nl = syn_layout(m.synopsis, fixed, lines, rows, &loop_at);

			ui_scrim(a->r, 0);
			menu_draw_ex(a, v->list.items[v->cursor].title, rows, nl, -1, fixed,
			             s->accent, NULL, false, loop_at);
		}
	}
	if (shot_cheevos) {
		/* Real titles, and the long ones are the point: a set's names run past
		 * the panel often, and this shot drew eight short ones - so the screen
		 * could be looked at without a device and still not show the thing
		 * worth looking at. Two of the three below are real RetroAchievements
		 * titles from sets on this card. */
		static const struct { const char *t; int p; bool got; } sample[] = {
			{ "The Path to Disaster", 5, true },
			{ "Hold On To Your Potatoes, We're In For A Bumpy Ride", 10, true },
			{ "Violated Heavens", 10, true },
			{ "I Am Not Left Handed Either, But I Can Still Beat You", 25, false },
			{ "Koma Faction's Fall", 25, false },
			{ "Not So Disaster", 10, false },
			{ "Storming The Fortress", 10, false },
			{ "Blazing through the Skies", 10, false },
		};
		int cn = (int)(sizeof sample / sizeof sample[0]), ci;
		menu_row rows[8];
		unsigned vcols[8] = { 0 };
		char vals[8][16], head[192];

		for (ci = 0; ci < cn; ci++) {
			snprintf(vals[ci], sizeof vals[ci], "%d", sample[ci].p);
			rows[ci] = (menu_row){ sample[ci].t, vals[ci], sample[ci].got };
			vcols[ci] = sample[ci].got ? VCOL_EARNED : 0u;
		}
		if (shot_cheevos_waiting > 0)
			snprintf(head, sizeof head, TN(S_CHEEVOLIST_HEADING_WAITING, shot_cheevos_waiting),
			         shot_cheevos_game, 3, 36, shot_cheevos_waiting);
		else
			snprintf(head, sizeof head, T(S_CHEEVOLIST_HEADING),
			         shot_cheevos_game, 3, 36, 25, 415);
		/* Drawn the way cheevos_screen draws it - menu_draw_ex, the earned
		 * colors, the shared width - because a shot that composes the panel
		 * differently from the screen is a picture of something that does not
		 * exist. It used to call menu_draw with a width of 0. */
		for (ci = 0; ci < cn; ci++) shot_badge_got[ci] = sample[ci].got;
		g_badge_r = a->r;
		g_row_icon = shot_row_badge;
		menu_draw_ex(a, head, rows, cn, shot_cheevos_sel, menu_std_width(a),
		             a->tint, vcols, true, 0);
		g_row_icon = NULL;
	}
	if (shot_popup) {
		static char pbody[96];
		char slot[32];

		snprintf(slot, sizeof slot, T(S_SLOTS_SLOT), 2);
		shot_notice = pbody;
		snprintf(pbody, sizeof pbody, "%s", slot);
		if (!strcmp(shot_popup, "saved"))             shot_notice_head = T(S_POPUP_SAVED);
		else if (!strcmp(shot_popup, "loaded"))       shot_notice_head = T(S_POPUP_LOADED);
		else if (!strcmp(shot_popup, "save_failed"))  shot_notice_head = T(S_POPUP_SAVE_FAILED);
		else if (!strcmp(shot_popup, "load_failed"))  shot_notice_head = T(S_POPUP_LOAD_FAILED);
		else if (!strcmp(shot_popup, "slot_empty"))   shot_notice_head = T(S_POPUP_SLOT_EMPTY);
		else if (!strcmp(shot_popup, "no_states")) {
			shot_notice_head = T(S_POPUP_NO_STATES);
			snprintf(pbody, sizeof pbody, "%s", "Super Metroid");
		} else if (!strcmp(shot_popup, "dslang")) {
			/* A DS game started fresh in a new Game Language (ds_resume_fresh) */
			shot_notice_head = T(S_POPUP_DSLANG_HEAD);
			snprintf(pbody, sizeof pbody, "%s", T(S_POPUP_DSLANG_BODY));
		} else if (!strcmp(shot_popup, "corestate")) {
			/* A game started fresh, its resume left by the other core
			 * (core_resume_fresh) */
			shot_notice_head = T(S_POPUP_CORESTATE_HEAD);
			snprintf(pbody, sizeof pbody, "%s", T(S_POPUP_CORESTATE_BODY));
		} else if (!strcmp(shot_popup, "player2_joined") || !strcmp(shot_popup, "player2_left")) {
			/* A friend's browser joining NES Player 2 or leaving it
			 * (p2op_notice) */
			shot_notice_head = T(S_PLAYER2_NOTICE);
			snprintf(pbody, sizeof pbody,
			         T(!strcmp(shot_popup, "player2_joined") ? S_PLAYER2_JOINED : S_PLAYER2_LEFT), 2);
		} else if (!strcmp(shot_popup, "player2_stopped")) {
			/* ...and Player 2 stopping by itself during the game */
			shot_notice_head = T(S_PLAYER2_NOTICE);
			snprintf(pbody, sizeof pbody, "%s", T(S_PLAYER2_STOPPED));
		} else if (!strcmp(shot_popup, "stick_on") || !strcmp(shot_popup, "stick_off")) {
			/* R2 in a PSP game (on_stick) */
			shot_notice_head = T(S_POPUP_STICK);
			snprintf(pbody, sizeof pbody, "%s",
			         T(!strcmp(shot_popup, "stick_on") ? S_POPUP_STICK_ON : S_POPUP_STICK_OFF));
		} else {
			shot_notice_head = T(S_POPUP_SPEED);
			snprintf(pbody, sizeof pbody,
			         shot_popup_half % 2 ? T(S_POPUP_SPEED_HALF) : T(S_POPUP_SPEED_WHOLE),
			         shot_popup_half / 2);
		}
	}
	if (shot_notice) {
		char dt[512];
		FILE *nf;

		snprintf(dt, sizeof dt, "%s.dtov", shot_path);
		if ((shot_popup ? notice_render(shot_notice_head, shot_notice, dt, theme_index())
		     : shot_notice_mark
		     ? notice_render_mark(shot_notice_head, shot_notice, (notice_mark)shot_notice_mark,
		                          dt, theme_index())
		     : notice_render_ex(shot_notice_head, shot_notice, shot_notice_badge,
		                        notice_look(shot_notice_kind), shot_notice_value,
		                        shot_notice_target, dt, theme_index())) &&
		    (nf = fopen(dt, "rb"))) {
			unsigned char hd[8];
			if (fread(hd, 1, 8, nf) == 8 && !memcmp(hd, "DTOV", 4)) {
				int nw = hd[4] | (hd[5] << 8), nh = hd[6] | (hd[7] << 8);
				SDL_Surface *ns = SDL_CreateRGBSurfaceWithFormat(
					0, nw, nh, 32, SDL_PIXELFORMAT_ARGB8888);
				if (ns && fread(ns->pixels, 4, (size_t)nw * nh, nf)
				          == (size_t)nw * nh) {
					SDL_Rect at = { (ZYPEROS_SCREEN_W - nw) / 2,
					                ZYPEROS_SCREEN_H - nh - ZYPEROS_SCREEN_H / 24,
					                nw, nh };
					SDL_Texture *nt = SDL_CreateTextureFromSurface(a->r, ns);
					if (nt) {
						SDL_SetTextureBlendMode(nt, SDL_BLENDMODE_BLEND);
						SDL_RenderCopy(a->r, nt, NULL, &at);
						SDL_DestroyTexture(nt);
					}
				}
				if (ns) SDL_FreeSurface(ns);
			}
			fclose(nf);
			remove(dt);
		}
	}
	/* The song pop-up, on a clock of its own so the frame is the same each
	 * time: shown at T0, drawn --songpop's MS into it */
	if (shot_songpop) {
		const unsigned T0 = 100000;
		const char *bar = strchr(shot_songpop, '|');
		char head[BGM_TAG_MAX], body[BGM_TAG_MAX];

		snprintf(head, sizeof head, "%.*s", bar ? (int)(bar - shot_songpop) : 0, shot_songpop);
		snprintf(body, sizeof body, "%s", bar ? bar + 1 : shot_songpop);
		zr_init(&g_songpop_zyper, T0, 1);
		if (!head[0]) snprintf(head, sizeof head, "%s", BGM_SONG_HEAD_NONE);
		songpop_show(&g_songpop, head, body, song_hold(head, body), T0);
		zr_works(&g_songpop_zyper, T0);
		song_popup_draw(a, T0 + (unsigned)shot_songpop_ms);
	}
	if (shot_heatpop >= 0) {
		const unsigned T0 = 100000;

		heatpop_show(T0);
		if (shot_ranews > 0) {
			snprintf(g_heatpop.line1, sizeof g_heatpop.line1, TN(S_CHEEVOS_SENT, shot_ranews), shot_ranews);
			snprintf(g_heatpop.line2, sizeof g_heatpop.line2, "%s", T(S_CHEEVOS_SENT_WHERE));
		}
		if (shot_upnews) {
			snprintf(g_heatpop.line1, sizeof g_heatpop.line1,
			         T(strcmp(shot_upnews, "ready") ? S_UPDATE_NEWS_AVAILABLE : S_UPDATE_NEWS_READY), "1.0.4");
			snprintf(g_heatpop.line2, sizeof g_heatpop.line2, "%s", T(S_UPDATE_NEWS_WHERE));
		}
		zr_init(&g_heatpop.rig, T0, 1);
		zr_works(&g_heatpop.rig, T0 + DS_MS_MOVE);
		heatpop_draw(a, T0 + (unsigned)shot_heatpop);
	}
	/* The replug message (--replug) where the overlay goes: over everything
	 * else, under the bar */
	if (g_replug.on) replug_draw_at(a->r);
	/* The volume or brightness bar over whatever else was asked for, held
	 * until it has slid all the way in. */
	if (shot_osd_kind) {
		plat_osd_show(shot_osd_kind, shot_osd_val,
		              shot_osd_kind == OSDBAR_BRIGHT ? PLAT_BRIGHT_MAX
		              : shot_osd_kind == OSDBAR_POWER ? OSDBAR_POWER_STEPS : PLAT_VOL_MAX);
		SDL_Delay(OSDBAR_IN_MS + 10);
		plat_draw_osd(a->r);
	}
}

/* The row --shot-at's plate leaves for --menu's: the nearest row above it
 * the cursor can rest on, or below it when there is none above. */
static int shot_menu_from(app *a, int sel)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading), i;

	for (i = (sel < n ? sel : n) - 1; i >= 0; i--)
		if (rows[i].label && rows[i].live) return i;
	for (i = sel + 1; i < n; i++)
		if (rows[i].label && rows[i].live) return i;
	return sel;
}

/* The row of the top menu called `label` in English, or -1: the same row in
 * every language, found in English as --menu's is */
static int shot_menu_row(app *a, const char *label)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	lang was = i18n_lang();
	int n, i;

	i18n_set(LANG_EN);
	n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, label)) break;
	/* In MENU, on whichever tab has it, and that tab is then on */
	if (i == n && a->screen != SCREEN_GAMES) {
		int t, at = g_tabs.at;

		for (t = 0; t < PT_TABS && i == n; t++) {
			g_tabs.at = t;
			n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading);
			for (i = 0; i < n; i++)
				if (rows[i].label && !strcmp(rows[i].label, label)) break;
		}
		if (i == n) g_tabs.at = at;
	}
	i18n_set(was);
	return i < n ? i : -1;
}

/* --gamemenu's motion: at rest as it was, then the press at `t0` - L1 or
 * R1 from --gm-tab-from's tab, or the Controls tab's keys - for
 * shot_motion's frames to go on from */
static void shot_gm_motion(app *a, unsigned t0)
{
	int to = shot_gamemenu_tab;
	const char *k;

	if (shot_gm_from >= 0) {
		shot_gamemenu_tab = shot_gm_from;
		shot_compose(a);                         /* at rest on the tab before */
		shot_gamemenu_tab = to;
		shot_gm_t0 = t0 ? t0 : 1;
		if (to % GT_TABS == GT_CONTROLS) ctlscr_enter();
		g_menu_epoch++;
		return;
	}
	shot_compose(a);                             /* at rest on its row */
	for (k = shot_gm_press; k && *k; ) {
		size_t len = strcspn(k, ",");
		ctlscr_game g;
		menu_row rows[CT_MAX_ROWS];
		unsigned vcol[CT_MAX_ROWS];
		int ids[CT_MAX_ROWS], n;

		gm_ctl_game(a, NULL, &g);
		n = ctlscr_rows(a, &g, rows, vcol, NULL, ids);
		if (shot_gamemenu_sel >= n) shot_gamemenu_sel = n - 1;
		if (len == 2 && !strncmp(k, "up", 2))
			shot_gamemenu_sel = menu_step_round(rows, n, shot_gamemenu_sel, -1);
		else if (len == 4 && !strncmp(k, "down", 4))
			shot_gamemenu_sel = menu_step_round(rows, n, shot_gamemenu_sel, +1);
		else if (len == 4 && !strncmp(k, "left", 4))
			ctlscr_key(a, &g, IN_LEFT, ids[shot_gamemenu_sel]);
		else if (len == 5 && !strncmp(k, "right", 5))
			ctlscr_key(a, &g, IN_RIGHT, ids[shot_gamemenu_sel]);
		else
			fprintf(stderr, "--gm-press: %.*s is no key here\n", (int)len, k);
		/* ...and the cursor where the key put it, as the runner takes it */
		if (g_menu_sel_to >= 0) {
			shot_gamemenu_sel = g_menu_sel_to;
			g_menu_sel_to = -1;
		}
		k += len + (k[len] == ',');
	}
	/* Where the cursor ended, off a quiet row as the runner steps off one,
	 * for the shot checks to read */
	if (shot_gm_press) {
		ctlscr_game g;
		menu_row rows[CT_MAX_ROWS];
		unsigned vcol[CT_MAX_ROWS];
		int ids[CT_MAX_ROWS], n, sel;

		gm_ctl_game(a, NULL, &g);
		n = ctlscr_rows(a, &g, rows, vcol, NULL, ids);
		sel = shot_gamemenu_sel < n ? shot_gamemenu_sel : n - 1;
		if (!rows[sel].live) {
			sel = menu_step_sel(rows, n, sel, +1);
			if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, -1);
		}
		fprintf(stderr, "gm-press: the cursor on %s, %s\n", rows[sel].label,
		        rows[sel].value && !ROW_IS_NOTE(rows[sel]) ? rows[sel].value : "");
	}
}

/* --shot-at: the press, and the frames after it up to the shot's time.
 *
 * The clock is held at the press and moved on a frame at a time, 60 a second
 * as on the device, each frame drawn and thrown away, and then to the shot's
 * time exactly, which take_shot draws. The tweens would land in the same
 * place from the press alone; the frames are for what follows from one frame
 * to the next - the tint, which steps by the time since the last frame, and
 * the static state the menu keeps between draws. */
void shot_motion(app *a)
{
	unsigned t0 = plat_now_ms(), t;

	g_clock_held = true;
	g_clock_at = t0;
	shot_t0 = t0;
	cf_clock(anim_ticks);
	/* The tint's last step is now, or the first frame after the press would
	 * step it by however long the shot took to get here, and the shot would
	 * be a picture of the setup's speed. */
	last_tint_ms = t0;
	if (shot_kb && shot_press != SHOT_STEP) {
		/* The same bridge as the real loop: --open from the shelf,
		 * --swap from its menu, --close back to that menu. */
		shot_keyboard = (keyboard_transition){ .a = a };
		if (shot_press != SHOT_OPEN) {
			shot_lv = panel_open();
			mm_press(shot_lv);
			draw_shelf(a);
			zyperos_menu_draw(a, shot_menu_sel);
		}
		keyboard_panel(&shot_keyboard, KB_PANEL_OPEN, false);
		if (shot_press == SHOT_CLOSE) {
			keyboard_panel(&shot_keyboard, KB_PANEL_FRAME, true);
			keyboard_panel(&shot_keyboard, KB_PANEL_LEAVE, false);
		}
		shot_keyboard_panel = true;
	} else if (shot_storage && !strcmp(shot_storage, "delete-open")) {
		/* As on the handheld: MENU's panel at rest and drawn, so it is the
		 * panel on screen; A on Storage, whose page is a base
		 * (store_base_open); a second on the page, for MENU's panel to have
		 * left over it; then A on a game, and Delete Game's question opens.
		 * Its first frame is the page with nothing on it (2026-11-08: it was
		 * the panel before it, leaving, and then the question coming) */
		int row = shot_menu_row(a, "Storage");

		shot_lv = panel_open();
		mm_press(shot_lv);
		draw_shelf(a);
		zyperos_menu_draw(a, row >= 0 ? row : 0);
		store_base_open();
		t0 += 1000;
		g_clock_at = t0;
		shot_t0 = t0;
		shot_lv = panel_open();
		shot_pressed = true;
	} else if (shot_menu && shot_press != SHOT_STEP) {
		/* Opened on the shelf, as MENU opens it, by panel_open as menu_run
		 * does: --open's press. The others' menu is at rest when theirs
		 * comes - B, whose panel the shelf then draws leaving, or A on its
		 * Controls row, whose page opens over it. */
		shot_lv = panel_open();
		if (shot_press != SHOT_OPEN) {
			int c = shot_press == SHOT_SWAP ? shot_menu_row(a, "Controls") : -1;

			if (c >= 0) shot_menu_sel = c;
			mm_press(shot_lv);
			shot_compose(a);
			if (shot_press == SHOT_CLOSE) panel_close(a, shot_lv, false);
			else shot_lv = panel_open();
		}
		shot_pressed = true;
	} else if (shot_menu) {
		int to = shot_menu_sel;

		shot_menu_sel = shot_menu_from(a, to);
		shot_compose(a);                    /* at rest on the row above */
		shot_menu_sel = to;                 /* the press, at the next draw */
	} else if (shot_gamemenu && (shot_gm_from >= 0 || shot_gm_press)) {
		shot_gm_motion(a, t0);
	} else if (a->screen == SCREEN_GAMES) {
		sysview *v = &a->view[a->sys_cursor];

		shot_compose(a);
		if (v->list.count > 1) {
			v->cursor = (v->cursor + 1) % v->list.count;
			cf_set_cursor_dir(&v->cf, v->cursor, v->list.count, +1);
		}
	} else {
		shot_compose(a);
		if (a->sys.count > 1) {
			a->sys_cursor = (a->sys_cursor + 1) % a->sys.count;
			home_select(&a->home, a->sys_cursor, a->sys.count, +1, anim_now());
		}
	}
	for (t = 0; t < (unsigned)shot_at; t += 1000 / 60) {
		g_clock_at = t0 + t;
		tick_tint(a);
		shot_compose(a);
	}
	g_clock_at = t0 + (unsigned)shot_at;
	tick_tint(a);
}

/* --launch: the zoom's frame at --shot-at's time, as anim_launch draws it.
 * It asks for the card itself, after draining whatever has landed, as
 * anim_launch does, rather than counting on the prime_window the shot runs
 * for the games screen alone: a shot has no worker, so the ask reads the
 * card on the frame, from either screen. */
static void shot_launch_frame(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	SDL_Rect from;
	int tw = 0, th = 0;
	SDL_Texture *card;
	float k = shot_at > 0 ? (float)shot_at / DS_MS_LAUNCH : 0.0f;

	texload_drain(a);
	card = v->list.count ? game_get_tex(a, v->cursor, &tw, &th, NULL) : NULL;
	cf_focus_rect(&CF_LAYOUT_GAMES, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, &from);
	launch_frame(a, card, &from, k > 1.0f ? 1.0f : k);
}

/* --flip's turn: the whole shot drawn once in the theme it started in, then
 * Auto set a minute before the edge that ends that theme - 7:00 for Salt &
 * Pepper, 19:00 for Beard, today, where the same theme is still on - and its
 * clock read at the edge itself. theme_tick says the theme changed and
 * theme_changed is told, which is tick_theme's body with the clock's time
 * handed in: the path 7:00 and 19:00 take on a handheld, and not one written
 * for the shot.
 *
 * Then the frame right after the turn, as the device draws it: there every
 * card is asked of a worker and nothing is read on the frame, so a card the
 * turn took away is a hole in the row until a worker hands its replacement
 * over. A shot has no worker and reads every card on the frame, so this one
 * frame is drawn with the reads held (g_loads_held), and a slot in view with
 * no card in it fails the shot (BLANK): on the systems row, and on a shelf of
 * games, where the row is what was drawn - not under Now Playing, which
 * covers it, and not on Muse's shelf, whose cards are made on the frame. The
 * shot itself is the frame after, every card read in the theme that is on. */
static void shot_flip_theme(app *a)
{
	time_t now = time(NULL), edge;
	struct tm tm;
	sysview *v = &a->view[a->sys_cursor];
	int n, i, k;

	shot_compose(a);
	localtime_r(&now, &tm);
	tm.tm_hour = theme_index() == DS_DARK ? 7 : 19;
	tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;
	edge = mktime(&tm);
	theme_init_at(APPEAR_AUTO, edge - 60);
	if (theme_tick_at(edge)) theme_changed(a);
	else fprintf(stderr, "--flip: the theme did not turn at %02d:00\n", tm.tm_hour);

	g_loads_held = true;
	shot_compose(a);
	g_loads_held = false;
	/* The games in view: the cursor's and CF_HALF_WINDOW either side,
	 * wrapping as prime_around does. Every one was read before the turn, by
	 * prime_window at the start and the frame drawn then, so an empty one is
	 * one the turn emptied. */
	if (a->screen != SCREEN_GAMES || shot_np >= 0 || v->list.count <= 0 ||
	    is_muse(&a->sys.systems[a->sys_cursor]))
		return;
	for (k = -CF_HALF_WINDOW; k <= CF_HALF_WINDOW; k++) {
		n = v->list.count;
		i = n >= 2 ? ((v->cursor + k) % n + n) % n : 0;
		if (!v->tex[i]) {
			fprintf(stderr, "  BLANK: %s had no card on the frame after the "
			        "turn, so the shelf had a hole until its card came back\n",
			        v->list.items[i].title);
			shot_failed = 1;
		}
	}
}

/* Every card in a slot has a foot: the fraction of its height its pixels
 * reach (content_bottom), which is where cf_draw ends the lift under it and
 * starts its reflection. A card in a slot with a foot of 0 is drawn with a
 * shadow, a glow and a reflection 0 units high at its top - a thin line above
 * a card on Beard and nothing at all on Salt & Pepper - which is what every
 * system with no card file was drawn with until its generated card was given
 * one. The name of the first card without one, or NULL. */
static const char *shot_cards_have_feet(const app *a)
{
	for (int i = 0; i < a->sys.count; i++) {
		const sysview *v = &a->view[i];

		for (int k = 0; v->tex && v->cb && k < v->list.count; k++)
			if (v->tex[k] && !(v->cb[k] > 0.0f)) return v->list.items[k].title;
	}
	return NULL;
}

/* --portload: the game by the title the shelf shows or its file's name, on
 * a port's or a "run:" system's shelf before any other, drawn as
 * launch_port draws it and written to the shot. Nothing is started and
 * nothing is written for splash. The shot fails (exit 3) when the screen
 * says it fell short - a line or the band under its contrast, or the words
 * or the cover in Zyper's frame - and when there is no such game. */
void shot_portload_take(app *a)
{
	SDL_Surface *out;
	int pass, si, gi = -1;

	for (pass = 0; pass < 2 && gi < 0; pass++)
		for (si = 0; si < a->sys.count && gi < 0; si++) {
			const sysview *v = &a->view[si];
			int k;

			if (pass == 0 && !is_port(&a->sys.systems[si])) continue;
			for (k = 0; k < v->list.count; k++)
				if (!strcmp(v->list.items[k].title, shot_portload) ||
				    !strcmp(v->list.items[k].name, shot_portload)) {
					a->sys_cursor = si;
					gi = k;
					break;
				}
		}
	if (gi < 0) {
		fprintf(stderr, "--portload: no game called %s\n", shot_portload);
		shot_failed = 1;
		return;
	}
	a->screen = SCREEN_GAMES;
	a->view[a->sys_cursor].cursor = gi;
	/* The screen's own checks: everything at its contrast, and nothing in
	 * Zyper's frame */
	if (!port_loading_draw(a, &a->sys.systems[shelf_owner(a, a->sys_cursor, gi)], gi))
		shot_failed = 1;
	/* ZYPEROS_PORTLOAD_OUT: the letters splash would draw the port's
	 * output with, written there too (tools/portout-check.sh) */
	{
		const char *outp = getenv("ZYPEROS_PORTLOAD_OUT");

		if (outp && *outp && !port_out_write(outp)) {
			fprintf(stderr, "--portload: could not write %s\n", outp);
			shot_failed = 1;
		}
	}
	out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, 32,
	                                     SDL_PIXELFORMAT_RGBA32);
	if (!out) return;
	plat_screen_finish();   /* the screens' shader, which the present would add */
	SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, out->pixels, out->pitch);
	plat_present();
	IMG_SavePNG(out, shot_path);
	SDL_FreeSurface(out);
	fprintf(stderr, "wrote %s  [portload] %s\n", shot_path,
	        a->view[a->sys_cursor].list.items[gi].title);
}

/* --tour's frame, at a clock fixed for the shot, so the same arguments draw
 * the same picture: the loops, Zyper's breath and the confetti are all read
 * from it. */
static void shot_tour_draw(app *a)
{
	const uint32_t now = 1000000u;
	const tour_hooks hk = { tour_zyper_draw, tour_poweroff_draw, a, P_ROOT };
	tour t;
	int at[TOUR_CHOICES];

	if (shot_tour < 0) {
		const float ms = (float)shot_tour_ms;

		draw_shelf(a);
		tour_message(a->r, T(S_TOURRUN_SKIP_MESSAGE),
		             tour_ease(TC_OUT, (uint32_t)shot_tour_ms, 0, DS_MS_MOVE)
		             * (1.0f - fminf(1.0f, fmaxf(0.0f, (ms - DS_MS_HOLD_SHORT) / DS_MS_FADE_OUT))));
	} else {
		const dev_chords c = shot_chords();

		tour_pose(&t, (tour_step)shot_tour, now, shot_tour_ms, shot_tour_done,
		          &c, dev_tutorial_model(plat_profile(), &c));
		tour_lang_from(&t, (int)i18n_lang());
		tour_set_music(&t, shot_tour_music);
		tour_settings(a, at);
		tour_choices_from(&t, at);
		if (shot_tour_pick >= 0)
			tour_pose_pick(&t, shot_tour_pick, now, shot_tour_pick_ms);
		/* The language step is drawn in the language under its cursor */
		if (t.step == TOUR_LANG) {
			const lang f = (lang)tour_lang_focus(&t, now);

			if (f != i18n_lang()) language_apply(a, f, false);
		}
		if (shot_tour_done >= 0 && tour_choice_of(t.step) >= 0)
			t.kept[tour_choice_of(t.step)] = t.pick[tour_choice_of(t.step)];
		if (shot_tour_wrong >= 0)
			tour_pose_wrong(&t, (tour_key)shot_tour_wrong, now, shot_tour_wrong_ms);
		tour_draw(a->r, &t, now, &hk);
	}
	if (shot_osd_kind) {
		plat_osd_show(shot_osd_kind, shot_osd_val,
		              shot_osd_kind == OSDBAR_BRIGHT ? PLAT_BRIGHT_MAX
		              : shot_osd_kind == OSDBAR_POWER ? OSDBAR_POWER_STEPS : PLAT_VOL_MAX);
		SDL_Delay(OSDBAR_IN_MS + 10);
		plat_draw_osd(a->r);
	}
	tour_draw_free();
}

/* --charging's frame, on a clock of its own, the estimate settled */
static void shot_charging_draw(app *a)
{
	const unsigned T0 = 100000;
	unsigned now = T0 + (unsigned)shot_charging;

	memset(&g_chg, 0, sizeof g_chg);
	g_chg.shown_at = T0;
	ch_screen_start(&g_chg.s, T0);
	/* kept: held at an 85% limit; limit: charging toward it; keptfull: held,
	 * but charged full before the limit was set */
	ch_fake_parse(shot_charging_kind == 5 ? "85 -5 4100 wall 1 1"
	              : shot_charging_kind == 7 ? "100 -5 4290 wall 1 1"
	              : shot_charging_kind == 8 ? "92 1480 4150 wall 1" : "62 1480 4120 wall 1", &g_chg.r);
	if (shot_charging_kind >= 5 && shot_charging_kind != 8) g_chg.r.limit = 85;
	/* The limit saved in MENU, for Y's hint: 85 but for soon's, which has
	 * none; tofull, Y pressed: the limit off this time, to 100% */
	g_charge_limit = shot_charging_kind == 8 ? 0 : 85;
	if (shot_charging_kind == 9) { g_full_shot = true; g_chg.r.limit = 0; }
	/* pending: the PHY has not named the charger yet */
	if (shot_charging_kind == 4) g_chg.r.kind = CH_PENDING;
	g_chg.valid = true;
	g_chg.eta = (ch_eta){ true, T0 - 60000, T0, 1480.0 };
	ch_words(&g_chg.r, ch_eta_minutes(&g_chg.eta, T0, &g_chg.r), false, g_chg.speed,
	         sizeof g_chg.speed, g_chg.when, sizeof g_chg.when);
	if (shot_charging_kind == 1) {
		ch_screen_tick(&g_chg.s, now - DS_MS_HOLD_POWER / 2, true, true, true);
		ch_screen_tick(&g_chg.s, now, false, true, true);
	} else if (shot_charging_kind == 2)
		ch_screen_tick(&g_chg.s, now, false, false, false);
	if (shot_charging_kind == 3) {
		/* the cable out, its hold, then the send-off */
		unsigned t = (unsigned)shot_charging > POWEROFF_DIM ? POWEROFF_DIM : (unsigned)shot_charging;

		ch_screen_tick(&g_chg.s, T0 + 500, false, false, false);
		ch_screen_tick(&g_chg.s, T0 + 500 + DS_MS_HOLD_SHORT, false, false, false);
		chg_draw(a, T0 + 1000, ds_out((float)t / POWEROFF_DIM), 1.0f - (float)t / POWEROFF_DIM);
		return;
	}
	chg_draw(a, now, 0.0f, 1.0f);
}

/* --lowbatt's frame, on a clock of its own so it is the same each time */
static void shot_lowbatt_draw(app *a)
{
	const unsigned T0 = 100000, PLUG = 2400;
	unsigned now = T0 + (unsigned)shot_lowbatt;
	float bar_k = 1.0f;

	g_lb.pct = 2;
	lowbatt_start(&g_lb.l, T0);
	zr_init(&g_lb.rig, T0, 1);
	zr_fails(&g_lb.rig, T0);
	if (shot_lowbatt_kind == 2) {
		/* the send-off, from the end of the hold */
		now = T0 + DS_MS_HOLD_LOW_BATTERY;
		zr_fails(&g_lb.rig, now - DS_MS_HOLD_ZYPER_FAIL / 2);
		zr_tick(&g_lb.rig, now);
		g_lb.pose = zr_pose_at(&g_lb.rig, now);
		lowbatt_off_frame(a, &g_lb.pose, (unsigned)shot_lowbatt, now);
		return;
	}
	if (shot_lowbatt_kind == 1 && shot_lowbatt >= (int)PLUG) {
		lowbatt_tick(&g_lb.l, T0 + PLUG, false, true, NULL);
		zr_fails(&g_lb.rig, T0 + PLUG - DS_MS_HOLD_ZYPER_FAIL / 2);
		zr_press(&g_lb.rig, T0 + PLUG);
		zr_works(&g_lb.rig, T0 + PLUG);
		bar_k = now - (T0 + PLUG) >= DS_MS_FADE_OUT ? 0.0f
		      : 1.0f - (float)(now - (T0 + PLUG)) / DS_MS_FADE_OUT;
	} else if (shot_lowbatt > (int)DS_MS_HOLD_ZYPER_FAIL / 2) {
		zr_fails(&g_lb.rig, now - DS_MS_HOLD_ZYPER_FAIL / 2);
	}
	zr_tick(&g_lb.rig, now);
	g_lb.pose = zr_pose_at(&g_lb.rig, now);
	lowbatt_draw(a, now, &g_lb.pose, 0.0f, 1.0f, bar_k);
}

void take_shot(app *a)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W,
	                                                  ZYPEROS_SCREEN_H, 32,
	                                                  SDL_PIXELFORMAT_RGBA32);

	if (shot_launch) {
		shot_launch_frame(a);
	} else if (shot_lowbatt >= 0) {
		shot_lowbatt_draw(a);
	} else if (shot_charging >= 0) {
		shot_charging_draw(a);
	} else if (shot_tour > -2) {
		shot_tour_draw(a);
	} else if (shot_focus_from >= 0) {
		int at = g_home_phase;

		g_home_phase = shot_focus_from;
		shot_compose(a);
		g_home_phase = at;
		/* focus_frame's own rule: a whole frame when anything the focus
		 * frame does not draw again is on screen */
		if (focus_ok(a) && focus_draw(a)) {
			/* What plat_present adds to a frame that skipped
			 * plat_draw_osd (plat_overlay_present) */
			if (g_replug.on) replug_draw_at(a->r);
		} else {
			fprintf(stderr, "focus: not for this sprite, a whole frame\n");
			shot_compose(a);
		}
	} else {
		if (shot_flip) shot_flip_theme(a);
		shot_compose(a);
	}
	if (out) {
		/* Read BEFORE presenting: the backbuffer is invalid afterwards.
		 * The screens' shader first, which the present would add. */
		plat_screen_finish();
		SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32,
		                     out->pixels, out->pitch);
		plat_present();
		IMG_SavePNG(out, shot_path);
		SDL_FreeSurface(out);
		/* Say what was drawn, not just that something was: a tool whose whole
		 * job is rendering one state is far more useful when the state it
		 * chose is in the output beside the filename. */
		{
			sysview *v = &a->view[a->sys_cursor];
			const char *what = a->screen == SCREEN_GAMES && v->list.count > 0
			                 ? v->list.items[v->cursor].title
			                 : a->sys.systems[a->sys_cursor].name;
			/* Checked rather than assumed. This line once named a focused
			 * game on a shot with no cards in it. A card drawn on the frame
			 * always ends up with a texture - box art, a preview, or the
			 * generated card - so one without means the picture is not what
			 * the name beside it claims, and that is worth saying loudly. */
			const char *note =
				a->screen == SCREEN_GAMES && v->list.count > 0 &&
				v->tex && !v->tex[v->cursor]
				? "  (NO ART: the focused card had no texture on this frame)" : "";
			const char *footless = shot_cards_have_feet(a);

			if (shot_tour > -2)
				fprintf(stderr, "wrote %s  [tour] %s\n", shot_path,
				        shot_tour < 0 ? "skip" : TOUR_STEP_NAMES[shot_tour]);
			else
				fprintf(stderr, "wrote %s  [%s] %s%s\n", shot_path,
				        a->screen == SCREEN_GAMES ? "games" : "systems", what, note);
			if (footless) {
				fprintf(stderr, "  NO FOOT: %s's card has a content bottom of 0, so "
				        "nothing is lifted or reflected under it\n", footless);
				shot_failed = 1;
			}
			{
				/* The screensaver draws no corner: it is the whole screen */
				const char *crowded = g_shot_saver >= 0 ? NULL : shot_corner_clear(a);

				if (crowded) {
					fprintf(stderr, "  CORNER: %s\n", crowded);
					shot_failed = 1;
				}
			}
		}
	}
}
