/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MEDIAFETCH_H
#define ZYPEROS_MEDIAFETCH_H

#include <stdbool.h>

#include "artsrc.h"

/* What the sources offer for one game, for its Cover and Video page
 * (DESIGN.md 11.34), fetched without blocking a frame: one mf_step a frame,
 * each starting a request or polling the one in flight, as art_step does.
 *
 * THE ORDER, my call on 2026-11-09: libretro first, because it needs no
 * account and answers at once, so the page has pictures in a second; then
 * ScreenScraper, whose lookup can take most of a minute; then SteamGridDB.
 * Every source is asked once a page; each one's answers are appended in its
 * own order (art_of's regions for ScreenScraper), so a tile never moves.
 *
 * ONE REQUEST AT A TIME: net.c has one async slot. Between the sources'
 * requests, and after them, what the page asks for comes first: the whole
 * picture for Look Closer or Use as Cover, then a video, then the small
 * copies of the tiles on screen (mf_want). Nothing is aborted to make room;
 * the request in flight finishes first.
 *
 * SCREENSCRAPER'S QUOTA: the lookup is one request, and each picture of
 * theirs is one more, so only the tiles the page shows are fetched small
 * (mf_want), and a whole picture only when it is looked at. libretro's
 * pictures need no account and come whole with the source's answer.
 *
 * Every file lands in MF_DIR, in RAM on the handheld, and goes with
 * mf_end; Use as Cover copies one onto the card (mf_use). */

#ifndef MF_DIR
#define MF_DIR "/tmp/zyperos-media"
#endif

#define MF_MAX       14        /* the most tiles one game is offered */
#define MF_THUMB_PX  256       /* ScreenScraper's small copy, at most */
#define MF_FULL_PX   512       /* and the whole, at the shelf's largest (artshrink.c) */
#define MF_SG_MAX    3         /* SteamGridDB's covers taken */
#define MF_URL       640
#define MF_PATH      600

/* A source's state, for the Found heading's "Asking ..." and the lines
 * under "Nothing found." */
typedef enum {
	MFQ_WAIT,       /* not asked yet */
	MFQ_ASKING,
	MFQ_DONE,       /* answered with pictures */
	MFQ_NONE,       /* answered with none: no match */
	MFQ_UNKNOWN,    /* ScreenScraper: not a game they know */
	MFQ_OFF,        /* no account, no key, no collection for this console */
	MFQ_NO_ANSWER,  /* no answer, or refused */
} mf_qstate;

typedef enum { MFT_NONE, MFT_COMING, MFT_HAVE, MFT_FAILED } mf_tstate;

typedef struct {
	art_source as;              /* where it is from, what it is */
	char       thumb_url[MF_URL], full_url[MF_URL];   /* credentials: never logged */
	char       thumb[MF_PATH], full[MF_PATH];         /* the files, once they are here */
	mf_tstate  thumb_st, full_st;
} mf_cand;

typedef struct {
	mf_cand   c[MF_MAX];
	int       n;
	mf_qstate q[AS_SOURCES];
	bool      offline;          /* begun with no network: nothing is asked */
	/* ScreenScraper's video snap for the game, once its lookup is in */
	bool      video_offer;
	long      video_size;       /* bytes, 0 when their reply did not say */
	/* A video coming: its bytes so far; done 1 when it arrived, -1 not */
	bool      video_coming;
	long      video_got;
	int       video_done;
} mf_state;

/* The game: its console's folder, its file as the shelf names it, its name
 * as its cover is named, the shelf's extensions, and where its folder is.
 * `net` false begins it offline, asking nothing. */
void mf_begin(const char *folder, const char *file, const char *stem, const char *exts,
              const char *roms, bool net);
/* One unit of work. True when something changed that the page shows. */
bool mf_step(void);
const mf_state *mf_get(void);
/* The source being asked, or AS_NONE */
as_source mf_asking(void);

/* The tiles on screen, by index, lo to hi: their small copies are wanted */
void mf_want(int lo, int hi);
/* The whole of candidate `i`, for Look Closer and Use as Cover: true once it
 * is here (c[i].full), and asked for otherwise */
bool mf_want_full(int i);
/* Download the video snap to <roms>/<folder>/.media/<stem>.mp4, over the one
 * there is once it has arrived whole and starts as an MP4 does */
bool mf_get_video(void);

/* Candidate `i` becomes the game's cover: the one there was goes to
 * .media/.before/<stem>.png, and the records with them (artsrc_swap). The
 * shelf's art_shrink is the caller's, as the page knows when. False with
 * nothing changed when it could not be copied. */
bool mf_use(int i, char *dest, int destn);
/* Before's cover and the shelf's change places, records and all: Put Back */
bool mf_put_back(void);

void mf_end(void);

/* ---- pure, for tools/media-check.c ---------------------------------------- */

/* An MP4's length in whole seconds, from its moov's mvhd; -1 when it is not
 * one or does not say */
int mf_mp4_seconds(const char *path);
/* <roms>/<folder>/.media/<stem><suffix>, and in .media/.before; false if cut */
bool mf_media_path(const char *roms, const char *folder, const char *stem,
                   const char *suffix, char *out, int n);
bool mf_before_path(const char *roms, const char *folder, const char *stem,
                    char *out, int n);
/* Use as Cover and Put Back on the card, which mf_use and mf_put_back are
 * for the page's game: `from` copied in as <stem>.png, the cover there was
 * to .before, the records with them (src/mediafile.c) */
bool mf_file_use(const char *roms, const char *folder, const char *stem, const char *from,
                 const art_source *as, char *dest, int destn);
bool mf_file_put_back(const char *roms, const char *folder, const char *stem);

#endif
