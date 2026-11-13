/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2.c: Player 2's start and stop, on fakes. Every process and protocol
 * action p2.c takes is one of p2_ops, so the fakes below write each down as
 * it is asked, and the checks read the order back: what goes to Diatom, the
 * hotspot and the cable, p2d's command line and its end, and the notices as
 * the share's guests change. No SDL, no Diatom, no network. */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/hotspot.h"
#include "../src/p2.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the fakes ---------------------------------------------------------- */

#define LOG_MAX 64
static char     g_log[LOG_MAX][320];
static int      g_nlog;
static unsigned g_clock = 1000;
static int      g_hs = HOTSPOT_STARTING;     /* what hotspot_poll says */
static unsigned g_guests;                    /* the share's bits */
static const char *g_answer = "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2";
static bool     g_resident_ok = true;
static bool     g_hotspot_ok = true;
static bool     g_mdns_ok = true;
static bool     g_spawn_fails;
static bool     g_alive;                     /* p2d is running */
static bool     g_ignores_term;              /* ...and a TERM does not end it */
static unsigned g_kill_at, g_term_at;

static void say(const char *fmt, ...)
{
	va_list ap;

	if (g_nlog >= LOG_MAX) return;
	va_start(ap, fmt);
	vsnprintf(g_log[g_nlog++], sizeof g_log[0], fmt, ap);
	va_end(ap);
}

static bool f_resident(const char *line) { say("resident %s", line); return g_resident_ok; }

static bool f_answer(const char *prefix, char *out, unsigned n, unsigned ms)
{
	say("answer %s", prefix);
	(void)ms;
	if (!g_answer) return false;
	snprintf(out, n, "%s", g_answer);
	return true;
}

static bool f_theme(void)          { say("theme"); return true; }
static bool f_hotspot_start(void)  { say("hotspot_start"); return g_hotspot_ok; }
static int  f_hotspot_poll(void)   { return g_hs; }
static void f_hotspot_stop(void)   { say("hotspot_stop"); }
static void f_cable_up(void)       { say("cable_up"); }
static void f_cable_down(void)     { say("cable_down"); }
static bool f_mdns_start(void)     { say("mdns_start"); return g_mdns_ok; }
static void f_mdns_poll(unsigned now) { (void)now; }
static void f_mdns_stop(void)      { say("mdns_stop"); }

static long f_spawn(char *const argv[])
{
	char line[300] = "spawn";
	int i;

	for (i = 0; argv[i]; i++) {
		strncat(line, " ", sizeof line - strlen(line) - 1);
		strncat(line, argv[i], sizeof line - strlen(line) - 1);
	}
	say("%s", line);
	if (g_spawn_fails) return -1;
	g_alive = true;
	return 4242;
}

static void f_signal(long pid, int sig)
{
	say("signal %ld %s", pid, sig == SIGTERM ? "TERM" : sig == SIGKILL ? "KILL" : "?");
	if (sig == SIGTERM) g_term_at = g_clock;
	if (sig == SIGKILL) g_kill_at = g_clock;
	if (sig == SIGKILL || !g_ignores_term) g_alive = false;
}

static bool f_ended(long pid, int *status)
{
	(void)pid;
	if (status) *status = 0;
	return !g_alive;
}

static unsigned f_now(void)          { return g_clock; }
static void     f_sleep(unsigned ms) { g_clock += ms; }
static unsigned f_guests(void)       { return g_guests; }
static void     f_notice(int player, bool joined) { say("notice %d %s", player, joined ? "joined" : "left"); }
static const char *f_lang(void)      { return "pt-BR"; }

static const p2_ops FAKE = {
	f_resident, f_answer, f_theme,
	f_hotspot_start, f_hotspot_poll, f_hotspot_stop,
	f_cable_up, f_cable_down,
	f_mdns_start, f_mdns_poll, f_mdns_stop,
	f_spawn, f_signal, f_ended,
	f_now, f_sleep, f_guests, f_notice, f_lang,
	"/card/ZyperOS/p2d", "/card/ZyperOS/res/web",
};

