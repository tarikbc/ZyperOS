/* SPDX-License-Identifier: GPL-3.0-only */
/* See pace.h. */
#include "pace.h"

#include <stdint.h>

void pace_init(pace *p, unsigned now)
{
	p->drawn = false;
	p->key = 0;
	p->drawn_at = now;
	p->busy_at = now;
	p->seen = true;
}

bool pace_due(pace *p, unsigned key, unsigned now, unsigned redraw_at, bool busy)
{
	if (busy) {
		p->busy_at = now;
		p->seen = true;
	}
	if (!p->drawn || busy || key != p->key) return true;
	/* The redraw clock's moment, come: a difference, so the clock may wrap */
	if (redraw_at != PACE_NEVER && (int)(now - redraw_at) >= 0) return true;
	return now - p->drawn_at >= PACE_HEARTBEAT_MS;
}

void pace_drawn(pace *p, unsigned key, unsigned now)
{
	p->drawn = true;
	p->key = key;
	p->drawn_at = now;
}

unsigned pace_rest(const pace *p, unsigned now, unsigned redraw_at, unsigned cap)
{
	unsigned rest = cap, beat = now - p->drawn_at;

	rest = beat >= PACE_HEARTBEAT_MS ? 1 : PACE_HEARTBEAT_MS - beat < rest ? PACE_HEARTBEAT_MS - beat : rest;
	if (redraw_at != PACE_NEVER) {
		int left = (int)(redraw_at - now);

		if (left <= 0) return 1;
		if ((unsigned)left < rest) rest = (unsigned)left;
	}
	return rest ? rest : 1;
}

bool pace_ram_idle(const pace *p, unsigned now)
{
	return p->seen && now - p->busy_at >= PACE_RAM_IDLE_MS;
}

void pace_screen_init(pace_screen *s, unsigned now)
{
	s->light = PACE_LIT;
	s->since = now;
	s->moving = false;
	s->clients = 0;
}

pace_light pace_screen_step(pace_screen *s, unsigned now, int auto_off_s, bool on_cable,
                            bool held, bool press, bool moving, int clients, bool *spent)
{
	bool wake = press || (moving && !s->moving) || clients > s->clients;

	*spent = press && s->light != PACE_LIT;
	s->moving = moving;
	s->clients = clients;
	if (wake || !on_cable || auto_off_s <= 0 || held) {
		s->since = now;
		s->light = PACE_LIT;
	} else if (now - s->since >= (unsigned)auto_off_s * 1000u) {
		s->light = moving ? PACE_DIM : PACE_OFF;
	}
	return s->light;
}

static unsigned pace_text(unsigned h, const char *s)
{
	return (uintptr_t)s < 4096 ? pace_int(h, (int)(uintptr_t)s) : pace_str(h, s);
}

unsigned pace_menu(unsigned h, const char *heading, const menu_row *rows, int n, int sel)
{
	int i;

	h = pace_text(h, heading);
	for (i = 0; rows && i < n; i++) {
		h = pace_text(h, rows[i].label);
		h = pace_text(h, rows[i].value);
		h = pace_int(h, rows[i].live);
	}
	h = pace_int(h, n);
	return pace_int(h, sel);
}

unsigned pace_str(unsigned h, const char *s)
{
	h = h * 33u + (s ? 1u : 2u);
	for (; s && *s; s++) h = h * 33u + (unsigned char)*s;
	return h;
}

unsigned pace_int(unsigned h, int v)
{
	return h * 1000003u ^ (unsigned)v;
}
