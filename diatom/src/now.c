/* What Diatom runs now, in a file: see now.h. */
#include "now.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NOW_EVERY_US 250000

static char     g_path[512], g_tmp[520], g_ask[520];
static char     g_core[1024], g_rom[1024];
static double   g_fps;
static unsigned g_seq;
static uint64_t g_next_us;
static int      g_on;

static void paths(void)
{
	const char *p = getenv("DIATOM_NOW");

	snprintf(g_path, sizeof g_path, "%s", p && *p ? p : "/tmp/diatom-now");
	snprintf(g_tmp, sizeof g_tmp, "%s.tmp", g_path);
	snprintf(g_ask, sizeof g_ask, "%s.ask", g_path);
}

static void write_now(uint64_t now_us, long frames, uint64_t run_us,
                      double speed, uint64_t dropped)
{
	FILE *f = fopen(g_tmp, "w");

	if (!f) return;
	fprintf(f, "pid=%ld\ncore=%s\nrom=%s\nfps=%.4f\nseq=%u\nt_us=%llu\n"
	           "frames=%ld\nrun_us=%llu\nspeed=%g\ndropped=%llu\n",
	        (long)getpid(), g_core, g_rom, g_fps, ++g_seq,
	        (unsigned long long)now_us, frames, (unsigned long long)run_us,
	        speed, (unsigned long long)dropped);
	if (fclose(f) != 0 || rename(g_tmp, g_path) != 0) unlink(g_tmp);
}

void diatom_now_begin(const char *core, const char *rom, double fps)
{
	paths();
	snprintf(g_core, sizeof g_core, "%s", core ? core : "");
	snprintf(g_rom, sizeof g_rom, "%s", rom ? rom : "");
	/* One line each: a name with a newline in it would make another key */
	for (char *c = g_core; *c; c++) if (*c == '\n') *c = '?';
	for (char *c = g_rom; *c; c++) if (*c == '\n') *c = '?';
	g_fps = fps;
	g_seq = 0;
	g_next_us = 0;
	g_on = 1;
	write_now(0, 0, 0, 1.0, 0);
}

void diatom_now_turn(uint64_t now_us, long frames, uint64_t run_us,
                     double speed, uint64_t dropped)
{
	if (!g_on || now_us < g_next_us) return;
	g_next_us = now_us + NOW_EVERY_US;
	if (access(g_ask, F_OK) == 0)
		write_now(now_us, frames, run_us, speed, dropped);
}

void diatom_now_end(void)
{
	if (!g_on) return;
	g_on = 0;
	unlink(g_path);
}
