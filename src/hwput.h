/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HWPUT_H
#define ZYPEROS_HWPUT_H

#include <stdbool.h>

/* Writes to the USB controller's files, made by a child process the
 * launcher never waits on for long.
 *
 * A write to a driver's file returns when the driver is done, and a driver
 * whose hardware stopped answering may never be. The Pixel 2's controller
 * (dwc2) hung in a write of "disconnect" to its soft_connect, as File
 * Transfer opened over a link in use: "dwc2_core_reset: HANG! Soft Reset
 * timeout", and the launcher, which made the write on its own main thread,
 * stopped with it. The screen kept its last frame and POWER, which the
 * launcher reads, did nothing; there was no panic (a task stuck in a driver
 * is only said, linux-px2.config) and no watchdog (2026-11-21).
 *
 * So the controller's writes - soft_connect, the gadget's UDC (its bind and
 * its unbind) - are a job: a child process forked for it makes them in
 * order, each said in the log just before it ("usb: soft_connect
 * disconnect"), so a write that hangs names itself. The launcher waits for
 * the child HWPUT_TIMEOUT_MS at most, or not at all; one that has not
 * finished by then is said ("usb: the controller did not answer"), and the
 * launcher carries on: its screen, its buttons and POWER work, and the
 * screen shows the cable's own trouble (the link steps' hint, linksteps.h).
 * The child is reaped when it ends (hwput_reap), whenever that is, so none
 * is left a zombie; until then no other job starts, since a second write
 * would only hang behind the first, and hwput_busy says so to anybody who
 * would read the controller's files.
 *
 * Only the controller's own writes: the files read (its state, usb0's
 * carrier) answer from memory and never wait on the hardware. */

/* One write. `say` is the line said before it; `text` is written in one
 * write, as is; then `pause_ms` before the next. */
typedef struct {
	const char *say;
	const char *path;
	const char *text;
	unsigned    pause_ms;
} hwput_step;

typedef struct {
	const char *what;          /* in the launcher's own lines: "soft_connect" */
	/* Asked first, in the child, when set: false leaves the writes out (a
	 * link that is fine as it is). Async-signal-safe calls only. */
	bool      (*need)(void *arg);
	void       *arg;
	const hwput_step *steps;
	int         n;
} hwput_job;

enum {
	HWPUT_DONE,       /* every write made */
	HWPUT_SKIPPED,    /* need said none was needed */
	HWPUT_FAILED,     /* a write failed (said in the log) */
	HWPUT_TIMEOUT,    /* not done in time: the child goes on alone */
	HWPUT_BUSY,       /* an earlier job's child is still in a write */
	HWPUT_STARTED,    /* not waited for: the child at work */
	HWPUT_NOFORK      /* no child: nothing written */
};

#define HWPUT_TIMEOUT_MS 2000u

/* The job, waited for up to the timeout (`wait`), or started and left. */
int hwput_run(const hwput_job *job, bool wait);

/* A job's child still at work; reaps one that ended first. */
bool hwput_busy(void);

/* A child that ended reaped, and a late one said once it is late. Cheap
 * (nothing at all with no child); asked by every hwput call and by the
 * screens that follow the cable. */
void hwput_reap(void);

/* For the check: another timeout than HWPUT_TIMEOUT_MS */
void hwput_set_timeout(unsigned ms);

/* For desk mode's stand-in cable (src/device/deskcable.c): the child waits
 * this long before each write, as a slow controller does */
void hwput_test_delay(unsigned ms);

/* For the check: the last job's result once its child ended (-1 before) */
int hwput_last(void);

/* The result of a job whose child ended since this was last asked, once
 * (HWPUT_DONE, HWPUT_SKIPPED or HWPUT_FAILED), or -1: for a caller that did
 * not wait (gadget.h) to act on its end. Reaps first. */
int hwput_take(void);

#endif
