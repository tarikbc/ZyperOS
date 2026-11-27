/* SPDX-License-Identifier: GPL-3.0-only */
/* The last start's summary, read and said once: src/lastboot.h */
#include "lastboot.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NBSP "\xc2\xa0"

/* A value longer than its field is left out, not cut: a time, a version or
 * a device that lost its end reads as another one. The kernel's last line
 * is only for the log, so it keeps what fits, marked as cut with "...". */
static void field(char *dst, size_t size, const char *v, size_t n, bool part)
{
	if (n < size) memcpy(dst, v, n + 1);
	else if (part && size > 4) {
		memcpy(dst, v, size - 4);
		memcpy(dst + size - 4, "...", 4);
	}
}

/* crashlog's words, and the letters */
static char reason_of(const char *v)
{
	static const struct { const char *w; char r; } W[] = {
		{ "watchdog", 'W' }, { "launcher", 'W' }, { "panic", 'P' }, { "lockup", 'P' }, { "rcu", 'P' },
		{ "oops", 'P' }, { "hung_task", 'T' }, { "rootwait", 'R' }, { "reset", 'X' },
	};
	size_t i;

	if (v[0] && !v[1] && strchr("WPTRX", v[0])) return v[0];
	for (i = 0; i < sizeof W / sizeof W[0]; i++)
		if (!strcmp(v, W[i].w)) return W[i].r;
	return 0;
}

static bool stage_of(const char *v, char out[4])
{
	static const char *const CODES[] = { "K1", "K2", "C1", "C2", "C3", "U1", "U2", "L1", "H" };
	static const struct { const char *w; const char *c; } W[] = {
		{ "kernel", "K1" }, { "kernel-card", "K2" }, { "mount", "C1" }, { "format", "C2" },
		{ "check", "C3" }, { "fsck", "C3" }, { "switch", "U1" }, { "update", "U1" },
		{ "copy", "U2" }, { "seed", "U2" }, { "launcher", "L1" }, { "launch", "L1" },
		{ "home", "H" }, { "playing", "H" },
	};
	size_t i;

	for (i = 0; i < sizeof CODES / sizeof CODES[0]; i++)
		if (!strcmp(v, CODES[i])) { snprintf(out, 4, "%s", CODES[i]); return true; }
	for (i = 0; i < sizeof W / sizeof W[0]; i++)
		if (!strcmp(v, W[i].w)) { snprintf(out, 4, "%s", W[i].c); return true; }
	return false;
}

bool lastboot_parse(const char *text, lastboot *out)
{
	const char *p = text;
	bool stage = false;

	memset(out, 0, sizeof *out);
	out->kernel_time = -1.0;
	while (p && *p) {
		const char *end = strchr(p, '\n'), *eq;
		size_t n = end ? (size_t)(end - p) : strlen(p), kn;
		char key[24], val[160];

		eq = memchr(p, '=', n);
		if (eq && (kn = (size_t)(eq - p)) > 0 && kn < sizeof key) {
			size_t vn = n - kn - 1;

			if (vn >= sizeof val) vn = sizeof val - 1;
			memcpy(key, p, kn);
			key[kn] = '\0';
			memcpy(val, eq + 1, vn);
			val[vn] = '\0';
			while (vn && (val[vn - 1] == '\r' || val[vn - 1] == ' ')) val[--vn] = '\0';
			if (!strcmp(key, "reason")) out->reason = reason_of(val);
			else if (!strcmp(key, "stage")) stage = stage_of(val, out->stage);
			else if (!strcmp(key, "kernel_time")) {
				char *e;
				double t = strtod(val, &e);

				if (e != val && t >= 0.0 && t < 1e7) out->kernel_time = t;
			} else if (!strcmp(key, "time") && strcmp(val, "-")) field(out->time, sizeof out->time, val, vn, false);
			else if (!strcmp(key, "version")) field(out->version, sizeof out->version, val, vn, false);
			else if (!strcmp(key, "device")) field(out->device, sizeof out->device, val, vn, false);
			else if (!strcmp(key, "last")) field(out->last, sizeof out->last, val, vn, true);
			else if (!strcmp(key, "seen")) out->seen = strtol(val, NULL, 10);
		}
		p = end ? end + 1 : NULL;
	}
	out->valid = stage && out->reason;
	return out->valid;
}

bool lastboot_read(const char *path, lastboot *out)
{
	char buf[1024];
	size_t n = 0;
	FILE *f = fopen(path, "r");

	memset(out, 0, sizeof *out);
	if (!f) return false;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return lastboot_parse(buf, out);
}

void lastboot_code(const lastboot *lb, char *out, size_t n)
{
	double t = lb->kernel_time;

	if (!n) return;
	/* No-break spaces: the code is one word to the line breaker, never
	 * "K1W ·" on one line and "0.6 s" on the next */
	if (t < 0.0) snprintf(out, n, "%s%c", lb->stage, lb->reason);
	else if (t < 9.95) snprintf(out, n, "%s%c" NBSP "\xc2\xb7" NBSP "%.1f" NBSP "s", lb->stage, lb->reason, t);
	else snprintf(out, n, "%s%c" NBSP "\xc2\xb7" NBSP "%.0f" NBSP "s", lb->stage, lb->reason, t);
}

bool lastboot_same(const lastboot *a, const lastboot *b)
{
	if (!a->valid || !b->valid) return false;
	return !strcmp(a->stage, b->stage) && a->reason == b->reason &&
	       (long)(a->kernel_time * 100 + .5) == (long)(b->kernel_time * 100 + .5) &&
	       !strcmp(a->time, b->time);
}

bool lastboot_froze(const lastboot *lb)
{
	return lb->valid && !strcmp(lb->stage, "H");
}

bool lastboot_write(const char *path, const lastboot *lb)
{
	char tmp[600];
	FILE *f;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.new", path);
	if (!(f = fopen(tmp, "w"))) return false;
	fprintf(f, "reason=%c\nstage=%s\n", lb->reason, lb->stage);
	if (lb->kernel_time >= 0.0) fprintf(f, "kernel_time=%.2f\n", lb->kernel_time);
	fprintf(f, "time=%s\nversion=%s\ndevice=%s\nlast=%s\nseen=%ld\n",
	        lb->time[0] ? lb->time : "-", lb->version, lb->device, lb->last, lb->seen);
	ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
	ok = fclose(f) == 0 && ok;
	if (ok) ok = rename(tmp, path) == 0;
	if (!ok) remove(tmp);
	return ok;
}

bool lastboot_due(const lastboot *run, const lastboot *kept, long now, lastboot *keep)
{
	if (!run->valid || lastboot_same(run, kept)) return false;
	*keep = *run;
	keep->seen = now;
	return true;
}

bool lastboot_row(const lastboot *kept, long now)
{
	if (!kept->valid || kept->seen <= 0) return false;
	return now < kept->seen || now - kept->seen < (long)LASTBOOT_KEEP_DAYS * 86400L;
}
