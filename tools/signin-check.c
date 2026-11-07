/* SPDX-License-Identifier: GPL-3.0-only */
/* RetroAchievements sign-in, telling a wrong password from another refusal
 * from no answer at all, through the real src/rafetch.c and src/net.c, with
 * no network.
 *
 *     make check-signin
 *
 * curl is all there is between sign-in and the network, and this check is its
 * own curl: src/net.c is read in here whole, with this binary first on its
 * list of curls (NET_CURL_BINARIES) and a folder of its own for the request's files
 * (NET_TMP, from the Makefile). Run as curl - with -K and a config - it
 * answers as curl would for the server it plays, which STANDIN in its
 * environment chooses: a token; a 401, a 403 and a 503 under `fail`; a
 * redirect not followed; a reply that says no in RetroAchievements' own
 * words, and one with no token; one cut short, one too long for the reply's
 * room; and nothing at all, from a name not found, the timeout, and a curl
 * that dies before it says anything. Each must give its own message and its
 * own result, and no token but a whole one is ever kept.
 *
 * As it answers, the stand-in holds the credential rules (src/net.h): nothing
 * on its command line but -K and the config's path; the password in the
 * config and nowhere else; the config and its stdout, where the reply goes,
 * regular files of this user's, 0600, one link each; and no redirect
 * followed. open() is watched as net.c calls it, under a umask of 0 that
 * would show anything looser, so every file the request makes is known to be
 * made new and 0600 from its first moment, and the folder is empty after
 * every request. The log says which failure it was, and never the password
 * or the token.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "../src/i18n.h"
#include "../src/net.h"
#include "../src/rafetch.h"

#ifndef NET_TMP
#error "build with -DNET_TMP, a folder of this check's own (the Makefile does)"
#endif

#define USER     "tester"
#define PASSWORD "hunter2-correct-horse"
#define TOKEN    "TOKENabcdef0123456789"
#define OTHER    "TOKENfromareplytoolong"

#define REPORT NET_TMP "-report"     /* what the stand-in found, run by run */
#define LOG    NET_TMP "-log"        /* stderr, while a sign-in runs */

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

/* ---- src/net.c, with this binary for its curl --------------------------- */

static char g_self[1024];       /* this binary, the curl net.c runs */
static bool g_online = true;    /* what net_online says, step by step */

/* Every file net.c made with open(O_CREAT): its path, flags and mode */
#define MADE_MAX 32
static struct { char path[512]; int flags; mode_t mode; } g_made[MADE_MAX];
static int g_nmade;

static int watched_open(const char *path, int flags, ...);

#define NET_CURL_BINARIES g_self
#define open watched_open
#define net_online net_online_of_this_computer
#include "../src/net.c"
#undef open
#undef net_online

/* For rafetch.c: whatever the step says, so "not on a network" is a step of
 * its own and every other one runs on a computer with no network as well */
bool net_online(void) { return g_online; }

static int watched_open(const char *path, int flags, ...)
{
	mode_t mode = 0;

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
		if (g_nmade < MADE_MAX) {
			snprintf(g_made[g_nmade].path, sizeof g_made[0].path, "%s", path);
			g_made[g_nmade].flags = flags;
			g_made[g_nmade].mode = mode;
			g_nmade++;
		}
	}
	return open(path, flags, mode);
}

/* ---- the stand-in ---------------------------------------------------------
 *
 * What each server answers, as curl run with `fail` hands it on: the body on
 * stdout (none for an HTTP error), then the config's write-out with the
 * status in it ("000" when nothing answered), and curl's exit status: 0, 22
 * for an HTTP error, 18 for a transfer cut short, 6 for a name not found, 28
 * for the time running out, 7 for a connection refused. A status of -1 is a
 * curl that never got as far as its write-out. */
typedef struct { const char *standin; int http; const char *body; int exit; } answer;

static const char LONG_REPLY_HEAD[] =
	"{\"Success\":true,\"User\":\"" USER "\",\"Token\":\"" OTHER "\",\"Messages\":\"";

