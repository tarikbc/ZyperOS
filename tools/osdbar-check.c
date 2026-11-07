/* The volume and brightness bar: what src/osdbar.c draws, without a screen.
 *
 * Three programs show this one drawing - the launcher, Diatom over a game, and
 * portkeys over a port - and only the first can be looked at off the device.
 * So what makes the bar mean what it says is held here instead: a segment lit
 * for every step up to the level and no further, at both scales it is drawn
 * at; nothing drawn outside the buffer's margin, which the panel's cursor
 * layer would show as a box; the colors where they belong, read from the
 * design tokens (src/design.h) in BOTH themes, so a theme that is a wrong
 * index or a color left as a literal shows; the rim as wide as DESIGN.md's
 * frame; a fade step, which only converts the bar painted before it, the
 * same pixels as the bar painted fresh, and any other change painted again;
 * and its life, in and held and out, at the moments the hosts ask about.
 *
 *   osdbar-check                checks
 *   osdbar-check --pam DIR      also writes the bars it drew as PAM images
 *
 * Links src/osdbar.c and NOT SDL. */
#include "../src/design.h"
#include "../src/osdbar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static const char *pam_dir;

static void dump(const char *name, const uint32_t *px, int w, int h)
{
	char path[512];
	FILE *f;
	int i;

	if (!pam_dir) return;
	snprintf(path, sizeof path, "%s/%s.pam", pam_dir, name);
	if (!(f = fopen(path, "wb"))) return;
	fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
	for (i = 0; i < w * h; i++) {
		unsigned char p[4] = { px[i] >> 16 & 255, px[i] >> 8 & 255, px[i] & 255, px[i] >> 24 };
		fwrite(p, 1, 4, f);
	}
	fclose(f);
}

/* The pill's geometry as src/osdbar.c lays it out, to find a segment's middle */
#define SEG_X0 (12.0f + 84.0f)
#define SEG_X1 (12.0f + 452.0f - 30.0f)
#define MID_Y  (12.0f + 32.0f)

static uint32_t at(const uint32_t *px, int w, float scale, float ux, float uy)
{
	return px[(int)(uy * scale) * w + (int)(ux * scale)];
}

static int is_color(uint32_t p, int r, int g, int b)
{
	return abs((int)(p >> 16 & 255) - r) < 6 && abs((int)(p >> 8 & 255) - g) < 6 &&
	       abs((int)(p & 255) - b) < 6 && (p >> 24) > 250;
}

static const char *tag(int theme)
{
	return theme == DS_LIGHT ? "light" : "dark";
}

static int is_rgb(uint32_t p, ds_rgb c)
{
	return is_color(p, c.r, c.g, c.b);
}

/* A lit segment is the theme's accent-ink for volume and its light for
 * brightness, an unlit one its track: the colors are the function's, not the
 * system's, so one control does not change color as it is scrolled past. */
static void segments(int theme, float scale, int kind, int level, int max)
{
	static uint32_t px[476 * 92];
	const ds_theme *th = &DS_THEME[theme];
	ds_rgb on = kind == OSDBAR_BRIGHT ? th->light : th->accent_ink;
	char what[160], name[64];
	int w, h, i, x, y, edge_empty = 1;
	float sw = (SEG_X1 - SEG_X0 - 4.0f * (max - 1)) / max;

	osdbar_size(scale, &w, &h);
	osdbar_render(px, w, h, w, scale, theme, kind, level, max, 1.0f, false);
	snprintf(name, sizeof name, "%s-%s-%d-of-%d-at-%.3f", tag(theme),
	         kind == OSDBAR_BRIGHT ? "bright" : "volume", level, max, scale);
	dump(name, px, w, h);
	for (i = 0; i < max; i++) {
		uint32_t p = at(px, w, scale, SEG_X0 + i * (sw + 4) + sw / 2, MID_Y);
		snprintf(what, sizeof what, "%s: segment %d %s", name, i + 1, i < level ? "lit" : "dark");
		ck(is_rgb(p, i < level ? on : th->track), what);
	}
	/* The outermost ring of pixels: the margin is there so nothing reaches it */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && (px[y * w + x] >> 24) > 2)
				edge_empty = 0;
	snprintf(what, sizeof what, "%s: nothing at the buffer's edge", name);
	ck(edge_empty, what);
}

