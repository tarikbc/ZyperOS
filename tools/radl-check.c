/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Achievements' Download (src/radl.h), a whole card's run, against a
 * RetroAchievements of this check's own: no network.
 *
 *     make check-radl
 *
 * curl is all there is between the run and the server, and this check is its
 * own curl, as tools/signin-check.c is: src/net.c read in here whole, this
 * binary first on its list of curls. Run as curl - with -K and a config - it
 * answers as dorequest.php would, from a file the check writes (SERVER):
 * hash libraries, the account's counts, sets, unlocks, badges, and the
 * refusals a run meets - a 429, a 500, a network gone, an account refused.
 *
 * A mixed card: a set on it and right, one out of date, one new, a hash
 * RetroAchievements does not know, one with nothing to earn, one marked none
 * that it knows now, a Game Boy game behind a 429, and a shelf with no
 * console. Then a second run that asks nothing per game and hashes nothing,
 * a run cut by the network and gone on with, one cancelled and done again,
 * one whose libraries never come, a DS game whose hash was kept from before
 * the DS hash, and one whose account is refused. The time
 * is the check's, so the minute a 429 asks for passes at once. */
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/cheevos.h"
#include "../src/config.h"
#include "../src/db.h"
#include "../src/rafetch.h"
#include "../src/radl.h"
#include "../src/rapack.h"
#include "../src/rahash.h"

#ifndef NET_TMP
#error "build with -DNET_TMP, a folder of this check's own (the Makefile does)"
#endif

#define CARD   NET_TMP "-card"
#define ROMS   CARD "/Roms"
#define SERVER NET_TMP "-server"      /* what the stand-in answers from */
#define LOG    NET_TMP "-log"         /* what it was asked, a line a request */
#define USER   "tester"
#define RUNLOG NET_TMP "-runlog"      /* the run's own log (stderr) */

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n");                                                     \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* ---- src/net.c, with this binary for its curl --------------------------- */

static char g_self[1024];
static bool g_online = true;

#define NET_CURL_BINARIES g_self
#define net_online net_online_of_this_computer
#include "../src/net.c"
#undef net_online

bool net_online(void) { return g_online; }

/* ---- the stand-in ----------------------------------------------------------- */

static bool cfg_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);

	if (strncmp(line, key, k) || strncmp(line + k, " = \"", 4)) return false;
	line += k + 4;
	snprintf(out, n, "%s", line);
	if (strrchr(out, '"')) *strrchr(out, '"') = '\0';
	return true;
}

/* A line of SERVER whose first words are `a` and `b`, the rest into `rest`.
 * "fail" lines count down: `fail <r> <code> <times>` answers <code> that
 * many times, then is spent (written back with one fewer). */
static bool server_line(const char *a, const char *b, char *rest, size_t n)
{
	FILE *f = fopen(SERVER, "r");
	char line[4096], w1[64], w2[64];
	bool hit = false;

	if (!f) return false;
	while (fgets(line, sizeof line, f)) {
		int used = 0;

		line[strcspn(line, "\n")] = '\0';
		if (sscanf(line, "%63s %63s %n", w1, w2, &used) < 2) continue;
		if (strcmp(w1, a) || strcmp(w2, b)) continue;
		snprintf(rest, n, "%s", used ? line + used : "");
		hit = true;
		break;
	}
	fclose(f);
	return hit;
}

/* A `fail <r> <code> <times>` line for this request - or `fail <r>/<c>`,
 * for one console's - and its code, once spent */
static int server_fail(const char *r, const char *c)
{
	FILE *f = fopen(SERVER, "r");
	char line[4096], out[65536] = "", w1[64], w2[64], rc[96];
	int code = 0;

	if (!f) return 0;
	snprintf(rc, sizeof rc, "%s/%s", r, c);
	while (fgets(line, sizeof line, f)) {
		int k, times;

		if (!code && sscanf(line, "%63s %63s %d %d", w1, w2, &k, &times) == 4 &&
		    !strcmp(w1, "fail") && (!strcmp(w2, r) || !strcmp(w2, rc)) && times > 0) {
			code = k;
			if (times > 1) {
				char again[160];

				snprintf(again, sizeof again, "fail %s %d %d\n", w2, k, times - 1);
				strncat(out, again, sizeof out - strlen(out) - 1);
			}
			continue;
		}
		strncat(out, line, sizeof out - strlen(out) - 1);
	}
	fclose(f);
	if (code && (f = fopen(SERVER, "w"))) { fputs(out, f); fclose(f); }
	return code;
}

static void png(const char *path)
{
	static const unsigned char sig[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0 };
	FILE *f = fopen(path, "wb");

	if (f) { fwrite(sig, 1, sizeof sig, f); fclose(f); }
}

