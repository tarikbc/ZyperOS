/* SPDX-License-Identifier: GPL-3.0-only */
/* See chd.h. Written from the format as MAME's chd.cpp, chdcodec.cpp and
 * huffman.cpp define it, and as libchdr reads it: libchdr on the Mac is the
 * reference tools/chd-check.py holds this to, hunk by hunk, on images chdman
 * makes there with each codec. */
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zlib.h>

#include "chd.h"
#include "flacdec.h"

#define FOURCC(a, b, c, d) \
	(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

#define CODEC_ZLIB FOURCC('z', 'l', 'i', 'b')
#define CODEC_LZMA FOURCC('l', 'z', 'm', 'a')
#define CODEC_HUFF FOURCC('h', 'u', 'f', 'f')
#define CODEC_FLAC FOURCC('f', 'l', 'a', 'c')
#define CODEC_CDZL FOURCC('c', 'd', 'z', 'l')
#define CODEC_CDLZ FOURCC('c', 'd', 'l', 'z')
#define CODEC_CDFL FOURCC('c', 'd', 'f', 'l')

#define META_CHT2 FOURCC('C', 'H', 'T', '2')
#define META_CHTR FOURCC('C', 'H', 'T', 'R')
#define META_CHCD FOURCC('C', 'H', 'C', 'D')

#define CD_FRAME  2448          /* a CD frame in a CHD: the sector, then subcode */
#define CD_SECTOR 2352
#define CD_SUB    96

/* V5 map types. The last seven only appear in the packed map, which turns
 * them into the first seven as it is read. */
enum {
	T_CODEC0, T_CODEC1, T_CODEC2, T_CODEC3,
	T_NONE, T_SELF, T_PARENT,
	T_RLE_SMALL, T_RLE_LARGE, T_SELF_0, T_SELF_1, T_PARENT_SELF, T_PARENT_0, T_PARENT_1
};

/* V3 and V4 map entry types */
enum { V4_COMPRESSED = 1, V4_UNCOMPRESSED = 2, V4_MINI = 3, V4_SELF = 4, V4_PARENT = 5 };

struct chd {
	FILE *f;
	uint64_t fsize;
	int version;
	uint32_t hunkbytes, unitbytes, hunkcount;
	uint64_t logical, mapoffset, metaoffset;
	uint32_t codec[4];        /* V5; V3/V4 keep theirs in codec[0] */
	bool packed_map;          /* V5 with a codec: the Huffman-coded map */

	/* The packed V5 map, unpacked to what a read needs: each hunk's type
	 * and its length (or the hunk a SELF entry repeats), and every 256th
	 * hunk's file offset. The others' offsets are the sum of the lengths
	 * since, which is how the map itself stores them. */
	uint8_t *type;
	uint32_t *val;
	uint64_t *mark;

	uint8_t *hunk;            /* the unpacked hunk, hunkbytes */
	int64_t at;               /* which one it is, -1 for none */
	uint8_t *packed;          /* its bytes as stored */
	size_t packed_cap;
	uint8_t *scratch;         /* a CD codec's sector data, before it is laid
	                           * out into frames */
	uint16_t *huff_lookup;    /* the Huffman codec's table, made once */
};

/* ------------------------------------------------------------- bytes ----- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be24(const uint8_t *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t be48(const uint8_t *p) { return (uint64_t)be16(p) << 32 | be32(p + 2); }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

static bool read_at(chd *c, uint64_t off, void *buf, size_t n)
{
	if (off > c->fsize || n > c->fsize - off) return false;
	if (fseeko(c->f, (off_t)off, SEEK_SET) != 0) return false;
	return fread(buf, 1, n, c->f) == n;
}

/* The CRC the V5 map is checked with: CCITT, 0x1021 from 0xffff, unreflected */
static uint16_t crc16(uint16_t crc, const uint8_t *p, size_t n)
{
	while (n--) {
		int i;

		crc ^= (uint16_t)(*p++ << 8);
		for (i = 0; i < 8; i++)
			crc = (crc & 0x8000) ? (uint16_t)(crc << 1 ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}

/* -------------------------------------------------------------- bits ----- */

/* Most significant bit first. Past the end it reads zeros and notes the
 * overrun, as MAME's bitstream does: the last code of a map or a hunk may be
 * peeked with fewer bits left than the longest code. */
typedef struct {
	const uint8_t *p;
	size_t n;
	uint64_t pos;
} bits;

static uint32_t bits_peek(const bits *b, int k)
{
	size_t i = (size_t)(b->pos >> 3);
	uint64_t w = 0;
	int j;

	if (k <= 0) return 0;
	for (j = 0; j < 5; j++)
		w = w << 8 | (i + (size_t)j < b->n ? b->p[i + (size_t)j] : 0);
	return (uint32_t)((w << (24 + (b->pos & 7))) >> (64 - k));
}

static uint32_t bits_read(bits *b, int k)
{
	uint32_t v = bits_peek(b, k);

	b->pos += (uint64_t)(k > 0 ? k : 0);
	return v;
}

static bool bits_over(const bits *b) { return b->pos > (uint64_t)b->n * 8; }

/* ----------------------------------------------------------- Huffman ----- */

/* MAME's canonical Huffman decoder: code lengths come first, in one of two
 * packings, and the codes follow from them. A lookup of 2^maxbits entries,
 * each the symbol and its length, decodes a symbol in one step. */
typedef struct {
	int numcodes, maxbits;
	uint8_t len[256];
	uint32_t code[256];
	uint16_t *lookup;
} huff;

static bool huff_codes(huff *h)
{
	uint32_t histo[33] = { 0 }, start = 0;
	int i, l;

	for (i = 0; i < h->numcodes; i++) {
		if (h->len[i] > h->maxbits) return false;
		histo[h->len[i]]++;
	}
	/* The longest codes take the lowest numbers, and each length must leave
	 * an even count to the next shorter (length 1 is let off, as in MAME) */
	for (l = 32; l > 0; l--) {
		uint32_t next = (start + histo[l]) >> 1;

		if (l != 1 && next * 2 != start + histo[l]) return false;
		histo[l] = start;
		start = next;
	}
	for (i = 0; i < h->numcodes; i++)
		if (h->len[i]) h->code[i] = histo[h->len[i]]++;
	return true;
}

static bool huff_table(huff *h)
{
	uint32_t size = 1u << h->maxbits, k;
	int i;

	/* A slot no code reaches - only a malformed tree leaves one - reads as
	 * symbol 0, one bit: wrong bytes, never a loop that eats no input */
	for (k = 0; k < size; k++) h->lookup[k] = 1;
	for (i = 0; i < h->numcodes; i++) {
		int shift = h->maxbits - h->len[i];
		uint32_t from, to;

		if (!h->len[i]) continue;
		from = h->code[i] << shift;
		to = ((h->code[i] + 1) << shift) - 1;
		if (to >= size || to < from) return false;
		for (k = from; k <= to; k++)
			h->lookup[k] = (uint16_t)(i << 5 | h->len[i]);
	}
	return true;
}

static int huff_one(const huff *h, bits *b)
{
	uint16_t v = h->lookup[bits_peek(b, h->maxbits)];

	b->pos += v & 31;
	return v >> 5;
}

/* The packing the V5 map uses: lengths of 4 bits (maxbits 8), with a 1 as
 * the escape for "the next length, so many times" */
static bool huff_import_rle(huff *h, bits *b)
{
	int k = h->maxbits >= 16 ? 5 : h->maxbits >= 8 ? 4 : 3;
	int i = 0;

	while (i < h->numcodes) {
		int v = (int)bits_read(b, k);

		if (v != 1) {
			h->len[i++] = (uint8_t)v;
			continue;
		}
		v = (int)bits_read(b, k);
		if (v == 1) {
			h->len[i++] = 1;
		} else {
			int rep = (int)bits_read(b, k) + 3;

			if (i + rep > h->numcodes) return false;
			while (rep--) h->len[i++] = (uint8_t)v;
		}
	}
	return huff_codes(h) && huff_table(h) && !bits_over(b);
}

/* The packing the Huffman codec uses: the 256 lengths are themselves
 * Huffman coded, by a small tree of 24 whose lengths come first, 3 bits
 * each. A small symbol 0 repeats the last length. */
static bool huff_import_huffman(huff *h, bits *b)
{
	uint16_t small_lookup[1 << 6];
	huff s = { .numcodes = 24, .maxbits = 6, .lookup = small_lookup };
	int start, i, count = 0, last = 0, rlebits = 0;
	uint32_t t;

	memset(s.len, 0, sizeof s.len);
	s.len[0] = (uint8_t)bits_read(b, 3);
	start = (int)bits_read(b, 3) + 1;
	for (i = 1; i < 24; i++) {
		if (i < start || count == 7) {
			s.len[i] = 0;
		} else {
			count = (int)bits_read(b, 3);
			s.len[i] = (uint8_t)(count == 7 ? 0 : count);
		}
	}
	if (!huff_codes(&s) || !huff_table(&s)) return false;

	for (t = (uint32_t)h->numcodes - 9; t; t >>= 1) rlebits++;
	for (i = 0; i < h->numcodes; ) {
		int v = huff_one(&s, b);

		if (v) {
			h->len[i++] = (uint8_t)(last = v - 1);
		} else {
			int rep = (int)bits_read(b, 3) + 2;

			if (rep == 7 + 2) rep += (int)bits_read(b, rlebits);
			for (; rep && i < h->numcodes; rep--) h->len[i++] = (uint8_t)last;
		}
	}
	return huff_codes(h) && huff_table(h);
}

/* ------------------------------------------------------------ codecs ----- */

/* zlib, as CHD stores it: raw deflate, no wrapper. The whole hunk or fail. */
static bool un_zlib(const uint8_t *src, size_t n, uint8_t *dst, size_t want)
{
	z_stream z;
	int rc;

	memset(&z, 0, sizeof z);
	if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
	z.next_in = (Bytef *)(uintptr_t)src;
	z.avail_in = (uInt)n;
	z.next_out = dst;
	z.avail_out = (uInt)want;
	rc = inflate(&z, Z_FINISH);
	inflateEnd(&z);
	return (rc == Z_STREAM_END || rc == Z_OK || rc == Z_BUF_ERROR) && z.total_out == want;
}

/* LZMA, as CHD stores it: a raw LZMA1 stream, lc 3 lp 0 pb 2, no header and
 * no end marker, so it ends when the hunk is full. The dictionary need only
 * hold the hunk. */
static bool un_lzma(const uint8_t *src, size_t n, uint8_t *dst, size_t want)
{
	lzma_options_lzma opt;
	lzma_filter fl[2];
	lzma_stream s = LZMA_STREAM_INIT;
	lzma_ret rc;
	bool ok;

	memset(&opt, 0, sizeof opt);
	opt.dict_size = want < LZMA_DICT_SIZE_MIN ? LZMA_DICT_SIZE_MIN : (uint32_t)want;
	opt.lc = 3;
	opt.lp = 0;
	opt.pb = 2;
	fl[0].id = LZMA_FILTER_LZMA1;
	fl[0].options = &opt;
	fl[1].id = LZMA_VLI_UNKNOWN;
	fl[1].options = NULL;
	if (lzma_raw_decoder(&s, fl) != LZMA_OK) return false;
	s.next_in = src;
	s.avail_in = n;
	s.next_out = dst;
	s.avail_out = want;
	do {
		rc = lzma_code(&s, LZMA_RUN);
	} while (rc == LZMA_OK && s.avail_out && s.avail_in);
	ok = s.avail_out == 0 && (rc == LZMA_OK || rc == LZMA_STREAM_END);
	lzma_end(&s);
	return ok;
}

static bool un_huff(chd *c, const uint8_t *src, size_t n, uint8_t *dst, size_t want)
{
	huff h = { .numcodes = 256, .maxbits = 16 };
	bits b = { src, n, 0 };
	size_t i;

	if (!c->huff_lookup) {
		c->huff_lookup = malloc(sizeof(uint16_t) << 16);
		if (!c->huff_lookup) return false;
	}
	h.lookup = c->huff_lookup;
	if (!huff_import_huffman(&h, &b)) return false;
	for (i = 0; i < want; i++) dst[i] = (uint8_t)huff_one(&h, &b);
	return !bits_over(&b);
}

/* FLAC over a hunk of 16-bit stereo: a first byte says which order the
 * samples go back in, 'L' or 'B' */
static bool un_flac(const uint8_t *src, size_t n, uint8_t *dst, size_t want)
{
	if (n < 1 || want % 4 || (src[0] != 'L' && src[0] != 'B')) return false;
	return flac_decode_s16(src + 1, n - 1, 2, dst, want / 4, src[0] == 'B');
}

static const uint8_t cd_sync[12] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

/* The CD codecs pack the sectors of a hunk apart from their subcode. cdzl
 * and cdlz begin with a bit a frame saying chdman took its sync and ECC out
 * (they can be made again), then the packed sectors' length; cdfl has
 * neither and packs the sectors as big-endian audio. The subcode, packed
 * after, is not read: nothing here needs it. */
static bool un_cd(chd *c, uint32_t codec, const uint8_t *src, size_t n, uint8_t *dst, size_t want)
{
	uint32_t frames = (uint32_t)(want / CD_FRAME), i;
	size_t sectors = (size_t)frames * CD_SECTOR;
	uint32_t eccbytes = (frames + 7) / 8;
	bool ok;

	if (!frames || want % CD_FRAME) return false;
	if (!c->scratch) {
		c->scratch = malloc(sectors);
		if (!c->scratch) return false;
	}
	if (codec == CODEC_CDFL) {
		ok = flac_decode_s16(src, n, 2, c->scratch, sectors / 4, true);
		eccbytes = 0;
	} else {
		uint32_t lenbytes = want < 65536 ? 2 : 3, base;
		size_t head = eccbytes + lenbytes;

		if (n < head) return false;
		base = lenbytes == 2 ? be16(src + eccbytes) : be24(src + eccbytes);
		if (base > n - head) return false;
		ok = codec == CODEC_CDLZ ? un_lzma(src + head, base, c->scratch, sectors)
		                         : un_zlib(src + head, base, c->scratch, sectors);
	}
	if (!ok) return false;
	for (i = 0; i < frames; i++) {
		uint8_t *fr = dst + (size_t)i * CD_FRAME;

		memcpy(fr, c->scratch + (size_t)i * CD_SECTOR, CD_SECTOR);
		memset(fr + CD_SECTOR, 0, CD_SUB);
		/* The sync is back for any reader that looks for it (a hash finds
		 * the sector size by it); the ECC is not: no hash reads it */
		if (eccbytes && (src[i / 8] & (1 << (i % 8))))
			memcpy(fr, cd_sync, sizeof cd_sync);
	}
	return true;
}

static bool unpack(chd *c, uint32_t codec, const uint8_t *src, size_t n, uint8_t *dst)
{
	size_t want = c->hunkbytes;

	switch (codec) {
	case CODEC_ZLIB: return un_zlib(src, n, dst, want);
	case CODEC_LZMA: return un_lzma(src, n, dst, want);
	case CODEC_HUFF: return un_huff(c, src, n, dst, want);
	case CODEC_FLAC: return un_flac(src, n, dst, want);
	case CODEC_CDZL:
	case CODEC_CDLZ:
	case CODEC_CDFL: return un_cd(c, codec, src, n, dst, want);
	default:         return false;     /* zstd, cdzs, the laserdisc codec */
	}
}

/* --------------------------------------------------------------- map ----- */

static bool v5_map(chd *c)
{
	uint8_t hdr[16];
	uint32_t mapbytes, h, last_self = 0;
	uint64_t cur, last_parent = 0;
	int lengthbits, selfbits, parentbits, rep = 0;
	uint8_t lastcomp = 0, *packed;
	uint16_t crc = 0xffff, want;
	uint16_t lookup[1 << 8];
	huff tree = { .numcodes = 16, .maxbits = 8, .lookup = lookup };
	bits b;

	if (!read_at(c, c->mapoffset, hdr, sizeof hdr)) return false;
	mapbytes = be32(hdr);
	cur = be48(hdr + 4);
	want = be16(hdr + 10);
	lengthbits = hdr[12];
	selfbits = hdr[13];
	parentbits = hdr[14];
	if (lengthbits > 32 || selfbits > 32 || parentbits > 32) return false;
	if (c->mapoffset + 16 > c->fsize || mapbytes > c->fsize - c->mapoffset - 16) return false;

	packed = malloc(mapbytes ? mapbytes : 1);
	c->type = malloc(c->hunkcount ? c->hunkcount : 1);
	c->val = malloc(sizeof *c->val * (c->hunkcount ? c->hunkcount : 1));
	c->mark = malloc(sizeof *c->mark * (c->hunkcount / 256 + 1));
	if (!packed || !c->type || !c->val || !c->mark) { free(packed); return false; }
	if (!read_at(c, c->mapoffset + 16, packed, mapbytes)) { free(packed); return false; }
	b = (bits){ packed, mapbytes, 0 };

	/* First every hunk's type, run-length coded... */
	if (!huff_import_rle(&tree, &b)) { free(packed); return false; }
	for (h = 0; h < c->hunkcount; h++) {
		if (rep > 0) {
			c->type[h] = lastcomp;
			rep--;
			continue;
		}
		if (bits_over(&b)) { free(packed); return false; }
		{
			int v = huff_one(&tree, &b);

			if (v == T_RLE_SMALL) {
				c->type[h] = lastcomp;
				rep = 2 + huff_one(&tree, &b);
			} else if (v == T_RLE_LARGE) {
				c->type[h] = lastcomp;
				rep = 2 + 16 + (huff_one(&tree, &b) << 4);
				rep += huff_one(&tree, &b);
			} else {
				c->type[h] = lastcomp = (uint8_t)v;
			}
		}
	}

	/* ...then what each needs, in hunk order. The CRC is over the map as
	 * MAME lays it out unpacked, 12 bytes a hunk. */
	for (h = 0; h < c->hunkcount; h++) {
		uint64_t off = cur;
		uint32_t len = 0;
		uint16_t hcrc = 0;
		uint8_t e[12];

		if (!(h & 255)) c->mark[h >> 8] = cur;
		switch (c->type[h]) {
		case T_CODEC0: case T_CODEC1: case T_CODEC2: case T_CODEC3:
			len = bits_read(&b, lengthbits);
			cur += len;
			hcrc = (uint16_t)bits_read(&b, 16);
			c->val[h] = len;
			break;
		case T_NONE:
			len = c->hunkbytes;
			cur += len;
			hcrc = (uint16_t)bits_read(&b, 16);
			c->val[h] = len;
			break;
		case T_SELF:
			off = last_self = bits_read(&b, selfbits);
			break;
		case T_PARENT:
			off = last_parent = bits_read(&b, parentbits);
			break;
		case T_SELF_1:
			last_self++;
			/* fall through */
		case T_SELF_0:
			c->type[h] = T_SELF;
			off = last_self;
			break;
		case T_PARENT_SELF:
			c->type[h] = T_PARENT;
			off = last_parent = (uint64_t)h * c->hunkbytes / c->unitbytes;
			break;
		case T_PARENT_1:
			last_parent += c->hunkbytes / c->unitbytes;
			/* fall through */
		case T_PARENT_0:
			c->type[h] = T_PARENT;
			off = last_parent;
			break;
		default:
			free(packed);
			return false;
		}
		if (c->type[h] == T_SELF) c->val[h] = (uint32_t)off;
		else if (c->type[h] == T_PARENT) c->val[h] = 0;
		e[0] = c->type[h];
		e[1] = (uint8_t)(len >> 16); e[2] = (uint8_t)(len >> 8); e[3] = (uint8_t)len;
		e[4] = (uint8_t)(off >> 40); e[5] = (uint8_t)(off >> 32); e[6] = (uint8_t)(off >> 24);
		e[7] = (uint8_t)(off >> 16); e[8] = (uint8_t)(off >> 8); e[9] = (uint8_t)off;
		e[10] = (uint8_t)(hcrc >> 8); e[11] = (uint8_t)hcrc;
		crc = crc16(crc, e, sizeof e);
	}
	free(packed);
	return crc == want;
}

/* Where V5's hunk h is stored: the last mark, plus what was stored since */
static uint64_t v5_offset(const chd *c, uint32_t h)
{
	uint64_t off = c->mark[h >> 8];
	uint32_t g;

	for (g = h & ~255u; g < h; g++)
		if (c->type[g] <= T_NONE) off += c->val[g];
	return off;
}

static bool fetch(chd *c, uint64_t off, size_t n)
{
	if (n > c->packed_cap) {
		uint8_t *p = realloc(c->packed, n);

		if (!p) return false;
		c->packed = p;
		c->packed_cap = n;
	}
	return read_at(c, off, c->packed, n);
}

/* ------------------------------------------------------------- hunks ----- */

static bool load(chd *c, uint32_t h, int depth)
{
	if (h >= c->hunkcount || depth > 16) return false;

	if (c->version >= 5 && c->packed_map) {
		uint8_t t = c->type[h];

		if (t <= T_CODEC3) {
			uint32_t codec = c->codec[t];

			return fetch(c, v5_offset(c, h), c->val[h]) &&
			       unpack(c, codec, c->packed, c->val[h], c->hunk);
		}
		if (t == T_NONE) return read_at(c, v5_offset(c, h), c->hunk, c->hunkbytes);
		/* A hunk the same as an earlier one; always earlier, or a crafted
		 * map could send this round in a circle */
		if (t == T_SELF) return c->val[h] < h && load(c, c->val[h], depth + 1);
		return false;               /* the parent's: not here */
	}

	if (c->version >= 5) {
		uint8_t e[4];
		uint64_t off;

		if (!read_at(c, c->mapoffset + (uint64_t)h * 4, e, 4)) return false;
		off = (uint64_t)be32(e) * c->hunkbytes;
		if (!off) {                 /* never written: zeros */
			memset(c->hunk, 0, c->hunkbytes);
			return true;
		}
		return read_at(c, off, c->hunk, c->hunkbytes);
	}

	/* V3 and V4: 16 bytes a hunk, right after the header */
	{
		uint8_t e[16];
		uint64_t off;
		uint32_t len;

		if (!read_at(c, c->mapoffset + (uint64_t)h * 16, e, 16)) return false;
		off = be64(e);
		len = (uint32_t)be16(e + 12) | (uint32_t)e[14] << 16;
		switch (e[15] & 15) {
		case V4_COMPRESSED:
			if (c->codec[0] != 1 && c->codec[0] != 2) return false;   /* zlib, zlib+ */
			return fetch(c, off, len) && un_zlib(c->packed, len, c->hunk, c->hunkbytes);
		case V4_UNCOMPRESSED:
			return read_at(c, off, c->hunk, c->hunkbytes);
		case V4_MINI: {
			uint32_t i;

			for (i = 0; i < c->hunkbytes; i++) c->hunk[i] = e[i % 8];
			return true;
		}
		case V4_SELF:
			return off < h && load(c, (uint32_t)off, depth + 1);
		default:
			return false;
		}
	}
}

const uint8_t *chd_hunk(chd *c, uint32_t n)
{
	if (!c) return NULL;
	if (c->at == (int64_t)n) return c->hunk;
	c->at = -1;
	if (!load(c, n, 0)) return NULL;
	c->at = n;
	return c->hunk;
}

/* ---------------------------------------------------------- metadata ----- */

/* The nth entry with this tag, NUL-terminated, or NULL */
static char *meta(chd *c, uint32_t tag, int nth, uint32_t *len_out)
{
	uint64_t off = c->metaoffset;
	int guard = 0;

	while (off && guard++ < 4096) {
		uint8_t h[16];
		uint32_t len;

		if (!read_at(c, off, h, sizeof h)) return NULL;
		len = be24(h + 5);
		if (be32(h) == tag && nth-- == 0) {
			char *d = malloc((size_t)len + 1);

			if (!d) return NULL;
			if (!read_at(c, off + 16, d, len)) { free(d); return NULL; }
			d[len] = '\0';
			if (len_out) *len_out = len;
			return d;
		}
		off = be64(h + 8);
	}
	return NULL;
}

/* The bytes of a frame that are its sector, by MAME's names for the track
 * types (both its spellings: chdman's own and the .cue's) */
static uint32_t track_datasize(const char *type)
{
	static const struct { const char *name; uint32_t size; } t[] = {
		{ "MODE1", 2048 }, { "MODE1/2048", 2048 }, { "MODE1_RAW", 2352 }, { "MODE1/2352", 2352 },
		{ "MODE2", 2336 }, { "MODE2/2336", 2336 }, { "MODE2_FORM1", 2048 }, { "MODE2/2048", 2048 },
		{ "MODE2_FORM2", 2324 }, { "MODE2/2324", 2324 }, { "MODE2_FORM_MIX", 2336 },
		{ "MODE2_RAW", 2352 }, { "MODE2/2352", 2352 }, { "CDI/2352", 2352 }, { "AUDIO", 2352 },
	};
	size_t i;

	for (i = 0; i < sizeof t / sizeof t[0]; i++)
		if (!strcmp(type, t[i].name)) return t[i].size;
	return 0;
}

bool chd_cd_track1(chd *c, uint32_t *start, uint32_t *frames, uint32_t *datasize)
{
	char *m, type[32] = "", pgtype[32] = "";
	int track = 0, nframes = 0, pregap = 0;
	uint32_t len = 0;

	if (!c) return false;
	if ((m = meta(c, META_CHT2, 0, NULL))) {
		char sub[32], pgsub[32];
		int postgap;

		if (sscanf(m, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s "
		              "PGSUB:%31s POSTGAP:%d", &track, type, sub, &nframes, &pregap,
		           pgtype, pgsub, &postgap) < 4)
			track = 0;
	} else if ((m = meta(c, META_CHTR, 0, NULL))) {
		char sub[32];

		if (sscanf(m, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d",
		           &track, type, sub, &nframes) < 4)
			track = 0;
	} else if ((m = meta(c, META_CHCD, 0, &len))) {
		/* The oldest form, binary: a count, then six big-endian words a
		 * track - its type by number, subcode type, data size, ... frames */
		static const char *const types[] = {
			"MODE1", "MODE1_RAW", "MODE2", "MODE2_FORM1", "MODE2_FORM2",
			"MODE2_FORM_MIX", "MODE2_RAW", "AUDIO"
		};
		const uint8_t *p = (const uint8_t *)m;

		if (len >= 4 + 24 && be32(p) >= 1 && be32(p + 4) < 8) {
			track = 1;
			snprintf(type, sizeof type, "%s", types[be32(p + 4)]);
			nframes = (int)be32(p + 4 + 16);
		}
	}
	free(m);
	if (track != 1 || nframes <= 0) return false;
	*datasize = track_datasize(type);
	if (!*datasize) return false;
	/* A pregap whose sectors are in the image ("V" before its type) comes
	 * before the track's own, which is where libretro's CHD reader starts the
	 * track too (libretro-common, chdstream_open) */
	*start = pgtype[0] == 'V' && pregap > 0 ? (uint32_t)pregap : 0;
	*frames = (uint32_t)nframes;
	return true;
}

/* -------------------------------------------------------------- open ----- */

chd *chd_open(const char *path)
{
	uint8_t h[124];
	chd *c;
	uint32_t length;

	c = calloc(1, sizeof *c);
	if (!c) return NULL;
	c->at = -1;
	c->f = fopen(path, "rb");
	if (!c->f) goto fail;
	if (fseeko(c->f, 0, SEEK_END) != 0) goto fail;
	c->fsize = (uint64_t)ftello(c->f);
	if (!read_at(c, 0, h, 16) || memcmp(h, "MComprHD", 8)) goto fail;
	length = be32(h + 8);
	c->version = (int)be32(h + 12);

	if (c->version == 5) {
		static const uint8_t none[20];

		if (length < 124 || !read_at(c, 0, h, 124)) goto fail;
		c->codec[0] = be32(h + 16); c->codec[1] = be32(h + 20);
		c->codec[2] = be32(h + 24); c->codec[3] = be32(h + 28);
		c->logical = be64(h + 32);
		c->mapoffset = be64(h + 40);
		c->metaoffset = be64(h + 48);
		c->hunkbytes = be32(h + 56);
		c->unitbytes = be32(h + 60);
		if (memcmp(h + 104, none, 20)) goto fail;     /* a diff: needs its parent */
		c->packed_map = c->codec[0] != 0;
	} else if (c->version == 4 || c->version == 3) {
		uint32_t flags;

		if (!read_at(c, 0, h, c->version == 4 ? 108 : 120)) goto fail;
		flags = be32(h + 16);
		if (flags & 1) goto fail;                     /* has a parent */
		c->codec[0] = be32(h + 20);
		c->hunkcount = be32(h + 24);
		c->logical = be64(h + 28);
		c->metaoffset = be64(h + 36);
		c->hunkbytes = be32(h + (c->version == 4 ? 44 : 76));
		c->mapoffset = c->version == 4 ? 108 : 120;
		/* No unit size in these: a CD's frames are what has a size */
		c->unitbytes = c->hunkbytes % CD_FRAME == 0 ? CD_FRAME : c->hunkbytes;
	} else {
		goto fail;
	}
	if (!c->hunkbytes || c->hunkbytes > 1024 * 1024 || !c->unitbytes ||
	    c->unitbytes > c->hunkbytes)
		goto fail;
	if (c->version == 5) {
		uint64_t n = (c->logical + c->hunkbytes - 1) / c->hunkbytes;

		if (n > 0xffffffffu) goto fail;
		c->hunkcount = (uint32_t)n;
	}
	c->hunk = malloc(c->hunkbytes);
	if (!c->hunk) goto fail;
	if (c->packed_map && !v5_map(c)) goto fail;
	return c;

fail:
	chd_close(c);
	return NULL;
}

void chd_close(chd *c)
{
	if (!c) return;
	if (c->f) fclose(c->f);
	free(c->type);
	free(c->val);
	free(c->mark);
	free(c->hunk);
	free(c->packed);
	free(c->scratch);
	free(c->huff_lookup);
	free(c);
}

uint32_t chd_hunkbytes(const chd *c) { return c ? c->hunkbytes : 0; }
uint32_t chd_unitbytes(const chd *c) { return c ? c->unitbytes : 0; }
uint64_t chd_logicalbytes(const chd *c) { return c ? c->logical : 0; }
