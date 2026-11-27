/* SPDX-License-Identifier: GPL-3.0-only */
/* The downloads' queue (src/dlq.h) and net.c's slots, with no network.
 *
 *     make check-dlq
 *
 * This binary is its own curl, as tools/radl-check.c is: src/net.c read in
 * here whole, this binary first on its list of curls. Run as curl - -K and a
 * config - it answers a GET by its URL's path: /wait/MS/WORD waits MS and
 * answers WORD, /hold/NAME/WORD waits until the check makes the file NAME in
 * NET_TMP and answers WORD, /code/N answers N with no body, /down answers
 * nothing (curl's 000 and its exit 6, a host not found).
 *
 * Two slots carry a slow request and a quick one at once, and each finishes
 * as its own; dropping one leaves the other. Two lanes of three slow
 * requests each run side by side in the time of one; while a game runs they
 * wait, and go on after it; a stop is its lane's done, once, with its request
 * dropped; a lane below the card's floor is stopped as DLQ_FULL; the input
 * wait is cut to 16 ms while a lane runs and is as asked when none does; the
 * step hook (what MENU > Downloads keeps of what came, src/dl_screen.c) is
 * called after every step of each lane, on its slot, the last before its
 * done; and a lane started with none running is a new run. The floor here
 * is past any disk, so the room rule fires on its first look. */
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef NET_TMP
#error "build with -DNET_TMP, a folder of this check's own (the Makefile does)"
#endif

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

/* ---- src/net.c and src/dlq.c, with this binary for its curl -------------- */

/* PATH_MAX: a fortified realpath (glibc) refuses a smaller one */
static char g_self[PATH_MAX];

#define NET_CURL_BINARIES g_self
#include "../src/net.c"
#include "../src/dlq.c"

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

static int standin(const char *cfg)
{
	FILE *f = fopen(cfg, "r");
	char line[4096], url[2048] = "", out[2048] = "", word[256] = "", name[64];
	const char *path;
	int ms = 0, code = 200;

	if (!f) return 2;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (!cfg_value(line, "url", url, sizeof url)) cfg_value(line, "output", out, sizeof out);
	}
	fclose(f);
	path = strstr(url, "://");
	path = path ? strchr(path + 3, '/') : NULL;
	if (path && sscanf(path, "/wait/%d/%255s", &ms, word) == 2) {
		struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

		nanosleep(&t, NULL);
	} else if (path && sscanf(path, "/hold/%63[^/]/%255s", name, word) == 2) {
		/* Held until the check lets go: in flight for certain, however
		 * slow the computer, rather than for a time the check hopes is
		 * long enough. 60 s at most, so a check that never lets go ends. */
		char go[512];
		struct timespec t = { 0, 2000000L };
		int i;

		snprintf(go, sizeof go, NET_TMP "/%s", name);
		for (i = 0; i < 30000 && access(go, F_OK) != 0; i++) nanosleep(&t, NULL);
	} else if (path && sscanf(path, "/code/%d", &code) == 1) {
		word[0] = '\0';
	} else if (path && !strcmp(path, "/down")) {
		printf("000");
		return 6;
	} else {
		code = 404;
	}
	if (code >= 200 && code < 300 && out[0]) {
		FILE *o = fopen(out, "w");

		if (o) { fputs(word, o); fclose(o); }
	}
	printf("%d", code);
	return code >= 200 && code < 300 ? 0 : 22;
}

/* ---- helpers ---------------------------------------------------------------- */

