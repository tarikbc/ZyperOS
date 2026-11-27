/* SPDX-License-Identifier: GPL-3.0-only */
/* See pcm.h. */
#include "pcm.h"
#include "ahead.h"
#include "../monoclock.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static snd_pcm_t *g_pcm;
static char       g_err[128];
static int        g_last;        /* the last open's error, 0 when it worked */

/* The last things done to the output, kept for when it goes wrong - #42.
 * Twice on 2026-09-25 MP4 stalled inside bluealsa's plugin, and the second
 * time the MP4 before it vanished with no line at all, so there was nothing
 * to say which call it had been making. Each entry is formatted when it is
 * made, wall clock included to line up with the syslog, so that dumping them
 * is only write() and is safe from a signal handler. */
#define TRAIL 32
static char     g_trail[TRAIL][128];
static unsigned g_trail_n;
static volatile long g_write_since;   /* when the write in progress began, or 0 */
static long     g_written;           /* frames written since the last open or prepare */
/* The output's own queue after the writer's last write, and when: what
 * pcm_queued adds to the ring's, less what has played since. Kept by the
 * writer thread so the player never calls into ALSA beside its write. */
static volatile long g_delay, g_delay_at;
static char     g_werr[128];         /* the writer's failure, for pcm_write to say */
static long     g_underruns;         /* the output ran dry, since the start */
static long     g_underrun_said_at, g_underrun_said;

void pcm_note(const char *fmt, ...)
{
	char *slot = g_trail[g_trail_n % TRAIL];
	struct timespec t;
	struct tm tm;
	va_list ap;
	int n;

	clock_gettime(CLOCK_REALTIME, &t);
	localtime_r(&t.tv_sec, &tm);
	n = snprintf(slot, sizeof g_trail[0], "mp4d:   %02d:%02d:%02d.%03ld ",
	             tm.tm_hour, tm.tm_min, tm.tm_sec, t.tv_nsec / 1000000);
	va_start(ap, fmt);
	vsnprintf(slot + n, sizeof g_trail[0] - n - 1, fmt, ap);
	va_end(ap);
	n = (int)strlen(slot);
	/* Into the syslog as well, where it lands between bluetoothd's transport
	 * lines in the order things happened. Not by time: the syslog's own stamps
	 * come in batches - two lines three seconds apart read the same to the
	 * millisecond, measured 2026-09-25 - hence the wall clock in the text. */
	syslog(LOG_INFO, "%s", slot + 8);
	slot[n] = '\n';
	slot[n + 1] = '\0';
	g_trail_n++;
}

void pcm_trail_dump(int fd)
{
	unsigned i = g_trail_n > TRAIL ? g_trail_n - TRAIL : 0;

	for (; i < g_trail_n; i++) {
		const char *s = g_trail[i % TRAIL];
		if (write(fd, s, strlen(s)) < 0) return;
	}
}

long pcm_in_write_ms(void)
{
	long since = g_write_since;
	return since ? now_ms() - since : 0;
}

static long write_out(const int16_t *frames, int n);

bool pcm_open(const char *device)
{
	bool ok;
	int r;

	pcm_close();
	if (!ahead_start(write_out)) {
		snprintf(g_err, sizeof g_err, "open %s: no writer thread", device);
		pcm_note("%s", g_err);
		return false;
	}
	ahead_hold();
	r = snd_pcm_open(&g_pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
	g_last = r < 0 ? r : 0;
	if (r < 0) {
		snprintf(g_err, sizeof g_err, "open %s: %s", device, snd_strerror(r));
		pcm_note("%s", g_err);
		g_pcm = NULL;
		ahead_release();
		return false;
	}
	/* soft_resample 0: the decoder already delivers 48 kHz, and asking ALSA
	 * to resample again would be the awrate path this exists to avoid.
	 * 200 ms of latency: long enough that a busy frame in a running game does
	 * not starve it, short enough that pause and seek feel immediate. */
	r = snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16_LE,
	                       SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 0, 200000);
	/* Except a headset that is not running at 48 kHz, where ALSA has to
	 * resample or nothing plays. A headset picks the rate when IT starts the
	 * connection: the OpenFit came back at 44.1 kHz on reconnecting by itself
	 * and every open failed on it, measured 2026-09-25. The wired path's
	 * reason against awrate does not carry over - the SBC encode that follows
	 * costs more than the resample does.
	 *
	 * Through `plug:`, because a bare `type bluealsa` PCM has no rate
	 * converter to allow: soft_resample 1 on the PCM itself failed the same
	 * way, measured the same day. Only on a rate mismatch, which is -EINVAL
	 * here; busy stays busy, for the caller to retry. */
	if (r == -EINVAL && strcmp(device, "default")) {
		char plug[160];

		snd_pcm_close(g_pcm);   /* held already: not pcm_close, which holds */
		g_pcm = NULL;
		snprintf(plug, sizeof plug, "plug:%s", device);
		r = snd_pcm_open(&g_pcm, plug, SND_PCM_STREAM_PLAYBACK, 0);
		if (r >= 0)
			r = snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16_LE,
			                       SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
			                       200000);
		else
			g_pcm = NULL;
		if (r >= 0)
			fprintf(stderr, "mp4d: %s is not at 48 kHz; ALSA resamples it\n", device);
	}
	if (r < 0) {
		snprintf(g_err, sizeof g_err, "set params on %s: %s", device,
		         snd_strerror(r));
		g_last = r;
		pcm_note("%s", g_err);
		if (g_pcm) snd_pcm_close(g_pcm);
		g_pcm = NULL;
		ahead_release();
		return false;
	}
	pcm_note("open %s: ok", device);
	g_written = 0;
	g_delay = 0;
	ok = g_pcm != NULL;
	ahead_release();
	return ok;
}

