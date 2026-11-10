/* SPDX-License-Identifier: GPL-3.0-only */
/* See keyhints.h. */
#include "keyhints.h"

#include <stdio.h>
#include <string.h>

int keyhints_rows(const key_hint *h, int n, int maxw, int gap, keyhint_width_fn width,
                  int start[KEYHINTS_MAX + 1])
{
	int i, rows = 0, used = 0;

	if (n > KEYHINTS_MAX) n = KEYHINTS_MAX;
	if (n <= 0) { start[0] = 0; return 0; }
	start[rows++] = 0;
	for (i = 0; i < n; i++) {
		int w = width(h[i].key, h[i].label);

		if (i > start[rows - 1] && used + gap + w > maxw && rows < KEYHINTS_MAX) {
			start[rows++] = i;
			used = w;
		} else {
			used += (i > start[rows - 1] ? gap : 0) + w;
		}
	}
	start[rows] = n;
	return rows;
}

size_t keyhints_pack(const key_hint *h, int n, char *out, size_t on)
{
	size_t used;
	int i;

	if (!on) return 0;
	used = (size_t)snprintf(out, on, "%s", KEYHINTS_MARK);
	for (i = 0; i < n && used < on; i++)
		used += (size_t)snprintf(out + used, on - used, "%s\x1f%s\x1e",
		                         h[i].key ? h[i].key : "", h[i].label ? h[i].label : "");
	if (used >= on) { out[0] = '\0'; return 0; }
	return used;
}

int keyhints_unpack(const char *text, char *buf, size_t bn, key_hint *out, int max)
{
	size_t ml = strlen(KEYHINTS_MARK);
	char *p, *end;
	int n = 0;

	if (!text || strncmp(text, KEYHINTS_MARK, ml) || !bn) return 0;
	snprintf(buf, bn, "%s", text + ml);
	p = buf;
	while (*p && n < max) {
		char *sep = strchr(p, '\x1f');

		if (!sep) break;
		end = strchr(sep + 1, '\x1e');
		if (!end) break;
		*sep = '\0';
		*end = '\0';
		out[n].key = p;
		out[n].label = sep + 1;
		n++;
		p = end + 1;
	}
	return n;
}
