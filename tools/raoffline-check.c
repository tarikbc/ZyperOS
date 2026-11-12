/* SPDX-License-Identifier: GPL-3.0-only */
/* Achievements earned offline, sent when a network is back:
 *
 *     make check-raoffline
 *
 * The launcher's own store (src/cheevos.c), its unlock queue and its worker
 * thread (src/raqueue.c) and the request it sends (src/rafetch.c), end to end,
 * with this binary for curl: src/net.c is read in whole with it first on its
 * list of curls, as tools/rabadge-check.c does, so every request the queue
 * makes lands here and is answered the way RetroAchievements answers it.
 *
 * What it holds, because each one fails silently on the device:
 *
 *   - nothing is sent while there is no network, and the unlock is kept
 *   - a network back sends what waited, from the shelf's watch alone, with
 *     no game played: the age of each unlock (`o`) and its ROM's hash (`m`)
 *     from the set's header, signed the way the server checks it
 *   - a row from before ages were kept goes with no `o` and no `m`
 *   - an unlock older than 14 days is sent as 14 days old, signed so, not
 *     as its real age, which the server would clamp and then distrust
 *   - the notice counts only what waited, not an unlock sent the moment its
 *     game ended
 *   - a pass that finds RetroAchievements down keeps everything, and the
 *     watch tries again after its wait and not before
 *
 * Makes no real request. The account is made up; its token is a fixture.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../src/cheevos.h"
#include "../src/db.h"
#include "../src/rafetch.h"
#include "../src/raqueue.h"

#ifndef NET_TMP
#error "build with -DNET_TMP, a folder of this check's own (the Makefile does)"
#endif

#define LOG   NET_TMP "-log"         /* what the stand-in was asked, one line a request */
#define SET   NET_TMP "-card/Blaster Master.set"
#define HASH  "0123456789abcdef0123456789abcdef"
#define USER  "tester"

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

static char g_self[1024];
static bool g_online;

#define NET_CURL_BINARIES g_self
#define net_online net_online_of_this_computer
#include "../src/net.c"
#undef net_online

/* Whatever the step says: the computer this runs on has a network, and the
 * whole point is a handheld that does not */
bool net_online(void) { return g_online; }

/* ---- the stand-in ---------------------------------------------------------- */

/* The quoted value after `key = ` on a line of the config */
static bool cfg_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);

	if (strncmp(line, key, k) || strncmp(line + k, " = \"", 4)) return false;
	line += k + 4;
	snprintf(out, n, "%s", line);
	if (strrchr(out, '"')) *strrchr(out, '"') = '\0';
	return true;
}

/* One request: its fields in a line of the log, and an answer. STANDIN says
 * which: "ok" takes every unlock, "down" is a network with no way out (curl's
 * "could not connect"). */
static int standin(int argc, char **argv)
{
	const char *mode = getenv("STANDIN");
	FILE *cfg, *log;
	char line[2048], kv[1024], a[32] = "";

	if (argc != 3 || strcmp(argv[1], "-K") || !(cfg = fopen(argv[2], "r"))) return 99;
	if (mode && !strcmp(mode, "down")) { fclose(cfg); return 7; }
	if (!(log = fopen(LOG, "a"))) { fclose(cfg); return 99; }
	fputs("req", log);
	while (fgets(line, sizeof line, cfg)) {
		line[strcspn(line, "\n")] = '\0';
		if (!cfg_value(line, "data-urlencode", kv, sizeof kv)) continue;
		/* The token never goes in the log, even a made-up one: the habit is
		 * the point */
		if (!strncmp(kv, "t=", 2)) continue;
		fprintf(log, " %s", kv);
		if (!strncmp(kv, "a=", 2)) snprintf(a, sizeof a, "%s", kv + 2);
	}
	fputs(" \n", log);
	fclose(log);
	fclose(cfg);
	printf("{\"Success\":true,\"AchievementID\":%s,\"AchievementsRemaining\":1,"
	       "\"Score\":0,\"SoftcoreScore\":10}\n200", a);
	return 0;
}

/* ---- helpers --------------------------------------------------------------- */

static void sh(const char *cmd)
{
	if (system(cmd) != 0) { printf("  (could not run: %s)\n", cmd); failures++; }
}

