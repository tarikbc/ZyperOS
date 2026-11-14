/* SPDX-License-Identifier: GPL-3.0-only */
/* Date & Time: the arithmetic and the words, with no SDL and no device.
 *
 * The GKD Pixel 2 has no network to take the time from, so its clock is
 * whatever it is set to here, and the hardware clock keeps it while the
 * device is off. The Brick sets its clock over Wi-Fi, but had no way to choose
 * a time zone either, so both get the same screen.
 *
 * Split out of main.c, like sys_menu.c: tools/clock-check.c links this file
 * alone. Setting the clock itself is plat_clock_set. */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "menu.h"

/* The screen's rows that change the time, in order. Time Zone comes before
 * them (2026-11-06: "in date/time settings, timezone should come
 * first": the zone is set once, and moves every field's value) and is not a
 * field of the time. */
typedef enum {
	CLK_YEAR, CLK_MONTH, CLK_DAY, CLK_HOUR, CLK_MINUTE, CLK_FIELDS
} clock_field;

/* The screen's rows, in order: Time Zone, then the fields from Year to
 * Minute. clock_rows fills them for `t`, the zone's row saying `zone` (its
 * city, or an id not on the list as it is), each field as clock_field_label
 * says it, into `b`; how many, at most CLOCK_ROWS. clock_row_field is the
 * field a row changes, -1 for Time Zone and anything past the fields. */
#define CLOCK_ROW_ZONE   0
#define CLOCK_ROW_FIELD0 1
#define CLOCK_ROWS       (CLOCK_ROW_FIELD0 + CLK_FIELDS)
typedef struct { char field[CLK_FIELDS][24]; } clock_bufs;
int clock_rows(time_t t, bool h24, const char *zone, menu_row *rows, int max,
               clock_bufs *b);
int clock_row_field(int row);

/* `t` with one field moved by `d` (local time), and the seconds at zero:
 * setting a clock by hand starts the minute it is set on. Each field wraps
 * within the one above it - the minute past 59 is 0 of the same hour - so a
 * row changes only itself. A day past the end of the new month comes back to
 * its last day. The year is the exception: it stops at 2000 and 2099. */
time_t clock_step(time_t t, clock_field f, int d);

/* What the field's row says for `t`: "2026", "October", "3", "6 AM", "05";
 * the hour "18", or "06", when `h24`. */
void clock_field_label(time_t t, clock_field f, bool h24, char *out, size_t n);

/* The menu row's value: "Oct 3, 6:12 AM", 12-hour, Eric's call 2026-10-03; or
 * "Oct 3, 06:12" when `h24`, MENU > Clock's 24-hour (mine, 2026-11-04). */
void clock_label(time_t t, bool h24, char *out, size_t n);

/* The shelf's clock, its status corner's: "10:08", 12-hour with no AM or PM,
 * the status lab's pick, since the words would carry the corner past the
 * systems card's edge; "22:08", or "09:05", when `h24`. */
void clock_short(time_t t, bool h24, char *out, size_t n);

/* Whether `t` is a time somebody set: 2026 or later. A Pixel 2 never set
 * keeps 2018, and a corner that said 10:43 then would say a time that is
 * not now; the shelf shows no clock until it is set. */
bool clock_is_set(time_t t);

/* The time zones on offer, by city. A short list rather than the four hundred
 * the zone database has, because stepping through those with left and right
 * would take minutes; every hour of offset anyone lives in is here. */
typedef struct {
	const char *id;     /* the zone database's name, which TZ takes */
	const char *city;   /* what the row shows */
} clock_zone;

extern const clock_zone CLOCK_ZONES[];
extern const int CLOCK_ZONE_COUNT;

/* The list's entry for a zone id, or -1 when it is not on the list (a setting
 * from before the list, or edited by hand). */
int clock_zone_find(const char *id);

