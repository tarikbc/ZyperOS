/* SPDX-License-Identifier: GPL-3.0-only */
/* The downloads' queue: src/dlq.h */
#include "dlq.h"

#include <stdio.h>
#include <sys/statvfs.h>

#include "monoclock.h"

typedef struct {
	const dlq_lane_def *d;
	bool     running;
	unsigned last;       /* when it last stepped */
	unsigned room_at;    /* when the card's room was last looked at */
	dlq_end  end;        /* how its last run ended */
	unsigned ends;       /* how many have ended */
} lane;

static lane g_lane[DLQ_LANES];
static bool g_game;
static bool g_page[DLQ_LANES];

/* The card below the floor: a lane writing it stops before it is full */
static bool card_low(const char *card)
{
	struct statvfs v;

	if (!card || statvfs(card, &v) != 0) return false;
	return (unsigned long long)v.f_bavail * v.f_frsize < DLQ_FLOOR;
}

static void finish(dlq_lane l, dlq_end how)
{
	lane *ln = &g_lane[l];
	net_slot was;

	if (!ln->running) return;
	ln->running = false;
	was = net_slot_use(ln->d->slot);
	ln->d->done(how);
	net_slot_use(was);
	ln->end = how;
	ln->ends++;
	fprintf(stderr, "dlq: %s %s\n", ln->d->name,
	        how == DLQ_ENDED ? "ended" : how == DLQ_FULL ? "stopped: the card is nearly full" : "stopped");
}

bool dlq_start(dlq_lane l, const dlq_lane_def *d, bool (*begin)(void))
{
	lane *ln;
	net_slot was;
	bool ok;

	if ((int)l < 0 || l >= DLQ_LANES || !d || !begin) return false;
	ln = &g_lane[l];
	if (ln->running) return false;
	was = net_slot_use(d->slot);
	ok = begin();
	net_slot_use(was);
	if (!ok) return false;
	ln->d = d;
	ln->running = true;
	ln->last = 0;
	ln->room_at = now_ms32();
	fprintf(stderr, "dlq: %s started%s\n", d->name, g_game ? ", waiting for the game to end" : "");
	return true;
}

void dlq_stop(dlq_lane l)
{
	if ((int)l >= 0 && l < DLQ_LANES) finish(l, DLQ_STOPPED);
}

void dlq_stop_all(void)
{
	int l;

	for (l = 0; l < DLQ_LANES; l++) finish((dlq_lane)l, DLQ_STOPPED);
}

bool dlq_running(dlq_lane l)
{
	return (int)l >= 0 && l < DLQ_LANES && g_lane[l].running;
}

bool dlq_busy(void)
{
	int l;

	for (l = 0; l < DLQ_LANES; l++) if (g_lane[l].running) return true;
	return false;
}

dlq_end dlq_last(dlq_lane l)
{
	return (int)l >= 0 && l < DLQ_LANES ? g_lane[l].end : DLQ_NONE;
}

unsigned dlq_ends(dlq_lane l)
{
	return (int)l >= 0 && l < DLQ_LANES ? g_lane[l].ends : 0;
}

/* Each running lane's step, at most once per its every_ms unless `any` */
static void step_lanes(bool any)
{
	unsigned now = now_ms32();
	int l;

	for (l = 0; l < DLQ_LANES; l++) {
		lane *ln = &g_lane[l];
		net_slot was;
		int r;

		if (!ln->running) continue;
		if (!any && ln->last && now - ln->last < ln->d->every_ms) continue;
		ln->last = now ? now : 1;
		was = net_slot_use(ln->d->slot);
		r = ln->d->step(now);
		net_slot_use(was);
		if (r != 1) {
			finish((dlq_lane)l, r == 0 ? DLQ_ENDED : DLQ_STOPPED);
			continue;
		}
		if (ln->d->card && now - ln->room_at >= DLQ_ROOM_MS) {
			ln->room_at = now;
			if (card_low(ln->d->card)) finish((dlq_lane)l, DLQ_FULL);
		}
	}
}

void dlq_tick(void)
{
	if (g_game || !dlq_busy()) return;
	step_lanes(false);
}

void dlq_game_tick(unsigned budget_ms)
{
	static bool said;
	unsigned t0;
	int i;

	if (!g_game || !dlq_busy()) return;
	if (!said) {
		said = true;
		fprintf(stderr, "dlq: going on in the game (dl.in_game), %u ms a tick at most\n", budget_ms);
	}
	t0 = now_ms32();
	for (i = 0; i < DLQ_GAME_STEPS && dlq_busy() && now_ms32() - t0 < budget_ms; i++) step_lanes(true);
}

unsigned dlq_wait(unsigned ms)
{
	if (g_game || !dlq_busy()) return ms;
	dlq_tick();
	return ms < 16 ? ms : 16;
}

void dlq_game(bool running)
{
	if (running == g_game) return;
	g_game = running;
	if (dlq_busy())
		fprintf(stderr, "dlq: %s\n", running ? "a game started: the lanes wait" : "the game is over: the lanes go on");
}

bool dlq_in_game(void) { return g_game; }

void dlq_page(dlq_lane l, bool open)
{
	if ((int)l >= 0 && l < DLQ_LANES) g_page[l] = open;
}

bool dlq_page_open(dlq_lane l)
{
	return (int)l >= 0 && l < DLQ_LANES && g_page[l];
}

bool dlq_behind(void)
{
	int l;

	for (l = 0; l < DLQ_LANES; l++) if (g_lane[l].running && !g_page[l]) return true;
	return false;
}

int dlq_pct(void)
{
	int l, n = 0, sum = 0;

	for (l = 0; l < DLQ_LANES; l++) {
		const lane *ln = &g_lane[l];
		int p;

		if (!ln->running) continue;
		p = ln->d->pct ? ln->d->pct() : 0;
		sum += p < 0 ? 0 : p > 100 ? 100 : p;
		n++;
	}
	return n ? sum / n : 0;
}