/* A fresh start for each case: everything off, the fakes as they begin */
static void fresh(void)
{
	p2_stop();
	g_nlog = 0;
	g_hs = HOTSPOT_STARTING;
	g_guests = 0;
	g_answer = "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2";
	g_resident_ok = g_hotspot_ok = g_mdns_ok = true;
	g_spawn_fails = g_alive = g_ignores_term = false;
	g_kill_at = g_term_at = 0;
}

/* The log from line `from` is exactly `want`, NULL ended */
static bool log_is(int from, const char *const *want)
{
	int i;

	for (i = 0; want[i]; i++)
		if (from + i >= g_nlog || strcmp(g_log[from + i], want[i])) {
			printf("  expected line %d \"%s\", got \"%s\"\n", from + i, want[i],
			       from + i < g_nlog ? g_log[from + i] : "(nothing)");
			return false;
		}
	if (from + i != g_nlog) {
		printf("  expected %d lines, got %d; the next: \"%s\"\n", from + i, g_nlog, g_log[from + i]);
		return false;
	}
	return true;
}

/* The ticks of `ms` milliseconds, 100 apart, as a game's are */
static void ticks(unsigned ms)
{
	unsigned t;

	for (t = 0; t < ms; t += 100) {
		g_clock += 100;
		p2_tick(g_clock);
	}
}

#define SET_ON  "resident SETPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"
#define SET_OFF "resident SETPLAYER2\ton=0"
#define P2D_HOT "spawn /card/ZyperOS/p2d --root /card/ZyperOS/res/web --share /tmp/zyperos-p2" \
                " --addr 10.42.1.1 --port 80 --dns --lang pt-BR --theme /tmp/zyperos-webtheme.css"
#define P2D_USB "spawn /card/ZyperOS/p2d --root /card/ZyperOS/res/web --share /tmp/zyperos-p2" \
                " --addr 10.42.0.1 --port 80 --lang pt-BR --theme /tmp/zyperos-webtheme.css"

/* ---- the checks --------------------------------------------------------- */

/* The system table: the NES's core alone, with its buttons and two players */
static void offered_only_on_nes(void)
{
	const p2_system *s = p2_system_of("fceumm");

	CHECK(p2_offered("fceumm"));
	CHECK(!p2_offered("snes9x2010") && !p2_offered("mgba") && !p2_offered("genesis_plus_gx"));
	CHECK(!p2_offered("") && !p2_offered(NULL) && !p2_offered("fceumm2"));
	CHECK(s && s->buttons == 0x01FDu && s->players == 2);
	CHECK(!p2_system_of("snes9x2010"));
	/* Nothing starts on a core it is not offered on, or with no link */
	fresh();
	CHECK(!p2_start("snes9x2010", P2_HOTSPOT) && p2_state_now() == P2_OFF && g_nlog == 0);
	CHECK(!p2_start("fceumm", P2_NO_LINK) && p2_state_now() == P2_OFF && g_nlog == 0);
}

/* The cable when a computer is on it, the hotspot when only an adapter is */
static void link_choice(void)
{
	CHECK(p2_link_now(true, false) == P2_HOTSPOT);
	CHECK(p2_link_now(false, true) == P2_CABLE);
	CHECK(p2_link_now(true, true) == P2_CABLE);
	CHECK(p2_link_now(false, false) == P2_NO_LINK);
}

/* The same address for every player, by the link it is on */
static void join_urls(void)
{
	fresh();
	CHECK(!strcmp(p2_join_url(), ""));
	CHECK(p2_link_on() == P2_NO_LINK);
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	CHECK(!strcmp(p2_join_url(), "http://10.42.1.1/play/") && p2_link_on() == P2_HOTSPOT);
	p2_stop();
	CHECK(!strcmp(p2_join_url(), ""));
	CHECK(p2_start("fceumm", P2_CABLE));
	CHECK(!strcmp(p2_join_url(), "http://zyperos.local/play/") && p2_link_on() == P2_CABLE);
	p2_stop();
	CHECK(!strcmp(p2_join_url(), "") && p2_players() == 0);
}

