/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* On-Screen Stats: a small line over the game, in a corner of the screen,
 * of what the player picked to see - how fast the game runs, how warm and
 * busy the chip is, the battery, the time - while the in-game menu's
 * On-Screen Stats is on (its page, src/hudmenu.h). Drawn by Diatom, which
 * knows the numbers; the launcher draws it for the page's preview and its
 * shots. As pixels, as the level bar is (src/osdbar.h), for the same reason:
 * Diatom draws its overlay with GLES and has no font engine, so the few
 * characters it writes come drawn already (src/hudglyphs.h, from
 * tools/genhudglyphs.py).
 *
 * Its items, in this order, each the player's to show or not:
 *
 *   FPS              59.7 fps     the pictures the game makes a second: a
 *                                 game that draws every second frame reads 30
 *   Speed            0.96x        how fast the game is emulated against its
 *                                 own rate, whatever it draws: a game at 30
 *                                 fps at full speed is 1.00x. None at full
 *                                 speed (0.98x to 1.02x), unless it is the
 *                                 only item, so the x does not come and go at
 *                                 0.99x
 *   CPU Temperature  52°C         the chip's hotter zone
 *   CPU Load         CPU 34%      busy against idle, over the half second
 *   CPU Clock        1.3 GHz      cpu0's clock now ("816 MHz" under one)
 *   Battery          [] 87%       a battery drawn, filled to its level
 *   Time             10:08        the clock as MENU > Clock says, 24 or 12
 *
 *   59.7 fps · 0.96x · 52°C · CPU 34% · 1.3 GHz · [] 87% · 10:08 PM
 *
 * Numbers in the text's color, their units and the dots between them
 * softer, so the eye finds the numbers. Twice a second, from the last half
 * second (Speed is what px2-perf calls the game's speed, Diatom's now.h, from
 * the same counters; how they are counted is Diatom's src/emumeter.h). Digits in cells of one width, so a number
 * holds still as it changes. An item whose number is not known (no thermal
 * zone, the desktop's stand-in) is left out.
 *
 * The player's style (hud_style), kept per player: the items, the corner,
 * how far from its edges, the background (a pill in the theme's surface with
 * an edge rim, or a shadow around the text alone), the opacity of the whole,
 * the text's color (the theme's text, white, or the design's green or
 * yellow, which read on bright games), and its size (the support type, or
 * the chrome type). A light text color takes Salt & Pepper's pill whatever
 * the theme, so it always reads. No background at all and the look's accent
 * were tried and left out: white text vanished on a bright game, and lime
 * on Beard's pill read weakly.
 *
 * It never lands under the level bar or the low-battery disc: in a top
 * corner, a line that would meet the bar's span drops below it, and in the
 * top right one that would meet the disc moves left of it (hud_place,
 * decided by the widest the items can say, so it holds still; make
 * check-hud tries every style). An achievement's notice, at the bottom
 * center, draws over it for its few seconds.
 *
 * The geometry is in DESIGN UNITS, the launcher's 1024x768 drawing space;
 * `scale` is output pixels per unit, 1 or 0.625 (the Pixel 2's panel), the
 * two the glyphs come at.
 *
 * Under either license above, as src/osdbar.c: ZyperOS is under
 * GPL-3.0-only and Diatom, which carries a copy of these files in its port/,
 * is MPL. Plain C, no allocation, nothing but libm. */
#ifndef ZYPEROS_HUD_H
#define ZYPEROS_HUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "design.h"

/* How often the numbers change, in milliseconds: twice a second */
#define HUD_EVERY_MS 500

/* Full speed: a speed inside this band says no speed at all */
#define HUD_FULL_LO 0.98
#define HUD_FULL_HI 1.02

enum { HUD_FPS, HUD_SPEED, HUD_TEMP, HUD_CPU, HUD_CLOCK, HUD_BATTERY, HUD_TIME, HUD_ITEMS };
enum { HUD_TOP_LEFT, HUD_TOP_RIGHT, HUD_BOTTOM_LEFT, HUD_BOTTOM_RIGHT, HUD_CORNERS };
enum { HUD_PILL, HUD_SHADOW, HUD_BACKGROUNDS };
enum { HUD_THEME, HUD_WHITE, HUD_GREEN, HUD_YELLOW, HUD_COLORS };
enum { HUD_NORMAL, HUD_SMALL, HUD_SIZES };
#define HUD_EDGES     3   /* Close, Normal, Far */
#define HUD_OPACITIES 3   /* 100%, 75%, 50% */

/* Each choice's word, as the launcher keeps it and sends it to Diatom
 * (SETHUD), the edge's distance in units and the opacity in percent */
extern const char *const hud_item_id[HUD_ITEMS];   /* fps speed temp cpu clock battery time */
extern const char *const hud_corner_id[HUD_CORNERS];          /* tl tr bl br */
extern const char *const hud_background_id[HUD_BACKGROUNDS];  /* pill shadow */
extern const char *const hud_color_id[HUD_COLORS];            /* theme white green yellow */
extern const char *const hud_size_id[HUD_SIZES];              /* normal small */
extern const int hud_edge_units[HUD_EDGES];                   /* 8 24 48 */
extern const int hud_opacity_pct[HUD_OPACITIES];              /* 100 75 50 */

typedef struct {
	uint8_t items;   /* a bit an item, 1 << HUD_FPS ... */
	uint8_t corner, edge, background, opacity, color, size;   /* indexes */
} hud_style;

/* FPS and Speed, top left, Normal distance, a pill, whole, the theme's
 * text, Normal size */
extern const hud_style hud_default;

/* `id` among `n` words, or -1 */
int hud_find(const char *const *ids, int n, const char *id);
/* A style's parts kept in range: anything out of it, the default's */
void hud_clamp(hud_style *st);
/* The items as "fps,speed" and back; an unknown word is skipped, and none
 * known is the default's */
void hud_items_str(uint8_t items, char *out, size_t n);
uint8_t hud_items_parse(const char *s);

/* The numbers: below zero (or NaN) is not known, and that item is left out */
typedef struct {
	double fps, speed;     /* speed: 1 is the game's own rate */
	int temp_c;
	int cpu_pct;
	int cpu_mhz;
	int battery_pct;
	int hour, min;         /* the time, 0-23 and 0-59 */
	bool clock24;
} hud_values;

/* The line for these numbers and these items, in UTF-8: the middle dot
 * between items, the battery as a private code point (U+E000 + its tenth,
 * drawn) before its level. Clamped to what it holds: 999.9 fps, 99.9x. An
 * empty string when nothing is known of what was picked. */
void hud_text(char *out, size_t n, const hud_values *v, uint8_t items);

/* Its size in pixels for `text` at `scale` in style `st` (0 x 0 for an
 * empty one) */
void hud_size(const char *text, float scale, const hud_style *st, int *w, int *h);

/* Where its top left goes on a surface `sw` x `sh` pixels at `scale`, for a
 * line `w` x `h` from hud_size: its corner, its distance from the edges,
 * clear of the level bar and the low-battery disc */
void hud_place(const hud_style *st, float scale, int w, int h, int sw, int sh, int *x, int *y);

/* Draws it into px, w x h from hud_size, `stride` pixels a row, clearing
 * what is there. `t` is the theme's colors (the player's look); NULL is
 * Beard's tokens. Pixels are 0xAARRGGBB, straight alpha, or premultiplied
 * with `premul`, as osdbar_render's. */
void hud_render(uint32_t *px, int w, int h, int stride, float scale, const char *text,
                const hud_style *st, const ds_theme *t, bool premul);

#endif
