/* SPDX-License-Identifier: GPL-3.0-only */
/* How long each game has been played.
 *
 * WALL CLOCK, from RUN to EXIT, and there is nothing to subtract: the device
 * has no suspend and is not getting one (see the Auto Off row in sys_menu.c).
 * Auto Off powers it OFF, so a session left running is bounded by that
 * interval rather than by how long someone was away.
 *
 * THE LAUNCH PATH DOES NO I/O. stats_begin notes a number in memory and
 * nothing else - no file opened, nothing written, nothing synced - because
 * launch speed is the point of this firmware and a millisecond bought here
 * would be the first of several. Everything else happens from the game tick,
 * which already runs at 10 Hz and is cheap by contract, or at exit.
 *
 * NOTHING IS FSYNCED WHILE A GAME RUNS. Measured on the card: an fsync is
 * 1.56 ms median but 16.4 ms at the tail, which is a dropped frame. The writes
 * during play are unsynced, so they cost 0.007 ms and reach the card when the
 * kernel gets to them - about 33 s later, measured. That window is the right
 * trade here and self-correcting: by the time a session is long enough to be
 * worth recording, its marker has been on the card for a long while.
 *
 * SESSIONS THAT NEVER REACH EXIT. A power cut or a flat battery means no EXIT,
 * so a marker is written a few seconds in and updated every minute or so. On
 * the next boot an unclosed row is closed at its last checkpoint - short by up
 * to that interval rather than lost entirely.
 *
 * Rows live in the LIBRARY database, not the device one: play time belongs to
 * the card the games are on, the same argument favorites and earned
 * achievements are stored under. Keyed
 * "sess.<start>.<clock>.<tag>\t<file>", where start is the unix time the
 * session began and clock is the millisecond clock at RUN. The second number
 * is there only to keep two launches of one game inside the same second from
 * sharing a key, and it has to be something already in hand: asking the
 * database whether a key is free would put a read on the launch path.
 */
#ifndef ZYPEROS_STATS_H
#define ZYPEROS_STATS_H

#include <stdbool.h>
#include <stddef.h>

/* A game is a system tag plus the ROM's launch path, the same key favorites
 * use, so a file of the same name on two systems counts as two games. */
#define STATS_TAG_MAX   16
#define STATS_FILE_MAX 544
#define STATS_MAX      512   /* distinct games held in memory when summarizing */

/* Marker after this long, so a launch someone backs straight out of writes
 * nothing at all. Then a checkpoint at this interval, which is also the most
 * a crash can lose. */
#define STATS_MARK_MS  (5 * 1000u)
#define STATS_CKPT_MS  (60 * 1000u)

/* RUN. In-memory only - see the header note. */
void stats_begin(const char *tag, const char *file, unsigned now_ms);

/* From the game tick. Writes at most one unsynced row, and usually nothing. */
void stats_tick(unsigned now_ms);

/* EXIT. The one write that is synced, on the path where the player is already
 * waiting for the shelf rather than for a game. `reason` is Diatom's own EXIT
 * reason, or NULL for a clean quit. */
void stats_end(const char *reason, unsigned now_ms);

/* Close any session left open by a power cut, at its last checkpoint. Call
 * once at startup, before anything reads a total. */
void stats_recover(void);

/* A game deleted from the card (src/gamedel.h): every session of this tag
 * and file out of the library database, which is its play time and the
 * facts the shelf sorts it by. How many rows went. */
int stats_forget(const char *tag, const char *file);

/* --- a program of its own ----------------------------------------------- *
 *
 * A port, PortMaster's or the Ports shelf's, OpenBOR, the standalone Flycast
 * and your PICO-8 with Splore run with no launcher beside them: the launcher
 * names the program in a request and EXITS, launch.sh runs it and starts the
 * launcher again after it (src/launch_screen.c, launch_port_core). So their
 * session cannot be held in memory from RUN to EXIT, as a Diatom game's is,
 * and none was kept for them at all: a cart played on your PICO-8 never
 * reached Recents, Played or the play time (1.1.0-rc.8: "The recents page
 * is not considering pico-8 games").
 *
 * The launcher hands the session over as it hands the program over: its row
 * in the library database at once, "open" at 0 s - the same marker a power
 * cut leaves, so a program that never comes back is still on Recents, short
 * by that session as a crash is - and a note in /tmp of when it began.
 * launch.sh closes it the moment the program ends, POWER and Auto Off
 * included, before anything powers off (`zyperos.elf --port-played`), and
 * the launcher's own start closes one still open (a launch.sh that did not).
 * Nothing checkpoints it meanwhile: no process of ZyperOS's that runs beside
 * every program writes this database.
 *
 * The seconds are the MONOTONIC clock's (stats_mono_ms), which every process
 * shares and which a clock set by the network while the program runs does
 * not move, so the time is the program's and nothing else's. The note is
 * "<start>\t<key>\t<mono_ms>\t<tag>\t<file>\n", written whole or not at
 * all: the row's key's number, and the clock the seconds are counted from. */
