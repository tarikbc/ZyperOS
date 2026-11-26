/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * slowline.h: what the boot screen says when a start takes longer than it
 * should (ZyperOS's DESIGN.md 11.20, "Slower than usual"), for splash.c (the
 * Pixel 2 and the R36S) and splash-fb.c (the Brick and the Smart Pro). Header
 * only and with no DRM in it, so ZyperOS's tools/bootslow-check.c reads and
 * draws with this same code on a computer.
 *
 * A start reaches ZyperOS's home in 1.4 to 2 s on a Pixel 2 and about 2.2 s
 * on a Brick. One that does not says so, in two steps:
 *
 *   SLOW_STAGE_S (5 s)  the step under way, at the foot where the first
 *                       start's line is: "Checking the card · C3", the
 *                       words in text-soft and the code in text-dim
 *   SLOW_HELP_S (20 s)  "Taking longer than usual" over Zyper, in label
 *                       text-soft, and under it, in meta text-dim, what to
 *                       do if it never ends: "Stuck? Hold POWER for 10 s,
 *                       then turn it on."
 *
 * The step is the one the start's scripts name in STAGE_FILE (datapart and
 * zyperos.init, as they go), one word: C1 the card's games partition
 * mounting, C2 it made and formatted, C3 its check, U1 System Update's switch,
 * U2 the first start's copy, L1 ZyperOS starting. The copy (U2) has its own
 * line and bar (copy_start in splash.c) and none of these; a step this does
 * not know says nothing.
 *
 * THE WORDS. Pictures, since the splash has no fonts: ZyperOS's
 * tools/genbootslow.py draws every line in every language, as coverage, one
 * file a language and a handheld's scale ("TSPN", described there), and this
 * lays each on the animation's ground in the theme's ink: each pixel a
 * letter covers is the ground mixed with the ink by its coverage, written
 * whole, so drawing it again on the same frame changes nothing, and a line
 * that goes is taken away by writing the ground on those pixels. The letters
 * are where no frame of the animation reaches, which ZyperOS's
 * tools/bootslow-check.c holds them to on every frame of every handheld.
 *
 * WHERE, in the launcher's units, scaled to the panel, centered: the step's
 * baseline at SLOW_LINE_Y, the first start's line's (11.20); the first line's
 * at SLOW_TOP_Y and the help's at SLOW_HELP_Y, over Zyper. The animation is
 * from 157 to 658 on every handheld (its frames, measured), so under the
 * wordmark there is room for one line, not two: a Chinese or Japanese letter
 * at meta reaches 35 units over its baseline and 11 under (the files,
 * measured), so a second line there would touch the wordmark or pass the
 * screen's inset. Over Zyper, 24 to 156, there is room for the two. On a panel mounted turned (the Pixel 2's, 480 x
 * 640) a landscape pixel (x, y) is the buffer's column y of row W - 1 - x,
 * as portout.h has it.
 *
 * THE BEAT. Every SLOW_BEAT_S while it runs, one line in the kernel's log
 * (slow_beat): the time, the frames it has put on the panel and the step.
 * After a watchdog's restart the console the kernel kept ends with it, which
 * tells a start whose splash was still drawing (the chip alive, something
 * else stuck) from one whose whole chip stopped.
 */
#ifndef SPLASH_SLOWLINE_H
#define SPLASH_SLOWLINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define SLOW_STAGE_S  5.0       /* the step from here */
#define SLOW_HELP_S   20.0      /* the first line and the help from here */
#define SLOW_STALL_S  60.0      /* the first start's bar still this long: the help in its line's place */
#define SLOW_BEAT_S   2.0       /* a line in the kernel's log this often */
#define SLOW_TESTER_S 2.0       /* SELECT held this long shows the kernel's lines (the tester's view) */

#define SLOW_QUICK_S  0.12      /* a line arriving: DESIGN.md 8's quick, on its out curve */
#define SLOW_DROP_Y   14        /* the first line and the help come down this far as they arrive, units */
#define SLOW_TOP_Y    80        /* the first line's baseline, units: 51 over it is 29 */
#define SLOW_HELP_Y   136       /* the help's: 11 under it is 147, Zyper from 157 */
#define SLOW_LINE_Y   706       /* the step's: the first start's line's */

