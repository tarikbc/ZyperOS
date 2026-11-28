/* SPDX-License-Identifier: GPL-3.0-only */
/* The real reader and loop, extracted at build time, not a model of them.
 * Only OS calls, the clock, and hardware/bar output are replaced. On Linux
 * this uses the real evdev ABI; the host fallback lets make check run on macOS.
 * Each regression observes levels, holds, repeat deadlines or bounded work. */
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include "battempty.h"
#include "device/pixel2_levels.h"
#include "idle.h"
#include "osdbar.h"
#include "pkhot.h"
#include "pkkeys.h"
#include "powerhold.h"
#include "status.h"
#ifdef __linux__
#include <linux/input.h>
#include <sys/ioctl.h>
#else
#define EV_SYN 0
#define EV_KEY 1
#define EV_SW 5
#define SYN_REPORT 0
#define SYN_DROPPED 3
#define SW_HEADPHONE_INSERT 2
#define KEY_VOLUMEUP PK_KEY_VOLUMEUP
#define KEY_POWER 116
#define KEY_VOLUMEDOWN PK_KEY_VOLUMEDOWN
#define BTN_TRIGGER_HAPPY1 PK_BTN_MENU
#define KEY_MAX 0x2ff
#define EVIOCGKEY(n) (n)
#define EVIOCSCLOCKID 0x706b
struct input_event { struct timeval time; uint16_t type, code; int32_t value; };
#endif
#ifndef input_event_sec
#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec
#endif
#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define HAS(bits,b) ((bits[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)
static int jack = -1;
static struct { int fd, pidfd; } G = { -1, -1 };
static int64_t wall_us, mono_ms, clocks[8], times[8];
static int calls, limit, steps, repeats, reads[3], queries, closes, waits[8];
static int ready, fail_read, fail_key_query, fail_clock_reset, fail_clock_read;
static int eof_device, short_device, interrupts, noisy, inject_menu;
static bool menu_down[2], sel_down[2];
static pk_levels levels;
static pk_kind first_kind;
static struct input_event queued[3][4096];
static int qlen[3], qpos[3];
/* The power key's device is id 2 (fd 12); what portkeys signals, and to whom */
static int kills, kill_sig;
static pid_t kill_pid, parent_pid;
static int checks, failures;
static void ck(bool ok, const char *name)
{
	checks++;
	if (!ok) { failures++; fprintf(stderr, "FAIL: %s\n", name); }
}
static void put(int id, int64_t us, int type, int code, int value)
{
	assert(qlen[id] < 4096);
	struct input_event *e = &queued[id][qlen[id]++];
	memset(e, 0, sizeof *e);
	e->input_event_sec = us / 1000000;
	e->input_event_usec = us % 1000000;
	e->type = (uint16_t)type;
	e->code = (uint16_t)code;
	e->value = value;
}
static long now_ms(void) { return (long)mono_ms; }
static int bar_tick(void) { return -1; }
/* Night Mode's hour (night_tick): no clock here, so never due */
static void night_tick(void) {}
static int night_wait(void) { return -1; }
/* The bar's POWER kind while a hold fills it (pw_bar): how often, the last
 * level, and how often it was let go to fade at once */
static int bar_shows, bar_level, bar_fades;
static void bar_show(int kind, int level, int max)
{
	(void)max;
	if (kind == OSDBAR_POWER) { bar_shows++; bar_level = level; }
}
static void bar_fade(void) { bar_fades++; }
static void release(bool unused) { (void)unused; }
/* A plug in or out (plug_changed, outside the extracted code: the output's
 * window and the volume boost), and the boost's clock after each pass's
 * steps (boost_clock): how often each was asked for */
static int plugs, boost_clocks;
static void plug_changed(void) { plugs++; }
static void boost_clock(const pkkeys *k) { (void)k; boost_clocks++; }
static int mock_close(int fd) { (void)fd; closes++; return 0; }
static void press(const pk_step *s)
{
	char line[96];
	if (!steps) first_kind = s->kind;
	steps++;
	repeats += s->held;
	pk_apply(NULL, &levels, s, line, sizeof line);
}
/* The battery's readings, one per call, the last one repeated: the level,
 * and the voltage (3800 mV, a battery far from empty, unless a test says) */
static int batt_seq[4], batt_mv_seq[4], batt_len, batt_i;
static bool batt_charging;
static bool batt_now(int *pct, bool *charging, int *mv)
{
	int i = batt_i < batt_len ? batt_i : batt_len - 1;

	if (!batt_len) return false;
	*pct = batt_seq[i];
	*mv = batt_mv_seq[i] ? batt_mv_seq[i] : 3800;
	batt_i++;
	*charging = batt_charging;
	return true;
}
/* The in-game menu (menu_show, outside the extracted code): how often it was
 * asked for, what it answers, and the power key's file read after it */
