/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "raqueue.h"

#include "cheevos.h"
#include "library.h"
#include "net.h"
#include "rafetch.h"
#include "rahash.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Pull down what the account already holds for the loaded game, and mark
 * those earned locally so they are not watched, not re-announced, and not
 * submitted again.
 *
 * The account wins on what EXISTS; the local store wins on what is still
 * owed, because it is the only record of anything earned offline. Neither is
 * discarded. Silent when offline or not signed in - that is the ordinary
 * case, and it leaves the device working from what it knows. */
int ra_merge_unlocks(const int *ids, int n)
{
	int i, added = 0;

	if (n <= 0 || chv_game() <= 0) return 0;

	/* Only ids the set actually has. The account carries entries the set does
	 * not - RetroAchievements' "Unknown Emulator" notice is one, and it went
	 * straight into the store the first time this ran. Recording those adds a
	 * row that can never be displayed and could later be submitted as a
	 * duplicate of something that was never an achievement. */
	for (i = 0; i < n; i++) {
		int k;

		for (k = 0; k < chv_count(); k++)
			if (chv_at(k)->id == ids[i]) break;
		if (k == chv_count()) continue;
		if (chv_note_earned(chv_game(), ids[i], true)) added++;
	}

	if (added) {
		chv_earned_save();
	}
	return added;
}

/* Send what is owed. Called once the game is over and the launcher has the
 * screen back - never from the wait loop.
 *
 * Anything that will not send stays pending and is tried again next time,
 * which is the offline queueing RA's own requirements ask for and the right
 * shape regardless: an unlock earned on a plane is still earned. */
/* Refusals in a row that stop a pass: one is an achievement's own, three
 * together are taken for the account's, which would refuse every row */
#define FLUSH_REFUSALS 3

static void ra_flush_unlocks(const char *rom, const char *tag, int cur)
{
	char hash[33] = "";
	int game, id, sent = 0, settled = 0;

	if (!ra_signed_in() || chv_pending_count() == 0 || !net_online()) return;

	/* The hash is what a real client sends alongside an award, and it is the
	 * one for the game just played - so only its own unlocks carry it. */
	ra_hash_rom(rom, tag, hash);

	/* A row settled leaves the pending list, so the next is at the same
	 * index; a row refused stays, and the next is one further on. A failure
	 * stops the loop rather than spinning on it - if one will not go, the
	 * rest will not either. A refusal is that one row's (RA_SUBMIT_REFUSED),
	 * and stopped every later unlock until it was skipped; FLUSH_REFUSALS in
	 * a row are taken for the account's, and stop the loop too. */
	int k = 0, refused = 0;

	while (chv_pending_at(k, &game, &id)) {
		int rc = ra_submit_unlock(id, game == cur ? hash : NULL);

		if (rc == RA_SUBMIT_FAILED) break;
		if (rc == RA_SUBMIT_REFUSED) {
			if (++refused >= FLUSH_REFUSALS) break;
			k++;
			continue;
		}
		refused = 0;
		chv_mark_synced(game, id);
		settled++;
		if (rc > 0) sent++;
	}
	/* On `settled`, not on `sent`. A row the account already had is settled
	 * without being sent, and writing only when something was sent left that
	 * mark in memory only - so it came back pending on the next boot and was
	 * retried, forever. Seen on the device: one unlock submitted by hand
	 * outside the launcher, refused as already-held, and asked again at the
	 * end of every game after that. */
	if (settled) {
		chv_earned_save();
		fprintf(stderr, "ra: %d sent, %d already held, %d still queued\n",
		        sent, settled - sent, chv_pending_count());
	}
}

/* ---------- the unlock queue, sent behind the shelf ---------------------- */

