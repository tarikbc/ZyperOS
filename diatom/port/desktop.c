/* Desktop port - SDL2.
 *
 * Built FIRST, before any device backend, and that ordering is deliberate: an
 * interface with only one implementation behind it grows that implementation's
 * assumptions no matter how carefully it is written. Two real backends is what
 * makes the seam honest.
 *
 * This file does not include libretro.h and must never need to. If it ever
 * does, the boundary is drawn in the wrong place.
 */
/* <SDL.h>, not <SDL2/SDL.h>: sdl2-config and pkg-config both put the SDL2
 * directory on the include path, and this is the form SDL documents. */
#include <SDL.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "diatom_deskfb.h"
#include "diatom_port.h"
#include "port_clock.h"
#include "port_power.h"

#define WINDOW_W 960
#define WINDOW_H 720
#define AUDIO_RATE 48000
/* Capacity in FRAMES (one frame = two int16 samples). 4096 at 48kHz is ~85ms,
 * with rate control aiming to hold it near half that. */
#define AUDIO_BUFFER_FRAMES 4096
#define AUDIO_FRAME_BYTES   (2 * (int)sizeof(int16_t))
/* What SDL takes from the queue at once, a device period (audio_open) */
#define AUDIO_PERIOD_FRAMES 1024

static int           g_win_w = WINDOW_W, g_win_h = WINDOW_H;
static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture  *g_texture;
static int           g_tex_w, g_tex_h;
static Uint32        g_tex_fmt;
static diatom_filter g_tex_filter;
static SDL_AudioDeviceID g_audio;
static bool          g_quit;
static uint32_t      g_buttons;
static uint32_t      g_pressed;     /* went down during the last poll */

/* DIATOM_TEST_CLOCK: a clock of the port's own, for the checks of the
 * pacing (test/displaylock.py, test/lateskip.py and the others that set it).
 *
 * On the wall clock a check of the pacing judges this computer as much as
 * the pacing: make check runs a dozen checks at once beside other builds, a
 * loop starts its frames late, and a rate or a frame left out says nothing
 * of the rule. The merge gate's load of 12 had the display lock's run at
 * 59.66 fps, slower than the unlocked 59.71 (2026-11-24), and a fast frame
 * of frameskip=steady's left undrawn. So with this set, time moves only when
 * something spends it:
 *
 *   - diatom_port_sleep_us, the frame loop's sleep to its next turn and a
 *     held push's wait for room (src/audio.c), moves it on by the sleep
 *   - the stub core's frame (STUBCORE_FRAME_US and the rest) moves it on by
 *     its cost, through diatom_test_clock_spend, which it looks up by name
 *     (test/stubcore.c); a stretch of frames slower than a refresh is then
 *     exactly that slow
 *
 * Nothing else takes time: presenting, the socket and the core's own work
 * are free, so the same run gives the same frames, the same skips and the
 * same rate on an idle Mac, a loaded one or CI.
 *
 * The sound device is the clock's too, as SDL's dummy device plays on the
 * wall clock: a queue of AUDIO_BUFFER_FRAMES, a period of AUDIO_PERIOD_FRAMES
 * taken from it at once every period at AUDIO_RATE, what it does not hold
 * then the device's silence, and what does not fit refused, as SDL's queue
 * is. Written and read on the frame loop's thread alone: a core that makes
 * its sound on a thread of its own is no check's.
 *
 * A busy machine and a device of its own mind, by fixed sequences, the same
 * every run: the value is SLEEP[:LATE[:PPM]], each 0 when left out. Each
 * sleep runs 0 to SLEEP microseconds over, which the absolute schedule
 * (main.c) has to absorb, and which a sound-paced core's rule must not count
 * as its frames' lateness (main.c, sound_debt_us). Each of the device's
 * periods is taken 0 to LATE microseconds late (less than a period), as its
 * thread wakes late, and the device plays PPM parts in a million slower
 * than AUDIO_RATE: SDL's dummy device took its periods 22.5 ms apart on a
 * Mac where 21.3 are due (2026-11-24), 5% slow, past what rate control
 * follows, so a sound-paced core's pushes wait for room (src/audio.c,
 * HOLD). The clock starts at 1000 s, not 0, which some timers read as
 * never. */
static int      g_tclock = -1;       /* -1 not read yet, 0 off, 1 on */
static uint64_t g_tclock_us = 1000000000ull;
static long     g_tclock_over;       /* the most a sleep overshoots, us */
static long     g_tclock_dev_late;   /* the most a period is taken late, us */
static long     g_tclock_dev_ppm;    /* how much slower the device plays, ppm */

static bool tclock_on(void)
{
	if (g_tclock < 0) {
		const char *e = getenv("DIATOM_TEST_CLOCK");
		const long period = AUDIO_PERIOD_FRAMES * 1000000L / AUDIO_RATE;
		char *end = NULL;

		g_tclock = e != NULL;
		g_tclock_over = e && *e ? strtol(e, &end, 10) : 0;
		g_tclock_dev_late = end && *end == ':' ? strtol(end + 1, &end, 10) : 0;
		g_tclock_dev_ppm = end && *end == ':' ? strtol(end + 1, NULL, 10) : 0;
		if (g_tclock_over < 0) g_tclock_over = 0;
		if (g_tclock_dev_late < 0) g_tclock_dev_late = 0;
		if (g_tclock_dev_late >= period) g_tclock_dev_late = period - 1;
	}
	return g_tclock == 1;
}

/* For the stub core, by name (dlsym): `us` of its frame's work */
void diatom_test_clock_spend(uint64_t us);
void diatom_test_clock_spend(uint64_t us)
{
	__atomic_fetch_add(&g_tclock_us, us, __ATOMIC_RELAXED);
}

