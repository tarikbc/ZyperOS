/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2share.h: Diatom's frames to p2d, and p2d's pad back. Runs the writer
 * and the reader against each other on one malloc'd block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/p2share.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void *fresh(void)
{
	void *b = calloc(1, P2S_SIZE);
	p2s_head *h = b;

	h->magic = P2S_MAGIC;
	h->version = P2S_VERSION;
	return b;
}

static void write_then_read(void)
{
	void *b = fresh();
	static uint32_t src[256 * 240], got[256 * 240];
	unsigned w = 0, h = 0;
	uint32_t frame = 0;

	for (int i = 0; i < 256 * 240; i++) src[i] = 0x00010203u * (uint32_t)(i & 0x3f) + (uint32_t)i;
	p2s_write(b, src, 256, 240, 256 * 4, 1);
	CHECK(p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	CHECK(w == 256 && h == 240 && frame == 1);
	CHECK(memcmp(src, got, sizeof src) == 0);
	free(b);
}

static void pitch_wider_than_row(void)
{
	void *b = fresh();
	static uint32_t src[8 * 300], got[4 * 2];
	unsigned w, h;
	uint32_t frame;

	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 8; x++) src[y * 8 + x] = (uint32_t)(y * 100 + x);
	p2s_write(b, src, 4, 2, 8 * 4, 1);
	CHECK(p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	CHECK(got[0] == 0 && got[3] == 3 && got[4] == 100 && got[7] == 103);
	free(b);
}

static void convert(int fmt, uint16_t v, uint32_t want)
{
	void *b = fresh();
	uint16_t px[2 * 2] = { v, v, v, v };
	uint32_t got[4];
	unsigned w, h;
	uint32_t frame;

	p2s_write(b, px, 2, 2, 2 * 2, fmt);
	CHECK(p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	CHECK(got[0] == want && got[3] == want);
	if (got[0] != want) printf("  fmt %d 0x%04x -> 0x%08x, want 0x%08x\n", fmt, v, got[0], want);
	free(b);
}

static void rgb565_converts(void)
{
	convert(2, 0xF800, 0x00FF0000u);
	convert(2, 0x07E0, 0x0000FF00u);
	convert(2, 0x001F, 0x000000FFu);
	convert(2, 0xFFFF, 0x00FFFFFFu);
	convert(0, 0x7C00, 0x00FF0000u);
	convert(0, 0x03E0, 0x0000FF00u);
	convert(0, 0x001F, 0x000000FFu);
	convert(0, 0x7FFF, 0x00FFFFFFu);
}

static void torn_slot_skipped(void)
{
	void *b = fresh();
	p2s_head *hd = b;
	static uint32_t px[16 * 16], got[16 * 16];
	unsigned w, h;
	uint32_t frame;

	p2s_write(b, px, 16, 16, 16 * 4, 1);
	CHECK(p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	hd->slot[hd->latest].seq |= 1u;
	CHECK(!p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	hd->slot[hd->latest].seq++;
	CHECK(p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	free(b);
}

static void not_ready_or_small_buffer(void)
{
	void *b = calloc(1, P2S_SIZE);
	static uint32_t px[16 * 16], got[16 * 16];
	unsigned w, h;
	uint32_t frame;

	/* no magic yet: Diatom writes it last */
	CHECK(!p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	((p2s_head *)b)->magic = P2S_MAGIC;
	((p2s_head *)b)->version = P2S_VERSION;
	/* no frame yet */
	CHECK(!p2s_read(b, (uint8_t *)got, sizeof got, &w, &h, &frame));
	p2s_write(b, px, 16, 16, 16 * 4, 1);
	CHECK(!p2s_read(b, (uint8_t *)got, 16 * 16 * 4 - 1, &w, &h, &frame));
	CHECK(p2s_read(b, (uint8_t *)got, 16 * 16 * 4, &w, &h, &frame));
	free(b);
}

static void three_slots_rotate(void)
{
	void *b = fresh();
	p2s_head *hd = b;
	static uint32_t px[16 * 16];
	const uint32_t want[4] = { 0, 1, 2, 0 };

	hd->latest = 2;	/* so the first write lands in slot 0 */
	for (int i = 0; i < 4; i++) {
		p2s_write(b, px, 16, 16, 16 * 4, 1);
		CHECK(hd->latest == want[i]);
	}
	CHECK(hd->frame == 4);
	CHECK(hd->slot[0].frame == 4 && hd->slot[1].frame == 2);
	free(b);
}

static void too_big_refused(void)
{
	void *b = fresh();
	p2s_head *hd = b;
	static uint32_t px[600 * 500];

	p2s_write(b, px, P2S_MAX_W + 1, 240, (P2S_MAX_W + 1) * 4, 1);
	CHECK(hd->frame == 0);
	p2s_write(b, px, 256, P2S_MAX_H + 1, 256 * 4, 1);
	CHECK(hd->frame == 0);
	p2s_write(b, px, 0, 240, 0, 1);
	p2s_write(b, px, 256, 0, 256 * 4, 1);
	CHECK(hd->frame == 0);
	p2s_write(b, px, P2S_MAX_W, P2S_MAX_H, P2S_MAX_W * 4, 1);
	CHECK(hd->frame == 1);
	free(b);
}

static void layout(void)
{
	CHECK(sizeof(p2s_head) <= P2S_HEAD);
	CHECK(P2S_SLOT_BYTES == 512u * 480u * 4u);
}

int main(void)
{
	write_then_read();
	pitch_wider_than_row();
	rgb565_converts();
	torn_slot_skipped();
	not_ready_or_small_buffer();
	three_slots_rotate();
	too_big_refused();
	layout();
	if (fails) return 1;
	printf("p2share: ok\n");
	return 0;
}
