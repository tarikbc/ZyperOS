/* SPDX-License-Identifier: GPL-3.0-only */
/* A page: a screen drawn on the ground rather than a panel over it, with
 * Zyper beside its heading - Flash Drive's pages, File Transfer's page, the
 * Scraper (DESIGN.md 11.18, 11.19, 11.35). One frame, from src/ds/layout.h:
 *
 *     Zyper (DS_PAGE_ZYPER_*)   the heading at head_x, its middle DS_PAGE_HEAD_Y
 *     x0 .......... what the page holds, from body_y .......... x1
 *     (DS_PAGE_NEWS_Y, DS_PAGE_NEWS2_Y: lines of words under two tiles)
 *     line_y: marks and their words over the keys
 *     foot_y: the button hints
 *
 * The caller draws the ground and Zyper (src/main.c's page_top, which hands
 * back the frame) and its own content in the frame's places; these draw the
 * parts every page has. Part of the design system (src/ds/, DESIGN.md "Where
 * each part lives"). */
#ifndef ZYPEROS_DS_PAGE_H
#define ZYPEROS_DS_PAGE_H

#include <SDL.h>
#include <stdbool.h>

#include "ds/glyph.h"
#include "ds/layout.h"
#include "menu.h"   /* menu_row, ROW_IS_NOTE */

typedef struct {
	int x0, x1;     /* the sides: the inset from each edge */
	int head_x;     /* the heading's left: beside Zyper, or x0 with him off */
	int body_y;     /* where what the page holds starts */
	int line_y;     /* the line over the keys */
	int foot_y;     /* the keys */
} ds_page_frame;

/* The frame, with Zyper beside the heading or not (the look's Zyper off) */
ds_page_frame ds_page_frame_of(bool zyper);

typedef enum {
	/* One heading, on two lines balanced where it is wider than the room,
	 * both **title** **text**, DS_PAGE_HEAD_STEP over and under the middle
	 * (Flash Drive) */
	DS_HEAD_BALANCED,
	/* Two lines split at "\n", the first **text**, the second **text-soft**,
	 * each **title**, **label** where it does not fit, sliding past that
	 * (File Transfer: the address and the PIN) */
	DS_HEAD_TWO,
	/* The heading in **title**, and where `sub` is given a smaller line under
	 * it in **meta** **text-soft** with a mark before it; each cut to the
	 * page's side (the Scraper) */
	DS_HEAD_SUB,
} ds_head_style;

/* The heading. `sub` and `mark` (a ui_glyph, UI_GLYPH_COUNT for none) are
 * DS_HEAD_SUB's; `now` (ms) is the clock a sliding line keeps. */
void ds_page_head(SDL_Renderer *r, const ds_page_frame *f, ds_head_style style, const char *title,
                  const char *sub, ui_glyph mark, unsigned now);

/* A mark and its words over the keys: a switch (ds_switch) on or off, a plain
 * mark in **accent-ink** beside words in **text-soft**, or both in **warning** */
typedef enum { DS_TONE_ON, DS_TONE_OFF, DS_TONE_INFO, DS_TONE_WARN } ds_tone;
typedef struct {
	ui_glyph g;
	const char *text;
	ds_tone tone;
} ds_line_item;

/* Up to four of them centred on line_y, DS_PAGE_LINE_GAP apart, closer where
 * they are wider than the page */
void ds_page_line(SDL_Renderer *r, const ds_page_frame *f, const ds_line_item *it, int n);

/* Rows as one line of **meta** across the page centred on `cy`: a note in
 * **text-soft**, or label and value pairs, the label **text-dim** and the
 * value **text-soft**, a dot between pairs; sliding where too long */
void ds_page_meta(SDL_Renderer *r, const ds_page_frame *f, const menu_row *rows, int n, int cy, unsigned now);

/* `n` button hints (key and word) centred at the foot, `gap` apart, `tight`
 * apart where they are wider than the page */
void ds_page_foot(SDL_Renderer *r, const ds_page_frame *f, const char *const *keys,
                  const char *const *words, int n, int gap, int tight);

#endif
