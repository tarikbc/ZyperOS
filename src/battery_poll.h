/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_BATTERY_POLL_H
#define ZYPEROS_BATTERY_POLL_H

#include <stdbool.h>
#include <stdint.h>

/* One reading shared by the corner, Diatom's warning and Auto Off. Zero-init.
 * Shutdown deliberately keeps its own two fresh readings in battery_empty. */
typedef struct {
	uint32_t capacity_at, charger_at;
	int pct;
	bool capacity_primed, charger_primed;
	bool valid, charging;
} battery_poll;

/* Call from input/tick loops, not just from drawing. True only when the
 * displayed state changes. A NULL field asks the reader to skip that field.
 * Time is monotonic milliseconds; elapsed subtraction survives wraparound. */
bool battery_poll_update(battery_poll *b, uint32_t now,
                         bool (*read_battery)(int *pct, bool *charging));

#endif
