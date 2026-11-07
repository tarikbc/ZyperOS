/* SPDX-License-Identifier: GPL-3.0-only */
/* See sshlink.h for what this is and what keeps it safe. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sshlink.h"

#include "i18n.h"

/* ---- what dropbear says ------------------------------------------------- *
 *
 * dropbear -E logs one line per event, the master's and every connection's
 * alike, down the one pipe: "[pid] Nov 05 16:00:00 message", or "[pid]
 * 1793896919 message" when the date will not format (svr-session.c). Only a
 * message's START is compared. 2026.91 prints an account's own name there
 * (from /etc/passwd), never the text a client sent, but what follows the
 * start is no business of this count either way. The messages
 * are dropbear 2026.91's own (svr-authpasswd.c, svr-authpubkey.c,
 * svr-session.c, svr-main.c, netio.c). "Exit (user)" is said only by a
 * connection that signed in; one that did not says "Exit before auth". */
static const char *message_of(const char *line)
{
	const char *p = strstr(line, "] ");
	int skip, i;

	if (line[0] != '[' || !p) return NULL;
	p += 2;
	/* the date: three words, or one when it is the epoch's seconds */
	skip = (*p >= '0' && *p <= '9') ? 1 : 3;
	for (i = 0; i < skip; i++) {
		p = strchr(p, ' ');
		if (!p) return NULL;
		p++;
	}
	return p;
}

#define STARTS(s, lit) (!strncmp((s), lit, sizeof lit - 1))

void sshlink_feed(sshlink_count *c, const char *line)
{
	const char *m = message_of(line);

	if (!m) return;
	if (STARTS(m, "Bad password attempt for "))
		c->bad++;
	else if (STARTS(m, "Password auth succeeded for ") || STARTS(m, "Pubkey auth succeeded for "))
		c->sessions++;
	else if (STARTS(m, "Exit (")) {
		if (c->sessions > 0) c->sessions--;
	} else if (STARTS(m, "Failed listening on ") || STARTS(m, "Error listening"))
		c->failed = true;                  /* then "Early exit", and it ends */
}

/* Root's line with its password field replaced, and every other account's
 * password field "*": over the cable only root signs in, and only with the
 * PIN or a key, whatever a system account's line says in the image. The
 * other fields stay as they were. */
bool sshlink_shadow(const char *shadow, const char *hash, char *out, size_t n)
{
	const char *p = shadow;
	size_t used = 0;
	bool root = false;

	if (!n) return false;
	out[0] = '\0';
	while (*p) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		int w;

		if (len >= 5 && !strncmp(p, "root:", 5)) {
			const char *rest = memchr(p + 5, ':', len - 5);

			/* root:<hash>:<the aging fields as they were> */
			w = snprintf(out + used, n - used, "root:%s%.*s\n", hash,
			             rest ? (int)(len - (size_t)(rest - p)) : 0, rest ? rest : "");
			root = true;
		} else {
			const char *c1 = memchr(p, ':', len);
			const char *c2 = c1 ? memchr(c1 + 1, ':', len - (size_t)(c1 + 1 - p)) : NULL;

			if (c1 && c2)
				w = snprintf(out + used, n - used, "%.*s*%.*s\n", (int)(c1 + 1 - p), p,
				             (int)(len - (size_t)(c2 - p)), c2);
			else
				w = snprintf(out + used, n - used, "%.*s\n", (int)len, p);
		}
		if (w < 0 || (size_t)w >= n - used) return false;
		used += (size_t)w;
		p += len;
		if (*p == '\n') p++;
	}
	return root;
}

#ifdef __linux__

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DROPBEAR     "/usr/sbin/dropbear"
#define DROPBEARKEY  "/usr/bin/dropbearkey"
#define RUN_DIR      "/tmp/zyperos-ssh"
#define RUN_SHADOW   RUN_DIR "/shadow"
#define RUN_HOME     RUN_DIR "/home"
#define RUN_SSH      RUN_HOME "/.ssh"
#define HOST_KEY     "dropbear_ed25519_host_key"
#define RUN_NS       "/tmp/zyperos-ssh.ns"   /* outside RUN_DIR: see learn_ns */
#define SHADOW_MAX   8192

