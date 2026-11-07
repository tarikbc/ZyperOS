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
 * Links tools/pkkeys.c, src/db.c and SQLite, and NOT SDL. */
#include "../src/db.h"
#include "../src/device/pixel2_levels.h"
#include "../src/keyrepeat.h"
#include "pkkeys.h"

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
} rig;

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
		pk_apply(r->db, &r->l, &s[i], line, sizeof line);
		strncat(r->log, line, sizeof r->log - strlen(r->log) - 1);
		if (r->n < 64) {
			r->step[r->n] = s[i];
			r->at[r->n] = r->now;
		}
		r->n++;
	}
}

/* One pass of portkeys' loop at r->now: the events read, in the order read,
 * the pass begun at `cutoff` (µs on the events' clock); then the repeats due. */
static void pass(rig *r, const pk_key *ev, int n, int64_t cutoff)
{
	pk_step s[32];

	take(r, s, pkkeys_pass(&r->k, ev, n, cutoff, r->now, s, 32));
	take(r, s, pkkeys_tick(&r->k, r->now, s, 32));
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
static void chord(rig *r, int vol_code, bool keys_first, int how)
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
				chord(&r, vc, kf, how);
				ck(r.l.bright == want && r.l.vol == 4 && r.n == 1,
				   "chord: menu+%s, %s, %s read first: brightness 7 -> %d and volume 4; got brightness %d, volume %d, %d step(s)",
				   code ? "voldown" : "volup", how_name[how], kf ? "gpio-keys" : "the joypad",
				   want, r.l.bright, r.l.vol, r.n);
				ck(!r.k.menu_held, "chord: menu+%s, %s: MENU let go after", code ? "voldown" : "volup", how_name[how]);
			}

	/* VOL+ alone after a chord is the volume again */
	rig_init(&r, 4, 7);
	r.now = 1000;
	chord(&r, PK_KEY_VOLUMEUP, true, BOTH);
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

static void the_database(void)
{
	char dir[] = "/tmp/zyperos-pkkeys-XXXXXX", path[128], bl[128];
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

	/* The presses of the report that saved 6 and 6: volup, voldown, volup,
	 * menu+volup with gpio-keys read first, menu+voldown woken between */
	r.now = 1000;
	tap(&r, PK_KEY_VOLUMEUP, 80);    run_to(&r, r.now + 150);
	tap(&r, PK_KEY_VOLUMEDOWN, 80);  run_to(&r, r.now + 150);
	tap(&r, PK_KEY_VOLUMEUP, 80);    run_to(&r, r.now + 150);
	chord(&r, PK_KEY_VOLUMEUP, true, BOTH);     run_to(&r, r.now + 150);
	chord(&r, PK_KEY_VOLUMEDOWN, true, BETWEEN);
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

int main(void)
{
	the_chord();
	the_kernel_repeat();
	the_clock();
	the_resync();
	the_log();
	the_database();
	the_pending_order();
	if (fails) {
		printf("pkkeys: %d of %d FAILED\n", fails, checks);
		return 1;
	}
	printf("pkkeys: ok (%d checks)\n", checks);
	return 0;
}
