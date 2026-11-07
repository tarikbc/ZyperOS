/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See net.h for why this shells out to curl and why the request is a file. */
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "monoclock.h"
#include "net.h"

#define RA_URL "https://retroachievements.org/dorequest.php"

/* Where a request's files are made: /tmp, RAM on the device. A check builds
 * with a folder of its own (tools/signin-check.c), to see every one go. */
#ifndef NET_TMP
#define NET_TMP "/tmp"
#endif

/* The curls to try, in order. A check builds with its own stand-in first,
 * to answer what a server would and to see what curl is handed. */
#ifndef NET_CURL_BINARIES
#define NET_CURL_BINARIES "/usr/bin/curl", "/bin/curl", "curl"
#endif

static char g_ca[512];

void net_set_ca_path(const char *path)
{
	snprintf(g_ca, sizeof g_ca, "%s", path ? path : "");
}

/* A request's file, made new each time and never written through whatever
 * is at its name. The names are known ahead - /tmp/zyperos-ra-<pid>-<n>.curl
 * and the rest - and /tmp is every user's on a development host: a link left
 * at one would have a config carrying a token, or the reply carrying one,
 * written to the link's target. O_EXCL after an unlink refuses to follow it,
 * the same as src/themefile.c, and a stale file of a launcher that was killed
 * mid-request is simply replaced. Somebody who wins the race between the two
 * makes the open fail, and the request with it. */
