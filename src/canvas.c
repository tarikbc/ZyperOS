/* SPDX-License-Identifier: GPL-3.0-only */
/* The canvas's width (src/canvas.h) */
#include "canvas.h"

/* A 4:3 handheld's until canvas_init: the 1024 every screen was made in */
int canvas_w = 1024;

int canvas_w_for(int aspect_w, int aspect_h)
{
	long w;

	if (aspect_w <= 0 || aspect_h <= 0) return 1024;
	/* To the nearest 8 units: (CANVAS_H * aw / ah) / 8, rounded, times 8 */
	w = ((long)CANVAS_H * aspect_w * 2 + (long)aspect_h * 8) / ((long)aspect_h * 16) * 8;
	if (w < CANVAS_W_MIN) w = CANVAS_W_MIN;
	if (w > CANVAS_W_MAX) w = CANVAS_W_MAX;
	return (int)w;
}

void canvas_init(const device_profile *p)
{
	canvas_w = p ? canvas_w_for(p->screen.aspect_w, p->screen.aspect_h) : 1024;
}
