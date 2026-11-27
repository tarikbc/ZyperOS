/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHOTNAME_H
#define ZYPEROS_SHOTNAME_H

/* A screenshot's file, as the Screenshot hotkey names it (DESIGN.md 11.61,
 * 11.68): Screenshots/<System>/<Game> 2026-11-18 21.40.05.png on the card,
 * the launcher giving the folder and the game (RUN shots=) and Diatom the
 * time. Pure C, no SDL: the launcher's Screenshots group reads names with it
 * (src/shelfbuild.c), launch_screen.c names the game with it, and
 * tools/shotname-check.c holds it (make check-shotname).
 *
 * A shot's time is kept as the seconds of its wall clock from 2000-01-01
 * 00:00:00, worked out from the name's digits by the calendar alone, never
 * through a time zone: the name read back from it is the name on the card,
 * whatever the zone is, and a later time is a bigger number, so the newest
 * sort first. */
#include <stdbool.h>
#include <stddef.h>

/* The card's folder of them, under P_CARD */
#define SHOT_DIR "Screenshots"
/* ...and the launcher's own screens' in it, MENU+L1's (src/menukeys.h):
 * Screenshots/ZyperOS/ZyperOS 2026-11-22 10.04.31.png, "ZyperOS" the game */
#define SHOT_LAUNCHER_DIR "ZyperOS"
/* " 2026-11-18 21.40.05.png": what follows the game's name */
#define SHOT_STAMP_LEN 19
#define SHOT_TAIL_LEN (1 + SHOT_STAMP_LEN + 4)

/* The game's part of a shot's name, as the launcher writes it: the shelf's
 * name for the game with what a file's name cannot hold ('/', '\\', a tab)
 * as '-'. Into out (n bytes), cut there. */
void shot_game_part(const char *name, char *out, size_t n);

/* A file in a Screenshots/<System> folder read: true when it is a shot,
 * "<game> YYYY-MM-DD HH.MM.SS.png" (".PNG" too), with its game part into
 * `game` (n bytes) and its time into *when (shot_secs). A name with no game
 * before the time, a time the calendar has not got, or any other file:
 * false. */
bool shot_parse(const char *file, char *game, size_t n, long *when);

/* A wall clock as shot_parse keeps it, from its parts; -1 when the calendar
 * has not got it (a 30 February, a 25th hour), or before 2000 */
long shot_secs(int year, int mon, int day, int hour, int min, int sec);
/* And its parts back */
void shot_parts(long when, int *year, int *mon, int *day, int *hour, int *min, int *sec);

/* The shot's file name again, from its game part and its time: what
 * shot_parse read. Into out (n bytes); false when it does not fit. */
bool shot_file_name(const char *game, long when, char *out, size_t n);

/* OpenBOR's own shots (DESIGN.md 11.68), which OpenBOR takes with SELECT and
 * names itself, in the folder it is started in (sd/pixel2/OpenBOR.sh:
 * Saves/openbor on the card): its engine/source/utils.c's screenshot(), at
 * the pinned b8303cc (mk/build-openbor.sh), writes
 * ScreenShots/<module> - 0000.png, the module its .pak's name without the
 * extension ("Menu" for one taken in OpenBOR's own menu), the number the
 * first free one from 0000 to 9999. No time in the name: the file's own. */
#define SHOT_OPENBOR_DIR "Saves/openbor/ScreenShots"
#define SHOT_OPENBOR_TAIL_LEN 11              /* " - 0000.png" */
/* True when `file` is one: its module into `game` (n bytes), cut there, and
 * its number into *num (NULL for none) */
bool shot_parse_openbor(const char *file, char *game, size_t n, int *num);

/* How many whole days `then` is before `now` on the calendar (0 the same
 * day, 1 the day before), both as shot_secs keeps them */
long shot_days_before(long then, long now);

/* A shelf of shots in order (MENU > Sort By, DESIGN.md 11.68): Recently
 * Added, the newest first; Name, the games A to Z and each one's newest
 * first; System, the home's order of the systems (their owners'), then as
 * Name. One shot as the order sees it: */
typedef struct {
	const char *title;   /* the game's name as its shelf shows it */
	long when;
	int owner;           /* its system's place on the home */
} shot_key;
typedef enum { SHOT_ORDER_ADDED, SHOT_ORDER_NAME, SHOT_ORDER_SYSTEM } shot_order;
/* <0, 0 or >0 as a goes before, with, or after b in `order` */
int shot_cmp(const shot_key *a, const shot_key *b, shot_order order);

#endif
