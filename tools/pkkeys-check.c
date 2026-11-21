/* What portkeys makes of the volume keys over a port: tools/pkkeys.c, fed the
 * events the Pixel 2's two devices send, without a device.
 *
 * Over a port the launcher has exited and portkeys steps ZyperOS's levels and
 * saves them in its database, so a key misread is a level the shelf comes back
 * at. Held here: MENU+VOL+ is the backlight whichever device is read first, the
 * joypad (MENU) or gpio-keys (VOL+), and when the joypad was read before MENU
 * was there; the kernel's autorepeat (value 2) is no step; a held key repeats
 * on the launcher's clock, src/keyrepeat.h's, as portkeys' poll timeout drives
 * it, and keeps the kind it went down as; a device gone or resynced lets go of
 * what it held; every step is a line for the log; and a round trip through the
 * launcher's own src/db.c, 4 and 7 loaded and the presses of the report that
 * saved 6 and 6 fed, saves 5 and 7.
 *
 * And the boost past the top (pk_boost, on src/boost.h), on the same loop:
 * three presses on the top open it and write the gain to the file the port's
 * plugin reads, a hold opens it after its time and a let-go before does not,
 * the held key stops at the first cell and a let-go frees it, going down
 * empties the cells and then folds them, the bar going away closes an empty
 * one, a plug in or out closes the volume's, MENU+VOL+ opens the backlight's,
 * and the cells are kept in the launcher's database and come back with the
 * gain. A step that is no boost's is pk_apply's, line for line.
 *
 * Links tools/pkkeys.c, src/db.c and SQLite, and NOT SDL. */
#include "../src/db.h"
#include "../src/device/pixel2_levels.h"
#include "../src/eqdsp.h"
#include "../src/keyrepeat.h"
#include "pkhot.h"
#include "pkkbd.h"
#include "pkkeys.h"
#include "pkshot.h"
#include "pkvpad.h"
#include "../src/p8draw.h"
#include "../src/rumble.h"

#include <zlib.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails, checks;

