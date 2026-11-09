/* zyhome: MENU+START, held a second, ends whatever runs and brings back the
 * home. The way out of a dead end: a game that will not open its menu, a
 * port that ignores MENU, a screen that stopped answering.
 *
 * Run by launch.sh for the whole time ZyperOS is up, beside everything and
 * owned by nothing, so it still works when the launcher, Diatom or a port is
 * the thing that is stuck. It only reads the pad (no grab), so every press
 * still reaches whoever had it.
 *
 * What it does, in order:
 *   1. leaves CTL_HOME_FLAG, so the launcher that starts next starts on the
 *      home and continues no game (src/main.c);
 *   2. a port running (its record, src/ctl.h): USR2 to launch.sh, the
 *      in-game menu's own Quit; if the port is still there after
 *      PORT_GRACE_MS, its process group is killed;
 *   3. otherwise the launcher: TERM, which ends a game the way POWER's save
 *      does (the resident is told to stop, the state is written); if it is
 *      still up after LAUNCHER_GRACE_MS, KILL to it and to Diatom.
 * launch.sh then starts Diatom and the launcher again, as after any exit. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "../src/ctl.h"
#include "zychord.h"

#define MAX_PADS          8
#define PORT_GRACE_MS     8000
#define LAUNCHER_GRACE_MS 6000
#define CODE_MENU         BTN_TRIGGER_HAPPY1      /* src/device/pixel2.c */
#define NLONGS(n)         (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define HAS(b, c)         (((b)[(c) / (8 * sizeof(long))] >> ((c) % (8 * sizeof(long)))) & 1)

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void nap(unsigned ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* The pids whose /proc comm is `name`, at most `n` */
static int pids_named(const char *name, int *out, int n)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int k = 0;

	if (!d) return 0;
	while (k < n && (e = readdir(d))) {
		char p[64], comm[64] = "";
		FILE *f;

		if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
		snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
		if (!(f = fopen(p, "r"))) continue;
		if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
		fclose(f);
		if (!strcmp(comm, name)) out[k++] = atoi(e->d_name);
	}
	closedir(d);
	return k;
}

static int signal_named(const char *name, int sig)
{
	int pids[8], n = pids_named(name, pids, 8);

	for (int i = 0; i < n; i++) kill(pids[i], sig);
	return n;
}

/* The pid whose command line has `arg` as one of its words, or -1 */
static int pid_with_arg(const char *arg)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int found = -1;

	if (!d) return -1;
	while (found < 0 && (e = readdir(d))) {
		char p[64], cmd[CTL_PATH * 2];
		FILE *f;
		size_t n;

		if (e->d_name[0] < '1' || e->d_name[0] > '9' || atoi(e->d_name) == (int)getpid())
			continue;
		snprintf(p, sizeof p, "/proc/%s/cmdline", e->d_name);
		if (!(f = fopen(p, "r"))) continue;
		n = fread(cmd, 1, sizeof cmd - 1, f);
		fclose(f);
		cmd[n] = '\0';
		for (size_t i = 0; i < n; i += strlen(cmd + i) + 1)
			if (!strcmp(cmd + i, arg)) { found = atoi(e->d_name); break; }
	}
	closedir(d);
	return found;
}

static void go_home(void)
{
	int pid, script;
	long since, until;
	char path[CTL_PATH];
	FILE *f = fopen(CTL_HOME_FLAG, "w");

	if (f) fclose(f);
	fprintf(stderr, "zyhome: MENU+START, back to the home\n");

	if (ctl_port_read(ctl_dir(), &pid, &since, path, sizeof path) && kill(pid, 0) == 0) {
		script = pid_with_arg(path);
		fprintf(stderr, "zyhome: ending the port %s\n", path);
		if (script > 0) kill(pid, SIGUSR2);
		until = now_ms() + PORT_GRACE_MS;
		while (now_ms() < until && pid_with_arg(path) > 0) nap(200);
		if ((script = pid_with_arg(path)) > 0) {
			fprintf(stderr, "zyhome: the port did not end, killing its group\n");
			kill(-script, SIGKILL);
			kill(script, SIGKILL);
		}
		return;
	}

	if (!signal_named("zyperos.elf", SIGTERM)) {
		/* No launcher and no port: launch.sh is between the two, and starts
		 * the launcher on its own, which reads the mark */
		return;
	}
	until = now_ms() + LAUNCHER_GRACE_MS;
	while (now_ms() < until) {
		int p[1];

		if (!pids_named("zyperos.elf", p, 1)) return;
		nap(100);
	}
	fprintf(stderr, "zyhome: the launcher did not end, killing it and Diatom\n");
	signal_named("zyperos.elf", SIGKILL);
	signal_named("diatom", SIGKILL);
}

int main(void)
{
	struct pollfd fds[MAX_PADS];
	int n = 0;
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	zychord c;

	signal(SIGPIPE, SIG_IGN);
	while (d && (e = readdir(d)) && n < MAX_PADS) {
		char path[300];
		unsigned long keys[NLONGS(KEY_MAX)] = { 0 };
		int fd;

		if (strncmp(e->d_name, "event", 5)) continue;
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
		if (HAS(keys, CODE_MENU) || HAS(keys, BTN_START)) {
			fds[n].fd = fd;
			fds[n++].events = POLLIN;
		} else {
			close(fd);
		}
	}
	if (d) closedir(d);
	if (!n) {
		fprintf(stderr, "zyhome: no pad with MENU or START\n");
		return 1;
	}

	zychord_init(&c);
	for (;;) {
		struct input_event ev[32];
		int wait = zychord_wait(&c, now_ms());

		if (poll(fds, n, wait) < 0 && errno != EINTR) return 1;
		for (int i = 0; i < n; i++) {
			ssize_t r;

			if (!(fds[i].revents & POLLIN)) continue;
			while ((r = read(fds[i].fd, ev, sizeof ev)) > 0)
				for (size_t k = 0; k < (size_t)r / sizeof ev[0]; k++) {
					if (ev[k].type != EV_KEY || ev[k].value == 2) continue;
					if (ev[k].code != CODE_MENU && ev[k].code != BTN_START) continue;
					zychord_key(&c, ev[k].code == CODE_MENU, ev[k].value != 0, now_ms());
				}
		}
		if (zychord_due(&c, now_ms())) go_home();
	}
}
