/* SPDX-License-Identifier: GPL-3.0-only */
/* See cardgen.h. */
#include "cardgen.h"

#include <stddef.h>

#include "theme.h"   /* theme_at, inline: nothing of theme.c is linked */

/* How far toward the ground the foot of the slab goes: halfway. */
#define FOOT 0.5f

static uint8_t mix(uint8_t a, uint8_t b, float t)
{
	return (uint8_t)((float)a + ((float)b - (float)a) * t + 0.5f);
}

ds_rgb cardgen_slab(int theme_ix, float k)
{
	const ds_theme *t = theme_at(theme_ix);
	float f;

	k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
	f = FOOT * k;
	return (ds_rgb){ mix(t->surface.r, t->ground.r, f),
	                 mix(t->surface.g, t->ground.g, f),
	                 mix(t->surface.b, t->ground.b, f) };
}

ds_rgb cardgen_title(int theme_ix)
{
	return theme_at(theme_ix)->text;
}

bool cardgen_kept(int made_for, int on)
{
	return made_for < 0 || made_for == on;
}
