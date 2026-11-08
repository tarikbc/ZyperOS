/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PKKEYS_H
#define ZYPEROS_PKKEYS_H

/* What portkeys (tools/portkeys.c) makes of the Pixel 2's volume keys while a
 * port runs: which key events are a step, of the volume or the backlight, and
 * the step itself on ZyperOS's levels, saved in the launcher's database.
 *
 *   VOL+ / VOL-          the volume, a position at a time
 *   MENU held + VOL      the backlight instead, a rung at a time
 *
 * As the launcher has it (src/device/pixel2.c's key_event): a volume key that
 * goes down while MENU is held is a brightness key until it is let go, MENU let
 * go first or not; the kernel's own autorepeat (value 2) is not a step; and a
 * key held down repeats on the launcher's clock, REPEAT_DELAY_MS and then every
 * REPEAT_RATE_MS (src/keyrepeat.h), from the time the key was handled.
 *
 * MENU and the volume keys are on two devices, the joypad and gpio-keys, and
 * a chord comes as two events microseconds apart, one on each. Read device by
 * device, the order the devices were found in decided which came first, so
 * MENU+VOL+ could be read VOL+ first and step the volume. So the events of a
 * pass over the devices are handled in the order of their times, whatever
 * device they came from, and one stamped after the pass began waits for the
 * next pass: the device read before it may hold an earlier event that was not
 * there yet when it was read.
 *
 * No SDL and no device: the evdev numbers are the kernel's, written out here
 * (portkeys.c holds them to <linux/input.h>), so tools/pkkeys-check.c feeds it
 * events on the host. SQLite only for the levels. */

#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* <linux/input.h>'s numbers for the three keys */
#define PK_KEY_VOLUMEDOWN 114
#define PK_KEY_VOLUMEUP   115
#define PK_BTN_MENU       0x2c0      /* BTN_TRIGGER_HAPPY1: the Pixel 2's FUNCTION, which is MENU */

/* An EV_KEY event as read: its time (ev.time, on the clock the devices were
 * set to), its code and value (1 down, 0 up, 2 the kernel's autorepeat). Any
 * code may be given; only the three above mean anything here. */
typedef struct {
	int64_t  t_us;
	uint16_t code;
	int32_t  value;
} pk_key;

typedef enum { PK_VOLUME, PK_BRIGHT } pk_kind;

/* A step the keys asked for: of which level, which way, and whether it is a
 * held key's repeat rather than its press. */
typedef struct {
	pk_kind kind;
	int     dir;                 /* +1 or -1 */
	bool    held;
} pk_step;

#define PK_PASS_MAX 256          /* events one pkkeys_pass is handed at most */
#define PK_WAITING  64           /* events kept for the next pass at most */

typedef struct {
	bool menu_held;              /* MENU is down */
	/* MENU on its own, as Diatom reads it (diatom/src/main.c): armed by its
	 * press, a modifier once a step goes under it (MENU+VOL, the backlight),
	 * and a tap when it is let go still armed. menu_tapped is that tap, for
	 * pkkeys_menu_tapped to take. */
	bool menu_armed, menu_tapped;
	struct {
		bool    down;
		bool    bright;          /* went down under MENU: a brightness key until let go */
		int64_t next_ms;         /* when it repeats next */
	} key[2];                    /* VOL+, VOL- */
	pk_key waiting[PK_WAITING];  /* stamped after the last pass began */
	int    nwaiting;
} pkkeys;

void pkkeys_init(pkkeys *k);

/* Filter before buffering: only MENU/VOL press and release transitions. */
bool pkkeys_relevant(uint16_t code, int32_t value);

/* One complete, bounded pass over ALL open nonblocking devices. `cutoff_us`
 * is sampled before the reads on their shared event clock; `now_ms` is on
 * CLOCK_MONOTONIC. Sort pending and current events together, stably. A new
 * event beyond the cutoff defers the entire suffix, including later pending
 * events. Otherwise pending events mature on this pass even if the clock
 * went back: a future timestamp alone cannot cause repeated zero waits.
 * INT64_MAX takes everything now.
 *
 * Preflight both limits before handling ANY event. Overflow returns -1,
 * clears pending/held state and produces no steps. The caller discards this
 * whole pass and resyncs all devices before accepting another batch. The
 * reader must never flush a partial batch to make room. `out` must hold all
 * possible steps (PK_WAITING + PK_PASS_MAX for a full pass). */
int pkkeys_pass(pkkeys *k, const pk_key *ev, int n, int64_t cutoff_us, int64_t now_ms,
                pk_step *out, int max);

/* Due repeats, at most one per held key. Pending events suppress them;
 * a late repeat restarts its interval from now, with no catchup burst. */
int pkkeys_tick(pkkeys *k, int64_t now_ms, pk_step *out, int max);

/* How long poll may wait before pkkeys_tick has a repeat to give, in ms: -1
 * for nothing held, 0 when one is due or events wait for a pass. */
int pkkeys_wait(const pkkeys *k, int64_t now_ms);

/* MENU was let go without a step under it since the last call: the in-game
 * menu over a port that has one (portkeys --menu). Taken once: the next call
 * is false until MENU is tapped again. A MENU only found down at a resync was
 * never pressed here, so its release is no tap, as in Diatom a MENU still held
 * as the game resumes is not. */
bool pkkeys_menu_tapped(pkkeys *k);

/* What a device says is down when its events cannot be trusted (after a
 * SYN_DROPPED, from EVIOCGKEY) or when it has gone, which is all up: MENU, and
 * the volume keys. A volume key let go in events that were lost stops; one
 * found down is left up, without a step. */
void pkkeys_menu_state(pkkeys *k, bool down);
void pkkeys_vol_state(pkkeys *k, bool up_down, bool down_down);

/* ---- the levels ------------------------------------------------------------
 *
 * ZyperOS's own, src/device/pixel2_levels.h's: 21 volume positions, 0 to
 * VOL_MAX, and 12 backlight rungs, 0 to BRIGHT_MAX, in the launcher's device
 * database as src/db.c keeps them, the "settings" table's "volume" and
 * "brightness". */
typedef struct {
	int vol;                     /* -1: never saved, and left alone until a press */
	int bright;
} pk_levels;

/* A setting as an integer, `fallback` when it is not there or there is no
 * database; and a level written the way db_set_int writes it. */
int  pk_db_get(sqlite3 *db, const char *key, int fallback);
void pk_db_set(sqlite3 *db, const char *key, int v);

/* As the launcher starts: the saved levels, or for the backlight, the rung
 * nearest what the panel is at (the sysfs file at `backlight`), or the middle. */
void pk_levels_load(sqlite3 *db, const char *backlight, pk_levels *l);

/* One step on the levels, saved when it moved the level; a volume never saved
 * starts from the middle. Writes the log line for it into `line`, as
 * "portkeys: volume 5 -> 6 (VOL+)\n", or "" for a held key's repeat that did
 * not move (at an end of the scale). True when the level moved. */
bool pk_apply(sqlite3 *db, pk_levels *l, const pk_step *s, char *line, size_t n);

#endif
