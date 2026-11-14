/* The two menus behind the MENU button, as rows rather than as pixels.
 *
 * MENU has two menus behind it, chosen by where it was pressed. From the
 * systems row it is about the firmware. From inside a system it is about THAT
 * system, because a menu that repeated the firmware's settings while a shelf
 * of NES games sat behind it would be answering a question nobody asked.
 *
 * Split out of main.c so that what these menus CONTAIN can be checked on a
 * build machine. Nothing here may include SDL: tools/menu-check.c links this
 * file without it, and that is the whole point.
 *
 * The caller reads the device and fills a sys_ui; this file turns that into
 * rows. Wi-Fi state in particular is asked for by the caller because asking
 * costs a fork - see menu_wifi in main.c - and this file must not care. */
#ifndef SYS_MENU_H
#define SYS_MENU_H

#include <stdbool.h>
#include <stddef.h>

#include "menu.h"
#include "config.h"
#include "wifi.h"
#include "audioout.h"
#include "sidelights.h"
#include "i18n.h"

/* The ZyperOS menu, in the order it is read.
 *
 * Play Time leads because it is the only row here anyone opens twice. Wi-Fi,
 * Bluetooth and Audio Output are setup: you use them when something is wrong
 * or new, and then never again. A menu ordered by what a device needs on its
 * first day puts the thing you actually come back to five rows down. */
/* Random Game under Play Time: the other row about playing rather than about
 * the device - one game from every system, picked for you (src/randgame.h). */
/* Controls and About are the two rows you only read, so they sit together at
 * the end, after everything that changes something. */
/* Date & Time is setup too, so it ends that group: on the Pixel 2, which has
 * no network to take the time from, it is the first thing to set. */
/* Search Games above them all: a game from any system by a few letters of its
 * name, on the keyboard (src/search.h) - the way to a game that is not on the
 * shelf in front of you, and a row reached for as often as Play Time. */
/* Audio Channels directly under Audio Output: the two together are how the
 * sound reaches you, where it goes and which ears hear what. */
/* PM_HOURS is the Clock row, 12-hour or 24-hour, directly under Date & Time:
 * how the time is written, for the shelf's corner and Date & Time's own row.
 * My note in the status lab, 2026-11-04. */
/* UI Sounds under them: the launcher's own sounds, which voice or none, are
 * the third thing about what you hear. my place for it, 2026-11-04, in
 * the sound lab, over beside Appearance with the look. */
/* Menu Music directly under UI Sounds: the other sound the launcher makes on
 * its own, ROGUE OS's frontend music from Roms/BGM (src/bgm.h). */
/* System Update just above About (the update lab, 2026-11-05): its value is
 * the installed version, or the news when there is some. Only on a device
 * whose card has the update program (docs/updates.md). */
/* PM_CHARGE is Charge Limit, directly under Auto Off: where charging stops,
 * Off, 80%, 85% or 90%, stepped round (DESIGN.md 11.26; my picks in
 * the charge limit lab, 2026-11-06). Only where the charger can stop short
 * (no_charge_limit). */
/* PM_SAVER is Screensaver, on Display after Battery Icon (1.0.5's tabs): the
 * video snaps of random games over the whole screen
 * after this long with nothing pressed on the shelf (src/saver.h, DESIGN.md
 * 11.37). Its value is the delay, in Auto Off's words, stepped round with
 * left and right the way Auto Off is, or off. On every device. */
/* PM_LIGHTS is Side Lights, under Auto Off and Charge Limit: what the battery
 * lights show (src/sidelights.h), the other row about the battery, a page of
 * switches since the side lights lab (2026-11-06). Only where the device has
 * such lights (no_lights). */
/* Show Tutorial between Controls and About: the same buttons as Controls,
 * taught a step at a time rather than listed (DESIGN.md 11.10), and like the
 * two either side of it, a row nobody needs every day. */
