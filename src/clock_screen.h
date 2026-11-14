/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CLOCK_SCREEN_H
#define ZYPEROS_CLOCK_SCREEN_H

/* MENU > Date & Time, with Time Zone's list and its Auto (src/autozone.h).
 * The screen is src/clock_screen.c; a shot's --datetime draws its rows. */
#include "app.h"
#include "clock.h"
#include "linebreak.h"
#include "menu.h"

#include <stdbool.h>
#include <time.h>

/* Date & Time (src/clock.h). Time Zone first, then each row is one part of
 * the clock, changed with left and right and set the moment it changes: the
 * system clock and the hardware clock both, so it holds through a power-off.
 * Time Zone steps through Auto and then CLOCK_ZONES, and is the library
 * database's "timezone", which the Brick's launch.sh reads at boot as well.
 * The rows are clock_rows', so tools/clock-check.c holds their order. */
typedef struct {
	clock_bufs b;
	char zone[80];   /* a city, an id not on the list as it is, or Auto's */
} clock_ui;

/* The list A on Time Zone opens (clock_zone_list): every choice, a tick on
 * the one that is on and the cursor on it, as MENU > Language has it. A
 * picks and closes; B closes with nothing changed. */
typedef struct {
	char          auto_words[80];
	int           pick;                         /* the row A picked, -1 */
	unsigned char sound[CLOCK_ZONE_LIST_MAX];   /* every row plain */
} zone_list_ui;

/* Date & Time's Auto, with its screen: true when it set a new zone. Every
 * two seconds from the screens' loops (tick_theme). */
bool tick_auto_zone(void);
/* What Auto tells and to whom, on up to two balanced lines */
int zone_auto_lines(app *a, char lines[2][LB_LINE], menu_row rows[2]);
/* The page's rows, the screen's and a shot's */
int clock_page_rows(clock_ui *c, time_t t, const char *id, bool autoz,
                    const char *found, bool wifi, menu_row *rows, int max);
/* Time Zone's list's rows, the screen's and a shot's */
int zone_list_rows(zone_list_ui *z, const char *id, bool autoz, const char *found,
                   menu_row *rows, int max);
/* MENU > Date & Time */
void clock_screen(app *a);

#endif
