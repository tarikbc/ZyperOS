/* SPDX-License-Identifier: GPL-3.0-only */
/* See discimg.h. Each container keeps one unpacked block, so a walk that
 * reads sector after sector unpacks each block once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zlib.h>

#include "chd.h"
#include "discimg.h"

#define CD_FRAME 2448

enum { D_FILE, D_CHD_CD, D_CHD_2048, D_CSO, D_PBP };

struct disc {
	int kind;
	FILE *f;
	uint64_t fsize;           /* the container's */
	uint64_t size;            /* the image's */

	/* CHD */
	chd *c;
	uint32_t start, frames, datasize, per;

	/* CSO and PBP: blocks of `block` bytes, the one in `buf` is `at` */
	uint32_t block, align, nblocks;
	uint64_t base;            /* PBP: where the blocks are counted from */
	uint64_t index;           /* where the index starts */
	uint8_t *buf, *packed;
	int64_t at;
};

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool file_at(disc *d, uint64_t off, void *buf, size_t n)
{
	if (off > d->fsize || n > d->fsize - off) return false;
	if (fseeko(d->f, (off_t)off, SEEK_SET) != 0) return false;
	return fread(buf, 1, n, d->f) == n;
}

static disc *disc_new(int kind)
{
	disc *d = calloc(1, sizeof *d);

	if (d) {
		d->kind = kind;
		d->at = -1;
	}
	return d;
}

static bool open_file(disc *d, const char *path)
{
	d->f = fopen(path, "rb");
	if (!d->f || fseeko(d->f, 0, SEEK_END) != 0) return false;
	d->fsize = (uint64_t)ftello(d->f);
	return true;
}

void disc_close(disc *d)
{
	if (!d) return;
	if (d->f) fclose(d->f);
	chd_close(d->c);
	free(d->buf);
	free(d->packed);
	free(d);
}

uint64_t disc_size(const disc *d) { return d ? d->size : 0; }

/* ------------------------------------------------------------- files ----- */

disc *disc_open_file(const char *path)
{
	disc *d = disc_new(D_FILE);

	if (!d || !open_file(d, path)) { disc_close(d); return NULL; }
	d->size = d->fsize;
	return d;
}

/* --------------------------------------------------------------- CHD ----- */

disc *disc_open_chd_cd(const char *path)
{
	disc *d = disc_new(D_CHD_CD);

	if (!d) return NULL;
	d->c = chd_open(path);
	if (!d->c || chd_hunkbytes(d->c) % CD_FRAME ||
	    !chd_cd_track1(d->c, &d->start, &d->frames, &d->datasize)) {
		disc_close(d);
		return NULL;
	}
	d->per = chd_hunkbytes(d->c) / CD_FRAME;
	d->size = (uint64_t)d->frames * d->datasize;
	return d;
}

disc *disc_open_chd_2048(const char *path)
{
	disc *d = disc_new(D_CHD_2048);
	uint32_t unit;

	if (!d) return NULL;
	d->c = chd_open(path);
	unit = chd_unitbytes(d->c);
	if (!d->c || unit < 2048 || chd_hunkbytes(d->c) % unit) {
		disc_close(d);
		return NULL;
	}
	d->per = chd_hunkbytes(d->c) / unit;
	d->size = chd_logicalbytes(d->c) / unit * 2048;
	return d;
}

static size_t chd_cd_read(disc *d, uint64_t off, uint8_t *buf, size_t n)
{
	size_t got = 0;

	while (n) {
		uint64_t fr = off / d->datasize, abs;
		uint32_t in = (uint32_t)(off % d->datasize);
		size_t take = d->datasize - in;
		const uint8_t *h;

		if (fr >= d->frames) break;
		abs = d->start + fr;
		h = chd_hunk(d->c, (uint32_t)(abs / d->per));
		if (!h) break;
		if (take > n) take = n;
		memcpy(buf, h + (abs % d->per) * CD_FRAME + in, take);
		buf += take; off += take; n -= take; got += take;
	}
	return got;
}