/* A fixed sequence's k-th number, 0 to most */
static uint64_t tclock_pick(uint64_t k, long most)
{
	uint64_t x = (k + 1) * 0x9E3779B97F4A7C15ull;

	if (most <= 0) return 0;
	x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29;
	return x % (uint64_t)(most + 1);
}

/* The test clock's sound device: what it holds, in frames, and how many of
 * its periods it has taken since it opened, the k-th due k periods after
 * the opening and 0 to DEVICE late */
static bool     g_vaudio;
static size_t   g_vq;
static uint64_t g_vq_open, g_vq_periods;

static void vaudio_play(void)
{
	const uint64_t now = __atomic_load_n(&g_tclock_us, __ATOMIC_RELAXED);

	for (;;) {
		const uint64_t k = g_vq_periods + 1;
		const uint64_t at = g_vq_open +
		                    (uint64_t)((double)k * AUDIO_PERIOD_FRAMES * 1e6 / AUDIO_RATE *
		                               (1.0 + g_tclock_dev_ppm / 1e6)) +
		                    tclock_pick(k, g_tclock_dev_late);

		if (at > now) break;
		g_vq = g_vq > AUDIO_PERIOD_FRAMES ? g_vq - AUDIO_PERIOD_FRAMES : 0;
		g_vq_periods = k;
	}
}

/* DIATOM_DESKFB (diatom_deskfb.h): the frames into a file ZyperOS's desk mode
 * shows in its own window, the buttons from it */
static diatom_deskfb *g_fb;

static void deskfb_open(const char *path, int w, int h)
{
	size_t bytes = DIATOM_DESKFB_BYTES(w, h);
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	void *m;

	if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0) {
		fprintf(stderr, "desktop: no desk frame file %s\n", path);
		if (fd >= 0) close(fd);
		return;
	}
	m = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return;
	g_fb = m;
	memset(g_fb, 0, sizeof *g_fb);
	g_fb->w = (uint32_t)w;
	g_fb->h = (uint32_t)h;
	g_fb->version = DIATOM_DESKFB_VERSION;
	__atomic_store_n(&g_fb->magic, DIATOM_DESKFB_MAGIC, __ATOMIC_RELEASE);
	fprintf(stderr, "desktop: frames to %s, %dx%d\n", path, w, h);
}

/* What was drawn, into the file, whole frames only */
static void deskfb_frame(void)
{
	uint32_t seq;

	if (!g_fb) return;
	seq = __atomic_load_n(&g_fb->seq, __ATOMIC_RELAXED);
	__atomic_store_n(&g_fb->seq, seq | 1u, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, g_fb + 1, (int)g_fb->w * 4);
	g_fb->polled = g_buttons;
	g_fb->frames++;
	__atomic_store_n(&g_fb->seq, (seq | 1u) + 1u, __ATOMIC_RELEASE);
}

/* ---------- the audio sink ------------------------------------------------ */

/* Empty means the default device. */
static char g_audio_dev[128];

/* Closing an SDL audio device JOINS its audio thread, and that thread can be
 * blocked writing to a sink that has stopped draining. So this call can fail to
 * return, and on 2026-09-05 a stalled A2DP sink did exactly that.
 *
 * The obvious fix - close on a detached thread - was tried and REVERTED the
 * same day. SDL is not safe against opening a device while a close of another
 * is in flight: test/port_test.c fails three ways with it, the first being that
 * the fallback open after a refused device stops working. A frontend that
 * cannot reopen its audio is worse than one that can hang on a dead headset.
 *
 * The hang is also not really here. The frame loop calls SDL_QueueAudio and
 * SDL_GetQueuedAudioSize every frame, and those take the same device lock the
 * audio thread holds while it writes - so a stalled sink stops the loop whether
 * or not anything closes. Fixing that means not letting the write stall, which
 * is the sink question, not the teardown question.
 */
static bool audio_open(const char *name)
{
	SDL_AudioSpec want, have;

	if (g_audio) { SDL_CloseAudioDevice(g_audio); g_audio = 0; }
	if (tclock_on()) {
		/* The test clock's own device, empty, and nothing of SDL's */
		g_vaudio = true;
		g_vq = 0;
		g_vq_open = __atomic_load_n(&g_tclock_us, __ATOMIC_RELAXED);
		g_vq_periods = 0;
		diatom_port_log(DIATOM_LOG_INFO, "audio: the test clock's device (DIATOM_TEST_CLOCK)");
		return true;
	}

	SDL_memset(&want, 0, sizeof want);
	want.freq     = AUDIO_RATE;
	want.format   = AUDIO_S16SYS;
	want.channels = 2;
	want.samples  = AUDIO_PERIOD_FRAMES;

	if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		return false;
	/* allowed_changes 0: SDL converts behind this spec, so caps.audio_rate
	 * stays true across a reopen. */
	g_audio = SDL_OpenAudioDevice(name && *name ? name : NULL, 0,
	                              &want, &have, 0);
	if (!g_audio) return false;
	SDL_PauseAudioDevice(g_audio, 0);
	/* What SDL actually negotiated, not what was asked for. A device can open
	 * cleanly and then carry no sound - that is exactly what a bluealsa sink
	 * did on 2026-09-05 - and without this there is no way to tell from
	 * outside whether SDL agreed to something the device cannot service. */
	{
		char msg[192];

		snprintf(msg, sizeof msg,
		         "audio: opened %s at %d Hz, %d ch, %d frames/period, %u byte buffer",
		         name && *name ? name : "the default device",
		         have.freq, have.channels, have.samples, have.size);
		diatom_port_log(DIATOM_LOG_INFO, msg);
	}
	return true;
}

