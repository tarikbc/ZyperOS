/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Vendored from ZyperOS src/osdbar.h, which is where it is changed: the
 * launcher, this port and portkeys draw the same bar from the same code. Copy
 * it back here whole, below this note, when it changes there: make check fails
 * while the two differ (tools/check-copies.py). */
#ifndef ZYPEROS_OSDBAR_H
#define ZYPEROS_OSDBAR_H

/* The volume and brightness bar, as pixels: one drawing for every place it
 * shows, so it is the same thing on the shelf, in a Diatom game and over a
 * port, rather than three hands copying one picture.
 *
 * A pill at the top center of the screen: the setting's mark on the left (a
 * speaker, with a cross at silence, or a sun whose rays grow with the level)
 * and one segment per step to its right, filled up to the level. And POWER's
 * hold, the same pill with the power mark, its segments filling as the
 * button is held (OSDBAR_POWER). Every color
 * and time is a token of src/design.h (DESIGN.md 11.6), drawn in the theme
 * the caller names, Beard or Salt & Pepper: the surface and edge for the
 * pill, the track for a step not yet reached, and for the steps reached a
 * color fixed per function rather than the system's, which would make one
 * control change color as you scrolled past it - accent-ink for volume, the
 * color of light for brightness. Segments rather than a smooth fill because
 * the levels ARE steps - twenty presses of the volume, eleven of the
 * brightness - and a player can count where they are and how far there is
 * to go.
 *
 * Why pixels and not SDL draws: two of the three places have no SDL. Diatom
 * draws its overlay with GLES, and portkeys has only a KMS buffer on the
 * panel's cursor layer while a port owns the display. All three take this
 * function's output as it is - ZyperOS as a texture, Diatom as a GL
 * texture, portkeys straight into the scanout buffer, turned for the panel -
 * so the design is written down once. Plain C, no allocation the
 * caller does not see, nothing but libm.
 *
 * Under either license above: ZyperOS is under GPL-3.0-only
 * and Diatom, which carries a copy of these two files in its port/,
 * with src/design.h they read, is MPL.
 *
 * The geometry is in DESIGN UNITS, the launcher's 1024x768 drawing space,
 * and `scale` is output pixels per unit: 1 for ZyperOS, 0.625 for the Pixel 2's
 * 640x480 panel. Every edge is antialiased from its distance to the pixel's
 * center, so it is as crisp at 0.625 as at 1. */

#include <stdbool.h>
#include <stdint.h>

#include "design.h"

enum { OSDBAR_BRIGHT = 1, OSDBAR_VOLUME = 2, OSDBAR_POWER = 3 };   /* plat_osd_show's kinds */

/* POWER held to turn the handheld off (src/powerhold.h, MENU > Power Button
 * on Hold): the power mark, and a segment lit for each twentieth of the hold,
 * in accent-ink as the lock screen's bar and the charging screen's hold
 * fill. Its level is how many are lit, of this many. */
#define OSDBAR_POWER_STEPS 20

/* The buffer: the pill (452 x 64), room for it to grow by its boost cells
 * (src/boost.h: up to 117 units wider, the six volume cells and the push at
 * the end), and a margin around it its shadow falls into, in units.
 * OSDBAR_X/Y are where the buffer's top left goes on the screen, which puts
 * the pill 22 units under the top edge, centered, at every width. */
#define OSDBAR_W 596
#define OSDBAR_H 92
#define OSDBAR_X ((1024 - OSDBAR_W) / 2)
#define OSDBAR_Y 10

/* Its life, the same everywhere, on the design's motion tokens: in over
 * OSDBAR_IN_MS (quick) on the quintic ease-out, sliding down OSDBAR_RISE
 * units as it fades up; held until OSDBAR_HOLD_MS (hold.bar) after the last
 * change; out over OSDBAR_OUT_MS (fade.out), linearly. A change while it
 * fades out brings it back to full at once, where it is - no second slide
 * for a bar already there. The names stay so a host written before the
 * tokens still builds. */
#define OSDBAR_IN_MS   DS_MS_QUICK
#define OSDBAR_HOLD_MS DS_MS_HOLD_BAR
#define OSDBAR_OUT_MS  DS_MS_FADE_OUT
#define OSDBAR_RISE    14

/* The buffer's size in pixels at `scale`. */
void osdbar_size(float scale, int *w, int *h);

/* Everything the bar shows at one moment, for a host that animates it
 * (src/boost.h works it out from the level, the boost and the clock):
 *
 * - the mark follows the level as it moves: the speaker's waves grow arc by
 *   arc, each over its third of the scale, and the sun's rays and disc grow;
 *   `mark` is that level as a share of the top, moving in quick, and `cut`
 *   the speaker's cross at silence, fading in and out;
 * - past the top, `ncells` boost cells in the theme's warning color, each a
 *   little taller than the one before, the bar grown both ways to hold them
 *   (`grow`), a thin mark at the top between them and the steps (`tick`);
 *   each cell's `fill` wipes it in from the left;
 * - before the boost opens, the end pushing out by one big charging cell
 *   that fills with `charge`; `morph` settles it to a cell's size as the
 *   other cells spawn; the boost's parts of the mark (a fourth wave, eight
 *   short rays) grow with the cells filled;
 * - `flash`, the rim in the warning color as it opens, and `push`, a small
 *   push of the end when a press finds the boost full.
 *
 * All of it 0 (and ncells 0) is the bar as it was before the boost: the
 * pill 452 wide, the level's cells, the mark. */
