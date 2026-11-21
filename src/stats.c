/* SPDX-License-Identifier: GPL-3.0-only */
/* See stats.h. No SDL and no platform header, so the check can drive it
 * without a window - `now_ms` comes from the caller for the same reason
 * idle_check takes one.
 *
 * A row is "sess.<start>.<tag>\t<file>" -> "<seconds>\t<state>", where state
 * is "open" while the game is running and the EXIT reason afterwards. The tab
 * is the separator for the reason favorites use one: a ROM filename may
 * legally contain almost anything except a tab or a newline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db.h"
#include "i18n.h"
#include "stats.h"

/* --- the session in progress -------------------------------------------- */
static char     g_tag[STATS_TAG_MAX];
static char     g_file[STATS_FILE_MAX];
static long     g_start;          /* unix time, the key's middle segment */
static unsigned g_t0;             /* now_ms at RUN, for the elapsed seconds */
static unsigned g_written;        /* now_ms of the last row written, 0 = none */
static bool     g_running;

/* The millisecond clock is in the key, not for ordering but for UNIQUENESS.
 * Two launches of the same game inside one wall-clock second would otherwise
 * share a key and the second would overwrite the first - which is what the
 * check found, by launching Contra twice in a row with no sleep between.
 *
 * It has to be something already in hand, because stats_begin does no I/O:
 * asking the database whether a key is taken would put a read on the launch
 * path, which is the one thing this design refuses. g_t0 is monotonic and
 * costs nothing. */
static void session_key(char *out, size_t n, long start, unsigned t0,
                        const char *tag, const char *file)
{
	snprintf(out, n, "sess.%ld.%u.%.*s\t%.*s", start, t0,
	         STATS_TAG_MAX - 1, tag, STATS_FILE_MAX - 1, file);
}

/* Elapsed in whole seconds. unsigned arithmetic, so a wrap of the millisecond
 * clock subtracts correctly rather than producing a session of 49 days. */
static long elapsed_s(unsigned now_ms)
{
	return (long)((now_ms - g_t0) / 1000u);
}

static void write_row(unsigned now_ms, const char *state)
{
	char key[STATS_TAG_MAX + STATS_FILE_MAX + 56], val[64];

	session_key(key, sizeof key, g_start, g_t0, g_tag, g_file);
	snprintf(val, sizeof val, "%ld\t%s", elapsed_s(now_ms), state);
	db_set_str(db_lib(), key, val);
	g_written = now_ms;
}

void stats_begin(const char *tag, const char *file, unsigned now_ms)
{
	g_running = false;
	if (!tag || !file || !*tag || !*file) return;
	if (strlen(tag) >= STATS_TAG_MAX || strlen(file) >= STATS_FILE_MAX) return;

	/* Everything here is memory. Nothing is opened, written or synced - the
	 * whole point of the design is that a launch costs nothing. */
	snprintf(g_tag, sizeof g_tag, "%s", tag);
	snprintf(g_file, sizeof g_file, "%s", file);
	g_start   = (long)time(NULL);
	g_t0      = now_ms;
	g_written = 0;
	g_running = true;
}

void stats_tick(unsigned now_ms)
{
	if (!g_running) return;

	/* Nothing at all until the session is worth recording, so backing
	 * straight out of a game leaves no row behind. */
	if (!g_written) {
		if (now_ms - g_t0 >= STATS_MARK_MS) write_row(now_ms, "open");
		return;
	}
	if (now_ms - g_written >= STATS_CKPT_MS) write_row(now_ms, "open");
}

void stats_end(const char *reason, unsigned now_ms)
{
	if (!g_running) return;
	g_running = false;

	/* A session too short to have written a marker still gets its row here,
	 * because by now we know it ended and how long it was. The marker exists
	 * for sessions that never reach this point, not for this one. */
	write_row(now_ms, reason && *reason ? reason : "quit");
}

/* --- recovery ------------------------------------------------------------ */

struct open_row { char key[STATS_TAG_MAX + STATS_FILE_MAX + 40];
                  char val[64]; struct open_row *next; };

static bool collect_open(const char *key, const char *value, void *ctx)
{
	struct open_row **head = ctx, *r;
	const char *tab = strchr(value, '\t');

	if (!tab || strcmp(tab + 1, "open") != 0) return true;
	if (!(r = malloc(sizeof *r))) return false;
	snprintf(r->key, sizeof r->key, "%s", key);
	snprintf(r->val, sizeof r->val, "%.*s\tlost", (int)(tab - value), value);
	r->next = *head;
	*head = r;
	return true;
}

