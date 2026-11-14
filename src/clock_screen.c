/* SPDX-License-Identifier: GPL-3.0-only */
#include "clock_screen.h"

#include "app.h"
#include "autozone.h"
#include "keyhints.h"
#include "net.h"
#include "panel.h"
#include "theme.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* ---- Auto (src/autozone.h) ------------------------------------------------
 *
 * On for every card from 1.0.5 (src/db.c's default, and autozone_migrate
 * once at the start for a card from before), off once a zone is picked on
 * the row. Each time the
 * handheld gets internet, Wi-Fi or a phone's tethering (net_internet), the
 * zone of where it is is asked of AUTOZONE_SERVICE on a thread of its own,
 * and set as the row sets one: the library database, TZ for this process and
 * the next game, and the boot screen's words. Nothing that comes back but a
 * zone this system has is set; no answer, or one it cannot read, keeps the
 * zone there is. */

/* Auto on, as the library database says: read once, then kept as the row
 * changes it */
static int g_zone_auto = -1;

static bool zone_auto_on(void)
{
	if (g_zone_auto < 0) g_zone_auto = db_get_int(db_lib(), AUTOZONE_KEY, 0) == 1;
	return g_zone_auto == 1;
}

static void zone_auto_set(bool on)
{
	g_zone_auto = on;
	db_set_str(db_lib(), AUTOZONE_KEY, on ? "1" : "0");
	/* Chosen now, it has found nothing yet: the row says Auto alone until
	 * its first answer */
	if (on) db_set_str(db_lib(), AUTOZONE_FOUND_KEY, "");
	fprintf(stderr, "clock: auto zone %s\n", on ? "on" : "off");
}

/* The one ask, on its own thread: net_get_hdr_answer waits for curl, up to
 * AUTOZONE_TIMEOUT_S, which no screen's loop may. Read by the main thread
 * once `done` says it is there. */
static struct {
	SDL_atomic_t done;
	char         body[1024];
	long         len;
	int          http;
} g_az;

static int zone_auto_thread(void *arg)
{
	(void)arg;
	g_az.len = net_get_hdr_answer(AUTOZONE_URL, NULL, g_az.body, sizeof g_az.body,
	                              AUTOZONE_TIMEOUT_S, &g_az.http);
	SDL_AtomicSet(&g_az.done, 1);
	return 0;
}

/* What came back, on the main thread, Auto still on: the zone set when it
 * is one this system has. True when it was one; *changed when it was not the
 * zone in use. */
static bool zone_auto_take(bool *changed)
{
	autozone_answer ans;
	char zone[64], was[64], off[16] = "";
	bool read = g_az.len > 0 && autozone_parse(g_az.body, (size_t)g_az.len, &ans);

	*changed = false;
	db_get_str(db_lib(), "timezone", was, sizeof was, "America/New_York");
	if (!read || !autozone_pick(&ans, clock_zone_have, zone, sizeof zone)) {
		if (g_az.len <= 0)
			fprintf(stderr, "clock: auto zone: no answer from %s (HTTP %d), %s kept\n",
			        AUTOZONE_SERVICE, g_az.http, was);
		else
			fprintf(stderr, "clock: auto zone: no zone in %s's answer, %s kept\n",
			        AUTOZONE_SERVICE, was);
		return false;
	}
	if (ans.has_offset) {
		char w[12];

		autozone_offset_words(ans.offset_s, w, sizeof w);
		snprintf(off, sizeof off, " (UTC%s)", w);
	}
	if (strcmp(zone, ans.id))
		fprintf(stderr, "clock: auto zone: %s has no zone %s here, so its offset's\n",
		        AUTOZONE_SERVICE, ans.id[0] ? ans.id : "(none)");
	db_set_str(db_lib(), AUTOZONE_FOUND_KEY, zone);
	if (!strcmp(zone, was)) {
		fprintf(stderr, "clock: auto zone %s%s from %s, as it was\n", zone, off, AUTOZONE_SERVICE);
		return true;
	}
	fprintf(stderr, "clock: auto zone %s%s from %s, was %s\n", zone, off, AUTOZONE_SERVICE, was);
	db_set_str(db_lib(), "timezone", zone);
	plat_clock_zone(zone);
	boot_theme_changed();
	*changed = true;
	return true;
}

