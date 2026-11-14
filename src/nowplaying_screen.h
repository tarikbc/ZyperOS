/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_NOWPLAYING_SCREEN_H
#define ZYPEROS_NOWPLAYING_SCREEN_H

/* MP4's Now Playing (Muse, DESIGN.md 11.21): the cover and the words, the
 * lock screen, and the video view. The screens are src/nowplaying_screen.c;
 * the player is src/musec.c, the lock's rule src/mp4lock.c. */
#include "app.h"
#include "mp4lock.h"
#include "musec.h"
#include "platform.h"

#include <stdbool.h>
#include <stddef.h>

/* How far one step of left or right seeks, for the press `b` held in `in` */
double seek_step(const in_state *in, in_button b);
/* `sec` as minutes and seconds */
void mmss(char *out, size_t n, double sec);
/* A chapter as Now Playing names it */
void chapter_label(int i, char *out, size_t n);
/* Now Playing drawn; when it next wants a frame */
unsigned np_draw(app *a, const mu_now *mn, const char *next);

/* MENU's Lock Screen and Shuffle All, asked from MP4's menu; whether a lock
 * can happen here */
void muse_ask_lock(void);
void muse_ask_shuffle(void);
bool lock_allowed(void);
/* An MP4 screen's presses: read, SELECT alone let go, a screen coming up,
 * and the volume, brightness, X and SELECT with START keys (true: lock) */
void muse_input(app *a);
bool muse_select(void);
void muse_input_reset(void);
bool muse_keys(app *a);
/* The lock screen: its panel, and the screen until it is let go */
void lock_panel(app *a, const mp4lock *l);
void muse_locked(app *a);
/* MP4's menu, from Now Playing and the shelf */
muse_exit muse_menu(app *a);

/* The video view: the whole picture, or with its controls */
enum { NPV_FULL, NPV_CONTROLS };
extern int g_np_view;                    /* -1 until read from the settings */
void np_draw_video(app *a, const mu_now *mn, float k);
/* Now Playing, opened from the shelf or a list */
muse_exit muse_now_screen(app *a);

#endif