/* Diatom's answer, by its pairs */
static void answers(void)
{
	bool on = false;
	unsigned b = 0;
	int n = 0;

	CHECK(p2_answer("PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2", &on, &b, &n));
	CHECK(on && b == 0x01FDu && n == 2);
	CHECK(p2_answer("PLAYER2\tplayers=3\ton=1\tbuttons=0x0FFF", &on, &b, &n));
	CHECK(on && b == 0x0FFFu && n == 3);
	b = 7; n = 7;
	CHECK(p2_answer("PLAYER2\ton=0", &on, &b, &n) && !on && b == 7 && n == 7);
	CHECK(p2_answer("PLAYER2\ton=1", &on, NULL, NULL) && on);
	CHECK(!p2_answer("SETPLAYER2\ton=1", &on, NULL, NULL));
	CHECK(!p2_answer("PLAYER23\ton=1", &on, NULL, NULL));
	CHECK(!p2_answer("PLAYER2", &on, NULL, NULL));
	CHECK(!p2_answer("PLAYER2\tbuttons=0x01FD", &on, NULL, NULL));
	CHECK(!p2_answer(NULL, &on, NULL, NULL));
}

/* On the hotspot: Diatom first, with the row's buttons and players, then the
 * theme, then the hotspot; p2d only once the hotspot is up, with --dns */
static void start_order_hotspot(void)
{
	static const char *const want[] = { SET_ON, "answer PLAYER2\t", "theme", "hotspot_start", NULL };
	static const char *const then[] = { P2D_HOT, NULL };

	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	CHECK(log_is(0, want));
	CHECK(p2_state_now() == P2_WAITING && p2_starting() && p2_players() == 2);
	ticks(500);                               /* hostapd still coming up */
	CHECK(g_nlog == 4 && p2_starting());
	g_hs = HOTSPOT_UP;
	ticks(100);
	CHECK(log_is(4, then));
	CHECK(!p2_starting() && p2_state_now() == P2_WAITING);
	ticks(1000);                              /* and only once */
	CHECK(g_nlog == 5);
	/* A start while on does nothing */
	CHECK(p2_start("fceumm", P2_HOTSPOT) && g_nlog == 5);
}

/* On the cable: Diatom, the theme, the link, zyperos.local and p2d at once,
 * with the cable's address and no DNS */
static void start_order_cable(void)
{
	static const char *const want[] = { SET_ON, "answer PLAYER2\t", "theme", "cable_up",
	                                    "mdns_start", P2D_USB, NULL };

	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	CHECK(log_is(0, want));
	CHECK(!p2_starting() && p2_state_now() == P2_WAITING);
	/* zyperos.local that did not start yet (the link not up) is asked again
	 * a second later, and not every tick */
	fresh();
	g_mdns_ok = false;
	CHECK(p2_start("fceumm", P2_CABLE));
	ticks(500);
	CHECK(g_nlog == 6);
	g_mdns_ok = true;
	ticks(600);
	CHECK(g_nlog == 7 && !strcmp(g_log[6], "mdns_start"));
	ticks(2000);
	CHECK(g_nlog == 7);
}

/* Refused by Diatom (it is no game, or it did not answer): nothing else
 * starts, it says it failed, and its stop has nothing to undo */
