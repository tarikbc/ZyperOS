/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-corestate: a game's resume when its auto state was left by the
 * shelf's other core (src/corestate.h). Diatom's state header read back from
 * made-up files, each core by the name it gives itself, and the decision: an
 * N64 game left on Mupen64Plus-Next starts fresh on ParallelN64, and the other
 * way round, while a state of the core running now, of a core the shelf does
 * not list, or no Diatom state at all goes to Diatom as before. Plain C,
 * scratch files under build-native/corestate. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/corestate.h"

static int failures;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); failures++; } } while (0)

static const char *D = "build-native/corestate";

/* A state as Diatom writes it (diatom/src/save.c): the header, then a little
 * payload */
static void state(const char *path, const char *magic, uint32_t version, const char *core)
{
	unsigned char h[8 + 4 + 4 + 64 + 32 + 16];
	FILE *f = fopen(path, "wb");

	memset(h, 0, sizeof h);
	memcpy(h, magic, 8);
	h[8] = (unsigned char)version;
	h[9] = (unsigned char)(version >> 8);
	h[12] = 16;
	snprintf((char *)h + 16, 64, "%s", core);
	snprintf((char *)h + 80, 32, "%s", "1.0");
	if (!f) return;
	fwrite(h, 1, sizeof h, f);
	fclose(f);
}

int main(void)
{
	static const char *N64 = "parallel_n64,mupen64plus_next";
	char p[256], buf[96];

	mkdir("build-native", 0755);
	mkdir(D, 0755);
	printf("corestate: a resume left by the shelf's other core\n");

	CHECK(!strcmp(cst_library("parallel_n64"), "ParaLLEl N64"), "ParallelN64 names itself ParaLLEl N64");
	CHECK(!strcmp(cst_library("mupen64plus_next"), "Mupen64Plus-Next"), "Mupen64Plus-Next its own name");
	CHECK(!cst_library("flycast2021") && !cst_library(NULL), "a core not listed has none");

	snprintf(p, sizeof p, "%s/mupen.state", D);
	state(p, "DIATOMST", 1, "Mupen64Plus-Next");
	CHECK(cst_writer(p, buf, sizeof buf) && !strcmp(buf, "Mupen64Plus-Next"), "the writer read from the header");
	CHECK(cst_other_core(buf, "parallel_n64", N64), "left on Mupen64Plus-Next, started on ParallelN64: fresh");
	CHECK(!cst_other_core(buf, "mupen64plus_next", N64), "left on it, started on it: a resume");

	snprintf(p, sizeof p, "%s/parallel.state", D);
	state(p, "DIATOMST", 1, "ParaLLEl N64");
	CHECK(cst_writer(p, buf, sizeof buf) && cst_other_core(buf, "mupen64plus_next", N64),
	      "and the other way round");
	CHECK(!cst_other_core(buf, "parallel_n64", N64), "ParallelN64's own: a resume");
	CHECK(!cst_other_core(buf, "parallel_n64", "parallel_n64"), "a shelf of one core: never");
	CHECK(cst_other_core(buf, "mupen64plus_next", " mupen64plus_next , parallel_n64 "),
	      "the list's spaces do not count");

	snprintf(p, sizeof p, "%s/other.state", D);
	state(p, "DIATOMST", 1, "Snes9x 2010");
	CHECK(cst_writer(p, buf, sizeof buf) && !cst_other_core(buf, "parallel_n64", N64),
	      "a core the shelf does not list: to Diatom as before");

	snprintf(p, sizeof p, "%s/v2.state", D);
	state(p, "DIATOMST", 2, "Mupen64Plus-Next");
	CHECK(!cst_writer(p, buf, sizeof buf) && !buf[0], "another header version: not read");
	snprintf(p, sizeof p, "%s/raw.state", D);
	state(p, "RASTATE1", 1, "Mupen64Plus-Next");
	CHECK(!cst_writer(p, buf, sizeof buf), "not a Diatom state: not read");
	snprintf(p, sizeof p, "%s/none.state", D);
	remove(p);
	CHECK(!cst_writer(p, buf, sizeof buf), "no state: not read");
	CHECK(!cst_other_core("", "parallel_n64", N64) && !cst_other_core(NULL, "parallel_n64", N64),
	      "and nothing read is no reason to start fresh");

	printf(failures ? "corestate: %d failed\n" : "ok: a resume from the other core starts fresh\n", failures);
	return failures ? 1 : 0;
}