/* PM_SCRAPER is Scraper, where Box Art was: the run that fetches missing box
 * art, Video Snaps, and the three sources it asks - ScreenScraper, libretro
 * and SteamGridDB - each linked on a page of its own. One row in place of
 * Box Art, ScreenScraper and SteamGridDB (My picks in the Scraper lab,
 * 2026-11-06; DESIGN.md 11.19). On every device: with no network it cannot
 * fetch, but Video Snaps still play and a key pasted over the USB cable
 * still wants a place to be seen. */
/* PM_STORAGE is Storage, just above System Update, as a phone keeps its
 * storage beside its software update: how the card is used, part by part
 * (src/storage.h, DESIGN.md 11.32). Its value is the room left. My pick in
 * the Storage lab, 2026-11-07. */
/* Developer Mode above System Update, on a development image only: the USB link and
 * its SSH always on with the cable in, rather than the question a computer
 * gets (src/usblink.h). My pick in the USB lab, 2026-11-05. */
/* PM_LANGUAGE is Language, under Date & Time and Clock: the regional
 * settings together, as a phone keeps Language and Region (My pick in
 * the languages lab, 2026-11-07; DESIGN.md 11.30). Its value is the
 * language's own name; A opens the five. */
/* MENU IS IN TABS (DESIGN.md 11.3, my picks 2026-11-12): Games, Display,
 * Sound, Connections, System and About, L1 and R1 going round them. The rows
 * are in the enum by tab, and in a tab in the enum's order; which tab a row
 * is in is sys_menu_tab_of's to say. A row added to MENU goes into this enum
 * where its tab's rows are, and into sys_menu_tab_of's switch, which the
 * compiler checks has every row (-Wswitch), and tools/menu-check.c that each
 * row is in a tab. */
/* PM_AUTOHOURS is Auto Hours, under Appearance: when Auto turns light and
 * when dark, by the half hour (src/theme.h), 7:00 and 19:00 as it ships.
 * PM_THEME is Theme, under it: the accent, a background tone for each theme,
 * the corners and Zyper (src/look.h, DESIGN.md 3.5). My picks 2026-11-12. */
typedef enum {
	/* Games */
	PM_SEARCH, PM_STATS, PM_RANDOM, PM_ACHIEVEMENTS, PM_SCRAPER,
	/* Display */
	PM_APPEARANCE, PM_AUTOHOURS, PM_THEME, PM_HOME, PM_ORDER, PM_BATTERY, PM_SAVER, PM_SHADER,
	/* Sound */
	PM_AUDIO, PM_CHANNELS, PM_SOUNDS, PM_MUSIC,
	/* Connections: USB Device first, while a plug waits for its answer */
	PM_USB_WAIT, PM_WIFI, PM_BT, PM_XFER, PM_USB, PM_DEVELOPER,
	/* System */
	PM_CLOCK, PM_HOURS, PM_LANGUAGE, PM_SLEEP, PM_CHARGE, PM_LIGHTS,
	PM_STORAGE, PM_UPDATE,
	/* About */
	PM_ABOUT, PM_CONTROLS, PM_TOUR,
	PM_ROWS
} pm_row;

/* MENU's tabs, in the order L1 and R1 go round them. MENU opens on Games
 * after a start, and on the tab and row it was closed on after that, until
 * the handheld powers off; on Connections' USB Device while a plug waits. */
typedef enum { PT_GAMES, PT_DISPLAY, PT_SOUND, PT_CONNECTIONS, PT_SYSTEM,
               PT_ABOUT, PT_TABS } pm_tab;


/* Random Game is under Games: one of the games that row counts, picked and
 * started (src/randgame.h). */
/* The system menu. Games and Core carry real values rather than invented ones,
 * because a placeholder that lies about the machine it is describing is worse
 * than no row. The rest name capabilities that already exist on the emulator
 * side - Diatom has per-system display modes and core-supplied button labels -
 * so these are hooks waiting to be wired, not wishes. */
/* View, under Sort By (1.0.5, DESIGN.md 11.39): the shelf's covers in a
 * row, or its games as a list, this shelf's own choice as Sort By is. */
