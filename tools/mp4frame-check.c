/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's shared video frames (src/mp4frame.h): the daemon's writer and the
 * launcher's reader, run against each other on two threads over the same
 * memory, as fast as they go. Every picture is filled with one byte, its
 * serial's low byte, so a picture the reader keeps (mpf_still_good) that is
 * not that byte all through was torn - written over while it was read - and
 * that is the one thing the seqlock is there to prevent. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mp4frame.h"

#define FRAMES 4000
#define W 320
#define H 180

static void *base;
static volatile int done;

static void *writer(void *arg)
{
	mpf_head *h = base;
	uint32_t n;

	(void)arg;
	for (n = 1; n <= FRAMES; n++) {
		int s = mpf_begin(h);

		memset(mpf_slot_data(base, s), (int)(n & 0xff), mpf_bytes(W, H));
		h->slot[s].w = W;
		h->slot[s].h = H;
		h->slot[s].serial = n;
		h->slot[s].pts = n / 25.0;
		mpf_publish(h, s);
	}
	done = 1;
	return NULL;
}

int main(void)
{
	mpf_head *h;
	pthread_t th;
	uint8_t *copy = malloc(mpf_bytes(W, H));
	uint32_t seen = 0, kept = 0, torn = 0, retried = 0, last = 0, backwards = 0;

	printf("mp4frame: the daemon's pictures, read while they are written\n");
	base = calloc(1, MPF_SIZE);
	if (!base || !copy) { printf("no memory\n"); return 1; }
	h = base;
	h->magic = MPF_MAGIC;
	h->version = MPF_VERSION;
	{
		uint32_t seq;

		if (mpf_peek(h, 0, &seq) >= 0) { printf("  FAIL: a picture before any was written\n"); return 1; }
	}
	pthread_create(&th, NULL, writer, NULL);
	while (!done || __atomic_load_n(&h->serial, __ATOMIC_ACQUIRE) != seen) {
		uint32_t seq, i, serial;
		int s = mpf_peek(h, seen, &seq);

		if (s < 0) continue;
		serial = h->slot[s].serial;
		memcpy(copy, mpf_slot_data(base, s), mpf_bytes(h->slot[s].w, h->slot[s].h));
		if (!mpf_still_good(h, s, seq)) { retried++; continue; }
		seen = serial;
		kept++;
		if (serial < last) backwards++;
		last = serial;
		for (i = 0; i < mpf_bytes(W, H); i++)
			if (copy[i] != (uint8_t)(serial & 0xff)) { torn++; break; }
	}
	pthread_join(th, NULL);
	printf("  %u pictures written, %u kept, %u read again after a race\n",
	       FRAMES, kept, retried);
	if (torn || backwards || last != FRAMES || kept == 0) {
		printf("  FAIL: %u torn, %u out of order, last %u of %u\n", torn, backwards, last, FRAMES);
		return 1;
	}
	{
		uint32_t seq;

		if (mpf_peek(h, last, &seq) >= 0) { printf("  FAIL: the same picture twice\n"); return 1; }
	}
	printf("ok\n");
	return 0;
}
