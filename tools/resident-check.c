/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's reader of Diatom's answers (src/resident.c), the device
 * stubbed, against a made-up Diatom on a socket of its own: a forked child
 * that says READY and answers each command it is sent with lines it was
 * given. What it holds: plat_resident_sync_line, which the in-game menu's
 * Player 2 page waits on for SETPLAYER2's answer, takes the line it asked
 * for and notes the picture on the way; and a game that ended meanwhile (an
 * EXIT before the answer: px2-open's quit, a crash) is not eaten, but left
 * for the wait loop, whose plat_resident_wait then says the game is over.
 * A crash's EXIT lets the socket go, so the launcher starts a new resident.
 * And the pid file a resident the launcher started again is written to
 * (plat_resident_note_pid). No SDL window: SDL_GetTicks only. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../src/device.h"
#include "../src/osdbar.h"
#include "../src/platform.h"

static int failures;

#define CHECK(c, what) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, what); failures++; } \
                            else printf("  ok   %s\n", what); } while (0)

/* ---- the device, stubbed: resident.c's other neighbours ------------------ */

void device_display_release(void) {}
void device_display_take(void) {}
unsigned device_game_wait(unsigned ms) { return ms; }
void device_levels_forget(void) {}
bool device_power_pressed(void) { return false; }
/* POWER's hold (src/powerhold.h): none under way */
int plat_power_hold(void) { return -1; }
int  plat_brightness_get(void) { return -1; }
void plat_brightness_set(int level) { (void)level; }
int  plat_coreopt_count(const char *tag) { (void)tag; return 0; }
const char *plat_coreopt(const char *tag, int i) { (void)tag; (void)i; return NULL; }
bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }
void plat_set_power_pressed(bool on) { (void)on; }
void plat_side_lights_say(const char *line) { (void)line; }
bool plat_terminating(void) { return false; }
const char *plat_turbo_map(const char *tag) { (void)tag; return NULL; }
int  plat_volume_get(void) { return -1; }
void plat_volume_set_pct(int pct) { (void)pct; }
/* The boost past the top (src/levels.c): the volume's cells the launcher
 * keeps, which a pause or an end takes back from the game, and each step of
 * a running game's said to MP4 (levels_game_boost), in the order it came,
 * with how many had come when the pause or the end took the levels back */
static int kept_vol_boost;
static int game_boosts[32], n_game_boosts, boosts_at_take = -1;
int  levels_boost(int kind) { return kind == OSDBAR_VOLUME ? kept_vol_boost : 0; }
void levels_from_game(int kind, int level, int boost)
{
	(void)level;
	if (kind != OSDBAR_VOLUME) return;
	kept_vol_boost = boost;
	if (boosts_at_take < 0) boosts_at_take = n_game_boosts;
}
void levels_game_boost(int kind, int boost)
{
	if (kind == OSDBAR_VOLUME && n_game_boosts < (int)(sizeof game_boosts / sizeof game_boosts[0]))
		game_boosts[n_game_boosts++] = boost;
}

static bool boosts_were(const int *want, int n)
{
	if (n_game_boosts != n) return false;
	for (int i = 0; i < n; i++)
		if (game_boosts[i] != want[i]) return false;
	return true;
}
int  side_lights_level_line(char *out, size_t cap, bool volume, int val, int max)
{
	(void)volume; (void)val; (void)max;
	if (cap) out[0] = '\0';
	return 0;
}

unsigned plat_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* What the on_stick callback heard: R2's turns, not the answers */
static int stick_calls;
static bool stick_last;

static void heard_stick(bool on)
{
	stick_calls++;
	stick_last = on;
}

/* What the on_hotkey callback heard: a shot taken (path NULL, its shutter)
 * and a shot written (its path), in the order they came */
static char hotkeys_heard[256];

static void heard_hotkey(const char *key, const char *path)
{
	size_t n = strlen(hotkeys_heard);

	snprintf(hotkeys_heard + n, sizeof hotkeys_heard - n, "%s%s:%s", n ? " " : "", key,
	         path ? "file" : "taken");
}

/* ---- a Diatom of its own ------------------------------------------------- */

