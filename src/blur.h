/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_BLUR_H
#define ZYPEROS_BLUR_H

/* The arithmetic under Beard's shadows (DESIGN.md 7, src/ui.c's ui_lift), in the part of it that is plain C: the box blur a shadow is
 * made with, the size of the one texture each kind of shadow is made in, and
 * the nine pieces that texture is drawn as under a rect of any size.
 *
 * src/ui.c turns these numbers into a texture and draws it; nothing here
 * knows a renderer. So tools/blur-check.c holds the four things a shadow
 * owes the eye and that nobody sees go wrong until a card stands on a seam:
 * a pass of the box moves light and loses none of it, three passes reach
 * exactly as far as the texture is padded and no further, the nine pieces
 * of the texture drawn under a big rect are the shadow that rect would cast,
 * with no line where a corner meets an edge, and the texture made at half
 * size and drawn at twice it is the shadow made at full size. */

#include <stdbool.h>

#include "design.h"

/* DESIGN.md 7's three shadows, each a blur and the corner of what it lifts -
 * a card's, a chip's, a panel's - so its blurred edge follows the shape: the
 * one place they are paired. src/ui.c makes its lifts' shadows from this
 * table and tools/blur-check.c tests every row of it, so a kind given another
 * corner is tested with that corner. A thumbnail's corner is a chip's, since
 * a slot's frame and Muse's square cover are near enough to square that under
 * a 20-unit blur the two cannot be told apart. */
enum { BLUR_FOCUS, BLUR_FOCUS_SMALL, BLUR_PANEL, BLUR_KINDS };

typedef struct {
	const char *name;   /* DESIGN.md's name for it */
	int blur;           /* units: the box's width less one */
	int radius;         /* the corner of what it lifts, units */
} blur_kind;

static const blur_kind BLUR_KIND[BLUR_KINDS] = {
	[BLUR_FOCUS]       = { "focus", DS_SHADOW_FOCUS_BLUR, DS_RADIUS_CARD },
	[BLUR_FOCUS_SMALL] = { "focus.small", DS_SHADOW_FOCUS_SMALL_BLUR, DS_RADIUS_CHIP },
	[BLUR_PANEL]       = { "panel", DS_SHADOW_PANEL_BLUR, DS_RADIUS_PANEL },
};

/* One pass of a box blur along a line of `n` values `stride` apart, `rad`
 * each side, so each value becomes the mean of the 2 * rad + 1 around it.
 * Nothing is beyond the ends: what a box reaches past them is zero, and what
 * it would carry past them is lost, so a line that is to keep its light is
 * padded by as far as the passes reach (blur_shadow's `m`). A running sum,
 * so a pass costs the same at any width. `line` is scratch for `n` values. */
void blur_pass(float *p, int n, int stride, int rad, float *line);

/* Three passes each way over a `w` x `h` grid, rows `w` apart: close to a
 * Gaussian of half the box's width, which is what CSS draws a box-shadow of
 * the same numbers with, and reaching 3 * `rad` past anything that was not
 * zero. `rad` 0 or less leaves the grid as it was. `line` is scratch for the
 * longer of `w` and `h`. */
void blur_box3(float *a, int w, int h, int rad, float *line);

/* The same three passes each way with a radius of their own: `rad[0]`, then
 * `rad[1]`, then `rad[2]`, reaching their sum. blur_box3 is this with three
 * of one. A radius of 0 or less is a pass that leaves the grid as it was. */
void blur_box3_by(float *a, int w, int h, const int rad[3], float *line);

/* Units a texel of a shadow's texture is drawn over: the shadows are made at
 * half the size they are drawn, and the texture's linear filtering draws
 * them back up. A blur 48 units wide has nothing in it finer than a few
 * units, so a texel of two loses none of it, and the three textures are a
 * quarter of the texels to blur and to upload: on Beard they are made before
 * the first frame, at every boot and after every fallback game.
 * tools/blur-check.c draws them back up as the GPU does and holds them to
 * the shadow made a texel a unit. */
