/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_STORAGE_H
#define ZYPEROS_STORAGE_H

/* MENU > Storage's numbers (DESIGN.md 11.32): how the card is used, part by
 * part, as iPhone Storage shows a phone. src/storage_page.c draws them; this
 * file measures them. No SDL, so tools/storage-check.c links it.
 *
 * ALLOCATED BYTES. A file is the clusters it holds (st_blocks), not its
 * length: on a 128 GB exFAT card a cluster is 128 KB, so eight thousand
 * covers of 40 KB take a gigabyte, and the parts add up to what the card
 * says is used. What nothing here measures - the filesystem's own tables,
 * a folder no system names - is Other: used less everything measured, so
 * the bar always comes to the whole card.
 *
 * THE CARD IS WALKED ON A THREAD, at the lowest CPU and I/O priority there
 * is, only while the page is open (st_refresh starts it, st_stop ends it),
 * and never while a game runs: the page is the only thing that starts it.
 *
 * A CACHE MAKES IT FAST. Every folder walked is kept with its mtime and its
 * own files' bytes (ZYPEROS_SHARED/storage.cache, written whole and renamed
 * into place), and each game's bytes with the folders they were counted
 * in. A refresh stats the folders and lists again only those whose mtime
 * moved (a folder listed within ST_RACY_S of a change is listed again, as
 * the change and the listing can share a timestamp), so the page opens at
 * once with the last numbers and they move to the new ones as the walk
 * reports. A change the folders' times do not show - files changed on a
 * computer that kept them - shows as Other moving; when it moves more
 * than ST_UNEXPLAINED, the walk lists everything again.
 *
 * A GAME'S BYTES ARE WHAT DELETE GAME GIVES BACK: src/gamedel.h's own plan
 * for it, summed, so the two can never disagree about which files are the
 * game's. gd_plan_make is not reentrant: nothing may call it while the walk
 * runs (st_stop first). */

#include <stdbool.h>
#include <stddef.h>

#define ST_MAX_SYS     64
#define ST_FOLDER      96
#define ST_RACY_S      2
#define ST_UNEXPLAINED (64ull * 1000 * 1000)
/* A recommendation is shown when it gives back at least this */
#define ST_REC_MIN     (10ull * 1000 * 1000)
/* Old save states: not written for this long, by a game not played for as
 * long or no longer on the card */
#define ST_OLD_S       (365L * 24 * 3600)

/* The bar's parts, in its order (DESIGN.md 11.32) */
typedef enum {
	STC_GAMES, STC_SAVES, STC_ART, STC_MEDIA, STC_ZYPEROS, STC_OTHER, STC_COUNT
} st_cat;

/* A system: its folders' bytes */
typedef struct {
	char folder[ST_FOLDER];          /* Roms/<folder> */
	unsigned long long games;        /* Roms/<folder>, all but .media */
	unsigned long long covers;       /* its .media, all but video */
	unsigned long long snaps;        /* its .media's video snaps */
	unsigned long long states;       /* its states folder */
	unsigned long long saves;        /* what its games have in Saves/ */
	unsigned long long unused_art;   /* .media of games not on the card */
	int unused_covers, unused_snaps;
	unsigned long long old_states;   /* ST_OLD_S's states */
	int old_state_games;
	int games_sized;                 /* its games with a size */
} st_sys;

typedef struct {
	unsigned long long total;        /* the card: the device, else the filesystem */
	unsigned long long free;         /* what can still be written */
	unsigned long long partitions;   /* the system's partitions, beside the filesystem */
	unsigned long long music, videos, books, bgm, bios, zyperos, emulators;
	unsigned long long saves, states;/* Saves/ and the states folder, whole */
	int nmusic, nvideos, nbooks;
	st_sys sys[ST_MAX_SYS];
	int nsys;
	unsigned long long cat[STC_COUNT];
	unsigned long long unused_art, old_states;
	int unused_covers, unused_snaps, old_state_games;
	bool measured;                   /* from a finished walk, now or cached */
	bool walking;                    /* a walk is under way */
	unsigned gen;                    /* moves with every change */
	/* The walk's progress, for the first one: the system it is in, and its
	 * games sized of how many */
	char at[ST_FOLDER];
	int at_done, at_total;
} st_card;

/* A game, as the walk is told of it (copied) */
typedef struct {
	const char *folder, *tag, *core, *exts, *file, *name;
	bool hooked;          /* its shelf's games are folders (ScummVM's) */
	long last_played;     /* unix time, 0 for never (stats_lookup) */
} st_game_in;

/* What a game's bytes are, by part */
typedef struct {
	unsigned long long bytes, games, saves, states, art;
} st_game_size;

/* Where the card's folders are */
typedef struct {
	const char *card;     /* the card's root: Music/, Videos/... */
	const char *roms;     /* Roms/ */
	const char *shared;   /* .userdata/shared: the states and the cache */
	const char *root;     /* ZyperOS/ */
	/* The card is a filesystem of its own (the handheld's), so Other is the
	 * card's and a jump in it means something changed unseen; a host's
	 * scratch folder shares a disk with everything else */
	bool own_fs;
} st_paths;

/* The systems the walk measures, by folder (systems.cfg's, MP4's none) */
typedef struct {
	const char *folders[ST_MAX_SYS];
	int n;
} st_systems;

/* Start a refresh on the thread: the cache read first if nothing is in hand,
 * then the walk. A walk under way is left to go on. Everything is copied. */
void st_refresh(const st_paths *p, const st_systems *s, const st_game_in *games, int n,
                long now);
/* Ask the walk to end and wait for it: what it finished is kept, and
 * written to the cache. Nothing when none runs. */
void st_stop(void);
/* The numbers as they are now (a copy) */
void st_snapshot(st_card *out);
/* A game's bytes, when the walk has them */
bool st_game(const char *folder, const char *file, st_game_size *out);
/* Forget everything in memory (a check's fresh start) */
void st_reset(void);

/* The same, on this thread, to the end: for the check and the host shots */
void st_measure_now(const st_paths *p, const st_systems *s, const st_game_in *games, int n,
                    long now, bool use_cache);

/* The recommendations' work, on the caller's thread with the walk stopped.
 * Each lists its folders again and holds each file to its rule again at the
 * moment it deletes it, so nothing the cache got wrong goes. The bytes
 * given back, and how many files (or games' states) went. */
unsigned long long st_delete_unused_art(const st_paths *p, const st_systems *s,
                                        const st_game_in *games, int n, int *files);
unsigned long long st_delete_old_states(const st_paths *p, const st_systems *s,
                                        const st_game_in *games, int n, long now, int *games_out);

/* The bar's parts and the recommendations' sums from the rest of :
 * Other is what is used less everything measured. For a shot's stand-in
 * card too. */
void st_totals(st_card *c);

/* The card's size as it is sold: 128 GB for a device of 127.9 GB, the next
 * size cards come in (powers of two to 512 GB, then 1, 1.5, 2 and 4 TB) when
 * the device is within a tenth under it, else itself */
unsigned long long st_nominal(unsigned long long total);

/* How many folders the last refresh listed: the check's proof that a
 * folder whose time did not move is not read again */
int st_listed(void);

/* A media suffix of a cover or a video snap, or of what a fetch stopped
 * halfway left (src/gamedel.c's list): its length in `name`, 0 for none.
 * `video` says which kind. */
size_t st_media_suffix(const char *name, bool *video);

#endif
