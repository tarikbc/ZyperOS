/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/ws.c and sha1.c: the WebSocket handshake key, the server frame head
 * and the client frame parser, checked with no socket. */
#include <stdio.h>
#include <string.h>

#include "../src/p2d/sha1.h"
#include "../src/p2d/ws.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void hex(const uint8_t d[20], char out[41])
{
	for (int i = 0; i < 20; i++) sprintf(out + i * 2, "%02x", d[i]);
}

static void sha1_vectors(void)
{
	uint8_t d[20];
	char h[41];

	sha1("abc", 3, d);
	hex(d, h);
	CHECK(strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
	sha1("", 0, d);
	hex(d, h);
	CHECK(strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
}

static void accept_rfc_example(void)
{
	char out[29];

	CHECK(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", out));
	CHECK(strcmp(out, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
	CHECK(!ws_accept_key(NULL, out));
	CHECK(!ws_accept_key("0123456789012345678901234567890123456789012345678901234567890123456789", out));
}

static void head_sizes(void)
{
	uint8_t h[10];

	CHECK(ws_frame_head(h, 2, 125) == 2 && h[0] == 0x82 && h[1] == 125);
	CHECK(ws_frame_head(h, 2, 126) == 4 && h[0] == 0x82 && h[1] == 126 && h[2] == 0 && h[3] == 126);
	CHECK(ws_frame_head(h, 2, 65535) == 4 && h[2] == 0xff && h[3] == 0xff);
	CHECK(ws_frame_head(h, 2, 65536) == 10 && h[1] == 127 && h[7] == 1 && h[8] == 0 && h[9] == 0);
	CHECK(h[2] == 0 && h[3] == 0 && h[4] == 0 && h[5] == 0 && h[6] == 0);
}

static void parse_masked_binary(void)
{
	const uint8_t key[4] = { 0x11, 0x22, 0x33, 0x44 };
	const uint8_t pl[3] = { 7, 8, 9 };
	uint8_t f[9] = { 0x82, 0x83, 0x11, 0x22, 0x33, 0x44 };
	ws_msg m;

	for (int i = 0; i < 3; i++) f[6 + i] = pl[i] ^ key[i & 3];
	CHECK(ws_parse(f, 9, &m) == 9);
	CHECK(m.opcode == 2 && m.len == 3 && memcmp(m.data, pl, 3) == 0);
	CHECK(ws_parse(f, 2, &m) == 0);
	CHECK(ws_parse(f, 8, &m) == 0);
	CHECK(ws_parse(f, 0, &m) == 0);
}

static void parse_refuses(void)
{
	uint8_t f[140] = { 0 };
	ws_msg m;

	f[0] = 0x82; f[1] = 0x01;                  /* unmasked */
	CHECK(ws_parse(f, 3, &m) == -1);
	memset(f, 0, sizeof f);
	f[0] = 0x82; f[1] = 0x80 | 126; f[3] = 126; /* 126 bytes */
	CHECK(ws_parse(f, sizeof f, &m) == -1);
	memset(f, 0, sizeof f);
	f[0] = 0x02; f[1] = 0x80;                  /* no FIN */
	CHECK(ws_parse(f, 6, &m) == -1);
	memset(f, 0, sizeof f);
	f[0] = 0xc2; f[1] = 0x80;                  /* RSV1 */
	CHECK(ws_parse(f, 6, &m) == -1);
	memset(f, 0, sizeof f);
	f[0] = 0x83; f[1] = 0x80;                  /* unknown opcode */
	CHECK(ws_parse(f, 6, &m) == -1);
}

static void parse_close_and_ping(void)
{
	const uint8_t close_f[6] = { 0x88, 0x80, 1, 2, 3, 4 };
	const uint8_t ping_f[6] = { 0x89, 0x80, 1, 2, 3, 4 };
	ws_msg m;

	CHECK(ws_parse(close_f, 6, &m) == 6 && m.opcode == 8 && m.len == 0);
	CHECK(ws_parse(ping_f, 6, &m) == 6 && m.opcode == 9 && m.len == 0);
}

int main(void)
{
	sha1_vectors();
	accept_rfc_example();
	head_sizes();
	parse_masked_binary();
	parse_refuses();
	parse_close_and_ping();
	if (fails) {
		printf("p2ws: %d FAIL\n", fails);
		return 1;
	}
	printf("p2ws: ok\n");
	return 0;
}
