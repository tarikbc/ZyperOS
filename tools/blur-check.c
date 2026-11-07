/* Beard's shadows, without a screen.
 *
 *     make check-blur
 *
 * A lift on Beard is a shadow (DESIGN.md 7): a rect blurred by three box
 * passes into one texture per kind, made once at half the size it is drawn,
 * and drawn back up in nine pieces under a rect of any size (src/ui.c, with
 * the arithmetic in src/blur.c). Each of the four steps fails in a way
 * nobody sees until a card stands on it:
 *
 *   a pass of the box that loses light makes every shadow paler than its
 *   numbers, by an amount that depends on its size;
 *   a texture padded by less than the three passes reach cuts the blur off
 *   in a hard edge a few units out from the card, and one padded by more is
 *   a texture bigger than it needs to be, on a handheld's GPU;
 *   a corner piece cut short of where the edge runs straight stretches a
 *   texel that saw the corner along the whole of the card's side, which is a
 *   line through the shadow at every corner;
 *   a texture made at half size whose boxes spread light more or less than
 *   the boxes at full size, or that is drawn back up off by a texel, is a
 *   shadow softer, harder or further out than DESIGN.md's numbers;
 *   and a shape of triangles filled for a shadow in its own shape (Zyper's)
 *   that sums where they overlap, or misses an edge two of them share, is a
 *   darker blot where his cap is drawn over his coat, or a pale line along
 *   a seam of his triangulation.
 *
 * So the check blurs what it can add up, draws the nine pieces into a buffer
 * the way SDL_RenderCopy would, nearest texel, at the texture's own size,
 * and compares them with the shadow of the same big rect blurred whole; and
 * draws them again at the screen's size the way the GPU does with linear
 * filtering, and compares that with the big rect's shadow made at full size,
 * a texel a unit. The kinds are DESIGN.md's, each with its corner, from the
 * one table that pairs them (blur.h's BLUR_KIND, which src/ui.c makes its
 * shadows from), at blur.h's BLUR_UNIT, which src/ui.c makes them at.
 *
 * Links src/blur.c and NOT SDL.
 */
#include "../src/blur.h"
#include "../src/design.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* A quarter of one step of an 8-bit alpha: a difference under it cannot
 * change a pixel of the texture, and the running sums' float error is a
 * thousandth of it. */
#define TOL (0.25f / 255.0f)

static double total(const float *a, size_t n)
{
	double s = 0.0;
	size_t i;

	for (i = 0; i < n; i++) s += a[i];
	return s;
}

/* One pass: an impulse becomes 2 * rad + 1 values of 1 / (2 * rad + 1) and
 * nothing either side; a line whose light is more than `rad` from both ends
 * keeps all of it; and a pass down a column moves nothing in the column
 * beside it. */
static void one_pass(void)
{
	enum { N = 64, RAD = 5 };
	float p[N], line[N], g[N * 3];
	int i, spread = 1;
	double before;

	printf("a pass of the box moves light and loses none of it\n");
	memset(p, 0, sizeof p);
	p[30] = 1.0f;
	blur_pass(p, N, 1, RAD, line);
	for (i = 0; i < N; i++) {
		float want = i >= 30 - RAD && i <= 30 + RAD ? 1.0f / (2 * RAD + 1) : 0.0f;

		if (fabsf(p[i] - want) > 1e-6f) spread = 0;
	}
	ck(spread, "an impulse is spread evenly over 2 * rad + 1 values, and no further");

	for (i = 0; i < N; i++)
		p[i] = i > RAD && i < N - RAD - 1 ? (float)((i * 37) % 11) / 10.0f : 0.0f;
	before = total(p, N);
	blur_pass(p, N, 1, RAD, line);
	ck(fabs(total(p, N) - before) < 1e-4, "light more than rad from both ends is all kept");

	p[0] = 1.0f;
	before = total(p, N);
	blur_pass(p, N, 1, RAD, line);
	ck(total(p, N) < before - 0.25, "and light at an end is lost past it, as blur.h says");

	for (i = 0; i < N * 3; i++) g[i] = (float)(i % 3);   /* columns of 0, 1, 2 */
	g[30 * 3 + 1] = 5.0f;
	blur_pass(g + 1, N, 3, RAD, line);
	spread = 1;
	for (i = 0; i < N; i++)
		if (g[i * 3] != 0.0f || g[i * 3 + 2] != 2.0f) spread = 0;
	ck(spread, "a pass down a column touches only that column");
	ck(fabsf(g[30 * 3 + 1] - (1.0f * (2 * RAD) + 5.0f) / (2 * RAD + 1)) < 1e-5f,
	   "and is the mean down it");
}