typedef enum {
	SM_GAMES, SM_RANDOM, SM_CORE, SM_DSLANG, SM_SORT, SM_VIEW,
	SM_DISPLAY, SM_SHADER, /* SM_BUTTONS, */ SM_BOXART, SM_RESCAN, SM_ROWS
} sm_row;

/* What the Favorites shelf's menu is, which is two of those rows.
 *
 * Favorites is not a system: build_favorites_shelf memsets one and fills in a
 * name, a tag, a card and an accent, leaving no core, no folder and no
 * extensions. Four of the six rows have nothing to work with - Core is blank,
 * Box Art and Rescan have no folder to scrape or sweep, and Display Mode is
 * worse than blank, because launch() resolves a game's CORE through its owner
 * and its display mode through the shelf it was started from. Setting one here
 * would give a game a different aspect depending on which shelf launched it.
 *
 * Games and Sort By are both true of a shelf of favorites, so those are what
 * it gets. Eric's call, 2026-09-17. */
/* Random Game joined them 2026-11-05: a favorite picked for you is the shelf's
 * own question, and it is between the two as it is on a console's menu. View
 * (1.0.5) is the shelf's, under Sort By as on a console's. */
#define SM_FAV_ROWS 4

/* What MP4's shelf menu is (Muse's, before it played video): how many
 * albums, Show - music, audiobooks or videos, the kinds its one shelf holds -
 * Shuffle All on music, every song on the card in a random order (
 * 2026-11-05) - Sort By - by artist or by album, its own
 * two orders, where a console's shelf has four - Album Art to fetch covers for
 * the albums that have none or only a small one, and Rescan Folder for music
 * copied onto the card while the Brick was on. Muse has no core, no ROM folder
 * and no display mode. Album order was Eric's, 2026-09-19; Show, 2026-09-27.
 *
 * On books the count is Books, Sort By is by author or title, and Album Art is
 * not there: MusicBrainz knows records. Show is there only when the card has
 * both kinds, since with one there is nothing to choose between. So the rows
 * move, and which one is where is sys_menu_muse_rows's to say.
 *
 * Videos are like books there: no Album Art, and the orders their own
 * (Folder and Title).
 *
 * All - every kind on one shelf, where Show starts since 2026-11-09 - has
 * the videos' orders, counts All, and has Shuffle All and Album Art when the
 * card has songs, which are the two rows' only business.
 *
 * Lock Screen is the pocket lock, the way to find it without knowing X:
 * live while something plays, and saying so when nothing does. */
typedef enum { SMM_COUNT, SMM_SHOW, SMM_SORT, SMM_SHUFFLE, SMM_LOCK, SMM_ART,
               SMM_RESCAN } sm_muse_row;
#define SM_MUSE_ROWS 7

/* MP4's rows in order, for the kind its shelf shows - 0 music, 1 books,
 * 2 videos, muselib.h's ml_kind, and 3 all of them with music among them
 * (all of them with no music has the videos' rows) - and
 * whether the card has more than one kind. Returns how many. The build and
 * the key handler both ask this, so the two cannot disagree about what row 2
 * is. no_wifi: no radio and no other way out (a phone's tethering), so no
 * Album Art row. */
int sys_menu_muse_rows(int kind, bool both, bool no_wifi, sm_muse_row *out);

/* 40: the ZyperOS menu reached 24 with Developer Mode, Random Game made 25,
 * and the two USB rows (USB Device, USB Accessories) 29 in all; 1.0.5's
 * Auto Hours, Theme, Screensaver and Shader made 34, every tab's rows
 * together (a tab shows its own, but the build for every tab, PT_TABS, holds
 * them all); room for a few more. The menu runner holds MENU_RUN_ROWS
 * (main.c), which must stay at least this. */
#define MENU_MAX_ROWS 40