typedef enum { ST_OFF, ST_ON, ST_DEV, ST_LOCKED, ST_FAILED, ST_RADIO } ssh_state;

static ssh_state     g_st = ST_OFF;
static pid_t         g_pid = -1;      /* the dropbear master */
static int           g_fd = -1;       /* its stderr, and every connection's */
static char          g_ns[64];        /* its mount namespace, "mnt:[...]" */
static sshlink_count g_c;
static char          g_line[512];
static size_t        g_ll;
static char          g_status[32];
static unsigned      g_radio_at;      /* when radio_up was last asked */
static int           g_web_bad;       /* wrong PINs on the page (sshlink_note_web) */

static void sleep_ms(long ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static bool rand_bytes(void *out, size_t n)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	bool ok;

	if (fd < 0) return false;
	ok = read(fd, out, n) == (ssize_t)n;
	close(fd);
	return ok;
}

/* The PIN as SHA-512 crypt, from the libcrypt dropbear itself checks with.
 * Opened here rather than linked: the launcher needs it only on a device
 * whose dropbear has it, and builds on hosts that have none. */
static bool pin_hash(const char *pin, char *out, size_t n)
{
	static const char abc[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	unsigned char r[16];
	char salt[3 + 16 + 1];
	char *(*crypt_fn)(const char *, const char *);
	const char *h;
	void *lib = dlopen("libcrypt.so.2", RTLD_NOW);
	int i;

	if (!lib) lib = dlopen("libcrypt.so.1", RTLD_NOW);
	if (!lib || !rand_bytes(r, sizeof r)) return false;
	*(void **)&crypt_fn = dlsym(lib, "crypt");
	if (!crypt_fn) return false;
	memcpy(salt, "$6$", 3);
	for (i = 0; i < 16; i++) salt[3 + i] = abc[r[i] % 64];
	salt[19] = '\0';
	h = crypt_fn(pin, salt);
	if (!h || strncmp(h, "$6$", 3) || strlen(h) >= n) return false;
	snprintf(out, n, "%s", h);
	return true;
}

/* Something already listening on port 22, on any address: a development
 * image's dropbear, started at boot for every interface. /proc/net/tcp has
 * the IPv4 sockets and tcp6 the rest; state 0A is LISTEN. */
static bool port22_taken(void)
{
	static const char *const files[] = { "/proc/net/tcp", "/proc/net/tcp6" };
	int k;

	for (k = 0; k < 2; k++) {
		FILE *f = fopen(files[k], "re");
		char line[256];

		if (!f) continue;
		while (fgets(line, sizeof line, f)) {
			unsigned port, st;

			if (sscanf(line, " %*d: %*[0-9A-Fa-f]:%x %*s %x", &port, &st) == 2 &&
			    port == 22 && st == 0x0A) {
				fclose(f);
				return true;
			}
		}
		fclose(f);
	}
	return false;
}

static void clean_run(void)
{
	unlink(RUN_SHADOW);
	unlink(RUN_SSH);
	rmdir(RUN_HOME);
	rmdir(RUN_DIR);
}

/* A program run to its end, quietly: dropbearkey, once per card. */
static bool run_quiet(char *const argv[])
{
	pid_t pid = fork();
	int st, i;

	if (pid < 0) return false;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
		execv(argv[0], argv);
		_exit(127);
	}
	for (i = 0; i < 500; i++) {                   /* ed25519: well under 5 s */
		if (waitpid(pid, &st, WNOHANG) == pid)
			return WIFEXITED(st) && WEXITSTATUS(st) == 0;
		sleep_ms(10);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	return false;
}

/* /tmp/zyperos-ssh: the shadow with the PIN's hash, and a home whose .ssh is
 * the card's key folder. The host key is made there the first time. */
static bool prepare(const char *pin, const char *keydir, char *key, size_t kn)
{
	char *shadow = NULL, *built = NULL;
	char hash[160];
	ssize_t got;
	bool ok = false;
	int fd;

	clean_run();
	if (mkdir(RUN_DIR, 0700) != 0 || mkdir(RUN_HOME, 0700) != 0) {
		fprintf(stderr, "ssh: could not make %s: %s\n", RUN_DIR, strerror(errno));
		return false;
	}
	if (mkdir(keydir, 0700) != 0 && errno != EEXIST) {
		fprintf(stderr, "ssh: could not make %s: %s\n", keydir, strerror(errno));
		return false;
	}
	if (symlink(keydir, RUN_SSH) != 0) return false;

	snprintf(key, kn, "%s/%s", keydir, HOST_KEY);
	if (access(key, R_OK) != 0) {
		/* Made beside it and renamed into place, so a power cut part way
		 * leaves no half a key that dropbear would refuse at every start */
		char tmp[640];
		char *argv[] = { (char *)DROPBEARKEY, (char *)"-t", (char *)"ed25519",
		                 (char *)"-f", tmp, NULL };

		char tmppub[660], pub[620];

		snprintf(tmp, sizeof tmp, "%s.new", key);
		snprintf(tmppub, sizeof tmppub, "%s.pub", tmp);
		snprintf(pub, sizeof pub, "%s.pub", key);
		unlink(tmp);
		if (!run_quiet(argv) || rename(tmp, key) != 0) {
			unlink(tmp);
			unlink(tmppub);
			fprintf(stderr, "ssh: no host key at %s\n", key);
			return false;
		}
		/* dropbearkey writes the public half beside it, as <file>.pub */
		if (rename(tmppub, pub) != 0) unlink(tmppub);
		fprintf(stderr, "ssh: made the host key %s\n", key);
	}

	/* Without a hash the PIN cannot sign in; keys still can, and root's
	 * password field says no password at all ("*", never empty). */
	if (!pin_hash(pin, hash, sizeof hash)) {
		fprintf(stderr, "ssh: no libcrypt, so keys only\n");
		snprintf(hash, sizeof hash, "*");
	}
	shadow = malloc(SHADOW_MAX);
	built = malloc(SHADOW_MAX + sizeof hash);
	fd = open("/etc/shadow", O_RDONLY | O_CLOEXEC);
	if (!shadow || !built || fd < 0) goto out;
	got = read(fd, shadow, SHADOW_MAX - 1);
	close(fd);
	if (got <= 0) goto out;
	shadow[got] = '\0';
	if (!sshlink_shadow(shadow, hash, built, SHADOW_MAX + sizeof hash)) goto out;
	fd = open(RUN_SHADOW, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) goto out;
	ok = write(fd, built, strlen(built)) == (ssize_t)strlen(built);
	close(fd);
out:
	if (built) memset(built, 0, SHADOW_MAX + sizeof hash);
	memset(hash, 0, sizeof hash);
	free(shadow);
	free(built);
	if (!ok) fprintf(stderr, "ssh: could not write %s\n", RUN_SHADOW);
	return ok;
}

/* dropbear, in a mount namespace of its own. Between fork and exec only
 * system calls: the launcher has threads, and nothing else is safe there. */
static pid_t spawn(const char *listen, const char *key, int errfd)
{
	static char *const env[] = { (char *)"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
	struct sigaction dfl;
	struct rlimit lim;
	sigset_t none;
	pid_t pid;
	int null, maxfd, sig;

	sigemptyset(&none);
	memset(&dfl, 0, sizeof dfl);
	dfl.sa_handler = SIG_DFL;
	maxfd = getrlimit(RLIMIT_NOFILE, &lim) == 0 && lim.rlim_cur != RLIM_INFINITY &&
	        lim.rlim_cur < 65536 ? (int)lim.rlim_cur : 65536;
	pid = fork();
	if (pid != 0) return pid;

	/* The child. It dies with the launcher, whatever ends it. */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	if (getppid() == 1) _exit(1);
	null = open("/dev/null", O_RDWR);
	if (null < 0) _exit(1);
	dup2(null, 0);
	dup2(null, 1);
	dup2(errfd, 2);
	/* Nothing of the launcher's: its display, its sockets, its files */
#ifdef SYS_close_range
	if (syscall(SYS_close_range, 3, ~0U, 0) != 0)
#endif
	{
		int fd;

		for (fd = 3; fd < maxfd; fd++) close(fd);
	}
	/* Every signal as a fresh process has it: an ignored one stays ignored
	 * across exec, and the shells dropbear starts would inherit that */
	sigprocmask(SIG_SETMASK, &none, NULL);
	for (sig = 1; sig < NSIG; sig++)
		if (sig != SIGKILL && sig != SIGSTOP) sigaction(sig, &dfl, NULL);
	/* Not the launcher's folder, which could hold the card busy */
	if (chdir("/") != 0) _exit(1);
	if (unshare(CLONE_NEWNS) != 0) _exit(2);
	/* Private, so the binds below stay in here */
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) _exit(3);
	if (mount(RUN_SHADOW, "/etc/shadow", NULL, MS_BIND, NULL) != 0) _exit(4);
	if (mount(RUN_HOME, "/root", NULL, MS_BIND, NULL) != 0) _exit(5);
	execle(DROPBEAR, "dropbear", "-F", "-E", "-p", listen, "-r", key,
	       "-T", "3", (char *)NULL, env);
	_exit(127);
}

/* A radio on the device: any network interface with an 802.11 side. On the
 * Pixel 2 there is none while the cable is a computer's (the one USB port
 * carries the adapter or the cable, never both). Where there is one, SSH does
 * not start, and stops if one comes up: dropbear binds the cable's address,
 * but Linux answers a packet for any of its addresses on any interface, so a
 * stranger on the same Wi-Fi who routes 10.42.0.1 at the device would reach
 * it. Without netfilter in the kernel, staying off is the guard. */
static bool radio_up(void)
{
	DIR *d = opendir("/sys/class/net");
	struct dirent *e;
	bool up = false;

	if (!d) return false;
	while (!up && (e = readdir(d))) {
		char p[300];

		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name);
		if (access(p, F_OK) == 0) up = true;
		snprintf(p, sizeof p, "/sys/class/net/%s/phy80211", e->d_name);
		if (access(p, F_OK) == 0) up = true;
	}
	closedir(d);
	return up;
}

