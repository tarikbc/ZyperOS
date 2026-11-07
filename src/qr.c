/* SPDX-License-Identifier: GPL-3.0-only */
/* A QR code encoder: byte mode, level M, versions 1 to QR_VERSION_MAX. See
 * qr.h. The steps are the standard's, in its order: the bits, the error
 * correction, the blocks interleaved, the fixed patterns, the bits placed in
 * the zigzag, and of the eight masks the one with the lowest penalty. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qr.h"

/* Level M's error correction, per version (index 0 unused): codewords in
 * each block, and blocks. ISO/IEC 18004 table 9. */
static const uint8_t ECC_PER_BLOCK[QR_VERSION_MAX + 1] =
	{ 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
static const uint8_t ECC_BLOCKS[QR_VERSION_MAX + 1] =
	{ 0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 };

#define CODEWORDS_MAX 346                 /* version 10's, data and correction */

typedef struct {
	int           size;
	unsigned char dark[QR_SIZE_MAX * QR_SIZE_MAX];
	unsigned char fixed[QR_SIZE_MAX * QR_SIZE_MAX];   /* a function pattern */
} grid;

static void set(grid *g, int x, int y, int dark, int fixed)
{
	g->dark[y * g->size + x] = (unsigned char)(dark != 0);
	if (fixed) g->fixed[y * g->size + x] = 1;
}

/* Modules left for data and correction once the patterns are drawn. */
static int raw_modules(int ver)
{
	int n = (16 * ver + 128) * ver + 64;

	if (ver >= 2) {
		int align = ver / 7 + 2;

		n -= (25 * align - 10) * align - 55;
		if (ver >= 7) n -= 36;
	}
	return n;
}

static int data_codewords(int ver)
{
	return raw_modules(ver) / 8 - ECC_PER_BLOCK[ver] * ECC_BLOCKS[ver];
}

/* ---- the error correction: Reed-Solomon over GF(256), x^8+x^4+x^3+x^2+1 */

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
	unsigned r = 0;
	int i;

	for (i = 7; i >= 0; i--) {
		r = (r << 1) ^ ((r >> 7) * 0x11d);
		r ^= ((b >> i) & 1u) * a;
	}
	return (uint8_t)r;
}

/* The generator of degree `d`, its leading 1 left out, highest first. */
static void rs_generator(int d, uint8_t *gen)
{
	uint8_t root = 1;
	int i, j;

	memset(gen, 0, (size_t)d);
	gen[d - 1] = 1;
	for (i = 0; i < d; i++) {
		for (j = 0; j < d; j++) {
			gen[j] = gf_mul(gen[j], root);
			if (j + 1 < d) gen[j] ^= gen[j + 1];
		}
		root = gf_mul(root, 2);
	}
}

static void rs_remainder(const uint8_t *data, int n, const uint8_t *gen, int d,
                         uint8_t *out)
{
	int i, j;

	memset(out, 0, (size_t)d);
	for (i = 0; i < n; i++) {
		uint8_t f = data[i] ^ out[0];

		memmove(out, out + 1, (size_t)(d - 1));
		out[d - 1] = 0;
		for (j = 0; j < d; j++) out[j] ^= gf_mul(gen[j], f);
	}
}

/* The data codewords split into the version's blocks, each followed by its
 * correction, then read out a codeword of each block at a time. The short
 * blocks come first and hold one data codeword fewer. */
static int interleave(int ver, const uint8_t *data, uint8_t *out)
{
	int blocks = ECC_BLOCKS[ver], ecc = ECC_PER_BLOCK[ver];
	int raw = raw_modules(ver) / 8;
	int shorts = blocks - raw % blocks, short_len = raw / blocks;
	uint8_t blk[8][CODEWORDS_MAX / 2], gen[32];
	int b, i, k = 0, n = 0;

	rs_generator(ecc, gen);
	for (b = 0; b < blocks; b++) {
		int len = short_len - ecc + (b < shorts ? 0 : 1);

		memcpy(blk[b], data + k, (size_t)len);
		k += len;
		/* A short block's missing codeword is a gap kept in its place */
		if (b < shorts) blk[b][len] = 0;
		rs_remainder(blk[b], len, gen, ecc, blk[b] + short_len + 1 - ecc);
	}
	for (i = 0; i <= short_len; i++)
		for (b = 0; b < blocks; b++)
			if (i != short_len - ecc || b >= shorts) out[n++] = blk[b][i];
	return n;
}

