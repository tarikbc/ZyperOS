/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Search Games' matching. See src/search.h. */
#include "search.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SR_TEXT 512

/* The letter a two-byte UTF-8 Latin-1 character is, accent off: U+00C0 to
 * U+00FF, the second byte 0x80-0xBF after 0xC3. 0 for one that is no letter. */
static char latin1_base(unsigned char second)
{
	static const char map[] =
		"aaaaaaaceeeeiiii" "dnooooo ouuuuyts"   /* C3 80 - C3 9F */
		"aaaaaaaceeeeiiii" "dnooooo ouuuuyty";  /* C3 A0 - C3 BF */

	if (second < 0x80 || second > 0xBF) return 0;
	return map[second - 0x80] == ' ' ? 0 : map[second - 0x80];
}

void sr_fold(const char *in, char *out, size_t n)
{
	size_t o = 0;
	bool space = true;     /* nothing yet, so a leading separator is dropped */

	if (!n) return;
	while (*in && o + 1 < n) {
		unsigned char c = (unsigned char)*in++;
		char x = 0;

		if (c >= 'A' && c <= 'Z') x = (char)(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) x = (char)c;
		else if (c == 0xC3 && *in) x = latin1_base((unsigned char)*in++);
		else if (c >= 0x80) {
			/* Another script's letter: kept as it is, byte for byte, so a
			 * Japanese title still finds itself */
			x = (char)c;
		}
		if (x) {
			out[o++] = x;
			space = false;
		} else if (!space) {
			out[o++] = ' ';
			space = true;
		}
	}
	if (o && out[o - 1] == ' ') o--;
	out[o] = '\0';
}

/* Whether `w` (len bytes) starts a word somewhere in `s` */
static bool starts_word(const char *s, const char *w, size_t len)
{
	const char *p;

	for (p = s; *p; p++)
		if ((p == s || p[-1] == ' ') && !strncmp(p, w, len)) return true;
	return false;
}

/* `s` holds `w` (len bytes) anywhere */
static bool holds(const char *s, const char *w, size_t len)
{
	const char *p;

	for (p = s; *p; p++)
		if (!strncmp(p, w, len)) return true;
	return false;
}

/* How well `item` answers the folded query `q`: 0 for not at all. */
static int sr_score(const char *q, const sr_item *item)
{
	char t[SR_TEXT], f[SR_TEXT];
	const char *w = q;
	int score = 400, in_title = 1;

	if (!q || !*q) return 0;
	sr_fold(item->title ? item->title : "", t, sizeof t);
	sr_fold(item->name ? item->name : "", f, sizeof f);
	while (*w) {
		size_t len = strcspn(w, " ");

		if (holds(t, w, len)) {
			if (starts_word(t, w, len)) score += 20;
		} else if (holds(f, w, len)) {
			in_title = 0;
		} else {
			return 0;
		}
		w += len;
		while (*w == ' ') w++;
	}
	if (!in_title) return 100;
	/* What was typed is how the title begins */
	if (!strncmp(t, q, strlen(q))) score += 400;
	return score;
}

typedef struct {
	sr_item it;
	int score;
} hit;

static int hit_cmp(const void *x, const void *y)
{
	const hit *a = x, *b = y;
	int c;

	if (a->score != b->score) return b->score - a->score;
	c = strcasecmp(a->it.title ? a->it.title : "", b->it.title ? b->it.title : "");
	if (c) return c;
	if (a->it.shelf != b->it.shelf) return a->it.shelf - b->it.shelf;
	return a->it.game - b->it.game;
}

int sr_search(const char *query, const sr_item *items, int n, sr_item *out, int max)
{
	char q[SR_TEXT];
	hit *h;
	int i, found = 0;

	sr_fold(query ? query : "", q, sizeof q);
	if (!q[0] || n <= 0) return 0;
	if (!(h = malloc((size_t)n * sizeof *h))) return 0;
	for (i = 0; i < n; i++) {
		int s = sr_score(q, &items[i]);

		if (s > 0) {
			h[found].it = items[i];
			h[found].score = s;
			found++;
		}
	}
	qsort(h, (size_t)found, sizeof *h, hit_cmp);
	for (i = 0; i < found && i < max; i++) out[i] = h[i].it;
	free(h);
	return found;
}
