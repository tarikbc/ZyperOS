/* SPDX-License-Identifier: GPL-3.0-only */
/* rec.dylib: records the host launcher (build-native/zyperos) as a video of
 * its own frames, driven by a script of button presses, with no window on the
 * desktop and no change to the launcher.
 *
 * Loaded with DYLD_INSERT_LIBRARIES into the headless host build (SDL's dummy
 * video driver, the software renderer), it interposes five SDL calls:
 *
 *   SDL_RenderPresent  reads the frame that is about to be shown and hands it
 *                      to ffmpeg, then moves the clock one display refresh on
 *   SDL_GetTicks(64)   answer that clock rather than the wall's
 *   SDL_Delay          moves it on by the delay (on the main thread)
 *   SDL_PollEvent      delivers the script's key presses when their time comes
 *
 * So the clock the launcher animates by is the frames it drew: every frame of
 * every motion is in the file, at 60 per second, however long the software
 * renderer and the encoder took to make it. The clock is held back to the
 * wall's pace when drawing is quicker than that, so art decoded on texload's
 * workers arrives about when it would on a handheld.
 *
 * Environment:
 *   REC_OUT     the video to write (an .mp4, H.264 at a high quality)
 *   REC_SCRIPT  the presses: lines "MS KEY [HOLD]" (ms after the first frame,
 *               a key name below, how long it is held, 90 by default), and
 *               "MS end" to stop. # starts a comment.
 *   REC_FFMPEG  the ffmpeg to run (ffmpeg on PATH by default)
 *   REC_FPS     the frame rate of the file (60)
 *   REC_CLOCK   "H:MM": the wall clock the launcher reads (time, and
 *               clock_gettime's CLOCK_REALTIME) starts today at that time and
 *               runs on the launcher's clock, so the top bar's time is the
 *               same in every take
 *   REC_MENU_MUSIC  set: the menus' music and sounds are set up as on a
 *               handheld (silent, on SDL's dummy audio driver). The launcher
 *               leaves them out of a headless run, which reads
 *               ZYPEROS_HEADLESS three times (src/main.c): for the sounds and
 *               the music, which this answers as unset, so MENU > Menu Music
 *               shows the tracks a card has, then for the window, which keeps
 *               its value. Should that order change, the window is the one
 *               told it is not headless, and the take stops at video init
 *   REC_BATT    a file with a battery level: the host build reads its stand-in
 *               gauge from /tmp/zyperos_fake_batt (src/device/brick.c), and
 *               this take reads REC_BATT there instead, so the corner shows a
 *               battery without a file in /tmp that other runs would see
 *
 * Key names: left right up down a b x y l1 r1 l2 r2 start select menu.
 * They are the host build's keyboard keys (src/device/brick.c, map_key).
 */
#include <SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DYLD_INTERPOSE(rep, orig)                                              \
	__attribute__((used)) static struct {                                  \
		const void *replacement, *replacee;                            \
	} interpose_##orig __attribute__((section("__DATA,__interpose"))) = {  \
		(const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig };

#define MAX_EVENTS 4096

typedef struct { double at; SDL_Keycode key; int down; } rec_ev;

static rec_ev g_ev[MAX_EVENTS];
static int g_nev, g_next;
static double g_end = -1;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static double g_vt;            /* the launcher's clock, ms */
static double g_real0;         /* the wall's clock at g_vt == 0 */
static double g_first = -1;    /* g_vt at the first frame: the script's 0 */
static int g_started;

static void start_wall(void);
static FILE *g_ff;
static unsigned char *g_last, *g_cur;
static int g_w, g_h;
static long g_ticks;           /* frames written */
static double g_fps = 60.0;
static double g_refresh = 1000.0 / 60.0;

static double wall_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static SDL_Keycode key_of(const char *name)
{
	static const struct { const char *n; SDL_Keycode k; } map[] = {
		{ "left", SDLK_LEFT }, { "right", SDLK_RIGHT }, { "up", SDLK_UP },
		{ "down", SDLK_DOWN }, { "a", SDLK_RETURN }, { "b", SDLK_ESCAPE },
		{ "x", SDLK_x }, { "y", SDLK_y }, { "l1", SDLK_COMMA },
		{ "r1", SDLK_PERIOD }, { "l2", SDLK_k }, { "r2", SDLK_l },
		{ "start", SDLK_s }, { "select", SDLK_LSHIFT }, { "menu", SDLK_m },
	};
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
		if (!strcmp(map[i].n, name)) return map[i].k;
	return SDLK_UNKNOWN;
}