enum { SLOW_TOP = 0, SLOW_STUCK = 6, SLOW_LINES = 7 };

typedef struct {
	unsigned  w;                /* pixels */
	int       dim;              /* 1: in text-dim, 0: in the line's own ink */
	uint8_t  *cov;              /* w x the line's h */
} slow_seg;

typedef struct {
	int       have;
	unsigned  h, ascent, w, nseg;
	slow_seg  seg[2];
} slow_line;

typedef struct {
	unsigned  scale_milli;      /* panel pixels a unit, in thousandths */
	slow_line line[SLOW_LINES];
} slow_set;

static inline unsigned slow_u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline uint32_t slow_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static inline void slow_free(slow_set *s)
{
	for (int i = 0; i < SLOW_LINES; i++)
		for (int k = 0; k < 2; k++) {
			free(s->line[i].seg[k].cov);
			s->line[i].seg[k].cov = NULL;
		}
	memset(s, 0, sizeof *s);
}

/* A file's lines, unpacked: 0, or -1 for a file that does not hold together
 * (nothing kept then) */
static inline int slow_load(const uint8_t *f, size_t size, slow_set *s)
{
	size_t at = 12;
	unsigned n;

	memset(s, 0, sizeof *s);
	if (size < 12 || memcmp(f, "TSPN", 4) || slow_u16(f + 4) != 1) return -1;
	s->scale_milli = slow_u16(f + 6);
	n = slow_u16(f + 8);
	if (!s->scale_milli || s->scale_milli > 4000) return -1;
	for (unsigned i = 0; i < n; i++) {
		unsigned id, nseg, h, w = 0;
		slow_line *l;

		if (at + 8 > size) goto bad;
		id = f[at];
		nseg = f[at + 1];
		h = slow_u16(f + at + 2);
		if (id >= SLOW_LINES || nseg < 1 || nseg > 2 || !h || h > 512) goto bad;
		l = &s->line[id];
		l->h = h;
		l->ascent = slow_u16(f + at + 4);
		l->nseg = nseg;
		at += 8;
		for (unsigned k = 0; k < nseg; k++) {
			unsigned sw;
			uint32_t len;
			uLongf got;

			if (at + 8 > size) goto bad;
			l->seg[k].dim = f[at] == 1;
			sw = slow_u16(f + at + 2);
			len = slow_u32(f + at + 4);
			at += 8;
			if (!sw || sw > 4096 || at + len > size) goto bad;
			got = (uLongf)sw * h;
			if (!(l->seg[k].cov = malloc(got))) goto bad;
			if (uncompress(l->seg[k].cov, &got, f + at, len) != Z_OK || got != (uLongf)sw * h) goto bad;
			l->seg[k].w = sw;
			w += sw;
			at += len;
		}
		l->w = w;
		l->have = 1;
	}
	return 0;
bad:
	slow_free(s);
	return -1;
}

/* A step's line, from its code: 1 to 5, or -1 for none (U2, the copy, has its
 * own; anything else is not known) */
static inline int slow_stage_line(const char *code)
{
	static const char *const C[] = { "C1", "C2", "C3", "U1", "L1" };

	for (int i = 0; i < 5; i++)
		if (code && !strcmp(code, C[i])) return i + 1;
	return -1;
}

/* What shows `t` seconds into the start with step `line` (slow_stage_line)
 * under way: *top the first line, *step the step's line (or -1), *help the
 * help under it */
static inline void slow_view(double t, int line, int *top, int *step, int *help)
{
	*top = *help = 0;
	*step = -1;
	if (line < 1 || t < SLOW_STAGE_S) return;
	*step = line;
	if (t >= SLOW_HELP_S) *top = *help = 1;
}

/* The pixel the ink makes over the ground at coverage a */
static inline uint32_t slow_mix(uint32_t ground, uint32_t ink, unsigned a)
{
	uint32_t r = ((ground >> 16 & 0xff) * (255 - a) + (ink >> 16 & 0xff) * a + 127) / 255;
	uint32_t g = ((ground >> 8 & 0xff) * (255 - a) + (ink >> 8 & 0xff) * a + 127) / 255;
	uint32_t b = ((ground & 0xff) * (255 - a) + (ink & 0xff) * a + 127) / 255;

	return 0xff000000u | r << 16 | g << 8 | b;
}

