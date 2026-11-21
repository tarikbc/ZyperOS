/* Play time: does it record what happened, and does it stay off the launch?
 *
 * The second half is the one worth a check. "Launch does no I/O" is a claim
 * about a path nobody watches, and the way it stops being true is one
 * reasonable-looking write at a time - so it is asserted here rather than
 * remembered. The rest is arithmetic that a power cut is allowed to interrupt
 * at any point, which is exactly the sort of thing a test can enumerate and a
 * person cannot.
 *
 * Time is passed in, the way idle_check takes it, so a checkpoint interval and
 * a clock wrap can be driven directly instead of waited for.
 *
 * Links src/stats.c and src/db.c and NOT SDL.
 */
#include "../src/db.h"
#include "../src/stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fails;
static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* This run's databases: the process id in the name, so two runs at once (two
 * worktrees, or make -j) never share or delete each other's. Set in main. */
static char DEV[64], LIB[64];

static void scrub(void)
{
	const char *base[] = { DEV, LIB };
	const char *suf[] = { "", "-wal", "-shm" };
	char p[160];
	unsigned i, j;
	db_shutdown();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++) {
			snprintf(p, sizeof p, "%s%s", base[i], suf[j]);
			unlink(p);
		}
	db_init(DEV, LIB, NULL);
}

static bool count_cb(const char *k, const char *v, void *ctx)
{
	(void)k; (void)v; (*(int *)ctx)++; return true;
}
static int rows(void)
{
	int n = 0;
	db_each_prefix(db_lib(), "sess.", count_cb, &n);
	return n;
}

static bool grab(const char *k, const char *v, void *ctx)
{
	(void)k;
	snprintf(ctx, 64, "%s", v);
	return false;                       /* the first is enough */
}
static const char *only_value(void)
{
	static char v[64];
	v[0] = '\0';
	db_each_prefix(db_lib(), "sess.", grab, v);
	return v;
}

/* THE property. A launch that touches storage is the failure this design
 * exists to prevent, and it would be invisible in every other test here. */
static void launch_writes_nothing(void)
{
	printf("a launch writes nothing at all:\n");
	scrub();
	ck(rows() == 0, "the store starts empty");
	stats_begin("NES", "Contra (USA).zip", 1000);
	ck(rows() == 0, "stats_begin wrote no row");
	stats_tick(1100);
	ck(rows() == 0, "and neither did the first tick");
	stats_tick(1000 + STATS_MARK_MS - 1);
	ck(rows() == 0, "nor a tick just before the marker is due");
}

static void a_short_session_leaves_no_marker(void)
{
	printf("backing straight out of a game:\n");
	scrub();
	stats_begin("NES", "Contra (USA).zip", 1000);
	stats_tick(2000);
	ck(rows() == 0, "nothing written during a two-second visit");
	stats_end(NULL, 3000);
	ck(rows() == 1, "but the session itself is still recorded");
	ck(!strcmp(only_value(), "2\tquit"), "two seconds, quit");
}

static void a_long_session_marks_and_checkpoints(void)
{
	printf("a session long enough to be worth recovering:\n");
	scrub();
	stats_begin("GBA", "Sigma Star Saga.zip", 0);
	stats_tick(STATS_MARK_MS);
	ck(rows() == 1, "the marker lands once the session is worth it");
	ck(!strcmp(only_value(), "5\topen"), "and says it is still open");

	stats_tick(STATS_MARK_MS + 1000);
	ck(!strcmp(only_value(), "5\topen"), "a tick before the interval changes nothing");

	stats_tick(STATS_MARK_MS + STATS_CKPT_MS);
	ck(!strcmp(only_value(), "65\topen"), "the checkpoint moves it forward");
	ck(rows() == 1, "and updates the row rather than adding one");

	stats_end("quit", 120000);
	ck(rows() == 1, "the end still updates the same row");
	ck(!strcmp(only_value(), "120\tquit"), "with the final length and reason");
}