/* ra_flush_unlocks, above, on a thread of its own.
 *
 * Quitting a game with something earned used to wait here, on the main thread,
 * between the game's wait loop and the shelf's - so the power button was read
 * by neither. It hashed the ROM, then sent each pending unlock one at a time,
 * each through a request with a 20s timeout: a second or two on a black screen
 * on a good network, and on a bad one a handheld ignoring its own power button
 * for minutes. Now the shelf comes straight back and the sending happens behind
 * it.
 *
 * The worker never touches the cheevos store. The main thread takes a snapshot
 * of what is owed and a copy of the account, hands both over, and later applies
 * the answers itself - chv_mark_synced and chv_earned_save only ever run here.
 * No lock guards the store because nothing but the main thread can reach it.
 *
 * Two things had to be made safe before this could exist. net_post_buf wrote
 * every request's config and response to one file per PROCESS, so a request
 * from here overlapping one from the main thread would have swapped them; it
 * is per call now. And ra_submit_unlock read the account straight from the
 * globals a sign-in rewrites, so the worker sends as a copy instead.
 *
 * If the thread cannot be started, ra_flush_unlocks runs as it always did. */

/* Most unlocks one pass carries. More than that owed - a long spell offline -
 * is sent by the pass that follows, which starts by itself once this one has
 * settled something. */
#define FLUSH_MAX 256

static SDL_Thread *g_fl_thread;
static SDL_mutex  *g_fl_lock;
static SDL_cond   *g_fl_wake;
static bool        g_fl_stop;

/* The job, written here and taken by the worker. */
static bool g_fl_job;                    /* one is waiting */
static bool g_fl_busy;                   /* the worker has one in hand */
static char g_fl_rom[LIB_PATH * 2], g_fl_tag[16];
static char g_fl_user[RA_USER_MAX], g_fl_token[RA_TOKEN_MAX];
static int  g_fl_cur;                    /* whose unlocks carry the ROM hash */
static int  g_fl_game[FLUSH_MAX], g_fl_id[FLUSH_MAX], g_fl_n;

/* The answers, written by the worker and applied here. */
static int  g_fl_rgame[FLUSH_MAX], g_fl_rid[FLUSH_MAX], g_fl_rrc[FLUSH_MAX];
static int  g_fl_rn;
static bool g_fl_finished;               /* the job in hand is done */
static bool g_fl_failed;                 /* it stopped on a real failure */

/* Main thread only. The latest game quit, for the pass that follows; and
 * whether the job in hand has settled anything, which is what guarantees a
 * follow-on pass makes progress rather than spinning.
 *
 * The game is carried, not read from chv_game() when the pass starts. A pass
 * can start from the shelf, where the info screen loads whichever set it is
 * showing - so chv_game() could name another game and hand it this ROM's
 * hash. */
static char g_fl_next_rom[LIB_PATH * 2], g_fl_next_tag[16];
static int  g_fl_next_cur;
static bool g_fl_again;
static int  g_fl_settled;