/* Near enough to a color to be it: antialiasing and the shadow showing
 * through the body's 7% move a channel by a few steps, not six. */
static int near_rgb(uint32_t p, ds_rgb c)
{
	return abs((int)(p >> 16 & 255) - c.r) < 6 && abs((int)(p >> 8 & 255) - c.g) < 6 &&
	       abs((int)(p & 255) - c.b) < 6;
}

/* The pill itself: its body the theme's surface at 93% (a little more where
 * the shadow shows through it), its rim the theme's edge, and the shadow
 * under it the theme's own opacity - 40% in Salt & Pepper, 18% in Beard,
 * where a shadow is the only lift a light ground gives. The shadow is read
 * 1.5 units under the pill, where the body has ended and the shadow's own
 * lower edge, 4 units lower and 8 units soft, is still 81% there. */
static void pill(int theme, float scale)
{
	static uint32_t px[476 * 92];
	const ds_theme *th = &DS_THEME[theme];
	char what[160];
	int w, h;
	uint32_t body, shadow;
	float shows = 0.07f * th->bar_shadow / 255.0f;   /* the shadow through the body */

	osdbar_size(scale, &w, &h);
	osdbar_render(px, w, h, w, scale, theme, OSDBAR_VOLUME, 10, 20, 1.0f, false);

	body = at(px, w, scale, SEG_X0 + 50, 12.0f + 8.0f);
	snprintf(what, sizeof what, "%s at %.3f: the body is the surface", tag(theme), scale);
	ck(near_rgb(body, th->surface), what);
	snprintf(what, sizeof what, "%s at %.3f: 93%% opaque, plus what the shadow adds through it", tag(theme), scale);
	ck(abs((int)(body >> 24) - (int)((0.93f + shows) * 255 + 0.5f)) <= 2, what);

	shadow = at(px, w, scale, SEG_X0 + 50, 12.0f + 64.0f + 1.5f);
	snprintf(what, sizeof what, "%s at %.3f: the shadow is the theme's opacity", tag(theme), scale);
	ck(abs((int)(shadow >> 24) - (int)(0.8125f * th->bar_shadow + 0.5f)) <= 3, what);

	if (scale == 1.0f) {
		uint32_t rim = at(px, w, scale, SEG_X0 + 50, 12.0f + 1.0f);
		/* The frame is DESIGN.md 3.2's: edge is the DS_FRAME-unit line
		 * around a surface. The pixel row DS_FRAME - 1 units in (its middle
		 * 2.5 in) is still the edge, the one a unit past it the surface */
		uint32_t rim_in = at(px, w, scale, SEG_X0 + 50, 12.0f + DS_FRAME - 1.0f);
		uint32_t past = at(px, w, scale, SEG_X0 + 50, 12.0f + DS_FRAME + 1.0f);
		snprintf(what, sizeof what, "%s: the rim is the edge", tag(theme));
		ck(near_rgb(rim, th->edge), what);
		snprintf(what, sizeof what, "%s: the rim is DS_FRAME (%d) units wide", tag(theme), DS_FRAME);
		ck(near_rgb(rim_in, th->edge) && near_rgb(past, th->surface), what);
	}
}

/* The bar is painted once and then only converted while it fades: a host
 * that fades by redrawing asks for the same bar at a new opacity on every
 * frame of the fade, and the opacity (and premultiplying) is the last step.
 * So a fade step must give exactly the pixels of the same bar painted fresh
 * at that opacity, and a change to anything else must paint it again.
 * "Fresh" is a render whose call before it differed in every input. */
typedef struct { int theme, kind, level, max; float scale; } bar_in;

static void draw(uint32_t *px, bar_in b, float alpha, bool premul)
{
	int w, h;

	osdbar_size(b.scale, &w, &h);
	osdbar_render(px, w, h, w, b.scale, b.theme, b.kind, b.level, b.max, alpha, premul);
}