static const answer ANSWERS[] = {
	{ "token",    200, "{\"Success\":true,\"User\":\"" USER "\",\"Token\":\"" TOKEN "\",\"Score\":5}", 0 },
	{ "401",      401, NULL, 22 },
	{ "403",      403, NULL, 22 },
	{ "503",      503, NULL, 22 },
	{ "redirect", 302, "<html><a href=\"https://example.invalid/\">moved</a></html>\n", 0 },
	{ "words",    200, "{\"Success\":false,\"Error\":\"Your account has not been verified.\"}", 0 },
	{ "notoken",  200, "{\"Success\":true,\"User\":\"" USER "\"}", 0 },
	{ "cut",      200, "{\"Success\":true,\"User\":\"" USER "\",\"Token\":\"TOKENab", 18 },
	{ "long",     200, NULL, 0 },        /* LONG_REPLY_HEAD and more than a reply's room */
	{ "noname",     0, NULL, 6 },
	{ "timeout",    0, NULL, 28 },
	{ "dies",      -1, NULL, 7 },
};

static void found(FILE *rep, const char *fmt, ...)
{
	va_list ap;

	fputs("FAIL ", rep);
	va_start(ap, fmt);
	vfprintf(rep, fmt, ap);
	va_end(ap);
	fputc('\n', rep);
}

/* A file this user owns, regular, 0600, one link: the rules for the config
 * and for curl's stdout */
static void own_file(FILE *rep, const char *what, const struct stat *st)
{
	if (!S_ISREG(st->st_mode)) found(rep, "%s is not a regular file", what);
	if ((st->st_mode & 07777) != 0600) found(rep, "%s is %04o, not 0600", what, (unsigned)(st->st_mode & 07777));
	if (st->st_uid != getuid()) found(rep, "%s is another user's", what);
	if (st->st_nlink != 1) found(rep, "%s has %ld links", what, (long)st->st_nlink);
}

/* The quoted value after `key = ` on a line of the config, unescaped as curl
 * reads it; false when there is no such line */
static bool cfg_value(const char *cfg, const char *key, char *out, size_t n)
{
	char pat[64];
	const char *p;
	size_t o = 0;

	snprintf(pat, sizeof pat, "%s = \"", key);
	for (p = cfg; (p = strstr(p, pat)); p++)
		if (p == cfg || p[-1] == '\n') break;
	if (!p) return false;
	for (p += strlen(pat); *p && *p != '"' && o + 1 < n; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			out[o++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'r' ? '\r' : *p == 'v' ? '\v' : *p;
		} else {
			out[o++] = *p;
		}
	}
	out[o] = '\0';
	return *p == '"';
}

static int standin(int argc, char **argv)
{
	static char cfg[8192], wo[128], line[64];
	const char *mode = getenv("STANDIN");
	const answer *a = NULL;
	FILE *rep = fopen(REPORT, "a"), *f;
	struct stat st;
	size_t i, n;
	int k;

	if (!rep) return 99;
	for (i = 0; i < sizeof ANSWERS / sizeof ANSWERS[0]; i++)
		if (mode && !strcmp(mode, ANSWERS[i].standin)) a = &ANSWERS[i];
	if (!a) found(rep, "no answer for STANDIN=%s", mode ? mode : "(unset)");

	/* The command line: -K and the config, nothing else, no credential */
	if (argc != 3) found(rep, "curl was given %d arguments", argc - 1);
	for (k = 0; k < argc; k++)
		if (strstr(argv[k], PASSWORD) || strstr(argv[k], TOKEN))
			found(rep, "a credential is on the command line, as argument %d", k);
	if (argc < 3 || strncmp(argv[2], NET_TMP "/", sizeof NET_TMP)) {
		found(rep, "the config is not in the request's folder");
		fputs("ran\n", rep);
		fclose(rep);
		return 2;
	}

	/* The config and stdout, as curl finds them */
	if (lstat(argv[2], &st) != 0) found(rep, "the config is not there");
	else own_file(rep, "the config", &st);
	if (fstat(1, &st) != 0) found(rep, "stdout is not open");
	else {
		own_file(rep, "stdout, the reply's file,", &st);
		if (st.st_size != 0) found(rep, "the reply's file was not new");
	}
	n = 0;
	if ((f = fopen(argv[2], "r"))) {
		n = fread(cfg, 1, sizeof cfg - 1, f);
		fclose(f);
	}
	cfg[n] = '\0';
	if (!strstr(cfg, "\ndata-urlencode = \"p=" PASSWORD "\"\n"))
		found(rep, "the password is not in the config as a field");
	if (!strstr(cfg, "\nurl = \"https://retroachievements.org/dorequest.php\"\n"))
		found(rep, "the request is not for retroachievements.org");
	if (!strstr(cfg, "\nfail\n")) found(rep, "the request does not fail on an HTTP error");
	if (strstr(cfg, "\nlocation\n")) found(rep, "a request with a password follows redirects");
	if (!cfg_value(cfg, "write-out", wo, sizeof wo)) found(rep, "no write-out: the status is lost");
	fputs("ran\n", rep);
	fclose(rep);

	/* The answer */
	if (!a) return 2;
	if (a->body) {
		if (write(1, a->body, strlen(a->body)) < 0) return 23;
	} else if (!strcmp(a->standin, "long")) {
		if (write(1, LONG_REPLY_HEAD, sizeof LONG_REPLY_HEAD - 1) < 0) return 23;
		for (i = 0; i < 3000; i++)
			if (write(1, "x", 1) < 0) return 23;
		if (write(1, "\"}", 2) < 0) return 23;
	}
	if (a->http >= 0) {
		const char *at = strstr(wo, "%{http_code}");

		/* The write-out as curl prints it, the status where it says */
		if (at) {
			snprintf(line, sizeof line, "%.*s%03d%s", (int)(at - wo), wo, a->http, at + 12);
			if (write(1, line, strlen(line)) < 0) return 23;
		} else if (write(1, wo, strlen(wo)) < 0) {
			return 23;
		}
	}
	return a->exit;
}

