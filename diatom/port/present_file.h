/* SPDX-License-Identifier: GPL-3.0-only */
/* Where a test kit's Diatom present-time file is (diatom/port/brick.c's
 * gl_init reads it; ZyperOS's src/device/brick_kit.c seeds it): the card's
 * folder for this handheld's settings, as the card's start script names it
 * (sd/brick/device.sh: USERDATA_PATH, .userdata/<device>), so the Smart Pro,
 * which runs the Brick's port, has its own in .userdata/smartpro. With no
 * USERDATA_PATH (a port started by hand), the Brick's, as it always was.
 * make check-kitbuild holds it. No SDL. */
#ifndef DIATOM_PRESENT_FILE_H
#define DIATOM_PRESENT_FILE_H

#include <stdio.h>
#include <stdlib.h>

#define PRESENT_FILE_DIR_DEFAULT "/mnt/SDCARD/.userdata/brick"
#define PRESENT_FILE_NAME        "diatom-present"

/* The folder into `dir` and the file into `file`; either may be NULL */
static inline void present_file_paths(char *dir, size_t dn, char *file, size_t fn)
{
	const char *u = getenv("USERDATA_PATH");

	if (!u || !*u) u = PRESENT_FILE_DIR_DEFAULT;
	if (dir) snprintf(dir, dn, "%s", u);
	if (file) snprintf(file, fn, "%s/%s", u, PRESENT_FILE_NAME);
}

#endif