bool diatom_port_audio_set(const char *name, char *actual, size_t cap)
{
	const char *want = name ? name : "";
	bool ok;

	/* Already there. A write on the state plane is idempotent, so re-stating
	 * the current device must not close and reopen a working sink: that drops
	 * whatever is queued and costs a fresh A2DP stream setup. Repeated writes
	 * are not hypothetical - a launcher that recomputes its routing on a timer
	 * sends one whenever it thinks the answer might have moved. */
	if ((g_audio || g_vaudio) && !strcmp(want, g_audio_dev)) {
		diatom_port_audio_get(actual, cap);
		return true;
	}

	ok = audio_open(name);

	if (!ok) {
		char msg[192];

		snprintf(msg, sizeof msg, "audio: %s unavailable (%s); using the default",
		         name && *name ? name : "the default device", SDL_GetError());
		diatom_port_log(DIATOM_LOG_WARN, msg);
		if (audio_open(NULL)) g_audio_dev[0] = '\0';
	} else {
		snprintf(g_audio_dev, sizeof g_audio_dev, "%s", name ? name : "");
	}
	diatom_port_audio_get(actual, cap);
	return ok;
}

/* Not at init: see diatom_port_audio_start in diatom_port.h. NOT fatal when
 * it fails. A port that cannot open a sound device can still put pixels where
 * the device wants them, and silence costs far less than no emulator: the
 * write path drops, and the host configures the session with capacity 0, which
 * turns dynamic rate control off rather than leaving it correcting for a
 * buffer that does not exist. Tried again at the next start, which costs
 * nothing when it fails. */
bool diatom_port_audio_start(void)
{
	if (g_audio || g_vaudio) return true;
	if (audio_open(NULL)) {
		g_audio_dev[0] = '\0';
		return true;
	}
	fprintf(stderr, "audio unavailable (%s); continuing without sound\n", SDL_GetError());
	return false;
}

void diatom_port_audio_get(char *out, size_t cap)
{
	if (out && cap) snprintf(out, cap, "%s", g_audio_dev);
}

/* No motor here. DIATOM_RUMBLE_LOG=path is a pretend one for a test: each
 * level the host asks of it, a line "strong weak" in that file, in the order
 * asked (test/rumble.py), and the time after them with DIATOM_RUMBLE_LOG_US. From any thread, as the port contract says, so
 * under a lock. */
static pthread_mutex_t g_rumble_mx = PTHREAD_MUTEX_INITIALIZER;
static FILE           *g_rumble_log;

static bool rumble_log_open(void)
{
	const char *e = getenv("DIATOM_RUMBLE_LOG");

	pthread_mutex_lock(&g_rumble_mx);
	if (!g_rumble_log && e && *e) g_rumble_log = fopen(e, "w");
	pthread_mutex_unlock(&g_rumble_mx);
	return g_rumble_log != NULL;
}

bool diatom_port_init(diatom_port_caps *out)
{

	/* Audio is initialised SEPARATELY and is allowed to fail. A subsystem that
	 * will not come up must not take the rest of the port with it - see the
	 * open below for why silence beats refusing to start. Rolled into one
	 * SDL_Init, a missing or busy audio device is indistinguishable from a
	 * missing display and kills both. */
	/* Behind ZyperOS's desk mode (DIATOM_DESKFB below) this process shows
	 * nothing of its own: no Dock icon on a Mac, never the keyboard's focus,
	 * which stays on the launcher's window */
	if (getenv("DIATOM_DESKFB")) SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}

	/* DIATOM_SURFACE=640x480 makes the window a device's size, so a layout
	 * worked out for the GKD Pixel 2's panel can be seen, and captured, at
	 * the size it was worked out for. Anything else is the default. */
	{
		const char *e = getenv("DIATOM_SURFACE");
		int w, h;

		if (e && sscanf(e, "%dx%d", &w, &h) == 2 && w >= 160 && h >= 120 &&
		    w <= 4096 && h <= 4096) {
			g_win_w = w;
			g_win_h = h;
		}
	}
	/* In ZyperOS's desk mode the window is never shown: its frames go to
	 * the launcher's (diatom_deskfb.h), drawn by SDL's software renderer,
	 * whose pixels read back at the cost of a copy */
	const char *deskfb = getenv("DIATOM_DESKFB");
	bool desk = deskfb && *deskfb;

	g_window = SDL_CreateWindow("diatom",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		g_win_w, g_win_h, desk ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
	if (!g_window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }

	g_renderer = SDL_CreateRenderer(g_window, -1, desk ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED);
	if (!g_renderer) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
	SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
	if (desk) deskfb_open(deskfb, g_win_w, g_win_h);

	/* Vsync OFF, deliberately.
	 *
	 * No console runs at the panel's rate - measured 50.0070, 59.7275, 59.8200,
	 * 60.0000 across six cores. Blocking on a 60Hz vblank while trying to hold
	 * 59.7275 leaves 0.07ms of slack per frame, so any jitter misses a vblank
	 * and costs a whole 16.67ms. That measured as a consistent 1.4% deficit.
	 *
	 * The host paces against a monotonic clock instead, and audio drift is
	 * absorbed by rate control rather than by hoping the panel agrees. */
	if (SDL_RenderSetVSync(g_renderer, 0) != 0)
		fprintf(stderr, "note: could not disable vsync: %s\n", SDL_GetError());

	/* NOT fatal. A port that cannot open a sound device can still put pixels
	 * where the device wants them, and refusing to start costs far more than
	 * silence does: Diatom is resident, so a failed open here leaves the
	 * launcher with no emulator at all and every launch then pays the cold
	 * cost (~1100 ms) instead of the warm one (~15 ms). That is the worst
	 * outcome available and it happened by accident - and a sink chosen at
	 * runtime needs this to be true, since anything settable while a game
	 * runs can fail while a game runs.
	 *
	 * capacity 0 is how the silence is reported rather than hidden: it turns
	 * off dynamic rate control, which would otherwise read a permanently empty
	 * queue as a permanent deficit and hold the resampler at its deviation
	 * limit forever, correcting for a buffer that does not exist. */
	/* No audio here: it opens once the launcher has drawn, or at the first
	 * game; see diatom_port_audio_start. */

	out->surface_w          = g_win_w;
	out->surface_h          = g_win_h;
	/* `have` is untouched when the open failed, so the rate comes from what
	 * was asked for: the resampler still needs a target to convert into,
	 * and a zero here would divide. */
	out->audio_rate         = AUDIO_RATE;
	/* No panel rate of its own here: the display lock is off, as the
	 * desktop window has no flips to time. DIATOM_REFRESH_HZ gives one, for
	 * the lock's own check (test/displaylock.py). */
	out->refresh_hz         = getenv("DIATOM_REFRESH_HZ") ? atof(getenv("DIATOM_REFRESH_HZ")) : 0;
	out->audio_buffer_frames = AUDIO_BUFFER_FRAMES;   /* open or not yet */
	out->present_blocks     = false;
	out->gpu                = false;
	/* No motor, unless a test asks for one to read (DIATOM_RUMBLE_LOG). */
	out->rumble             = rumble_log_open();
	return true;
}