static void a_power_cut_is_recovered_at_the_checkpoint(void)
{
	printf("a session the device never saw end:\n");
	scrub();
	stats_begin("SNES", "Super Metroid.zip", 0);
	stats_tick(STATS_MARK_MS);
	stats_tick(STATS_MARK_MS + STATS_CKPT_MS);      /* 65s on the card */
	/* and then the battery goes. No stats_end, ever. */

	db_shutdown();
	db_init(DEV, LIB, NULL);                        /* next boot */
	ck(!strcmp(only_value(), "65\topen"), "it is still marked open");
	stats_recover();
	ck(!strcmp(only_value(), "65\tlost"),
	   "recovery closes it at the last checkpoint, and says it was lost");
	ck(rows() == 1, "without inventing a second row");

	stats_recover();
	ck(!strcmp(only_value(), "65\tlost"), "and running recovery twice is a no-op");
}

static void the_clock_wrapping_is_not_a_49_day_session(void)
{
	printf("the millisecond clock wrapping:\n");
	scrub();
	/* SDL_GetTicks is 32-bit and wraps after ~49 days of uptime. Signed
	 * arithmetic here would turn a 10-second session into a colossal one. */
	stats_begin("GB", "Link's Awakening.zip", 0xFFFFF000u);
	stats_end("quit", 0xFFFFF000u + 10000u);        /* wraps past zero */
	ck(!strcmp(only_value(), "10\tquit"), "ten seconds, not forty-nine days");
}

static void two_systems_can_hold_the_same_filename(void)
{
	printf("the same file on two shelves is two games:\n");
	scrub();
	stats_begin("GB", "Tetris.zip", 0);   stats_end("quit", 60000);
	stats_begin("GBC", "Tetris.zip", 0);  stats_end("quit", 30000);
	ck(stats_summarize(STATS_ALL, false, time(NULL)) == 2,
	   "two distinct games");
}

static void summarize_folds_and_ranks(void)
{
	const char *tag, *file;
	long secs;
	int launches;

	printf("summarizing:\n");
	scrub();
	/* Contra twice, Metroid once and longer. The two Contra runs start in
	 * the same wall-clock second on purpose - back to back with no sleep is
	 * exactly how the key collision was found. */
	stats_begin("NES", "Contra.zip", 0);       stats_end("quit", 60000);
	stats_begin("NES", "Contra.zip", 1);       stats_end("quit", 120001);
	stats_begin("SNES", "Metroid.zip", 0);     stats_end("quit", 600000);

	ck(stats_summarize(STATS_ALL, false, time(NULL)) == 2, "two games");
	ck(stats_total_seconds() == 60 + 120 + 600, "total is every session");
	ck(stats_total_launches() == 3, "three launches");

	ck(stats_at(0, &tag, &file, &secs, &launches), "the first row reads");
	ck(!strcmp(file, "Metroid.zip"), "most played first");
	ck(secs == 600 && launches == 1, "with its own total and count");

	ck(stats_at(1, &tag, &file, &secs, &launches), "the second row reads");
	ck(!strcmp(file, "Contra.zip") && secs == 180 && launches == 2,
	   "and the two Contra sessions folded into one row");

	ck(!stats_at(2, NULL, NULL, NULL, NULL), "and there is no third");
}

/* A session at a chosen moment in the past.
 *
 * stats_begin reads the clock itself, and correctly so - a launch does no I/O
 * and has nothing to ask - which leaves a window test no way to place a
 * session anywhere but now. So it writes the row it wants to see. This is the
 * only code outside stats.c that knows the key format, and if the two ever
 * disagree, the tests below stop folding anything and say so loudly. */
static void seed(long start, const char *tag, const char *file,
                 long secs, const char *state)
{
	static unsigned uniq;
	char k[STATS_TAG_MAX + STATS_FILE_MAX + 56], v[64];

	snprintf(k, sizeof k, "sess.%ld.%u.%s\t%s", start, ++uniq, tag, file);
	snprintf(v, sizeof v, "%ld\t%s", secs, state);
	db_set_str(db_lib(), k, v);
}

