/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* See hud.h */
#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hudglyphs.h"

const char *const hud_item_id[HUD_ITEMS] = { "fps", "speed", "temp", "cpu", "clock", "battery", "time" };
const char *const hud_corner_id[HUD_CORNERS] = { "tl", "tr", "bl", "br" };
const char *const hud_background_id[HUD_BACKGROUNDS] = { "pill", "shadow" };
const char *const hud_color_id[HUD_COLORS] = { "theme", "white", "green", "yellow" };
const char *const hud_size_id[HUD_SIZES] = { "normal", "small" };
const int hud_edge_units[HUD_EDGES] = { 8, 24, 48 };
const int hud_opacity_pct[HUD_OPACITIES] = { 100, 75, 50 };

const hud_style hud_default = { (1u << HUD_FPS) | (1u << HUD_SPEED), HUD_TOP_LEFT, 1, HUD_PILL,
                                0, HUD_THEME, HUD_NORMAL };

/* The pill, in units, at each size: the type with room around it, its ends
 * round, and the edge's rim (DESIGN.md 3.2, DS_FRAME's line, less one unit:
 * the level bar's own, at a third of its size). Without a pill, the text
 * keeps a margin its shadow falls into. */
static const float PAD_X[HUD_SIZES] = { 12, 10 };
static const float PAD_Y[HUD_SIZES] = { 10, 8 };
#define RIM   2.0f
#define BARE_PAD 4.0f
/* The shadow: the text grown this far, in units, in the ink */
#define SHADOW_R 2.0f
#define SHADOW_A 0.8f
/* Units and dots, as text-soft is to text when the color is not the
 * theme's */
#define SOFT_A 0.72f

/* The battery, drawn: a private code point, U+E000 and its level's tenth */
#define CP_BATTERY 0xe000u
#define CP_BATTERY_END (CP_BATTERY + 10)

/* What it keeps clear of, in units (the 1024-unit screen) and with this gap:
 * the level bar's pill (src/osdbar.h: 452 x 64, centered, 22 under the top)
 * and the low-battery disc (pixel2.c's draw_low_battery: radius 9, centered
 * 34 px from the top and right edges of the 640-pixel panel, 0.625 px to the
 * unit) */
#define GAP 8
#define BAR_L ((1024 - 452) / 2)
#define BAR_R (BAR_L + 452)
#define BAR_B (22 + 64)
#define DISC_IN 54   /* 34 / 0.625 */
#define DISC_R  15   /* 9 / 0.625, rounded up */

int hud_find(const char *const *ids, int n, const char *id)
{
	int i;

	for (i = 0; id && i < n; i++)
		if (!strcmp(ids[i], id)) return i;
	return -1;
}

void hud_clamp(hud_style *st)
{
	st->items &= (1u << HUD_ITEMS) - 1;
	if (!st->items) st->items = hud_default.items;
	if (st->corner >= HUD_CORNERS) st->corner = hud_default.corner;
	if (st->edge >= HUD_EDGES) st->edge = hud_default.edge;
	if (st->background >= HUD_BACKGROUNDS) st->background = hud_default.background;
	if (st->opacity >= HUD_OPACITIES) st->opacity = hud_default.opacity;
	if (st->color >= HUD_COLORS) st->color = hud_default.color;
	if (st->size >= HUD_SIZES) st->size = hud_default.size;
}

void hud_items_str(uint8_t items, char *out, size_t n)
{
	size_t k = 0;
	int i;

	if (n) out[0] = 0;
	for (i = 0; i < HUD_ITEMS; i++)
		if (items & (1u << i)) {
			int m = snprintf(out + k, k < n ? n - k : 0, "%s%s", k ? "," : "", hud_item_id[i]);
			if (m > 0) k += (size_t)m;
			if (k >= n) break;
		}
}

