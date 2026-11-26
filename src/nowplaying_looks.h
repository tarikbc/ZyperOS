/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_NOWPLAYING_LOOKS_H
#define ZYPEROS_NOWPLAYING_LOOKS_H

/* MP4's Player Looks drawn (DESIGN.md 11.64): Now Playing as the look MENU >
 * Player Look has on, for music. The looks and their motion are
 * src/mp4look.h's; Cover, the default, is src/nowplaying_screen.c's np_draw
 * as it always was. */
#include "app.h"
#include "keyhints.h"
#include "mp4look.h"
#include "mp4c.h"

#include <stdbool.h>

/* The look that is on (the setting mp4.look, Cover until it is changed),
 * and setting it */
mp4l_id np_look(void);
void    np_look_set(mp4l_id id);
/* A shot's look, never saved (--np-look); MP4L_COUNT for none */
void    np_look_shot(mp4l_id id);

/* The Cassette look's tape style (the setting mp4.tape, Smoke until it is
 * changed), and setting it; a shot's, never saved (--np-tape) */
mp4t_id np_tape(void);
void    np_tape_set(mp4t_id id);
void    np_tape_shot(mp4t_id id);
/* A style's name, and what it is for MENU's list, in the language on */
const char *tape_style_name(mp4t_id id);
const char *tape_style_note(mp4t_id id);
/* Up or Down stepped the style on Now Playing: its name said a moment */
void    npl_tape_said(void);
/* The daemon's answer to mp4c_length_ask: the album in `dir`, its tracks'
 * lengths summed (s), for the tape's number */
void    npl_album_length(const char *dir, double s);

/* The look for what plays: the one that is on for music, Cover for a book
 * and a video, which have their own screens */
mp4l_id np_look_for(void);

/* Now Playing in `look` (any but Cover). Returns how long until the
 * picture would change by itself, in ms: 0 while it moves, which is always
 * while music plays */
unsigned npl_draw(app *a, mp4l_id look, const mu_now *mn);

/* Under the lock nothing moves (DESIGN.md 11.64): the bars rest and the
 * daemon stops measuring, until it is let go */
void npl_still(bool still);

/* MENU > Player Look's picture of `look` in the box (x, y, w, h): Now
 * Playing in small, playing - the song that plays, or the shelf's album's
 * first played in show, with bars to match */
void npl_preview(app *a, mp4l_id look, int x, int y, int w, int h);

/* MENU > Tape Style's picture of `style`: the Cassette look's, as
 * npl_preview draws it, in that style */
void npl_preview_tape(app *a, mp4t_id style, int x, int y, int w, int h);

/* For --np-bench N: N frames of the look that is on with a song's bars
 * moving, each frame's drawing timed; the mean, the 95th percentile and the
 * worst said on stderr */
void npl_bench(app *a, const mu_now *mn, int frames);

/* For --shot: bars and caps as a song has them, held still */
void npl_shot_bars(void);
/* The song a shot or the list's show plays, with no queue to say it: its
 * album (a library index) and its place in it; -1 none */
void npl_song(int album, int place);

/* Now Playing's buttons (src/nowplaying_screen.c), the looks' foot too */
int np_hint_list(key_hint *h, bool playing, int kind, int count, bool video);

#endif
