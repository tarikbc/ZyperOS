/* What the two MENU-button menus contain. See src/sys_menu.h for why this is
 * a file of its own.
 *
 * No SDL, no device reads, no forks. Everything this needs arrives in sys_ui. */
#include <stdio.h>
#include <string.h>

#include "design.h"
#include "sys_menu.h"

void sys_menu_auto_off_label(int seconds, char *out, size_t n)
{
	if (seconds <= 0)      snprintf(out, n, "never");
	else if (seconds < 60) snprintf(out, n, "%ds", seconds);
	else                   snprintf(out, n, "%dm", seconds / 60);
}

/* Three rows say the same thing when the radio is down, and they say it in
 * the value column rather than by vanishing: a row that disappears when it
 * cannot be used teaches nobody why. */
#define NEEDS_WIFI(on) ((on) ? NULL : "needs Wi-Fi")

int sys_menu_zyperos_rows(const sys_ui *u, pm_row *out)
{
	int n = 0, r;

	for (r = 0; r < PM_ROWS; r++) {
		/* Everything that needs the network, including both sign-ins: an
		 * account nobody can reach is a row that can only fail. Audio Output
		 * stays, since a cable and the speaker are still two places. */
		if (u->no_wifi && (r == PM_WIFI || r == PM_ACHIEVEMENTS))
			continue;
		/* Unless the files go over a cable, which needs no radio. */
		if (u->no_wifi && !u->cable_xfer && r == PM_XFER) continue;
		if (u->no_bt && r == PM_BT) continue;
		if (u->no_developer && r == PM_DEVELOPER) continue;
		if (!u->update && r == PM_UPDATE) continue;
		if (u->no_lights && r == PM_LIGHTS) continue;
		if (u->no_charge_limit && r == PM_CHARGE) continue;
		out[n++] = (pm_row)r;
	}
	return n;
}

int sys_menu_system_rows(const sys_ui *u, sm_row *out)
{
	int n = 0, r;

	for (r = 0; r < SM_ROWS; r++) {
		if (u->no_wifi && r == SM_BOXART) continue;
		out[n++] = (sm_row)r;
	}
	return n;
}

int sys_menu_scraper_rows(const scraper_ui *u, scraper_bufs *b, menu_row *out,
                          sc_row *ids)
{
	int n = 0;
	bool counts = u->running || u->ran;

	/* The run: its progress while it goes, else what it would fetch */
	if (u->running) {
		snprintf(b->fetch, sizeof b->fetch, "%d / %d", u->done, u->total);
		ids[n] = SC_FETCH;
		out[n++] = (menu_row){ "Fetching", b->fetch, true };
	} else {
		if (!u->net) snprintf(b->fetch, sizeof b->fetch, "needs Wi-Fi");
		else if (u->missing == 0) snprintf(b->fetch, sizeof b->fetch, "complete");
		else if (u->missing > 0) snprintf(b->fetch, sizeof b->fetch, "%d missing", u->missing);
		else b->fetch[0] = '\0';
		ids[n] = SC_FETCH;
		out[n++] = (menu_row){ "Fetch Missing Art", b->fetch[0] ? b->fetch : NULL, u->net };
	}
	ids[n] = SC_SNAPS;
	out[n++] = (menu_row){ "Video Snaps", u->snaps, !u->running };
	ids[n] = SC_NONE;
	out[n++] = MENU_HR;

	/* The sources, in the order a run asks them */
	if (counts) {
		snprintf(b->ss, sizeof b->ss, "%d covers", u->ss_found);
	} else if (!u->ss_have) {
		snprintf(b->ss, sizeof b->ss, "not in this build");
	} else if (u->ss_in) {
		snprintf(b->ss, sizeof b->ss, "linked as %s", u->ss_name ? u->ss_name : "");
	} else {
		snprintf(b->ss, sizeof b->ss, "link");
	}
	ids[n] = SC_SS;
	out[n++] = (menu_row){ "ScreenScraper", b->ss, !u->running && u->ss_have };
	if (counts) snprintf(b->lr, sizeof b->lr, "%d covers", u->lr_found);
	else snprintf(b->lr, sizeof b->lr, "no account");
	ids[n] = SC_LIBRETRO;
	out[n++] = (menu_row){ "libretro", b->lr, false };
	if (counts) snprintf(b->sg, sizeof b->sg, "%d covers", u->sg_found);
	else snprintf(b->sg, sizeof b->sg, "%s", u->sgdb ? u->sgdb : "link");
	ids[n] = SC_SGDB;
	out[n++] = (menu_row){ "SteamGridDB", b->sg, !u->running };
	/* No notes: at 640x480 the panel holds these six rows and no more. */
	return n;
}

