/* SPDX-License-Identifier: GPL-3.0-only */
/* The boot screen of a start slower than usual (plastron's
 * package/splash/src/slowline.h, DESIGN.md 11.20), with the splash's own code,
 * on the splash's own frames:
 *
 *   - every language's lines read, for each handheld's scale;
 *   - nothing before 5 s, the step from 5 s, the first line and the help from
 *     20 s; the copy (U2) and a step it does not know say nothing;
 *   - no letter of any line, in any language, lands on a pixel of the
 *     animation that is not its ground, on any frame that can be up then
 *     (the Pixel 2's loop in both themes, the Brick's and the Smart Pro's
 *     last frame);
 *   - drawn, changed, taken away: the frame is its own again to the pixel,
 *     the tester's view too, and the rows it says changed cover every pixel
 *     that did;
 *   - the first start's line, its bar still: the help in its place, the bar
 *     untouched;
 *   - the beat's line, a /dev/kmsg record, the step's word.
 *
 *   build-native/bootslow-check [--shots DIR]   DIR: each handheld, theme and
 *   language at 6 s, 21 s and in the tester's view, as PPM, landscape */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../plastron/buildroot/package/splash/src/slowline.h"

#define PKG "plastron/buildroot/package/splash/"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *const LANGS[] = { "en", "zh-Hans", "es", "pt-BR", "fr", "de", "ja", "it" };
#define NLANG 8

static uint8_t *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b;
	long sz;

	*n = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || !(b = malloc((size_t)sz))) { fclose(f); return NULL; }
	if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
	fclose(f);
	*n = (size_t)sz;
	return b;
}

/* The splash's frames: "TSPY" (the Pixel 2's, a loop) or "TSPZ" (the
 * Brick's and the Smart Pro's), each frame's band over the one before, as
 * splash.c and splash-fb.c decode them */
typedef struct {
	uint8_t *f;
	size_t size;
	unsigned w, h, count, loop, header;
	uint32_t *px;
} frames;

static int frames_open(frames *fr, const char *path)
{
	memset(fr, 0, sizeof *fr);
	if (!(fr->f = slurp(path, &fr->size)) || fr->size < 16) return -1;
	fr->w = slow_u16(fr->f + 4);
	fr->h = slow_u16(fr->f + 6);
	fr->count = slow_u16(fr->f + 8);
	if (!memcmp(fr->f, "TSPY", 4)) { fr->loop = slow_u16(fr->f + 12); fr->header = 16; }
	else if (!memcmp(fr->f, "TSPZ", 4)) { fr->loop = fr->count; fr->header = 12; }
	else return -1;
	fr->px = calloc((size_t)fr->w * fr->h, 4);
	return fr->px ? 0 : -1;
}

static int frame_decode(frames *fr, unsigned n)
{
	size_t stride = (size_t)fr->w * 4;
	uint32_t at = slow_u32(fr->f + fr->header + 4 * n);
	unsigned first, rows;
	uLongf size;

	if (at + 8 > fr->size) return -1;
	first = slow_u16(fr->f + at);
	rows = slow_u16(fr->f + at + 2);
	if (!rows) return 0;
	uint8_t *band = (uint8_t *)fr->px + first * stride;
	size = rows * stride;
	if (uncompress(band, &size, fr->f + at + 8, slow_u32(fr->f + at + 4)) != Z_OK) return -1;
	for (unsigned y = 0; y < rows; y++)
		for (size_t i = 4; i < stride; i++) band[y * stride + i] += band[y * stride + i - 4];
	return 0;
}

typedef struct {
	const char *name, *frames, *dir;
	int turned, light;
	uint32_t ground, soft, dim;
} handheld;

static const handheld HANDHELDS[] = {
	{ "pixel2-dark", PKG "frames.bin", "px2", 1, 0, 0xff111310, 0xffc3c3ba, 0xff969998 },
	{ "pixel2-light", PKG "frames-light.bin", "px2", 1, 1, 0xffe6e4da, 0xff3d403e, 0xff5a5d5a },
	{ "brick", PKG "frames-brick.bin", "brick", 0, 0, 0xff111310, 0xffc3c3ba, 0xff969998 },
	{ "smartpro", PKG "smartpro/frames.bin", "smartpro", 0, 0, 0xff111310, 0xffc3c3ba, 0xff969998 },
};
#define NHH 4

