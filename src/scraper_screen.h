/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SCRAPER_SCREEN_H
#define ZYPEROS_SCRAPER_SCREEN_H

/* MENU > Scraper (DESIGN.md 11.19): one page - its plan, its run and what
 * it found, its questions and settings, a test of it on a host and its
 * --scraper-page shot - and the ScreenScraper and Video Snaps pages it
 * opens. The screens are src/scraper_screen.c; the run is src/artscrape.c. */
#include "app.h"
#include "panel.h"

/* What main.c uses of it; each is described where it is defined */
extern int shot_scraper_page, shot_scraper_page_t;
extern int g_scp_names;
void scraper_page_shot(app *a);
menu_result ss_page(app *a);
menu_result snaps_screen(app *a);

/* --rows-report: Video Snaps' rows (menu_rows_print) */
void snaps_rows_report(app *a);
/* A run that ended behind the screens (src/dlq.h): the shelves take its
 * names, covers and snaps, on the shelf's own pass */
void scraper_after_run(app *a);

/* For the Downloads page (src/dl_screen.h): the kinds a run gets, and what
 * the page's plan counts with them in - covers to look for, videos to get,
 * names to look up, the room and the time it takes (the page's own
 * estimates), the systems whose switch is on, and whether it can run
 * (a network, the room). scraper_run starts the page's run with them, as
 * its A does; false when there is nothing to fetch, no network or no room. */
typedef struct { bool covers, videos, names; } scraper_kinds;
typedef struct {
	int covers, videos, names, looks;
	int cov_systems, vid_systems;
	double bytes, secs;
	bool net, ss_in, fits;
} scraper_counts;
void scraper_plan_of(app *a, scraper_kinds k, scraper_counts *out);
bool scraper_run(app *a, scraper_kinds k);
/* Names and Details (Settings): the same switch as the page's */
bool scraper_names_on(void);
void scraper_names_set(bool on);
/* A time as the page says one: "12 min", "1 h 20 min" */
void scraper_time_text(double secs, char *out, size_t n);

#endif
