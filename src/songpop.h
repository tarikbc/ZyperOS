/* SPDX-License-Identifier: GPL-3.0-only */
/* Zyper's pop-up with the menu music's new song, on the home and the games
 * shelf (DESIGN.md 11.22; 2026-11-06: "whenever the bg song changes,
 * can we have zyper popup and say the new song name?"): its words and its
 * life, the drawing being main.c's.
 *
 * The notice's look (11.7) and the bar's motion (11.6): in over **quick** on
 * **out**, sliding down SONGPOP_RISE as it fades up; held **hold-long** from
 * the last change, longer when the name scrolls; out over **fade-out**. A new
 * song while it is up changes the words where it is - back to full at once
 * if it was fading - rather than queueing behind it: an R2 every half second
 * would otherwise wait two and a half seconds a press. That is why this is
 * not src/noticeq.h's queue, which is for Diatom's one overlay over a game.
 *
 * Pure: no SDL, the caller's milliseconds, so tools/bgm-check.c holds it. */
#ifndef ZYPEROS_SONGPOP_H
#define ZYPEROS_SONGPOP_H

#include <stdbool.h>

#include "bgm_tags.h"

#define SONGPOP_RISE 14

typedef struct {
	char     head[BGM_TAG_MAX];   /* where it is from: the game, the artist */
	char     body[BGM_TAG_MAX];   /* the song */
	unsigned shown_at;            /* when it began to come in */
	unsigned changed_at;          /* when the words last changed */
	unsigned hold_ms;             /* how long it stays after that */
	unsigned serial;              /* moves with the words, for the caller's
	                               * Zyper and the name's scroll */
	bool     on;
} songpop;

/* The words, now: up from nothing, or changed in place on one already up */
void songpop_show(songpop *p, const char *head, const char *body,
                  unsigned hold_ms, unsigned now);
/* Gone at once: a menu, a game, Muse, the music off */
void songpop_hide(songpop *p);
/* Where it is at `now`: its opacity, 0 to 1, and how far above its place it
 * still is, in units. False when it is not up, and from then it is off. */
bool songpop_at(songpop *p, unsigned now, float *alpha, float *rise);
/* How long from `now` before it next changes, for the redraw clock: 0 while
 * it moves, the rest of the hold while it stands. Meaningless when off. */
unsigned songpop_wait(const songpop *p, unsigned now);

/* MP4's words, for the same pop-up when L2 or R2 on the shelf moves MP4
 * rather than the menu music (MP4 playing behind the shelf holds the menu
 * music; DESIGN.md 11.22). `title`, `artist` and `album` are what MP4 says
 * of the file playing (src/musec.h's mu_now); `chapter` is the chapter
 * playing as Now Playing names it, or NULL or "" in a file without them.
 *
 *   a chaptered book: the book (its title, else album, else artist) over
 *                     the chapter
 *   a book in files:  the book (album, else artist) over the file's title
 *   music or a video: the artist, else the album, over the title
 *
 * and BGM_SONG_HEAD_NONE above for nothing to say there, or for a head that
 * would only repeat the body. */
void songpop_mp4_words(const char *title, const char *artist, const char *album,
                       const char *chapter, bool book,
                       char *head, size_t hn, char *body, size_t bn);

#endif
