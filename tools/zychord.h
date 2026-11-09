/* MENU+START, the way out of anything (tools/zyhome.c): the chord, apart
 * from the device, so tools/zyhome-check.c can drive it with made-up times.
 *
 * Both held together for ZYCHORD_HOLD_MS, in either order, fires once. It
 * fires again only after both are let go: a hand still on the buttons while
 * the home comes back is not a second press. */
#ifndef ZYCHORD_H
#define ZYCHORD_H

#include <stdbool.h>

#define ZYCHORD_HOLD_MS 1000

typedef struct {
	bool menu, start;          /* down now */
	long since;                /* when both went down, or -1 */
	bool fired;                /* until both are up again */
} zychord;

static inline void zychord_init(zychord *c)
{
	c->menu = c->start = false;
	c->since = -1;
	c->fired = false;
}

/* One button changed: MENU (is_menu) or START, down or up, at `now` */
static inline void zychord_key(zychord *c, bool is_menu, bool down, long now)
{
	if (is_menu) c->menu = down;
	else c->start = down;
	if (c->menu && c->start) {
		if (c->since < 0) c->since = now;
	} else {
		c->since = -1;
		if (!c->menu && !c->start) c->fired = false;
	}
}

/* Whether the chord fires at `now`: true once per hold */
static inline bool zychord_due(zychord *c, long now)
{
	if (c->fired || c->since < 0 || now - c->since < ZYCHORD_HOLD_MS) return false;
	c->fired = true;
	return true;
}

/* How long a poll may sleep before the chord can be due: -1 for no limit */
static inline int zychord_wait(const zychord *c, long now)
{
	long left;

	if (c->fired || c->since < 0) return -1;
	left = ZYCHORD_HOLD_MS - (now - c->since);
	return left < 0 ? 0 : (int)left;
}

#endif
