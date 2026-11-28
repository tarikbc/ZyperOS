/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's seek steps: src/seekstep.h */
#include "seekstep.h"

#include "keyrepeat.h"

double seek_step_for(bool tap, unsigned held_ms, double len)
{
	static const double step[] = { SEEK_TAP_S, 60, 300 };
	/* The share of the track a second of holding may cover, by stage */
	static const double share[] = { 1.0 / 16, 1.0 / 10, 1.0 / 6 };
	int st;
	double cap;

	if (tap) return SEEK_TAP_S;
	st = repeat_stage(held_ms);
	if (len <= 0) return step[st];
	/* A second of repeats is 1000 / REPEAT_RATE_MS steps */
	cap = len * share[st] * REPEAT_RATE_MS / 1000.0;
	if (cap < 1) cap = 1;
	return step[st] < cap ? step[st] : cap;
}