static void refused(void)
{
	static const char *const want[] = { SET_ON, "answer PLAYER2\t", NULL };

	fresh();
	g_answer = "PLAYER2\ton=0";
	CHECK(!p2_start("fceumm", P2_HOTSPOT));
	CHECK(log_is(0, want) && p2_state_now() == P2_FAILED);
	p2_stop();
	CHECK(g_nlog == 2 && p2_state_now() == P2_OFF);
	fresh();
	g_answer = NULL;
	CHECK(!p2_start("fceumm", P2_CABLE) && p2_state_now() == P2_FAILED);
	CHECK(log_is(0, want));
	/* The hotspot that will not start: Diatom turned off again at once */
	fresh();
	g_hotspot_ok = false;
	CHECK(!p2_start("fceumm", P2_HOTSPOT) && p2_state_now() == P2_FAILED);
	p2_stop();
	{
		static const char *const w[] = { SET_ON, "answer PLAYER2\t", "theme", "hotspot_start",
		                                 SET_OFF, "hotspot_stop", NULL };
		CHECK(log_is(0, w));
	}
	/* p2d that will not start on the cable */
	fresh();
	g_spawn_fails = true;
	CHECK(!p2_start("fceumm", P2_CABLE) && p2_state_now() == P2_FAILED);
	p2_stop();
	{
		static const char *const w[] = { SET_ON, "answer PLAYER2\t", "theme", "cable_up",
		                                 "mdns_start", P2D_USB, SET_OFF, "mdns_stop",
		                                 "cable_down", NULL };
		CHECK(log_is(0, w));
	}
}

/* However the game ends - Quit, a crash or Diatom gone, the power button,
 * the battery, a new game - the launcher calls p2_stop, and it undoes the
 * start in the reverse order; a second stop does nothing */
