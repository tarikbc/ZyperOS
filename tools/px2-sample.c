/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-sample: where a thread spends its time, without perf.
 *
 *   px2-sample TID [SECONDS] [HZ]       (defaults: 10 s at 100 Hz)
 *
 * plastron's kernel has no perf events, and a port that runs at 2 fps with one
 * core flat out needs an answer to "in what". This stops the thread with
 * ptrace HZ times a second, reads its program counter, and lets it go: a
 * statistical profile with nothing on the device but this binary. It prints
 * the share of samples per mapping (a library, or [anon] for JIT code such as
 * Mono's), then the hottest addresses as "library+offset", which a host can
 * turn into function names with addr2line against the unstripped build.
 *
 * JIT code has no file to name it by. A runtime that writes a perf map names
 * its own: Mono does with MONO_ENV_OPTIONS=--jitmap (/tmp/perf-PID.map), and
 * then the [anon] samples are counted by method as well.
 *
 * Each sample costs the thread a stop and a start, a few tens of
 * microseconds: at 100 Hz the program runs about a percent slower.
 *
 * Build: the plastron toolchain, no libraries. Run as root on the device. */
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

struct region { uint64_t lo, hi, off; char name[160]; };
struct hit { uint64_t pc; unsigned n; };

static struct region *regions;
static int nregions;

static void load_maps(int tid)
{
	char path[64], line[512];
	FILE *f;

	snprintf(path, sizeof path, "/proc/%d/maps", tid);
	if (!(f = fopen(path, "r"))) return;
	free(regions);
	regions = NULL;
	nregions = 0;
	while (fgets(line, sizeof line, f)) {
		struct region r = { 0 };
		char perms[8] = "", name[400] = "";
		unsigned long long lo, hi, off;

		if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %399[^\n]", &lo, &hi, perms, &off, name) < 4)
			continue;
		if (!strchr(perms, 'x')) continue;
		r.lo = lo; r.hi = hi; r.off = off;
		snprintf(r.name, sizeof r.name, "%s", name[0] ? name : "[anon]");
		regions = realloc(regions, sizeof *regions * (size_t)(nregions + 1));
		regions[nregions++] = r;
	}
	fclose(f);
}

static const struct region *find(uint64_t pc)
{
	for (int i = 0; i < nregions; i++)
		if (pc >= regions[i].lo && pc < regions[i].hi) return &regions[i];
	return NULL;
}

/* /tmp/perf-PID.map: "start size name", one JIT-compiled method a line */
struct jit { uint64_t lo, hi; char name[120]; unsigned n; };
static struct jit *jits;
static int njits;

static void load_jitmap(int pid)
{
	char path[64], line[512];
	FILE *f;

	snprintf(path, sizeof path, "/tmp/perf-%d.map", pid);
	if (!(f = fopen(path, "r"))) return;
	while (fgets(line, sizeof line, f)) {
		unsigned long long lo, size;
		char name[400];

		if (sscanf(line, "%llx %llx %399[^\n]", &lo, &size, name) != 3) continue;
		jits = realloc(jits, sizeof *jits * (size_t)(njits + 1));
		jits[njits].lo = lo;
		jits[njits].hi = lo + size;
		jits[njits].n = 0;
		snprintf(jits[njits].name, sizeof jits[njits].name, "%s", name);
		njits++;
	}
	fclose(f);
}

static int by_n(const void *a, const void *b)
{
	return (int)((const struct hit *)b)->n - (int)((const struct hit *)a)->n;
}

int main(int argc, char **argv)
{
	int tid, seconds, hz, cap = 4096, nhits = 0, total = 0;
	struct hit *hits;
	struct timespec gap;

	if (argc < 2) {
		fprintf(stderr, "usage: px2-sample TID [SECONDS] [HZ]\n");
		return 2;
	}
	tid = atoi(argv[1]);
	{
		/* The map is per process: the thread group's id */
		char path[64], line[128];
		FILE *f;
		int pid = tid;

		snprintf(path, sizeof path, "/proc/%d/status", tid);
		if ((f = fopen(path, "r"))) {
			while (fgets(line, sizeof line, f))
				if (sscanf(line, "Tgid: %d", &pid) == 1) break;
			fclose(f);
		}
		load_jitmap(pid);
	}
	seconds = argc > 2 ? atoi(argv[2]) : 10;
	hz = argc > 3 ? atoi(argv[3]) : 100;
	gap = (struct timespec){ 0, 1000000000L / hz };
	hits = calloc((size_t)cap, sizeof *hits);

	if (ptrace(PTRACE_SEIZE, tid, 0, 0) < 0) {
		perror("px2-sample: seize");
		return 1;
	}
	load_maps(tid);
	for (int s = 0; s < seconds * hz; s++) {
		uint64_t regs[34];         /* aarch64 user_pt_regs: x0-x30, sp, pc, pstate */
		struct iovec io = { regs, sizeof regs };
		int status, i;

		if (ptrace(PTRACE_INTERRUPT, tid, 0, 0) < 0) break;
		if (waitpid(tid, &status, __WALL) < 0) break;
		if (ptrace(PTRACE_GETREGSET, tid, (void *)NT_PRSTATUS, &io) == 0) {
			uint64_t pc = regs[32];

			for (i = 0; i < nhits && hits[i].pc != pc; i++) {}
			if (i == nhits && nhits < cap) hits[nhits++] = (struct hit){ pc, 0 };
			if (i < cap) hits[i].n++;
			total++;
		}
		ptrace(PTRACE_CONT, tid, 0, 0);
		nanosleep(&gap, NULL);
		if (s % hz == 0) load_maps(tid);    /* JIT and dlopen move things */
	}
	ptrace(PTRACE_DETACH, tid, 0, 0);
	if (!total) return 1;

	/* Per mapping */
	{
		struct { char name[160]; unsigned n; } lib[128];
		int nlib = 0;

		for (int i = 0; i < nhits; i++) {
			const struct region *r = find(hits[i].pc);
			const char *name = r ? r->name : "[unmapped]";
			int k;

			for (k = 0; k < nlib && strcmp(lib[k].name, name); k++) {}
			if (k == nlib && nlib < 128) {
				snprintf(lib[nlib].name, sizeof lib[nlib].name, "%s", name);
				lib[nlib++].n = 0;
			}
			if (k < 128) lib[k].n += hits[i].n;
		}
		printf("%d samples of thread %d\n\n", total, tid);
		for (int pass = 0; pass < nlib; pass++) {
			int best = -1;
			for (int k = 0; k < nlib; k++)
				if (lib[k].n && (best < 0 || lib[k].n > lib[best].n)) best = k;
			if (best < 0) break;
			printf("%5.1f%%  %s\n", 100.0 * lib[best].n / total, lib[best].name);
			lib[best].n = 0;
		}
	}

	/* JIT code by method, when the runtime wrote a map */
	if (njits) {
		for (int i = 0; i < nhits; i++)
			for (int k = 0; k < njits; k++)
				if (hits[i].pc >= jits[k].lo && hits[i].pc < jits[k].hi) {
					jits[k].n += hits[i].n;
					break;
				}
		printf("\nhottest methods (perf map):\n");
		for (int pass = 0; pass < 30; pass++) {
			int best = -1;
			for (int k = 0; k < njits; k++)
				if (jits[k].n && (best < 0 || jits[k].n > jits[best].n)) best = k;
			if (best < 0) break;
			printf("%5.1f%%  %s\n", 100.0 * jits[best].n / total, jits[best].name);
			jits[best].n = 0;
		}
	}

	/* The hottest addresses, as library+offset into the file */
	qsort(hits, (size_t)nhits, sizeof *hits, by_n);
	printf("\nhottest:\n");
	for (int i = 0; i < nhits && i < 40; i++) {
		const struct region *r = find(hits[i].pc);
		if (r && r->name[0] == '/')
			printf("%5.1f%%  %s+0x%llx\n", 100.0 * hits[i].n / total, r->name,
			       (unsigned long long)(hits[i].pc - r->lo + r->off));
		else
			printf("%5.1f%%  %s 0x%llx\n", 100.0 * hits[i].n / total,
			       r ? r->name : "[unmapped]", (unsigned long long)hits[i].pc);
	}
	return 0;
}
