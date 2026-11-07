/* SPDX-License-Identifier: GPL-3.0-only */
#include "battery_poll.h"

#include <stddef.h>

bool battery_poll_update(battery_poll *b, uint32_t now,
                         bool (*read_battery)(int *pct, bool *charging))
{
	bool valid = b->valid, charging = b->charging;
	int pct = b->pct;

	/* Capacity is slow-changing; reading the cable must not read it too. */
	if (!b->capacity_primed || now - b->capacity_at >= 5000) {
		b->capacity_primed = true;
		b->capacity_at = now;
		b->pct = 0;
		b->valid = read_battery(&b->pct, NULL);
	}
	/* 200 ms plus an input-loop pass (normally 8 or 16 ms) leaves room
	 * inside the 250 ms UI target. This does not bound kernel propagation. */
	if (!b->charger_primed || now - b->charger_at >= 200) {
		b->charger_primed = true;
		b->charger_at = now;
		b->charging = false;
		if (!read_battery(NULL, &b->charging)) b->charging = false;
	}
	return valid != b->valid || charging != b->charging ||
	       (b->valid && pct != b->pct);
}
