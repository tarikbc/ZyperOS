/* SPDX-License-Identifier: GPL-3.0-only */
/* src/lastboot.h held on a build machine: the summary crashlog writes, read in
 * letters and in words; the code "K1W · 0.6 s" with no-break spaces; said once
 * (the same report again says nothing, a new one says itself); the card's copy
 * written and read back; the About row kept LASTBOOT_KEEP_DAYS and no longer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/lastboot.h"

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void code_is(const char *summary, const char *want)
{
	lastboot lb;
	char got[64];

	CHECK(lastboot_parse(summary, &lb), "not read: %s", summary);
	lastboot_code(&lb, got, sizeof got);
	CHECK(!strcmp(got, want), "code of %s: \"%s\", wanted \"%s\"", summary, got, want);
}

int main(void)
{
	lastboot a, b, keep;
	char path[] = "/tmp/lastboot-check-XXXXXX", file[600];
	long day = 86400, now = 1795500000L;

	/* Letters, the code with its no-break spaces (U+00A0) around the dot and
	 * before the unit; one decimal under 10 s, whole seconds from there */
	code_is("reason=W\nstage=K1\nkernel_time=0.62\n", "K1W\xc2\xa0\xc2\xb7\xc2\xa0" "0.6\xc2\xa0s");
	code_is("reason=P\nstage=L1\nkernel_time=9.94\n", "L1P\xc2\xa0\xc2\xb7\xc2\xa0" "9.9\xc2\xa0s");
	code_is("reason=T\nstage=C3\nkernel_time=9.96\n", "C3T\xc2\xa0\xc2\xb7\xc2\xa0" "10\xc2\xa0s");
	code_is("reason=X\nstage=H\nkernel_time=1834.2\n", "HX\xc2\xa0\xc2\xb7\xc2\xa0" "1834\xc2\xa0s");
	/* No time: the letters alone */
	code_is("reason=R\nstage=K2\n", "K2R");
	/* crashlog's words, CR line ends, spaces after a value, keys it does
	 * not know, and lines that are not key=value */
	code_is("# a report\r\nreason=watchdog \r\nstage=kernel\r\nkernel_time=0.62\r\nboard=px2\r\nnot a line\r\n",
	        "K1W\xc2\xa0\xc2\xb7\xc2\xa0" "0.6\xc2\xa0s");
	code_is("reason=hung_task\nstage=check\nkernel_time=41\n", "C3T\xc2\xa0\xc2\xb7\xc2\xa0" "41\xc2\xa0s");
	code_is("reason=lockup\nstage=launcher\nkernel_time=3\n", "L1P\xc2\xa0\xc2\xb7\xc2\xa0" "3.0\xc2\xa0s");
	code_is("reason=launcher\nstage=H\nkernel_time=181.1\nreport=crash-20261124-101500.txt\n",
	        "HW\xc2\xa0\xc2\xb7\xc2\xa0" "181\xc2\xa0s");
	code_is("reason=rootwait\nstage=K1\nkernel_time=20.4\n", "K1R\xc2\xa0\xc2\xb7\xc2\xa0" "20\xc2\xa0s");

	/* Not a summary: no stage, no reason, one it does not know */
	CHECK(!lastboot_parse("", &a), "an empty file read as a summary");
	CHECK(!lastboot_parse("reason=W\n", &a), "a summary with no stage");
	CHECK(!lastboot_parse("stage=K1\n", &a), "a summary with no reason");
	CHECK(!lastboot_parse("reason=Q\nstage=K1\n", &a), "a reason it does not know");
	CHECK(!lastboot_parse("reason=W\nstage=Z9\n", &a), "a stage it does not know");
	CHECK(!lastboot_parse("reason=W\nstage=K1\nkernel_time=-3\n", &a) || a.kernel_time < 0,
	      "a negative time taken");

	/* A freeze while playing, and a start that did not finish */
	lastboot_parse("reason=P\nstage=H\nkernel_time=1834.2\n", &a);
	CHECK(lastboot_froze(&a), "stage H is not a freeze");
	lastboot_parse("reason=W\nstage=L1\nkernel_time=61\n", &a);
	CHECK(!lastboot_froze(&a), "stage L1 read as a freeze");

	/* Said once: a report with no copy on the card is due, and the copy
	 * carries the day it was said; the same report again (the launcher
	 * started again after a game, the same /run file) is not; another
	 * report is */
	lastboot_parse("reason=W\nstage=K1\nkernel_time=0.62\ntime=2026-11-23 22:14:05\nversion=1.1.0\ndevice=px2\nlast=[    0.620000] mmc0: x\n", &a);
	memset(&b, 0, sizeof b);
	CHECK(lastboot_due(&a, &b, now, &keep), "a new report is not said");
	CHECK(keep.seen == now, "the copy's day is %ld, not %ld", keep.seen, now);
	CHECK(!lastboot_due(&a, &keep, now + 600, &b), "the same report said twice");
	lastboot_parse("reason=W\nstage=K1\nkernel_time=0.62\ntime=2026-11-24 08:01:00\n", &b);
	CHECK(lastboot_due(&b, &keep, now + day, &a), "a second report, another day, is not said");
	memset(&b, 0, sizeof b);
	CHECK(!lastboot_due(&b, &keep, now, &a), "no report at all is said");

	/* The card's copy: written whole, read back the same report with its day */
	lastboot_parse("reason=W\nstage=K1\nkernel_time=0.62\ntime=2026-11-23 22:14:05\nversion=1.1.0\ndevice=px2\nlast=[    0.620000] mmc0: x\n", &a);
	lastboot_due(&a, &b, now, &keep);
	CHECK(mkdtemp(path) != NULL, "no scratch folder");
	snprintf(file, sizeof file, "%s/%s", path, LASTBOOT_FILE);
	CHECK(lastboot_write(file, &keep), "the copy was not written");
	CHECK(lastboot_read(file, &b), "the copy does not read back");
	CHECK(lastboot_same(&b, &a) && b.seen == now && !strcmp(b.version, "1.1.0") &&
	      !strcmp(b.device, "px2") && !strcmp(b.last, "[    0.620000] mmc0: x"),
	      "the copy read back is another report");
	snprintf(file + strlen(file), sizeof file - strlen(file), ".new");
	CHECK(access(file, F_OK) != 0, "the copy's .new was left behind");
	remove(file);
	file[strlen(file) - 4] = '\0';
	remove(file);
	rmdir(path);

	/* The About row: shown from the day it was said for LASTBOOT_KEEP_DAYS,
	 * and with a clock that went back; not after, and not with no copy */
	CHECK(lastboot_row(&b, now), "no row the day it was said");
	CHECK(lastboot_row(&b, now + (LASTBOOT_KEEP_DAYS - 1) * day), "no row on day %d", LASTBOOT_KEEP_DAYS - 1);
	CHECK(!lastboot_row(&b, now + LASTBOOT_KEEP_DAYS * day), "a row after %d days", LASTBOOT_KEEP_DAYS);
	CHECK(lastboot_row(&b, now - day), "no row with a clock a day behind");
	memset(&a, 0, sizeof a);
	CHECK(!lastboot_row(&a, now), "a row with no copy");

	if (fails) { printf("lastboot-check: %d failed\n", fails); return 1; }
	printf("lastboot-check: the summary in letters and words, the code, said once, the card's copy, the About row's %d days\n",
	       LASTBOOT_KEEP_DAYS);
	return 0;
}
