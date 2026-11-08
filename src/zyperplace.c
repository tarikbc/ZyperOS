/* SPDX-License-Identifier: GPL-3.0-only */
/* Where Zyper's vertices go for a pose. See src/zyperplace.h. */
#include "zyperplace.h"

#include "zypermark.h"

#include <math.h>

void zyper_place(float cx, float cy, float h, const zr_pose *p, float brow,
                 float *xy)
{
	const float k = h / ZYPER_H;
	const float c = cosf(p->tilt * (float)M_PI / 180.0f);
	const float s = sinf(p->tilt * (float)M_PI / 180.0f);
	const float ec = cosf(p->ears * (float)M_PI / 180.0f);
	const float es = sinf(p->ears * (float)M_PI / 180.0f);
	const float shut = 1.0f - ZYPER_EYE_SHUT * (p->blink > 1.0f ? 1.0f : p->blink);
	const float mshut = 1.0f - ZYPER_MOUTH_SHUT * (p->mouth > 1.0f ? 1.0f : p->mouth < 0.0f ? 0.0f : p->mouth);
	const float chin = cy + h / 2.0f;
	const float wide = p->squash > 0.0f ? 1.0f / sqrtf(p->squash) : 1.0f;
	int i;

	for (i = 0; i < ZYPER_NVERT; i++) {
		float x = zyper_verts[i].x, y = zyper_verts[i].y, sx, sy;

		if (i >= ZYPER_TONGUE_FIRST)
			y = ZYPER_TONGUE_Y0 + (y - ZYPER_TONGUE_Y0) * p->tongue;
		if (i >= ZYPER_BROW_FIRST && i < ZYPER_BROW_END)
			y += ZYPER_BROW_LIFT - brow;
		if (i >= ZYPER_EYES_FIRST && i < ZYPER_EYES_END)
			y = ZYPER_EYE_Y + (y - ZYPER_EYE_Y) * shut;
		if (i >= ZYPER_MOUTH_FIRST && i < ZYPER_MOUTH_END)
			y = ZYPER_MOUTH_Y + (y - ZYPER_MOUTH_Y) * mshut;
		if (i >= ZYPER_EARS_FIRST && i < ZYPER_EARS_END && p->ears != 0.0f) {
			/* Each ear about its own fold, the far one the mirror way */
			const float m = x < 0.0f ? 1.0f : -1.0f;
			const float fx = ZYPER_EAR_FOLD_X * m, fy = ZYPER_EAR_FOLD_Y;
			const float ux = x - fx, uy = y - fy;

			x = fx + ux * ec - uy * es * m;
			y = fy + ux * es * m + uy * ec;
		}
		/* Tilt about his origin, then center on the tilted extent, the same
		 * order tools/zyperdef.py draws him in; then the squash about his
		 * chin and the move, the order the Zyper lab drew them in. */
		sx = cx + (x * c - y * s - ZYPER_CX) * k;
		sy = cy + (x * s + y * c - ZYPER_CY) * k;
		xy[i * 2 + 0] = cx + (sx - cx) * wide;
		xy[i * 2 + 1] = chin + (sy - chin) * p->squash + p->dy;
	}
}
