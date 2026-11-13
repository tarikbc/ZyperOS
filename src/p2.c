/* SPDX-License-Identifier: GPL-3.0-only */
/* Player 2 in a game, the launcher's side. See p2.h. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hotspot.h"
#include "p2.h"
#include "p2share.h"
#include "webtheme.h"

/* The systems Player 2 is offered on (p2.h says how one is added). The NES
 * alone for now: the page's pad is the NES's, and the codec sends a picture
 * of 256 colours at most (src/p2d/codec.h), which is the NES's palette. */
static const p2_system SYSTEMS[] = {
	{ "fceumm", 0x01FDu, 2 },
};

const p2_system *p2_system_of(const char *core)
{
	size_t i;

	for (i = 0; core && i < sizeof SYSTEMS / sizeof SYSTEMS[0]; i++)
		if (!strcmp(core, SYSTEMS[i].core)) return &SYSTEMS[i];
	return NULL;
}

bool p2_offered(const char *core, bool device)
{
	return device && p2_system_of(core) != NULL;
}

p2_link p2_link_now(bool adapter_in, bool computer_on_cable)
{
	if (computer_on_cable) return P2_CABLE;
	return adapter_in ? P2_HOTSPOT : P2_NO_LINK;
}

bool p2_answer(const char *line, bool *on, unsigned *buttons, int *players)
{
	const char *p;
	bool said = false;

	if (!line || strncmp(line, "PLAYER2\t", 8)) return false;
	for (p = line + 8; *p; ) {
		size_t len = strcspn(p, "\t");

		if (len > 3 && !strncmp(p, "on=", 3)) {
			if (on) *on = p[3] == '1';
			said = true;
		} else if (len > 8 && !strncmp(p, "buttons=", 8)) {
			if (buttons) *buttons = (unsigned)strtoul(p + 8, NULL, 0);
		} else if (len > 8 && !strncmp(p, "players=", 8)) {
			if (players) *players = atoi(p + 8);
		}
		p += len;
		if (*p) p++;
	}
	return said;
}

/* ---- on and off --------------------------------------------------------- */

/* How often the share's guests are read: the notice comes within a tenth of
 * a second, and a game's tick is ten a second anyway */
#define GUESTS_MS 100
/* zyperos.local asked again while it would not start: the cable's link comes
 * up a moment after it is bound */
#define MDNS_RETRY_MS 1000
/* p2d's time to go on SIGTERM, then SIGKILL, and as long again for that */
#define P2D_GRACE_MS 1000
/* Diatom answers SETPLAYER2 from its pause in a frame */
#define ANSWER_MS 500

static const p2_ops *g_ops;

static struct {
	p2_state st;
	p2_link  link;
	const p2_system *sys;
	int      players;     /* as Diatom answered */
	bool     said_on;     /* Diatom took SETPLAYER2 on=1, and was not told off */
	bool     hot;         /* hotspot_start was asked: hotspot_stop is owed */
	bool     cable;       /* the cable's link was bound for this */
	bool     mdns;        /* zyperos.local is up */
	bool     starting;    /* the hotspot is coming up; p2d after it */
	bool     over;        /* the game is over (p2_game_over) */
	long     pid;         /* p2d, -1 for none */
	unsigned term_at;     /* when p2d was sent SIGTERM, 0 for not */
	unsigned guests;      /* the bits as last read, within the players */
	unsigned guests_at, mdns_at;
} g = { .link = P2_NO_LINK, .pid = -1 };

void p2_use(const p2_ops *ops)
{
	g_ops = ops;
}

/* The guests' bits that are a player of this system's: port k+1 is bit k,
 * for the players - 1 guests */
static unsigned guest_mask(void)
{
	return g.players > 1 ? (1u << (g.players - 1)) - 1u : 0u;
}

/* A notice for every guest whose bit came or went */
static void guests_now(unsigned bits)
{
	unsigned k;

	bits &= guest_mask();
	for (k = 0; k < P2S_GUESTS; k++) {
		unsigned b = 1u << k;

		if ((bits & b) != (g.guests & b) && g_ops->notice)
			g_ops->notice((bits & b) ? P2_JOINED : P2_LEFT, (int)k + 2);
	}
	g.guests = bits;
}

/* p2d for the link: the page from the card's res/web, the share Diatom
 * made, the handheld's language and theme. On the hotspot it is the DNS too
 * (--dns), every name the handheld, so a phone's internet check is sent to
 * the page and its sign-in sheet opens it. */