static int menu_shows, menu_answer;
static int menu_show(void) { menu_shows++; return menu_answer; }
/* Over your PICO-8 (outside the extracted code): SELECT's hotkeys, judged
 * by the real tools/pkhot.c as the reader reads the joypad, and what they
 * ask for, here only counted; and Auto Off's clock, the real src/idle.c */
static pkhot HOT;
static unsigned hot_pending, hot_done;
static int hot_dos;
static int hot_do(unsigned acts) { hot_dos++; hot_done |= acts; return 0; }
static idle_clock idle_c;
static bool idle_input, idle_charging, idle_fired;
/* MENU's chords over a port (outside the extracted code): the screenshot
 * and the lock, here only counted; locked, the keys go to lock_key */
static struct { bool on, opened; } LK;
static int shots_taken, locks, lock_keys;
static void shot_take(void) { shots_taken++; }
static void lock_start(void) { locks++; LK.on = true; }
static void lock_key(uint16_t code, int32_t value) { (void)code; (void)value; lock_keys++; }
static int lock_tick(long now) { (void)now; return -1; }
/* The pad PICO-8 reads while portkeys holds the joypad (vpad_*, outside the
 * extracted code): the joypad's events handed to it, in order, and the
 * loop's asks */
static bool vpad_held, vpad_home;
static int vpad_wait = -1, vpad_polls, vpad_resyncs, vpad_gone, vpad_events;
static uint16_t vpad_codes[16];
static void vpad_event(uint16_t code, int32_t value, int64_t ms)
{
	(void)value; (void)ms;
	if (vpad_events < 16) vpad_codes[vpad_events] = code;
	vpad_events++;
}
static int vpad_poll(long now, int64_t chord_ms, int pad, clockid_t clk)
{
	(void)now; (void)chord_ms; (void)pad; (void)clk;
	vpad_polls++;
	return -1;
}
static void vpad_resync(void) { vpad_resyncs++; }
static void vpad_pad_gone(void) { vpad_gone++; }
/* Developer SSH over the port (tools/pkssh.c, outside the extracted code):
 * nothing to look at, and the TERM flag never set */
static volatile sig_atomic_t g_term;
static int pkssh_tick(long now) { (void)now; return -1; }
static void pkssh_stop(void) {}
static pid_t mock_getppid(void) { return parent_pid; }
static int mock_kill(pid_t pid, int sig) { kills++; kill_pid = pid; kill_sig = sig; return 0; }
static ssize_t mock_read(int fd, void *out, size_t count)
{
	int id = fd - 10;
	assert(id >= 0 && id < 3);
	reads[id]++;
	if (id == fail_read) { errno = ENODEV; return -1; }
	if (id == eof_device) return 0;
	if (id == short_device) return 1;
	if (interrupts && reads[id] < 200) { errno = EINTR; return -1; }
	if (noisy && reads[id] < 200) {
		struct input_event *e = out;
		for (size_t i = 0; i < count / sizeof *e; i++) {
			memset(e + i, 0, sizeof *e);
			e[i].type = EV_KEY; e[i].code = 304; e[i].value = 2;
		}
		return (ssize_t)count;
	}
	if (inject_menu && id == 1) {
		put(0, 1000001, EV_KEY, PK_BTN_MENU, 1);
		inject_menu = 0;
	}
	int size = qlen[id] - qpos[id], max = (int)(count / sizeof(struct input_event));
	if (!size) { errno = EAGAIN; return -1; }
	if (size > max) size = max;
	memcpy(out, queued[id] + qpos[id], (size_t)size * sizeof(struct input_event));
	qpos[id] += size;
	return (ssize_t)((size_t)size * sizeof(struct input_event));
}
static int mock_ioctl(int fd, unsigned long request, void *data)
{
	int id = fd - 10;
	assert(id >= 0 && id < 2);
	if (request == EVIOCSCLOCKID) {
		assert(*(int *)data == CLOCK_REALTIME);
		if (id == fail_clock_reset) { errno = EINVAL; return -1; }
		return 0;
	}
	assert(request == EVIOCGKEY(NLONGS(KEY_MAX + 1) * sizeof(unsigned long)));
	queries++;
	if (id == fail_key_query) { errno = ENODEV; return -1; }
	unsigned long *bits = data;
	memset(bits, 0, NLONGS(KEY_MAX + 1) * sizeof *bits);
	if (menu_down[id]) bits[PK_BTN_MENU / BITS_PER_LONG] |= 1UL << (PK_BTN_MENU % BITS_PER_LONG);
	if (sel_down[id]) bits[PKHOT_BTN_SELECT / BITS_PER_LONG] |= 1UL << (PKHOT_BTN_SELECT % BITS_PER_LONG);
	/* EVIOCGKEY removes queued EV_KEY events represented by the snapshot. */
	int end = qpos[id];
	for (int i = end; i < qlen[id]; i++)
		if (queued[id][i].type != EV_KEY) queued[id][end++] = queued[id][i];
	qlen[id] = end;
	return 0;
}
static int mock_clock_gettime(clockid_t id, struct timespec *t)
{
	(void)id;
	if (fail_clock_read) { errno = EINVAL; return -1; }
	t->tv_sec = wall_us / 1000000;
	t->tv_nsec = (wall_us % 1000000) * 1000;
	return 0;
}
/* The low-battery disc's readings (lowbat_reading) and On-Screen Stats'
 * vertical blanks (over_poll_fd, over_vblank), outside the extracted code:
 * every reading the loop passed on, and whether the blanks' file was polled
 * and read when it was ready */