static int flush_worker(void *unused)
{
	(void)unused;
	SDL_LockMutex(g_fl_lock);
	for (;;) {
		char rom[LIB_PATH * 2], tag[16], user[RA_USER_MAX], token[RA_TOKEN_MAX];
		char hash[33] = "";
		int  game[FLUSH_MAX], id[FLUSH_MAX], n, cur, i;
		bool failed = false, hashed = false;
		int refused = 0;

		while (!g_fl_stop && !g_fl_job) SDL_CondWait(g_fl_wake, g_fl_lock);
		if (g_fl_stop) break;
		memcpy(rom, g_fl_rom, sizeof rom);
		memcpy(tag, g_fl_tag, sizeof tag);
		memcpy(user, g_fl_user, sizeof user);
		memcpy(token, g_fl_token, sizeof token);
		n = g_fl_n;
		cur = g_fl_cur;
		memcpy(game, g_fl_game, (size_t)n * sizeof game[0]);
		memcpy(id, g_fl_id, (size_t)n * sizeof id[0]);
		g_fl_job = false;
		g_fl_busy = true;
		SDL_UnlockMutex(g_fl_lock);

		for (i = 0; i < n; i++) {
			int rc;

			/* Checked before each request, not only between jobs: a launcher
			 * on its way out should not start a fresh 20s wait. */
			SDL_LockMutex(g_fl_lock);
			if (g_fl_stop) { SDL_UnlockMutex(g_fl_lock); break; }
			SDL_UnlockMutex(g_fl_lock);

			/* The hash only for the game just played, and only once, since
			 * it reads the whole ROM. */
			if (game[i] == cur && !hashed) {
				ra_hash_rom(rom, tag, hash);
				hashed = true;
			}
			rc = ra_submit_unlock_as(user, token, id[i],
			                         game[i] == cur ? hash : NULL);

			/* Bounded although it cannot fill: a pass never starts while the
			 * answers to the last one are waiting (see ra_flush_start), so
			 * this holds at most this pass's n. An answer dropped here would
			 * only mean that unlock is sent again next time and settled as
			 * already held. */
			SDL_LockMutex(g_fl_lock);
			if (g_fl_rn < FLUSH_MAX) {
				g_fl_rgame[g_fl_rn] = game[i];
				g_fl_rid[g_fl_rn]   = id[i];
				g_fl_rrc[g_fl_rn]   = rc;
				g_fl_rn++;
			}
			SDL_UnlockMutex(g_fl_lock);

			/* A real failure stops the pass - if one will not go, the rest
			 * will not either. 0, "the account already had it", does not, and
			 * neither does one achievement refused on its own: that one stays
			 * owed and the rest go (FLUSH_REFUSALS, ra_flush_unlocks). */
			if (rc == RA_SUBMIT_FAILED) { failed = true; break; }
			if (rc == RA_SUBMIT_REFUSED) {
				if (++refused >= FLUSH_REFUSALS) { failed = true; break; }
			} else {
				refused = 0;
			}
		}

		SDL_LockMutex(g_fl_lock);
		g_fl_busy = false;
		g_fl_finished = true;
		g_fl_failed = failed;
	}
	SDL_UnlockMutex(g_fl_lock);
	return 0;
}

static bool flush_started(void)
{
	if (g_fl_thread) return true;
	if (!(g_fl_lock = SDL_CreateMutex())) return false;
	if (!(g_fl_wake = SDL_CreateCond())) {
		SDL_DestroyMutex(g_fl_lock);
		g_fl_lock = NULL;
		return false;
	}
	if (!(g_fl_thread = SDL_CreateThread(flush_worker, "zyperos-raflush", NULL))) {
		SDL_DestroyCond(g_fl_wake);  g_fl_wake = NULL;
		SDL_DestroyMutex(g_fl_lock); g_fl_lock = NULL;
		return false;
	}
	return true;
}

/* Hand what is owed to the worker and return at once. Main thread. */
void ra_flush_start(const char *rom, const char *tag, int cur)
{
	int n = 0, game, id;

	if (!ra_signed_in() || chv_pending_count() == 0 || !net_online()) return;
	if (!flush_started()) { ra_flush_unlocks(rom, tag, cur); return; }

	/* Not copied when they already are these buffers: the follow-on pass hands
	 * them straight back in, and snprintf from a buffer into itself is
	 * undefined. */
	if (rom != g_fl_next_rom)
		snprintf(g_fl_next_rom, sizeof g_fl_next_rom, "%s", rom);
	if (tag != g_fl_next_tag)
		snprintf(g_fl_next_tag, sizeof g_fl_next_tag, "%s", tag);
	g_fl_next_cur = cur;

	SDL_LockMutex(g_fl_lock);
	/* Busy until its answers have been APPLIED, not merely until the worker
	 * finishes. A pass can finish during a game, and its answers then wait
	 * for the main loop. Starting another before they are applied would
	 * snapshot those same unlocks as still owed and send them twice, and would
	 * append a second pass's answers onto the first's. */
	if (g_fl_job || g_fl_busy || g_fl_finished || g_fl_rn > 0) {
		/* Note it instead; the pass that follows picks up everything still
		 * owed once these answers are in. */
		g_fl_again = true;
		SDL_UnlockMutex(g_fl_lock);
		return;
	}
	while (n < FLUSH_MAX && chv_pending_at(n, &game, &id)) {
		g_fl_game[n] = game;
		g_fl_id[n]   = id;
		n++;
	}
	snprintf(g_fl_rom, sizeof g_fl_rom, "%s", rom);
	snprintf(g_fl_tag, sizeof g_fl_tag, "%s", tag);
	ra_creds_copy(g_fl_user, sizeof g_fl_user, g_fl_token, sizeof g_fl_token);
	g_fl_cur = cur;
	g_fl_n = n;
	g_fl_settled = 0;
	g_fl_job = true;
	SDL_CondSignal(g_fl_wake);
	SDL_UnlockMutex(g_fl_lock);
}