/* Into a landscape pixel (x, y) of a W x H screen, in a buffer of `stride`
 * pixels a row, turned (the Pixel 2's panel) or not */
static inline void slow_set_px(uint32_t *buf, unsigned stride, unsigned W, unsigned H, int turned,
                               int x, int y, uint32_t v)
{
	if (x < 0 || y < 0 || (unsigned)x >= W || (unsigned)y >= H) return;
	if (turned) buf[(size_t)(W - 1 - x) * stride + y] = v;
	else buf[(size_t)y * stride + x] = v;
}

/* Line `l` centered on landscape x `cx` with its baseline on `by`: each
 * pixel it inks written as the ground mixed with its segment's ink, by
 * coverage, the rest left as it is (the box's corners may reach the
 * wordmark's foot; the letters never do). `erase` writes the ground there
 * instead, which takes the line away again. `alpha` (0 to 256) is how much
 * of it is in, as it arrives. Its box in landscape pixels into *x0, *y0 (its
 * top left). */
static inline void slow_put(const slow_line *l, uint32_t *buf, unsigned stride, unsigned W, unsigned H,
                            int turned, int cx, int by, uint32_t ground, uint32_t ink, uint32_t dim,
                            int erase, unsigned alpha, int *x0, int *y0)
{
	int x = cx - (int)l->w / 2, y = by - (int)l->ascent;

	if (x0) *x0 = x;
	if (y0) *y0 = y;
	for (unsigned k = 0; k < l->nseg; k++) {
		const slow_seg *s = &l->seg[k];
		uint32_t c = s->dim ? dim : ink;

		for (unsigned r = 0; r < l->h; r++)
			for (unsigned i = 0; i < s->w; i++) {
				unsigned a = s->cov[(size_t)r * s->w + i];

				if (a) slow_set_px(buf, stride, W, H, turned, x + (int)i, y + (int)r,
				                   erase ? ground : slow_mix(ground, c, a * alpha >> 8));
			}
		x += (int)s->w;
	}
}

/* A rect of the landscape screen as one color */
static inline void slow_clear(uint32_t *buf, unsigned stride, unsigned W, unsigned H, int turned,
                              int x, int y, unsigned w, unsigned h, uint32_t ground)
{
	for (unsigned r = 0; r < h; r++)
		for (unsigned i = 0; i < w; i++)
			slow_set_px(buf, stride, W, H, turned, x + (int)i, y + (int)r, ground);
}

/* A unit's position in panel pixels */
static inline int slow_px(const slow_set *s, int units)
{
	return (int)(((long)units * (long)s->scale_milli + 500) / 1000);
}

/* The beat, a line for the kernel's log, with its line end */
static inline int slow_beat(char *out, size_t n, double t, unsigned frames, const char *stage)
{
	return snprintf(out, n, "splash: %.1f s, %u frames, stage=%s\n", t, frames,
	                stage && *stage ? stage : "-");
}

/* The step's code read from what the scripts wrote: its first word, at most
 * three characters of letters and digits; "" for none */
static inline void slow_stage_word(const char *text, char out[4])
{
	size_t i = 0;

	while (*text == ' ' || *text == '\t') text++;
	while (i < 3 && ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= '0' && text[i] <= '9'))) {
		out[i] = text[i];
		i++;
	}
	if (text[i] && text[i] != '\n' && text[i] != ' ' && text[i] != '\r') i = 0;
	out[i] = 0;
}

/*
 * THE TESTER'S VIEW. SELECT held SLOW_TESTER_S on the boot screen shows the
 * kernel's latest lines at the foot, as they come, and under them the step,
 * the time and the frames, in the splash's ASCII letters (glyphs.bin, ZyperOS's
 * tools/genclosed.py) on a dark band; let go, and the boot screen is as it
 * was. A tester with a handheld that is slow to start reads where it is with
 * no computer. Kernel lines are English and ASCII: no pictures a language.
 * The band is from SLOW_TESTER_TOP_Y to the foot on every handheld: all of
 * Zyper shows over it and none of the wordmark, never a cut letter of it.
 * As many rows as fit: the foot's last, the kernel's newest over it.
 */
