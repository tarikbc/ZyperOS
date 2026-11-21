/* SPDX-License-Identifier: GPL-3.0-only */
/* portkeys's shutter over your PICO-8 (tools/pkshutter.c) on this computer's
 * alsa-lib, as tools/shotfeel-check.sh sets it up: the system's "default" a
 * plug into "mixed", and "usb" the DAC's, each a stand-in that writes down
 * every frame that reaches it (alsa-lib's file PCM over its null one, raw),
 * and "game" one more, PICO-8's stream, which plays a tone all through.
 *
 *   - A shot's shutter starts and returns at once (a shot never waits on
 *     the output), and a second while it sounds plays none.
 *   - What reaches the mix is the shutter's file, sample for sample, whole
 *     to its end (drained, not cut), on both sides; Mono Left on the DAC
 *     is the left side alone, on the DAC's device and nowhere else.
 *   - PICO-8's stream, opened before and written all through, has every
 *     frame of its tone as it was written: the shutter is a player of its
 *     own beside it, never in it.
 *   - A device that will not open plays nothing, and the next shot's
 *     shutter plays again.
 *
 *     shotfeel-alsa RES_SOUNDS SCRATCH     (SCRATCH holds mix.raw, usb.raw, game.raw)
 */
#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fileio.h"
#include "pkshutter.h"
#include "uisound.h"

static int fails, checks;

static void ck(int ok, const char *what)
{
	checks++;
	if (!ok) fails++;
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

static long now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000L + t.tv_nsec / 1000;
}

static int16_t *raw(const char *dir, const char *name, size_t *frames)
{
	char path[600];
	size_t n = 0;
	char *buf;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	buf = file_read(path, 1L << 26, &n);
	*frames = buf ? n / 4 : 0;
	return (int16_t *)buf;
}

/* PICO-8's stream: GAME_SLICES slices of a 440 Hz tone, 10 ms apart */
#define GAME_SLICES 60
#define GAME_SLICE  480
static int16_t g_tone[GAME_SLICES * GAME_SLICE * 2];
static int     g_game_ok;

static void *game(void *unused)
{
	snd_pcm_t *pcm;
	struct timespec nap = { 0, 10 * 1000000L };
	int i, ok = 1;

	(void)unused;
	if (snd_pcm_open(&pcm, "game", SND_PCM_STREAM_PLAYBACK, 0) < 0 ||
	    snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1, 40000) < 0)
		return NULL;
	for (i = 0; i < GAME_SLICES; i++) {
		if (snd_pcm_writei(pcm, g_tone + (size_t)i * GAME_SLICE * 2, GAME_SLICE) != GAME_SLICE) ok = 0;
		nanosleep(&nap, NULL);
	}
	snd_pcm_drain(pcm);
	snd_pcm_close(pcm);
	g_game_ok = ok;
	return NULL;
}

int main(int argc, char **argv)
{
	char path[600];
	int16_t *want, *got;
	size_t n, frames, i;
	int wf = 0, side_ok, voice = DS_VOICE_DEFAULT;
	pthread_t th;
	long t0, took;
	bool first, second;

	if (argc != 3) {
		fprintf(stderr, "usage: shotfeel-alsa RES_SOUNDS SCRATCH\n");
		return 2;
	}
	snprintf(path, sizeof path, "%s/%s/shutter.wav", argv[1], DS_VOICE_ID[voice]);
	{
		char *buf = file_read(path, 1L << 20, &n);

		want = buf ? uisound_wav((const uint8_t *)buf, n, &wf) : NULL;
		free(buf);
	}
	ck(want && wf > 0, "the shutter's file reads, as the launcher reads it");
	ck(pkshutter_load(argv[1]), "portkeys reads every voice's shutter before a shot");
	if (!want) return 1;

	for (i = 0; i < GAME_SLICES * GAME_SLICE; i++)
		g_tone[2 * i] = g_tone[2 * i + 1] = (int16_t)(sin(2 * M_PI * 440.0 * (double)i / 48000.0) * 9000.0);
	if (pthread_create(&th, NULL, game, NULL) != 0) return 1;
	nanosleep(&(struct timespec){ 0, 100 * 1000000L }, NULL);

	printf("a shot over PICO-8, its tone playing:\n");
	t0 = now_us();
	first = pkshutter_play(voice, "default", true, true);
	took = now_us() - t0;
	second = pkshutter_play(voice, "default", true, true);
	ck(first, "the shutter starts");
	ck(took < 5000, "and the shot goes on at once: it never waits on the output");
	ck(!second && pkshutter_busy(), "a second shot while it sounds plays none");
	ck(pkshutter_wait(3000) && !pkshutter_busy(), "it ends");
	got = raw(argv[2], "mix.raw", &frames);
	side_ok = got && frames >= (size_t)wf;
	for (i = 0; side_ok && i < (size_t)wf; i++)
		side_ok = got[2 * i] == want[i] && got[2 * i + 1] == want[i];
	for (; side_ok && i < frames; i++) side_ok = !got[2 * i] && !got[2 * i + 1];
	ck(side_ok, "the mix got the shutter's file sample for sample, to its end, on both sides, then silence");
	free(got);

	printf("Mono Left, a DAC in:\n");
	ck(pkshutter_play(voice, "usb", true, false) && pkshutter_wait(3000), "the shutter plays on the DAC's device");
	got = raw(argv[2], "usb.raw", &frames);
	side_ok = got && frames >= (size_t)wf;
	for (i = 0; side_ok && i < (size_t)wf; i++) side_ok = got[2 * i] == want[i] && !got[2 * i + 1];
	ck(side_ok, "the left side alone, the right silent");
	free(got);
	got = raw(argv[2], "mix.raw", &frames);
	ck(got && frames >= (size_t)wf, "and the system's mix was not opened for it: what it had is still there");
	free(got);

	printf("a device that will not open:\n");
	ck(pkshutter_play(voice, "zyperos_no_such_pcm", true, true) && pkshutter_wait(3000),
	   "the shutter plays nothing there, and ends");
	ck(pkshutter_play(voice, "default", true, true) && pkshutter_wait(3000), "the next shot's shutter plays again");

	pthread_join(th, NULL);
	printf("PICO-8's stream, beside it all along:\n");
	ck(g_game_ok, "every write PICO-8 made was taken whole");
	got = raw(argv[2], "game.raw", &frames);
	ck(got && frames == GAME_SLICES * GAME_SLICE && !memcmp(got, g_tone, sizeof g_tone),
	   "its tone reached its own device frame for frame as it was written, nothing added, cut or paused");
	free(got);
	free(want);
	if (fails) {
		printf("shotfeel-alsa: %d of %d FAILED\n", fails, checks);
		return 1;
	}
	printf("shotfeel-alsa: ok (%d checks)\n", checks);
	return 0;
}