uint8_t hud_items_parse(const char *s)
{
	uint8_t items = 0;

	while (s && *s) {
		char w[16];
		size_t k = 0;
		int i;

		while (*s && *s != ',') {
			if (k + 1 < sizeof w) w[k++] = *s;
			s++;
		}
		w[k] = 0;
		if (*s == ',') s++;
		i = hud_find(hud_item_id, HUD_ITEMS, w);
		if (i >= 0) items |= (uint8_t)(1u << i);
	}
	return items ? items : hud_default.items;
}

static const hud_style *style_or_default(const hud_style *st)
{
	return st ? st : &hud_default;
}

/* The table for a size at a scale: that type, at the nearest scale */
static const hudglyph_size *glyphs_for(float scale, int size)
{
	const int type = size == HUD_SMALL ? DS_TYPE_CHROME : DS_TYPE_SUPPORT;
	int i, best = -1;

	for (i = 0; i < HUDGLYPH_NSIZES; i++)
		if (HUDGLYPH_SIZES[i].type == type &&
		    (best < 0 || fabsf(HUDGLYPH_SIZES[i].scale - scale) <
		                 fabsf(HUDGLYPH_SIZES[best].scale - scale)))
			best = i;
	return &HUDGLYPH_SIZES[best < 0 ? 0 : best];
}

static const hudglyph *glyph(const hudglyph_size *s, uint32_t cp)
{
	int i;

	for (i = 0; i < s->n; i++)
		if (s->glyph[i].cp == cp) return &s->glyph[i];
	return NULL;
}

/* The next code point of a UTF-8 string, which is all the line writes:
 * ASCII, the middle dot and the degree sign, and the battery's private
 * code points */
static uint32_t next_cp(const char **p)
{
	const unsigned char *s = (const unsigned char *)*p;
	uint32_t c = *s++;

	if (c >= 0xc0 && c < 0xe0 && (s[0] & 0xc0) == 0x80) {
		c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
	} else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
		c = ((c & 0x0f) << 12) | ((uint32_t)(s[0] & 0x3f) << 6) | (s[1] & 0x3f);
		s += 2;
	}
	*p = (const char *)s;
	return c;
}

static bool is_battery(uint32_t cp)
{
	return cp >= CP_BATTERY && cp <= CP_BATTERY_END;
}

/* A number's characters, drawn in the text's color; the rest are units */
static bool is_number(uint32_t cp)
{
	return (cp >= '0' && cp <= '9') || cp == '.' || cp == ':' || is_battery(cp);
}

/* A digit's cell: the widest digit's advance, in 64ths */
static int digit_cell(const hudglyph_size *s)
{
	int cell = 0, d;

	for (d = '0'; d <= '9'; d++) {
		const hudglyph *g = glyph(s, (uint32_t)d);
		if (g && g->adv64 > cell) cell = g->adv64;
	}
	return cell;
}

/* The battery's box, in pixels from the pen and the line's top: as wide as
 * the type's size, half as tall, on the digits' middle; its advance with a
 * space after */
static void battery_box(const hudglyph_size *s, float *w, float *h)
{
	*w = s->px * 0.92f;
	*h = s->px * 0.48f;
}

static int advance(const hudglyph_size *s, uint32_t cp, int cell)
{
	const hudglyph *g;
	float bw, bh;

	if (cp >= '0' && cp <= '9') return cell;
	if (is_battery(cp)) {
		battery_box(s, &bw, &bh);
		return (int)lroundf((bw + s->px * 0.12f) * 64);
	}
	g = glyph(s, cp);
	return g ? g->adv64 : 0;
}

/* Appends `part` to the line, after a dot when the line has something */
static void add(char *out, size_t n, size_t *k, const char *part)
{
	int m;

	if (*k >= n) return;
	m = snprintf(out + *k, n - *k, "%s%s", *k ? " \xc2\xb7 " : "", part);
	if (m > 0) *k += (size_t)m;
	if (*k >= n) *k = n - 1;
}

static bool known(double v)
{
	return v >= 0;
}