void diatom_port_shutdown(void)
{
	if (g_audio)    SDL_CloseAudioDevice(g_audio);
	if (g_texture)  SDL_DestroyTexture(g_texture);
	if (g_renderer) SDL_DestroyRenderer(g_renderer);
	if (g_window)   SDL_DestroyWindow(g_window);
	SDL_Quit();
}

static bool ensure_texture(int w, int h, diatom_pixfmt fmt, diatom_filter filter)
{
	Uint32 sdlfmt = (fmt == DIATOM_PIX_RGB565)
	              ? SDL_PIXELFORMAT_RGB565
	              : SDL_PIXELFORMAT_ARGB8888;
	/* SDL has no sharp-bilinear, so this is an APPROXIMATION: linear blends
	 * across the whole source pixel where sharp-bilinear blends across one
	 * destination pixel, which reads as blurrier than the device will look.
	 * Sharp-Shimmerless is drawn the same way, for the same reason. Good
	 * enough to check the geometry is right on desktop; judgment about how a
	 * filter actually looks belongs on the panel. */
	SDL_ScaleMode mode = (filter != DIATOM_FILTER_NEAREST)
	                   ? SDL_ScaleModeLinear
	                   : SDL_ScaleModeNearest;

	if (g_texture && g_tex_w == w && g_tex_h == h && g_tex_fmt == sdlfmt) {
		if (filter != g_tex_filter) {
			SDL_SetTextureScaleMode(g_texture, mode);
			g_tex_filter = filter;
		}
		return true;
	}

	if (g_texture) SDL_DestroyTexture(g_texture);
	g_texture = SDL_CreateTexture(g_renderer, sdlfmt,
	                              SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!g_texture) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetTextureScaleMode(g_texture, mode);
	g_tex_w = w; g_tex_h = h; g_tex_fmt = sdlfmt; g_tex_filter = filter;
	return true;
}

/* The overlay from diatom_port_overlay: a borrowed pointer, not a copy. The
 * texture is rebuilt only when the image changes, which for a notice on a
 * timer is once. */
static const uint8_t *g_ov;
static int            g_ov_w, g_ov_h;
static uint64_t       g_ov_until;
static SDL_Texture   *g_ov_tex;

void diatom_port_overlay(const uint8_t *bgra, int w, int h, unsigned ms)
{
	int sw = 0, sh = 0;

	if (g_ov_tex) { SDL_DestroyTexture(g_ov_tex); g_ov_tex = NULL; }
	g_ov = NULL;
	g_ov_until = 0;
	if (!bgra || ms == 0 || w <= 0 || h <= 0) return;

	SDL_GetRendererOutputSize(g_renderer, &sw, &sh);
	/* Refused, not clipped - see diatom_port.h. */
	if ((sw && w > sw) || (sh && h > sh)) {
		diatom_port_log(DIATOM_LOG_WARN, "overlay larger than the window; ignored");
		return;
	}

	g_ov_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
	                             SDL_TEXTUREACCESS_STATIC, w, h);
	if (!g_ov_tex) return;
	SDL_SetTextureBlendMode(g_ov_tex, SDL_BLENDMODE_BLEND);
	SDL_UpdateTexture(g_ov_tex, NULL, bgra, w * 4);
	g_ov = bgra;
	g_ov_w = w;
	g_ov_h = h;
	g_ov_until = diatom_port_now_us() + (uint64_t)ms * 1000ull;
}

/* The Border (diatom_port_border): a texture made once a call, from a copy
 * (SDL copies the pixels in), stretched under the game, smooth */
static SDL_Texture *g_bd_tex;

void diatom_port_border(const uint8_t *bgra, int w, int h)
{
	if (g_bd_tex && bgra && w > 0 && h > 0) {
		int tw, th;

		/* The same size again (the blur): the texture that is there */
		if (SDL_QueryTexture(g_bd_tex, NULL, NULL, &tw, &th) == 0 && tw == w && th == h) {
			SDL_UpdateTexture(g_bd_tex, NULL, bgra, w * 4);
			return;
		}
	}
	if (g_bd_tex) { SDL_DestroyTexture(g_bd_tex); g_bd_tex = NULL; }
	if (!bgra || w <= 0 || h <= 0) return;
	g_bd_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
	if (!g_bd_tex) return;
	SDL_SetTextureScaleMode(g_bd_tex, SDL_ScaleModeLinear);
	/* Opaque whatever its alpha: no blending, so it covers */
	SDL_SetTextureBlendMode(g_bd_tex, SDL_BLENDMODE_NONE);
	SDL_UpdateTexture(g_bd_tex, NULL, bgra, w * 4);
}

