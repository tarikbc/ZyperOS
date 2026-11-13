/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/codec.c: every frame survives the round trip, the flags say what the
 * page needs, and the encode time on this machine. Writes the fixtures the
 * page's decoder test reads (build-native/p2codec-fixture.bin and .rgba). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/p2d/codec.h"

#define W 256
#define H 240
#define NFRAMES 100
#define NCOLORS 40
#define CAP 70000

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t color(unsigned i)
{
	return ((i * 37u + 20u) & 0xFF) << 16 | ((i * 91u + 5u) & 0xFF) << 8 | ((i * 53u + 11u) & 0xFF);
}

/* Frame f (1..): a background of 39 colors in tiles, a 24x24 box of the 40th
 * moving two pixels a frame. Deterministic. */
static void make_frame(uint32_t *px, unsigned f)
{
	unsigned bx = (f * 2) % (W - 24), by = 40 + (f % 7) * 4;
	for (unsigned y = 0; y < H; y++)
		for (unsigned x = 0; x < W; x++) {
			unsigned c = ((x / 8) + (y / 8) * 3) % (NCOLORS - 1);
			if (x >= bx && x < bx + 24 && y >= by && y < by + 24) c = NCOLORS - 1;
			px[y * W + x] = color(c);
		}
}

static void to_rgba(const uint32_t *px, uint8_t *o, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		o[i * 4] = px[i] >> 16; o[i * 4 + 1] = px[i] >> 8; o[i * 4 + 2] = px[i]; o[i * 4 + 3] = 255;
	}
}

static uint32_t src[W * H], dst[W * H];
static uint8_t msg[NFRAMES][CAP];
static long mlen[NFRAMES];

static void colors_are_distinct(void)
{
	for (unsigned i = 0; i < NCOLORS; i++)
		for (unsigned j = i + 1; j < NCOLORS; j++) CHECK(color(i) != color(j));
}

static void round_trip_100_frames(const char *dir)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	char path[512];
	snprintf(path, sizeof path, "%s/p2codec-fixture.bin", dir);
	FILE *fb = fopen(path, "wb");
	snprintf(path, sizeof path, "%s/p2codec-fixture.rgba", dir);
	FILE *fr = fopen(path, "wb");
	CHECK(fb && fr);
	static uint8_t rgba[W * H * 4];
	for (unsigned f = 1; f <= NFRAMES; f++) {
		make_frame(src, f);
		long n = p2c_encode(e, (const uint8_t *)src, W, H, f, msg[f - 1], CAP);
		CHECK(n > 0);
		if (n <= 0) break;
		mlen[f - 1] = n;
		unsigned w = 0, h = 0;
		memset(dst, 0, sizeof dst);
		long r = p2c_decode(d, msg[f - 1], (size_t)n, (uint8_t *)dst, &w, &h);
		CHECK(r == (long)f);
		CHECK(w == W && h == H);
		CHECK(memcmp(src, dst, sizeof src) == 0);
		if (fb && fr) {
			uint8_t le[4] = { n, n >> 8, n >> 16, n >> 24 };
			fwrite(le, 1, 4, fb);
			fwrite(msg[f - 1], 1, (size_t)n, fb);
			to_rgba(src, rgba, W * H);
			fwrite(rgba, 1, sizeof rgba, fr);
		}
	}
	if (fb) fclose(fb);
	if (fr) fclose(fr);
	if (getenv("P2CODEC_SIZES"))
		printf("p2codec: first key %ld bytes, delta %ld bytes, flags %d %d\n",
		       mlen[0], mlen[50], msg[0][1], msg[50][1]);
	p2c_dec_free(d);
	p2c_free(e);
}

static void first_is_key_with_palette(void)
{
	CHECK(mlen[0] > 0 && msg[0][0] == 'F' && msg[0][1] == 3);
	CHECK(msg[1][1] == 0);
}

static void still_frame_is_small(void)
{
	p2c_enc *e = p2c_new();
	static uint8_t a[CAP], b[CAP];
	make_frame(src, 1);
	long n1 = p2c_encode(e, (const uint8_t *)src, W, H, 1, a, CAP);
	long n2 = p2c_encode(e, (const uint8_t *)src, W, H, 2, b, CAP);
	CHECK(n1 > 0 && n2 > 0 && n2 < 200);
	CHECK(b[1] == 0);
	if (getenv("P2CODEC_SIZES")) printf("p2codec: still frame %ld bytes\n", n2);
	p2c_free(e);
}

static void new_color_sends_palette(void)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	static uint8_t a[CAP];
	unsigned w, h;
	make_frame(src, 1);
	long n1 = p2c_encode(e, (const uint8_t *)src, W, H, 1, a, CAP);
	CHECK(n1 > 0 && p2c_decode(d, a, (size_t)n1, (uint8_t *)dst, &w, &h) == 1);
	long n2 = p2c_encode(e, (const uint8_t *)src, W, H, 2, a, CAP);
	CHECK(n2 > 0 && p2c_decode(d, a, (size_t)n2, (uint8_t *)dst, &w, &h) == 2);
	src[100 * W + 100] = color(NCOLORS);
	long n = p2c_encode(e, (const uint8_t *)src, W, H, 3, a, CAP);
	CHECK(n > 0 && (a[1] & 3) == 3);
	memset(dst, 0, sizeof dst);
	CHECK(p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == 3);
	CHECK(memcmp(src, dst, sizeof src) == 0);
	p2c_dec_free(d);
	p2c_free(e);
}