static int standin(int argc, char **argv)
{
	FILE *cfg, *log;
	char line[2048], kv[1024], r[32] = "", c[16] = "", m[40] = "", gid[16] = "";
	char rest[4096], url[512] = "";
	bool status = false, fail = false, get = false;
	int code;

	if (argc != 3 || strcmp(argv[1], "-K") || !(cfg = fopen(argv[2], "r"))) return 99;
	log = fopen(LOG, "a");
	while (fgets(line, sizeof line, cfg)) {
		line[strcspn(line, "\n")] = '\0';
		if (!strcmp(line, "fail")) fail = true;
		if (!strncmp(line, "write-out", 9)) status = true;
		if (cfg_value(line, "url", kv, sizeof kv)) {
			snprintf(url, sizeof url, "%s", kv);
			if (strstr(kv, "/Badge/")) get = true;
		}
		/* A badge: the url's file, at the output that follows it */
		if (get && cfg_value(line, "output", kv, sizeof kv)) {
			const char *name = strrchr(url, '/');

			if (name && strncmp(name + 1, "missing", 7)) png(kv);
			if (log) fprintf(log, "badge %s\n", name ? name + 1 : "?");
			continue;
		}
		if (!cfg_value(line, "data-urlencode", kv, sizeof kv)) continue;
		if (!strncmp(kv, "r=", 2)) snprintf(r, sizeof r, "%s", kv + 2);
		if (!strncmp(kv, "c=", 2)) snprintf(c, sizeof c, "%s", kv + 2);
		if (!strncmp(kv, "m=", 2)) snprintf(m, sizeof m, "%s", kv + 2);
		if (!strncmp(kv, "g=", 2)) snprintf(gid, sizeof gid, "%s", kv + 2);
		/* The token is never in the log, even a made-up one; only that it was sent */
		if (!strncmp(kv, "t=", 2) && log) fputs("(token) ", log);
	}
	fclose(cfg);
	if (get) { if (log) fclose(log); return 0; }
	if (log) { fprintf(log, "%s c=%s m=%s g=%s\n", r, c, m, gid); fclose(log); }

	code = server_fail(r, c);
	/* Could not connect, a moment after the request went: long enough for
	 * the check to see it asked and take the network away first */
	if (code == 7) { usleep(300000); return 7; }
	if (code) {
		if (status) printf("\n%d", code);
		return fail ? 22 : 0;
	}
	if (!strcmp(r, "hashlibrary")) {
		/* `lib <console> <md5>=<id> <md5>=<id> ...` */
		char *p, *tok;
		bool first = true;

		printf("{\"Success\":true,\"MD5List\":{");
		if (server_line("lib", c, rest, sizeof rest))
			for (p = rest; (tok = strsep(&p, " ")); ) {
				char *eq = strchr(tok, '=');

				if (!eq) continue;
				*eq = '\0';
				printf("%s\"%s\":%s", first ? "" : ",", tok, eq + 1);
				first = false;
			}
		printf("}}");
	} else if (!strcmp(r, "allprogress")) {
		/* `prog <console> <id>:<achievements>:<unlocked> ...` */
		char *p, *tok;
		bool first = true;

		if (!server_line("prog", c, rest, sizeof rest) || !rest[0]) {
			printf("{\"Success\":true,\"Response\":[]}");   /* PHP's empty array */
		} else {
			printf("{\"Success\":true,\"Response\":{");
			for (p = rest; (tok = strsep(&p, " ")); ) {
				int id, n, u;

				if (sscanf(tok, "%d:%d:%d", &id, &n, &u) != 3) continue;
				printf("%s\"%d\":{\"Achievements\":%d", first ? "" : ",", id, n);
				if (u) printf(",\"Unlocked\":%d", u);
				printf("}");
				first = false;
			}
			printf("}}");
		}
	} else if (!strcmp(r, "gameid")) {
		/* Through the libraries: the id the hash has in any of them */
		FILE *f = fopen(SERVER, "r");
		int id = 0;

		while (f && fgets(line, sizeof line, f)) {
			char *at;

			if (strncmp(line, "lib ", 4) || !m[0] || !(at = strstr(line, m))) continue;
			id = atoi(at + strlen(m) + 1);
		}
		if (f) fclose(f);
		printf("{\"Success\":true,\"GameID\":%d}", id);
	} else if (!strcmp(r, "patch")) {
		/* `patch <id> <title> <n>`: n official achievements, ids id*100+k,
		 * badges B<id>_<k>, and one unofficial that must not be kept */
		int n = 0, k;
		char title[256] = "";

		if (server_line("patch", gid, rest, sizeof rest)) sscanf(rest, "%255s %d", title, &n);
		printf("{\"Success\":true,\"PatchData\":{\"ID\":%s,\"Title\":\"%s\",\"ConsoleID\":7,"
		       "\"Achievements\":[", gid, title);
		for (k = 1; k <= n; k++)
			printf("%s{\"ID\":%d,\"MemAddr\":\"0xH%04x=%d\",\"Title\":\"A%d\",\"Description\":\"D\","
			       "\"Points\":5,\"Flags\":3,\"BadgeName\":\"B%s_%d\"}",
			       k > 1 ? "," : "", atoi(gid) * 100 + k, k, k, k, gid, k);
		printf("%s{\"ID\":%d,\"MemAddr\":\"0xH0000=1\",\"Title\":\"U\",\"Flags\":5}]}}",
		       n ? "," : "", atoi(gid) * 100 + 99);
	} else if (!strcmp(r, "unlocks")) {
		/* `unlocks <id> <a> <b> ...` */
		char *p, *tok;
		bool first = true;

		printf("{\"Success\":true,\"UserUnlocks\":[");
		if (server_line("unlocks", gid, rest, sizeof rest))
			for (p = rest; (tok = strsep(&p, " ")); )
				if (*tok) { printf("%s%s", first ? "" : ",", tok); first = false; }
		printf("],\"GameID\":%s}", gid);
	} else {
		printf("{\"Success\":false,\"Error\":\"Unknown request\"}");
	}
	if (status) printf("\n200");
	return 0;
}

