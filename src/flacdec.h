/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_FLACDEC_H
#define ZYPEROS_FLACDEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A CHD disc image (MAME's compressed format) codes its CD audio hunks as
 * FLAC, but not as a FLAC file: a hunk coded "flac" or "cdfl" holds bare
 * frames, with no "fLaC" marker and no metadata blocks in front of them.
 * What a STREAMINFO block would have said is fixed by the format instead -
 * 16-bit samples, 44100 Hz, a channel count the caller already knows - so
 * this decodes frames and nothing else, from the first byte of src.
 *
 * It is written from the FLAC specification (RFC 9639) and takes all a
 * libFLAC encoder can emit for 16-bit audio. Both CRCs are checked, so a
 * damaged hunk ends in false rather than in a track with a click in it.
 */

/* Decodes raw FLAC frames from src[0..srclen) until `frames` samples per
 * channel are out, written interleaved as 16-bit samples into out
 * (frames * channels * 2 bytes), each sample big-endian when big_endian,
 * little-endian otherwise. False on any malformed or unsupported input, on a
 * frame whose channel count is not `channels` or whose sample size is not 16
 * bits, or when src runs out first. Never reads past src+srclen, never writes
 * past out+frames*channels*2. */
bool flac_decode_s16(const uint8_t *src, size_t srclen, unsigned channels,
                     uint8_t *out, size_t frames, bool big_endian);

#endif
