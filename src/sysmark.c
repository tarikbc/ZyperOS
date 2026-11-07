/* SPDX-License-Identifier: GPL-3.0-only */
/* See sysmark.h. */
#include "sysmark.h"

#include "config.h"  /* CFG_NO_COLOR: a system with no color of its own */
#include "theme.h"   /* theme_at, inline: nothing of theme.c is linked */

static ds_rgb rgb_of(unsigned c)
{
	return (ds_rgb){ (uint8_t)(c >> 16 & 255), (uint8_t)(c >> 8 & 255),
	                 (uint8_t)(c & 255) };
}

static unsigned hex(ds_rgb c)
{
	return (unsigned)c.r << 16 | (unsigned)c.g << 8 | c.b;
}

/* `t` of the way from `a` to `b`, cut down to a whole number as src/ui.c's
 * ui_mix cuts it, so a mark is the same pixels it was when the rule lived
 * there. */
static uint8_t toward(uint8_t a, uint8_t b, float t)
{
	return (uint8_t)(int)((int)a + ((int)b - (int)a) * t);
}

/* System-colored interface marks use this rule. Source console sprites and
 * game covers retain their original colors and do not pass through it. */
ds_rgb sysmark(unsigned rgb, int theme_ix)
{
	const ds_theme *t = theme_at(theme_ix);
	const ds_rgb c = rgb_of(rgb);

	if (rgb == CFG_NO_COLOR) return t->text_dim;
	if (rgb == hex(t->accent)) return t->accent_ink;
	if (theme_clamp(theme_ix) == DS_DARK) return c;
	return (ds_rgb){ toward(c.r, t->text.r, 0.25f), toward(c.g, t->text.g, 0.25f),
	                 toward(c.b, t->text.b, 0.25f) };
}
