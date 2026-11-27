/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RADL_H
#define ZYPEROS_RADL_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/* RetroAchievements for every game on the card, ahead of time: MENU >
 * Achievements' Download (1.0.6, asked for on the Discord: "think of it like
 * a scraper for achievements").
 *
 * A game's set used to come only from its first play with Wi-Fi (src/
 * rafetch.h), so a game first played offline had no achievements at all.
 * This puts on the card, for each game, what that first play would have:
 *
 *   the set      Roms/<System>/.cheevos/<name>.set, the same file through the
 *                same converter (ra_set_from_json_hash), or <name>.none when
 *                RetroAchievements has nothing for the ROM (chv_none_mark)
 *   its badges   .cheevos/badges/<name>.png, one curl a set (net_get_pngs)
 *   the account  what it already holds for the game, merged into the earned
 *                store as on the account (chv_note_earned), so the count is
 *                right offline and Diatom does not watch what is earned
 *
 * WHAT IT ASKS, and why not one game at a time as a first play does. Two
 * requests a system answer most of it (RAWeb, app/Connect/Actions):
 *
 *   r=hashlibrary&c=<console>   every hash RetroAchievements knows for a
 *                               console, with its game: no account. A hash
 *                               not in it is one r=gameid would answer with
 *                               0 or a stand-in id for an unsupported dump,
 *                               both "none" here already.
 *   r=allprogress&c=<console>   every game of the console with achievements:
 *                               how many it has now, and how many the account
 *                               holds. A set on the card with another count is
 *                               out of date; an account that holds more than
 *                               the store knows has unlocks to take.
 *
 * Then per game only what is missing: r=patch for a set not on the card or
 * out of date, r=unlocks when the account holds more. A game already right
 * costs nothing. A hash the system's library lacks is asked once with
 * r=gameid, as a first play asks it - a game on another console's shelf is
 * in that console's library - and is then marked none, which a later run
 * checks against the library alone. A console whose library did not come
 * asks r=gameid for each of its games.
 *
 * HASHES are the slow part of a first run, and are kept: Roms/<System>/
 * .cheevos/.hashes, one line a file - its size, its time, its hash and the
 * count its set had when last checked - so a file is hashed once, until it
 * changes, or the way its system is hashed does (HASHES_HEAD in radl.c). They are made on a thread of their own while the system's two
 * requests are on their way (ra_hash_rom: a PSP .pbp is the whole file).
 *
 * PACE. RetroAchievements throttles dorequest.php per account, 180 a minute
 * as RAWeb ships (config/connect.php), and answers 429 past it. This asks one
 * thing at a time, RADL_GAP_MS apart at least: 100 a minute at most. A 429
 * waits RADL_SLOW_MS, the server's minute, and asks again. No answer is asked
 * again after 5, 15 and 30 s, and while there is no network at all the run
 * waits for one and goes on by itself. Every game's result is on the card the
 * moment it arrives, so a run stopped by B, POWER, Auto Off or a network that
 * never came back leaves nothing half done, and the next one starts from what
 * is still missing.
 *
 * The player's own account and nothing else: the token from src/rafetch.h,
 * the files on their own card. That is RetroAchievements' rule for the API
 * (public/dorequest.php): per-user credentials, nothing cached server-side or
 * shared, its limits respected, a User-Agent of our own (src/net.c).
 *
 * Driven from the page's frame loop, radl_step a frame, like Box Art's run
 * (src/artscrape.h): a step starts a request or polls the one in flight
 * (net's one async slot) and never waits. The time is the caller's, so a
 * check can pass the minute a 429 asks for in a moment. */

/* A set's path, Roms/<folder>/.cheevos/<name>.set (library.h's LIB_PATH
 * twice and the rest) */
#define RADL_SET_PATH (544 * 2 + 64)

/* Between two requests to dorequest.php, at least */
#define RADL_GAP_MS   600
/* After a 429: the server's window is a minute */
#define RADL_SLOW_MS  60000
/* The card's room a run stops at */
#define RADL_FLOOR    (64ULL * 1000 * 1000)

/* RetroAchievements' console ids for a systems.cfg tag (rcheevos's
 * rc_consoles.h), at most RADL_CONSOLES: a shelf that takes two kinds of
 * game asks for both (the NES's Famicom Disk System, the PC Engine's CDs).
 * 0 for a tag RetroAchievements has no console for. */
#define RADL_CONSOLES 3
int radl_consoles(const char *tag, int *out);

/* ---- before a run: what the card has ------------------------------------ */

typedef struct {
	char folder[CFG_STR];
	char tag[8];
	int  games;     /* games whose achievements can be asked for */
	int  ready;     /* with a set on the card: they work offline */
	int  none;      /* RetroAchievements has nothing for them (.none) */
	int  todo;      /* not asked yet: a download would ask */
} radl_sys;

/* One line a system with such games, in systems.cfg's order: a directory
 * read and an access() or two a game, for when a page opens - never a
 * frame. Returns how many were written to `out`. */
int radl_plan(const systems_cfg *sys, const char *roms_dir, radl_sys *out, int max);
/* One system's line (radl_plan's): false for one with no such games. Its
 * .cheevos folder is read once, not a look a game. */
bool radl_plan_one(const system_cfg *s, const char *roms_dir, radl_sys *out);

/* ---- the run --------------------------------------------------------------- */

/* Start. False when nobody is signed in, there is no network, or a run is
 * already going. `only` is a system's folder, or NULL for every one. */
bool radl_begin(const systems_cfg *sys, const char *roms_dir, const char *only,
                unsigned now_ms);
/* 1 still going, 0 finished (radl_status says how), -1 nothing running */
int  radl_step(unsigned now_ms);
/* Stop now: the request in flight dropped, the threads left to end alone */
void radl_cancel(void);
bool radl_running(void);

enum {
	RADL_WORKING,     /* asking, or reading the card */
	RADL_HASHING,     /* making the hashes of a system's new files */
	RADL_SLOWED,      /* RetroAchievements asked for a pause (429) */
	RADL_RETRYING,    /* no answer: asking again in a moment */
	RADL_NO_NETWORK,  /* the network went: waiting for it */
};
enum {
	RADL_OK,
	RADL_SIGN_IN,     /* the account was refused: sign in again */
	RADL_NO_ROOM,     /* the card is full */
	RADL_NO_ANSWER,   /* RetroAchievements stopped answering */
};

typedef struct {
	int  state;           /* RADL_WORKING... */
	int  problem;         /* why the run stopped, RADL_OK for none */
	int  systems, systems_done;
	int  games, games_done;
	int  hashing, hashed; /* this system's files to hash, and done */
	/* What the run did */
	int  fetched;         /* sets new on the card */
	int  updated;         /* sets replaced by RetroAchievements' newer one */
	int  ready;           /* games with a set: before the run, and new */
	int  none;            /* RetroAchievements has nothing */
	int  failed;          /* could not be asked */
	int  unlocks;         /* the account's unlocks the store took */
	int  badges;          /* badges that landed */
	int  requests;        /* to dorequest.php, for the log and the check */
	/* When a pause ends (RADL_SLOWED, RADL_RETRYING), in the caller's ms */
	unsigned resume_at;
	/* The game being asked about, and its system's folder */
	char now[256];
	char folder_now[CFG_STR];
	/* The set that arrived last, for a page that shows them coming in:
	 * `set_seq` counts arrivals */
	unsigned set_seq;
	char set_path[RADL_SET_PATH];
	char set_title[128];
	int  set_count;
} radl_progress;

void radl_status(radl_progress *out);

/* ---- parts, for the check -------------------------------------------------- */

/* A set file's header and achievements: game id, how many, their ids and
 * badge names (at most `max` of each; NULL for none wanted), and the ROM's
 * hash from the header into `hash` (33 bytes, "" for none). False when it is
 * not a set. */
bool radl_read_set(const char *path, int *game, int *count, int *ids,
                   char (*badges)[24], int max, char *hash);

/* Every set's loose badges on the card into its pack (src/rapack.h), once
 * a start, on a thread of the caller's: a folder whose badges/ has no loose
 * PNG costs one readdir. How many it packed. */
int radl_badges_migrate(const char *roms);

/* What a set on the card wants, given what allprogress said (`published`, -1
 * when it did not) and the count it had when last checked (`checked`, -1
 * never): true to fetch it again. A count that differs from the last check's
 * is RetroAchievements' set changing; with no last check, the set's own
 * count is compared, once. */
bool radl_set_stale(int set_count, int published, int checked);

#endif
