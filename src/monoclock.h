/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MONOCLOCK_H
#define ZYPEROS_MONOCLOCK_H

#include <time.h>

/* Milliseconds on CLOCK_MONOTONIC, for what times sockets, children and
 * replies rather than frames: httpd.c's idle connections, Hare's lockout, the
 * hotspot's log, curl's requests, the scrapers' log lines, Muse's writes.
 *
 * Not plat_now_ms, which is SDL_GetTicks: that would drag SDL into every
 * check that links these, and Muse has no SDL at all. Header-only for the
 * same reason: a check needs no source of its own for it, and Muse, a
 * separate program, includes it by path (src/muse/pcm.c). */
static inline long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The same in 32 bits, as plat_now_ms is: it wraps every 49.7 days, which an
 * unsigned difference of two of them comes through. */
static inline unsigned now_ms32(void)
{
	return (unsigned)now_ms();
}

#endif
