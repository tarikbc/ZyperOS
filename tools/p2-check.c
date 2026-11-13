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
static int      g_guests;                    /* the share's bits, -1 for no share */
static const char *g_answer = "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2";
/* Diatom's answers not read yet, oldest first, as its socket holds them: each
 * SETPLAYER2 it is sent puts its answer behind the others. On=1's is
 * g_answer (NULL: a Diatom that answers no SETPLAYER2 at all), on=0's
 * "PLAYER2\ton=0". g_diatom_on is whether Diatom shares the game now. */
#define QUEUE_MAX 8
static const char *g_queue[QUEUE_MAX];
static int      g_queued;
static bool     g_diatom_on;
static bool     g_resident_ok = true;
static bool     g_hotspot_ok = true;
static bool     g_mdns_ok = true;
static bool     g_spawn_fails;
static bool     g_alive;                     /* p2d is running */
static bool     g_ignores_term;              /* ...and a TERM does not end it */
static int      g_exit = 1;                  /* its status when it ends by itself */
static unsigned g_kill_at, g_term_at;

static void say(const char *fmt, ...)
{
	va_list ap;

	if (g_nlog >= LOG_MAX) return;
	va_start(ap, fmt);
	vsnprintf(g_log[g_nlog++], sizeof g_log[0], fmt, ap);
	va_end(ap);
}

static bool f_resident(const char *line)
{
	say("resident %s", line);
	if (!g_resident_ok) return false;
	if (!strncmp(line, "SETPLAYER2\t", 11)) {
		bool on = strstr(line, "\ton=1") != NULL;
		const char *a = !g_answer ? NULL : on ? g_answer : "PLAYER2\ton=0";

		if (g_answer) g_diatom_on = on && strstr(g_answer, "\ton=1") != NULL;
		if (a && g_queued < QUEUE_MAX) g_queue[g_queued++] = a;
	}
	return true;
}

/* The oldest answer waiting. With ms 0 (only what is waiting already) it is
 * written down only when there was one, as the line it took. */
static bool f_answer(const char *prefix, char *out, unsigned n, unsigned ms)
{
	if (ms) say("answer %s", prefix);
	if (!g_queued) return false;
	if (!ms) say("waiting %s", g_queue[0]);
	snprintf(out, n, "%s", g_queue[0]);
	memmove(g_queue, g_queue + 1, (size_t)--g_queued * sizeof g_queue[0]);
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
	g_exit = sig == SIGTERM ? 0 : -1;
}

static bool f_ended(long pid, int *status)
{
	(void)pid;
	if (status) *status = g_exit;
	return !g_alive;
}

static unsigned f_now(void)          { return g_clock; }
static void     f_sleep(unsigned ms) { g_clock += ms; }
static int      f_guests(void)       { return g_guests; }
static void     f_unshare(void)      { say("unshare"); }
static void     f_notice(p2_news what, int player)
{
	if (what == P2_STOPPED) say("notice stopped");
	else say("notice %d %s", player, what == P2_JOINED ? "joined" : "left");
}
static const char *f_lang(void)      { return "pt-BR"; }

