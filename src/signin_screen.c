/* SPDX-License-Identifier: GPL-3.0-only */
#include "signin_screen.h"

#include "aout_route.h"
#include "app.h"
#include "design.h"
#include "net.h"
#include "panel.h"
#include "rafetch.h"
#include "sgdb.h"
#include "ss.h"
#include "levels.h"

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

/* Facts about the machine, which is a different thing from settings. The
 * address in particular has nowhere else to live: the Wi-Fi screen names the
 * network, and short of asking the router there is no way to find out what
 * address the device took. */
/* Signing in to RetroAchievements. Two prompts and a request; the account
 * screen is deliberately not a screen, because there is nothing to look at
 * until there is something to say.
 *
 * The password is used to get a token and then wiped. Nothing stores it -
 * ra.cfg holds the token, which is what every later call uses anyway.
 *
 * Says how it went, for Zyper in the top menu: MENU_CHANGED signed in,
 * MENU_FAILED refused or with no network, MENU_STAY when it was left at a
 * prompt. */
menu_result ra_signin_screen(app *a)
{
	char user[RA_USER_MAX] = "", pass[96] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ra_user());

	if (!net_online()) {
		row = (menu_row){ MSG_NEEDS_WIFI, NULL, false };
		timed_message(a, "RetroAchievements", &row, 1, 0, MENU_ZYPEROS);
		return MENU_FAILED;
	}

	kr = keyboard_prompt(a, T(S_CHEEVOS_RA_USER), user, (int)sizeof user);
	if (kr == KB_POWER) { power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !user[0]) return MENU_STAY;

	kr = keyboard_prompt(a, T(S_PANEL_PASSWORD), pass, (int)sizeof pass);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return MENU_STAY; }

	wait_panel(a, "RetroAchievements", T(S_PANEL_SIGNING_IN));
	ok = ra_sign_in(user, pass, err, sizeof err);
	/* Wiped the moment it has been used, the same as the Wi-Fi passphrase.
	 * The token it bought is the thing worth keeping. */
	memset(pass, 0, sizeof pass);

	if (ok) {
		ra_creds_save();
		snprintf(err, sizeof err, T(S_PANEL_SIGNED_IN_AS), ra_user());
	} else if (!err[0]) {
		snprintf(err, sizeof err, "%s", T(S_PANEL_SIGN_IN_FAILED));
	}

	row = (menu_row){ err, NULL, ok };
	timed_message(a, "RetroAchievements", &row, 1, 0, MENU_ZYPEROS);
	return ok ? MENU_CHANGED : MENU_FAILED;
}

/* The ScreenScraper account. The same shape as the screen above, and the
 * differences are the interesting part.
 *
 * There is nothing to trade the password for. RetroAchievements returns a
 * token and the password is dropped here and never stored; ScreenScraper signs
 * every request with the password itself, so what ss_sign_in keeps is a
 * reusable credential and the only "sign-in" available is asking them whether
 * they answer to it. The password is still wiped from THIS screen's stack the
 * moment it has been handed over, because a buffer that outlives its use is
 * the one thing a screen can get wrong on its own.
 *
 * The third state is the one RetroAchievements does not have: a build made
 * without SS_DEVID cannot sign anyone in, and says so rather than taking a
 * password it has no way to use. See src/ss.h. It says how it went as the
 * screen above does, and that third state is a failure too. */
menu_result ss_signin_screen(app *a)
{
	char user[SS_USER_MAX] = "", pass[SS_PASS_MAX] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ss_user());

	if (!ss_have_dev() || !net_online()) {
		row = (menu_row){ !ss_have_dev()
		                  ? T(S_SCRAPER_SS_NO_KEY)
		                  : MSG_NEEDS_WIFI, NULL, false };
		timed_message(a, "ScreenScraper", &row, 1, 0, MENU_ZYPEROS);
		return MENU_FAILED;
	}

	kr = keyboard_prompt(a, T(S_SCRAPER_SS_USER), user, (int)sizeof user);
	if (kr == KB_POWER) { power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !user[0]) return MENU_STAY;

	kr = keyboard_prompt(a, T(S_PANEL_PASSWORD), pass, (int)sizeof pass);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return MENU_STAY; }

	wait_panel(a, "ScreenScraper", T(S_PANEL_SIGNING_IN));
	ok = ss_sign_in(user, pass, err, sizeof err);
	memset(pass, 0, sizeof pass);

	/* ss_sign_in stores the account itself, because it is the half that knows
	 * whether the reply was real. Nothing to save here. */
	if (ok) snprintf(err, sizeof err, T(S_PANEL_SIGNED_IN_AS), ss_user());
	else if (!err[0]) snprintf(err, sizeof err, "%s", T(S_PANEL_SIGN_IN_FAILED));

	row = (menu_row){ err, NULL, ok };
	timed_message(a, "ScreenScraper", &row, 1, 0, MENU_ZYPEROS);
	return ok ? MENU_CHANGED : MENU_FAILED;
}