static void ck(int cond, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (cond) return;
	fails++;
	printf("  FAIL ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/* ---- a rig: the keys, the levels and a database, on portkeys' loop -------- */

typedef struct {
	pkkeys    k;
	pk_levels l;
	sqlite3  *db;
	int64_t   now;              /* ms, CLOCK_MONOTONIC's stand-in */
	int       n;                /* steps given */
	pk_step   step[64];
	int64_t   at[64];           /* when each was given */
	char      log[4096];        /* the lines pk_apply wrote */
	pk_boost *bo;               /* when set, the steps go through the boost (pk_press) */
	int       did;              /* what the boost did, every step and tick ORed */
	int       opened;           /* how often it opened */
	int       buzzes;           /* how often portkeys would buzz for it (pk_buzz_pct) */
	int       buzz_pct;         /* and at what share, the last time */
} rig;

/* What portkeys's press() and boost_clock() hand boost_buzz: each step's and
 * each tick's `did`, the share pk_buzz_pct asks of the motor for it */
static void buzz_of(rig *r, int did)
{
	const int pct = pk_buzz_pct(r->db, did);

	if (pct > 0) {
		r->buzzes++;
		r->buzz_pct = pct;
	}
}

static void rig_init(rig *r, int vol, int bright)
{
	memset(r, 0, sizeof *r);
	pkkeys_init(&r->k);
	r->l.vol = vol;
	r->l.bright = bright;
}

static void take(rig *r, const pk_step *s, int m)
{
	char line[128];
	int i;

	for (i = 0; i < m; i++) {
		if (r->bo) {
			int did = pk_press(r->db, &r->l, r->bo, &s[i], (uint32_t)r->now, line, sizeof line);

			r->did |= did;
			r->opened += !!(did & BOOST_OPENED);
			buzz_of(r, did);
		} else {
			pk_apply(r->db, &r->l, &s[i], line, sizeof line);
		}
		strncat(r->log, line, sizeof r->log - strlen(r->log) - 1);
		if (r->n < 64) {
			r->step[r->n] = s[i];
			r->at[r->n] = r->now;
		}
		r->n++;
	}
}

/* One pass of portkeys' loop at r->now: the events read, in the order read,
 * the pass begun at `cutoff` (µs on the events' clock); then the repeats due;
 * then, through the boost, its clock for both levels (boost_clock). */
static void pass(rig *r, const pk_key *ev, int n, int64_t cutoff)
{
	pk_step s[32];

	take(r, s, pkkeys_pass(&r->k, ev, n, cutoff, r->now, s, 32));
	take(r, s, pkkeys_tick(&r->k, r->now, s, 32));
	if (r->bo) {
		int kind;

		for (kind = PK_VOLUME; kind <= PK_BRIGHT; kind++) {
			int did = pk_boost_tick(r->db, r->bo, &r->k, (pk_kind)kind, (uint32_t)r->now);

			r->did |= did;
			r->opened += !!(did & BOOST_OPENED);
			buzz_of(r, did);
		}
	}
}

/* Time passes to `t` as it does for portkeys: poll sleeps for what
 * pkkeys_wait says, and each wakeup is a pass with nothing read. */
static void run_to(rig *r, int64_t t)
{
	int guard = 0;

	while (guard++ < 10000) {
		int w = pkkeys_wait(&r->k, r->now);

		if (w < 0 || r->now + w > t) break;
		r->now += w;
		pass(r, NULL, 0, INT64_MAX);
	}
	r->now = t;
}

/* The same, but woken every millisecond, for whatever else the loop waits on */
static void tick_to(rig *r, int64_t t)
{
	while (r->now < t) {
		r->now++;
		pass(r, NULL, 0, INT64_MAX);
	}
}

static pk_key ev(int64_t t_us, int code, int value) { return (pk_key){ t_us, (uint16_t)code, value }; }

/* A key pressed and let go `ms` later, in two passes, at r->now */
static void tap(rig *r, int code, int ms)
{
	pk_key e = ev(r->now * 1000, code, 1);

	pass(r, &e, 1, r->now * 1000 + 100);
	run_to(r, r->now + ms);
	e = ev(r->now * 1000, code, 0);
	pass(r, &e, 1, r->now * 1000 + 100);
}

/* px2-input's "menu+volup": MENU down on the joypad, VOL+ down on gpio-keys
 * 5 µs later; 80 ms, then both up. How portkeys came to read them:
 *   BOTH       one pass, both queued when it read, the device read first first
 *   BETWEEN    woken between the two writes: MENU alone, then VOL+
 *   PAD_EARLY  the pass began before either came: read joypad first, the
 *              joypad was still empty and gpio-keys had VOL+ (MENU comes in
 *              the next pass); read gpio-keys first, both were there by then */
enum { BOTH, BETWEEN, PAD_EARLY };
static void menu_chord(rig *r, int vol_code, bool keys_first, int how)
{
	int64_t t = r->now * 1000;
	pk_key menu = ev(t, PK_BTN_MENU, 1), vol = ev(t + 5, vol_code, 1), two[2];

	if (how == BOTH) {
		two[0] = keys_first ? vol : menu;
		two[1] = keys_first ? menu : vol;
		pass(r, two, 2, t + 100);
	} else if (how == BETWEEN) {
		pass(r, &menu, 1, t + 2);
		pass(r, &vol, 1, t + 100);
	} else if (keys_first) {
		two[0] = vol;
		two[1] = menu;
		pass(r, two, 2, t - 1);
		pass(r, NULL, 0, t + 100);
	} else {
		pass(r, &vol, 1, t - 1);
		pass(r, &menu, 1, t + 100);
	}
	run_to(r, r->now + 80);
	t = r->now * 1000;
	menu = ev(t, PK_BTN_MENU, 0);
	vol = ev(t + 5, vol_code, 0);
	two[0] = keys_first ? vol : menu;
	two[1] = keys_first ? menu : vol;
	pass(r, two, 2, t + 100);
}

/* ---- the chord ------------------------------------------------------------- */

static void the_chord(void)
{
	static const char *how_name[] = { "in one pass", "woken between the writes", "the pass begun before either came" };
	rig r;
	int how, kf, code;

	for (code = 0; code < 2; code++)
		for (how = BOTH; how <= PAD_EARLY; how++)
			for (kf = 0; kf < 2; kf++) {
				int vc = code ? PK_KEY_VOLUMEDOWN : PK_KEY_VOLUMEUP, want = code ? 6 : 8;

				rig_init(&r, 4, 7);
				r.now = 1000;
				menu_chord(&r, vc, kf, how);
				ck(r.l.bright == want && r.l.vol == 4 && r.n == 1,
				   "chord: menu+%s, %s, %s read first: brightness 7 -> %d and volume 4; got brightness %d, volume %d, %d step(s)",
				   code ? "voldown" : "volup", how_name[how], kf ? "gpio-keys" : "the joypad",
				   want, r.l.bright, r.l.vol, r.n);
				ck(!r.k.menu_held, "chord: menu+%s, %s: MENU let go after", code ? "voldown" : "volup", how_name[how]);
			}

	/* VOL+ alone after a chord is the volume again */
	rig_init(&r, 4, 7);
	r.now = 1000;
	menu_chord(&r, PK_KEY_VOLUMEUP, true, BOTH);
	run_to(&r, r.now + 150);
	tap(&r, PK_KEY_VOLUMEUP, 80);
	ck(r.l.vol == 5 && r.l.bright == 8, "chord then volup: volume 4 -> 5, brightness 8 kept; got %d, %d", r.l.vol, r.l.bright);

	/* An event stamped after the pass began waits a pass, and only one: the
	 * next pass handles it whatever its time, a clock that went back included */
	{
		pk_key vol = ev(5000, PK_KEY_VOLUMEUP, 1);
		pk_step s[4];
		int m;

		rig_init(&r, 4, 7);
		m = pkkeys_pass(&r.k, &vol, 1, 4999, 5, s, 4);
		ck(m == 0, "late: VOL+ stamped after the pass began is not a step in it; got %d", m);
		ck(pkkeys_wait(&r.k, 5) == 0, "late: and the loop is asked to come back at once; wait %d", pkkeys_wait(&r.k, 5));
		m = pkkeys_pass(&r.k, NULL, 0, 1000, 5, s, 4);
		ck(m == 1 && s[0].kind == PK_VOLUME && s[0].dir == +1,
		   "late: the next pass takes it, even with its cutoff before it; got %d step(s)", m);
		ck(pkkeys_wait(&r.k, 5) == REPEAT_DELAY_MS, "late: then the hold's own wait; wait %d", pkkeys_wait(&r.k, 5));
	}

	/* Equal times keep the order they were read in, and the other keys are nothing */
	{
		pk_key e[4] = { ev(7, 304 /* BTN_SOUTH */, 1), ev(7, PK_KEY_VOLUMEUP, 1),
		                ev(7, PK_KEY_VOLUMEUP, 0), ev(7, 304, 0) };
		pk_step s[4];
		int m;

		rig_init(&r, 4, 7);
		m = pkkeys_pass(&r.k, e, 4, 100, 0, s, 4);
		ck(m == 1 && !r.k.key[0].down, "same time: VOL+ down then up is one step and let go; got %d", m);
	}
}

/* ---- the kernel's autorepeat ---------------------------------------------------- */

static void the_kernel_repeat(void)
{
	static const int value[5] = { 1, 2, 2, 2, 0 };
	static const int at[5] = { 0, 250, 283, 316, 330 };   /* gpio-keys: 250 ms, then 33 */
	rig r;
	int i;

	/* All five in one pass */
	{
		pk_key e[5];

		rig_init(&r, 4, 7);
		for (i = 0; i < 5; i++) e[i] = ev(at[i] * 1000, PK_KEY_VOLUMEUP, value[i]);
		pass(&r, e, 5, 1000000);
		ck(r.n == 1 && r.l.vol == 5, "values 1 2 2 2 0, one pass: one step, volume 4 -> 5; got %d step(s), volume %d", r.n, r.l.vol);
	}

	/* One a pass, all inside the launcher's first REPEAT_DELAY_MS */
	rig_init(&r, 4, 7);
	for (i = 0; i < 5; i++) {
		pk_key e = ev(r.now * 1000, PK_KEY_VOLUMEUP, value[i]);

		pass(&r, &e, 1, r.now * 1000 + 100);
		run_to(&r, r.now + (REPEAT_DELAY_MS - 50) / 4);
	}
	ck(r.n == 1 && r.l.vol == 5, "values 1 2 2 2 0, a pass each within %d ms: one step; got %d step(s), volume %d",
	   REPEAT_DELAY_MS, r.n, r.l.vol);

	/* At the kernel's own times: 330 ms held is the press and the launcher's
	 * repeat at REPEAT_DELAY_MS, not four steps */
	{
		rig_init(&r, 4, 7);
		for (i = 0; i < 5; i++) {
			pk_key e;

			run_to(&r, at[i]);
			e = ev(at[i] * 1000, PK_KEY_VOLUMEUP, value[i]);
			pass(&r, &e, 1, at[i] * 1000 + 100);
		}
		ck(r.n == 2 && r.at[0] == 0 && r.at[1] == REPEAT_DELAY_MS,
		   "values 1 2 2 2 0 at 0 250 283 316 330 ms: steps at 0 and %d ms only; got %d step(s)%s",
		   REPEAT_DELAY_MS, r.n, r.n > 1 && r.at[1] != REPEAT_DELAY_MS ? ", the second not at the delay" : "");
	}
}

/* ---- the launcher's clock ---------------------------------------------------- */

static void want_schedule(const rig *r, int hold_ms, const char *what)
{
	int64_t t[16];
	int n = 0, i, ok;

	t[n++] = 0;
	for (int64_t x = REPEAT_DELAY_MS; x < hold_ms && n < 16; x += REPEAT_RATE_MS) t[n++] = x;
	ok = r->n == n;
	for (i = 0; ok && i < n; i++) ok = r->at[i] == t[i];
	{
		char got[256] = "", one[16];

		for (i = 0; i < r->n && i < 16; i++) {
			snprintf(one, sizeof one, "%s%lld", i ? " " : "", (long long)r->at[i]);
			strncat(got, one, sizeof got - strlen(got) - 1);
		}
		ck(ok, "%s: %d steps at 0, %d, then every %d ms; got %d at [%s]", what, n, REPEAT_DELAY_MS,
		   REPEAT_RATE_MS, r->n, got);
	}
}

static void the_clock(void)
{
	rig r;
	pk_key e;
	int w, i, bright;

	/* REPEAT_DELAY_MS and REPEAT_RATE_MS are src/keyrepeat.h's, which
	 * src/platform.c's in_repeat reads: what is held is the launcher's own */
	ck(REPEAT_DELAY_MS == 300 && REPEAT_RATE_MS == 90,
	   "the launcher's clock is 300 ms and then 90 ms, as Diatom's copy (diatom/src/levels.c) has it; is %d and %d",
	   REPEAT_DELAY_MS, REPEAT_RATE_MS);

	/* 600 ms held, driven by the wait portkeys' poll is given */
	rig_init(&r, 4, 7);
	e = ev(0, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	w = pkkeys_wait(&r.k, r.now);
	ck(w == REPEAT_DELAY_MS, "hold: poll is told %d ms after the press; got %d", REPEAT_DELAY_MS, w);
	run_to(&r, REPEAT_DELAY_MS);
	w = pkkeys_wait(&r.k, r.now);
	ck(w == REPEAT_RATE_MS, "hold: and %d ms after the first repeat; got %d", REPEAT_RATE_MS, w);
	run_to(&r, 600);
	e = ev(600000, PK_KEY_VOLUMEUP, 0);
	pass(&r, &e, 1, 600100);
	want_schedule(&r, 600, "volup held 600 ms, on poll's timeout");
	ck(r.l.vol == 4 + r.n, "hold: the volume moved a position a step: %d", r.l.vol);
	ck(pkkeys_wait(&r.k, r.now) == -1, "hold: let go, poll is told nothing; got %d", pkkeys_wait(&r.k, r.now));
	i = r.n;
	run_to(&r, 5000);
	ck(r.n == i, "hold: and no step after; got %d more", r.n - i);

	/* The same hold, woken every millisecond by something else */
	rig_init(&r, 4, 7);
	e = ev(0, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	tick_to(&r, 600);
	e = ev(600000, PK_KEY_VOLUMEUP, 0);
	pass(&r, &e, 1, 600100);
	want_schedule(&r, 600, "volup held 600 ms, woken every ms");

	/* Woken late, every 70 ms by something else: each repeat counted from
	 * when the one before was given, as in_repeat counts (last_repeat = now),
	 * so 0, 350, 490 and 630 for a hold to 650 */
	rig_init(&r, 4, 7);
	e = ev(0, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	while (r.now + 70 <= 650) {
		r.now += 70;
		pass(&r, NULL, 0, INT64_MAX);
	}
	ck(r.n == 4 && r.at[1] == 350 && r.at[2] == 490 && r.at[3] == 630,
	   "volup held 650 ms, woken every 70 ms: steps at 0 350 490 630, as in_repeat would; got %d", r.n);

	/* Short of the delay, one step */
	rig_init(&r, 4, 7);
	tap(&r, PK_KEY_VOLUMEUP, REPEAT_DELAY_MS - 1);
	ck(r.n == 1, "a hold of %d ms is one step; got %d", REPEAT_DELAY_MS - 1, r.n);

	/* MENU+VOL+ held, MENU let go first at 350 ms: brightness throughout,
	 * as the launcher keeps what a key went down as (vol_as) */
	rig_init(&r, 4, 3);
	e = ev(0, PK_BTN_MENU, 1);
	pass(&r, &e, 1, 1);
	e = ev(5, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	run_to(&r, 350);
	e = ev(350000, PK_BTN_MENU, 0);
	pass(&r, &e, 1, 350100);
	run_to(&r, 600);
	e = ev(600000, PK_KEY_VOLUMEUP, 0);
	pass(&r, &e, 1, 600100);
	want_schedule(&r, 600, "menu+volup held 600 ms, MENU let go at 350");
	for (bright = 1, i = 0; i < r.n && i < 64; i++) bright &= r.step[i].kind == PK_BRIGHT;
	ck(bright && r.l.vol == 4 && r.l.bright == 3 + r.n,
	   "menu+volup held: every step the backlight, MENU let go or not; volume %d, brightness %d", r.l.vol, r.l.bright);

	/* VOL+ held, MENU pressed at 100 ms: the volume throughout */
	rig_init(&r, 4, 7);
	e = ev(0, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	run_to(&r, 100);
	e = ev(100000, PK_BTN_MENU, 1);
	pass(&r, &e, 1, 100100);
	run_to(&r, 600);
	e = ev(600000, PK_KEY_VOLUMEUP, 0);
	pass(&r, &e, 1, 600100);
	ck(r.l.bright == 7 && r.l.vol == 4 + r.n && r.n == 5,
	   "volup held, MENU pressed under it: the volume throughout; volume %d, brightness %d, %d steps",
	   r.l.vol, r.l.bright, r.n);

	/* Both held: each on its own clock */
	rig_init(&r, 10, 7);
	{
		pk_key two[2] = { ev(0, PK_KEY_VOLUMEUP, 1), ev(200000, PK_KEY_VOLUMEDOWN, 1) };
		int up = 0, down = 0;

		pass(&r, &two[0], 1, 100);
		run_to(&r, 200);
		pass(&r, &two[1], 1, 200100);
		run_to(&r, 650);
		for (i = 0; i < r.n && i < 64; i++) {
			if (r.step[i].dir > 0) up++;
			else down++;
		}
		/* up at 0 300 390 480 570, down at 200 500 590 */
		ck(up == 5 && down == 3, "volup at 0 and voldown at 200 held to 650: 5 up and 3 down; got %d and %d", up, down);
	}
}

/* ---- devices gone and resynced ----------------------------------------------- */

static void the_resync(void)
{
	rig r;
	pk_key e;
	int n;

	/* MENU held and the joypad goes: its release will never come */
	rig_init(&r, 4, 7);
	e = ev(0, PK_BTN_MENU, 1);
	pass(&r, &e, 1, 100);
	pkkeys_menu_state(&r.k, false);
	tap(&r, PK_KEY_VOLUMEUP, 80);
	ck(r.l.vol == 5 && r.l.bright == 7, "joypad gone with MENU down: VOL+ is the volume; volume %d, brightness %d",
	   r.l.vol, r.l.bright);

	/* VOL+ held and its release lost in a SYN_DROPPED: gpio-keys says it is
	 * up, and the hold stops */
	rig_init(&r, 4, 7);
	e = ev(0, PK_KEY_VOLUMEUP, 1);
	pass(&r, &e, 1, 100);
	run_to(&r, 100);
	pkkeys_vol_state(&r.k, false, false);
	run_to(&r, 2000);
	ck(r.n == 1 && pkkeys_wait(&r.k, r.now) == -1, "VOL+ found up at a resync: no repeat; got %d step(s)", r.n);

	/* And one found down that was never seen going down is no step, nor
	 * are the kernel's repeats of it after */
	rig_init(&r, 4, 7);
	pkkeys_vol_state(&r.k, true, false);
	run_to(&r, 250);
	for (n = 0; n < 4; n++) {
		e = ev(r.now * 1000, PK_KEY_VOLUMEUP, 2);
		pass(&r, &e, 1, r.now * 1000 + 100);
		run_to(&r, r.now + 33);
	}
	run_to(&r, 2000);
	n = r.n;
	ck(n == 0, "VOL+ found down at a resync, never seen pressed, then repeated by the kernel: no step; got %d", n);

	/* MENU found down at a resync: the next VOL+ is the backlight */
	rig_init(&r, 4, 7);
	pkkeys_menu_state(&r.k, true);
	tap(&r, PK_KEY_VOLUMEDOWN, 80);
	ck(r.l.bright == 6 && r.l.vol == 4, "MENU found down at a resync: VOL- is the backlight; brightness %d", r.l.bright);
}

/* ---- MENU on its own: the in-game menu over a port -------------------------- */

static void the_menu_tap(void)
{
	rig r;
	pk_key e[2];
	bool t;

	/* Pressed and let go, nothing under it: one tap, taken once */
	rig_init(&r, 4, 7);
	tap(&r, PK_BTN_MENU, 120);
	t = pkkeys_menu_tapped(&r.k);
	ck(t && !pkkeys_menu_tapped(&r.k) && r.n == 0, "MENU alone: one tap, taken once, no step");

	/* MENU+VOL- is the backlight, and MENU let go after it opens nothing */
	rig_init(&r, 4, 7);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 1);
	pass(&r, e, 1, r.now * 1000 + 100);
	tap(&r, PK_KEY_VOLUMEDOWN, 80);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 0);
	pass(&r, e, 1, r.now * 1000 + 100);
	ck(r.l.bright == 6 && !pkkeys_menu_tapped(&r.k), "MENU+VOL-: the backlight, and no tap; brightness %d",
	   r.l.bright);

	/* The chord from px2-input, both down in one pass, both up in the next */
	rig_init(&r, 4, 7);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 1);
	e[1] = ev(r.now * 1000 + 5, PK_KEY_VOLUMEUP, 1);
	pass(&r, e, 2, r.now * 1000 + 100);
	run_to(&r, r.now + 80);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 0);
	e[1] = ev(r.now * 1000 + 5, PK_KEY_VOLUMEUP, 0);
	pass(&r, e, 2, r.now * 1000 + 100);
	ck(r.l.bright == 8 && !pkkeys_menu_tapped(&r.k), "menu+volup in one pass: no tap; brightness %d", r.l.bright);

	/* Let go of MENU first, then VOL+: the volume, and a tap all the same,
	 * as a VOL+ after MENU's release is no chord */
	rig_init(&r, 4, 7);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 1);
	e[1] = ev(r.now * 1000 + 40000, PK_BTN_MENU, 0);
	pass(&r, e, 2, r.now * 1000 + 50000);
	r.now += 60;
	tap(&r, PK_KEY_VOLUMEUP, 80);
	ck(r.l.vol == 5 && pkkeys_menu_tapped(&r.k), "MENU tapped, then VOL+: a tap and the volume; volume %d", r.l.vol);

	/* The kernel's autorepeat of MENU is no second press */
	rig_init(&r, 4, 7);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 1);
	e[1] = ev(r.now * 1000 + 300000, PK_BTN_MENU, 2);
	pass(&r, e, 2, r.now * 1000 + 400000);
	ck(!pkkeys_menu_tapped(&r.k) && r.k.menu_armed, "MENU held, the kernel repeating it: still one press, no tap yet");

	/* MENU found down at a resync (the menu closed with MENU still held, or
	 * events lost): its release was never pressed here, and opens nothing */
	rig_init(&r, 4, 7);
	pkkeys_menu_state(&r.k, true);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 0);
	pass(&r, e, 1, r.now * 1000 + 100);
	ck(!pkkeys_menu_tapped(&r.k) && !r.k.menu_held, "MENU found down at a resync, then let go: no tap");

	/* Pressed before a resync and let go after it: no tap either */
	rig_init(&r, 4, 7);
	e[0] = ev(r.now * 1000, PK_BTN_MENU, 1);
	pass(&r, e, 1, r.now * 1000 + 100);
	pkkeys_init(&r.k);
	pkkeys_menu_state(&r.k, true);
	e[0] = ev(r.now * 1000 + 1000, PK_BTN_MENU, 0);
	pass(&r, e, 1, r.now * 1000 + 2000);
	ck(!pkkeys_menu_tapped(&r.k), "MENU pressed, a resync, MENU let go: no tap");
}

