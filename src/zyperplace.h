/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ZYPERPLACE_H
#define ZYPEROS_ZYPERPLACE_H

/* Where each of Zyper's ZYPER_NVERT vertices (src/zypermark.h) lands for a
 * pose, as plain numbers: about (cx, cy), `h` tall at rest, in the rig's pose
 * `p` (src/zyperrig.h) with the high brow lifted `brow`. `xy` takes 2 floats
 * a vertex, x then y; the colors stay in the header's table.
 *
 * Split out of draw_zyper_pose (src/zyperdraw.c) so the one set of formulas
 * serves everything that draws him: the renderer there, which hands the
 * triangles to SDL, and src/noticeart.c, which fills them into the pixels of
 * a notice over a game, where there is no renderer to hand them to. Neither
 * needs SDL here, and neither is a copy of the other. Every parameter is
 * described at draw_zyper_pose. */
#include "zyperrig.h"

void zyper_place(float cx, float cy, float h, const zr_pose *p, float brow,
                 float *xy);

#endif
