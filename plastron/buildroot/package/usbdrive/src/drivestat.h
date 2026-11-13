/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What the Flash Drive page says while the card is the computer's: for each
 * way, to the card (the computer writes) and from it (the computer reads),
 * whether it moves, how fast, and how much since the export began.
 *
 * The handheld sees blocks, not files. What moves is counted where the
 * gadget serves it (2026-11-12): the bytes the f_mass_storage kernel thread
 * ("file-storage") read and wrote, its /proc/<pid>/io rchar and wchar, as
 * each USB command is served. The games partition's /sys stat, which this
 * first read, counts a write only once the kernel writes its page cache to
 * the card, up to dirty_expire (30 s) later: a copy that ended seconds ago
 * read as nothing written, and B ejected without asking (measured on the
 * Pixel 2: 4 MB written, the stat still flat at 30 s). The stat stays as a
 * fallback when the thread cannot be read, and then B always asks
 * (drive.h's drv_copying). Each way is moving while its bytes move and for
 * DST_IDLE_MS after (a pause between two files keeps the last speed), then
 * idle at 0; its speed over the last look; its total. No time left: a
 * block count cannot know how many files are coming.
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
	unsigned long long now_bps; /* bytes a second over the last look alone:
	                             * 0 for a look that moved nothing, with no
	                             * DST_IDLE_MS wait (the side lights') */
} dst_way;

typedef struct {
	bool started;
	unsigned long long rd0, wr0;     /* the bytes at the last look */
	unsigned last_ms;                /* the last look */
	dst_way in, out;                 /* to the card (written), from it (read) */
	char shown[168];                 /* what the page shows, to see a change */
	bool lit;                        /* the side lights were told files move */
} dst_state;

/* One look: the bytes read from the card and written to it so far, at
 * now_ms. True when what the page shows changed (a way's state, speed or
 * total, as tokens). */
bool dst_step(dst_state *s, unsigned long long rd_bytes, unsigned long long wr_bytes, unsigned now_ms);

/* What the side lights hear after a look (plastron's gauge, package/gauge/
 * src/lights.h: "move IN OUT", bytes a second to the card and from it): each
 * way's now_bps, a way under DST_LIGHTS_MIN taken as still (a computer's own
 * small writes when it opens the drive, a folder's list); "move 0 0" once
 * when both go still. False when there is nothing to say: both still, and
 * the lights already told so, or a line that does not fit `cap`. Once after
 * each dst_step. */
#define DST_LIGHTS_MIN 65536ull
bool dst_lights_line(dst_state *s, char *out, size_t cap);

/* A task's /proc/<pid>/io read: its rchar and wchar. False when either is
 * missing. */
bool dst_parse_io(const char *text, unsigned long long *rchar, unsigned long long *wchar);

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
