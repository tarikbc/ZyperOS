/* SPDX-License-Identifier: GPL-3.0-only */
/* See flacdec.h. A FLAC frame decoder, written from RFC 9639, for one job:
 * the audio hunks of a CHD disc image, which are bare FLAC frames. A FLAC
 * library would want a whole stream, so a STREAMINFO block would have to be
 * faked up in front of every hunk; decoding the frames directly is less code
 * than that, and nothing more has to be linked in for it.
 *
 * A disc image is a file someone downloaded, so nothing a frame says is used
 * before it is checked: both CRCs, every reserved code, every size against
 * the buffer it indexes, and every decoded sample against the bit depth it
 * was coded at. A frame that lies ends in false, never in a read or a write
 * past the end of a buffer.
 */
#include <stdlib.h>
#include <string.h>

#include "flacdec.h"

/* A frame's own block size field reaches 65536, but no frame may be larger
 * than STREAMINFO's largest block, and that field stops at 65535. */
#define MAX_BLOCK 65535u

/* --------------------------------------------------------------- CRCs ---- */

/* The frame header ends in a CRC-8 (polynomial 0x07) and the frame in a
 * CRC-16 (0x8005), both MSB first with no reflection and no final xor. The
 * tables are built per call, on the stack, so there is no state to share
 * between threads: 2048 steps, against the thousands of bytes in a hunk. */
typedef struct {
	uint8_t c8[256];
	uint16_t c16[256];
} crcs;

static void crc_init(crcs *t)
{
	unsigned i, k;

	for (i = 0; i < 256; i++) {
		unsigned a = i, b = i << 8;

		for (k = 0; k < 8; k++) {
			a = (a & 0x80) ? (a << 1) ^ 0x07 : a << 1;
			b = (b & 0x8000) ? (b << 1) ^ 0x8005 : b << 1;
		}
		t->c8[i] = (uint8_t)a;
		t->c16[i] = (uint16_t)b;
	}
}

static unsigned crc8(const crcs *t, const uint8_t *p, size_t n)
{
	unsigned c = 0;

	while (n--)
		c = t->c8[c ^ *p++];
	return c;
}

static unsigned crc16(const crcs *t, const uint8_t *p, size_t n)
{
	unsigned c = 0;

	while (n--)
		c = ((c << 8) & 0xffff) ^ t->c16[(c >> 8) ^ *p++];
	return c;
}

/* --------------------------------------------------------------- bits ---- */

/* Subframes are not byte aligned, so they are read through a 64-bit window.
 * The next unread bit is the window's top bit and everything below the `n`
 * real bits is zero; the unary reader relies on that to find a 1 by counting
 * leading zeros. Running out sets `bad` and reads zeros from then on, so a
 * caller checks once after a run of reads instead of after every one. */
typedef struct {
	const uint8_t *p;
	size_t len, pos;	/* bytes in p; the next one to load */
	uint64_t win;
	unsigned n;
	bool bad;
} bits;

static void br_fill(bits *b)
{
	while (b->n <= 56 && b->pos < b->len) {
		b->win |= (uint64_t)b->p[b->pos++] << (56 - b->n);
		b->n += 8;
	}
}

/* k bits, 0 to 32, as an unsigned number. */
static uint32_t br_get(bits *b, unsigned k)
{
	uint32_t v;

	if (k == 0)
		return 0;
	if (b->n < k) {
		br_fill(b);
		if (b->n < k) {
			b->bad = true;
			b->win = 0;
			b->n = 0;
			return 0;
		}
	}
	v = (uint32_t)(b->win >> (64 - k));
	b->win <<= k;
	b->n -= k;
	return v;
}

/* k bits, 0 to 31, as a two's complement number. */
static int32_t br_sget(bits *b, unsigned k)
{
	int64_t v = br_get(b, k);

	if (k && (v >> (k - 1)) & 1)
		v -= (int64_t)1 << k;
	return (int32_t)v;
}

