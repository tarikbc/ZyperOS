/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's own crash (src/crashrec.h), in child processes that crash
 * on purpose: a bad pointer on the main thread, abort(), a bad pointer on a
 * named thread, and a stack that ran out. Each must still die of its own
 * signal, as the shell's "Segmentation fault" needs, and before that say
 * the signal, its address, the thread and a backtrace, on stderr (the log)
 * and in the record file, the same lines in both. A child that crashes with
 * no handler says nothing, which is the launcher before this. And the
 * record is what crashrec_waiting finds; no record, nothing waits. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../src/crashrec.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static int fails;
static char dir[256];

static void ck(int ok, const char *what)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s\n", what);
		fails++;
	}
}

/* Not static, and not inlined: a frame the backtrace can name */
__attribute__((noinline)) void crashrec_check_fault(void)
{
	*(volatile int *)(uintptr_t)0x10 = 1;
}

static void *fault_thread(void *arg)
{
	(void)arg;
#ifdef __linux__
	prctl(PR_SET_NAME, "zyperos-after", 0, 0, 0);
#else
	pthread_setname_np("zyperos-after");
#endif
	crashrec_check_fault();
	return NULL;
}

__attribute__((noinline)) static int deep(int n)
{
	volatile char pad[512];

	pad[0] = (char)n;
	/* n never comes back to -1: it only grows, until the stack runs out */
	return n == -1 ? 0 : deep(n + 1) + pad[0];
}

enum { FAULT, ABORT, THREAD, OVERFLOW };

/* A child doing `how` with the handler on (or off), its stderr into `err`;
 * its end as waitpid has it */
static int child(int how, bool handler, const char *record, char *err, size_t en)
{
	int p[2], st = 0;
	pid_t pid;
	ssize_t got;
	size_t at = 0;

	if (pipe(p) != 0) return -1;
	pid = fork();
	if (pid == 0) {
		close(p[0]);
		dup2(p[1], STDERR_FILENO);
		if (handler) crashrec_install(record, "9.9.9-check");
		if (how == FAULT) crashrec_check_fault();
		if (how == ABORT) abort();
		if (how == THREAD) {
			pthread_t t;

			pthread_create(&t, NULL, fault_thread, NULL);
			pthread_join(t, NULL);
		}
		if (how == OVERFLOW) deep(0);
		_exit(0);
	}
	close(p[1]);
	while (at + 1 < en && (got = read(p[0], err + at, en - 1 - at)) > 0) at += (size_t)got;
	err[at] = '\0';
	close(p[0]);
	waitpid(pid, &st, 0);
	return st;
}

static char *slurp(const char *path)
{
	static char b[65536];
	FILE *f = fopen(path, "r");
	size_t n = 0;

	b[0] = '\0';
	if (!f) return b;
	n = fread(b, 1, sizeof b - 1, f);
	b[n] = '\0';
	fclose(f);
	return b;
}

static int frames(const char *s)
{
	const char *bt = strstr(s, "crash: backtrace");
	int n = 0;

	if (!bt) return 0;
	for (bt = strchr(bt, '\n'); bt && bt[1]; bt = strchr(bt + 1, '\n')) n++;
	return n;
}

static void one(const char *what, int how, int sig, int sig2, const char *head, const char *thread)
{
	char err[65536], record[300], line[160];
	int st;

	snprintf(record, sizeof record, "%s/record-%d.txt", dir, how);
	unlink(record);
	st = child(how, true, record, err, sizeof err);
	snprintf(line, sizeof line, "%s: the child died of its own signal", what);
	ck(WIFSIGNALED(st) && (WTERMSIG(st) == sig || WTERMSIG(st) == sig2), line);
	snprintf(line, sizeof line, "%s: the log says \"%s\"", what, head);
	ck(strstr(err, head) != NULL, line);
	snprintf(line, sizeof line, "%s: the log names the version", what);
	ck(strstr(err, "ZyperOS 9.9.9-check") != NULL, line);
	if (thread) {
		snprintf(line, sizeof line, "%s: the log names the thread %s", what, thread);
		ck(strstr(err, thread) != NULL, line);
	}
	snprintf(line, sizeof line, "%s: a backtrace of 3 frames or more in the log (%d)", what, frames(err));
	ck(frames(err) >= 3, line);
	snprintf(line, sizeof line, "%s: the record has the log's lines", what);
	ck(!strcmp(slurp(record), err), line);
	snprintf(line, sizeof line, "%s: the record waits for the next start", what);
	ck(crashrec_waiting(record), line);
	if (fails) fprintf(stderr, "--- what the %s child said ---\n%s---\n", what, err);
}

int main(void)
{
	char err[4096], record[300];
	int st;

	snprintf(dir, sizeof dir, "%s/crashrec-check.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	if (!mkdtemp(dir)) return 1;

	/* Before: no handler, a crash says nothing and leaves nothing */
	snprintf(record, sizeof record, "%s/none.txt", dir);
	st = child(FAULT, false, record, err, sizeof err);
	ck(WIFSIGNALED(st) && !strstr(err, "crash:"), "no handler: the child dies saying nothing");
	ck(!crashrec_waiting(record), "no handler: no record waits");

#ifdef __APPLE__
	/* macOS gives a bad pointer below the first page SIGSEGV or SIGBUS */
	one("a bad pointer", FAULT, SIGSEGV, SIGBUS, "at 0x10 in thread", NULL);
	one("a bad pointer on a thread", THREAD, SIGSEGV, SIGBUS, "at 0x10 in thread", NULL);
#else
	one("a bad pointer", FAULT, SIGSEGV, SIGSEGV, "crash: SIGSEGV (code 1) at 0x10 in thread", NULL);
	one("a bad pointer on a thread", THREAD, SIGSEGV, SIGSEGV, "crash: SIGSEGV (code 1) at 0x10 in thread",
	    "zyperos-after");
#endif
	one("abort()", ABORT, SIGABRT, SIGABRT, "crash: SIGABRT", NULL);
	one("a stack that ran out", OVERFLOW, SIGSEGV, SIGBUS, "crash: SIG", NULL);
	ck(!crashrec_waiting(NULL), "NULL: nothing waits");

	{
		char cmd[320];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
		if (system(cmd) != 0) { }
	}
	if (fails) {
		fprintf(stderr, "check-crashrec: %d failed\n", fails);
		return 1;
	}
	printf("check-crashrec: a crash says its signal, address, thread and backtrace, in the log and the record, "
	       "and still dies of it\n");
	return 0;
}