static void draw_overlay(void)
{
	SDL_Rect r;
	int sw = 0, sh = 0;

	if (!g_ov_tex || diatom_port_now_us() >= g_ov_until) return;
	SDL_GetRendererOutputSize(g_renderer, &sw, &sh);
	r.w = g_ov_w;
	r.h = g_ov_h;
	r.x = (sw - g_ov_w) / 2;
	r.y = sh - g_ov_h - sh / 24;
	SDL_RenderCopy(g_renderer, g_ov_tex, NULL, &r);
}

/* See diatom_port_planes: each plane is one more copy out of the same
 * texture, a source rect to a destination rect, which SDL's renderer does as
 * it does the whole frame. */
static diatom_plane g_planes[DIATOM_PLANES_MAX];
static int          g_nplanes;

void diatom_port_planes(const diatom_plane *planes, int n)
{
	if (!planes || n < 0) n = 0;
	if (n > DIATOM_PLANES_MAX) n = DIATOM_PLANES_MAX;
	if (n) memcpy(g_planes, planes, sizeof *planes * (size_t)n);
	g_nplanes = n;
}

/* SDL's renderer draws no old screens (CRT, Scanlines, LCD), so there is no
 * effect here to weaken */
void diatom_port_strength(int percent) { (void)percent; }

/* No flip to watch on a desktop: SDL presents and says nothing of when. For
 * test/flipwatch.py, DIATOM_TEST_MISSED=N counts every frame presented after
 * the N-th as a refresh missed with the frame ready, as a shader on a capped
 * GPU makes them on the Pixel 2 (src/flipwatch.h). */
static unsigned g_presents, g_refreshes_missed;

unsigned diatom_port_refreshes_missed(void) { return g_refreshes_missed; }

static void count_missed(void)
{
	static long after = -2;

	if (after == -2) {
		const char *e = getenv("DIATOM_TEST_MISSED");

		after = e && e[0] ? strtol(e, NULL, 10) : -1;
	}
	if (after >= 0 && ++g_presents > (unsigned long)after) g_refreshes_missed++;
}

void diatom_port_present(const void *src, int w, int h, size_t pitch,
                         diatom_pixfmt fmt, diatom_rect dst,
                         diatom_filter filter)
{
	SDL_Rect r, sr;
	int i;

	count_missed();

	if (w > 0 && h > 0 && !ensure_texture(w, h, fmt, filter)) return;
	if (src && g_texture) SDL_UpdateTexture(g_texture, NULL, src, (int)pitch);

	SDL_RenderClear(g_renderer);
	if (g_bd_tex) SDL_RenderCopy(g_renderer, g_bd_tex, NULL, NULL);
	if (g_texture && g_nplanes) {
		for (i = 0; i < g_nplanes; i++) {
			sr.x = g_planes[i].src.x; sr.y = g_planes[i].src.y;
			sr.w = g_planes[i].src.w; sr.h = g_planes[i].src.h;
			r.x = g_planes[i].dst.x; r.y = g_planes[i].dst.y;
			r.w = g_planes[i].dst.w; r.h = g_planes[i].dst.h;
			SDL_RenderCopy(g_renderer, g_texture, &sr, &r);
		}
	} else if (g_texture) {
		r.x = dst.x; r.y = dst.y; r.w = dst.w; r.h = dst.h;
		SDL_RenderCopy(g_renderer, g_texture, NULL, &r);
	}
	draw_overlay();
	deskfb_frame();
	SDL_RenderPresent(g_renderer);
}

/* DIATOM_AUDIO_LOG=path, for a test: each write the game's sound makes, a
 * line "frames loudest" (the frames taken, the loudest sample of them), so a
 * check sees the game's own sound go on as it was, with nothing added to it
 * (test/shotfeel.py: a screenshot's shutter is the launcher's, beside it).
 * Written from the frame loop alone. */
static FILE *g_audio_log;
static bool  g_audio_log_looked;

static void audio_log(const int16_t *frames, size_t n)
{
	int top = 0;
	size_t i;

	if (!g_audio_log_looked) {
		const char *e = getenv("DIATOM_AUDIO_LOG");

		g_audio_log_looked = true;
		if (e && *e) g_audio_log = fopen(e, "w");
	}
	if (!g_audio_log) return;
	for (i = 0; i < 2 * n; i++) {
		int a = frames[i] < 0 ? -(int)frames[i] : frames[i];

		if (a > top) top = a;
	}
	fprintf(g_audio_log, "%zu %d\n", n, top);
	fflush(g_audio_log);
}