static int log_lines(void)
{
	FILE *f = fopen(LOG, "r");
	char line[2048];
	int n = 0;

	if (!f) return 0;
	while (fgets(line, sizeof line, f)) n++;
	fclose(f);
	return n;
}

/* The request for achievement `id`, the last one asked, into `out` */
static bool log_for(int id, char *out, size_t n)
{
	FILE *f = fopen(LOG, "r");
	char line[2048], want[32];
	bool got = false;

	if (!f) return false;
	snprintf(want, sizeof want, " a=%d ", id);
	while (fgets(line, sizeof line, f))
		if (strstr(line, want)) { snprintf(out, n, "%s", line); got = true; }
	fclose(f);
	return got;
}

/* The value of ` k=` in a logged request, "" when it was not sent */
static void field(const char *req, const char *k, char *out, size_t n)
{
	char want[16];
	const char *p;

	out[0] = '\0';
	snprintf(want, sizeof want, " %s=", k);
	if (!(p = strstr(req, want))) return;
	p += strlen(want);
	snprintf(out, n, "%.*s", (int)strcspn(p, " \n"), p);
}

/* Apply the worker's answers until a pass is over, as the main loop does */
static void settle(void)
{
	int i;

	for (i = 0; i < 150; i++) {
		ra_flush_poll();
		SDL_Delay(10);
	}
}

static void write_set(void)
{
	FILE *f = fopen(SET, "w");

	if (!f) { printf("  FAIL: cannot write %s\n", SET); failures++; return; }
	fputs("#! zyperos-cheevos 2\tgame=1459\tconsole=7\ttitle=Blaster Master\thash=" HASH "\n", f);
	fputs("#:\t24698\t5\tBringing the Heat-Seeker\tUse a homing missile.\t85741\n", f);
	fputs("24698\t0xH06f3=0\n", f);
	fputs("#:\t24699\t10\tMaking Lightning Waves\tUse the wave gun.\t85742\n", f);
	fputs("24699\t0xH06f3=1\n", f);
	fputs("#:\t24700\t50\tSpread out the Power\tUse a multi-warhead missile.\t85743\n", f);
	fputs("24700\t0xH06f3=2\n", f);
	fputs("#:\t24701\t25\tOne More\tAnother one.\t85744\n", f);
	fputs("24701\t0xH06f3=3\n", f);
	fputs("#:\t24702\t25\tLong Ago\tEarned long ago.\t85745\n", f);
	fputs("24702\t0xH06f3=4\n", f);
	fclose(f);
}

