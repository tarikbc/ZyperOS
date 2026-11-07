/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See linebreak.h. */
#include "linebreak.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Words a text is read as; the rest of a longer one is its last word, spaces
 * and all, and is cut where it does not fit. A game's name has a handful. */
#define LB_WORDS 48

void lb_fit(const char *src, char *dst, size_t dstn, int maxw,
            lb_width_fn width, void *ctx)
{
	size_t n;

	if (!dst || !dstn) return;
	snprintf(dst, dstn, "%s", src ? src : "");
	if (width(ctx, dst) <= maxw) return;

	n = strlen(dst);
	while (n > 0) {
		n--;
		while (n > 0 && ((unsigned char)dst[n] & 0xC0) == 0x80) n--;
		if (n + 3 >= dstn) continue;
		memcpy(dst + n, "...", 4);
		if (width(ctx, dst) <= maxw) return;
		dst[n] = '\0';
	}
}

/* `s` with any UTF-8 character its end cuts in half taken off: for a run
 * copied into a line by its bytes, which can end inside one. */
static void lb_whole(char *s)
{
	size_t n = strlen(s), p = n;
	int need;

	while (p > 0 && n - p < 4 && ((unsigned char)s[p - 1] & 0xC0) == 0x80) p--;
	if (p == 0) return;
	p--;                                 /* the last character's first byte */
	need = (unsigned char)s[p] < 0x80 ? 1 : ((unsigned char)s[p] & 0xE0) == 0xC0 ? 2
	     : ((unsigned char)s[p] & 0xF0) == 0xE0 ? 3 : 4;
	if (p + (size_t)need > n) s[p] = '\0';
}

/* The text as its words, one space between each, a word too wide for the box
 * already cut: words [i, j) on one line are buf from at[i] to end[j - 1]. */
typedef struct {
	char buf[LB_WORDS * LB_LINE];
	int at[LB_WORDS], end[LB_WORDS], n;
	int maxw;
	lb_width_fn width;
	void *ctx;
	int span[LB_WORDS][LB_WORDS + 1];        /* -1: not measured yet */
	int best[LB_WORDS + 1][LB_MAX_LINES + 1]; /* -1: not worked out yet */
	int next[LB_WORDS + 1][LB_MAX_LINES + 1];
} lb_text;

/* Words [i, j) as one line, into `out`; false when they are longer than a
 * line can hold. */
static int lb_line(const lb_text *t, int i, int j, char out[LB_LINE])
{
	int len = t->end[j - 1] - t->at[i];

	if (len >= LB_LINE) return 0;
	memcpy(out, t->buf + t->at[i], (size_t)len);
	out[len] = '\0';
	return 1;
}

/* How wide words [i, j) are on one line, measured once; INT_MAX for a line
 * too long to hold. */
static int lb_span(lb_text *t, int i, int j)
{
	char line[LB_LINE];

	if (t->span[i][j] < 0)
		t->span[i][j] = lb_line(t, i, j, line) ? t->width(t->ctx, line) : INT_MAX;
	return t->span[i][j];
}

/* The narrowest the widest line can be when words [i, n) are broken into
 * exactly `k` lines, none wider than the box, and where the first of those
 * lines ends (next); INT_MAX when no such break exists. Each line takes at
 * least a word. A line only gets wider as it takes more words, so the first
 * line stops growing at the first that would not fit. Of breaks as narrow as
 * each other, the first found: the one whose first line is shortest. */
static int lb_best(lb_text *t, int i, int k)
{
	int r = INT_MAX, j;

	if (t->best[i][k] >= 0) return t->best[i][k];
	if (k == 1) {
		int w = i < t->n ? lb_span(t, i, t->n) : INT_MAX;

		if (w <= t->maxw) { r = w; t->next[i][k] = t->n; }
	} else {
		for (j = i + 1; j <= t->n - (k - 1); j++) {
			int w = lb_span(t, i, j), rest, m;

			if (w > t->maxw) break;
			rest = lb_best(t, j, k - 1);
			if (rest == INT_MAX) continue;
			m = w > rest ? w : rest;
			if (m < r) { r = m; t->next[i][k] = j; }
		}
	}
	t->best[i][k] = r;
	return r;
}

