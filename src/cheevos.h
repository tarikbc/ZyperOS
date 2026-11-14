/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CHEEVOS_H
#define ZYPEROS_CHEEVOS_H

#include <stdbool.h>
#include <stddef.h>

/* RetroAchievements, the launcher's half.
 *
 * The division: Diatom evaluates conditions against every frame, because they
 * compare against the PREVIOUS frame and this process only sees the socket
 * every 100ms. This side owns everything else - which set belongs to which
 * game, what an achievement is called, and what the player has already earned.
 *
 * A set lives at `Roms/<System>/.cheevos/<name>.set`, beside the box art in
 * `.media/`. The launcher fetches it on a game's first play with a network
 * (src/rafetch.h), or `tools/ra-sets.py` puts it there from a computer. It
 * stays on the card, so every later play works from it with or without a
 * network: the Pixel 2 has Wi-Fi only with a USB adapter, and most play is
 * offline. What is earned offline waits in the store below and is sent when a
 * network is back (src/raqueue.h).
 *
 * One file, two readers. Diatom reads the bare `<id>\t<condition>` lines and
 * skips every `#` line; this reads the `#` lines for the menu. A second file
 * would eventually disagree with the first about which achievements exist.
 */

#define CHV_MAX     256    /* the largest measured is 203 - Mickey's Speedway USA */
#define CHV_TITLE    96
/* 242 is the longest of the 3542 achievements on the card, measured
 * 2026-09-10: Gain Ground on Genesis, "This Little Light of Mine", a paragraph
 * of conditions. At 192 it was cut at parse time, mid-sentence and with no
 * ellipsis, losing instructions rather than flavor - the reader was never told
 * that "No Exit allowed. Special Mode okay." was part of the requirement. */
#define CHV_DESC    256

typedef struct {
	int  id;
	int  points;
	bool earned;       /* before this game started */
	bool earned_now;   /* during it, so the menu can say what just happened */
	char title[CHV_TITLE];
	char desc[CHV_DESC];
	/* The badge's name on media.retroachievements.org, "" in a set written
	 * before badges were kept (format 2, 2026-11-05) - see src/rabadge.h */
	char badge[24];
	/* The count while it moves, "74/100", from Diatom this session; "" for
	 * none. Shown in the list where the points are. */
	char progress[24];
} cheevo;

/* Roms/<folder>/.cheevos/<name>.set */
void chv_path(const char *roms_root, const char *folder, const char *name,
              char *out, size_t n);

/* What the card knows about a game's set, for its Achievements rows.
 *
 * A set is fetched the first time a game is played with a network, so a game
 * only ever played offline has none yet - and "none" there was a lie by
 * omission: the game may well have achievements, nobody has asked. The
 * marker beside where the set would be, `<name>.none`, is RetroAchievements'
 * answer that there is nothing for this ROM (it does not know the ROM, or
 * the set is empty, or the file could not be read); a later fetch that
 * finds a set takes the marker away. A game the launcher has no hash for at
 * all, by its name (src/rahash.h, ra_hash_support), is none of these: its
 * rows say so before any of this is asked. */
typedef enum {
	CHV_SET,        /* a set on the card */
	CHV_NONE,       /* asked, and there is none - or nobody signed in to ask */
	CHV_UNCHECKED,  /* never played with a network: not asked yet */
} chv_state;

chv_state chv_state_of(const char *set_path, bool signed_in);
/* `<name>.none` for `<name>.set` */
void chv_none_path(const char *set_path, char *out, size_t n);
/* Write the marker (RetroAchievements has nothing) or take it away (a set
 * arrived), making the shelf's .cheevos folder when it has none yet. Quiet on
 * failure: it only words a row. */
void chv_none_mark(const char *set_path, bool none);

/* Load the set for a game. False means there is no set, which is the ordinary
 * case for most of a library and is not an error. */
bool chv_load(const char *set_path);
void chv_clear(void);
/* The set file the loaded set came from, "" for none */
const char *chv_source(void);
/* A count moving, from Diatom: kept on the achievement for the list. False
 * for an id not in the set. */
