/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MEDIAPAGE_H
#define ZYPEROS_MEDIAPAGE_H

/* A game's Cover and Video page, as it is drawn (DESIGN.md 11.34): the
 * cover on the shelf (Now) at the left, what the sources offer (Found) as a
 * strip at its right, the focused one named under it with its source and
 * A's hint; a rule; then the game's video, its picture and its rows. Or
 * the big look at one picture beside Now (Look Closer). What the tiles are
 * and what a press does is src/main.c's (media_screen); what the sources
 * offer is src/mediafetch.h's. Everything here draws from a page the caller
 * fills, so a shot draws exactly what the screen does. The top bar is the
 * caller's (draw_topbar); the game's name stands in its row. */
#include <SDL.h>
#include <stdbool.h>

#define MPG_TILES 16
#define MPG_ROWS  3

/* Sizes, in the launcher's 1024x768 units: DESIGN.md 11.34 */
#define MPG_BOX_W   264        /* Now's box, and the focused tile's */
#define MPG_BOX_H   232
#define MPG_FLOOR   384        /* what every picture stands on */
#define MPG_STRIP_X 352        /* the strip, from */
#define MPG_GAP     24

typedef struct {
	SDL_Texture *tex;          /* NULL: not here yet */
	int          w, h;         /* its shape */
	const char  *chip;         /* "Now", "Before", or NULL */
	float        load;         /* a bar under it while it comes, 0..1; < 0 none */
	bool         failed;       /* it did not come: an empty tile */
} mpg_tile;

typedef struct {
	const char *name, *value;
	bool        live;
} mpg_row;

typedef struct {
	/* The bar's row: the game's name, in the room topbar_half leaves */
	const char  *title;
	int          title_half;
	unsigned     title_phase;

	const char  *now_head;
	SDL_Texture *now_tex;      /* the cover, or the generated card */
	int          now_w, now_h;
	const char  *now_credit;   /* its source, "No cover yet", or NULL */

	const char  *found_head;
	char         found_right[96];   /* "1 / 9", "Asking ScreenScraper" */
	const char  *found_right_short; /* ...its source's name, where that meets the heading */
	bool         found_dots;        /* ...with three dots filling in turn */
	int          dots_step;
	mpg_tile     tile[MPG_TILES];
	int          ntiles, focus;
	bool         strip_focus;       /* the cursor is on the strip */
	const char  *cap1, *cap2;       /* the focused tile's kind and its source */
	const char  *cap2_short;        /* the source alone, where its artist would meet A's hint */
	const char  *hint;              /* A's word, while the strip has the cursor */

	/* Where the strip would be, a message: its first line, then lines in
	 * meta (one string wrapped, or each a line), with Zyper at the right */
	const char  *msg;
	const char  *msg_wrap;
	const char  *msg_lines[3];
	int          nmsg_lines;

	const char  *video_head;
	SDL_Texture *vid_tex;      /* the video's picture, or NULL */
	int          vid_w, vid_h;
	SDL_Rect     vid_src;      /* its part of the texture (snapv_texture); none: all */
	bool         vid_disc;     /* the play mark: still, the cursor elsewhere */
	const char  *vid_status;   /* on the empty tile: "No video", "Looking it up" */
	const char  *vid_note;     /* where the rows would be, with none: why */
	float        vid_load;     /* a bar while it downloads; < 0 none */
	mpg_row      rows[MPG_ROWS];
	int          nrows, row;   /* row: the cursor's, -1 on the strip */

	/* Look Closer */
	bool         big;
	SDL_Texture *big_tex;
	int          big_w, big_h;
	float        big_load;
	const char  *back, *use;

	void (*zyper)(SDL_Renderer *r, float cx, float cy, float h, float tilt, bool shadow);
} mpg_page;

/* The page, over the ground the caller has cleared and its top bar */
void mpg_draw(SDL_Renderer *r, const mpg_page *p, unsigned now);

/* Where the strip puts tile `i` when `focus` has the cursor, for the window
 * of small copies to ask for: false when it is off the screen */
bool mpg_tile_shown(const mpg_page *p, int i);

#endif
