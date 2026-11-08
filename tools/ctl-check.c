/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's command channel (src/ctl.h), the part px2-open talks to.
 *
 *     make check-ctl
 *
 * The command lines, good and bad; a game's path split into its system's
 * folder and the file, and everything that is not a game under Roms turned
 * away (another folder, a relative path, "..", a link that leads out); the
 * system found by its folder in either case, the game by its launch path or
 * its disc folder; a core named by its name or by what the Core row says; and
 * the socket itself in a folder in /tmp: closed to others, a command in and
 * an answer out, and the port record read back.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../src/ctl.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static void check_parse(void)
{
	ctl_cmd c;
	char err[160];

	printf("  the command lines:\n");
	CHECK(ctl_parse("status", &c, err, sizeof err) && c.verb == CTL_STATUS, "status");
	CHECK(ctl_parse("quit\n", &c, err, sizeof err) && c.verb == CTL_QUIT, "quit with its newline");
	CHECK(ctl_parse("open\tpath=/mnt/SDCARD/Roms/NES/A b.nes\r\n", &c, err, sizeof err) &&
	      c.verb == CTL_OPEN && !strcmp(c.path, "/mnt/SDCARD/Roms/NES/A b.nes") &&
	      !c.fresh && !c.core[0], "open with a path with a space: '%s'", c.path);
	CHECK(ctl_parse("open\tfresh=1\tcore=parallel_n64\tpath=/x/y", &c, err, sizeof err) &&
	      c.fresh && !strcmp(c.core, "parallel_n64") && !strcmp(c.path, "/x/y"),
	      "open with every field, in another order");
	CHECK(ctl_parse("open\tpath=/x\tfresh=0", &c, err, sizeof err) && !c.fresh, "fresh=0");

	CHECK(!ctl_parse("", &c, err, sizeof err), "an empty line");
	CHECK(!ctl_parse("launch\tpath=/x", &c, err, sizeof err) && strstr(err, "launch"),
	      "an unknown command: '%s'", err);
	CHECK(!ctl_parse("open", &c, err, sizeof err) && strstr(err, "path"), "open without a path");
	CHECK(!ctl_parse("open\tpath=", &c, err, sizeof err), "open with an empty path");
	CHECK(!ctl_parse("open\tpath=/a\tpath=/b", &c, err, sizeof err), "path twice");
	CHECK(!ctl_parse("open\tpath=/a\tspeed=2", &c, err, sizeof err) && strstr(err, "speed"),
	      "an unknown field: '%s'", err);
	CHECK(!ctl_parse("open\t/mnt/SDCARD/Roms/NES/x.nes", &c, err, sizeof err),
	      "a path without its key");
	CHECK(!ctl_parse("open\tpath=/a\tfresh=yes", &c, err, sizeof err), "fresh=yes");
	CHECK(!ctl_parse("open\tpath=/a\tcore=", &c, err, sizeof err), "an empty core");
	CHECK(!ctl_parse("quit\tnow=1", &c, err, sizeof err), "quit with a field");
	CHECK(!ctl_parse("status\tx", &c, err, sizeof err), "status with a field");
	{
		char big[CTL_LINE + 10];

		memset(big, 'a', sizeof big - 1);
		big[sizeof big - 1] = '\0';
		memcpy(big, "open\tpath=/", 11);
		CHECK(!ctl_parse(big, &c, err, sizeof err), "a line too long");
	}
}

static void check_split(void)
{
	const char *R = "/mnt/SDCARD/Roms";
	char folder[256], file[600], err[160];

	printf("  a game's path:\n");
	CHECK(ctl_path_split(R, "/mnt/SDCARD/Roms/NES/ZyperOS Test.nes", folder, sizeof folder,
	                     file, sizeof file, err, sizeof err) &&
	      !strcmp(folder, "NES") && !strcmp(file, "ZyperOS Test.nes"), "a NES game: %s | %s",
	      folder, file);
	CHECK(ctl_path_split(R, "/mnt/SDCARD/Roms/Nintendo 64/Super Mario 64 (USA).zip", folder,
	                     sizeof folder, file, sizeof file, err, sizeof err) &&
	      !strcmp(folder, "Nintendo 64") && !strcmp(file, "Super Mario 64 (USA).zip"),
	      "a folder with a space");
	CHECK(ctl_path_split(R, "/mnt/SDCARD/Roms/PlayStation/Game (USA)/Game (USA).cue", folder,
	                     sizeof folder, file, sizeof file, err, sizeof err) &&
	      !strcmp(folder, "PlayStation") && !strcmp(file, "Game (USA)/Game (USA).cue"),
	      "a disc folder's game");
	CHECK(ctl_path_split(R, "/mnt/SDCARD/Roms/Ports/Test Port.sh", folder, sizeof folder,
	                     file, sizeof file, err, sizeof err) &&
	      !strcmp(folder, "Ports"), "a port");

	static const char *bad[] = {
		"NES/x.nes",                                 /* relative */
		"/etc/passwd",
		"/mnt/SDCARD/Saves/NES/x.srm",
		"/mnt/SDCARD/Romsx/NES/x.nes",               /* a folder beginning alike */
		"/mnt/SDCARD/Roms",
		"/mnt/SDCARD/Roms/",
		"/mnt/SDCARD/Roms/NES",                      /* a system, not a game */
		"/mnt/SDCARD/Roms/NES/",
		"/mnt/SDCARD/Roms/NES//x.nes",
		"/mnt/SDCARD/Roms/../ZyperOS/launch.sh",
		"/mnt/SDCARD/Roms/NES/../../ZyperOS/launch.sh",
		"/mnt/SDCARD/Roms/./NES/x.nes",
		"/mnt/SDCARD/Roms/NES/..",
		"/mnt/SDCARD/Roms/NES/x\n.nes",
		"/mnt/SDCARD/Roms/NES/x\t.nes",
	};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
		CHECK(!ctl_path_split(R, bad[i], folder, sizeof folder, file, sizeof file, err,
		                      sizeof err) && err[0], "taken: '%s'", bad[i]);
	/* ...and a dot in a name is a name */
	CHECK(ctl_path_split(R, "/mnt/SDCARD/Roms/NES/..hidden.nes", folder, sizeof folder, file,
	                     sizeof file, err, sizeof err), "a name beginning with two dots");
}