static size_t chd_2048_read(disc *d, uint64_t off, uint8_t *buf, size_t n)
{
	uint32_t unit = chd_unitbytes(d->c);
	size_t got = 0;

	while (n && off < d->size) {
		uint64_t b = off / 2048;
		uint32_t in = (uint32_t)(off % 2048);
		size_t take = 2048 - in;
		const uint8_t *h = chd_hunk(d->c, (uint32_t)(b / d->per));

		if (!h) break;
		if (take > n) take = n;
		memcpy(buf, h + (b % d->per) * unit + in, take);
		buf += take; off += take; n -= take; got += take;
	}
	return got;
}

/* --------------------------------------------------------------- CSO ----- */

/* The header is 24 bytes: "CISO", its own size (most tools write 0), the
 * image's size, the block size, the version and the index's alignment
 * shift. The index follows: one word a block and one more, each the block's
 * offset >> align, the high bit set when it is stored plain. */
disc *disc_open_cso(const char *path)
{
	disc *d = disc_new(D_CSO);
	uint8_t h[24];
	uint64_t total, n;

	if (!d || !open_file(d, path) || !file_at(d, 0, h, sizeof h) || memcmp(h, "CISO", 4))
		goto fail;
	total = (uint64_t)le32(h + 8) | (uint64_t)le32(h + 12) << 32;
	d->block = le32(h + 16);
	d->align = h[21];
	if (h[20] > 1) goto fail;                     /* PPSSPP: "CSO version too high" */
	if (d->block < 2048 || d->block > 16u << 20 || (d->block & (d->block - 1)) ||
	    d->align > 20)
		goto fail;
	n = total / d->block + (total % d->block != 0);
	if (n > 0x7fffffff || 24 + (n + 1) * 4 > d->fsize) goto fail;
	d->nblocks = (uint32_t)n;
	d->index = 24;
	d->size = total;
	d->buf = malloc(d->block);
	d->packed = malloc((size_t)d->block + ((size_t)1 << d->align));
	if (!d->buf || !d->packed) goto fail;
	return d;
fail:
	disc_close(d);
	return NULL;
}

static bool cso_load(disc *d, uint32_t b)
{
	uint8_t e[8];
	uint32_t i0, i1;
	uint64_t pos, end;
	size_t len;
	z_stream z;
	int rc;

	if (d->at == (int64_t)b) return true;
	d->at = -1;
	if (b >= d->nblocks || !file_at(d, d->index + (uint64_t)b * 4, e, 8)) return false;
	i0 = le32(e);
	i1 = le32(e + 4);
	pos = (uint64_t)(i0 & 0x7fffffff) << d->align;
	end = (uint64_t)(i1 & 0x7fffffff) << d->align;
	if (end < pos) return false;
	if (i0 & 0x80000000) {
		/* Plain. The last block of an image may be shorter: zeros after */
		size_t have = d->fsize > pos ? (size_t)(d->fsize - pos < d->block ? d->fsize - pos : d->block) : 0;

		if (!have || !file_at(d, pos, d->buf, have)) return false;
		memset(d->buf + have, 0, d->block - have);
	} else {
		len = (size_t)(end - pos);
		if (len > (size_t)d->block + ((size_t)1 << d->align)) len = (size_t)d->block + ((size_t)1 << d->align);
		if (pos + len > d->fsize) len = (size_t)(d->fsize - pos);
		if (!file_at(d, pos, d->packed, len)) return false;
		memset(&z, 0, sizeof z);
		if (inflateInit2(&z, -15) != Z_OK) return false;
		z.next_in = d->packed;
		z.avail_in = (uInt)len;
		z.next_out = d->buf;
		z.avail_out = d->block;
		rc = inflate(&z, Z_FINISH);
		inflateEnd(&z);
		/* PPSSPP's test: the stream ends, and fills the block */
		if (rc != Z_STREAM_END || z.total_out != d->block) return false;
	}
	d->at = b;
	return true;
}

/* --------------------------------------------------------------- PBP ----- */

#define PBP_BLOCK (16 * 2352)

/* An EBOOT.PBP begins "\0PBP", a version, then eight offsets; the last, at
 * 0x24, is DATA.PSAR. A PSone disc there is "PSISOIMG0000": its block index
 * at +0x4000, 32 bytes an entry (offset from +0x100000, a 16-bit length,
 * the rest checksums), and its blocks from +0x100000, each 16 raw sectors
 * in raw deflate (PCSX ReARMed inflates every one; a whole-block length that
 * does not inflate is taken as stored). A multi-disc one is
 * "PSTITLEIMG000000" with each disc's PSISOIMG offset at +0x200. */