/* The array every caller declares must hold every row a build can produce, and
 * on 2026-09-16 it stopped: PM_ROWS went to 13 against a cap of 12 when the
 * ScreenScraper row landed, so sys_menu_build wrote one past the end of the
 * caller's stack array. The launcher aborted on its first frame and
 * tools/menu-check.c passed, because it overflows identically and nothing
 * there was watching that memory.
 *
 * A number that has to be kept in step with an enum, by hand, in another file.
 * Now it is the compiler's job.
 *
 * _Static_assert and not #if: PM_ROWS is an enum constant, and the
 * preprocessor reads an identifier it does not know as 0 - so `#if
 * MENU_MAX_ROWS < PM_ROWS` is `13 < 0`, false forever, a guard that compiles
 * and protects nothing. Written that way first, and only caught by putting the
 * cap back to 12 and watching the build succeed. */
_Static_assert(MENU_MAX_ROWS >= PM_ROWS, "MENU_MAX_ROWS < PM_ROWS");
_Static_assert(MENU_MAX_ROWS >= SM_ROWS, "MENU_MAX_ROWS < SM_ROWS");

/* The file server's two names, one per way the files travel: over the air,
 * Wi-Fi File Transfer; over the USB cable (the GKD Pixel 2), USB File Transfer.
 * Eric's, 2026-10-05. Only what people see takes the cable name; the code
 * and the server stay the hare's, one for both. In the language that is on,
 * so not for a static initializer. */
#define XFER_NAME_WIFI  T(S_SYSMENU_XFER_WIFI)
#define XFER_NAME_CABLE T(S_SYSMENU_XFER_CABLE)

/* Where the built rows' text lives. A row holds pointers, not copies, so the
 * strings a build formats have to outlive the build; the caller owns this and
 * keeps it alive as long as it keeps the rows. */
/* And each row's sound class (MENU_SND_*, menu.h), set by the build by the
 * row's identity, so the runner can answer a press before the screen's
 * handler. Beside the rows rather than in them: a field on menu_row would
 * have every { label, value, live } initializer warn under -Wextra. */
typedef struct {
	char a[24], b[CFG_STR], c[16], d[40], e[8], ch[40], sv[16];
	unsigned char sound[MENU_MAX_ROWS];
} menu_bufs;

/* Everything either menu needs to know about the device, gathered by the
 * caller. A struct rather than a dozen arguments so that adding a fact to a
 * row does not change every call site, and so a check can state a device's
 * whole situation in one initializer. */