/* Every process in dropbear's mount namespace: the master, each connection
 * (each its own session, setsid by dropbear, so not in any one group) and
 * every shell they started. The kernel here has mount namespaces and no PID
 * ones, so this walk, not an init dying, is what ends them all. */
static int kill_ns(int sig)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int n = 0;

	if (!d || !g_ns[0]) { if (d) closedir(d); return 0; }
	while ((e = readdir(d))) {
		char path[300], ns[64];
		ssize_t len;

		if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof path, "/proc/%s/ns/mnt", e->d_name);
		len = readlink(path, ns, sizeof ns - 1);
		if (len <= 0) continue;
		ns[len] = '\0';
		if (!strcmp(ns, g_ns)) {
			kill((pid_t)atoi(e->d_name), sig);
			n++;
		}
	}
	closedir(d);
	return n;
}

/* dropbear's namespace, once it is in one: the moment after fork it is still
 * the launcher's. Kept in RUN_NS too, outside RUN_DIR, so a launcher that
 * died with sessions open finds them on its next start (sshlink_stop). */
static void learn_ns(void)
{
	char path[64], ns[64], own[64];
	ssize_t len, ol;
	int fd;

	if (g_ns[0] || g_pid <= 0) return;
	snprintf(path, sizeof path, "/proc/%d/ns/mnt", (int)g_pid);
	len = readlink(path, ns, sizeof ns - 1);
	ol = readlink("/proc/self/ns/mnt", own, sizeof own - 1);
	if (len <= 0 || ol <= 0) return;
	ns[len] = '\0';
	own[ol] = '\0';
	if (!strcmp(ns, own)) return;
	snprintf(g_ns, sizeof g_ns, "%s", ns);
	fd = open(RUN_NS, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		if (write(fd, g_ns, strlen(g_ns)) < 0) { /* the walk still has g_ns */ }
		close(fd);
	}
}