size_t diatom_port_audio_write(const int16_t *frames, size_t n)
{
	size_t queued, room;

	/* Never blocks; drops on overflow. A blocking write would pace the whole
	 * program off the audio clock, which rules out dynamic rate control.
	 *
	 * Take only what fits. The previous test was `queued >= capacity`, which
	 * refuses only an ALREADY FULL queue and then admits a whole batch: at 4095
	 * queued a 832-frame batch landed entire, and the queue reached a measured
	 * **4927 against a stated 4096**. That mattered beyond the extra latency,
	 * because rate control clamps its error term at +1.0 - reached at exactly
	 * capacity - so everything above capacity was invisible to the controller
	 * that was supposed to be preventing it.
	 *
	 * Clamping rather than refusing the batch outright loses less: the frames
	 * that do not fit are dropped either way, and there is no reason to discard
	 * the ones that would have. */
	/* A sink can die under the port - a headset switched off, or carried out
	 * of range. SDL reports the device STOPPED; falling back happens here
	 * rather than in a poll of its own because this already runs every frame,
	 * and the host finds out through diatom_port_audio_get, which is the only
	 * way it can (the port cannot announce anything).
	 *
	 * Only when a name was asked for. If the DEFAULT device dies there is
	 * nowhere better to go, and retrying it every frame would be a reopen
	 * storm in place of silence. */
	if (g_audio && g_audio_dev[0] &&
	    SDL_GetAudioDeviceStatus(g_audio) == SDL_AUDIO_STOPPED) {
		diatom_port_log(DIATOM_LOG_WARN,
		                "audio: the sink went away; falling back to the default");
		g_audio_dev[0] = '\0';
		audio_open(NULL);
	}
	if (g_vaudio) {
		if (!frames || !n) return 0;
		vaudio_play();
		room = g_vq >= AUDIO_BUFFER_FRAMES ? 0 : AUDIO_BUFFER_FRAMES - g_vq;
		if (n > room) n = room;
		if (!n) return 0;
		g_vq += n;
		audio_log(frames, n);
		return n;
	}
	if (!g_audio || !frames || !n) return 0;
	queued = diatom_port_audio_queued();
	room   = queued >= (size_t)AUDIO_BUFFER_FRAMES
	       ? 0 : (size_t)AUDIO_BUFFER_FRAMES - queued;
	if (n > room) n = room;
	if (!n) return 0;

	SDL_QueueAudio(g_audio, frames, (Uint32)(n * AUDIO_FRAME_BYTES));
	audio_log(frames, n);
	return n;
}

size_t diatom_port_audio_queued(void)
{
	if (g_vaudio) {
		vaudio_play();
		return g_vq;
	}
	if (!g_audio) return 0;
	return SDL_GetQueuedAudioSize(g_audio) / AUDIO_FRAME_BYTES;
}

/* ZyperOS's one key map (its src/device/keymap.h), the Simulator's too */
static const struct { SDL_Scancode key; int btn; } keymap[] = {
	{ SDL_SCANCODE_UP,     DIATOM_BTN_UP     },
	{ SDL_SCANCODE_DOWN,   DIATOM_BTN_DOWN   },
	{ SDL_SCANCODE_LEFT,   DIATOM_BTN_LEFT   },
	{ SDL_SCANCODE_RIGHT,  DIATOM_BTN_RIGHT  },
	{ SDL_SCANCODE_X,      DIATOM_BTN_A      },
	{ SDL_SCANCODE_Z,      DIATOM_BTN_B      },
	{ SDL_SCANCODE_S,      DIATOM_BTN_X      },
	{ SDL_SCANCODE_A,      DIATOM_BTN_Y      },
	{ SDL_SCANCODE_Q,      DIATOM_BTN_L1     },
	{ SDL_SCANCODE_W,      DIATOM_BTN_R1     },
	{ SDL_SCANCODE_E,      DIATOM_BTN_L2     },
	{ SDL_SCANCODE_R,      DIATOM_BTN_R2     },
	{ SDL_SCANCODE_RSHIFT, DIATOM_BTN_SELECT },
	{ SDL_SCANCODE_BACKSPACE, DIATOM_BTN_SELECT },
	{ SDL_SCANCODE_RETURN, DIATOM_BTN_START  },
	{ SDL_SCANCODE_KP_ENTER, DIATOM_BTN_START },
	{ SDL_SCANCODE_ESCAPE, DIATOM_BTN_MENU   },
	{ SDL_SCANCODE_M,      DIATOM_BTN_MENU   },
};

/* DIATOM_INPUT_SCRIPT: buttons held by script rather than by hand, for a
 * window nobody can type into - an offscreen SDL in a container. A list of
 * `poll:buttons` steps separated by `;`, each holding its buttons (names as
 * the protocol's, joined by `+`, none for nothing) from that poll on:
 *
 *     DIATOM_INPUT_SCRIPT='300:start;306:;400:l2;406:;420:r2;426:'
 *
 * A button with `~` before its name is a tap: it went down and up again
 * before that poll, so the poll has it pressed (diatom_port_input_pressed)
 * and never held, as a quick press between two polls of a slow game is;
 * `500:select+~r1` holds SELECT and taps R1.
 *
 * A poll is a frame: the core polls once in each. OR'd with the keyboard,
 * so a script and a person can share a window. Read once, at the first poll. */
static struct { long at; uint32_t held, tap; } g_script[64];
static int  g_script_n = -1;
static long g_polls;

static void script_load(void)
{
	static const char *const names[DIATOM_BTN_COUNT] = {
		"up", "down", "left", "right", "a", "b", "x", "y",
		"l1", "r1", "l2", "r2", "select", "start", "menu"
	};
	const char *e = getenv("DIATOM_INPUT_SCRIPT");
	char buf[2048], *step, *save = NULL;

	g_script_n = 0;
	if (!e || !*e) return;
	snprintf(buf, sizeof buf, "%s", e);
	for (step = strtok_r(buf, ";", &save); step && g_script_n < 64;
	     step = strtok_r(NULL, ";", &save)) {
		char *colon = strchr(step, ':'), *b, *bs = NULL;
		uint32_t held = 0, tap = 0;
		int k;

		if (!colon) continue;
		*colon = '\0';
		for (b = strtok_r(colon + 1, "+", &bs); b; b = strtok_r(NULL, "+", &bs)) {
			const bool t = *b == '~';

			for (k = 0; k < DIATOM_BTN_COUNT; k++)
				if (!strcmp(b + t, names[k])) *(t ? &tap : &held) |= DIATOM_BIT(k);
		}
		g_script[g_script_n].at = strtol(step, NULL, 10);
		g_script[g_script_n].held = held;
		g_script[g_script_n].tap = tap;
		g_script_n++;
	}
	fprintf(stderr, "desktop: input script of %d step(s)\n", g_script_n);
}

