/* SPDX-License-Identifier: GPL-3.0-only */
#include "devmode_screen.h"

#include "aout_route.h"
#include "app.h"
#include "devmode.h"
#include "keyhints.h"
#include "mdns.h"
#include "panel.h"
#include "sshlink.h"
#include "sys_menu.h"
#include "taskhold.h"
#include "uisound.h"
#include "usblink.h"
#include "zyperdraw.h"

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

/* ---- Developer Mode's SSH, on a release image (devmode.h) -----------------
 *
 * While Developer Mode is on and a computer is on the cable, SSH runs on the
 * cable's address (sshlink.h), with no question and no page open: the link is
 * kept by usblink (usblink_release_dev), and this starts SSH once per plug,
 * from every screen's loop (tick_theme) and a game's (on_game_tick), so a
 * game does not end it. Two ways in, both the player's own: the keys on the
 * card (the Developer folder, devmode_import_dir) and a password made for
 * this plug, which only the Developer page shows and nothing keeps. It ends
 * when the cable comes out, when Developer Mode is turned off, when File
 * Transfer opens (which runs its own SSH with its PIN, and this one starts
 * again after it), and with the launcher: a launcher that restarts (a
 * deploy) ends the sessions and starts SSH again at once, so a key logs in
 * again a few seconds later. Five wrong passwords lock it until the next
 * plug.
 *
 * With SSH over Wi-Fi on (devmode_wifi), a radio brings the other SSH
 * instead: every address, keys only, no password made at all
 * (sshlink_start_net), for as long as the radio is there, with
 * zyperos.local on the radio's network. sshlink_want decides between them
 * at every look, five a second; a change of answer ends the one running
 * (and its sessions) and starts the other. Wi-Fi File Transfer leaves it
 * running: it runs no SSH of its own off the cable. A signed-in session holds
 * Auto Off's clock, as a browser on File Transfer does (TASK_SSH), so a
 * deploy over Wi-Fi on the battery is not cut; sleep is as it was. */
dv_state g_dv;

static void dev_ssh_forget(void)
{
	memset(g_dv.pw, 0, sizeof g_dv.pw);
	memset(&g_dv, 0, sizeof g_dv);
}

/* Ours ended, or ends now (`stop`: it still runs and is ours to stop).
 * What the plug spent stays spent; the name goes unless File Transfer has
 * it now. */
static void dev_ssh_end(bool stop, const char *why)
{
	bool spent = g_dv.spent;
	unsigned at = g_dv.at;

	if (stop) sshlink_stop();
	if (g_dv.run && !g_xs.on) mdns_stop();
	if (why) fprintf(stderr, "devmode: %s\n", why);
	dev_ssh_forget();
	g_dv.spent = spent;
	g_dv.at = at;
	/* One that ended by itself is not started again at once: a dropbear
	 * that dies as it starts would otherwise be forked five times a second */
	if (!stop) g_dv.retry = plat_now_ms() ? plat_now_ms() : 1;
}

/* The card's Developer folder and the keys the player put in it, joined to
 * the ones ssh-copy-id saves: how many were read */
static int dev_keys_read(void)
{
	char dir[CFG_STR * 2], keydir[CFG_STR * 2];

	snprintf(dir, sizeof dir, "%s/%s", P_CARD, DEVMODE_KEYS_DIR);
	mkdir(dir, 0755);                        /* so the card shows where they go */
	snprintf(keydir, sizeof keydir, "%s/ssh", P_USERDATA);
	return devmode_import_dir(dir, keydir);
}

/* Ours, its log read and somebody signed in holding Auto Off */
static void dev_ssh_poll(unsigned now)
{
	sshlink_poll();
	if (sshlink_sessions() > 0) task_note(TASK_SSH, now);
}