/* ---- the steps and the log ------------------------------------------------------ */

static void the_log(void)
{
	pk_levels l;
	char line[128];
	pk_step s;
	bool moved;

	l = (pk_levels){ 5, 7 };
	s = (pk_step){ PK_VOLUME, +1, false };
	pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!strcmp(line, "portkeys: volume 5 -> 6 (VOL+)\n"), "log: \"portkeys: volume 5 -> 6 (VOL+)\"; got \"%s\"", line);
	s = (pk_step){ PK_BRIGHT, -1, false };
	pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!strcmp(line, "portkeys: brightness 7 -> 6 (MENU+VOL-)\n"),
	   "log: \"portkeys: brightness 7 -> 6 (MENU+VOL-)\"; got \"%s\"", line);
	s = (pk_step){ PK_VOLUME, -1, true };
	pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!strcmp(line, "portkeys: volume 6 -> 5 (VOL- held)\n"), "log: a repeat says held; got \"%s\"", line);

	/* At the top: a press still says so, a repeat moves nothing and says nothing */
	l = (pk_levels){ VOL_MAX, BRIGHT_MAX };
	s = (pk_step){ PK_VOLUME, +1, false };
	moved = pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!moved && l.vol == VOL_MAX && !strcmp(line, "portkeys: volume 20 -> 20 (VOL+)\n"),
	   "log: a press at the top is a line and no move; got \"%s\"", line);
	s.held = true;
	moved = pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!moved && !line[0], "log: a repeat at the top is no line; got \"%s\"", line);
	s = (pk_step){ PK_BRIGHT, +1, true };
	moved = pk_apply(NULL, &l, &s, line, sizeof line);
	ck(!moved && l.bright == BRIGHT_MAX && !line[0], "log: nor at the backlight's; got \"%s\"", line);
	l = (pk_levels){ 0, 0 };
	s = (pk_step){ PK_BRIGHT, -1, false };
	pk_apply(NULL, &l, &s, line, sizeof line);
	ck(l.bright == 0 && !strcmp(line, "portkeys: brightness 0 -> 0 (MENU+VOL-)\n"), "log: the bottom; got \"%s\"", line);

	/* A volume never saved starts from the middle */
	l = (pk_levels){ -1, 7 };
	s = (pk_step){ PK_VOLUME, +1, false };
	moved = pk_apply(NULL, &l, &s, line, sizeof line);
	ck(moved && l.vol == VOL_MAX / 2 + 1, "never saved: the first press starts from %d; got %d", VOL_MAX / 2, l.vol);
}

/* ---- through the launcher's database ------------------------------------------------ */

/* `boost`: the same presses through the boost (pk_press), as portkeys makes
 * them now, which saves and says the same, the top never reached */
static void the_database(bool boost)
{
	char dir[] = "/tmp/zyperos-pkkeys-XXXXXX", path[128], bl[128], share[160];
	pk_boost bo;
	static const char *want =
		"portkeys: volume 4 -> 5 (VOL+)\n"
		"portkeys: volume 5 -> 4 (VOL-)\n"
		"portkeys: volume 4 -> 5 (VOL+)\n"
		"portkeys: brightness 7 -> 8 (MENU+VOL+)\n"
		"portkeys: brightness 8 -> 7 (MENU+VOL-)\n";
	db *d;
	rig r;
	FILE *f;

	if (!mkdtemp(dir)) {
		ck(0, "db: a scratch directory");
		return;
	}
	snprintf(path, sizeof path, "%s/zyperos.db", dir);
	snprintf(bl, sizeof bl, "%s/brightness", dir);
	snprintf(share, sizeof share, "%s/eq", dir);

	/* The launcher saved 4 and 7, and exited for the port */
	d = db_open(path, DB_DEVICE);
	ck(d != NULL, "db: the launcher opens a scratch device database");
	if (!d) return;
	db_set_int(d, "volume", 4);
	db_set_int(d, "brightness", 7);
	db_close(d);

	/* portkeys opens it as main() does and loads the levels */
	rig_init(&r, -1, -1);
	ck(sqlite3_open_v2(path, &r.db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK, "db: portkeys opens it");
	pk_levels_load(r.db, bl, &r.l);
	ck(r.l.vol == 4 && r.l.bright == 7, "db: portkeys loads 4 and 7; got %d and %d", r.l.vol, r.l.bright);
	if (boost) {
		pk_boost_init(&bo, share);
		ck(pk_boost_load(r.db, &r.l, &bo), "db: the boost's file made beside it");
		r.bo = &bo;
	}

	/* The presses of the report that saved 6 and 6: volup, voldown, volup,
	 * menu+volup with gpio-keys read first, menu+voldown woken between */
	r.now = 1000;
	tap(&r, PK_KEY_VOLUMEUP, 80);    run_to(&r, r.now + 150);
	tap(&r, PK_KEY_VOLUMEDOWN, 80);  run_to(&r, r.now + 150);
	tap(&r, PK_KEY_VOLUMEUP, 80);    run_to(&r, r.now + 150);
	menu_chord(&r, PK_KEY_VOLUMEUP, true, BOTH);     run_to(&r, r.now + 150);
	menu_chord(&r, PK_KEY_VOLUMEDOWN, true, BETWEEN);
	if (boost) {
		ck(!r.did || r.did == BOOST_LEVEL, "db: through the boost, steps and nothing else; got %d", r.did);
		ck(pk_db_get(r.db, PK_VOL_BOOST, -1) == -1 && pk_db_get(r.db, PK_BRIGHT_BOOST, -1) == -1,
		   "db: no boost kept, none having opened");
		pk_boost_free(&bo);
		r.bo = NULL;
		unlink(share);
	}
	sqlite3_close(r.db);

	d = db_open(path, DB_DEVICE);
	ck(d != NULL, "db: the launcher opens it again");
	if (d) {
		int v = db_get_int(d, "volume", -1), b = db_get_int(d, "brightness", -1);

		ck(v == 5 && b == 7, "db: the launcher reads back the 5 and 7 the presses meant; got volume %d, brightness %d", v, b);
		db_close(d);
	}
	ck(!strcmp(r.log, want), "db: and the log says each step:\n--- want\n%s--- got\n%s---", want, r.log);

	/* No brightness saved: the rung nearest the panel's, and with no panel
	 * file and no database the middle, the volume left alone */
	d = db_open(path, DB_DEVICE);
	if (d) {
		db_del(d, "brightness");
		db_close(d);
	}
	f = fopen(bl, "w");
	if (f) {
		fprintf(f, "%d\n", bright_ladder[9] + 3);
		fclose(f);
	}
	sqlite3_open_v2(path, &r.db, SQLITE_OPEN_READWRITE, NULL);
	pk_levels_load(r.db, bl, &r.l);
	ck(r.l.bright == 9, "db: no brightness saved, the panel at %d: rung 9; got %d", bright_ladder[9] + 3, r.l.bright);
	sqlite3_close(r.db);
	unlink(bl);
	pk_levels_load(NULL, bl, &r.l);
	ck(r.l.vol == -1 && r.l.bright == BRIGHT_MAX / 2, "db: none at all: volume untouched, brightness the middle; got %d, %d",
	   r.l.vol, r.l.bright);

	{
		const char *suffix[] = { "", "-wal", "-shm", "-journal" };
		char p[160];

		for (unsigned i = 0; i < sizeof suffix / sizeof suffix[0]; i++) {
			snprintf(p, sizeof p, "%s%s", path, suffix[i]);
			unlink(p);
		}
		rmdir(dir);
	}
}

/* A pending suffix cannot be overtaken, including after a wall-clock jump.
 * Capacity checks must happen before even an eligible prefix emits a step. */
static void the_pending_order(void)
{
	pkkeys k;
	pk_step out[PK_WAITING + PK_PASS_MAX];
	pk_key batch[PK_PASS_MAX + 1];
	int m;

	pkkeys_init(&k);
	batch[0] = ev(200, PK_KEY_VOLUMEUP, 0);
	ck(pkkeys_pass(&k, batch, 1, 100, 0, out, 320) == 0, "pending release waits");
	batch[0] = ev(150, PK_KEY_VOLUMEUP, 1);
	ck(pkkeys_pass(&k, batch, 1, 100, 0, out, 320) == 0 && k.nwaiting == 2,
	   "an earlier new press holds back the pending release");
	m = pkkeys_pass(&k, NULL, 0, 100, 0, out, 320);
	ck(m == 1 && !k.key[0].down && !k.nwaiting && pkkeys_wait(&k, 0) == -1,
	   "future timestamps settle in order without a stuck hold or zero-wait loop");

	pkkeys_init(&k);
	batch[0] = ev(0, PK_KEY_VOLUMEUP, 1);
	for (int i = 1; i <= 65; i++) batch[i] = ev(1000 + i, PK_BTN_MENU, i % 2);
	m = pkkeys_pass(&k, batch, 66, 100, 0, out, 320);
	ck(m == -1 && !k.key[0].down && !k.nwaiting && !k.menu_held,
	   "pending overflow discards the eligible prefix too");

	pkkeys_init(&k);
	for (int i = 0; i < 64; i++) batch[i] = ev(1000 + i, PK_KEY_VOLUMEUP, !(i % 2));
	ck(pkkeys_pass(&k, batch, 64, 100, 0, out, 320) == 0 && k.nwaiting == 64,
	   "exactly 64 pending transitions fit");
	m = pkkeys_pass(&k, NULL, 0, 100, 0, out, 320);
	ck(m == 32 && !k.key[0].down && !k.nwaiting, "64 pending transitions preserve press/release order");

	pkkeys_init(&k);
	for (int i = 0; i < 257; i++) batch[i] = ev(i, PK_KEY_VOLUMEUP, !(i % 2));
	m = pkkeys_pass(&k, batch, 257, INT64_MAX, 0, out, 320);
	ck(m == -1 && !k.key[0].down && !k.nwaiting, "oversized pass resets before any step");
	m = pkkeys_pass(&k, batch, 256, INT64_MAX, 0, out, 320);
	ck(m == 128 && !k.key[0].down, "exactly 256 transitions fit and stay in order");
}

/* The volume ladder's ends (src/device/pixel2_levels.h). Position 1 on the
 * speaker was raw 172 until 1.0.6, called right by ear in 2026-10 and too
 * loud at night by players (Discord, 1.0.5): the floor went down two of its
 * old steps, so positions 1 and 2 are quieter than it was and position 3 is
 * about it, with the top, the twenty positions and their bar as they were.
 * The headphones' window is as it was. And Diatom's Pixel port holds the
 * same numbers (diatom/port/pixel2.c): a level read back across a handover
 * must mean the same position on both sides. */
static long define_in(const char *path, const char *name)
{
	FILE *f = fopen(path, "r");
	char line[256], key[64];
	long v;

	if (!f) return -1;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "#define %63s %ld", key, &v) == 2 && !strcmp(key, name)) {
			fclose(f);
			return v;
		}
	fclose(f);
	return -1;
}