void stats_recover(void)
{
	struct open_row *open = NULL, *r;

	/* Collected before anything is written. Rewriting rows while enumerating
	 * the same prefix is asking a question and changing the answer at once -
	 * the same care fav_save needs for its deletions. */
	db_each_prefix(db_lib(), "sess.", collect_open, &open);
	while ((r = open)) {
		open = r->next;
		/* The seconds stay as they were: the last checkpoint is the most that
		 * can honestly be claimed for a session nobody saw end. Marked so it
		 * is not mistaken for a clean quit later. */
		db_set_str(db_lib(), r->key, r->val);
		free(r);
	}
}

/* --- a program of its own (stats.h) -------------------------------------- */

long long stats_mono_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool stats_hand_over(const char *tag, const char *file, const char *note,
                     long now, long long mono_ms)
{
	char key[STATS_TAG_MAX + STATS_FILE_MAX + 56], tmp[512];
	FILE *f;
	bool ok;

	if (!tag || !file || !note || !*tag || !*file) return false;
	if (strlen(tag) >= STATS_TAG_MAX || strlen(file) >= STATS_FILE_MAX) return false;
	if (strchr(tag, '\t') || strchr(file, '\t') || strchr(file, '\n')) return false;
	/* The note first, whole, then renamed in: the row alone would be a
	 * session nothing could close, and a note cut short a session read
	 * wrong */
	snprintf(tmp, sizeof tmp, "%s.tmp", note);
	f = fopen(tmp, "w");
	ok = f && fprintf(f, "%ld\t%u\t%lld\t%s\t%s\n", now, (unsigned)mono_ms, mono_ms, tag, file) > 0;
	if (f && fclose(f) != 0) ok = false;
	if (!ok || rename(tmp, note) != 0) {
		remove(tmp);
		return false;
	}
	/* The key's middle number keeps two starts in one second apart, as
	 * stats_begin's millisecond clock does; this one is the monotonic */
	session_key(key, sizeof key, now, (unsigned)mono_ms, tag, file);
	db_set_str(db_lib(), key, "0\topen");
	return true;
}

bool stats_handed_back(const char *note, long long mono_ms)
{
	char line[STATS_TAG_MAX + STATS_FILE_MAX + 80], key[STATS_TAG_MAX + STATS_FILE_MAX + 56];
	char val[64], tag[STATS_TAG_MAX];
	const char *t1, *t2, *t3, *t4;
	long long t0;
	unsigned long kn;
	long start, secs;
	FILE *f = fopen(note, "r");
	bool got;

	if (!f) return false;
	got = fgets(line, sizeof line, f) != NULL;
	fclose(f);
	remove(note);
	if (!got) return false;
	line[strcspn(line, "\n")] = '\0';
	/* <start>\t<key>\t<mono_ms>\t<tag>\t<file>: the key's number as the
	 * row was written, the clock apart from it */
	if (!(t1 = strchr(line, '\t')) || !(t2 = strchr(t1 + 1, '\t')) || !(t3 = strchr(t2 + 1, '\t')) ||
	    !(t4 = strchr(t3 + 1, '\t')))
		return false;
	if ((size_t)(t4 - t3 - 1) >= STATS_TAG_MAX || t4 == t3 + 1 || !t4[1]) return false;
	start = strtol(line, NULL, 10);
	kn = strtoul(t1 + 1, NULL, 10);
	t0 = strtoll(t2 + 1, NULL, 10);
	/* The clock is not held to be past zero: a launcher writes it at or
	 * after boot, and a check that puts the start an hour back on a computer
	 * up for minutes (CI's Linux runners, 2026-11-21) puts it before boot.
	 * The seconds are only ever the difference */
	if (start <= 0) return false;
	snprintf(tag, sizeof tag, "%.*s", (int)(t4 - t3 - 1), t3 + 1);
	/* A clock that went back is a boot between the two, which /tmp does
	 * not outlive: nothing honest to claim, so nothing is */
	secs = mono_ms >= t0 ? (long)((mono_ms - t0) / 1000) : 0;
	session_key(key, sizeof key, start, (unsigned)kn, tag, t4 + 1);
	snprintf(val, sizeof val, "%ld\tquit", secs);
	db_set_str(db_lib(), key, val);
	return true;
}

/* --- a game deleted from the card ---------------------------------------- */

