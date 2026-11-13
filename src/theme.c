/* SPDX-License-Identifier: GPL-3.0-only */
/* Which theme is on. See src/theme.h. */
#include "theme.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "i18n.h"
#include "look.h"
#include "themefile.h"

/* How long the clock is trusted for, in seconds: Auto's hours are whole hours,
 * so a theme a minute out of date at an edge is the same theme to anybody who
 * is looking. */
#define RECHECK_S 60

/* The state, which is three facts and the place the fourth is written. Static
 * and not a struct the caller holds: there is one theme at a time and every
 * screen asks for it, and a handle every draw call had to be passed would be
 * the whole of ui.c's signatures changing for nothing. */
static appearance cur = APPEAR_LIGHT;
static int idx = DS_LIGHT;
/* Auto's hours, minutes into the local day (theme_set_hours) */
static int light_from = THEME_LIGHT_FROM, dark_from = THEME_DARK_FROM;
static time_t looked;          /* when the clock was last read, for Auto */
static const char *file = THEME_FILE;

int theme_resolve_min(appearance a, int minute)
{
	switch (a) {
	case APPEAR_DARK:  return DS_DARK;
	case APPEAR_AUTO:
		/* Light from light_from up to dark_from, and dark from there round
		 * to light_from: 7:00 up to 19:00 as it ships. Hours that go round
		 * midnight (light from 20:00 to 6:00) are the same rule the other
		 * way; the same hour twice is never dark. Not a clock that could not
		 * be read: see theme.h. */
		if (minute < 0 || minute >= 24 * 60) return DS_LIGHT;
		if (light_from == dark_from) return DS_LIGHT;
		if (light_from < dark_from)
			return minute >= light_from && minute < dark_from ? DS_LIGHT : DS_DARK;
		return minute >= light_from || minute < dark_from ? DS_LIGHT : DS_DARK;
	case APPEAR_LIGHT:
	default:           return DS_LIGHT;
	}
}

int theme_resolve(appearance a, int hour)
{
	return theme_resolve_min(a, hour < 0 || hour > 23 ? -1 : hour * 60);
}

static int hours_clamp(int m, int fallback)
{
	return m >= 0 && m < 24 * 60 ? m - m % THEME_HOURS_STEP : fallback;
}

void theme_set_hours(int light_min, int dark_min)
{
	light_from = hours_clamp(light_min, THEME_LIGHT_FROM);
	dark_from = hours_clamp(dark_min, THEME_DARK_FROM);
}

void theme_hours(int *light_min, int *dark_min)
{
	if (light_min) *light_min = light_from;
	if (dark_min) *dark_min = dark_from;
}

const char *appearance_name(appearance a)
{
	switch (a) {
	case APPEAR_DARK: return T(S_THEME_DARK);
	case APPEAR_AUTO: return T(S_THEME_AUTO);
	case APPEAR_LIGHT:
	default:          return T(S_THEME_LIGHT);
	}
}

const char *appearance_id(appearance a)
{
	switch (a) {
	case APPEAR_DARK: return "dark";
	case APPEAR_AUTO: return "auto";
	case APPEAR_LIGHT:
	default:          return "light";
	}
}

appearance appearance_parse(const char *s)
{
	if (!s) return APPEAR_LIGHT;
	/* The whole word and nothing else: strcasecmp is a comparison of the two
	 * strings to their ends, so "darkness" and "dark " are not "dark". */
	if (!strcasecmp(s, "dark")) return APPEAR_DARK;
	if (!strcasecmp(s, "auto")) return APPEAR_AUTO;
	return APPEAR_LIGHT;
}

appearance appearance_step(appearance a, int d)
{
	int i = (int)a;

	/* Three of them in a ring. The remainder of a negative number is
	 * negative in C, hence the second modulus. */
	if (i < 0 || i > (int)APPEAR_AUTO) i = APPEAR_LIGHT;
	return (appearance)((((i + d) % 3) + 3) % 3);
}

/* The local minute of the day of `t`, or -1 if the time cannot be made into
 * one (a time_t so far from now that the year does not fit), which
 * theme_resolve_min reads as the default and not as night. */
static int minute_of(time_t t)
{
	struct tm tm;

	if (!localtime_r(&t, &tm)) return -1;
	return tm.tm_hour * 60 + tm.tm_min;
}

int theme_saved_at(const char *saved, time_t now)
{
	/* theme_init_at's reading, without its state: the word as the launcher
	 * parses it, and the clock asked only when the setting is Auto. */
	appearance a = appearance_parse(saved);

	return theme_resolve_min(a, a == APPEAR_AUTO ? minute_of(now) : 0);
}

void theme_set_file(const char *path)
{
	file = path;
}

/* Says the theme that is on to the programs that read the file, or says on
 * stderr why not. Never more than that, as theme.h says why. */
static void publish(void)
{
	if (!file) return;
	/* with the bar's colors when the player's look has its own (look.h) */
	if (!themefile_write_bar(file, idx, look_is_default(look_now()) ? NULL : theme_at(idx)))
		fprintf(stderr, "theme: %s: %s\n", file, strerror(errno));
}

void theme_init_at(appearance a, time_t now)
{
	/* A setting that is not one of the three is stored as the Light it acts
	 * as, so theme_appearance() never hands the menu a number it has no name
	 * for. */
	cur = a == APPEAR_DARK || a == APPEAR_AUTO ? a : APPEAR_LIGHT;
	looked = now;
	idx = theme_resolve_min(cur, cur == APPEAR_AUTO ? minute_of(now) : 0);
	publish();
}

void theme_init(appearance a)
{
	theme_init_at(a, time(NULL));
}

bool theme_refresh_at(time_t now)
{
	int next;

	if (cur != APPEAR_AUTO) return false;

	looked = now;
	next = theme_resolve_min(cur, minute_of(now));
	if (next == idx) return false;
	idx = next;
	publish();
	return true;
}

bool theme_refresh(void)
{
	return theme_refresh_at(time(NULL));
}

bool theme_tick_at(time_t now)
{
	if (cur != APPEAR_AUTO) return false;
	/* Trusted for a minute from the last look. A time before the last look
	 * is a clock that was set back, by hand in Date & Time: the minute it
	 * was trusted for belongs to a time that has not come, so it is read
	 * again. */
	if (now >= looked && now - looked < RECHECK_S) return false;
	return theme_refresh_at(now);
}

bool theme_tick(void)
{
	return theme_tick_at(time(NULL));
}

const ds_theme *theme(void)
{
	return theme_at(idx);
}

int theme_index(void)
{
	return idx;
}

appearance theme_appearance(void)
{
	return cur;
}
