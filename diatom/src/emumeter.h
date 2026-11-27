/* How fast a game is emulated, and how often it draws: the two numbers
 * On-Screen Stats (ZyperOS's src/hud.h) and px2-perf (now.h) say.
 *
 * Speed is the emulation's: the frames the core emulated a second, against
 * the frame rate it announces (retro_get_system_av_info). It is not the
 * pictures a second. A game that shows a new picture every second vblank is
 * at full speed at 30 pictures a second, and a core says so in one of two
 * ways, which this counts the same:
 *
 *   - retro_run is one frame, and the picture is repeated: a NULL frame, or
 *     the same one. Calls and frames are the same number.
 *   - retro_run runs until the game shows a new picture, so it is two frames
 *     long: PPSSPP's, Flycast's, ParallelN64's "fullspeed" (pixel2_coreopts.c)
 *     when a game draws at 30 or 20. It is called 30 or 20 times a second
 *     at full speed, and the speed read 0.5x and 0.33x (the Dreamcast at
 *     30 fps on the Brick, 1.0.6).
 *
 * A call that is longer than a frame has no sign of it in the API, but it
 * brings its frames' time of sound, and a core's sound is its emulated time,
 * whichever thread it makes it on: the frames a window held are the sound it
 * made, as frames, when that is more than the calls. Never fewer than the
 * calls: a call is at least a frame, so a core that made no sound (muted, or
 * fast forward's, which Flycast makes none in), or less than a frame's, is
 * counted by its calls as it was. A core slow for its game makes less sound
 * as it emulates less, so it reads under 1x as it should.
 *
 * A window is half a second of the game's time (the menu's left out), the
 * overlay's own, and what the sound says is judged over it whole: its calls
 * bring their sound in pieces (a thread's chunks, Metroid Zero Mission's
 * 600 frames of sound in one call and 1097 in the rest), and a window a
 * little over its calls is the pieces, not a longer call (SOUND_SNAP).
 *
 * FPS is the pictures: the core's video callbacks that brought one, a second
 * of game time, less those of the frames run undrawn that are not put on the
 * panel (frameskip=late: a DS game's undrawn frame hands over the last
 * picture again, main.c). A core that resends the picture it drew (not a
 * NULL frame) is counted at its calls, as a picture cannot be told from the
 * last without a compare, which a frame does not pay for.
 *
 * Pure counters in, numbers out: nothing here reads a clock or a core. */
#ifndef DIATOM_EMUMETER_H
#define DIATOM_EMUMETER_H

#include <stdint.h>

/* A window, in microseconds of game time: the overlay's half second */
#define EMUMETER_WINDOW_US 500000

/* Sound this much over a window's calls (5%) is chunking, not longer calls:
 * one chunk of a thread's sound in a window of 15 calls is 4%, one call's
 * sound moved from a window to the next 7% at 30 fps. Past it, a call is
 * longer than a frame. Real ones are far past it: a 30 fps game is 100% */
#define EMUMETER_SOUND_SNAP 1.05

typedef struct {
	double   fps;       /* the core's own frame rate */
	double   rate;      /* and its sound rate: sound frames a second */
	uint64_t t0;        /* the open window began, in game time (us) */
	long     runs0;     /* retro_run calls at its start */
	uint64_t sound0;    /* sound frames the core made, at its start */
	uint64_t pics0;     /* new pictures, at its start */
	double   emu;       /* frames emulated in the windows closed */
	double   per_run;   /* frames a call in the last window: 1, or more */
	/* The last window closed, said to the overlay */
	double   speed;     /* 1 is the game's own rate */
	double   pic_fps;   /* new pictures a second */
	int      have;      /* a window has closed */
} emumeter;

/* The frames a stretch emulated, from its calls and its sound frames: the
 * rule above. `fps` and `rate` are the core's; 0 for either leaves it the
 * calls. */
double emumeter_frames_of(long runs, uint64_t sound_frames, double fps, double rate);

/* A game starts, with the counters as they are */
void emumeter_begin(emumeter *m, double fps, double sound_rate,
                    long runs, uint64_t sound, uint64_t pics, uint64_t run_us);

/* Once a turn: closes the window when its half second is up, and says so
 * (true: speed and pic_fps are its) */
int emumeter_turn(emumeter *m, long runs, uint64_t sound, uint64_t pics, uint64_t run_us);

/* The frames emulated so far, the open window at the last one's rate: what
 * the now file's counter holds */
long emumeter_frames(const emumeter *m, long runs);

/* The game's clock skipped (the menu): the open window's calls folded into
 * the count at the last rate, and a new one begun, so the sound a core's
 * thread made while the menu was up is not this one's */
void emumeter_rebase(emumeter *m, long runs, uint64_t sound, uint64_t pics, uint64_t run_us);

#endif /* DIATOM_EMUMETER_H */