/* The entry `d` steps from `id`'s, stopping at the ends. An id not on the list
 * steps onto it from New York, the shipped default. */
int clock_zone_step(const char *id, int d);

/* Auto (src/autozone.h) is the row's first choice, before the list's west
 * end, as Game Language's Auto is before its languages: left from Honolulu
 * is Auto, right from Auto is Honolulu, and left from Auto stays. The choice
 * `d` steps to from `id`'s, or from Auto when `autoz`: CLOCK_ZONE_AUTO or an
 * entry of the list. An id not on the list steps onto it from New York, as
 * clock_zone_step has it. */
#define CLOCK_ZONE_AUTO (-1)
int clock_zone_step_auto(const char *id, bool autoz, int d);

/* A name the zone database could have: its letters, digits and _ + - /, in
 * parts that each start with a letter, 63 bytes at most. Never a path (no
 * dot, no leading slash) nor one of TZ's other forms (":file", "<-03>3"):
 * TZ is read by the C library as the name of a file under its zone
 * directory, and a name from a reply or a card is not passed there
 * unchecked. */
bool clock_zone_name_ok(const char *id);

/* Where the zone files are, on the devices (plastron's tzdata, every zone)
 * and on a Mac or a Linux host */
#define CLOCK_ZONEINFO "/usr/share/zoneinfo"

/* This system has zone `id`: on the list, or a name clock_zone_name_ok takes
 * whose file is in CLOCK_ZONEINFO */
bool clock_zone_have(const char *id);

/* The words for zone `id`, for a row: the list's city ("New York", "India"),
 * else the name's last part with spaces ("Asia/Ho_Chi_Minh" is "Ho Chi
 * Minh"), and "UTC+3" for the zone database's Etc/GMT-3, whose sign is
 * the other way round. */
void clock_zone_city(const char *id, char *out, size_t n);

/* What a choice of the zone (CLOCK_ZONE_AUTO or an entry of the list) does,
 * from zone `id` with Auto `autoz`, by the row's left and right or by the
 * list A opens: the one rule for both. A zone picked by hand turns Auto off
 * and is set (`zone`, NULL when it is the zone in use with Auto off already).
 * Auto turns Auto on and keeps the zone until its first answer;
 * `auto_chosen` when it was off, so what Auto tells is said. */
typedef struct {
	bool        auto_on;
	bool        auto_chosen;
	const char *zone;
} clock_zone_change;
clock_zone_change clock_zone_choose(const char *id, bool autoz, int choice);

/* The list A on the Time Zone row opens (2026-11-14: "when clicking on the
 * time zone row, i should see a list of all the options"): Auto first, as
 * clock_zone_value says it ("Auto (Sao Paulo)" once it has found a zone),
 * then every zone of the list, west to east. Into `rows`, with no values,
 * Auto's words in `auto_buf`; how many, at most `max` (CLOCK_ZONE_LIST_MAX
 * holds them all). clock_zone_list_on is the row of the choice that is on,
 * which takes the tick, or -1 for an id not on the list with Auto off;
 * clock_zone_list_row is where the cursor starts, that row or New York's (as
 * the row steps from such an id); clock_zone_list_choice is the choice a
 * row is, -2 for none. */
#define CLOCK_ZONE_LIST_MAX 48
int clock_zone_list(const char *id, bool autoz, const char *found, menu_row *rows,
                    int max, char *auto_buf, size_t n);
int clock_zone_list_on(const char *id, bool autoz);
int clock_zone_list_row(const char *id, bool autoz);
int clock_zone_list_choice(int row);

/* The Time Zone row's value. Auto off: the zone's city, or an id not on the
 * list as it is. Auto on: "Auto" until it has found a zone, then "Auto
 * (Sao Paulo)", the city of the zone it found, while that is the zone in use
 * (`found`, the last Auto set, is `id`). */
void clock_zone_value(const char *id, bool autoz, const char *found, char *out,
                      size_t n);

#endif
