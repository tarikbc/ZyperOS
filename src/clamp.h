/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_CLAMP_H
#define ZYPEROS_CLAMP_H

/* A value held within [lo, hi]. Header-only, so the drawing modules, the
 * device files and the checks that link them need nothing more.
 *
 * src/osdbar.c keeps its own: diatom/port/osdbar.c is a copy of it, whole,
 * and Diatom has no copy of this. */
static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clamp01(float v) { return clampf(v, 0.0f, 1.0f); }

static inline int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

#endif
