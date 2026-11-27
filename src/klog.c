/* SPDX-License-Identifier: GPL-3.0-only */
/* See klog.h. */
#include "klog.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

static const char *const KEPT[] = {
	"usb: ", "wire: ", "xfer: ", "mdns: ", "crash: ", "power: ", "watchdog: ",
};

bool klog_kept(const char *line, size_t n)
{
	size_t i;

	for (i = 0; i < sizeof KEPT / sizeof KEPT[0]; i++) {
		size_t k = strlen(KEPT[i]);

		if (n >= k && !memcmp(line, KEPT[i], k)) return true;
	}
	return false;
}

/* The kernel's log, open from klog_open on; -1 before or without it */
static int g_kmsg = -1;

/* What the kernel's log is given: "<13>zyperos: " and the line, its newline
 * ensured. LOG_USER's notice: under the console's level (7), so pstore's
 * console has it, as it has the kernel's own info lines. */
#define KMSG_HEAD "<13>zyperos: "
#define LINE_MAX_K 400

static void put_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);

		if (w < 0) {
			if (errno == EINTR) continue;
			return;
		}
		p += w;
		n -= (size_t)w;
	}
}

/* One record of the kernel's log: a single write. Async-signal-safe. */
static void kmsg_put(const char *line, size_t n)
{
	char rec[sizeof KMSG_HEAD + LINE_MAX_K + 1];
	size_t h = sizeof KMSG_HEAD - 1;

	if (g_kmsg < 0) return;
	while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
	if (n > LINE_MAX_K) n = LINE_MAX_K;
	memcpy(rec, KMSG_HEAD, h);
	memcpy(rec + h, line, n);
	rec[h + n] = '\n';
	if (write(g_kmsg, rec, h + n + 1) < 0) { /* the log file has it still */ }
}

void klog_kmsg(const char *line)
{
	kmsg_put(line, strlen(line));
}

void klog_raw(const char *line)
{
	size_t n = strlen(line);

	put_all(2, line, n);
	if (!n || line[n - 1] != '\n') put_all(2, "\n", 1);
	kmsg_put(line, n);
}

int klog_maxfd(void)
{
	long m = sysconf(_SC_OPEN_MAX);

	return m > 0 && m < 65536 ? (int)m : 1024;
}

void klog_child_fds(int maxfd)
{
	int fd;

	if (g_kmsg >= 0 && g_kmsg != 3) {
		if (dup2(g_kmsg, 3) == 3) g_kmsg = 3;
		else g_kmsg = -1;
	}
	fd = g_kmsg == 3 ? 4 : 3;
#if defined(__linux__) && defined(SYS_close_range)
	if (syscall(SYS_close_range, (unsigned)fd, ~0u, 0u) == 0) return;
#endif
	for (; fd < maxfd; fd++) close(fd);
}

/* ---- the last lines, for the watchdog -------------------------------- */

static pthread_mutex_t g_last_mx = PTHREAD_MUTEX_INITIALIZER;
static char g_last[LINE_MAX_K], g_last_kept[LINE_MAX_K];

static void last_set(char *dst, const char *line, size_t n)
{
	while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
	if (n >= LINE_MAX_K) n = LINE_MAX_K - 1;
	memcpy(dst, line, n);
	dst[n] = '\0';
}

void klog_last(char *line, size_t n, char *kept, size_t kn)
{
	if (line && n) line[0] = '\0';
	if (kept && kn) kept[0] = '\0';
	/* The watchdog asks from its own thread while the main one may be stuck
	 * anywhere, even holding this: then it says nothing rather than wait */
	if (pthread_mutex_trylock(&g_last_mx)) return;
	if (line && n) snprintf(line, n, "%s", g_last);
	if (kept && kn) snprintf(kept, kn, "%s", g_last_kept);
	pthread_mutex_unlock(&g_last_mx);
}

/* ---- the rate, and the sync thread ----------------------------------- */

/* A kept line in a loop would fill the kernel's log, and pstore's console
 * keeps only its last 512 KB: at most KMSG_BURST lines at once, a new one
 * every KMSG_EVERY_MS after. The rest are in zyperos.log as ever, and the
 * next line copied says how many were left out. */
#define KMSG_BURST    40
#define KMSG_EVERY_MS 250u

static unsigned g_mirrored, g_dropped, g_dropped_said, g_synced;
static int      g_tokens = KMSG_BURST;
static unsigned g_tok_at;

static unsigned mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

static bool kmsg_allowed(void)
{
	unsigned now = mono_ms(), gained = (now - g_tok_at) / KMSG_EVERY_MS;

	if (gained) {
		g_tokens = g_tokens + (int)gained > KMSG_BURST ? KMSG_BURST : g_tokens + (int)gained;
		g_tok_at += gained * KMSG_EVERY_MS;
	}
	if (g_tokens <= 0) return false;
	g_tokens--;
	return true;
}

static pthread_mutex_t g_sync_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_sync_cv = PTHREAD_COND_INITIALIZER;
static unsigned        g_sync_asked, g_sync_done;

static void *sync_run(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&g_sync_mx);
	for (;;) {
		unsigned want;

		while (g_sync_done == g_sync_asked) pthread_cond_wait(&g_sync_cv, &g_sync_mx);
		want = g_sync_asked;
		pthread_mutex_unlock(&g_sync_mx);
		/* Every line asked so far, in one sync: a burst is one */
#ifdef __APPLE__
		fsync(2);
#else
		fdatasync(2);
#endif
		pthread_mutex_lock(&g_sync_mx);
		g_sync_done = want;
		g_synced++;
		pthread_cond_broadcast(&g_sync_cv);
	}
	return NULL;
}

