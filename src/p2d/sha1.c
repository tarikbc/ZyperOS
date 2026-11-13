/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/sha1.c: plain SHA-1 (FIPS 180-4). The WebSocket handshake needs it
 * and nothing else here does, so it stays small instead of pulling in a
 * crypto library. Not used for anything that needs to be secure. */
#include <string.h>

#include "sha1.h"

static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }

static void block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
		       (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (int i = 0; i < 80; i++) {
		uint32_t f, k, t;

		if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5a827999u; }
		else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ed9eba1u; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
		else             { f = b ^ c ^ d;                   k = 0xca62c1d6u; }
		t = rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const void *data, size_t n, uint8_t out[20])
{
	uint32_t h[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
	const uint8_t *p = data;
	uint8_t tail[128];
	size_t rest = n % 64, tl;
	uint64_t bits = (uint64_t)n * 8;

	for (size_t i = 0; i + 64 <= n; i += 64) block(h, p + i);
	/* The last bytes, a 0x80, zeros, then the length in bits: one or two blocks. */
	memcpy(tail, p + (n - rest), rest);
	tail[rest] = 0x80;
	tl = rest < 56 ? 64 : 128;
	memset(tail + rest + 1, 0, tl - rest - 1);
	for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
	block(h, tail);
	if (tl == 128) block(h, tail + 64);
	for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}