void pcm_close(void)
{
	if (!g_pcm) return;
	ahead_hold();
	pcm_note("close");
	snd_pcm_close(g_pcm);
	g_pcm = NULL;
	g_delay = 0;
	ahead_release();
}

/* An underrun, said on stderr as it happens, but at most once a second with
 * how many there were since: a run of them is one line a second, not fifty */
static void underrun(void)
{
	long t = now_ms();

	g_underruns++;
	if (g_underrun_said_at && t - g_underrun_said_at < 1000) return;
	fprintf(stderr, "mp4d: underrun: the output ran dry (%ld since the start, %ld in the last %ld ms), "
	        "%ld frames decoded ahead\n", g_underruns, g_underruns - g_underrun_said,
	        g_underrun_said_at ? t - g_underrun_said_at : 0L, ahead_frames());
	g_underrun_said_at = t;
	g_underrun_said = g_underruns;
}

/* The writer thread's output (ahead.h): every frame written, recovering from
 * an underrun on the way, then the output's queue measured for pcm_queued */
static long write_out(const int16_t *frames, int n)
{
	snd_pcm_sframes_t d = 0;
	const int all = n;

	while (g_pcm && n > 0) {
		long t0 = now_ms(), took;
		long w;

		g_write_since = t0;
		w = snd_pcm_writei(g_pcm, frames, (unsigned long)n);
		g_write_since = 0;

		/* A write is ~21 ms of audio into a 200 ms buffer, so a quarter of a
		 * second inside one is the output not taking it - noted, because a
		 * stall starts as exactly that. */
		if ((took = now_ms() - t0) > 250)
			pcm_note("write of %d frames took %ld ms (%s)", n, took,
			          w < 0 ? snd_strerror((int)w) : "ok");
		if (w < 0) {
			/* An underrun is ordinary - a pause, a slow card read - and
			 * recover re-prepares the stream. Anything else is not. */
			int r = snd_pcm_recover(g_pcm, (int)w, 1);

			if (w == -EPIPE) underrun();
			pcm_note("write: %s, recover %s", snd_strerror((int)w),
			         r < 0 ? snd_strerror(r) : "ok");
			if (r < 0) {
				snprintf(g_werr, sizeof g_werr, "write: %s", snd_strerror((int)w));
				return -1;
			}
			continue;
		}
		frames += w * 2;
		n -= (int)w;
		g_written += w;
	}
	if (!g_pcm) return -1;
	if (snd_pcm_delay(g_pcm, &d) < 0 || d < 0) d = 0;
	g_delay = d;
	g_delay_at = now_ms();
	return all;
}

/* Into the ring, decoded ahead; the writer thread takes it to the output */
bool pcm_write(const int16_t *frames, int n)
{
	if (!g_pcm) return false;
	if (!ahead_push(frames, n)) {
		snprintf(g_err, sizeof g_err, "%s", g_werr[0] ? g_werr : "write: the output stopped");
		return false;
	}
	return true;
}

void pcm_drop(void)
{
	if (!g_pcm) return;
	/* Nothing to drop on a device that has not been written to since it was
	 * opened or last prepared - and on a bluealsa headset a drop there is
	 * what stalled MP4. The drop makes bluealsa release the A2DP
	 * stream it acquired a millisecond earlier for the open, and the writes
	 * that follow wait for a stream it never takes again. All three stalls on
	 * 2026-09-25/26 were SINK then PLAY, the drop right behind the open; the
	 * third was caught in order in the syslog: Acquire, ACTIVE, the open,
	 * PLAY, drop, Release, IDLE, then a write that never returned. */
	ahead_hold();   /* what is decoded ahead goes too */
	if (!g_written) {
		pcm_note("drop skipped: nothing written since the open or prepare");
		ahead_release();
		return;
	}
	pcm_note("drop+prepare");
	snd_pcm_drop(g_pcm);
	snd_pcm_prepare(g_pcm);
	g_written = 0;
	g_delay = 0;
	ahead_release();
}

long pcm_queued(void)
{
	long d = g_delay, played;

	if (!g_pcm) return 0;
	/* The output's queue as the writer last measured it, less what has
	 * played since, and what is decoded ahead of it */
	played = g_delay_at ? (now_ms() - g_delay_at) * 48 : 0;
	d = d > played ? d - played : 0;
	return ahead_frames() + d;
}

long pcm_ahead(void) { return ahead_frames(); }

const char *pcm_error(void) { return g_err; }

bool pcm_is_open(void) { return g_pcm != NULL; }

bool pcm_busy(void) { return g_last == -EBUSY; }
