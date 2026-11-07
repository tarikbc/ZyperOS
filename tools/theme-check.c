/* The theme core: which theme is on, for a setting and an hour, without a
 * screen.
 *
 * MENU > Appearance is Light, Dark or Auto, and Auto is a rule about the
 * handheld's clock: Beard from 7:00 to 19:00, Salt & Pepper the rest of the
 * day. A rule about a clock is the kind of thing that is right at noon and at
 * midnight, which is when anybody looks, and wrong at the two edges nobody
 * is awake for - so the edges are what is held here: 6:59 and 7:00, 18:59 and
 * 19:00, and the minute's throttle on the clock that decides how late the
 * flip may come. Beside them: Light and Dark mean the same at every hour, a
 * setting that is not one of the three reads as Light and not as something
 * that turns the shelf dark, the names the menu shows and the words the
 * database stores each come back as the setting they were made from, and the
 * file the bar's other two programs read says the theme that is on - at
 * start, after a change, and after Auto's flip - and not one that was. And
 * the saved word read the way portkeys reads it over a port, where nothing
 * writes the file: Auto's edges at the local hour, in the zone TZ names.
 *
 * Links src/theme.c and src/themefile.c and NOT SDL, and never touches
 * /tmp/zyperos-theme: the launcher on a development host may be running.
 */
#include "../src/design.h"
#include "../src/theme.h"
#include "../src/themefile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static char dir[64], file[96];

/* The seconds of 2026-11-04 at h:m:s in the zone the check set, which is UTC:
 * the clock's local hour is then the hour that was asked for, on any host. */
static time_t at(int h, int m, int s)
{
	struct tm tm;

	memset(&tm, 0, sizeof tm);
	tm.tm_year = 2026 - 1900; tm.tm_mon = 10; tm.tm_mday = 4;
	tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
	return mktime(&tm);
}

/* Whether theme() is the table of DS_THEME[i], by what is in it. Not by its
 * address: src/design.h's table is static const, so this file and src/theme.c
 * each have a copy of it and the two are never the same object. */
static int is_table(const ds_theme *t, int i)
{
	const ds_theme *w = &DS_THEME[i];

	return !strcmp(t->name, w->name) &&
	       !memcmp(&t->ground, &w->ground, sizeof w->ground) &&
	       !memcmp(&t->text, &w->text, sizeof w->text) &&
	       !memcmp(&t->accent_ink, &w->accent_ink, sizeof w->accent_ink) &&
	       t->scrim_menu == w->scrim_menu;
}

/* What the theme file says, as the readers of it see it */
static int on_file(void) { return themefile_read(file); }

