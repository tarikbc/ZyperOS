/* SPDX-License-Identifier: GPL-3.0-only */
/* On-Screen Stats (src/hud.h): its line for each item, the speed as a
 * multiplier and none at full speed, rounded and clamped; the items kept
 * as words; every style's place, at both scales, for every set of items,
 * inside the screen, clear of the level bar and the low-battery disc, and
 * where it was as the numbers change; its colors where they should be; and
 * Diatom's copies of the three files the same as ZyperOS's.
 *
 *   hud-check                           the checks
 *   hud-check --dump FILE TEXT SCALE THEME ITEMS CORNER EDGE BG OPACITY COLOR SIZE
 *                                       the line as raw RGBA after a
 *                                       "W H X Y" line, X Y its place on the
 *                                       1024x768-unit screen at SCALE, for a
 *                                       sheet (THEME 0 Beard, 1 Salt &
 *                                       Pepper; ITEMS "fps,speed"; the rest
 *                                       the style's words, EDGE 0-2 and
 *                                       OPACITY 0-2). TEXT "-" is the line
 *                                       for sample numbers and ITEMS.
 *   hud-check --bar FILE SCALE THEME LEVEL
 *                                       the volume bar the same way, "W H" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/hud.h"
#include "../src/osdbar.h"

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int dump(const char *file, const uint32_t *px, int w, int h, const char *head)
{
	FILE *f = fopen(file, "wb");
	int i;

	if (!f) return 1;
	fprintf(f, "%s\n", head);
	for (i = 0; i < w * h; i++) {
		unsigned char c[4] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8),
		                       (unsigned char)px[i], (unsigned char)(px[i] >> 24) };
		fwrite(c, 1, 4, f);
	}
	return fclose(f) != 0;
}

/* Whether two files are byte for byte the same */
static int same_file(const char *a, const char *b)
{
	FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
	int ca, cb, same = fa && fb;

	while (same && ((int)((ca = fgetc(fa)) != EOF) | (int)((cb = fgetc(fb)) != EOF)))
		if (ca != cb) same = 0;
	if (fa) fclose(fa);
	if (fb) fclose(fb);
	return same;
}

/* Numbers nothing knows, but what a case sets */
static hud_values none(void)
{
	hud_values v = { -1, -1, -1, -1, -1, -1, -1, -1, true };
	return v;
}

static void text_is(const hud_values *v, uint8_t items, const char *want, const char *what)
{
	char t[160], msg[256];

	hud_text(t, sizeof t, v, items);
	snprintf(msg, sizeof msg, "%s: \"%s\", wanted \"%s\"", what, t, want);
	ck(!strcmp(t, want), msg);
}

#define I(x) (1u << HUD_##x)

/* Whether two boxes meet, in pixels */
static int meets(int x, int y, int w, int h, float l, float t, float r, float b)
{
	return x < r && x + w > l && y < b && y + h > t;
}

static uint8_t A(uint32_t p) { return (uint8_t)(p >> 24); }
static int is_rgb(uint32_t p, ds_rgb c)
{
	return ((p >> 16) & 255) == c.r && ((p >> 8) & 255) == c.g && (p & 255) == c.b;
}

