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
 * events on the host. SQLite only for the levels, and a mapped file for the
 * volume boost's gain (pk_boost, below). */

#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boost.h"
#include "osdbar.h"

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

/* VOL+ is down now as a step of `kind`: what it went down as, MENU let go
 * since or not. What the boost's hold on the top goes by (pk_boost_tick). */
bool pkkeys_up_held(const pkkeys *k, pk_kind kind);

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

/* ---- the boost past the top --------------------------------------------------
 *
 * As the launcher has it (src/levels.c, on src/boost.h; DESIGN.md 11.6): at
 * the top, VOL+ held BOOST_HOLD_MS more, or pressed BOOST_TAPS more times with
 * no more than BOOST_GAP_MS between two, opens cells past it. The volume's are
 * BOOST_DB_CELL dB of software gain each, which the port's ALSA plugin
 * (plastron's zyperos-eq) reads every period from the shared file
 * (src/eqdsp.h's eq_share); the launcher has exited, so the gain there is
 * this one's to write, on every change and as portkeys starts. The
 * backlight's are a lift of the mid-tones in the display's gamma table, which
 * portkeys loads (its port_gamma, src/panelgamma.h's lift). The cells filled
 * are kept in the database beside the level, under the launcher's own keys,
 * so the shelf comes back with the boost the port was left at. Over a port
 * both open (BOOST_OPEN), and a level step does what it did before (pk_apply).
 *
 * The functions that move the cells keep them and write the volume's gain
 * themselves, and return src/boost.h's BOOST_LEVEL, BOOST_GAIN, BOOST_OPENED
 * for portkeys to do the rest: the hardware's level, the lift, the bar. */
#define PK_VOL_BOOST    "volume_boost"
#define PK_BRIGHT_BOOST "brightness_boost"

typedef struct {
	boost_bar   bar[2];          /* [PK_VOLUME], [PK_BRIGHT] */
	bool        up_was[2];       /* VOL+ held as that kind at the last pk_boost_tick */
	bool        up_seen[2];      /* a step going up since then */
	const char *share_path;      /* the file the gain goes in */
	void       *share;           /* it, mapped; NULL until it maps */
} pk_boost;

/* Nothing open, the gain going to `share_path`, or for NULL to
 * $ZYPEROS_EQ_FILE, else EQ_SHARE_PATH, as the launcher has it (src/eq.c) */
void pk_boost_init(pk_boost *b, const char *share_path);

/* As portkeys starts, after pk_levels_load, and again when the launcher's
 * in-game menu has been up: each bar at its level (a volume never saved at
 * the middle, as pk_apply starts it), with the cells kept beside it, which
 * count only at the top; and the volume's gain written to the file. False
 * when the file could not be had: the next change tries again. */
bool pk_boost_load(sqlite3 *db, const pk_levels *l, pk_boost *b);

/* The volume's gain into the file: mapped the first time, made as the
 * launcher makes it when it is not there, written only when it differs.
 * False when the file could not be had. */
bool pk_boost_share(pk_boost *b);

/* The mapping let go of, for a check that makes another */
void pk_boost_free(pk_boost *b);

/* One step through the boost (src/boost.h's boost_key) at `now` ms: a level
 * step, or a press at an end that moved nothing, is pk_apply's, its saving
 * and its line; the cells alone have a line of their own, "portkeys: volume
 * boost +2 -> +4 dB (VOL+)", and are kept, with the gain written. */
int pk_press(sqlite3 *db, pk_levels *l, pk_boost *b, const pk_step *s, uint32_t now,
             char *line, size_t n);

/* The clock, on every pass of portkeys' loop after its steps: VOL+ let go
 * (seen down at the last pass, or pressed in this one and up already), a hold
 * on the top charging and, BOOST_HOLD_MS on, opening; presses that stopped
 * draining. For `kind`'s bar; an opening is kept, the gain written. */
int pk_boost_tick(sqlite3 *db, pk_boost *b, const pkkeys *k, pk_kind kind, uint32_t now);

/* Whether the bar should be drawn every frame: a boost charging, opening,
 * filling or folding, or VOL+ held on the top of a closed one */
bool pk_boost_moving(const pk_boost *b, uint32_t now);

/* The bar went away: an empty boost closes, a filled one stays. Returns which
 * closed, 1 << kind, each kept and the gain written. */
int pk_boost_gone(sqlite3 *db, pk_boost *b);

/* The output changed (a plug in or out): the volume boost closes, kept and
 * its gain 0 in the file. True when one was open. */
bool pk_boost_output_changed(sqlite3 *db, pk_boost *b);

/* The cells filled of `kind` (0 closed), the volume's gain in dB, the
 * backlight's lift (panelgamma_color's) */