static int near(uint32_t a, uint32_t b, int tol)
{
	for (int s = 0; s < 24; s += 8) {
		int d = (int)(a >> s & 0xff) - (int)(b >> s & 0xff);
		if (d > tol || d < -tol) return 0;
	}
	return 1;
}

/* Every pixel a line inks at its places is the ground in frame `px` */
static int clear_of(const slow_run *r, const uint32_t *px, const char *what)
{
	static const int Y[] = { SLOW_TOP_Y, SLOW_LINE_Y, SLOW_HELP_Y };
	int bad = 0;

	for (int id = 0; id < SLOW_LINES; id++) {
		const slow_line *l = &r->set.line[id];

		/* The first line over Zyper, the help over Zyper and (a first
		 * start's copy) in its line's place, the steps at the foot */
		for (int k = 0; k < 3; k++) {
			if ((id == SLOW_TOP) != (k == 0)) continue;
			if (id != SLOW_STUCK && k == 2) continue;
			int x = (int)r->W / 2 - (int)l->w / 2, y = slow_px(&r->set, Y[k]) - (int)l->ascent;

			for (unsigned s = 0; s < l->nseg; s++) {
				for (unsigned row = 0; row < l->h; row++)
					for (unsigned i = 0; i < l->seg[s].w; i++) {
						int lx = x + (int)i, ly = y + (int)row;
						uint32_t p;

						if (!l->seg[s].cov[(size_t)row * l->seg[s].w + i]) continue;
						if (lx < 0 || ly < 0 || lx >= (int)r->W || ly >= (int)r->H) { bad++; continue; }
						p = r->turned ? px[(size_t)(r->W - 1 - lx) * r->stride + ly] : px[(size_t)ly * r->stride + lx];
						if (!near(p, r->ground, 6)) bad++;
					}
				x += (int)l->seg[s].w;
			}
		}
	}
	CHECK(!bad, "%s: %d letter pixels on the animation or off the screen", what, bad);
	return bad;
}

static void ppm(const char *path, const slow_run *r, const uint32_t *px)
{
	FILE *f = fopen(path, "wb");

	if (!f) return;
	fprintf(f, "P6\n%u %u\n255\n", r->W, r->H);
	for (unsigned y = 0; y < r->H; y++)
		for (unsigned x = 0; x < r->W; x++) {
			uint32_t p = r->turned ? px[(size_t)(r->W - 1 - x) * r->stride + y] : px[(size_t)y * r->stride + x];
			fputc(p >> 16 & 0xff, f);
			fputc(p >> 8 & 0xff, f);
			fputc(p & 0xff, f);
		}
	fclose(f);
}

/* Rows of the buffer that differ between a and b, against [lo, hi) */
static int rows_cover(const slow_run *r, const uint32_t *a, const uint32_t *b, int lo, int hi)
{
	unsigned nrows = r->turned ? r->W : r->H;

	for (unsigned y = 0; y < nrows; y++)
		if (memcmp(a + (size_t)y * r->stride, b + (size_t)y * r->stride, (size_t)r->stride * 4) &&
		    ((int)y < lo || (int)y >= hi))
			return 0;
	return 1;
}

/* The topmost landscape row of the screen's upper third that is not as
 * `base` has it, and how unlike it the most: where a line over Zyper is,
 * and how much of it is in. H for none. */
static unsigned top_ink(const slow_run *r, const uint32_t *base, const uint32_t *buf, int *most)
{
	unsigned top = r->H;

	*most = 0;
	for (unsigned y = 0; y < r->H / 3; y++)
		for (unsigned x = 0; x < r->W; x++) {
			size_t i = r->turned ? (size_t)(r->W - 1 - x) * r->stride + y : (size_t)y * r->stride + x;
			int d = (int)(buf[i] & 0xff) - (int)(base[i] & 0xff);

			if (buf[i] == base[i]) continue;
			if (y < top) top = y;
			if (d < 0) d = -d;
			if (d > *most) *most = d;
		}
	return top;
}