void hud_text(char *out, size_t n, const hud_values *v, uint8_t items)
{
	char part[32];
	size_t k = 0;

	if (!n) return;
	out[0] = 0;
	if ((items & (1u << HUD_FPS)) && known(v->fps)) {
		snprintf(part, sizeof part, "%.1f fps", v->fps > 999.9 ? 999.9 : v->fps);
		add(out, n, &k, part);
	}
	if ((items & (1u << HUD_SPEED)) && known(v->speed)) {
		/* 99.9x at most: Max runs a Game Boy game past 10x (2026-11-23),
		 * and "12.6x" is no wider than "0.97x", the widest it was */
		const double s = v->speed > 99.9 ? 99.9 : v->speed;
		const bool full = s >= HUD_FULL_LO && s <= HUD_FULL_HI;

		if (!full) {
			/* Two decimals near 1x, where one would say 1x or 0.9x
			 * of a game that is not; one from 1.1x up, where fast
			 * forward wavers in the second; and no zeros at the end:
			 * 0.5x, 2x */
			char x[16];
			size_t m;

			snprintf(x, sizeof x, s < 1.1 ? "%.2f" : "%.1f", s);
			m = strlen(x);
			while (m > 1 && x[m - 1] == '0') x[--m] = 0;
			if (m > 1 && x[m - 1] == '.') x[--m] = 0;
			snprintf(part, sizeof part, "%sx", x);
			add(out, n, &k, part);
		} else if (items == (1u << HUD_SPEED)) {
			/* Speed alone says it at full speed too, or nothing */
			add(out, n, &k, "1x");
		}
	}
	if ((items & (1u << HUD_TEMP)) && v->temp_c >= 0) {
		snprintf(part, sizeof part, "%d\xc2\xb0" "C", v->temp_c > 999 ? 999 : v->temp_c);
		add(out, n, &k, part);
	}
	if ((items & (1u << HUD_CPU)) && v->cpu_pct >= 0) {
		snprintf(part, sizeof part, "CPU %d%%", v->cpu_pct > 100 ? 100 : v->cpu_pct);
		add(out, n, &k, part);
	}
	if ((items & (1u << HUD_CLOCK)) && v->cpu_mhz > 0) {
		if (v->cpu_mhz >= 1000) snprintf(part, sizeof part, "%.1f GHz", v->cpu_mhz / 1000.0);
		else snprintf(part, sizeof part, "%d MHz", v->cpu_mhz);
		add(out, n, &k, part);
	}
	if ((items & (1u << HUD_BATTERY)) && v->battery_pct >= 0) {
		const int pct = v->battery_pct > 100 ? 100 : v->battery_pct;
		const uint32_t cp = CP_BATTERY + (uint32_t)((pct + 5) / 10);

		snprintf(part, sizeof part, "%c%c%c%d%%", (char)(0xe0 | (cp >> 12)),
		         (char)(0x80 | ((cp >> 6) & 0x3f)), (char)(0x80 | (cp & 0x3f)), pct);
		add(out, n, &k, part);
	}
	if ((items & (1u << HUD_TIME)) && v->hour >= 0 && v->min >= 0) {
		if (v->clock24) {
			snprintf(part, sizeof part, "%d:%02d", v->hour % 24, v->min % 60);
		} else {
			const int h = v->hour % 12 ? v->hour % 12 : 12;
			snprintf(part, sizeof part, "%d:%02d %s", h, v->min % 60, v->hour % 24 < 12 ? "AM" : "PM");
		}
		add(out, n, &k, part);
	}
}

static int text_width64(const hudglyph_size *s, const char *text)
{
	int cell = digit_cell(s), w = 0;
	const char *p = text;

	while (*p) w += advance(s, next_cp(&p), cell);
	return w;
}

static void pads(const hud_style *st, float scale, float *px, float *py)
{
	if (st->background == HUD_PILL) {
		*px = PAD_X[st->size] * scale;
		*py = PAD_Y[st->size] * scale;
	} else {
		*px = *py = BARE_PAD * scale;
	}
}

void hud_size(const char *text, float scale, const hud_style *st, int *w, int *h)
{
	const hudglyph_size *s;
	float px, py;

	*w = *h = 0;
	if (!text || !*text) return;
	st = style_or_default(st);
	s = glyphs_for(scale, st->size);
	pads(st, scale, &px, &py);
	*w = (int)ceilf(text_width64(s, text) / 64.0f + 2 * px);
	*h = (int)ceilf((float)s->px + 2 * py);
}