typedef struct {
	bool games;              /* the system menu, rather than ZyperOS's own */
	bool fav;                /* and that shelf is Favorites; see SM_FAV_ROWS */
	bool muse;               /* or Muse's; see SM_MUSE_ROWS */
	bool muse_books;         /* and it shows books rather than music */
	bool muse_videos;        /* or videos */
	bool muse_all;           /* or every kind, mixed (ML_ALL) */
	bool muse_songs;         /* and the card has music, for All's rows */
	bool muse_both;          /* and the card has more than one kind */
	bool muse_playing;       /* and music is playing, so it can lock */

	/* What the device HAS, rather than what state it is in. A radio that is
	 * merely off keeps its rows, saying "needs Wi-Fi", because turning it on
	 * is the fix; a device with no radio at all loses them, because nothing
	 * is. False means present, so a caller or a check that says nothing about
	 * it gets the Brick. */
	bool        no_wifi;
	bool        no_bt;
	/* Files move over the USB cable rather than Wi-Fi (plat_cable_link), so
	 * the transfer row stays without a radio, under its cable name. */
	bool        cable_xfer;
	/* A way out to the internet (Wi-Fi, or a phone's USB tethering: the
	 * launcher's net_internet), so what fetches from it stays without a
	 * radio: Achievements, Box Art, Album Art. The Wi-Fi rows do not. */
	bool        online;
	/* A release image, which has no Developer Mode (src/usblink.h); false,
	 * as for the radios, is the row present: a development image. And
	 * whether Developer Mode is on there. */
	bool        no_developer;
	bool        developer;
	/* On no network, the adapter can make one of its own for the transfer
	 * (hotspot_available): the Wi-Fi row stays live, saying so. */
	bool        hotspot;
	/* File Transfer on, behind the shelf (main.c, xs_*): what it is doing,
	 * "on", "receiving", "closing", which the row says under the name of the
	 * way it runs, `xfer_cable` saying which; NULL when it is off */
	const char *xfer;
	bool        xfer_cable;
	/* No lights of its own for the battery (plat_battery_lights), so no Side
	 * Lights row. False means present like the two above, so a check that
	 * says nothing gets every row in the enum's order - on a Brick the
	 * launcher sets it. */
	bool        no_lights;
	/* No charge limit on this device's charger (plat_charge_limit_supported),
	 * so no Charge Limit row, as no_lights is for Side Lights */
	bool        no_charge_limit;
	/* The Pixel 2's USB port powers an accessory only when told (plastron's
	 * usbrole, src/usbpower.h): USB Accessories, Ask or Automatic, under
	 * File Transfer; no row where nothing decides that (the Brick). And
	 * USB Device at the top while a plug waits for an answer, opening the
	 * question again for it (What did you plug in?, DESIGN.md 11.36). */
	bool        no_usb_power;
	bool        usb_auto;
	bool        no_usb_wait;   /* no plug waits: no USB Device row (every
	                            * row shows with the struct zeroed, as the
	                            * checks build it) */

	wifi_state  wifi;        /* already cached by the caller; see menu_wifi */
	const char *ssid;        /* the network's name when connected, else NULL */

	/* The ZyperOS menu: the tab it shows (pm_tab), or PT_TABS for every row
	 * in the enum's order (a measure, a check) */
	int         tab;
	/* Auto Hours' value, "7:00 AM to 7:00 PM" as MENU > Clock writes the
	 * time; NULL reads nothing */
	const char *auto_hours;
	/* Theme's value: the accent's name (src/look.h); NULL reads Lime */
	const char *theme;
	bool        ra_in;
	const char *ra_name;     /* only read when ra_in */
	/* The Scraper row's value: "142 missing", "complete", or NULL until a
	 * count has been taken (art_count_missing, when its screen opens). */
	const char *scraper;
	/* System Update (src/sysupdate.h): the row's value, or NULL on a device
	 * or card without the update program, which has no row */
	const char *update;
	/* Storage's value, the room left on the card ("7.4 GB free") */
	const char *storage;
	/* The Appearance setting's name, "Light", "Dark" or "Auto" - what
	 * appearance_name says, and not the theme it comes to now, which for Auto
	 * is the clock's to say. NULL reads Light, the default. A label and not
	 * an enum, as cards and dmode are, so this file need not link theme.c. */
	const char *appearance;
	/* Shader, for every system (src/app.h SHADERS): Off, a shader's name, or
	 * "per system" when systems differ. NULL reads Off. A label, for the
	 * reason appearance is one. */
	const char *shader_all;
	const char *home;       /* Spotlight (default) or Gallery */
	/* System Order, under Home Screen: "A to Z" (default, NULL reads it) or
	 * "Release Date", systems.cfg's order (2026-11-06) */
	const char *order;
	const char *battery;    /* the corner's battery: Bars (default), Percentage or Time Left */
	int         auto_off;    /* seconds, 0 for off */
	int         saver;       /* the screensaver's delay, seconds, 0 for off */
	int         charge_limit;   /* percent, 0 for Off */
	/* Side Lights: side_lights_value's, "All on", "3 on" or "Off". NULL
	 * reads All on, the default. A label, for the reason appearance is one. */
	const char *lights;
	const char *clock;       /* the local date and time, "Oct 3, 6:12 AM" */
	bool        clock_24h;   /* MENU > Clock is 24-hour; false is 12-hour */
	/* Where sound goes: the policy the player set, and where it actually ends
	 * up under that policy. Both, because the row has to name a place - "Auto"
	 * on its own is a rule, not somewhere you can hear. */
	aout_policy audio_policy;
	aout_dest   audio_dest;
	/* Which ears hear what, as chosen. The row names the choice, not what a
	 * speaker makes of it: "mono left" stays mono left with the cable out.
	 * Auto is a rule, as Audio Output's is, so its row names the mix it makes
	 * now, `audio_mix`: "auto (mono)" on the Pixel 2's speaker. */
	aout_channels audio_channels;
	aout_channels audio_mix;
	/* UI Sounds: the voice's name, "Wood", "Glass" or "Bubble", or "Off" -
	 * uisound_setting_name's. NULL reads the default voice, as Appearance's
	 * NULL reads Light. A label, for the reason appearance is one. */
	const char *ui_sounds;
	/* Menu Music: "On", "Off" or "no tracks" - bgm_menu_value's. NULL reads
	 * "Off". A label, for the reason ui_sounds is one. */
	const char *menu_music;

	/* Bluetooth: the name of the connected headset, or NULL. The row used to
	 * read "not yet" and be dead, which was true of the pairing screen and
	 * false of the feature - game audio has gone to a headset since
	 * 2026-09-03. */
	const char *bt_name;

	/* The system menu */
	const char *sys_name;
	const char *sys_core;
	/* The shelf lists more than one core (the Dreamcast: flycast2021 and
	 * the standalone Flycast 2.7), so its Core row is a choice rather than a
	 * fact. */
	bool        core_choice;
	/* Game Language, under Core, on the Nintendo DS shelf (src/dslang.h):
	 * the row's value, "Auto (English)" or a language's name. NULL: the
	 * shelf has no such row, and every other shelf's menu is as it was. */
	const char *dslang;
	int         game_count;
	const char *dmode;
	/* This system's shader's label, after Display Mode; NULL reads Off */
	const char *shader;
	/* The label of the shelf's sort order. A label rather than an index for
	 * the same reason dmode is one: this file must not know the table, or a
	 * check that links it would have to link the table too. */
	const char *sort;
	/* The shelf shows a list rather than its covers (View, SM_VIEW) */
	bool        listed;
} sys_ui;

