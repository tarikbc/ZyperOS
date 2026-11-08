/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's command channel: see ctl.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* struct ucred */
#endif
#include "ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define CTL_NOSIGPIPE MSG_NOSIGNAL
#else
#define CTL_NOSIGPIPE 0      /* the Mac's host check: SO_NOSIGPIPE, in ctl_take */
#endif

const char *ctl_dir(void)
{
	const char *e = getenv("ZYPEROS_CTL_DIR");

	return e && *e ? e : CTL_DIR;
}

static void say(char *err, size_t en, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(char *err, size_t en, const char *fmt, ...)
{
	va_list ap;

	if (!err || !en) return;
	va_start(ap, fmt);
	vsnprintf(err, en, fmt, ap);
	va_end(ap);
}

static bool has_control(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 0x20 || *s == 0x7f) return true;
	return false;
}

bool ctl_parse(const char *line, ctl_cmd *c, char *err, size_t en)
{
	char buf[CTL_LINE], *f, *save = NULL;
	size_t n;
	bool got_path = false, got_fresh = false, got_core = false;

	memset(c, 0, sizeof *c);
	n = strlen(line);
	if (n >= sizeof buf) { say(err, en, "the line is too long"); return false; }
	memcpy(buf, line, n + 1);
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

	f = strtok_r(buf, "\t", &save);
	if (!f) { say(err, en, "an empty line"); return false; }
	if (!strcmp(f, "open")) c->verb = CTL_OPEN;
	else if (!strcmp(f, "quit")) c->verb = CTL_QUIT;
	else if (!strcmp(f, "status")) c->verb = CTL_STATUS;
	else { say(err, en, "no command '%.40s' (open, quit, status)", f); return false; }

	while ((f = strtok_r(NULL, "\t", &save))) {
		char *eq = strchr(f, '=');
		const char *v;

		if (c->verb != CTL_OPEN) {
			say(err, en, "%s takes nothing more", c->verb == CTL_QUIT ? "quit" : "status");
			return false;
		}
		if (!eq) { say(err, en, "'%.40s' is not a key=value field", f); return false; }
		*eq = '\0';
		v = eq + 1;
		if (!strcmp(f, "path")) {
			if (got_path) { say(err, en, "path twice"); return false; }
			got_path = true;
			if (strlen(v) >= sizeof c->path) { say(err, en, "the path is too long"); return false; }
			snprintf(c->path, sizeof c->path, "%s", v);
		} else if (!strcmp(f, "fresh")) {
			if (got_fresh) { say(err, en, "fresh twice"); return false; }
			got_fresh = true;
			if (strcmp(v, "0") && strcmp(v, "1")) { say(err, en, "fresh is 0 or 1"); return false; }
			c->fresh = v[0] == '1';
		} else if (!strcmp(f, "core")) {
			if (got_core) { say(err, en, "core twice"); return false; }
			got_core = true;
			if (!*v || strlen(v) >= sizeof c->core) { say(err, en, "no core name"); return false; }
			snprintf(c->core, sizeof c->core, "%s", v);
		} else {
			say(err, en, "open has no field '%.40s' (path, fresh, core)", f);
			return false;
		}
	}
	if (c->verb == CTL_OPEN && !c->path[0]) { say(err, en, "open needs a path"); return false; }
	return true;
}

bool ctl_path_split(const char *roms, const char *path, char *folder, size_t fn,
                    char *file, size_t filen, char *err, size_t en)
{
	size_t rl = strlen(roms);
	const char *rest, *p, *slash;

	if (path[0] != '/') { say(err, en, "not an absolute path"); return false; }
	if (has_control(path)) { say(err, en, "a control character in the path"); return false; }
	if (strncmp(path, roms, rl) != 0 || path[rl] != '/') {
		say(err, en, "not under %s", roms);
		return false;
	}
	rest = path + rl + 1;
	/* Every part a name: nothing empty (//, a / at the end), no . or .. */
	for (p = rest;;) {
		size_t k = strcspn(p, "/");

		if (k == 0) { say(err, en, "an empty part in the path"); return false; }
		if ((k == 1 && p[0] == '.') || (k == 2 && p[0] == '.' && p[1] == '.')) {
			say(err, en, "a . or .. in the path");
			return false;
		}
		if (!p[k]) break;
		p += k + 1;
	}
	slash = strchr(rest, '/');
	if (!slash) { say(err, en, "a system's folder, not a game in it"); return false; }
	if ((size_t)(slash - rest) >= fn || strlen(slash + 1) >= filen) {
		say(err, en, "the path is too long");
		return false;
	}
	snprintf(folder, fn, "%.*s", (int)(slash - rest), rest);
	snprintf(file, filen, "%s", slash + 1);
	return true;
}

