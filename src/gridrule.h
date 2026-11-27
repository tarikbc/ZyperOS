/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GRIDRULE_H
#define ZYPEROS_GRIDRULE_H

/* The grid's rule (MENU > View > Grid, DESIGN.md 11.47), with nothing of a
 * renderer's or the launcher's in it, so tools/gridrule-check.c holds it to
 * a table: which shape a shelf takes from its art, where its cells go, and
 * which art is a cut-out. src/gamegrid.h draws by it. */
#include <SDL.h>
#include <stdbool.h>

#define GG_ROWS 2

/* ---- the rule, pure (tools/gamegrid-check.c) ---------------------------- */

/* The groups a cover's shape (width over height) falls in */
#define GG_PORTRAIT_UNDER  .83f
#define GG_SQUARE_TO      1.20f
/* How far a cell may be from the shelf's shape, and a cover from its cell's
 * before it is drawn whole instead of filling it: 12%, so less than 6% of a
 * cover is ever cut from a side */
#define GG_NEAR           1.12f
/* The shape with nothing to go by: the generated card's portrait */
#define GG_SHAPE_NONE      .70f
/* A shelf of cut-outs alone (a wheel set): landscape cards */
#define GG_SHAPE_CUTOUTS  1.40f

/* The shelf's shape from the shapes of its covers (`art`, `n` of them; a
 * folder, a generated card and a cut-out are not among them) and how many
 * cut-outs it has. The group with at least half of the covers gives it, its
 * middle cover's (the median); with no such group, square. Kept between 1:2
 * and 2:1. */
float gg_shape(const float *art, int n, int cutouts);

/* Where the cells go, in the room (`left`, `top`, `w`, `h`), for a shelf of
 * `shape`: two rows, the cells as wide as the room less the gaps allows and
 * no taller than two rows allow; the fewest columns that show at least
 * `least` covers with cells within GG_NEAR of the shape. The grid in the
 * middle of its room. */
typedef struct { int x, y, cols, rows, cw, ch, gap; } gg_geom;
gg_geom gg_layout(int left, int top, int w, int h, float shape, int gap, int least);

/* L1 (dir < 0) and R1 (dir > 0) on a list or a grid of `n`, every list
 * page's one rule (DESIGN.md 11.3): a screenful (`page`) on, the same
 * column on a grid, or to the end where less than a screenful is left; from
 * the end itself, a press (`fresh`) goes round to the other end and a held
 * one stays (the caller's step sound plays edge once), as a held direction
 * stops at a list's end. So a list that fits on one screen gets first and
 * last, never a jump of the page modulo its length. The shelf's list and
 * grid, the home's Grid and a menu list that pages. Pure. */
int gg_page(int cursor, int n, int page, int dir, bool fresh);

/* The rows whose covers the grid keeps decoded: its window (GG_ROWS rows
 * from `top`) and GG_AHEAD rows either side, counted round the ends, since
 * Left from the first cover, Right from the last and L1/R1 at the ends come
 * round; at an end, one row more round it, which the glide into the other
 * end's page shows a sliver of. So whatever one press brings into sight - a
 * row, a page, the other end - is in hand before it slides in. Pure. */
#define GG_AHEAD GG_ROWS
bool gg_row_kept(int row, int top, int nrows);

/* The rows outside the window to ask for ahead, at most `max`, the nearest
 * kept when there are more: farthest first, since the loader decodes the
 * newest ask first, and the row under the window asked last of its
 * distance. How many. Pure. */
int gg_rows_ahead(int top, int nrows, int *rows, int max);

/* The key gg_note and gg_art_of take: a file name's hash */
unsigned gg_key(const char *file);

/* Whether a picture is a cut-out: a quarter of its edge clear, or a sixth
 * of all of it. Pure, on an ARGB8888 surface. */
bool gg_cutout(const SDL_Surface *s);

/* ---- the home's Grid (DESIGN.md 11.1) ------------------------------------ */

/* The home's Grid sizes itself to how many systems it shows (2026-11-19, Man
 * in Boots on the Discord: "a little too much white space" on a card of few
 * systems; my picks from the home grid lab): the biggest still of four steps
 * whose rows hold them all, so fewer systems get bigger tiles, and a card of
 * more than two rows of today's tiles keeps today's grid. A cell is its still
 * and the system's name under it, GH_NAME_GAP apart, in the type the still's
 * size gives it. */
#define GH_STEPS      4
#define GH_STILL_FULL 104   /* today's still, the full grid's */
#define GH_CELL_MIN   128   /* the full grid's cell at the least: as many across as fit */
#define GH_CELL_SIDE  25    /* a step's cell: its still and 12 either side (129 for 104) */
#define GH_NAME_GAP   8     /* the still to its name (related) */
#define GH_WIN_FULL   3     /* the full grid's rows in sight; it scrolls past them */

/* The name's type under a still: chrome (24, in a 28 box, today's), support
 * (28, DS_LINE_SUPPORT 36) from a 176 still, meta (37, DS_LINE_META 40) from
 * a 256 one: the name grows with its still */
enum { GH_NAME_CHROME, GH_NAME_SUPPORT, GH_NAME_META };

typedef struct {
	int n;          /* systems */
	int cols;       /* across */
	int rows;       /* rows, all of them */
	int win;        /* rows in sight: the grid scrolls when rows > win */
	int still;      /* a still's side */
	int cw, ch;     /* a cell: its width, and its still and its name down */
	int name, box;  /* the name's type (GH_NAME_*) and its line box */
	int x, y;       /* a full row's top left, the window at its first row */
	int gap;
} gh_geom;

/* Where the home's `n` systems go in the room (`left`, `top`, `w`, `h`),
 * cells `gap` apart. The steps, the first whose rows hold them all: a still
 * of 256 in one row, 176 in two, 128 in two, 104 in two; as many across as
 * cells of the still and GH_CELL_SIDE fit, the columns as few as the rows
 * allow (5 is 3 + 2, 7 is 4 + 3). More than that: today's grid, cells of at
 * least GH_CELL_MIN filling the width, GH_WIN_FULL rows in sight. On the
 * 1024-wide canvas: 1 to 3 at 256, 4 to 8 at 176, 9 and 10 at 128, 11 and 12
 * at 104 in two rows, 13 and more today's. The grid in the middle of its room
 * (the full grid's window of three rows). Pure. */
gh_geom gh_layout(int n, int left, int top, int w, int h, int gap);

/* Cell `i`'s top left with the window at the first row: a short last row in
 * the middle under the full ones. Pure. */
void gh_cell(const gh_geom *g, int i, int *x, int *y);

/* Up (`dir` -1) and Down (+1) from `cursor`: the system in the row over or
 * under whose middle is nearest `anchor` (the x where the column started:
 * Down then Up comes back), the first of two as near; -1 from the top row or
 * the last. Pure. */
int gh_row_step(const gh_geom *g, int cursor, int dir, int anchor);

#endif
