/* SPDX-License-Identifier: GPL-3.0-only */
/* See usblog.h. */
#include "usblog.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void usblog_load(usblog *l, const char *text)
{
	const char *p = text;

	memset(l, 0, sizeof *l);
	while (p && *p) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);

		if (len) {
			if (l->n == USBLOG_MAX) {
				memmove(l->line[0], l->line[1], sizeof l->line[0] * (USBLOG_MAX - 1));
				l->n--;
			}
			snprintf(l->line[l->n++], USBLOG_LINE, "%.*s", (int)len, p);
		}
		p = e ? e + 1 : NULL;
	}
}

/* A new line at the end, the oldest going when full */
static void begin(usblog *l, long long now, const char *what)
{
	char when[32];
	time_t t = (time_t)now;
	struct tm tm;

	if (l->n == USBLOG_MAX) {
		memmove(l->line[0], l->line[1], sizeof l->line[0] * (USBLOG_MAX - 1));
		l->n--;
	}
	if (localtime_r(&t, &tm)) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
	else snprintf(when, sizeof when, "%lld", now);
	snprintf(l->line[l->n], USBLOG_LINE, "%s  %s", when, what);
	l->n++;
	l->open = true;
	l->since = now;
	l->host = l->computer = l->paused = false;
	l->dirty = true;
}

void usblog_note(usblog *l, const char *what)
{
	char *s;
	size_t len;

	if (!l->open || !l->n || !what || !*what) return;
	s = l->line[l->n - 1];
	len = strlen(s);
	snprintf(s + len, USBLOG_LINE - len, " \xc2\xb7 %s", what);
	l->dirty = true;
}

static void end(usblog *l, long long now)
{
	char t[48];
	long long d = now - l->since;

	if (d < 0) d = 0;
	if (d < 60) snprintf(t, sizeof t, "out after %lld s", d);
	else if (d < 3600) snprintf(t, sizeof t, "out after %lld min %lld s", d / 60, d % 60);
	else snprintf(t, sizeof t, "out after %lld h %lld min", d / 3600, d % 3600 / 60);
	usblog_note(l, t);
	l->open = false;
	l->plug = 0;
}

void usblog_follow(usblog *l, const usb_power *p, long long now)
{
	bool in, wants, power;

	if (!p->known) return;
	wants = p->pin == 1;
	power = p->vbus == 1;
	in = wants || power;
	/* Out: no pin, no power */
	if (l->open && !in) {
		end(l, now);
		return;
	}
	/* A new plug: the pin to "wants power" (usbrole's number moves), or
	 * power in with nothing open */
	if (wants && (!l->open || (l->plug && p->plug != l->plug) || (!l->plug && !l->vbus))) {
		char what[64];

		if (l->open) end(l, now);
		snprintf(what, sizeof what, "plug %u: wants power, %s", p->plug, power ? "power in" : "no power in");
		begin(l, now, what);
		l->plug = p->plug;
		l->vbus = power;
	} else if (power && !l->open) {
		begin(l, now, "power in");
		l->plug = 0;
		l->vbus = true;
	}
	if (!l->open) return;
	/* What the port does, said as it changes */
	if (power && !l->vbus) usblog_note(l, "power in");
	l->vbus = power;
	if (p->host && !l->host) usblog_note(l, p->automatic && !p->paused ? "powered automatically" : "powered");
	if (!p->host && l->host && !p->computer) usblog_note(l, "power off");
	l->host = p->host;
	if (p->computer && !l->computer) usblog_note(l, "a computer on the bus: not powered");
	l->computer = p->computer;
	if (p->paused && !l->paused && p->automatic) usblog_note(l, "Automatic paused");
	l->paused = p->paused;
}

size_t usblog_text(const usblog *l, char *out, size_t n)
{
	size_t len = 0;
	int i;

	if (n) out[0] = '\0';
	for (i = 0; i < l->n && len + 1 < n; i++) {
		int w = snprintf(out + len, n - len, "%s\n", l->line[i]);

		if (w < 0) break;
		len += (size_t)w < n - len ? (size_t)w : n - len - 1;
	}
	return len;
}
