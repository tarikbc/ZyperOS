/* src/emumeter.c: the frames a stretch of play emulated, from the calls to
 * retro_run and the sound the core made, and the overlay's Speed and FPS from
 * them (the stub core, end to end, is test/hudspeed.py).
 *
 * Offline and instant: it links src/emumeter.c and nothing else. */
#include <math.h>
#include <stdio.h>

#include "emumeter.h"

static int g_fail, g_pass;

static void ck(int cond, const char *what)
{
	if (cond) { g_pass++; return; }
	g_fail++;
	printf("  FAIL %s\n", what);
}

static int near(double a, double b, double tol)
{
	return fabs(a - b) <= tol;
}

#define FPS  59.7275
#define RATE 32040.0
#define SOUND_A_FRAME (RATE / FPS)   /* 536.4 sound frames */

int main(void)
{
	emumeter m;
	uint64_t sound, pics, t;
	long runs;
	int i, closed;

	/* ---- the rule -------------------------------------------------- */
	ck(emumeter_frames_of(0, 1000, FPS, RATE) == 0.0, "no calls, no frames");
	ck(emumeter_frames_of(60, 0, FPS, RATE) == 60.0, "a core that made no sound: its calls");
	ck(emumeter_frames_of(60, 100, FPS, RATE) == 60.0,
	   "less sound than frames (a core muted for a stretch): its calls, never fewer");
	ck(emumeter_frames_of(60, (uint64_t)(60 * SOUND_A_FRAME), FPS, RATE) == 60.0,
	   "a frame of sound a call: the calls");
	ck(emumeter_frames_of(60, (uint64_t)(60 * SOUND_A_FRAME * 1.04), FPS, RATE) == 60.0,
	   "4% over: chunking, the calls");
	ck(near(emumeter_frames_of(30, (uint64_t)(60 * SOUND_A_FRAME), FPS, RATE), 60.0, 0.1),
	   "30 calls with 60 frames of sound: 60 frames (a call two frames long)");
	ck(near(emumeter_frames_of(20, (uint64_t)(60 * SOUND_A_FRAME), FPS, RATE), 60.0, 0.1),
	   "20 calls with 60 frames of sound: 60 frames (three long)");
	ck(near(emumeter_frames_of(45, (uint64_t)(60 * SOUND_A_FRAME), FPS, RATE), 60.0, 0.1),
	   "a mix of one and two frames a call: the sound's 60");
	ck(emumeter_frames_of(30, 99999, 0.0, RATE) == 30.0 &&
	   emumeter_frames_of(30, 99999, FPS, 0.0) == 30.0,
	   "no rate known: the calls");

	/* ---- windows --------------------------------------------------- */
	/* A core that draws every frame at full speed, one sound frame a call;
	 * a call every 1/FPS s of the game's time */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	ck(!m.have, "no window yet");
	closed = 0;
	for (i = 0; i < 90; i++) {
		runs++; pics++;
		sound = (uint64_t)((double)runs * SOUND_A_FRAME);
		t = (uint64_t)((double)runs * 1e6 / FPS);
		closed += emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(closed == 3, "a window every half second: three in 1.5 s");
	ck(near(m.speed, 1.0, 0.01) && near(m.pic_fps, FPS, 0.5), "full speed, every frame drawn: 1.00x and 59.7 fps");
	ck(near((double)emumeter_frames(&m, runs), 90.0, 1.0), "90 frames emulated");

	/* The same core, a repeat on every second call (a NULL frame): 1.00x, 30 fps */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 90; i++) {
		runs++; if (!(runs % 2)) pics++;
		sound = (uint64_t)((double)runs * SOUND_A_FRAME);
		t = (uint64_t)((double)runs * 1e6 / FPS);
		emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(near(m.speed, 1.0, 0.01) && near(m.pic_fps, FPS / 2, 0.5), "a repeat every second call: 1.00x and 30 fps");

	/* A call two frames long at full speed: 30 calls a second, a picture each */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 45; i++) {
		runs++; pics++;
		sound = (uint64_t)((double)runs * 2 * SOUND_A_FRAME);
		t = (uint64_t)((double)runs * 2e6 / FPS);
		emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(near(m.speed, 1.0, 0.02) && near(m.pic_fps, FPS / 2, 0.5),
	   "calls two frames long: 1.00x and 30 fps, not 0.5x");
	ck(near(m.per_run, 2.0, 0.1), "two frames a call");
	ck(near((double)emumeter_frames(&m, runs), 90.0, 2.0), "and 90 frames emulated in 45 calls");

	/* A slow core: a frame every 25 ms, a frame of sound each: 0.67x */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 40; i++) {
		runs++; pics++;
		sound = (uint64_t)((double)runs * SOUND_A_FRAME);
		t = (uint64_t)runs * 25000;
		emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(near(m.speed, 0.67, 0.02) && near(m.pic_fps, 40.0, 0.5), "a core at 25 ms a frame: 0.67x and 40 fps");

	/* Fast forward at 3x: three frames of game time in one frame's time */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 270; i++) {
		runs++; pics++;
		sound = (uint64_t)((double)runs * SOUND_A_FRAME);
		t = (uint64_t)((double)runs * 1e6 / FPS / 3.0);
		emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(near(m.speed, 3.0, 0.05), "fast forward at 3x: 3.00x");

	/* A core's sound in chunks that do not line up with a half second: a
	 * window of 30 calls that holds a chunk too many or too few is 1.00x */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 60; i++) {
		runs++; pics++;
		/* sound in blocks of 1024 frames, as a thread's would */
		sound = (uint64_t)((double)runs * SOUND_A_FRAME / 1024.0) * 1024;
		t = (uint64_t)((double)runs * 1e6 / FPS);
		if (emumeter_turn(&m, runs, sound, pics, t))
			ck(near(m.speed, 1.0, 0.001), "a thread's chunks: every window 1.00x, not 1.03x");
	}

	/* A machine so slow that a window holds five calls, each two frames long,
	 * 100 ms apart: the overlay's windows and the now file's counter are one
	 * count. Every window that closes, the counter is the windows' frames so
	 * far, and the windows' speeds say 0.33x, the machine's. (The CI runner
	 * ran a 25 ms core at 9.7 calls a second: Speed and the now file are made
	 * of the same counters, and said the same.) */
	{
		double sum = 0.0, last_t = 0.0;
		int windows = 0, off = 0;

		runs = 0; sound = 0; pics = 0; t = 0;
		emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
		for (i = 0; i < 200; i++) {
			runs++; pics++;
			sound = (uint64_t)((double)runs * 2 * SOUND_A_FRAME);
			t = (uint64_t)runs * 100000;
			if (emumeter_turn(&m, runs, sound, pics, t)) {
				sum += m.speed * ((double)t - last_t) / 1e6 * FPS;
				last_t = (double)t;
				windows++;
				if (!near((double)emumeter_frames(&m, runs), sum, 1.0)) off++;
			}
		}
		ck(windows >= 30, "a window every five calls: forty of them in 200 calls, thirty at least");
		ck(off == 0, "at every window the now file's counter is the windows' frames");
		ck(near(m.speed, 2.0 * 10.0 / FPS, 0.01), "100 ms a call, two frames each: 0.33x");
	}

	/* A stall: no calls in a window. Speed 0, and no division by it */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	ck(emumeter_turn(&m, 0, 0, 0, 600000) == 1 && m.speed == 0.0 && m.pic_fps == 0.0,
	   "no calls in a window: 0x, 0 fps");

	/* The menu: the open window's calls kept in the count, a new one begun
	 * where the sound counter is now, so a thread's sound made while the menu
	 * was up is not counted in the first window after it */
	runs = 0; sound = 0; pics = 0; t = 0;
	emumeter_begin(&m, FPS, RATE, runs, sound, pics, t);
	for (i = 0; i < 40; i++) {
		runs++; pics++;
		sound = (uint64_t)((double)runs * SOUND_A_FRAME);
		t = (uint64_t)((double)runs * 1e6 / FPS);
		emumeter_turn(&m, runs, sound, pics, t);
	}
	{
		long before = emumeter_frames(&m, runs);

		sound += 100000;   /* the menu: a thread that made sound meanwhile */
		emumeter_rebase(&m, runs, sound, pics, t);
		ck(emumeter_frames(&m, runs) == before, "rebased: the count does not move");
	}
	for (i = 0; i < 40; i++) {
		runs++; pics++;
		sound += (uint64_t)SOUND_A_FRAME;
		t += (uint64_t)(1e6 / FPS);
		emumeter_turn(&m, runs, sound, pics, t);
	}
	ck(near(m.speed, 1.0, 0.02), "after the menu: 1.00x, the menu's sound left out");

	/* A new game starts again */
	emumeter_begin(&m, 50.0, 32000.0, 0, 0, 0, 0);
	ck(!m.have && m.speed < 0.0, "a new game: no numbers yet");

	if (g_fail) {
		printf("emumeter: %d failed, %d passed\n", g_fail, g_pass);
		return 1;
	}
	printf("ok: emumeter, %d checks - frames from calls and sound, windows, the menu\n", g_pass);
	return 0;
}
