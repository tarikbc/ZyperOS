/* SPDX-License-Identifier: GPL-3.0-only */
/* Random Game's pick. See src/randgame.h. */
#include "randgame.h"

#include <time.h>

/* The k-th game across the shelves in use, as (shelf, game) */
static bool nth(const int *count, const bool *use, int n, long k, int *shelf, int *game)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!use[i] || count[i] <= 0) continue;
		if (k < count[i]) {
			*shelf = i;
			*game = (int)k;
			return true;
		}
		k -= count[i];
	}
	return false;
}

bool rg_pick(const int *count, const bool *use, int n, uint32_t r,
             int avoid_shelf, int avoid_game, int *shelf, int *game)
{
	long total = 0, k;
	int i;

	for (i = 0; i < n; i++)
		if (use[i] && count[i] > 0) total += count[i];
	if (total <= 0) return false;
	k = (long)(r % (uint32_t)total);
	if (!nth(count, use, n, k, shelf, game)) return false;
	/* The same game again is the next one along instead */
	if (total > 1 && *shelf == avoid_shelf && *game == avoid_game)
		return nth(count, use, n, (k + 1) % total, shelf, game);
	return true;
}

uint32_t rg_random(void)
{
	static uint32_t n;
	struct timespec ts;
	uint32_t x;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	x = (uint32_t)ts.tv_nsec ^ (uint32_t)ts.tv_sec * 2654435761u ^ ++n * 0x9e3779b9u;
	/* A few rounds of xorshift, so the low bits the modulo reads are mixed */
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return x;
}