int sys_menu_ss_rows(bool have, const char *user, const char *status,
                     menu_row *out, ssp_row *ids)
{
	int n = 0;

	ids[n] = SSP_ACCOUNT;
	out[n++] = (menu_row){ "Account", !have ? "not in this build" : user ? user : "none", have };
	if (have && user) {
		ids[n] = SSP_STATUS;
		out[n++] = (menu_row){ "Status", status ? status : "linked", false };
		ids[n] = SSP_UNLINK;
		out[n++] = (menu_row){ "Unlink", NULL, true };
	}
	ids[n] = SSP_NONE;
	out[n++] = MENU_RULE;
	ids[n] = SSP_NONE;
	out[n++] = MENU_NOTE("Covers, text and video snaps.");
	ids[n] = SSP_NONE;
	out[n++] = MENU_NOTE(have ? "Free at screenscraper.fr." : "Needs a build with its key.");
	return n;
}

const char *const SNAP_SOUND_NAME[3] = { "muted", "quiet", "full" };

void sys_menu_bytes(unsigned long long v, char *out, size_t n)
{
	if (v < 1000ull * 1000)            snprintf(out, n, "%llu KB", (v + 999) / 1000);
	else if (v < 1000ull * 1000 * 1000) snprintf(out, n, "%llu MB", (v + 500000) / 1000000);
	else snprintf(out, n, "%.1f GB", (double)v / 1e9);
}

int sys_menu_snaps_rows(const snaps_ui *u, snaps_bufs *b, menu_row *out,
                        sn_row *ids, int *console)
{
	int n = 0, i;

	/* Fetching needs ScreenScraper, the one source with snaps; what is on
	 * the card already plays without it. */
	ids[n] = SN_FETCH; console[n] = -1;
	out[n++] = (menu_row){ "Fetch with Box Art",
	                       !u->ss_have ? "not in this build" :
	                       !u->ss_in ? "needs ScreenScraper" : u->fetch ? "on" : "off",
	                       u->ss_have && u->ss_in };
	ids[n] = SN_PLAY; console[n] = -1;
	out[n++] = (menu_row){ "Play on the Shelf", u->play ? "on" : "off", true };
	ids[n] = SN_SOUND; console[n] = -1;
	out[n++] = (menu_row){ "Sound", SNAP_SOUND_NAME[u->sound >= 0 && u->sound < 3 ? u->sound : 0], true };
	ids[n] = SN_NONE; console[n] = -1;
	out[n++] = MENU_HR;
	for (i = 0; i < u->nc && i < CFG_MAX_SYSTEMS; i++) {
		char sz[24];

		sys_menu_bytes(u->c[i].bytes, sz, sizeof sz);
		snprintf(b->size[i], sizeof b->size[i], "%s · %s", u->c[i].on ? "on" : "off", sz);
		ids[n] = SN_CONSOLE; console[n] = i;
		out[n++] = (menu_row){ u->c[i].name, b->size[i], true };
	}
	sys_menu_bytes(u->total, b->total, sizeof b->total);
	ids[n] = SN_DELETE; console[n] = -1;
	out[n++] = (menu_row){ "Delete All", b->total, u->total > 0 };
	return n;
}

int sys_menu_lights_rows(side_lights on, menu_row *out, side_light_part *ids)
{
	int i;

	for (i = 0; i < LP_COUNT; i++) {
		ids[i] = (side_light_part)i;
		out[i] = (menu_row){ side_lights_part_name((side_light_part)i),
		                     side_lights_has(on, (side_light_part)i) ? "On" : "Off", true };
	}
	return LP_COUNT;
}

