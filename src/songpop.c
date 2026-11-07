/* SPDX-License-Identifier: GPL-3.0-only */
/* The song pop-up's life. See src/songpop.h. */
#include "songpop.h"

#include <stdio.h>
#include <string.h>

#include "design.h"

void songpop_show(songpop *p, const char *head, const char *body,
                  unsigned hold_ms, unsigned now)
{
	if (!p->on) p->shown_at = now;
	snprintf(p->head, sizeof p->head, "%s", head ? head : "");
	snprintf(p->body, sizeof p->body, "%s", body ? body : "");
	p->changed_at = now;
	p->hold_ms = hold_ms;
	p->serial++;
	p->on = true;
}

void songpop_hide(songpop *p)
{
	p->on = false;
}

bool songpop_at(songpop *p, unsigned now, float *alpha, float *rise)
{
	unsigned since_shown = now - p->shown_at, since_change = now - p->changed_at;
	float in, out = 1.0f;

	if (!p->on) return false;
	if (since_change >= p->hold_ms + DS_MS_FADE_OUT) {
		p->on = false;
		return false;
	}
	in = ds_out(since_shown >= DS_MS_QUICK ? 1.0f : (float)since_shown / DS_MS_QUICK);
	if (since_change > p->hold_ms)
		out = 1.0f - (float)(since_change - p->hold_ms) / DS_MS_FADE_OUT;
	*alpha = in < out ? in : out;
	*rise = (1.0f - in) * SONGPOP_RISE;
	return true;
}

unsigned songpop_wait(const songpop *p, unsigned now)
{
	unsigned since_shown = now - p->shown_at, since_change = now - p->changed_at;

	if (since_shown < DS_MS_QUICK || since_change >= p->hold_ms) return 0;
	return p->hold_ms - since_change;
}

/* The first of `a`, `b` and `c` that says something and is not `not` */
static const char *first_of(const char *a, const char *b, const char *c, const char *not)
{
	const char *const each[3] = { a, b, c };
	int i;

	for (i = 0; i < 3; i++)
		if (each[i] && each[i][0] && strcmp(each[i], not)) return each[i];
	return BGM_SONG_HEAD_NONE;
}

void songpop_mp4_words(const char *title, const char *artist, const char *album,
                       const char *chapter, bool book,
                       char *head, size_t hn, char *body, size_t bn)
{
	const char *b = chapter && chapter[0] ? chapter : title ? title : "";
	const char *h = chapter && chapter[0] ? first_of(title, album, artist, b)
	              : book                  ? first_of(album, artist, NULL, b)
	                                      : first_of(artist, album, NULL, b);

	if (hn) snprintf(head, hn, "%s", h);
	if (bn) snprintf(body, bn, "%s", b);
}