/* The number of 0 bits before the next 1, which is consumed too. */
static uint64_t br_unary(bits *b)
{
	uint64_t z = 0;
	unsigned c;

	while (b->win == 0) {
		z += b->n;
		b->n = 0;
		br_fill(b);
		if (b->n == 0) {
			b->bad = true;
			return 0;
		}
	}
	c = (unsigned)__builtin_clzll(b->win);
	b->win = (b->win << c) << 1;
	b->n -= c + 1;
	return z + c;
}

/* --------------------------------------------------------- the header ---- */

typedef struct {
	unsigned blocksize;
	unsigned assign;	/* 0-7 independent channels, 8-10 stereo pairs */
	unsigned channels;
	size_t len;		/* bytes, CRC-8 included */
} header;

/* Parses the header at p, which has len bytes left in it. The sample rate is
 * read past and not kept: a hunk's rate is the disc's, whatever a frame says,
 * and the samples come out the same either way. */
static bool frame_header(const crcs *t, const uint8_t *p, size_t len, header *h)
{
	unsigned bs, sr, ch, ss, more, bsx, srx, k;
	size_t i = 5;

	if (len < 6 || p[0] != 0xff || (p[1] & 0xfe) != 0xf8)
		return false;
	bs = p[2] >> 4;
	sr = p[2] & 15;
	ch = p[3] >> 4;
	ss = (p[3] >> 1) & 7;
	/* Block size 0, sample rate 15, channel codes 11-15 and the bit after the
	 * sample size are reserved. A sample size of 0 means "as in STREAMINFO",
	 * which for a CHD hunk is 16 bits; 4 says 16 outright. */
	if (bs == 0 || sr == 15 || ch > 10 || (p[3] & 1) || (ss != 0 && ss != 4))
		return false;

	/* The frame number (fixed block size) or first sample number (variable),
	 * coded the way UTF-8 codes a character, stretched to 7 bytes for the 36
	 * bits a sample number can need. Only the length matters here. */
	if (p[4] < 0x80)
		more = 0;
	else if (p[4] < 0xc0 || p[4] == 0xff)
		return false;
	else
		more = (unsigned)__builtin_clz(~((unsigned)p[4] << 24)) - 1;
	if (more == 6 && !(p[1] & 1))
		return false;	/* a frame number has 31 bits, 6 bytes at most */

	bsx = bs == 6 ? 1 : bs == 7 ? 2 : 0;
	srx = sr == 12 ? 1 : (sr == 13 || sr == 14) ? 2 : 0;
	if (len < i + more + bsx + srx + 1)
		return false;
	for (k = 0; k < more; k++)
		if ((p[i++] & 0xc0) != 0x80)
			return false;

	if (bs == 1)
		h->blocksize = 192;
	else if (bs <= 5)
		h->blocksize = 576u << (bs - 2);
	else if (bs == 6)
		h->blocksize = p[i] + 1u;
	else if (bs == 7)
		h->blocksize = ((unsigned)p[i] << 8 | p[i + 1]) + 1u;
	else
		h->blocksize = 256u << (bs - 8);
	i += bsx + srx;
	if (h->blocksize > MAX_BLOCK)
		return false;

	if (crc8(t, p, i) != p[i])
		return false;
	h->assign = ch;
	h->channels = ch < 8 ? ch + 1 : 2;
	h->len = i + 1;
	return true;
}

/* ---------------------------------------------------------- subframes ---- */

/* The fixed predictors of orders 0-4 are linear predictors too, with these
 * coefficients and no shift, so one loop runs both kinds of subframe. */
static const int32_t fixed_coef[5][4] = {
	{ 0 }, { 1 }, { 2, -1 }, { 3, -3, 1 }, { 4, -6, 4, -1 }
};

/* The residual, into res[order..n): partitions of Rice codes, or of raw
 * numbers where a partition is escaped. */
