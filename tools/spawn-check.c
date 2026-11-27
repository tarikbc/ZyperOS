/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-spawn: spawn_detached (src/spawn.h), the start of a program that
 * outlives the launcher - MP4, a resident Diatom started again after a crash -
 * with real processes on this computer.
 *
 * What it holds: the pid it returns is the program's own (alive, in a session
 * of its own, not this process's child, so nothing here has to reap it); a
 * program that cannot be exec'd is -1 with the exec's errno, not a pid that
 * names nothing (the resident's pid file would name it); the environment and
 * the working folder it is given; and nothing of this process's past stderr
 * is open in it. */
#include "../src/spawn.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;

#define CHECK(cond, what) do { \
	if (cond) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

static void nap_ms(int ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static long mono_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* The first line of path, once the detached program has written it: 30 s at most, on the
 * monotonic clock. It returns as soon as the line is there, so that is only how long a program
 * that never writes keeps the check: 200 x 10 ms were too few for a shell to start once make
 * check ran four at a time on a loaded Mac (2026-11-24). */
static int read_when_there(const char *path, char *out, size_t cap)
{
	const long until = mono_ms() + 30000;

	while (mono_ms() < until) {
		FILE *f = fopen(path, "r");

		if (f) {
			bool got = fgets(out, (int)cap, f) != NULL;

			fclose(f);
			if (got && strchr(out, '\n')) {
				out[strcspn(out, "\n")] = '\0';
				return 0;
			}
		}
		nap_ms(10);
	}
	return -1;
}

/* The parent process of pid, by ps, which a Mac and Linux both have */
static long parent_of(long pid)
{
	char cmd[64], line[64];
	FILE *p;
	long ppid = -1;

	snprintf(cmd, sizeof cmd, "ps -o ppid= -p %ld", pid);
	if (!(p = popen(cmd, "r"))) return -1;
	if (fgets(line, sizeof line, p)) ppid = strtol(line, NULL, 10);
	pclose(p);
	return ppid;
}

int main(void)
{
	char dir[] = "/tmp/spawn-check.XXXXXX", path[PATH_MAX], line[PATH_MAX], want[PATH_MAX];
	char script[PATH_MAX * 2];
	long pid;
	int st, kept;

	printf("check-spawn:\n");
	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }

	/* A program that keeps running */
	{
		char *argv[] = { (char *)"/bin/sleep", (char *)"30", NULL };

		pid = spawn_detached(argv, NULL, NULL);
		CHECK(pid > 0 && kill((pid_t)pid, 0) == 0, "a program that runs: its pid, and it is alive");
		CHECK(waitpid((pid_t)pid, &st, WNOHANG) < 0 && errno == ECHILD,
		      "  ...not this process's child: nothing here has to reap it");
		CHECK(parent_of(pid) > 0 && parent_of(pid) != (long)getpid(),
		      "  ...its parent another than this process (init, once the middle child is gone)");
		CHECK(getsid((pid_t)pid) == (pid_t)pid, "  ...in a session of its own");
		if (pid > 0) kill((pid_t)pid, SIGKILL);
	}

	/* A program that is not there */
	{
		char *argv[] = { (char *)"/nonexistent/diatom", NULL };

		errno = 0;
		pid = spawn_detached(argv, NULL, NULL);
		CHECK(pid == -1 && errno == ENOENT,
		      "a program that cannot be exec'd: -1 and the exec's ENOENT, not a pid naming nothing");
	}

	/* The environment and the folder it is given, and none of this
	 * process's descriptors: fd `kept` is open here, close-on-exec not set */
	kept = open("/dev/null", O_RDONLY);
	while (kept >= 0 && kept < 9) kept = dup(kept);
	snprintf(path, sizeof path, "%s/out", dir);
	snprintf(script, sizeof script,
	         "{ echo \"$SPAWN_CHECK\"; pwd -P; [ -e /dev/fd/%d ] && echo open || echo closed; } > %s.tmp && mv %s.tmp %s",
	         kept, path, path, path);
	{
		char *argv[] = { (char *)"/bin/sh", (char *)"-c", script, NULL };
		char *envp[] = { (char *)"SPAWN_CHECK=given", (char *)"PATH=/bin:/usr/bin", NULL };
		char *want_dir = realpath(dir, NULL);
		FILE *f;
		char l1[64] = "", l2[PATH_MAX] = "", l3[64] = "";

		pid = spawn_detached(argv, envp, dir);
		CHECK(pid > 0, "a shell given an environment and a folder: started");
		if (read_when_there(path, line, sizeof line) == 0 && (f = fopen(path, "r"))) {
			if (fgets(l1, sizeof l1, f)) l1[strcspn(l1, "\n")] = '\0';
			if (fgets(l2, sizeof l2, f)) l2[strcspn(l2, "\n")] = '\0';
			if (fgets(l3, sizeof l3, f)) l3[strcspn(l3, "\n")] = '\0';
			fclose(f);
		}
		snprintf(want, sizeof want, "%s", want_dir ? want_dir : dir);
		free(want_dir);
		CHECK(!strcmp(l1, "given"), "  ...in the environment it was given");
		CHECK(!strcmp(l2, want), "  ...in the folder it was given");
		CHECK(!strcmp(l3, "closed"), "  ...with none of this process's descriptors open in it");
	}
	if (kept >= 0) close(kept);

	unlink(path);
	rmdir(dir);
	printf(fails ? "check-spawn: %d FAILED\n" : "check-spawn: all passed\n", fails);
	return fails != 0;
}
