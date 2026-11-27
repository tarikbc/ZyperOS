/* SPDX-License-Identifier: GPL-3.0-only */
/* Every question with two answers, and the keys on its panel (src/confirmq.h) */
#include "confirmq.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NONE S_COUNT
#define CANCEL S_PANEL_CANCEL
#define DEL S_GAMEMENU_DELETE_YES

static const cq_def CQ[CQ_COUNT] = {
	[CQ_WIFI_FORGET] = { "wifi-forget", S_WIFI_FORGET_HEADING, S_WIFI_FORGET_ASK, NONE,
	                     CANCEL, S_WIFI_FORGET, CQ_LOSES },
	[CQ_WIFI_FORGET_JOINED] = { "wifi-forget-joined", S_WIFI_FORGET_HEADING, S_WIFI_FORGET_JOINED, NONE,
	                            CANCEL, S_WIFI_FORGET, CQ_LOSES },
	[CQ_P8_RESTART_DISPLAY] = { "pico8-restart-display", S_PICO8_RESTART_HEADING, S_PICO8_RESTART_ASK,
	                            S_PICO8_RESTART_DISPLAY, CANCEL, S_PICO8_RESTART_YES, CQ_LOSES },
	[CQ_P8_RESTART_BUTTONS] = { "pico8-restart-buttons", S_PICO8_RESTART_HEADING, S_PICO8_RESTART_ASK,
	                            S_PICO8_RESTART_BUTTONS, S_PICO8_RESTART_LATER, S_PICO8_RESTART_YES,
	                            CQ_LOSES | CQ_B_NONE },
	[CQ_P8_RESET] = { "pico8-reset", S_HOTKEYS_RESET_HEADING, S_HOTKEYS_RESET_ASK, S_HOTKEYS_RESET_LOST,
	                  CANCEL, S_GAMEMENU_RESET, CQ_LOSES },
	[CQ_GAME_RESET] = { "game-reset", S_HOTKEYS_RESET_HEADING, S_HOTKEYS_RESET_ASK, S_HOTKEYS_RESET_LOST,
	                    CANCEL, S_GAMEMENU_RESET, CQ_LOSES },
	[CQ_SPLORE_WIFI] = { "splore-wifi", S_PICO8_WIFI_HEADING, S_PICO8_WIFI_BODY, NONE,
	                     S_PICO8_WIFI_ON, S_PICO8_OFFLINE, CQ_B_NONE },
	[CQ_CHEATS_CRASH] = { "cheats-crash", S_CHEATS_CRASH_HEADING, S_CHEATS_CRASH_TITLE, S_CHEATS_CRASH_BODY,
	                      S_CHEATS_CRASH_WITHOUT, S_CHEATS_CRASH_WITH, CQ_B_NONE },
	[CQ_CHEATS_FIRST] = { "cheats-first", S_CHEATS_ASK_HEADING, S_CHEATS_ASK_TITLE, S_CHEATS_ASK_BODY,
	                      CANCEL, S_CHEATS_ASK_YES, 0 },
	[CQ_HOTKEYS_SWAP] = { "hotkeys-swap", S_HOTKEYS_SWAP_HEADING, S_HOTKEYS_SWAP_Q, NONE,
	                      CANCEL, S_HOTKEYS_SWAP, 0 },
	[CQ_HOTKEYS_RESET_GAME] = { "hotkeys-reset-game", S_HOTKEYS_RESET_ALL, S_HOTKEYS_RESET_GAME, NONE,
	                            CANCEL, S_HOTKEYS_RESET_YES, CQ_LOSES },
	[CQ_HOTKEYS_RESET_SYSTEM] = { "hotkeys-reset-system", S_HOTKEYS_RESET_ALL, S_HOTKEYS_RESET_SYSTEM, NONE,
	                              CANCEL, S_HOTKEYS_RESET_YES, CQ_LOSES },
	[CQ_HOTKEYS_RESET_EVERY] = { "hotkeys-reset-every", S_HOTKEYS_RESET_ALL, S_HOTKEYS_RESET_EVERY, NONE,
	                             CANCEL, S_HOTKEYS_RESET_YES, CQ_LOSES },
	[CQ_STORE_UNUSED_ART] = { "storage-unused-art", S_STORAGE_UNUSED_HEADING, S_STORAGE_UNUSED_ASK, NONE,
	                          CANCEL, DEL, CQ_LOSES },
	[CQ_STORE_OLD_STATES] = { "storage-old-states", S_STORAGE_OLD_HEADING, S_STORAGE_OLD_ASK, NONE,
	                          CANCEL, DEL, CQ_LOSES | CQ_COUNTED },
	[CQ_STORE_SYS_SNAPS] = { "storage-system-snaps", S_STORAGE_DELETE_SNAPS, S_DOWNLOADS_DELETE_SYS_VIDEOS_ASK,
	                         S_DOWNLOADS_DELETE_VIDEOS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_STORE_MEDIA_FOLDER] = { "storage-media-folder", S_STORAGE_MEDIA_HEADING_FOLDER, S_STORAGE_MEDIA_ASK_FOLDER,
	                            NONE, CANCEL, DEL, CQ_LOSES },
	[CQ_STORE_MEDIA_FILE] = { "storage-media-file", S_STORAGE_MEDIA_HEADING_FILE, S_STORAGE_MEDIA_ASK_FILE, NONE,
	                          CANCEL, DEL, CQ_LOSES },
	[CQ_STORE_ORPHAN_PORT] = { "storage-orphan-port", S_GAMEMENU_DELETE, S_STORAGE_ORPHAN_ASK, NONE,
	                           CANCEL, DEL, CQ_LOSES },
	[CQ_GAME_DELETE] = { "game-delete", S_GAMEMENU_DELETE, S_GAMEMENU_DELETE_ASK, NONE, CANCEL, DEL, CQ_LOSES },
	[CQ_PORT_DELETE] = { "port-delete", S_GAMEMENU_DELETE, S_GAMEMENU_DELETE_ASK_PORT, NONE, CANCEL, DEL, CQ_LOSES },
	[CQ_ROLLBACK] = { "update-rollback", S_UPDATEPAGE_ROLLBACK, S_UPDATEPAGE_ROLLBACK_ASK, NONE,
	                  CANCEL, S_UPDATEPAGE_ROLLBACK, 0 },
	[CQ_COLLECT_DELETE] = { "collection-delete", S_COLLECT_DELETE, S_COLLECT_DELETE_ASK, NONE,
	                        CANCEL, DEL, CQ_LOSES },
	[CQ_SHOT_DELETE] = { "screenshot-delete", S_SHOTS_DELETE, S_SHOTS_DELETE_ASK, NONE, CANCEL, DEL, CQ_LOSES },
	[CQ_MEDIA_VIDEO_DELETE] = { "media-video-delete", S_MEDIA_DELETE_HEADING, S_MEDIA_DELETE_ASK, NONE,
	                            CANCEL, DEL, CQ_LOSES },
	[CQ_SGDB_UNLINK] = { "sgdb-unlink", S_SCRAPER_UNLINK_KEY_HEADING, S_SCRAPER_UNLINK_KEY_ASK, NONE,
	                     CANCEL, S_SCRAPER_UNLINK, CQ_LOSES },
	[CQ_SS_UNLINK] = { "ss-unlink", S_SCRAPER_UNLINK_ACCOUNT_HEADING, S_SCRAPER_UNLINK_ACCOUNT_ASK, NONE,
	                   CANCEL, S_SCRAPER_UNLINK, CQ_LOSES },
	[CQ_SNAPS_DELETE_ALL] = { "snaps-delete-all", S_STORAGE_DELETE_SNAPS, S_DOWNLOADS_DELETE_VIDEOS_ASK,
	                          S_DOWNLOADS_DELETE_VIDEOS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_BUTTONS_RESET_GAME] = { "reset-buttons-game", S_GAMEMENU_RESET_BUTTONS, S_GAMEMENU_RESET_BUTTONS_GAME,
	                            NONE, CANCEL, S_GAMEMENU_RESET_BUTTONS_YES, CQ_LOSES },
	[CQ_BUTTONS_RESET_ALL] = { "reset-buttons-all", S_GAMEMENU_RESET_BUTTONS, S_GAMEMENU_RESET_BUTTONS_ALL,
	                           NONE, CANCEL, S_GAMEMENU_RESET_BUTTONS_YES, CQ_LOSES },
	[CQ_USB_OFFER] = { "usb-offer", S_XFER_USB_OFFER_HEADING, S_XFER_USB_OFFER, S_XFER_USB_OFFER_SAID,
	                   S_XFER_USB_OFFER_KEEP, S_SYSMENU_USB_AUTO, CQ_B_NONE },
	[CQ_RA_SIGN_OUT] = { "dl-sign-out", S_DOWNLOADS_SIGN_OUT, S_DOWNLOADS_SIGN_OUT_ASK, S_DOWNLOADS_SIGN_OUT_BODY,
	                     CANCEL, S_DOWNLOADS_SIGN_OUT, CQ_LOSES },
	[CQ_DL_DELETE_VIDEOS] = { "dl-delete-videos", S_STORAGE_DELETE_SNAPS, S_DOWNLOADS_DELETE_VIDEOS_ASK,
	                          S_DOWNLOADS_DELETE_VIDEOS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_DL_DELETE_CHEATS] = { "dl-delete-cheats", S_DOWNLOADS_DELETE_CHEATS_HEADING, S_DOWNLOADS_DELETE_CHEATS_ASK,
	                          S_DOWNLOADS_DELETE_CHEATS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_DL_DELETE_SETS] = { "dl-delete-sets", S_DOWNLOADS_DELETE_SETS_HEADING, S_DOWNLOADS_DELETE_SETS_ASK,
	                        S_DOWNLOADS_DELETE_SETS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_DL_SYS_VIDEOS] = { "dl-sys-videos", S_STORAGE_DELETE_SNAPS, S_DOWNLOADS_DELETE_SYS_VIDEOS_ASK,
	                       S_DOWNLOADS_DELETE_VIDEOS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_DL_SYS_SETS] = { "dl-sys-sets", S_DOWNLOADS_DELETE_SETS_HEADING, S_DOWNLOADS_DELETE_SYS_SETS_ASK,
	                     S_DOWNLOADS_DELETE_SETS_BODY, CANCEL, DEL, CQ_LOSES },
	[CQ_DL_SYS_CHEATS] = { "dl-sys-cheats", S_DOWNLOADS_DELETE_CHEATS_HEADING, S_DOWNLOADS_DELETE_SYS_CHEATS_ASK,
	                       S_DOWNLOADS_DELETE_CHEATS_BODY, CANCEL, DEL, CQ_LOSES },
	/* Tarik's, after the lab (2026-11-23): Bluetooth's Forget asks, as Wi-Fi's does */
	[CQ_BT_FORGET] = { "bt-forget", S_BT_FORGET_HEADING, S_BT_FORGET_ASK, NONE, CANCEL, S_BT_FORGET, CQ_LOSES },
};

