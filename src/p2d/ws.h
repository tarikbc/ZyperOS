/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/ws.h: the socket-free half of the WebSocket (RFC 6455) for p2d: the
 * handshake key, the head of a server frame, and the parser for client frames. */
#ifndef P2D_WS_H
#define P2D_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* base64(SHA-1(client_key + the RFC GUID)). out gets 28 characters and a NUL.
 * False for a NULL or over-long key. */
bool ws_accept_key(const char *client_key, char out[29]);

/* Writes the head of an unmasked server frame with FIN set. Returns 2, 4 or 10. */
size_t ws_frame_head(uint8_t *out, int opcode, size_t len);

/* The guest only sends tiny messages, so 125 bytes is the most we accept. */
typedef struct { int opcode; size_t len; uint8_t data[125]; } ws_msg;

/* Parses one client frame from in. Returns the bytes used, 0 when more bytes are
 * needed, -1 for a protocol error. data is unmasked. */
int ws_parse(const uint8_t *in, size_t n, ws_msg *m);

#endif
