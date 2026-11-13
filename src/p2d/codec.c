/* SPDX-License-Identifier: GPL-3.0-only */
/* The frame codec. See codec.h for the message. The encoder runs 60 times a
 * second next to the game, so a pixel maps to its palette index through the
 * last color seen and a small hash table, and nothing is allocated per frame
 * once the size is known. */
#include "codec.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define KEY_EVERY 120
#define HASH_BITS 10
#define HASH_SIZE (1u << HASH_BITS)
#define MAX_PIXELS (4096u * 4096u)

struct p2c_enc {
	z_stream z;
	uint8_t *cur, *prev, *diff;
	size_t cap;			/* bytes in each of the three buffers */
	unsigned w, h;			/* size of prev; 0 when none */
	uint32_t pal[256];
	unsigned npal;
	uint32_t hkey[HASH_SIZE];
	int16_t hidx[HASH_SIZE];	/* -1 empty */
	unsigned since_key;
	int need_key;
};

static void clear_palette(p2c_enc *e)
{
	e->npal = 0;
	for (unsigned i = 0; i < HASH_SIZE; i++) e->hidx[i] = -1;
}

void p2c_reset(p2c_enc *e)
{
	clear_palette(e);
	e->w = e->h = 0;
	e->need_key = 1;
}

p2c_enc *p2c_new(void)
{
	p2c_enc *e = calloc(1, sizeof *e);
	if (!e) return NULL;
	if (deflateInit2(&e->z, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		free(e);
		return NULL;
	}
	p2c_reset(e);
	return e;
}

void p2c_free(p2c_enc *e)
{
	if (!e) return;
	deflateEnd(&e->z);
	free(e->cur);
	free(e->prev);
	free(e->diff);
	free(e);
}

/* The index of color c, adding it when new; -1 when the palette is full. */
static int lookup(p2c_enc *e, uint32_t c)
{
	unsigned s = (c * 2654435761u) >> (32 - HASH_BITS);
	while (e->hidx[s] >= 0) {
		if (e->hkey[s] == c) return e->hidx[s];
		s = (s + 1) & (HASH_SIZE - 1);
	}
	if (e->npal >= 256) return -1;
	e->hkey[s] = c;
	e->hidx[s] = (int16_t)e->npal;
	e->pal[e->npal] = c;
	return (int)e->npal++;
}

/* Map a frame to indices in e->cur; -1 when the palette overflows. */
static int map_frame(p2c_enc *e, const uint8_t *xrgb, size_t n)
{
	uint32_t last = 0;
	int lastidx = -1;
	for (size_t i = 0; i < n; i++) {
		uint32_t c;
		memcpy(&c, xrgb + i * 4, 4);
		c &= 0xFFFFFF;
		if (lastidx < 0 || c != last) {
			lastidx = lookup(e, c);
			if (lastidx < 0) return -1;
			last = c;
		}
		e->cur[i] = (uint8_t)lastidx;
	}
	return 0;
}

long p2c_encode(p2c_enc *e, const uint8_t *xrgb, unsigned w, unsigned h,
                uint32_t frame, uint8_t *out, size_t cap)
{
	size_t n = (size_t)w * h;
	if (!e || w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF || n > MAX_PIXELS || cap < P2C_HEAD) return -1;

	if (n > e->cap) {
		uint8_t *b[3];
		for (int i = 0; i < 3; i++) b[i] = malloc(n);
		if (!b[0] || !b[1] || !b[2]) {
			for (int i = 0; i < 3; i++) free(b[i]);
			p2c_reset(e);
			return -1;
		}
		free(e->cur); free(e->prev); free(e->diff);
		e->cur = b[0]; e->prev = b[1]; e->diff = b[2];
		e->cap = n;
		p2c_reset(e);	/* prev is gone */
	}

	unsigned before = e->npal;
	if (map_frame(e, xrgb, n) < 0) {
		/* Old colors may fill the palette: start a fresh one, once. */
		p2c_reset(e);
		before = 0;
		if (map_frame(e, xrgb, n) < 0) {
			p2c_reset(e);
			return -1;
		}
	}
	int key = e->need_key || e->w != w || e->h != h || e->since_key >= KEY_EVERY || e->npal != before;
	int pal = e->need_key || e->npal != before;

	const uint8_t *src = e->cur;
	if (!key) {
		/* A byte-wise XOR loop; the compiler vectorizes it. */
		for (size_t i = 0; i < n; i++) e->diff[i] = e->cur[i] ^ e->prev[i];
		src = e->diff;
	}

	size_t o = P2C_HEAD;
	size_t palbytes = pal ? 2 + (size_t)e->npal * 3 : 0;
	if (cap < o + palbytes) {
		p2c_reset(e);
		return -1;
	}
	out[0] = 'F';
	out[1] = (uint8_t)((key ? P2C_KEY : 0) | (pal ? P2C_PAL : 0));
	out[2] = (uint8_t)w; out[3] = (uint8_t)(w >> 8);
	out[4] = (uint8_t)h; out[5] = (uint8_t)(h >> 8);
	out[6] = (uint8_t)frame; out[7] = (uint8_t)(frame >> 8);
	out[8] = (uint8_t)(frame >> 16); out[9] = (uint8_t)(frame >> 24);
	if (pal) {
		out[o++] = (uint8_t)e->npal;
		out[o++] = (uint8_t)(e->npal >> 8);
		for (unsigned i = 0; i < e->npal; i++) {
			out[o++] = (uint8_t)(e->pal[i] >> 16);
			out[o++] = (uint8_t)(e->pal[i] >> 8);
			out[o++] = (uint8_t)e->pal[i];
		}
	}

	/* A delta is mostly zeros, which RLE finds and packs far smaller (a still
	 * frame is 76 bytes against 286); a key is shapes, where it does worse. */
	deflateReset(&e->z);
	deflateParams(&e->z, 1, key ? Z_DEFAULT_STRATEGY : Z_RLE);
	e->z.next_in = (Bytef *)src;
	e->z.avail_in = (uInt)n;
	e->z.next_out = out + o;
	e->z.avail_out = (uInt)(cap - o);
	if (deflate(&e->z, Z_FINISH) != Z_STREAM_END) {
		p2c_reset(e);
		return -1;
	}

	uint8_t *t = e->prev; e->prev = e->cur; e->cur = t;
	e->w = w;
	e->h = h;
	e->need_key = 0;
	e->since_key = key ? 1 : e->since_key + 1;
	return (long)(cap - o - e->z.avail_out + o);
}

/* The decoder: the test's mirror of the page's. */

struct p2c_dec {
	z_stream z;
	uint8_t *prev, *tmp;
	size_t cap;
	unsigned w, h;
	uint32_t pal[256];
	int have_key;
};

p2c_dec *p2c_dec_new(void)
{
	p2c_dec *d = calloc(1, sizeof *d);
	if (!d) return NULL;
	if (inflateInit2(&d->z, -15) != Z_OK) {
		free(d);
		return NULL;
	}
	return d;
}

void p2c_dec_free(p2c_dec *d)
{
	if (!d) return;
	inflateEnd(&d->z);
	free(d->prev);
	free(d->tmp);
	free(d);
}

long p2c_decode(p2c_dec *d, const uint8_t *msg, size_t n, uint8_t *xrgb_out, unsigned *w, unsigned *h)
{
	if (n < P2C_HEAD || msg[0] != 'F') return -1;
	unsigned flags = msg[1];
	unsigned mw = msg[2] | msg[3] << 8, mh = msg[4] | msg[5] << 8;
	uint32_t frame = (uint32_t)msg[6] | (uint32_t)msg[7] << 8 | (uint32_t)msg[8] << 16 | (uint32_t)msg[9] << 24;
	size_t px = (size_t)mw * mh;
	if (px == 0 || px > MAX_PIXELS) return -1;
	int key = flags & P2C_KEY;
	if (!key && (!d->have_key || d->w != mw || d->h != mh)) return -1;

	size_t o = P2C_HEAD;
	uint32_t pal[256];
	memcpy(pal, d->pal, sizeof pal);
	if (flags & P2C_PAL) {
		if (n < o + 2) return -1;
		unsigned cnt = msg[o] | msg[o + 1] << 8;
		o += 2;
		if (cnt < 1 || cnt > 256 || n < o + (size_t)cnt * 3) return -1;
		for (unsigned i = 0; i < cnt; i++, o += 3)
			pal[i] = (uint32_t)msg[o] << 16 | (uint32_t)msg[o + 1] << 8 | msg[o + 2];
	}

	if (px > d->cap) {
		uint8_t *a = realloc(d->prev, px), *b;
		if (!a) return -1;
		d->prev = a;
		b = realloc(d->tmp, px);
		if (!b) return -1;
		d->tmp = b;
		d->cap = px;
	}
	inflateReset(&d->z);
	d->z.next_in = (Bytef *)(msg + o);
	d->z.avail_in = (uInt)(n - o);
	d->z.next_out = d->tmp;
	d->z.avail_out = (uInt)px;
	if (inflate(&d->z, Z_FINISH) != Z_STREAM_END || d->z.total_out != px) return -1;

	if (key) memcpy(d->prev, d->tmp, px);
	else for (size_t i = 0; i < px; i++) d->prev[i] ^= d->tmp[i];
	memcpy(d->pal, pal, sizeof pal);
	d->w = mw;
	d->h = mh;
	d->have_key = 1;
	for (size_t i = 0; i < px; i++) {
		uint32_t c = d->pal[d->prev[i]];
		memcpy(xrgb_out + i * 4, &c, 4);
	}
	*w = mw;
	*h = mh;
	return (long)frame;
}