/* The boundaries are asked of stats_window_start rather than computed here,
 * because a check that recomputes them with the same arithmetic proves only
 * that the arithmetic was copied correctly. What is asserted is the PROPERTY:
 * a second before a boundary is outside the window and a second after is
 * inside it. That holds in any timezone, which a hardcoded date would not. */
/* An instant on the local clock, which main sets to a zone with daylight
 * saving time so the edges below are the same wherever the check runs */
static long local_at(int y, int mon, int d, int h, int mi)
{
	struct tm tm = { 0 };

	tm.tm_year = y - 1900; tm.tm_mon = mon - 1; tm.tm_mday = d;
	tm.tm_hour = h; tm.tm_min = mi; tm.tm_isdst = -1;
	return (long)mktime(&tm);
}

/* The same clock time `days` calendar days before, as a person counts them:
 * across a clock change that is not days * 86400 seconds */
static long days_before(long now, int days)
{
	time_t t = (time_t)now;
	struct tm tm = *localtime(&t);

	tm.tm_mday -= days;
	tm.tm_isdst = -1;
	return (long)mktime(&tm);
}

/* When the windows and "ago" are checked: now, and the edges that broke or
 * could. Checked at the real clock alone, the result was the date's: the
 * week assertion below failed on Thursday 2026-10-01, and "3d ago" read "4d
 * ago" just after midnight for three days after the clocks went forward. */
static int instants(long *at)
{
	int n = 0;

	at[n++] = (long)time(NULL);
	at[n++] = local_at(2026, 10, 1, 12, 0);   /* a Thursday; its week began in September */
	at[n++] = local_at(2026, 3, 9, 0, 30);    /* the days after the clocks went forward */
	at[n++] = local_at(2026, 3, 10, 0, 30);
	at[n++] = local_at(2026, 3, 11, 0, 30);
	at[n++] = local_at(2026, 11, 2, 0, 30);   /* the day after they went back */
	at[n++] = local_at(2027, 1, 1, 0, 10);    /* a new year */
	at[n++] = local_at(2027, 3, 1, 0, 5);     /* the day after a February */
	return n;
}

static void windows_at(long now)
{
	int w;

	ck(stats_window_start(STATS_ALL, now) == 0, "all time starts at zero");
	ck(stats_window_start(STATS_TODAY, now) >= stats_window_start(STATS_WEEK, now),
	   "today starts no earlier than this week");
	/* Not "this week starts no earlier than this month": weeks straddle
	 * months and years, so on the first days of a month the week began in
	 * the last one. That assertion failed on Thursday 2026-10-01, whose week
	 * began on Sunday 27 September (now one of the instants). What is always
	 * true is that the week began less than seven days before today: six days
	 * and an hour at most, across a clock change. */
	ck(stats_window_start(STATS_TODAY, now) - stats_window_start(STATS_WEEK, now)
	       < 7L * 24 * 3600,
	   "this week began less than seven days before today");
	ck(stats_window_start(STATS_MONTH, now) >= stats_window_start(STATS_YEAR, now),
	   "this month starts no earlier than this year");
	ck(stats_window_start(STATS_YEAR, now) <= now, "this year has begun");

	for (w = STATS_YEAR; w <= STATS_TODAY; w++) {
		long edge = stats_window_start(w, now);

		scrub();
		seed(edge - 1, "NES", "Before.zip", 100, "quit");
		seed(edge,     "NES", "On.zip",     200, "quit");
		seed(now,      "NES", "After.zip",  300, "quit");
		ck(stats_summarize(w, false, now) == 2,
		   stats_window_name(w));
		ck(stats_total_seconds() == 500,
		   "the session before the boundary is not counted");
		ck(stats_summarize(STATS_ALL, false, now) == 3,
		   "and all time still has all three");
	}
}

static void windows_cut_on_a_boundary(void)
{
	long at[16];
	int i, n = instants(at);

	printf("what each window includes:\n");
	for (i = 0; i < n; i++) windows_at(at[i]);
}