bool ctl_path_real(const char *roms, const char *path, char *err, size_t en)
{
	char rr[PATH_MAX], rp[PATH_MAX];
	size_t rl;

	if (access(path, F_OK) != 0) { say(err, en, "no such file"); return false; }
	if (!realpath(roms, rr)) { say(err, en, "no %s", roms); return false; }
	if (!realpath(path, rp)) { say(err, en, "cannot follow the path"); return false; }
	rl = strlen(rr);
	if (strncmp(rp, rr, rl) != 0 || rp[rl] != '/') {
		say(err, en, "it leads out of %s", roms);
		return false;
	}
	return true;
}

int ctl_find_system(const char *folder, const char *const *folders, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (folders[i] && !strcmp(folders[i], folder)) return i;
	for (i = 0; i < n; i++)
		if (folders[i] && !strcasecmp(folders[i], folder)) return i;
	return -1;
}

int ctl_find_game(const char *file, const char *const *files, int n)
{
	size_t fl = strlen(file);
	int i;

	for (i = 0; i < n; i++)
		if (files[i] && !strcmp(files[i], file)) return i;
	for (i = 0; i < n; i++)
		if (files[i] && !strcasecmp(files[i], file)) return i;
	/* A disc folder's game, named by its folder: "Game" for "Game/Game.cue" */
	for (i = 0; i < n; i++)
		if (files[i] && !strncasecmp(files[i], file, fl) && files[i][fl] == '/' &&
		    !strchr(files[i] + fl + 1, '/'))
			return i;
	return -1;
}

bool ctl_core_is(const char *want, const char *core)
{
	const char *b, *dot;
	size_t k;

	if (!*want) return false;
	if (!strcmp(want, core)) return true;
	if (strncmp(core, "run:", 4) != 0) return false;
	b = strrchr(core, '/');
	b = b ? b + 1 : core + 4;
	dot = strrchr(b, '.');
	k = dot ? (size_t)(dot - b) : strlen(b);
	return strlen(want) == k && !strncasecmp(want, b, k);
}

static bool sock_path(const char *dir, struct sockaddr_un *sa)
{
	memset(sa, 0, sizeof *sa);
	sa->sun_family = AF_UNIX;
	return (size_t)snprintf(sa->sun_path, sizeof sa->sun_path, "%s/%s", dir, CTL_SOCK_NAME) <
	       sizeof sa->sun_path;
}

int ctl_listen(const char *dir)
{
	struct sockaddr_un sa;
	struct stat st;
	int fd;

	if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
		fprintf(stderr, "ctl: cannot make %s: %s\n", dir, strerror(errno));
		return -1;
	}
	/* This user's folder alone, and a folder: never one another user made
	 * first, or a link to somewhere else */
	if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
		fprintf(stderr, "ctl: %s is not a folder of this user's\n", dir);
		return -1;
	}
	if ((st.st_mode & 077) && chmod(dir, 0700) != 0) {
		fprintf(stderr, "ctl: cannot close %s to others\n", dir);
		return -1;
	}
	if (!sock_path(dir, &sa)) return -1;
	unlink(sa.sun_path);
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || chmod(sa.sun_path, 0600) != 0 ||
	    listen(fd, 4) != 0) {
		fprintf(stderr, "ctl: cannot listen on %s: %s\n", sa.sun_path, strerror(errno));
		close(fd);
		unlink(sa.sun_path);
		return -1;
	}
	return fd;
}

static bool peer_ok(int cfd)
{
#ifdef SO_PEERCRED
	struct ucred uc;
	socklen_t l = sizeof uc;

	if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &uc, &l) != 0) return false;
	return uc.uid == geteuid();
#else
	uid_t u;
	gid_t g;

	if (getpeereid(cfd, &u, &g) != 0) return false;
	return u == geteuid();
