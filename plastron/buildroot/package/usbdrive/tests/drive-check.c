/* SPDX-License-Identifier: GPL-3.0-or-later */
/* usbdrive's rules (src/drive.h): never exported while mounted or held; and
 * the export's end: ejected, the cable out, POWER, nobody taking it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/drive.h"
#include "../src/drivestat.h"
#include "../src/atlas.h"

static int fails;

/* A number as the page would show it, '.' for the mark */
static const char *total(unsigned long long b)
{
	static char out[4][32];
	static int k;
	int tok[DST_FMT_MAX], n = dst_fmt_total(b, tok, DST_FMT_MAX);

	k = (k + 1) % 4;
	dst_tokens_text(tok, n, '.', out[k], sizeof out[k]);
	return out[k];
}

static const char *rate(unsigned long long b)
{
	static char out[4][32];
	static int k;
	int tok[DST_FMT_MAX], n = dst_fmt_rate(b, tok, DST_FMT_MAX);

	k = (k + 1) % 4;
	dst_tokens_text(tok, n, '.', out[k], sizeof out[k]);
	return out[k];
}
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void)
{
	drv_wait w;
	unsigned t;

	/* Fail closed */
	CHECK(!drv_may_export(true, 0), "exported while mounted");
	CHECK(!drv_may_export(false, 1), "exported while a program holds the card");
	CHECK(!drv_may_export(true, 3), "exported mounted and held");
	CHECK(drv_may_export(false, 0), "not exported, unmounted and free");

	/* A Mac takes it, writes, ejects */
	memset(&w, 0, sizeof w);
	CHECK(drv_wait_step(&w, true, true, false, false, 1000) == DRV_STAY, "at once: ended");
	CHECK(drv_wait_step(&w, true, true, true, false, 3000) == DRV_STAY, "configured: ended");
	CHECK(drv_wait_step(&w, true, true, true, false, 60000) == DRV_STAY, "a minute in use: ended");
	CHECK(drv_wait_step(&w, true, false, true, false, 61000) == DRV_EJECTED, "the file cleared: not ejected");

	/* No file ever (the LUN refused it): not an eject */
	memset(&w, 0, sizeof w);
	CHECK(drv_wait_step(&w, true, false, true, false, 0) == DRV_STAY, "no file at the start read as an eject");

	/* The cable out: a dip is not it, 1.5 s of no power is */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0);
	CHECK(drv_wait_step(&w, false, true, true, false, 5000) == DRV_STAY, "unplugged at once");
	CHECK(drv_wait_step(&w, true, true, true, false, 5400) == DRV_STAY, "power back: ended");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000) == DRV_STAY, "the dip's start counted on");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000 + DRV_UNPLUG_MS - 1) == DRV_STAY, "unplugged early");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000 + DRV_UNPLUG_MS) == DRV_UNPLUGGED, "not unplugged");

	/* POWER: at once, whatever else */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0);
	CHECK(drv_wait_step(&w, true, true, true, true, 100) == DRV_POWER, "POWER not taken");

	/* A charger, or a cable that only charges: nobody configures it */
	memset(&w, 0, sizeof w);
	for (t = 0; t < DRV_NOHOST_MS; t += 200)
		CHECK(drv_wait_step(&w, true, true, false, false, t) == DRV_STAY, "no computer: ended at %u", t);
	CHECK(drv_wait_step(&w, true, true, false, false, DRV_NOHOST_MS) == DRV_NOHOST, "no computer: never given up");
	/* ...but one that took it a moment before keeps it */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, false, false, 0);
	drv_wait_step(&w, true, true, true, false, 59000);
	CHECK(drv_wait_step(&w, true, true, false, false, DRV_NOHOST_MS + 1) == DRV_STAY,
	      "a computer that took it, given up");

	/* The clock's wrap */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0xfffffe00u);
	drv_wait_step(&w, false, true, true, false, 0xffffff00u);
	CHECK(drv_wait_step(&w, false, true, true, false, 0xffffff00u + DRV_UNPLUG_MS) == DRV_UNPLUGGED,
	      "unplugged across the wrap");

	/* The Flash Drive page's numbers (drivestat.h) */
	{
		static const struct { unsigned long long b; const char *want; } T[] = {
			{ 0, "0 B" }, { 999, "999 B" }, { 1000, "1 KB" }, { 999499, "999 KB" },
			{ 999500, "1 MB" }, { 312000000ull, "312 MB" }, { 999499999ull, "999 MB" },
			{ 999500000ull, "1.00 GB" }, { 1240000000ull, "1.24 GB" }, { 1235000000ull, "1.24 GB" },
			{ 999990000000ull, "999.99 GB" }, { 999995000000ull, "1000 GB" },
		};
		static const struct { unsigned long long b; const char *want; } R[] = {
			{ 0, "0 KB/s" }, { 512000, "512 KB/s" }, { 999499, "999 KB/s" }, { 999500, "1.0 MB/s" },
			{ 23400000ull, "23.4 MB/s" }, { 23449999ull, "23.4 MB/s" }, { 23450000ull, "23.5 MB/s" },
			{ 999949999ull, "999.9 MB/s" }, { 999950000ull, "1.00 GB/s" },
		};
		dst_state d;
		size_t i;
		char dec[32];
		int tok[DST_FMT_MAX], n;

		for (i = 0; i < sizeof T / sizeof T[0]; i++)
			CHECK(!strcmp(total(T[i].b), T[i].want), "%llu bytes: \"%s\", not \"%s\"", T[i].b, total(T[i].b), T[i].want);
		for (i = 0; i < sizeof R / sizeof R[0]; i++)
			CHECK(!strcmp(rate(R[i].b), R[i].want), "%llu B/s: \"%s\", not \"%s\"", R[i].b, rate(R[i].b), R[i].want);
		/* The language's mark is a token of its own */
		n = dst_fmt_total(1240000000ull, tok, DST_FMT_MAX);
		dst_tokens_text(tok, n, ',', dec, sizeof dec);
		CHECK(!strcmp(dec, "1,24 GB"), "the mark: %s", dec);
		/* The widest the page has room for */
		n = dst_fmt_rate(999949999ull, tok, DST_FMT_MAX);
		CHECK(n == 8, "999.9 MB/s: %d tokens", n);

		/* Idle at the start, a copy, a pause, idle, reading */
		memset(&d, 0, sizeof d);
		CHECK(dst_step(&d, 1000, 5000, 10000), "the first look shows nothing");
		CHECK(d.status == DST_IDLE && d.wrote == 0 && d.read == 0, "the first look moved something");
		CHECK(!dst_step(&d, 1000, 5000, 11000), "nothing moved, but it changed");
		/* 23.4 MB in a second, as sectors */
		CHECK(dst_step(&d, 1000 + 64, 5000 + 45703, 12000), "a copy did not change the page");
		CHECK(d.status == DST_WRITING, "writing: status %d", (int)d.status);
		CHECK(!strcmp(rate(d.rate), "23.4 MB/s"), "writing: %s", rate(d.rate));
		CHECK(d.wrote == 45703ull * 512 && d.read == 64 * 512, "the totals: %llu %llu", d.wrote, d.read);
		/* A second with nothing: still copying at the last speed (a pause
		 * between two files) */
		dst_step(&d, 1064, 50703, 13000);
		CHECK(d.status == DST_WRITING && !strcmp(rate(d.rate), "23.4 MB/s"), "a pause: %d %s",
		      (int)d.status, rate(d.rate));
		/* Two seconds with nothing: idle, no speed */
		CHECK(dst_step(&d, 1064, 50703, 14000), "idle did not change the page");
		CHECK(d.status == DST_IDLE && d.rate == 0, "idle: %d %llu", (int)d.status, d.rate);
		CHECK(!dst_step(&d, 1064, 50703, 15000), "idle redrew");
		CHECK(!dst_step(&d, 1064, 50703, 60000), "idle redrew a minute on");
		/* Only reads: reading, at their speed */
		dst_step(&d, 1064 + 2000, 50703, 61000);
		CHECK(d.status == DST_READING && !strcmp(rate(d.rate), "1.0 MB/s"), "reading: %d %s",
		      (int)d.status, rate(d.rate));
		/* Reads and writes: writing names it */
		dst_step(&d, 3064 + 100, 50703 + 100, 62000);
		CHECK(d.status == DST_WRITING, "both ways: %d", (int)d.status);
		/* A counter that goes back: a new start for it, nothing negative */
		{
			unsigned long long wrote = d.wrote, read = d.read;

			dst_step(&d, 10, 20, 63000);
			CHECK(d.wrote == wrote && d.read == read, "went back: %llu %llu", d.wrote, d.read);
			dst_step(&d, 10 + 2, 20 + 2, 64000);
			CHECK(d.wrote == wrote + 1024 && d.read == read + 1024, "after going back: %llu %llu",
			      d.wrote - wrote, d.read - read);
		}
		/* The clock's wrap: a second is a second */
		memset(&d, 0, sizeof d);
		dst_step(&d, 0, 0, 0xfffffc00u);
		dst_step(&d, 0, 1954, 0xfffffc00u + 1000u);
		CHECK(!strcmp(rate(d.rate), "1.0 MB/s"), "across the wrap: %s", rate(d.rate));
	}

	/* The atlas (atlas.h): read, and a row drawn into its slot */
	{
		static const char *const GL[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "dec", "sp",
		                                  "B", "KB", "MB", "GB", "ps", "st_write", "st_read", "st_idle" };
		char text[2048];
		size_t at = 0, k;
		at_layout l;
		uint32_t *atl = calloc(64 * 32, 4), *out = calloc(64 * 32, 4);
		int g[3] = { 1, DST_T_DEC, 2 }, x;

		at += (size_t)snprintf(text + at, sizeof text - at, "zyperos-drive-atlas 1\nsize 64 30\n"
		                       "slot status 0 0 30 8 left\nslot speed 30 0 60 8 right\n"
		                       "slot written 30 10 60 18 right\nslot read 30 20 60 28 right\n");
		/* glyph i at (i * 3, 24), 3 wide, 8 tall, colored i + 1 */
		for (k = 0; k < sizeof GL / sizeof GL[0]; k++)
			at += (size_t)snprintf(text + at, sizeof text - at, "glyph %s %d 24 3 8\n", GL[k], (int)k * 3);
		CHECK(!at_parse(text, &l), "an atlas taller than the picture was taken");
		at = 0;
		at += (size_t)snprintf(text + at, sizeof text - at, "zyperos-drive-atlas 1\nsize 64 32\n"
		                       "slot status 0 0 30 8 left\nslot speed 30 0 60 8 right\n"
		                       "slot written 30 10 60 18 right\nslot read 30 20 60 28 right\n");
		for (k = 0; k < sizeof GL / sizeof GL[0]; k++)
			at += (size_t)snprintf(text + at, sizeof text - at, "glyph %s %d 24 3 8\n", GL[k], (int)k * 3);
		CHECK(at_parse(text, &l), "a good atlas refused");
		CHECK(l.right[AT_SLOT_SPEED] && !l.right[AT_SLOT_STATUS], "the sides");
		{
			char bad[2048];

			snprintf(bad, sizeof bad, "%s", text);
			bad[strlen(bad) - 2] = '\0';   /* the last glyph line cut */
			*strrchr(bad, '\n') = '\0';
			CHECK(!at_parse(bad, &l), "an atlas missing a glyph was taken");
			CHECK(at_parse(text, &l), "a good atlas refused again");
		}
		for (k = 0; k < 64 * 32; k++) atl[k] = 0xAA;   /* the atlas's ground */
		for (k = 0; k < sizeof GL / sizeof GL[0]; k++)
			for (x = 0; x < 3; x++) {
				int y;

				for (y = 24; y < 32; y++) atl[y * 64 + (int)k * 3 + x] = (uint32_t)k + 1;
			}
		/* "1.2" right-aligned in speed (30..60): the glyphs at 51, 54, 57 */
		at_compose(&l, AT_SLOT_SPEED, g, 3, atl, out);
		CHECK(out[3 * 64 + 50] == 0xAA && out[3 * 64 + 51] == 2 && out[3 * 64 + 54] == DST_T_DEC + 1 &&
		      out[3 * 64 + 57] == 3 && out[3 * 64 + 59] == 3, "right-aligned: %x %x %x %x",
		      out[3 * 64 + 50], out[3 * 64 + 51], out[3 * 64 + 54], out[3 * 64 + 57]);
		CHECK(out[3 * 64 + 29] == 0 && out[9 * 64 + 40] == 0, "drawn outside its slot");
		/* The page's own first numbers never show through a shorter row */
		CHECK(out[3 * 64 + 31] == 0xAA, "the slot not cleared to the card's color: %x", out[3 * 64 + 31]);
		/* The status from the left; a row wider than the slot is cut at it */
		{
			int st = AT_G_ST_READ, many[12] = { 0 };

			at_compose(&l, AT_SLOT_STATUS, &st, 1, atl, out);
			CHECK(out[0] == AT_G_ST_READ + 1 && out[3] == 0xAA, "left: %x %x", out[0], out[3]);
			at_compose(&l, AT_SLOT_WRITTEN, many, 12, atl, out);
			CHECK(out[10 * 64 + 29] == 0 && out[10 * 64 + 30] == 1 && out[10 * 64 + 59] == 1,
			      "a wide row ran past its slot");
		}
		free(atl);
		free(out);
	}

	if (fails) {
		fprintf(stderr, "usbdrive drive-check: %d failed\n", fails);
		return 1;
	}
	printf("usbdrive drive-check: never exported mounted or held; ejected, unplugged (1.5 s), POWER, "
	       "no computer in a minute; the page's numbers, idle in 2 s, a counter going back; the atlas\n");
	return 0;
}
