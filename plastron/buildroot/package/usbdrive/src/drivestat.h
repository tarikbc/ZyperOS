/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What the Flash Drive page says while the card is the computer's: for each
 * way, to the card (the computer writes) and from it (the computer reads),
 * whether it moves, how fast, and how much since the export began.
 *
 * The handheld sees blocks, not files: the games partition's /sys stat
 * (sectors read, field 3, and written, field 7, 512 bytes each whatever the
 * card's own block size). So each way says what its blocks say: moving
 * while its sectors move and for DST_IDLE_MS after (a pause between two
 * files keeps the last speed), then idle at 0; its speed over the last
 * look; its total. No time left: a block count cannot know how many files
 * are coming.
 *
 * A stat that goes back (a counter that wrapped, or a partition that came
 * back) is taken as a new start for that counter, never as a negative
 * number or a huge one.
 *
 * The numbers become tokens (dst_fmt_*): digits, the decimal mark, a space,
 * a unit, "/s"; the page's glyph atlas (ZyperOS draws it, src/main.c's
 * usb_drive) has a picture for each, in the language's own mark and units.
 * 1000-based, as the computers' own file windows count.
 *
 * Pure: no files, no clock of its own. tests/drive-check.c holds it.
 */
#ifndef USBDRIVE_DRIVESTAT_H
#define USBDRIVE_DRIVESTAT_H

#include <stdbool.h>
#include <stddef.h>

#define DST_IDLE_MS 2000u   /* nothing moved this long: the way is idle, at 0 */

/* One way: to the card or from it */
typedef struct {
	unsigned long long total;   /* bytes since the export began */
	unsigned long long rate;    /* bytes a second; 0 when idle */
	bool active;                /* moved in the last DST_IDLE_MS */
	unsigned moved_ms;          /* when it last moved */
} dst_way;

typedef struct {
	bool started;
	unsigned long long rd0, wr0;     /* the sectors at the last look */
	unsigned last_ms;                /* the last look */
	dst_way in, out;                 /* to the card (written), from it (read) */
	char shown[168];                 /* what the page shows, to see a change */
} dst_state;

/* One look: the stat's sectors read and written, at now_ms. True when what
 * the page shows changed (a way's state, speed or total, as tokens). */
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

/* A speed: "0 MB/s", "512 KB/s", "23.4 MB/s". Returns the count of tokens. */
int dst_fmt_rate(unsigned long long bytes_per_s, int *tok, int max);

/* Tokens as text, for checks: digits, the mark as `dec`, units in English */
void dst_tokens_text(const int *tok, int n, char dec, char *out, size_t on);

#endif
