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

/* A button with the gadget's counters read (sure) and nothing dirty: the
 * question is the last write's age alone */
static drv_act key(drv_keys *k, int which, bool down, unsigned now, bool wrote, unsigned last)
{
	return drv_key(k, which, down, drv_copying(true, wrote, now, last, 0));
}

/* drivestat counts bytes; these cases were written in 512-byte sectors */
static bool step(dst_state *d, unsigned long long rd, unsigned long long wr, unsigned now)
{
	return dst_step(d, rd * 512, wr * 512, now);
}

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
			{ 0, "0 MB/s" }, { 1, "0 KB/s" }, { 512000, "512 KB/s" }, { 999499, "999 KB/s" },
			{ 999500, "1.0 MB/s" }, { 23400000ull, "23.4 MB/s" }, { 23449999ull, "23.4 MB/s" },
			{ 23450000ull, "23.5 MB/s" }, { 999949999ull, "999.9 MB/s" }, { 999950000ull, "1.00 GB/s" },
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

		/* Both idle at the start */
		memset(&d, 0, sizeof d);
		CHECK(step(&d, 1000, 5000, 10000), "the first look shows nothing");
		CHECK(!d.in.active && !d.out.active && d.in.total == 0 && d.out.total == 0, "the first look moved");
		CHECK(!step(&d, 1000, 5000, 11000), "nothing moved, but it changed");
		/* To the card: 23.4 MB in a second, as sectors; a few reads with it */
		CHECK(step(&d, 1000 + 64, 5000 + 45703, 12000), "a copy did not change the page");
		CHECK(d.in.active && !strcmp(rate(d.in.rate), "23.4 MB/s"), "in: %d %s", d.in.active, rate(d.in.rate));
		CHECK(d.out.active && d.out.total == 64 * 512, "out with it: %d %llu", d.out.active, d.out.total);
		CHECK(d.in.total == 45703ull * 512, "in total: %llu", d.in.total);
		/* A second with nothing: both still moving at their last speed (a
		 * pause between two files) */
		CHECK(!step(&d, 1064, 50703, 13000), "a pause redrew");
		CHECK(d.in.active && !strcmp(rate(d.in.rate), "23.4 MB/s"), "a pause: %d %s", d.in.active,
		      rate(d.in.rate));
		/* Two seconds with nothing: idle at 0, the totals kept */
		CHECK(step(&d, 1064, 50703, 14000), "idle did not change the page");
		CHECK(!d.in.active && d.in.rate == 0 && !d.out.active && d.out.rate == 0, "idle: %d %llu",
		      d.in.active, d.in.rate);
		CHECK(d.in.total == 45703ull * 512, "idle lost the total");
		CHECK(!step(&d, 1064, 50703, 15000), "idle redrew");
		CHECK(!step(&d, 1064, 50703, 60000), "idle redrew a minute on");
		/* From the card alone: out moves, in stays idle */
		step(&d, 1064 + 2000, 50703, 61000);
		CHECK(d.out.active && !strcmp(rate(d.out.rate), "1.0 MB/s") && !d.in.active, "out alone: %d %s %d",
		      d.out.active, rate(d.out.rate), d.in.active);
		/* Both ways at once, each at its own speed */
		step(&d, 3064 + 9766, 50703 + 19532, 62000);
		CHECK(d.in.active && d.out.active && !strcmp(rate(d.in.rate), "10.0 MB/s") &&
		      !strcmp(rate(d.out.rate), "5.0 MB/s"), "both: %s %s", rate(d.in.rate), rate(d.out.rate));
		/* A counter that goes back: a new start for it, nothing negative */
		{
			unsigned long long in = d.in.total, out = d.out.total;

			step(&d, 10, 20, 63000);
			CHECK(d.in.total == in && d.out.total == out, "went back: %llu %llu", d.in.total, d.out.total);
			step(&d, 10 + 2, 20 + 2, 64000);
			CHECK(d.in.total == in + 1024 && d.out.total == out + 1024, "after going back: %llu %llu",
			      d.in.total - in, d.out.total - out);
		}
		/* The clock's wrap: a second is a second */
		memset(&d, 0, sizeof d);
		step(&d, 0, 0, 0xfffffc00u);
		step(&d, 0, 1954, 0xfffffc00u + 1000u);
		CHECK(!strcmp(rate(d.in.rate), "1.0 MB/s"), "across the wrap: %s", rate(d.in.rate));
		step(&d, 0, 1954, 0xfffffc00u + 3000u);
		CHECK(!d.in.active, "idle across the wrap");
	}

	/* The atlas (atlas.h): read, and a row drawn into its slot */
	{
		static const char *const TOK[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "dec", "sp",
		                                   "B", "KB", "MB", "GB", "ps" };
		static const char *const MK[] = { "in_on", "in_off", "out_on", "out_off" };
		/* 64 x 48: the speeds' slots 10 tall, the totals' 6, the marks' 8;
		 * the big glyphs on row 30 (10 tall), small on 40 (6), marks on 20 */
		static const char HEAD[] =
			"zyperos-drive-atlas 2\nsize 64 48\n"
			"slot speed_in 0 0 30 10 left\nslot speed_out 32 0 62 10 left\n"
			"slot total_in 0 10 30 16 left\nslot total_out 32 10 62 16 left\n"
			"slot mark_in 0 16 8 24 left\nslot mark_out 32 16 40 24 left\n";
		char text[3000];
		size_t at = 0, k;
		at_layout l;
		uint32_t *atl = calloc(64 * 48, 4), *out = calloc(64 * 48, 4);
		int g[3] = { AT_BIG + 1, AT_BIG + DST_T_DEC, AT_BIG + 2 }, x, y;

		at += (size_t)snprintf(text + at, sizeof text - at, "%s", HEAD);
		for (k = 0; k < DST_TOKENS; k++) {
			at += (size_t)snprintf(text + at, sizeof text - at, "glyph b%s %d 30 3 10\n", TOK[k], (int)k * 3);
			at += (size_t)snprintf(text + at, sizeof text - at, "glyph s%s %d 40 3 6\n", TOK[k], (int)k * 3);
		}
		for (k = 0; k < 4; k++)
			at += (size_t)snprintf(text + at, sizeof text - at, "glyph %s %d 20 8 8\n", MK[k], (int)k * 8);
		CHECK(at_parse(text, &l), "a good atlas refused");
		{
			char bad[3200];
			const char *const BAD[] = {
				"glyph s0 0 30 3 10\n",                         /* a small one as tall as a speed */
				"glyph in_on 0 0 8 9\n",                        /* a mark taller than its slot */
				"glyph b1 2147483000 0 2147483000 8\n",         /* x + w overflows */
				"glyph b2 0 2147483000 3 2147483000\n",         /* y + h overflows */
				"glyph b3 -5 0 3 8\n",                          /* off the left */
				"slot total_out 60 10 30 16 left\n",            /* X1 < X0 */
				"slot total_out 32 16 62 10 left\n",            /* Y1 < Y0 */
				"slot total_out 32 10 2147483647 16 left\n",    /* past the picture */
				"slot total_out -2147483647 10 62 16 left\n",   /* a huge negative */
				"size 2147483647 2147483647\n",                 /* a huge picture */
				"zyperos-drive-atlas 1\n",                      /* the one-card page's */
			};
			at_layout lb;

			for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
				snprintf(bad, sizeof bad, "%s%s", text, BAD[k]);   /* the later line wins */
				CHECK(!at_parse(bad, &lb), "a bad atlas taken: %s", BAD[k]);
			}
			/* A speed slot lower than the big glyphs: refused */
			snprintf(bad, sizeof bad, "%sslot speed_out 32 0 62 8 left\n", text);
			CHECK(!at_parse(bad, &lb), "a big glyph taller than a speed slot taken");
			/* A missing glyph */
			snprintf(bad, sizeof bad, "%s", text);
			*strstr(bad, "glyph sKB") = 'x';
			CHECK(!at_parse(bad, &lb), "an atlas missing a glyph taken");
		}
		for (k = 0; k < 64 * 48; k++) atl[k] = 0xAA;   /* the atlas's ground */
		for (k = 0; k < DST_TOKENS; k++)
			for (x = 0; x < 3; x++) {
				for (y = 30; y < 40; y++) atl[y * 64 + (int)k * 3 + x] = 0x100 + (uint32_t)k;
				for (y = 40; y < 46; y++) atl[y * 64 + (int)k * 3 + x] = 0x200 + (uint32_t)k;
			}
		for (k = 0; k < 4; k++)
			for (x = 0; x < 8; x++)
				for (y = 20; y < 28; y++) atl[y * 64 + (int)k * 8 + x] = 0x300 + (uint32_t)k;
		/* "1.2" in the in speed, from the left: big glyphs at 0, 3, 6 */
		at_compose(&l, AT_SLOT_SPEED_IN, g, 3, atl, out);
		CHECK(out[5 * 64 + 0] == 0x101 && out[5 * 64 + 3] == 0x100 + DST_T_DEC && out[5 * 64 + 6] == 0x102 &&
		      out[5 * 64 + 9] == 0xAA, "from the left: %x %x %x %x", out[5 * 64], out[5 * 64 + 3],
		      out[5 * 64 + 6], out[5 * 64 + 9]);
		CHECK(out[5 * 64 + 31] == 0 && out[11 * 64 + 1] == 0, "drawn outside its slot");
		/* A mark moving, then idle: the slot holds the other picture */
		{
			int m = AT_G_OUT_ON;

			at_compose(&l, AT_SLOT_MARK_OUT, &m, 1, atl, out);
			CHECK(out[20 * 64 + 32] == 0x302, "out moving: %x", out[20 * 64 + 32]);
			m = AT_G_OUT_OFF;
			at_compose(&l, AT_SLOT_MARK_OUT, &m, 1, atl, out);
			CHECK(out[20 * 64 + 32] == 0x303, "out idle: %x", out[20 * 64 + 32]);
		}
		/* A right-aligned slot, a row cut at it, and a shorter row clearing
		 * the longer one before it */
		{
			int many[12], two[2] = { AT_SMALL + 7, AT_SMALL + 7 };

			for (k = 0; k < 12; k++) many[k] = AT_SMALL + 0;
			l.right[AT_SLOT_TOTAL_IN] = true;
			at_compose(&l, AT_SLOT_TOTAL_IN, many, 12, atl, out);
			CHECK(out[12 * 64 + 29] == 0x200 && out[12 * 64 + 30] == 0, "a wide row ran past its slot");
			at_compose(&l, AT_SLOT_TOTAL_IN, two, 2, atl, out);
			CHECK(out[12 * 64 + 29] == 0x207 && out[12 * 64 + 24] == 0x207 && out[12 * 64 + 23] == 0xAA,
			      "right-aligned and cleared: %x %x %x", out[12 * 64 + 29], out[12 * 64 + 24], out[12 * 64 + 23]);
		}
		/* A layout that never went through at_parse: a glyph taller than
		 * its slot at the picture's top edge draws only inside the slot */
		{
			at_layout raw = l;
			int g0 = AT_BIG;

			raw.slot[AT_SLOT_SPEED_IN] = (at_rect){ 0, 0, 30, 4 };
			raw.glyph[AT_BIG] = (at_rect){ 0, 0, 3, 48 };
			for (k = 0; k < 64 * 48; k++) out[k] = 0x11;
			at_compose(&raw, AT_SLOT_SPEED_IN, &g0, 1, atl, out);
			CHECK(out[4 * 64] == 0x11 && out[47 * 64] == 0x11, "drawn past a slot's rows");
		}
		free(atl);
		free(out);
	}

	/* The handheld's buttons on the page (drv_key) */
	{
		drv_keys k = { .level = 5, .max = 11 };

		CHECK(key(&k, DRV_K_VOLUP, true, 1000, false, 0) == DRV_A_NONE && k.level == 5,
		      "volume alone changed the brightness");
		key(&k, DRV_K_FN, true, 1000, false, 0);
		CHECK(key(&k, DRV_K_VOLUP, true, 1000, false, 0) == DRV_A_BRIGHT && k.level == 6, "FN+up: %d", k.level);
		CHECK(key(&k, DRV_K_VOLUP, false, 1000, false, 0) == DRV_A_NONE, "a release did something");
		CHECK(key(&k, DRV_K_VOLDN, true, 1000, false, 0) == DRV_A_BRIGHT && k.level == 5, "FN+down: %d", k.level);
		k.level = 11;
		CHECK(key(&k, DRV_K_VOLUP, true, 1000, false, 0) == DRV_A_NONE && k.level == 11, "past the top");
		k.level = 0;
		CHECK(key(&k, DRV_K_VOLDN, true, 1000, false, 0) == DRV_A_NONE && k.level == 0, "past the bottom");
		key(&k, DRV_K_FN, false, 1000, false, 0);
		CHECK(key(&k, DRV_K_VOLDN, true, 1000, false, 0) == DRV_A_NONE, "FN let go, still brightness");
		/* B: nothing written, or long ago: eject at once */
		CHECK(key(&k, DRV_K_B, true, 10000, false, 0) == DRV_A_EJECT, "B with nothing written did not eject");
		CHECK(key(&k, DRV_K_B, true, 10000, true, 10000 - DRV_ASK_MS) == DRV_A_EJECT, "B 5 s after a write asked");
		/* Written just now: asked; B keeps it, the page back */
		CHECK(key(&k, DRV_K_B, true, 10000, true, 9000) == DRV_A_ASK && k.asking, "B 1 s after a write: not asked");
		CHECK(key(&k, DRV_K_B, false, 10050, true, 9000) == DRV_A_NONE && k.asking, "B's release answered");
		CHECK(key(&k, DRV_K_B, true, 10500, true, 9000) == DRV_A_KEEP && !k.asking, "B on the question did not keep it");
		/* Asked again; A ejects */
		CHECK(key(&k, DRV_K_A, true, 11000, true, 9000) == DRV_A_NONE, "A with no question did something");
		CHECK(key(&k, DRV_K_B, true, 11000, true, 9000) == DRV_A_ASK, "not asked again");
		CHECK(key(&k, DRV_K_A, true, 11500, true, 9000) == DRV_A_EJECT && !k.asking, "A on the question did not eject");
		/* The clock's wrap: a write just before it is just now */
		memset(&k, 0, sizeof k);
		CHECK(key(&k, DRV_K_B, true, 500, true, 0xfffffe00u) == DRV_A_ASK, "a write across the wrap");
	}

	/* bright.txt (at_parse_bright), read as untrusted */
	{
		static const char GOOD[] = "zyperos-drive-bright 1\nlevel 7\n"
		                           "ladder 17 22 28 36 46 58 74 95 122 156 199 255\nbar 171 6 298 58\n";
		const char *const BAD[] = {
			"zyperos-drive-bright 2\nlevel 1\nladder 1 2\nbar 0 0 10 10\n",                 /* another format */
			"zyperos-drive-bright 1\nlevel 12\nladder 17 22 28 36 46 58 74 95 122 156 199 255\nbar 171 6 298 58\n",
			"zyperos-drive-bright 1\nlevel -1\nladder 17 22\nbar 0 0 10 10\n",
			"zyperos-drive-bright 1\nlevel 0\nladder 17\nbar 0 0 10 10\n",                  /* one rung */
			"zyperos-drive-bright 1\nlevel 0\nladder 0 22\nbar 0 0 10 10\n",                /* 0 is dark */
			"zyperos-drive-bright 1\nlevel 0\nladder 17 256\nbar 0 0 10 10\n",
			"zyperos-drive-bright 1\nlevel 0\nladder 22 17\nbar 0 0 10 10\n",               /* going down */
			"zyperos-drive-bright 1\nlevel 0\nladder 17 22\nbar 600 0 100 10\n",            /* past the edge */
			"zyperos-drive-bright 1\nlevel 0\nladder 17 22\nbar 2147483000 0 2147483000 10\n",
			"zyperos-drive-bright 1\nlevel 0\nladder 17 22\n",                              /* no bar */
			("zyperos-drive-bright 1\nlevel 0\nladder 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 "
			 "23 24 25 26 27 28 29 30 31 32 33\nbar 0 0 10 10\n"),                         /* too many */
		};
		at_bright b;
		size_t i;

		CHECK(at_parse_bright(GOOD, 640, 480, &b) && b.level == 7 && b.rungs == 12 && b.ladder[11] == 255 &&
		      b.bar.w == 298, "a good bright.txt refused");
		for (i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
			CHECK(!at_parse_bright(BAD[i], 640, 480, &b), "a bad bright.txt taken: %zu", i);
	}

	/* B's question fails safe (drv_copying), and what it reads */
	{
		unsigned long long rc = 0, wc = 0;
		unsigned long kb = 0;
		drv_keys k = { .max = 11 };

		/* The gadget's counters not readable: always asked, never skipped */
		CHECK(drv_copying(false, false, 100000, 0, 0), "not sure, but not asked");
		CHECK(drv_key(&k, DRV_K_B, true, drv_copying(false, false, 100000, 0, 0)) == DRV_A_ASK,
		      "not sure: B ejected without asking");
		CHECK(drv_key(&k, DRV_K_A, true, true) == DRV_A_EJECT, "A after the not-sure question");
		/* Sure, nothing written, little dirty: B ejects; much dirty: asked */
		CHECK(!drv_copying(true, false, 100000, 0, 1024), "1 MB dirty asked");
		CHECK(drv_copying(true, false, 100000, 0, 1025), "past 1 MB dirty, not asked");
		CHECK(drv_copying(true, true, 100000, 96000, 0), "a write 4 s ago, not asked");
		CHECK(!drv_copying(true, true, 100000, 95000, 0), "a write 5 s ago asked");
		/* /proc/<pid>/io, as the kernel writes it */
		CHECK(dst_parse_io("rchar: 1234\nwchar: 56789\nsyscr: 0\nsyscw: 0\nread_bytes: 0\n"
		                   "write_bytes: 4096\ncancelled_write_bytes: 0\n", &rc, &wc) &&
		      rc == 1234 && wc == 56789, "io: %llu %llu", rc, wc);
		CHECK(!dst_parse_io("rchar: 1\n", &rc, &wc), "io without wchar taken");
		CHECK(!dst_parse_io("xrchar: 1\nxwchar: 2\n", &rc, &wc), "io with other names taken");
		CHECK(!dst_parse_io("", &rc, &wc), "empty io taken");
		/* /proc/meminfo's Dirty */
		CHECK(drv_parse_dirty("MemTotal: 1000 kB\nDirty:            4160 kB\nWriteback: 0 kB\n", &kb) && kb == 4160,
		      "dirty: %lu", kb);
		CHECK(drv_parse_dirty("Dirty: 8 kB\n", &kb) && kb == 8, "dirty first: %lu", kb);
		CHECK(!drv_parse_dirty("MemTotal: 1000 kB\nNotDirty: 5 kB\n", &kb), "no Dirty taken");
	}

	if (fails) {
		fprintf(stderr, "usbdrive drive-check: %d failed\n", fails);
		return 1;
	}
	printf("usbdrive drive-check: never exported mounted or held; ejected, unplugged (1.5 s), POWER, "
	       "no computer in a minute; the page's numbers each way, idle in 2 s, a counter going back; the atlas and bright.txt, read as untrusted; B's eject and its question, failing safe; the gadget's counters; FN + volume\n");
	return 0;
}
