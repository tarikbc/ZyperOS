/* SPDX-License-Identifier: GPL-3.0-only */
/* Does Date & Time step and read the way its rows say?
 *
 *     make check-clock
 *
 * Each row moves only itself, so the edges are the whole of it: the minute
 * after 59, the day after the month's last, January 31 moved to February, a
 * leap year, noon and midnight in 12-hour time and in 24-hour (MENU >
 * Clock), the shelf's short clock in both, the year an unset clock is
 * known by, and the zone list's ends. Run in New York, the shipped zone, so daylight saving is in play.
 *
 * No SDL, no device, and the clock is never set: time is a number this file
 * chooses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/clock.h"

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

/* Local time, from its parts */
static time_t at(int year, int mon, int day, int hour, int min)
{
	struct tm tm = { 0 };

	tm.tm_year = year - 1900; tm.tm_mon = mon - 1; tm.tm_mday = day;
	tm.tm_hour = hour; tm.tm_min = min; tm.tm_isdst = -1;
	return mktime(&tm);
}

static int part(time_t t, clock_field f)
{
	struct tm tm;

	localtime_r(&t, &tm);
	switch (f) {
	case CLK_YEAR:   return tm.tm_year + 1900;
	case CLK_MONTH:  return tm.tm_mon + 1;
	case CLK_DAY:    return tm.tm_mday;
	case CLK_HOUR:   return tm.tm_hour;
	case CLK_MINUTE: return tm.tm_min;
	default:         return tm.tm_sec;
	}
}

static int label_is(time_t t, clock_field f, const char *want)
{
	char got[32];

	clock_field_label(t, f, false, got, sizeof got);
	if (strcmp(got, want)) { printf("  (read \"%s\")\n", got); return 0; }
	return 1;
}