struct gone_row { char key[STATS_TAG_MAX + STATS_FILE_MAX + 56]; struct gone_row *next; };
struct gone_ctx { const char *tag, *file; struct gone_row *head; };

/* sess.<start>.<t0>.<tag>\t<file>, for this tag and file exactly */
static bool collect_gone(const char *key, const char *value, void *ctx)
{
	struct gone_ctx *g = ctx;
	const char *dot = strchr(key + strlen("sess."), '.'), *tab;
	struct gone_row *r;
	size_t tl = strlen(g->tag);

	(void)value;
	if (!dot || !(dot = strchr(dot + 1, '.')) || !(tab = strchr(dot + 1, '\t'))) return true;
	if ((size_t)(tab - dot - 1) != tl || strncmp(dot + 1, g->tag, tl) || strcmp(tab + 1, g->file))
		return true;
	if (!(r = malloc(sizeof *r))) return false;
	snprintf(r->key, sizeof r->key, "%s", key);
	r->next = g->head;
	g->head = r;
	return true;
}

int stats_forget(const char *tag, const char *file)
{
	struct gone_ctx g = { tag, file, NULL };
	struct gone_row *r;
	int n = 0;

	if (!tag || !file) return 0;
	/* Collected first, then deleted: the same care stats_recover takes */
	db_each_prefix(db_lib(), "sess.", collect_gone, &g);
	while ((r = g.head)) {
		g.head = r->next;
		if (db_del(db_lib(), r->key)) n++;
		free(r);
	}
	return n;
}

/* --- summarizing --------------------------------------------------------- */

typedef struct {
	char tag[STATS_TAG_MAX];
	char file[STATS_FILE_MAX];
	long seconds;
	int  launches;
	long longest;      /* the longest single session folded into this row */
	long last;         /* unix time that session started */
	int  lost;         /* of those launches, how many never reached EXIT */
} stats_row;

static stats_row g_rows[STATS_MAX];
static int  g_nrows;
static long g_total;
static int  g_launches;
static int  g_lost;

/* What one summarizing pass is filtering and grouping by. Passed through
 * db_each_prefix's ctx rather than kept in a global, so the fold has no state
 * of its own to get out of step with the call that started it. */
typedef struct {
	long cutoff;       /* sessions that started before this are not counted */
	bool by_system;
} fold_ctx;

static int find_row(const char *tag, const char *file)
{
	int i;
	for (i = 0; i < g_nrows; i++)
		if (!strcmp(g_rows[i].tag, tag) && !strcmp(g_rows[i].file, file))
			return i;
	return -1;
}

const char *stats_window_name(stats_window w)
{
	switch (w) {
	case STATS_YEAR:  return T(S_STATS_THIS_YEAR);
	case STATS_MONTH: return T(S_STATS_THIS_MONTH);
	case STATS_WEEK:  return T(S_STATS_THIS_WEEK);
	case STATS_TODAY: return T(S_STATS_TODAY_WINDOW);
	default:          return T(S_STATS_ALL_TIME);
	}
}

/* Midnight local, then back to the start of the week, month or year.
 *
 * mktime is what makes this correct rather than arithmetic on seconds: a day
 * is not always 86400 seconds where daylight saving exists, and a month is
 * never a fixed number of them. Filling in a struct tm and asking mktime what
 * that instant was is the only way to get "the first of this month" right
 * without owning a calendar. */
long stats_window_start(stats_window w, long now)
{
	time_t t = (time_t)now;
	struct tm tm;

	if (w == STATS_ALL) return 0;
	tm = *localtime(&t);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;              /* let mktime decide; -1, never 0 */
	switch (w) {
	case STATS_YEAR:  tm.tm_mon = 0;  /* fall through */
	case STATS_MONTH: tm.tm_mday = 1; break;
	/* Weeks start Sunday here because tm_wday does. Nothing in the UI names
	 * the first day, so this is a convention, not a claim. */
	case STATS_WEEK:  tm.tm_mday -= tm.tm_wday; break;
	default: break;
	}
	return (long)mktime(&tm);
}