static const p2_ops FAKE = {
	f_resident, f_answer, f_theme,
	f_hotspot_start, f_hotspot_poll, f_hotspot_stop,
	f_cable_up, f_cable_down,
	f_mdns_start, f_mdns_poll, f_mdns_stop,
	f_spawn, f_signal, f_ended,
	f_now, f_sleep, f_guests, f_unshare, f_notice, f_lang,
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
	g_queued = 0;
	g_diatom_on = false;
	g_resident_ok = g_hotspot_ok = g_mdns_ok = true;
	g_spawn_fails = g_alive = g_ignores_term = false;
	g_exit = 1;
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

	CHECK(p2_offered("fceumm", true));
	CHECK(!p2_offered("snes9x2010", true) && !p2_offered("mgba", true) &&
	      !p2_offered("genesis_plus_gx", true));
	CHECK(!p2_offered("", true) && !p2_offered(NULL, true) && !p2_offered("fceumm2", true));
	/* ...and on a device that has it: not the Brick, which never ran it */
	CHECK(!p2_offered("fceumm", false));
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

/* Refused by Diatom (it is no game), or not answered: nothing else starts,
 * it says it failed, its link is none (nothing to close), and a Diatom that
 * did not answer in time is told off, in case it takes it late */
static void refused(void)
{
	static const char *const want[] = { SET_ON, "answer PLAYER2\t", NULL };
	static const char *const late[] = { SET_ON, "answer PLAYER2\t", SET_OFF, NULL };

	fresh();
	g_answer = "PLAYER2\ton=0";
	CHECK(!p2_start("fceumm", P2_HOTSPOT));
	CHECK(log_is(0, want) && p2_state_now() == P2_FAILED);
	CHECK(p2_link_on() == P2_NO_LINK && !strcmp(p2_join_url(), ""));
	p2_stop();
	CHECK(g_nlog == 2 && p2_state_now() == P2_OFF);
	/* No answer in time */
	fresh();
	g_answer = NULL;
	CHECK(!p2_start("fceumm", P2_CABLE) && p2_state_now() == P2_FAILED);
	CHECK(log_is(0, late) && p2_link_on() == P2_NO_LINK);
	p2_stop();
	CHECK(g_nlog == 3);
	/* An answer that is no PLAYER2 answer is none either */
	fresh();
	g_answer = "PLAYER2\tbuttons=0x01FD";
	CHECK(!p2_start("fceumm", P2_HOTSPOT));
	CHECK(log_is(0, late));
	/* No Diatom to send it to: nothing else is asked */
	fresh();
	g_resident_ok = false;
	CHECK(!p2_start("fceumm", P2_HOTSPOT) && p2_state_now() == P2_FAILED);
	CHECK(g_nlog == 1 && !strcmp(g_log[0], SET_ON) && p2_link_on() == P2_NO_LINK);
	/* The hotspot that will not start: Diatom turned off again at once, and
	 * the hotspot is still the link to close (hotspot_stop cleans up) */
	fresh();
	g_hotspot_ok = false;
	CHECK(!p2_start("fceumm", P2_HOTSPOT) && p2_state_now() == P2_FAILED);
	CHECK(p2_link_on() == P2_HOTSPOT);
	p2_stop();
	{
		static const char *const w[] = { SET_ON, "answer PLAYER2\t", "theme", "hotspot_start",
		                                 SET_OFF, "hotspot_stop", NULL };
		CHECK(log_is(0, w));
	}
	/* p2d that will not start on the cable: said by the page, no notice */
	fresh();
	g_spawn_fails = true;
	CHECK(!p2_start("fceumm", P2_CABLE) && p2_state_now() == P2_FAILED);
	CHECK(p2_link_on() == P2_CABLE);
	p2_stop();
	{
		static const char *const w[] = { SET_ON, "answer PLAYER2\t", "theme", "cable_up",
		                                 "mdns_start", P2D_USB, SET_OFF, "mdns_stop",
		                                 "cable_down", NULL };
		CHECK(log_is(0, w));
	}
}

/* However the game ends - Quit, a crash or Diatom gone, the power button,
 * the battery, a new game - the launcher says the game is over and calls
 * p2_stop, which removes the share's file first (a Diatom killed before it
 * could leaves it, and p2d tells its guests the game ended by the share
 * gone), then undoes the start in the reverse order; a second stop does
 * nothing. The page's stop, with the game going on, leaves the share to
 * Diatom. */
static void p2_stop_after_any_end(void)
{
	/* Up on the hotspot with p2d */
	{
		static const char *const want[] = { "unshare", "signal 4242 TERM", SET_OFF,
		                                    "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		g_hs = HOTSPOT_UP;
		ticks(100);
		at = g_nlog;
		p2_game_over();
		p2_stop();
		CHECK(log_is(at, want));
		CHECK(p2_state_now() == P2_OFF && p2_link_on() == P2_NO_LINK && !p2_starting());
		at = g_nlog;
		p2_game_over();
		p2_stop();
		CHECK(g_nlog == at);
	}
	/* Stopped from the page, the game going on */
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
	}
	/* Ended while the hotspot was still coming up: no p2d to stop */
	{
		static const char *const want[] = { "unshare", SET_OFF, "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		ticks(300);
		at = g_nlog;
		p2_game_over();
		p2_stop();
		CHECK(log_is(at, want));
	}
	/* p2d ended by itself with 0 first (Diatom dropped the share as the game
	 * ended): the game's end, so the tick says nothing and sends nothing */
	{
		static const char *const want[] = { "unshare", SET_OFF, "hotspot_stop", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_HOTSPOT));
		g_hs = HOTSPOT_UP;
		g_guests = 1;
		ticks(200);
		at = g_nlog;
		g_alive = false;
		g_exit = 0;
		ticks(1000);
		CHECK(g_nlog == at);
		p2_game_over();
		p2_stop();
		CHECK(log_is(at, want) && p2_state_now() == P2_OFF);
	}
	/* p2d that does not go on SIGTERM gets SIGKILL a second later */
	{
		static const char *const want[] = { "unshare", "signal 4242 TERM", "signal 4242 KILL",
		                                    SET_OFF, "mdns_stop", "cable_down", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_CABLE));
		g_ignores_term = true;
		at = g_nlog;
		p2_game_over();
		p2_stop();
		CHECK(log_is(at, want));
		CHECK(g_kill_at - g_term_at >= 1000 && g_kill_at - g_term_at < 1100);
	}
	/* On the cable, a guest in */
	{
		static const char *const want[] = { "unshare", "signal 4242 TERM", SET_OFF, "mdns_stop",
		                                    "cable_down", NULL };
		int at;

		fresh();
		CHECK(p2_start("fceumm", P2_CABLE));
		g_guests = 1;
		ticks(200);
		at = g_nlog;
		p2_game_over();
		p2_stop();
		CHECK(log_is(at, want));
		at = g_nlog;
		p2_stop();
		CHECK(g_nlog == at);
	}
	/* Nothing on: nothing, the share's file included */
	fresh();
	p2_game_over();
	p2_stop();
	CHECK(g_nlog == 0);
}

