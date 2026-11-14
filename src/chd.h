/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CHD_H
#define ZYPEROS_CHD_H

#include <stdbool.h>
#include <stdint.h>

/* MAME's compressed disc image, read one hunk at a time - enough of it to
 * reach the few sectors a RetroAchievements hash reads (src/rahash.c), and
 * no more. A CHD is a header, a map that says where each hunk is and how it
 * was packed, the packed hunks, and a metadata list (the track list, for a
 * CD).
 *
 * WHAT THIS READS: versions 3, 4 and 5; V5's Huffman-coded map; the codecs
 * chdman packs a CD or a DVD with - zlib, LZMA (the system's liblzma, which
 * FFmpeg already brings to the device), CHD's own Huffman, FLAC
 * (src/flacdec.c), and their CD forms, which pack the 2352 bytes of each
 * sector and its subcode apart.
 *
 * WHAT IT DOES NOT: zstd (`chdman -c cdzs`/`zstd`, never a default: the
 * device has no libzstd), the laserdisc codec, and a CHD that needs a parent
 * (a diff). Those open, and the hunks they would need answer NULL, so the
 * hash is refused rather than made of the wrong bytes.
 *
 * Memory: a hunk and its packed bytes (19 KB on a CD), 5 bytes a hunk for a
 * V5 map (about 190 KB for a 700 MB CD, 2 MB for a 1.8 GB DVD at 4 KB
 * hunks), never the image. */

typedef struct chd chd;

chd *chd_open(const char *path);
void chd_close(chd *c);

uint32_t chd_hunkbytes(const chd *c);
/* The bytes of one unit: a CD frame (2448: 2352 of sector and 96 of
 * subcode), a DVD's 2048-byte sector */
uint32_t chd_unitbytes(const chd *c);
uint64_t chd_logicalbytes(const chd *c);

/* Hunk n, unpacked, in the reader's own buffer: chd_hunkbytes long, good
 * until the next call. NULL past the end, for a codec this reader does not
 * have, a parent's hunk, or a hunk that does not unpack. A CD hunk's sectors
 * come back with their sync bytes put back where chdman took them out, and
 * their subcode as zeros: a hash reads neither the subcode nor the ECC. */
const uint8_t *chd_hunk(chd *c, uint32_t n);

/* A CD image's first track: the frame it starts at, its frames, and how many
 * bytes at the start of each 2448-byte frame are the sector (2352 raw, 2048
 * cooked, 2336 Mode 2). False when there is no track list: a DVD image or a
 * hard disk. */
bool chd_cd_track1(chd *c, uint32_t *start, uint32_t *frames, uint32_t *datasize);

#endif
