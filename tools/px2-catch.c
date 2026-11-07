/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-catch: where a process crashed, on a device with no debugger.
 *
 *   px2-catch PID [SECONDS]     (default 60 s)
 *
 * Attaches to PID's main thread with ptrace and waits. When the thread takes
 * a fatal signal (SEGV, BUS, ABRT, ILL, FPE) it prints the signal and the
 * faulting address, pc and lr, every register x0-x30 with the mapping each
 * points into, the words around x27, a frame-pointer walk and the code
 * addresses found on the stack near sp, each as file+offset, which a host
 * turns into function names with nm or addr2line against the unstripped
 * build. Then it lets the signal through, so the crash goes on as it would
 * have. Nothing is printed and nothing changes if no such signal comes.
 *
 * Found the launcher's crash with DOWN held in MENU (2026-11-05): SIGSEGV in
 * strchr from menu_word, with the row pointer 48 bytes before the rows.
 *
 * Build: the plastron toolchain, no libraries. Run as root on the device. */
#include <elf.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

static int pid;

/* The mapping `a` is in, as "file+offset"; NULL when none is, `exec` limits
 * it to code */
static int region(uint64_t a, bool exec, char *out, size_t n)
{
	char path[64], line[512];
	FILE *f;
	int found = 0;

	snprintf(path, sizeof path, "/proc/%d/maps", pid);
	if (!(f = fopen(path, "r"))) return 0;
	while (fgets(line, sizeof line, f)) {
		unsigned long long lo, hi, off;
		char perms[8], name[400] = "";

		if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %399[^\n]", &lo, &hi, perms, &off, name) >= 4 &&
		    a >= lo && a < hi && (!exec || perms[2] == 'x')) {
			snprintf(out, n, "%s+0x%llx", name[0] ? name : "[anon]",
			         (unsigned long long)(a - lo + off));
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static void where(const char *what, uint64_t a)
{
	char at[480];

	if (region(a, false, at, sizeof at)) printf("%s 0x%llx  %s\n", what, (unsigned long long)a, at);
	else printf("%s 0x%llx\n", what, (unsigned long long)a);
}

static uint64_t peek(uint64_t a)
{
	return (uint64_t)ptrace(PTRACE_PEEKDATA, pid, (void *)a, 0);
}

int main(int argc, char **argv)
{
	int seconds, status;
	time_t end;

	if (argc < 2) {
		fprintf(stderr, "usage: px2-catch PID [SECONDS]\n");
		return 2;
	}
	pid = atoi(argv[1]);
	seconds = argc > 2 ? atoi(argv[2]) : 60;
	if (ptrace(PTRACE_SEIZE, pid, 0, 0) < 0) {
		perror("px2-catch: seize");
		return 1;
	}
	end = time(NULL) + seconds;
	printf("watching %d\n", pid);
	fflush(stdout);
	while (time(NULL) < end) {
		if (waitpid(pid, &status, __WALL) < 0) {
			perror("px2-catch: waitpid");
			return 1;
		}
		if (WIFEXITED(status) || WIFSIGNALED(status)) {
			printf("gone\n");
			return 0;
		}
		if (WIFSTOPPED(status)) {
			int sig = WSTOPSIG(status);

			if (sig == SIGSEGV || sig == SIGBUS || sig == SIGABRT || sig == SIGILL || sig == SIGFPE) {
				uint64_t regs[34];    /* x0-x30, sp, pc, pstate */
				struct iovec io = { regs, sizeof regs };
				siginfo_t si;
				char at[480];

				ptrace(PTRACE_GETSIGINFO, pid, 0, &si);
				ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &io);
				printf("signal %d (%s) at address %p\n", sig, strsignal(sig), si.si_addr);
				where("pc", regs[32]);
				where("lr", regs[30]);
				for (int i = 0; i < 31; i++) {
					char w[8];

					snprintf(w, sizeof w, "x%d", i);
					where(w, regs[i]);
				}
				printf("sp 0x%llx\n", (unsigned long long)regs[31]);
				for (int k = -6; k < 9; k++)
					printf("  [x27%+d] 0x%016llx\n", 8 * k,
					       (unsigned long long)peek(regs[27] + 8 * k));
				for (uint64_t fp = regs[29], d = 0; fp && d < 16; d++) {
					uint64_t next = peek(fp), ret = peek(fp + 8);

					if (!ret) break;
					where("  frame", ret);
					if (next <= fp) break;
					fp = next;
				}
				for (int i = 0; i < 48; i++)
					if (region(peek(regs[31] + 8 * i), true, at, sizeof at))
						printf("  stack[%d] %s\n", i, at);
				fflush(stdout);
				ptrace(PTRACE_DETACH, pid, 0, (void *)(long)sig);
				return 0;
			}
			/* Anything else goes on to the process as it came */
			ptrace(PTRACE_CONT, pid, 0, (void *)(long)(sig == SIGTRAP || sig == SIGSTOP ? 0 : sig));
		}
	}
	ptrace(PTRACE_DETACH, pid, 0, 0);
	printf("no fatal signal in %d s\n", seconds);
	return 0;
}