/* The ZyperOS menu's rows and the system menu's, in order, for what the device
 * has - see no_wifi in sys_ui. Return how many. The build and the key handler
 * both ask, so the two cannot disagree about what row 3 is, the same reason
 * sys_menu_muse_rows exists. */
int sys_menu_zyperos_rows(const sys_ui *u, pm_row *out);
int sys_menu_system_rows(const sys_ui *u, sm_row *out);

/* The tab `r` is in; PT_TABS for a value that is no row */
pm_tab sys_menu_tab_of(pm_row r);
/* A tab's name, in the language that is on: the tab row's words */
const char *sys_menu_tab_name(pm_tab t);
/* The rows of tab `t` the device has, in order (sys_menu_zyperos_rows,
 * kept to the tab). Returns how many. The build and the key handler both
 * ask, so they cannot disagree about what row 3 of a tab is. */
int sys_menu_tab_rows(const sys_ui *u, pm_tab t, pm_row *out);

/* Seconds to the label a row shows. Pure, and here rather than in main.c so
 * the check can hold it to "never", "30s" and "2m" without a device. */
void sys_menu_auto_off_label(int seconds, char *out, size_t n);
/* The screensaver's delay in the same words: "off" for 0, then "30s", "1m" */
void sys_menu_saver_label(int seconds, char *out, size_t n);

/* System Order's value, "A to Z" or "Release Date", and Battery Icon's,
 * "Bars" (0), "Percentage" (1) or "Time Left" (2), in the language that is
 * on: what sys_ui's order and battery take */
const char *sys_menu_order_name(bool release);
const char *sys_menu_battery_name(int mode);

/* ---- MENU > Scraper (DESIGN.md 11.19) -----------------------------------
 *
 * One list: the run and Video Snaps, a rule, then the sources in the order a
 * run asks them, each saying who it is linked as. While a run goes, the
 * first row is its progress and each source counts the covers it found;
 * after one, the counts stay until the screen closes. Six rows and a rule,
 * which is what the panel holds at 640x480: nothing else goes on it. Each builder fills `ids`
 * with every row's part (SC_NONE and the like for rules and notes), so the
 * key handler and the check agree on what row 3 is. */