/* A development image's name over Wi-Fi (sshlink_dev_image_name): its own
 * dropbear already answers there with the image's key; the name and the
 * address in the log are what this adds. Followed every two seconds, the
 * address too (a new one starts the name again there); File Transfer's
 * name, while it has one, left to it. Started again a minute after a name
 * that was given up (another host claiming every one it tried), not at
 * every look. */
static struct {
	bool     run;
	char     ip[64];
	unsigned at, started;
} g_dimg;

static void dev_image_name(unsigned now)
{
	char ifn[16], wip[64];

	if (g_dimg.run) mdns_poll(now);
	if (g_dimg.at && now - g_dimg.at < 2000u) return;
	g_dimg.at = now ? now : 1;
	wip[0] = '\0';
	if (!g_xs.on) sshlink_radio(ifn, sizeof ifn, wip, sizeof wip);
	if (!sshlink_dev_image_name(true, g_xs.on, wip)) {
		/* File Transfer's now, or no address: ours stops, and File
		 * Transfer's stays its own */
		if (g_dimg.run && !g_xs.on) mdns_stop();
		if (g_dimg.ip[0] && !g_xs.on) fprintf(stderr, "devmode: development image: Wi-Fi's address gone, no zyperos.local\n");
		memset(&g_dimg, 0, sizeof g_dimg);
		g_dimg.at = now ? now : 1;
		return;
	}
	if (g_dimg.run && !strcmp(wip, g_dimg.ip) &&
	    (mdns_name() || now - g_dimg.started < 60000u)) return;
	if (strcmp(wip, g_dimg.ip))
		fprintf(stderr, "devmode: development image: SSH over Wi-Fi at %s, keys only (the image's dropbear and key), zyperos.local on %s\n",
		        wip, ifn);
	snprintf(g_dimg.ip, sizeof g_dimg.ip, "%s", wip);
	g_dimg.run = mdns_start(ifn, wip, true);
	g_dimg.started = now ? now : 1;
}