static int open_new(const char *path, int flags, mode_t mode)
{
	unlink(path);
	return open(path, flags | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
}

static const char *curl_bin(void)
{
	static const char *tried[] = { NET_CURL_BINARIES };
	static const char *found;
	size_t i;

	if (found) return found;
	for (i = 0; i < sizeof tried / sizeof tried[0]; i++) {
		if (access(tried[i], X_OK) == 0) { found = tried[i]; return found; }
	}
	return NULL;
}

/* Quote for curl's config-file syntax: values are double quoted, and inside
 * them only backslash and quote need escaping. Nothing here is attacker
 * controlled today - hashes, ids and a token - but a ROM title will end up in
 * one of these eventually and this is the cheap moment to be right. */
static void cfg_quote(FILE *f, const char *v)
{
	fputc('"', f);
	for (; *v; v++) {
		unsigned char c = (unsigned char)*v;

		/* A line break inside a value would end the line, and what came
		 * after it would be read as an option of its own. curl's config
		 * knows these four escapes; any other control character is
		 * dropped. The callers refuse such URLs first (url_ok); this is
		 * the second lock on the same door. */
		if (c == '\n') { fputs("\\n", f); continue; }
		if (c == '\r') { fputs("\\r", f); continue; }
		if (c == '\t') { fputs("\\t", f); continue; }
		if (c == '\v') { fputs("\\v", f); continue; }
		if (c < 0x20 || c == 0x7f) continue;
		if (c == '"' || c == '\\') fputc('\\', f);
		fputc(c, f);
	}
	fputc('"', f);
}

/* A URL curl may be handed: something, and no control character in it.
 * Some URLs here come from a server's reply - a ScreenScraper cover or video
 * snap, a SteamGridDB grid - and a newline in one, written into the config
 * file, would have been an option of the reply's choosing. Refused here,
 * before any file is written or any curl started. */
static bool url_ok(const char *u)
{
	if (!u || !*u) return false;
	for (; *u; u++)
		if ((unsigned char)*u < 0x20 || (unsigned char)*u == 0x7f) return false;
	return true;
}

/* Web addresses only, there and after any redirect: never file://, which
 * would copy a file of the device's into wherever the output goes, nor any
 * of the other schemes curl knows. */
static void cfg_protocols(FILE *f)
{
	fprintf(f, "proto = \"=http,https\"\nproto-redir = \"=http,https\"\n");
}

/* The same config, for a GET of an arbitrary URL. No fields, no token, so
 * nothing here needs hiding from a process list - but it goes through a file
 * anyway, because two ways of invoking curl is two things to keep right. */
/* Who is asking, on every request. MusicBrainz asks each application to name
 * itself and a way to reach whoever runs it, and to throttle or refuse what
 * does not; the project's address is that way, and nobody's email is. The
 * other services are sent the same line, since one client should say one
 * thing about itself. */
#define NET_UA "ZyperOS/" ZYPEROS_VERSION " ( https://github.com/tarikbc/ZyperOS )"

/* One request header, written as curl's config wants it. A header is how
 * SteamGridDB takes its key, so this is a credential: it goes into the 0600
 * file and nowhere else, never argv, never a URL. */
static void cfg_header(FILE *f, const char *header)
{
	if (!header || !*header) return;
	fprintf(f, "header = ");
	cfg_quote(f, header);
	fputc('\n', f);
}

static bool write_get_config(const char *path, const char *url,
                            const char *body_path, int timeout_s,
                            const char *header)
{
	FILE *f;
	int fd;

	fd = open_new(path, O_WRONLY, 0600);
	if (fd < 0) return false;
	f = fdopen(fd, "w");
	if (!f) { close(fd); return false; }

	/* No show-error. A 404 is a normal answer to a GET here - box art asks
	 * for the name the card uses and expects to be told no about one in six -
	 * and curl printing "curl: (22)" for each put thirty-two of them in the
	 * device log per run. The caller knows what it asked for and says so in
	 * words; curl's exit code is enough for it to know.
	 *
	 * A request that carries a header carries a key, and does not follow a
	 * redirect: the key goes to the host the caller named and to no other.
	 * Newer curls drop a custom Authorization on a hop to another host, but
	 * nothing here depends on which curl the card has. */
	fprintf(f, "silent\nfail\n%s", header && *header ? "" : "location\n");
	cfg_protocols(f);
	cfg_header(f, header);
	/* THE STATUS, ON STDOUT, WITH THE BODY SENT TO ITS OWN FILE.
	 *
	 * curl runs with `fail`, so an HTTP error is an exit code and nothing
	 * else - and the callers now need to tell one refusal from another.
	 * ScreenScraper answers "we do not know this game" and "you have spent
	 * today's quota" and "too many at once" all as 4xx, and a run that treats
	 * those alike either gives up on a library it could have scraped or keeps
	 * asking a server that has already said no.
	 *
	 * write-out prints whatever happened, error or not, so the code arrives
	 * even when the body does not. It goes to stdout, which means the body
	 * cannot: `output` names the file instead. The device carries curl 7.54,
	 * which has neither --fail-with-body nor write-out's %%output{} - this
	 * works on any of them. */
	fprintf(f, "write-out = "); cfg_quote(f, "%{http_code}"); fputc('\n', f);
	if (body_path) { fprintf(f, "output = "); cfg_quote(f, body_path); fputc('\n', f); }
	fprintf(f, "max-time = %d\n", timeout_s);
	if (g_ca[0]) { fprintf(f, "cacert = "); cfg_quote(f, g_ca); fputc('\n', f); }
	fprintf(f, "user-agent = "); cfg_quote(f, NET_UA); fputc('\n', f);
	fprintf(f, "url = "); cfg_quote(f, url); fputc('\n', f);
	fclose(f);
	return true;
}

/* `url` is where it goes and `get` says how the fields travel: as a body, or
 * urlencoded onto the query string. Both were once RA_URL and a POST, which is
 * all RetroAchievements needs; ScreenScraper authenticates every call with
 * query parameters instead, and it carries the account password in them.
 *
 * Which is exactly why it goes through here rather than through a URL built by
 * a caller: curl's --data-urlencode with --get puts those parameters in THIS
 * file, mode 0600, and never in argv where anything that can list processes
 * would read them. One place knows about the trust store, the agent and the
 * quoting, and it stays one place. */
static bool write_config(const char *path, const char *url, bool get,
                         const net_field *fl, int n, int timeout_s,
                         const char *header, bool status)
{
	FILE *f;
	int fd, i;

	/* 0600 from the moment it exists: it carries the account token, and a
	 * file created readable and chmod'ed after has a window. */
	fd = open_new(path, O_WRONLY, 0600);
	if (fd < 0) return false;
	f = fdopen(fd, "w");
	if (!f) { close(fd); return false; }

	fprintf(f, "silent\nshow-error\nfail\n");
	cfg_protocols(f);
	/* `status`: the status after the body, on the same stdout, so the
	 * caller can tell a refusal from no answer (request): `fail` leaves an
	 * HTTP error no body, and a wrong password read as no network. Not the
	 * body to `output` and the status alone on stdout, as write_get_config
	 * has it: curl would make the body's file itself, at its own mode, and
	 * this body carries a token, where stdout is a file made 0600 here. The
	 * line break keeps the status off the body's last line; curl 7.54 has
	 * write-out. Not for net_post_async, whose reply file its callers read
	 * whole. */
	if (status) { fprintf(f, "write-out = "); cfg_quote(f, "\n%{http_code}"); fputc('\n', f); }
	fprintf(f, "max-time = %d\n", timeout_s);
	/* Verification stays ON. The device has no trust store of its own, which
	 * is the whole reason this file ships; -k would be the other way to make
	 * the handshake succeed and is not on the table for a request carrying a
	 * token. */
	if (g_ca[0]) { fprintf(f, "cacert = "); cfg_quote(f, g_ca); fputc('\n', f); }
	/* RA refuses curl's default agent with `unsupported_client`, which reads
	 * like a permissions problem and is not one. */
	fprintf(f, "user-agent = "); cfg_quote(f, NET_UA); fputc('\n', f);
	cfg_header(f, header);

	for (i = 0; i < n; i++) {
		char kv[1024];
		snprintf(kv, sizeof kv, "%s=%s", fl[i].k, fl[i].v ? fl[i].v : "");
		fprintf(f, "data-urlencode = ");
		cfg_quote(f, kv);
		fputc('\n', f);
	}
	if (get) fprintf(f, "get\n");
	fprintf(f, "url = "); cfg_quote(f, url); fputc('\n', f);
	fclose(f);
	return true;
}

/* curl -K cfg started, stdin /dev/null and stdout `out_fd`: its pid, or -1.
 * Everything it is asked is in the config, and nothing in argv, which any
 * process can read. */
static pid_t spawn_curl(const char *bin, const char *cfg, int out_fd)
{
	pid_t pid = fork();

	if (pid == 0) {
		char *argv[] = { (char *)bin, (char *)"-K", (char *)cfg, NULL };
		int devnull = open("/dev/null", O_RDONLY);

		if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
		dup2(out_fd, 1);
		execv(bin, argv);
		_exit(127);
	}
	return pid;
}

/* Run curl with stdout on `out_fd`. Returns its exit status, or -1. */
static int run_curl(const char *cfg, int out_fd)
{
	const char *bin = curl_bin();
	pid_t pid;
	int st = -1;

	if (!bin) {
		fprintf(stderr, "ra: no curl on this device\n");
		return -1;
	}
	if ((pid = spawn_curl(bin, cfg, out_fd)) < 0) return -1;
	if (waitpid(pid, &st, 0) != pid) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* A path no other call can be using: per process AND per call.
 *
 * It was per process alone - "/tmp/zyperos-ra-<pid>.curl" - which every thread
 * in the launcher shares, and the response goes beside it as ".body". That held
 * only while every synchronous request ran on the main thread, one after
 * another. The moment two overlap, a sign-in on the main thread and an unlock
 * sent from a worker, both write the one config and read the one body, and each
 * gets the other's credentials, or the other's answer, or half of both. Nothing
 * would crash; a request would simply be refused or misread. The counter makes
 * every call its own pair of files. */
static void tmp_config(char *out, size_t n)
{
	static atomic_uint seq;

	snprintf(out, n, NET_TMP "/zyperos-ra-%ld-%u.curl", (long)getpid(),
	         (unsigned)atomic_fetch_add(&seq, 1));
}

/* What one blocking request came to. `exit` is curl's (-1 when it did not
 * run or did not exit), `http` the status its write-out printed after the
 * body (0 when it printed none, and "000" when nothing answered), `len` how
 * much of the body is in the caller's buffer (-1 for none: curl did not exit
 * 0), and `whole` whether that is all of it. */
typedef struct {
	int  exit, http;
	long len;
	bool whole;
} net_got;

/* The two blocking calls differ only in where they go and how the fields
 * travel, so they are one body with the config written two ways. What it
 * came to is for the callers below to read: the plain calls as they always
 * have, the answer calls with the status. */
static net_got request(const char *url, bool get, const net_field *f, int n,
                       char *out, size_t outn, int timeout_s, const char *header)
{
	net_got g = { -1, 0, -1, false };
	char cfg[sizeof NET_TMP + 64], tmp[sizeof NET_TMP + 80];
	struct stat st;
	int fd;

	if (!out || outn == 0) return g;
	out[0] = '\0';
	if (!url_ok(url) || (header && !url_ok(header))) return g;
	tmp_config(cfg, sizeof cfg);
	snprintf(tmp, sizeof tmp, "%s.body", cfg);
	if (!write_config(cfg, url, get, f, n, timeout_s, header, true)) return g;

	fd = open_new(tmp, O_RDWR, 0600);
	if (fd < 0) { unlink(cfg); return g; }
	g.exit = run_curl(cfg, fd);
	unlink(cfg);
	if (fstat(fd, &st) == 0) {
		/* The body, then a line break and the status's three digits */
		long body = (long)st.st_size;
		char tail[4];

		if (body >= 4 && pread(fd, tail, 4, body - 4) == 4 && tail[0] == '\n' &&
		    isdigit((unsigned char)tail[1]) && isdigit((unsigned char)tail[2]) &&
		    isdigit((unsigned char)tail[3])) {
			g.http = (tail[1] - '0') * 100 + (tail[2] - '0') * 10 + (tail[3] - '0');
			body -= 4;
		}
		if (g.exit == 0) {
			size_t want = (size_t)body < outn - 1 ? (size_t)body : outn - 1;
			ssize_t r = pread(fd, out, want, 0);

			if (r >= 0) {
				out[r] = '\0';
				g.len = (long)r;
				g.whole = r == (ssize_t)body;
			}
		}
	}
	close(fd);
	unlink(tmp);
	/* Which it was, for the log: never what was sent or what came back */
	if (g.exit == 22)
		fprintf(stderr, "net: refused, HTTP %d\n", g.http);
	else if (g.exit != 0)
		fprintf(stderr, "net: no answer (curl exit %d)\n", g.exit);
	else if (g.http < 200 || g.http > 299)
		fprintf(stderr, "net: answered HTTP %d\n", g.http);
	else if (!g.whole)
		fprintf(stderr, "net: a reply past its %zu bytes of room\n", outn - 1);
	return g;
}

/* The plain calls: the body whenever curl exits 0, as they always had it */
static long body_of(net_got g)
{
	return g.exit == 0 ? g.len : -1;
}

/* The answer calls: the server's status into `*http`, but only where a
 * server's answer came whole - exit 0, or 22 for `fail`'s HTTP error. Every
 * other exit is a request cut short or never made (a name, a route, the
 * time), and whatever status line came before the cut is no answer. The
 * body only for a 2xx all there in the buffer: half a token is not one. */
static long answer_of(net_got g, int *http)
{
	int h = g.exit == 0 || g.exit == 22 ? g.http : 0;

	if (h < 100 || h > 599) h = 0;
	if (http) *http = h;
	return g.exit == 0 && h >= 200 && h <= 299 && g.whole ? g.len : -1;
}

/* The first eight bytes of every PNG */
static bool is_png(const char *path)
{
	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	unsigned char b[8];
	FILE *f = fopen(path, "rb");
	bool ok;

	if (!f) return false;
	ok = fread(b, 1, 8, f) == 8 && !memcmp(b, sig, 8);
	fclose(f);
	return ok;
}

int net_get_pngs(const char *const *urls, const char *const *paths, int n,
                 int timeout_s)
{
	char cfg[sizeof NET_TMP + 64], part[600];
	FILE *f;
	int fd, i, got = 0, devnull;

	if (n <= 0) return 0;
	for (i = 0; i < n; i++)
		if (!url_ok(urls[i])) return 0;
	tmp_config(cfg, sizeof cfg);
	fd = open_new(cfg, O_WRONLY, 0600);
	if (fd < 0) return 0;
	f = fdopen(fd, "w");
	if (!f) { close(fd); return 0; }
	/* No `fail`: a missing badge is one file of many, and curl going on to
	 * the next is what is wanted. What came back is judged by is_png. */
	fprintf(f, "silent\nlocation\n");
	cfg_protocols(f);
	fprintf(f, "max-time = %d\n", timeout_s);
	if (g_ca[0]) { fprintf(f, "cacert = "); cfg_quote(f, g_ca); fputc('\n', f); }
	fprintf(f, "user-agent = "); cfg_quote(f, NET_UA); fputc('\n', f);
	for (i = 0; i < n; i++) {
		snprintf(part, sizeof part, "%s.part", paths[i]);
		fprintf(f, "url = "); cfg_quote(f, urls[i]); fputc('\n', f);
		fprintf(f, "output = "); cfg_quote(f, part); fputc('\n', f);
	}
	fclose(f);

	devnull = open("/dev/null", O_WRONLY);
	run_curl(cfg, devnull >= 0 ? devnull : 1);
	if (devnull >= 0) close(devnull);
	unlink(cfg);

	for (i = 0; i < n; i++) {
		snprintf(part, sizeof part, "%s.part", paths[i]);
		if (is_png(part) && rename(part, paths[i]) == 0) got++;
		else unlink(part);
	}
	return got;
}

long net_get_buf(const char *url, const net_field *f, int n, char *out,
                 size_t outn, int timeout_s)
{
	return body_of(request(url, true, f, n, out, outn, timeout_s, NULL));
}

long net_get_hdr_buf(const char *url, const char *header, char *out, size_t outn,
                     int timeout_s)
{
	return body_of(request(url, true, NULL, 0, out, outn, timeout_s, header));
}

long net_post_buf(const net_field *f, int n, char *out, size_t outn, int timeout_s)
{
	return body_of(request(RA_URL, false, f, n, out, outn, timeout_s, NULL));
}

long net_post_answer(const net_field *f, int n, char *out, size_t outn,
                     int timeout_s, int *http)
{
	return answer_of(request(RA_URL, false, f, n, out, outn, timeout_s, NULL), http);
}

long net_get_hdr_answer(const char *url, const char *header, char *out,
                        size_t outn, int timeout_s, int *http)
{
	return answer_of(request(url, true, NULL, 0, out, outn, timeout_s, header), http);
}

/* ---- the same thing, without waiting ------------------------------------ */

static pid_t g_async = -1;
static char  g_async_cfg[sizeof NET_TMP + 96];
/* When the request in flight started, and what the last one to finish took
 * and exited with. See net_async_ms. */
static long  g_async_t0;
static int   g_async_ms, g_async_exit;

bool net_post_async(const net_field *f, int n, const char *path, int timeout_s)
{
	const char *bin = curl_bin();
	int fd;

	if (g_async > 0 && net_async_poll() == 0) return false;
	if (!bin) return false;

	snprintf(g_async_cfg, sizeof g_async_cfg, NET_TMP "/zyperos-ra-async-%ld.curl",
	         (long)getpid());
	if (!write_config(g_async_cfg, RA_URL, false, f, n, timeout_s, NULL, false)) return false;

	fd = open_new(path, O_WRONLY, 0644);
	if (fd < 0) { unlink(g_async_cfg); return false; }

	g_async_t0 = now_ms();
	g_async = spawn_curl(bin, g_async_cfg, fd);
	if (g_async < 0) { close(fd); unlink(g_async_cfg); g_async = -1; return false; }
	close(fd);
	return true;
}

/* Where an async GET is being written, so the poll can put it in place. Empty
 * when the request in flight is a POST, which manages its own destination. */
static char g_async_dest[512];
static char g_async_part[520];
/* Where curl prints the status, and the last one it printed. Kept apart from
 * the body so that a caller can ask what happened even when there is no body
 * to read - which is every refusal, since curl runs with `fail`. */
static char g_async_st[560];
static int  g_async_http;

void net_async_abort(void)
{
	if (g_async > 0) {
		kill(g_async, SIGKILL);
		waitpid(g_async, NULL, 0);
	}
	g_async = -1;
	if (g_async_cfg[0]) unlink(g_async_cfg);
	if (g_async_part[0]) unlink(g_async_part);
	if (g_async_st[0]) { unlink(g_async_st); g_async_st[0] = '\0'; }
	g_async_dest[0] = '\0';
	g_async_part[0] = '\0';
}

bool net_get_async(const char *url, const char *path, int timeout_s)
{
	return net_get_async_hdr(url, path, timeout_s, NULL);
}

bool net_get_async_hdr(const char *url, const char *path, int timeout_s,
                       const char *header)
{
	const char *bin = curl_bin();
	int fd;

	/* A slot left set by a caller that walked away is cleared here rather
	 * than refused forever.
	 *
	 * This deadlocked once, and silently: a screen closed mid-fetch left
	 * g_async pointing at a pid nobody would ever wait for. Every later
	 * request then returned false - so no WAIT phase was entered, so
	 * net_async_poll was never called, so the slot was never cleared. Box art
	 * reported "could not start curl" on every system for the rest of the
	 * launcher's life, with curl sitting right there working. Only a restart
	 * fixed it, which is the shape of a bug nobody reports accurately. */
	if (g_async > 0 && net_async_poll() == 0) return false;   /* genuinely busy */
	if (!bin) return false;
	if (!url_ok(url) || (header && !url_ok(header))) return false;

	snprintf(g_async_cfg, sizeof g_async_cfg, NET_TMP "/zyperos-get-async-%ld.curl",
	         (long)getpid());
	snprintf(g_async_dest, sizeof g_async_dest, "%s", path);
	if (snprintf(g_async_part, sizeof g_async_part, "%s.part", path)
	    >= (int)sizeof g_async_part) {
		g_async_dest[0] = '\0';
		return false;
	}
	if (!write_get_config(g_async_cfg, url, g_async_part, timeout_s, header)) {
		g_async_dest[0] = '\0';
		return false;
	}
	snprintf(g_async_st, sizeof g_async_st, "%s.http", g_async_part);

	g_async_http = 0;
	fd = open_new(g_async_st, O_WRONLY, 0644);
	if (fd < 0) { unlink(g_async_cfg); g_async_dest[0] = '\0'; return false; }

	g_async_t0 = now_ms();
	g_async = spawn_curl(bin, g_async_cfg, fd);
	if (g_async < 0) {
		close(fd);
		unlink(g_async_cfg);
		unlink(g_async_part);
		g_async = -1;
		g_async_dest[0] = '\0';
		return false;
	}
	close(fd);
	return true;
}

int net_async_poll(void)
{
	int st;
	pid_t r;
	bool ok;

	if (g_async <= 0) return -1;
	r = waitpid(g_async, &st, WNOHANG);
	if (r == 0) return 0;                      /* still going */

	g_async = -1;
	unlink(g_async_cfg);
	ok = (r > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	g_async_ms = (int)(now_ms() - g_async_t0);
	g_async_exit = (r > 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;

	g_async_http = 0;
	if (g_async_st[0]) {
		FILE *h = fopen(g_async_st, "r");

		if (h) {
			if (fscanf(h, "%d", &g_async_http) != 1) g_async_http = 0;
			fclose(h);
		}
		unlink(g_async_st);
		g_async_st[0] = '\0';
	}

	/* A GET renames into place here rather than in the child, because only
	 * the parent knows the request succeeded. curl -f exits non-zero on an
	 * HTTP error but has usually already written the error body. */
	if (g_async_dest[0]) {
		if (ok && rename(g_async_part, g_async_dest) != 0) ok = false;
		if (!ok) unlink(g_async_part);
		g_async_dest[0] = '\0';
	}
	return ok ? 1 : -1;
}

int net_async_http(void)
{
	return g_async_http;
}

int net_async_ms(void)   { return g_async_ms; }
int net_async_exit(void) { return g_async_exit; }

bool net_online(void)
{
	struct ifaddrs *ifa, *p;
	bool up = false;

	if (!curl_bin()) return false;
	if (getifaddrs(&ifa) != 0) return false;
	for (p = ifa; p; p = p->ifa_next) {
		if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
		if (!(p->ifa_flags & IFF_UP)) continue;
		if (p->ifa_flags & IFF_LOOPBACK) continue;
		up = true;
		break;
	}
	freeifaddrs(ifa);
	return up;
}