static void the_ladder(void)
{
	static const char *const names[] = { "SPK_ATT_TOP", "SPK_ATT_BOTTOM",
	                                     "HP_ATT_TOP", "HP_ATT_BOTTOM" };
	static const long mine[] = { SPK_ATT_TOP, SPK_ATT_BOTTOM, HP_ATT_TOP, HP_ATT_BOTTOM };
	size_t i;

	ck(px2_gain_raw(0, 0) == 0 && px2_gain_raw(0, 1) == 0, "ladder: position 0 is the cut, raw 0");
	ck(px2_gain_raw(20, 0) == 255, "ladder: speaker top is raw 255; got %ld", px2_gain_raw(20, 0));
	ck(px2_gain_raw(1, 0) == 163 && px2_gain_raw(2, 0) == 168,
	   "ladder: speaker positions 1 and 2 under the old floor (172): got %ld, %ld",
	   px2_gain_raw(1, 0), px2_gain_raw(2, 0));
	ck(px2_gain_raw(3, 0) >= 171 && px2_gain_raw(3, 0) <= 174,
	   "ladder: speaker position 3 about the old floor; got %ld", px2_gain_raw(3, 0));
	for (i = 1; i < 20; i++)
		ck(px2_gain_raw((int)i + 1, 0) > px2_gain_raw((int)i, 0),
		   "ladder: speaker position %zu louder than %zu", i + 1, i);
	ck(px2_gain_raw(1, 1) == 76 && px2_gain_raw(20, 1) == 244,
	   "ladder: headphones as they were, 76 to 244; got %ld to %ld",
	   px2_gain_raw(1, 1), px2_gain_raw(20, 1));
	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		ck(define_in("diatom/port/pixel2.c", names[i]) == mine[i],
		   "ladder: Diatom's Pixel port has %s %ld, as the launcher; got %ld",
		   names[i], mine[i], define_in("diatom/port/pixel2.c", names[i]));
}

/* ---- the boost past the top ------------------------------------------------------ */

/* The same as run_to while the bar moves: portkeys woken every frame
 * (bar_tick's 16 ms) as well as for the repeats */
static void frames_to(rig *r, int64_t t)
{
	int guard = 0;

	while (r->now < t && guard++ < 100000) {
		int w = pkkeys_wait(&r->k, r->now);
		int64_t next = r->now + 16;

		if (w >= 0 && r->now + w < next) next = r->now + w;
		if (next > t) next = t;
		r->now = next;
		pass(r, NULL, 0, INT64_MAX);
	}
}

/* A key down or up at r->now, in a pass of its own */
static void key(rig *r, int code, int value)
{
	pk_key e = ev(r->now * 1000, code, value);

	pass(r, &e, 1, r->now * 1000 + 100);
}

/* A key held `ms` and let go, the bar's frames between, then `gap` ms */
static void hold(rig *r, int code, int ms, int gap)
{
	key(r, code, 1);
	frames_to(r, r->now + ms);
	key(r, code, 0);
	frames_to(r, r->now + gap);
}

/* What a reader of the shared file sees there (eq_share_read): the gain, or
 * -1 for no file or none whole; and the bands into `bands` */
static int file_gain(const char *path, int8_t bands[EQ_BANDS])
{
	eq_share s;
	int8_t got[EQ_BANDS], g = -1;
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return -1;
	n = fread(&s, 1, sizeof s, f);
	fclose(f);
	if (n != sizeof s || !eq_share_read(&s, got, &g)) return -1;
	if (bands) memcpy(bands, got, sizeof got);
	return g;
}

/* A launcher's database with the levels and boosts given (-1: not saved), and
 * the shared file's place in a folder not made yet, as /run/zyperos can be */
typedef struct {
	char dir[64], db[128], run[128], share[160];
} scratch;

static bool scratch_make(scratch *sc, int vol, int bright, int vcells, int bcells)
{
	db *d;

	snprintf(sc->dir, sizeof sc->dir, "/tmp/zyperos-pkboost-XXXXXX");
	if (!mkdtemp(sc->dir)) return false;
	snprintf(sc->db, sizeof sc->db, "%s/zyperos.db", sc->dir);
	snprintf(sc->run, sizeof sc->run, "%s/run", sc->dir);
	snprintf(sc->share, sizeof sc->share, "%s/eq", sc->run);
	if (!(d = db_open(sc->db, DB_DEVICE))) return false;
	if (vol >= 0) db_set_int(d, "volume", vol);
	if (bright >= 0) db_set_int(d, "brightness", bright);
	if (vcells >= 0) db_set_int(d, PK_VOL_BOOST, vcells);
	if (bcells >= 0) db_set_int(d, PK_BRIGHT_BOOST, bcells);
	db_close(d);
	return true;
}

static void scratch_drop(scratch *sc)
{
	const char *suffix[] = { "", "-wal", "-shm", "-journal" };
	char p[160];
	unsigned i;

	for (i = 0; i < sizeof suffix / sizeof suffix[0]; i++) {
		snprintf(p, sizeof p, "%s%s", sc->db, suffix[i]);
		unlink(p);
	}
	unlink(sc->share);
	rmdir(sc->run);
	rmdir(sc->dir);
}