static bool spawn_p2d(const char *addr, bool dns)
{
	char *argv[18];
	const char *lang = g_ops->lang ? g_ops->lang() : "en";
	int n = 0;

	argv[n++] = (char *)g_ops->p2d;
	argv[n++] = (char *)"--root";  argv[n++] = (char *)g_ops->root;
	argv[n++] = (char *)"--share"; argv[n++] = (char *)P2S_PATH;
	argv[n++] = (char *)"--addr";  argv[n++] = (char *)addr;
	argv[n++] = (char *)"--port";  argv[n++] = (char *)P2_PORT;
	if (dns) argv[n++] = (char *)"--dns";
	argv[n++] = (char *)"--lang";  argv[n++] = (char *)(lang && *lang ? lang : "en");
	argv[n++] = (char *)"--theme"; argv[n++] = (char *)WEBTHEME_FILE;
	argv[n] = NULL;
	g.pid = g_ops->spawn(argv);
	g.term_at = 0;
	if (g.pid < 0) {
		fprintf(stderr, "player 2: p2d did not start\n");
		return false;
	}
	fprintf(stderr, "player 2: p2d on %s\n", addr);
	return true;
}

/* What stops at once, from the tick or a start that failed: the guests are
 * gone, p2d is told to go (not waited for), and Diatom stops sharing the
 * game. The hotspot and the cable stay for p2_stop, whose wait for the Wi-Fi
 * to come back is no tick's to take. From the tick, during the game, one
 * notice says Player 2 stopped (its guests with it); a start that failed is
 * said by the page it was started from. */
static void fail_now(const char *why, bool tell)
{
	fprintf(stderr, "player 2: %s\n", why);
	g.guests = 0;
	if (tell && g_ops->notice) g_ops->notice(P2_STOPPED, 0);
	if (g.pid > 0 && !g.term_at) {
		g_ops->signal(g.pid, SIGTERM);
		g.term_at = g_ops->now();
		if (!g.term_at) g.term_at = 1;
	}
	if (g.said_on) {
		g_ops->resident("SETPLAYER2\ton=0");
		g.said_on = false;
	}
	g.starting = false;
	g.st = P2_FAILED;
}

bool p2_start(const char *core, p2_link l)
{
	const p2_system *s = p2_system_of(core);
	char line[96], ans[128];
	bool on = false, answered;
	unsigned buttons;
	int players;

	if (!g_ops || !s || l == P2_NO_LINK) return false;
	if (g.st == P2_WAITING || g.st == P2_CONNECTED) return true;
	p2_stop();                         /* what a failure left */

	g.sys = s;
	g.link = l;
	buttons = s->buttons;
	players = s->players;
	snprintf(line, sizeof line, "SETPLAYER2\ton=1\tbuttons=0x%04X\tplayers=%d", s->buttons,
	         s->players);
	/* Refused outside a game, and said so: Diatom answers PLAYER2 at once */
	if (!g_ops->resident(line)) {
		fprintf(stderr, "player 2: no game to ask\n");
		g.st = P2_FAILED;
		g.link = P2_NO_LINK;
		return false;
	}
	answered = g_ops->answer("PLAYER2\t", ans, sizeof ans, ANSWER_MS) &&
	           p2_answer(ans, &on, &buttons, &players);
	if (!answered || !on) {
		/* No answer in time is no answer: a Diatom that takes it late
		 * would share the game's frames with nobody until the game ended,
		 * so it is told off as well */
		if (answered) {
			fprintf(stderr, "player 2: the game did not take it\n");
		} else {
			fprintf(stderr, "player 2: the game did not answer\n");
			g_ops->resident("SETPLAYER2\ton=0");
		}
		g.st = P2_FAILED;
		g.link = P2_NO_LINK;
		return false;
	}
	g.said_on = true;
	/* What Diatom made of it: its players size the guests' bits */
	g.players = players >= 2 && players <= P2S_PLAYERS_MAX ? players : s->players;
	/* Before p2d, which serves it; a page with none keeps its own look */
	if (!g_ops->theme()) fprintf(stderr, "player 2: no theme file; the page keeps its own\n");
	g.guests = 0;
	g.guests_at = g.mdns_at = g_ops->now();
	if (l == P2_HOTSPOT) {
		g.hot = true;
		if (!g_ops->hotspot_start()) {
			fail_now("the hotspot did not start", false);
			return false;
		}
		g.starting = true;
		g.st = P2_WAITING;
		return true;
	}
	g_ops->cable_up();
	g.cable = true;
	g.mdns = g_ops->mdns_start();
	if (!spawn_p2d(P2_CABLE_ADDR, false)) {
		fail_now("p2d did not start on the cable", false);
		return false;
	}
	g.st = P2_WAITING;
	return true;
}

/* The game ended under it: nothing more is said, the guests go quietly,
 * and the launcher's p2_stop at the game's end does the rest */
