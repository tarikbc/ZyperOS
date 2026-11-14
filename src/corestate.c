/* SPDX-License-Identifier: GPL-3.0-only */
/* See corestate.h. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "corestate.h"

/* diatom/src/statefile.c's header: 8 + 4 + 4 + 64 + 32 bytes, no padding on
 * any target, and in version 2 (packed states, 2026-11-12) two fields after
 * those: the core's name is at 16 in both */
#define HDR_MAGIC   "DIATOMST"
#define HDR_NAME_AT 16
#define HDR_NAME_N  64

bool cst_writer(const char *state_path, char *buf, size_t n)
{
	unsigned char h[HDR_NAME_AT + HDR_NAME_N];
	FILE *f;
	size_t got;
	uint32_t version;

	if (!buf || n == 0) return false;
	buf[0] = '\0';
	if (!state_path || !(f = fopen(state_path, "rb"))) return false;
	got = fread(h, 1, sizeof h, f);
	fclose(f);
	if (got < sizeof h || memcmp(h, HDR_MAGIC, 8) != 0) return false;
	/* Little-endian, as every device Diatom runs on writes it */
	version = (uint32_t)h[8] | (uint32_t)h[9] << 8 | (uint32_t)h[10] << 16 |
	          (uint32_t)h[11] << 24;
	if (version != 1 && version != 2) return false;
	snprintf(buf, n, "%.*s", HDR_NAME_N, (const char *)h + HDR_NAME_AT);
	return true;
}

static const struct { const char *core, *library; } LIBRARY[] = {
	{ "parallel_n64",     "ParaLLEl N64" },
	{ "mupen64plus_next", "Mupen64Plus-Next" },
};

const char *cst_library(const char *core)
{
	size_t i;

	for (i = 0; core && i < sizeof LIBRARY / sizeof LIBRARY[0]; i++)
		if (!strcmp(LIBRARY[i].core, core)) return LIBRARY[i].library;
	return NULL;
}

bool cst_other_core(const char *writer, const char *running, const char *cores)
{
	char one[64];
	const char *p = cores, *e;

	if (!writer || !writer[0] || !running || !cores) return false;
	/* The core running now wrote it: a resume as ever */
	if (cst_library(running) && !strcmp(cst_library(running), writer)) return false;
	while (p && *p) {
		size_t len;

		while (*p == ' ' || *p == '\t') p++;
		e = strchr(p, ',');
		len = e ? (size_t)(e - p) : strlen(p);
		while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
		snprintf(one, sizeof one, "%.*s", (int)len, p);
		if (strcmp(one, running) && cst_library(one) && !strcmp(cst_library(one), writer))
			return true;
		p = e ? e + 1 : NULL;
	}
	return false;
}