static bool residual(bits *b, int32_t *res, unsigned n, unsigned order)
{
	unsigned method = br_get(b, 2), porder = br_get(b, 4), part, pbits, esc;
	unsigned i = order, p;

	if (b->bad || method > 1)
		return false;
	pbits = method ? 5 : 4;
	esc = (1u << pbits) - 1;
	/* The partitions split the block evenly, and the first one, which also
	 * carries the warm-up samples, must still have room for them. */
	part = n >> porder;
	if ((n & ((1u << porder) - 1)) || part < order)
		return false;

	for (p = 0; p < 1u << porder; p++) {
		unsigned end = i + part - (p ? 0 : order), k = br_get(b, pbits);

		if (b->bad)
			return false;
		if (k == esc) {
			unsigned w = br_get(b, 5);

			for (; i < end; i++)
				res[i] = br_sget(b, w);
		} else {
			/* A Rice code is a unary quotient, then k bits of remainder;
			 * the number is folded so that 0, -1, 1, -2 count 0, 1, 2, 3.
			 * One that cannot unfold into 32 bits is no encoder's. */
			for (; i < end; i++) {
				uint64_t q = br_unary(b);
				uint32_t r = br_get(b, k), u;

				if (b->bad || q > (UINT32_MAX >> k))
					return false;
				u = (uint32_t)(q << k) | r;
				res[i] = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
			}
		}
		if (b->bad)
			return false;
	}
	return true;
}

/* Adds each predicted sample to the residual already in s[order..n). A
 * prediction is taken to 64 bits before the shift, as RFC 9639 asks: 32
 * coefficients of 15 bits on 17-bit samples need 37. The shift is meant as a
 * floor, which is what >> on a negative int64_t does with GCC and Clang. A
 * sample outside the depth it was coded at means a broken frame, and stopping
 * there keeps every later sum inside 64 bits. */
static bool predict(int32_t *s, unsigned n, const int32_t *c, unsigned order,
                    unsigned shift, unsigned depth)
{
	const int64_t hi = ((int64_t)1 << (depth - 1)) - 1, lo = -hi - 1;
	unsigned i, j;

	for (i = order; i < n; i++) {
		int64_t sum = 0, v;

		for (j = 0; j < order; j++)
			sum += (int64_t)c[j] * s[i - 1 - j];
		v = s[i] + (sum >> shift);
		if (v < lo || v > hi)
			return false;
		s[i] = (int32_t)v;
	}
	return true;
}

/* One channel's n samples into s, coded at bps bits: the frame's 16, or 17
 * for the side channel of a stereo pair, which holds a difference. */
static bool subframe(bits *b, int32_t *s, unsigned n, unsigned bps)
{
	unsigned head = br_get(b, 8), type, wasted = 0, depth, order, i;

	if (b->bad || (head & 0x80))
		return false;
	type = (head >> 1) & 0x3f;
	/* Wasted bits: low bits that are 0 in every sample of the subframe, left
	 * out of the coding and put back at the end. */
	if (head & 1) {
		uint64_t z = br_unary(b);

		if (b->bad || z + 1 >= bps)
			return false;
		wasted = (unsigned)z + 1;
	}
	depth = bps - wasted;

	if (type == 0) {		/* CONSTANT */
		int32_t v = br_sget(b, depth);

		for (i = 0; i < n; i++)
			s[i] = v;
	} else if (type == 1) {		/* VERBATIM */
		for (i = 0; i < n; i++)
			s[i] = br_sget(b, depth);
	} else if (type >= 8 && type <= 12) {	/* FIXED, orders 0-4 */
		order = type - 8;
		if (order > n)
			return false;
		for (i = 0; i < order; i++)
			s[i] = br_sget(b, depth);
		if (b->bad || !residual(b, s, n, order) ||
		    !predict(s, n, fixed_coef[order], order, 0, depth))
			return false;
	} else if (type >= 32) {	/* LPC, orders 1-32 */
		int32_t coef[32];
		unsigned prec;
		int32_t shift;

		order = type - 31;
		if (order > n)
			return false;
		for (i = 0; i < order; i++)
			s[i] = br_sget(b, depth);
		/* Coefficient precision 15 (16 bits) is reserved, and so is a
		 * negative shift: RFC 9639 leaves it undefined. */
		prec = br_get(b, 4);
		shift = br_sget(b, 5);
		if (b->bad || prec == 15 || shift < 0)
			return false;
		for (i = 0; i < order; i++)
			coef[i] = br_sget(b, prec + 1);
		if (b->bad || !residual(b, s, n, order) ||
		    !predict(s, n, coef, order, (unsigned)shift, depth))
			return false;
	} else {
		return false;		/* reserved subframe types */
	}
	if (b->bad)
		return false;
	if (wasted)
		for (i = 0; i < n; i++)
			s[i] *= (int32_t)1 << wasted;
	return true;
}

