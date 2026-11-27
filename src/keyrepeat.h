/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_KEYREPEAT_H
#define ZYPEROS_KEYREPEAT_H

/* Hold-to-repeat: the first repeat REPEAT_DELAY_MS after a key goes down,
 * then one every REPEAT_RATE_MS for as long as it stays down. Short delay and
 * a quick rate: this is a shelf you sweep along, and waiting on a key repeat
 * is the most obvious kind of slow.
 *
 * The launcher's (in_repeat, src/platform.c), and portkeys' too: a volume key
 * held over a port repeats on this clock (tools/pkkeys.c), so a hold does the
 * same thing over Stardew Valley as on the shelf. Diatom keeps a copy of the
 * two numbers for a hold in a game (diatom/src/levels.c); change one, change
 * the other. Plain defines and no SDL, so portkeys can include it. */
#define REPEAT_DELAY_MS 300
#define REPEAT_RATE_MS  90

/* A hold that goes further the longer it lasts: a step on the repeats
 * before REPEAT_FAR_MS held, a longer one up to REPEAT_FARTHER_MS, and a
 * longer again after. The clock stays the one above; each repeat covers
 * more. MP4's seek (seek_step: ten seconds, a minute, five minutes) and
 * MENU > Storage's list (home_held_step: twice, then four times as far). */
#define REPEAT_FAR_MS     1000
#define REPEAT_FARTHER_MS 3000

static inline int repeat_stage(unsigned held_ms)
{
	return held_ms < REPEAT_FAR_MS ? 0 : held_ms < REPEAT_FARTHER_MS ? 1 : 2;
}

#endif