/* Apply whatever the worker has answered, and start the next pass when one is
 * owed. Main thread, every pass of the main loop - it is a lock and a count
 * when there is nothing to do. Nothing here is drawn, so it asks for no frame. */
void ra_flush_poll(void)
{
	int  rgame[FLUSH_MAX], rid[FLUSH_MAX], rrc[FLUSH_MAX], rn, i, sent = 0, settled = 0;
	bool finished, failed, again;

	if (!g_fl_thread) return;
	SDL_LockMutex(g_fl_lock);
	rn = g_fl_rn;
	memcpy(rgame, g_fl_rgame, (size_t)rn * sizeof rgame[0]);
	memcpy(rid, g_fl_rid, (size_t)rn * sizeof rid[0]);
	memcpy(rrc, g_fl_rrc, (size_t)rn * sizeof rrc[0]);
	g_fl_rn = 0;
	finished = g_fl_finished;
	failed = g_fl_failed;
	g_fl_finished = false;
	again = g_fl_again;
	if (finished) g_fl_again = false;
	SDL_UnlockMutex(g_fl_lock);

	for (i = 0; i < rn; i++) {
		if (rrc[i] < 0) continue;
		chv_mark_synced(rgame[i], rid[i]);
		settled++;
		if (rrc[i] > 0) sent++;
	}
	/* On settled, not on sent, for the reason ra_flush_unlocks gives. */
	if (settled) {
		g_fl_settled += settled;
		chv_earned_save();
		fprintf(stderr, "ra: %d sent, %d already held, %d still queued\n",
		        sent, settled - sent, chv_pending_count());
	}

	if (!finished) return;
	/* Another pass when a later game was quit during this one, or when this one
	 * ran out of room with more still owed. The second only if it settled
	 * something, so a pass that achieved nothing cannot keep starting itself. */
	if (again || (!failed && g_fl_settled > 0 && chv_pending_count() > 0))
		ra_flush_start(g_fl_next_rom, g_fl_next_tag, g_fl_next_cur);
}

/* Tell the worker to stop, and do not wait for it.
 *
 * Waiting could mean sitting out a request's 20s timeout on the way to exit or
 * power off. It needs no waiting: the worker touches nothing that shutdown
 * frees - no SDL object is destroyed here, it never reaches the store or the
 * database - so the process ending takes it cleanly. What it had not answered
 * stays owed and is sent next time; one it had sent but not yet reported is
 * sent again, and the account's "already has this achievement" settles it. */
void ra_flush_stop(void)
{
	if (!g_fl_thread) return;
	SDL_LockMutex(g_fl_lock);
	g_fl_stop = true;
	SDL_CondSignal(g_fl_wake);
	SDL_UnlockMutex(g_fl_lock);
	SDL_DetachThread(g_fl_thread);
	g_fl_thread = NULL;
}