int main(int argc, char **argv)
{
	const char *shots = argc == 3 && !strcmp(argv[1], "--shots") ? argv[2] : NULL;
	int top, step, help, total = 0;
	char line[160], word[4];

	if (shots) mkdir(shots, 0755);

	/* When: nothing before 5 s, the step from 5 s, all three from 20 s */
	slow_view(4.99, 3, &top, &step, &help);
	CHECK(step < 0 && !top && !help, "something at 4.99 s");
	slow_view(5.0, 3, &top, &step, &help);
	CHECK(step == 3 && !top && !help, "not the step alone at 5 s");
	slow_view(19.99, 3, &top, &step, &help);
	CHECK(step == 3 && !top && !help, "more than the step at 19.99 s");
	slow_view(20.0, 3, &top, &step, &help);
	CHECK(step == 3 && top && help, "not the three at 20 s");
	slow_view(60.0, -1, &top, &step, &help);
	CHECK(step < 0 && !top && !help, "something with no step at 60 s");
	/* The steps' lines: the copy (U2) and what it does not know say nothing */
	CHECK(slow_stage_line("C1") == 1 && slow_stage_line("C2") == 2 && slow_stage_line("C3") == 3 &&
	      slow_stage_line("U1") == 4 && slow_stage_line("L1") == 5, "a step on another line");
	CHECK(slow_stage_line("U2") < 0 && slow_stage_line("K1") < 0 && slow_stage_line("") < 0 &&
	      slow_stage_line(NULL) < 0, "the copy or an unknown step has a line");
	/* The step's word, as the scripts write it */
	slow_stage_word("C3\n", word);
	CHECK(!strcmp(word, "C3"), "C3\\n read as %s", word);
	slow_stage_word(" L1 launch.sh\n", word);
	CHECK(!strcmp(word, "L1"), "L1 with more read as %s", word);
	slow_stage_word("c3", word);
	CHECK(!word[0], "a word in small letters taken: %s", word);
	slow_stage_word("C3X9", word);
	CHECK(!word[0], "a word too long taken: %s", word);
	/* The beat, and a record of the kernel's log */
	slow_beat(line, sizeof line, 12.0, 663, "L1");
	CHECK(!strcmp(line, "splash: 12.0 s, 663 frames, stage=L1\n"), "the beat: %s", line);
	slow_beat(line, sizeof line, 2.0, 63, "");
	CHECK(!strcmp(line, "splash: 2.0 s, 63 frames, stage=-\n"), "the beat with no step: %s", line);
	CHECK(slow_kmsg_line("6,1234,620000,-;mmc0: new card\n", line, sizeof line) == 0 &&
	      !strcmp(line, "[    0.620000] mmc0: new card"), "a kmsg record: %s", line);
	CHECK(slow_kmsg_line("12,99,61250000,c;zyboot: stage=L1\xc3\xa9\n", line, sizeof line) == 0 &&
	      !strcmp(line, "[   61.250000] zyboot: stage=L1??"), "a kmsg record with UTF-8: %s", line);
	CHECK(slow_kmsg_line("not a record", line, sizeof line) < 0, "not a record read");

	for (int h = 0; h < NHH; h++) {
		const handheld *hh = &HANDHELDS[h];
		frames fr;
		size_t gn;
		char gpath[128];
		uint8_t *gf;

		snprintf(gpath, sizeof gpath, PKG "slow/%s/glyphs.bin", hh->dir);
		gf = slurp(gpath, &gn);

		if (frames_open(&fr, hh->frames)) { CHECK(0, "%s: no frames at %s", hh->name, hh->frames); continue; }
		for (int li = 0; li < NLANG; li++) {
			char path[256];
			size_t n;
			uint8_t *f;
			slow_run r;
			unsigned W = hh->turned ? fr.h : fr.w, H = hh->turned ? fr.w : fr.h;

			snprintf(path, sizeof path, PKG "slow/%s/%s.bin", hh->dir, LANGS[li]);
			slow_run_init(&r, W, H, fr.w, hh->turned, hh->ground, hh->soft, hh->dim);
			f = slurp(path, &n);
			CHECK(f && slow_load(f, n, &r.set) == 0, "%s: %s does not read", hh->name, path);
			free(f);
			r.have = 1;
			r.have_g = gf && slow_glyphs_load(gf, gn, &r.g) == 0;
			CHECK(r.have_g, "%s does not read", gpath);
			for (int id = 0; id < SLOW_LINES; id++)
				CHECK(r.set.line[id].have, "%s %s: no line %d", hh->name, LANGS[li], id);
			for (int id = 1; id <= 5; id++)
				CHECK(r.set.line[id].nseg == 2 && !r.set.line[id].seg[0].dim && r.set.line[id].seg[1].dim,
				      "%s %s: step %d is not its words and then its code in text-dim", hh->name, LANGS[li], id);
			CHECK(r.set.line[SLOW_TOP].h > r.set.line[1].h, "%s %s: the first line is not the larger",
			      hh->name, LANGS[li]);
			/* Clear of the animation on every frame that can be up from 5 s:
			 * the Pixel 2's loop (all of it, and the band back to its
			 * start), the others' last frame */
			memset(fr.px, 0, (size_t)fr.w * fr.h * 4);
			for (unsigned k = 0; k < fr.count; k++) {
				char what[96];

				if (frame_decode(&fr, k)) { CHECK(0, "%s: frame %u", hh->name, k); break; }
				if (li > 0 && k + 1 < fr.count) continue;   /* every frame once, in English */
				if (k >= (fr.loop < fr.count ? fr.loop : fr.count - 1)) {
					snprintf(what, sizeof what, "%s %s frame %u", hh->name, LANGS[li], k);
					if (clear_of(&r, fr.px, what)) break;
				}
			}
			/* Drawn, changed, taken away, on the last frame */
			{
				size_t bytes = (size_t)fr.w * fr.h * 4;
				uint32_t *base = malloc(bytes), *buf = malloc(bytes), *prev = malloc(bytes);
				char kl[3][160] = { "[    0.556257] mmcblk0: mmc0:aaaa SN128 119 GiB",
				                    "[    0.903206] datapart: /dev/mmcblk0p3 was not unmounted cleanly; checking it, a line long enough to wrap twice on a small panel",
				                    "[    0.903400] zyboot: stage=C3" };
				int lo, hi, c;

				memcpy(base, fr.px, bytes);
				memcpy(buf, base, bytes);
				lo = hi = 0;
				c = slow_frame(&r, buf, 4.0, "C3", 0, kl, 3, "C3  4.0 s  183 frames", &lo, &hi);
				CHECK(!c && !memcmp(buf, base, bytes), "%s %s: drawn at 4 s", hh->name, LANGS[li]);
				/* The step arrives in quick: nothing of it at its first
				 * frame, some by half way, all of it after */
				memcpy(prev, buf, bytes);
				lo = hi = 0;
				c = slow_frame(&r, buf, 6.0, "C3", 0, kl, 3, "", &lo, &hi);
				CHECK(c && !memcmp(buf, base, bytes), "%s %s: the step whole on its first frame", hh->name, LANGS[li]);
				lo = hi = 0;
				c = slow_frame(&r, buf, 6.0 + SLOW_QUICK_S / 4, "C3", 0, kl, 3, "", &lo, &hi);
				CHECK(c && memcmp(buf, base, bytes) && rows_cover(&r, prev, buf, lo, hi),
				      "%s %s: the step not coming in, or its rows", hh->name, LANGS[li]);
				memcpy(prev, buf, bytes);
				lo = hi = 0;
				c = slow_frame(&r, buf, 6.0 + SLOW_QUICK_S, "C3", 0, kl, 3, "", &lo, &hi);
				CHECK(c && memcmp(buf, prev, bytes) && rows_cover(&r, prev, buf, lo, hi),
				      "%s %s: the step at 6 s, or its rows", hh->name, LANGS[li]);
				if (shots && (h != 1 || li == 0)) {
					snprintf(path, sizeof path, "%s/%s-%s-06s.ppm", shots, hh->name, LANGS[li]);
					ppm(path, &r, buf);
				}
				/* The same again changes nothing, said or drawn */
				memcpy(prev, buf, bytes);
				lo = hi = 0;
				c = slow_frame(&r, buf, 6.5, "C3", 0, kl, 3, "", &lo, &hi);
				CHECK(!c && !memcmp(prev, buf, bytes), "%s %s: the same step drew again", hh->name, LANGS[li]);
				/* The first line and the help arrive in quick, coming down
				 * SLOW_DROP_Y as they come in: higher and fainter half way */
				{
					unsigned t_half, t_all;
					int m_half, m_all;

					lo = hi = 0;
					c = slow_frame(&r, buf, 21.0, "L1", 0, kl, 3, "", &lo, &hi);
					CHECK(c && rows_cover(&r, prev, buf, lo, hi), "%s %s: 21 s's rows", hh->name, LANGS[li]);
					memcpy(prev, buf, bytes);
					lo = hi = 0;
					c = slow_frame(&r, buf, 21.0 + SLOW_QUICK_S / 4, "L1", 0, kl, 3, "", &lo, &hi);
					CHECK(c && rows_cover(&r, prev, buf, lo, hi), "%s %s: 21 s's rows coming in", hh->name, LANGS[li]);
					t_half = top_ink(&r, base, buf, &m_half);
					memcpy(prev, buf, bytes);
					lo = hi = 0;
					c = slow_frame(&r, buf, 21.0 + SLOW_QUICK_S, "L1", 0, kl, 3, "", &lo, &hi);
					CHECK(c && rows_cover(&r, prev, buf, lo, hi), "%s %s: 21 s's rows come in", hh->name, LANGS[li]);
					t_all = top_ink(&r, base, buf, &m_all);
					CHECK(t_half < t_all && m_half < m_all && t_all < r.H,
					      "%s %s: the first line did not come down and in (top %u then %u, ink %d then %d)",
					      hh->name, LANGS[li], t_half, t_all, m_half, m_all);
					memcpy(prev, buf, bytes);
					lo = hi = 0;
					c = slow_frame(&r, buf, 21.5, "L1", 0, kl, 3, "", &lo, &hi);
					CHECK(!c && !memcmp(prev, buf, bytes), "%s %s: the lines in moved again", hh->name, LANGS[li]);
				}
				if (shots) {
					snprintf(path, sizeof path, "%s/%s-%s-21s.ppm", shots, hh->name, LANGS[li]);
					ppm(path, &r, buf);
				}
				memcpy(prev, buf, bytes);
				lo = hi = 0;
				c = slow_frame(&r, buf, 23.0, "L1", 1, kl, 3, "L1  23.0 s  1323 frames", &lo, &hi);
				CHECK(c && rows_cover(&r, prev, buf, lo, hi), "%s %s: the tester's view's rows", hh->name, LANGS[li]);
				if (shots && li == 0) {
					snprintf(path, sizeof path, "%s/%s-tester.ppm", shots, hh->name);
					ppm(path, &r, buf);
				}
				lo = hi = 0;
				c = slow_frame(&r, buf, 23.5, "L1", 0, kl, 3, "", &lo, &hi);
				CHECK(c && !memcmp(prev, buf, bytes), "%s %s: the tester's view left something behind",
				      hh->name, LANGS[li]);
				lo = hi = 0;
				c = slow_frame(&r, buf, 24.0, "", 0, kl, 3, "", &lo, &hi);
				CHECK(c && !memcmp(buf, base, bytes), "%s %s: the lines left something behind", hh->name, LANGS[li]);
				/* The copy's line, its bar still: the bar's rows untouched */
				if (h == 0 || h == 1) {
					memcpy(buf, base, bytes);
					lo = hi = 0;
					slow_copy_stuck(&r, buf, &lo, &hi);
					int y0 = slow_px(&r.set, 723), same = 1;
					for (unsigned y = (unsigned)y0; y < r.H; y++)
						for (unsigned x = 0; x < r.W; x++)
							if (buf[(size_t)(r.W - 1 - x) * r.stride + y] != base[(size_t)(r.W - 1 - x) * r.stride + y])
								same = 0;
					CHECK(same && lo < hi, "%s %s: the copy's help reached the bar", hh->name, LANGS[li]);
					if (shots && li == 0) {
						snprintf(path, sizeof path, "%s/%s-copy-stuck.ppm", shots, hh->name);
						ppm(path, &r, buf);
					}
				}
				free(base);
				free(buf);
				free(prev);
			}
			slow_free(&r.set);
			free(r.under);
			total++;
		}
		free(gf);
		free(fr.f);
		free(fr.px);
	}
	if (fails) { printf("bootslow-check: %d failed\n", fails); return 1; }
	printf("bootslow-check: %d handheld and language sets: nothing before 5 s, the step from 5 s, the first line and the"
	       " help from 20 s, each arriving in quick, clear of every frame, taken away to the pixel, the tester's view, the copy's help over its"
	       " line only, the beat\n", total);
	return 0;
}