/* Three passes of radius `rad` reach exactly 3 * rad: an impulse is lit
 * 3 * rad away, by the one path that gets there, (2 * rad + 1)^-3 of it, and
 * is dark one further; and three passes each way keep all of its light. In
 * one line, where the running sums start fresh at the near end and a value a
 * hundred-thousandth of the impulse is not lost in their rounding. */
static void three_reach(void)
{
	enum { N = 161, MID = 80 };
	float p[N], line[N], *a = calloc((size_t)N * N, sizeof *a);
	int rad, pass;
	char msg[128];

	printf("three passes reach 3 * rad and no further\n");
	if (!a) { ck(0, "memory"); return; }
	for (rad = 1; rad <= 24; rad++) {
		int m = 3 * rad;
		double tip = 1.0 / pow(2 * rad + 1, 3);

		memset(p, 0, sizeof p);
		p[MID] = 1.0f;
		for (pass = 0; pass < 3; pass++) blur_pass(p, N, 1, rad, line);
		snprintf(msg, sizeof msg, "rad %d: lit %d away, %.3g of it", rad, m, tip);
		ck(fabs(p[MID - m] - tip) < 1e-3 * tip && fabs(p[MID + m] - tip) < 1e-2 * tip,
		   msg);
		snprintf(msg, sizeof msg, "rad %d: dark %d away", rad, m + 1);
		ck(fabsf(p[MID - m - 1]) < 1e-3 * tip && fabsf(p[MID + m + 1]) < 1e-2 * tip,
		   msg);
		if (rad > 8) continue;
		memset(a, 0, (size_t)N * N * sizeof *a);
		a[MID * N + MID] = 1.0f;
		blur_box3(a, N, N, rad, line);
		snprintf(msg, sizeof msg, "rad %d: three passes each way keep the light", rad);
		ck(fabs(total(a, (size_t)N * N) - 1.0) < 1e-5, msg);
	}
	/* Three of their own radii, as a shadow made at half size has: they
	 * reach their sum, lit there by the one path that gets there, and keep
	 * the light. */
	for (rad = 2; rad <= 16; rad++) {
		const int three[3] = { rad, rad, rad - 1 };
		const int m = 3 * rad - 1;
		double tip = 1.0 / ((2.0 * rad + 1) * (2.0 * rad + 1) * (2.0 * rad - 1));
		float row = 0.0f, past = 0.0f;
		int y;

		memset(a, 0, (size_t)N * N * sizeof *a);
		a[MID * N + MID] = 1.0f;
		blur_box3_by(a, N, N, three, line);
		/* Along the middle column, m away: the row's tip times the
		 * column's, the whole of each kernel's other axis summed in. */
		for (y = 0; y < N; y++) {
			row += a[(size_t)y * N + MID - m];
			past += a[(size_t)y * N + MID - m - 1];
		}
		snprintf(msg, sizeof msg, "radii %d, %d, %d: lit %d away, dark %d away, light kept",
		         rad, rad, rad - 1, m, m + 1);
		ck(fabs(row - tip) < 1e-2 * tip && fabsf(past) < 1e-3 * tip &&
		   fabs(total(a, (size_t)N * N) - 1.0) < 1e-5, msg);
	}
	free(a);
}

