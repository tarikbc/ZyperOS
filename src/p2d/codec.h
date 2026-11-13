/* SPDX-License-Identifier: GPL-3.0-only */
/* The frame codec: a 256x240 XRGB8888 frame in, a small message out, for the
 * guest's browser (it inflates with DecompressionStream('deflate-raw')).
 *
 * Message, little-endian:
 *   byte 0      'F'
 *   byte 1      flags: bit 0 KEY (no delta), bit 1 PAL (a palette follows)
 *   bytes 2-3   width, u16
 *   bytes 4-5   height, u16
 *   bytes 6-9   frame number, u32
 *   [PAL]       u16 count (1..256), then count x 3 bytes R, G, B
 *   rest        raw deflate (zlib level 1, windowBits -15) of width x height
 *               palette indices, XORed with the previous frame's unless KEY
 */
#ifndef P2D_CODEC_H
#define P2D_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define P2C_KEY 1
#define P2C_PAL 2
#define P2C_HEAD 10

typedef struct p2c_enc p2c_enc;

p2c_enc *p2c_new(void);
void p2c_free(p2c_enc *e);

/* The next frame is KEY with PAL. */
void p2c_reset(p2c_enc *e);

/* The message's length, or -1 when the frame has more than 256 colors or cap
 * is too small. After a -1 the encoder resets itself, so the next message
 * decodes on a decoder that saw only the messages that succeeded. */
long p2c_encode(p2c_enc *e, const uint8_t *xrgb, unsigned w, unsigned h,
                uint32_t frame, uint8_t *out, size_t cap);

typedef struct p2c_dec p2c_dec;

p2c_dec *p2c_dec_new(void);
void p2c_dec_free(p2c_dec *d);

/* The frame number, or -1 for a bad message. xrgb_out holds w x h x 4 bytes. */
long p2c_decode(p2c_dec *d, const uint8_t *msg, size_t n,
                uint8_t *xrgb_out, unsigned *w, unsigned *h);

#endif