static int ev_cmp(const void *a, const void *b)
{
	const rec_ev *x = a, *y = b;
	return x->at < y->at ? -1 : x->at > y->at ? 1 : 0;
}

static void load_script(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[256];

	if (!f) { fprintf(stderr, "rec: cannot read %s\n", path); exit(4); }
	while (fgets(line, sizeof line, f)) {
		char name[32];
		double at, hold = 90;
		char *hash = strchr(line, '#');
		int n;

		if (hash) *hash = 0;
		n = sscanf(line, "%lf %31s %lf", &at, name, &hold);
		if (n < 2) continue;
		if (!strcmp(name, "end")) { g_end = at; continue; }
		SDL_Keycode k = key_of(name);
		if (k == SDLK_UNKNOWN) { fprintf(stderr, "rec: no key %s\n", name); exit(4); }
		if (g_nev + 2 > MAX_EVENTS) break;
		g_ev[g_nev++] = (rec_ev){ at, k, 1 };
		g_ev[g_nev++] = (rec_ev){ at + hold, k, 0 };
	}
	fclose(f);
	qsort(g_ev, (size_t)g_nev, sizeof g_ev[0], ev_cmp);
	if (g_end < 0) { fprintf(stderr, "rec: the script has no end line\n"); exit(4); }
}

static void start(void)
{
	const char *s;

	if (g_started) return;
	g_started = 1;
	if ((s = getenv("REC_FPS"))) g_fps = atof(s);
	if ((s = getenv("REC_SCRIPT"))) load_script(s);
	else { fprintf(stderr, "rec: REC_SCRIPT is not set\n"); exit(4); }
	g_real0 = wall_ms();
	g_vt = 0;
	start_wall();
}

static void open_ff(int w, int h)
{
	const char *out = getenv("REC_OUT");
	const char *ff = getenv("REC_FFMPEG");
	char cmd[2048];

	if (!out) { fprintf(stderr, "rec: REC_OUT is not set\n"); exit(4); }
	snprintf(cmd, sizeof cmd,
	         "'%s' -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %g -i - "
	         "-c:v libx264 -preset fast -crf 10 -pix_fmt yuv420p -color_range tv "
	         "-colorspace bt709 -color_primaries bt709 -color_trc bt709 "
	         "-vf scale=out_color_matrix=bt709:out_range=tv -movflags +faststart '%s'",
	         ff ? ff : "ffmpeg", w, h, g_fps, out);
	g_ff = popen(cmd, "w");
	if (!g_ff) { fprintf(stderr, "rec: cannot start ffmpeg\n"); exit(4); }
	g_w = w; g_h = h;
	g_last = calloc((size_t)w * h, 4);
	g_cur = malloc((size_t)w * h * 4);
}

/* Every tick of the file up to the launcher's clock, from the frame on screen */
static void emit_until(double vt)
{
	if (!g_ff || g_first < 0) return;
	while ((double)g_ticks * (1000.0 / g_fps) <= vt - g_first) {
		fwrite(g_last, 4, (size_t)g_w * g_h, g_ff);
		g_ticks++;
	}
}

static void finish(void)
{
	if (g_ff) {
		emit_until(g_first + g_end);
		pclose(g_ff);
		g_ff = NULL;
		fprintf(stderr, "rec: %ld frames, %.2f s\n", g_ticks, g_ticks / g_fps);
	}
	_exit(0);
}

/* REC_CLOCK's time today, in seconds since the epoch, or -1 */
static double g_wall0 = -1;

static void start_wall(void)
{
	const char *s = getenv("REC_CLOCK");
	int h, m;

	if (s && sscanf(s, "%d:%d", &h, &m) == 2) {
		time_t now = time(NULL);
		struct tm tm;

		localtime_r(&now, &tm);
		tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = 0; tm.tm_isdst = -1;
		g_wall0 = (double)mktime(&tm);
	}
}

static time_t rec_time(time_t *t)
{
	time_t v;

	start();
	v = g_wall0 >= 0 ? (time_t)(g_wall0 + g_vt / 1000.0) : time(NULL);
	if (t) *t = v;
	return v;
}

