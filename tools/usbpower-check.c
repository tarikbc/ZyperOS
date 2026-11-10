/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's half of usbrole (src/usbpower.h): usbrole's state lines as
 * it writes them, a request as usbrole reads it, and the question asked once
 * per plug, after the plug has waited USBPOWER_ASK_MS, never for a computer's
 * plug that usbrole says may not be asked about. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/usbpower.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

/* The plug asked about, or 0 (USBPOWER_ASK only) */
static unsigned follow(usbpower_follow_state *f, const usb_power *p, unsigned now)
{
	unsigned plug;

	return usbpower_follow(f, p, now, &plug) == USBPOWER_ASK ? plug : 0;
}

int main(void)
{
	usb_power p;
	usbpower_follow_state f = { 0 };
	unsigned plug;
	char dir[] = "/tmp/usbpower-check-XXXXXX", path[256], text[32] = "";
	FILE *fp;
	unsigned t;

	/* usbrole's own line (plastron's usbrole.c write_state) */
	CHECK(usbpower_parse("plug=7 ask=1 pin=1 vbus=0 computer=0 role=device auto=0\n", &p) &&
	      p.plug == 7 && p.ask && p.pin == 1 && p.vbus == 0 && !p.computer && !p.host,
	      "a plug that may be asked about");
	CHECK(usbpower_waits(&p), "a plug that waits for power is not waiting");
	CHECK(usbpower_parse("plug=3 ask=0 pin=1 vbus=0 computer=0 role=device auto=0\n", &p) && !p.ask &&
	      usbpower_waits(&p), "a computer's plug that waits");
	CHECK(usbpower_parse("plug=3 ask=0 pin=1 vbus=1 computer=0 role=host auto=0\n", &p) && p.host &&
	      !usbpower_waits(&p), "a host port waits");
	CHECK(usbpower_parse("plug=0 ask=0 pin=0 vbus=1 computer=0 role=device auto=0\n", &p) &&
	      !usbpower_waits(&p), "a computer giving power waits");
	CHECK(!usbpower_parse("plug=1 ask=1 pin=1\n", &p) && !p.known, "half a line read");
	CHECK(!usbpower_parse("plug=1 ask=1 pin=1 vbus=0 computer=0 role=otg auto=0\n", &p), "a role of no kind read");
	CHECK(!usbpower_parse("", &p) && !usbpower_parse(NULL, &p), "nothing read");

	/* Once per plug, after 2 s of being askable; a plug that stops being
	 * askable starts again; a new plug is asked about again */
	usbpower_parse("plug=4 ask=1 pin=1 vbus=0 computer=0 role=device auto=0", &p);
	for (t = 1000; t < 1000 + USBPOWER_ASK_MS; t += 100)
		CHECK(!follow(&f, &p, t), "asked at %u, before 2 s", t);
	CHECK(follow(&f, &p, 1000 + USBPOWER_ASK_MS) == 4, "not asked after 2 s");
	CHECK(!follow(&f, &p, 9000), "asked twice for one plug");
	usbpower_parse("plug=5 ask=1 pin=1 vbus=0 computer=0 role=device auto=0", &p);
	CHECK(!follow(&f, &p, 10000), "a new plug asked at once");
	usbpower_parse("plug=5 ask=0 pin=1 vbus=1 computer=0 role=device auto=0", &p);
	CHECK(!follow(&f, &p, 11000), "asked while power comes in");
	usbpower_parse("plug=5 ask=1 pin=1 vbus=0 computer=0 role=device auto=0", &p);
	CHECK(!follow(&f, &p, 11500), "the wait did not start again");
	CHECK(follow(&f, &p, 11500 + USBPOWER_ASK_MS) == 5, "plug 5 never asked about");
	usbpower_parse("plug=6 ask=0 pin=1 vbus=0 computer=0 role=device auto=0", &p);
	CHECK(!follow(&f, &p, 30000) && !follow(&f, &p, 60000),
	      "a computer's plug was asked about");

	/* The quick swap of 2026-11-10: a plug right after a computer's is
	 * asked about like any other, never told to replug; told to replug only
	 * when a computer showed up on the bus during the plug, once */
	{
		usbpower_follow_state g = { 0 };

		usbpower_parse("plug=1 ask=1 pin=1 vbus=0 computer=0 role=device auto=0", &p);
		CHECK(usbpower_follow(&g, &p, 1000, &plug) == USBPOWER_NOTHING, "asked at once");
		CHECK(usbpower_follow(&g, &p, 1000 + USBPOWER_ASK_MS, &plug) == USBPOWER_ASK && plug == 1,
		      "a plug right after a computer's not asked about");
		usbpower_parse("plug=1 ask=0 pin=1 vbus=0 computer=1 role=device auto=0", &p);
		CHECK(usbpower_follow(&g, &p, 9000, &plug) == USBPOWER_REPLUG && plug == 1,
		      "a computer on the bus, waiting: not told to replug");
		CHECK(usbpower_follow(&g, &p, 9500, &plug) == USBPOWER_NOTHING, "told twice for one plug");
		/* Waiting with no computer seen and not askable (the boot grace):
		 * neither asked nor told */
		usbpower_parse("plug=2 ask=0 pin=1 vbus=0 computer=0 role=device auto=0", &p);
		CHECK(usbpower_follow(&g, &p, 10000, &plug) == USBPOWER_NOTHING &&
		      usbpower_follow(&g, &p, 20000, &plug) == USBPOWER_NOTHING, "told or asked in the grace");
		/* Power Automatically: never asked, usbrole powers it */
		usbpower_parse("plug=3 ask=1 pin=1 vbus=0 computer=0 role=device auto=1", &p);
		CHECK(usbpower_follow(&g, &p, 30000, &plug) == USBPOWER_NOTHING &&
		      usbpower_follow(&g, &p, 40000, &plug) == USBPOWER_NOTHING, "asked in automatic");
		CHECK(p.automatic, "auto=1 not read");
		CHECK(!p.paused, "a line without paused= read as paused");
		/* Automatic paused by a computer (the Mac's dropping port,
		 * 2026-11-10): its next plug gets the replug message at once,
		 * and the question after 2 s, once each */
		CHECK(usbpower_parse("plug=7 ask=1 pin=1 vbus=0 computer=0 role=device auto=1 paused=1", &p) &&
		      p.paused && p.automatic, "paused=1 not read");
		CHECK(usbpower_follow(&g, &p, 50000, &plug) == USBPOWER_REPLUG && plug == 7,
		      "paused: the plug after the computer not told to replug");
		CHECK(usbpower_follow(&g, &p, 50200, &plug) == USBPOWER_NOTHING, "paused: asked at once");
		CHECK(usbpower_follow(&g, &p, 50200 + USBPOWER_ASK_MS, &plug) == USBPOWER_ASK && plug == 7,
		      "paused: not asked about after 2 s");
		CHECK(usbpower_follow(&g, &p, 60000, &plug) == USBPOWER_NOTHING, "paused: asked or told twice");
		CHECK(!usbpower_parse("plug=7 ask=1 pin=1 vbus=0 computer=0 role=device auto=1 paused=", &p) ||
		      !p.paused, "an empty paused= read as paused");
	}

	/* The files, in a scratch folder */
	CHECK(mkdtemp(dir) != NULL, "no scratch folder");
	snprintf(path, sizeof path, "%s/state", dir);
	CHECK(!usbpower_read(dir, &p), "a missing state read");
	if ((fp = fopen(path, "w"))) {
		fputs("plug=9 ask=1 pin=1 vbus=0 computer=0 role=device auto=0\n", fp);
		fclose(fp);
	}
	CHECK(usbpower_read(dir, &p) && p.plug == 9 && p.ask, "the state file not read");
	CHECK(usbpower_request(dir, 9), "the request not written");
	snprintf(path, sizeof path, "%s/host", dir);
	if ((fp = fopen(path, "r"))) {
		if (!fgets(text, sizeof text, fp)) text[0] = '\0';
		fclose(fp);
	}
	CHECK(!strcmp(text, "9\n"), "the request reads \"%s\"", text);
	snprintf(path, sizeof path, "%s/host.new", dir);
	CHECK(access(path, F_OK) != 0, "the request's temporary file left behind");
	/* A link where the state goes is not followed */
	snprintf(path, sizeof path, "%s/state", dir);
	unlink(path);
	CHECK(symlink("/etc/hosts", path) == 0, "no link made");
	CHECK(!usbpower_read(dir, &p), "a link read as the state");
	/* The setting, written whole */
	CHECK(usbpower_mode(dir, true), "the mode not written");
	{
		char mp[256], mt[16] = "";
		FILE *mf;

		snprintf(mp, sizeof mp, "%s/mode", dir);
		if ((mf = fopen(mp, "r"))) { if (!fgets(mt, sizeof mt, mf)) mt[0] = 0; fclose(mf); }
		CHECK(!strcmp(mt, "auto\n"), "the mode reads \"%s\"", mt);
		CHECK(usbpower_mode(dir, false), "the mode not written back");
		if ((mf = fopen(mp, "r"))) { if (!fgets(mt, sizeof mt, mf)) mt[0] = 0; fclose(mf); }
		CHECK(!strcmp(mt, "ask\n"), "the mode reads \"%s\"", mt);
		unlink(mp);
	}
	unlink(path);
	snprintf(path, sizeof path, "%s/host", dir);
	unlink(path);
	rmdir(dir);

	if (fails) {
		fprintf(stderr, "usbpower-check: %d failed\n", fails);
		return 1;
	}
	printf("usbpower-check: usbrole's lines read, a request written whole, asked once per plug after 2 s\n");
	return 0;
}
