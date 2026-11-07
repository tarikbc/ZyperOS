/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Menu music's player. See src/bgm.h.
 *
 * One thread. It reads the folder - Roms/BGM, or ZyperOS's own tracks when
 * that has none (bgm_scan_either) - opens a track and the output when there is
 * music to play, decodes and writes a slice at a time - the write is what
 * paces it - and fades out and lets the output go when something holds it:
 * a game, Muse, the video player, the setting turned off. The track stays
 * open while held, so the music comes back where it stopped. The main thread
 * only sets a few fields under a lock and signals.
 *
 * The output is ALSA's "default", opened by name as the interface sounds'
 * is (src/pcm_out.c, which both use): the system's dmix, so the music mixes
 * with the sounds of a press without either knowing. At the mix's own 48 kHz,
 * every track converted to it here (bgm_rs, src/bgm.h), so ALSA's plug has
 * nothing to convert. On the desktop build it is SDL's default audio device
 * instead. */
#include "bgm.h"
#include "bgm_dec.h"
#include "pcm_out.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Frames decoded at a time: 23 ms at 44.1 kHz */
#define BGM_SLICE 1024
/* ...and what they come to at BGM_OUT_RATE, from as low as an 8 kHz MP3 */
#define BGM_OUT_MAX (BGM_SLICE * (BGM_OUT_RATE / 8000) + 8)
/* How much the output holds ahead of what is heard. Music needs no less, and
 * every period is a wakeup, so it is long; the fade out is heard after it. */
#define BGM_LATENCY_US 120000
/* A track comes in over this; a hold, a skip or Off fades it over this */
#define BGM_FADE_IN_MS  1200
#define BGM_FADE_OUT_MS 150
/* After an output would not open, or no track would decode, this long before
 * trying again */
#define BGM_RETRY_MS 3000

static struct {
	SDL_mutex  *lock;
	SDL_cond   *wake;
	SDL_Thread *thread;
	bool        quit, done;
	char        dir[512];
	char        builtin[512];  /* ZyperOS's tracks, "" for none */
	/* The settings, the main thread's to set */
	bool        on;
	int         volume;
	char        track[256];
	unsigned    hold;
	char        want[64];      /* the device; "" is nowhere */
	bool        left, right;   /* the sides that carry it */
	unsigned    skips;         /* bgm_skip()s and R2s asked for */
	unsigned    backs;         /* ...and L2s */
	bool        rescan;
	/* The thread's to set, the main thread's to read */
	bgm_list    list;
	char        from[512];     /* the folder list came from */
	bool        from_builtin;
	char        now[256];
	/* The last track begun that was news (bgm_song) */
	unsigned    song_seq;
	Uint32      song_at;
	char        song_file[256];
	bgm_tags    song_tags;
} g = { .on = true, .volume = BGM_VOLUME_DEFAULT, .hold = BGM_HOLD_START,
        .left = true, .right = true };

/* ---- the output ------------------------------------------------------------- */

/* On the desktop build, SDL's queue holds as much as ALSA's buffer would. */
static pcm_out g_pcm = {
	.who = "bgm", .rate = BGM_OUT_RATE, .buffer_us = BGM_LATENCY_US,
	.samples = 1024, .ahead = (int)((long long)BGM_OUT_RATE * BGM_LATENCY_US / 1000000),
	.poll_ms = 5, .tail_ms = 30,
};

/* ---- the thread ---------------------------------------------------------------- */

/* What the thread holds; only it touches these */
typedef struct {
	bgm_dec *dec;
	char     file[256];     /* the file dec is, "" for none */
	bgm_deck deck;
	bool     open;          /* the output */
	char     at[64];        /* ...on this device */
	bgm_rs   rs;            /* the track's rate to BGM_OUT_RATE */
	int      fade, fade_len;/* output frames into the fade in, of fade_len */
	int      gain;          /* the gain the last slice ended on */
	unsigned skips, backs;  /* g.skips and g.backs as last taken */
	int      steps;         /* presses taken and not yet acted on, R2s less L2s */
	bool     pressed;       /* ...any at all: R2 then L2 is this track again */
	bgm_hist hist;          /* what was heard, for L2 */
	char     last[256];     /* the file opened last, kept past its close */
	Uint32   retry_at;
} player;

