/* What the two MENU-button menus contain. See src/sys_menu.h for why this is
 * a file of its own.
 *
 * No SDL, no device reads, no forks. Everything this needs arrives in sys_ui. */
#include <stdio.h>
#include <string.h>

#include "design.h"
#include "sys_menu.h"
#include "i18n.h"
#include "uisound.h"

void sys_menu_auto_off_label(int seconds, char *out, size_t n)
{
	if (seconds <= 0)      snprintf(out, n, "%s", T(S_SYSMENU_AUTO_OFF_NEVER));
	else if (seconds < 60) snprintf(out, n, T(S_SYSMENU_AUTO_OFF_SECS), seconds);
	else                   snprintf(out, n, T(S_SYSMENU_AUTO_OFF_MINS), seconds / 60);
}

const char *sys_menu_order_name(bool release)
{
	return T(release ? S_SYSMENU_ORDER_RELEASE : S_SYSMENU_ORDER_AZ);
}

const char *sys_menu_battery_name(int mode)
{
	return T(mode == 2 ? S_SYSMENU_BATTERY_TIME : mode == 1 ? S_SYSMENU_BATTERY_PERCENT
	                                                       : S_SYSMENU_BATTERY_BARS);
}

/* Three rows say the same thing when the radio is down, and they say it in
 * the value column rather than by vanishing: a row that disappears when it
 * cannot be used teaches nobody why. */
#define NEEDS_WIFI(on) ((on) ? NULL : T(S_MENU_NEEDS_WIFI))

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
		if (u->no_usb_power && r == PM_USB) continue;
		if ((u->no_usb_power || u->no_usb_wait) && r == PM_USB_WAIT) continue;
		out[n++] = (pm_row)r;
	}
	return n;
}

int sys_menu_system_rows(const sys_ui *u, sm_row *out)
{
	int n = 0, r;

	for (r = 0; r < SM_ROWS; r++) {
		if (u->no_wifi && r == SM_BOXART) continue;
		if (!u->dslang && r == SM_DSLANG) continue;
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
		out[n++] = (menu_row){ T(S_SYSMENU_SC_FETCHING), b->fetch, true };
	} else {
		if (!u->net) snprintf(b->fetch, sizeof b->fetch, "%s", T(S_MENU_NEEDS_WIFI));
		else if (u->missing == 0) snprintf(b->fetch, sizeof b->fetch, "%s", T(S_SYSMENU_SC_COMPLETE));
		else if (u->missing > 0)
			snprintf(b->fetch, sizeof b->fetch, TN(S_SYSMENU_SC_MISSING, u->missing), u->missing);
		else b->fetch[0] = '\0';
		ids[n] = SC_FETCH;
		out[n++] = (menu_row){ T(S_SYSMENU_SC_FETCH), b->fetch[0] ? b->fetch : NULL, u->net };
	}
	ids[n] = SC_SNAPS;
	out[n++] = (menu_row){ T(S_SYSMENU_SC_VIDEO_SNAPS), u->snaps, !u->running };
	ids[n] = SC_NONE;
	out[n++] = MENU_HR;

	/* The sources, in the order a run asks them */
	if (counts) {
		snprintf(b->ss, sizeof b->ss, TN(S_SYSMENU_SC_COVERS, u->ss_found), u->ss_found);
	} else if (!u->ss_have) {
		snprintf(b->ss, sizeof b->ss, "%s", T(S_SYSMENU_NOT_IN_BUILD));
	} else if (u->ss_in) {
		snprintf(b->ss, sizeof b->ss, T(S_SYSMENU_SC_LINKED_AS), u->ss_name ? u->ss_name : "");
	} else {
		snprintf(b->ss, sizeof b->ss, "%s", T(S_SYSMENU_SC_LINK));
	}
	ids[n] = SC_SS;
	out[n++] = (menu_row){ "ScreenScraper", b->ss, !u->running && u->ss_have };
	if (counts) snprintf(b->lr, sizeof b->lr, TN(S_SYSMENU_SC_COVERS, u->lr_found), u->lr_found);
	else snprintf(b->lr, sizeof b->lr, "%s", T(S_SYSMENU_SC_NO_ACCOUNT));
	ids[n] = SC_LIBRETRO;
	out[n++] = (menu_row){ "libretro", b->lr, false };
	if (counts) snprintf(b->sg, sizeof b->sg, TN(S_SYSMENU_SC_COVERS, u->sg_found), u->sg_found);
	else snprintf(b->sg, sizeof b->sg, "%s", u->sgdb ? u->sgdb : T(S_SYSMENU_SC_LINK));
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
	out[n++] = (menu_row){ T(S_SYSMENU_SS_ACCOUNT), !have ? T(S_SYSMENU_NOT_IN_BUILD)
	                       : user ? user : T(S_SYSMENU_SS_ACCOUNT_NONE), have };
	if (have && user) {
		ids[n] = SSP_STATUS;
		out[n++] = (menu_row){ T(S_SYSMENU_STATUS), status ? status : T(S_SYSMENU_SS_LINKED), false };
		ids[n] = SSP_UNLINK;
		out[n++] = (menu_row){ T(S_SYSMENU_UNLINK), NULL, true };
	}
	ids[n] = SSP_NONE;
	out[n++] = MENU_RULE;
	ids[n] = SSP_NONE;
	out[n++] = MENU_NOTE(T(S_SYSMENU_SS_NOTE_BRINGS));
	ids[n] = SSP_NONE;
	out[n++] = MENU_NOTE(have ? T(S_SYSMENU_SS_NOTE_FREE) : T(S_SYSMENU_SS_NOTE_KEY));
	return n;
}

