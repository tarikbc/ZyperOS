/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MP4LOOK_H
#define ZYPEROS_MP4LOOK_H

/* MP4's Player Looks (DESIGN.md 11.64, my picks in the Sound lab,
 * 2026-11-19): how Now Playing draws a song. Cover, the album's cover
 * large, is the default and was the only one; the others are drawn by
 * src/nowplaying_looks.c from what this holds, which is the part with no
 * SDL in it, so tools/mp4look-check.c can hold the motion to its numbers.
 *
 * The bars are the sound (src/mp4d/bands.h): 32 bands from the daemon, 30
 * times a second, each bar the loudest of the bands under it. A bar rises at
 * once and falls on **follow** (tau DS_MS_BAR_FALL); its cap holds where the
 * bar last reached for **peak-hold**, then drops with gravity, from the top
 * to the floor in MP4L_CAP_DROP_MS. Paused, the bars fall to rest, and once
 * every bar and cap is down nothing moves and nothing is drawn. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	MP4L_COVER,
	MP4L_CASSETTE,
	MP4L_VINYL,
	MP4L_CLASSIC,
	MP4L_SPECTRUM,
	MP4L_COUNT
} mp4l_id;

/* The setting's word for a look ("cover", "classic", ...), kept as a word
 * so the list's order can change without a card's choice moving; and back,
 * Cover for one it does not know */
const char *mp4l_key(mp4l_id id);
mp4l_id     mp4l_from_key(const char *key);

/* How many bars a look draws, 0 for none (Cover) */
int  mp4l_bars(mp4l_id id);

/* ---- the motion ------------------------------------------------------------ */

#define MP4L_BANDS    32
#define MP4L_BARS_MAX 32
/* The cap's fall, from the top to the floor, after its hold */
#define MP4L_CAP_DROP_MS 667

typedef struct {
	int   n;                       /* bars, mp4l_bars of the look */
	float lv[MP4L_BARS_MAX];       /* each bar's height, 0 to 1 */
	float cap[MP4L_BARS_MAX];      /* its cap's */
	float hold[MP4L_BARS_MAX];     /* ms the cap still holds */
	float fall[MP4L_BARS_MAX];     /* the cap's speed down, per ms */
} mp4l_bars_t;

/* `n` bars, every one at rest */
void mp4l_bars_reset(mp4l_bars_t *b, int n);

/* One frame, `dt` ms after the last. `bands` the daemon's 32 levels (0-255)
 * while they are fresh, NULL when there are none to follow (paused, or
 * nothing came): every bar then falls to rest. */
void mp4l_bars_step(mp4l_bars_t *b, float dt, const uint8_t *bands);

/* Every bar and every cap down: nothing to draw again */
bool mp4l_bars_rest(const mp4l_bars_t *b);

/* ---- turning: the reels, the record ---------------------------------------------
 *
 * How fast what turns is turning, 0 stopped to 1 at its speed: up to it over
 * **spin-up** as the music plays, down over **spin-down** as it stops, both
 * on **out**, and from wherever it is when the other comes (interruptible).
 * The sound stops at once; only the turning runs on. */
typedef struct {
	float v;                 /* now */
	float from, to;          /* the move under way */
	float t, dur;            /* ms into it, and its length; t >= dur at rest */
} mp4l_spin;

void mp4l_spin_to(mp4l_spin *s, bool on);
void mp4l_spin_step(mp4l_spin *s, float dt);
/* Turning, or on its way to or from it */
bool mp4l_spin_moving(const mp4l_spin *s);

/* ---- the cassette's tape ---------------------------------------------------------
 *
 * Where the tape is (0 the song's start, 1 its end) and each reel's angle.
 * Playing it follows the song; a seek's jump it winds to on **follow** (tau
 * DS_MS_WIND), the reels spinning with it; a new song rewinds it to the
 * start over **rewind** (on **turn**). Each reel turns at the tape's speed
 * over its pack's radius, so the near-empty one turns fast: the left pack
 * empties as the song plays, the right one fills, the pack's area going
 * with the tape. Radii and the tape's speed in the caller's units. */
typedef struct {
	float f;                 /* the tape where it is shown */
	float from;              /* a rewind's start */
	float t;                 /* ms into a rewind; < 0 none */
	float ang[2];            /* the reels' angles, degrees clockwise */
	float up;                /* ms of spin-up the playing turn has still to come, after a wind */
	int   song;              /* the song the tape is on (its place in the queue) */
	bool  known;
} mp4l_tape;

/* A pack's radius with the tape at `f`: reel 0 the left (full at the
 * start), 1 the right */
float mp4l_tape_r(float f, int reel, float hub_r, float full_r);