/* The script's buttons held at this poll, and in *tap those it taps at it */
static uint32_t script_held(uint32_t *tap)
{
	uint32_t held = 0;
	int i;

	if (g_script_n < 0) script_load();
	g_polls++;
	*tap = 0;
	for (i = 0; i < g_script_n && g_script[i].at <= g_polls; i++) {
		held = g_script[i].held;
		*tap = g_script[i].at == g_polls ? g_script[i].tap : 0;
	}
	return held;
}

void diatom_port_input_poll(void)
{
	const Uint8 *keys;
	SDL_Event ev;
	size_t i;
	uint32_t s = 0, down = 0, tap;
	const uint32_t before = g_buttons;

	/* A key pressed since the last poll, though let go again before this
	 * one: the keyboard's state alone would miss it */
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) g_quit = true;
		if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
			for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
				if (ev.key.keysym.scancode == keymap[i].key) down |= DIATOM_BIT(keymap[i].btn);
	}

	keys = SDL_GetKeyboardState(NULL);
	for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
		if (keys[keymap[i].key]) s |= DIATOM_BIT(keymap[i].btn);

	g_buttons = s | script_held(&tap) |
	            (g_fb ? __atomic_load_n(&g_fb->buttons, __ATOMIC_ACQUIRE) : 0);
	g_pressed = (g_buttons & ~before) | down | tap;
}

uint32_t diatom_port_input_state(void) { return g_buttons; }
uint32_t diatom_port_input_pressed(void) { return g_pressed; }

/* No sticks: the keyboard is a d-pad */
bool diatom_port_input_axes(int16_t axes[4]) { (void)axes; return false; }

/* The pretend motor writes down what a motor of the Pixel 2's kind would
 * run at: each strength times the scale, rounded (test/rumble.py) */
static unsigned rumble_times(uint16_t v, float scale)
{
	if (scale >= 1.0f) return v;
	if (scale <= 0.0f) return 0;
	return (unsigned)((float)v * scale + 0.5f);
}

bool diatom_port_rumble(uint16_t strong, uint16_t weak, float scale)
{
	bool ok;

	pthread_mutex_lock(&g_rumble_mx);
	ok = g_rumble_log != NULL;
	if (ok) {
		fprintf(g_rumble_log, "%u %u", rumble_times(strong, scale), rumble_times(weak, scale));
		/* DIATOM_RUMBLE_LOG_US: and when, for a check of a buzz's length
		 * (test/shotfeel.py) */
		if (getenv("DIATOM_RUMBLE_LOG_US"))
			fprintf(g_rumble_log, " %llu", (unsigned long long)diatom_port_now_us());
		fputc('\n', g_rumble_log);
		fflush(g_rumble_log);
	}
	pthread_mutex_unlock(&g_rumble_mx);
	return ok;
}

/* Nothing queues here: the poll reads the keyboard as it is now, and its
 * presses are the poll's own, started again at each. */
void     diatom_port_input_reset(void) { g_pressed = 0; }

bool     diatom_port_should_quit(void) { return g_quit; }

bool diatom_port_capture(const char *path)
{
	SDL_Surface *s;
	int w, h;
	bool ok;

	if (!g_renderer || !path) return false;
	SDL_GetRendererOutputSize(g_renderer, &w, &h);

	s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return false;

	/* Read back what was actually presented, so the capture shows the real
	 * scaling and letterboxing rather than the core's raw framebuffer. */
	if (SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
	                         s->pixels, s->pitch) != 0) {
		SDL_FreeSurface(s);
		return false;
	}
	/* Save as plain 24-bit RGB. A 32-bit BMP carries a V4/V5 header with alpha
	 * masks that several readers - macOS ImageIO among them - refuse, and the
	 * alpha channel is meaningless here anyway. */
	{
		SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
		SDL_FreeSurface(s);
		if (!rgb) return false;
		ok = SDL_SaveBMP(rgb, path) == 0;
		SDL_FreeSurface(rgb);
	}
	return ok;
}

bool diatom_port_vblank(uint64_t *at_us, double *period_us)
{
	(void)at_us; (void)period_us;
	return false;
}

uint64_t diatom_port_now_us(void)
{
	if (tclock_on()) return __atomic_load_n(&g_tclock_us, __ATOMIC_RELAXED);
	return diatom_ticks_to_us(SDL_GetPerformanceCounter(),
	                          SDL_GetPerformanceFrequency());
}

void diatom_port_sleep_us(double us)
{
	static uint64_t sleeps;

	if (!tclock_on()) {
		diatom_nanosleep_us(us);
		return;
	}
	if (us <= 0) return;
	/* Rounded up, as a sleep is never shorter than asked, and the overshoot */
	__atomic_fetch_add(&g_tclock_us, (uint64_t)ceil(us) + tclock_pick(sleeps++, g_tclock_over),
	                   __ATOMIC_RELAXED);
}

void diatom_port_log(diatom_log_level lvl, const char *msg)
{
	static const char *tag[] = { "debug", "info", "warn", "error" };
	fprintf(stderr, "[%s] %s\n", tag[lvl], msg);
}

/* No volume or brightness control here: the desktop backend is for iteration,
 * and the machine's own mixer and display already own both. No positions
 * makes `LEVELS count=0` the answer to a launcher's query, which tells it not
 * to expect events rather than leaving it to infer that from silence. */