/* ------------------------------------------------------------- frames ---- */

/* Which channel of a pair, if either, is a side (left minus right) channel. */
static bool is_side(unsigned assign, unsigned ch)
{
	return (assign == 8 && ch == 1) || (assign == 9 && ch == 0) ||
	       (assign == 10 && ch == 1);
}

/* Undoes the stereo decorrelation over the first n samples of a and c, the
 * two channels as coded, leaving left in a and right in c. */
static void restore_pair(unsigned assign, int32_t *a, int32_t *c, size_t n)
{
	size_t i;

	if (assign == 8)		/* left, side */
		for (i = 0; i < n; i++)
			c[i] = a[i] - c[i];
	else if (assign == 9)		/* side, right */
		for (i = 0; i < n; i++)
			a[i] += c[i];
	else if (assign == 10)		/* mid, side */
		/* The mid channel was (left + right) >> 1, so it lost a bit; the
		 * side channel's lowest bit is that same bit, since a sum and a
		 * difference of two numbers are both odd or both even. */
		for (i = 0; i < n; i++) {
			int32_t m = a[i] * 2 + (c[i] & 1), d = c[i];

			a[i] = (m + d) >> 1;
			c[i] = (m - d) >> 1;
		}
}

bool flac_decode_s16(const uint8_t *src, size_t srclen, unsigned channels,
                     uint8_t *out, size_t frames, bool big_endian)
{
	crcs t;
	int32_t *buf = NULL;
	size_t cap = 0, off = 0, done = 0;
	bool ok = false;

	if (frames == 0)
		return true;
	if (!src || !out || channels < 1 || channels > 8)
		return false;
	crc_init(&t);

	while (done < frames) {
		header h;
		bits b;
		size_t end, take, i;
		unsigned ch, bs;
		uint8_t *o;

		if (!frame_header(&t, src + off, srclen - off, &h) ||
		    h.channels != channels)
			goto out;
		bs = h.blocksize;
		/* Scratch for the largest block met so far: a hunk's frames
		 * mostly share one size, so this grows once. */
		if (bs > cap) {
			int32_t *nb = realloc(buf, (size_t)bs * channels * sizeof *buf);

			if (!nb)
				goto out;
			buf = nb;
			cap = bs;
		}

		memset(&b, 0, sizeof b);
		b.p = src;
		b.len = srclen;
		b.pos = off + h.len;
		for (ch = 0; ch < channels; ch++)
			if (!subframe(&b, buf + (size_t)ch * bs, bs,
			              16 + is_side(h.assign, ch)))
				goto out;
		/* Zero bits to the next byte, then the CRC-16 of every byte of
		 * the frame before it, the header's included. */
		if (br_get(&b, b.n % 8) != 0 || b.bad)
			goto out;
		end = b.pos - b.n / 8;
		if (br_get(&b, 16) != crc16(&t, src + off, end - off) || b.bad)
			goto out;
		off = end + 2;

		take = frames - done < bs ? frames - done : bs;
		if (h.assign >= 8)
			restore_pair(h.assign, buf, buf + bs, take);
		o = out + done * channels * 2;
		for (i = 0; i < take; i++)
			for (ch = 0; ch < channels; ch++) {
				int32_t v = buf[(size_t)ch * bs + i];
				uint16_t u = (uint16_t)v;

				if (v < -32768 || v > 32767)
					goto out;
				o[big_endian ? 0 : 1] = (uint8_t)(u >> 8);
				o[big_endian ? 1 : 0] = (uint8_t)u;
				o += 2;
			}
		done += take;
	}
	ok = true;
out:
	free(buf);
	return ok;
}