/* What it says to the Nth command line it reads, after READY */
static const char *const ANSWERS[] = {
	/* SETPLAYER2 on=1: a picture first, then the answer */
	"PREVIEW\tpath=/tmp/resident-check.png\nPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2\n",
	/* SETPLAYER2 on=1 again: the game ended first */
	"EXIT\treason=user\n",
	/* the third: nothing at all */
	"",
	/* SETSTICK from the Controls tab: its answer; then RESUME, and R2 turns
	 * the d-pad back in the game before MENU pauses it again */
	"STICK\ton=1\n",
	"STICK\ton=0\nPAUSED\n",
	/* SETSTICK again, its answer taken by a paused reader; then RESUME and
	 * R2's turn */
	"STICK\ton=0\n",
	"STICK\ton=1\nPAUSED\n",
	/* RESUME, and the core crashes as the game ends: the EXIT comes from
	 * the signal handler, and the socket stays open while it finishes */
	"EXIT\treason=crash\tsignal=SIGSEGV\n",
};

static void fake_diatom(int listener)
{
	char buf[512];
	size_t used = 0;
	int c = accept(listener, NULL, NULL), k = 0;

	if (c < 0) _exit(1);
	if (write(c, "READY\tproto=14\tstate=idle\n", 26) != 26) _exit(1);
	for (;;) {
		ssize_t n = read(c, buf + used, sizeof buf - used - 1);
		char *nl;

		if (n <= 0) _exit(0);
		used += (size_t)n;
		while ((nl = memchr(buf, '\n', used))) {
			size_t len = (size_t)(nl - buf) + 1;

			if (k < (int)(sizeof ANSWERS / sizeof ANSWERS[0])) {
				const char *a = ANSWERS[k++];

				if (*a && write(c, a, strlen(a)) != (ssize_t)strlen(a)) _exit(1);
			}
			memmove(buf, buf + len, used - len);
			used -= len;
		}
	}
}

/* A Diatom with a game going on, MP4's music behind it: what it says at
 * each RESUME it is sent, its LEVEL lines as its frames go (diatom/src/main.c,
 * levels_tick: said again in its first frames, then at each change) */
#define VOL_LEVEL(b) "LEVEL\tkind=volume\tindex=20\tcount=21\tboost=" #b "\n"
static const char *const GAME_ANSWERS[] = {
	/* the launcher keeps none: its levels said again, then VOL+ held a
	 * second on the top, a press, VOL- twice, and three presses, then MENU */
	VOL_LEVEL(0) "LEVEL\tkind=brightness\tindex=11\tcount=12\tboost=0\n"
	VOL_LEVEL(1) VOL_LEVEL(2) VOL_LEVEL(1) VOL_LEVEL(0) VOL_LEVEL(1) "PAUSED\n",
	/* the menu filled it to three: the first frame's line is from before
	 * its SETLEVEL, with the one it had; then SETLEVEL's, a press, the end */
	VOL_LEVEL(1) VOL_LEVEL(3) VOL_LEVEL(4) "EXIT\treason=user\n",
	/* two screenshots: each said as it is taken (its shutter), the first's
	 * file once written, between them; then MENU */
	"HOTKEY\tkey=shot\nSHOT\tpath=/tmp/Screenshots/SNES/Game 2026-11-20 21.40.05.png\n"
	"HOTKEY\tkey=shot\nPAUSED\n",
};

static void fake_game(int listener)
{
	char buf[512];
	size_t used = 0;
	int c = accept(listener, NULL, NULL), k = 0;

	if (c < 0) _exit(1);
	if (write(c, "READY\tproto=14\tstate=idle\n", 26) != 26) _exit(1);
	for (;;) {
		ssize_t n = read(c, buf + used, sizeof buf - used - 1);
		char *nl;

		if (n <= 0) _exit(0);
		used += (size_t)n;
		while ((nl = memchr(buf, '\n', used))) {
			size_t len = (size_t)(nl - buf) + 1;

			if (!strncmp(buf, "RESUME", 6) && k < (int)(sizeof GAME_ANSWERS / sizeof GAME_ANSWERS[0])) {
				const char *a = GAME_ANSWERS[k++];

				if (write(c, a, strlen(a)) != (ssize_t)strlen(a)) _exit(1);
			}
			memmove(buf, buf + len, used - len);
			used -= len;
		}
	}
}

/* A Diatom whose core would not load: it answers a RUN with the ERROR its
 * real one sends (diatom/src/corefail.h), and any other line with nothing */