/* Whether a box meets another, both in units */
static bool meets(float x, float y, float w, float h, float l, float t, float r, float b)
{
	return x < r && x + w > l && y < b && y + h > t;
}

void hud_place(const hud_style *st, float scale, int w, int h, int sw, int sh, int *x, int *y)
{
	const float SW = sw / scale, SH = sh / scale;
	/* The widest each item says */
	const hud_values widest = { 999.9, 0.97, 100, 100, 999, 100, 12, 0, false };
	char text[128];
	float W, H, ux, uy, edge;
	bool right, top;
	int ww, wh;

	st = style_or_default(st);
	edge = (float)hud_edge_units[st->edge < HUD_EDGES ? st->edge : 1];
	right = st->corner == HUD_TOP_RIGHT || st->corner == HUD_BOTTOM_RIGHT;
	top = st->corner == HUD_TOP_LEFT || st->corner == HUD_TOP_RIGHT;

	/* Decided by the widest the items can say, so a line that grows from
	 * "59.7 fps" to a slow game's keeps its place */
	hud_text(text, sizeof text, &widest, st->items);
	hud_size(text, scale, st, &ww, &wh);
	if (ww < w) ww = w;
	if (wh < h) wh = h;
	W = ww / scale;
	H = wh / scale;

	ux = right ? SW - edge - W : edge;
	uy = top ? edge : SH - edge - H;
	if (top) {
		/* The level bar is where Diatom draws it: on the 1024 units */
		const float bl = BAR_L - GAP, br = BAR_R + GAP;
		const float dl = SW - DISC_IN - DISC_R - GAP, db = DISC_IN + DISC_R + GAP;

		/* Left of the disc, in the top right */
		if (right && meets(ux, uy, W, H, dl, 0, SW, db)) ux = dl - W;
		/* Under the level bar, when it would meet the bar's span; there
		 * the disc is above it, and the corner its own again */
		if (meets(ux, uy, W, H, bl, 0, br, BAR_B + GAP)) {
			uy = (float)(BAR_B + GAP);
			ux = right ? SW - edge - W : edge;
			if (right && meets(ux, uy, W, H, dl, 0, SW, db)) ux = dl - W;
		}
	}
	/* The line's own box at its corner: a right one ends where the widest
	 * would, so it grows left; a bottom one sits on the widest's bottom */
	if (right) ux += W - w / scale;
	if (!top) uy += H - h / scale;
	*x = (int)lroundf(ux * scale);
	*y = (int)lroundf(uy * scale);
}

/* Signed distance to a rounded box whose ends are half circles */
static float sd_pill(float x, float y, float w, float h)
{
	float r = h / 2, cx = fminf(fmaxf(x, r), w - r);

	return hypotf(x - cx, y - r) - r;
}

/* Signed distance to a box (x0, y0)-(x1, y1) with corners of radius r */
static float sd_rbox(float x, float y, float x0, float y0, float x1, float y1, float r)
{
	const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
	const float qx = fabsf(x - cx) - ((x1 - x0) / 2 - r), qy = fabsf(y - cy) - ((y1 - y0) / 2 - r);

	return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - r;
}

static float cover(float d)
{
	return fminf(fmaxf(0.5f - d, 0), 1);
}

static uint32_t argb(ds_rgb c, float a, bool premul)
{
	float k = premul ? a : 1.0f;
	unsigned A = (unsigned)lroundf(a * 255);

	return A << 24 | (unsigned)lroundf(c.r * k) << 16 | (unsigned)lroundf(c.g * k) << 8 |
	       (unsigned)lroundf(c.b * k);
}

/* `c` at `a` over the straight-alpha pixel at *d, which this file painted */
static void over(float *d, ds_rgb c, float a)
{
	float da = d[3], oa = a + da * (1 - a);

	if (oa <= 0) return;
	d[0] = (c.r * a + d[0] * da * (1 - a)) / oa;
	d[1] = (c.g * a + d[1] * da * (1 - a)) / oa;
	d[2] = (c.b * a + d[2] * da * (1 - a)) / oa;
	d[3] = oa;
}