/* portkeys as it starts over that database (main): the levels, the boosts */
static bool rig_boost(rig *r, pk_boost *bo, const scratch *sc)
{
	rig_init(r, -1, -1);
	if (sqlite3_open_v2(sc->db, &r->db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) return false;
	pk_levels_load(r->db, "/nonexistent", &r->l);
	pk_boost_init(bo, sc->share);
	r->bo = bo;
	r->now = 10000;
	return pk_boost_load(r->db, &r->l, bo);
}

static void rig_done(rig *r)
{
	pk_boost_free(r->bo);
	sqlite3_close(r->db);
	r->db = NULL;
}

/* A step that is no boost's is pk_apply's: the same level, the same line, at
 * every level the scale has an end or a middle at, both kinds, both ways, a
 * press and a repeat; the gain's file one that cannot be had */
static void the_boost_is_apply(void)
{
	static const int vols[] = { -1, 0, 1, VOL_MAX / 2, VOL_MAX - 1, VOL_MAX };
	static const int brights[] = { 0, 1, BRIGHT_MAX - 1, BRIGHT_MAX };
	unsigned v, b;
	int kind, dir, held, bad = 0;

	for (v = 0; v < sizeof vols / sizeof vols[0]; v++)
		for (b = 0; b < sizeof brights / sizeof brights[0]; b++)
			for (kind = 0; kind < 2; kind++)
				for (dir = -1; dir <= 1; dir += 2)
					for (held = 0; held < 2; held++) {
						pk_levels a = { vols[v], brights[b] }, c = a;
						pk_step s = { kind ? PK_BRIGHT : PK_VOLUME, dir, held };
						pk_boost bo;
						char la[128], lc[128];
						bool moved;
						int did;

						pk_boost_init(&bo, "/dev/null/eq");
						pk_boost_load(NULL, &c, &bo);
						moved = pk_apply(NULL, &a, &s, la, sizeof la);
						did = pk_press(NULL, &c, &bo, &s, 1000, lc, sizeof lc);
						if (strcmp(la, lc) || a.vol != c.vol || a.bright != c.bright ||
						    moved != !!(did & BOOST_LEVEL) || (did & ~BOOST_LEVEL)) {
							if (!bad++)
								ck(0, "boost: a step is pk_apply's: from %d/%d %s %+d%s, \"%s\" vs \"%s\"",
								   vols[v], brights[b], kind ? "brightness" : "volume", dir,
								   held ? " held" : "", la, lc);
						}
					}
	ck(!bad, "boost: every plain step as pk_apply makes it (%d differ)", bad);
}

/* Three presses on the top: open, the gain in the file, kept; a held key
 * then fills it, going down empties it and then folds it. `one_pass`: the
 * third press and its release read in one pass, which must free the stop the
 * opening left as a release of its own does. */
static void the_boost_taps(bool one_pass)
{
	const char *how = one_pass ? " (third press and release in one pass)" : "";
	static const int8_t bands[EQ_BANDS] = { 3, -2, 0, 1, 4 };
	int8_t got[EQ_BANDS];
	pk_boost bo;
	scratch sc;
	rig r;
	db *d;
	int i;

	if (!scratch_make(&sc, VOL_MAX, 7, -1, -1)) {
		ck(0, "boost: a scratch database");
		return;
	}
	ck(rig_boost(&r, &bo, &sc), "boost: the file for the gain is made, its folder too%s", how);
	ck(file_gain(sc.share, got) == 0 && !memcmp(got, (int8_t[EQ_BANDS]){ 0 }, sizeof got),
	   "boost: a new file is whole, flat and 0 dB; got %d", file_gain(sc.share, NULL));
	/* The launcher's bands, as if it had written them: the gain goes in
	 * beside them and leaves them */
	eq_share_put(bo.share, bands, -1);

	hold(&r, PK_KEY_VOLUMEUP, 80, 200);
	hold(&r, PK_KEY_VOLUMEUP, 80, 200);
	ck(!r.opened && !pk_boost_cells(&bo, PK_VOLUME) && file_gain(sc.share, NULL) == 0,
	   "boost: two presses on the top open nothing%s", how);
	if (one_pass) {
		pk_key two[2] = { ev(r.now * 1000, PK_KEY_VOLUMEUP, 1), ev(r.now * 1000 + 40000, PK_KEY_VOLUMEUP, 0) };

		pass(&r, two, 2, r.now * 1000 + 50000);
		frames_to(&r, r.now + 200);
	} else {
		hold(&r, PK_KEY_VOLUMEUP, 80, 200);
	}
	ck(r.opened == 1 && pk_boost_cells(&bo, PK_VOLUME) == 1 && r.l.vol == VOL_MAX,
	   "boost: three presses on the top open it, one cell, the level at the top%s; got %d cells, volume %d",
	   how, pk_boost_cells(&bo, PK_VOLUME), r.l.vol);
	ck(file_gain(sc.share, got) == BOOST_DB_CELL && !memcmp(got, bands, sizeof got),
	   "boost: its %d dB in the file, the bands left as they were%s; got %d", BOOST_DB_CELL, how,
	   file_gain(sc.share, NULL));
	ck(pk_db_get(r.db, PK_VOL_BOOST, -1) == 1 && pk_db_get(r.db, "volume", -1) == VOL_MAX,
	   "boost: kept, one cell beside the top%s", how);
	{
		char want[64];

		snprintf(want, sizeof want, "portkeys: volume boost +0 -> +%d dB (VOL+)\n", BOOST_DB_CELL);
		ck(strstr(r.log, want) != NULL, "boost: the log says it opened; got\n%s", r.log);
	}

	/* A press and a held key fill it, each repeat a cell, to the last */
	hold(&r, PK_KEY_VOLUMEUP, REPEAT_DELAY_MS + 3 * REPEAT_RATE_MS + 20, 200);
	ck(pk_boost_cells(&bo, PK_VOLUME) == BOOST_VOL_CELLS &&
	   file_gain(sc.share, NULL) == BOOST_VOL_CELLS * BOOST_DB_CELL,
	   "boost: a held key fills it after a let-go, to %d cells, %d dB%s; got %d cells, %d dB",
	   BOOST_VOL_CELLS, BOOST_VOL_CELLS * BOOST_DB_CELL, how, pk_boost_cells(&bo, PK_VOLUME),
	   file_gain(sc.share, NULL));

	/* Going down empties a cell and leaves the level */
	hold(&r, PK_KEY_VOLUMEDOWN, 80, 200);
	ck(pk_boost_cells(&bo, PK_VOLUME) == 5 && r.l.vol == VOL_MAX && file_gain(sc.share, NULL) == 5 * BOOST_DB_CELL &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == 5 && pk_db_get(r.db, "volume", -1) == VOL_MAX,
	   "boost: VOL- takes a cell off and leaves the level; got %d cells, volume %d",
	   pk_boost_cells(&bo, PK_VOLUME), r.l.vol);
	ck(pk_boost_gone(r.db, &bo) == 0 && pk_boost_cells(&bo, PK_VOLUME) == 5,
	   "boost: a filled one stays when the bar goes");
	rig_done(&r);

	/* Kept: portkeys started again, with the file gone, and the launcher */
	unlink(sc.share);
	ck(rig_boost(&r, &bo, &sc) && pk_boost_cells(&bo, PK_VOLUME) == 5 && file_gain(sc.share, NULL) == 5 * BOOST_DB_CELL,
	   "boost: kept, the five cells back as portkeys starts, their 5 cells' dB in the file; got %d cells, %d dB",
	   pk_boost_cells(&bo, PK_VOLUME), file_gain(sc.share, NULL));
	if ((d = db_open(sc.db, DB_DEVICE))) {
		ck(db_get_int(d, PK_VOL_BOOST, -1) == 5, "boost: and the launcher reads them (src/db.c)");
		db_close(d);
	}

	/* Down to none, which waits at the top, then one more under it */
	for (i = 0; i < 5; i++) hold(&r, PK_KEY_VOLUMEDOWN, 80, 150);
	ck(bo.bar[PK_VOLUME].open && !pk_boost_cells(&bo, PK_VOLUME) && r.l.vol == VOL_MAX &&
	   file_gain(sc.share, NULL) == 0, "boost: emptied, it waits at the top, 0 dB");
	hold(&r, PK_KEY_VOLUMEDOWN, 80, 150);
	ck(!bo.bar[PK_VOLUME].open && r.l.vol == VOL_MAX - 1 && pk_db_get(r.db, "volume", -1) == VOL_MAX - 1 &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == 0, "boost: one more press folds it and goes under the top; got volume %d",
	   r.l.vol);
	rig_done(&r);
	scratch_drop(&sc);
}

/* A hold on the top: let go before its time, nothing; held its time, open,
 * and the key still held stops at the first cell until let go */
static void the_boost_hold(void)
{
	pk_boost bo;
	scratch sc;
	rig r;

	if (!scratch_make(&sc, VOL_MAX, 7, -1, -1) || !rig_boost(&r, &bo, &sc)) {
		ck(0, "boost: a scratch database");
		return;
	}
	key(&r, PK_KEY_VOLUMEUP, 1);
	frames_to(&r, r.now + BOOST_HOLD_MS / 2);
	ck(pk_boost_moving(&bo, (uint32_t)r.now), "boost: a hold on the top charges, drawn every frame");
	key(&r, PK_KEY_VOLUMEUP, 0);
	frames_to(&r, r.now + BOOST_GAP_MS + BOOST_HOLD_MS);
	ck(!r.opened && !bo.bar[PK_VOLUME].open && file_gain(sc.share, NULL) == 0 &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == -1,
	   "boost: let go mid-charge, nothing opens, no gain, nothing kept");
	ck(!pk_boost_moving(&bo, (uint32_t)r.now), "boost: and the charge has drained");

	key(&r, PK_KEY_VOLUMEUP, 1);
	frames_to(&r, r.now + BOOST_HOLD_MS + 20);
	ck(r.opened == 1 && pk_boost_cells(&bo, PK_VOLUME) == 1 && file_gain(sc.share, NULL) == BOOST_DB_CELL &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == 1,
	   "boost: held %d ms on the top it opens, %d dB, kept; got %d cells", BOOST_HOLD_MS, BOOST_DB_CELL,
	   pk_boost_cells(&bo, PK_VOLUME));
	frames_to(&r, r.now + 1000);
	ck(pk_boost_cells(&bo, PK_VOLUME) == 1, "boost: the key still held stops at the first cell; got %d",
	   pk_boost_cells(&bo, PK_VOLUME));
	key(&r, PK_KEY_VOLUMEUP, 0);
	frames_to(&r, r.now + 100);
	hold(&r, PK_KEY_VOLUMEUP, 80, 100);
	ck(pk_boost_cells(&bo, PK_VOLUME) == 2 && file_gain(sc.share, NULL) == 2 * BOOST_DB_CELL,
	   "boost: let go, the next press is the next cell; got %d", pk_boost_cells(&bo, PK_VOLUME));
	rig_done(&r);
	scratch_drop(&sc);
}

/* MENU+VOL+ on the top rung, three times: the backlight's boost, its lift,
 * kept apart from the volume's */
static void the_boost_bright(void)
{
	pk_boost bo;
	scratch sc;
	rig r;
	int i;

	if (!scratch_make(&sc, 10, BRIGHT_MAX, -1, -1) || !rig_boost(&r, &bo, &sc)) {
		ck(0, "boost: a scratch database");
		return;
	}
	for (i = 0; i < BOOST_TAPS; i++) {
		menu_chord(&r, PK_KEY_VOLUMEUP, i & 1, BOTH);
		frames_to(&r, r.now + 200);
	}
	ck(r.opened == 1 && pk_boost_lift(&bo) == 1 && !pk_boost_cells(&bo, PK_VOLUME) &&
	   r.l.bright == BRIGHT_MAX && r.l.vol == 10,
	   "boost: MENU+VOL+ three times on the top rung opens the backlight's, lift 1; got lift %d",
	   pk_boost_lift(&bo));
	ck(pk_db_get(r.db, PK_BRIGHT_BOOST, -1) == 1 && pk_db_get(r.db, PK_VOL_BOOST, -1) == -1 &&
	   file_gain(sc.share, NULL) == 0, "boost: kept as the backlight's, no gain in the file");
	ck(strstr(r.log, "portkeys: brightness boost 0 -> 1 (MENU+VOL+)\n") != NULL,
	   "boost: the log says it; got\n%s", r.log);
	rig_done(&r);
	scratch_drop(&sc);
}

/* What closes one other than the keys: a plug in or out, the volume's; the
 * bar going away, an empty one; and a boost kept under the top never opens */
static void the_boost_closing(void)
{
	pk_boost bo;
	scratch sc;
	rig r;
	int i;

	if (!scratch_make(&sc, VOL_MAX, 7, 3, -1) || !rig_boost(&r, &bo, &sc)) {
		ck(0, "boost: a scratch database");
		return;
	}
	ck(pk_boost_cells(&bo, PK_VOLUME) == 3 && file_gain(sc.share, NULL) == 3 * BOOST_DB_CELL,
	   "boost: three cells kept, %d dB written as portkeys starts; got %d", 3 * BOOST_DB_CELL,
	   file_gain(sc.share, NULL));
	ck(pk_boost_output_changed(r.db, &bo) && !bo.bar[PK_VOLUME].open && file_gain(sc.share, NULL) == 0 &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == 0 && r.l.vol == VOL_MAX,
	   "boost: a plug in or out closes the volume's, 0 dB, kept closed, the level left");
	ck(!pk_boost_output_changed(r.db, &bo), "boost: and with none open, it is no change");

	for (i = 0; i < BOOST_TAPS; i++) hold(&r, PK_KEY_VOLUMEUP, 80, 200);
	hold(&r, PK_KEY_VOLUMEDOWN, 80, 200);
	ck(bo.bar[PK_VOLUME].open && !pk_boost_cells(&bo, PK_VOLUME), "boost: opened and emptied, it waits");
	ck(pk_boost_gone(r.db, &bo) == 1 << PK_VOLUME && !bo.bar[PK_VOLUME].open &&
	   pk_db_get(r.db, PK_VOL_BOOST, -1) == 0 && file_gain(sc.share, NULL) == 0,
	   "boost: an empty one closes when the bar goes");
	rig_done(&r);
	scratch_drop(&sc);

	if (!scratch_make(&sc, VOL_MAX - 5, 7, 3, -1) || !rig_boost(&r, &bo, &sc)) {
		ck(0, "boost: a scratch database");
		return;
	}
	ck(!bo.bar[PK_VOLUME].open && file_gain(sc.share, NULL) == 0,
	   "boost: cells kept beside a level under the top do not open");
	rig_done(&r);
	scratch_drop(&sc);
}

/* The launcher's own buzz length, the number src/levels.c hands
 * plat_rumble_buzz as its boost opens; -1 when it cannot be read */
static long launcher_buzz_ms(void)
{
	FILE *f = fopen("src/levels.c", "r");
	char line[256];
	long ms = -1;

	if (!f) return -1;
	while (fgets(line, sizeof line, f)) {
		char *at = strstr(line, "plat_rumble_buzz("), *comma;

		if (at && (comma = strrchr(at, ',')) && sscanf(comma + 1, " %ld", &ms) == 1) break;
		ms = -1;
	}
	fclose(f);
	return ms;
}

/* The boost's buzz as it opens (pk_buzz_pct), as the launcher gives its
 * own (src/levels.c): at MENU > Sound > Rumble's share, the word written and
 * read back through the launcher's src/db.c and src/rumble.c; once for each
 * opening of either boost, by presses or by a hold; never for a step, a cell
 * filled or emptied or a fold; none with Rumble Off. Its length is the
 * launcher's and Diatom's, its strength the launcher's scale. On 1.1.0-rc.8
 * portkeys gave none: nothing read BOOST_OPENED. */
static void the_boost_buzz(void)
{
	static const char *const words[] = { NULL, "off", "low", "medium", "full", "louder" };
	unsigned w;
	int pct;

	for (w = 0; w < sizeof words / sizeof words[0]; w++) {
		const char *name = words[w] ? words[w] : "(none)";
		const int want = rumble_level_pct(rumble_level_parse(words[w], RUMBLE_DEFAULT));
		const int felt = want > 0 ? 1 : 0;
		pk_boost bo;
		scratch sc;
		rig r;
		db *d;
		int i;

		if (!scratch_make(&sc, VOL_MAX, BRIGHT_MAX, -1, -1) || !(d = db_open(sc.db, DB_DEVICE))) {
			ck(0, "buzz: a scratch database");
			return;
		}
		if (words[w]) db_set_str(d, "rumble", words[w]);
		db_close(d);
		if (!rig_boost(&r, &bo, &sc)) {
			ck(0, "buzz: portkeys over the scratch database");
			scratch_drop(&sc);
			return;
		}
		ck(pk_rumble_pct(r.db) == want, "buzz: Rumble %s is %d%%, as the launcher reads it; got %d%%", name,
		   want, pk_rumble_pct(r.db));
		ck(!pk_buzz_pct(r.db, BOOST_LEVEL) && !pk_buzz_pct(r.db, BOOST_GAIN) &&
		   !pk_buzz_pct(r.db, BOOST_LEVEL | BOOST_GAIN) && pk_buzz_pct(r.db, BOOST_GAIN | BOOST_OPENED) == want,
		   "buzz: Rumble %s, only an opening asks the motor", name);

		/* Three presses on the top: one buzz */
		for (i = 0; i < BOOST_TAPS; i++) hold(&r, PK_KEY_VOLUMEUP, 80, 200);
		ck(r.opened == 1 && r.buzzes == felt && (!felt || r.buzz_pct == want),
		   "buzz: Rumble %s, three presses open the volume's, %d buzz at %d%%; got %d at %d%%", name, felt,
		   want, r.buzzes, r.buzz_pct);
		/* Filled by a held key, emptied, folded under the top: none more */
		hold(&r, PK_KEY_VOLUMEUP, REPEAT_DELAY_MS + 3 * REPEAT_RATE_MS + 20, 200);
		for (i = 0; i <= BOOST_VOL_CELLS; i++) hold(&r, PK_KEY_VOLUMEDOWN, 80, 150);
		hold(&r, PK_KEY_VOLUMEUP, 80, 200);
		ck(r.l.vol == VOL_MAX && !bo.bar[PK_VOLUME].open && r.buzzes == felt,
		   "buzz: Rumble %s, the cells filled, emptied and folded, and a step back to the top: no buzz; "
		   "got %d", name, r.buzzes - felt);
		/* A hold on the top: the second */
		key(&r, PK_KEY_VOLUMEUP, 1);
		frames_to(&r, r.now + BOOST_HOLD_MS + 20);
		key(&r, PK_KEY_VOLUMEUP, 0);
		frames_to(&r, r.now + 200);
		ck(r.opened == 2 && r.buzzes == 2 * felt,
		   "buzz: Rumble %s, held on the top it opens again, buzzing again; got %d buzzes", name, r.buzzes);
		/* MENU+VOL+ three times on the top rung: the backlight's, the third */
		for (i = 0; i < BOOST_TAPS; i++) {
			menu_chord(&r, PK_KEY_VOLUMEUP, i & 1, BOTH);
			frames_to(&r, r.now + 200);
		}
		ck(r.opened == 3 && pk_boost_lift(&bo) == 1 && r.buzzes == 3 * felt,
		   "buzz: Rumble %s, the backlight's opening buzzes as the volume's; got %d buzzes", name, r.buzzes);
		rig_done(&r);
		scratch_drop(&sc);
	}
	ck(pk_rumble_pct(NULL) == rumble_level_pct(RUMBLE_DEFAULT),
	   "buzz: with no database, the launcher's own default, %d%%", rumble_level_pct(RUMBLE_DEFAULT));

	/* As long as the launcher's and Diatom's, as strong as the launcher's */
	ck(launcher_buzz_ms() == PK_BUZZ_MS && define_in("diatom/src/levels.c", "BUZZ_MS") == PK_BUZZ_MS,
	   "buzz: %d ms, as the launcher's (src/levels.c, %ld) and Diatom's (BUZZ_MS, %ld)", PK_BUZZ_MS,
	   launcher_buzz_ms(), define_in("diatom/src/levels.c", "BUZZ_MS"));
	for (pct = 0; pct <= 100; pct++) {
		const float scale = (float)pct / 100.0f;
		const uint16_t v = pct <= 0 ? 0 : scale >= 1.0f ? 0xFFFF : (uint16_t)((float)0xFFFF * scale + 0.5f);

		if (pk_buzz_strength(pct) != v) break;
	}
	ck(pct > 100 && pk_buzz_strength(100) == 0xFFFF && pk_buzz_strength(0) == 0 && pk_buzz_strength(1) > 0,
	   "buzz: the strength at every share as plat_rumble_buzz scales it; first apart at %d%%", pct);
}

/* ---- whose display may be borrowed (pk_looks) ---------------------------------
 *
 * A display file is copied only once settled: seen open by the same process
 * under the same number at a look PK_SETTLE_MS before, with no gap longer
 * than PK_LOOK_GAP_MS, or of a process whose master was held. And the rc.9
 * failure as a timeline: SDL 2.32's two looks at the display as a program
 * starts (open, take the master, check, close; a few ms each), then its own
 * file for good; portkeys looking once a second, or as a sighting settles:
 * at no start phase is a copy allowed inside SDL's looks, and the program's
 * own file is copied within PK_SETTLE_MS and a look of its opening. */
static void the_display_looks(void)
{
	pk_looks l;
	bool ok;
	int t0, bad = 0, late = 0, worst = 0;

	pk_looks_init(&l);
	pk_looks_begin(&l, 1000);
	ok = pk_looks_seen(&l, 935, 7, 1000);
	pk_looks_end(&l, true);
	ck(!ok && pk_looks_wait(&l, 1000) == PK_SETTLE_MS,
	   "looks: a display file seen once is not copied, settling in %d ms; got %ld", PK_SETTLE_MS,
	   pk_looks_wait(&l, 1000));
	pk_looks_begin(&l, 1000 + PK_SETTLE_MS);
	ok = pk_looks_seen(&l, 935, 7, 1000 + PK_SETTLE_MS);
	pk_looks_end(&l, true);
	ck(ok && pk_looks_wait(&l, 1000 + PK_SETTLE_MS) == -1, "looks: still open %d ms on, it is", PK_SETTLE_MS);

	/* Gone at a look, then open again: from the start again */
	pk_looks_init(&l);
	pk_looks_begin(&l, 1000);
	pk_looks_seen(&l, 935, 7, 1000);
	pk_looks_end(&l, true);
	pk_looks_begin(&l, 1300);
	pk_looks_end(&l, true);
	pk_looks_begin(&l, 1600);
	ok = pk_looks_seen(&l, 935, 7, 1600);
	pk_looks_end(&l, true);
	ck(!ok && l.n == 1, "looks: closed at a look and open again, it settles afresh");

	/* A gap in the looks longer than PK_LOOK_GAP_MS: afresh too */
	pk_looks_init(&l);
	pk_looks_begin(&l, 1000);
	pk_looks_seen(&l, 935, 7, 1000);
	pk_looks_end(&l, true);
	pk_looks_begin(&l, 1001 + PK_LOOK_GAP_MS);
	ok = pk_looks_seen(&l, 935, 7, 1001 + PK_LOOK_GAP_MS);
	pk_looks_end(&l, true);
	ck(!ok, "looks: after a gap of more than %d ms between looks, afresh", PK_LOOK_GAP_MS);

	/* A process whose master was held: at once; any other: not */
	pk_looks_trust(&l, 935);
	pk_looks_begin(&l, 5000);
	ok = pk_looks_seen(&l, 935, 9, 5000) && !pk_looks_seen(&l, 1051, 7, 5000);
	pk_looks_end(&l, true);
	ck(ok, "looks: the process whose master was held at once, another one not");

	/* A look stopped at the master it found keeps what it did not reach */
	pk_looks_init(&l);
	pk_looks_begin(&l, 1000);
	pk_looks_seen(&l, 700, 4, 1000);
	pk_looks_seen(&l, 935, 7, 1000);
	pk_looks_end(&l, true);
	pk_looks_begin(&l, 1400);
	pk_looks_seen(&l, 700, 4, 1400);
	pk_looks_end(&l, false);
	pk_looks_begin(&l, 1500);
	ok = pk_looks_seen(&l, 935, 7, 1500);
	pk_looks_end(&l, true);
	ck(ok, "looks: one a stopped look did not reach keeps its time");

	/* The timeline, at every start phase to the millisecond: portkeys's
	 * looks from 0, once a second or as a sighting settles; the program's
	 * SDL looks at t0 (3 ms) and t0 + 3 (3 ms), its own file from t0 + 8 */
	for (t0 = 0; t0 < 2000; t0++) {
		long t = 0, copied = -1;
		int guard = 0;

		pk_looks_init(&l);
		while (t < t0 + 4000 && copied < 0 && guard++ < 1000) {
			const bool probe = (t >= t0 && t < t0 + 6), real = t >= t0 + 8;
			long w;

			pk_looks_begin(&l, t);
			/* splash's file, open all the while and never the master */
			pk_looks_seen(&l, 600, 5, t);
			if ((probe || real) && pk_looks_seen(&l, 935, 7, t)) {
				if (probe) bad++;
				else copied = t;
			}
			pk_looks_end(&l, true);
			w = pk_looks_wait(&l, t);
			t += w >= 0 ? (w < 10 ? 10 : w) : 1000;
		}
		if (copied < 0) late++;
		else if (copied - (t0 + 8) > worst) worst = (int)(copied - (t0 + 8));
	}
	ck(!bad, "looks: at no start phase a copy inside SDL's look at the display; got %d", bad);
	ck(!late && worst <= 1000 + PK_SETTLE_MS + 10,
	   "looks: the program's own display copied within a look and %d ms of its opening; worst %d ms, %d never",
	   PK_SETTLE_MS, worst, late);
}

/* A boost kept from before, as portkeys starts over it (pk_boost_load): it
 * is open, its gain in the file, and nothing of it moves or opens - no
 * charge, no flash, no buzz - so nothing asks for the bar or the display
 * until a key is pressed (on 1.1.0-rc.9 the display was looked for at the
 * start, for On-Screen Stats, not for this; the_display_looks holds that) */
static void the_kept_boost_at_start(void)
{
	pk_boost bo;
	scratch sc;
	rig r;

	if (!scratch_make(&sc, VOL_MAX, BRIGHT_MAX, BOOST_VOL_CELLS, BOOST_BRIGHT_CELLS) || !rig_boost(&r, &bo, &sc)) {
		ck(0, "kept: a scratch database");
		return;
	}
	ck(pk_boost_cells(&bo, PK_VOLUME) == BOOST_VOL_CELLS && pk_boost_lift(&bo) == BOOST_BRIGHT_CELLS &&
	   file_gain(sc.share, NULL) == BOOST_VOL_CELLS * BOOST_DB_CELL,
	   "kept: both boosts back as portkeys starts, the gain in the file");
	ck(!pk_boost_moving(&bo, (uint32_t)r.now), "kept: nothing of them moves at the start");
	frames_to(&r, r.now + 3000);
	ck(!r.did && !r.opened && !r.buzzes && !pk_boost_moving(&bo, (uint32_t)r.now) && r.n == 0,
	   "kept: 3 s with no key: no step, no opening, no buzz, nothing to draw; got did %d", r.did);
	rig_done(&r);
	scratch_drop(&sc);
}

/* ---- over your PICO-8: SELECT's hotkeys, and its keyboard ------------------
 *
 * tools/pkhot.c, Diatom's chord rule on the joypad's events with the table
 * the launcher hands over: SELECT then the button is the act, the button a
 * moment before SELECT too, the d-pad first makes SELECT the game's, the
 * acts portkeys cannot do (Quick Save) are none, a SELECT held alone long is
 * still a hotkey's holder, the kernel's autorepeat is nothing, and a resync
 * that finds SELECT down starts no chord. And tools/pkkbd.c's CTRL-R as the
 * kernel reads it from uinput: Left Ctrl down, R down, then R up and Left
 * Ctrl up, each with a report, the keys held across the wait. */
static unsigned hot_ev(pkhot *h, uint16_t code, int value, uint64_t ms)
{
	return pkhot_key(h, code, value, ms);
}

static void the_hotkeys(void)
{
	pkhot h;
	unsigned a;

	pkhot_init(&h);
	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 1000) | hot_ev(&h, PKHOT_BTN_WEST, 1, 1050);
	ck(a == 0, "hotkeys: none handed over, SELECT+Y is the game's");
	ck(pkhot_set(&h, "save:r1,load:l1,faster:r2,slower:l2,shot:y,reset:b,quit:x"),
	   "hotkeys: hk_run's form is read");
	ck(!pkhot_set(&h, "shot:menu") && !h.on, "hotkeys: MENU as a key is refused, and none kept");
	pkhot_init(&h);
	pkhot_set(&h, "save:r1,load:l1,faster:r2,slower:l2,shot:y,reset:b,quit:x");
	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 1000);
	a |= hot_ev(&h, PKHOT_BTN_WEST, 1, 1050);
	ck(a == (1u << CHORD_SHOT), "hotkeys: SELECT then Y is the screenshot (%x)", a);
	a = hot_ev(&h, PKHOT_BTN_WEST, 0, 1100) | hot_ev(&h, PKHOT_BTN_EAST, 1, 1150);
	ck(a == (1u << CHORD_RESET), "hotkeys: B under the same SELECT is Reset Game (%x)", a);
	a = hot_ev(&h, PKHOT_BTN_EAST, 0, 1200) | hot_ev(&h, PKHOT_BTN_SELECT, 0, 1250);
	ck(a == 0, "hotkeys: the let-goes are nothing");

	a = hot_ev(&h, PKHOT_BTN_NORTH, 1, 2000) | hot_ev(&h, PKHOT_BTN_SELECT, 1, 2000 + CHORD_LEAD_MS);
	ck(a == (1u << CHORD_QUIT), "hotkeys: X a moment before SELECT is Quit (%x)", a);
	hot_ev(&h, PKHOT_BTN_NORTH, 0, 2200);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 2210);
	a = hot_ev(&h, PKHOT_BTN_NORTH, 1, 3000) | hot_ev(&h, PKHOT_BTN_SELECT, 1, 3000 + CHORD_LEAD_MS + 50);
	ck(a == 0, "hotkeys: X held longer before SELECT is the game's");
	hot_ev(&h, PKHOT_BTN_NORTH, 0, 3300);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 3310);

	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 4000) | hot_ev(&h, PKHOT_BTN_UP, 1, 4020) |
	    hot_ev(&h, PKHOT_BTN_WEST, 1, 4040);
	ck(a == 0, "hotkeys: SELECT with the d-pad first is the game's, Y after it too");
	hot_ev(&h, PKHOT_BTN_UP, 0, 4100);
	hot_ev(&h, PKHOT_BTN_WEST, 0, 4110);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 4120);

	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 5000) | hot_ev(&h, PKHOT_BTN_TR, 1, 5050);
	ck(a == 0, "hotkeys: SELECT+R1, Quick Save, is no act over your PICO-8");
	hot_ev(&h, PKHOT_BTN_TR, 0, 5100);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 5110);

	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 6000) | hot_ev(&h, PKHOT_BTN_SELECT, 2, 6300) |
	    hot_ev(&h, PKHOT_BTN_WEST, 1, 6000 + CHORD_HOLD_MS + 200);
	ck(a == (1u << CHORD_SHOT), "hotkeys: SELECT held alone long, then Y, is still the screenshot");
	hot_ev(&h, PKHOT_BTN_WEST, 0, 7000);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 7010);

	pkhot_resync(&h, pkhot_bit(PKHOT_BTN_SELECT));
	a = hot_ev(&h, PKHOT_BTN_WEST, 1, 8000);
	ck(a == 0, "hotkeys: a SELECT a resync found down starts no chord");
	hot_ev(&h, PKHOT_BTN_WEST, 0, 8050);
	hot_ev(&h, PKHOT_BTN_SELECT, 0, 8060);
	a = hot_ev(&h, PKHOT_BTN_SELECT, 1, 9000) | hot_ev(&h, PKHOT_BTN_WEST, 1, 9050);
	ck(a == (1u << CHORD_SHOT), "hotkeys: and the next SELECT is one again");
}