disc *disc_open_pbp_psx(const char *path)
{
	disc *d = disc_new(D_PBP);
	uint8_t h[40], sig[16];
	uint64_t psar, img;

	if (!d || !open_file(d, path) || !file_at(d, 0, h, sizeof h) || memcmp(h, "\0PBP", 4))
		goto fail;
	psar = le32(h + 0x24);
	if (!file_at(d, psar, sig, sizeof sig)) goto fail;
	img = psar;
	if (!memcmp(sig, "PSTITLEIMG", 10)) {
		uint8_t o[4];

		/* The first disc, as an .m3u's first is the one hashed */
		if (!file_at(d, psar + 0x200, o, 4) || !le32(o)) goto fail;
		img = psar + le32(o);
		if (!file_at(d, img, sig, sizeof sig)) goto fail;
	}
	if (memcmp(sig, "PSISOIMG00", 10)) goto fail;
	d->base = img + 0x100000;
	d->index = img + 0x4000;
	d->block = PBP_BLOCK;
	/* The index is not counted out: it ends at its first empty entry, which
	 * a read reaches only past the disc's end (pbp_load refuses it) */
	d->nblocks = (0x100000 - 0x4000) / 32;
	d->size = (uint64_t)d->nblocks * PBP_BLOCK;
	d->buf = malloc(PBP_BLOCK);
	d->packed = malloc(PBP_BLOCK);
	if (!d->buf || !d->packed) goto fail;
	return d;
fail:
	disc_close(d);
	return NULL;
}

static bool pbp_load(disc *d, uint32_t b)
{
	uint8_t e[8];
	uint64_t pos;
	uint32_t len;

	if (d->at == (int64_t)b) return true;
	d->at = -1;
	if (b >= d->nblocks || !file_at(d, d->index + (uint64_t)b * 32, e, sizeof e)) return false;
	pos = d->base + le32(e);
	len = (uint32_t)e[4] | (uint32_t)e[5] << 8;
	if (!len || len > PBP_BLOCK || !file_at(d, pos, d->packed, len)) return false;
	{
		z_stream z;
		int rc;

		memset(&z, 0, sizeof z);
		if (inflateInit2(&z, -15) != Z_OK) return false;
		z.next_in = d->packed;
		z.avail_in = len;
		z.next_out = d->buf;
		z.avail_out = PBP_BLOCK;
		rc = inflate(&z, Z_FINISH);
		inflateEnd(&z);
		if (rc == Z_STREAM_END) {
			/* The disc's last block may unpack short; the rest is no sector */
			memset(d->buf + z.total_out, 0, PBP_BLOCK - z.total_out);
		} else if (len == PBP_BLOCK) {
			/* A whole block that is no zlib: stored as it is, which some
			 * converters do with a block that would not shrink */
			memcpy(d->buf, d->packed, PBP_BLOCK);
		} else {
			return false;
		}
	}
	d->at = b;
	return true;
}

/* ------------------------------------------------------------- reads ----- */

static size_t block_read(disc *d, uint64_t off, uint8_t *buf, size_t n)
{
	size_t got = 0;

	while (n && off < d->size) {
		uint32_t b = (uint32_t)(off / d->block), in = (uint32_t)(off % d->block);
		size_t take = d->block - in;
		bool ok = d->kind == D_CSO ? cso_load(d, b) : pbp_load(d, b);

		if (!ok) break;
		if (take > n) take = n;
		if (take > d->size - off) take = (size_t)(d->size - off);
		memcpy(buf, d->buf + in, take);
		buf += take; off += take; n -= take; got += take;
	}
	return got;
}

size_t disc_read(disc *d, uint64_t off, void *buf, size_t n)
{
	if (!d) return 0;
	switch (d->kind) {
	case D_FILE:
		if (off >= d->fsize) return 0;
		if (fseeko(d->f, (off_t)off, SEEK_SET) != 0) return 0;
		return fread(buf, 1, n, d->f);
	case D_CHD_CD:   return chd_cd_read(d, off, buf, n);
	case D_CHD_2048: return chd_2048_read(d, off, buf, n);
	default:         return block_read(d, off, buf, n);
	}
}