/* ---- the steps ------------------------------------------------------------ */

static long file_text(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "rb");
	size_t got = 0;

	out[0] = '\0';
	if (!f) return -1;
	got = fread(out, 1, n - 1, f);
	fclose(f);
	out[got] = '\0';
	return (long)got;
}

static int count(const char *text, const char *word)
{
	int k = 0;
	const char *p = text;

	while ((p = strstr(p, word))) { k++; p += strlen(word); }
	return k;
}

static int left_in_tmp(void)
{
	DIR *d = opendir(NET_TMP);
	struct dirent *e;
	int k = 0;

	if (!d) return -1;
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) k++;
	closedir(d);
	return k;
}

/* One sign-in, with the stand-in answering as `standin` says, or with no
 * network when `online` is false. `runs` is how many times curl must run;
 * `want` the message; `logged` what the log must say. */
static void step(const char *what, const char *standin, bool online, const char *user,
                 bool want_ok, const char *want, int runs, const char *logged)
{
	static char report[8192], log[8192];
	char err[160], u[RA_USER_MAX], t[RA_TOKEN_MAX];
	int saved, fd, i;
	bool ok;

	printf("  %s:\n", what);
	remove(REPORT);
	g_nmade = 0;
	g_online = online;
	setenv("STANDIN", standin, 1);

	fflush(stderr);
	saved = dup(2);
	fd = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd >= 0) { dup2(fd, 2); close(fd); }
	ok = ra_sign_in(user, PASSWORD, err, sizeof err);
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	file_text(LOG, log, sizeof log);
	file_text(REPORT, report, sizeof report);

	CHECK(ok == want_ok, "sign-in said %s", ok ? "yes" : "no");
	CHECK(!strcmp(err, want), "it said \"%s\", wanted \"%s\"", err, want);
	CHECK(count(report, "ran\n") == runs, "curl ran %d times, wanted %d", count(report, "ran\n"), runs);
	CHECK(!strstr(report, "FAIL"), "as curl, the check found:\n%s", report);
	CHECK(g_nmade == 2 * runs, "the request made %d files, wanted %d", g_nmade, 2 * runs);
	for (i = 0; i < g_nmade; i++) {
		CHECK(!strncmp(g_made[i].path, NET_TMP "/", sizeof NET_TMP), "%s is made outside its folder", g_made[i].path);
		CHECK((g_made[i].flags & (O_EXCL | O_NOFOLLOW)) == (O_EXCL | O_NOFOLLOW),
		      "%s is not made new: it could be a link's target", g_made[i].path);
		CHECK(g_made[i].mode == 0600, "%s is made %04o, not 0600", g_made[i].path, (unsigned)g_made[i].mode);
	}
	CHECK(left_in_tmp() == 0, "%d of the request's files are left", left_in_tmp());
	CHECK(!strstr(log, PASSWORD) && !strstr(log, TOKEN) && !strstr(log, OTHER) && !strstr(log, "TOKENab"),
	      "the log carries a credential:\n%s", log);
	CHECK(!logged || strstr(log, logged), "the log does not say \"%s\":\n%s", logged ? logged : "", log);

	/* The account: a whole token from the one reply that had it, and never
	 * a part of one, or one from a reply too long to read */
	ra_creds_copy(u, sizeof u, t, sizeof t);
	CHECK(!strcmp(t, TOKEN) && !strcmp(u, USER), "the account is now \"%s\" with \"%s\"", u, t);
}