#define SLOW_TESTER_TOP_Y 481   /* the band's top, units: Zyper ends at 458, the wordmark starts at 505 */
#define SLOW_TESTER_MAXROWS 12  /* kernel rows shown at most, wrapped rows counted */

typedef struct {
	const uint8_t *at[128];
	unsigned h, ascent;
} slow_glyphs;

/* glyphs.bin ("TSPG", height, baseline, first, count, then each letter's
 * advance and coverage), mapped or read whole: 0, or -1 */
static inline int slow_glyphs_load(const uint8_t *f, size_t size, slow_glyphs *g)
{
	size_t at = 12;
	unsigned first, count;

	memset(g, 0, sizeof *g);
	if (size < 12 || memcmp(f, "TSPG", 4)) return -1;
	g->h = slow_u16(f + 4);
	g->ascent = slow_u16(f + 6);
	first = slow_u16(f + 8);
	count = slow_u16(f + 10);
	for (unsigned c = first; c < first + count && c < 128; c++) {
		if (at >= size || at + 1 + (size_t)f[at] * g->h > size) return -1;
		g->at[c] = f + at;
		at += 1 + (size_t)f[at] * g->h;
	}
	return g->h ? 0 : -1;
}

static inline unsigned slow_text_w(const slow_glyphs *g, const char *t, size_t n)
{
	unsigned w = 0;

	for (size_t i = 0; i < n && t[i]; i++)
		if ((unsigned char)t[i] < 128 && g->at[(unsigned char)t[i]]) w += g->at[(unsigned char)t[i]][0];
	return w;
}

/* n bytes of `t` from landscape (x, top y), over whatever is there (the band
 * was just filled, so the same text again looks the same) */
static inline void slow_text(const slow_glyphs *g, const char *t, size_t n, uint32_t *buf, unsigned stride,
                             unsigned W, unsigned H, int turned, int x, int y, uint32_t under, uint32_t ink)
{
	for (size_t i = 0; i < n && t[i]; i++) {
		const uint8_t *c = (unsigned char)t[i] < 128 ? g->at[(unsigned char)t[i]] : NULL;

		if (!c) continue;
		for (unsigned r = 0; r < g->h; r++)
			for (unsigned k = 0; k < c[0]; k++) {
				unsigned a = c[1 + r * c[0] + k];

				if (a) slow_set_px(buf, stride, W, H, turned, x + (int)k, y + (int)r, slow_mix(under, ink, a));
			}
		x += c[0];
	}
}

/* A /dev/kmsg record ("6,1234,620000,-;mmc0: new card\n") as the kernel's
 * log prints it ("[    0.620000] mmc0: new card"), its non-ASCII bytes as
 * "?": 0, or -1 for a record that is not one */