/* ---- the card -------------------------------------------------------------- */

static void sh(const char *cmd)
{
	if (system(cmd) != 0) { printf("  (could not run: %s)\n", cmd); failures++; }
}

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (!f) { printf("  FAIL: cannot write %s\n", path); failures++; return; }
	fputs(text, f);
	fclose(f);
}

/* A ROM of its own bytes, and its RetroAchievements hash: a NES file with no
 * header and a Game Boy file are hashed whole */
static void rom(const char *path, const char *bytes, char *md5)
{
	put(path, bytes);
	ra_md5_hex(bytes, strlen(bytes), md5);
}

static void set_file(const char *path, int game, int n, const char *hash)
{
	FILE *f = fopen(path, "w");
	int k;

	if (!f) { failures++; return; }
	fprintf(f, "#! zyperos-cheevos 2\tgame=%d\tconsole=7\ttitle=T%s%s\n", game,
	        hash ? "\thash=" : "", hash ? hash : "");
	for (k = 1; k <= n; k++) {
		fprintf(f, "#:\t%d\t5\tA%d\tD\tB%d_%d\n", game * 100 + k, k, game, k);
		fprintf(f, "%d\t0xH%04x=%d\n", game * 100 + k, k, k);
	}
	fclose(f);
}

static int log_count(const char *what)
{
	FILE *f = fopen(LOG, "r");
	char line[2048];
	int n = 0;

	if (!f) return 0;
	while (fgets(line, sizeof line, f))
		if (!what || strstr(line, what)) n++;
	fclose(f);
	return n;
}

static int set_count(const char *path)
{
	int n = -1, game;

	if (!radl_read_set(path, &game, &n, NULL, NULL, 0, NULL)) return -1;
	return n;
}

static bool has(const char *path) { return access(path, F_OK) == 0; }
/* A badge in its set's pack (src/rapack.h), and not loose beside it */
static bool packed(const char *set, const char *name)
{
	char pack[1200], loose[1200];

	snprintf(loose, sizeof loose, "%.*sbadges/%s.png", (int)(strrchr(set, '/') - set + 1), set, name);
	return rp_pack_path(set, pack, sizeof pack) && rp_has(pack, name) && !has(loose);
}

static systems_cfg g_sys;

static void add_system(const char *folder, const char *tag, const char *exts)
{
	system_cfg *s = &g_sys.systems[g_sys.count++];

	snprintf(s->folder, sizeof s->folder, "%s", folder);
	snprintf(s->name, sizeof s->name, "%s", folder);
	snprintf(s->tag, sizeof s->tag, "%s", tag);
	snprintf(s->exts, sizeof s->exts, "%s", exts);
}

/* Steps until the run is over, the clock moving 50 ms a step; `on_step`
 * may change the world mid-run. Returns how many steps it took. */
static unsigned g_now = 1000;
static bool g_hashed;            /* a step saw files being hashed */

static bool g_saw_slow;          /* a step saw a 429's minute */
static bool g_saw_wait;          /* the run waited for a network */