int main(int argc, char **argv)
{
	static uint32_t px[1024 * 128];
	int w, h, i;

	if (argc == 13 && !strcmp(argv[1], "--dump")) {
		const float s = (float)atof(argv[4]);
		const int th = atoi(argv[5]);
		hud_style st = hud_default;
		char text[160];
		int x, y, k;
		char head[64];

		st.items = hud_items_parse(argv[6]);
		k = hud_find(hud_corner_id, HUD_CORNERS, argv[7]);
		if (k >= 0) st.corner = (uint8_t)k;
		st.edge = (uint8_t)atoi(argv[8]);
		k = hud_find(hud_background_id, HUD_BACKGROUNDS, argv[9]);
		if (k >= 0) st.background = (uint8_t)k;
		st.opacity = (uint8_t)atoi(argv[10]);
		k = hud_find(hud_color_id, HUD_COLORS, argv[11]);
		if (k >= 0) st.color = (uint8_t)k;
		k = hud_find(hud_size_id, HUD_SIZES, argv[12]);
		if (k >= 0) st.size = (uint8_t)k;
		hud_clamp(&st);
		if (!strcmp(argv[3], "-")) {
			const hud_values v = { 57.4, 0.96, 52, 34, 1296, 87, 22, 8, true };
			hud_text(text, sizeof text, &v, st.items);
		} else {
			snprintf(text, sizeof text, "%s", argv[3]);
		}
		hud_size(text, s, &st, &w, &h);
		hud_render(px, w, h, w, s, text, &st, &DS_THEME[th == 1 ? DS_DARK : DS_LIGHT], false);
		hud_place(&st, s, w, h, (int)(1024 * s + 0.5f), (int)(768 * s + 0.5f), &x, &y);
		snprintf(head, sizeof head, "%d %d %d %d", w, h, x, y);
		return dump(argv[2], px, w, h, head);
	}
	if (argc == 6 && !strcmp(argv[1], "--bar")) {
		static uint32_t bar[1024 * 256];
		const float s = (float)atof(argv[3]);
		char head[32];

		osdbar_size(s, &w, &h);
		osdbar_render(bar, w, h, w, s, atoi(argv[4]), OSDBAR_VOLUME, atoi(argv[5]), 20, 1.0f, false);
		snprintf(head, sizeof head, "%d %d", w, h);
		return dump(argv[2], bar, w, h, head);
	}

	printf("its line:\n");
	{
		hud_values v = none();
		const uint8_t FS = I(FPS) | I(SPEED);

		v.fps = 59.73; v.speed = 1.0;
		text_is(&v, FS, "59.7 fps", "full speed: the rate alone");
		v.fps = 59.7; v.speed = 0.985;
		text_is(&v, FS, "59.7 fps", "within the band, still full speed");
		v.fps = 19.84; v.speed = 0.332;
		text_is(&v, FS, "19.8 fps \xc2\xb7 0.33x", "a third of the speed, two decimals");
		v.fps = 30; v.speed = 0.5;
		text_is(&v, FS, "30.0 fps \xc2\xb7 0.5x", "no zero at the end");
		v.fps = 62.9; v.speed = 1.05;
		text_is(&v, FS, "62.9 fps \xc2\xb7 1.05x", "just over, two decimals");
		v.fps = 119.4; v.speed = 2.0;
		text_is(&v, FS, "119.4 fps \xc2\xb7 2x", "fast forward, 2x");
		v.fps = 89.6; v.speed = 1.5;
		text_is(&v, FS, "89.6 fps \xc2\xb7 1.5x", "one decimal from 1.1x");
		v.fps = 5000; v.speed = 5000;
		text_is(&v, FS, "999.9 fps \xc2\xb7 99.9x", "clamped at what it holds");
		v.fps = 755.2; v.speed = 12.64;
		text_is(&v, FS, "755.2 fps \xc2\xb7 12.6x", "Max past 10x: said as it is, not 9.9x");
		v.fps = 59.7; v.speed = 1.0;
		text_is(&v, I(SPEED), "1x", "Speed alone says full speed too");
		v.speed = 0.5;
		text_is(&v, I(SPEED), "0.5x", "Speed alone");
		v = none();
		v.temp_c = 52;
		text_is(&v, I(TEMP), "52\xc2\xb0" "C", "the temperature");
		v = none();
		v.cpu_pct = 34;
		text_is(&v, I(CPU), "CPU 34%", "the load");
		v = none();
		v.cpu_mhz = 1296;
		text_is(&v, I(CLOCK), "1.3 GHz", "the clock in GHz");
		v.cpu_mhz = 816;
		text_is(&v, I(CLOCK), "816 MHz", "under a GHz, in MHz");
		v = none();
		v.battery_pct = 87;
		text_is(&v, I(BATTERY), "\xee\x80\x89" "87%", "the battery, drawn at its ninth tenth");
		v = none();
		v.hour = 22; v.min = 8; v.clock24 = true;
		text_is(&v, I(TIME), "22:08", "the time, 24 hours");
		v.clock24 = false;
		text_is(&v, I(TIME), "10:08 PM", "the time, 12 hours");
		v.hour = 0;
		text_is(&v, I(TIME), "12:08 AM", "midnight, 12 hours");
		v = none();
		v.fps = 59.7; v.speed = 0.96; v.cpu_pct = 34;
		text_is(&v, I(FPS) | I(SPEED) | I(TEMP) | I(CPU),
		        "59.7 fps \xc2\xb7 0.96x \xc2\xb7 CPU 34%", "an item not known is left out");
		v = none();
		text_is(&v, (uint8_t)((1u << HUD_ITEMS) - 1), "", "nothing known, nothing said");
	}

	printf("its items as words:\n");
	{
		char t[96];

		hud_items_str(I(FPS) | I(TEMP) | I(TIME), t, sizeof t);
		ck(!strcmp(t, "fps,temp,time"), "the items' words, in order");
		ck(hud_items_parse("fps,temp,time") == (I(FPS) | I(TEMP) | I(TIME)), "and back");
		ck(hud_items_parse("time,bogus,fps") == (I(FPS) | I(TIME)), "an unknown word skipped");
		ck(hud_items_parse("") == hud_default.items, "none is the default's");
	}

	printf("every style's place, at both scales, every set of items:\n");
	{
		int bad_in = 0, bad_bar = 0, bad_disc = 0, bad_still = 0, bad_buf = 0, n = 0;
		int c, e, b, sz, items;

		for (i = 0; i < 2; i++) {
			const float s = i ? 0.625f : 1.0f;
			const int sw = (int)(1024 * s + 0.5f), sh = (int)(768 * s + 0.5f);
			/* The level bar's pill and the disc, in pixels */
			const float bl = (1024 - 452) / 2.0f * s, br = bl + 452 * s, bb = 86 * s;
			const float dcx = sw - 34 * s / 0.625f, dcy = 34 * s / 0.625f, dr = 9 * s / 0.625f;

			for (items = 1; items < (1 << HUD_ITEMS); items++)
			for (c = 0; c < HUD_CORNERS; c++)
			for (e = 0; e < HUD_EDGES; e++)
			for (b = 0; b < HUD_BACKGROUNDS; b++)
			for (sz = 0; sz < HUD_SIZES; sz++) {
				const hud_style st = { (uint8_t)items, (uint8_t)c, (uint8_t)e, (uint8_t)b, 0, 0,
				                       (uint8_t)sz };
				/* Calm, slow, and the widest each can be */
				const hud_values vs[3] = {
					{ 59.7, 1.0, 41, 7, 600, 9, 9, 5, true },
					{ 19.8, 0.33, 63, 98, 1296, 55, 22, 48, false },
					{ 999.9, 0.99, 100, 100, 999, 100, 12, 59, false },
				};
				int k, x0 = 0, y0 = 0, r0 = 0, b0 = 0;

				for (k = 0; k < 3; k++) {
					char t[160];
					int x, y;

					hud_text(t, sizeof t, &vs[k], (uint8_t)items);
					hud_size(t, s, &st, &w, &h);
					if (!w) continue;
					hud_place(&st, s, w, h, sw, sh, &x, &y);
					n++;
					if (w > 1024 || h > 128) bad_buf++;
					if (x < 0 || y < 0 || x + w > sw || y + h > sh) bad_in++;
					if (meets(x, y, w, h, bl, 0, br, bb)) bad_bar++;
					if (meets(x, y, w, h, dcx - dr, dcy - dr, dcx + dr, dcy + dr)) bad_disc++;
					/* Where it was as the numbers changed: the same
					 * edge on each side its corner holds */
					if (!k) { x0 = x; y0 = y; r0 = x + w; b0 = y + h; }
					else if ((c == HUD_TOP_RIGHT || c == HUD_BOTTOM_RIGHT ? x + w != r0 : x != x0) ||
					         (c == HUD_TOP_LEFT || c == HUD_TOP_RIGHT ? y != y0 : y + h != b0))
						bad_still++;
				}
			}
		}
		printf("  %d places tried\n", n);
		ck(!bad_buf, "a size the render buffer holds");
		ck(!bad_in, "inside the screen");
		ck(!bad_bar, "clear of the level bar");
		ck(!bad_disc, "clear of the low-battery disc");
		ck(!bad_still, "where it was as the numbers change");
	}
	{
		/* The default, Normal distance in the top left */
		int x, y;

		hud_size("59.7 fps", 0.625f, NULL, &w, &h);
		hud_place(NULL, 0.625f, w, h, 640, 480, &x, &y);
		ck(x == 15 && y == 15, "the default at 24 units from the top left");
	}

	printf("its colors:\n");
	{
		hud_style st = hud_default;
		const char *t = "59.7 fps";
		uint32_t p;

		hud_size(t, 1.0f, &st, &w, &h);
		hud_render(px, w, h, w, 1.0f, t, &st, &DS_THEME[DS_DARK], false);
		ck(A(px[0]) == 0, "a corner outside the pill is clear");
		p = px[(h / 2) * w + (w - 6)];
		ck(A(p) == 255 && is_rgb(p, DS_THEME[DS_DARK].surface), "the pill's surface in the theme's color");

		st.color = HUD_WHITE;
		hud_render(px, w, h, w, 1.0f, t, &st, &DS_THEME[DS_LIGHT], false);
		p = px[(h / 2) * w + (w - 6)];
		ck(A(p) == 255 && is_rgb(p, DS_THEME[DS_DARK].surface),
		   "white text on Beard: Salt & Pepper's pill");

		st = hud_default;
		st.opacity = 2;
		hud_render(px, w, h, w, 1.0f, t, &st, &DS_THEME[DS_DARK], false);
		p = px[(h / 2) * w + (w - 6)];
		ck(A(p) == 128, "at 50%, the pill half there");

		st = hud_default;
		hud_size(t, 1.0f, &st, &w, &h);
		st.background = HUD_SHADOW;
		hud_render(px, w, h, w, 1.0f, t, &st, &DS_THEME[DS_DARK], false);
		{
			int dark = 0, k;

			for (k = 0; k < w * h; k++)
				if (A(px[k]) > 0 && is_rgb(px[k], DS_THEME[DS_DARK].ground)) dark++;
			ck(dark > 0, "with a shadow, the ink around light text");
		}
		{
			/* The battery drawn: something in its box, at its level */
			const char *bt = "\xee\x80\x8a" "100%";
			int any = 0, k, xx, yy;

			st = hud_default;
			st.background = HUD_SHADOW;
			hud_size(bt, 1.0f, &st, &w, &h);
			hud_render(px, w, h, w, 1.0f, bt, &st, &DS_THEME[DS_DARK], false);
			for (yy = h / 3; yy < 2 * h / 3; yy++)
				for (xx = 4; xx < 4 + 20 && xx < w; xx++) any += A(px[yy * w + xx]) > 128;
			ck(any > 40, "the battery drawn, full");
			(void)k;
		}
	}

	printf("Diatom's copies:\n");
	ck(same_file("src/hud.c", "diatom/port/hud.c"), "diatom/port/hud.c is src/hud.c");
	ck(same_file("src/hud.h", "diatom/port/hud.h"), "diatom/port/hud.h is src/hud.h");
	ck(same_file("src/hudglyphs.h", "diatom/port/hudglyphs.h"), "diatom/port/hudglyphs.h is src/hudglyphs.h");

	if (fails) { printf("\n%d FAILED\n", fails); return 1; }
	printf("\nok: On-Screen Stats says each item, in every style, clear of the bar and the disc\n");
	return 0;
}
