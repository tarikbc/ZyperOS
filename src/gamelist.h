/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMELIST_H
#define ZYPEROS_GAMELIST_H

/* A shelf as a list (MENU > View > List, DESIGN.md 11.39): its games and
 * folders as rows of names down the left, each with a small mark - a little
 * card in the system's color with the name's first letter, or the pixel
 * folder with its count - and the focused one's cover and facts on the
 * right. The cover row's alternative for a shelf whose games are known by
 * name more than by cover (Arcade, Ports, a long library).
 *
 * Up and Down step, Left and Right jump a letter, L1 and R1 a page: the row
 * turned, as the cover row's d-pad turns with it (main.c, update_games). A
 * press comes round past its ends; a held Up or Down stops at the end with
 * the edge sound once, as every list's does (DESIGN.md 11.3, shelf_move).
 *
 * No I/O: the caller hands it its facts, as games_draw's are handed. */
#include "coverflow.h"
#include "keyhints.h"
#include "listbox.h"         /* GL_ROWS */
#include "snaprest.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Where the list stands and where it is going, as rows: the first row in
 * sight and the focus plate, each easing from where it was to where the
 * cursor has put it (ms.move, out); and the focused entry's details over
 * its media, down at rest while its snap plays (src/snaprest.h), which the
 * caller touches at a press. The caller keeps one a shelf. */
typedef struct {
	bool     set;
	int      cursor, count;
	float    top_from, top_to, plate_from, plate_to;
	uint32_t moved;
	snaprest details;
} gl_motion;

typedef struct {
	const char *heading;            /* the system, and the folder it is in */
	int count, cursor;
	unsigned accent;                /* the system's color, for the marks */
	/* Row i's own system's color, on a shelf of many systems' games
	 * (Favorites); NULL for `accent` on every row */
	unsigned (*accent_of)(void *ctx, int i);
	/* Row i's name, and its games when it is a folder (0 for a game) */
	const char *(*title_of)(void *ctx, int i);
	int (*folder_of)(void *ctx, int i);
	/* Whether row i is a favorite: a heart at its end. NULL on Favorites. */
	bool (*fav_of)(void *ctx, int i);
	/* The pixel folder (ui_make_folder_icon), FA_ICON_W x FA_ICON_H; NULL
	 * draws none */
	SDL_Texture *folder_icon;
	/* The focused game's video snap (src/snapshelf.h), asked the frame the
	 * focus comes, as the Video view's is: its picture, its shape and the
	 * part of its texture the picture is in, how far the cover has turned
	 * into it (0 to 1), and whether it has one at all, for the play mark on
	 * its cover */
	SDL_Texture *snap;
	int snap_w, snap_h;
	SDL_Rect snap_src;
	float snap_k;
	bool has_snap;
	/* A shot's details, F of the way down (--band F); 0 for their own */
	float details;
	/* The focused entry's facts, as the cover row has them; `needs` its
	 * firmware the card lacks, in **warning** */
	const char *played, *origin, *needs;
	bool has_achievements, resumable, install, inside;
	const char *cheats;   /* "2 cheats on" (src/cheats_screen.h), NULL or "" for none */
	bool cheats_crashed;  /* ...and it stopped last time with them on: a line of its own */
	bool howto, opens;     /* Splore's entry (src/games.h) */
	int earned, total;
	/* A shelf of another kind's own words (MP4's albums, DESIGN.md 11.67):
	 * A's word for the focused entry, and the foot's hints whole; NULL and
	 * 0 for a games shelf's */
	const char *a_word;
	const key_hint *foot;
	int nfoot;
	/* Entry i's art badge (src/artbadge.h): what it is, on a shelf of
	 * kinds (MP4's All); NULL, or AB_NONE for i, for none */
	int (*badge_of)(void *ctx, int i);
} gl_info;

/* Reset to the cursor, with no motion: a shelf opened, a folder changed */
void gl_reset(gl_motion *m, int cursor, int count);

/* One frame. `texture` gives the focused entry's card, as the cover row's
 * does. What it returns is when to draw again, as games_draw's. */
uint32_t gl_draw(gl_motion *m, SDL_Renderer *r, const gl_info *info,
                 cf_tex_fn texture, void *ctx, uint32_t now, uint32_t phase);

/* Whether it is moving at `now`, for when the focused entry's facts are
 * read (they wait for it to settle, as the cover row's do) */
bool gl_moving(const gl_motion *m, uint32_t now);

/* The first row in sight for `cursor` of `count`, from `top`: the cursor
 * kept a row from the window's edge where the list allows it */
int gl_window(int cursor, int count, int top);

#endif
