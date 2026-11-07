/* SPDX-License-Identifier: GPL-3.0-only */
/* See spawn.h. The Wi-Fi's wpa_cli and Bluetooth's bluetoothctl. */
#include "spawn.h"

#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The deadline is not defensive programming, it is a bug fix. On a fresh card
 * Bluetooth is off, so launch.sh never runs hciattach and there is no adapter;
 * `bluetoothctl power on` in that state does not fail and does not exit, it
 * WAITS for a controller that is never coming. The launcher sat in waitpid
 * behind it and the screen froze on "Turning on...", needing the process
 * killed over adb. Seen on the device 2026-09-06.
 *
 * bluetoothctl has a --timeout of its own and it is used for scanning, but
 * relying on the tool to bound itself is what produced the hang. This bounds
 * it from outside, where a tool that ignores its own timeout cannot reach. */
int spawn_capture(char *const argv[], char *out, size_t cap, int timeout_s)
{
	int fd[2], status = -1;
	pid_t pid;
	size_t used = 0;
	struct timespec t0, now;
	bool killed = false;

	if (out && cap) out[0] = '\0';
	if (pipe(fd) < 0) return -1;

	pid = fork();
	if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		dup2(fd[1], STDERR_FILENO);
		close(fd[1]);
		execv(argv[0], argv);
		_exit(127);
	}
	close(fd[1]);
	clock_gettime(CLOCK_MONOTONIC, &t0);

	for (;;) {
		ssize_t n;
		char scratch[256];
		char *dst;
		size_t room;

		if (timeout_s > 0) {
			struct pollfd pfd = { fd[0], POLLIN, 0 };

			clock_gettime(CLOCK_MONOTONIC, &now);
			if (now.tv_sec - t0.tv_sec >= timeout_s) {
				/* SIGKILL rather than SIGTERM: a bluetoothctl waiting on a
				 * controller has already shown it is not minded to leave. */
				kill(pid, SIGKILL);
				killed = true;
				break;
			}
			if (poll(&pfd, 1, 200) <= 0) continue;
		}
		dst  = (out && used + 1 < cap) ? out + used : scratch;
		room = (out && used + 1 < cap) ? cap - used - 1 : sizeof scratch;
		n = read(fd[0], dst, room);
		if (n <= 0) break;
		if (dst != scratch) used += (size_t)n;
	}
	if (out && cap) out[used] = '\0';
	close(fd[0]);
	if (waitpid(pid, &status, 0) < 0) return -1;
	if (killed) return -2;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