static void check_real(void)
{
	char root[] = "/tmp/ctl-check-XXXXXX", roms[300], p[400], out[400], err[160];

	printf("  the path on the card:\n");
	if (!mkdtemp(root)) { CHECK(0, "no folder in /tmp"); return; }
	snprintf(roms, sizeof roms, "%s/Roms", root);
	mkdir(roms, 0755);
	snprintf(p, sizeof p, "%s/NES", roms);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/NES/game.nes", roms);
	fclose(fopen(p, "w"));
	snprintf(out, sizeof out, "%s/secret", root);
	fclose(fopen(out, "w"));

	CHECK(ctl_path_real(roms, p, err, sizeof err), "a game that is there: %s", err);
	snprintf(p, sizeof p, "%s/NES/missing.nes", roms);
	CHECK(!ctl_path_real(roms, p, err, sizeof err) && strstr(err, "no such"), "a game not there");
	snprintf(p, sizeof p, "%s/NES/link.nes", roms);
	CHECK(symlink(out, p) == 0, "no link made");
	CHECK(!ctl_path_real(roms, p, err, sizeof err) && strstr(err, "out of"),
	      "a link out of Roms: '%s'", err);
	unlink(p);
	unlink(out);
	snprintf(p, sizeof p, "%s/NES/game.nes", roms);
	unlink(p);
	snprintf(p, sizeof p, "%s/NES", roms);
	rmdir(p);
	rmdir(roms);
	rmdir(root);
}

static void check_find(void)
{
	const char *folders[] = { NULL, "NES", "Nintendo 64", "Ports", "nes" };
	const char *files[] = { "Alpha.nes", "Game (USA)/Game (USA).cue", "beta.NES",
	                        "Deep/Er/x.cue", NULL };

	printf("  the system and the game:\n");
	CHECK(ctl_find_system("NES", folders, 5) == 1, "NES");
	CHECK(ctl_find_system("nes", folders, 5) == 4, "nes, its own folder first");
	CHECK(ctl_find_system("NINTENDO 64", folders, 5) == 2, "a folder in another case");
	CHECK(ctl_find_system("Saturn", folders, 5) == -1, "a folder no shelf has");
	CHECK(ctl_find_system("", folders, 5) == -1, "nothing");

	CHECK(ctl_find_game("Alpha.nes", files, 5) == 0, "a file");
	CHECK(ctl_find_game("BETA.nes", files, 5) == 2, "a file in another case");
	CHECK(ctl_find_game("Game (USA)/Game (USA).cue", files, 5) == 1, "a disc folder's cue");
	CHECK(ctl_find_game("Game (USA)", files, 5) == 1, "a disc folder by its folder");
	CHECK(ctl_find_game("Deep", files, 5) == -1, "a folder two deep is no disc folder");
	CHECK(ctl_find_game("Alpha", files, 5) == -1, "a name without its extension");
	CHECK(ctl_find_game("Gamma.nes", files, 5) == -1, "a game not on the shelf");

	printf("  the core:\n");
	CHECK(ctl_core_is("flycast2021", "flycast2021"), "a libretro core by its name");
	CHECK(!ctl_core_is("flycast", "flycast2021"), "a part of a name");
	CHECK(ctl_core_is("Flycast", "run:Emulators/Flycast/Flycast.sh"), "a script by its row");
	CHECK(ctl_core_is("flycast", "run:Emulators/Flycast/Flycast.sh"), "...in any case");
	CHECK(ctl_core_is("run:Emulators/Flycast/Flycast.sh", "run:Emulators/Flycast/Flycast.sh"),
	      "a script by its whole name");
	CHECK(!ctl_core_is("Emulators", "run:Emulators/Flycast/Flycast.sh"), "a script's folder");
	CHECK(!ctl_core_is("", "fceumm"), "no name");
}