static int rec_clock_gettime(clockid_t id, struct timespec *ts)
{
	if (id == CLOCK_REALTIME && ts) {
		start();
		if (g_wall0 >= 0) {
			double s = g_wall0 + g_vt / 1000.0;

			ts->tv_sec = (time_t)s;
			ts->tv_nsec = (long)((s - (double)ts->tv_sec) * 1e9);
			return 0;
		}
	}
	return clock_gettime(id, ts);
}

static Uint32 rec_GetTicks(void)
{
	double v;

	start();
	pthread_mutex_lock(&g_lock);
	/* A loop that waits on the clock alone, drawing nothing, still ends */
	if (pthread_main_np()) g_vt += 0.0005;
	v = g_vt;
	pthread_mutex_unlock(&g_lock);
	return (Uint32)v;
}

static Uint64 rec_GetTicks64(void) { return rec_GetTicks(); }

static void rec_Delay(Uint32 ms)
{
	start();
	if (!pthread_main_np()) { SDL_Delay(ms); return; }
	pthread_mutex_lock(&g_lock);
	g_vt += ms;
	pthread_mutex_unlock(&g_lock);
	/* At the wall's pace, so a worker's art lands when it would */
	double ahead = g_vt - (wall_ms() - g_real0);
	if (ahead > 0) usleep((useconds_t)(ahead * 1000));
	if (g_end >= 0 && g_first >= 0 && g_vt - g_first >= g_end) finish();
}

static void rec_RenderPresent(SDL_Renderer *r)
{
	int w, h;

	start();
	if (SDL_GetRendererOutputSize(r, &w, &h) == 0) {
		if (!g_ff) open_ff(w, h);
		if (w == g_w && h == g_h &&
		    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA32, g_cur, w * 4) == 0) {
			if (g_first < 0) g_first = g_vt;
			emit_until(g_vt);
			unsigned char *t = g_last; g_last = g_cur; g_cur = t;
		}
	}
	SDL_RenderPresent(r);
	pthread_mutex_lock(&g_lock);
	g_vt += g_refresh;
	pthread_mutex_unlock(&g_lock);
	double ahead = g_vt - (wall_ms() - g_real0);
	if (ahead > 0) usleep((useconds_t)(ahead * 1000));
	if (g_end >= 0 && g_first >= 0 && g_vt - g_first >= g_end) finish();
}

static int rec_PollEvent(SDL_Event *e)
{
	start();
	if (g_first >= 0 && g_next < g_nev && g_vt - g_first >= g_ev[g_next].at) {
		if (!e) return 1;
		memset(e, 0, sizeof *e);
		e->type = g_ev[g_next].down ? SDL_KEYDOWN : SDL_KEYUP;
		e->key.state = g_ev[g_next].down ? SDL_PRESSED : SDL_RELEASED;
		e->key.keysym.sym = g_ev[g_next].key;
		e->key.timestamp = (Uint32)g_vt;
		g_next++;
		return 1;
	}
	return SDL_PollEvent(e);
}

static FILE *rec_fopen(const char *path, const char *mode)
{
	const char *batt = getenv("REC_BATT");

	if (batt && path && !strcmp(path, "/tmp/zyperos_fake_batt")) return fopen(batt, mode);
	return fopen(path, mode);
}

static char *rec_getenv(const char *name)
{
	static int asked;

	if (name && !strcmp(name, "ZYPEROS_HEADLESS") && getenv("REC_MENU_MUSIC") && asked++ < 2)
		return NULL;
	return getenv(name);
}

DYLD_INTERPOSE(rec_fopen, fopen)
DYLD_INTERPOSE(rec_getenv, getenv)
DYLD_INTERPOSE(rec_GetTicks, SDL_GetTicks)
DYLD_INTERPOSE(rec_GetTicks64, SDL_GetTicks64)
DYLD_INTERPOSE(rec_Delay, SDL_Delay)
DYLD_INTERPOSE(rec_RenderPresent, SDL_RenderPresent)
DYLD_INTERPOSE(rec_PollEvent, SDL_PollEvent)
DYLD_INTERPOSE(rec_time, time)
DYLD_INTERPOSE(rec_clock_gettime, clock_gettime)
