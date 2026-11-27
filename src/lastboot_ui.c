/* SPDX-License-Identifier: GPL-3.0-only */
/* The last restart ZyperOS made, kept in About ZyperOS (src/lastboot.h,
 * DESIGN.md 11.70) */
#include "lastboot.h"

#include <stdio.h>
#include <time.h>

#include "platform.h"

bool lastboot_about(char *out, size_t n)
{
	char path[512];
	lastboot kept;

	snprintf(path, sizeof path, "%s/%s", P_USERDATA, LASTBOOT_FILE);
	if (!lastboot_read(path, &kept) || !lastboot_row(&kept, (long)time(NULL))) return false;
	/* The code alone: with the day after it ("vor 3 Wochen") German's row
	 * did not fit the panel */
	lastboot_code(&kept, out, n);
	return true;
}