/* Every two seconds from the screens' loops (tick_theme): an ask started
 * when autozone_due says, and its answer taken when the thread is done. A
 * connection that comes during a game is asked about back on the shelf. */
bool tick_auto_zone(void)
{
	static unsigned last;
	static autozone_clock ac;
	static bool running;
	unsigned now = plat_now_ms();
	long long secs = (long long)time(NULL);
	bool changed = false, on;

	if (shot_st.shot) return false;
	if (last && now - last < 2000u) return false;
	last = now ? now : 1;
	on = zone_auto_on();
	if (running) {
		bool ok = false;

		if (!SDL_AtomicGet(&g_az.done)) return false;
		running = false;
		/* Auto turned off while it asked: its answer is for nobody */
		if (on) ok = zone_auto_take(&changed);
		autozone_result(&ac, ok, now, secs);
	}
	if (autozone_due(&ac, on, on && net_internet(NULL), clock_is_set((time_t)secs), now, secs)) {
		SDL_Thread *th;

		SDL_AtomicSet(&g_az.done, 0);
		g_az.len = -1;
		g_az.http = 0;
		fprintf(stderr, "clock: auto zone: asking %s\n", AUTOZONE_SERVICE);
		th = SDL_CreateThread(zone_auto_thread, "zyperos-zone", NULL);
		if (th) {
			SDL_DetachThread(th);
			running = true;
		} else {
			autozone_result(&ac, false, now, secs);
		}
	}
	return changed;
}

/* What Auto tells and to whom (the service sees the handheld's public
 * address), said as it is chosen, on up to two balanced lines, as USB
 * Accessories' Automatic says its one risk (usb_auto_lines) */
int zone_auto_lines(app *a, char lines[2][LB_LINE], menu_row rows[2])
{
	int w = menu_std_width(a), i;
	int n = ui_balance_lines(ui_font(UI_F_MENU), T(S_DATETIME_AUTO_NOTE), w, 2, lines);

	ui_fit_drawn(T(S_DATETIME_AUTO_NOTE));
	for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
	return n;
}

static void zone_auto_message(app *a)
{
	char lines[2][LB_LINE];
	menu_row rows[2];
	int n = zone_auto_lines(a, lines, rows);

	timed_message(a, T(S_CLOCK_TIME_ZONE), rows, n, menu_std_width(a), MENU_ZYPEROS);
}

/* The page's rows for zone `id` with Auto `autoz`, the zone Auto set last
 * `found`, at `t`: clock_build's, and a shot's (--datetime). What Auto tells
 * the service is said as it is chosen (zone_auto_message), not in a note
 * here: the six rows fill the panel, and a note under them is seen only
 * when the list goes round. */
int clock_page_rows(clock_ui *c, time_t t, const char *id, bool autoz,
                    const char *found, bool wifi, menu_row *rows, int max)
{
	int n;

	clock_zone_value(id, autoz, found, c->zone, sizeof c->zone);
	n = clock_rows(t, g_clock24, c->zone, rows, max, &c->b);
	if (n + 3 <= max) {
		rows[n++] = MENU_RULE;
		rows[n++] = MENU_NOTE(keyhints_legend((key_hint[]){ { KEYHINT_LEFT_RIGHT, T(S_KEYHINT_CHANGE) } }, 1));
		/* Only where it is true: the Brick's ntpd sets the clock again once
		 * Wi-Fi connects, so a time set here lasts until then */
		if (wifi) rows[n++] = MENU_NOTE(T(S_DATETIME_WIFI_SETS));
	}
	return n;
}

/* A choice of the zone made (clock_zone_choose's rule): by the row's left
 * and right, or picked in its list */
