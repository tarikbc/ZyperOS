/* SPDX-License-Identifier: GPL-3.0-only */
/* The shutter over your PICO-8: see pkshutter.h. */
#include "pkshutter.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fileio.h"
#include "pcm_out.h"
#include "uisound.h"

/* A slice, mixed and written at a time: 10 ms, as the launcher's player */
#define SLICE (UIS_RATE / 100)

static int16_t *g_pcm[DS_VOICES];
static int      g_frames[DS_VOICES];

/* The one playing: set by pkshutter_play, cleared by its thread as it ends */
static volatile int g_busy;
static struct {
	int  voice;
	char device[64];
	bool left, right;
} g_job;

/* As the launcher's player opens its output (src/uisound_out.c): 40 ms of
 * buffer, started on the first slice, so the shutter is heard at once */
static pcm_out g_out = {
	.who = "portkeys: shutter", .rate = UIS_RATE, .buffer_us = 40000, .start = SLICE,
	.samples = 512, .ahead = UIS_RATE * 30 / 1000, .poll_ms = 2, .tail_ms = 20,
};

bool pkshutter_load(const char *dir)
{
	char path[600];
	int v, n = 0;

	for (v = 0; v < DS_VOICES; v++) {
		size_t len;
		char *buf;

		if (g_pcm[v]) { n++; continue; }
		snprintf(path, sizeof path, "%s/%s/%s.wav", dir, DS_VOICE_ID[v], DS_SOUND_ID[DS_SOUND_SHUTTER]);
		if (!(buf = file_read(path, 1L << 20, &len))) continue;
		g_pcm[v] = uisound_wav((const uint8_t *)buf, len, &g_frames[v]);
		free(buf);
		if (g_pcm[v]) n++;
	}
	if (n < DS_VOICES)
		fprintf(stderr, "portkeys: %d of %d shutters read from %s\n", n, DS_VOICES, dir);
	return n > 0;
}

static void *player(void *unused)
{
	int16_t slice[2 * SLICE];
	uis_voice x = { g_pcm[g_job.voice], g_frames[g_job.voice], 0, 32768 };
	int nv = 1;
	bool ok;

	(void)unused;
	ok = pcm_out_open(&g_out, g_job.device);
	while (ok && nv) {
		nv = uisound_mix(&x, nv, slice, SLICE, g_job.left, g_job.right);
		ok = pcm_out_write(&g_out, slice, SLICE);
	}
	/* Drained, so its end is heard: a close drops what is still buffered */
	if (g_out.pcm || g_out.dev) pcm_out_close(&g_out, ok);
	fprintf(stderr, "portkeys: the shutter %s on %s\n", ok ? "played" : "did not play", g_job.device);
	__sync_synchronize();
	g_busy = 0;
	return NULL;
}

bool pkshutter_play(int voice, const char *device, bool left, bool right)
{
	pthread_t th;

	if (voice < 0 || voice >= DS_VOICES || !g_pcm[voice] || !device || !*device) return false;
	if (!__sync_bool_compare_and_swap(&g_busy, 0, 1)) return false;
	g_job.voice = voice;
	snprintf(g_job.device, sizeof g_job.device, "%s", device);
	g_job.left = left;
	g_job.right = right;
	if (pthread_create(&th, NULL, player, NULL) != 0) {
		g_busy = 0;
		return false;
	}
	pthread_detach(th);
	return true;
}

bool pkshutter_busy(void)
{
	return g_busy != 0;
}

bool pkshutter_wait(int ms)
{
	struct timespec nap = { 0, 5 * 1000000L };

	for (; g_busy && ms > 0; ms -= 5) nanosleep(&nap, NULL);
	return !g_busy;
}