const cq_def *cq_of(cq_id q)
{
	return q >= 0 && q < CQ_COUNT ? &CQ[q] : &CQ[CQ_GAME_DELETE];
}

cq_id cq_named(const char *name)
{
	int i;

	for (i = 0; name && i < CQ_COUNT; i++)
		if (CQ[i].name && !strcmp(CQ[i].name, name)) return (cq_id)i;
	return CQ_COUNT;
}

/* How many arguments a format takes: its conversions, %% not one */
static int conversions(const char *f)
{
	int n = 0;

	for (; *f; f++) {
		if (*f != '%') continue;
		if (f[1] == '%') { f++; continue; }
		n++;
	}
	return n;
}

/* The two sentences of the last few questions made of two, by the whole
 * they made (cq_parts) */
#define PARTS_KEPT 4
static struct { char whole[1024], one[512], two[512]; } g_parts[PARTS_KEPT];
static int g_parts_at;

static void parts_keep(const char *whole, const char *one, const char *two)
{
	snprintf(g_parts[g_parts_at].whole, sizeof g_parts[0].whole, "%s", whole);
	snprintf(g_parts[g_parts_at].one, sizeof g_parts[0].one, "%s", one);
	snprintf(g_parts[g_parts_at].two, sizeof g_parts[0].two, "%s", two);
	g_parts_at = (g_parts_at + 1) % PARTS_KEPT;
}