/* The folder read into g.list. True when that is not the list it had: the
 * deck and what was heard are indices into it, and go with it. */
static bool scan(void)
{
	bgm_list l = { 0 };
	char dir[512], builtin[512];
	const char *from;
	bool same;
	int i;

	snprintf(dir, sizeof dir, "%s", g.dir);
	snprintf(builtin, sizeof builtin, "%s", g.builtin);
	SDL_UnlockMutex(g.lock);
	from = bgm_scan_either(dir, builtin[0] ? builtin : NULL, &l);
	SDL_LockMutex(g.lock);
	same = l.n == g.list.n && (from == builtin) == g.from_builtin;
	for (i = 0; same && i < l.n; i++) same = !strcmp(l.names[i], g.list.names[i]);
	bgm_free(&g.list);
	g.list = l;
	snprintf(g.from, sizeof g.from, "%s", from ? from : "");
	g.from_builtin = from == builtin;
	return !same;
}

static void close_track(player *p)
{
	bgm_dec_close(p->dec);
	p->dec = NULL;
	p->file[0] = '\0';
	g.now[0] = '\0';
}

static void close_output(player *p, bool drain)
{
	p->open = false;
	SDL_UnlockMutex(g.lock);
	pcm_out_close(&g_pcm, drain);
	SDL_LockMutex(g.lock);
}

/* The file the setting says should be playing, or NULL for the deck's next */
static const char *wanted_file(void)
{
	return g.track[0] && bgm_find(&g.list, g.track) >= 0 ? g.track : NULL;
}

/* R2s and L2s since the player last looked, added to those waiting. True
 * when there were any. */
static bool take_presses(player *p)
{
	if (g.skips == p->skips && g.backs == p->backs) return false;
	p->steps += (int)(g.skips - p->skips) - (int)(g.backs - p->backs);
	p->pressed = true;
	p->skips = g.skips;
	p->backs = g.backs;
	return true;
}

/* The track `steps` along in shuffle: back along what was heard, the one
 * playing again from its start when nothing was before it; forward along it
 * and then the deck, each track the deck deals heard (kept in the history)
 * whether it is played or passed. 0 is the one playing again. -1 for an
 * empty folder. */
static int pick(player *p, int steps)
{
	int k = bgm_hist_now(&p->hist);

	if (p->deck.n != g.list.n) {
		bgm_deck_free(&p->deck);
		bgm_deck_init(&p->deck, g.list.n,
		              (uint32_t)time(NULL) ^ SDL_GetTicks() ^ (uint32_t)getpid());
	}
	if (k >= g.list.n) k = -1;
	for (; steps < 0; steps++) {
		int b = bgm_hist_back(&p->hist);

		if (b < 0 || b >= g.list.n) break;
		k = b;
	}
	if (steps <= 0 && k >= 0) return k;
	if (steps <= 0) steps = 1;
	while (steps-- > 0) {
		int f = bgm_hist_fwd(&p->hist);

		if (f < 0 || f >= g.list.n) {
			if ((f = bgm_deck_next(&p->deck)) < 0) return -1;
			bgm_hist_push(&p->hist, f);
		}
		k = f;
	}
	return k;
}

/* A track begun that is news, for the pop-up */
static void announce(const char *file, const bgm_tags *t)
{
	g.song_seq++;
	g.song_at = SDL_GetTicks();
	snprintf(g.song_file, sizeof g.song_file, "%s", file);
	g.song_tags = *t;
}

/* Opens the next track: the one the setting names; in shuffle, as far along
 * as the presses waiting say (pick), or the next. `ended`: the track before
 * ended by itself, so the same one again is one track looping and no news.
 * Tries each track at most once before giving up for BGM_RETRY_MS. */