/* What PICO-8 reads while portkeys holds the joypad (pkhot_out, fed to
 * tools/pkvpad.c's pad): every button at once, MENU never; with hotkeys, a
 * chord never (SELECT and its button, the button a moment before SELECT
 * too), SELECT alone given back as it is let go for at least two turns, or
 * the game's once held CHORD_HOLD_MS or pressed with another button; and the
 * pad's writes, one event a button that changed and a report */
#define B_(x) DIATOM_BIT(DIATOM_BTN_##x)

/* Turns between events up to `until`, as portkeys's loop takes them */
static void pad_ticks_to(pkhot *h, uint64_t *t, uint64_t until)
{
	while (*t < until) {
		int w = pkhot_wait(h, *t);

		if (w < 0) {
			*t = until;
			break;
		}
		*t += (uint64_t)(w > 0 ? w : 1);
		if (*t > until) *t = until;
		pkhot_tick(h, *t);
	}
}

static void the_game_pad(void)
{
	pkhot h;
	uint64_t t;
	bool given = false;

	pkhot_init(&h);
	pkhot_key(&h, PKHOT_BTN_SELECT, 1, 1000);
	ck(pkhot_out(&h) == B_(SELECT), "game pad: no hotkeys, SELECT at once");
	pkhot_key(&h, 0x2c0, 1, 1010);
	ck(pkhot_out(&h) == B_(SELECT), "game pad: MENU never reaches the game");
	pkhot_key(&h, 0x2c0, 0, 1020);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, 1030);
	ck(pkhot_out(&h) == 0 && pkhot_wait(&h, 1030) < 0, "game pad: no hotkeys, nothing waits");

	pkhot_set(&h, "save:r1,load:l1,faster:r2,slower:l2,shot:y,quit:x");
	t = 2000;
	pkhot_key(&h, PKHOT_BTN_SOUTH, 1, t);
	ck(pkhot_out(&h) == B_(A), "game pad: A at once, the event it came in");
	pkhot_key(&h, PKHOT_BTN_SOUTH, 0, t += 30);
	ck(pkhot_out(&h) == 0, "game pad: and let go at once");

	t = 3000;
	pkhot_key(&h, PKHOT_BTN_SELECT, 1, t);
	ck(pkhot_out(&h) == 0 && pkhot_wait(&h, t) == PKHOT_TICK_MS, "game pad: SELECT waits to see whose it is");
	pad_ticks_to(&h, &t, 3050);
	ck(pkhot_key(&h, PKHOT_BTN_WEST, 1, t) == (1u << CHORD_SHOT) && pkhot_out(&h) == 0,
	   "game pad: SELECT+Y is the screenshot, and the game sees neither");
	pkhot_key(&h, PKHOT_BTN_WEST, 0, t += 40);
	pad_ticks_to(&h, &t, 3150);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, t);
	pad_ticks_to(&h, &t, 3400);
	ck(pkhot_out(&h) == 0 && pkhot_wait(&h, t) < 0, "game pad: nor SELECT after the chord, nothing owed");

	t = 4000;
	pkhot_key(&h, PKHOT_BTN_SELECT, 1, t);
	pad_ticks_to(&h, &t, 4120);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, t);
	{
		int turns = 0;

		while (pkhot_wait(&h, t) >= 0 && turns < 40) {
			t += (uint64_t)PKHOT_TICK_MS;
			pkhot_tick(&h, t);
			if (pkhot_out(&h) & B_(SELECT)) given = true, turns++;
			else if (given) break;
		}
		ck(given && turns >= 2, "game pad: SELECT alone given as it is let go, two turns at least (%d)", turns);
	}
	ck(pkhot_out(&h) == 0 && pkhot_wait(&h, t) < 0, "game pad: and taken back after");

	t = 5000;
	pkhot_key(&h, PKHOT_BTN_SELECT, 1, t);
	pkhot_key(&h, PKHOT_BTN_UP, 1, t += 20);
	ck(pkhot_out(&h) == (B_(SELECT) | B_(UP)), "game pad: SELECT with the d-pad, both the game's at once");
	pkhot_key(&h, PKHOT_BTN_UP, 0, t += 20);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, t += 20);
	ck(pkhot_out(&h) == 0, "game pad: and let go");

	t = 6000;
	pkhot_key(&h, PKHOT_BTN_NORTH, 1, t);
	ck(pkhot_out(&h) == B_(X), "game pad: X alone, the game's at once");
	ck(pkhot_key(&h, PKHOT_BTN_SELECT, 1, t += 60) == (1u << CHORD_QUIT) && pkhot_out(&h) == 0,
	   "game pad: SELECT a moment after it, Quit, and X taken back from the game");
	pkhot_key(&h, PKHOT_BTN_NORTH, 0, t += 30);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, t += 30);
	pad_ticks_to(&h, &t, 6400);
	ck(pkhot_out(&h) == 0, "game pad: nothing after");

	t = 7000;
	pkhot_key(&h, PKHOT_BTN_SELECT, 1, t);
	pad_ticks_to(&h, &t, 7000 + CHORD_HOLD_MS + 2 * PKHOT_TICK_MS);
	ck(pkhot_out(&h) == B_(SELECT), "game pad: SELECT held alone past CHORD_HOLD_MS is the game's, held");
	ck(pkhot_key(&h, PKHOT_BTN_WEST, 1, t += 10) == (1u << CHORD_SHOT) && !(pkhot_out(&h) & B_(Y)),
	   "game pad: Y then is still the screenshot, and hidden");
	pkhot_key(&h, PKHOT_BTN_WEST, 0, t += 30);
	pkhot_key(&h, PKHOT_BTN_SELECT, 0, t += 30);
	ck(pkhot_out(&h) == 0, "game pad: let go");
}

