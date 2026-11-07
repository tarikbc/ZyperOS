/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_THEME_H
#define ZYPEROS_THEME_H

/* Which theme is on: the one answer every screen asks, and the one place the
 * handheld's clock is read to give it.
 *
 * DESIGN.md has two themes, Beard (DS_LIGHT) and Salt & Pepper (DS_DARK), and
 * MENU > Appearance has three settings for choosing between them: Light, Dark,
 * and Auto, which is Beard from 7:00 to 19:00 by the handheld's clock and
 * Salt & Pepper the rest of the day. The setting is what a player chose and
 * is saved; the theme is what it comes to at this minute and is not. Every
 * screen draws from theme(), and nothing but this file turns one into the
 * other.
 *
 * Plain C and no SDL, like src/sys_menu.c and src/controls.c, so that
 * tools/theme-check.c links it without a window; and no database either:
 * the caller reads the saved word and hands it to theme_init, and saves the
 * word it is given by the menu. The clock is time() and localtime_r, which is
 * what the handheld's own zone setting moves (plat_clock_zone sets TZ), so
 * Auto follows MENU > Date & Time without being told about it.
 *
 * It also keeps THEME_FILE (src/themefile.h) saying the theme that is on, for
 * Diatom and portkeys, which draw the volume and brightness bar without the
 * launcher: written at every start and at every change this process sees.
 * That is every change while it runs, a resident Diatom's games included,
 * whose wait asks the clock (on_game_tick). It is not while a port runs,
 * which the launcher exits for (launch_port), and there portkeys reads the
 * setting from the database and follows Auto itself (theme_saved_at). Nor is
 * it while the fallback Diatom runs a game, which the launcher waits for in
 * plat_run with nothing asking: across 7:00 or 19:00 there, the bar keeps the
 * theme the game started in (DESIGN.md 14). */

#include <stdbool.h>
#include <time.h>

#include "design.h"

/* The setting, in the order the row cycles through them. Light is first and
 * the default: DESIGN.md says Beard is the theme of a new device, and it is
 * what anything that is not one of the three reads as. */
typedef enum { APPEAR_LIGHT, APPEAR_DARK, APPEAR_AUTO } appearance;

/* The theme a setting makes at an hour of the local day (0-23): DS_LIGHT or
 * DS_DARK. Pure, which is the point of it being a function of two numbers:
 * Auto's edges are what a check can hold and a person cannot be awake to see.
 *
 * Light and Dark ignore the hour. Auto is Light for hours 7 through 18, that
 * is from 7:00:00 up to but not including 19:00:00, and Dark for the rest.
 * An hour outside 0-23 is a clock that could not be read, and that is not
 * night: it is Light, the default, whatever the setting is called. */
int theme_resolve(appearance a, int hour);

/* The theme the saved word `saved` makes at `now`: the word read as the
 * launcher reads it (appearance_parse, so NULL, "" and a word that is none of
 * the three are Light) and Auto resolved at the local hour of `now`, in the
 * zone TZ names (theme_resolve). Pure but for the clock's zone: no state here
 * changes and no file is written.
 *
 * For a program that draws in the launcher's theme without the launcher:
 * portkeys, over a port, which the launcher exits for, so THEME_FILE is not
 * written again until it is back. With this the bar follows Auto across
 * 7:00 and 19:00 as the shelf would, from the setting in the database. */
int theme_saved_at(const char *saved, time_t now);

/* What the menu shows: "Light", "Dark", "Auto". A setting that is not one of
 * the three is named as the Light it acts as. */
const char *appearance_name(appearance a);

/* What the database stores, under the key `appearance`: "light", "dark",
 * "auto". Words and not numbers, so the file can be read and edited by a
 * person, and so the order of the enum is not a promise the saved data keeps. */
const char *appearance_id(appearance a);

/* The setting a word names, from the database or from the menu's capitals:
 * the whole word, in either case, and nothing else. Anything that is not one
 * of the three - a missing key, an empty one, a word from a later release -
 * is APPEAR_LIGHT, so a setting gone wrong never turns the shelf dark. */
appearance appearance_parse(const char *s);

/* The setting `d` steps from `a` (-1 or +1, or 0 for none), wrapping at both
 * ends: Light, Dark, Auto and round again. A setting that is not one of the
 * three steps from Light. */
appearance appearance_step(appearance a, int d);

/* Where THEME_FILE is written, THEME_FILE unless this says otherwise, and
 * nowhere at all for NULL: a check on a development host, where a launcher
 * may be running, must not write over the one it would be testing. The path
 * is not copied, and must outlive its use. */
void theme_set_file(const char *path);

/* Starts, or changes, the setting: resolves it for now, and writes the theme
 * file whether or not the theme changed, because a file left by an earlier
 * launcher says what that one decided. At start from the saved word, and again
 * from the menu as the row is changed, where it takes effect at once.
 *
 * A file that cannot be written is a line on stderr and nothing more: a bar
 * in the wrong theme over a game is a smaller harm than a launcher that does
 * not start, and the theme on screen is right either way. */
void theme_init(appearance a);

/* theme_init at `now`, for the check, which has to ask what Auto does at
 * 6:59 and at 7:00 and cannot wait for either. */
void theme_init_at(appearance a, time_t now);

/* Auto's clock, from the main loop: true when the theme changed since it
 * was last asked, and then the file says so too; the caller redraws.
 *
 * Reads the clock at most once a minute. The main loop passes every 16 ms
 * when nothing is drawn and the answer changes twice a day, so a read and a
 * localtime_r on every pass would be asked a question whose answer is "no"
 * nearly four thousand times for each time it is "yes". The price is that Auto
 * is up to a minute late at its edges, which is not a thing anyone sees.
 * A clock that moved back, as it does when the time is set by hand in Date &
 * Time, is looked at at once, as is one that moved forward by more than the
 * minute. Light and Dark have nothing to look at and are never true. */
bool theme_tick(void);

/* theme_tick at `now`, for the check. */
bool theme_tick_at(time_t now);

/* Reads the clock now, whatever the minute says, and answers as theme_tick
 * does. For the moment something is known to have moved under Auto: the time
 * zone chosen in Date & Time puts the same instant in another hour, and the
 * theme is to follow it at once and not at the next look. A look like any
 * other, so the minute is counted from it. Never true for Light or Dark. */
bool theme_refresh(void);

/* theme_refresh at `now`, for the check. */
bool theme_refresh_at(time_t now);

/* A theme's index as one of the two: DS_DARK for DS_DARK and DS_LIGHT for
 * anything else, the default, so an index handed in from anywhere picks a
 * table that exists. And that theme's table, for drawing in a theme a caller
 * names rather than the one on: a card drawn on the loader's workers, a
 * notice, what sits on scrim.game. Inline and plain, so the checks that link
 * no theme.c (tools/cardgen-check.c) can use them, and each points into the
 * including file's own copy of DS_THEME. */
static inline int theme_clamp(int ix)
{
	return ix == DS_DARK ? DS_DARK : DS_LIGHT;
}

static inline const ds_theme *theme_at(int ix)
{
	return &DS_THEME[theme_clamp(ix)];
}

/* The theme that is on: its table in src/design.h, and its index into
 * DS_THEME, which is DS_LIGHT or DS_DARK. Beard until theme_init is called,
 * so a screen drawn before the setting is read is the default and not a
 * crash. */
const ds_theme *theme(void);
int theme_index(void);

/* The setting that is on, which is what the row shows and the next press
 * steps from. APPEAR_LIGHT until theme_init. */
appearance theme_appearance(void);

#endif
