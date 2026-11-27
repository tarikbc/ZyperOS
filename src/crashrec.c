/* SPDX-License-Identifier: GPL-3.0-only */
/* See crashrec.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "crashrec.h"

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define FRAMES 48

static char                  g_record[256];
static char                  g_version[48];
static volatile sig_atomic_t g_in;
/* The main thread's stack for the handler: a fixed size, since SIGSTKSZ is
 * not a constant on newer glibc */
static char                  g_alt[64 * 1024];

static const int SIGNALS[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

/* ---- text without stdio: what a handler may do ------------------------ */

typedef struct {
	char   b[320];
	size_t n;
} line;

static void put(line *l, const char *s)
{
	while (*s && l->n + 1 < sizeof l->b) l->b[l->n++] = *s++;
	l->b[l->n] = '\0';
}

static void put_hex(line *l, uintptr_t v)
{
	char d[2 + 2 * sizeof v + 1];
	int i = (int)sizeof d - 1;

	d[i] = '\0';
	do {
		d[--i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v && i > 2);
	d[--i] = 'x';
	d[--i] = '0';
	put(l, d + i);
}

static void put_dec(line *l, long v)
{
	char d[24];
	int i = (int)sizeof d - 1;
	bool neg = v < 0;
	unsigned long u = neg ? 0ul - (unsigned long)v : (unsigned long)v;

	d[i] = '\0';
	do {
		d[--i] = (char)('0' + u % 10);
		u /= 10;
	} while (u && i > 1);
	if (neg) d[--i] = '-';
	put(l, d + i);
}

static const char *sig_name(int sig)
{
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS:  return "SIGBUS";
	case SIGILL:  return "SIGILL";
	case SIGFPE:  return "SIGFPE";
	case SIGABRT: return "SIGABRT";
	default:      return "a signal";
	}
}

static void say(int fd, const line *l)
{
	size_t at = 0;

	while (fd >= 0 && at < l->n) {
		ssize_t w = write(fd, l->b + at, l->n - at);

		if (w <= 0) break;
		at += (size_t)w;
	}
}

/* ---- the handler ---------------------------------------------------------- */

static void on_crash(int sig, siginfo_t *si, void *uc)
{
	line head = { .n = 0 }, bt = { .n = 0 };
	char thread[17] = "?";
	int fd, fds[2], k;
#ifdef HAVE_BACKTRACE
	void *frames[FRAMES];
	int nf;
#endif

	(void)uc;
	/* A crash in the handler itself: the signal's own action, at once */
	if (g_in) {
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	g_in = 1;
#ifdef __linux__
	prctl(PR_GET_NAME, thread, 0, 0, 0);
	thread[16] = '\0';
#endif
	put(&head, "crash: ");
	put(&head, sig_name(sig));
	put(&head, " (code ");
	put_dec(&head, si ? si->si_code : 0);
	put(&head, ") at ");
	put_hex(&head, si ? (uintptr_t)si->si_addr : 0);
	put(&head, " in thread ");
	put(&head, thread);
	put(&head, ", pid ");
	put_dec(&head, (long)getpid());
	put(&head, ", ZyperOS ");
	put(&head, g_version[0] ? g_version : "?");
	put(&head, "\n");
	put(&bt, "crash: backtrace, each frame the binary and its offset (addr2line -e with this build):\n");
	fd = g_record[0] ? open(g_record, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
	fds[0] = STDERR_FILENO;
	fds[1] = fd;
#ifdef HAVE_BACKTRACE
	nf = backtrace(frames, FRAMES);
#endif
	for (k = 0; k < 2; k++) {
		if (fds[k] < 0) continue;
		say(fds[k], &head);
		say(fds[k], &bt);
#ifdef HAVE_BACKTRACE
		backtrace_symbols_fd(frames, nf, fds[k]);
#endif
	}
	if (fd >= 0) close(fd);
	/* The signal's own action: the process dies of it, as without this */
	signal(sig, SIG_DFL);
	raise(sig);
}

void crashrec_install(const char *record, const char *version)
{
	struct sigaction sa;
	stack_t st;
	size_t i;

	strncpy(g_record, record ? record : "", sizeof g_record - 1);
	g_record[sizeof g_record - 1] = '\0';
	strncpy(g_version, version ? version : "", sizeof g_version - 1);
	g_version[sizeof g_version - 1] = '\0';
#ifdef HAVE_BACKTRACE
	{
		/* Loaded now: its first call may load libgcc_s, which a handler
		 * must not have to do */
		void *one[1];

		(void)backtrace(one, 1);
	}
#endif
	memset(&st, 0, sizeof st);
	st.ss_sp = g_alt;
	st.ss_size = sizeof g_alt;
	sigaltstack(&st, NULL);
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_crash;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < sizeof SIGNALS / sizeof SIGNALS[0]; i++) sigaction(SIGNALS[i], &sa, NULL);
}

bool crashrec_waiting(const char *record)
{
	struct stat st;

	return record && stat(record, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