const char *const SNAP_SOUND_NAME[3] = { "muted", "quiet", "full" };
/* What the Sound row says for each: the words above are the database's */
static const str_id SNAP_SOUND_TEXT[3] = { S_SYSMENU_SN_MUTED, S_SYSMENU_SN_QUIET, S_SYSMENU_SN_FULL };

void sys_menu_bytes(unsigned long long v, char *out, size_t n)
{
	char num[32], *dot;

	if (v < 1000ull * 1000)            snprintf(out, n, T(S_SYSMENU_SIZE_KB), (v + 999) / 1000);
	else if (v < 1000ull * 1000 * 1000) snprintf(out, n, T(S_SYSMENU_SIZE_MB), (v + 500000) / 1000000);
	else {
		/* %.1f writes a point; the language may write a comma */
		snprintf(num, sizeof num, "%.1f", (double)v / 1e9);
		if ((dot = strchr(num, '.')) && T(S_MENU_DECIMAL_POINT)[0] == ',') *dot = ',';
		snprintf(out, n, T(S_SYSMENU_SIZE_GB), num);
	}
}

int sys_menu_snaps_rows(const snaps_ui *u, snaps_bufs *b, menu_row *out,
                        sn_row *ids, int *console)
{
	int n = 0, i;

	/* Fetching needs ScreenScraper, the one source with snaps; what is on
	 * the card already plays without it. */
	ids[n] = SN_FETCH; console[n] = -1;
	out[n++] = (menu_row){ T(S_SYSMENU_SN_FETCH),
	                       !u->ss_have ? T(S_SYSMENU_NOT_IN_BUILD) :
	                       !u->ss_in ? T(S_SYSMENU_SN_NEEDS_SS) : u->fetch ? T(S_MENU_ON) : T(S_MENU_OFF),
	                       u->ss_have && u->ss_in };
	ids[n] = SN_PLAY; console[n] = -1;
	out[n++] = (menu_row){ T(S_SYSMENU_SN_PLAY), u->play ? T(S_MENU_ON) : T(S_MENU_OFF), true };
	ids[n] = SN_SOUND; console[n] = -1;
	out[n++] = (menu_row){ T(S_SYSMENU_SN_SOUND),
	                       T(SNAP_SOUND_TEXT[u->sound >= 0 && u->sound < 3 ? u->sound : 0]), true };
	ids[n] = SN_NONE; console[n] = -1;
	out[n++] = MENU_HR;
	for (i = 0; i < u->nc && i < CFG_MAX_SYSTEMS; i++) {
		char sz[24];

		sys_menu_bytes(u->c[i].bytes, sz, sizeof sz);
		snprintf(b->size[i], sizeof b->size[i], "%s · %s", u->c[i].on ? T(S_MENU_ON) : T(S_MENU_OFF), sz);
		ids[n] = SN_CONSOLE; console[n] = i;
		out[n++] = (menu_row){ u->c[i].name, b->size[i], true };
	}
	sys_menu_bytes(u->total, b->total, sizeof b->total);
	ids[n] = SN_DELETE; console[n] = -1;
	out[n++] = (menu_row){ T(S_SYSMENU_SN_DELETE), b->total, u->total > 0 };
	return n;
}