static long ms_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(int ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static bool file_is(const char *path, const char *want)
{
	char got[256] = "";
	FILE *f = fopen(path, "r");

	if (!f) return false;
	if (!fgets(got, sizeof got, f)) got[0] = '\0';
	fclose(f);
	return !strcmp(got, want);
}

/* How long a wait below waits for what it waits for, in ms. Each returns as soon as that holds,
 * so this is only how long a broken queue keeps the check: two or three seconds were too few
 * once make check ran four at a time on a loaded Mac (2026-11-24). */
#define WAIT_MS 30000

/* Poll the slot in use until its request ends, at most `ms` */
static int poll_until(int ms)
{
	long t0 = ms_now();
	int r;

	while ((r = net_async_poll()) == 0 && ms_now() - t0 < ms) nap(5);
	return r;
}

/* ---- a lane of this check's own: `want` slow requests, one after another ---- */

typedef struct {
	const char *name;
	int want, done_n, started, wait_ms;
	int dones;           /* its done(), called how many times */
	dlq_end how;
	bool fail_begin;
	int steps;           /* its step(), called how many times */
} fake;

static fake g_fa, g_fb;
static fake *g_mine[DLQ_LANES];   /* the fake on each lane, for its functions */
/* Ends of a request seen while the other lane had one in flight: two lanes
 * side by side, told by what happened rather than by a clock */
static int g_overlap;

static int fake_step(fake *f)
{
	char url[256], path[512], word[64];

	f->steps++;
	if (f->started) {
		int r = net_async_poll();

		if (r == 0) return 1;
		f->started = 0;
		if (r != 1) return -1;
		snprintf(path, sizeof path, NET_TMP "/%s-%d", f->name, f->done_n);
		snprintf(word, sizeof word, "%s%d", f->name, f->done_n);
		if (!file_is(path, word)) return -1;      /* another slot's answer */
		f->done_n++;
		{
			const fake *other = f == g_mine[DLQ_ART] ? g_mine[DLQ_RA] : g_mine[DLQ_ART];

			if (other && other->started) g_overlap++;
		}
	}
	if (f->done_n == f->want) return 0;
	snprintf(url, sizeof url, "https://standin.invalid/wait/%d/%s%d", f->wait_ms, f->name, f->done_n);
	snprintf(path, sizeof path, NET_TMP "/%s-%d", f->name, f->done_n);
	if (!net_get_async(url, path, 10)) return -1;
	f->started = 1;
	return 1;
}

static int step_a(unsigned now) { (void)now; return fake_step(g_mine[DLQ_ART]); }
static int step_b(unsigned now) { (void)now; return fake_step(g_mine[DLQ_RA]); }
static void done_of(fake *f, dlq_end how)
{
	f->dones++;
	f->how = how;
	if (how != DLQ_ENDED) net_async_abort();  /* a run stopped drops its request */
}
static void done_a(dlq_end how) { done_of(g_mine[DLQ_ART], how); }
static void done_b(dlq_end how) { done_of(g_mine[DLQ_RA], how); }
static int pct_a(void) { return g_mine[DLQ_ART]->done_n * 100 / g_mine[DLQ_ART]->want; }
static int pct_b(void) { return g_mine[DLQ_RA]->done_n * 100 / g_mine[DLQ_RA]->want; }
static bool begin_a(void) { return !g_mine[DLQ_ART]->fail_begin; }
static bool begin_b(void) { return !g_mine[DLQ_RA]->fail_begin; }

static dlq_lane_def LANE_A = { .name = "lane a", .slot = NET_SLOT_ART, .step = step_a, .done = done_a, .pct = pct_a, .every_ms = 0 };
static dlq_lane_def LANE_B = { .name = "lane b", .slot = NET_SLOT_RA,  .step = step_b, .done = done_b, .pct = pct_b, .every_ms = 0 };

/* The step hook (dlq_on_step): each lane's calls, and any made off its slot
 * or after its done */
static int g_hooked[DLQ_LANES], g_hook_wrong;

static void on_step(dlq_lane l)
{
	net_slot now = net_slot_use(NET_SLOT_MAIN);

	net_slot_use(now);
	g_hooked[l]++;
	if (now != (l == DLQ_ART ? NET_SLOT_ART : NET_SLOT_RA) || g_mine[l]->dones) g_hook_wrong++;
}

/* Let a /hold/NAME request go on */
static void let_go(const char *name)
{
	char go[512];
	FILE *f;

	snprintf(go, sizeof go, NET_TMP "/%s", name);
	if ((f = fopen(go, "w"))) fclose(f);
}

static void fresh(fake *f, const char *name, int want, int wait_ms)
{
	memset(f, 0, sizeof *f);
	f->name = name;
	f->want = want;
	f->wait_ms = wait_ms;
}

/* Tick until no lane runs, at most `ms`; how long it took */
static long run_out(int ms)
{
	long t0 = ms_now();

	while (dlq_busy() && ms_now() - t0 < ms) { dlq_tick(); nap(2); }
	return ms_now() - t0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "-K")) return standin(argv[2]);
	if (!realpath(argv[0], g_self)) snprintf(g_self, sizeof g_self, "%s", argv[0]);
	if (system("rm -rf '" NET_TMP "' && mkdir -p '" NET_TMP "'") != 0) {
		printf("  could not make " NET_TMP "\n");
		return 1;
	}
	if (!freopen(NET_TMP "/log", "w", stderr)) printf("  (the log stays on the terminal)\n");

	printf("dlq: net's slots\n");
	{
		net_slot was = net_slot_use(NET_SLOT_ART);
		long t0 = ms_now();
		int ra;

		CHECK(was == NET_SLOT_MAIN, "the screens' slot is the one in use to start");
		/* The slow one held until the quick one has ended and been seen
		 * to: "still in flight" is then so on any computer. It was a 3 s
		 * request raced against a 50 ms one (2026-11-24). */
		CHECK(net_get_async("https://standin.invalid/hold/go-slow/slow", NET_TMP "/slow", 10), "a slow request starts on the art slot");
		net_slot_use(NET_SLOT_RA);
		CHECK(net_get_async("https://standin.invalid/wait/50/quick", NET_TMP "/quick", 10),
		      "a quick one starts on the achievements slot while it runs");
		ra = poll_until(WAIT_MS);
		CHECK(ra == 1 && file_is(NET_TMP "/quick", "quick"), "the quick one ends with its own answer");
		/* By what happened, not by the clock: the slow one has not ended */
		net_slot_use(NET_SLOT_ART);
		CHECK(net_async_poll() == 0, "while the slow one is still in flight on its slot (%ld ms in)", ms_now() - t0);
		net_slot_use(NET_SLOT_MAIN);
		CHECK(!net_async_held(), "the screens' slot is free all along");
		net_slot_use(NET_SLOT_ART);
		let_go("go-slow");
		CHECK(poll_until(WAIT_MS) == 1 && file_is(NET_TMP "/slow", "slow"), "the slow one ends with its own answer");

		/* Dropping one slot's request leaves the other's */
		/* Held, never let go: it is dropped while in flight, however slow */
		CHECK(net_get_async("https://standin.invalid/hold/never/dropped", NET_TMP "/dropped", 10), "a request to drop starts");
		net_slot_use(NET_SLOT_RA);
		CHECK(net_get_async("https://standin.invalid/wait/100/kept", NET_TMP "/kept", 10), "and one to keep, on the other slot");
		net_slot_use(NET_SLOT_ART);
		net_async_abort();
		CHECK(!net_async_held() && access(NET_TMP "/dropped", F_OK) != 0, "the dropped one is gone, no file left");
		net_slot_use(NET_SLOT_RA);
		CHECK(poll_until(WAIT_MS) == 1 && file_is(NET_TMP "/kept", "kept"), "the other ends with its answer");
		CHECK(net_get_async("https://standin.invalid/code/429", NET_TMP "/busy", 10) && poll_until(WAIT_MS) == -1 &&
		      net_async_http() == 429, "a refusal is read on its own slot (429)");
		/* A refusal and no answer are told apart (Box Art's "index refused
		 * by the server" and "index fetch failed"): both are -1, the
		 * status says which */
		CHECK(net_get_async("https://standin.invalid/code/404", NET_TMP "/gone", 10) && poll_until(WAIT_MS) == -1 &&
		      net_async_http() == 404 && access(NET_TMP "/gone", F_OK) != 0, "a 404 is a refusal, 404, no file");
		CHECK(net_get_async("https://standin.invalid/down", NET_TMP "/down", 10) && poll_until(WAIT_MS) == -1 &&
		      net_async_http() == 0 && net_async_exit() == 6, "no answer is status 0, curl's own exit");
		net_slot_use(was);
	}

	printf("dlq: two lanes side by side\n");
	{
		long took;

		g_mine[DLQ_ART] = &g_fa;
		g_mine[DLQ_RA] = &g_fb;
		fresh(&g_fa, "a", 3, 300);
		fresh(&g_fb, "b", 3, 300);
		g_overlap = 0;
		CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a), "lane a starts");
		CHECK(dlq_start(DLQ_RA, &LANE_B, begin_b), "lane b starts");
		CHECK(!dlq_start(DLQ_ART, &LANE_A, begin_a), "a lane already running does not start again");
		CHECK(dlq_busy() && dlq_running(DLQ_ART) && dlq_running(DLQ_RA), "both run");
		dlq_tick();
		CHECK(g_fa.started && g_fb.started, "both started a request before either ended one");
		took = run_out(WAIT_MS);
		CHECK(g_fa.done_n == 3 && g_fb.done_n == 3, "each did its three (a %d, b %d)", g_fa.done_n, g_fb.done_n);
		CHECK(g_overlap >= 2, "side by side: %d of their 6 requests ended while the other lane had one in flight", g_overlap);
		fprintf(stderr, "dlq-check: two lanes of three 300 ms requests took %ld ms\n", took);
		CHECK(g_fa.dones == 1 && g_fa.how == DLQ_ENDED && g_fb.dones == 1 && g_fb.how == DLQ_ENDED,
		      "each lane's done once, as ended");
		CHECK(dlq_last(DLQ_ART) == DLQ_ENDED && dlq_ends(DLQ_ART) == 1 && dlq_ends(DLQ_RA) == 1, "their ends are counted");
		CHECK(!dlq_busy(), "and nothing runs");
	}

	printf("dlq: a game\n");
	{
		fresh(&g_fa, "g", 2, 50);
		CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a), "a lane starts");
		dlq_game(true);
		run_out(400);
		CHECK(g_fa.done_n == 0 && !g_fa.started, "while a game runs it waits: nothing asked");
		CHECK(dlq_wait(80) == 80, "and the input wait is not cut for it");
		dlq_game(false);
		run_out(WAIT_MS);
		CHECK(g_fa.done_n == 2 && g_fa.dones == 1 && g_fa.how == DLQ_ENDED, "after the game it goes on to its end");
	}

	printf("dlq: stop\n");
	{
		fresh(&g_fa, "s", 5, 400);
		fresh(&g_fb, "t", 5, 400);
		CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a) && dlq_start(DLQ_RA, &LANE_B, begin_b), "two lanes start");
		dlq_tick();
		CHECK(g_fa.started && g_fb.started, "each has a request in flight");
		CHECK(dlq_pct() == 0, "nothing done yet: the ring at 0");
		dlq_stop(DLQ_ART);
		CHECK(g_fa.dones == 1 && g_fa.how == DLQ_STOPPED && !dlq_running(DLQ_ART), "Stop: its done, once, as stopped");
		{
			net_slot was = net_slot_use(NET_SLOT_ART);

			CHECK(!net_async_held(), "its request dropped");
			net_slot_use(was);
		}
		CHECK(dlq_running(DLQ_RA) && g_fb.dones == 0, "the other goes on");
		dlq_stop(DLQ_ART);
		CHECK(g_fa.dones == 1, "a second stop does nothing");
		dlq_stop_all();
		CHECK(!dlq_busy() && g_fb.dones == 1 && g_fb.how == DLQ_STOPPED, "POWER stops every lane");
	}

	printf("dlq: a lane that does not begin, the room, the wait\n");
	{
		unsigned ends = dlq_ends(DLQ_ART);

		fresh(&g_fa, "n", 1, 10);
		g_fa.fail_begin = true;
		CHECK(!dlq_start(DLQ_ART, &LANE_A, begin_a) && !dlq_running(DLQ_ART) && g_fa.dones == 0,
		      "a run that cannot begin is not started");
		fresh(&g_fa, "r", 3, 50);
		LANE_A.card = "/";
		CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a), "a lane on a card starts");
		CHECK(dlq_wait(80) <= 16, "the input wait is cut to 16 ms while it runs");
		run_out(WAIT_MS);
		CHECK(g_fa.dones == 1 && g_fa.how == DLQ_FULL && dlq_last(DLQ_ART) == DLQ_FULL && dlq_ends(DLQ_ART) == ends + 1,
		      "below the card's floor it is stopped, as full");
		LANE_A.card = NULL;
		CHECK(dlq_wait(80) == 80, "nothing running: the wait is as asked");
		fresh(&g_fb, "p", 2, 100);
		CHECK(dlq_start(DLQ_RA, &LANE_B, begin_b), "a lane starts for its page");
		CHECK(dlq_behind(), "behind the screens, the corner shows it");
		dlq_page(DLQ_RA, true);
		CHECK(dlq_page_open(DLQ_RA) && !dlq_page_open(DLQ_ART) && !dlq_behind(),
		      "its page open, the page shows it and the corner does not");
		dlq_page(DLQ_RA, false);
		dlq_stop_all();
	}

	printf("dlq: what each step brought, and the runs\n");
	{
		unsigned runs = dlq_runs();

		g_mine[DLQ_ART] = &g_fa;
		g_mine[DLQ_RA] = &g_fb;
		fresh(&g_fa, "h", 2, 30);
		fresh(&g_fb, "k", 3, 30);
		memset(g_hooked, 0, sizeof g_hooked);
		g_hook_wrong = 0;
		dlq_on_step(on_step);
		CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a) && dlq_runs() == runs + 1, "a lane started with none running is a run");
		CHECK(dlq_start(DLQ_RA, &LANE_B, begin_b) && dlq_runs() == runs + 1, "one started beside it is the same run");
		run_out(WAIT_MS);
		CHECK(g_fa.dones == 1 && g_fb.dones == 1, "both end");
		CHECK(g_hooked[DLQ_ART] == g_fa.steps && g_hooked[DLQ_RA] == g_fb.steps && g_fa.steps > 0,
		      "the hook after every step of each lane (a %d of %d, b %d of %d)", g_hooked[DLQ_ART], g_fa.steps,
		      g_hooked[DLQ_RA], g_fb.steps);
		CHECK(!g_hook_wrong, "each on its lane's slot, the last before its done (%d not)", g_hook_wrong);
		dlq_on_step(NULL);
		{
			int was = g_hooked[DLQ_ART];

			fresh(&g_fa, "m", 1, 10);
			CHECK(dlq_start(DLQ_ART, &LANE_A, begin_a) && dlq_runs() == runs + 2, "the next lane on its own, another run");
			run_out(WAIT_MS);
			CHECK(g_fa.dones == 1 && g_hooked[DLQ_ART] == was, "with no hook, nothing is called");
		}
	}

	if (failures) {
		printf("dlq: %d failed (log: " NET_TMP "/log)\n", failures);
		return 1;
	}
	printf("dlq: all passed\n");
	return 0;
}