typedef enum { SC_NONE, SC_FETCH, SC_SNAPS, SC_SS, SC_LIBRETRO, SC_SGDB } sc_row;
#define SC_ROWS 10
typedef struct {
	bool        net;          /* a network to fetch over */
	int         missing;      /* covers missing, or -1 before a count */
	const char *snaps;        /* the Video Snaps value: "on · 1.4 GB", "off" */
	bool        ss_have;      /* this build carries ScreenScraper's key */
	bool        ss_in;
	const char *ss_name;      /* only read when ss_in */
	const char *sgdb;         /* "••••1a2b", "key refused", or NULL: none */
	bool        running, ran; /* a run going; a run finished on this screen */
	int         done, total;  /* games looked at, of those missing */
	int         ss_found, lr_found, sg_found, not_found;
} scraper_ui;
typedef struct { char fetch[48], ss[64], lr[32], sg[32]; } scraper_bufs;
int sys_menu_scraper_rows(const scraper_ui *u, scraper_bufs *b, menu_row *out,
                          sc_row *ids);

/* ScreenScraper's page: the account, whether it works, Unlink, and what it
 * brings. `user` NULL is not linked; `status` the Status value. */
typedef enum { SSP_NONE, SSP_ACCOUNT, SSP_STATUS, SSP_UNLINK } ssp_row;
#define SSP_ROWS 7
int sys_menu_ss_rows(bool have, const char *user, const char *status,
                     menu_row *out, ssp_row *ids);

/* SteamGridDB's page: the key, masked - its last four characters - and what
 * is known about it, Unlink, and where else a key can come from. `masked` is
 * NULL with no key; `state` is the status value ("works", "not checked",
 * "refused"). */
typedef enum { SG_NONE, SG_KEY, SG_STATUS, SG_REMOVE } sg_row;
#define SG_ROWS 7
int sys_menu_sgdb_rows(const char *masked, const char *state, menu_row *out,
                       sg_row *ids);

/* The Video Snaps page: fetching them, playing them, their sound, then every
 * console with the room its snaps take and its own on or off, and Delete
 * All. Fetching needs ScreenScraper, the only source with snaps. */
typedef enum { SN_NONE, SN_FETCH, SN_PLAY, SN_SOUND, SN_CONSOLE, SN_DELETE } sn_row;
enum { SNAP_MUTED, SNAP_QUIET, SNAP_FULL };
/* The words the settings database keeps for them; the row says them in the
 * language that is on */
extern const char *const SNAP_SOUND_NAME[3];
typedef struct { const char *name; bool on; unsigned long long bytes; } snaps_console;
typedef struct {
	bool fetch, play, ss_in;
	bool ss_have;                     /* this build has ScreenScraper's key */
	int  sound;                       /* SNAP_MUTED ... */
	const snaps_console *c;
	int  nc;
	unsigned long long total;
} snaps_ui;
#define SN_ROWS (CFG_MAX_SYSTEMS + 7)
typedef struct { char size[CFG_MAX_SYSTEMS][32]; char total[32]; } snaps_bufs;
/* `console` gets, for a SN_CONSOLE row, its index in u->c. */
int sys_menu_snaps_rows(const snaps_ui *u, snaps_bufs *b, menu_row *out,
                        sn_row *ids, int *console);
/* Bytes as the rows write them: "212 MB", "1.4 GB". */
void sys_menu_bytes(unsigned long long n, char *out, size_t outn);

/* MENU > Side Lights' page (src/sidelights.h): a row for each part, On or
 * Off, stepped by Left, Right or A. `ids` gets each row's part. */
int sys_menu_lights_rows(side_lights on, menu_row *out, side_light_part *ids);

/* Build whichever menu u->games calls for. Returns the row count, so the input
 * loop never needs to know which of the two it is driving. */
int sys_menu_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                   const char **heading);

#endif