static bool fold(const char *key, const char *value, void *ctx)
{
	/* sess.<start>.<t0>.<tag>\t<file>. The first segment was skipped along
	 * with the second until the windows needed it: it is when the session
	 * began, which is what "this week" is asked against. */
	const char *rest = key + strlen("sess.");
	const char *dot = strchr(rest, '.');
	const char *tab, *sep = strchr(value, '\t');
	const fold_ctx *f = ctx;
	char tag[STATS_TAG_MAX];
	long secs, start;
	int i;

	if (!dot || !sep) return true;
	start = strtol(rest, NULL, 10);
	if (!(dot = strchr(dot + 1, '.'))) return true;
	if (!(tab = strchr(dot + 1, '\t'))) return true;
	if ((size_t)(tab - dot - 1) >= STATS_TAG_MAX) return true;
	secs = strtol(value, NULL, 10);
	if (secs < 0) return true;
	if (start < f->cutoff) return true;      /* outside the window */

	snprintf(tag, sizeof tag, "%.*s", (int)(tab - dot - 1), dot + 1);
	/* By system, every session on a machine lands in one row, and the file
	 * is empty because the row is not about a file. */
	i = find_row(tag, f->by_system ? "" : tab + 1);
	if (i < 0) {
		if (g_nrows >= STATS_MAX) return false;
		i = g_nrows++;
		snprintf(g_rows[i].tag, STATS_TAG_MAX, "%s", tag);
		snprintf(g_rows[i].file, STATS_FILE_MAX, "%s",
		         f->by_system ? "" : tab + 1);
		g_rows[i].seconds = 0;
		g_rows[i].launches = 0;
		g_rows[i].longest = 0;
		g_rows[i].last = 0;
		g_rows[i].lost = 0;
	}
	g_rows[i].seconds += secs;
	g_rows[i].launches++;
	if (secs > g_rows[i].longest) g_rows[i].longest = secs;
	if (start > g_rows[i].last)   g_rows[i].last = start;
	/* "open" counts as lost too. A row still marked open during a summary is
	 * a session this boot has not recovered, which is the same event. */
	if (strcmp(sep + 1, "quit") != 0) { g_rows[i].lost++; g_lost++; }
	g_total += secs;
	g_launches++;
	return true;
}

static int by_seconds(const void *a, const void *b)
{
	const stats_row *x = a, *y = b;
	if (x->seconds != y->seconds) return x->seconds < y->seconds ? 1 : -1;
	return strcmp(x->file, y->file);      /* stable enough to read twice */
}

/* stats_recent's pass: the rows kept so far, newest-first once done */
typedef struct {
	stats_recent_row *out;
	int n, max;
} recent_ctx;

static bool recent_fold(const char *key, const char *value, void *ctx)
{
	const char *rest = key + strlen("sess.");
	const char *dot = strchr(rest, '.'), *tab;
	recent_ctx *r = ctx;
	char tag[STATS_TAG_MAX];
	long start;
	int i, oldest = -1;

	(void)value;
	if (!dot) return true;
	start = strtol(rest, NULL, 10);
	if (!(dot = strchr(dot + 1, '.'))) return true;
	if (!(tab = strchr(dot + 1, '\t'))) return true;
	if ((size_t)(tab - dot - 1) >= STATS_TAG_MAX || !tab[1]) return true;
	snprintf(tag, sizeof tag, "%.*s", (int)(tab - dot - 1), dot + 1);
	for (i = 0; i < r->n; i++) {
		if (!strcmp(r->out[i].tag, tag) && !strcmp(r->out[i].file, tab + 1)) {
			if (start > r->out[i].last) r->out[i].last = start;
			return true;
		}
		if (oldest < 0 || r->out[i].last < r->out[oldest].last) oldest = i;
	}
	/* A new game: in, or in place of the one started longest ago when that
	 * one is older */
	if (r->n < r->max) i = r->n++;
	else if (oldest >= 0 && start > r->out[oldest].last) i = oldest;
	else return true;
	snprintf(r->out[i].tag, sizeof r->out[i].tag, "%s", tag);
	snprintf(r->out[i].file, sizeof r->out[i].file, "%s", tab + 1);
	r->out[i].last = start;
	return true;
}

static int by_last(const void *a, const void *b)
{
	const stats_recent_row *x = a, *y = b;

	if (x->last != y->last) return x->last < y->last ? 1 : -1;
	return strcmp(x->file, y->file);
}

int stats_recent(stats_recent_row *out, int max)
{
	recent_ctx r = { out, 0, max };

	if (!out || max <= 0) return 0;
	db_each_prefix(db_lib(), "sess.", recent_fold, &r);
	qsort(out, (size_t)r.n, sizeof *out, by_last);
	return r.n;
}