static void p2_stop_after_any_end(void)
{
	/* Up on the hotspot with p2d */
	{
		static const char *const want[] = { "signal 4242 TERM", SET_OFF, "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		g_hs = HOTSPOT_UP;
		ticks(100);
		at = g_nlog;
		p2_stop();
		CHECK(log_is(at, want));
		CHECK(p2_state_now() == P2_OFF && p2_link_on() == P2_NO_LINK && !p2_starting());
		at = g_nlog;
		p2_stop();
		CHECK(g_nlog == at);
	}
	/* Ended while the hotspot was still coming up: no p2d to stop */
	{
		static const char *const want[] = { SET_OFF, "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		ticks(300);
		at = g_nlog;
		p2_stop();
		CHECK(log_is(at, want));
	}
	/* p2d ended by itself first (Diatom dropped the share as the game ended):
	 * the tick turns Diatom's side off at once, and the stop the rest */
	{
		static const char *const tick[] = { SET_OFF, NULL };
		static const char *const want[] = { "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		g_hs = HOTSPOT_UP;
		ticks(100);
		at = g_nlog;
		g_alive = false;
		ticks(100);
		CHECK(log_is(at, tick) && p2_state_now() == P2_FAILED);
		at = g_nlog;
		p2_stop();
		CHECK(log_is(at, want) && p2_state_now() == P2_OFF);
	}
	/* p2d that does not go on SIGTERM gets SIGKILL a second later */
	{
		static const char *const want[] = { "signal 4242 TERM", "signal 4242 KILL", SET_OFF,
		                                    "mdns_stop", "cable_down", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_CABLE));
		g_ignores_term = true;
		at = g_nlog;
		p2_stop();
		CHECK(log_is(at, want));
		CHECK(g_kill_at - g_term_at >= 1000 && g_kill_at - g_term_at < 1100);
	}
	/* On the cable */
	{
		static const char *const want[] = { "signal 4242 TERM", SET_OFF, "mdns_stop",
		                                    "cable_down", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_CABLE));
		g_guests = 1;
		ticks(200);
		at = g_nlog;
		p2_stop();
		CHECK(log_is(at, want));
		at = g_nlog;
		p2_stop();
		CHECK(g_nlog == at);
	}
	/* Nothing on: nothing */
	fresh();
	p2_stop();
	CHECK(g_nlog == 0);
}

/* A notice when a guest's bit comes and when it goes, by player; bits past
 * the system's players are no player's */
static void guest_notices(void)
{
	static const char *const joined[] = { "notice 2 joined", NULL };
	static const char *const left[] = { "notice 2 left", NULL };
	int at;

	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	at = g_nlog;
	ticks(300);
	CHECK(g_nlog == at && p2_state_now() == P2_WAITING && p2_guests() == 0);
	g_guests = 1;
	ticks(200);
	CHECK(log_is(at, joined) && p2_state_now() == P2_CONNECTED && p2_guests() == 1);
	at = g_nlog;
	ticks(1000);
	CHECK(g_nlog == at);
	g_guests = 1 | 2;                         /* player 3: not on the NES */
	ticks(200);
	CHECK(g_nlog == at && p2_guests() == 1);
	g_guests = 0;
	ticks(200);
	CHECK(log_is(at, left) && p2_state_now() == P2_WAITING && p2_guests() == 0);
	/* p2d gone with a guest in: the guest left, and it failed */
	g_guests = 1;
	ticks(200);
	at = g_nlog;
	g_alive = false;
	ticks(100);
	{
		static const char *const want[] = { "notice 2 left", SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED && p2_guests() == 0);
	}
	at = g_nlog;
	ticks(1000);                              /* and nothing more from the tick */
	CHECK(g_nlog == at);
	p2_stop();
}

/* The hotspot that failed to come up, or went: P2_FAILED, Diatom off at once,
 * p2d stopped without waiting, and the hotspot left to p2_stop, which
 * blocks while the Wi-Fi comes back */
static void hotspot_failures(void)
{
	int at;

	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	at = g_nlog;
	g_hs = HOTSPOT_FAILED;
	ticks(100);
	{
		static const char *const want[] = { SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED && !p2_starting());
	}
	at = g_nlog;
	p2_stop();
	{
		static const char *const want[] = { "hotspot_stop", NULL };
		CHECK(log_is(at, want));
	}
	/* Up with p2d, then hostapd gone */
	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	g_hs = HOTSPOT_UP;
	ticks(100);
	at = g_nlog;
	g_hs = HOTSPOT_FAILED;
	ticks(100);
	{
		static const char *const want[] = { "signal 4242 TERM", SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED);
	}
	at = g_nlog;
	p2_stop();
	{
		static const char *const want[] = { "hotspot_stop", NULL };
		CHECK(log_is(at, want));
	}
	/* A start after a failure stops what was left first */
	fresh();
	g_hotspot_ok = false;
	CHECK(!p2_start("fceumm", P2_HOTSPOT));
	g_hotspot_ok = true;
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	{
		static const char *const want[] = { "hotspot_stop", SET_ON, "answer PLAYER2\t", "theme",
		                                    "hotspot_start", NULL };
		CHECK(log_is(at, want));
	}
	p2_stop();
}

/* The theme file once more when the theme changes, only while it is on */
static void theme_follows(void)
{
	fresh();
	p2_theme_changed();
	CHECK(g_nlog == 0);
	CHECK(p2_start("fceumm", P2_CABLE));
	g_nlog = 0;
	p2_theme_changed();
	CHECK(g_nlog == 1 && !strcmp(g_log[0], "theme"));
	p2_stop();
	g_nlog = 0;
	p2_theme_changed();
	CHECK(g_nlog == 0);
}

int main(void)
{
	/* p2.c's lines are the launcher's log; the check says only what failed */
	if (!freopen("/dev/null", "w", stderr)) return 1;
	p2_use(&FAKE);
	offered_only_on_nes();
	link_choice();
	join_urls();
	answers();
	start_order_hotspot();
	start_order_cable();
	refused();
	p2_stop_after_any_end();
	guest_notices();
	hotspot_failures();
	theme_follows();
	if (fails) { printf("p2: %d failed\n", fails); return 1; }
	printf("p2: ok\n");
	return 0;
}