#define OSDBAR_BOOST_MAX 6
typedef struct {
	int kind;                         /* OSDBAR_BRIGHT, OSDBAR_VOLUME, OSDBAR_POWER */
	int level, max;                   /* the cells lit, of max */
	float mark;                       /* 0..1, the mark's level */
	float cut;                        /* 0..1, the speaker's cross */
	int ncells;                       /* boost cells, 0..OSDBAR_BOOST_MAX */
	float fill[OSDBAR_BOOST_MAX];     /* 0..1 each */
	float charge;                     /* 0..1, the charging cell's fill */
	float grow, morph, tick, flash, push;   /* 0..1 each */
	/* The volume's bar with the mute switch down (the Brick's side
	 * switch): the speaker crossed in text-soft, no waves, no steps and no
	 * boost, so a press shows nothing moving that plays nothing. The pill is
	 * only as wide as what it holds, centered: the mark in the room it has
	 * before the steps, then the host's word, `word_w` units wide ("Muted",
	 * at most OSDBAR_WORD_MAX; 0 for a host with no words, which shows the
	 * mark alone in a pill that hugs it), then the bar's own end padding.
	 * The host writes the word from osdbar_word_x. */
	bool muted;
	float word_w;
} osdbar_look;

/* Where the steps stand, in the buffer's units: from OSDBAR_WORD_X0 to
 * OSDBAR_WORD_X1 with their middle on OSDBAR_WORD_CY (src/osdbar.c's SEG_X0,
 * SEG_X1 and the pill's middle); the most a muted bar's word may take is
 * their width */
#define OSDBAR_WORD_X0 (OSDBAR_W / 2 - 226 + 84)
#define OSDBAR_WORD_X1 (OSDBAR_W / 2 + 226 - 30)
#define OSDBAR_WORD_MAX (OSDBAR_WORD_X1 - OSDBAR_WORD_X0)
#define OSDBAR_WORD_CY (OSDBAR_H / 2 - 2)   /* the pill 12 under the top, 64 tall */

/* A look of the plain bar: the level, its mark at rest, no boost */
void osdbar_look_plain(osdbar_look *l, int kind, int level, int max);

/* The pill's width for a look, in units: 452 but for a muted volume's bar,
 * which hugs its mark and word; and where in the buffer that word starts */
float osdbar_pill_w(const osdbar_look *l);
float osdbar_word_x(const osdbar_look *l);

/* osdbar_render for a look: the same buffer, the same caching (a call with
 * the same look, theme and size at another opacity converts what was
 * painted), painted again whenever any of it changes. */
void osdbar_render_look(uint32_t *px, int w, int h, int stride, float scale,
                        int theme, const osdbar_look *look, float alpha, bool premul);

/* Theme `theme`'s colors for the bar from now on: the six roles it draws with
 * (surface, edge, track, accent-ink, light, ink) taken from `t`, the player's
 * look (src/look.h, DESIGN.md 3.5), or the tokens' again for NULL. The
 * launcher says it as the look changes; Diatom and portkeys from the theme
 * file (themefile_read_bar). For the thread that draws the bar, as
 * osdbar_render is. */
void osdbar_colors(int theme, const ds_theme *t);

/* Draws the bar into px, w x h from osdbar_size, `stride` pixels a row,
 * clearing what is there. `theme` is DS_LIGHT or DS_DARK (src/design.h);
 * anything else draws DS_LIGHT, Beard, the default, as a theme read from a
 * file another process wrote may be anything. Pixels are 0xAARRGGBB in a
 * uint32_t - ARGB8888 to both SDL and DRM on a little-endian machine.
 * `premul` gives premultiplied color, which the display controller's
 * blending expects; SDL's default blending wants it straight. `alpha`
 * (0..1) is multiplied in, for a host that fades by redrawing rather than by
 * blending the whole thing: a call that differs from the one before only in
 * `alpha` or `premul` does not paint the bar again but converts what the last
 * one painted, kept in one static buffer - so this is for one thread. */
void osdbar_render(uint32_t *px, int w, int h, int stride, float scale,
                   int theme, int kind, int level, int max, float alpha, bool premul);

/* Where in its life the bar is, from the milliseconds since it appeared and
 * since the last change: false once it is gone, else its opacity (0..1) and
 * how far above its resting place it still is, in units (0..OSDBAR_RISE). */
bool osdbar_envelope(uint32_t since_shown, uint32_t since_change,
                     float *alpha, float *rise);

/* The milliseconds, from the same two clocks, until the bar next looks
 * different on its own: 0 while it moves, the time to the fade while it
 * holds. What a host that only redraws on change waits for. Meaningless once
 * osdbar_envelope says it is gone. */
uint32_t osdbar_next_change(uint32_t since_shown, uint32_t since_change);

#endif
