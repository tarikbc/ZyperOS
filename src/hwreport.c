/* SPDX-License-Identifier: GPL-3.0-only */
/* The log pack's hardware report: see hwreport.h. */
#include "hwreport.h"

#include <stdio.h>
#include <string.h>
#ifdef __linux__
#include <sys/klog.h>
#endif

/* `s` into out, "-" when it is empty */
static const char *or_dash(const char *s)
{
	return s && *s ? s : "-";
}

size_t hwreport_build(const hw_facts *f, char *out, size_t n)
{
	size_t at = 0;
	int i;

#define PUT(...) do { \
		int k_ = snprintf(out + at, n - at, __VA_ARGS__); \
		if (k_ > 0) at += (size_t)k_ < n - at ? (size_t)k_ : n - at - 1; \
	} while (0)
	if (!n) return 0;
	out[0] = 0;
	PUT("Hardware:\n");
	PUT("  Panel: %s\n", or_dash(f->panel));
	PUT("  Inputs: ");
	for (i = 0; i < f->n_inputs; i++) PUT("%s%s", i ? ", " : "", f->inputs[i]);
	PUT("%s\n", f->n_inputs ? "" : "-");
	PUT("  GPU driver: %s\n", or_dash(f->gpu_kernel));
	PUT("  Graphics: %s\n", or_dash(f->gpu_user));
	PUT("  Wi-Fi: %s\n", or_dash(f->wifi));
	if (f->closed_total >= 0)
		PUT("  Closed files: %d of %d\n", f->closed_present, f->closed_total);
	if (f->leds[0]) PUT("  LEDs: %s\n", f->leds);
	if (f->motor_rail[0]) PUT("  Motor rail: %s\n", f->motor_rail);
	PUT("Boot times:\n");
	if (f->boot_times[0]) {
		const char *p = f->boot_times;

		while (*p) {
			size_t len = strcspn(p, "\n");

			if (len) PUT("  %.*s\n", (int)len, p);
			p += len;
			if (*p == '\n') p++;
		}
	} else {
		PUT("  -\n");
	}
#undef PUT
	return at;
}

int hw_inputs_from(const char *devices, char (*out)[64], int max)
{
	const char *p = devices;
	int n = 0;

	/* A device's name: N: Name="brick-pad" */
	while (p && n < max && (p = strstr(p, "N: Name=\""))) {
		const char *s = p + 9, *e = strchr(s, '"');

		if (!e) break;
		snprintf(out[n++], 64, "%.*s", (int)(e - s), s);
		p = e;
	}
	return n;
}

bool hw_klog_line(const char *klog, const char *needle, char *out, size_t n)
{
	const char *p = klog, *last = NULL;

	while (p && (p = strstr(p, needle))) {
		last = p;
		p += strlen(needle);
	}
	if (!last || !n) return false;
	snprintf(out, n, "%.*s", (int)strcspn(last, "\n"), last);
	return true;
}

void hw_klog_text(char *out, size_t n)
{
	if (!n) return;
	out[0] = 0;
#ifdef __linux__
	{
		int got = klogctl(3 /* SYSLOG_ACTION_READ_ALL */, out, (int)n - 1);

		out[got > 0 ? got : 0] = 0;
	}
#endif
}