/* One command through the socket, as px2-open sends it */
static int send_line(const char *dir, const char *line)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	snprintf(sa.sun_path, sizeof sa.sun_path, "%s/%s", dir, CTL_SOCK_NAME);
	if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
		if (fd >= 0) close(fd);
		return -1;
	}
	if (write(fd, line, strlen(line)) < 0) { close(fd); return -1; }
	return fd;
}

static void check_socket(void)
{
	char root[] = "/tmp/ctl-sock-XXXXXX", dir[300], line[CTL_LINE], ans[256], sock[400];
	struct stat st;
	int lfd, cfd, fd;
	ssize_t r;

	printf("  the socket:\n");
	if (!mkdtemp(root)) { CHECK(0, "no folder in /tmp"); return; }
	snprintf(dir, sizeof dir, "%s/zyperos", root);
	lfd = ctl_listen(dir);
	CHECK(lfd >= 0, "no socket");
	if (lfd < 0) return;
	CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700, "the folder is %o",
	      (unsigned)(st.st_mode & 0777));
	snprintf(sock, sizeof sock, "%s/%s", dir, CTL_SOCK_NAME);
	CHECK(stat(sock, &st) == 0 && (st.st_mode & 0777) == 0600, "the socket is %o",
	      (unsigned)(st.st_mode & 0777));
	CHECK(ctl_take(lfd, line, sizeof line) < 0, "a command with nobody there");

	fd = send_line(dir, "status\n");
	CHECK(fd >= 0, "no connection");
	cfd = ctl_take(lfd, line, sizeof line);
	CHECK(cfd >= 0 && !strcmp(line, "status"), "the line read: '%s'", line);
	ctl_answer(cfd, "ok\tstate=%s", "shelf");
	r = fd >= 0 ? read(fd, ans, sizeof ans - 1) : -1;
	ans[r > 0 ? r : 0] = '\0';
	CHECK(!strcmp(ans, "ok\tstate=shelf\n"), "the answer: '%s'", ans);
	if (fd >= 0) close(fd);

	/* A peer that says nothing costs the read's timeout, and no command */
	fd = send_line(dir, "");
	CHECK(ctl_take(lfd, line, sizeof line) < 0, "a command from a silent peer");
	if (fd >= 0) close(fd);

	/* An asker that gave up and closed before the launcher read it: its
	 * command is dropped, not done late */
	fd = send_line(dir, "quit\n");
	if (fd >= 0) close(fd);
	CHECK(ctl_take(lfd, line, sizeof line) < 0, "a command from an asker gone: '%s'", line);

	/* ...and one gone between its command and the answer: the answer goes
	 * nowhere, and no SIGPIPE ends the launcher (this check with it) */
	fd = send_line(dir, "status\n");
	cfd = ctl_take(lfd, line, sizeof line);
	CHECK(cfd >= 0, "no command");
	if (fd >= 0) close(fd);
	ctl_answer(cfd, "ok\tstate=%s", "shelf");
	ctl_answer(-1, "ok");

	/* A second listener takes the place of the first's socket (a launcher
	 * started again after a crash left one) */
	{
		int l2 = ctl_listen(dir);

		CHECK(l2 >= 0, "no socket over a stale one");
		ctl_shut(l2, dir);
	}
	ctl_shut(lfd, dir);
	CHECK(access(sock, F_OK) != 0, "the socket is still there");

	/* The port record */
	{
		int pid;
		long since;
		char path[CTL_PATH];

		CHECK(!ctl_port_read(dir, &pid, &since, path, sizeof path), "a record before one");
		CHECK(ctl_port_write(dir, 1234, 1762600000L, "/mnt/SDCARD/Roms/Ports/Test Port.sh"),
		      "no record written");
		CHECK(ctl_port_read(dir, &pid, &since, path, sizeof path) && pid == 1234 &&
		      since == 1762600000L && !strcmp(path, "/mnt/SDCARD/Roms/Ports/Test Port.sh"),
		      "the record read back: %d %ld %s", pid, since, path);
		ctl_port_clear(dir);
		CHECK(!ctl_port_read(dir, &pid, &since, path, sizeof path), "a record after its clear");
	}

	/* Not a folder of its own: a link where the folder goes */
	rmdir(dir);
	CHECK(symlink("/tmp", dir) == 0, "no link made");
	lfd = ctl_listen(dir);
	CHECK(lfd < 0, "a socket through a link to another folder");
	ctl_shut(lfd, dir);
	unlink(dir);
	rmdir(root);
}

int main(void)
{
	printf("ctl-check:\n");
	check_parse();
	check_split();
	check_real();
	check_find();
	check_socket();
	if (failures) {
		printf("ctl-check: %d failed\n", failures);
		return 1;
	}
	printf("ctl-check: all passed\n");
	return 0;
}
