/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_KLOG_H
#define ZYPEROS_KLOG_H

#include <stdbool.h>
#include <stddef.h>

/* The log lines that have to outlive a freeze.
 *
 * The launcher's log, zyperos.log on the card, is its stderr (launch.sh). A
 * line written there waits in the page cache for up to half a minute before
 * it is on the card, so a handheld that freezes and is then held off, or is
 * reset, loses its last lines: the ones that would have named the step it
 * froze on. A Pixel 2 that stopped inside File Transfer's opening, in a write
 * to the USB controller, left "night: Night Mode on" as its last line, a
 * minute older than the freeze (2026-11-21).
 *
 * So the lines of the risky parts - the USB cable's (usb:, wire:), File
 * Transfer's (xfer:), the name on the network (mdns:), a crash's record
 * (crash:), the power button's and the power-off's (power:) and the
 * watchdog's own (watchdog:) - go to the kernel's log too, /dev/kmsg, as
 * "zyperos: <the line>". pstore's console keeps the kernel's log in RAM
 * across a warm reset, and plastron's crashlog puts its end in the report at
 * the next start (its console-ramoops). And the log file is fdatasync'ed
 * after them, by a thread of its own: the line is on the card a moment later
 * and the caller never waits for the card. Every other line goes as it
 * always did. These are rare (a dozen when a cable is plugged in), so the
 * normal paths cost nothing: tools/klog-check.c measures it.
 *
 * Where it can (glibc's fopencookie, macOS's funopen), klog_open puts a
 * filter in front of stderr: every fprintf(stderr, ...) goes through it, a
 * line at a time, and no call site changes. A forked child that does not
 * exec, or a signal handler, which may not use stdio, says its line with
 * klog_raw. */

/* The rule: a line that starts with one of the kept prefixes */
bool klog_kept(const char *line, size_t n);

/* The kernel's log at `kmsg` (NULL: /dev/kmsg, and the kernel told not to
 * drop a burst of lines from it) and the filter in front of stderr. False,
 * with stderr as it was, where there is no such file or no filter. Once. */
bool klog_open(const char *kmsg);

/* Async-signal-safe: `line` (one line, its newline optional) to stderr's
 * descriptor and to the kernel's log, kept prefix or not, with no sync. For
 * a forked child and a crash's handler. */
void klog_raw(const char *line);

/* Async-signal-safe, and never waits on the card: `line` to the kernel's
 * log only (the watchdog's, whose file line goes another way) */
void klog_kmsg(const char *line);

/* In a forked child that does not exec, async-signal-safe: every descriptor
 * closed but stdin, stdout, stderr and the kernel's log, which moves to 3.
 * A child left stuck in a driver then holds nothing of the launcher's: not
 * the display (a launcher started again could not take it), not the
 * transfer server's port, not Diatom's socket. `maxfd`: from klog_maxfd,
 * asked before the fork. */
void klog_child_fds(int maxfd);
int  klog_maxfd(void);

/* The last line the log had, and the last kept one, for the watchdog's
 * report ("" before any). Never waits: a line being written leaves the last
 * copy as it was. */
void klog_last(char *line, size_t n, char *kept, size_t kn);

/* For the check: lines copied to the kernel's log, those left out over the
 * rate, and the log's syncs done */
unsigned klog_mirrored(void);
unsigned klog_dropped(void);
unsigned klog_synced(void);

/* For the check: the rate's burst whole again */
void klog_rate_reset(void);

/* For the check: the sync thread done with everything asked of it (up to
 * `ms`); true when it is */
bool klog_settle(unsigned ms);

#endif