int main(void)
{
	int h, a;

	/* Line by line, so the one line of log the unwritable path makes lands
	 * under the heading that says to expect it and not above all of them */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setenv("TZ", "UTC", 1);
	tzset();

	snprintf(dir, sizeof dir, "/tmp/zyperos-theme-check-%ld", (long)getpid());
	if (mkdir(dir, 0700) != 0) { printf("theme: no scratch directory\n"); return 1; }
	snprintf(file, sizeof file, "%s/theme", dir);

	/* Auto's rule, and its four edges: Light from 7:00, up to but not
	 * including 19:00 - hours 7 through 18. */
	printf("Auto:\n");
	ck(theme_resolve(APPEAR_AUTO, 6) == DS_DARK, "6:59 is still night");
	ck(theme_resolve(APPEAR_AUTO, 7) == DS_LIGHT, "7:00 is day");
	ck(theme_resolve(APPEAR_AUTO, 18) == DS_LIGHT, "18:59 is still day");
	ck(theme_resolve(APPEAR_AUTO, 19) == DS_DARK, "19:00 is night");
	ck(theme_resolve(APPEAR_AUTO, 0) == DS_DARK, "midnight is night");
	ck(theme_resolve(APPEAR_AUTO, 12) == DS_LIGHT, "noon is day");
	ck(theme_resolve(APPEAR_AUTO, 23) == DS_DARK, "23:00 is night");
	{
		int day = 0, ok = 1;

		for (h = 0; h < 24; h++) {
			if (theme_resolve(APPEAR_AUTO, h) == DS_LIGHT) day++;
			if (theme_resolve(APPEAR_AUTO, h) != (h >= 7 && h < 19 ? DS_LIGHT : DS_DARK)) ok = 0;
		}
		ck(day == 12 && ok, "twelve hours of day, the twelve from seven");
	}
	/* An hour that is not an hour is a clock that could not be read, and that
	 * is not night: Beard is the default, and the smaller harm */
	ck(theme_resolve(APPEAR_AUTO, -1) == DS_LIGHT, "an hour below zero reads as the default");
	ck(theme_resolve(APPEAR_AUTO, 24) == DS_LIGHT, "so does one past 23");

	/* Light and Dark mean the same at any hour of the day */
	printf("Light and Dark:\n");
	{
		int ok_l = 1, ok_d = 1;

		for (h = -1; h <= 24; h++) {
			if (theme_resolve(APPEAR_LIGHT, h) != DS_LIGHT) ok_l = 0;
			if (theme_resolve(APPEAR_DARK, h) != DS_DARK) ok_d = 0;
		}
		ck(ok_l, "Light is Beard at every hour");
		ck(ok_d, "Dark is Salt & Pepper at every hour");
	}
	ck(theme_resolve((appearance)99, 12) == DS_LIGHT, "a setting past the three is Light");
	ck(theme_resolve((appearance)-1, 3) == DS_LIGHT, "so is a negative one, at night too");

	/* The words, in both directions */
	printf("names:\n");
	ck(!strcmp(appearance_name(APPEAR_LIGHT), "Light"), "Light's name");
	ck(!strcmp(appearance_name(APPEAR_DARK), "Dark"), "Dark's name");
	ck(!strcmp(appearance_name(APPEAR_AUTO), "Auto"), "Auto's name");
	ck(!strcmp(appearance_name((appearance)99), "Light"), "a setting past the three is named Light");
	ck(!strcmp(appearance_id(APPEAR_LIGHT), "light"), "Light is stored as light");
	ck(!strcmp(appearance_id(APPEAR_DARK), "dark"), "Dark is stored as dark");
	ck(!strcmp(appearance_id(APPEAR_AUTO), "auto"), "Auto is stored as auto");
	{
		int ok_n = 1, ok_i = 1;

		for (a = APPEAR_LIGHT; a <= APPEAR_AUTO; a++) {
			if (appearance_parse(appearance_name((appearance)a)) != (appearance)a) ok_n = 0;
			if (appearance_parse(appearance_id((appearance)a)) != (appearance)a) ok_i = 0;
		}
		ck(ok_n, "every name the menu shows reads back as its setting");
		ck(ok_i, "and every word the database stores");
	}
	ck(appearance_parse("purple") == APPEAR_LIGHT, "`purple` is Light");
	ck(appearance_parse("") == APPEAR_LIGHT, "an empty setting is Light");
	ck(appearance_parse(NULL) == APPEAR_LIGHT, "no setting is Light");
	ck(appearance_parse("darkness") == APPEAR_LIGHT, "a word that only starts with dark is Light");
	ck(appearance_parse("dar") == APPEAR_LIGHT, "a word dark starts is Light");
	ck(appearance_parse("auto ") == APPEAR_LIGHT, "a word with a space after it is not the word");
	ck(appearance_parse("DARK") == APPEAR_DARK, "a database edited by hand, in capitals, still says dark");
	ck(appearance_parse("Auto") == APPEAR_AUTO, "and in the menu's capitals, auto");

	/* Stepping, for the row: the three in a ring, either way */
	printf("stepping:\n");
	ck(appearance_step(APPEAR_LIGHT, 1) == APPEAR_DARK, "Light, forward, is Dark");
	ck(appearance_step(APPEAR_DARK, 1) == APPEAR_AUTO, "Dark, forward, is Auto");
	ck(appearance_step(APPEAR_AUTO, 1) == APPEAR_LIGHT, "Auto, forward, wraps to Light");
	ck(appearance_step(APPEAR_LIGHT, -1) == APPEAR_AUTO, "Light, back, wraps to Auto");
	ck(appearance_step(APPEAR_AUTO, -1) == APPEAR_DARK, "Auto, back, is Dark");
	ck(appearance_step(APPEAR_DARK, -1) == APPEAR_LIGHT, "Dark, back, is Light");
	ck(appearance_step((appearance)99, 1) == APPEAR_DARK, "a setting past the three steps from Light");

	/* Before anything starts it: Beard, which is what a screen drawn before
	 * the setting is read must be */
	printf("before theme_init:\n");
	ck(theme_index() == DS_LIGHT, "the theme is Beard");
	ck(is_table(theme(), DS_LIGHT), "and theme() is its table");
	ck(theme_appearance() == APPEAR_LIGHT, "the setting is Light");

	/* The file: where it goes is the check's, so a launcher on this host
	 * keeps its own */
	theme_set_file(file);

	printf("theme_init:\n");
	theme_init(APPEAR_DARK);
	ck(theme_index() == DS_DARK && is_table(theme(), DS_DARK), "Dark is Salt & Pepper");
	ck(!strcmp(theme()->name, "Salt & Pepper"), "by name");
	ck(theme_appearance() == APPEAR_DARK, "and the setting is Dark");
	ck(on_file() == DS_DARK, "and the file says dark");
	theme_init(APPEAR_LIGHT);
	ck(theme_index() == DS_LIGHT && is_table(theme(), DS_LIGHT), "Light is Beard");
	ck(!strcmp(theme()->name, "Beard"), "by name");
	ck(on_file() == DS_LIGHT, "and the file says light");
	/* Written at every start whether or not it changed: a file left from an
	 * earlier launcher says what that one decided */
	themefile_write(file, DS_DARK);
	theme_init(APPEAR_LIGHT);
	ck(on_file() == DS_LIGHT, "a start writes the file even when the theme did not change");
	theme_init((appearance)99);
	ck(theme_index() == DS_LIGHT && theme_appearance() == APPEAR_LIGHT,
	   "a setting past the three starts as Light");

	/* Auto at start follows the clock, which the check hands in */
	printf("Auto at start:\n");
	theme_init_at(APPEAR_AUTO, at(6, 59, 0));
	ck(theme_index() == DS_DARK && on_file() == DS_DARK, "at 6:59 it starts dark");
	theme_init_at(APPEAR_AUTO, at(7, 0, 0));
	ck(theme_index() == DS_LIGHT && on_file() == DS_LIGHT, "at 7:00 it starts light");
	ck(theme_appearance() == APPEAR_AUTO, "and is still Auto");

	/* The tick: once a minute at most, true only when the theme changed */
	printf("theme_tick:\n");
	theme_init_at(APPEAR_AUTO, at(18, 59, 30));
	ck(theme_index() == DS_LIGHT, "18:59:30 starts light");
	ck(!theme_tick_at(at(18, 59, 40)), "ten seconds on, nothing changed");
	/* 19:00:10 is past the edge, and 40 seconds after the last look: not
	 * looked at again yet, so the theme is as it was - the throttle is
	 * what makes the cost one clock read a minute and not one per frame */
	ck(!theme_tick_at(at(19, 0, 10)), "past 19:00 but inside the minute: not looked at");
	ck(theme_index() == DS_LIGHT && on_file() == DS_LIGHT, "still light, and the file with it");
	ck(theme_tick_at(at(19, 0, 30)), "a minute after the start: it looks, and says it changed");
	ck(theme_index() == DS_DARK && on_file() == DS_DARK, "dark, and the file says so");
	ck(!theme_tick_at(at(19, 0, 31)), "and one second on it says nothing");
	ck(!theme_tick_at(at(19, 1, 29)), "just under another minute: nothing");
	ck(!theme_tick_at(at(19, 1, 30)), "a minute on and the same theme: looked at, unchanged, false");
	ck(on_file() == DS_DARK, "the file keeps saying dark");

	/* The other edge, from night into day */
	theme_init_at(APPEAR_AUTO, at(6, 58, 0));
	ck(theme_index() == DS_DARK, "6:58 starts dark");
	ck(!theme_tick_at(at(6, 59, 30)), "6:59:30: a minute on, still night");
	ck(theme_tick_at(at(7, 0, 30)), "7:00:30: day, changed");
	ck(theme_index() == DS_LIGHT && on_file() == DS_LIGHT, "light, and the file says so");

	/* A clock set by hand: Date & Time on a handheld with no network is
	 * how the time gets set, and a time set earlier is not a minute that
	 * has not passed yet. */
	theme_init_at(APPEAR_AUTO, at(12, 0, 0));
	ck(theme_index() == DS_LIGHT, "noon starts light");
	ck(theme_tick_at(at(3, 0, 0)), "the clock set back to 3:00 is looked at at once, and it is night");
	ck(theme_index() == DS_DARK && on_file() == DS_DARK, "dark, and the file says so");
	ck(theme_tick_at(at(3, 0, 0) + 3600 * 9), "set forward nine hours: looked at, and it is day");
	ck(theme_index() == DS_LIGHT, "light");

	/* Light and Dark never flip, whatever the hour: nothing to look at, and
	 * nothing written */
	theme_init_at(APPEAR_DARK, at(12, 0, 0));
	ck(theme_index() == DS_DARK, "Dark at noon is dark");
	themefile_write(file, DS_LIGHT);
	ck(!theme_tick_at(at(12, 5, 0)) && !theme_tick_at(at(13, 0, 0)) && !theme_tick_at(at(3, 0, 0)),
	   "Dark never ticks");
	ck(theme_index() == DS_DARK, "and stays dark");
	ck(on_file() == DS_LIGHT, "and writes nothing: the file is the writer's last word");
	theme_init_at(APPEAR_LIGHT, at(22, 0, 0));
	ck(theme_index() == DS_LIGHT, "Light at ten at night is light");
	ck(!theme_tick_at(at(23, 0, 0)) && !theme_tick_at(at(23, 30, 0)), "Light never ticks");

	/* The zone changed, in Date & Time: the same instant is another hour,
	 * and Auto is to follow it at once and not at the next minute's look.
	 * UTC-9 is nine hours EAST of Greenwich in a TZ string (the sign is
	 * backwards, as POSIX has it), so noon UTC is 21:00 there. */
	printf("theme_refresh:\n");
	{
		time_t noon = at(12, 0, 0);

		theme_init_at(APPEAR_AUTO, noon);
		ck(theme_index() == DS_LIGHT, "noon in UTC starts light");
		ck(!theme_refresh_at(noon + 1), "a refresh with nothing moved says nothing");
		setenv("TZ", "UTC-9", 1);
		tzset();
		ck(theme_refresh_at(noon + 2), "the zone moved nine hours east: 21:00, and it is looked at inside the minute");
		ck(theme_index() == DS_DARK && on_file() == DS_DARK, "dark, and the file says so");
		ck(!theme_refresh_at(noon + 3), "and a second refresh says nothing");
		ck(!theme_tick_at(noon + 30), "the refresh was a look: the tick has a minute from it");
		setenv("TZ", "UTC", 1);
		tzset();
		ck(theme_refresh_at(noon + 4), "back to UTC: noon, light, at once");
		ck(theme_index() == DS_LIGHT && on_file() == DS_LIGHT, "and the file says so");
		theme_init_at(APPEAR_DARK, noon);
		themefile_write(file, DS_LIGHT);
		ck(!theme_refresh_at(noon + 5) && theme_index() == DS_DARK, "Dark has nothing to refresh");
		ck(on_file() == DS_LIGHT, "and writes nothing");
	}

	/* The setting as another program reads it from the database: portkeys,
	 * over a port, which the launcher exits for, so nothing rewrites the
	 * theme file across 7:00 or 19:00 and the bar has to follow Auto
	 * itself. The same word read the same way, the same edges at the local
	 * hour, the zone as TZ says it, and nothing of the launcher's changed. */
	printf("theme_saved_at:\n");
	ck(theme_saved_at("auto", at(6, 59, 59)) == DS_DARK, "auto at 6:59:59 is night");
	ck(theme_saved_at("auto", at(7, 0, 0)) == DS_LIGHT, "auto at 7:00 is day");
	ck(theme_saved_at("auto", at(18, 59, 59)) == DS_LIGHT, "auto at 18:59:59 is still day");
	ck(theme_saved_at("auto", at(19, 0, 0)) == DS_DARK, "auto at 19:00 is night");
	ck(theme_saved_at("Auto", at(23, 0, 0)) == DS_DARK, "a word in capitals reads as the launcher reads it");
	ck(theme_saved_at("dark", at(12, 0, 0)) == DS_DARK, "dark at noon is dark");
	ck(theme_saved_at("light", at(0, 0, 0)) == DS_LIGHT, "light at midnight is light");
	ck(theme_saved_at("", at(0, 0, 0)) == DS_LIGHT, "no word saved is Light, at night too");
	ck(theme_saved_at("purple", at(0, 0, 0)) == DS_LIGHT, "a word that is none of the three is Light");
	ck(theme_saved_at(NULL, at(0, 0, 0)) == DS_LIGHT, "and so is no word at all");
	{
		/* The instants taken in UTC, before the zone moves: at() is local
		 * time, and the point is the same instant in another hour. */
		time_t noon = at(12, 0, 0), late = at(23, 30, 0), early = at(0, 30, 0);

		setenv("TZ", "UTC-9", 1);
		tzset();
		ck(theme_saved_at("auto", noon) == DS_DARK, "noon in UTC is 21:00 nine hours east: night");
		setenv("TZ", "UTC+5", 1);
		tzset();
		ck(theme_saved_at("auto", late) == DS_LIGHT, "23:30 in UTC is 18:30 five hours west: day");
		ck(theme_saved_at("auto", early) == DS_DARK, "and 0:30 is 19:30 the day before there: night");
		setenv("TZ", "UTC", 1);
		tzset();
	}
	theme_init_at(APPEAR_LIGHT, at(12, 0, 0));
	(void)theme_saved_at("dark", at(12, 0, 0));
	(void)theme_saved_at("auto", at(22, 0, 0));
	ck(theme_index() == DS_LIGHT && theme_appearance() == APPEAR_LIGHT,
	   "asking changes neither the theme that is on nor the setting");
	ck(on_file() == DS_LIGHT, "and writes no file");

	/* The change the menu makes: Auto to Dark and back, mid-session */
	theme_init_at(APPEAR_AUTO, at(12, 0, 0));
	theme_init_at(APPEAR_DARK, at(12, 0, 5));
	ck(theme_index() == DS_DARK && on_file() == DS_DARK, "Auto to Dark at noon is at once dark");
	theme_init_at(APPEAR_AUTO, at(12, 0, 10));
	ck(theme_index() == DS_LIGHT && on_file() == DS_LIGHT, "and back to Auto is light again, at once");
	ck(!theme_tick_at(at(12, 0, 20)), "with no tick owed for ten seconds");

	/* The real clock, whatever it says, gives one of the two and never
	 * throws: the path the launcher takes */
	theme_init(APPEAR_AUTO);
	ck(theme_index() == DS_LIGHT || theme_index() == DS_DARK, "theme_init(Auto) reads the clock");
	(void)theme_tick();
	ck(theme_index() == DS_LIGHT || theme_index() == DS_DARK, "theme_tick reads it again");
	ck(on_file() == theme_index(), "and the file agrees with the theme, whichever it is");

	/* No file, and a file that cannot be written: the theme still changes,
	 * which is the launcher's, and the bar's other programs read Beard */
	printf("no file (the line of log below, from the unwritable one, is expected):\n");
	theme_set_file(NULL);
	theme_init(APPEAR_DARK);
	ck(theme_index() == DS_DARK, "with no file the theme is still Dark");
	{
		char bad[160];

		snprintf(bad, sizeof bad, "%s/no/such/dir/theme", dir);
		theme_set_file(bad);
		theme_init(APPEAR_LIGHT);
		ck(theme_index() == DS_LIGHT, "a file that cannot be written is a line in the log, not a failure");
	}

	/* Cleanup */
	theme_set_file(NULL);
	{
		char tmp[112];

		snprintf(tmp, sizeof tmp, "%s.tmp", file);
		unlink(tmp);
	}
	unlink(file);
	rmdir(dir);

	if (fails) { printf("theme: %d FAILED\n", fails); return 1; }
	printf("theme: ok\n");
	return 0;
}
