/* SPDX-License-Identifier: GPL-3.0-only */
/* The interface sounds' player. See src/uisound.h.
 *
 * One thread, which reads the voices, opens the output when there is a sound
 * to play, mixes what is playing a slice at a time and writes it, and lets
 * the output go when nothing has played for a while. The main thread only
 * adds a sound to a list under a lock and signals: a press never waits on the
 * device, on a file, or on the sound before it.
 *
 * The output on a device is ALSA, opened by name like Muse's (src/muse/pcm.c)
 * and menu music's (src/pcm_out.c, which both use): "default", which is the
 * system's dmix in front of the codec, so these mix with Muse and with a
 * paused game without either knowing, or the DAC's own mixed device. It never
 * touches a mixer control: the volume keys, the mute switch and the DAC's
 * software volume all sit below the mix, so they set these sounds as they set
 * everything else. On the desktop build it is SDL's default audio device
 * instead, so the host build is heard too. */
#include "uisound.h"

#include "fileio.h"
#include "pcm_out.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Sounds at once. A press that would be a ninth drops the oldest. */
#define UIS_VOICES 8
/* A slice: 10 ms, mixed and written at a time. */
#define UIS_SLICE (UIS_RATE / 100)
/* How far ahead of what is heard the player writes. A sound started while
 * another plays joins it after this much, so it is kept short; the output's
 * own buffer may be longer, and is not filled. */
#define UIS_AHEAD (UIS_RATE * 30 / 1000)
/* How long the output stays open with nothing playing. Opening is the slow
 * part of a sound (dmix sets up its shared buffer, the codec may power up),
 * so a run of presses keeps it; an open output with nothing to say still
 * keeps the codec running, so a quiet launcher lets it go. */
#define UIS_LINGER_MS 10000
/* After an output would not open, the sounds asked for in this long are
 * dropped rather than each trying again. */
#define UIS_RETRY_MS 2000

static struct {
	SDL_mutex  *lock;
	SDL_cond   *wake;      /* a sound, a route, a release, quit */
	SDL_Thread *thread;
	bool        quit, done;
	bool        release;   /* close as soon as nothing plays; a sound clears it */
	char        dir[512];
	int16_t    *pcm[DS_VOICES][DS_SOUNDS];
	int         frames[DS_VOICES][DS_SOUNDS];
	int         setting;
	uis_voice   v[UIS_VOICES];
	int         nv;
	/* The device to play on; "" is nowhere, which is where the player
	 * starts: until the launcher has said where the sound goes, a sound
	 * could reach the speaker past a headset, or the codec past a DAC. */
	char        want[64];
	bool        left, right;
	bool        open;      /* the output is open; the thread's to change */
} g = { .setting = DS_VOICE_DEFAULT, .left = true, .right = true };

/* ---- the output ------------------------------------------------------------ */

/* 40 ms of buffer asked for; dmix may give more, which UIS_AHEAD keeps
 * unfilled. Started on the first slice written, not once that buffer is full:
 * a tick is shorter than it, and would wait for the next sound to be heard. */
static pcm_out g_pcm = {
	.who = "uisound", .rate = UIS_RATE, .buffer_us = 40000, .start = UIS_SLICE,
	.samples = 512, .ahead = UIS_AHEAD, .poll_ms = 2, .tail_ms = 20,
};

static bool out_write(const int16_t *buf, int frames)
{
	long ahead = pcm_out_delay(&g_pcm);

	/* Hold back to UIS_AHEAD of what is heard. A stream that ran dry while
	 * nothing played cannot say, which is not a reason to wait; the write
	 * brings it back. SDL's writes wait by g_pcm.ahead instead. */
	if (ahead > UIS_AHEAD)
		usleep((useconds_t)((ahead - UIS_AHEAD) * 1000000LL / UIS_RATE));
	return pcm_out_write(&g_pcm, buf, frames);
}

/* ---- the thread -------------------------------------------------------------- */

static int16_t *read_wav(const char *path, int *frames)
{
	size_t n;
	char *buf = file_read(path, 1L << 22, &n);
	int16_t *pcm = NULL;

	*frames = 0;
	if (buf) pcm = uisound_wav((const uint8_t *)buf, n, frames);
	free(buf);
	return pcm;
}

/* Every voice, read once: they are a few hundred kilobytes all told, and a
 * voice changed in the menu is then heard on the press that changes it. */
static void load(void)
{
	char path[640];
	int v, s, missing = 0;

	for (v = 0; v < DS_VOICES; v++)
		for (s = 0; s < DS_SOUNDS; s++) {
			int16_t *pcm;
			int n;

			snprintf(path, sizeof path, "%s/%s/%s.wav", g.dir, DS_VOICE_ID[v],
			         DS_SOUND_ID[s]);
			pcm = read_wav(path, &n);
			if (!pcm) missing++;
			SDL_LockMutex(g.lock);
			g.pcm[v][s] = pcm;
			g.frames[v][s] = n;
			SDL_UnlockMutex(g.lock);
		}
	if (missing)
		fprintf(stderr, "uisound: %d of %d sounds missing or unreadable in %s\n",
		        missing, DS_VOICES * DS_SOUNDS, g.dir);
}

/* Let the output go, with the lock held on the way in and out. `drain`: let
 * what was written be heard first. A close drops it, and the last of a sound
 * is still in the buffer when the mix runs out - the end of A's accept, cut
 * off at every launch. Not for a move to another device, where the rest
 * would be heard in the wrong place anyway. */
static void close_output(bool drain)
{
	g.open = false;
	SDL_UnlockMutex(g.lock);
	pcm_out_close(&g_pcm, drain);
	SDL_LockMutex(g.lock);
}