#define BLUR_UNIT 2

/* The texture a shadow of `blur` units under a rect with a corner of `radius`
 * units is made in, a texel to `unit` units (BLUR_UNIT, or 1 for the shadow
 * at full size that blur-check holds it to). Square, and as small as can be
 * cut into nine pieces exactly; all in texels but `unit`:
 *
 *   rad  the three boxes' radii. DESIGN.md's blur is a box's width, `blur`
 *        + 1 units, three times; what makes the shadow's softness is how far
 *        the three spread light together (their variances add), and a box a
 *        whole number of texels wide is not `blur` + 1 units at any texel
 *        but one. So the three are the whole texels whose spread comes
 *        nearest: at a unit a texel, blur / 2 three times, as it always was;
 *        at two, for every kind DESIGN.md 7 has, two at blur / 4 and one a
 *        texel narrower, whose blur is within 1.6% of the width it has at
 *        full size, where three at blur / 4 make it 1.5% to 4.5% wider
 *   m    how far the three boxes reach past the rect, their sum: the padding
 *   k    a corner piece's side: m of blur outside the rect, and inside it
 *        the corner's own `radius` (in texels, rounded up) plus the m a
 *        texel there gathers from. Past that, every texel along an edge saw
 *        the same straight line through the blur, so stretching one is what
 *        a longer rect would have cast
 *   n    the texture's side: two corners and the two texels between */
typedef struct {
	int unit, rad[3], m, k, n;
} blur_shadow;

blur_shadow blur_shadow_size(int blur, int radius, int unit);

/* How far the shadow reaches past its rect on the screen, in units: m texels
 * of `unit` units. The extent blur_slice cuts is the rect grown by this each
 * way. */
static inline int blur_pad(const blur_shadow *s)
{
	return s->m * s->unit;
}

/* The rect the shadow is of, before any blur: 1 inside [m, n - m) each way
 * with its corners rounded by `radius` units (radius / unit texels), 0
 * outside; a texel is in by its center. No antialiasing: the blur is wider
 * than any step. `a` is n * n values, rows n apart. */
void blur_shadow_rect(float *a, const blur_shadow *s, int radius);

/* The whole shadow: the rect, then blur_box3_by at `rad`. `line` is scratch
 * for n values. */
void blur_shadow_fill(float *a, const blur_shadow *s, int radius, float *line);

typedef struct {
	int x, y, w, h;
} blur_rect;

/* The nine pieces the texture is drawn as, to cover the shadow's whole
 * extent at (`ox`, `oy`), `ow` x `oh` units (the rect it is under, grown by
 * blur_pad each way and dropped): `src` in the texture's texels and `dst` on
 * the screen's units, row by row from the top left. Corners at `unit` units
 * a texel, edges stretched along their length, the middle both ways.
 *
 * An extent too small for two whole corners each way - a one-line message
 * under a 64-unit panel blur - gets each corner cut to the whole texels in
 * half of it, which keeps the rim right and is a touch dark in the middle,
 * where the rect itself covers it; what is left between the two, less than
 * two texels' units, is the middle. A piece with nothing to cover is 0 wide
 * or high, and is not drawn. */
void blur_slice(const blur_shadow *s, int ox, int oy, int ow, int oh,
                blur_rect src[9], blur_rect dst[9]);

/* A shape made of triangles, as a grid of `w` x `h` texels for a shadow in
 * its own shape (Zyper's on Beard, src/main.c): each texel of `a` set to the
 * share of it, 0 to 1, that any of the `n` / 3 triangles in `xy` covers, by
 * 4 x 4 samples a texel. `xy` is x, y pairs in texels, three to a triangle,
 * as SDL_RenderGeometry takes a list with no indices, in either winding.
 * A union and not a sum: a shape drawn over another casts one shadow, and an
 * edge two triangles share is covered once. False, with `a` all zero, when
 * there is no memory for the samples. */
bool blur_fill_tris(float *a, int w, int h, const float *xy, int n);

#endif