static bool open_track(player *p, bool ended)
{
	bool pressed = p->pressed;
	int steps = pressed ? p->steps : 1, tries;

	p->steps = 0;
	p->pressed = false;
	for (tries = 0; tries < g.list.n && !g.quit; tries++) {
		const char *want = wanted_file();
		char path[1024], file[256];
		bgm_tags tags;
		bgm_dec *d;
		int k;

		if (want) {
			snprintf(file, sizeof file, "%s", want);
			k = bgm_find(&g.list, want);
		} else {
			if ((k = pick(p, steps)) < 0) return false;
			snprintf(file, sizeof file, "%s", g.list.names[k]);
		}
		snprintf(path, sizeof path, "%s/%s", g.from, file);
		SDL_UnlockMutex(g.lock);
		d = bgm_dec_open(path, &tags);
		SDL_LockMutex(g.lock);
		if (d) {
			p->dec = d;
			snprintf(p->file, sizeof p->file, "%s", file);
			snprintf(g.now, sizeof g.now, "%s", file);
			p->fade = 0;
			p->fade_len = BGM_OUT_RATE * BGM_FADE_IN_MS / 1000;
			bgm_rs_init(&p->rs, bgm_dec_rate(d), BGM_OUT_RATE);
			/* One track is heard too, so L2 in shuffle after it comes
			 * back to it */
			if (want && k >= 0 && bgm_hist_now(&p->hist) != k) bgm_hist_push(&p->hist, k);
			if (pressed || !ended || strcmp(file, p->last)) announce(file, &tags);
			snprintf(p->last, sizeof p->last, "%s", file);
			return true;
		}
		fprintf(stderr, "bgm: cannot play %s\n", path);
		if (want) break;
		/* On past it, the way the presses were going */
		steps = steps < 0 ? -1 : 1;
	}
	return false;
}

/* One slice faded from the gain the last ended on down to nothing, written:
 * how a hold, a skip or Off ends what plays. Returns when the fade is over. */
/* What one slice comes to at BGM_OUT_RATE. Only this thread touches it. */
static int16_t g_out[2 * BGM_OUT_MAX];

/* `want` frames of the track decoded and brought to BGM_OUT_RATE in g_out,
 * the lock let go meanwhile: how many output frames, -1 at the end */
static int slice(player *p, int want)
{
	int16_t in[2 * BGM_SLICE];
	int n, m = 0;

	SDL_UnlockMutex(g.lock);
	n = bgm_dec_read(p->dec, in, want < BGM_SLICE ? want : BGM_SLICE);
	if (n > 0) m = bgm_rs_run(&p->rs, in, n, g_out, BGM_OUT_MAX);
	SDL_LockMutex(g.lock);
	return n > 0 ? m : -1;
}

static void fade_out(player *p)
{
	int left = BGM_OUT_RATE * BGM_FADE_OUT_MS / 1000;

	while (left > 0 && p->dec && p->open) {
		int n = slice(p, BGM_SLICE / 2), g0 = p->gain, g1;

		if (n < 0) break;
		if (n > left) n = left;
		g1 = (int)((long long)g0 * (left - n) / left);
		bgm_apply(g_out, n, g0, g1);
		if (g.left != g.right) bgm_one_side(g_out, n, g.left);
		p->gain = g1;
		left -= n;
		SDL_UnlockMutex(g.lock);
		pcm_out_write(&g_pcm, g_out, n);
		SDL_LockMutex(g.lock);
	}
	p->gain = 0;
	/* Heard from the start again, fading in */
	p->fade = 0;
}