static void fake_refusing(int listener, const char *error)
{
	char buf[1024];
	size_t used = 0;
	int c = accept(listener, NULL, NULL);

	if (c < 0) _exit(1);
	if (write(c, "READY\tproto=14\tstate=idle\n", 26) != 26) _exit(1);
	for (;;) {
		ssize_t n = read(c, buf + used, sizeof buf - used - 1);
		char *nl;

		if (n <= 0) _exit(0);
		used += (size_t)n;
		while ((nl = memchr(buf, '\n', used))) {
			size_t len = (size_t)(nl - buf) + 1;

			if (!strncmp(buf, "RUN\t", 4) && write(c, error, strlen(error)) != (ssize_t)strlen(error)) _exit(1);
			memmove(buf, buf + len, used - len);
			used -= len;
		}
	}
}

/* A game sent to a Diatom that answers with `error`, and what the launcher read: mode 0 waits
 * and takes the reason, 1 waits and leaves it, 2 only sends and takes */
static int refused_by(const char *error, plat_start_error *e, bool *got, int mode)
{
	char path[64];
	struct sockaddr_un a;
	int listener, r;
	pid_t pid;
	plat_game_run run;

	snprintf(path, sizeof path, "/tmp/zyperos-resident-check-refusing-%d.sock", (int)getpid());
	unlink(path);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 1) != 0) {
		printf("FAIL: no socket at %s\n", path);
		failures++;
		return -1;
	}
	if ((pid = fork()) == 0) fake_refusing(listener, error);
	close(listener);
	setenv("ZYPEROS_DIATOM_SOCKET", path, 1);
	memset(&run, 0, sizeof run);
	run.tag = "SNES";
	run.core = "/mnt/SDCARD/ZyperOS/cores/bsnes_libretro.so";
	run.rom = "/mnt/SDCARD/Roms/SNES/Game.sfc";
	if (!plat_resident_send(&run)) { printf("FAIL: the game was not sent\n"); failures++; r = -1; }
	else r = mode == 2 ? -2 : plat_resident_wait();
	*got = mode == 1 ? false : plat_resident_start_error(e);
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	unlink(path);
	return r;
}