/* Reads `text` into `t`: its words, one space apart, each too wide for the
 * box on its own cut to fit it. */
static void lb_read(lb_text *t, const char *text)
{
	size_t len = 0;
	const char *p = text;

	t->n = 0;
	while (*p) {
		char word[LB_LINE];
		size_t wl;

		while (*p == ' ') p++;
		if (!*p) break;
		if (t->n == LB_WORDS - 1) {
			/* The last word holds the rest, its spaces as they are */
			wl = strlen(p);
			while (wl > 0 && p[wl - 1] == ' ') wl--;
		} else {
			wl = strcspn(p, " ");
		}
		snprintf(word, sizeof word, "%.*s", (int)wl, p);
		lb_whole(word);
		p += wl;
		if (t->width(t->ctx, word) > t->maxw) {
			char cut[LB_LINE];

			lb_fit(word, cut, sizeof cut, t->maxw, t->width, t->ctx);
			snprintf(word, sizeof word, "%s", cut);
		}
		wl = strlen(word);
		if (len + wl + 2 > sizeof t->buf) break;
		if (t->n) t->buf[len++] = ' ';
		t->at[t->n] = (int)len;
		memcpy(t->buf + len, word, wl);
		len += wl;
		t->end[t->n++] = (int)len;
	}
	t->buf[len] = '\0';
}

int lb_balance(const char *text, int maxw, int cap, lb_width_fn width,
               void *ctx, char lines[][LB_LINE])
{
	lb_text t;   /* 26 KB */
	int k, i, n;

	if (!text || cap < 1) return 0;
	if (cap > LB_MAX_LINES) cap = LB_MAX_LINES;
	t.maxw = maxw;
	t.width = width;
	t.ctx = ctx;
	lb_read(&t, text);
	if (!t.n) return 0;
	memset(t.span, -1, sizeof t.span);
	memset(t.best, -1, sizeof t.best);

	/* The fewest lines that fit, and the narrowest break into that many */
	for (k = 1; k <= cap; k++) {
		if (lb_best(&t, 0, k) == INT_MAX) continue;
		for (i = 0, n = 0; n < k; n++) {
			int j = t.next[i][k - n];

			lb_line(&t, i, j, lines[n]);
			i = j;
		}
		return k;
	}

	/* No `cap` lines hold it: as many words as fit on each line but the
	 * last, which takes the rest and is cut to fit. Every word fits a line
	 * on its own, cut as it is. */
	for (i = 0, n = 0; n < cap - 1 && i < t.n; n++) {
		int j = i + 1;

		while (j < t.n && lb_span(&t, i, j + 1) <= maxw) j++;
		lb_line(&t, i, j, lines[n]);
		i = j;
	}
	if (i < t.n) {
		char rest[LB_LINE];

		if (!lb_line(&t, i, t.n, rest)) {
			snprintf(rest, sizeof rest, "%.*s", LB_LINE - 1, t.buf + t.at[i]);
			lb_whole(rest);
		}
		lb_fit(rest, lines[n], LB_LINE, maxw, width, ctx);
		n++;
	}
	return n;
}

void lb_edges(const char *text, int at, lb_width_fn width, void *ctx,
              int *in, int *out)
{
	size_t n = text ? strlen(text) : 0, i = 0;
	char small[LB_LINE], *buf = small;
	int w = 0, before = 0;

	*in = *out = 0;
	if (!n) return;
	/* A name fits the stack; a longer text is copied to the heap, and one
	 * that cannot be leaves the edge where it was asked, as it was before
	 * there was a rule: a window that cuts a letter rather than one that
	 * hides the text for want of a few bytes. */
	if (n >= sizeof small && !(buf = malloc(n + 1))) {
		*in = *out = at;
		return;
	}
	memcpy(buf, text, n + 1);
	/* The text's first characters, one more each time, measured whole: the
	 * width of a run is where the next character starts, kerning and all,
	 * which is what a letter's end is to a window. */
	while (i < n) {
		i++;
		while (i < n && ((unsigned char)text[i] & 0xC0) == 0x80) i++;
		buf[i] = '\0';
		w = width(ctx, buf);
		buf[i] = text[i];
		if (w <= at) before = w;
		if (w >= at) break;
	}
	if (buf != small) free(buf);
	*in = before;
	*out = w;
}