/* Everything stopped: the master, unless `reaped` says it is already gone,
 * and every process in its namespace whether or not the master lives - a
 * master that died leaves its connections running, and they are exactly
 * what must not outlive the screen. */
static void halt(ssh_state why, bool reaped)
{
	int i, st;

	if (g_pid > 0 && !reaped) {
		learn_ns();
		kill(g_pid, SIGTERM);
	}
	kill_ns(SIGTERM);
	if (g_pid > 0 && !reaped) {
		for (i = 0; i < 25; i++) {
			if (waitpid(g_pid, &st, WNOHANG) == g_pid) { reaped = true; break; }
			sleep_ms(20);
		}
		if (!reaped) {
			kill(g_pid, SIGKILL);
			waitpid(g_pid, &st, 0);
		}
	}
	g_pid = -1;
	/* A shell that ignored the TERM */
	if (kill_ns(0) > 0) { sleep_ms(50); kill_ns(SIGKILL); }
	if (g_fd >= 0) { close(g_fd); g_fd = -1; }
	g_ns[0] = '\0';
	unlink(RUN_NS);
	g_c.sessions = 0;
	clean_run();
	g_st = why;
}

/* No more PINs, at once: root's password field becomes "*" in the very file
 * dropbear's namespace reads, before anything is killed, so a connection
 * that looks root up from here on finds no password to guess. */
