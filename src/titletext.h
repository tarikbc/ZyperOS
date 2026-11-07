/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TITLETEXT_H
#define ZYPEROS_TITLETEXT_H

#include <stddef.h>
#include <string.h>

/* Display text only. Keep whole UTF-8 characters and collapse ASCII white
 * space. Invalid UTF-8 rejects the title; a full buffer keeps a whole prefix.
 * No folding, punctuation removal or transliteration of catalog titles. */
static inline void title_text(const char *src, char *out, size_t n)
{
	const unsigned char *p = (const unsigned char *)(src ? src : "");
	size_t used = 0;
	int space = 0;

	if (!out || !n) return;
	while (*p) {
		size_t len = 1, i;
		unsigned cp = *p;

		if (*p <= 0x20 || *p == 0x7f) { space = used != 0; p++; continue; }
		if (*p >= 0xc2 && *p <= 0xdf) { len = 2; cp &= 0x1f; }
		else if (*p >= 0xe0 && *p <= 0xef) { len = 3; cp &= 0x0f; }
		else if (*p >= 0xf0 && *p <= 0xf4) { len = 4; cp &= 7; }
		else if (*p >= 0x80) { used = 0; break; }
		for (i = 1; i < len; i++) {
			if ((p[i] & 0xc0) != 0x80) { out[0] = '\0'; return; }
			cp = (cp << 6) | (p[i] & 0x3f);
		}
		if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
		    (len == 4 && cp < 0x10000) || cp > 0x10ffff ||
		    (cp >= 0xd800 && cp <= 0xdfff)) { used = 0; break; }
		if (used + (size_t)space + len >= n) break;
		if (space) out[used++] = ' ';
		memmove(out + used, p, len);
		used += len;
		p += len;
		space = 0;
	}
	out[used] = '\0';
}

#endif