static bool is_light(ds_rgb c)
{
	return c.r * 299 + c.g * 587 + c.b * 114 > 128 * 1000;
}

/* The battery into `cov` at pen x `x0` (pixels), its middle at `mid`: the
 * body's outline, its cap, and inside it a fill to its level's tenth */
static void draw_battery(float *cov, int w, int h, const hudglyph_size *s, float scale, float x0,
                         float mid, uint32_t cp)
{
	float bw, bh;
	const float line = fmaxf(1.0f, 2.0f * scale);
	const float level = (float)(cp - CP_BATTERY) / 10.0f;
	float body_r, cap_w, X0, Y0, X1, Y1;
	int x, y;

	battery_box(s, &bw, &bh);
	cap_w = fmaxf(line, bw * 0.08f);
	X0 = x0;
	Y0 = mid - bh / 2;
	X1 = x0 + bw - cap_w;
	Y1 = mid + bh / 2;
	body_r = bh * 0.22f;
	for (y = (int)floorf(Y0) - 1; y <= (int)ceilf(Y1) + 1; y++)
		for (x = (int)floorf(X0) - 1; x <= (int)ceilf(X1 + cap_w) + 1; x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			const float d = sd_rbox(px, py, X0, Y0, X1, Y1, body_r);
			float c = cover(d) - cover(d + line);
			/* the cap, on the right, a third of the body's height */
			c = fmaxf(c, cover(sd_rbox(px, py, X1 - line / 2, mid - bh / 6, X1 + cap_w, mid + bh / 6,
			                           line / 2)));
			/* the fill, a gap inside the outline */
			if (level > 0) {
				const float in = line * 1.75f;
				const float fx1 = X0 + in + (X1 - X0 - 2 * in) * level;

				c = fmaxf(c, cover(sd_rbox(px, py, X0 + in, Y0 + in, fx1, Y1 - in, line / 2)));
			}
			if (c > 0 && x >= 0 && x < w && y >= 0 && y < h)
				cov[y * w + x] = fmaxf(cov[y * w + x], c);
		}
}