bool cq_parts(const char *whole, const char **one, const char **two)
{
	int i;

	for (i = 0; whole && i < PARTS_KEPT; i++)
		if (g_parts[i].whole[0] && !strcmp(g_parts[i].whole, whole)) {
			*one = g_parts[i].one;
			*two = g_parts[i].two;
			return true;
		}
	return false;
}

void cq_text(cq_id q, char *out, size_t n, ...)
{
	const cq_def *d = cq_of(q);
	va_list ap;

	if (!out || !n) return;
	out[0] = '\0';
	va_start(ap, n);
	if (d->text2 == S_COUNT) {
		if (d->flags & CQ_COUNTED) {
			va_list cp;
			int count;

			va_copy(cp, ap);
			count = va_arg(cp, int);
			va_end(cp);
			vsnprintf(out, n, TN(d->text, count), ap);
		} else {
			vsnprintf(out, n, T(d->text), ap);
		}
	} else {
		/* Two sentences, every argument a string: the first's, then the
		 * second's, as many as each one's conversions take */
		const char *s[8] = { "", "", "", "", "", "", "", "" };
		const char *f1 = T(d->text), *f2 = T(d->text2);
		int k1 = conversions(f1), k2 = conversions(f2), i;
		char one[512], two[512];

		if (k1 > 4) k1 = 4;
		if (k2 > 4) k2 = 4;
		for (i = 0; i < k1 + k2; i++) s[i] = va_arg(ap, const char *);
		snprintf(one, sizeof one, f1, s[0], s[1], s[2], s[3]);
		snprintf(two, sizeof two, f2, s[k1], s[k1 + 1], s[k1 + 2], s[k1 + 3]);
		snprintf(out, n, T(S_PANEL_JOIN), one, two);
		parts_keep(out, one, two);
	}
	va_end(ap);
}

