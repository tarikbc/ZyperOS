/* SPDX-License-Identifier: GPL-3.0-only */
/* src/p2d/sha1.h: SHA-1, only for the WebSocket handshake key. */
#ifndef P2D_SHA1_H
#define P2D_SHA1_H

#include <stddef.h>
#include <stdint.h>

void sha1(const void *data, size_t n, uint8_t out[20]);

#endif