static void too_many_colors(void)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	static uint8_t a[CAP];
	unsigned w, h;
	make_frame(src, 1);
	long n = p2c_encode(e, (const uint8_t *)src, W, H, 1, a, CAP);
	CHECK(n > 0 && p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == 1);
	for (unsigned i = 0; i < W * H; i++) src[i] = 0x100000u + (i % 300);
	CHECK(p2c_encode(e, (const uint8_t *)src, W, H, 2, a, CAP) == -1);
	/* A cap too small is a failure too; the next frame still decodes. */
	make_frame(src, 3);
	CHECK(p2c_encode(e, (const uint8_t *)src, W, H, 3, a, 20) == -1);
	make_frame(src, 4);
	n = p2c_encode(e, (const uint8_t *)src, W, H, 4, a, CAP);
	CHECK(n > 0);
	memset(dst, 0, sizeof dst);
	CHECK(p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == 4);
	CHECK(memcmp(src, dst, sizeof src) == 0);
	p2c_dec_free(d);
	p2c_free(e);
}

static void full_palette_history_restarts(void)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	static uint8_t a[CAP];
	unsigned w, h;
	for (unsigned f = 1; f <= 2; f++) {
		for (unsigned i = 0; i < W * H; i++) src[i] = 0x200000u + (f - 1) * 0x1000u + (i % 200);
		long n = p2c_encode(e, (const uint8_t *)src, W, H, f, a, CAP);
		CHECK(n > 0 && (a[1] & 3) == 3);
		memset(dst, 0, sizeof dst);
		CHECK(n > 0 && p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == (long)f);
		CHECK(memcmp(src, dst, sizeof src) == 0);
	}
	p2c_dec_free(d);
	p2c_free(e);
}

static void size_change_is_key(void)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	static uint8_t a[CAP];
	unsigned w, h;
	make_frame(src, 1);
	long n = p2c_encode(e, (const uint8_t *)src, W, H, 1, a, CAP);
	CHECK(n > 0 && p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == 1);
	n = p2c_encode(e, (const uint8_t *)src, W, 224, 2, a, CAP);
	CHECK(n > 0 && (a[1] & 1));
	memset(dst, 0, sizeof dst);
	CHECK(p2c_decode(d, a, (size_t)n, (uint8_t *)dst, &w, &h) == 2);
	CHECK(w == W && h == 224 && memcmp(src, dst, (size_t)W * 224 * 4) == 0);
	p2c_dec_free(d);
	p2c_free(e);
}

static void periodic_key_and_bad_messages(void)
{
	p2c_enc *e = p2c_new();
	p2c_dec *d = p2c_dec_new();
	static uint8_t a[CAP];
	unsigned w, h;
	make_frame(src, 1);
	for (unsigned f = 0; f < 125; f++) {
		long n = p2c_encode(e, (const uint8_t *)src, W, H, f, a, CAP);
		CHECK(n > 0);
		CHECK(((a[1] & 1) != 0) == (f == 0 || f == 120));
	}
	/* A delta before any key, a short header, a bad 'F', a wrong length. */
	CHECK(p2c_decode(d, a, 5, (uint8_t *)dst, &w, &h) == -1);
	CHECK(p2c_decode(d, msg[1], (size_t)mlen[1], (uint8_t *)dst, &w, &h) == -1);
	CHECK(p2c_decode(d, msg[0], 4, (uint8_t *)dst, &w, &h) == -1);
	CHECK(p2c_decode(d, msg[0], (size_t)mlen[0] - 3, (uint8_t *)dst, &w, &h) == -1);
	uint8_t bad[CAP];
	memcpy(bad, msg[0], (size_t)mlen[0]);
	bad[0] = 'X';
	CHECK(p2c_decode(d, bad, (size_t)mlen[0], (uint8_t *)dst, &w, &h) == -1);
	p2c_dec_free(d);
	p2c_free(e);
}

static void encode_time(void)
{
	p2c_enc *e = p2c_new();
	static uint8_t a[CAP];
	static uint32_t frames[NFRAMES][W * H];
	for (unsigned f = 0; f < NFRAMES; f++) make_frame(frames[f], f + 1);
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (unsigned f = 0; f < NFRAMES; f++)
		CHECK(p2c_encode(e, (const uint8_t *)frames[f], W, H, f + 1, a, CAP) > 0);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / NFRAMES;
	printf("p2codec: %.2f ms a frame\n", ms);
	p2c_free(e);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "build-native";
	colors_are_distinct();
	round_trip_100_frames(dir);
	first_is_key_with_palette();
	still_frame_is_small();
	new_color_sends_palette();
	too_many_colors();
	full_palette_history_restarts();
	size_change_is_key();
	periodic_key_and_bad_messages();
	encode_time();
	if (fails) { printf("p2codec: %d FAILED\n", fails); return 1; }
	printf("p2codec: ok\n");
	return 0;
}
