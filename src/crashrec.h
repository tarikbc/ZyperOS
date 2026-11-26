/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's own crash, recorded where plastron's crash report finds it.
 *
 * A crash of the launcher left one line, the shell's "Segmentation fault",
 * and nothing to say where: no signal's address, no backtrace, and no
 * zyperos-crash.txt, which plastron's crashlog makes only of what the kernel
 * kept across a reset (a panic, a reset Linux did not ask for). The launcher
 * started again a moment later, so a player saw a flash of the boot screen
 * and the report had nothing to go on (two of them on a Pixel 2 on Discord,
 * 2026-11-23, at a USB File Transfer's start).
 *
 * crashrec_install catches SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT, and
 * before the process dies of it writes what happened: the signal, its code,
 * the address it was about, the thread, the version, and the backtrace,
 * each frame as the binary and its offset (glibc's backtrace_symbols_fd),
 * which addr2line turns into a line of the source with the build's own
 * binary. It goes to stderr, which is the logs' zyperos.log, and into a
 * record file. Then the signal's own action: the process dies of it as
 * before, and the shell still says so.
 *
 * The launcher that starts next hands the record to crashlog --launcher
 * (main.c, crashrec_waiting), which makes it a report as it makes a panic's:
 * crash-DATE.txt in the logs folder, which Download logs packs, and
 * zyperos-crash.txt on PX2BOOT. One crash report, wherever the crash was.
 *
 * Only what a signal handler may do: write, open, close, backtrace (asked
 * once here first, so its library is loaded before it is needed), on a stack
 * of its own for the main thread, so a stack that ran out still says so. */
#ifndef CRASHREC_H
#define CRASHREC_H

#include <stdbool.h>

/* Where the launcher's record waits for the next start: /run, which a
 * reboot empties, so a record is only ever this boot's */
#define CRASHREC_PATH "/run/zyperos-launcher-crash.txt"
/* plastron's crash report (the Pixel 2's image); where it is not, the
 * record stays in /run and the log has it */
#define CRASHLOG "/usr/sbin/crashlog"

/* Catch the signals above from now on, for this process and its threads.
 * `record` is the file to write (NULL: stderr alone), `version` said in it. */
void crashrec_install(const char *record, const char *version);

/* Whether a launcher before this one left a record at `record` */
bool crashrec_waiting(const char *record);

#endif