/* The sizes: at a texel a unit, three boxes `blur` + 1 units wide, as
 * DESIGN.md's blur says, which is the shadow the rest is held to; at
 * BLUR_UNIT, three whole texels each side whose blur is as wide as that one's
 * within 2% (the square root of the variances added, the width of the
 * Gaussian three boxes come close to), and a corner piece that is still a
 * whole number of the full size's units. */
static double spread(const int rad[3], int unit)
{
	double v = 0.0;
	int i;

	for (i = 0; i < 3; i++) {
		double w = 2.0 * rad[i] + 1.0;

		v += (double)unit * unit * (w * w - 1.0) / 12.0;
	}
	return sqrt(v);
}

static void sizes(void)
{
	char msg[192];
	int i;

	printf("a shadow made at half size spreads as far as one made whole\n");
	for (i = 0; i < BLUR_KINDS; i++) {
		const blur_kind *bk = &BLUR_KIND[i];
		blur_shadow f = blur_shadow_size(bk->blur, bk->radius, 1);
		blur_shadow h = blur_shadow_size(bk->blur, bk->radius, BLUR_UNIT);
		double sf = spread(f.rad, 1), sh = spread(h.rad, BLUR_UNIT);

		snprintf(msg, sizeof msg, "%s: whole, three boxes of %d each side", bk->name,
		         bk->blur / 2);
		ck(f.unit == 1 && f.rad[0] == bk->blur / 2 && f.rad[1] == bk->blur / 2 &&
		   f.rad[2] == bk->blur / 2 && f.m == 3 * (bk->blur / 2), msg);
		snprintf(msg, sizeof msg, "%s: at %d units a texel, radii %d, %d, %d, a blur "
		         "%.1f units wide against %.1f (%+.1f%%)", bk->name, BLUR_UNIT,
		         h.rad[0], h.rad[1], h.rad[2], sh, sf, 100.0 * (sh / sf - 1.0));
		ck(h.unit == BLUR_UNIT && fabs(sh / sf - 1.0) < 0.02, msg);
	}
}

/* Each kind's texture, padded by m: what three passes reach, so it is the
 * rect's shadow with nothing cut off at the texture's edge - the same, texel
 * for texel, as the rect blurred in a buffer padded twice as far and cut down
 * to the texture's size afterwards - and its outermost texels beside an edge
 * are lit, so there is no padding the blur never reaches either. */
static void texture_padding(void)
{
	char msg[160];
	int i;

	printf("each shadow's texture is padded by what the blur reaches\n");
	for (i = 0; i < BLUR_KINDS; i++) {
		const int R = BLUR_KIND[i].radius;
		blur_shadow s = blur_shadow_size(BLUR_KIND[i].blur, R, BLUR_UNIT);
		/* The same rect, the same corners, in a buffer padded by 2m */
		blur_shadow wide = s;
		const int n = s.n, wn = n + 2 * s.m;
		float *a = malloc((size_t)n * n * sizeof *a);
		float *w = malloc((size_t)wn * wn * sizeof *w);
		float *line = malloc((size_t)wn * sizeof *line);
		float worst = 0.0f, tip;
		int mid = n / 2, x, y;

		if (!a || !w || !line) { ck(0, "memory"); free(a); free(w); free(line); return; }
		wide.m = 2 * s.m;
		wide.n = wn;
		blur_shadow_fill(a, &s, R, line);
		blur_shadow_fill(w, &wide, R, line);
		for (y = 0; y < n; y++)
			for (x = 0; x < n; x++) {
				float d = fabsf(a[(size_t)y * n + x] -
				                w[(size_t)(y + s.m) * wn + x + s.m]);

				if (d > worst) worst = d;
			}
		snprintf(msg, sizeof msg, "%s: padded by %d texels, nothing cut off (off by %.2g)",
		         BLUR_KIND[i].name, s.m, worst);
		ck(worst <= 1e-6f, msg);
		/* Lit at the edge by the tip of the kernel: a straight edge m away
		 * gets one part in each box's width, three times. */
		tip = 1.0f / ((2.0f * s.rad[0] + 1.0f) * (2.0f * s.rad[1] + 1.0f) *
		              (2.0f * s.rad[2] + 1.0f));
		snprintf(msg, sizeof msg, "%s: the blur reaches the texture's edge "
		         "(%.3g, owed %.3g)", BLUR_KIND[i].name, a[(size_t)mid * n], tip);
		ck(fabsf(a[(size_t)mid * n] - tip) < 0.05f * tip &&
		   fabsf(a[mid] - tip) < 0.05f * tip, msg);
		free(a);
		free(w);
		free(line);
	}
}