static void the_vpad_writes(void)
{
	int fds[2];
	struct { struct timeval t; uint16_t type, code; int32_t value; } e[4];
	ssize_t r;

	if (pipe(fds) != 0) {
		ck(0, "vpad: a pipe");
		return;
	}
	ck(pkvpad_put(fds[1], 0, B_(A) | B_(SELECT)), "vpad: A and SELECT written");
	r = read(fds[0], e, sizeof e);
	ck(r == 3 * (ssize_t)sizeof e[0] && e[0].type == 1 && e[0].code == PKHOT_BTN_SOUTH && e[0].value == 1 &&
	   e[1].type == 1 && e[1].code == PKHOT_BTN_SELECT && e[1].value == 1 && e[2].type == 0 && e[2].code == 0,
	   "vpad: each button pressed, then a report");
	ck(pkvpad_put(fds[1], B_(A), B_(A)), "vpad: nothing changed is no write");
	ck(pkvpad_put(fds[1], B_(A) | B_(SELECT), B_(A)), "vpad: SELECT let go");
	r = read(fds[0], e, sizeof e);
	ck(r == 2 * (ssize_t)sizeof e[0] && e[0].code == PKHOT_BTN_SELECT && e[0].value == 0 && e[1].type == 0,
	   "vpad: only what changed, let go, and a report");
	ck(pkvpad_put(fds[1], 0, B_(MENU)) && !pkvpad_put(-1, 0, B_(A)), "vpad: MENU is no button of it; none to write on is false");
	close(fds[1]);
	r = read(fds[0], e, sizeof e);
	ck(r == 0, "vpad: and MENU wrote nothing");
	close(fds[0]);
}

