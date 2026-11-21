/* SPDX-License-Identifier: GPL-3.0-only */
/* initns: a command in init's mount namespace, for tools/px2-deploy.sh.
 *
 *   initns COMMAND [ARGS...]
 *
 * On a release image, Developer Mode's SSH runs in a mount namespace of its
 * own (src/sshlink.c, spawn), so a bind mount made in it is seen in it alone:
 * the launcher, a port and what init starts again go on seeing the image's
 * own file. BusyBox has no nsenter. This is the one call nsenter would make,
 * setns(2) on /proc/1/ns/mnt, then the command, which then sees (and makes)
 * the mounts the rest of the device has. A development image's SSH is in
 * init's namespace already, and needs none of it.
 *
 * tools/px2-deploy.sh's stand-ins for system files (--system) are bound with
 * it, from /tmp, so they go at the next restart. Built with the programs
 * (make px2-programs) and copied to /tmp by a deploy that needs it: never on
 * the card, never in the image. Root only, as setns(2) is. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: initns COMMAND [ARGS...]\n");
		return 2;
	}
	fd = open("/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
	if (fd < 0 || setns(fd, CLONE_NEWNS) != 0) {
		fprintf(stderr, "initns: init's mount namespace: %s\n", strerror(errno));
		return 1;
	}
	close(fd);
	/* setns put the root and the working folder at the namespace's root */
	if (chdir("/") != 0) {
		fprintf(stderr, "initns: /: %s\n", strerror(errno));
		return 1;
	}
	execvp(argv[1], argv + 1);
	fprintf(stderr, "initns: %s: %s\n", argv[1], strerror(errno));
	return 127;
}