static int lowbat_calls, lowbat_pct;
static bool lowbat_charging;
static void lowbat_reading(int pct, bool charging)
{
	lowbat_calls++;
	lowbat_pct = pct;
	lowbat_charging = charging;
}
static int vbl_fd = -1, vbl_reads;
static int over_poll_fd(void) { return vbl_fd; }
static void over_vblank(void) { vbl_reads++; }
static int mock_poll(struct pollfd *, nfds_t, int);
#define read mock_read
#define close mock_close
#define ioctl mock_ioctl
#define clock_gettime mock_clock_gettime
#define poll mock_poll
#define getppid mock_getppid
#define kill mock_kill
#include "portkeys-flow.inc"

static int mock_poll(struct pollfd *f, nfds_t count, int timeout)
{
	assert(calls < 8);
	waits[calls] = timeout;
	if (calls++ >= limit) { n = 0; return -1; }
	wall_us = clocks[calls - 1]; mono_ms = times[calls - 1];
	for (nfds_t i = 0; i < count; i++) f[i].revents = (ready & (1 << i)) ? POLLIN : 0;
	return ready != 0;
}
static void reset(void)
{
	pkkeys_init(&K); n = 2; calls = steps = repeats = queries = closes = 0;
	limit = 1; ready = 3; levels = (pk_levels){4, 7};
	fail_read = fail_key_query = fail_clock_reset = eof_device = short_device = -1;
	fail_clock_read = interrupts = noisy = inject_menu = 0;
	memset(qlen, 0, sizeof qlen); memset(qpos, 0, sizeof qpos);
	memset(reads, 0, sizeof reads); memset(menu_down, 0, sizeof menu_down);
	memset(sel_down, 0, sizeof sel_down);
	pkhot_init(&HOT); hot_pending = hot_done = 0; hot_dos = 0;
	pkkeys_menu_keys(&K, IN_L1, IN_L2); LK.on = LK.opened = false; shots_taken = locks = lock_keys = 0;
	vpad_held = vpad_home = false; vpad_wait = -1; vpad_polls = vpad_resyncs = vpad_gone = vpad_events = 0;
	idle_c = (idle_clock){ 0 }; idle_input = idle_charging = idle_fired = false;
	memset(has_menu, 0, sizeof has_menu); memset(has_vol, 0, sizeof has_vol);
	memset(dropped, 0, sizeof dropped); memset(resync, 0, sizeof resync);
	lost_menu = lost_vol = false; ev_clock = CLOCK_MONOTONIC;
	powfd = -1; kills = kill_sig = 0; kill_pid = 0; parent_pid = 4242;
	batt_next = 0; batt_run = (batt_empty){ 0 }; batt_said = BATT_FINE;
	batt_len = batt_i = 0; batt_charging = false; memset(batt_mv_seq, 0, sizeof batt_mv_seq);
	menu_shows = 0; menu_answer = 1;
	lowbat_calls = lowbat_pct = 0; lowbat_charging = false; vbl_fd = -1; vbl_reads = 0;
	powerhold_set_mode(false); PW = (powerhold){ 0 }; pw_shown = false;
	bar_shows = bar_level = bar_fades = 0;
	plugs = boost_clocks = 0;
	for (int i = 0; i < 8; i++) { clocks[i] = 1000100; times[i] = 1000; waits[i] = -99; }
	wall_us = clocks[0]; mono_ms = times[0];
	fds[0] = (struct pollfd){10, POLLIN, 0}; fds[1] = (struct pollfd){11, POLLIN, 0};
}
static void hold(void)
{
	pk_step s[2]; pk_key e = {0, PK_KEY_VOLUMEUP, 1};
	int m = pkkeys_pass(&K, &e, 1, 0, 0, s, 2);
	for (int i = 0; i < m; i++) press(&s[i]);
}
int main(void)
{
	for (int pad = 0; pad < 2; pad++) {
		int vol = 1 - pad;
		reset(); ready = 1 << vol; has_menu[pad] = has_vol[vol] = true;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(vol, 1000005, EV_KEY, PK_KEY_VOLUMEUP, 1);
		run_loop();
		ck(levels.vol == 4 && levels.bright == 8 && first_kind == PK_BRIGHT,
		   "poll misses MENU readiness: chord in either device order");

		reset(); has_menu[pad] = has_vol[vol] = true;
		put(vol, 1000005, EV_KEY, PK_KEY_VOLUMEUP, 1);
		for (int j = 0; j < 512; j++) put(vol, 1000006 + j, EV_KEY, j % 2 ? 304 : PK_KEY_VOLUMEUP, 2);
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		run_loop();
		ck(levels.vol == 4 && levels.bright == 8 && steps == 1,
		   "over 256 irrelevant/repeat events cannot split the chord");

		reset(); limit = 2; has_menu[pad] = has_vol[vol] = true;
		for (int j = 0; j < 65; j++) put(vol, 1001000 + j, EV_KEY, PK_KEY_VOLUMEUP, !(j % 2));
		put(pad, 1000999, EV_KEY, PK_BTN_MENU, 1); menu_down[pad] = true;
		run_loop();
		ck(steps == 0 && K.nwaiting == 0 && !K.key[0].down && K.menu_held,
		   "over 64 pending keys discard the whole batch and resync MENU");

		reset(); has_menu[pad] = has_vol[vol] = true;
		for (int j = 0; j < 257; j++) put(vol, 900005 + j, EV_KEY, PK_KEY_VOLUMEUP, !(j % 2));
		put(pad, 900000, EV_KEY, PK_BTN_MENU, 1);
		run_loop();
		ck(steps == 0 && !K.key[0].down && K.nwaiting == 0,
		   "over 256 relevant keys discard the whole batch in both device orders");
	}
	reset(); n = 1; has_vol[0] = true; clocks[0] = 299900; times[0] = 300; hold();
	put(0, 283000, EV_KEY, PK_KEY_VOLUMEUP, 2);
	put(0, 299950, EV_KEY, PK_KEY_VOLUMEUP, 0);
	run_loop();
	ck(levels.vol == 5 && repeats == 0, "pending release at 299.950 ms blocks the 300 ms repeat");

	reset(); limit = 3; n = 1; has_vol[0] = true; hold();
	for (int i = 0; i < 8; i++) { clocks[i] = 100; times[i] = 1000; }
	put(0, 9000000000LL, EV_KEY, PK_BTN_MENU, 1);
	run_loop();
	ck(K.nwaiting == 0 && repeats == 1 && waits[1] == 0 && waits[2] == 90,
	   "future wall timestamp resolves once with no spin or catchup burst");

	reset(); limit = 2; has_menu[0] = has_vol[1] = true; inject_menu = 1;
	clocks[0] = 1000000; clocks[1] = 1000100;
	put(1, 1000005, EV_KEY, PK_KEY_VOLUMEUP, 1);
	run_loop();
	ck(levels.vol == 4 && levels.bright == 8, "MENU arrives after its first read: next pass joins the chord");

	reset(); limit = 2; n = 1; has_vol[0] = true;
	put(0, 1001000, EV_KEY, PK_KEY_VOLUMEUP, 1);
	for (int j = 0; j < 80; j++) put(0, 1001001 + j, EV_KEY, 304, j % 2);
	put(0, 1001100, EV_KEY, PK_KEY_VOLUMEUP, 0);
	run_loop();
	ck(steps == 1 && !K.key[0].down && K.nwaiting == 0, "irrelevant keys do not exhaust the pending queue");

	reset(); n = 1; has_vol[0] = true; hold();
	put(0, 900000, EV_SYN, SYN_DROPPED, 0);
	put(0, 900001, EV_KEY, PK_KEY_VOLUMEUP, 0);
	put(0, 900002, EV_SYN, SYN_REPORT, 0);
	run_loop();
	ck(!K.key[0].down && repeats == 0 && queries == 1, "SYN_DROPPED release clears the hold");

	for (int error = 0; error < 3; error++) {
		reset(); has_vol[0] = true; hold();
		if (error == 0) fail_read = 0;
		if (error == 1) eof_device = 0;
		if (error == 2) short_device = 0;
		run_loop();
		ck(!K.key[0].down && repeats == 0 && closes == 1, "read error, EOF or malformed read removes input safely");
	}
	reset(); n = 1; has_vol[0] = true; hold(); fail_key_query = 0;
	put(0, 900000, EV_SYN, SYN_DROPPED, 0); put(0, 900001, EV_SYN, SYN_REPORT, 0);
	run_loop();
	ck(!K.key[0].down && repeats == 0, "failed resync cannot leave a repeat armed");

	for (int mode = 0; mode < 2; mode++) {
		reset(); has_vol[0] = true; hold(); noisy = mode == 0; interrupts = mode == 1;
		run_loop();
		ck(reads[0] <= 32 && reads[1] <= 32 && repeats == 0 && !K.key[0].down,
		   "constant noise or EINTR has bounded work and cancels holds");
	}
	reset();
	ck(event_clock_fallback(EINVAL) && ev_clock == CLOCK_REALTIME,
	   "fallback accepts realtime only after every reset succeeds");
	for (int id = 0; id < 2; id++) {
		reset(); fail_clock_reset = id;
		ck(!event_clock_fallback(EINVAL) && ev_clock == CLOCK_MONOTONIC && steps == 0,
		   "failed clock reset rejects mixed timestamp clocks");
	}
	reset(); fail_clock_read = 1;
	put(0, 900000, EV_KEY, PK_KEY_VOLUMEUP, 1);
	ck(run_loop() == 1 && steps == 0, "failed cutoff clock read stops before applying any keys");
	/* POWER over a port, on its own file (fd 12, after the two devices):
	 * the press goes to launch.sh, the parent, as USR1, once, and neither its
	 * release nor a repeat is a second press or a volume step; with the
	 * parent gone (init's child) nothing is sent, since USR1 halts init */
	reset(); has_vol[0] = has_menu[1] = true; powfd = 12; ready = 1 << 2;
	put(2, 1000000, EV_KEY, KEY_POWER, 1); put(2, 1000001, EV_SYN, SYN_REPORT, 0);
	put(2, 1000002, EV_KEY, KEY_POWER, 2); put(2, 1000003, EV_KEY, KEY_POWER, 0);
	run_loop();
	ck(kills == 1 && kill_pid == 4242 && kill_sig == SIGUSR1 && steps == 0 && powfd == 12,
	   "POWER over a port is one USR1 to launch.sh");
	reset(); powfd = 12; ready = 1 << 2; parent_pid = 1;
	put(2, 1000000, EV_KEY, KEY_POWER, 1);
	run_loop();
	ck(kills == 0, "POWER never signals init");
	reset(); powfd = 12; ready = 1 << 2; eof_device = 2;
	run_loop();
	ck(kills == 0 && powfd == -1 && closes == 1, "a power key gone is closed, not read again");
	/* MENU > Power Button on Hold (src/powerhold.h): the press is not the
	 * turn-off; the hold is, POWERHOLD_MS after it, once, with the loop woken
	 * for the bar's next segment and the bar showing the hold; let go early,
	 * nothing ends */
	reset(); has_vol[0] = has_menu[1] = true; powfd = 12; ready = 1 << 2; limit = 3;
	powerhold_set_mode(true);
	times[0] = 1000; times[1] = 1000 + POWERHOLD_MS / 2; times[2] = 1000 + POWERHOLD_MS + 50;
	put(2, 1000000, EV_KEY, KEY_POWER, 1);
	run_loop();
	ck(kills == 1 && kill_sig == SIGUSR1 && kill_pid == 4242,
	   "Hold: POWER held for its time is one USR1 to launch.sh");
	ck(waits[1] > 0 && waits[1] <= (int)(POWERHOLD_MS / OSDBAR_POWER_STEPS),
	   "Hold: the loop wakes for the bar's next segment while POWER is held");
	ck(bar_shows >= 2 && bar_level == OSDBAR_POWER_STEPS, "Hold: the bar fills, and ends full");
	reset(); has_vol[0] = has_menu[1] = true; powfd = 12; ready = 1 << 2; limit = 2;
	powerhold_set_mode(true);
	times[0] = 1000; times[1] = 1000 + POWERHOLD_MS + 50;
	put(2, 1000000, EV_KEY, KEY_POWER, 1); put(2, 1000300, EV_KEY, KEY_POWER, 0);
	run_loop();
	ck(kills == 0, "Hold: a press let go early ends nothing");
	/* The battery run flat over a port: two readings at BATT_EMPTY_PCT, 5 s
	 * apart, off the charger, are one USR1, and a third is not a second; on
	 * the charger, a point above, or one stray reading, nothing */
	for (int c = 0; c < 4; c++) {
		reset(); has_vol[0] = has_menu[1] = true; limit = 3;
		times[0] = 6000; times[1] = 11000; times[2] = 16000;
		batt_len = 1; batt_seq[0] = BATT_EMPTY_PCT;
		if (c == 1) batt_charging = true;
		if (c == 2) batt_seq[0] = BATT_EMPTY_PCT + 1;
		if (c == 3) { batt_len = 2; batt_seq[1] = 50; }
		run_loop();
		ck(c == 0 ? kills == 1 && kill_pid == 4242 && kill_sig == SIGUSR1 && batt_i == 4 : kills == 0,
		   c == 0 ? "an empty battery over a port is one USR1 to launch.sh" :
		   c == 1 ? "on the charger, an empty reading powers nothing off" :
		   c == 2 ? "a point above empty powers nothing off" :
		            "one stray empty reading powers nothing off");
	}
	/* And by the voltage, whatever the level says (src/battempty.h): three
	 * readings in a row under the Pixel 2's empty voltage at 50%, off the
	 * charger, are one USR1; two and then one at the voltage itself, or on
	 * the charger, nothing */
	for (int c = 0; c < 3; c++) {
		reset(); has_vol[0] = has_menu[1] = true; limit = 3;
		times[0] = 6000; times[1] = 11000; times[2] = 16000;
		batt_len = 1; batt_seq[0] = 50; batt_mv_seq[0] = PX2_BATT_EMPTY_MV - 1;
		if (c == 1) {
			batt_len = 3; batt_seq[1] = batt_seq[2] = 50;
			batt_mv_seq[1] = PX2_BATT_EMPTY_MV - 1; batt_mv_seq[2] = PX2_BATT_EMPTY_MV;
		}
		if (c == 2) batt_charging = true;
		run_loop();
		ck(c == 0 ? kills == 1 && kill_pid == 4242 && kill_sig == SIGUSR1 && batt_i >= 3 : kills == 0,
		   c == 0 ? "three readings under the empty voltage at 50% over a port are one USR1 to launch.sh" :
		   c == 1 ? "two readings under the empty voltage and one at it power nothing off" :
		            "on the charger, a voltage under empty powers nothing off");
	}
	reset(); batt_len = 1; batt_seq[0] = 50;
	run_loop();
	ck(waits[0] == 5000, "the loop wakes for the battery every 5 s when nothing else wakes it");
	/* The low-battery disc over your PICO-8: every reading goes to it, the
	 * charger with it, once a look, low or not (lowbat_reading) */
	reset(); has_vol[0] = has_menu[1] = true; limit = 3;
	times[0] = 6000; times[1] = 7000; times[2] = 11000;
	batt_len = 2; batt_seq[0] = 8; batt_seq[1] = 4; batt_charging = true;
	run_loop();
	ck(lowbat_calls == 3 && lowbat_pct == 4 && lowbat_charging && kills == 0,
	   "every 5 s look at the battery is a reading for the disc, the charger with it");
	reset(); batt_len = 0;
	run_loop();
	ck(lowbat_calls == 0, "and no reading, no disc's");
	/* On-Screen Stats' blanks: the file polled with the keys, and read when
	 * it is ready, not before */
	reset(); vbl_fd = 20; ready = 1 << 2;
	run_loop();
	ck(vbl_reads == 1, "a vertical blank ready is read (over_vblank)");
	reset(); vbl_fd = 20; ready = 1;
	run_loop();
	ck(vbl_reads == 0, "and a pass with none ready reads none");
	/* MENU on its own asks for the in-game menu once, in the pass that reads
	 * its release; MENU+VOL+ (the backlight) does not. After a menu that was
	 * up, the power key's file is drained unread, so a POWER pressed in the
	 * menu (the launcher's) does not end the port a second time, and the keys
	 * are queried again (resync) */
	for (int pad = 0; pad < 2; pad++) {
		int vol = 1 - pad;

		reset(); has_menu[pad] = has_vol[vol] = true; powfd = 12; ready = 1 << pad;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		put(2, 1000060, EV_KEY, KEY_POWER, 1);
		run_loop();
		ck(menu_shows == 1 && steps == 0 && kills == 0 && queries == 2 && qpos[2] == qlen[2],
		   "MENU alone: the in-game menu once, then the power key drained and the keys resynced");

		reset(); has_menu[pad] = has_vol[vol] = true; ready = 1 << pad;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(vol, 1000005, EV_KEY, PK_KEY_VOLUMEUP, 1);
		put(vol, 1000040, EV_KEY, PK_KEY_VOLUMEUP, 0);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		run_loop();
		ck(menu_shows == 0 && levels.bright == 8, "MENU+VOL+: the backlight, and no menu");

		reset(); has_menu[pad] = has_vol[vol] = true; ready = 1 << pad; menu_answer = 0;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		run_loop();
		ck(menu_shows == 1 && queries == 0, "no menu over this port: nothing resynced");

		/* MENU's chords, by the Pixel 2's table: MENU+L1 the screenshot
		 * and MENU+L2 the lock, MENU then opening no menu; locked, every
		 * key is the lock's */
		reset(); has_menu[pad] = has_vol[vol] = true; ready = 1 << pad;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(pad, 1000010, EV_KEY, 0x136, 1);
		put(pad, 1000040, EV_KEY, 0x136, 0);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		run_loop();
		ck(shots_taken == 1 && locks == 0 && menu_shows == 0, "MENU+L1: the screenshot, and no menu");

		reset(); has_menu[pad] = has_vol[vol] = true; ready = 1 << pad;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(pad, 1000010, EV_KEY, 0x138, 1);
		put(pad, 1000040, EV_KEY, 0x138, 0);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		run_loop();
		ck(locks == 1 && shots_taken == 0 && menu_shows == 0, "MENU+L2: the lock, and no menu");

		reset(); has_menu[pad] = has_vol[vol] = true; ready = 1 << pad; LK.on = true;
		put(pad, 1000000, EV_KEY, PK_BTN_MENU, 1);
		put(pad, 1000050, EV_KEY, PK_BTN_MENU, 0);
		put(vol, 1000060, EV_KEY, PK_KEY_VOLUMEUP, 1);
		run_loop();
		ck(lock_keys == 3 && menu_shows == 0 && steps == 0, "locked: every key the lock's, no menu, no step");
	}
	/* A plug in or out is the jack's switch, read with the keys: one change
	 * of output for the pass that read it, and the boost's clock runs after
	 * every pass, a pass with nothing read too */
	reset(); n = 1; has_vol[0] = true; limit = 2;
	put(0, 1000000, EV_SW, SW_HEADPHONE_INSERT, 1);
	put(0, 1000001, EV_SYN, SYN_REPORT, 0);
	run_loop();
	ck(plugs == 1 && steps == 0, "a plug in is one change of output, and no step");
	ck(boost_clocks == 2, "the boost's clock runs after every pass");
	reset(); n = 1; has_vol[0] = true;
	put(0, 1000000, EV_KEY, PK_KEY_VOLUMEUP, 1);
	run_loop();
	ck(plugs == 0 && steps == 1 && boost_clocks == 1, "a key is no change of output");
	/* SELECT's hotkeys over your PICO-8: the joypad's events, read with the
	 * rest, go through Diatom's chord rule in the order they came, and what
	 * they ask for is done once after the pass; never from another device,
	 * never with no table, never for SELECT that went with the d-pad first,
	 * and never for a SELECT a resync found down (its press was not seen) */
	for (int c = 0; c < 6; c++) {
		reset(); has_menu[0] = has_vol[1] = true; ready = 1;
		if (c != 2) pkhot_set(&HOT, "save:r1,load:l1,faster:r2,slower:l2,shot:y,quit:x");
		const int dev = c == 1 ? 1 : 0;
		if (c == 4) put(dev, 1000000, EV_SYN, SYN_DROPPED, 0), put(dev, 1000001, EV_SYN, SYN_REPORT, 0);
		if (c == 4) sel_down[dev] = true, limit = 2;
		if (c != 4) put(dev, 1000010, EV_KEY, PKHOT_BTN_SELECT, 1);
		if (c == 3) put(dev, 1000020, EV_KEY, PKHOT_BTN_UP, 1);
		put(dev, 1000050, EV_KEY, c == 5 ? PKHOT_BTN_TR : PKHOT_BTN_WEST, 1);
		run_loop();
		ck(c == 0 ? hot_dos == 1 && hot_done == (1u << CHORD_SHOT) && steps == 0 : hot_dos == 0,
		   c == 0 ? "SELECT+Y over your PICO-8: the screenshot, once, after the pass" :
		   c == 1 ? "SELECT and Y from a device that is not the joypad: nothing" :
		   c == 2 ? "no hotkeys handed over: nothing" :
		   c == 3 ? "SELECT with the d-pad first is the game's: nothing" :
		   c == 4 ? "a SELECT found down at a resync is no chord's" :
		            "SELECT+R1, Quick Save, is no act over your PICO-8: nothing");
	}
	reset(); has_menu[0] = true; n = 1; ready = 1;
	pkhot_set(&HOT, "quit:x");
	put(0, 1000000, EV_KEY, PKHOT_BTN_NORTH, 1);
	put(0, 1000060, EV_KEY, PKHOT_BTN_SELECT, 1);
	run_loop();
	ck(hot_done == (1u << CHORD_QUIT), "X a moment before SELECT is the same chord: Quit");
	/* Auto Off over your PICO-8 (src/idle.h): its time with no key, the
	 * looks under IDLE_GAP_MS apart, is one USR1 to launch.sh, as POWER;
	 * the charger holds it, a longer gap (the in-game menu was up) starts it
	 * again, and with Auto Off at Never the loop is not woken for it */
	for (int c = 0; c < 4; c++) {
		reset(); n = 1; has_vol[0] = true; limit = 6; ready = 0;
		idle_c.seconds = c == 3 ? 0 : 2;
		for (int i = 0; i < 6; i++) times[i] = 1000 + 800 * i;
		if (c == 1) { batt_len = 1; batt_seq[0] = 50; batt_charging = true; }
		/* 2.4 s after its first look without the gap, 1.6 s after the gap */
		if (c == 2) { limit = 4; for (int i = 1; i < 6; i++) times[i] += 1500; }
		run_loop();
		ck(c == 0 ? kills == 1 && kill_sig == SIGUSR1 && kill_pid == 4242 && idle_fired : kills == 0,
		   c == 0 ? "Auto Off: no key for its time is one USR1 to launch.sh" :
		   c == 1 ? "Auto Off: the charger holds the clock" :
		   c == 2 ? "Auto Off: a gap past IDLE_GAP_MS starts the clock again" :
		            "Auto Off at Never ends nothing");
		if (c == 0) ck(waits[0] > 0 && waits[0] <= (int)IDLE_GAP_MS - 100,
		               "Auto Off: the loop looks well inside its gap while it counts");
		if (c == 3) ck(waits[0] == 5000, "Auto Off at Never: the loop is not woken for it");
	}
	/* The pad PICO-8 reads: every joypad event handed to it in the order
	 * read, none from another device; a pass asks vpad_poll each time; a
	 * resync of the joypad resyncs it, the joypad gone tells it; and while
	 * MENU+START has sent the port home, a MENU let go opens no menu */
	reset(); has_menu[0] = has_vol[1] = true; ready = 3;
	put(0, 1000000, EV_KEY, PKHOT_BTN_SELECT, 1); put(0, 1000010, EV_KEY, PKHOT_BTN_SOUTH, 1);
	put(0, 1000020, EV_KEY, PKHOT_BTN_SOUTH, 0); put(1, 1000005, EV_KEY, PK_KEY_VOLUMEUP, 1);
	run_loop();
	ck(vpad_events == 3 && vpad_codes[0] == PKHOT_BTN_SELECT && vpad_codes[1] == PKHOT_BTN_SOUTH &&
	   vpad_codes[2] == PKHOT_BTN_SOUTH, "the joypad's events reach the pad PICO-8 reads, in order, and only its");
	ck(vpad_polls == 1, "each pass asks the pad PICO-8 reads for its turn");
	reset(); n = 1; has_menu[0] = true; limit = 2;
	put(0, 900000, EV_SYN, SYN_DROPPED, 0); put(0, 900001, EV_SYN, SYN_REPORT, 0);
	run_loop();
	ck(vpad_resyncs >= 1, "a resync of the joypad resyncs the pad PICO-8 reads");
	reset(); has_menu[0] = has_vol[1] = true; eof_device = 0;
	run_loop();
	ck(vpad_gone == 1, "the joypad gone, the pad PICO-8 reads is told");
	reset(); has_menu[0] = has_vol[1] = true; ready = 1; vpad_home = true;
	put(0, 1000000, EV_KEY, PK_BTN_MENU, 1); put(0, 1000050, EV_KEY, PK_BTN_MENU, 0);
	run_loop();
	ck(menu_shows == 0, "after MENU+START sent the port home, MENU let go opens no menu");
	printf("portkeys-flow: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