bool chv_note_progress(int id, const char *text);

int         chv_count(void);
const cheevo *chv_at(int i);
int         chv_game(void);        /* RetroAchievements game id */
int         chv_console(void);     /* RetroAchievements console id, for RUN */
const char *chv_game_title(void);
/* The ROM's RetroAchievements hash, from the set's header, "" in a set
 * written before it was kept (2026-11-12). An unlock carries it, so one
 * earned offline is sent with the hash of the ROM it was earned on. */
const char *chv_game_hash(void);
int         chv_earned(void);      /* including this session */
int         chv_points_earned(void);
int         chv_points_total(void);
int         chv_new_this_session(void);

/* Write what Diatom should watch: the set minus what is already earned.
 * Diatom has no account and no idea what earned means, so filtering is this
 * side's job. Streams the source file rather than holding it - a single
 * condition string has been measured at 30,897 characters.
 * False means nothing is left to watch, and the caller should not send it. */
bool chv_write_active(const char *path);

/* An unlock arrived from Diatom. True if it was news, which is what decides
 * whether the store needs writing. Now, softcore, with a network. */
bool chv_note_unlock(int id);
/* The same, said in full: when (Unix time), in which mode, and whether there
 * was a network to send it on. ZyperOS plays softcore only, so `hardcore` is
 * false from the launcher today; it is kept so that the day a hardcore mode
 * exists, an unlock is sent in the mode it was earned in. `offline` is only
 * for the notice that says waiting unlocks went out (chv_mark_synced). */
bool chv_note_unlock_at(int id, long when, bool hardcore, bool offline);

/* The earned store: one flat file for the whole library, keyed by game id and
 * achievement id, the same way favorites are keyed by tag and file.
 *
 * Each row carries whether RetroAchievements has it yet. The store is the
 * DURABLE record and the account is a peer to reconcile with, not the other
 * way round: a device that is offline for a week still knows what was earned,
 * and sends it when it can. That is what RA's own requirements call offline
 * queueing, and it is the right shape regardless of them.
 *
 *     key   chv.<game id>.<achievement id>
 *     value s                                 on the account
 *           p\t<when>\t<h>\t<hash>           owed: Unix time, 0 or 1, the ROM's
 *                                             md5 or ""
 *
 * A pending row with no fields after the `p` is from before they were kept
 * (2026-11-12): its time is unknown, so it is sent with none and the account
 * dates it the moment it arrives. A value that is not `s` is pending, which is
 * true of every row written before the store said so at all. */
void chv_earned_load(void);
/* Writes the rows that changed since the last save, not all of them: this
 * runs on every unlock, mid-game, and the store holds the whole library. */
bool chv_earned_save(void);

/* Record something earned. `synced` is true when it came FROM the account, so
 * it is already there and needs no sending. Returns true if this was new. */
bool chv_note_earned(int game, int id, bool synced);

/* What still needs sending. Iterating is by index into the store, so a caller
 * can walk them and mark each as it succeeds. */
int  chv_pending_count(void);
/* Owed for one game, for its list's heading */
int  chv_pending_for(int game);
bool chv_pending_at(int n, int *game, int *id);

/* One owed unlock, whole, for the sending */
#define CHV_HASH 33
typedef struct {
	int  game, id;
	long when;              /* Unix time it was earned, 0 unknown */
	bool hardcore;
	char hash[CHV_HASH];    /* the ROM's, "" unknown */
} chv_owed;
bool chv_pending_get(int n, chv_owed *out);

/* Sent, or settled. True when the row had waited - earned offline, owed when
 * the launcher started, or left owed by a pass that failed - which is what the
 * "sent" notice counts: an unlock sent the moment its game ends needs no news,
 * the player saw it unlock a minute ago. */
bool chv_mark_synced(int game, int id);
/* Every owed row has now waited: a pass ended without sending them */
void chv_hold_pending(void);
/* Whether this achievement of the loaded game is earned and still owed */
bool chv_is_pending(int id);

#endif