static void folding_by_system(void)
{
	const char *tag, *file;
	long secs, now = (long)time(NULL);
	int launches;

	printf("by system rather than by game:\n");
	scrub();
	seed(now, "NES", "Contra.zip",  600, "quit");
	seed(now, "NES", "Metroid.zip", 300, "quit");
	seed(now, "GB",  "Tetris.zip",  100, "quit");

	ck(stats_summarize(STATS_ALL, true, now) == 2, "three games, two systems");
	ck(stats_at(0, &tag, &file, &secs, &launches), "the first row reads");
	ck(!strcmp(tag, "NES"), "the busiest system first");
	ck(secs == 900 && launches == 2, "both its games folded into it");
	ck(!*file, "and a system row names no file");
	ck(stats_summarize(STATS_ALL, false, now) == 3, "by game it is three again");
}

/* Recents' rows (stats_recent): each game once, by its last start, newest
 * first, at most as many as asked; and the newest kept when there are more
 * games than that, not the first ones read */
static void recent_games(void)
{
	stats_recent_row r[3];
	long now = (long)time(NULL);
	int n;

	printf("the games played last:\n");
	scrub();
	ck(stats_recent(r, 3) == 0, "none played, none");
	seed(now - 5000, "NES", "Contra.zip", 900, "quit");
	seed(now - 4000, "SNES", "Metroid.zip", 60, "quit");
	seed(now - 3000, "GB", "Tetris.zip", 30, "lost");
	seed(now - 2000, "NES", "Contra.zip", 10, "quit");
	seed(now - 1000, "GBA", "Golden Sun.zip", 20, "quit");
	seed(now - 1500, "GBC", "Tetris.zip", 20, "quit");
	n = stats_recent(r, 3);
	ck(n == 3, "three asked, three given");
	ck(n == 3 && !strcmp(r[0].tag, "GBA") && r[0].last == now - 1000, "the newest first");
	ck(n == 3 && !strcmp(r[1].tag, "GBC") && !strcmp(r[1].file, "Tetris.zip"),
	   "a file of the same name on another system is another game");
	ck(n == 3 && !strcmp(r[2].tag, "NES") && r[2].last == now - 2000,
	   "a game played twice is one row, at its last start");
	ck(stats_recent(r, 0) == 0 && stats_recent(NULL, 3) == 0, "nothing asked, nothing given");
	scrub();
}

static void the_extras(void)
{
	long now = (long)time(NULL), longest = 0, last = 0;
	int lost = 0;

	printf("longest, last played and lost:\n");
	scrub();
	seed(now - 200000, "NES", "Contra.zip", 900, "quit");
	seed(now - 100000, "NES", "Contra.zip", 300, "lost");
	seed(now -  50000, "NES", "Contra.zip", 120, "quit");

	ck(stats_summarize(STATS_ALL, false, now) == 1, "one game");
	ck(stats_extra(0, &longest, &last, &lost), "its extras read");
	ck(longest == 900, "the longest single session, not the total");
	ck(last == now - 50000, "last played is the most recent start");
	ck(lost == 1 && stats_total_lost() == 1, "one session never reached exit");

	/* A row still marked open is a session this boot has not recovered. It
	 * counts as lost for the same reason stats_recover will call it that. */
	scrub();
	seed(now, "NES", "Contra.zip", 60, "open");
	ck(stats_summarize(STATS_ALL, false, now) == 1, "an open row still folds");
	ck(stats_total_lost() == 1, "and counts as lost");
}

static void how_long_ago(void)
{
	long at[16];
	char b[16];
	int i, n = instants(at);

	printf("how long ago it reads:\n");
	for (i = 0; i < n; i++) {
		long now = at[i];

		stats_ago(0, now, b, sizeof b);   ck(!strcmp(b, "never"), "never played");
		stats_ago(now, now, b, sizeof b); ck(!strcmp(b, "today"), "today");
		stats_ago(days_before(now, 1), now, b, sizeof b);
		ck(!strcmp(b, "yesterday"), "yesterday");
		stats_ago(days_before(now, 3), now, b, sizeof b);
		ck(!strcmp(b, "3d ago"), "a few days");
		stats_ago(days_before(now, 21), now, b, sizeof b);
		ck(!strcmp(b, "3w ago"), "a few weeks");
	}
}