void dev_ssh_tick(void)
{
	unsigned now = plat_now_ms();
	char keydir[CFG_STR * 2], ifn[16], wip[64];
	sshlink_facts f;
	sshlink_mode want;

	/* The in-game menu over a port is a second zyperos.elf for a moment:
	 * Developer SSH is portkeys' then (tools/pkssh.c), and a start here
	 * would end its sessions, as this menu's exit would end its own */
	if (sshlink_owner_of(false, g_port_menu) != SSHLINK_BY_LAUNCHER) return;
	if (usblink_dev_image()) dev_image_name(now);
	if (usblink_dev_image() || !devmode_unlocked(db_dev())) {
		/* Off. File Transfer over the cable may have SSH of its own with
		 * its PIN, which took ours over: left to it. */
		if (g_dv.run || g_dv.spent) {
			if (g_dv.run && !(g_xs.on && g_xs.cable)) sshlink_stop();
			if (g_dv.run && !g_xs.on) mdns_stop();
			dev_ssh_forget();
		}
		return;
	}
	if (g_xs.on) {
		/* File Transfer is open. Over the cable it started SSH of its own
		 * with its PIN, which took ours over (sshlink_start ends the one
		 * running) and which it polls and locks: left to it. Over Wi-Fi
		 * (or the hotspot) it runs no SSH and has the name: ours over
		 * Wi-Fi goes on, polled here (keys only, nothing to lock); the
		 * cable's must not go on unpolled - a lockout needs the poll, and
		 * a radio ends it - so it ends here. It starts again once File
		 * Transfer closes. */
		/* ...and only while it is still wanted: SSH over Wi-Fi turned off
		 * on the Developer page with File Transfer behind the shelf ends it
		 * now, not when File Transfer closes */
		if (g_dv.run && (g_xs.cable || g_dv.mode != SSHLINK_NET ||
		                 sshlink_running() != SSHLINK_NET || !devmode_wifi(db_dev())))
			dev_ssh_end(!g_xs.cable && sshlink_running() != SSHLINK_OFF, NULL);
		/* A fresh password once it closes, as a fresh plug has, at once */
		g_dv.spent = false;
		g_dv.retry = 0;
		if (g_dv.run) dev_ssh_poll(now);
		return;
	}
	if (g_dv.run) {
		dev_ssh_poll(now);
		mdns_poll(now);
	}
	/* The port and the radio are read five times a second at most */
	if (g_dv.at && now - g_dv.at < 200u) return;
	g_dv.at = now ? now : 1;
	f.cable = plat_usb_peer() == USB_PEER_COMPUTER;
	f.over_wifi = devmode_wifi(db_dev());
	/* The address only when it is wanted: no getifaddrs with the choice off */
	wip[0] = '\0';
	f.radio = g_dv.radio = sshlink_radio(ifn, sizeof ifn, wip, f.over_wifi ? sizeof wip : 0);
	f.cable_spent = false;                   /* sshlink_step's to say */
	{
		sshlink_plan p = { g_dv.run ? (sshlink_mode)g_dv.mode : SSHLINK_OFF, g_dv.spent };

		want = sshlink_step(&p, &f, sshlink_running());
		g_dv.spent = p.spent;
	}
	if (g_dv.run && (want != (sshlink_mode)g_dv.mode || sshlink_running() != (sshlink_mode)g_dv.mode)) {
		bool still = sshlink_running() == (sshlink_mode)g_dv.mode;

		dev_ssh_end(still, !still ? "SSH ended by itself" :
		            want == SSHLINK_NET ? "a radio, SSH over Wi-Fi next" :
		            f.radio ? "a radio, so the cable's SSH stopped" :
		            g_dv.mode == SSHLINK_NET ? (f.over_wifi ? "the radio is gone, SSH over Wi-Fi stopped"
		                                                    : "SSH over Wi-Fi turned off")
		                                     : "the cable is out, SSH stopped");
	}
	if (g_dv.run && g_dv.mode == SSHLINK_NET) {
		/* The address to type, and the name on the radio's network with
		 * it, following the network: a new address starts the name again
		 * there (mdns_start goes on as it is when nothing changed), and
		 * no address takes it away. Every two seconds. */
		if (!g_dv.addr_at || now - g_dv.addr_at >= 2000u || strcmp(wip, g_dv.ip)) {
			g_dv.addr_at = now ? now : 1;
			if (wip[0] && strcmp(wip, g_dv.ip)) fprintf(stderr, "devmode: SSH over Wi-Fi at %s\n", wip);
			snprintf(g_dv.ip, sizeof g_dv.ip, "%s", wip);
			snprintf(g_dv.ifn, sizeof g_dv.ifn, "%s", ifn);
			if (wip[0]) mdns_start(ifn, wip, true);
			else mdns_stop();
		}
		return;
	}
	if (g_dv.run || want == SSHLINK_OFF) return;
	if (g_dv.retry && now - g_dv.retry < 15000u) return;
	g_dv.nkeys = dev_keys_read();
	snprintf(keydir, sizeof keydir, "%s/ssh", P_USERDATA);
	if (want == SSHLINK_CABLE) {
		if (!plat_cable_link(g_dv.ip, sizeof g_dv.ip) || !devmode_password(g_dv.pw, sizeof g_dv.pw)) {
			g_dv.retry = now ? now : 1;
			memset(g_dv.pw, 0, sizeof g_dv.pw);
			return;
		}
		g_dv.up = sshlink_start(g_dv.ip, g_dv.pw, keydir);
	} else {
		/* Keys only: no password is made, and the page says so */
		memset(g_dv.pw, 0, sizeof g_dv.pw);
		snprintf(g_dv.ip, sizeof g_dv.ip, "%s", wip);
		snprintf(g_dv.ifn, sizeof g_dv.ifn, "%s", ifn);
		g_dv.up = sshlink_start_net(keydir);
	}
	fprintf(stderr, "devmode: SSH %s%s%s%s, %d key%s from the card\n",
	        g_dv.up ? "on" : "did not start", want == SSHLINK_NET ? " over Wi-Fi (keys only)" : "",
	        want == SSHLINK_NET && wip[0] ? " at " : "", want == SSHLINK_NET ? wip : "",
	        g_dv.nkeys > 0 ? g_dv.nkeys : 0, g_dv.nkeys == 1 ? "" : "s");
	if (g_dv.up) {
		g_dv.run = true;
		g_dv.mode = want;
		g_dv.addr_at = now ? now : 1;
		/* ssh root@zyperos.local: on the cable as File Transfer's cable
		 * has it, the name ours at once; on Wi-Fi probed for first */
		if (want == SSHLINK_CABLE) mdns_start("usb0", g_dv.ip, false);
		else if (wip[0]) mdns_start(ifn, wip, true);
	} else {
		/* Wi-Fi up, or no key yet, say: tried again in a while */
		g_dv.retry = now ? now : 1;
		memset(g_dv.pw, 0, sizeof g_dv.pw);
	}
}

