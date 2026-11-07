/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See texload.h for why this exists. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#include "texload.h"
#include "cardgen.h"   /* cardgen_kept: a card is shown in its own theme only */
#include "putpace.h"

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
	/* The card drawn where neither file is a picture (texload_want_or_draw),
	 * a copy, as the paths are; `theme` is its theme. */
	bool          carded;
	ui_card_order card;
} req;

typedef struct {
	int          sys, idx;
	unsigned     gen;
	SDL_Surface *surf;
	/* The theme themed art was asked for in; -1 for art that is no
	 * theme's. */
	int          theme;
	int          asked;      /* the theme the request carried, for queued() */
	bool         card;       /* surf is the generated card, drawn for `theme` */
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
/* Room in the result ring, for a worker holding a finished picture. The
 * frame takes a few a frame now (texload_deliver), not every one, and a
 * worker drawing cards finishes many in a frame: a full ring used to throw
 * the newest away, to be asked for, read and drawn all over again. So a
 * worker with a full ring waits for the frame to take one, and the requests
 * wait behind it, newest first as ever. */
static SDL_cond   *g_room;
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
		bool         drawn, gone;

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
		/* No file, so the card in its place, here rather than on the frame:
		 * several times an upload, and a shelf of games with no art asks for
		 * a window of them at once. Not for a shelf that has gone since it
		 * was asked for (texload_bump), which would only be thrown away -
		 * and is what a fallback game's ui_quit is waiting behind. NULL when
		 * the faces cannot be had, and the frame makes it as it always did. */
		drawn = false;
		if (!s && job.carded) {
			SDL_LockMutex(g_lock);
			gone = job.gen != g_gen;
			SDL_UnlockMutex(g_lock);
			if (!gone && (s = ui_card_paint(&job.card, true))) drawn = true;
		}

		SDL_LockMutex(g_lock);
		while (g_running && job.gen == g_gen && g_rs_n >= QN)
			SDL_CondWait(g_room, g_lock);
		g_cur_sys[me] = g_cur_idx[me] = -1;
		/* A result whose generation moved on is for a shelf that no longer
		 * exists in that shape, and installing it would put one game's art
		 * on another. */
		if (g_running && job.gen == g_gen && g_rs_n < QN) {
			res *r = &g_rs[(g_rs_head + g_rs_n) % QN];
			r->sys = job.sys;
			r->idx = job.idx;
			r->gen = job.gen;
			r->surf = s;
			r->theme = job.themed || drawn ? job.theme : -1;
			r->asked = job.theme;
			r->card = drawn;
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
	if (!(g_wake = SDL_CreateCond()) || !(g_room = SDL_CreateCond())) {
		if (g_wake) SDL_DestroyCond(g_wake);
		g_wake = NULL;
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
		SDL_DestroyCond(g_room);   g_room = NULL;
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
	SDL_CondBroadcast(g_room);
	SDL_UnlockMutex(g_lock);
	for (k = 0; k < NWORKERS; k++) {
		if (!g_thread[k]) continue;
		SDL_WaitThread(g_thread[k], NULL);
		g_thread[k] = NULL;
	}
	texload_bump();              /* frees whatever was still waiting */
	SDL_DestroyCond(g_wake);     g_wake = NULL;
	SDL_DestroyCond(g_room);     g_room = NULL;
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
	SDL_CondBroadcast(g_room);
	SDL_UnlockMutex(g_lock);
}

void texload_forget(void)
{
	if (!g_lock) return;
	SDL_LockMutex(g_lock);
	g_rq_n = g_rq_head = 0;
	SDL_UnlockMutex(g_lock);
}

/* texload_want, texload_want_art and texload_want_or_draw: one queue, with
 * or without a theme, with or without a card. */
static bool ask(int sys, int idx, const char *first, const char *second,
                int theme_ix, bool themed, const ui_card_order *card)
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
	q->carded = card != NULL;
	if (card) q->card = *card;
	SDL_CondSignal(g_wake);
	SDL_UnlockMutex(g_lock);
	return true;
}

bool texload_want(int sys, int idx, const char *first, const char *second)
{
	return ask(sys, idx, first, second, -1, false, NULL);
}

bool texload_want_art(int sys, int idx, const char *first, const char *second,
                      int theme_ix)
{
	return ask(sys, idx, first, second, theme_ix, theme_ix >= 0, NULL);
}

bool texload_want_or_draw(int sys, int idx, const char *first,
                          const char *second, const ui_card_order *card)
{
	return ask(sys, idx, first, second, card ? card->theme_ix : -1, false, card);
}

/* The result `at` places from the oldest, taken out of the ring, the older
 * ones moved along a place each so the ring keeps its order. Called with the
 * lock held. */
static void pop(int at, texload_got *g)
{
	const res *r = &g_rs[(g_rs_head + at) % QN];
	int k;

	*g = (texload_got){ r->sys, r->idx, r->surf, r->theme, r->card };
	for (k = at; k > 0; k--)
		g_rs[(g_rs_head + k) % QN] = g_rs[(g_rs_head + k - 1) % QN];
	g_rs_head = (g_rs_head + 1) % QN;
	g_rs_n--;
	SDL_CondSignal(g_room);
}

bool texload_take(texload_got *g)
{
	if (!g_thread[0]) return false;
	SDL_LockMutex(g_lock);
	if (g_rs_n == 0) {
		SDL_UnlockMutex(g_lock);
		return false;
	}
	pop(0, g);
	SDL_UnlockMutex(g_lock);
	return true;
}

/* The frame's pace, kept from frame to frame: what a megapixel costs to put
 * on screen is this device's, learned as it goes (src/putpace.h). One frame
 * thread, so one pace. */
static putpace g_pace = { TEXLOAD_SHARE_MS, 0.0f, 0.0f, 0 };

static float ms_since(Uint64 t0)
{
	return (float)((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
	               (double)SDL_GetPerformanceFrequency());
}

/* One taken picture to `put`, or to the bin when it is a card of a theme no
 * longer on, which is false. Not under the lock: `put` makes a texture, and
 * a worker waiting to hand its next picture over should not wait for the
 * GPU. */
static bool hand(texload_got *g, int theme_on,
                 void (*put)(void *ctx, texload_got *g), void *ctx)
{
	if (g->card && !cardgen_kept(g->theme, theme_on)) {
		SDL_FreeSurface(g->surf);
		return false;
	}
	put(ctx, g);
	return true;
}

int texload_deliver(int theme_on, void (*put)(void *ctx, texload_got *g),
                    void *ctx)
{
	int n = 0;

	if (!g_thread[0]) return 0;
	putpace_frame(&g_pace);
	for (;;) {
		texload_got g;
		long px;
		Uint64 t0;

		SDL_LockMutex(g_lock);
		if (g_rs_n == 0) {
			SDL_UnlockMutex(g_lock);
			break;
		}
		/* Asked before it is taken: one that does not fit stays first in
		 * line for the next frame. */
		px = g_rs[g_rs_head].surf ? (long)g_rs[g_rs_head].surf->w * g_rs[g_rs_head].surf->h : 0;
		if (!putpace_room(&g_pace, px)) {
			SDL_UnlockMutex(g_lock);
			break;
		}
		pop(0, &g);
		SDL_UnlockMutex(g_lock);

		/* A card thrown away for its theme is not one put on screen: it
		 * takes no frame's turn and teaches nothing, a free saying nothing
		 * of what an upload costs, and costs a few microseconds. */
		t0 = SDL_GetPerformanceCounter();
		if (hand(&g, theme_on, put, ctx)) {
			putpace_took(&g_pace, px, ms_since(t0));
			n++;
		}
	}
	return n;
}

bool texload_deliver_slot(int sys, int idx, int theme_on,
                          void (*put)(void *ctx, texload_got *g), void *ctx)
{
	texload_got g;
	int k;

	if (!g_thread[0]) return false;
	SDL_LockMutex(g_lock);
	for (k = 0; k < g_rs_n; k++) {
		const res *r = &g_rs[(g_rs_head + k) % QN];
		if (r->sys == sys && r->idx == idx) break;
	}
	if (k == g_rs_n) {
		SDL_UnlockMutex(g_lock);
		return false;
	}
	pop(k, &g);
	SDL_UnlockMutex(g_lock);
	return hand(&g, theme_on, put, ctx);
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

int texload_waiting(void)
{
	int n;

	if (!g_thread[0]) return 0;
	SDL_LockMutex(g_lock);
	n = g_rs_n;
	SDL_UnlockMutex(g_lock);
	return n;
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
