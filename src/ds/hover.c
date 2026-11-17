/* SPDX-License-Identifier: GPL-3.0-only */
/* What an option list's cursor shows (src/ds/hover.h) */
#include "ds/hover.h"

#include <stddef.h>

/* Runs showing a choice now (ds_hover_showing) */
static int g_showing;

void ds_hover_begin(ds_hover_run *r, const ds_hover *h, void *ctx, int kept, int at, unsigned now)
{
	r->h = h;
	r->ctx = ctx;
	r->kept = kept;
	r->at = at;
	r->shown = -1;
	r->since = now;
}

static void hover_at(ds_hover_run *r, int k, unsigned now)
{
	if (k != r->at) {
		r->at = k;
		r->since = now;
	}
	if (k == r->kept || k < 0) {
		/* Back on the kept one: what is kept, at once (no rest: the
		 * kept one is not something new to start) */
		if (r->shown >= 0) {
			/* A list that plays its own moves is heard coming back too:
			 * the kept one shown, which puts it back with its sound */
			if (r->h->own_sound && k >= 0) r->h->show(r->ctx, k);
			else if (r->h->undo) r->h->undo(r->ctx, r->kept);
			r->shown = -1;
			g_showing--;
		}
		return;
	}
	if (k == r->shown) return;
	if (now - r->since < r->h->rest_ms) return;
	r->h->show(r->ctx, k);
	if (r->shown < 0) g_showing++;
	r->shown = k;
}

void ds_hover_at(ds_hover_run *r, int k, unsigned now)
{
	if (!r->h) return;
	if (r->h->show) hover_at(r, k, now);
	if (r->h->tick) r->h->tick(r->ctx);
}

void ds_hover_end(ds_hover_run *r)
{
	if (r->h && r->shown >= 0) {
		if (r->h->undo) r->h->undo(r->ctx, r->kept);
		g_showing--;
	}
	r->shown = -1;
	if (r->h && r->h->end) r->h->end(r->ctx);
	r->h = NULL;
}

bool ds_hover_showing(void)
{
	return g_showing > 0;
}

bool ds_hover_quiet(const ds_hover *h)
{
	return h && h->show && h->own_sound;
}