int sys_menu_sgdb_rows(const char *masked, const char *state, menu_row *out,
                       sg_row *ids)
{
	int n = 0;

	/* A on Key types one, with a key there or not; the two notes say where
	 * one comes from, and that pasting beats typing 32 characters. */
	ids[n] = SG_KEY;
	out[n++] = (menu_row){ "Key", masked ? masked : "none", true };
	if (masked) {
		ids[n] = SG_STATUS;
		out[n++] = (menu_row){ "Status", state ? state : "not checked", false };
		ids[n] = SG_REMOVE;
		out[n++] = (menu_row){ "Unlink", NULL, true };
	}
	ids[n] = SG_NONE;
	out[n++] = MENU_RULE;
	ids[n] = SG_NONE;
	out[n++] = MENU_NOTE("Get one at steamgriddb.com.");
	ids[n] = SG_NONE;
	out[n++] = MENU_NOTE("Or paste it in File Transfer.");
	return n;
}

int sys_menu_muse_rows(int kind, bool both, bool no_wifi, sm_muse_row *out)
{
	int n = 0;

	out[n++] = SMM_COUNT;
	if (both) out[n++] = SMM_SHOW;
	out[n++] = SMM_SORT;
	if (kind == 0) out[n++] = SMM_SHUFFLE;
	out[n++] = SMM_LOCK;
	if (kind == 0 && !no_wifi) out[n++] = SMM_ART;
	out[n++] = SMM_RESCAN;
	return n;
}

/* Each row's sound class, by its id (menu.h; DESIGN.md 9). A value that
 * steps round is heard on the press; Auto Off stops at its ends and is heard
 * after, by whether it moved; A on a row that does nothing with it is deny;
 * UI Sounds plays its own, in the voice it comes to. */
static const unsigned char PM_SOUND[PM_ROWS] = {
	[PM_AUDIO]      = MENU_SND_RING,
	[PM_CHANNELS]   = MENU_SND_RING,
	[PM_SOUNDS]     = MENU_SND_OWN,
	[PM_SLEEP]      = MENU_SND_NO_A,
	[PM_CHARGE]     = MENU_SND_RING,
	[PM_APPEARANCE] = MENU_SND_RING,
	[PM_HOME]       = MENU_SND_RING,
	[PM_ORDER]      = MENU_SND_RING,
	[PM_BATTERY]    = MENU_SND_RING,
	[PM_HOURS]      = MENU_SND_RING,
	[PM_DEVELOPER]  = MENU_SND_RING,
	/* A pick is heard as reward, or deny with nothing to pick */
	[PM_RANDOM]     = MENU_SND_OWN,
};
/* Core steps on A as well; Sort By and Display Mode do nothing with it. */
static const unsigned char SM_SOUND[SM_ROWS] = {
	[SM_CORE]    = MENU_SND_RING,
	[SM_SORT]    = MENU_SND_RING | MENU_SND_NO_A,
	[SM_DISPLAY] = MENU_SND_RING | MENU_SND_NO_A,
	[SM_RANDOM]  = MENU_SND_OWN,
};
static unsigned char smm_sound(sm_muse_row r)
{
	return r == SMM_SHOW ? MENU_SND_RING
	     : r == SMM_SORT ? MENU_SND_RING | MENU_SND_NO_A : 0;
}

