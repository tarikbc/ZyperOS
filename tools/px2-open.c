/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-open: open a game in the running ZyperOS from a shell, as A on it in
 * its shelf would, and quit it again.
 *
 *   px2-open [--fresh] [--core NAME] [--wait-running SECONDS] PATH
 *   px2-open --quit
 *   px2-open --status
 *   px2-open --wait-running SECONDS
 *
 * PATH is a game under /mnt/SDCARD/Roms, a port's script in Roms/Ports
 * included: the launcher finds its system by its folder, starts it on the
 * core that shelf runs, from its auto state, with the in-game menu, the logs
 * and everything else A gives it (src/ctl.h, the command channel). --fresh
 * starts it without its auto state this once (the auto state it writes when
 * it ends replaces the one it had); --core one of the other cores its shelf
 * offers, this once (the Dreamcast's Flycast, say). A game running already is
 * quit first, as the in-game menu's Quit quits it, a port too. --wait-running
 * waits up to SECONDS for the game to say its first frame is up, and fails
 * when it ends first or never starts; alone, it waits for whatever is opened.
 *
 * --quit ends the running game as the in-game menu's Quit does (over the
 * menu as well) and waits for the shelf. --status prints where the launcher
 * is, one key=value a line: state (shelf, screen, starting, running, paused,
 * stopping, port), and for a game its system, path, core, since (Unix time)
 * and up (seconds).
 *
 * Exit status: 0 done, 1 refused (the launcher's error is printed), 2 no
 * launcher answered or the wait ran out, 64 a wrong command line.
 *
 * For a debugging session over SSH with nobody at the device, beside px2-shot
 * and px2-input; never a player's way in. The socket is root's alone.
 *
 * Build: the plastron toolchain with src/ctl.c, no libraries. Run as root. */
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../src/ctl.h"

enum { ASK_OK = 0, ASK_NONE = -2, ASK_SLOW = -3 };

static void nap(unsigned ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* One command, its answer in `ans`: ASK_NONE when no launcher listens,
 * ASK_SLOW when it does not answer within `ms` (busy drawing a screen that
 * does not read the channel, or blocked in a launch) */
static int ask(const char *line, char *ans, size_t an, unsigned ms)
{
	struct sockaddr_un sa;
	struct timeval tv = { ms / 1000, (int)(ms % 1000) * 1000 };
	size_t got = 0;
	int fd;

	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s/%s", ctl_dir(), CTL_SOCK_NAME);
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return ASK_NONE;
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
		int e = errno;

		close(fd);
		if (e == EACCES || e == EPERM) {
			fprintf(stderr, "px2-open: %s: only root can use it\n", sa.sun_path);
			exit(1);
		}
		return ASK_NONE;
	}
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	if (write(fd, line, strlen(line)) < 0 || write(fd, "\n", 1) < 0) {
		close(fd);
		return ASK_SLOW;
	}
	while (got + 1 < an) {
		ssize_t r = read(fd, ans + got, an - 1 - got);

		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		got += (size_t)r;
		if (memchr(ans, '\n', got)) break;
	}
	close(fd);
	ans[got] = '\0';
	if (!got) return ASK_SLOW;
	ans[strcspn(ans, "\n")] = '\0';
	return ASK_OK;
}

/* ask, again while the launcher is too busy to answer, for up to `total_ms`:
 * a command it never read is dropped when this side gives up on it (ctl_take),
 * so asking again does it once */
static int ask_again(const char *line, char *ans, size_t an, unsigned total_ms)
{
	long until = now_ms() + total_ms;
	int r;

	while ((r = ask(line, ans, an, 1500)) == ASK_SLOW && now_ms() < until)
		nap(200);
	return r;
}

/* A field of an answer, "" when it has none */
static const char *field(const char *ans, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	const char *p = ans;

	out[0] = '\0';
	while ((p = strchr(p, '\t'))) {
		p++;
		if (!strncmp(p, key, kl) && p[kl] == '=') {
			p += kl + 1;
			snprintf(out, n, "%.*s", (int)strcspn(p, "\t"), p);
			break;
		}
	}
	return out;
}

static bool is_ok(const char *ans)
{
	return !strncmp(ans, "ok", 2) && (ans[2] == '\0' || ans[2] == '\t');
}

/* The launcher's error, as a line for the person at the shell */
static int refused(const char *ans)
{
	char code[64], msg[CTL_LINE];

	field(ans, "code", code, sizeof code);
	field(ans, "msg", msg, sizeof msg);
	fprintf(stderr, "px2-open: %s (%s)\n", msg[0] ? msg : ans, code[0] ? code : "error");
	return 1;
}

/* A port running, with no launcher: its record, and launch.sh still there to
 * end it */
static bool port_running(int *pid, long *since, char *path, size_t pn)
{
	char p[64], cmd[512] = "";
	FILE *f;
	size_t n;

	if (!ctl_port_read(ctl_dir(), pid, since, path, pn)) return false;
	snprintf(p, sizeof p, "/proc/%d/cmdline", *pid);
	if (!(f = fopen(p, "r"))) return false;
	n = fread(cmd, 1, sizeof cmd - 1, f);
	fclose(f);
	for (size_t i = 0; i < n; i++)
		if (!cmd[i]) cmd[i] = ' ';
	cmd[n] = '\0';
	return strstr(cmd, "launch.sh") != NULL;
}

/* The port's own process: its script, which launch.sh starts with the
 * script's path as an argument once it has put its USR1 and USR2 traps up
 * (run_port). Until then launch.sh is still waiting for the launcher to
 * exit, or stopping Diatom, and a USR2 would end launch.sh itself. */
static bool port_started(const char *path)
{
	char p[64], cmd[CTL_PATH * 2];
	struct dirent *e;
	DIR *d = opendir("/proc");
	bool found = false;

	if (!d) return false;
	while (!found && (e = readdir(d))) {
		FILE *f;
		size_t n, i;

		if (e->d_name[0] < '1' || e->d_name[0] > '9' || atoi(e->d_name) == (int)getpid())
			continue;
		snprintf(p, sizeof p, "/proc/%d/cmdline", atoi(e->d_name));
		if (!(f = fopen(p, "r"))) continue;
		n = fread(cmd, 1, sizeof cmd - 1, f);
		fclose(f);
		cmd[n] = '\0';
		for (i = 0; i < n; i += strlen(cmd + i) + 1)
			if (!strcmp(cmd + i, path)) { found = true; break; }
	}
	closedir(d);
	return found;
}

/* Where the launcher is, as one answer: a port's from its record */
static int state_now(char *ans, size_t an, unsigned ms)
{
	int r = ask("status", ans, an, ms), pid;
	long since;
	char path[CTL_PATH];

	if (r != ASK_NONE) return r;
	if (!port_running(&pid, &since, path, sizeof path)) return ASK_NONE;
	/* "starting" until its script runs: the launcher is going, launch.sh
	 * has not started it yet */
	snprintf(ans, an, "ok\tstate=%s\tpath=%s\tsince=%ld\tup=%ld\tlauncher=%d",
	         port_started(path) ? "port" : "starting", path, since,
	         (long)time(NULL) - since, pid);
	return ASK_OK;
}

static bool in_game(const char *state)
{
	return !strcmp(state, "starting") || !strcmp(state, "running") ||
	       !strcmp(state, "paused") || !strcmp(state, "stopping") || !strcmp(state, "port");
}

/* Until the launcher is on its shelf or a screen over it, no game running.
 * A game's end passes through a moment of "screen" (the frame the shelf
 * comes back with), in which an open would be busy: the shelf itself is
 * waited for 2 s more, for a script that opens the next game at once. */
static int wait_shelf(int secs)
{
	long until = now_ms() + secs * 1000L, screen_until = 0;
	char ans[CTL_LINE], st[32];

	while (now_ms() < until) {
		if (state_now(ans, sizeof ans, 1000) == ASK_OK &&
		    !in_game(field(ans, "state", st, sizeof st))) {
			if (!strcmp(st, "screen") && !screen_until) screen_until = now_ms() + 2000;
			if (strcmp(st, "screen") || now_ms() >= screen_until) {
				printf("state=%s\n", st);
				return 0;
			}
		}
		nap(200);
	}
	fprintf(stderr, "px2-open: still not back on the shelf after %d s\n", secs);
	return 2;
}

/* The port ended as its in-game menu's Quit ends it: USR2 to launch.sh, once
 * the port runs and launch.sh has its trap up (port_started) */
static int quit_port(int pid, const char *path, int secs)
{
	long until = now_ms() + 15000;

	printf("quit=%s\n", path);
	while (!port_started(path)) {
		if (now_ms() > until || kill(pid, 0) != 0) {
			fprintf(stderr, "px2-open: the port never started; launch.sh left as it is\n");
			return 2;
		}
		nap(200);
	}
	if (kill(pid, SIGUSR2) != 0) {
		fprintf(stderr, "px2-open: cannot reach launch.sh (%d): %s\n", pid, strerror(errno));
		return 1;
	}
	return wait_shelf(secs);
}

static int do_quit(void)
{
	char ans[CTL_LINE], v[CTL_PATH];
	long since;
	int r = ask_again("quit", ans, sizeof ans, 20000), pid;

	if (r == ASK_NONE) {
		if (port_running(&pid, &since, v, sizeof v)) return quit_port(pid, v, 30);
		fprintf(stderr, "px2-open: no launcher answers (is ZyperOS running?)\n");
		return 2;
	}
	if (r == ASK_SLOW) {
		fprintf(stderr, "px2-open: the launcher did not answer\n");
		return 2;
	}
	if (!is_ok(ans)) return refused(ans);
	printf("quit=%s\n", field(ans, "quit", v, sizeof v));
	return wait_shelf(20);
}

static int do_status(void)
{
	char ans[CTL_LINE];
	int r = ask_again("status", ans, sizeof ans, 5000);
	const char *p;

	if (r == ASK_NONE) r = state_now(ans, sizeof ans, 1000);

	if (r == ASK_NONE) {
		fprintf(stderr, "px2-open: no launcher answers (is ZyperOS running?)\n");
		return 2;
	}
	if (r == ASK_SLOW) {
		fprintf(stderr, "px2-open: the launcher did not answer (a screen that does not "
		                "read the channel, or a game started without the resident emulator)\n");
		return 2;
	}
	if (!is_ok(ans)) return refused(ans);
	for (p = strchr(ans, '\t'); p; p = strchr(p, '\t')) {
		p++;
		printf("%.*s\n", (int)strcspn(p, "\t"), p);
	}
	return 0;
}

/* Until a game opened at `after` (Unix time, 0 any) says it is running.
 * `opened`: the open's own wait, which a shelf answer ends - the launcher
 * answers on its shelf only once the launch it was asked for is over. */
static int wait_running(int secs, long after, bool opened)
{
	long until = now_ms() + secs * 1000L;
	char ans[CTL_LINE], st[32], v[CTL_PATH];

	while (now_ms() < until) {
		if (state_now(ans, sizeof ans, 1000) == ASK_OK && is_ok(ans)) {
			field(ans, "state", st, sizeof st);
			if ((!strcmp(st, "running") || !strcmp(st, "port")) &&
			    (!after || atol(field(ans, "since", v, sizeof v)) >= after)) {
				printf("state=%s\npath=%s\n", st, field(ans, "path", v, sizeof v));
				return 0;
			}
			if (opened && !strcmp(st, "shelf") && !field(ans, "next", v, sizeof v)[0]) {
				fprintf(stderr, "px2-open: the game did not start, or ended at once "
				                "(the launcher's log says why)\n");
				return 1;
			}
		}
		nap(200);
	}
	fprintf(stderr, "px2-open: not running after %d s\n", secs);
	return 2;
}

static int do_open(const char *arg, bool fresh, const char *core, int wait)
{
	char path[CTL_PATH], line[CTL_LINE], ans[CTL_LINE], v[CTL_PATH];
	long since, sent;
	int r, pid;

	if (arg[0] == '/') {
		snprintf(path, sizeof path, "%s", arg);
	} else {
		char cwd[CTL_PATH];

		if (!getcwd(cwd, sizeof cwd)) { perror("px2-open: getcwd"); return 1; }
		if (snprintf(path, sizeof path, "%s/%s", cwd, arg) >= (int)sizeof path) {
			fprintf(stderr, "px2-open: the path is too long\n");
			return 1;
		}
	}
	if (strchr(path, '\t') || strchr(path, '\n')) {
		fprintf(stderr, "px2-open: a tab or a newline in the path\n");
		return 1;
	}
	snprintf(line, sizeof line, "open\tpath=%s%s%s%s", path, fresh ? "\tfresh=1" : "",
	         core ? "\tcore=" : "", core ? core : "");

	/* A port has the device, with no launcher to ask: ended first, as the
	 * launcher ends a game first, and the launcher waited for */
	if (ask_again("status", ans, sizeof ans, 20000) == ASK_NONE &&
	    port_running(&pid, &since, v, sizeof v)) {
		if ((r = quit_port(pid, v, 30)) != 0) return r;
	}
	sent = (long)time(NULL);
	r = ask_again(line, ans, sizeof ans, 20000);
	/* Busy in the moment a game's end shows the shelf again: once more */
	if (r == ASK_OK && !is_ok(ans) && !strcmp(field(ans, "code", v, sizeof v), "busy")) {
		nap(1500);
		r = ask_again(line, ans, sizeof ans, 20000);
	}
	if (r == ASK_NONE) {
		fprintf(stderr, "px2-open: no launcher answers (is ZyperOS running?)\n");
		return 2;
	}
	if (r == ASK_SLOW) {
		fprintf(stderr, "px2-open: the launcher did not answer\n");
		return 2;
	}
	if (!is_ok(ans)) return refused(ans);
	if (field(ans, "quit", v, sizeof v)[0]) printf("quit=%s\n", v);
	printf("opening=%s\n", field(ans, "opening", v, sizeof v));
	if (field(ans, "system", v, sizeof v)[0]) printf("system=%s\n", v);
	if (field(ans, "core", v, sizeof v)[0]) printf("core=%s\n", v);
	if (field(ans, "port", v, sizeof v)[0]) printf("port=1\n");
	fflush(stdout);
	return wait > 0 ? wait_running(wait, sent - 1, true) : 0;
}

static int usage(void)
{
	fprintf(stderr,
	        "usage: px2-open [--fresh] [--core NAME] [--wait-running SECONDS] PATH\n"
	        "       px2-open --quit\n"
	        "       px2-open --status\n"
	        "       px2-open --wait-running SECONDS\n");
	return 64;
}

int main(int argc, char **argv)
{
	const char *path = NULL, *core = NULL;
	bool fresh = false, quit = false, status = false;
	int wait = 0, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fresh")) fresh = true;
		else if (!strcmp(argv[i], "--quit")) quit = true;
		else if (!strcmp(argv[i], "--status")) status = true;
		else if (!strcmp(argv[i], "--core") && i + 1 < argc) core = argv[++i];
		else if (!strcmp(argv[i], "--wait-running") && i + 1 < argc) {
			char *end;

			wait = (int)strtol(argv[++i], &end, 10);
			if (*end || wait <= 0) return usage();
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		} else if (argv[i][0] == '-' && argv[i][1]) return usage();
		else if (!path) path = argv[i];
		else return usage();
	}
	if (quit + status + (path != NULL) > 1) return usage();
	if (quit) return (fresh || core || wait) ? usage() : do_quit();
	if (status) return (fresh || core || wait) ? usage() : do_status();
	if (path) return do_open(path, fresh, core, wait);
	if (wait && !fresh && !core) return wait_running(wait, 0, false);
	return usage();
}