/* The nine pieces cover the shadow's extent once, every unit of it, at any
 * size: a gap is a line of no shadow and an overlap a line of twice as much.
 * The sizes are some of each kind's own besides: either side of the extent
 * that has room for two whole corners, where the cut starts, by a unit at a
 * time, as a texel is two of them. */
static void pieces_tile(void)
{
	static const int SIZES[] = { 1, 2, 3, 4, 5, 7, 40, 90, 143, 144, 145, 300, 441, 442, 443, 900 };
	enum { NS = (int)(sizeof SIZES / sizeof SIZES[0]), NK = 7 };
	char msg[160];
	int i, a, b, d;

	printf("the nine pieces cover the shadow once, at any size\n");
	for (i = 0; i < BLUR_KINDS; i++) {
		blur_shadow s = blur_shadow_size(BLUR_KIND[i].blur, BLUR_KIND[i].radius,
		                                 BLUR_UNIT);
		int size[NS + NK];

		for (a = 0; a < NS; a++) size[a] = SIZES[a];
		for (d = 0; d < NK; d++) size[NS + d] = 2 * s.k * s.unit - NK / 2 + d;
		for (a = 0; a < NS + NK; a++)
			for (b = 0; b < NS + NK; b++) {
				int ow = size[a], oh = size[b], x, y, p, bad = 0, out = 0;
				unsigned char *seen = calloc((size_t)ow * oh, 1);
				blur_rect src[9], dst[9];

				if (!seen) { ck(0, "memory"); return; }
				blur_slice(&s, 10, 20, ow, oh, src, dst);
				for (p = 0; p < 9; p++) {
					if (dst[p].w <= 0 || dst[p].h <= 0) continue;
					if (src[p].x < 0 || src[p].y < 0 || src[p].w <= 0 || src[p].h <= 0 ||
					    src[p].x + src[p].w > s.n || src[p].y + src[p].h > s.n)
						out = 1;
					for (y = dst[p].y; y < dst[p].y + dst[p].h; y++)
						for (x = dst[p].x; x < dst[p].x + dst[p].w; x++) {
							if (x < 10 || y < 20 || x >= 10 + ow || y >= 20 + oh)
								out = 1;
							else
								seen[(size_t)(y - 20) * ow + (x - 10)]++;
						}
				}
				for (p = 0; p < ow * oh; p++) if (seen[p] != 1) bad = 1;
				free(seen);
				snprintf(msg, sizeof msg, "%s at %d x %d: every unit once, inside it, "
				         "from inside the texture", BLUR_KIND[i].name, ow, oh);
				ck(!bad && !out, msg);
			}
	}
}

/* The rect `w` x `h` with corners of `R`, 1 inside and 0 out, padded by `m`
 * each way in a buffer of (w + 2m) x (h + 2m): a texel is in by its center.
 * A copy of blur_shadow_rect's on purpose: an oracle of its own, so a fault
 * there is not the same fault on both sides of the comparisons below. */
static void oracle_rect(float *a, int w, int h, int m, float R)
{
	const int ow = w + 2 * m, oh = h + 2 * m;
	int x, y;

	for (y = 0; y < oh; y++)
		for (x = 0; x < ow; x++) {
			float px = (float)x + 0.5f, py = (float)y + 0.5f;
			float dx = 0.0f, dy = 0.0f;
			int in = x >= m && x < ow - m && y >= m && y < oh - m;

			if (px < (float)m + R) dx = (float)m + R - px;
			if (px > (float)(ow - m) - R) dx = px - ((float)(ow - m) - R);
			if (py < (float)m + R) dy = (float)m + R - py;
			if (py > (float)(oh - m) - R) dy = py - ((float)(oh - m) - R);
			a[(size_t)y * ow + x] = in && dx * dx + dy * dy <= R * R ? 1.0f : 0.0f;
		}
}

