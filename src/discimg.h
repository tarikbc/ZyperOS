/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DISCIMG_H
#define ZYPEROS_DISCIMG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A disc image read as the plain bytes it stands for, whatever holds them:
 * so src/rahash.c's ISO 9660 walk, written for a .bin or an .iso, reads a
 * .chd, a .cso or a PlayStation .pbp the same way, a few sectors at a time
 * and never the whole image.
 *
 *   disc_open_file       the file as it is (.bin, .iso, .img)
 *   disc_open_chd_cd     a CD CHD's first track, as its .bin was: each
 *                        sector's bytes (2352 raw, 2048 cooked) one after
 *                        another. That is what libretro's CHD reader hands
 *                        rcheevos in RetroArch, so the hash is the .bin's.
 *   disc_open_chd_2048   any CHD as 2048-byte blocks, the first 2048 bytes of
 *                        each unit: PPSSPP's own rule for a PSP CHD
 *                        (CHDFileBlockDevice), so a DVD image and a cooked CD
 *                        image of the same UMD read alike
 *   disc_open_cso        a PSP .cso, CISO version 0 or 1, as PPSSPP reads
 *                        one (CISOFileBlockDevice): zlib blocks, the high
 *                        bit of an index entry for one stored plain. Version
 *                        2 is refused, as PPSSPP refuses it.
 *   disc_open_pbp_psx    a PSone Classics EBOOT.PBP's disc (the first, for a
 *                        multi-disc one), as raw 2352-byte sectors from the
 *                        disc's start, the way PCSX ReARMed reads it:
 *                        PSISOIMG0000's zlib blocks of 16 sectors.
 *
 * Each read answers how many bytes it put in `buf`, short at the end of the
 * image or where a block does not unpack: the walk then finds no file and
 * the hash is refused. */
typedef struct disc disc;

disc *disc_open_file(const char *path);
disc *disc_open_chd_cd(const char *path);
disc *disc_open_chd_2048(const char *path);
disc *disc_open_cso(const char *path);
disc *disc_open_pbp_psx(const char *path);

size_t disc_read(disc *d, uint64_t off, void *buf, size_t n);
uint64_t disc_size(const disc *d);
void disc_close(disc *d);

#endif