static void zone_choose(app *a, int choice)
{
	char id[64];
	clock_zone_change c;

	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	c = clock_zone_choose(id, zone_auto_on(), choice);
	if (c.auto_on != zone_auto_on()) zone_auto_set(c.auto_on);
	if (c.zone) {
		db_set_str(db_lib(), "timezone", c.zone);
		plat_clock_zone(c.zone);
		boot_theme_changed();
	}
	/* The zone stays as it is until Auto's first answer, which it asks for
	 * as soon as there is internet (tick_auto_zone); what that tells the
	 * service is said now */
	if (c.auto_chosen) zone_auto_message(a);
}

/* The list's rows for zone `id`, Auto `autoz` and what it found: the
 * screen's, and a shot's (--datetime) */
int zone_list_rows(zone_list_ui *z, const char *id, bool autoz, const char *found,
                   menu_row *rows, int max)
{
	int n = clock_zone_list(id, autoz, found, rows, max, z->auto_words, sizeof z->auto_words);
	int on = clock_zone_list_on(id, autoz);

	if (on >= 0 && on < n) rows[on].value = MENU_TICK;
	return n;
}

static int zone_list_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	zone_list_ui *z = ctx;
	char id[64], found[64];

	*heading = T(S_CLOCK_TIME_ZONE);
	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	db_get_str(db_lib(), AUTOZONE_FOUND_KEY, found, sizeof found, "");
	return zone_list_rows(z, id, zone_auto_on(), found, rows,
	                      max < CLOCK_ZONE_LIST_MAX ? max : CLOCK_ZONE_LIST_MAX);
}

static menu_result zone_list_key(app *a, void *ctx, in_button key, int sel)
{
	zone_list_ui *z = ctx;

	(void)a;
	if (key != IN_ACCEPT || clock_zone_list_choice(sel) < -1) return MENU_STAY;
	z->pick = sel;
	return MENU_DONE;
}

static void zone_list_screen(app *a)
{
	zone_list_ui z;
	char id[64];

	memset(&z, 0, sizeof z);
	z.pick = -1;
	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = z.sound,
	                           .start_sel = clock_zone_list_row(id, zone_auto_on()) },
	         zone_list_build, zone_list_key, &z);
	/* Set once the list has gone, so Auto's message comes over the page */
	if (z.pick >= 0) zone_choose(a, clock_zone_list_choice(z.pick));
}

static int clock_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	clock_ui *c = ctx;
	char id[64], found[64];

	*heading = T(S_DATETIME_TITLE);
	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	db_get_str(db_lib(), AUTOZONE_FOUND_KEY, found, sizeof found, "");
	return clock_page_rows(c, time(NULL), id, zone_auto_on(), found, plat_has_wifi(),
	                       rows, max);
}

static menu_result clock_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)ctx;
	if (key == IN_ACCEPT && sel == CLOCK_ROW_ZONE) {
		zone_list_screen(a);
	} else if (!d) {
		return MENU_STAY;
	} else if (clock_row_field(sel) >= 0) {
		plat_clock_set(clock_step(time(NULL), (clock_field)clock_row_field(sel), d));
	} else if (sel == CLOCK_ROW_ZONE) {
		char id[64];

		db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
		zone_choose(a, clock_zone_step_auto(id, zone_auto_on(), d));
	}
	/* Either way the same instant is another hour of the day, and Auto is
	 * to follow it now and not at its next look a minute on: a player who
	 * has just set the zone to where they are, to see the shelf in the theme
	 * of that hour, is looking at the shelf. */
	if (theme_refresh()) theme_changed(a);
	return MENU_STAY;
}

void clock_screen(app *a)
{
	clock_ui c;
	menu_style st = {
		/* Fixed, so the panel does not change size as "May" becomes
		 * "September" under a held key */
		.fixed_w = menu_std_width(a),
		.accent  = MENU_ZYPEROS,
	};

	memset(&c, 0, sizeof c);
	menu_run(a, &st, clock_build, clock_key, &c);
}
