/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* RetroAchievements sign-in's words, and the HTTP status under them.
 *
 *     make check-rasignin
 *
 * The real src/net.c and src/rafetch.c, with curl played by
 * tools/curl-standin.py (net.c built with NET_CURL). The stand-in reads the
 * config net.c writes and answers as RetroAchievements and curl would - a
 * token, a 401 for a wrong password, a 503, a redirect, a reply cut short,
 * nothing at all - so no request leaves this computer.
 *
 * Three things, each one that a device gets wrong quietly:
 *
 *   THE WORDS. A wrong password says so, a refusal gives its number, and
 *   "could not reach" is said only when nothing answered. It once said
 *   "could not reach" for all three, which sends a player to look at the
 *   Wi-Fi when the password was the problem.
 *
 *   THE STATUS. request_buf reports what the server answered with, under
 *   curl's `fail` where a refusal has no body, for a reply too long for the
 *   buffer, and for a body that itself ends in digits; 0 when nothing
 *   answered. A caller that does not care (the unlock) passes NULL.
 *
 *   THE RULES. The stand-in sees what curl sees: argv is the config and
 *   nothing else, the config and the reply are 0600 before a byte is in
 *   them; and here, every temp file is gone after. */
#include "../src/net.h"
#include "../src/rafetch.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static char g_log[256];

/* Files net.c made for this process that are still there */
static int leftovers(void)
{
	char prefix[64];
	struct dirent *e;
	DIR *d = opendir("/tmp");
	int n = 0;

	if (!d) return 0;
	snprintf(prefix, sizeof prefix, "zyperos-ra-%ld-", (long)getpid());
	while ((e = readdir(d)))
		if (!strncmp(e->d_name, prefix, strlen(prefix))) {
			printf("  left behind: /tmp/%s\n", e->d_name);
			n++;
		}
	closedir(d);
	return n;
}

/* Sign in as `user` and say whether it ended as expected, with `want` as
 * the words (NULL: signed in) */
static void sign_in(const char *user, const char *want, const char *what)
{
	char err[160];
	bool ok = ra_sign_in(user, "hunter2", err, sizeof err);

	if (!want) {
		ck(ok && !err[0], what);
	} else {
		ck(!ok && !strcmp(err, want), what);
		if (ok || strcmp(err, want)) printf("    said \"%s\"\n", ok ? "(signed in)" : err);
	}
}

int main(void)
{
	char out[64], user[RA_USER_MAX], token[RA_TOKEN_MAX];
	char big[8192];
	net_field f[3];
	FILE *lf;
	long got;
	int http;

	/* net.c says what failed on stderr; a line at a time keeps it under
	 * the heading it belongs to */
	setvbuf(stdout, NULL, _IOLBF, 0);
	snprintf(g_log, sizeof g_log, "/tmp/rasignin-check-%ld.log", (long)getpid());
	unlink(g_log);
	setenv("NET_CHECK_LOG", g_log, 1);

	printf("what sign-in says:\n");
	sign_in("good", NULL, "the right password signs in");
	ra_creds_copy(user, sizeof user, token, sizeof token);
	ck(!strcmp(user, "good") && !strcmp(token, "tok-good"), "and keeps the user and the token");
	sign_in("wrong", "wrong user name or password",
	        "their 401 is a wrong user name or password");
	sign_in("busy", "retroachievements.org refused (HTTP 503)",
	        "any other refusal gives its number");
	sign_in("moved", "retroachievements.org refused (HTTP 301)",
	        "and so does a redirect, which a POST does not follow");
	sign_in("gone", "could not reach retroachievements.org",
	        "nothing answered (no DNS): could not reach");
	sign_in("slow", "could not reach retroachievements.org",
	        "the timeout before an answer: could not reach");
	sign_in("cut", "could not reach retroachievements.org",
	        "a 200 cut short is not a token: could not reach");
	sign_in("said-no", "Your account is not active.",
	        "a reply that says no gives RA's own words");
	ra_creds_copy(user, sizeof user, token, sizeof token);
	ck(!strcmp(user, "good") && !strcmp(token, "tok-good"),
	   "and none of those undid the account signed in before");

	printf("the status request_buf reports:\n");
	f[0].k = "r"; f[0].v = "login2";
	f[1].k = "u"; f[1].v = "wrong";
	f[2].k = "p"; f[2].v = "hunter2";
	http = -1;
	got = net_post_buf(f, 3, out, sizeof out, 5, &http);
	ck(got < 0 && http == 401 && !out[0], "a refusal under `fail`: no body, the 401");
	f[1].v = "gone";
	http = -1;
	got = net_post_buf(f, 3, out, sizeof out, 5, &http);
	ck(got < 0 && http == 0, "nothing answered: 0");
	f[1].v = "cut";
	http = -1;
	memset(out, 0, sizeof out);
	got = net_post_buf(f, 3, out, sizeof out, 5, &http);
	ck(got < 0 && http == 200 && !memchr(out, '{', sizeof out),
	   "a 200 cut short: its status, and none of its half a token copied out");
	f[1].v = "good";
	got = net_post_buf(f, 3, out, sizeof out, 5, NULL);
	ck(got > 0 && !strncmp(out, "{\"Success\":true", 15), "NULL for a caller that does not care");

	http = -1;
	got = net_get_hdr_buf("https://www.steamgriddb.com/api/v2/key-bad",
	                      "Authorization: Bearer k", out, sizeof out, 5, &http);
	ck(got < 0 && http == 401, "a GET with a key: a refused key is a 401 (SteamGridDB)");
	http = -1;
	got = net_get_hdr_buf("https://www.steamgriddb.com/api/v2/key-good",
	                      "Authorization: Bearer k", out, sizeof out, 5, &http);
	ck(got == (long)strlen("{\"success\":true,\"data\":[]}") && http == 200 &&
	   !strcmp(out, "{\"success\":true,\"data\":[]}"), "a good one is the body and a 200");
	http = -1;
	got = net_get_hdr_buf("https://www.steamgriddb.com/api/v2/key-none",
	                      "Authorization: Bearer k", out, sizeof out, 5, &http);
	ck(got < 0 && http == 0, "and no answer is 0, which keeps the key");
	http = -1;
	got = net_get_hdr_buf("https://x/big", NULL, out, sizeof out, 5, &http);
	ck(got == (long)sizeof out - 1 && http == 200 && strspn(out, "x") == sizeof out - 1,
	   "a reply too long for the buffer: cut to fit, status kept");
	http = -1;
	got = net_get_hdr_buf("https://x/digits", NULL, big, sizeof big, 5, &http);
	ck(got > 0 && http == 200 && !strcmp(big, "the answer ends in 404"),
	   "a body that ends in digits keeps them, and the status is the 200");

	printf("an unlock, which passes NULL:\n");
	ck(ra_submit_unlock_as("good", "tok-good", 123, NULL) == RA_SUBMIT_SENT, "sent");
	ck(ra_submit_unlock_as("gone", "tok-good", 123, NULL) == RA_SUBMIT_FAILED,
	   "and with no answer, owed for later");

	printf("the rules:\n");
	ck(leftovers() == 0, "every config and reply file is removed");
	lf = fopen(g_log, "r");
	if (lf) {
		char line[256];

		while (fgets(line, sizeof line, lf)) {
			printf("    %s", line);
			fails++;
		}
		fclose(lf);
		unlink(g_log);
	}

	if (fails) { printf("rasignin-check: %d FAILED\n", fails); return 1; }
	printf("rasignin-check: ok\n");
	return 0;
}