/* One frame, `dt` ms: `song` and `target` where the music is, `spin` how
 * fast the reels turn playing (mp4l_spin's v), `speed` the tape's in units
 * a second, `wind` the units of tape a whole song is when winding, for the
 * reels' turn as the tape winds */
void mp4l_tape_step(mp4l_tape *t, float dt, int song, float target, float spin, float speed,
                    float hub_r, float full_r, float wind);

/* Rewinding or winding: the tape not yet where the song is */
bool mp4l_tape_moving(const mp4l_tape *t, float target);

/* ---- the record ------------------------------------------------------------------
 *
 * The record's angle, turning clockwise at 33 1/3 a minute (200 degrees a
 * second) times mp4l_spin's v; and the tonearm's needle, its radius, gliding
 * to the song's place on **follow** (tau DS_MS_ARM_GLIDE). */
#define MP4L_RPM_DEG_S 200.0f

typedef struct {
	float ang;               /* degrees clockwise */
	float arm;               /* the needle's radius, the caller's units */
	bool  known;
} mp4l_disc;

/* The needle's radius for song `place` of `n`, `f` into it: the grooves from
 * `out` to `in` shared out evenly, the first song's start at `out` */
float mp4l_groove_r(int place, int n, float f, float out, float in);

/* The tonearm's angle (degrees, clockwise from pointing right) that puts its
 * needle `r` from the spindle: the pivot `d` from it at `pivot_deg` (up and
 * right is negative), the needle `len` from the pivot; on the side of the
 * line from the pivot to the spindle that keeps the arm over the record's
 * right, as a turntable's sweeps */
float mp4l_arm_angle(float r, float d, float pivot_deg, float len);

/* One frame, `dt` ms: `spin` mp4l_spin's v, `arm` where the needle goes */
void mp4l_disc_step(mp4l_disc *d, float dt, float spin, float arm);
/* The needle not yet where it goes */
bool mp4l_disc_moving(const mp4l_disc *d, float arm);

/* ---- the cassette's styles (DESIGN.md 11.64, my picks in the Tape Styles
 * labs, 2026-11-23) ------------------------------------------------------------
 *
 * The tape fills the screen, in one of seven styles, each pixel art drawn
 * by code on a grid of cells (src/pixtape.h); Pinstripe, the first, the
 * default. MENU's Tape Style row and Up and Down on Now Playing step round
 * the kept ones. Kept as `mp4.tape`, a word, as the look is. */
typedef enum {
	MP4T_PINSTRIPE,
	MP4T_BAND,               /* Cover Band: its band the cover's main color */
	MP4T_FERRIC,
	MP4T_METAL,
	MP4T_CLEAR,
	MP4T_RED,
	MP4T_WORN,
	MP4T_COUNT
} mp4t_id;

/* The style a card has until it says another, and for a word no longer
 * known (the smooth "smoke" there was) */
#define MP4T_DEFAULT MP4T_PINSTRIPE

const char *mp4t_key(mp4t_id id);
/* MP4T_DEFAULT for a word it does not know */
mp4t_id     mp4t_from_key(const char *key);
/* Whether a style is offered (all seven are); and the next kept one from
 * `id`, `d` +1 or -1, round. A style no longer kept steps from the first. */
bool        mp4t_kept(mp4t_id id);
mp4t_id     mp4t_step(mp4t_id id, int d);

/* The tape's number, as a C-cassette's: the shortest of 46, 60 and 90
 * minutes that holds the album, a side each half; 120 past 90. 0 when the
 * album's length is not known yet. */
int  mp4t_tape_no(double album_s);
/* The song's side: the album's first half A, the rest B, as an album went
 * onto a tape (`place` 0-based of `n`) */
char mp4t_side(int place, int n);

/* The pixel tape's turning parts are sprites made once (src/pixtape.h):
 * a hub for every MP4T_HUB_STEP degrees, MP4T_HUB_FRAMES of them, and a
 * pack for every half cell of radius. Each frame shows the one nearest the
 * model's angle (mp4l_tape's, degrees clockwise) and radius. */
#define MP4T_HUB_STEP   7.5f
#define MP4T_HUB_FRAMES 48
int   mp4t_hub_frame(float ang);
/* A pack's radius `r`, in cells, to the nearest half cell, as twice it */
int   mp4t_pack_half(float r_cells);

/* A cell in units: 8 where 8 units are whole pixels on the panel (the
 * Pixel 2's 5, the Brick's 8), else 16 (the Smart Pro's 15): `px_per_unit`
 * the panel's pixels a canvas unit. 0 when neither is whole. */
int   mp4t_cell(float px_per_unit);
/* The panel's pixels a canvas unit: its height as the picture is seen
 * (a panel turned a quarter shows its width) over the canvas's 768 */
float mp4t_px_per_unit(int panel_w, int panel_h, int turn);

#endif
