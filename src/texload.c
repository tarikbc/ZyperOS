/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See texload.h for why this exists. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#include "texload.h"

/* Slots each way. Comfortably more than the seven cards a shelf draws, so a
 * fast scroll queues ahead without the ring filling, and small enough that a
 * shelf abandoned mid-scroll leaves a few decodes to throw away rather than
 * hundreds. */
#define QN    24
/* LIB_PATH * 3, the size main.c builds these paths in, without taking a
 * dependency on library.h for one number. */
#define PATHN 1664

typedef struct {
	int      sys, idx;
	unsigned gen;
	/* Tried in this order; an empty path is skipped. */
	char     first[PATHN], second[PATHN];
	/* The theme the main thread read when it asked: the worker never reads
	 * theme(). */
	int      theme;
	/* The art itself is drawn for `theme` (texload_want_art), so whatever
	 * file is found comes back saying so. */
	bool     themed;
} req;

typedef struct {
	int          sys, idx;
	unsigned     gen;
	SDL_Surface *surf;
	/* The theme themed art was asked for in; -1 for art that is no
	 * theme's. */
	int          theme;
	int          asked;      /* the theme the request carried, for queued() */
} res;

/* Two, on a four-core device, leaving a core for the render thread and one
 * for everything else.
 *
 * Two was chosen when NES covers were 496KB and Genesis 572KB against SNES's
 * 272KB: those two dropped cards on a fast scroll where SNES never did,
 * despite SNES having the most games, so the obvious reading was that the art
 * was too big and shrinking it would remove the need for a second worker.
 *
 * TESTED, 2026-09-13, AND THAT WAS WRONG. After the rescrape put NES at 308KB
 * and Genesis at 354KB - about 40% less work per card - one worker still drops
 * a card occasionally on those two shelves. Both changes are pulling their
 * weight and neither makes the other unnecessary. Do not trim this back to one
 * again on the theory that the art is small enough now; it has been measured
 * and it is not.
 *
 * The cost of the second is close to nothing: workers sleep on the condvar
 * when there is nothing to decode, and while scrolling the total work is
 * unchanged - it is the same covers, spread over two cores instead of one. */
#define NWORKERS 2

static SDL_Thread *g_thread[NWORKERS];
static SDL_mutex  *g_lock;
static SDL_cond   *g_wake;
static bool        g_running;

static req      g_rq[QN];
static int      g_rq_head, g_rq_n;
static res      g_rs[QN];
static int      g_rs_head, g_rs_n;
static unsigned g_gen = 1;
/* What each worker holds right now. Without it, a card asked for every frame
 * would be queued again the moment it left the request ring and before its
 * result arrived - the one window where it is in neither. One slot per worker,
 * because two sharing a slot would let each hide the other's card from the
 * dedup and queue it twice.
 *
 * With the generation it was asked for, because texload_bump cannot reach a
 * decode already under way: it empties both rings but the worker still holds
 * the job. Without the generation, that disowned job kept answering "already
 * coming" for its slot, so the request for what the slot NOW holds was refused
 * until the stale decode finished and was thrown away.
 *
 * And with the theme it was asked in, for the same reason across a change
 * of theme (queued). */
static int      g_cur_sys[NWORKERS], g_cur_idx[NWORKERS], g_cur_theme[NWORKERS];
static unsigned g_cur_gen[NWORKERS];

/* Whether (sys, idx) is already on its way in theme `theme_ix`: being
 * decoded, or decoded and waiting to be taken. Called with the lock held.
 *
 * In that theme and no other. After a change of theme every card is asked
 * for again in the theme that is on, and one being decoded for the theme
 * that was, or decoded for it and not taken yet, is not that card: it does
 * not stop the new ask, and it lands to be turned away (texload_drain,
 * cardgen_kept). One not started yet is the caller's to find (waiting). */
static bool queued(int sys, int idx, int theme_ix)
{
	int k;

	for (k = 0; k < NWORKERS; k++)
		if (sys == g_cur_sys[k] && idx == g_cur_idx[k] &&
		    g_cur_gen[k] == g_gen && g_cur_theme[k] == theme_ix)
			return true;
	for (k = 0; k < g_rs_n; k++) {
		const res *r = &g_rs[(g_rs_head + k) % QN];
		if (r->sys == sys && r->idx == idx && r->asked == theme_ix) return true;
	}
	return false;
}

/* Where the request for (sys, idx) not started yet stands in the request
 * ring, counted from its oldest, or -1. Called with the lock held. */