/* The game's end says nothing: once the launcher says the game is over, the
 * ticks that still come (a wait panel's on the way to the shelf) neither
 * tell a guest's leaving nor touch Diatom; and the share gone during the
 * game (Diatom ending it) is the same */
static void game_end_is_quiet(void)
{
	int at;

	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	g_guests = 1;
	ticks(200);
	CHECK(p2_state_now() == P2_CONNECTED);
	at = g_nlog;
	p2_game_over();
	g_guests = -1;
	g_alive = false;
	ticks(1000);
	CHECK(g_nlog == at && p2_guests() == 0);
	p2_theme_changed();
	CHECK(g_nlog == at);
	/* The share gone before the launcher heard the game end */
	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	g_guests = 1;
	ticks(200);
	at = g_nlog;
	g_guests = -1;
	ticks(1000);
	CHECK(g_nlog == at && p2_guests() == 0);
	p2_stop();
	{
		static const char *const want[] = { "unshare", "signal 4242 TERM", SET_OFF, "mdns_stop",
		                                    "cable_down", NULL };
		CHECK(log_is(at, want));
	}
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
	/* Gone: said after 5 s of it, and the row says connected until then */
	g_guests = 0;
	ticks(200);
	CHECK(g_nlog == at && p2_state_now() == P2_CONNECTED && p2_guests() == 1);
	ticks(4600);
	CHECK(g_nlog == at && p2_guests() == 1);
	ticks(300);
	CHECK(log_is(at, left) && p2_state_now() == P2_WAITING && p2_guests() == 0);
	/* p2d crashed with a guest in: one notice that Player 2 stopped (its
	 * guest with it), Diatom off, and it failed */
	g_guests = 1;
	ticks(200);
	at = g_nlog;
	g_alive = false;
	g_exit = -1;
	ticks(100);
	{
		static const char *const want[] = { "notice stopped", SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED && p2_guests() == 0);
	}
	at = g_nlog;
	ticks(1000);                              /* and nothing more from the tick */
	CHECK(g_nlog == at);
	p2_stop();
}

/* A guest's link that drops and comes back by itself (the page tries again
 * every 2 s): nothing said, the row connected throughout. An iPhone's did
 * right after it joined, and the handheld said "left" and "joined" over a
 * game nobody had left. A guest on the way out when Player 2 stops, or the
 * game ends, is not said to go either. */
static void blip_says_nothing(void)
{
	static const char *const joined[] = { "notice 2 joined", NULL };
	int at;

	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	at = g_nlog;
	g_guests = 1;
	ticks(200);
	CHECK(log_is(at, joined));
	at = g_nlog;
	g_guests = 0;                             /* the link drops */
	ticks(2300);
	CHECK(g_nlog == at && p2_guests() == 1 && p2_state_now() == P2_CONNECTED);
	g_guests = 1;                             /* and the page is back */
	ticks(200);
	CHECK(g_nlog == at && p2_guests() == 1);
	ticks(6000);                              /* and stays: still nothing */
	CHECK(g_nlog == at);
	g_guests = 0;                             /* gone again, then stopped */
	ticks(1000);
	p2_stop();
	{
		int i;
		bool said_left = false;

		for (i = at; i < g_nlog; i++)
			if (!strcmp(g_log[i], "notice 2 left")) said_left = true;
		CHECK(!said_left);
	}
	/* The game ends while a guest is on the way out: nothing said */
	fresh();
	CHECK(p2_start("fceumm", P2_CABLE));
	g_guests = 1;
	ticks(200);
	at = g_nlog;
	g_guests = 0;
	ticks(1000);
	p2_game_over();
	ticks(6000);
	{
		int i;
		bool said_left = false;

		for (i = at; i < g_nlog; i++)
			if (!strcmp(g_log[i], "notice 2 left")) said_left = true;
		CHECK(!said_left);
	}
	p2_stop();
}

/* The hotspot that failed to come up, or went: P2_FAILED, a notice that it
 * stopped, Diatom off at once, p2d stopped without waiting, and the hotspot
 * left to p2_stop, which blocks while the Wi-Fi comes back */