/* ---- MENU > Developer Mode's page ---------------------------------------
 *
 * A menu panel (DESIGN.md 11.3): two switches, a rule, then what to type
 * and what is known, as full-width lines, and the keys under them. The
 * cursor rests on SSH over Wi-Fi, with Developer Mode under it, because
 * turning Developer Mode off is not one press to undo (the hold on About),
 * and the resting cursor must not be one A away from it. Turned off, the
 * page closes back to MENU. My pick, 2026-11-15. */

/* The page's rows, from what it is told, so the page and its shot (the
 * overflow check draws it in every language) are one list. `lines` holds
 * the text the full-width lines point into. */
int dev_rows(menu_row rows[DEV_ROWS], char lines[DEV_LINES][DEV_LINE], bool over_wifi, bool wifi,
             bool up, const char *ip, const char *name, const char *pw, int nkeys, const char *ssh)
{
	int n = 0, k = 0;

	rows[n++] = (menu_row){ T(S_DEVMODE_WIFI), over_wifi ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE), true };
	rows[n++] = (menu_row){ T(S_SYSMENU_DEVELOPER), T(S_MENU_ON_TITLE), true };
	rows[n++] = MENU_RULE;
	/* What to type: the address first and, over Wi-Fi, the name under it,
	 * as Wi-Fi File Transfer puts the address first (mdns.h); on the cable
	 * the password made for this plug, which Wi-Fi never takes */
	if (up && ip && ip[0]) {
		snprintf(lines[k], DEV_LINE, "ssh root@%s", ip);
		rows[n++] = MENU_BODY(lines[k++]);
		if (wifi && name && name[0]) snprintf(lines[k], DEV_LINE, "ssh root@%s", name);
		else if (wifi) lines[k][0] = '\0';
		else snprintf(lines[k], DEV_LINE, T(S_DEVMODE_PASSWORD_IS), pw);
		if (lines[k][0]) rows[n++] = MENU_BODY(lines[k++]);
	}
	/* The keys SSH takes, the Developer folder's and ssh-copy-id's alike,
	 * a line of its own (with SSH's status beside it, it ran past the panel
	 * in English already); with none, where one goes */
	if (nkeys > 0) snprintf(lines[k], DEV_LINE, TN(S_DEVMODE_KEYS_ON_CARD, nkeys), nkeys);
	else snprintf(lines[k], DEV_LINE, "%s", T(S_DEVMODE_KEYS_IN_FOLDER_NONE));
	rows[n++] = MENU_BODY(lines[k++]);
	/* And SSH's own status only while nothing listens, where it is the
	 * why ("not with Wi-Fi", "needs a key", "locked"): the panel holds three
	 * lines under the rule, and while it listens the commands say more */
	if (!(up && ip && ip[0])) {
		/* drawn inside the line, so said whole to the host's check */
		ui_fit_drawn(ssh);
		snprintf(lines[k], DEV_LINE, "SSH %s", ssh);
		rows[n++] = MENU_BODY(lines[k++]);
	}
	rows[n++] = MENU_NOTE(keyhints_legend((key_hint[]){ { "A", T(S_KEYHINT_CHANGE) }, { "B", T(S_KEYHINT_CLOSE) } }, 2));
	return n;
}