cq_state cq_start(bool side)
{
	return (cq_state){ 0, side };
}

cq_step cq_press(const cq_def *d, cq_state s, cq_key k, bool fresh)
{
	cq_step r = { s.sel, false, false, CQ_NONE, -1, false };
	bool along = s.side ? (k == CQK_LEFT || k == CQK_RIGHT) : (k == CQK_UP || k == CQK_DOWN);
	bool across = s.side ? (k == CQK_UP || k == CQK_DOWN) : (k == CQK_LEFT || k == CQK_RIGHT);

	if (along) {
		int dir = (k == CQK_LEFT || k == CQK_UP) ? -1 : +1, to = s.sel + dir;

		/* Lists go round (DESIGN.md 11.3): a fresh press past an end lands
		 * on the other end; a held one stops there */
		if (to < 0 || to > 1) to = fresh ? (to < 0 ? 1 : 0) : s.sel;
		r.sel = to;
		r.moved = to != s.sel;
		r.edge = !r.moved;
		return r;
	}
	/* Across the answers there is nowhere to go: said once, by the edge */
	if (across) {
		r.edge = fresh;
		return r;
	}
	switch (k) {
	case CQK_A:
		r.answer = s.sel + 1;
		r.press = s.sel;
		r.done = true;
		break;
	case CQK_B:
		/* B is the safe answer, and never an answer that stays */
		if (!(d->flags & CQ_B_NONE)) {
			r.answer = CQ_LEFT;
			r.press = 0;
		}
		r.done = true;
		break;
	case CQK_MENU:
	case CQK_POWER:
		r.done = true;
		break;
	default:
		break;
	}
	return r;
}