/* Under a rect big enough for two whole corners each way, the nine pieces of
 * the texture, drawn at the texture's own size as SDL_RenderCopy draws them
 * at nearest texel, are the shadow that rect casts blurred whole at that
 * size: no seam where a corner meets an edge, and no step where an edge is
 * stretched. The rects are an even number of units, so they are a whole
 * number of texels at BLUR_UNIT and every piece lands on whole texels. */
static void pieces_are_the_shadow(void)
{
	static const struct { int w, h; } RECT[] = {
		{ 486, 622 }, { 940, 360 }, { 300, 300 }, { 260, 900 },
	};
	char msg[192];
	int i, j;

	printf("drawn in nine pieces, a shadow is the big rect's own\n");
	for (i = 0; i < BLUR_KINDS; i++) {
		const int R = BLUR_KIND[i].radius;
		blur_shadow s = blur_shadow_size(BLUR_KIND[i].blur, R, BLUR_UNIT);
		const int u = s.unit;
		float *tex = malloc((size_t)s.n * s.n * sizeof *tex);
		float *line = malloc(2048 * sizeof *line);

		if (!tex || !line) { ck(0, "memory"); free(tex); free(line); return; }
		blur_shadow_fill(tex, &s, R, line);
		for (j = 0; j < (int)(sizeof RECT / sizeof RECT[0]); j++) {
			/* The big rect's own shadow: the same rect and the same blur,
			 * at the texture's size, padded as the texture is. A
			 * blur_shadow with this rect's sides would be square, so it is
			 * filled here instead. */
			const int tw = RECT[j].w / u, th = RECT[j].h / u;
			const int ow = tw + 2 * s.m, oh = th + 2 * s.m;
			float *whole = malloc((size_t)ow * oh * sizeof *whole);
			float worst = 0.0f;
			int wx = 0, wy = 0, x, y, p, whole_texels = 1;
			blur_rect src[9], dst[9];

			if (!whole) { ck(0, "memory"); break; }
			/* Big enough is two whole corners each way, which every rect
			 * here is for every kind: one that is not is held by
			 * pieces_tile and not to this. */
			ck(ow >= 2 * s.k && oh >= 2 * s.k, "the rect is big enough for the cut");
			oracle_rect(whole, tw, th, s.m, (float)R / (float)u);
			blur_box3_by(whole, ow, oh, s.rad, line);

			blur_slice(&s, 0, 0, ow * u, oh * u, src, dst);
			for (p = 0; p < 9; p++)
				if (dst[p].x % u || dst[p].y % u || dst[p].w % u || dst[p].h % u)
					whole_texels = 0;
			ck(whole_texels, "every piece lands on whole texels");
			for (p = 0; p < 9; p++)
				for (y = dst[p].y / u; y < (dst[p].y + dst[p].h) / u; y++)
					for (x = dst[p].x / u; x < (dst[p].x + dst[p].w) / u; x++) {
						int tx = src[p].x + (x - dst[p].x / u) * src[p].w * u / dst[p].w;
						int ty = src[p].y + (y - dst[p].y / u) * src[p].h * u / dst[p].h;
						float d = fabsf(tex[(size_t)ty * s.n + tx] -
						                whole[(size_t)y * ow + x]);

						if (d > worst) { worst = d; wx = x; wy = y; }
					}
			snprintf(msg, sizeof msg, "%s under %d x %d: off by %.2g at (%d, %d) texels "
			         "from the rect's corner, over %.2g", BLUR_KIND[i].name, RECT[j].w,
			         RECT[j].h, worst, wx - s.m, wy - s.m, TOL);
			ck(worst <= TOL, msg);
			free(whole);
		}
		free(tex);
		free(line);
	}
}

