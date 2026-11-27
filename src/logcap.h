/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LOGCAP_H
#define ZYPEROS_LOGCAP_H

#include <stdbool.h>

/* Each zyperos.log held to about 2 MB (1.1.1).
 *
 * launch.sh keeps ten boots of the log, but one boot's had no limit: a long
 * session with a loop that says something every frame could write tens of
 * megabytes to the card, and a log that size is no use to anyone reading it.
 *
 * Past LOGCAP_MAX the file keeps its start, the boot (up to LOGCAP_HEAD,
 * to a whole line), a line saying how many lines went,
 *
 *     ... 41210 lines dropped ...
 *
 * and its newest LOGCAP_TAIL, from a whole line, so it can grow by the
 * difference again before the next trim. A later trim adds what it drops to
 * the count already there. Every line kept is whole, so src/logpack.h reads
 * the file as any other; plastron's crash reports are files of their own and
 * never in it.
 *
 * In place, in the same file, as everything that writes to it - the launcher,
 * Diatom, the cores, launch.sh's own lines - appends (">>", O_APPEND) and so
 * goes on writing at its new end. A line written in the instant between the
 * last read and the truncate can be lost; that is the price of not stopping
 * every writer. No cost on a line: the launcher looks at the size every
 * LOGCAP_EVERY_S seconds, on a thread of its own, during a game too. */

#define LOGCAP_MAX     (2L * 1024 * 1024)
#define LOGCAP_HEAD    (256L * 1024)
#define LOGCAP_TAIL    (1536L * 1024)
#define LOGCAP_EVERY_S 10

/* Trim `path` when it is over `max`: its first `head` bytes and its last
 * `tail`, both to whole lines, around the marker. How many lines went this
 * time, 0 when it was under `max` or could not be read, -1 on a failure. */
long logcap_trim(const char *path, long max, long head, long tail);

/* The launcher's log: the file its stderr is (/proc/self/fd/2), only when that
 * is a regular file, watched every LOGCAP_EVERY_S seconds. Nothing on the
 * host, or with stderr a terminal. */
void logcap_start(void);

#endif
