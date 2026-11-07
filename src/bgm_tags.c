/* SPDX-License-Identifier: GPL-3.0-only */
/* A menu music track's name from its tags. See src/bgm_tags.h.
 *
 * ID3v2 is read from the file a frame at a time, by the sizes in the frame
 * headers, rather than the whole tag read in and searched: the tag of a
 * soundtrack rip is mostly its cover, and the three frames wanted here are a
 * few dozen bytes. */
#include "bgm_tags.h"

#include <stdint.h>
#include <string.h>
#include <strings.h>

/* A frame's text is read when it is at most this long. A longer one is no
 * title anyone reads in a pop-up, and is passed over like a picture. */
#define FRAME_MAX 1024

/* ---- text, to UTF-8 ------------------------------------------------------------ */

/* A field being written: `full` once a character did not fit, so a short one
 * after it is not let in out of order. */
typedef struct {
	char  *s;
	size_t n, len;
	bool   full;
} sbuf;

static void put(sbuf *b, uint32_t cp)
{
	unsigned char u[4];
	size_t k;

	if (b->full) return;
	/* A tab or a line break in a title is a space on one line */
	if (cp < 0x20 || cp == 0x7f) cp = ' ';
	if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
	if (cp < 0x80) {
		u[0] = (unsigned char)cp; k = 1;
	} else if (cp < 0x800) {
		u[0] = (unsigned char)(0xc0 | cp >> 6);
		u[1] = (unsigned char)(0x80 | (cp & 0x3f)); k = 2;
	} else if (cp < 0x10000) {
		u[0] = (unsigned char)(0xe0 | cp >> 12);
		u[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3f));
		u[2] = (unsigned char)(0x80 | (cp & 0x3f)); k = 3;
	} else {
		u[0] = (unsigned char)(0xf0 | cp >> 18);
		u[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3f));
		u[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3f));
		u[3] = (unsigned char)(0x80 | (cp & 0x3f)); k = 4;
	}
	if (b->len + k >= b->n) { b->full = true; return; }
	memcpy(b->s + b->len, u, k);
	b->len += k;
	b->s[b->len] = '\0';
}

static void latin1(sbuf *b, const uint8_t *p, size_t len)
{
	size_t i;

	for (i = 0; i < len && p[i]; i++) put(b, p[i]);
}

static void utf16(sbuf *b, const uint8_t *p, size_t len, bool be)
{
	size_t i;

	for (i = 0; i + 1 < len; i += 2) {
		uint32_t u = be ? (uint32_t)p[i] << 8 | p[i + 1] : (uint32_t)p[i + 1] << 8 | p[i];

		if (!u) break;
		if (u >= 0xd800 && u <= 0xdbff && i + 3 < len) {
			uint32_t l = be ? (uint32_t)p[i + 2] << 8 | p[i + 3]
			                : (uint32_t)p[i + 3] << 8 | p[i + 2];

			if (l >= 0xdc00 && l <= 0xdfff) {
				put(b, 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00));
				i += 2;
				continue;
			}
		}
		put(b, u);
	}
}

/* UTF-8, a byte that does not start a whole sequence taken as Latin-1: a tag
 * that says UTF-8 and is not is common, and its accents still read */
static void utf8(sbuf *b, const uint8_t *p, size_t len)
{
	size_t i = 0;

	while (i < len && p[i]) {
		uint32_t c = p[i], cp;
		size_t k = c >= 0xf0 && c < 0xf5 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 && c < 0xe0 ? 2 : 1, j;

		if (c < 0x80 || k == 1 || c >= 0xf5) { put(b, c); i++; continue; }
		cp = c & (0x7f >> k);
		for (j = 1; j < k; j++) {
			if (i + j >= len || (p[i + j] & 0xc0) != 0x80) break;
			cp = cp << 6 | (p[i + j] & 0x3f);
		}
		if (j < k) { put(b, c); i++; continue; }
		put(b, cp);
		i += k;
	}
}

/* Spaces off both ends, in place */
static void trim(char *s)
{
	size_t n = strlen(s), a = 0;

	while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
	while (s[a] == ' ') a++;
	if (a) memmove(s, s + a, n - a + 1);
}

/* An ID3v2 text frame's body - its encoding byte, then the text - into out.
 * 2.4 may hold several values apart by a NUL; the first is the one. */
static void text_frame(const uint8_t *d, size_t len, char *out, size_t n)
{
	sbuf b = { out, n, 0, false };

	if (!len || !n) return;
	out[0] = '\0';
	switch (d[0]) {
	case 1:   /* UTF-16 after a byte order mark; little-endian without one */
		if (len >= 3 && d[1] == 0xfe && d[2] == 0xff) utf16(&b, d + 3, len - 3, true);
		else if (len >= 3 && d[1] == 0xff && d[2] == 0xfe) utf16(&b, d + 3, len - 3, false);
		else utf16(&b, d + 1, len - 1, false);
		break;
	case 2:
		utf16(&b, d + 1, len - 1, true);
		break;
	case 3:
		if (len >= 4 && d[1] == 0xef && d[2] == 0xbb && d[3] == 0xbf) utf8(&b, d + 4, len - 4);
		else utf8(&b, d + 1, len - 1);
		break;
	default:
		latin1(&b, d + 1, len - 1);
		break;
	}
	trim(out);
}

