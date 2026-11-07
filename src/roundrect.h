/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_ROUNDRECT_H
#define ZYPEROS_ROUNDRECT_H

/* A rounded rect as rects for ui_round_rect. The shape is the one it always
 * drew: the middle at full width, and each of the `radius` rows above and
 * below it inset by dx = radius - round(sqrt(radius^2 - (radius - y)^2)),
 * which shrinks toward the middle.
 *
 * It went to the renderer as those rows, each one unit tall and nearly the
 * rect's whole width: 57 for MENU's panel, 29 for its focus plate. The same
 * pixels go as rects no wider than a corner, but two: the band of corner rows
 * where all of them reach (the top row's inset), top and bottom, and each
 * corner's steps as columns, column x in every row whose inset is x or less.
 * About as many rects as there were rows, a few more, but each a corner's
 * size and not the whole rect's, and the Pixel 2's GPU draws them sooner:
 * with SDL batching on and no sleep after a present, MENU at rest went from
 * 51.8 fps to 60 (2026-11-05).
 *
 * Every pixel of the shape is in exactly one rect, so it blends exactly as it
 * did; tools/roundrect-check.c holds that for every size up to ROUND_MAX. */

/* A rect, as SDL_Rect is laid out */
typedef struct { int x, y, w, h; } rr_rect;

/* The largest radius laid out this way; a larger one goes row by row. */
#define ROUND_MAX 128
/* The most rects round_rect_rects writes */
#define ROUND_RECTS (3 + 4 * ROUND_MAX)

/* `radius` as the shape takes it: no more than half of either side */
int round_rect_radius(int w, int h, int radius);

/* The rects of the rect at (x, y), w by h, with corners of `radius` (already
 * round_rect_radius'd, at most ROUND_MAX), into `out`; how many. The middle
 * comes first, and may be empty (h == 2 * radius). */
int round_rect_rects(int x, int y, int w, int h, int radius, rr_rect *out);

#endif