int stats_summarize(stats_window w, bool by_system, long now)
{
	fold_ctx f;

	f.cutoff = stats_window_start(w, now);
	f.by_system = by_system;
	g_nrows = 0;
	g_total = 0;
	g_launches = 0;
	g_lost = 0;
	db_each_prefix(db_lib(), "sess.", fold, &f);
	qsort(g_rows, g_nrows, sizeof g_rows[0], by_seconds);
	return g_nrows;
}

bool stats_at(int i, const char **tag, const char **file,
              long *seconds, int *launches)
{
	if (i < 0 || i >= g_nrows) return false;
	if (tag)      *tag      = g_rows[i].tag;
	if (file)     *file     = g_rows[i].file;
	if (seconds)  *seconds  = g_rows[i].seconds;
	if (launches) *launches = g_rows[i].launches;
	return true;
}

bool stats_lookup(const char *tag, const char *file,
                  long *seconds, long *last)
{
	int i;

	if (!tag || !file) return false;
	i = find_row(tag, file);
	if (i < 0) return false;
	if (seconds) *seconds = g_rows[i].seconds;
	if (last)    *last    = g_rows[i].last;
	return true;
}

bool stats_extra(int i, long *longest, long *last, int *lost)
{
	if (i < 0 || i >= g_nrows) return false;
	if (longest) *longest = g_rows[i].longest;
	if (last)    *last    = g_rows[i].last;
	if (lost)    *lost    = g_rows[i].lost;
	return true;
}

long stats_total_seconds(void) { return g_total; }
int  stats_total_launches(void) { return g_launches; }
int  stats_total_lost(void) { return g_lost; }

void stats_format(long seconds, char *out, size_t n)
{
	if (!out || !n) return;
	if (seconds <= 0)      snprintf(out, n, "%s", T(S_STATS_NEVER));
	else if (seconds < 60) snprintf(out, n, T(S_STATS_SECS), seconds);
	else if (seconds < 3600)
		snprintf(out, n, T(S_STATS_MINS_SECS), seconds / 60, seconds % 60);
	else
		snprintf(out, n, T(S_STATS_HOURS_MINS), seconds / 3600, (seconds % 3600) / 60);
}

/* Days apart on the CALENDAR, not elapsed hours divided by 24: something
 * played at 11pm was played yesterday when read at 1am, and "2h ago" would be
 * a true sentence that answers the wrong question. */
static long calendar_days(long then, long now)
{
	time_t a = (time_t)then, b = (time_t)now;
	struct tm ta, tb;

	ta = *localtime(&a);
	tb = *localtime(&b);
	ta.tm_hour = ta.tm_min = ta.tm_sec = 0; ta.tm_isdst = -1;
	tb.tm_hour = tb.tm_min = tb.tm_sec = 0; tb.tm_isdst = -1;
	return ((long)mktime(&tb) - (long)mktime(&ta) + 43200) / 86400;
}

void stats_ago(long then, long now, char *out, size_t n)
{
	long days;

	if (!out || !n) return;
	if (then <= 0) { snprintf(out, n, "%s", T(S_STATS_NEVER)); return; }
	days = calendar_days(then, now);

	if (days <= 0)     snprintf(out, n, "%s", T(S_STATS_TODAY));
	else if (days == 1) snprintf(out, n, "%s", T(S_STATS_YESTERDAY));
	else if (days < 7)  snprintf(out, n, T(S_STATS_DAYS_AGO), days);
	else if (days < 60) snprintf(out, n, T(S_STATS_WEEKS_AGO), days / 7);
	else                snprintf(out, n, T(S_STATS_MONTHS_AGO), days / 30);
}

/* The clock for today and yesterday, as the Screenshots shelf has always put
 * it after the day (src/shots_screen.c, shots_facts): 24-hour "21:40", or
 * 12-hour "9:40 PM" with no leading zero */
void stats_when(long then, long now, bool clock24, char *out, size_t n)
{
	char ago[32], clock[16];
	time_t t = (time_t)then;
	struct tm tm;

	if (!out || !n) return;
	stats_ago(then, now, ago, sizeof ago);
	if (then <= 0 || calendar_days(then, now) > 1) {
		snprintf(out, n, "%s", ago);
		return;
	}
	localtime_r(&t, &tm);
	strftime(clock, sizeof clock, clock24 ? "%H:%M" : "%I:%M %p", &tm);
	snprintf(out, n, "%s %s", ago, clock[0] == '0' && !clock24 ? clock + 1 : clock);
}
