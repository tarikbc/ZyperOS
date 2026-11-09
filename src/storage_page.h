/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_STORAGE_PAGE_H
#define ZYPEROS_STORAGE_PAGE_H

/* MENU > Storage's page, as it is drawn (DESIGN.md 11.32): the heading, the
 * room used and free, one stacked bar and its legend, a list of rows with
 * the focused one's facts under it; or, the first time, Zyper over the
 * measuring. What the rows are and what A does is src/main.c's
 * (storage_screen); the numbers are src/storage.h's. Everything here draws
 * from a page the caller fills, so a shot draws exactly what the screen
 * does. */
#include <SDL.h>
#include <stdbool.h>

#define SP_PARTS   8
#define SP_VALUE   32
#define SP_CAPTION 256

typedef enum {
	SP_ICON_NONE,     /* a game: its name at the row's start */
	SP_ICON_TEX,      /* a system's sprite, 128 square */
	SP_ICON_SHEET,    /* a folder's two frames side by side; the first */
	SP_ICON_ZYPER,    /* Zyper's face: ZyperOS */
	SP_ICON_SPARK     /* a recommendation */
} sp_icon;

typedef struct {
	const char *name;
	char value[SP_VALUE];
	char caption[SP_CAPTION];   /* the line under the list while it is focused */
	sp_icon icon;
	SDL_Texture *tex;
	bool rule_after;            /* the recommendations end under it */
	int kind, index;            /* the caller's: what A does here */
} sp_row;

/* A part of the bar, and its legend's dot */
typedef struct {
	unsigned long long bytes;
	SDL_Color col;
	bool stripes;               /* not measured yet */
	const char *name;           /* NULL: in the bar, not the legend */
	char value[SP_VALUE];       /* the legend's size, or empty */
} sp_part;

typedef struct {
	const char *heading;
	char heading_right[SP_VALUE];   /* the card's size, meta, at the heading's right */
	const char *used_fmt;           /* "%s used": the number drawn as a title */
	char used[SP_VALUE];
	char right[48];                 /* "7.4 GB free", "116 games" */
	SDL_Texture *head_tex;          /* a system's sprite, left of the numbers */
	bool head_sheet;                /* ...a folder's two frames: the first */
	const char *sort_label;         /* the list's order, beside X under it */
	sp_part part[SP_PARTS];
	int nparts;
	unsigned long long whole;       /* the bar's whole: the card, or the system */
	sp_row *rows;
	int nrows, sel;
	/* The first measure: Zyper, the line under him and the system it is in */
	bool measuring;
	const char *measure_line;
	char measure_at[96];
	const char *note;               /* the foot line when there is no list */
	/* Zyper, drawn by the caller (src/zyperdraw.c, which the checks that
	 * link this file do not): about (cx, cy), `h` tall, his head at `tilt`,
	 * his shadow under him when `shadow` */
	void (*zyper)(SDL_Renderer *r, float cx, float cy, float h, float tilt, bool shadow);
} sp_page;

/* Where the list is: the focus fill and the window, gliding to the cursor */
typedef struct {
	int sel;
	float fill_from, fill_to, top_from, top_to;
	unsigned t0, sel_at;
	bool ready;
} sp_motion;

/* Rows that fit the window, for the caller's scrolling */
int sp_rows_fit(void);

/* The page, over the ground the caller has cleared */
void sp_draw(SDL_Renderer *r, const sp_page *p, sp_motion *m, unsigned now);

/* True while the fill or the window is still moving, for the redraw clock */
bool sp_moving(const sp_motion *m, unsigned now);

#endif