/* ---- ID3v2 ------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t syncsafe(const uint8_t *p)
{
	return (uint32_t)(p[0] & 0x7f) << 21 | (uint32_t)(p[1] & 0x7f) << 14 |
	       (uint32_t)(p[2] & 0x7f) << 7 | (p[3] & 0x7f);
}

/* Unsynchronisation undone in place, every 0xFF 0x00 back to 0xFF: the new
 * length */
static size_t resync(uint8_t *d, size_t len)
{
	size_t i, o = 0;

	for (i = 0; i < len; i++) {
		d[o++] = d[i];
		if (d[i] == 0xff && i + 1 < len && d[i + 1] == 0x00) i++;
	}
	return o;
}

/* Where a frame goes: the title, the artist, the album artist (the artist
 * when there is none) or the album. NULL for any other frame. */
static char *field(bgm_tags *t, char *albart, const uint8_t *id, bool v22)
{
	static const char *const v2[] = { "TT2", "TP1", "TP2", "TAL" };
	static const char *const v3[] = { "TIT2", "TPE1", "TPE2", "TALB" };
	char *const to[] = { t->title, t->artist, albart, t->album };
	int i;

	for (i = 0; i < 4; i++)
		if (!memcmp(id, v22 ? v2[i] : v3[i], v22 ? 3 : 4)) return to[i];
	return NULL;
}

/* The frames between `pos` and `end`, the file at `pos` */
static void frames(FILE *f, unsigned ver, unsigned tflags, long pos, long end,
                   bgm_tags *t, char *albart)
{
	const long hl = ver == 2 ? 6 : 10;
	uint8_t buf[FRAME_MAX];

	while (pos + hl <= end) {
		uint8_t fh[10];
		uint32_t size;
		unsigned ff = 0;
		bool skip;
		char *dst;

		if (fread(fh, 1, (size_t)hl, f) != (size_t)hl || fh[0] == 0)
			return;                   /* the padding, with no frame after it */
		size = ver == 2 ? (uint32_t)fh[3] << 16 | (uint32_t)fh[4] << 8 | fh[5]
		     : ver == 3 ? be32(fh + 4) : syncsafe(fh + 4);
		if (ver >= 3) ff = (unsigned)fh[8] << 8 | fh[9];
		pos += hl;
		if (size == 0 || (long)size > end - pos) return;
		/* Compressed or encrypted: 2.3's flags, then 2.4's */
		skip = ver == 3 ? (ff & 0x00c0) != 0 : ver == 4 ? (ff & 0x000c) != 0 : false;
		dst = field(t, albart, fh, ver == 2);
		if (dst && !dst[0] && !skip && size <= FRAME_MAX) {
			size_t len = size, at = 0;

			if (fread(buf, 1, len, f) != len) return;
			if (ver == 3 && (ff & 0x0020)) at = 1;                /* its group */
			if (ver == 4) {
				if (ff & 0x0040) at += 1;                     /* its group */
				if (ff & 0x0001) at += 4;                     /* its length */
				if (at < len && ((ff & 0x0002) || (tflags & 0x80)))
					len = at + resync(buf + at, len - at);
			}
			if (at < len) text_frame(buf + at, len - at, dst, BGM_TAG_MAX);
		} else if (fseek(f, pos + (long)size, SEEK_SET) != 0) {
			return;
		}
		pos += (long)size;
	}
}

bool bgm_id3v2(FILE *f, bgm_tags *t)
{
	uint8_t h[10];
	char albart[BGM_TAG_MAX] = "";
	unsigned ver, flags;
	long end;

	if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3) || h[3] == 0xff ||
	    h[4] == 0xff || (h[6] | h[7] | h[8] | h[9]) & 0x80) {
		rewind(f);
		return false;
	}
	ver = h[3];
	flags = h[5];
	end = 10 + (long)syncsafe(h + 6);
	/* 2.2's compression flag, and 2.3's unsynchronised body, say the frame
	 * sizes are not where the frames end: the tag is passed over unread */
	if (ver >= 2 && ver <= 4 && !(ver == 2 && (flags & 0x40)) &&
	    !(ver == 3 && (flags & 0x80))) {
		long pos = 10;
		bool ok = true;

		/* The extended header: 2.3's size leaves itself out, 2.4's does not */
		if (ver >= 3 && (flags & 0x40)) {
			uint8_t e[4];

			ok = fread(e, 1, 4, f) == 4;
			if (ok) pos += ver == 4 ? (long)syncsafe(e) : 4 + (long)be32(e);
			ok = ok && fseek(f, pos, SEEK_SET) == 0;
		}
		if (ok) frames(f, ver, flags, pos, end, t, albart);
	}
	if (!t->artist[0] && albart[0]) memcpy(t->artist, albart, sizeof t->artist);
	if (ver == 4 && (flags & 0x10)) end += 10;       /* a footer too */
	if (fseek(f, end, SEEK_SET) != 0) rewind(f);
	return true;
}