static void hotspot_failures(void)
{
	int at;

	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	at = g_nlog;
	g_hs = HOTSPOT_FAILED;
	ticks(100);
	{
		static const char *const want[] = { "notice stopped", SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED && !p2_starting());
		CHECK(p2_link_on() == P2_HOTSPOT);
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
		static const char *const want[] = { "notice stopped", "signal 4242 TERM", SET_OFF, NULL };
		CHECK(log_is(at, want) && p2_state_now() == P2_FAILED);
	}
	at = g_nlog;
	p2_stop();
	{
		static const char *const want[] = { "hotspot_stop", NULL };
		CHECK(log_is(at, want));
	}
	/* A start after a failure stops what was left first, and reads away
	 * Diatom's answer to the failure's on=0 */
	fresh();
	g_hotspot_ok = false;
	CHECK(!p2_start("fceumm", P2_HOTSPOT));
	g_hotspot_ok = true;
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	{
		static const char *const want[] = { "hotspot_stop", "waiting PLAYER2\ton=0", SET_ON,
		                                    "answer PLAYER2\t", "theme", "hotspot_start", NULL };
		CHECK(log_is(at, want));
	}
	p2_stop();
}

/* A start after a stop or a failure takes Diatom's answer to its own on=1.
 * The stop's or the failure's on=0 was answered while nothing read Diatom's
 * lines (the game paused under the page, or the in-game menu), so that
 * PLAYER2 on=0 waits ahead of the new one: read as this start's answer, the
 * start failed and played the deny sound while Diatom went on sharing every
 * frame with nobody. */
static void restart_reads_its_own_answer(void)
{
	static const char *const again_hot[] = { "waiting PLAYER2\ton=0", SET_ON, "answer PLAYER2\t",
	                                         "theme", "hotspot_start", NULL };
	int at;

	/* The page's A, A again: start, stop, start */
	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	g_hs = HOTSPOT_UP;
	ticks(100);
	p2_stop();
	CHECK(!g_diatom_on && g_queued == 1);
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	CHECK(log_is(at, again_hot));
	CHECK(p2_state_now() == P2_WAITING && g_diatom_on && g_queued == 0);
	p2_stop();
	CHECK(!g_diatom_on);
	/* The hotspot that did not come up (the tick's failure), then A */
	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	g_hs = HOTSPOT_FAILED;
	ticks(100);
	CHECK(p2_state_now() == P2_FAILED && !g_diatom_on && g_queued == 1);
	g_hs = HOTSPOT_STARTING;
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_HOTSPOT));
	{
		static const char *const want[] = { "hotspot_stop", "waiting PLAYER2\ton=0", SET_ON,
		                                    "answer PLAYER2\t", "theme", "hotspot_start", NULL };
		CHECK(log_is(at, want));
	}
	CHECK(p2_state_now() == P2_WAITING && g_diatom_on);
	/* p2d that did not start on the cable, then A */
	fresh();
	g_spawn_fails = true;
	CHECK(!p2_start("fceumm", P2_CABLE) && !g_diatom_on && g_queued == 1);
	g_spawn_fails = false;
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_CABLE));
	{
		static const char *const want[] = { "mdns_stop", "cable_down", "waiting PLAYER2\ton=0", SET_ON,
		                                    "answer PLAYER2\t", "theme", "cable_up", "mdns_start",
		                                    P2D_USB, NULL };
		CHECK(log_is(at, want));
	}
	CHECK(p2_state_now() == P2_WAITING && g_diatom_on);
	/* More than one waiting: a Diatom that answered a start's on=1 only
	 * after its time, and then the on=0 that start sent it. Every one is
	 * read away, and the next start is what Diatom says to it. */
	fresh();
	g_answer = NULL;
	CHECK(!p2_start("fceumm", P2_CABLE));
	g_answer = "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2";
	g_queue[g_queued++] = g_answer;
	g_queue[g_queued++] = "PLAYER2\ton=0";
	at = g_nlog;
	CHECK(p2_start("fceumm", P2_CABLE) && g_diatom_on && g_queued == 0);
	CHECK(at + 2 < g_nlog && !strcmp(g_log[at], "waiting PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2") &&
	      !strcmp(g_log[at + 1], "waiting PLAYER2\ton=0") && !strcmp(g_log[at + 2], SET_ON));
	p2_stop();
	/* Nothing waiting: nothing read, the start as it always was */
	fresh();
	CHECK(p2_start("fceumm", P2_HOTSPOT) && g_nlog == 4 && !strcmp(g_log[0], SET_ON));
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
	game_end_is_quiet();
	guest_notices();
	blip_says_nothing();
	hotspot_failures();
	restart_reads_its_own_answer();
	theme_follows();
	if (fails) { printf("p2: %d failed\n", fails); return 1; }
	printf("p2: ok\n");
	return 0;
}