int sys_menu_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                   const char **heading)
{
	bool net = u->wifi == WIFI_CONNECTED;

	memset(b->sound, 0, sizeof b->sound);

	if (u->games) {
		snprintf(b->a, sizeof b->a, "%d", u->game_count);
		snprintf(b->b, sizeof b->b, "%s", u->sys_core ? u->sys_core : "");
		*heading = u->sys_name;
		/* A shelf of favorites gets the two rows that are true of it and
		 * nothing else - not even the enum's order, since Core sits between
		 * them and it has none. See SM_FAV_ROWS. */
		if (u->fav) {
			out[0] = (menu_row){ "Games",   b->a,                      false };
			out[1] = (menu_row){ "Random Game", NULL, u->game_count > 0 };
			out[2] = (menu_row){ "Sort By", u->sort ? u->sort : "Name", true };
			b->sound[1] = SM_SOUND[SM_RANDOM];
			b->sound[2] = SM_SOUND[SM_SORT];
			return SM_FAV_ROWS;
		}
		if (u->muse) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int k, n = sys_menu_muse_rows(u->muse_books ? 1 : u->muse_videos ? 2 : 0,
			                              u->muse_both, u->no_wifi, ids);

			for (k = 0; k < n; k++) b->sound[k] = smm_sound(ids[k]);
			for (k = 0; k < n; k++)
				switch (ids[k]) {
				case SMM_COUNT:
					out[k] = (menu_row){ u->muse_books ? "Books"
					                     : u->muse_videos ? "Videos" : "Albums",
					                     b->a, false };
					break;
				case SMM_SHOW:
					out[k] = (menu_row){ "Show", u->muse_books ? "Audiobooks"
					                     : u->muse_videos ? "Videos" : "Music", true };
					break;
				case SMM_SORT:
					out[k] = (menu_row){ "Sort By", u->sort ? u->sort
					                     : u->muse_books ? "Author"
					                     : u->muse_videos ? "Folder" : "Artist", true };
					break;
				case SMM_SHUFFLE:
					out[k] = (menu_row){ "Shuffle All", NULL, true };
					break;
				case SMM_LOCK:
					out[k] = (menu_row){ "Lock Screen",
					                     u->muse_playing ? NULL : "while playing",
					                     u->muse_playing };
					break;
				case SMM_ART:
					out[k] = (menu_row){ "Album Art", NEEDS_WIFI(net), net };
					break;
				case SMM_RESCAN:
					out[k] = (menu_row){ "Rescan Folder", NULL, true };
					break;
				}
			return n;
		}
		menu_row full[SM_ROWS];

		full[SM_GAMES]   = (menu_row){ "Games",         b->a,        false };
		/* One of them, picked and started: src/randgame.h */
		full[SM_RANDOM]  = (menu_row){ "Random Game",   NULL,        u->game_count > 0 };
		full[SM_CORE]    = (menu_row){ "Core",          b->b,        u->core_choice };
		full[SM_SORT]    = (menu_row){ "Sort By",
		                              u->sort ? u->sort : "Name", true  };
		full[SM_DISPLAY] = (menu_row){ "Display Mode",  u->dmode,    true  };
		/* Button Mapping is out until there is something behind it. Diatom
		 * supplies core button labels, so the hook is real - but a dead row
		 * in a menu of live ones is a promise the launcher is not keeping,
		 * and it has sat there unwired longer than it was ever going to be
		 * worth. Put the enum entry back with it when it is built.
		 *
		 * Show went with it on 2026-09-17, and it was the weaker of the two:
		 * Button Mapping at least named something the emulator can do, where
		 * Show read "All games" because that string was written here and
		 * nothing could ever change it. Games and Core are dead rows too and
		 * they stay, because what they report is true - the shelf's count and
		 * the core that will run it. The filter Show promised is on the shelf
		 * already: Y marks a favorite and Favorites is its own shelf. */
		/* full[SM_BUTTONS] = (menu_row){ "Button Mapping", NULL,    false }; */
		/* Just this system. Needs the network like its counterpart in the
		 * ZyperOS menu, and says so rather than opening a screen that can only
		 * report the same thing. */
		full[SM_BOXART]  = (menu_row){ "Box Art",       NEEDS_WIFI(net), net };
		/* Live now that there is something behind it. It rescans the whole
		 * card rather than this one folder - the work is three directory reads
		 * and the shared parts (hiding a system that emptied, rebuilding
		 * Favorites) have to run anyway - but the folder you are standing in
		 * is the one you came here to refresh, so the name still describes
		 * what you asked for. */
		full[SM_RESCAN]  = (menu_row){ "Rescan Folder", NULL,        true  };
		{
			sm_row ids[SM_ROWS];
			int k, n = sys_menu_system_rows(u, ids);

			for (k = 0; k < n; k++) {
				out[k] = full[ids[k]];
				b->sound[k] = SM_SOUND[ids[k]];
			}
			return n;
		}
	}

	menu_row full[PM_ROWS];

	*heading = "ZyperOS";
	/* The network's name, not its address. A settings row should say what the
	 * setting IS; the address is a fact about the machine and lives on the
	 * About page with the other ones. */
	if (net && u->ssid && u->ssid[0])
		snprintf(b->b, sizeof b->b, "%s", u->ssid);
	else
		snprintf(b->b, sizeof b->b, "%s",
		         u->wifi == WIFI_CONNECTING ? "connecting" :
		         u->wifi == WIFI_IDLE       ? "not connected" : "off");
	full[PM_WIFI]         = (menu_row){ "Wi-Fi",     b->b,      true  };
	full[PM_BT]           = (menu_row){ "Bluetooth",
	                                   u->bt_name ? u->bt_name : "not connected",
	                                   true  };
	/* Where the system's sound goes - not Diatom's, which is why the label says
	 * neither "game" nor "emulator": the audiobook and music player will read
	 * the same setting. See src/audioout.c for the rule.
	 *
	 * Auto names what it resolved to, because a row that reads only "Auto"
	 * makes the player guess which of three places they are about to hear. */
	if (u->audio_policy == AOUT_AUTO)
		snprintf(b->d, sizeof b->d, "auto (%s)", aout_dest_name(u->audio_dest));
	else
		snprintf(b->d, sizeof b->d, "%s", aout_dest_name(u->audio_dest));
	full[PM_AUDIO]        = (menu_row){ "Audio Output", b->d, true };
	/* Stereo, mono, or mono on one side, for a player who hears with one ear.
	 * On every device and with every output: the speaker makes plain mono of
	 * a side (src/audioout.h), and the row still says what was chosen. */
	full[PM_CHANNELS]     = (menu_row){ "Audio Channels",
	                                   aout_channels_name(u->audio_channels), true };
	/* The launcher's own sounds: which of the three voices, or Off. Live on
	 * every device and with every output; on a Bluetooth headset the sounds
	 * are not played at all (src/uisound.h), and the row still says which
	 * voice comes back with the speaker. */
	full[PM_SOUNDS]       = (menu_row){ "UI Sounds",
	                                   u->ui_sounds ? u->ui_sounds
	                                                : DS_VOICE_NAME[DS_VOICE_DEFAULT],
	                                   true };
	/* Menu music, which opens its own screen: on or off, how loud, which
	 * track (src/bgm_menu.h). */
	full[PM_MUSIC]        = (menu_row){ "Menu Music",
	                                   u->menu_music ? u->menu_music : "Off", true };
	/* Files onto and off the device over Wi-Fi: a small web server on the LAN
	 * that a phone or a laptop opens. Named for OTA, which is what everyone
	 * already calls this, and for the other half of the fable - the tortoise
	 * runs the system, the hare carries the files.
	 *
	 * Directly under Wi-Fi because it is useless without it, and reads as an
	 * answer to the row above rather than a separate idea.
	 *
	 * Over the USB cable, where a device does it that way, it is Down to the
	 * Wire, and the cable is always there to try. */
	full[PM_XFER]         = u->cable_xfer
	                      ? (menu_row){ XFER_NAME_CABLE, NULL, true }
	                      : (menu_row){ XFER_NAME_WIFI, NEEDS_WIFI(net), net };
	/* On no network with an adapter that can host one, the screen makes its
	 * own (src/hotspot.h), and the row says that is what will happen. */
	if (!u->cable_xfer && !net && u->hotspot)
		full[PM_XFER] = (menu_row){ XFER_NAME_WIFI, "hotspot", true };
	/* On behind the shelf: the row opens it again, and says what it does */
	if (u->xfer)
		full[PM_XFER] = (menu_row){ u->xfer_cable ? XFER_NAME_CABLE : XFER_NAME_WIFI, u->xfer, true };
	full[PM_STATS]        = (menu_row){ "Play Time",  NULL,      true  };
	/* A game from any system, picked and started (src/randgame.h) */
	full[PM_RANDOM]       = (menu_row){ "Random Game", NULL,     true  };
	/* On every device: the Brick takes its time from Wi-Fi, but the time zone
	 * is chosen here on both. */
	full[PM_CLOCK]        = (menu_row){ "Date & Time", u->clock,  true  };
	/* How the time is written, on the shelf and in the row above. On every
	 * device, like Date & Time. */
	full[PM_HOURS]        = (menu_row){ "Clock",
	                                   u->clock_24h ? "24-hour" : "12-hour", true };
	full[PM_SLEEP]        = (menu_row){ "Auto Off",  b->c,      true  };
	/* Off, 80%, 85%, 90%, stepped round like Side Lights */
	if (u->charge_limit > 0) snprintf(b->e, sizeof b->e, "%d%%", u->charge_limit);
	else snprintf(b->e, sizeof b->e, "Off");
	full[PM_CHARGE]       = (menu_row){ "Charge Limit", b->e,  true  };
	/* How many parts of what the side lights show are on; A opens the page
	 * of switches (sys_menu_lights_rows). L2+R2 shows the level in every
	 * setting. */
	full[PM_LIGHTS]       = (menu_row){ "Side Lights",
	                                   u->lights ? u->lights : "All on", true };
	full[PM_HOME] = (menu_row){ "Home Screen", u->home ? u->home : "Spotlight", true };
	/* The systems' order on the home, A to Z or by release date, stepped
	 * round like Home Screen (docs/design/mock/controls-lab.html) */
	full[PM_ORDER] = (menu_row){ "System Order", u->order ? u->order : "A to Z", true };
	/* The status corner's battery: the mark alone, or its charge written
	 * beside it (2026-11-05). On every device: both have a battery. */
	full[PM_BATTERY] = (menu_row){ "Battery Icon", u->battery ? u->battery : "Bars", true };
	/* Appearance applies on every device. */
	full[PM_APPEARANCE]   = (menu_row){ "Appearance",
	                                   u->appearance ? u->appearance : "Light", true };
	/* Not "Sleep". The device has no suspend and is not getting one. This
	 * powers off, and resume-into-game brings you back where you were, which
	 * is what sleep would have been for. */
	sys_menu_auto_off_label(u->auto_off, b->c, sizeof b->c);
	/* Box Art, its sources and Video Snaps, behind one row (DESIGN.md
	 * 11.19). Live with no network too: its fetch says it needs Wi-Fi, and
	 * the rest of it does not. */
	full[PM_SCRAPER]      = (menu_row){ "Scraper", u->scraper, true };
	full[PM_ACHIEVEMENTS] = (menu_row){ "Cheevos",
	                                   u->ra_in ? u->ra_name : "sign in",
	                                   true };
	/* What every button does, per screen. Needs nothing of the device, which
	 * is the point: it is the page you reach when the thing you have forgotten
	 * is which button opens Muse. See src/controls.h. */
	full[PM_CONTROLS]     = (menu_row){ "Controls",    NULL,   true  };
	/* The first-launch tutorial again, as often as anyone likes. Needs
	 * nothing either: it is drawn from shapes, not from the library. */
	full[PM_TOUR]         = (menu_row){ "Show Tutorial", NULL, true  };
	full[PM_DEVELOPER]    = (menu_row){ "Developer Mode",
	                                   u->developer ? "on" : "off", true };
	full[PM_UPDATE]       = (menu_row){ "System Update", u->update, true };
	full[PM_ABOUT]        = (menu_row){ "About ZyperOS", NULL,   true  };
	/* Any game by its name, typed (src/search.h) */
	full[PM_SEARCH]       = (menu_row){ "Search Games", NULL,    true  };
	{
		pm_row ids[PM_ROWS];
		int k, n = sys_menu_zyperos_rows(u, ids);

		for (k = 0; k < n; k++) {
			out[k] = full[ids[k]];
			b->sound[k] = PM_SOUND[ids[k]];
		}
		return n;
	}
}
