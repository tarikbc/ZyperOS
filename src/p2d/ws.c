/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/ws.c: WebSocket handshake key and frames. No sockets here, so a host
 * check can run it. */
#include <string.h>

#include "sha1.h"
#include "ws.h"

#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

bool ws_accept_key(const char *client_key, char out[29])
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char buf[64 + sizeof GUID];
	uint8_t d[20];
	size_t kl;

	if (!client_key) return false;
	kl = strlen(client_key);
	if (kl > 64) return false; /* a real key is 24 characters */
	memcpy(buf, client_key, kl);
	memcpy(buf + kl, GUID, sizeof GUID - 1);
	sha1(buf, kl + sizeof GUID - 1, d);
	/* 20 bytes make six full groups of three and a last group of two. */
	for (int i = 0, o = 0; i < 20; i += 3, o += 4) {
		uint32_t v = (uint32_t)d[i] << 16;

		if (i + 1 < 20) v |= (uint32_t)d[i + 1] << 8;
		if (i + 2 < 20) v |= d[i + 2];
		out[o] = b64[v >> 18];
		out[o + 1] = b64[(v >> 12) & 63];
		out[o + 2] = i + 1 < 20 ? b64[(v >> 6) & 63] : '=';
		out[o + 3] = i + 2 < 20 ? b64[v & 63] : '=';
	}
	out[28] = '\0';
	return true;
}

size_t ws_frame_head(uint8_t *out, int opcode, size_t len)
{
	out[0] = (uint8_t)(0x80 | opcode);
	if (len <= 125) {
		out[1] = (uint8_t)len;
		return 2;
	}
	if (len <= 65535) {
		out[1] = 126;
		out[2] = (uint8_t)(len >> 8);
		out[3] = (uint8_t)len;
		return 4;
	}
	out[1] = 127;
	for (int i = 0; i < 8; i++) out[2 + i] = (uint8_t)((uint64_t)len >> (56 - 8 * i));
	return 10;
}

int ws_parse(const uint8_t *in, size_t n, ws_msg *m)
{
	size_t len;
	int op;

	if (n < 2) return 0;
	if (!(in[0] & 0x80)) return -1;      /* fragments are not supported */
	if (in[0] & 0x70) return -1;         /* reserved bits */
	op = in[0] & 0x0f;
	if (op != 1 && op != 2 && op != 8 && op != 9 && op != 10) return -1;
	if (!(in[1] & 0x80)) return -1;      /* a client must mask */
	len = in[1] & 0x7f;
	if (len > 125) return -1;            /* 126 and 127 mean longer than we take */
	if (n < 6 + len) return 0;
	m->opcode = op;
	m->len = len;
	for (size_t i = 0; i < len; i++) m->data[i] = in[6 + i] ^ in[2 + (i & 3)];
	return (int)(6 + len);
}