void hud_render(uint32_t *px, int w, int h, int stride, float scale, const char *text,
                const hud_style *st, const ds_theme *t, bool premul)
{
	static float buf[1024 * 128 * 4];
	static float cov[1024 * 128], soft[1024 * 128];
	const ds_theme *th = t ? t : &DS_THEME[DS_LIGHT];
	const hud_style *sty = style_or_default(st);
	const hudglyph_size *s = glyphs_for(scale, sty->size);
	const float rim = RIM * scale;
	const float alpha = hud_opacity_pct[sty->opacity < HUD_OPACITIES ? sty->opacity : 0] / 100.0f;
	const ds_theme *pill = th;
	ds_rgb ink, halo;
	float padx, pady, mid;
	int cell = digit_cell(s), pen, x, y, top;
	const char *p = text;

	if (w <= 0 || h <= 0 || w > 1024 || h > 128) return;
	memset(buf, 0, sizeof(float) * (size_t)w * (size_t)h * 4);
	memset(cov, 0, sizeof(float) * (size_t)w * (size_t)h);
	memset(soft, 0, sizeof(float) * (size_t)w * (size_t)h);
	pads(sty, scale, &padx, &pady);

	/* The text's color; a light one takes Salt & Pepper's pill when the
	 * theme's is light, so it always reads */
	switch (sty->color) {
	case HUD_WHITE:  ink = (ds_rgb){ 255, 255, 255 }; break;
	case HUD_GREEN:  ink = DS_ACCENT[1]; break;
	case HUD_YELLOW: ink = DS_ACCENT[8]; break;
	default:         ink = th->text; break;
	}
	if (is_light(ink) && is_light(th->surface)) pill = &DS_THEME[DS_DARK];
	/* Without a pill, the shadow is what the pill would have been: dark
	 * around light text, light around dark */
	halo = is_light(ink) ? DS_THEME[DS_DARK].ground : DS_THEME[DS_LIGHT].surface;

	/* The text's coverage: numbers in `cov`, units and dots in `soft` */
	pen = (int)lroundf(padx * 64);
	{
		/* The line placed so the digits' middle is the line's */
		const hudglyph *zero = glyph(s, '0');
		top = zero ? (int)lroundf(h / 2.0f - (zero->y + zero->h / 2.0f)) : (int)lroundf(pady);
		mid = zero ? top + zero->y + zero->h / 2.0f : h / 2.0f;
	}
	while (*p) {
		uint32_t cp = next_cp(&p);
		const hudglyph *g = glyph(s, cp);
		int adv = advance(s, cp, cell), gx, gy;
		float *into = is_number(cp) ? cov : soft;

		if (is_battery(cp)) {
			draw_battery(cov, w, h, s, scale, pen / 64.0f, mid, cp);
		} else if (g && g->w && g->h) {
			/* A digit centered in its cell */
			int x0 = pen / 64 + g->x + (cp >= '0' && cp <= '9' ? (cell - g->adv64) / 128 : 0);
			int y0 = top + g->y;

			for (gy = 0; gy < g->h; gy++)
				for (gx = 0; gx < g->w; gx++) {
					int X = x0 + gx, Y = y0 + gy;
					float a = s->alpha[g->off + gy * g->w + gx] / 255.0f;

					if (a > 0 && X >= 0 && X < w && Y >= 0 && Y < h)
						into[Y * w + X] = fmaxf(into[Y * w + X], a);
				}
		}
		pen += adv;
	}

	if (sty->background == HUD_PILL) {
		/* The pill: the edge's color, then the surface inside the rim,
		 * each edge antialiased from its distance to the pixel's center */
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				float d = sd_pill(x + 0.5f, y + 0.5f, (float)w, (float)h);
				float outer = cover(d);
				float inner = cover(d + rim);
				float *o = &buf[((size_t)y * (size_t)w + (size_t)x) * 4];

				if (outer > 0) over(o, pill->edge, outer);
				if (inner > 0) over(o, pill->surface, inner);
			}
	} else if (sty->background == HUD_SHADOW) {
		/* The shadow: the text grown by SHADOW_R, its rim soft, one unit
		 * down */
		const float r = SHADOW_R * scale;
		const int R = (int)ceilf(r + 0.5f), down = (int)lroundf(scale);

		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				float m = 0;
				int dx, dy;

				for (dy = -R; dy <= R; dy++)
					for (dx = -R; dx <= R; dx++) {
						int X = x + dx, Y = y + dy - down;
						float k, c;

						if (X < 0 || X >= w || Y < 0 || Y >= h) continue;
						k = fminf(fmaxf(r + 0.5f - hypotf((float)dx, (float)dy), 0), 1);
						c = fmaxf(cov[Y * w + X], soft[Y * w + X]) * k;
						if (c > m) m = c;
					}
				if (m > 0) over(&buf[((size_t)y * (size_t)w + (size_t)x) * 4], halo, m * SHADOW_A);
			}
	}

	/* The text over it: the numbers in the color, the rest in the theme's
	 * text-soft, or the color a little lighter */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			float *o = &buf[((size_t)y * (size_t)w + (size_t)x) * 4];

			if (soft[y * w + x] > 0) {
				if (sty->color == HUD_THEME) over(o, th->text_soft, soft[y * w + x]);
				else over(o, ink, soft[y * w + x] * SOFT_A);
			}
			if (cov[y * w + x] > 0) over(o, ink, cov[y * w + x]);
		}

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const float *o = &buf[((size_t)y * (size_t)w + (size_t)x) * 4];
			ds_rgb c = { (uint8_t)lroundf(o[0]), (uint8_t)lroundf(o[1]), (uint8_t)lroundf(o[2]) };

			px[y * stride + x] = argb(c, o[3] * alpha, premul);
		}
}
