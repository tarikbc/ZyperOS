/* SPDX-License-Identifier: GPL-3.0-only */
/* The start before this one did not finish, or the handheld froze, and was
 * restarted: what the next start says about it (DESIGN.md 11.70).
 *
 * A start that stops in the kernel (a GKD Pixel 2 that stays black after a
 * cold start, roverfine, 1.1.0) or a handheld that freezes while playing is
 * restarted by a watchdog or a panic, and plastron's crashlog writes a report
 * at the next start: zyperos-crash.txt on PX2BOOT and crash-*.txt in the logs
 * Download logs packs. Beside it, a summary of a few lines (LASTBOOT_RUN,
 * key=value): why (reason), where it stopped (stage), when (kernel_time,
 * the kernel's clock at its last line, and the date). This reads it.
 *
 * Said once, by Zyper's bubble on the home (11.23's, with the code), and kept
 * for LASTBOOT_KEEP_DAYS in a row of About ZyperOS's first page, so the code
 * a helper on the Discord asks for is still there after the bubble has gone.
 * The summary is copied onto the card (P_USERDATA/LASTBOOT_FILE) the first
 * time it is seen, with the day it was seen: the same report in /run again (a
 * launcher started again after a game) is not said again.
 *
 * The code, "K1W · 0.6 s": the stage, the reason and the kernel's time, in
 * letters that read the same in every language and from a photo.
 *
 *   stage   K1  the kernel, before the card was found (the card's probe)
 *           K2  the kernel, the card found, before init
 *           C1  the card's games partition mounting (datapart)
 *           C2  that partition made and formatted (a card's first start)
 *           C3  its check after a power-off that was not clean (fsck.exfat)
 *           U1  System Update's switch (update-apply.sh)
 *           U2  ZyperOS copied onto the card (the first start's copy)
 *           L1  the launcher starting, up to its first frame
 *           H   after the home: a freeze while playing
 *   reason  W   a watchdog restarted it: the launcher's, or the chip's (a
 *               reset with no panic before the home)
 *           P   a kernel panic (a lockup and an RCU stall panic too)
 *           T   a task stuck (hung_task)
 *           R   the card was not found (rootwait)
 *           X   a reset after the home with no report of its own
 *
 * The time: one decimal under 10 s, whole seconds from there, "." whatever
 * the language (a code, not a number to read).
 *
 * Pure: no SDL, the caller's paths and clock, so tools/lastboot-check.c holds
 * it on a build machine. */
#ifndef ZYPEROS_LASTBOOT_H
#define ZYPEROS_LASTBOOT_H

#include <stdbool.h>
#include <stddef.h>

/* plastron's crashlog leaves this start's summary here (tmpfs, this start
 * only); its copy on the card, under P_USERDATA */
#define LASTBOOT_RUN       "/run/zyperos-lastboot.txt"
#define LASTBOOT_FILE      "lastboot.txt"
#define LASTBOOT_KEEP_DAYS 30

typedef struct {
	bool   valid;
	char   stage[4];       /* "K1" .. "L1", "H" */
	char   reason;         /* 'W', 'P', 'T', 'R', 'X' */
	double kernel_time;    /* seconds, the kernel's clock at its last line */
	char   time[24];       /* "2026-11-23 22:14:05", or "" */
	char   version[32];
	char   device[16];
	char   last[128];      /* the kernel's last line, for the log */
	long   seen;           /* the card's copy: when it was first said (epoch s), 0 none */
} lastboot;

/* Reads a summary from `text`: key=value lines, unknown keys and lines
 * passed over, a stage or a reason in letters or in crashlog's words
 * ("watchdog", "panic", "rootwait", "hung_task", "lockup", "rcu", "reset";
 * "kernel", "card", ...). Valid with a stage and a reason it knows. */
bool lastboot_parse(const char *text, lastboot *out);
bool lastboot_read(const char *path, lastboot *out);

/* "K1W · 0.6 s" into out, its spaces no-break */
void lastboot_code(const lastboot *lb, char *out, size_t n);

/* The same report: one stage, reason, kernel time and date */
bool lastboot_same(const lastboot *a, const lastboot *b);

/* A freeze while playing (stage H) rather than a start that did not finish */
bool lastboot_froze(const lastboot *lb);

/* The card's copy, with its seen day, written whole then renamed */
bool lastboot_write(const char *path, const lastboot *lb);

/* What this start does with `run` (this start's summary, may be invalid) and
 * `kept` (the card's copy, may be invalid), at `now` (epoch s): true when the
 * bubble says it now; *keep is then what to write as the card's copy (run
 * with seen = now). */
bool lastboot_due(const lastboot *run, const lastboot *kept, long now, lastboot *keep);

/* The About row shows `kept` at `now`: seen within LASTBOOT_KEEP_DAYS, or a
 * clock that went back (now before seen) */
bool lastboot_row(const lastboot *kept, long now);

/* The launcher's side, not pure: lastboot_watch (src/main.c), once a pass
 * of the shelf, reads this start's summary (LASTBOOT_RUN, or
 * $ZYPEROS_LASTBOOT_RUN on a computer) against the card's copy the first
 * time, and when it is new has Zyper's bubble say it (lastboot_bubble,
 * src/shelf_draw.c) as soon as nothing else of his is up, and writes the
 * card's copy then. lastboot_about (src/lastboot_ui.c): the About row's
 * value, the code, while the card's copy is within LASTBOOT_KEEP_DAYS;
 * false for no row. */
bool lastboot_about(char *out, size_t n);

#endif