#define STATS_PORT_NOTE "/tmp/zyperos_port_played"
/* What launch.sh runs as the program ends: the launcher with this alone
 * closes the session and exits, no display, no input (src/main.c) */
#define STATS_PORT_PLAYED_ARG "--port-played"

/* CLOCK_MONOTONIC in milliseconds: the same in every process of a boot. */
long long stats_mono_ms(void);

/* The program is about to run: its row "open" and the note at `note` (the
 * launcher's plat_flag of STATS_PORT_NOTE). `now` is the unix time, `mono_ms`
 * stats_mono_ms, passed in so the check can place them. False when the note
 * could not be written, and nothing else is then. */
bool stats_hand_over(const char *tag, const char *file, const char *note,
                     long now, long long mono_ms);

/* The program has ended: its session closed at `mono_ms` as a quit, and the
 * note removed. False with no note, or a note that is not one (removed):
 * nothing ran, or it was closed already. */
bool stats_handed_back(const char *note, long long mono_ms);

/* --- reading, for the menu ---------------------------------------------- */

/* Which slice of history a summary covers.
 *
 * CALENDAR boundaries, not rolling ones: "this week" is the week you are in,
 * not the last seven days. Someone asking what they played this week means
 * the week, and a rolling window answers a question nobody asked - it also
 * changes its own answer every hour, which makes a number impossible to
 * check twice.
 *
 * Applied to the session's START, which is the key's first segment, so a
 * session that ran across midnight belongs to the day it began on. */
typedef enum {
	STATS_ALL, STATS_YEAR, STATS_MONTH, STATS_WEEK, STATS_TODAY,
	STATS_WINDOWS
} stats_window;

/* "All Time", "This Year", "This Month", "This Week", "Today". */
const char *stats_window_name(stats_window w);

/* The unix time a window opens at, or 0 for STATS_ALL. Exposed for the check,
 * which has to be able to place a session on either side of a boundary. */
long stats_window_start(stats_window w, long now);

/* Fold the sessions inside `w` into one row per game - or one per SYSTEM when
 * `by_system`, which is the "what do I actually play" question rather than
 * "which game" - most-played first. `now` is passed in for the same reason
 * now_ms is: a window boundary has to be drivable from a test.
 *
 * Returns the number of rows. Cheap enough to call on every view change: it
 * is one pass over the session rows, and there are hundreds, not millions. */
int  stats_summarize(stats_window w, bool by_system, long now);

/* tag is the system, and file is the ROM - empty when the rows were folded
 * by system, because a system is not one file. */
bool stats_at(int i, const char **tag, const char **file,
              long *seconds, int *launches);

/* The rest of what a row knows: the longest single session in it, the unix
 * time it was last started, and how many of its sessions ended `lost` rather
 * than `quit`. */
bool stats_extra(int i, long *longest, long *last, int *lost);

/* One game's totals, for a caller that has a game rather than a row: the
 * shelf sorting by play time asks this per entry. Reads whatever the last
 * stats_summarize folded, so call that first - and note the folded rows are
 * shared with the play-time screen, which re-folds them under its own window
 * whenever it is opened. Both re-fold on entry, so neither reads the other's
 * answer today; it is one set of rows, not two, and worth knowing.
 *
 * Returns false for a game with no sessions, leaving the outputs untouched. */
bool stats_lookup(const char *tag, const char *file,
                  long *seconds, long *last);

/* The games started last, newest first: each game once, by when it was last
 * started, up to `max` of them into `out`. Walks the session rows themselves
 * rather than a summary, so a card with more than STATS_MAX games played
 * still has its newest ones. For the home's Recents (src/shelfbuild.c).
 * Returns how many. */
typedef struct {
	char tag[STATS_TAG_MAX];
	char file[STATS_FILE_MAX];
	long last;          /* unix time it was last started */
} stats_recent_row;
int stats_recent(stats_recent_row *out, int max);

long stats_total_seconds(void);
int  stats_total_launches(void);

/* Sessions that never reached EXIT - a flat battery, a power cut, or a crash.
 * A play statistic only by accident: it is the one number either project has
 * that says a game DIED rather than was quit. */
int  stats_total_lost(void);

/* "12h 34m", "7m 12s", "never". `out` takes at least 16 bytes. */
void stats_format(long seconds, char *out, size_t n);

/* "3h ago", "2d ago", "today", "never". `out` takes at least 16 bytes. */
void stats_ago(long then, long now, char *out, size_t n);

/* When, as the Screenshots shelf says when a shot was taken (DESIGN.md
 * 11.68) and Recents when a game was last played (11.48): stats_ago's words,
 * and for today and yesterday the clock as MENU > Clock has it ("today
 * 21:40", "yesterday 9:05 PM", "3d ago"). `out` takes at least 32 bytes. */
void stats_when(long then, long now, bool clock24, char *out, size_t n);

#endif
