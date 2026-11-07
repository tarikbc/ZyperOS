/* SPDX-License-Identifier: GPL-3.0-only */
/* When to say "Getting hot". See src/heat.h. */
#include "heat.h"

#include <string.h>

void heat_init(heat_state *h)
{
	memset(h, 0, sizeof *h);
	h->mc = -1;
	h->armed = true;
}

void heat_note(heat_state *h, unsigned now, int mc)
{
	h->mc = mc;
	if (mc >= 0 && (unsigned)mc >= HEAT_WARN_MC) {
		if (h->hot < HEAT_SEEN) h->hot++;
	} else {
		/* Under the trip again: a warning that waited for the home goes
		 * unsaid, as there is nothing to say any more */
		h->hot = 0;
		h->due = false;
	}
	if (mc >= 0 && (unsigned)mc < HEAT_REARM_MC) h->armed = true;
	if (h->armed && h->hot >= HEAT_SEEN &&
	    (!h->shown || now - h->shown_at >= HEAT_QUIET_MS))
		h->due = true;
}

bool heat_poll(heat_state *h, unsigned now, bool (*read)(int *mc))
{
	int mc;

	if (h->primed && now - h->read_at < HEAT_POLL_MS) return false;
	h->primed = true;
	h->read_at = now;
	if (!read || !read(&mc)) return false;
	heat_note(h, now, mc);
	return true;
}

bool heat_take(heat_state *h, unsigned now)
{
	if (!h->due) return false;
	h->due = false;
	h->armed = false;
	h->shown = true;
	h->shown_at = now;
	return true;
}