int main(void)
{
	char path[64], out[256];
	struct sockaddr_un a;
	int listener;
	pid_t pid;
	unsigned t0;

	snprintf(path, sizeof path, "/tmp/zyperos-resident-check-%d.sock", (int)getpid());
	unlink(path);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 1) != 0) {
		printf("FAIL: no socket at %s\n", path);
		return 1;
	}
	if ((pid = fork()) == 0) fake_diatom(listener);
	close(listener);
	setenv("ZYPEROS_DIATOM_SOCKET", path, 1);

	printf("Diatom's answer to SETPLAYER2, read while paused:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the line goes");
	CHECK(plat_resident_sync_line("PLAYER2\t", out, sizeof out, 1000) &&
	      !strcmp(out, "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the answer is the line asked for");
	CHECK(!strcmp(plat_resident_last_preview(), "/tmp/resident-check.png"),
	      "and the picture on the way is noted, as the other readers note it");

	printf("a game that ended before the answer:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the line goes");
	t0 = plat_now_ms();
	CHECK(!plat_resident_sync_line("PLAYER2\t", out, sizeof out, 1000), "no answer is taken past its EXIT");
	CHECK(plat_now_ms() - t0 < 500, "and it does not wait out its time for one");
	CHECK(plat_resident_wait() == RES_EXIT, "the EXIT is left for the wait loop, which ends the game");

	printf("nothing at all:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=0"), "the line goes");
	t0 = plat_now_ms();
	CHECK(!plat_resident_sync_line("PLAYER2\t", out, sizeof out, 150), "no answer");
	CHECK(plat_now_ms() - t0 >= 140, "after the time it was given");

	printf("the Controls tab's SETSTICK, and R2's turn in the game:\n");
	plat_resident_on_stick(heard_stick);
	CHECK(plat_resident_set_stick("toggle-stick"), "the line goes");
	CHECK(plat_resident_line("RESUME"), "and RESUME");
	CHECK(plat_resident_wait() == RES_PAUSED, "the game is paused again");
	CHECK(stick_calls == 1 && !stick_last,
	      "the answer reached no callback (no pop-up); R2's turn back did");
	CHECK(plat_resident_set_stick("toggle"), "SETSTICK again");
	CHECK(!plat_resident_sync_line("NOPE\t", out, sizeof out, 200),
	      "a paused reader takes its answer on the way");
	CHECK(plat_resident_line("RESUME"), "RESUME");
	CHECK(plat_resident_wait() == RES_PAUSED, "paused again");
	CHECK(stick_calls == 2 && stick_last, "and still R2's turn alone is heard");
	plat_resident_on_stick(NULL);

	printf("a game whose emulator crashed on the way out:\n");
	CHECK(plat_resident_line("RESUME"), "RESUME");
	CHECK(plat_resident_wait() == RES_EXIT, "its EXIT ends the game");
	CHECK(plat_resident_crashed(), "and says it crashed");
	CHECK(!plat_resident_ready(),
	      "its socket, still open, is not a resident that answers (respawn_resident starts one)");

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	unlink(path);

	/* MP4's music behind a running game: each step of the game's volume
	 * boost reaches MP4 as its line is read, not at the pause or the end;
	 * a line from before Diatom took the launcher's cells never does */
	printf("MP4 behind a running game, its volume boost:\n");
	snprintf(path, sizeof path, "/tmp/zyperos-resident-check-game-%d.sock", (int)getpid());
	unlink(path);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 1) != 0) {
		printf("FAIL: no socket at %s\n", path);
		return 1;
	}
	if ((pid = fork()) == 0) fake_game(listener);
	close(listener);
	setenv("ZYPEROS_DIATOM_SOCKET", path, 1);
	{
		static const int first[] = { 0, 1, 2, 1, 0, 1 }, second[] = { 3, 4 };

		kept_vol_boost = 0;
		n_game_boosts = 0;
		boosts_at_take = -1;
		CHECK(plat_resident_line("RESUME"), "RESUME");
		CHECK(plat_resident_wait() == RES_PAUSED, "the game plays, then MENU pauses it");
		CHECK(boosts_were(first, 6),
		      "MP4 told each step as it came: 0, then the hold's 1, a press's 2, VOL- to 1 and 0, three presses' 1");
		CHECK(boosts_at_take == 6, "  ...every one before the pause took the levels back");
		CHECK(kept_vol_boost == 1, "and the pause hands the launcher the game's cells");

		kept_vol_boost = 3;          /* the menu's VOL+ filled two more */
		n_game_boosts = 0;
		boosts_at_take = -1;
		CHECK(plat_resident_line("RESUME"), "RESUME after the menu");
		CHECK(plat_resident_wait() == RES_EXIT, "the game plays on and ends");
		CHECK(boosts_were(second, 2),
		      "the first frame's line from before SETLEVEL (1) is never told; SETLEVEL's 3 and a press's 4 are");
		CHECK(boosts_at_take == 2 && kept_vol_boost == 4, "  ...before the end hands the 4 back");
	}
	/* The Screenshot hotkey (DESIGN.md 11.61): the launcher hears each shot
	 * as Diatom takes it, for its shutter, and again when its file is said */
	printf("two screenshots in a running game:\n");
	plat_resident_on_hotkey(heard_hotkey);
	CHECK(plat_resident_line("RESUME"), "RESUME");
	CHECK(plat_resident_wait() == RES_PAUSED, "the game plays, then MENU pauses it");
	CHECK(!strcmp(hotkeys_heard, "shot:taken shot:file shot:taken"),
	      "each shot is heard as it is taken (its shutter, once), and the first again as its file is said");
	if (strcmp(hotkeys_heard, "shot:taken shot:file shot:taken")) printf("         heard: %s\n", hotkeys_heard);
	plat_resident_on_hotkey(NULL);
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	unlink(path);

	/* A core that would not load: Diatom's ERROR before RUNNING says why, and
	 * the launcher keeps it for the shelf (src/startfail.h) instead of falling
	 * back to a second Diatom that fails the same way */
	printf("a game whose core would not load:\n");
	{
		plat_start_error e;
		bool got;
		int r;

		memset(&e, 0, sizeof e);
		r = refused_by("ERROR\tcode=core_load\tmsg=/mnt/SDCARD/ZyperOS/cores/bsnes_libretro.so\twhy=library"
		               "\tname=libfoo.so.2\tversion=\n", &e, &got, 0);
		CHECK(r == RES_DEAD, "the game never started: the wait says dead");
		CHECK(got, "and the reason is there to take");
		CHECK(!strcmp(e.code, "core_load") && !strcmp(e.why, "library") && !strcmp(e.name, "libfoo.so.2") &&
		      !strcmp(e.core, "/mnt/SDCARD/ZyperOS/cores/bsnes_libretro.so") && e.version[0] == '\0',
		      "why, the library and the core's path, read from the line");
		{
			plat_start_error again;

			CHECK(!plat_resident_start_error(&again), "taken once");
		}
		memset(&e, 0, sizeof e);
		r = refused_by("ERROR\tcode=core_load\tmsg=/c/x_libretro.so\twhy=version\tname=libstdc++.so.6"
		               "\tversion=GLIBCXX_3.4.30\n", &e, &got, 0);
		CHECK(r == RES_DEAD && got && !strcmp(e.why, "version") && !strcmp(e.name, "libstdc++.so.6") &&
		      !strcmp(e.version, "GLIBCXX_3.4.30"), "a library too old: its file and the version");
		memset(&e, 0, sizeof e);
		r = refused_by("ERROR\tcode=core_missing\tmsg=no core given\n", &e, &got, 0);
		CHECK(r == RES_DEAD && !got, "another refusal is the log's: nothing to take");
		r = refused_by("ERROR\tcode=core_load\tmsg=/c/y_libretro.so\twhy=machine\tname=\tversion=\n", &e, &got, 0);
		CHECK(r == RES_DEAD && got && !strcmp(e.why, "machine") && e.name[0] == '\0', "another machine: no name");
		/* a game sent again forgets what was not taken */
		r = refused_by("ERROR\tcode=core_load\tmsg=/c/z_libretro.so\twhy=other\tname=\tversion=\n", &e, &got, 1);
		CHECK(r == RES_DEAD, "a refusal, left untaken");
		r = refused_by("", &e, &got, 2);
		CHECK(!got, "a new game starts with nothing left over from the last");
	}

	/* The pid file a resident the launcher started again goes in, where
	 * launch.sh looks before it starts one of its own */
	printf("The pid file, for a resident started again:\n");
	{
		char pf[96], tmp[112], got[32] = "";
		FILE *f;

		CHECK(!strcmp((unsetenv("ZYPEROS_DIATOM_PIDFILE"), plat_resident_pidfile()), "/tmp/diatom.pid"),
		      "launch.sh's own file, /tmp/diatom.pid, unless a check names another");
		snprintf(pf, sizeof pf, "/tmp/zyperos-resident-check-%d.pid", (int)getpid());
		snprintf(tmp, sizeof tmp, "%s.new", pf);
		setenv("ZYPEROS_DIATOM_PIDFILE", pf, 1);
		if ((f = fopen(pf, "w"))) { fputs("853\n", f); fclose(f); }   /* the one that crashed */
		CHECK(plat_resident_note_pid(1907), "the new pid written");
		if ((f = fopen(pf, "r"))) { if (!fgets(got, sizeof got, f)) got[0] = '\0'; fclose(f); }
		CHECK(!strcmp(got, "1907\n"), "  ...over the crashed one's, as launch.sh writes its own (\"1907\\n\")");
		CHECK(access(tmp, F_OK) != 0, "  ...through a file renamed over it, none left behind");
		CHECK(!plat_resident_note_pid(0) && !plat_resident_note_pid(-1), "no pid, nothing written");
		if ((f = fopen(pf, "r"))) { if (!fgets(got, sizeof got, f)) got[0] = '\0'; fclose(f); }
		CHECK(!strcmp(got, "1907\n"), "  ...the file as it was");
		setenv("ZYPEROS_DIATOM_PIDFILE", "/nonexistent-dir/diatom.pid", 1);
		CHECK(!plat_resident_note_pid(1907), "a folder that is not there: false, for the log to say");
		unsetenv("ZYPEROS_DIATOM_PIDFILE");
		unlink(pf);
	}

	if (failures) { printf("%d resident check(s) failed\n", failures); return 1; }
	printf("ok: the resident's answers\n");
	return 0;
}