/* A program of its own (stats.h): the launcher hands the session over with
 * the program and exits, launch.sh's --port-played closes it as the program
 * ends. Its row is "open" from the hand-over, a power cut's marker; the time
 * is the monotonic clock's, passed in here so it can be placed */
static char NOTE[64];

static bool note_there(void)
{
	return access(NOTE, F_OK) == 0;
}

static void a_program_of_its_own(void)
{
	stats_recent_row r[2];
	long now = (long)time(NULL), secs = 0, last = 0;
	FILE *f;

	printf("a program of its own (your PICO-8, OpenBOR, a port):\n");
	scrub();
	unlink(NOTE);
	ck(!stats_handed_back(NOTE, 1000), "nothing handed over, nothing to close");
	ck(stats_hand_over("PICO8", "celeste.p8", NOTE, now, 5000), "handed over");
	ck(note_there(), "the note is in /tmp");
	ck(rows() == 1 && !strcmp(only_value(), "0\topen"),
	   "its row at once, open at 0 s: a power cut over it still leaves the game on Recents");
	ck(stats_recent(r, 2) == 1 && !strcmp(r[0].tag, "PICO8") && !strcmp(r[0].file, "celeste.p8") &&
	   r[0].last == now, "and it is the game played last, from its start");
	ck(stats_handed_back(NOTE, 5000 + 125400), "closed as it ends");
	ck(!note_there(), "the note gone");
	ck(rows() == 1 && !strcmp(only_value(), "125\tquit"), "the same row, 125 s, quit");
	ck(!stats_handed_back(NOTE, 999999), "closed once: a second close finds nothing");
	ck(stats_summarize(STATS_ALL, false, now) == 1 && stats_lookup("PICO8", "celeste.p8", &secs, &last) &&
	   secs == 125 && last == now, "Played and the play time have it");

	printf("  two in one second, a clock gone back, a power cut:\n");
	ck(stats_hand_over("PICO8", "celeste.p8", NOTE, now, 6000), "the same cart again in the same second");
	ck(stats_handed_back(NOTE, 66000), "closed");
	ck(rows() == 2, "two rows, not one written over: the monotonic clock keeps the keys apart");
	ck(stats_summarize(STATS_ALL, false, now) == 1 && stats_lookup("PICO8", "celeste.p8", &secs, NULL) &&
	   secs == 185, "185 s in all");
	scrub();
	ck(stats_hand_over("OPENBOR", "Beats of Rage.pak", NOTE, now, 90000), "OpenBOR handed over");
	ck(stats_handed_back(NOTE, 1000), "closed with a clock that went back");
	ck(!strcmp(only_value(), "0\tquit"), "nothing honest to claim, so 0 s");
	scrub();
	ck(stats_hand_over("PRT", "Stardew Valley.sh", NOTE, now, 1000), "a port handed over");
	db_shutdown();                                  /* the power goes */
	db_init(DEV, LIB, NULL);
	unlink(NOTE);                                   /* /tmp with it */
	stats_recover();
	ck(rows() == 1 && !strcmp(only_value(), "0\tlost"), "the next start calls it lost, on Recents still");

	printf("  notes that are no note:\n");
	scrub();
	ck(!stats_hand_over("PICO8", "a\tb.p8", NOTE, now, 1) && !note_there() && rows() == 0,
	   "a file with a tab: nothing handed over");
	ck(!stats_hand_over("", "x.p8", NOTE, now, 1) && !stats_hand_over("PICO8", "", NOTE, now, 1),
	   "no tag or no file: nothing");
	f = fopen(NOTE, "w");
	if (f) { fputs("garbage\n", f); fclose(f); }
	ck(!stats_handed_back(NOTE, 1) && !note_there() && rows() == 0, "a note cut short: removed, nothing written");
	f = fopen(NOTE, "w");
	if (f) { fprintf(f, "%ld\t5\t5\tPICO8\t\n", now); fclose(f); }
	ck(!stats_handed_back(NOTE, 6000) && rows() == 0, "a note with no file: nothing written");
	f = fopen(NOTE, "w");
	if (f) { fprintf(f, "%ld\t7\t-5000\tPICO8\tceleste.p8\n", now); fclose(f); }
	ck(stats_handed_back(NOTE, 1000) && rows() == 1 && !strcmp(only_value(), "6\tquit"),
	   "a start before the clock's zero (a check's, on a computer just started): the difference");
	scrub();
}

