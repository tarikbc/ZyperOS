/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SPAWN_H
#define ZYPEROS_SPAWN_H

#include <stddef.h>

/* Run argv[0] with argv, by fork and execv and never a shell: what reaches
 * here is an SSID or a device name from the air, and this process is root
 * (src/wifi.c says why). Its stdout and stderr go into `out`, which is always
 * NUL-terminated (none kept when out is NULL); the rest is read and dropped.
 *
 * Returns its exit status, -1 if it could not be run, and -2 if it was still
 * going `timeout_s` seconds on and was killed. 0 or less is no deadline, for
 * a tool that always exits on its own. */
int spawn_capture(char *const argv[], char *out, size_t cap, int timeout_s);

#endif