static void fresh(uint32_t *px, bar_in b, float alpha, bool premul)
{
	bar_in z = { 1 - b.theme, b.kind == OSDBAR_VOLUME ? OSDBAR_BRIGHT : OSDBAR_VOLUME, 2, 7,
	             b.scale == 1.0f ? 0.625f : 1.0f };
	static uint32_t scratch[476 * 92];

	draw(scratch, z, 1.0f, false);
	draw(px, b, alpha, premul);
}

static void repaints(void)
{
	static uint32_t want[476 * 92], got[476 * 92], before[476 * 92];
	const bar_in x = { DS_DARK, OSDBAR_VOLUME, 13, 20, 0.625f };
	const bar_in v[] = {
		{ DS_LIGHT, OSDBAR_VOLUME, 13, 20, 0.625f },
		{ DS_DARK, OSDBAR_BRIGHT, 13, 20, 0.625f },
		{ DS_DARK, OSDBAR_VOLUME, 14, 20, 0.625f },
		{ DS_DARK, OSDBAR_VOLUME, 13, 19, 0.625f },
		{ DS_DARK, OSDBAR_VOLUME, 13, 20, 1.0f },
	};
	const char *field[] = { "the theme", "the kind", "the level", "the max", "the scale" };
	const float fades[] = { 0.4f, 0.05f, 0.0f, 1.0f };
	char what[160];
	int w, h, i, k;
	size_t n;

	osdbar_size(x.scale, &w, &h);
	n = (size_t)w * h * sizeof want[0];
	for (k = 0; k < 4; k++) {
		int premul;
		for (premul = 0; premul < 2; premul++) {
			fresh(want, x, fades[k], premul);
			draw(got, x, 1.0f, false);           /* the bar at full ... */
			draw(got, x, 0.7f, !premul);         /* ... a step of the fade ... */
			draw(got, x, fades[k], premul);      /* ... and the step asked about */
			snprintf(what, sizeof what, "a fade step to %.2f%s is the bar painted fresh at it",
			         fades[k], premul ? ", premultiplied," : "");
			ck(!memcmp(want, got, n), what);
		}
	}
	/* x painted, then v asked for, then v painted fresh to compare with: a
	 * field the buffer were kept across would give x's pixels for v */
	fresh(before, x, 1.0f, false);
	for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++) {
		int vw, vh;

		osdbar_size(v[i].scale, &vw, &vh);
		fresh(got, x, 1.0f, false);
		draw(got, v[i], 1.0f, false);
		fresh(want, v[i], 1.0f, false);
		snprintf(what, sizeof what, "a change to %s paints the bar again", field[i]);
		ck(!memcmp(want, got, (size_t)vw * vh * sizeof want[0]), what);
		if (vw == w && vh == h) {
			snprintf(what, sizeof what, "and %s changes what is drawn", field[i]);
			ck(memcmp(before, want, n) != 0, what);
		}
	}
}