/* When, as Recents and Screenshots say it: the day, and the clock for today
 * and yesterday */
static void when_it_was(void)
{
	long at[16];
	char b[32], want[32];
	int i, n = instants(at);

	printf("when it was played, as Recents says it:\n");
	stats_when(0, at[0], true, b, sizeof b);
	ck(!strcmp(b, "never"), "never");
	for (i = 0; i < n; i++) {
		long now = at[i], then;
		time_t t;
		struct tm tm;

		t = (time_t)now;
		tm = *localtime(&t);
		tm.tm_hour = 9; tm.tm_min = 5; tm.tm_sec = 0; tm.tm_isdst = -1;
		then = (long)mktime(&tm);
		if (then > now) then = now;
		t = (time_t)then;
		tm = *localtime(&t);
		stats_when(then, now, true, b, sizeof b);
		snprintf(want, sizeof want, "today %02d:%02d", tm.tm_hour, tm.tm_min);
		ck(!strcmp(b, want), "today, the clock 24-hour");
		stats_when(then, now, false, b, sizeof b);
		snprintf(want, sizeof want, "today %d:%02d %s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min,
		         tm.tm_hour < 12 ? "AM" : "PM");
		ck(!strcmp(b, want), "today, 12-hour, no leading zero");
		then = days_before(now, 1);
		t = (time_t)then;
		tm = *localtime(&t);
		stats_when(then, now, true, b, sizeof b);
		snprintf(want, sizeof want, "yesterday %02d:%02d", tm.tm_hour, tm.tm_min);
		ck(!strcmp(b, want), "yesterday with its clock");
		stats_when(days_before(now, 3), now, true, b, sizeof b);
		ck(!strcmp(b, "3d ago"), "past yesterday the day alone");
	}
}

static void formatting(void)
{
	char b[16];
	printf("how a duration reads:\n");
	stats_format(0, b, sizeof b);     ck(!strcmp(b, "never"), "nothing played");
	stats_format(45, b, sizeof b);    ck(!strcmp(b, "45s"), "under a minute");
	stats_format(432, b, sizeof b);   ck(!strcmp(b, "7m 12s"), "minutes and seconds");
	stats_format(45296, b, sizeof b); ck(!strcmp(b, "12h 34m"), "hours and minutes");
}

int main(void)
{
	/* A zone with daylight saving time, so the clock-change edges are real
	 * wherever this runs; without the zone's data the C library falls back
	 * to UTC and the edges are ordinary days, which still passes */
	setenv("TZ", "America/New_York", 1);
	tzset();
	snprintf(DEV, sizeof DEV, "/tmp/zyperos-stats-%ld-dev.db", (long)getpid());
	snprintf(LIB, sizeof LIB, "/tmp/zyperos-stats-%ld-lib.db", (long)getpid());
	snprintf(NOTE, sizeof NOTE, "/tmp/zyperos-stats-%ld-note", (long)getpid());
	launch_writes_nothing();
	a_short_session_leaves_no_marker();
	a_long_session_marks_and_checkpoints();
	a_power_cut_is_recovered_at_the_checkpoint();
	the_clock_wrapping_is_not_a_49_day_session();
	two_systems_can_hold_the_same_filename();
	summarize_folds_and_ranks();
	windows_cut_on_a_boundary();
	folding_by_system();
	the_extras();
	recent_games();
	a_program_of_its_own();
	formatting();
	how_long_ago();
	when_it_was();
	db_shutdown();
	scrub();
	db_shutdown();
	unlink(NOTE);

	if (fails) { printf("\n%d FAILED\n", fails); return 1; }
	printf("\nok: play time is recorded, and a launch still writes nothing\n");
	return 0;
}