int pk_boost_cells(const pk_boost *b, pk_kind kind);
int pk_boost_gain(const pk_boost *b);
int pk_boost_lift(const pk_boost *b);

/* The bar for `kind` at `now`, as osdbar_render_look draws it */
void pk_boost_look(pk_boost *b, pk_kind kind, uint32_t now, osdbar_look *l);

/* ---- the boost's buzz as it opens --------------------------------------------
 *
 * As the launcher gives it (src/levels.c) and Diatom in a game
 * (diatom/src/levels.c): a short buzz of the motor as either boost opens, a
 * press or a hold, PK_BUZZ_MS long, at MENU > Sound > Rumble's share of full
 * strength (src/rumble.h: Off none, Low 35%, Medium 65%, Full all of it), the
 * level the launcher's own buzz goes by. portkeys plays it on the joypad's
 * force feedback (its boost_buzz). Before this, over a port nothing was felt:
 * BOOST_OPENED came back from pk_press and pk_boost_tick and nothing read it. */
#define PK_BUZZ_MS 40

/* MENU > Sound > Rumble's share, percent, as src/rumble.c's rumble_menu and
 * rumble_level_pct give it: the device database's "rumble" ("off" 0, "low"
 * 35, "medium" 65), and 100 for "full", for none (a card that never chose)
 * and for a word this release cannot read */
int pk_rumble_pct(sqlite3 *db);

/* What a step or the clock did (`did`, src/boost.h's BOOST_ flags, from
 * pk_press or pk_boost_tick) asks of the motor: the share to buzz at when it
 * says the boost opened, 0 for anything else or with Rumble Off */
int pk_buzz_pct(sqlite3 *db, int did);

/* The magnitude for the share, 0 to 0xFFFF, as the launcher scales it
 * (plat_rumble_buzz, src/device/pixel2.c): full at 100, rounded */
uint16_t pk_buzz_strength(int pct);

/* ---- whose display may be borrowed -------------------------------------------
 *
 * portkeys borrows the port's display for the bar, the disc, On-Screen Stats,
 * the in-game menu and a screenshot by copying the port's own open
 * /dev/dri/card0 (portkeys.c's master_copy): a copy of a master's file is
 * master. A copy also keeps the file open. SDL's KMSDRM, as a program starts
 * (SDL 2.32's get_driindex, twice, then its own open), opens the display,
 * takes the master, checks it, and closes it again within milliseconds: a
 * copy taken in those milliseconds keeps that file, and the master with it,
 * after SDL has closed it, and SDL's next open cannot become master, so the
 * program fails with "kmsdrm not available". On 1.1.0-rc.9 your PICO-8 failed
 * to start one time in two that way: portkeys looked for a master once a
 * second for On-Screen Stats, and a look in SDL's milliseconds copied PICO-8's
 * (the log's "showing the bar over pid 935" right after its FATAL ERROR).
 *
 * So a display file is copied only once it is settled: the same process had
 * it open under the same number at an earlier look at least PK_SETTLE_MS
 * before, with no gap of more than PK_LOOK_GAP_MS between the looks - far
 * longer than SDL's look at the display lasts, so what is open then is the
 * program's own display - or the process is one whose master has been held
 * before (pk_looks_trust): a program that has its display. A look reads only
 * /proc's links and copies nothing, so it cannot be in the way. */
#define PK_SETTLE_MS    300
#define PK_LOOK_GAP_MS  1500
#define PK_LOOKS_MAX    16

typedef struct {
	int  pid, fd;            /* the process and its number for the display */
	long first, last;        /* seen first in this run of looks, and last */
	bool seen;               /* in the look under way */
} pk_look;

typedef struct {
	pk_look e[PK_LOOKS_MAX];
	int     n;
	int     trusted;         /* the pid whose master was held, 0 none */
	int     waiting;         /* display files seen in the last look and not settled */
	long    settles_at;      /* when the first of those settles, -1 none */
} pk_looks;

void pk_looks_init(pk_looks *l);
/* A look starting at `now` (ms) */
void pk_looks_begin(pk_looks *l, long now);
/* `pid` has the display open as `fd` in this look: whether that file may be
 * copied (settled, or the pid trusted) */
bool pk_looks_seen(pk_looks *l, int pid, int fd, long now);
/* The look ended, `whole` when every process was looked at (a look stopped
 * at the master it found keeps what it did not reach) */
void pk_looks_end(pk_looks *l, bool whole);
/* `pid`'s master held: its display files may be copied at once from now on */
void pk_looks_trust(pk_looks *l, int pid);
/* ms from `now` until a display file seen and not settled settles, -1 for
 * none seen */
long pk_looks_wait(const pk_looks *l, long now);

#endif