int sys_menu_lights_rows(side_lights on, menu_row *out, side_light_part *ids)
{
	int i;

	for (i = 0; i < LP_COUNT; i++) {
		ids[i] = (side_light_part)i;
		out[i] = (menu_row){ side_lights_part_name((side_light_part)i),
		                     side_lights_has(on, (side_light_part)i) ? T(S_MENU_ON_TITLE)
		                                                             : T(S_MENU_OFF_TITLE), true };
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
	out[n++] = (menu_row){ T(S_SYSMENU_SG_KEY), masked ? masked : T(S_SYSMENU_SG_KEY_NONE), true };
	if (masked) {
		ids[n] = SG_STATUS;
		out[n++] = (menu_row){ T(S_SYSMENU_STATUS), state ? state : T(S_SYSMENU_SG_NOT_CHECKED), false };
		ids[n] = SG_REMOVE;
		out[n++] = (menu_row){ T(S_SYSMENU_UNLINK), NULL, true };
	}
	ids[n] = SG_NONE;
	out[n++] = MENU_RULE;
	ids[n] = SG_NONE;
	out[n++] = MENU_NOTE(T(S_SYSMENU_SG_NOTE_GET));
	ids[n] = SG_NONE;
	out[n++] = MENU_NOTE(T(S_SYSMENU_SG_NOTE_PASTE));
	return n;
}

int sys_menu_muse_rows(int kind, bool both, bool no_wifi, sm_muse_row *out)
{
	int n = 0;

	out[n++] = SMM_COUNT;
	if (both) out[n++] = SMM_SHOW;
	out[n++] = SMM_SORT;
	if (kind == 0 || kind == 3) out[n++] = SMM_SHUFFLE;
	out[n++] = SMM_LOCK;
	if ((kind == 0 || kind == 3) && !no_wifi) out[n++] = SMM_ART;
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
	[PM_USB]        = MENU_SND_RING,
	/* A pick is heard as reward, or deny with nothing to pick */
	[PM_RANDOM]     = MENU_SND_OWN,
};
/* Core and Game Language step on A as well; Sort By and Display Mode do
 * nothing with it. */
static const unsigned char SM_SOUND[SM_ROWS] = {
	[SM_CORE]    = MENU_SND_RING,
	[SM_DSLANG]  = MENU_SND_RING,
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
			out[0] = (menu_row){ T(S_SYSMENU_GAMES), b->a,               false };
			out[1] = (menu_row){ T(S_SYSMENU_RANDOM_GAME), NULL, u->game_count > 0 };
			out[2] = (menu_row){ T(S_SYSMENU_SORT_BY), u->sort ? u->sort : T(S_SORT_NAME), true };
			b->sound[1] = SM_SOUND[SM_RANDOM];
			b->sound[2] = SM_SOUND[SM_SORT];
			return SM_FAV_ROWS;
		}
		if (u->muse) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int k, n = sys_menu_muse_rows(u->muse_books ? 1 : u->muse_videos ? 2
			                              : u->muse_all ? (u->muse_songs ? 3 : 2) : 0,
			                              u->muse_both, u->no_wifi, ids);

			for (k = 0; k < n; k++) b->sound[k] = smm_sound(ids[k]);
			for (k = 0; k < n; k++)
				switch (ids[k]) {
				case SMM_COUNT:
					out[k] = (menu_row){ u->muse_books ? T(S_SYSMENU_MUSE_BOOKS)
					                     : u->muse_videos ? T(S_SYSMENU_MUSE_VIDEOS)
					                     : u->muse_all ? T(S_SYSMENU_MUSE_TOTAL)
					                     : T(S_SYSMENU_MUSE_ALBUMS),
					                     b->a, false };
					break;
				case SMM_SHOW:
					out[k] = (menu_row){ T(S_SYSMENU_MUSE_SHOW), u->muse_books ? T(S_SYSMENU_MUSE_AUDIOBOOKS)
					                     : u->muse_videos ? T(S_SYSMENU_MUSE_VIDEOS)
					                     : u->muse_all ? T(S_SYSMENU_MUSE_ALL)
					                     : T(S_SYSMENU_MUSE_MUSIC), true };
					break;
				case SMM_SORT:
					out[k] = (menu_row){ T(S_SYSMENU_SORT_BY), u->sort ? u->sort
					                     : u->muse_books ? T(S_SYSMENU_MUSE_AUTHOR)
					                     : u->muse_videos || u->muse_all ? T(S_SYSMENU_MUSE_FOLDER)
					                     : T(S_SYSMENU_MUSE_ARTIST), true };
					break;
				case SMM_SHUFFLE:
					out[k] = (menu_row){ T(S_SYSMENU_MUSE_SHUFFLE), NULL, true };
					break;
				case SMM_LOCK:
					out[k] = (menu_row){ T(S_SYSMENU_MUSE_LOCK),
					                     u->muse_playing ? NULL : T(S_SYSMENU_MUSE_LOCK_WHEN),
					                     u->muse_playing };
					break;
				case SMM_ART:
					out[k] = (menu_row){ T(S_SYSMENU_MUSE_ART), NEEDS_WIFI(net), net };
					break;
				case SMM_RESCAN:
					out[k] = (menu_row){ T(S_SYSMENU_RESCAN), NULL, true };
					break;
				}
			return n;
		}
		menu_row full[SM_ROWS];

		full[SM_GAMES]   = (menu_row){ T(S_SYSMENU_GAMES),        b->a, false };
		/* One of them, picked and started: src/randgame.h */
		full[SM_RANDOM]  = (menu_row){ T(S_SYSMENU_RANDOM_GAME),  NULL, u->game_count > 0 };
		full[SM_CORE]    = (menu_row){ T(S_SYSMENU_CORE),         b->b, u->core_choice };
		/* The Nintendo DS's game language, a choice stepped like the core */
		full[SM_DSLANG]  = (menu_row){ T(S_SYSMENU_DSLANG), u->dslang, u->dslang != NULL };
		full[SM_SORT]    = (menu_row){ T(S_SYSMENU_SORT_BY),
		                              u->sort ? u->sort : T(S_SORT_NAME), true  };
		full[SM_DISPLAY] = (menu_row){ T(S_SYSMENU_DISPLAY_MODE), u->dmode, true  };
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
		full[SM_BOXART]  = (menu_row){ T(S_SYSMENU_BOX_ART), NEEDS_WIFI(net), net };
		/* Live now that there is something behind it. It rescans the whole
		 * card rather than this one folder - the work is three directory reads
		 * and the shared parts (hiding a system that emptied, rebuilding
		 * Favorites) have to run anyway - but the folder you are standing in
		 * is the one you came here to refresh, so the name still describes
		 * what you asked for. */
		full[SM_RESCAN]  = (menu_row){ T(S_SYSMENU_RESCAN), NULL, true  };
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
		         u->wifi == WIFI_CONNECTING ? T(S_SYSMENU_WIFI_CONNECTING) :
		         u->wifi == WIFI_IDLE       ? T(S_MENU_NOT_CONNECTED) : T(S_MENU_OFF));
	full[PM_WIFI]         = (menu_row){ T(S_MENU_WIFI), b->b, true };
	full[PM_BT]           = (menu_row){ "Bluetooth",
	                                   u->bt_name ? u->bt_name : T(S_MENU_NOT_CONNECTED),
	                                   true  };
	/* Where the system's sound goes - not Diatom's, which is why the label says
	 * neither "game" nor "emulator": the audiobook and music player will read
	 * the same setting. See src/audioout.c for the rule.
	 *
	 * Auto names what it resolved to, because a row that reads only "Auto"
	 * makes the player guess which of three places they are about to hear. */
	if (u->audio_policy == AOUT_AUTO)
		snprintf(b->d, sizeof b->d, T(S_SYSMENU_AUDIO_AUTO), aout_dest_label(u->audio_dest));
	else
		snprintf(b->d, sizeof b->d, "%s", aout_dest_label(u->audio_dest));
	full[PM_AUDIO]        = (menu_row){ T(S_SYSMENU_AUDIO_OUTPUT), b->d, true };
	/* Stereo, mono, or mono on one side, for a player who hears with one ear,
	 * or Auto, mono on a handheld's one speaker. On every device and with
	 * every output: the speaker makes plain mono of a side (src/audioout.h),
	 * and the row still says what was chosen - and, on Auto, what it makes. */
	aout_channels_label(u->audio_channels, u->audio_mix, b->ch, sizeof b->ch);
	full[PM_CHANNELS]     = (menu_row){ T(S_SYSMENU_AUDIO_CHANNELS), b->ch, true };
	/* The launcher's own sounds: which of the three voices, or Off. Live on
	 * every device and with every output; on a Bluetooth headset the sounds
	 * are not played at all (src/uisound.h), and the row still says which
	 * voice comes back with the speaker. */
	full[PM_SOUNDS]       = (menu_row){ T(S_SYSMENU_UI_SOUNDS),
	                                   u->ui_sounds ? u->ui_sounds
	                                                : uisound_voice_name(DS_VOICE_DEFAULT),
	                                   true };
	/* Menu music, which opens its own screen: on or off, how loud, which
	 * track (src/bgm_menu.h). */
	full[PM_MUSIC]        = (menu_row){ T(S_SYSMENU_MENU_MUSIC),
	                                   u->menu_music ? u->menu_music : T(S_MENU_OFF_TITLE), true };
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
		full[PM_XFER] = (menu_row){ XFER_NAME_WIFI, T(S_SYSMENU_HOTSPOT), true };
	/* On behind the shelf: the row opens it again, and says what it does */
	if (u->xfer)
		full[PM_XFER] = (menu_row){ u->xfer_cable ? XFER_NAME_CABLE : XFER_NAME_WIFI, u->xfer, true };
	full[PM_STATS]        = (menu_row){ T(S_SYSMENU_PLAY_TIME),   NULL, true  };
	/* A game from any system, picked and started (src/randgame.h) */
	full[PM_RANDOM]       = (menu_row){ T(S_SYSMENU_RANDOM_GAME), NULL, true  };
	/* On every device: the Brick takes its time from Wi-Fi, but the time zone
	 * is chosen here on both. */
	full[PM_CLOCK]        = (menu_row){ T(S_SYSMENU_DATE_TIME), u->clock, true  };
	/* How the time is written, on the shelf and in the row above. On every
	 * device, like Date & Time. */
	full[PM_HOURS]        = (menu_row){ T(S_SYSMENU_CLOCK),
	                                   u->clock_24h ? T(S_SYSMENU_CLOCK_24H) : T(S_SYSMENU_CLOCK_12H), true };
	/* The language that is on, in its own words: a player who chose one they
	 * cannot read finds their own name here (src/i18n.h) */
	full[PM_LANGUAGE]     = (menu_row){ T(S_MENU_LANGUAGE), lang_name(i18n_lang()), true };
	full[PM_SLEEP]        = (menu_row){ T(S_SYSMENU_AUTO_OFF), b->c, true  };
	/* Off, 80%, 85%, 90%, stepped round like Side Lights */
	if (u->charge_limit > 0) snprintf(b->e, sizeof b->e, "%d%%", u->charge_limit);
	full[PM_CHARGE]       = (menu_row){ T(S_SYSMENU_CHARGE_LIMIT),
	                                   u->charge_limit > 0 ? b->e : T(S_MENU_OFF_TITLE), true  };
	/* How many parts of what the side lights show are on; A opens the page
	 * of switches (sys_menu_lights_rows). L2+R2 shows the level in every
	 * setting. */
	full[PM_LIGHTS]       = (menu_row){ T(S_SYSMENU_SIDE_LIGHTS),
	                                   u->lights ? u->lights : T(S_LIGHTS_ALL_ON), true };
	full[PM_HOME] = (menu_row){ T(S_SYSMENU_HOME_SCREEN), u->home ? u->home : T(S_HOME_SPOTLIGHT), true };
	/* The systems' order on the home, A to Z or by release date, stepped
	 * round like Home Screen (docs/design/mock/controls-lab.html) */
	full[PM_ORDER] = (menu_row){ T(S_SYSMENU_SYSTEM_ORDER), u->order ? u->order : sys_menu_order_name(false), true };
	/* The status corner's battery: the mark alone, or its charge written
	 * beside it (2026-11-05). On every device: both have a battery. */
	full[PM_BATTERY] = (menu_row){ T(S_SYSMENU_BATTERY_ICON), u->battery ? u->battery : sys_menu_battery_name(0), true };
	/* Appearance applies on every device. */
	full[PM_APPEARANCE]   = (menu_row){ T(S_SYSMENU_APPEARANCE),
	                                   u->appearance ? u->appearance : T(S_THEME_LIGHT), true };
	/* Not "Sleep". The device has no suspend and is not getting one. This
	 * powers off, and resume-into-game brings you back where you were, which
	 * is what sleep would have been for. */
	sys_menu_auto_off_label(u->auto_off, b->c, sizeof b->c);
	/* Box Art, its sources and Video Snaps, behind one row (DESIGN.md
	 * 11.19). Live with no network too: its fetch says it needs Wi-Fi, and
	 * the rest of it does not. */
	full[PM_SCRAPER]      = (menu_row){ T(S_SYSMENU_SCRAPER), u->scraper, true };
	full[PM_ACHIEVEMENTS] = (menu_row){ T(S_SYSMENU_CHEEVOS),
	                                   u->ra_in ? u->ra_name : T(S_SYSMENU_SIGN_IN),
	                                   true };
	/* What every button does, per screen. Needs nothing of the device, which
	 * is the point: it is the page you reach when the thing you have forgotten
	 * is which button opens Muse. See src/controls.h. */
	full[PM_CONTROLS]     = (menu_row){ T(S_SYSMENU_CONTROLS), NULL, true  };
	/* The first-launch tutorial again, as often as anyone likes. Needs
	 * nothing either: it is drawn from shapes, not from the library. */
	full[PM_TOUR]         = (menu_row){ T(S_SYSMENU_TUTORIAL), NULL, true  };
	full[PM_DEVELOPER]    = (menu_row){ T(S_SYSMENU_DEVELOPER),
	                                   u->developer ? T(S_MENU_ON) : T(S_MENU_OFF), true };
	/* Ask or Automatic, stepped round; Automatic says its one risk once,
	 * as it is chosen (main.c) */
	full[PM_USB]          = (menu_row){ T(S_SYSMENU_USB_ACCESSORIES),
	                                   u->usb_auto ? T(S_SYSMENU_USB_AUTO) : T(S_SYSMENU_USB_ASK), true };
	/* A plug waiting: the question again for it */
	full[PM_USB_WAIT]     = (menu_row){ T(S_SYSMENU_USB_DEVICE), T(S_SYSMENU_USB_CHOOSE), true };
	/* How the card is used (src/storage.h): its value the room left */
	full[PM_STORAGE]      = (menu_row){ T(S_STORAGE_TITLE), u->storage, true };
	full[PM_UPDATE]       = (menu_row){ T(S_SYSMENU_UPDATE), u->update, true };
	full[PM_ABOUT]        = (menu_row){ T(S_SYSMENU_ABOUT), NULL, true  };
	/* Any game by its name, typed (src/search.h) */
	full[PM_SEARCH]       = (menu_row){ T(S_SYSMENU_SEARCH), NULL, true  };
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