static void the_keyboard(void)
{
	int fds[2], i;
	struct { struct timeval t; uint16_t type, code; int32_t value; } e[8];
	static const int want[8][3] = {
		{ 1, PKKBD_KEY_LEFTCTRL, 1 }, { 0, 0, 0 }, { 1, PKKBD_KEY_R, 1 }, { 0, 0, 0 },
		{ 1, PKKBD_KEY_R, 0 }, { 0, 0, 0 }, { 1, PKKBD_KEY_LEFTCTRL, 0 }, { 0, 0, 0 },
	};
	bool same = true;

	ck(sizeof e[0] == 24, "keyboard: an input_event is 24 bytes, the kernel's on a 64-bit system");
	if (pipe(fds) != 0) {
		ck(0, "keyboard: a pipe");
		return;
	}
	ck(pkkbd_ctrl_r(fds[1], NULL), "keyboard: CTRL-R written");
	ck(!pkkbd_ctrl_r(-1, NULL), "keyboard: none to write on is false");
	close(fds[1]);
	ck(read(fds[0], e, sizeof e) == (ssize_t)sizeof e, "keyboard: eight events");
	close(fds[0]);
	for (i = 0; i < 8; i++)
		if (e[i].type != want[i][0] || e[i].code != want[i][1] || e[i].value != want[i][2]) same = false;
	ck(same, "keyboard: Left Ctrl, R, then R and Left Ctrl let go, each with a report");
}

/* tools/pkshot.c: PICO-8's own 128 x 128 out of a frame of the panel,
 * turned as the Pixel 2's is and as a landscape one, in every rect the
 * Display modes draw it in (src/p8draw.h), the cut rows of Zoomed and Edges
 * Cut black; the PNG whole (its chunks' CRCs, its pixels back through
 * zlib); and the shot laid into the pop-up's square, the corners left to
 * the slab */
static uint8_t cell(int i, int j, int k)
{
	return (uint8_t)(k == 0 ? i * 2 : k == 1 ? j * 2 : (i ^ j) | 1);
}

static bool shot_mode(const char *mode, bool turned)
{
	const int lw = 640, lh = 480, fw = turned ? lh : lw, fh = turned ? lw : lh;
	uint32_t *f = calloc((size_t)fw * fh, 4);
	uint8_t out[PKSHOT_BYTES];
	dmode_rect r;
	int x, y, i, j;
	bool ok = true;

	if (!f) return false;
	p8_draw_rect(mode, lw, lh, true, &r);
	/* PICO-8's pixels on the landscape window where the mode draws them,
	 * each a block, and the window turned onto the panel */
	for (y = 0; y < lh; y++)
		for (x = 0; x < lw; x++) {
			uint32_t v = 0x00101010;

			if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
				i = (int)((long)(x - r.x) * PKSHOT_SIDE / r.w);
				j = (int)((long)(y - r.y) * PKSHOT_SIDE / r.h);
				v = (uint32_t)cell(i, j, 0) << 16 | (uint32_t)cell(i, j, 1) << 8 | cell(i, j, 2);
			}
			if (turned) f[(size_t)(lw - 1 - x) * fw + y] = v;
			else        f[(size_t)y * fw + x] = v;
		}
	pkshot_pick((const uint8_t *)f, fw, fh, (size_t)fw * 4, &r, out);
	for (j = 0; j < PKSHOT_SIDE; j++)
		for (i = 0; i < PKSHOT_SIDE; i++) {
			const uint8_t *d = out + ((size_t)j * PKSHOT_SIDE + i) * 3;
			const int cy = r.y + (int)((j + 0.5) * r.h / PKSHOT_SIDE);
			const bool on = cy >= 0 && cy < lh;

			if (on ? d[0] != cell(i, j, 0) || d[1] != cell(i, j, 1) || d[2] != cell(i, j, 2)
			       : d[0] || d[1] || d[2])
				ok = false;
		}
	free(f);
	return ok;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void the_screenshot(void)
{
	static const char *const modes[] = { "stretch", "aspect", "square", "integer", "integer-vertical", "overscale",
	                                     "fill", "native" };
	static uint8_t rgb[PKSHOT_BYTES], back[PKSHOT_BYTES + PKSHOT_SIDE];
	uint8_t *png;
	size_t len = 0, at;
	uLongf bn = sizeof back;
	bool crcs = true, inflated = false;
	uint32_t notice[60 * 30];
	const int slot[4] = { 10, 5, 20, 6 };
	int k, i, j;

	for (k = 0; k < (int)(sizeof modes / sizeof modes[0]); k++) {
		ck(shot_mode(modes[k], true), "screenshot: %s, PICO-8's 128 out of the turned panel", modes[k]);
		ck(shot_mode(modes[k], false), "screenshot: %s, out of a landscape one", modes[k]);
	}
	for (j = 0; j < PKSHOT_SIDE; j++)
		for (i = 0; i < PKSHOT_SIDE; i++)
			for (k = 0; k < 3; k++) rgb[((size_t)j * PKSHOT_SIDE + i) * 3 + k] = cell(i, j, k);
	png = pkshot_png(rgb, PKSHOT_SIDE, PKSHOT_SIDE, &len);
	ck(png && len > 8 + 25 + 12 && !memcmp(png, "\x89PNG\r\n\x1a\n", 8) && !memcmp(png + 12, "IHDR", 4) &&
	   be32(png + 16) == PKSHOT_SIDE && be32(png + 20) == PKSHOT_SIDE && png[24] == 8 && png[25] == 2,
	   "screenshot: a PNG, 128 x 128, 8-bit RGB");
	for (at = 8; png && at + 12 <= len;) {
		const uint32_t n = be32(png + at);

		if (at + 12 + n > len || (uint32_t)crc32(0L, png + at + 4, 4 + n) != be32(png + at + 8 + n)) {
			crcs = false;
			break;
		}
		if (!memcmp(png + at + 4, "IDAT", 4))
			inflated = uncompress(back, &bn, png + at + 8, n) == Z_OK && bn == sizeof back;
		at += 12 + n;
	}
	ck(crcs && png && at == len && !memcmp(png + len - 8, "IEND", 4), "screenshot: every chunk's CRC, IEND last");
	for (j = 0; inflated && j < PKSHOT_SIDE; j++)
		if (back[(size_t)j * (PKSHOT_SIDE * 3 + 1)] != 0 ||
		    memcmp(back + (size_t)j * (PKSHOT_SIDE * 3 + 1) + 1, rgb + (size_t)j * PKSHOT_SIDE * 3, PKSHOT_SIDE * 3))
			inflated = false;
	ck(inflated, "screenshot: its pixels, row by row, filter none");
	free(png);

	for (i = 0; i < 60 * 30; i++) notice[i] = 0xd9202020;
	pkshot_lay(notice, 60, 30, slot, rgb);
	{
		const uint32_t mid = notice[(5 + 10) * 60 + 10 + 10];
		const uint8_t *c = rgb + ((size_t)(10 * PKSHOT_SIDE / 20) * PKSHOT_SIDE + 10 * PKSHOT_SIDE / 20) * 3;

		ck(mid == (0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]),
		   "screenshot: the shot laid in the pop-up's square, opaque, its nearest pixel");
		ck(notice[5 * 60 + 10] == 0xd9202020 && notice[4 * 60 + 15] == 0xd9202020 &&
		   notice[(5 + 19) * 60 + 10 + 19] == 0xd9202020,
		   "screenshot: its corners rounded and the rest of the slab left as it was");
	}
}

int main(void)
{
	the_ladder();
	the_chord();
	the_kernel_repeat();
	the_clock();
	the_resync();
	the_menu_tap();
	the_log();
	the_database(false);
	the_database(true);
	the_pending_order();
	the_boost_is_apply();
	the_boost_taps(false);
	the_boost_taps(true);
	the_boost_hold();
	the_boost_bright();
	the_boost_closing();
	the_boost_buzz();
	the_kept_boost_at_start();
	the_display_looks();
	the_hotkeys();
	the_keyboard();
	the_game_pad();
	the_vpad_writes();
	the_screenshot();
	if (fails) {
		printf("pkkeys: %d of %d FAILED\n", fails, checks);
		return 1;
	}
	printf("pkkeys: ok (%d checks)\n", checks);
	return 0;
}