int  diatom_port_level_positions(diatom_level_kind kind) { (void)kind; return 0; }
int  diatom_port_level_read(diatom_level_kind kind) { (void)kind; return -1; }
void diatom_port_level_write(diatom_level_kind kind, int pos) { (void)kind; (void)pos; }
uint32_t diatom_port_level_keys(void) { return 0; }
void diatom_port_level_shown(diatom_level_kind kind, int pos, int positions)
{
	(void)kind; (void)pos; (void)positions;
}
void diatom_port_level_look(const osdbar_look *look) { (void)look; }
void diatom_port_level_lift(int lift) { (void)lift; }
bool diatom_port_level_plugged(void) { return false; }
void diatom_port_level_invalidate(void) { }

/* No device UI drawn over the game on a desktop: the window is for iteration,
 * and the host half of the low-battery state is tested through its answers. */
void diatom_port_low_battery(bool on, bool blink) { (void)on; (void)blink; }
/* No overlay to draw, but the numbers it would say, written down when
 * DIATOM_HUD_LOG is set, each with the time (Diatom's clock, the now file's
 * t_us) its window ended, so test/hudspeed.py can read them */
void diatom_port_hud(bool on, double fps, double speed)
{
	static int log = -1;

	if (log < 0) log = getenv("DIATOM_HUD_LOG") != NULL;
	if (log && on)
		printf("diatom: hud t=%llu fps=%.2f speed=%.3f\n",
		       (unsigned long long)diatom_port_now_us(), fps, speed);
}
void diatom_port_hud_style(const diatom_hud_words *w) { (void)w; }
void diatom_port_power_hold(bool on, unsigned ms, unsigned at, bool done)
{
	(void)on; (void)ms; (void)at; (void)done;
}

/* Nothing to drain: this port presents synchronously inside
 * diatom_port_present - SDL_RenderPresent has returned by the time it does,
 * so there is never a flip in flight to wait for. The definition exists
 * because the seam is part of the port interface, and a port that silently
 * lacked it would fail at link time on the day someone needed it. */
void diatom_port_present_stop(diatom_park park) { (void)park; }

/* No analog stage on a desktop, so there is nothing to hold off. Kept as state
 * so the host half can be exercised without hardware: a port that answers what
 * it was told is enough for test/stateplane.py. */
static bool g_muted;
void diatom_port_mute_set(bool on) { g_muted = on; }
bool diatom_port_mute_get(void) { return g_muted; }
/* No clocks to hold either. For test/flipwatch.py, the holds are written as
 * "1" or "0" to cpu_hold and gpu_hold in DIATOM_POWER_DIR (below), where
 * the Pixel 2 writes its CPU's and GPU's lowest clocks. */
static void hold_say(const char *name, bool on)
{
	const char *dir = getenv("DIATOM_POWER_DIR");
	char path[512];
	FILE *f;

	if (!dir || !dir[0] || snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) return;
	if ((f = fopen(path, "w"))) {
		fputs(on ? "1" : "0", f);
		fclose(f);
	}
}

void diatom_port_fast_clock(bool cpu, bool gpu)
{
	static int cpu_was = -1, gpu_was = -1;

	if ((int)cpu != cpu_was) hold_say("cpu_hold", cpu);
	if ((int)gpu != gpu_was) hold_say("gpu_hold", gpu);
	cpu_was = cpu;
	gpu_was = gpu;
}

/* No clocks on a desktop. For test/power.py, DIATOM_POWER_DIR names a folder
 * whose two files stand for the Pixel 2's DDR and GPU ceilings (PX2_POWER's
 * rows), written through the same memo as the Pixel 2's (port_power.h), so
 * the host's handovers are checked end to end with no device: a second game
 * in one resident process, and a game back from the menu, each lift a
 * ceiling the launcher put down in between. */
static diatom_power_memo g_power_memo = { -1 };
static diatom_power_profile g_power_test;
static char g_power_ram[512], g_power_gpu[512];

static const diatom_power_profile *power_test(void)
{
	static int known = -1;
	const char *dir;

	if (known < 0) {
		dir = getenv("DIATOM_POWER_DIR");
		known = dir && dir[0] &&
		        snprintf(g_power_ram, sizeof g_power_ram, "%s/ram_max_freq", dir) < (int)sizeof g_power_ram &&
		        snprintf(g_power_gpu, sizeof g_power_gpu, "%s/gpu_max_freq", dir) < (int)sizeof g_power_gpu;
		g_power_test.clock[0] = (diatom_clock){ g_power_ram, { "0", "0", "328000000" }, NULL };
		g_power_test.clock[1] = (diatom_clock){ g_power_gpu, { "0", "300000000", "200000000" }, NULL };
		g_power_test.n = 2;
	}
	return known ? &g_power_test : NULL;
}

void diatom_port_power(int level)
{
	const diatom_power_profile *p = power_test();

	if (p) diatom_power_say(&g_power_memo, p, level);
}

void diatom_port_power_invalidate(void) { diatom_power_forget(&g_power_memo); }

/* No GPU frames on the desktop yet: its window is an SDL renderer's, and a
 * core that asks for the GPU is refused (see diatom_port.h). */
bool      diatom_port_gpu_begin(const diatom_gpu_spec *spec) { (void)spec; return false; }
void      diatom_port_gpu_end(void) {}
uintptr_t diatom_port_gpu_target(void) { return 0; }
void     *diatom_port_gpu_proc(const char *name) { (void)name; return NULL; }
void      diatom_port_present_gpu(int w, int h, diatom_rect dst, diatom_filter filter)
{ (void)w; (void)h; (void)dst; (void)filter; }
bool      diatom_port_gpu_read(void *xrgb, int *w, int *h, size_t cap)
{ (void)xrgb; (void)w; (void)h; (void)cap; return false; }