/* MENU > SteamGridDB: the key for Box Art's last source (src/sgdb.h).
 *
 * A key rather than an account, so a screen rather than two prompts: the key
 * masked to its last four, what is known about it, and a row to take it off.
 * Typing one checks it once against their API; a key they refuse is not kept,
 * and the keyboard comes back with what was typed, since a 32-character key
 * typed on a D-pad is wrong by one letter more often than it is wrong. A key
 * that got no answer is kept, for the next Box Art run to check.
 *
 * The rows are sys_menu_sgdb_rows's, so tools/menu-check.c can hold them. */
const char *sgdb_status(void)
{
	return sgdb_key_state() == SGDB_WORKS ? "works"
	     : sgdb_key_state() == SGDB_REFUSED ? "refused" : "not checked";
}

/* The same, as the Status row says it, in the language that is on:
 * sgdb_status's words are the page's, which compares them */
static const char *sgdb_status_shown(void)
{
	return sgdb_key_state() == SGDB_WORKS ? T(S_SCRAPER_SGDB_WORKS)
	     : sgdb_key_state() == SGDB_REFUSED ? T(S_SCRAPER_SGDB_REFUSED)
	     : T(S_SCRAPER_SGDB_NOT_CHECKED);
}

static int sgdb_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static char masked[32];
	sg_row ids[SG_ROWS];

	(void)ctx;
	(void)max;
	*heading = "SteamGridDB";
	sgdb_key_masked(masked, sizeof masked);
	return sys_menu_sgdb_rows(sgdb_have_key() ? masked : NULL, sgdb_status_shown(),
	                          rows, ids);
}

/* The check, while it runs: the wait panel every frame and the power button
 * read, which a blocking request would not. Still RUNNING when the device
 * was turned off under it. */
static sgdb_try sgdb_wait(app *a)
{
	sgdb_try t;

	while ((t = sgdb_try_poll()) == SGDB_TRY_RUNNING) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER]) { power_off(a); break; }
		levels_keys(&a->in, BOOST_HIDE);
		wait_panel(a, "SteamGridDB", sgdb_try_said());
		ui_frame_rest(16);
	}
	memset(&a->in, 0, sizeof a->in);
	return t;
}

static menu_result sgdb_enter(app *a)
{
	char key[SGDB_KEY_MAX * 2] = "";
	menu_result said = MENU_STAY;

	if (!net_online()) {
		result_message(a, "SteamGridDB", MSG_NEEDS_WIFI);
		return MENU_FAILED;
	}
	for (;;) {
		kb_result kr = keyboard_prompt(a, T(S_SCRAPER_SGDB_API_KEY), key, (int)sizeof key);
		sgdb_try t;

		if (kr == KB_POWER) { memset(key, 0, sizeof key); power_off(a); return MENU_STAY; }
		if (kr != KB_ACCEPT || !key[0]) break;
		t = sgdb_try_begin(key);
		if (t == SGDB_TRY_RUNNING) t = sgdb_wait(a);
		if (t == SGDB_TRY_RUNNING || !a->running || want_quit) break;
		if (!result_message(a, "SteamGridDB", sgdb_try_said())) break;
		if (t == SGDB_TRY_WORKS || t == SGDB_TRY_KEPT) { said = MENU_CHANGED; break; }
		/* Refused, or not a key: the keyboard again, with what was typed */
		said = MENU_FAILED;
	}
	memset(key, 0, sizeof key);
	return said;
}

static menu_result sgdb_key(app *a, void *ctx, in_button key, int sel)
{
	menu_result *said = ctx, r = MENU_STAY;
	menu_row rows[SG_ROWS];
	sg_row ids[SG_ROWS];
	int n;

	if (key != IN_ACCEPT) return MENU_STAY;
	n = sys_menu_sgdb_rows(sgdb_have_key() ? "" : NULL, NULL, rows, ids);
	if (sel < 0 || sel >= n) return MENU_STAY;
	if (ids[sel] == SG_KEY) {
		r = sgdb_enter(a);
	} else if (ids[sel] == SG_REMOVE && confirm_ask(a, CQ_SGDB_UNLINK, T(S_SCRAPER_UNLINK_KEY_ASK))) {
		sgdb_key_forget();
		result_message(a, "SteamGridDB", T(S_SCRAPER_UNLINKED));
		r = MENU_CHANGED;
	}
	if (r == MENU_CHANGED || r == MENU_FAILED) *said = r;
	return r;
}

/* Says how it went, for Zyper in the top menu, as the sign-ins do: the last
 * thing done here that changed something or failed. */
menu_result sgdb_screen(app *a)
{
	menu_result said = MENU_STAY;

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a) },
	         sgdb_build, sgdb_key, &said);
	return said;
}
