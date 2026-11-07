/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_ABOUT_H
#define ZYPEROS_ABOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "menu.h"

/* About's second page. Storage is the card's filesystem, in bytes. Battery
 * capacities are the controller's microamp-hours; zero means unavailable.
 * Full/design is an estimate, not an independently measured wear reading. */
typedef struct {
	bool storage_known;
	uint64_t total_bytes, used_bytes, free_bytes;
	uint64_t full_uah, design_uah;
} about_info;

#define ABOUT_DETAIL_ROWS 6
#define ABOUT_VALUE_LEN 48
typedef struct { char value[ABOUT_DETAIL_ROWS][ABOUT_VALUE_LEN]; } about_values;

/* statvfs's block counts, converted with overflow and consistency checks.
 * Free is space available to the application; used excludes reserved blocks. */
bool about_storage_counts(about_info *info, uint64_t unit, uint64_t blocks,
                          uint64_t free_blocks, uint64_t available_blocks);
/* Refresh both sources; a missing source clears its previous values. A NULL
 * battery directory is a host or device without charge capacity readings. */
void about_read(about_info *info, const char *card, const char *battery_dir);
/* Six read-only rows, in the approved Details order. GB is decimal; capacity
 * is rounded to mAh and grouped; the health estimate is capped at 100%. */
int about_detail_rows(const about_info *info, menu_row rows[ABOUT_DETAIL_ROWS],
                       about_values *buf);
#endif