int main(int argc, char **argv)
{
	char turned[160];

	if (argc > 1 && !strcmp(argv[1], "-K")) return standin(argc, argv);

	printf("signin: RetroAchievements sign-in told apart, against a stand-in for curl\n");
	if (!realpath(argv[0], g_self)) {
		printf("  FAIL: where is this check? %s\n", strerror(errno));
		return 1;
	}
	/* Nothing looser can hide behind a umask: a file made 0644 is 0644 */
	umask(0);
	mkdir(NET_TMP, 0700);
	{
		DIR *d = opendir(NET_TMP);
		struct dirent *e;
		char p[sizeof NET_TMP + 300];

		while (d && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(p, sizeof p, "%s/%s", NET_TMP, e->d_name);
			remove(p);
		}
		if (d) closedir(d);
	}

	step("a token, and the account is signed in", "token", true, USER,
	     true, "", 1, NULL);
	step("a 401: the user name or the password is wrong", "401", true, USER,
	     false, T(S_RA_SIGN_IN_WRONG), 1, "wrong user or password (HTTP 401)");
	snprintf(turned, sizeof turned, T(S_RA_SIGN_IN_HTTP_ANSWER), 503);
	step("a 503: no sign-in, with its number", "503", true, USER,
	     false, turned, 1, "HTTP 503");
	snprintf(turned, sizeof turned, T(S_RA_SIGN_IN_HTTP_ANSWER), 403);
	step("a 403: no sign-in, with its number", "403", true, USER,
	     false, turned, 1, "HTTP 403");
	snprintf(turned, sizeof turned, T(S_RA_SIGN_IN_HTTP_ANSWER), 302);
	step("a redirect, not followed: no sign-in, with its number", "redirect", true, USER,
	     false, turned, 1, "HTTP 302");
	/* The words a person sees, with the number where it can be read out */
	CHECK(!strcmp(turned, "retroachievements.org would not sign you in (HTTP 302)"),
	      "the message for an HTTP answer reads \"%s\"", turned);
	step("no in RetroAchievements' own words, shown as they are", "words", true, USER,
	     false, "Your account has not been verified.", 1, NULL);
	step("a yes with no token", "notoken", true, USER,
	     false, T(S_RA_SIGN_IN_NO_TOKEN), 1, NULL);
	step("a reply cut short, never read as a token", "cut", true, USER,
	     false, T(S_RA_SIGN_IN_NO_REACH), 1, "no answer (curl exit 18)");
	step("a reply too long for its room, never read", "long", true, USER,
	     false, T(S_RA_SIGN_IN_NO_TOKEN), 1, "past its 2047 bytes");
	step("nothing at all: no such name", "noname", true, USER,
	     false, T(S_RA_SIGN_IN_NO_REACH), 1, "no answer (curl exit 6)");
	step("nothing at all: the time ran out", "timeout", true, USER,
	     false, T(S_RA_SIGN_IN_NO_REACH), 1, "no answer (curl exit 28)");
	step("nothing at all: curl said nothing", "dies", true, USER,
	     false, T(S_RA_SIGN_IN_NO_REACH), 1, "no answer (curl exit 7)");
	step("not on a network: nothing asked", "token", false, USER,
	     false, T(S_RA_SIGN_IN_NO_NETWORK), 0, NULL);
	step("no user: nothing asked", "token", true, "",
	     false, T(S_RA_SIGN_IN_NEED_BOTH), 0, NULL);

	remove(REPORT);
	remove(LOG);
	rmdir(NET_TMP);
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: a wrong password, a refusal and no answer each say so, and the credentials stay off the command line and in 0600 files that go\n");
	return 0;
}