/* The texture as the GPU reads it with linear filtering, at (`u`, `v`) in
 * texels: the four texels whose centers are round it, mixed by how near each
 * is, the texture's edge held - not a piece's, since a piece's neighbours in
 * the texture are what it meets on the screen. It is what SDL's renderers
 * draw a texture set to SDL_ScaleModeLinear with, and a copy of it here on
 * purpose, as oracle_rect is. */
static float linear_at(const float *t, int n, float u, float v)
{
	float x = u - 0.5f, y = v - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y), x1, y1;
	float fx = x - (float)x0, fy = y - (float)y0;

	x1 = x0 + 1;
	y1 = y0 + 1;
	x0 = x0 < 0 ? 0 : (x0 > n - 1 ? n - 1 : x0);
	x1 = x1 < 0 ? 0 : (x1 > n - 1 ? n - 1 : x1);
	y0 = y0 < 0 ? 0 : (y0 > n - 1 ? n - 1 : y0);
	y1 = y1 < 0 ? 0 : (y1 > n - 1 ? n - 1 : y1);
	return (t[(size_t)y0 * n + x0] * (1.0f - fx) + t[(size_t)y0 * n + x1] * fx) * (1.0f - fy) +
	       (t[(size_t)y1 * n + x0] * (1.0f - fx) + t[(size_t)y1 * n + x1] * fx) * fy;
}

/* How far a shadow made at half size and drawn back up may be from the one
 * made whole, in its alpha: 1.5 of the 255 steps. Drawn at the most any kind
 * is drawn at, **focus**'s 22% of **ink**, that is a third of one step of
 * the screen's 8 bits, which no eye and no shot can see. */
#define TOL_HALF (1.5f / 255.0f)

/* Under a rect big enough for two whole corners each way, of any size in
 * units, odd ones too, the nine pieces of the texture made at BLUR_UNIT,
 * drawn on the screen as the GPU draws them - each pixel's center mapped
 * into its piece and read with linear filtering - are the shadow that rect
 * casts made whole at full size, a texel a unit and three boxes of blur / 2,
 * as it was made before it was made at half size. That is the whole of what
 * the half size owes: the same softness, as far out, in the same place. */