static int waiting(int sys, int idx)
{
	int k;

	for (k = 0; k < g_rq_n; k++) {
		const req *q = &g_rq[(g_rq_head + k) % QN];
		if (q->sys == sys && q->idx == idx) return k;
	}
	return -1;
}

/* The request `at` places from the oldest taken out of the ring, the newer
 * ones moved down a place each so the ring keeps its order. Called with the
 * lock held. */
static void unqueue(int at)
{
	int k;

	for (k = at; k + 1 < g_rq_n; k++)
		g_rq[(g_rq_head + k) % QN] = g_rq[(g_rq_head + k + 1) % QN];
	g_rq_n--;
}

/* Worker side. Touches no renderer and no launcher state - only its own copy
 * of the job, which is why the paths are copied into the request rather than
 * pointed at: the list they came from can be rebuilt while this runs. */
static SDL_Surface *decode(const char *path)
{
	SDL_Surface *raw, *conv;

	if (!path || !*path) return NULL;
	if (!(raw = IMG_Load(path))) return NULL;
	conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	return conv;
}

static int worker(void *arg)
{
	const int me = (int)(intptr_t)arg;

	for (;;) {
		req          job;
		SDL_Surface *s;

		SDL_LockMutex(g_lock);
		while (g_running && g_rq_n == 0) SDL_CondWait(g_wake, g_lock);
		if (!g_running) { SDL_UnlockMutex(g_lock); break; }
		/* NEWEST FIRST. A queue served oldest-first spends a fast scroll
		 * decoding cards the cursor left several hops ago while the ones on
		 * screen wait behind them - which is exactly when it matters, and
		 * exactly when it used to give up. The newest request is the card
		 * being looked at. */
		g_rq_n--;
		job = g_rq[(g_rq_head + g_rq_n) % QN];
		g_cur_sys[me] = job.sys;
		g_cur_idx[me] = job.idx;
		g_cur_gen[me] = job.gen;
		g_cur_theme[me] = job.theme;
		SDL_UnlockMutex(g_lock);

		s = decode(job.first);
		if (!s) s = decode(job.second);

		SDL_LockMutex(g_lock);
		g_cur_sys[me] = g_cur_idx[me] = -1;
		/* A result whose generation moved on is for a shelf that no longer
		 * exists in that shape, and installing it would put one game's art
		 * on another. */
		if (job.gen == g_gen && g_rs_n < QN) {
			res *r = &g_rs[(g_rs_head + g_rs_n) % QN];
			r->sys = job.sys;
			r->idx = job.idx;
			r->gen = job.gen;
			r->surf = s;
			r->theme = job.themed ? job.theme : -1;
			r->asked = job.theme;
			g_rs_n++;
		} else if (s) {
			SDL_FreeSurface(s);
		}
		SDL_UnlockMutex(g_lock);
	}
	return 0;
}

bool texload_start(void)
{
	int k;

	if (g_thread[0]) return true;
	for (k = 0; k < NWORKERS; k++) g_cur_sys[k] = g_cur_idx[k] = -1;
	if (!(g_lock = SDL_CreateMutex())) return false;
	if (!(g_wake = SDL_CreateCond())) {
		SDL_DestroyMutex(g_lock);
		g_lock = NULL;
		return false;
	}
	g_running = true;
	for (k = 0; k < NWORKERS; k++) {
		char name[24];
		snprintf(name, sizeof name, "zyperos-texload%d", k);
		g_thread[k] = SDL_CreateThread(worker, name, (void *)(intptr_t)k);
	}
	if (!g_thread[0]) {
		/* None at all: fall back to decoding on the frame, as before. */
		g_running = false;
		SDL_DestroyCond(g_wake);   g_wake = NULL;
		SDL_DestroyMutex(g_lock);  g_lock = NULL;
		fprintf(stderr, "texload: no worker (%s); art decodes on the frame\n",
		        SDL_GetError());
		return false;
	}
	return true;
}

void texload_stop(void)
{
	int k;

	if (!g_thread[0]) return;
	SDL_LockMutex(g_lock);
	g_running = false;
	SDL_CondBroadcast(g_wake);      /* every waiter, not one of them */
	SDL_UnlockMutex(g_lock);
	for (k = 0; k < NWORKERS; k++) {
		if (!g_thread[k]) continue;
		SDL_WaitThread(g_thread[k], NULL);
		g_thread[k] = NULL;
	}
	texload_bump();              /* frees whatever was still waiting */
	SDL_DestroyCond(g_wake);     g_wake = NULL;
	SDL_DestroyMutex(g_lock);    g_lock = NULL;
}