/* ---- ID3v1 ------------------------------------------------------------------- */

static void v1_field(const uint8_t *p, char *out)
{
	sbuf b = { out, BGM_TAG_MAX, 0, false };

	if (out[0]) return;
	latin1(&b, p, 30);
	trim(out);
}

void bgm_id3v1(FILE *f, bgm_tags *t)
{
	long at = ftell(f);
	uint8_t b[128];

	if (at < 0) return;
	if (fseek(f, -128, SEEK_END) == 0 && fread(b, 1, sizeof b, f) == sizeof b &&
	    !memcmp(b, "TAG", 3)) {
		v1_field(b + 3, t->title);
		v1_field(b + 33, t->artist);
		v1_field(b + 63, t->album);
	}
	fseek(f, at, SEEK_SET);
}

/* ---- Vorbis comments ----------------------------------------------------------- */

void bgm_vorbis_tags(char *const *list, int n, bgm_tags *t)
{
	static const char *const keys[] = { "TITLE", "ARTIST", "ALBUMARTIST", "ALBUM" };
	char albart[BGM_TAG_MAX] = "";
	char *const to[] = { t->title, t->artist, albart, t->album };
	int i, k;

	for (i = 0; list && i < n; i++) {
		const char *c = list[i], *eq;

		if (!c || !(eq = strchr(c, '='))) continue;
		for (k = 0; k < 4; k++) {
			sbuf b = { to[k], BGM_TAG_MAX, 0, false };

			if (strlen(keys[k]) != (size_t)(eq - c) ||
			    strncasecmp(c, keys[k], (size_t)(eq - c)) || to[k][0])
				continue;
			utf8(&b, (const uint8_t *)eq + 1, strlen(eq + 1));
			trim(to[k]);
		}
	}
	if (!t->artist[0] && albart[0]) memcpy(t->artist, albart, sizeof t->artist);
}

/* ---- the words ----------------------------------------------------------------- */

void bgm_song_label(const bgm_tags *t, const char *file,
                    char *head, size_t hn, char *body, size_t bn)
{
	const char *h = t && t->album[0] ? t->album
	              : t && t->artist[0] ? t->artist : BGM_SONG_HEAD_NONE;

	if (hn) snprintf(head, hn, "%s", h);
	if (!bn) return;
	if (t && t->title[0]) snprintf(body, bn, "%s", t->title);
	else bgm_tidy_name(file ? file : "", body, bn);
}

/* A leading track number's length, its separator and the spaces after it
 * included, or 0 for none: 1 to 3 digits and a '.', '-', '_' or ')', or a
 * zero-padded number and a space ("01 Title"), with something after. A
 * number and a space alone is a name's own ("7 Seas", "99 Luftballons"). */
static size_t track_number(const char *s)
{
	size_t d = 0, k;

	while (s[d] >= '0' && s[d] <= '9') d++;
	if (d == 0 || d > 3) return 0;
	if (!(s[d] == '.' || s[d] == '-' || s[d] == '_' || s[d] == ')' ||
	      (s[d] == ' ' && d >= 2 && s[0] == '0')))
		return 0;
	k = d;
	while (s[k] == ' ' || s[k] == '.' || s[k] == '-' || s[k] == '_' || s[k] == ')') k++;
	return s[k] ? k : 0;
}

void bgm_tidy_name(const char *file, char *out, size_t n)
{
	char base[256];
	const char *dot = strrchr(file, '.'), *s = base;
	size_t len = dot && dot != file ? (size_t)(dot - file) : strlen(file), o = 0, k;
	int pass;

	if (!n) return;
	if (len >= sizeof base) len = sizeof base - 1;
	memcpy(base, file, len);
	base[len] = '\0';
	/* Twice, for a disc and a track: "1-01 Title" */
	for (pass = 0; pass < 2 && (k = track_number(s)); pass++) s += k;
	for (; *s && o + 1 < n; s++) {
		char c = *s == '_' ? ' ' : *s;

		if (c == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
		out[o++] = c;
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = '\0';
	/* Cut on a character's edge when it did not fit */
	while (o > 0 && s[0] && ((unsigned char)out[o - 1] & 0xc0) == 0x80) o--;
	if (o > 0 && s[0] && (unsigned char)out[o - 1] >= 0xc0) o--;
	out[o] = '\0';
	if (!o) snprintf(out, n, "%.*s", (int)len, file);
}