int main(void)
{
	char s[32];
	time_t t;

	setenv("TZ", "America/New_York", 1);
	tzset();

	printf("stepping:\n");
	t = clock_step(at(2026, 10, 3, 18, 59) + 30, CLK_MINUTE, +1);
	CHECK(part(t, CLK_MINUTE) == 0 && part(t, CLK_HOUR) == 18,
	      "the minute after 59 is 0 of the same hour");
	CHECK(part(t, -1) == 0, "and the seconds are zero");
	t = clock_step(at(2026, 10, 31, 9, 0), CLK_DAY, +1);
	CHECK(part(t, CLK_DAY) == 1 && part(t, CLK_MONTH) == 10,
	      "the day after the 31st is the 1st of the same month");
	t = clock_step(at(2026, 1, 31, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_MONTH) == 2 && part(t, CLK_DAY) == 28,
	      "January 31 moved to February is February 28");
	t = clock_step(at(2028, 1, 31, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_DAY) == 29, "and February 29 in a leap year");
	t = clock_step(at(2026, 12, 15, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_MONTH) == 1 && part(t, CLK_YEAR) == 2026,
	      "December's next month is January of the same year");
	t = clock_step(at(2026, 10, 3, 23, 10), CLK_HOUR, +1);
	CHECK(part(t, CLK_HOUR) == 0 && part(t, CLK_DAY) == 3,
	      "the hour after 11 PM is midnight of the same day");
	t = clock_step(at(2026, 10, 3, 0, 10), CLK_HOUR, -1);
	CHECK(part(t, CLK_HOUR) == 23, "and back again");
	t = clock_step(at(2099, 6, 1, 9, 0), CLK_YEAR, +1);
	CHECK(part(t, CLK_YEAR) == 2099, "the year stops at 2099");
	t = clock_step(at(2000, 6, 1, 9, 0), CLK_YEAR, -1);
	CHECK(part(t, CLK_YEAR) == 2000, "and at 2000");
	t = clock_step(at(2018, 9, 4, 10, 43), CLK_YEAR, +8);
	CHECK(part(t, CLK_YEAR) == 2026 && part(t, CLK_MONTH) == 9,
	      "an unset Pixel's 2018 steps up to now");
	/* Daylight saving: July is EDT, January EST, and the hour shown stays
	 * the hour that was set */
	t = clock_step(at(2026, 7, 4, 15, 0), CLK_MONTH, +6);
	CHECK(part(t, CLK_MONTH) == 1 && part(t, CLK_HOUR) == 15,
	      "moving across daylight saving keeps the hour on the clock face");

	printf("words:\n");
	t = at(2026, 10, 3, 18, 5);
	CHECK(label_is(t, CLK_YEAR, "2026"), "the year");
	CHECK(label_is(t, CLK_MONTH, "October"), "the month by name");
	CHECK(label_is(t, CLK_DAY, "3"), "the day without a leading zero");
	CHECK(label_is(t, CLK_HOUR, "6 PM"), "the hour in 12-hour time");
	CHECK(label_is(t, CLK_MINUTE, "05"), "the minute with one");
	CHECK(label_is(at(2026, 10, 3, 0, 0), CLK_HOUR, "12 AM"), "midnight is 12 AM");
	CHECK(label_is(at(2026, 10, 3, 12, 0), CLK_HOUR, "12 PM"), "noon is 12 PM");
	clock_label(t, false, s, sizeof s);
	CHECK(!strcmp(s, "Oct 3, 6:05 PM"), "the menu row reads \"%s\"", s);
	clock_label(at(2026, 1, 9, 0, 30), false, s, sizeof s);
	CHECK(!strcmp(s, "Jan 9, 12:30 AM"), "and just after midnight \"%s\"", s);

	/* MENU > Clock: 24-hour, my note in the status lab, 2026-11-04.
	 * The menu row and the Hour row follow it, as the shelf's corner does. */
	printf("24-hour:\n");
	clock_label(t, true, s, sizeof s);
	CHECK(!strcmp(s, "Oct 3, 18:05"), "the menu row reads \"%s\"", s);
	clock_label(at(2026, 1, 9, 0, 30), true, s, sizeof s);
	CHECK(!strcmp(s, "Jan 9, 00:30"), "and just after midnight \"%s\"", s);
	clock_field_label(t, CLK_HOUR, true, s, sizeof s);
	CHECK(!strcmp(s, "18"), "the Hour row reads \"%s\"", s);
	clock_field_label(at(2026, 10, 3, 6, 0), CLK_HOUR, true, s, sizeof s);
	CHECK(!strcmp(s, "06"), "and in the morning \"%s\"", s);
	clock_field_label(t, CLK_MINUTE, true, s, sizeof s);
	CHECK(!strcmp(s, "05"), "the minute is the minute either way");

	/* The shelf's corner: "10:08", the status lab's pick, with no AM or
	 * PM, which in the corner would reach the systems card. */
	printf("the shelf's clock:\n");
	clock_short(at(2026, 11, 4, 22, 8), false, s, sizeof s);
	CHECK(!strcmp(s, "10:08"), "10:08 at night reads \"%s\"", s);
	clock_short(at(2026, 11, 4, 22, 8), true, s, sizeof s);
	CHECK(!strcmp(s, "22:08"), "or \"%s\" in 24-hour", s);
	clock_short(at(2026, 11, 4, 9, 5), false, s, sizeof s);
	CHECK(!strcmp(s, "9:05"), "the hour without a leading zero, \"%s\"", s);
	clock_short(at(2026, 11, 4, 9, 5), true, s, sizeof s);
	CHECK(!strcmp(s, "09:05"), "and with one in 24-hour, \"%s\"", s);
	clock_short(at(2026, 11, 4, 0, 30), false, s, sizeof s);
	CHECK(!strcmp(s, "12:30"), "half past midnight is \"%s\"", s);
	clock_short(at(2026, 11, 4, 0, 30), true, s, sizeof s);
	CHECK(!strcmp(s, "00:30"), "or \"%s\"", s);
	clock_short(at(2026, 11, 4, 12, 0), false, s, sizeof s);
	CHECK(!strcmp(s, "12:00"), "noon is \"%s\"", s);
	clock_short(at(2026, 11, 4, 12, 0), true, s, sizeof s);
	CHECK(!strcmp(s, "12:00"), "either way, \"%s\"", s);

	/* An unset Pixel 2 keeps its clock from 2018, and the corner says
	 * nothing rather than a time that is not now. */
	CHECK(!clock_is_set(at(2018, 9, 4, 10, 43)), "2018 is a clock nobody set");
	CHECK(!clock_is_set(at(2025, 12, 31, 23, 59)), "and so is the last minute of 2025");
	CHECK(clock_is_set(at(2026, 1, 1, 0, 0)), "2026 is set");
	CHECK(clock_is_set(at(2099, 6, 1, 9, 0)), "and so is 2099");

	printf("zones:\n");
	CHECK(clock_zone_find("America/New_York") >= 0, "the shipped zone is on the list");
	CHECK(clock_zone_find("Mars/Olympus_Mons") < 0, "a zone not on it is not");
	CHECK(clock_zone_step("Mars/Olympus_Mons", +1) == clock_zone_find("America/New_York"),
	      "and steps onto it from New York");
	CHECK(clock_zone_step(CLOCK_ZONES[0].id, -1) == 0, "the west end stops");
	CHECK(clock_zone_step(CLOCK_ZONES[CLOCK_ZONE_COUNT - 1].id, +1) == CLOCK_ZONE_COUNT - 1,
	      "and so does the east");
	CHECK(!strcmp(CLOCK_ZONES[clock_zone_step("America/New_York", +1)].city, "Halifax"),
	      "one east of New York is Halifax");
	/* Auto, before the west end (src/autozone.h) */
	CHECK(clock_zone_step_auto(CLOCK_ZONES[0].id, false, -1) == CLOCK_ZONE_AUTO,
	      "left from the west end is Auto");
	CHECK(clock_zone_step_auto("America/New_York", true, -1) == CLOCK_ZONE_AUTO,
	      "left from Auto stays on Auto");
	CHECK(clock_zone_step_auto("America/Sao_Paulo", true, +1) == 0,
	      "right from Auto is the list's first, whatever zone Auto found");
	CHECK(clock_zone_step_auto("America/New_York", false, +1) == clock_zone_find("America/Halifax"),
	      "and the list steps as it did");
	CHECK(clock_zone_step_auto("Asia/Ho_Chi_Minh", false, -1) == clock_zone_find("America/New_York"),
	      "an id off the list steps onto it from New York");
	CHECK(clock_zone_step_auto(CLOCK_ZONES[CLOCK_ZONE_COUNT - 1].id, false, +1) == CLOCK_ZONE_COUNT - 1,
	      "the east end stops");
	/* A zone name for TZ, never a path */
	CHECK(clock_zone_name_ok("America/Argentina/Buenos_Aires") && clock_zone_name_ok("Etc/GMT+3") &&
	      clock_zone_name_ok("America/Port-au-Prince") && clock_zone_name_ok("UTC"),
	      "the zone database's names are names");
	CHECK(!clock_zone_name_ok(":/etc/passwd") && !clock_zone_name_ok("/etc/localtime") &&
	      !clock_zone_name_ok("../etc/passwd") && !clock_zone_name_ok("America/../../etc") &&
	      !clock_zone_name_ok("<-03>3") && !clock_zone_name_ok("America/") &&
	      !clock_zone_name_ok("America//Sao_Paulo") && !clock_zone_name_ok("") &&
	      !clock_zone_name_ok(NULL) && !clock_zone_name_ok("Am erica/X"),
	      "and paths, TZ's other forms and the rest are not");
	CHECK(clock_zone_have("America/New_York") && clock_zone_have("Asia/Ho_Chi_Minh") &&
	      !clock_zone_have("Mars/Olympus_Mons") && !clock_zone_have("America"),
	      "this system has a zone of the list and one off it, not a made-up one or a folder");
	{
		/* West to east, by what each zone is on one fixed day. A zone this
		 * machine did not know would read as UTC and break the order, so
		 * this catches a misspelled id too. */
		int i;
		long prev = -86400;

		for (i = 0; i < CLOCK_ZONE_COUNT; i++) {
			struct tm tm;
			time_t noon = 1767268800;   /* 2026-01-01 12:00 UTC */

			setenv("TZ", CLOCK_ZONES[i].id, 1);
			tzset();
			localtime_r(&noon, &tm);
			CHECK(tm.tm_gmtoff >= prev, "%s is east of the zone before it",
			      CLOCK_ZONES[i].city);
			prev = tm.tm_gmtoff;
		}
	}

	/* The list A on Time Zone opens, and what a choice does */
	printf("the zone's list:\n");
	{
		menu_row rows[CLOCK_ZONE_LIST_MAX];
		char words[80];
		clock_zone_change c;
		int n, i, ok;

		CHECK(CLOCK_ZONE_COUNT + 1 <= CLOCK_ZONE_LIST_MAX, "the list's room holds every choice");
		n = clock_zone_list("America/Sao_Paulo", true, "America/Sao_Paulo", rows,
		                    CLOCK_ZONE_LIST_MAX, words, sizeof words);
		CHECK(n == CLOCK_ZONE_COUNT + 1, "Auto and every zone, %d rows", n);
		CHECK(!strcmp(rows[0].label, "Auto (Sao Paulo)"), "Auto first, with what it found: \"%s\"",
		      rows[0].label);
		for (i = 0, ok = 1; i < CLOCK_ZONE_COUNT && i + 1 < n; i++)
			ok &= !strcmp(rows[i + 1].label, CLOCK_ZONES[i].city) && rows[i + 1].live;
		CHECK(ok, "then the zones west to east, as the row steps them");
		CHECK(clock_zone_list_on("America/Sao_Paulo", true) == 0 &&
		      clock_zone_list_row("America/Sao_Paulo", true) == 0,
		      "Auto on: its row has the tick and the cursor");
		clock_zone_list("America/New_York", true, "", rows, CLOCK_ZONE_LIST_MAX, words, sizeof words);
		CHECK(!strcmp(rows[0].label, "Auto"), "Auto with nothing found yet says Auto alone");
		clock_zone_list("America/Sao_Paulo", false, "America/Sao_Paulo", rows, CLOCK_ZONE_LIST_MAX,
		                words, sizeof words);
		CHECK(!strcmp(rows[0].label, "Auto"), "and so does Auto off, whatever it found once");
		CHECK(clock_zone_list_on("Asia/Tokyo", false) == 1 + clock_zone_find("Asia/Tokyo") &&
		      clock_zone_list_row("Asia/Tokyo", false) == 1 + clock_zone_find("Asia/Tokyo"),
		      "a zone picked by hand: its row has the tick and the cursor");
		CHECK(clock_zone_list_on("Asia/Ho_Chi_Minh", false) < 0 &&
		      clock_zone_list_row("Asia/Ho_Chi_Minh", false) == 1 + clock_zone_find("America/New_York"),
		      "an id off the list: no tick, and the cursor on New York");
		n = clock_zone_list("Pacific/Honolulu", false, "", rows, 3, words, sizeof words);
		CHECK(n == 3, "a shorter array gets the first rows");
		CHECK(clock_zone_list_choice(0) == CLOCK_ZONE_AUTO &&
		      clock_zone_list_choice(1) == 0 &&
		      clock_zone_list_choice(CLOCK_ZONE_COUNT) == CLOCK_ZONE_COUNT - 1 &&
		      clock_zone_list_choice(CLOCK_ZONE_COUNT + 1) < -1 && clock_zone_list_choice(-1) < -1,
		      "each row is its choice, and past the rows none");

		c = clock_zone_choose("America/Sao_Paulo", true, clock_zone_find("Asia/Tokyo"));
		CHECK(!c.auto_on && c.zone && !strcmp(c.zone, "Asia/Tokyo") && !c.auto_chosen,
		      "a zone picked by hand turns Auto off and is set");
		c = clock_zone_choose("America/Sao_Paulo", true, clock_zone_find("America/Sao_Paulo"));
		CHECK(!c.auto_on && c.zone && !strcmp(c.zone, "America/Sao_Paulo"),
		      "the zone Auto found, picked by hand, turns Auto off too");
		c = clock_zone_choose("Asia/Tokyo", false, clock_zone_find("Asia/Tokyo"));
		CHECK(!c.auto_on && !c.zone, "the zone in use picked again changes nothing");
		c = clock_zone_choose("Asia/Tokyo", false, CLOCK_ZONE_AUTO);
		CHECK(c.auto_on && c.auto_chosen && !c.zone,
		      "Auto picked turns Auto on, keeps the zone, and says what it tells");
		c = clock_zone_choose("America/Sao_Paulo", true, CLOCK_ZONE_AUTO);
		CHECK(c.auto_on && !c.auto_chosen && !c.zone, "Auto picked again with Auto on, nothing");
		c = clock_zone_choose("Asia/Tokyo", false, -2);
		CHECK(!c.auto_on && !c.zone && !c.auto_chosen, "no choice, nothing");
		c = clock_zone_choose("America/New_York", true,
		                      clock_zone_step_auto("America/New_York", true, +1));
		CHECK(!c.auto_on && c.zone && !strcmp(c.zone, CLOCK_ZONES[0].id),
		      "and the row's right from Auto is the same rule: Auto off, Honolulu set");
	}

	printf("the screen's rows:\n");
	{
		static const char *const names[CLOCK_ROWS] = {
			"Time Zone", "Year", "Month", "Day", "Hour", "Minute" };
		menu_row rows[CLOCK_ROWS + 2];
		clock_bufs b;
		int n = clock_rows(at(2026, 10, 3, 18, 5), false, "New York", rows,
		                   CLOCK_ROWS + 2, &b), i;

		CHECK(n == CLOCK_ROWS, "six rows, %d", n);
		for (i = 0; i < n && i < CLOCK_ROWS; i++)
			CHECK(!strcmp(rows[i].label, names[i]) && rows[i].live,
			      "row %d is %s", i, names[i]);
		CHECK(!strcmp(rows[CLOCK_ROW_ZONE].value, "New York"),
		      "Time Zone first, as I asked, and it says the zone");
		CHECK(!strcmp(rows[CLOCK_ROW_FIELD0 + CLK_HOUR].value, "6 PM"),
		      "the fields after it read the time");
		CHECK(clock_row_field(CLOCK_ROW_ZONE) < 0 &&
		      clock_row_field(CLOCK_ROW_FIELD0) == CLK_YEAR &&
		      clock_row_field(CLOCK_ROWS - 1) == CLK_MINUTE &&
		      clock_row_field(CLOCK_ROWS) < 0, "each row changes its own field");
		n = clock_rows(at(2026, 10, 3, 18, 5), false, "New York", rows, 2, &b);
		CHECK(n == 2, "a shorter array gets the first rows");
	}

	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