int main(int argc, char **argv)
{
	static uint32_t px[476 * 92];
	static uint32_t other[476 * 92];
	float a, rise;
	int w, h, t;

	if (argc == 3 && !strcmp(argv[1], "--pam")) pam_dir = argv[2];

	osdbar_size(1.0f, &w, &h);
	ck(w == OSDBAR_W && h == OSDBAR_H, "the buffer is the design's size at scale 1");
	osdbar_size(0.625f, &w, &h);
	ck(w == 298 && h == 58, "and 298 x 58 on the Pixel 2's panel");

	for (t = 0; t < DS_THEMES; t++) {
		segments(t, 1.0f, OSDBAR_VOLUME, 0, 20);
		segments(t, 1.0f, OSDBAR_VOLUME, 13, 20);
		segments(t, 1.0f, OSDBAR_VOLUME, 20, 20);
		segments(t, 0.625f, OSDBAR_VOLUME, 7, 20);
		segments(t, 1.0f, OSDBAR_BRIGHT, 0, 11);
		segments(t, 0.625f, OSDBAR_BRIGHT, 6, 11);
		segments(t, 0.625f, OSDBAR_BRIGHT, 11, 11);
		pill(t, 1.0f);
		pill(t, 0.625f);
	}
	repaints();

	/* A theme that is no theme draws the default, Beard, rather than past
	 * the table: the index comes from a file another process writes */
	osdbar_size(1.0f, &w, &h);
	osdbar_render(px, w, h, w, 1.0f, DS_LIGHT, OSDBAR_VOLUME, 5, 20, 1.0f, false);
	osdbar_render(other, w, h, w, 1.0f, 99, OSDBAR_VOLUME, 5, 20, 1.0f, false);
	ck(!memcmp(px, other, (size_t)w * h * sizeof px[0]), "an unknown theme draws Beard");
	osdbar_render(other, w, h, w, 1.0f, -1, OSDBAR_VOLUME, 5, 20, 1.0f, false);
	ck(!memcmp(px, other, (size_t)w * h * sizeof px[0]), "and so does a negative one");

	/* Premultiplied and faded: no channel above the alpha, and half the alpha
	 * at half the fade, which is what the display controller blends with */
	osdbar_size(0.625f, &w, &h);
	osdbar_render(px, w, h, w, 0.625f, DS_DARK, OSDBAR_VOLUME, 10, 20, 0.5f, true);
	{
		uint32_t p = at(px, w, 0.625f, SEG_X0 + 2, MID_Y);
		int ok = 1, i;
		for (i = 0; i < w * h; i++) {
			uint32_t q = px[i], A = q >> 24;
			if ((q >> 16 & 255) > A || (q >> 8 & 255) > A || (q & 255) > A) ok = 0;
		}
		ck(ok, "premultiplied: no channel brighter than its alpha");
		ck(abs((int)(p >> 24) - 128) <= 2, "half the fade, half the alpha");
	}

	/* Its life */
	ck(osdbar_envelope(0, 0, &a, &rise) && a == 0 && rise == OSDBAR_RISE,
	   "appearing: clear, and a slide above its place");
	ck(osdbar_envelope(OSDBAR_IN_MS / 2, OSDBAR_IN_MS / 2, &a, &rise) &&
	   fabsf(a - ds_out(0.5f)) < 0.01f, "arrives on the quintic ease-out");
	ck(osdbar_envelope(OSDBAR_IN_MS / 2, OSDBAR_IN_MS / 2, &a, &rise) &&
	   fabsf(rise - (1 - ds_out(0.5f)) * OSDBAR_RISE) < 0.01f, "and the slide is on the same curve");
	ck(osdbar_envelope(OSDBAR_IN_MS, OSDBAR_IN_MS, &a, &rise) && a == 1 && rise == 0,
	   "in: whole and in place");
	ck(OSDBAR_IN_MS == DS_MS_QUICK && OSDBAR_HOLD_MS == DS_MS_HOLD_BAR &&
	   OSDBAR_OUT_MS == DS_MS_FADE_OUT, "its times are the design's");
	ck(osdbar_envelope(5000, OSDBAR_HOLD_MS, &a, &rise) && a == 1,
	   "held until the hold is up");
	ck(osdbar_envelope(5000, OSDBAR_HOLD_MS + DS_MS_FADE_OUT / 2, &a, &rise) &&
	   fabsf(a - 0.5f) < 0.01f && rise == 0, "half way out, half there, no slide");
	ck(!osdbar_envelope(5000, DS_MS_HOLD_BAR + DS_MS_FADE_OUT, &a, &rise), "then gone");
	ck(osdbar_next_change(10, 10) == 0, "moving while it comes in");
	ck(osdbar_next_change(500, 400) == OSDBAR_HOLD_MS - 400, "still until the fade");
	ck(osdbar_next_change(5000, OSDBAR_HOLD_MS + 10) == 0, "moving while it goes");

	if (fails) { printf("osdbar: %d FAILED\n", fails); return 1; }
	printf("osdbar: ok\n");
	return 0;
}