static int run_until_done(int (*on_step)(int step, const radl_progress *p))
{
	int step, rc = 1;

	g_hashed = false;
	for (step = 0; step < 200000 && rc == 1; step++) {
		radl_progress p;

		rc = radl_step(g_now);
		radl_status(&p);
		if (p.hashing) g_hashed = true;
		if (p.state == RADL_SLOWED && p.resume_at >= g_now + RADL_SLOW_MS - 100) g_saw_slow = true;
		if (on_step && on_step(step, &p)) break;
		/* A pause is passed at once: the check's time is its own */
		if ((p.state == RADL_SLOWED || p.state == RADL_RETRYING) && p.resume_at > g_now)
			g_now = p.resume_at;
		else
			g_now += 50;
		usleep(200);                 /* the stand-in's own time, for real */
	}
	return step;
}

/* ---- the scenarios ----------------------------------------------------------- */

static char H_CACHED[33], H_STALE[33], H_NEW[33], H_UNKNOWN[33], H_EMPTY[33], H_WASNONE[33],
            H_GB[33], H_LATER[33], H_MISFILED[33];

/* The server as it is, `later` with the game copied later known to it,
 * and `extra` lines after (fail lines) */
static void write_server(bool later, const char *extra)
{
	char text[8192], lib106[48] = "", prog106[16] = "";

	if (later) {
		snprintf(lib106, sizeof lib106, " %s=106", H_LATER);
		snprintf(prog106, sizeof prog106, " 106:1:0");
	}
	snprintf(text, sizeof text,
	         "lib 7 %s=101 %s=102 %s=103 %s=104 %s=105%s\n"
	         "lib 81\n"
	         "lib 4 %s=201 %s=202\n"
	         "prog 7 101:2:1 102:3:0 103:2:1 105:1:0%s\n"
	         "prog 81\n"
	         "prog 4 201:1:0\n"
	         "patch 101 Cached 2\npatch 102 Stale 3\npatch 103 New 2\npatch 104 Empty 0\n"
	         "patch 105 WasNone 1\npatch 201 GbGame 1\npatch 106 Later 1\npatch 202 Misfiled 1\n"
	         "unlocks 101 10101 999999\nunlocks 103 10302\n"
	         "%s",
	         H_CACHED, H_STALE, H_NEW, H_EMPTY, H_WASNONE, lib106, H_GB, H_MISFILED, prog106,
	         extra ? extra : "");
	put(SERVER, text);
}

static int g_drop_at = -1;

/* The network goes as the run asks for its first set, and comes back once
 * the run has waited forty steps for it: it says so, asks nothing
 * meanwhile, and goes on */
static int drop_network(int step, const radl_progress *p)
{
	static int gone_at = -1, waited = 0, asked_then = 0;

	if (g_drop_at < 0) return 0;
	if (gone_at < 0) {
		if (log_count("patch c=") > 0) {
			g_online = false;
			gone_at = step;
			waited = 0;
		}
		return 0;
	}
	if (p->state != RADL_NO_NETWORK) {
		CHECK(step - gone_at < 20000, "the run never said the network went: state %d", p->state);
		if (step - gone_at >= 20000) { g_online = true; g_drop_at = gone_at = -1; }
		return 0;
	}
	if (!waited++) asked_then = log_count(NULL);
	if (waited < 40) return 0;
	CHECK(log_count(NULL) == asked_then, "with no network it asks nothing: %d requests, %d before",
	      log_count(NULL), asked_then);
	g_online = true;
	g_drop_at = -1;
	gone_at = -1;
	g_saw_wait = true;
	return 0;
}

static int g_cancel_after = -1;

