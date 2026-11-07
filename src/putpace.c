/* SPDX-License-Identifier: GPL-3.0-only */
/* See putpace.h. */
#include "putpace.h"

static float mpx(long px)
{
	return (float)(px < PUTPACE_LEAST_PX ? PUTPACE_LEAST_PX : px) / 1e6f;
}

void putpace_init(putpace *p, float share)
{
	p->share = share;
	p->per_mpx = 0.0f;
	p->spent = 0.0f;
	p->put = 0;
}

void putpace_frame(putpace *p)
{
	p->spent = 0.0f;
	p->put = 0;
}

float putpace_guess(const putpace *p, long px)
{
	return px > 0 ? p->per_mpx * mpx(px) : 0.0f;
}

bool putpace_room(const putpace *p, long px)
{
	return p->put == 0 || p->spent + putpace_guess(p, px) <= p->share;
}

void putpace_took(putpace *p, long px, float ms)
{
	if (ms < 0.0f) ms = 0.0f;
	p->spent += ms;
	p->put++;
	if (px > 0) {
		float now = ms / mpx(px);

		if (now > p->per_mpx || p->per_mpx == 0.0f) p->per_mpx = now;
		else p->per_mpx += (now - p->per_mpx) / 8.0f;
	}
}
