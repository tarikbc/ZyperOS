/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
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
#include "pkkeys.h"
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
static bool menu_down[2];
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
static void release(bool unused) { (void)unused; }
static void apply_volume(void) {}
static int mock_close(int fd) { (void)fd; closes++; return 0; }
static void press(const pk_step *s)
{
	char line[96];
	if (!steps) first_kind = s->kind;
	steps++;
	repeats += s->held;
	pk_apply(NULL, &levels, s, line, sizeof line);
}
/* The battery's readings, one per call, the last one repeated */
static int batt_seq[4], batt_len, batt_i;
static bool batt_charging;
static bool batt_now(int *pct, bool *charging)
{
	if (!batt_len) return false;
	*pct = batt_seq[batt_i < batt_len ? batt_i : batt_len - 1];
	batt_i++;
	*charging = batt_charging;
	return true;
}
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
	memset(has_menu, 0, sizeof has_menu); memset(has_vol, 0, sizeof has_vol);
	memset(dropped, 0, sizeof dropped); memset(resync, 0, sizeof resync);
	lost_menu = lost_vol = false; ev_clock = CLOCK_MONOTONIC;
	powfd = -1; kills = kill_sig = 0; kill_pid = 0; parent_pid = 4242;
	batt_next = 0; batt_seen = 0; batt_len = batt_i = 0; batt_charging = false;
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
	reset(); batt_len = 1; batt_seq[0] = 50;
	run_loop();
	ck(waits[0] == 5000, "the loop wakes for the battery every 5 s when nothing else wakes it");
	printf("portkeys-flow: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