#endif
}

int ctl_take(int lfd, char *line, size_t n)
{
	struct timeval tv = { 0, 300 * 1000 };
	size_t got = 0;
	int cfd;

	if (lfd < 0 || n < 2) return -1;
	if ((cfd = accept(lfd, NULL, NULL)) < 0) return -1;
	fcntl(cfd, F_SETFD, FD_CLOEXEC);
	/* Blocking from here, each way a moment at most (accept's descriptor
	 * takes the listening one's flags on some systems, not on Linux) */
	fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) & ~O_NONBLOCK);
	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
	{
		int one = 1;

		setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
	}
#endif
	if (!peer_ok(cfd)) {
		ctl_answer(cfd, "error\tcode=denied\tmsg=root only");
		return -1;
	}
	while (got + 1 < n) {
		ssize_t r = read(cfd, line + got, n - 1 - got);

		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		got += (size_t)r;
		if (memchr(line + got - (size_t)r, '\n', (size_t)r)) break;
	}
	line[got] = '\0';
	if (!got) { close(cfd); return -1; }
	if (!strchr(line, '\n') && got + 1 >= n) {
		ctl_answer(cfd, "error\tcode=bad-command\tmsg=the line is too long");
		return -1;
	}
	line[strcspn(line, "\r\n")] = '\0';
	/* An asker that gave up waiting (the launcher was busy, in a launch or a
	 * screen that does not read the channel) has closed its end: its command
	 * is not done late, when nobody is there to hear what came of it */
	{
		char c;

		if (recv(cfd, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
			fprintf(stderr, "ctl: '%s' dropped, its asker gone\n", line);
			close(cfd);
			return -1;
		}
	}
	return cfd;
}

void ctl_answer(int cfd, const char *fmt, ...)
{
	char buf[CTL_LINE];
	va_list ap;
	int k;

	if (cfd < 0) return;
	va_start(ap, fmt);
	k = vsnprintf(buf, sizeof buf - 1, fmt, ap);
	va_end(ap);
	if (k < 0) k = 0;
	if (k > (int)sizeof buf - 2) k = (int)sizeof buf - 2;
	buf[k++] = '\n';
	{
		const char *p = buf;
		size_t left = (size_t)k;

		while (left) {
			/* No SIGPIPE for an asker gone in the meantime: it would end
			 * the launcher */
			ssize_t w = send(cfd, p, left, CTL_NOSIGPIPE);

			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) break;
			p += w;
			left -= (size_t)w;
		}
	}
	close(cfd);
}

void ctl_shut(int lfd, const char *dir)
{
	struct sockaddr_un sa;

	if (lfd < 0) return;
	close(lfd);
	if (sock_path(dir, &sa)) unlink(sa.sun_path);
}

static void port_file(const char *dir, char *out, size_t n)
{
	snprintf(out, n, "%s/%s", dir, CTL_PORT_NAME);
}

bool ctl_port_write(const char *dir, int pid, long since, const char *path)
{
	char p[CTL_PATH], tmp[CTL_PATH + 8];
	FILE *f;
	bool ok;

	port_file(dir, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.tmp", p);
	if (!(f = fopen(tmp, "w"))) return false;
	ok = fprintf(f, "%d\t%ld\t%s\n", pid, since, path) > 0;
	if (fclose(f) != 0) ok = false;
	if (!ok || rename(tmp, p) != 0) { unlink(tmp); return false; }
	return true;
}

bool ctl_port_read(const char *dir, int *pid, long *since, char *path, size_t pn)
{
	char p[CTL_PATH], line[CTL_LINE], *a, *b;
	FILE *f;

	port_file(dir, p, sizeof p);
	if (!(f = fopen(p, "r"))) return false;
	if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
	fclose(f);
	line[strcspn(line, "\r\n")] = '\0';
	if (!(a = strchr(line, '\t')) || !(b = strchr(a + 1, '\t'))) return false;
	*pid = atoi(line);
	*since = atol(a + 1);
	snprintf(path, pn, "%s", b + 1);
	return *pid > 0;
}

void ctl_port_clear(const char *dir)
{
	char p[CTL_PATH];

	port_file(dir, p, sizeof p);
	unlink(p);
}
