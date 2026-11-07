/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The boot screen's theme words: see boottheme.h. */
#include "boottheme.h"

#include <stdio.h>
#include <string.h>

/* A zone name the kernel's line and U-Boot's env import carry as one word:
 * the zone database's own letters, digits and _ + - / */
static int zone_ok(const char *z)
{
	if (!z || !*z || strlen(z) > 48) return 0;
	for (; *z; z++)
		if (!((*z >= 'a' && *z <= 'z') || (*z >= 'A' && *z <= 'Z') || (*z >= '0' && *z <= '9') ||
		      *z == '_' || *z == '+' || *z == '-' || *z == '/'))
			return 0;
	return 1;
}

void boottheme_words(const char *appearance, const char *zone,
                     char *line, size_t n, char *file, size_t fn)
{
	const char *t = appearance && (!strcmp(appearance, "dark") || !strcmp(appearance, "auto"))
	                ? appearance : "light";
	const char *z = zone_ok(zone) ? zone : "-";

	if (line && n) snprintf(line, n, "zytheme=%s zyzone=%s", t, z);
	if (file && fn) snprintf(file, fn, "zytheme=%s\nzyzone=%s\n", t, z);
}

/* The value of word `key` on the line, up to the next space, into out */
static int word_of(const char *cmdline, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	const char *p = cmdline;

	while (p && (p = strstr(p, key))) {
		if ((p == cmdline || p[-1] == ' ') && p[kl] == '=') {
			size_t len = strcspn(p + kl + 1, " \t\r\n");
			if (len >= n) len = n - 1;
			memcpy(out, p + kl + 1, len);
			out[len] = 0;
			return 1;
		}
		p += kl;
	}
	return 0;
}

void boottheme_from_cmdline(const char *cmdline, char *line, size_t n)
{
	char t[16] = "", z[64] = "";
	int has_t = word_of(cmdline ? cmdline : "", "zytheme", t, sizeof t);
	int has_z = word_of(cmdline ? cmdline : "", "zyzone", z, sizeof z);

	if (!n) return;
	if (!has_t && !has_z) { line[0] = 0; return; }
	snprintf(line, n, "zytheme=%s zyzone=%s", t, z);
}