void texload_bump(void)
{
	int k;

	if (!g_lock) return;
	SDL_LockMutex(g_lock);
	g_gen++;
	/* Queued work is dropped outright. Work already in flight is left to
	 * finish and is thrown away when it lands, because its generation will
	 * no longer match - stopping it mid-decode would cost more than letting
	 * one image finish into the bin. */
	g_rq_n = g_rq_head = 0;
	for (k = 0; k < g_rs_n; k++) {
		res *r = &g_rs[(g_rs_head + k) % QN];
		if (r->surf) SDL_FreeSurface(r->surf);
	}
	g_rs_n = g_rs_head = 0;
	SDL_UnlockMutex(g_lock);
}

void texload_forget(void)
{
	if (!g_lock) return;
	SDL_LockMutex(g_lock);
	g_rq_n = g_rq_head = 0;
	SDL_UnlockMutex(g_lock);
}

/* texload_want and texload_want_art: one queue, with or without a theme. */
static bool ask(int sys, int idx, const char *first, const char *second,
                int theme_ix, bool themed)
{
	req *q;
	int at;

	if (!g_thread[0]) return false;
	SDL_LockMutex(g_lock);
	at = waiting(sys, idx);
	if (queued(sys, idx, theme_ix) ||
	    (at >= 0 && g_rq[(g_rq_head + at) % QN].theme == theme_ix)) {
		SDL_UnlockMutex(g_lock);
		return true;
	}
	/* Waiting in another theme, asked before a change of theme and not
	 * started: taken out, so the files of the theme that was are never read
	 * only for the card to be turned away, and this ask queued as any is, the
	 * newest. Not rewritten where it stood, behind everything asked since:
	 * the card on screen, asked for last, would be decoded last.
	 *
	 * Full: drop the OLDEST rather than refuse the newest. Refusing meant the
	 * cards on screen were the ones that never got queued, because they are
	 * the ones asked for last. The card at the back has been stale longest
	 * and is the one nobody is waiting for. */
	if (at >= 0) unqueue(at);
	if (g_rq_n >= QN) {
		g_rq_head = (g_rq_head + 1) % QN;
		g_rq_n--;
	}
	q = &g_rq[(g_rq_head + g_rq_n) % QN];
	g_rq_n++;
	q->sys = sys;
	q->idx = idx;
	q->gen = g_gen;
	snprintf(q->first,  sizeof q->first,  "%s", first  ? first  : "");
	snprintf(q->second, sizeof q->second, "%s", second ? second : "");
	q->theme = theme_ix;
	q->themed = themed;
	SDL_CondSignal(g_wake);
	SDL_UnlockMutex(g_lock);
	return true;
}

bool texload_want(int sys, int idx, const char *first, const char *second)
{
	return ask(sys, idx, first, second, -1, false);
}

bool texload_want_art(int sys, int idx, const char *first, const char *second,
                      int theme_ix)
{
	return ask(sys, idx, first, second, theme_ix, theme_ix >= 0);
}

bool texload_take(int *sys, int *idx, SDL_Surface **surf, int *theme_ix)
{
	res *r;

	if (!g_thread[0]) return false;
	SDL_LockMutex(g_lock);
	if (g_rs_n == 0) {
		SDL_UnlockMutex(g_lock);
		return false;
	}
	r = &g_rs[g_rs_head];
	*sys  = r->sys;
	*idx  = r->idx;
	*surf = r->surf;
	if (theme_ix) *theme_ix = r->theme;
	g_rs_head = (g_rs_head + 1) % QN;
	g_rs_n--;
	SDL_UnlockMutex(g_lock);
	return true;
}

bool texload_ready(void)
{
	bool any;

	if (!g_thread[0]) return false;
	SDL_LockMutex(g_lock);
	any = g_rs_n > 0;
	SDL_UnlockMutex(g_lock);
	return any;
}

bool texload_idle(void)
{
	bool idle;

	if (!g_thread[0]) return true;
	SDL_LockMutex(g_lock);
	idle = g_rq_n == 0 && g_rs_n == 0;
	for (int k = 0; k < NWORKERS; k++)
		if (g_cur_sys[k] >= 0) idle = false;
	SDL_UnlockMutex(g_lock);
	return idle;
}