/* SSH over Wi-Fi's message, as it is turned on: only a key signs in over
 * Wi-Fi, and how the first key gets on; up to four balanced lines, as
 * Automatic's, and the one key that closes it under them, B as a message
 * is dismissed (DESIGN.md 11.8) and as the launcher's one-button legends
 * say it (b_legend) */
int dev_wifi_lines(app *a, char lines[4][LB_LINE], menu_row rows[5])
{
	int w = menu_std_width(a), i;
	int n = ui_balance_lines(ui_font(UI_F_MENU), T(S_DEVMODE_WIFI_SAID), w, 4, lines);

	ui_fit_drawn(T(S_DEVMODE_WIFI_SAID));
	for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
	rows[n++] = MENU_NOTE(b_legend(S_KEYHINT_CLOSE));
	return n;
}

static void dev_wifi_message(app *a)
{
	char lines[4][LB_LINE];
	menu_row rows[5];
	int n = dev_wifi_lines(a, lines, rows);

	timed_message(a, T(S_DEVMODE_WIFI), rows, n, menu_std_width(a), MENU_ZYPEROS);
}

typedef struct {
	int           nkeys;                 /* the keys SSH takes, read as the page opens */
	char          lines[DEV_LINES][DEV_LINE];
	unsigned char sound[DEV_ROWS];
} dev_page;

static int dev_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	dev_page *pg = ctx;
	int n;

	(void)max;
	*heading = T(S_XFER_DEV_HEADING);
	n = dev_rows(rows, pg->lines, devmode_wifi(db_dev()), g_dv.mode == SSHLINK_NET, g_dv.up,
	             g_dv.ip, mdns_ready() ? mdns_name() : NULL, g_dv.pw, pg->nkeys, sshlink_status());
	memset(pg->sound, 0, sizeof pg->sound);
	pg->sound[DEV_ROW_WIFI] = pg->sound[DEV_ROW_MODE] = MENU_SND_RING;
	return n;
}

/* Both rows are switches: Left, Right and A turn them over (DESIGN.md
 * 11.3). SSH over Wi-Fi says once, as it comes on, that only a key signs
 * in there; Developer Mode off ends SSH and closes the page. */
static menu_result dev_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	if (key != IN_LEFT && key != IN_RIGHT && key != IN_ACCEPT) return MENU_STAY;
	if (sel == DEV_ROW_WIFI) {
		bool on = !devmode_wifi(db_dev());

		devmode_set_wifi(db_dev(), on);
		fprintf(stderr, "devmode: SSH over Wi-Fi %s\n", on ? "on" : "off");
		g_dv.at = 0;                             /* followed at once */
		dev_ssh_tick();
		if (on) dev_wifi_message(a);
		return MENU_CHANGED;
	}
	if (sel == DEV_ROW_MODE) {
		devmode_set(db_dev(), false);
		usblink_release_dev(false);
		fprintf(stderr, "devmode: off\n");
		dev_ssh_tick();                          /* SSH ends now */
		return MENU_DONE;
	}
	return MENU_STAY;
}

/* MENU's Developer Mode row opens this page. B closes it and leaves
 * everything running. */
void dev_screen(app *a)
{
	static dev_page pg;

	char keydir[CFG_STR * 2];

	memset(&pg, 0, sizeof pg);
	if (!g_dv.up) g_dv.nkeys = dev_keys_read();
	snprintf(keydir, sizeof keydir, "%s/ssh", P_USERDATA);
	pg.nkeys = sshlink_keys(keydir);
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = pg.sound },
	         dev_build, dev_key, &pg);
}
