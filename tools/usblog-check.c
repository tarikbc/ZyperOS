/* SPDX-License-Identifier: GPL-3.0-only */
/* src/usblog.h held: a plug a line, grown as it goes, closed with its time
 * in; power in alone a line too; the last 50 kept; the file read back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/usblog.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static usb_power st(unsigned plug, int pin, int vbus, int host, int computer, int automatic, int paused)
{
	usb_power p;

	memset(&p, 0, sizeof p);
	p.known = true;
	p.plug = plug;
	p.pin = pin;
	p.vbus = vbus;
	p.host = host;
	p.computer = computer;
	p.automatic = automatic;
	p.paused = paused;
	return p;
}

/* The last line, after its "YYYY-MM-DD HH:MM:SS  " */
static const char *last(const usblog *l)
{
	const char *s = l->n ? l->line[l->n - 1] : "";
	return strlen(s) > 21 ? s + 21 : s;
}

int main(void)
{
	static usblog l;
	static char text[USBLOG_MAX * USBLOG_LINE + 64];
	usb_power p;
	const long long T = 1793900000LL;
	int i;

	setenv("TZ", "UTC", 1);
	tzset();
	memset(&l, 0, sizeof l);

	/* A DAC: asked, USB Accessory, powered, out after 3 min 12 s */
	p = st(0, 0, 0, 0, 0, 0, 0);
	usblog_follow(&l, &p, T);
	CHECK(l.n == 0, "an empty port wrote a line");
	p = st(1, 1, 0, 0, 0, 0, 0);
	usblog_follow(&l, &p, T + 1);
	usblog_follow(&l, &p, T + 2);
	CHECK(l.n == 1 && !strcmp(last(&l), "plug 1: wants power, no power in"), "the plug's start: \"%s\"", last(&l));
	CHECK(!strncmp(l.line[0], "2026-11-05 17:33:21", 19), "its time: \"%.19s\"", l.line[0]);
	usblog_note(&l, "asked: USB Accessory");
	p = st(1, 1, 0, 1, 0, 0, 0);
	usblog_follow(&l, &p, T + 3);
	usblog_follow(&l, &p, T + 4);
	p = st(1, 0, 0, 0, 0, 0, 0);
	usblog_follow(&l, &p, T + 1 + 192);
	CHECK(!strcmp(last(&l), "plug 1: wants power, no power in \xc2\xb7 asked: USB Accessory \xc2\xb7 powered "
	                       "\xc2\xb7 out after 3 min 12 s"), "the DAC's line: \"%s\"", last(&l));
	CHECK(!l.open, "out: still open");
	usblog_note(&l, "late");
	CHECK(!strstr(last(&l), "late"), "a note with nothing in went on the last line");

	/* The Mac on Automatic: powered, seen, paused, the next plug asked */
	p = st(2, 1, 0, 0, 0, 1, 0);
	usblog_follow(&l, &p, T + 300);
	p = st(2, 1, 0, 1, 0, 1, 0);
	usblog_follow(&l, &p, T + 302);
	p = st(2, 1, 0, 0, 1, 1, 1);
	usblog_follow(&l, &p, T + 304);
	CHECK(strstr(last(&l), "powered automatically") && strstr(last(&l), "a computer on the bus: not powered") &&
	      strstr(last(&l), "Automatic paused") && !strstr(last(&l), "power off"),
	      "the Mac on Automatic: \"%s\"", last(&l));
	/* Its port drops and comes back as plug 3 within the same poll gap */
	p = st(3, 1, 0, 0, 0, 1, 1);
	usblog_follow(&l, &p, T + 305);
	CHECK(l.n == 3 && !strncmp(last(&l), "plug 3: wants power", 19) && strstr(l.line[1], "out after 5 s"),
	      "a new plug number: a new line, the last one closed");
	usblog_note(&l, "replug message");
	/* The Mac giving power now: the same cable, power in */
	p = st(3, 0, 1, 0, 0, 1, 1);
	usblog_follow(&l, &p, T + 320);
	usblog_note(&l, "Computer Connected: File Transfer");
	CHECK(strstr(last(&l), "replug message \xc2\xb7 power in \xc2\xb7 Computer Connected: File Transfer"),
	      "the Mac's power: \"%s\"", last(&l));
	p = st(3, 0, 0, 0, 0, 1, 1);
	usblog_follow(&l, &p, T + 320 + 7300);
	CHECK(strstr(last(&l), "out after 2 h 1 min"), "hours: \"%s\"", last(&l));

	/* A wall charger: power in alone */
	p = st(3, 0, 1, 0, 0, 0, 0);
	usblog_follow(&l, &p, T + 9000);
	CHECK(!strcmp(last(&l), "power in"), "a charger: \"%s\"", last(&l));
	p = st(3, 0, 0, 0, 0, 0, 0);
	usblog_follow(&l, &p, T + 9030);

	/* An unread state writes nothing */
	p = st(9, 1, 0, 0, 0, 0, 0);
	p.known = false;
	usblog_follow(&l, &p, T + 9100);
	CHECK(l.n == 4, "an unread state wrote a line");

	/* The last 50, and the file read back */
	for (i = 0; i < 60; i++) {
		p = st(100 + (unsigned)i, 1, 0, 0, 0, 0, 0);
		usblog_follow(&l, &p, T + 10000 + i * 10);
		p = st(100 + (unsigned)i, 0, 0, 0, 0, 0, 0);
		usblog_follow(&l, &p, T + 10005 + i * 10);
	}
	CHECK(l.n == USBLOG_MAX && strstr(l.line[0], "plug 110:") && strstr(l.line[49], "plug 159:"),
	      "not the last 50: %d lines, first \"%s\"", l.n, l.line[0]);
	usblog_text(&l, text, sizeof text);
	CHECK(strlen(text) < 8192, "the file is %zu bytes", strlen(text));
	{
		static usblog back;

		usblog_load(&back, text);
		CHECK(back.n == USBLOG_MAX && !strcmp(back.line[49], l.line[49]) && !back.open, "the file read back");
		usblog_load(&back, "one\n\ntwo\nthree");
		CHECK(back.n == 3 && !strcmp(back.line[2], "three"), "blank lines and no last newline");
	}

	if (fails) {
		fprintf(stderr, "usblog-check: %d failed\n", fails);
		return 1;
	}
	printf("usblog-check: a plug a line, grown and closed with its time in; power in alone; the last 50, "
	       "under 8 KB, read back\n");
	return 0;
}