static void lock_pin(void)
{
	char *sh = malloc(SHADOW_MAX), *out = malloc(SHADOW_MAX + 8);
	int fd = open(RUN_SHADOW, O_RDWR | O_CLOEXEC);
	ssize_t got;

	if (sh && out && fd >= 0 && (got = read(fd, sh, SHADOW_MAX - 1)) > 0) {
		sh[got] = '\0';
		if (sshlink_shadow(sh, "*", out, SHADOW_MAX + 8) &&
		    ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0 &&
		    write(fd, out, strlen(out)) < 0) { /* halt follows anyway */ }
	}
	if (fd >= 0) close(fd);
	if (sh) memset(sh, 0, SHADOW_MAX);
	free(sh);
	free(out);
}

bool sshlink_start(const char *addr, const char *pin, const char *keydir)
{
	char listen[80], key[600];
	int fds[2], i;

	sshlink_stop();
	memset(&g_c, 0, sizeof g_c);
	g_ll = 0;
	g_ns[0] = '\0';
	g_radio_at = 0;
	g_web_bad = 0;
	if (access(DROPBEAR, X_OK) != 0) {
		fprintf(stderr, "ssh: no %s in this image\n", DROPBEAR);
		g_st = ST_FAILED;
		return false;
	}
	if (port22_taken()) {
		fprintf(stderr, "ssh: port 22 is taken (a development image's dropbear)\n");
		g_st = ST_DEV;
		return false;
	}
	if (radio_up()) {
		fprintf(stderr, "ssh: not while a Wi-Fi interface is up\n");
		g_st = ST_RADIO;
		return false;
	}
	if (!prepare(pin, keydir, key, sizeof key) || pipe2(fds, O_CLOEXEC) != 0) {
		clean_run();
		g_st = ST_FAILED;
		return false;
	}
	snprintf(listen, sizeof listen, "%s:22", addr);
	g_pid = spawn(listen, key, fds[1]);
	close(fds[1]);
	if (g_pid < 0) {
		close(fds[0]);
		clean_run();
		g_st = ST_FAILED;
		return false;
	}
	g_fd = fds[0];
	fcntl(g_fd, F_SETFL, fcntl(g_fd, F_GETFL) | O_NONBLOCK);
	/* Its namespace, before it can take a connection: the child unshares
	 * first thing, well before dropbear listens. */
	for (i = 0; i < 50 && !g_ns[0]; i++) {
		learn_ns();
		if (!g_ns[0]) sleep_ms(2);
	}
	g_st = ST_ON;
	fprintf(stderr, "ssh: dropbear %d on %s\n", (int)g_pid, listen);
	return true;
}

