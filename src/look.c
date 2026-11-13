/* SPDX-License-Identifier: GPL-3.0-only */
/* The player's look, worked out (src/look.h, DESIGN.md 3.5). */
#include "look.h"

#include <math.h>
#include <string.h>

#include "theme.h"

/* ---- color, in OKLab ----------------------------------------------------- */

typedef struct { float l, a, b; } lab;

static float lin(uint8_t v)
{
	float c = v / 255.0f;

	return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static uint8_t delin(float c)
{
	float v;

	if (c < 0.0f) c = 0.0f;
	if (c > 1.0f) c = 1.0f;
	v = c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
	return (uint8_t)(v * 255.0f + 0.5f);
}

static lab to_lab(ds_rgb c)
{
	float r = lin(c.r), g = lin(c.g), b = lin(c.b);
	float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
	float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
	float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

	return (lab){ 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
	              1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
	              0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

static ds_rgb from_lab(lab x)
{
	float l = x.l + 0.3963377774f * x.a + 0.2158037573f * x.b;
	float m = x.l - 0.1055613458f * x.a - 0.0638541728f * x.b;
	float s = x.l - 0.0894841775f * x.a - 1.2914855480f * x.b;

	l = l * l * l; m = m * m * m; s = s * s * s;
	return (ds_rgb){ delin( 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
	                 delin(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
	                 delin(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s) };
}

static float luminance(ds_rgb c)
{
	return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float look_ratio(ds_rgb a, ds_rgb b)
{
	float x = luminance(a), y = luminance(b);

	return x > y ? (x + 0.05f) / (y + 0.05f) : (y + 0.05f) / (x + 0.05f);
}

static bool same(ds_rgb a, ds_rgb b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* At least `need` on both `on0` and `on1` */
static bool owed(ds_rgb c, ds_rgb on0, ds_rgb on1, float need)
{
	return look_ratio(c, on0) >= need && look_ratio(c, on1) >= need;
}

/* `c` walked in lightness, darker in a light theme and lighter in a dark
 * one, its chroma kept times `keep` a step, until it is `need` on both
 * `on0` and `on1` and (with a track) `track_need` on that; at most 100
 * steps of a hundredth, which takes any color to black or white */
static ds_rgb walk(ds_rgb c, bool light, float keep, ds_rgb on0, ds_rgb on1,
                   float need, const ds_rgb *track, float track_need)
{
	lab x = to_lab(c);
	int i;

	for (i = 0; i < 100; i++) {
		if (owed(c, on0, on1, need) &&
		    (!track || look_ratio(c, *track) >= track_need))
			break;
		x.l += light ? -0.01f : 0.01f;
		x.a *= keep;
		x.b *= keep;
		c = from_lab(x);
	}
	return c;
}

/* ---- a theme from an accent and a tone ---------------------------------- */

void look_theme(int base, ds_rgb ground, ds_rgb accent, ds_theme *out)
{
	const ds_theme *d = &DS_THEME[theme_clamp(base)];
	const bool light = theme_clamp(base) == DS_LIGHT;
	const ds_rgb ink = d->ink, white = DS_THEME[DS_LIGHT].surface;

	*out = *d;
	if (!same(ground, d->ground)) {
		lab g0 = to_lab(d->ground), g = to_lab(ground);
		float dl = g.l - g0.l;
		ds_rgb *surf[4] = { &out->surface, &out->raised, &out->edge, &out->track };
		const ds_rgb src[4] = { d->surface, d->raised, d->edge, d->track };
		ds_rgb *text[3] = { &out->text, &out->text_soft, &out->text_dim };
		const ds_rgb tsrc[3] = { d->text, d->text_soft, d->text_dim };
		int i;

		out->ground = ground;
		/* The surfaces keep their lightness steps from the ground and take
		 * the tone's tint, the panel a little less of it */
		for (i = 0; i < 4; i++) {
			lab r = to_lab(src[i]);
			float k = i == 0 ? 0.7f : 1.0f;

			*surf[i] = from_lab((lab){ r.l + dl, g.a * k, g.b * k });
		}
		/* The text keeps its lightness and leans a third of the way to the
		 * tint, then is walked back to its 4.5:1 */
		for (i = 0; i < 3; i++) {
			lab r = to_lab(tsrc[i]);

			*text[i] = from_lab((lab){ r.l, r.a + (g.a - g0.a) * 0.35f,
			                           r.b + (g.b - g0.b) * 0.35f });
			*text[i] = walk(*text[i], light, 1.0f, out->ground, out->surface,
			                4.5f, NULL, 0.0f);
		}
		/* The marks keep their colors unless the tone took them under
		 * theirs (DESIGN.md 3.2, designdef's TEXT_ON and MARK_ON) */
		out->warning = walk(out->warning, light, 1.0f, out->surface, out->surface, 4.5f,
		                    &out->ground, 3.0f);
		out->favorite = walk(out->favorite, light, 1.0f, out->surface, out->surface, 3.0f,
		                     NULL, 0.0f);
		out->light = walk(out->light, light, 1.0f, out->surface, out->track, 3.0f,
		                  NULL, 0.0f);
		out->store_games = walk(out->store_games, light, 1.0f, out->ground, out->ground,
		                        3.0f, NULL, 0.0f);
		out->store_saves = walk(out->store_saves, light, 1.0f, out->ground, out->ground,
		                        3.0f, NULL, 0.0f);
		out->store_art = walk(out->store_art, light, 1.0f, out->ground, out->ground,
		                      3.0f, NULL, 0.0f);
	}
	if (same(accent, d->accent) && same(ground, d->ground)) return;
	out->accent = accent;
	/* Ink on the fill, or the beard's white where ink would not read */
	out->on_accent = look_ratio(ink, accent) >= 4.5f ? ink : white;
	/* The accent as a mark: Salt & Pepper's lime is its own, and anything
	 * else is walked to 4.5:1 as text and 3:1 on the track, as lime's ink
	 * was chosen for Beard */
	if (!light && same(accent, d->accent) &&
	    owed(accent, out->ground, out->surface, 4.5f) && look_ratio(accent, out->track) >= 3.0f)
		out->accent_ink = accent;
	else
		out->accent_ink = walk(accent, light, light ? 0.99f : 0.98f, out->ground,
		                       out->surface, 4.5f, &out->track, 3.0f);
}

/* ---- the look that is on -------------------------------------------------- */

static look     g_look = LOOK_DEFAULT;
static ds_theme g_live[DS_THEMES];
static bool     g_on;
static unsigned g_gen;

static int clamp_ix(int i, int n)
{
	return i >= 0 && i < n ? i : 0;
}

bool look_is_default(const look *l)
{
	return !l || (clamp_ix(l->accent, DS_ACCENTS) == 0 &&
	              clamp_ix(l->tone[DS_LIGHT], DS_TONES) == 0 &&
	              clamp_ix(l->tone[DS_DARK], DS_TONES) == 0);
}

void look_set(const look *l)
{
	int t;

	g_look = l ? *l : LOOK_DEFAULT;
	g_look.accent = clamp_ix(g_look.accent, DS_ACCENTS);
	for (t = 0; t < DS_THEMES; t++) {
		g_look.tone[t] = clamp_ix(g_look.tone[t], DS_TONES);
		look_theme(t, DS_TONE[t][g_look.tone[t]], DS_ACCENT[g_look.accent], &g_live[t]);
	}
	/* The colors as they ship are the tokens' own tables, by address too */
	g_on = !look_is_default(&g_look);
	g_gen++;
}

unsigned look_gen(void)
{
	return g_gen;
}

const look *look_now(void)
{
	return &g_look;
}

bool look_square(void)
{
	return g_look.square;
}

bool look_zyper_off(void)
{
	return g_look.zyper_off;
}

const ds_theme *theme_at(int ix)
{
	ix = theme_clamp(ix);
	return g_on ? &g_live[ix] : &DS_THEME[ix];
}

static int find(const char *const *ids, int n, const char *s)
{
	int i;

	for (i = 0; s && i < n; i++)
		if (!strcmp(ids[i], s)) return i;
	return 0;
}

look look_parse(const char *accent, const char *light, const char *dark,
                const char *corners, const char *zyper)
{
	look l = LOOK_DEFAULT;

	l.accent = find(DS_ACCENT_ID, DS_ACCENTS, accent);
	l.tone[DS_LIGHT] = find(DS_TONE_ID[DS_LIGHT], DS_TONES, light);
	l.tone[DS_DARK] = find(DS_TONE_ID[DS_DARK], DS_TONES, dark);
	l.square = corners && !strcmp(corners, "square");
	l.zyper_off = zyper && !strcmp(zyper, "off");
	return l;
}