int main(int argc, char **argv)
{
	char req[2048], v[64], o[32], m[64], h[8], want[33];
	unsigned t = 100000;                 /* the shelf's clock, made up */
	long now = (long)time(NULL);

	if (argc == 3 && !strcmp(argv[1], "-K")) return standin(argc, argv);

	if (!realpath(argv[0], g_self)) snprintf(g_self, sizeof g_self, "%s", argv[0]);
	if (SDL_Init(0) != 0) { printf("SDL: %s\n", SDL_GetError()); return 2; }
	sh("rm -rf '" NET_TMP "' '" NET_TMP "-card' '" LOG "'");
	sh("mkdir -p '" NET_TMP "' '" NET_TMP "-card'");
	db_init(NET_TMP "/dev.db", NET_TMP "/lib.db", NULL);
	db_set_str(db_dev(), "ra.user", USER);
	db_set_str(db_dev(), "ra.token", "fixture-token-not-real");
	CHECK(ra_creds_load(), "the made-up account signs in");
	setenv("STANDIN", "ok", 1);

	/* A row from before ages were kept: owed, no fields */
	db_set_str(db_lib(), "chv.1459.24700", "p");
	write_set();
	chv_earned_load();
	CHECK(chv_load(SET), "the set loads");
	CHECK(!strcmp(chv_game_hash(), HASH), "the header's hash is read: %s", chv_game_hash());

	printf("raoffline: offline\n");
	g_online = false;
	CHECK(chv_note_unlock_at(24698, now - 3600, false, true), "an unlock an hour ago, offline");
	CHECK(chv_note_unlock_at(24702, now - 40L * 24 * 3600, false, true), "and one 40 days ago");
	CHECK(chv_earned_save(), "the store writes");
	CHECK(chv_pending_count() == 3, "three wait: %d", chv_pending_count());
	CHECK(chv_pending_for(1459) == 3 && chv_is_pending(24698), "this game's wait, by count and by row");
	ra_flush_watch(t);
	ra_flush_start("", "", 0);           /* the end of a game, offline */
	settle();
	CHECK(log_lines() == 0, "nothing is sent with no network: %d requests", log_lines());
	CHECK(chv_pending_count() == 3, "and nothing is lost: %d", chv_pending_count());

	printf("raoffline: a network back, on the shelf\n");
	g_online = true;
	t += 2500;
	ra_flush_watch(t);
	settle();
	CHECK(log_lines() == 3, "the three that waited are sent: %d requests", log_lines());
	CHECK(chv_pending_count() == 0, "none waits now: %d", chv_pending_count());
	CHECK(ra_flush_take_sent() == 3, "the notice counts the three");
	CHECK(ra_flush_take_sent() == 0, "once");

	CHECK(log_for(24698, req, sizeof req), "24698 was asked");
	field(req, "o", o, sizeof o);
	field(req, "m", m, sizeof m);
	field(req, "h", h, sizeof h);
	field(req, "v", v, sizeof v);
	CHECK(atol(o) >= 3600 && atol(o) <= 3660, "dated an hour back: o=%s", o);
	CHECK(!strcmp(m, HASH), "with the ROM's hash from the set: m=%s", m);
	CHECK(!strcmp(h, "0"), "softcore: h=%s", h);
	ra_award_signature(24698, USER, false, (unsigned)atol(o), want);
	CHECK(!strcmp(v, want), "signed with the id twice and the offset: v=%s, want %s", v, want);

	CHECK(log_for(24702, req, sizeof req), "24702 was asked");
	field(req, "o", o, sizeof o);
	field(req, "v", v, sizeof v);
	CHECK(atol(o) == RA_OFFSET_MAX, "40 days ago is sent as 14: o=%s", o);
	ra_award_signature(24702, USER, false, (unsigned)RA_OFFSET_MAX, want);
	CHECK(!strcmp(v, want), "and signed as 14 days, so the server keeps it");

	CHECK(log_for(24700, req, sizeof req), "the old row was asked");
	field(req, "o", o, sizeof o);
	field(req, "m", m, sizeof m);
	field(req, "v", v, sizeof v);
	CHECK(!o[0] && !m[0], "with no age and no hash it does not have: o=%s m=%s", o, m);
	ra_award_signature(24700, USER, false, 0, want);
	CHECK(!strcmp(v, want), "signed without an offset");

	printf("raoffline: an unlock with a network, sent as its game ends\n");
	CHECK(chv_note_unlock_at(24699, now, false, false), "unlocked online");
	ra_flush_start("", "", 0);
	settle();
	CHECK(chv_pending_count() == 0, "sent");
	CHECK(ra_flush_take_sent() == 0, "and no notice: the player saw it unlock");

	printf("raoffline: RetroAchievements down\n");
	setenv("STANDIN", "down", 1);
	CHECK(chv_note_unlock_at(24701, now, false, false), "unlocked online");
	ra_flush_start("", "", 0);
	settle();
	CHECK(chv_pending_count() == 1, "kept: %d", chv_pending_count());
	setenv("STANDIN", "ok", 1);
	t += 2500;
	ra_flush_watch(t);                   /* the network never went away */
	settle();
	CHECK(chv_pending_count() == 1, "not asked again at once: %d", chv_pending_count());
	t += 5u * 60u * 1000u;
	ra_flush_watch(t);
	settle();
	CHECK(chv_pending_count() == 0, "asked again after the wait: %d", chv_pending_count());
	CHECK(ra_flush_take_sent() == 1, "and it had waited, so the notice says so");

	printf("raoffline: the store, read again\n");
	CHECK(chv_earned_save(), "the store writes");
	chv_earned_load();                   /* a launcher started again */
	CHECK(chv_pending_count() == 0, "it came back settled: %d", chv_pending_count());

	ra_flush_stop();
	db_shutdown();
	sh("rm -rf '" NET_TMP "' '" NET_TMP "-card' '" LOG "'");
	SDL_Quit();
	if (failures) { printf("%d failed\n", failures); return 1; }
	printf("ok: unlocks wait offline, go when a network is back, dated and hashed as earned,\n"
	       "signed as the server checks; the notice counts only what waited\n");
	return 0;
}
