/* SPDX-License-Identifier: GPL-3.0-only */
/* See ahead.h. */
#include "ahead.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv_data = PTHREAD_COND_INITIALIZER;   /* frames came, or a release */
static pthread_cond_t  cv_room = PTHREAD_COND_INITIALIZER;   /* frames went, or a hold */
static pthread_cond_t  cv_idle = PTHREAD_COND_INITIALIZER;   /* the write in progress ended */

static int16_t    ring[AHEAD_FRAMES * 2];
static int        head, len;      /* the oldest frame, and how many */
static int        in_hand;        /* frames the writer took, written or being written */
static bool       writing, held, failed, started;
static ahead_sink out;
static pthread_t  th;

static void *writer(void *arg)
{
	static int16_t chunk[AHEAD_WRITE * 2];

	(void)arg;
	pthread_mutex_lock(&mu);
	for (;;) {
		int n, first;
		long r;

		while (held || failed || len == 0) pthread_cond_wait(&cv_data, &mu);
		n = len < AHEAD_WRITE ? len : AHEAD_WRITE;
		first = AHEAD_FRAMES - head < n ? AHEAD_FRAMES - head : n;
		memcpy(chunk, ring + head * 2, (size_t)first * 2 * sizeof chunk[0]);
		if (n > first) memcpy(chunk + first * 2, ring, (size_t)(n - first) * 2 * sizeof chunk[0]);
		head = (head + n) % AHEAD_FRAMES;
		len -= n;
		in_hand = n;
		writing = true;
		pthread_cond_broadcast(&cv_room);
		pthread_mutex_unlock(&mu);

		r = out(chunk, n);

		pthread_mutex_lock(&mu);
		writing = false;
		in_hand = 0;
		if (r < 0) {
			failed = true;
			pthread_cond_broadcast(&cv_room);
		}
		pthread_cond_broadcast(&cv_idle);
	}
	return NULL;
}

bool ahead_start(ahead_sink sink)
{
	bool ok;

	pthread_mutex_lock(&mu);
	if (!started) {
		out = sink;
		started = pthread_create(&th, NULL, writer, NULL) == 0;
		if (started) pthread_detach(th);
	}
	ok = started;
	pthread_mutex_unlock(&mu);
	return ok;
}

bool ahead_push(const int16_t *frames, int n)
{
	pthread_mutex_lock(&mu);
	while (n > 0) {
		int room, k, at, first;

		while (started && !failed && (room = AHEAD_FRAMES - len) == 0)
			pthread_cond_wait(&cv_room, &mu);
		if (!started || failed) break;
		k = n < room ? n : room;
		at = (head + len) % AHEAD_FRAMES;
		first = AHEAD_FRAMES - at < k ? AHEAD_FRAMES - at : k;
		memcpy(ring + at * 2, frames, (size_t)first * 2 * sizeof ring[0]);
		if (k > first) memcpy(ring, frames + first * 2, (size_t)(k - first) * 2 * sizeof ring[0]);
		len += k;
		frames += k * 2;
		n -= k;
		pthread_cond_signal(&cv_data);
	}
	pthread_mutex_unlock(&mu);
	return n == 0;
}

long ahead_frames(void)
{
	long n;

	pthread_mutex_lock(&mu);
	n = len + in_hand;
	pthread_mutex_unlock(&mu);
	return n;
}

bool ahead_failed(void)
{
	bool f;

	pthread_mutex_lock(&mu);
	f = failed;
	pthread_mutex_unlock(&mu);
	return f;
}

void ahead_hold(void)
{
	pthread_mutex_lock(&mu);
	held = true;
	while (writing) pthread_cond_wait(&cv_idle, &mu);
	head = len = 0;
	failed = false;
	pthread_cond_broadcast(&cv_room);
	pthread_mutex_unlock(&mu);
}

void ahead_release(void)
{
	pthread_mutex_lock(&mu);
	held = false;
	pthread_cond_signal(&cv_data);
	pthread_mutex_unlock(&mu);
}
