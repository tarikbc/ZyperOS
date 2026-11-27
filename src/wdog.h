/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_WDOG_H
#define ZYPEROS_WDOG_H

#include <stdbool.h>

/* The launcher's watchdog: a thread that sees the main loop stop.
 *
 * The launcher is the handheld's whole face: it draws every screen and reads
 * every button, POWER's too. When its main thread stops (a driver that never
 * answers a write, a lock that is never let go) the screen keeps its last
 * frame and nothing a player does reaches anything, until they hold POWER
 * for the PMIC's cut, which takes the RAM and its log with it. One Pixel 2
 * stopped like that in a write to its USB controller (hwput.h, 2026-11-21):
 * no panic, no reset, no line said.
 *
 * So the main loop feeds the watchdog wherever it comes round: each frame
 * drawn (plat_present), each look at the buttons (plat_input_poll and
 * plat_input_wait), and each turn of the loops that wait while another
 * program has the screen (a game in Diatom, plat_resident_wait; a program
 * run the slow way, plat_run). A feed is one store. If the loop has not come
 * round for WDOG_DARK_MS, the watchdog says so in the log, with the log's
 * last lines (klog.h: they name the stage), and turns the backlight off, so
 * a frozen screen does not stay lit on a battery. At WDOG_REBOOT_MS it syncs
 * what it can (sysrq s) and restarts the handheld (sysrq b), and plastron's
 * crashlog reports it at the next start with those lines, which pstore's
 * console kept. A loop that comes back before then turns the screen on
 * again and is said.
 *
 * Some of the loop's own work is long and legitimately does not come round:
 * Bluetooth's power and pairing, Wi-Fi's joins, the hotspot's start and stop,
 * Storage's deletes of whole folders, Download logs' archive, a card's sync,
 * a whole read of the card's shelves. Each is marked, wdog_hold before and
 * wdog_release after, and while one is held the watchdog waits however long
 * it takes. A hold is named, and nests.
 *
 * Device-generic: the backlight is the platform's own call (plat_backlight),
 * handed in, and the restart the kernel's. Started by the launcher's main
 * mode only, never in a host's shot or the other modes. tools/wdog-check.c
 * runs it with short times and a stuck loop. */

#define WDOG_DARK_MS   60000u
#define WDOG_REBOOT_MS 120000u

typedef struct {
	unsigned    dark_ms, reboot_ms;
	unsigned    tick_ms;          /* how often it looks */
	unsigned    sync_ms;          /* from the emergency sync to the restart */
	const char *sysrq;            /* /proc/sysrq-trigger */
	void      (*backlight)(bool on);
	unsigned    stall_ms;         /* the test hook's stop (0: WDOG_STALL_S) */
} wdog_cfg;

enum { WDOG_FINE, WDOG_DARK, WDOG_REBOOT };

/* Pure: what the watchdog does at `now` with the loop last fed at `fed`
 * (milliseconds of a monotonic clock, which may wrap), a task held or not */
int wdog_judge(unsigned now, unsigned fed, bool held, const wdog_cfg *c);

/* The thread, with these times (NULL: the device's, WDOG_DARK_MS,
 * WDOG_REBOOT_MS, /proc/sysrq-trigger, and plat_backlight given here) */
bool wdog_start(const wdog_cfg *c, void (*backlight)(bool on));
void wdog_stop(void);

/* The main loop came round. A store; and the test hook: SIGUSR2 to the
 * launcher stops the loop here for WDOG_STALL_S, on purpose, so a handheld
 * shows what the watchdog does (docs/building.md). */
#define WDOG_STALL_S 150
void wdog_feed(void);

/* A long task of the main loop's own begins (named, for the log), and ends */
void wdog_hold(const char *what);
void wdog_release(void);

/* For the check: what was done so far */
typedef struct { unsigned darks, lights, reboots; } wdog_seen;
wdog_seen wdog_done(void);

#endif