static inline int slow_kmsg_line(const char *rec, char *out, size_t n)
{
	unsigned long long us;
	const char *semi = strchr(rec, ';'), *p = rec;
	size_t k;
	int w;

	if (!semi) return -1;
	for (int field = 0; field < 2; field++) {
		p = strchr(p, ',');
		if (!p || p > semi) return -1;
		p++;
	}
	us = strtoull(p, NULL, 10);
	w = snprintf(out, n, "[%5llu.%06llu] ", us / 1000000, us % 1000000);
	if (w < 0 || (size_t)w >= n) return -1;
	for (k = (size_t)w, p = semi + 1; *p && *p != '\n' && k + 1 < n; p++)
		out[k++] = (unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7f ? *p : '?';
	out[k] = 0;
	return 0;
}

/* The view: a band from landscape `y0` to the foot, `foot` on its last row,
 * brighter, and over it the kernel's `lines` wrapped to its width, the newest
 * last, as many rows as fit */
static inline void slow_tester(const slow_glyphs *g, char lines[][160], int nlines, const char *foot,
                               uint32_t *buf, unsigned stride, unsigned W, unsigned H, int turned,
                               uint32_t band, uint32_t ink, uint32_t bright, int y0)
{
	int margin = (int)(W / 32), maxw = (int)W - 2 * margin, pitch = (int)g->h + 2;
	int fit = ((int)H - y0 - margin) / pitch - 1, foot_y = (int)H - margin / 2 - pitch;
	const char *seg[64];
	size_t len[64];
	int nseg = 0;

	if (fit > SLOW_TESTER_MAXROWS) fit = SLOW_TESTER_MAXROWS;
	if (fit < 0) fit = 0;
	/* Wrap: each line in rows as wide as the band, at a space when the row
	 * has one, its next row from the word after it; a word longer than a
	 * row is cut where the row ends */
	for (int i = 0; i < nlines && nseg < 64; i++) {
		const char *t = lines[i];
		size_t rest = strlen(t);

		while (rest && nseg < 64) {
			size_t take = 0, cut;

			while (take < rest && (int)slow_text_w(g, t, take + 1) <= maxw) take++;
			if (!take) take = 1;
			cut = take;
			if (take < rest) {
				size_t sp = take;

				while (sp > 0 && t[sp] != ' ') sp--;
				if (sp > 0) {
					cut = sp;
					while (sp < rest && t[sp] == ' ') sp++;
					take = sp;
				}
			}
			seg[nseg] = t;
			len[nseg++] = cut;
			t += take;
			rest -= take;
		}
	}
	slow_clear(buf, stride, W, H, turned, 0, y0, W, H - (unsigned)y0, band);
	int first = nseg > fit ? nseg - fit : 0;
	for (int i = first; i < nseg; i++)
		slow_text(g, seg[i], len[i], buf, stride, W, H, turned, margin, foot_y - (nseg - i) * pitch, band, ink);
	slow_text(g, foot, strlen(foot), buf, stride, W, H, turned, margin, foot_y, band, bright);
}

/*
 * THE RUN: one boot screen's state, frame by frame (slow_frame), for the
 * splash's loop and the check alike. The caller reads the step, SELECT and
 * the kernel's log; this decides what shows, draws it into the frame being
 * made and says which of the buffer's rows changed, so a splash that copies
 * only changed rows to the panel copies these too (each of its two buffers,
 * the frame after).
 */
typedef struct {
	slow_set    set;
	slow_glyphs g;
	int         have, have_g;
	unsigned    W, H, stride;   /* the landscape screen, the buffer's row in pixels */
	int         turned;
	uint32_t    ground, soft, dim, band, bright;
	/* What is drawn: each line's id and baseline (landscape), -1 for none */
	int         id[3], cx[3], by[3];
	unsigned    alpha[3];       /* how much of each was in when drawn */
	/* What is wanted, drawn or not (the tester's view hides the step), and
	 * when each slot's line arrived: a line that comes where there was none
	 * fades in over SLOW_QUICK_S, the first line and the help coming down
	 * SLOW_DROP_Y as they do (the boot notice lab's picks) */
	int         want[3];
	double      at[3];
	int         keep;           /* the buffer keeps what is drawn: unchanged lines are not drawn again */
	int         tester;         /* the tester's view up */
	int         tester_y;
	uint32_t   *under;          /* the frame's pixels under the tester's band */
	char        tester_sig[64]; /* what it showed last */
} slow_run;

static inline void slow_run_init(slow_run *r, unsigned W, unsigned H, unsigned stride, int turned,
                                 uint32_t ground, uint32_t soft, uint32_t dim)
{
	memset(r, 0, sizeof *r);
	r->W = W;
	r->H = H;
	r->stride = stride;
	r->turned = turned;
	r->ground = ground;
	r->soft = soft;
	r->dim = dim;
	/* The tester's band: the ground darkened, its letters in the step's
	 * inks of Salt & Pepper whatever the theme (a tester's screen) */
	r->band = 0xff0b0c0a;
	r->bright = 0xffe6e4da;
	for (int i = 0; i < 3; i++) r->id[i] = r->want[i] = -1;
}

/* The buffer's rows a landscape box covers, merged into [*lo, *hi) */
static inline void slow_rows(const slow_run *r, int x, int y, int w, int h, int *lo, int *hi)
{
	int a, b;

	if (r->turned) { a = (int)r->W - (x + w); b = (int)r->W - x; }
	else { a = y; b = y + h; }
	if (a < 0) a = 0;
	if (b > (int)(r->turned ? r->W : r->H)) b = (int)(r->turned ? r->W : r->H);
	if (a >= b) return;
	if (*hi <= *lo) { *lo = a; *hi = b; return; }
	if (a < *lo) *lo = a;
	if (b > *hi) *hi = b;
}

/* The slots: 0 the first line, 1 the step, 2 the help */
static inline void slow_want(const slow_run *r, double t, const char *stage, int id[3], int by[3])
{
	int top, step, help;

	slow_view(t, slow_stage_line(stage), &top, &step, &help);
	id[0] = top && r->set.line[SLOW_TOP].have ? SLOW_TOP : -1;
	id[1] = step > 0 && r->set.line[step].have ? step : -1;
	id[2] = help && r->set.line[SLOW_STUCK].have ? SLOW_STUCK : -1;
	by[0] = slow_px(&r->set, SLOW_TOP_Y);
	by[1] = slow_px(&r->set, SLOW_LINE_Y);
	by[2] = slow_px(&r->set, SLOW_HELP_Y);
}

/* How far a line that arrived at `at` is in at `t`: 0 to 1 on the out
 * curve (1 - (1 - p)^5) */
static inline double slow_in(double at, double t)
{
	double p = (t - at) / SLOW_QUICK_S, q;

	if (p >= 1.0) return 1.0;
	if (p <= 0.0) return 0.0;
	q = 1.0 - p;
	return 1.0 - q * q * q * q * q;
}

/* One frame at `t`: the lines for `stage`, or the tester's view while
 * `tester` (SELECT held long enough) with the kernel's `lines` and `foot`.
 * Drawn into `buf` every frame (the animation's bands may have written over
 * them); 1 when what shows changed, with the buffer's rows to copy in
 * [*lo, *hi) (left as they were when nothing changed) */
static inline int slow_frame(slow_run *r, uint32_t *buf, double t, const char *stage, int tester,
                             char lines[][160], int nlines, const char *foot, int *lo, int *hi)
{
	int id[3], by[3], changed = 0, cx = (int)r->W / 2;
	unsigned alpha[3];

	if (!r->have) return 0;
	slow_want(r, t, stage, id, by);
	if (tester && !r->have_g) tester = 0;
	/* Arriving: a slot's line where there was none starts now; one that
	 * changes in place (the next step) is simply the next */
	for (int i = 0; i < 3; i++) {
		double in;

		if (id[i] >= 0 && r->want[i] < 0) r->at[i] = t;
		r->want[i] = id[i];
		in = id[i] >= 0 ? slow_in(r->at[i], t) : 1.0;
		alpha[i] = (unsigned)(in * 256.0 + 0.5);
		if (i != 1) by[i] -= slow_px(&r->set, SLOW_DROP_Y) - (int)(slow_px(&r->set, SLOW_DROP_Y) * in + 0.5);
	}
	/* The tester's view goes: the frame's own pixels back under its band */
	if (!tester && r->tester) {
		for (unsigned y = (unsigned)r->tester_y; y < r->H; y++)
			for (unsigned x = 0; x < r->W; x++)
				slow_set_px(buf, r->stride, r->W, r->H, r->turned, (int)x, (int)y,
				            r->under[(size_t)(y - (unsigned)r->tester_y) * r->W + x]);
		slow_rows(r, 0, r->tester_y, (int)r->W, (int)r->H - r->tester_y, lo, hi);
		r->tester = 0;
		r->tester_sig[0] = 0;
		changed = 1;
		for (int i = 0; i < 3; i++) r->id[i] = -1;   /* drawn again below */
	}
	/* The tester's view is all there is at the foot: the step is its last
	 * row. The first line and the help, over Zyper, stay */
	if (tester) id[1] = -1;
	/* Lines that go or move: taken away first, before the tester's band
	 * keeps what is under it */
	for (int i = 0; i < 3; i++) {
		if (r->id[i] < 0 || (r->id[i] == id[i] && r->by[i] == by[i])) continue;
		const slow_line *l = &r->set.line[r->id[i]];
		int x0, y0;

		slow_put(l, buf, r->stride, r->W, r->H, r->turned, r->cx[i], r->by[i], r->ground, 0, 0, 1, 256, &x0, &y0);
		slow_rows(r, x0, y0, (int)l->w, (int)l->h, lo, hi);
		r->id[i] = -1;
		changed = 1;
	}
	if (tester) {
		char sig[64];
		int top;

		snprintf(sig, sizeof sig, "%d:%s:%.40s", nlines, nlines ? lines[nlines - 1] + 1 : "", foot);
		if (!r->tester) {
			r->tester_y = slow_px(&r->set, SLOW_TESTER_TOP_Y);
			if (r->tester_y < 0) r->tester_y = 0;
			if (r->tester_y > (int)r->H) r->tester_y = (int)r->H;
			free(r->under);
			r->under = malloc((size_t)r->W * (r->H - (unsigned)r->tester_y) * 4);
			if (!r->under) return changed;
			for (unsigned y = (unsigned)r->tester_y; y < r->H; y++)
				for (unsigned x = 0; x < r->W; x++)
					r->under[(size_t)(y - (unsigned)r->tester_y) * r->W + x] = r->turned
						? buf[(size_t)(r->W - 1 - x) * r->stride + y] : buf[(size_t)y * r->stride + x];
			r->tester = 1;
		}
		top = r->tester_y;
		slow_tester(&r->g, lines, nlines, foot, buf, r->stride, r->W, r->H, r->turned,
		            r->band, 0xffc3c3ba, r->bright, top);
		if (strcmp(sig, r->tester_sig)) {
			snprintf(r->tester_sig, sizeof r->tester_sig, "%s", sig);
			slow_rows(r, 0, top, (int)r->W, (int)r->H - top, lo, hi);
			changed = 1;
		}
	}
	for (int i = 0; i < 3; i++) {
		if (id[i] < 0 || (r->keep && r->id[i] == id[i] && r->by[i] == by[i] && r->alpha[i] == alpha[i]))
			continue;
		const slow_line *l = &r->set.line[id[i]];
		int x0, y0;

		slow_put(l, buf, r->stride, r->W, r->H, r->turned, cx, by[i], r->ground,
		         i == 2 ? r->dim : r->soft, r->dim, 0, alpha[i], &x0, &y0);
		if (r->id[i] != id[i] || r->alpha[i] != alpha[i]) {
			slow_rows(r, x0, y0, (int)l->w, (int)l->h, lo, hi);
			changed = 1;
		}
		r->id[i] = id[i];
		r->cx[i] = cx;
		r->by[i] = by[i];
		r->alpha[i] = alpha[i];
	}
	return changed;
}

/* The first start's copy, its bar still SLOW_STALL_S: the help in its line's
 * place, in the line's text-soft, over the copy's own pictures (copy_draw
 * draws them every frame, so this follows it every frame). The rows from the
 * line's top to just over the bar (the first start's 706 less a meta line,
 * to 720; the bar starts at 723)
 * are the ground first, edge to edge: nothing else is in them. Their buffer
 * rows into [*lo, *hi). */
#define SLOW_COPY_TOP 669
#define SLOW_COPY_END 720
static inline void slow_copy_stuck(slow_run *r, uint32_t *buf, int *lo, int *hi)
{
	const slow_line *l = &r->set.line[SLOW_STUCK];
	int y0 = slow_px(&r->set, SLOW_COPY_TOP), y1 = slow_px(&r->set, SLOW_COPY_END);

	if (!r->have || !l->have) return;
	slow_clear(buf, r->stride, r->W, r->H, r->turned, 0, y0, r->W, (unsigned)(y1 - y0), r->ground);
	slow_put(l, buf, r->stride, r->W, r->H, r->turned, (int)r->W / 2, slow_px(&r->set, SLOW_LINE_Y),
	         r->ground, r->soft, r->dim, 0, 256, NULL, NULL);
	slow_rows(r, 0, y0, (int)r->W, y1 - y0, lo, hi);
}

#endif