static void game_ended(const char *why)
{
	if (why) fprintf(stderr, "player 2: %s\n", why);
	g.over = true;
	g.guests = 0;
	g.starting = false;
}

void p2_tick(unsigned now_ms)
{
	int status = 0, bits;

	if (!g_ops || g.over || (g.st != P2_WAITING && g.st != P2_CONNECTED)) return;
	if (g.link == P2_HOTSPOT) {
		int hs = g_ops->hotspot_poll();

		if (g.starting) {
			if (hs == HOTSPOT_STARTING) return;
			if (hs != HOTSPOT_UP) { fail_now("the hotspot did not come up", true); return; }
			/* Up: its address is there, which p2d's DNS binds once */
			g.starting = false;
			if (!spawn_p2d(P2_HOTSPOT_ADDR, true)) {
				fail_now("p2d did not start on the hotspot", true);
				return;
			}
		} else if (hs != HOTSPOT_UP) {
			fail_now("the hotspot went away", true);
			return;
		}
	} else if (g.link == P2_CABLE) {
		if (g.mdns) g_ops->mdns_poll(now_ms);
		else if (now_ms - g.mdns_at >= MDNS_RETRY_MS) {
			g.mdns_at = now_ms;
			g.mdns = g_ops->mdns_start();
		}
	}
	/* p2d gone. With 0 it saw the share go, which is the game ending (Diatom
	 * takes the share away only then): nothing to say. Otherwise it crashed,
	 * or its port was taken, or (1) no share came in five seconds, which a
	 * restart would not mend either: it fails, and says so */
	if (g.pid > 0 && g_ops->ended(g.pid, &status)) {
		g.pid = -1;
		if (status == 0) { game_ended("p2d ended with the game"); return; }
		fail_now("p2d ended with an error", true);
		return;
	}
	if (now_ms - g.guests_at >= GUESTS_MS) {
		g.guests_at = now_ms;
		bits = g_ops->guests();
		if (bits < 0) { game_ended("the game's share went"); return; }
		guests_now((unsigned)bits);
		g.st = g.guests ? P2_CONNECTED : P2_WAITING;
	}
}

void p2_game_over(void)
{
	if (g.st != P2_OFF) game_ended(NULL);
}

void p2_stop(void)
{
	int status;

	if (!g_ops) return;
	if (g.pid > 0) {
		unsigned t0, waited;
		bool killed = false;

		if (!g.term_at) {
			g_ops->signal(g.pid, SIGTERM);
			g.term_at = g_ops->now();
		}
		t0 = g.term_at;
		while (!g_ops->ended(g.pid, &status)) {
			waited = g_ops->now() - t0;
			if (!killed && waited >= P2D_GRACE_MS) {
				fprintf(stderr, "player 2: p2d did not go on SIGTERM\n");
				g_ops->signal(g.pid, SIGKILL);
				killed = true;
				t0 = g_ops->now();
			} else if (killed && waited >= P2D_GRACE_MS) {
				fprintf(stderr, "player 2: p2d did not go on SIGKILL either\n");
				break;
			}
			g_ops->sleep(10);
		}
		g.pid = -1;
	}
	if (g.said_on) g_ops->resident("SETPLAYER2\ton=0");
	/* Diatom removes it as the game ends, unless it was killed first */
	if (g.over) g_ops->unshare();
	if (g.hot) g_ops->hotspot_stop();
	if (g.mdns) g_ops->mdns_stop();
	if (g.cable) g_ops->cable_down();
	if (g.st != P2_OFF) fprintf(stderr, "player 2: off\n");
	g.st = P2_OFF;
	g.link = P2_NO_LINK;
	g.sys = NULL;
	g.players = 0;
	g.said_on = g.hot = g.cable = g.mdns = g.starting = g.over = false;
	g.term_at = 0;
	g.guests = 0;
}

void p2_theme_changed(void)
{
	if (g_ops && !g.over && (g.st == P2_WAITING || g.st == P2_CONNECTED)) g_ops->theme();
}

p2_state p2_state_now(void) { return g.st; }
p2_link  p2_link_on(void)
{
	if (g.st == P2_OFF) return P2_NO_LINK;
	return g.hot ? P2_HOTSPOT : g.cable ? P2_CABLE : P2_NO_LINK;
}
bool     p2_starting(void)  { return g.starting; }
unsigned p2_guests(void)    { return g.guests; }
int      p2_players(void)   { return g.players; }

const char *p2_join_url(void)
{
	switch (p2_link_on()) {
	case P2_HOTSPOT: return "http://" P2_HOTSPOT_ADDR "/play/";
	case P2_CABLE:   return "http://zyperos.local/play/";
	default:         return "";
	}
}