static int player(void *unused)
{
	int16_t slice[2 * UIS_SLICE];
	char at[64] = "";
	Uint32 quiet_since = 0, retry_at = 0;

	(void)unused;
	load();
	SDL_LockMutex(g.lock);
	while (!g.quit) {
		Uint32 now = SDL_GetTicks();
		bool ok;

		if (!g.want[0]) g.nv = 0;                 /* nowhere to play */
		if (!g.nv) {
			if (g.open && (g.release || !g.want[0] ||
			               now - quiet_since >= UIS_LINGER_MS)) {
				close_output(g.want[0] != '\0');
				continue;
			}
			if (!g.open) {
				SDL_CondWait(g.wake, g.lock);
			} else {
				SDL_CondWaitTimeout(g.wake, g.lock,
				                    UIS_LINGER_MS - (now - quiet_since));
			}
			continue;
		}
		/* The route moved - a cable in, a DAC plugged - while open. */
		if (g.open && strcmp(at, g.want)) close_output(false);
		if (!g.open) {
			char dev[64];

			if ((int)(now - retry_at) < 0) { g.nv = 0; continue; }
			snprintf(dev, sizeof dev, "%s", g.want);
			SDL_UnlockMutex(g.lock);
			ok = pcm_out_open(&g_pcm, dev);
			SDL_LockMutex(g.lock);
			if (!ok) {
				retry_at = SDL_GetTicks() + UIS_RETRY_MS;
				g.nv = 0;
				continue;
			}
			g.open = true;
			snprintf(at, sizeof at, "%s", dev);
		}
		g.nv = uisound_mix(g.v, g.nv, slice, UIS_SLICE, g.left, g.right);
		SDL_UnlockMutex(g.lock);
		ok = out_write(slice, UIS_SLICE);
		SDL_LockMutex(g.lock);
		quiet_since = SDL_GetTicks();
		if (!ok) {
			close_output(false);
			retry_at = SDL_GetTicks() + UIS_RETRY_MS;
		}
	}
	if (g.open) close_output(false);
	g.done = true;
	SDL_UnlockMutex(g.lock);
	return 0;
}

/* ---- the main thread's side ------------------------------------------------------ */

void uisound_init(const char *dir)
{
	if (g.thread) return;
#ifndef __linux__
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		fprintf(stderr, "uisound: %s\n", SDL_GetError());
#endif
	snprintf(g.dir, sizeof g.dir, "%s", dir);
	g.lock = SDL_CreateMutex();
	g.wake = SDL_CreateCond();
	if (!g.lock || !g.wake ||
	    !(g.thread = SDL_CreateThread(player, "zyperos-uisound", NULL)))
		fprintf(stderr, "uisound: no player: %s\n", SDL_GetError());
}

/* Waits a moment for the player to let the output go, and no longer: the
 * launcher's exit is what starts a port, and a device that will not finish a
 * write must not hold that up. A player still busy is left to the exit. */
void uisound_quit(void)
{
	Uint32 until = SDL_GetTicks() + 300;
	bool done;

	if (!g.thread) return;
	SDL_LockMutex(g.lock);
	g.quit = true;
	SDL_CondSignal(g.wake);
	while (!(done = g.done) && (int)(until - SDL_GetTicks()) > 0) {
		SDL_UnlockMutex(g.lock);
		SDL_Delay(5);
		SDL_LockMutex(g.lock);
	}
	SDL_UnlockMutex(g.lock);
	if (done) SDL_WaitThread(g.thread, NULL);
	else      SDL_DetachThread(g.thread);
	g.thread = NULL;
}

void uisound_set(int setting)
{
	if (setting < 0 || setting > UIS_OFF) setting = DS_VOICE_DEFAULT;
	if (!g.lock) { g.setting = setting; return; }
	SDL_LockMutex(g.lock);
	g.setting = setting;
	SDL_UnlockMutex(g.lock);
}

int uisound_setting(void)
{
	return g.setting;
}

void uisound_cue(uis_cue c)
{
	uis_voice x;

	if (!g.thread || c.level <= 0.0f || (int)c.sound < 0 || c.sound >= DS_SOUNDS)
		return;
	SDL_LockMutex(g.lock);
	if (g.setting < DS_VOICES && g.want[0] && g.pcm[g.setting][c.sound]) {
		x.pcm    = g.pcm[g.setting][c.sound];
		x.frames = g.frames[g.setting][c.sound];
		x.pos    = 0;
		x.gain   = (int)(c.level * 32768.0f + 0.5f);
		if (g.nv == UIS_VOICES) {
			memmove(g.v, g.v + 1, (UIS_VOICES - 1) * sizeof *g.v);
			g.nv--;
		}
		g.v[g.nv++] = x;
		g.release = false;
		SDL_CondSignal(g.wake);
	}
	SDL_UnlockMutex(g.lock);
}

void uisound_play(ds_sound s)
{
	uisound_cue((uis_cue){ s, 1.0f });
}

void uisound_route(const char *device, bool left, bool right)
{
	const char *want = device ? device : "";

	if (!g.thread) return;
	SDL_LockMutex(g.lock);
	if (strcmp(g.want, want) || g.left != left || g.right != right) {
		snprintf(g.want, sizeof g.want, "%s", want);
		g.left = left;
		g.right = right;
		SDL_CondSignal(g.wake);
	}
	SDL_UnlockMutex(g.lock);
}

void uisound_release(void)
{
	if (!g.thread) return;
	SDL_LockMutex(g.lock);
	g.release = true;
	SDL_CondSignal(g.wake);
	SDL_UnlockMutex(g.lock);
}
