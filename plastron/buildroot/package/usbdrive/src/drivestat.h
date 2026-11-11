/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What the Flash Drive page says while the card is the computer's: what is
 * moving, how fast, and how much went each way since the export began.
 *
 * The handheld sees blocks, not files: the games partition's /sys stat
 * (sectors read, field 3, and written, field 7, 512 bytes each). So it says
 * what the blocks say: "Copying to the card" while sectors are written,
 * "Reading from the card" while only read, "Nothing copying" once neither
 * has moved for DST_IDLE_MS; the speed of the way the status names, over
 * the last look; and the two totals. No time left: a block count cannot
 * know how many files are coming.
 *
 * A stat that goes back (a counter that wrapped, or a partition that came
 * back) is taken as a new start for that counter, never as a negative
 * number or a huge one.
 *
 * The numbers become tokens (dst_fmt_*): digits, the decimal mark, a space,
 * a unit, "/s"; the page's glyph atlas (ZyperOS draws it, src/main.c's
 * usb_drive) has one picture for each, in the language's own mark and
 * units. 1000-based, as the computers' own file windows count.
 *
 * Pure: no files, no clock of its own. tests/drive-check.c holds it.
 */
#ifndef USBDRIVE_DRIVESTAT_H
#define USBDRIVE_DRIVESTAT_H

#include <stdbool.h>
#include <stddef.h>

#define DST_IDLE_MS 2000u   /* nothing moved this long: "Nothing copying" */

typedef enum { DST_IDLE, DST_WRITING, DST_READING } dst_status;

typedef struct {
	bool started;
	unsigned long long rd0, wr0;     /* the sectors at the last look */
	unsigned long long read, wrote;  /* bytes since the export began */
	unsigned last_ms;                /* the last look */
	unsigned moved_ms;               /* when a sector last moved */
	unsigned long long rate;         /* bytes a second, the status's way */
	dst_status status;
	char shown[96];                  /* what the page shows, to see a change */
} dst_state;

/* One look: the stat's sectors read and written, at now_ms. True when what
 * the page shows changed (status, speed or a total, as tokens). */
bool dst_step(dst_state *s, unsigned long long rd_sectors, unsigned long long wr_sectors,
              unsigned now_ms);

/* The tokens of a number on the page */
enum {
	DST_T_0 = 0,           /* DST_T_0 + d for the digit d */
	DST_T_DEC = 10,        /* the decimal mark */
	DST_T_SP,              /* the space before a unit */
	DST_T_B, DST_T_KB, DST_T_MB, DST_T_GB,
	DST_T_PS,              /* "/s" after a unit */
	DST_TOKENS
};
#define DST_FMT_MAX 16

/* A total: "312 MB", "1.24 GB", "0 B". Returns the count of tokens. */
int dst_fmt_total(unsigned long long bytes, int *tok, int max);

/* A speed: "512 KB/s", "23.4 MB/s". Returns the count of tokens. */
int dst_fmt_rate(unsigned long long bytes_per_s, int *tok, int max);

/* Tokens as text, for checks: digits, the mark as `dec`, units in English */
void dst_tokens_text(const int *tok, int n, char dec, char *out, size_t on);

#endif