/* ---- the fixed patterns ------------------------------------------------ */

static void finder(grid *g, int cx, int cy)
{
	int dx, dy;

	for (dy = -4; dy <= 4; dy++)
		for (dx = -4; dx <= 4; dx++) {
			int x = cx + dx, y = cy + dy;
			int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

			if (x >= 0 && x < g->size && y >= 0 && y < g->size)
				set(g, x, y, d != 2 && d != 4, 1);
		}
}

static void alignment(grid *g, int cx, int cy)
{
	int dx, dy;

	for (dy = -2; dy <= 2; dy++)
		for (dx = -2; dx <= 2; dx++) {
			int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

			set(g, cx + dx, cy + dy, d != 1, 1);
		}
}

/* The level and the mask, BCH(15,5)-protected, in its two copies, and the
 * dark module beside the bottom-left finder. Level M is 00. */
static void format_bits(grid *g, int mask)
{
	int data = (0 << 3) | mask, rem = data, bits, i, s = g->size;

	for (i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	bits = ((data << 10) | rem) ^ 0x5412;
	for (i = 0; i <= 5; i++) set(g, 8, i, (bits >> i) & 1, 1);
	set(g, 8, 7, (bits >> 6) & 1, 1);
	set(g, 8, 8, (bits >> 7) & 1, 1);
	set(g, 7, 8, (bits >> 8) & 1, 1);
	for (i = 9; i < 15; i++) set(g, 14 - i, 8, (bits >> i) & 1, 1);
	for (i = 0; i < 8; i++) set(g, s - 1 - i, 8, (bits >> i) & 1, 1);
	for (i = 8; i < 15; i++) set(g, 8, s - 15 + i, (bits >> i) & 1, 1);
	set(g, 8, s - 8, 1, 1);
}

static void version_bits(grid *g, int ver)
{
	int rem = ver, bits, i;

	if (ver < 7) return;
	for (i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
	bits = (ver << 12) | rem;
	for (i = 0; i < 18; i++) {
		int a = g->size - 11 + i % 3, b = i / 3, bit = (bits >> i) & 1;

		set(g, a, b, bit, 1);
		set(g, b, a, bit, 1);
	}
}

static void patterns(grid *g, int ver)
{
	int i, j, s = g->size;

	for (i = 0; i < s; i++) {
		set(g, 6, i, i % 2 == 0, 1);
		set(g, i, 6, i % 2 == 0, 1);
	}
	finder(g, 3, 3);
	finder(g, s - 4, 3);
	finder(g, 3, s - 4);
	if (ver >= 2) {
		int n = ver / 7 + 2, pos[7];
		int step = (ver * 8 + n * 3 + 5) / (n * 4 - 4) * 2, p;

		pos[0] = 6;
		for (i = n - 1, p = ver * 4 + 10; i >= 1; i--, p -= step) pos[i] = p;
		for (i = 0; i < n; i++)
			for (j = 0; j < n; j++) {
				/* not over the three finders */
				if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
					continue;
				alignment(g, pos[i], pos[j]);
			}
	}
	format_bits(g, 0);      /* reserved now, written for real once masked */
	version_bits(g, ver);
}

/* ---- the data, and the mask -------------------------------------------- */

/* Two columns at a time from the right, up then down, round the timing
 * column, into every module no pattern holds. */
static void place(grid *g, const uint8_t *cw, int n)
{
	int right, vert, j, bit = 0, s = g->size;

	for (right = s - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (vert = 0; vert < s; vert++)
			for (j = 0; j < 2; j++) {
				int x = right - j, up = ((right + 1) & 2) == 0;
				int y = up ? s - 1 - vert : vert;

				if (g->fixed[y * s + x] || bit >= n * 8) continue;
				g->dark[y * s + x] = (cw[bit >> 3] >> (7 - (bit & 7))) & 1;
				bit++;
			}
	}
}

static int mask_bit(int m, int x, int y)
{
	switch (m) {
	case 0:  return (x + y) % 2 == 0;
	case 1:  return y % 2 == 0;
	case 2:  return x % 3 == 0;
	case 3:  return (x + y) % 3 == 0;
	case 4:  return (x / 3 + y / 2) % 2 == 0;
	case 5:  return x * y % 2 + x * y % 3 == 0;
	case 6:  return (x * y % 2 + x * y % 3) % 2 == 0;
	default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

static void apply_mask(grid *g, int m)
{
	int x, y, s = g->size;

	for (y = 0; y < s; y++)
		for (x = 0; x < s; x++)
			if (!g->fixed[y * s + x] && mask_bit(m, x, y))
				g->dark[y * s + x] ^= 1;
}

static int at(const grid *g, int x, int y, int across)
{
	return across ? g->dark[y * g->size + x] : g->dark[x * g->size + y];
}

/* The standard's four penalties: runs of five or more alike, 2x2 blocks
 * alike, the finder's 1:1:3:1:1 with four light beside it, and dark far
 * from half. */
static long penalty(const grid *g)
{
	static const unsigned char F1[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
	static const unsigned char F2[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
	int s = g->size, x, y, across, dark = 0;
	long p = 0;

	for (across = 0; across < 2; across++)
		for (y = 0; y < s; y++) {
			int run = 1;

			for (x = 1; x <= s; x++) {
				if (x < s && at(g, x, y, across) == at(g, x - 1, y, across)) { run++; continue; }
				if (run >= 5) p += 3 + (run - 5);
				run = 1;
			}
			for (x = 0; x + 11 <= s; x++) {
				int k, a = 1, b = 1;

				for (k = 0; k < 11; k++) {
					int d = at(g, x + k, y, across);

					if (d != F1[k]) a = 0;
					if (d != F2[k]) b = 0;
				}
				p += 40 * (a + b);
			}
		}
	for (y = 0; y + 1 < s; y++)
		for (x = 0; x + 1 < s; x++) {
			int c = g->dark[y * s + x];

			if (c == g->dark[y * s + x + 1] && c == g->dark[(y + 1) * s + x] &&
			    c == g->dark[(y + 1) * s + x + 1])
				p += 3;
		}
	for (x = 0; x < s * s; x++) dark += g->dark[x];
	p += 10L * (labs(dark * 100L / (s * s) - 50) / 5);
	return p;
}

int qr_encode(const char *text, unsigned char *out)
{
	static grid g, best;
	uint8_t data[CODEWORDS_MAX], cw[CODEWORDS_MAX];
	size_t len;
	int ver, cap, bits = 0, n, m, i, pick = 0;
	long lowest = -1;

	if (!text || !out) return 0;
	len = strlen(text);
	for (ver = 1; ver <= QR_VERSION_MAX; ver++)
		if (4 + (ver < 10 ? 8 : 16) + 8 * (long)len <= data_codewords(ver) * 8L) break;
	if (ver > QR_VERSION_MAX) return 0;
	cap = data_codewords(ver);

	/* The bits: byte mode, the count, the bytes, up to four zeros to end,
	 * zeros to the byte, then the two pad bytes taking turns. */
	memset(data, 0, sizeof data);
#define PUT(v, nb) do { int b_; for (b_ = (nb) - 1; b_ >= 0; b_--, bits++) \
	if (((v) >> b_) & 1) data[bits >> 3] |= (uint8_t)(0x80 >> (bits & 7)); } while (0)
	PUT(4, 4);
	PUT((unsigned)len, ver < 10 ? 8 : 16);
	for (i = 0; i < (int)len; i++) PUT((unsigned char)text[i], 8);
#undef PUT
	bits += cap * 8 - bits < 4 ? cap * 8 - bits : 4;
	for (i = (bits + 7) / 8; i < cap; i++) data[i] = (i - (bits + 7) / 8) % 2 ? 0x11 : 0xec;

	n = interleave(ver, data, cw);

	memset(&g, 0, sizeof g);
	g.size = 17 + 4 * ver;
	patterns(&g, ver);
	place(&g, cw, n);
	for (m = 0; m < 8; m++) {
		long p;

		apply_mask(&g, m);
		format_bits(&g, m);
		p = penalty(&g);
		if (lowest < 0 || p < lowest) { lowest = p; pick = m; best = g; }
		apply_mask(&g, m);                 /* undone: the mask is its own inverse */
	}
	memcpy(out, best.dark, (size_t)(best.size * best.size));
	(void)pick;
	return best.size;
}