static int run(void *unused)
{
	player p;

	(void)unused;
	memset(&p, 0, sizeof p);
	SDL_LockMutex(g.lock);
	scan();
	if (g.list.n) fprintf(stderr, "bgm: %d tracks in %s\n", g.list.n, g.from);
	while (!g.quit) {
		bool play = g.on && !g.hold && g.want[0] && g.list.n > 0;
		const char *want = wanted_file();
		bool pressed = take_presses(&p);
		/* Skipped or gone back, another track chosen, or the one playing
		 * gone from the folder. Shuffle chosen over one track lets that
		 * one finish. */
		bool change = p.dec && (pressed ||
		                        (want ? strcmp(want, p.file) != 0
		                              : bgm_find(&g.list, p.file) < 0));
		Uint32 now = SDL_GetTicks();
		int n, g0, g1;

		if (g.rescan) {
			g.rescan = false;
			/* The same folder keeps its deck and what was heard: MENU >
			 * Menu Music reads it again each time it opens */
			if (scan()) {
				bgm_deck_free(&p.deck);
				bgm_hist_clear(&p.hist);
			}
			continue;
		}
		if (!play || change) {
			if (p.open && p.dec && p.gain > 0) fade_out(&p);
			if (change) close_track(&p);
			if (!play) {
				if (p.open) close_output(&p, g.want[0] != '\0');
				/* Off lets the track go too: it starts again fresh */
				if (!g.on && p.dec) close_track(&p);
				if (!g.quit) SDL_CondWait(g.wake, g.lock);
				continue;
			}
		}
		if ((int)(now - p.retry_at) < 0) {
			SDL_CondWaitTimeout(g.wake, g.lock, p.retry_at - now);
			continue;
		}
		if (!p.dec && !open_track(&p, false)) {
			p.retry_at = SDL_GetTicks() + BGM_RETRY_MS;
			continue;
		}
		/* The route moved */
		if (p.open && strcmp(p.at, g.want)) close_output(&p, false);
		if (!p.open) {
			char dev[64];
			bool ok;

			snprintf(dev, sizeof dev, "%s", g.want);
			SDL_UnlockMutex(g.lock);
			ok = pcm_out_open(&g_pcm, dev);
			SDL_LockMutex(g.lock);
			if (!ok) {
				p.retry_at = SDL_GetTicks() + BGM_RETRY_MS;
				continue;
			}
			p.open = true;
			snprintf(p.at, sizeof p.at, "%s", dev);
			p.gain = 0;
		}
		n = slice(&p, BGM_SLICE);
		if (n < 0) {
			/* The end: the same track again on one track, the deck's next on
			 * shuffle, and no fade for either - the track ended itself */
			close_track(&p);
			if (!open_track(&p, true)) p.retry_at = SDL_GetTicks() + BGM_RETRY_MS;
			else p.fade = p.fade_len;   /* straight on, at full */
			continue;
		}
		if (n == 0) continue;
		g0 = p.gain;
		if (p.fade < p.fade_len) p.fade += n;
		g1 = bgm_gain(g.volume, p.fade, p.fade_len);
		bgm_apply(g_out, n, g0, g1);
		if (g.left != g.right) bgm_one_side(g_out, n, g.left);
		p.gain = g1;
		SDL_UnlockMutex(g.lock);
		n = pcm_out_write(&g_pcm, g_out, n) ? 0 : -1;
		SDL_LockMutex(g.lock);
		if (n < 0) {
			close_output(&p, false);
			p.retry_at = SDL_GetTicks() + BGM_RETRY_MS;
		}
	}
	if (p.open) close_output(&p, false);
	close_track(&p);
	bgm_deck_free(&p.deck);
	g.done = true;
	SDL_UnlockMutex(g.lock);
	return 0;
}

/* ---- the main thread's side -------------------------------------------------------- */

void bgm_init(const char *dir, const char *builtin)
{
	if (g.thread) return;
#ifndef __linux__
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		fprintf(stderr, "bgm: %s\n", SDL_GetError());
#endif
	snprintf(g.dir, sizeof g.dir, "%s", dir);
	snprintf(g.builtin, sizeof g.builtin, "%s", builtin ? builtin : "");
	g.lock = SDL_CreateMutex();
	g.wake = SDL_CreateCond();
	if (!g.lock || !g.wake ||
	    !(g.thread = SDL_CreateThread(run, "zyperos-bgm", NULL)))
		fprintf(stderr, "bgm: no player: %s\n", SDL_GetError());
}

/* A moment for the thread to let the output go, and no longer: the exit is
 * what starts a port, and a device that will not finish a write must not
 * hold that up. src/uisound_out.c's rule. */