static bool g_sync_thread;

static void sync_ask(void)
{
	if (!g_sync_thread) return;
	pthread_mutex_lock(&g_sync_mx);
	g_sync_asked++;
	pthread_cond_broadcast(&g_sync_cv);
	pthread_mutex_unlock(&g_sync_mx);
}

bool klog_settle(unsigned ms)
{
	unsigned t0 = mono_ms();
	bool done;

	for (;;) {
		pthread_mutex_lock(&g_sync_mx);
		done = g_sync_done == g_sync_asked;
		pthread_mutex_unlock(&g_sync_mx);
		if (done || mono_ms() - t0 >= ms) return done;
		usleep(2000);
	}
}

void klog_rate_reset(void)
{
	g_tokens = KMSG_BURST;
	g_tok_at = mono_ms();
}

unsigned klog_mirrored(void) { return g_mirrored; }
unsigned klog_dropped(void) { return g_dropped; }
unsigned klog_synced(void) { return g_synced; }

/* ---- the filter in front of stderr ----------------------------------- */

/* A whole line, from the stream (its lock held: one writer at a time) */
static void line_done(const char *line, size_t n)
{
	bool kept = klog_kept(line, n);

	pthread_mutex_lock(&g_last_mx);
	last_set(g_last, line, n);
	if (kept) last_set(g_last_kept, line, n);
	pthread_mutex_unlock(&g_last_mx);
	if (!kept) return;
	if (g_kmsg >= 0) {
		if (kmsg_allowed()) {
			if (g_dropped != g_dropped_said) {
				char note[96];
				int k = snprintf(note, sizeof note,
				                 "log: %u kept lines not copied here, over the rate",
				                 g_dropped - g_dropped_said);

				g_dropped_said = g_dropped;
				if (k > 0) kmsg_put(note, (size_t)k);
			}
			kmsg_put(line, n);
			g_mirrored++;
		} else {
			g_dropped++;
		}
	}
	sync_ask();
}

static char   g_part[LINE_MAX_K];
static size_t g_part_n;
static bool   g_part_long;      /* the line so far was cut at LINE_MAX_K */

static void stream_bytes(const char *buf, size_t n)
{
	while (n) {
		const char *nl = memchr(buf, '\n', n);
		size_t take = nl ? (size_t)(nl - buf) + 1 : n;

		if (!g_part_n && !g_part_long && nl) {
			line_done(buf, take);           /* the usual: a line in one piece */
		} else {
			size_t room = sizeof g_part - g_part_n, k = take < room ? take : room;

			memcpy(g_part + g_part_n, buf, k);
			g_part_n += k;
			if (k < take) g_part_long = true;
			if (nl) {
				line_done(g_part, g_part_n);
				g_part_n = 0;
				g_part_long = false;
			}
		}
		buf += take;
		n -= take;
	}
}

static ssize_t filter_write(const char *buf, size_t n)
{
	put_all(2, buf, n);
	stream_bytes(buf, n);
	/* All of it, always: stderr never fails its caller */
	return (ssize_t)n;
}

#if defined(__GLIBC__)
static ssize_t cookie_write(void *c, const char *buf, size_t n)
{
	(void)c;
	return filter_write(buf, n);
}

static FILE *filter_stream(void)
{
	cookie_io_functions_t io = { .read = NULL, .write = cookie_write, .seek = NULL, .close = NULL };

	return fopencookie(NULL, "w", io);
}
#elif defined(__APPLE__)
static int fun_write(void *c, const char *buf, int n)
{
	(void)c;
	return n > 0 ? (int)filter_write(buf, (size_t)n) : 0;
}

static FILE *filter_stream(void)
{
	return funopen(NULL, NULL, fun_write, NULL, NULL);
}
#else
static FILE *filter_stream(void) { return NULL; }
#endif

bool klog_open(const char *kmsg)
{
	static bool opened;
	pthread_t t;
	FILE *f;

	if (opened) return g_kmsg >= 0;
	opened = true;
	if ((g_kmsg = open(kmsg ? kmsg : "/dev/kmsg", O_WRONLY | O_APPEND | O_CLOEXEC)) < 0) return false;
	if (!kmsg) {
		/* Not "ratelimit": the kernel keeps 10 lines in 5 s per writer
		 * and drops the rest, a cable's plug says more than that, and
		 * this one has its own rate (KMSG_BURST) */
		int fd = open("/proc/sys/kernel/printk_devkmsg", O_WRONLY | O_CLOEXEC);

		if (fd >= 0) {
			if (write(fd, "on\n", 3) < 0) { /* ratelimited, then */ }
			close(fd);
		}
	}
	if (!(f = filter_stream())) {
		close(g_kmsg);
		g_kmsg = -1;
		return false;
	}
	/* A line at a time, as the filter wants them; a line longer than this
	 * comes in pieces and is put back together (stream_bytes) */
	setvbuf(f, NULL, _IOLBF, 4096);
	fflush(stderr);
	g_tok_at = mono_ms();
	g_sync_thread = pthread_create(&t, NULL, sync_run, NULL) == 0;
	if (g_sync_thread) pthread_detach(t);
	stderr = f;
	return true;
}