void sshlink_poll(void)
{
	char buf[1024];
	ssize_t n;
	int st, i, rounds;
	unsigned now;
	struct timespec ts;

	if (g_st != ST_ON) return;
	learn_ns();
	/* All of it, so a burst of wrong PINs is counted in the frame it lands
	 * in, not a kilobyte a frame behind; bounded all the same. */
	for (rounds = 0; rounds < 64 && (n = read(g_fd, buf, sizeof buf)) > 0; rounds++) {
		for (i = 0; i < n; i++) {
			if (buf[i] != '\n' && g_ll < sizeof g_line - 1) { g_line[g_ll++] = buf[i]; continue; }
			if (buf[i] != '\n') continue;        /* past the end of a long line */
			g_line[g_ll] = '\0';
			g_ll = 0;
			sshlink_feed(&g_c, g_line);
			fprintf(stderr, "ssh: %s\n", g_line);
			if (g_c.bad + g_web_bad >= SSHLINK_MAX_TRIES) break;
		}
		if (g_c.bad + g_web_bad >= SSHLINK_MAX_TRIES) break;
	}
	if (g_c.bad + g_web_bad >= SSHLINK_MAX_TRIES) {
		lock_pin();
		fprintf(stderr, "ssh: %d wrong PINs (%d on the page), off until the screen opens again\n",
		        g_c.bad + g_web_bad, g_web_bad);
		halt(ST_LOCKED, false);
		return;
	}
	if (g_c.failed) { halt(ST_FAILED, false); return; }
	if (waitpid(g_pid, &st, WNOHANG) == g_pid) {
		fprintf(stderr, "ssh: dropbear ended (%d)\n",
		        WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st));
		halt(ST_FAILED, true);
		return;
	}
	/* Every two seconds: a radio, and the sign-in count against what is
	 * running. A connection killed outright logs no "Exit", and a count left
	 * above nothing would hold Auto Off off for as long as the screen stays
	 * open; with nothing but the master in the namespace, nobody is in. */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
	if (now - g_radio_at >= 2000u) {
		g_radio_at = now;
		if (radio_up()) {
			fprintf(stderr, "ssh: a Wi-Fi interface came up, so SSH stops\n");
			halt(ST_RADIO, false);
			return;
		}
		if (g_c.sessions > 0 && g_ns[0] && kill_ns(0) <= 1) g_c.sessions = 0;
	}
}

/* With the screen, and once as the launcher starts: a launcher that died
 * with sessions open left their namespace's name in RUN_NS, and those
 * sessions are ended here. */
void sshlink_stop(void)
{
	if (g_pid <= 0 && !g_ns[0]) {
		int fd = open(RUN_NS, O_RDONLY | O_CLOEXEC);

		if (fd >= 0) {
			ssize_t len = read(fd, g_ns, sizeof g_ns - 1);

			close(fd);
			g_ns[len > 0 ? len : 0] = '\0';
			if (g_ns[0]) fprintf(stderr, "ssh: ending what a launcher before this one left (%s)\n", g_ns);
		}
	}
	if (g_pid > 0 || g_fd >= 0 || g_ns[0]) {
		halt(ST_OFF, false);
		fprintf(stderr, "ssh: stopped\n");
	}
	g_st = ST_OFF;
}

int sshlink_sessions(void) { return g_st == ST_ON ? g_c.sessions : 0; }

bool sshlink_on(void) { return g_st == ST_ON; }

void sshlink_note_web(int wrong) { g_web_bad = wrong; }

const char *sshlink_status(void)
{
	switch (g_st) {
	case ST_ON:
		if (g_c.sessions == 0) return T(S_SSH_WAITING);
		snprintf(g_status, sizeof g_status, TN(S_SSH_SIGNED_IN, g_c.sessions), g_c.sessions);
		return g_status;
	case ST_DEV:    return T(S_SSH_DEV_IMAGE);
	case ST_LOCKED: return T(S_SSH_LOCKED);
	case ST_RADIO:  return T(S_SSH_NOT_WITH_WIFI);
	default:        return T(S_SSH_OFF);
	}
}

#else  /* not Linux: no dropbear, nothing to start */

bool sshlink_start(const char *addr, const char *pin, const char *keydir)
{
	(void)addr; (void)pin; (void)keydir;
	return false;
}
void sshlink_poll(void) {}
void sshlink_stop(void) {}
bool sshlink_on(void) { return false; }
void sshlink_note_web(int wrong) { (void)wrong; }
int sshlink_sessions(void) { return 0; }
const char *sshlink_status(void) { return T(S_SSH_OFF); }

#endif