static void half_is_whole(void)
{
	static const struct { int w, h; } RECT[] = {
		{ 485, 622 }, { 940, 360 }, { 300, 300 }, { 261, 899 }, { 150, 111 },
	};
	char msg[224];
	int i, j;

	printf("made at half size and drawn at twice it, a shadow is the one made whole\n");
	for (i = 0; i < BLUR_KINDS; i++) {
		const int R = BLUR_KIND[i].radius;
		const blur_shadow s = blur_shadow_size(BLUR_KIND[i].blur, R, BLUR_UNIT);
		const blur_shadow f = blur_shadow_size(BLUR_KIND[i].blur, R, 1);
		const int pad = blur_pad(&s);
		float *tex = malloc((size_t)s.n * s.n * sizeof *tex);
		float *line = malloc(2048 * sizeof *line);

		if (!tex || !line) { ck(0, "memory"); free(tex); free(line); return; }
		blur_shadow_fill(tex, &s, R, line);
		for (j = 0; j < (int)(sizeof RECT / sizeof RECT[0]); j++) {
			/* The shadow made whole, padded by the full size's reach,
			 * which is a texel or so past the half size's: what the half
			 * size leaves out there is the tip of three kernels, a
			 * hundred-thousandth of the light, and it is compared too. */
			const int ow = RECT[j].w + 2 * f.m, oh = RECT[j].h + 2 * f.m;
			const int sw = RECT[j].w + 2 * pad, sh = RECT[j].h + 2 * pad;
			const int off = f.m - pad;
			float *whole = malloc((size_t)ow * oh * sizeof *whole);
			float worst = 0.0f;
			int wx = 0, wy = 0, x, y, p;
			blur_rect src[9], dst[9];

			if (!whole) { ck(0, "memory"); break; }
			if (sw < 2 * s.k * s.unit || sh < 2 * s.k * s.unit) { free(whole); continue; }
			oracle_rect(whole, RECT[j].w, RECT[j].h, f.m, (float)R);
			blur_box3(whole, ow, oh, BLUR_KIND[i].blur / 2, line);

			/* Everything but the half size's extent is the tip it leaves out */
			for (y = 0; y < oh; y++)
				for (x = 0; x < ow; x++) {
					int in = x >= off && x < off + sw && y >= off && y < off + sh;
					float d = in ? 0.0f : whole[(size_t)y * ow + x];

					if (d > worst) { worst = d; wx = x; wy = y; }
				}
			blur_slice(&s, off, off, sw, sh, src, dst);
			for (p = 0; p < 9; p++)
				for (y = dst[p].y; y < dst[p].y + dst[p].h; y++)
					for (x = dst[p].x; x < dst[p].x + dst[p].w; x++) {
						float tu = (float)src[p].x + ((float)(x - dst[p].x) + 0.5f) *
						           (float)src[p].w / (float)dst[p].w;
						float tv = (float)src[p].y + ((float)(y - dst[p].y) + 0.5f) *
						           (float)src[p].h / (float)dst[p].h;
						float d = fabsf(linear_at(tex, s.n, tu, tv) -
						                whole[(size_t)y * ow + x]);

						if (d > worst) { worst = d; wx = x; wy = y; }
					}
			snprintf(msg, sizeof msg, "%s under %d x %d: off by %.2f of 255 at (%d, %d) "
			         "units from the rect's corner, over %.1f", BLUR_KIND[i].name,
			         RECT[j].w, RECT[j].h, worst * 255.0f, wx - f.m, wy - f.m,
			         TOL_HALF * 255.0f);
			ck(worst <= TOL_HALF, msg);
			free(whole);
		}
		free(tex);
		free(line);
	}
}

/* A shape made of triangles, filled for a shadow cast in its own shape
 * (Zyper's on Beard): a square of two triangles is its own texels and
 * nothing else, the edge they share included and counted once; the same
 * triangle twice, or a shape drawn over another, is the union of them and
 * not their sum; a triangle's coverage adds up to its area; and either
 * winding fills alike. */
static void tris_fill(void)
{
	enum { W = 16, H = 12 };
	float a[W * H], b[W * H];
	/* [2, 6) by [3, 7), split on its diagonal, one triangle each way round */
	const float sq[] = { 2, 3, 6, 3, 6, 7,   2, 3, 2, 7, 6, 7 };
	const float tri[] = { 8, 1, 15, 1, 8, 9 };          /* legs of 7 and 8 */
	const float twice[] = { 8, 1, 15, 1, 8, 9,   8, 9, 15, 1, 8, 1 };
	int x, y, exact = 1;

	ck(blur_fill_tris(a, W, H, sq, 6), "a square fills");
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++)
			if (a[y * W + x] != (x >= 2 && x < 6 && y >= 3 && y < 7 ? 1.0f : 0.0f))
				exact = 0;
	ck(exact, "a square of two triangles is its own texels, its diagonal once");

	ck(blur_fill_tris(a, W, H, tri, 3), "a triangle fills");
	ck(fabs(total(a, W * H) - 28.0) < 0.5, "a triangle's coverage is its area");
	ck(blur_fill_tris(b, W, H, twice, 6), "the same triangle twice fills");
	ck(!memcmp(a, b, sizeof a), "and is the union, in either winding: once");
}

int main(void)
{
	one_pass();
	three_reach();
	sizes();
	texture_padding();
	pieces_tile();
	pieces_are_the_shadow();
	half_is_whole();
	tris_fill();
	if (fails) { printf("\n%d FAILED\n", fails); return 1; }
	printf("\nok: a shadow keeps its light, reaches its padding, has no seams, "
	       "and made at half size is the one made whole\n");
	return 0;
}