void bgm_quit(void)
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

/* Every setter: the field under the lock, and the thread woken */
#define SET(stmt) do {                                   \
	if (g.lock) SDL_LockMutex(g.lock);               \
	stmt;                                            \
	if (g.lock) { SDL_CondSignal(g.wake); SDL_UnlockMutex(g.lock); } \
} while (0)

void bgm_set_on(bool on)              { SET(g.on = on); }
void bgm_set_volume(int volume)       { SET(g.volume = bgm_volume_step(volume, 0)); }
void bgm_set_track(const char *file)  { SET(snprintf(g.track, sizeof g.track, "%s", file ? file : "")); }
void bgm_skip(void)                   { SET(g.skips++); }

bool bgm_step(int dir, char *track, size_t n)
{
	bool moved = false;
	int at, k;

	if (n) track[0] = '\0';
	if (!g.lock) return false;
	SDL_LockMutex(g.lock);
	at = g.track[0] ? bgm_find(&g.list, g.track) : -1;
	k = at >= 0 ? bgm_file_step(at, g.list.n, dir < 0 ? -1 : 1) : -1;
	if (k >= 0 && k != at) {
		snprintf(g.track, sizeof g.track, "%s", g.list.names[k]);
		snprintf(track, n, "%s", g.track);
		moved = true;
	} else if (dir < 0 && at < 0) {
		g.backs++;
	} else {
		/* Shuffle's next; or the one track of a folder of one, again */
		g.skips++;
	}
	SDL_CondSignal(g.wake);
	SDL_UnlockMutex(g.lock);
	return moved;
}

bool bgm_audible(void)
{
	bool a;

	if (!g.lock) return false;
	SDL_LockMutex(g.lock);
	a = g.on && !g.hold && g.want[0] && g.list.n > 0;
	SDL_UnlockMutex(g.lock);
	return a;
}

void bgm_song_now(bgm_song *s)
{
	memset(s, 0, sizeof *s);
	if (!g.lock) return;
	SDL_LockMutex(g.lock);
	s->seq = g.song_seq;
	s->at = g.song_at;
	snprintf(s->file, sizeof s->file, "%s", g.song_file);
	s->tags = g.song_tags;
	SDL_UnlockMutex(g.lock);
}
void bgm_rescan(void)                 { SET(g.rescan = true); }
/* Asked on every pass of the shelf's loop (aout_apply), so the thread is told
 * only of a change: told every time, it woke 65 times a second to find the
 * same device (2026-11-07). uisound_route does the same. */
void bgm_route(const char *device, bool left, bool right)
{
	const char *want = device ? device : "";

	if (g.lock) SDL_LockMutex(g.lock);
	if (strcmp(g.want, want) || g.left != left || g.right != right) {
		snprintf(g.want, sizeof g.want, "%s", want);
		g.left = left;
		g.right = right;
		if (g.lock) SDL_CondSignal(g.wake);
	}
	if (g.lock) SDL_UnlockMutex(g.lock);
}

bool        bgm_on(void)     { return g.on; }
int         bgm_volume(void) { return g.volume; }
const char *bgm_track(void)  { return g.track; }

void bgm_hold(unsigned why, bool held)
{
	unsigned h;

	if (!g.lock) { g.hold = held ? g.hold | why : g.hold & ~why; return; }
	SDL_LockMutex(g.lock);
	h = held ? g.hold | why : g.hold & ~why;
	if (h != g.hold) {
		g.hold = h;
		SDL_CondSignal(g.wake);
	}
	SDL_UnlockMutex(g.lock);
}

int bgm_count(void)
{
	int n;

	if (!g.lock) return 0;
	SDL_LockMutex(g.lock);
	n = g.list.n;
	SDL_UnlockMutex(g.lock);
	return n;
}

void bgm_now(char *out, size_t n)
{
	if (!n) return;
	out[0] = '\0';
	if (!g.lock) return;
	SDL_LockMutex(g.lock);
	snprintf(out, n, "%s", g.now);
	SDL_UnlockMutex(g.lock);
}