static int cancel_mid(int step, const radl_progress *p)
{
	(void)step;
	if (g_cancel_after >= 0 && p->requests >= g_cancel_after) {
		radl_cancel();
		g_cancel_after = -1;
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	char set[1024];
	radl_progress p;
	radl_sys plan[8];
	int n;

	if (argc == 3 && !strcmp(argv[1], "-K")) return standin(argc, argv);
	if (!realpath(argv[0], g_self)) snprintf(g_self, sizeof g_self, "%s", argv[0]);
	/* The run's log to a file, shown when something fails */
	unlink(RUNLOG);
	if (!freopen(RUNLOG, "w", stderr)) printf("  (the run's log stays on the terminal)\n");

	printf("radl: parts\n");
	{
		int c[RADL_CONSOLES];

		CHECK(radl_consoles("NES", c) == 2 && c[0] == 7 && c[1] == 81, "the NES shelf asks the NES and the FDS");
		CHECK(radl_consoles("PSP", c) == 1 && c[0] == 41, "the PSP is 41");
		CHECK(radl_consoles("PRT", c) == 0 && radl_consoles("PICO8", c) == 0, "Ports and PICO-8 have none");
		CHECK(!radl_set_stale(10, -1, -1), "a set RetroAchievements said nothing about stays");
		CHECK(radl_set_stale(10, 12, -1), "a set with fewer than it has now is out of date");
		CHECK(!radl_set_stale(10, 10, -1), "one with as many is not");
		CHECK(!radl_set_stale(9, 10, 10), "one checked at this count is not, whatever it holds");
		CHECK(radl_set_stale(10, 11, 10), "and is when the count moves past the check");
	}

	sh("rm -rf '" NET_TMP "' '" CARD "' '" SERVER "' '" LOG "'");
	sh("mkdir -p '" NET_TMP "' '" ROMS "/NES/.cheevos' '" ROMS "/Game Boy' '" ROMS "/Ports'");
	db_init(NET_TMP "/dev.db", NET_TMP "/lib.db", NULL);
	db_set_str(db_dev(), "ra.user", USER);
	db_set_str(db_dev(), "ra.token", "fixture-token-not-real");
	CHECK(ra_creds_load(), "the made-up account signs in");
	chv_earned_load();

	add_system("NES", "NES", "nes,fds,zip");
	add_system("Game Boy", "GB", "gb,zip");
	add_system("Ports", "PRT", "sh");

	rom(ROMS "/NES/Cached.nes", "cached rom bytes", H_CACHED);
	rom(ROMS "/NES/Stale.nes", "stale rom bytes", H_STALE);
	rom(ROMS "/NES/New.nes", "new rom bytes", H_NEW);
	rom(ROMS "/NES/Unknown.nes", "a hack nobody knows", H_UNKNOWN);
	rom(ROMS "/NES/Empty.nes", "a game with nothing to earn", H_EMPTY);
	rom(ROMS "/NES/Was None.nes", "known since", H_WASNONE);
	rom(ROMS "/Game Boy/Gb Game.gb", "game boy bytes", H_GB);
	/* A Game Boy game on the NES shelf: not in the NES's library, in the Game
	 * Boy's (a first play finds it by its hash, whatever the shelf) */
	rom(ROMS "/NES/Misfiled.nes", "a game boy game on the wrong shelf", H_MISFILED);
	put(ROMS "/Ports/Doom.sh", "#!/bin/sh\n");
	set_file(ROMS "/NES/.cheevos/Cached.set", 101, 2, H_CACHED);
	set_file(ROMS "/NES/.cheevos/Stale.set", 102, 1, H_STALE);
	put(ROMS "/NES/.cheevos/Was None.none", "");
	/* One of Cached's badges on the card already, one not */
	sh("mkdir -p '" ROMS "/NES/.cheevos/badges'");
	png(ROMS "/NES/.cheevos/badges/B101_1.png");

	printf("radl: the plan, before a run\n");
	n = radl_plan(&g_sys, ROMS, plan, 8);
	CHECK(n == 2, "two systems with games RetroAchievements could know (Ports has none): %d", n);
	CHECK(n >= 1 && plan[0].games == 7 && plan[0].ready == 2 && plan[0].none == 1 && plan[0].todo == 4,
	      "NES: 7 games, 2 ready, 1 none, 4 to ask: %d %d %d %d",
	      plan[0].games, plan[0].ready, plan[0].none, plan[0].todo);
	CHECK(n >= 2 && plan[1].games == 1 && plan[1].todo == 1, "Game Boy: one to ask");

	printf("radl: a first run on a mixed card\n");
	/* The Game Boy's library answers 429 first: a minute, then again */
	write_server(false, "fail hashlibrary/4 429 1\n");
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "a run starts");
	radl_status(&p);
	CHECK(p.systems == 2 && p.games == 8, "it will look at 2 systems, 8 games: %d %d", p.systems, p.games);
	g_saw_slow = false;
	run_until_done(NULL);
	CHECK(g_saw_slow, "the 429 was met and waited out (RADL_SLOWED, the server's minute)");
	radl_status(&p);
	CHECK(!radl_running() && p.problem == RADL_OK, "the run ends well: problem %d", p.problem);
	CHECK(p.fetched == 4, "four sets new on the card (New, Was None, Misfiled, Gb Game): %d", p.fetched);
	CHECK(p.updated == 1, "one out of date replaced (Stale): %d", p.updated);
	CHECK(p.none == 2, "two RetroAchievements has nothing for (Unknown, Empty): %d", p.none);
	CHECK(log_count("g=104") == 0, "Empty's set never asked: the account's counts do not list it");
	CHECK(p.ready == 6, "six ready offline: %d", p.ready);
	CHECK(p.failed == 0, "none failed: %d", p.failed);
	CHECK(p.unlocks == 2, "the account's unlocks taken (Cached 1, New 1): %d", p.unlocks);
	CHECK(log_count("hashlibrary c=4") == 2, "the Game Boy's library asked again after the 429: %d",
	      log_count("hashlibrary c=4"));
	CHECK(log_count("patch c=") == 5, "five sets asked for (Stale, New, Was None, Misfiled, Gb Game): %d",
	      log_count("patch c="));
	CHECK(log_count("gameid") == 2, "the two hashes the NES's library lacks asked once by hash alone "
	      "(Unknown, Misfiled): %d", log_count("gameid"));
	CHECK(set_count(ROMS "/NES/.cheevos/Misfiled.set") == 1, "and the one on the wrong shelf found");
	CHECK(log_count("unlocks c=") == 2, "unlocks asked only where the account holds more: %d",
	      log_count("unlocks c="));
	CHECK(log_count("c=0") == 0 && log_count("Ports") == 0, "nothing asked for Ports");
	CHECK(log_count("hashlibrary c=7 m= g=\n") == 1 && log_count("(token) hashlibrary") == 0,
	      "the libraries are asked with no account");

	chv_path(ROMS, "NES", "New", set, sizeof set);
	CHECK(set_count(set) == 2, "New's set is on the card, two achievements (the unofficial one left out)");
	{
		char h[33];
		int gid;

		radl_read_set(set, &gid, &n, NULL, NULL, 0, h);
		CHECK(gid == 103 && !strcmp(h, H_NEW), "with its game and its hash in the header: %d %s", gid, h);
	}
	CHECK(set_count(ROMS "/NES/.cheevos/Stale.set") == 3, "Stale's set is the new one, three");
	{
		char h[33];

		radl_read_set(ROMS "/NES/.cheevos/Stale.set", NULL, NULL, NULL, NULL, 0, h);
		CHECK(!strcmp(h, H_STALE), "and keeps the ROM's hash: %s", h);
	}
	CHECK(set_count(ROMS "/NES/.cheevos/Was None.set") == 1 && !has(ROMS "/NES/.cheevos/Was None.none"),
	      "Was None has its set, and its marker is gone");
	CHECK(has(ROMS "/NES/.cheevos/Unknown.none") && !has(ROMS "/NES/.cheevos/Unknown.set"),
	      "Unknown is marked: RetroAchievements has nothing");
	CHECK(has(ROMS "/NES/.cheevos/Empty.none"), "Empty too: nothing a player can earn");
	CHECK(set_count(ROMS "/Game Boy/.cheevos/Gb Game.set") == 1, "the Game Boy game's set, after the 429");
	CHECK(!has(ROMS "/Ports/.cheevos"), "and nothing written for Ports");
	CHECK(packed(ROMS "/NES/.cheevos/New.set", "B103_1") && packed(ROMS "/NES/.cheevos/New.set", "B103_2"),
	      "New's badges landed, in its pack");
	CHECK(packed(ROMS "/NES/.cheevos/Cached.set", "B101_2"), "and Cached's missing one");
	CHECK(packed(ROMS "/NES/.cheevos/Cached.set", "B101_1"),
	      "and the one it had loose, taken into the pack too (not fetched again)");
	CHECK(log_count("badge B101_1") == 0, "a badge on the card is not asked for again");
	CHECK(chv_synced_for(101) == 1 && chv_synced_for(103) == 1,
	      "the store holds the account's unlocks: %d %d", chv_synced_for(101), chv_synced_for(103));
	CHECK(chv_synced_for(1) == 0, "and not the account's notice that is not in the set");
	CHECK(has(ROMS "/NES/.cheevos/.hashes"), "the NES hashes are kept");

	printf("radl: a second run: nothing new\n");
	unlink(LOG);
	write_server(false, NULL);
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "a second run starts");
	run_until_done(NULL);
	radl_status(&p);
	CHECK(!g_hashed, "nothing hashed: every file's hash was kept");
	CHECK(p.fetched == 0 && p.updated == 0 && p.unlocks == 0, "nothing new: %d %d %d",
	      p.fetched, p.updated, p.unlocks);
	CHECK(p.ready == 6 && p.none == 2, "the same six ready, two none: %d %d", p.ready, p.none);
	CHECK(log_count("gameid") == 0, "and Unknown, marked none, is not asked by its hash again");
	/* NES: two libraries (two games have no set), two counts; Game Boy: its count */
	CHECK(p.requests == 5 && log_count(NULL) == 5,
	      "five requests, a system's few, none a game: %d", p.requests);
	CHECK(log_count("badge") == 0, "no badge asked: they are all on the card");

	printf("radl: the network goes mid-run and comes back\n");
	unlink(LOG);
	rom(ROMS "/NES/Later.nes", "a game copied later", H_LATER);
	/* its set's request meets no network */
	write_server(true, "fail patch 7 1\n");
	g_drop_at = 0;
	g_saw_wait = false;
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "a run starts");
	run_until_done(drop_network);
	CHECK(g_saw_wait, "the run waited for the network (RADL_NO_NETWORK)");
	radl_status(&p);
	CHECK(p.problem == RADL_OK && p.fetched == 1, "Later's set came once the network was back: %d %d",
	      p.problem, p.fetched);
	CHECK(set_count(ROMS "/NES/.cheevos/Later.set") == 1, "Later's set is on the card");

	printf("radl: a run stopped, and the next one finishes it\n");
	unlink(LOG);
	unlink(ROMS "/NES/.cheevos/New.set");
	unlink(ROMS "/Game Boy/.cheevos/Gb Game.set");
	write_server(true, NULL);
	g_cancel_after = 2;                  /* after the NES's two libraries */
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "a run starts");
	run_until_done(cancel_mid);
	CHECK(!radl_running(), "B stops it");
	CHECK(!has(ROMS "/NES/.cheevos/New.set"), "before New's set was asked for");
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "and a new run starts");
	run_until_done(NULL);
	radl_status(&p);
	CHECK(p.fetched == 2 && set_count(ROMS "/NES/.cheevos/New.set") == 2 &&
	      set_count(ROMS "/Game Boy/.cheevos/Gb Game.set") == 1,
	      "which puts back what was missing, and only that: %d new", p.fetched);
	CHECK(log_count("patch c=") == 2, "two sets asked for: %d", log_count("patch c="));

	printf("radl: the libraries never come: each game by its hash\n");
	unlink(LOG);
	unlink(ROMS "/NES/.cheevos/New.set");
	unlink(ROMS "/NES/.cheevos/Unknown.none");
	write_server(true, "fail hashlibrary 500 99\n");
	CHECK(radl_begin(&g_sys, ROMS, "NES", g_now), "a run for the NES alone starts");
	run_until_done(NULL);
	radl_status(&p);
	CHECK(p.problem == RADL_OK, "it ends well: %d", p.problem);
	CHECK(log_count("hashlibrary c=7") == 4, "the library asked four times (three again): %d",
	      log_count("hashlibrary c=7"));
	CHECK(log_count("gameid") >= 2, "then New and Unknown by their hashes: %d", log_count("gameid"));
	CHECK(set_count(ROMS "/NES/.cheevos/New.set") == 2, "New's set came that way");
	CHECK(has(ROMS "/NES/.cheevos/Unknown.none"), "and Unknown is none again");
	CHECK(log_count("Game Boy") == 0 && log_count("c=4") == 0, "nothing asked for the Game Boy");

	printf("radl: a DS game's hash kept from before the DS hash\n");
	{
		/* 1.0.6's first Download (#196) hashed a DS game whole and kept that
		 * in .hashes; #235 hashes it as RetroAchievements does, but a file
		 * of the same size and time kept the old one, so Mario Kart DS was
		 * looked up by a hash nobody has and stayed none (2026-11-17, the
		 * 1.0.6 test card). A file kept that way is hashed again, once. */
		static unsigned char ds[0xA000];
		char h_ds[33] = "", h_whole[33], line[512];
		struct stat st;
		unsigned seed = 1459;
		size_t i;

		for (i = 0; i < sizeof ds; i++) {
			seed = seed * 1103515245u + 12345u;
			ds[i] = (unsigned char)(seed >> 16);
		}
		/* arm9 at 0x4000 (0x1000), arm7 at 0x6000 (0x800), icon at 0x8000 */
		ds[0x20] = 0x00; ds[0x21] = 0x40; ds[0x22] = 0; ds[0x23] = 0;
		ds[0x2C] = 0x00; ds[0x2D] = 0x10; ds[0x2E] = 0; ds[0x2F] = 0;
		ds[0x30] = 0x00; ds[0x31] = 0x60; ds[0x32] = 0; ds[0x33] = 0;
		ds[0x3C] = 0x00; ds[0x3D] = 0x08; ds[0x3E] = 0; ds[0x3F] = 0;
		ds[0x68] = 0x00; ds[0x69] = 0x80; ds[0x6A] = 0; ds[0x6B] = 0;
		/* not a 0x2E 00 00 EA SuperCard header */
		ds[0] = 0;
		sh("mkdir -p '" ROMS "/Nintendo DS/.cheevos'");
		{
			FILE *f = fopen(ROMS "/Nintendo DS/Kart.nds", "wb");

			if (f) { fwrite(ds, 1, sizeof ds, f); fclose(f); }
		}
		CHECK(ra_hash_rom(ROMS "/Nintendo DS/Kart.nds", "NDS", h_ds), "the DS image hashes");
		ra_md5_hex(ds, sizeof ds, h_whole);
		CHECK(strcmp(h_ds, h_whole) != 0, "and not as the whole file: %s", h_ds);
		/* What that first Download left: the whole file's hash, and none */
		stat(ROMS "/Nintendo DS/Kart.nds", &st);
		snprintf(line, sizeof line, "#! zyperos-rahash 1\n%lld\t%ld\t%s\t-1\tKart.nds\n",
		         (long long)st.st_size, (long)st.st_mtime, h_whole);
		put(ROMS "/Nintendo DS/.cheevos/.hashes", line);
		put(ROMS "/Nintendo DS/.cheevos/Kart.none", "");

		add_system("Nintendo DS", "NDS", "nds,zip");
		unlink(LOG);
		snprintf(line, sizeof line, "lib 18 %s=301\nprog 18 301:2:0\npatch 301 Kart 2\n", h_ds);
		write_server(true, line);
		CHECK(radl_begin(&g_sys, ROMS, "Nintendo DS", g_now), "a run for the DS alone starts");
		run_until_done(NULL);
		radl_status(&p);
		CHECK(g_hashed, "the DS file is hashed again, not taken from .hashes");
		CHECK(p.problem == RADL_OK && p.fetched == 1 && p.none == 0,
		      "its set came: %d fetched, %d none, problem %d", p.fetched, p.none, p.problem);
		CHECK(log_count("hashlibrary c=18") == 1 && log_count(h_whole) == 0,
		      "the DS library asked, and the old hash never: %d", log_count("hashlibrary c=18"));
		CHECK(set_count(ROMS "/Nintendo DS/.cheevos/Kart.set") == 2 &&
		      !has(ROMS "/Nintendo DS/.cheevos/Kart.none"),
		      "Kart's set is on the card, and its marker is gone");
		{
			char h[33] = "";

			radl_read_set(ROMS "/Nintendo DS/.cheevos/Kart.set", NULL, NULL, NULL, NULL, 0, h);
			CHECK(!strcmp(h, h_ds), "with the DS hash in its header: %s", h);
		}

		/* The next run trusts what this one kept */
		unlink(ROMS "/Nintendo DS/.cheevos/Kart.set");
		put(ROMS "/Nintendo DS/.cheevos/Kart.none", "");
		unlink(LOG);
		CHECK(radl_begin(&g_sys, ROMS, "Nintendo DS", g_now), "a second DS run starts");
		run_until_done(NULL);
		radl_status(&p);
		CHECK(!g_hashed, "nothing hashed: the hash this version kept is used");
		CHECK(p.fetched == 1 && set_count(ROMS "/Nintendo DS/.cheevos/Kart.set") == 2,
		      "and the set comes by it: %d", p.fetched);

		/* Any other shelf's hashes from that first Download are right, and
		 * kept: a NES .hashes put back to its first head hashes nothing */
		{
			static char body[16384], again[sizeof body + 32];
			FILE *f = fopen(ROMS "/NES/.cheevos/.hashes", "r");
			size_t len = f ? fread(body, 1, sizeof body - 1, f) : 0;
			const char *nl;

			if (f) fclose(f);
			body[len] = '\0';
			nl = strchr(body, '\n');
			CHECK(nl && !strncmp(body, "#! zyperos-rahash ", 18), "the NES keeps its hashes");
			snprintf(again, sizeof again, "#! zyperos-rahash 1%s", nl ? nl : "\n");
			put(ROMS "/NES/.cheevos/.hashes", again);
		}
		g_sys.count--;                   /* the DS shelf: this scenario's alone */
		unlink(LOG);
		CHECK(radl_begin(&g_sys, ROMS, "NES", g_now), "a NES run starts");
		run_until_done(NULL);
		CHECK(!g_hashed, "and hashes nothing: the NES's hashes never changed");
	}

	printf("radl: the account is refused\n");
	unlink(LOG);
	unlink(ROMS "/NES/.cheevos/New.set");
	write_server(true, "fail allprogress 401 99\n");
	CHECK(radl_begin(&g_sys, ROMS, NULL, g_now), "a run starts");
	run_until_done(NULL);
	radl_status(&p);
	CHECK(p.problem == RADL_SIGN_IN, "it stops and says to sign in again: %d", p.problem);
	CHECK(log_count("allprogress") == 1, "after one refusal: %d", log_count("allprogress"));

	printf("radl: no network, or nobody signed in: no run\n");
	g_online = false;
	CHECK(!radl_begin(&g_sys, ROMS, NULL, g_now), "no network: it does not start");
	g_online = true;

	sh("rm -rf '" NET_TMP "' '" CARD "' '" SERVER "' '" LOG "'");
	if (failures) {
		printf("radl-check: %d FAILED (the run's log: %s)\n", failures, RUNLOG);
		return 1;
	}
	unlink(RUNLOG);
	printf("radl-check: ok\n");
	return 0;
}
