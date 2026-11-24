/* SPDX-License-Identifier: GPL-3.0-only */
/* The grid's rule (src/gridrule.h, DESIGN.md 11.47), held to a table.
 *
 *     make check-gridrule
 *
 * 1. The shape a shelf takes from its art: every common shape alone, a shelf
 *    mostly of one shape, an even mix (square), cut-outs alone, no art, and
 *    the clamp at 1:2 and 2:1.
 * 2. Where the cells go on the 1024 x 768 canvas for each, as DESIGN.md's
 *    table gives them, and on a wider canvas: inside the room, the cells
 *    filling its width, at least six covers, every cell within 12% of the
 *    shape, the columns the fewest that are.
 * 3. Which art is a cut-out: a logo on nothing, a box in 3D on nothing, and
 *    neither a cover nor a cover with a clear corner. */
#include "gridrule.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failed;

#define NEED(cond, ...) do { if (!(cond)) { failed++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void shape(const char *what, const float *art, int n, int cutouts, float want)
{
	float got = gg_shape(art, n, cutouts);

	NEED(fabsf(got - want) < .005f, "shape of %s: %.3f, want %.3f", what, got, want);
}

/* The room on a canvas `sw` wide: as src/gamegrid.c's gg_geom_for has it */
#define LEFT 64
#define TOP  96
#define FOOT 568
static void cells(const char *what, int sw, float s, int cols, int cw, int ch)
{
	gg_geom g = gg_layout(LEFT, TOP, sw - 2 * LEFT, FOOT - TOP, s, 24, 6);
	int gw = g.cols * g.cw + (g.cols - 1) * g.gap, gh = g.rows * g.ch + (g.rows - 1) * g.gap;
	float a = (float)g.cw / g.ch;

	if (cols) NEED(g.cols == cols && g.cw == cw && g.ch == ch, "%s: %d across, %d x %d; want %d, %d x %d",
	               what, g.cols, g.cw, g.ch, cols, cw, ch);
	NEED(g.rows == 2, "%s: %d rows", what, g.rows);
	NEED(g.cols * g.rows >= 6, "%s: %d covers", what, g.cols * g.rows);
	NEED(g.x >= LEFT && g.x + gw <= sw - LEFT, "%s: the grid leaves its room across", what);
	NEED(g.y >= TOP && g.y + gh <= FOOT, "%s: the grid leaves its room down (%d to %d)", what, g.y, g.y + gh);
	NEED(sw - 2 * LEFT - gw < g.cols, "%s: the cells do not fill the width (%d of %d)", what, gw, sw - 2 * LEFT);
	NEED(a <= s * GG_NEAR + .01f && a >= s / GG_NEAR - .01f, "%s: a cell of %.3f for a shelf of %.3f", what, a, s);
	/* the fewest columns: one fewer is not within 12%, or shows too few */
	if (g.cols > 3) {
		float cw1 = (float)(sw - 2 * LEFT - (g.cols - 2) * 24) / (g.cols - 1);
		float ch1 = fminf(cw1 / s, (FOOT - TOP - 24) / 2.0f);

		NEED(cw1 / ch1 > s * GG_NEAR, "%s: %d columns would do", what, g.cols - 1);
	}
}

static SDL_Surface *surface(int w, int h)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);

	if (s) SDL_FillRect(s, NULL, 0xff336699u);
	return s;
}

static void clear(SDL_Surface *s, int x, int y, int w, int h)
{
	SDL_FillRect(s, &(SDL_Rect){ x, y, w, h }, 0x00000000u);
}

int main(void)
{
	static const float p23[] = { .667f, .667f, .667f, .667f, .667f, .667f };
	static const float boxes[] = { .70f, .71f, .72f, .70f, .72f };
	static const float psp[] = { .58f, .57f, .58f, .58f, .58f };
	static const float sq[] = { 1.0f, 1.0f, .98f, 1.0f, 1.02f };
	static const float ds[] = { 1.11f, 1.11f, 1.10f };
	static const float snes[] = { 1.40f, 1.39f, 1.40f, 1.41f };
	static const float wide[] = { 1.778f, 1.778f, 1.778f };
	static const float mostly[] = { .667f, .667f, .667f, .667f, .667f, .667f, .667f, 1.43f, 1.43f };
	static const float even[] = { .667f, .667f, .70f, .58f, 1.0f, 1.0f, 1.0f, 1.11f, 1.40f, 1.40f, 1.37f, 1.778f };
	static const float half[] = { .667f, .667f, 1.0f, 1.40f };
	static const float hero[] = { 3.1f, 3.1f, 3.1f };
	static const float thin[] = { .3f, .3f };
	SDL_Surface *s;

	/* 1. the shape */
	shape("2:3 grids", p23, 6, 0, .667f);
	shape("portrait boxes", boxes, 5, 0, .71f);
	shape("PSP cases", psp, 5, 0, .58f);
	shape("square art", sq, 5, 0, 1.0f);
	shape("DS boxes", ds, 3, 0, 1.11f);
	shape("SNES boxes", snes, 4, 0, 1.40f);
	shape("16:9 art", wide, 3, 0, 1.778f);
	shape("mostly 2:3, two screenshots", mostly, 9, 1, .667f);
	shape("an even mix", even, 12, 1, 1.0f);
	shape("a group with half the art", half, 4, 0, .667f);
	shape("cut-outs alone", NULL, 0, 8, GG_SHAPE_CUTOUTS);
	shape("no art", NULL, 0, 0, GG_SHAPE_NONE);
	shape("3:1 heroes (kept to 2:1)", hero, 3, 0, 2.0f);
	shape("thin art (kept to 1:2)", thin, 2, 0, .5f);

	/* 2. the cells, DESIGN.md 11.47's table */
	cells("2:3", 1024, .667f, 5, 160, 224);
	cells("portrait boxes", 1024, .71f, 5, 160, 224);
	cells("PSP cases", 1024, .58f, 6, 129, 222);
	cells("square", 1024, 1.0f, 4, 206, 206);
	cells("DS boxes", 1024, 1.11f, 4, 206, 185);
	cells("SNES boxes", 1024, 1.40f, 3, 282, 201);
	cells("4:3 screenshots", 1024, 1.333f, 3, 282, 212);
	cells("16:9 art", 1024, 16.0f / 9, 3, 282, 159);
	cells("2:1 art", 1024, 2.0f, 3, 282, 141);
	cells("thin art", 1024, .5f, 0, 0, 0);
	/* a wider canvas (a 16:9 screen in the 768-high design space) takes
	 * more columns, by the same rule */
	for (float a = .5f; a <= 2.0f; a += .05f) {
		char what[48];

		snprintf(what, sizeof what, "1365 wide, shape %.2f", a);
		cells(what, 1365, a, 0, 0, 0);
		snprintf(what, sizeof what, "1024 wide, shape %.2f", a);
		cells(what, 1024, a, 0, 0, 0);
	}

	/* 3. cut-outs */
	s = surface(400, 600);
	NEED(s && !gg_cutout(s), "a cover is no cut-out");
	clear(s, 0, 0, 40, 40);
	NEED(!gg_cutout(s), "a cover with a clear corner is no cut-out");
	SDL_FreeSurface(s);
	s = surface(800, 300);
	clear(s, 0, 0, 800, 300);
	SDL_FillRect(s, &(SDL_Rect){ 60, 80, 680, 140 }, 0xffcc3322u);
	NEED(gg_cutout(s), "a logo on nothing is a cut-out");
	SDL_FreeSurface(s);
	s = surface(500, 500);
	clear(s, 0, 0, 500, 500);
	SDL_FillRect(s, &(SDL_Rect){ 40, 20, 420, 470 }, 0xff226644u);
	NEED(gg_cutout(s), "a box in 3D on nothing is a cut-out");
	SDL_FreeSurface(s);
	s = surface(900, 300);
	clear(s, 0, 0, 900, 300);
	for (int x = 0; x < 900; x += 60) SDL_FillRect(s, &(SDL_Rect){ x, 0, 40, 300 }, 0xffffffffu);
	NEED(gg_cutout(s), "a logo that reaches its edges, clear between letters, is a cut-out");
	SDL_FreeSurface(s);
	NEED(!gg_cutout(NULL), "no picture is no cut-out");

	/* the key is a file name's, and tells two apart */
	NEED(gg_key("Metroid (USA).nes") == gg_key("Metroid (USA).nes"), "a key is the same twice");
	NEED(gg_key("Metroid (USA).nes") != gg_key("Metroid (Europe).nes"), "two names, two keys");

	/* L1 and R1: a screenful in the same column, else to the end, and
	 * from the end round to the other one */
	NEED(gg_page(0, 23, 10, +1, true) == 10 && gg_page(10, 23, 10, +1, true) == 20, "R1 a screenful, the same column");
	NEED(gg_page(15, 23, 10, +1, true) == 22, "R1 with less than a screenful left goes to the last");
	NEED(gg_page(22, 23, 10, +1, true) == 0, "R1 on the last comes round to the first");
	NEED(gg_page(20, 23, 10, -1, true) == 10 && gg_page(5, 23, 10, -1, true) == 0, "L1 a screenful, else to the first");
	NEED(gg_page(0, 23, 10, -1, true) == 22, "L1 on the first comes round to the last");
	NEED(gg_page(0, 12, 18, +1, true) == 11 && gg_page(11, 12, 18, +1, true) == 0, "a grid on one screen: R1 last, then first");
	NEED(gg_page(4, 12, 18, -1, true) == 0 && gg_page(0, 12, 18, -1, true) == 11, "a grid on one screen: L1 first, then last");
	NEED(gg_page(0, 1, 10, +1, true) == 0 && gg_page(0, 0, 10, +1, true) == 0, "one cover, or none, stays put");
	/* held (DESIGN.md 11.3): on to the end as a press, and stopping there */
	NEED(gg_page(15, 23, 10, +1, false) == 22 && gg_page(5, 23, 10, -1, false) == 0,
	     "a held R1 or L1 does not go on to the end");
	NEED(gg_page(22, 23, 10, +1, false) == 22, "a held R1 on the last goes round, not stopping there");
	NEED(gg_page(0, 23, 10, -1, false) == 0, "a held L1 on the first goes round, not stopping there");

	/* The rows kept and asked ahead: the window and two rows either side,
	 * round the ends, so a row, a page or a step round an end lands on
	 * covers decoded already (a shelf of 13 rows, its window from row 5) */
	NEED(gg_row_kept(3, 5, 13) && gg_row_kept(8, 5, 13), "two rows either side are kept");
	NEED(!gg_row_kept(2, 5, 13) && !gg_row_kept(9, 5, 13), "a third row away is not");
	NEED(gg_row_kept(10, 0, 13) && gg_row_kept(11, 0, 13) && gg_row_kept(12, 0, 13) && !gg_row_kept(9, 0, 13),
	     "at the top, the last three rows: L1 and Left come round, gliding in past the third");
	NEED(gg_row_kept(0, 11, 13) && gg_row_kept(1, 11, 13) && gg_row_kept(2, 11, 13) && !gg_row_kept(3, 11, 13),
	     "at the foot, the first three rows: R1 and Right come round");
	NEED(!gg_row_kept(10, 1, 13) && !gg_row_kept(2, 10, 13), "a window off the ends reaches two round them");
	NEED(gg_row_kept(4, 0, 5) && gg_row_kept(2, 0, 5), "a short grid keeps every row");
	{
		int rows[8], n;

		n = gg_rows_ahead(5, 13, rows, 8);
		NEED(n == 4 && rows[0] == 3 && rows[1] == 8 && rows[2] == 4 && rows[3] == 7,
		     "ahead: the farthest first, the row under the window asked last (decoded first)");
		n = gg_rows_ahead(0, 13, rows, 8);
		NEED(n == 5 && rows[0] == 10 && rows[1] == 11 && rows[2] == 3 && rows[3] == 12 && rows[4] == 2,
		     "ahead at the top: three round the end for the rows before it");
		n = gg_rows_ahead(0, 3, rows, 8);
		NEED(n == 1 && rows[0] == 2, "ahead on three rows: the one outside the window, once");
		n = gg_rows_ahead(0, 2, rows, 8);
		NEED(n == 0, "ahead on two rows: nothing outside the window");
		n = gg_rows_ahead(5, 13, rows, 2);
		NEED(n == 2 && rows[0] == 4 && rows[1] == 7, "ahead, capped: the nearest rows kept");
	}

	/* 4. The home's Grid sizes itself (DESIGN.md 11.1's table) */
	{
		static const struct { int n, still, cols, rows, win, name; } want[] = {
			{ 1, 256, 1, 1, 1, GH_NAME_META }, { 2, 256, 2, 1, 1, GH_NAME_META },
			{ 3, 256, 3, 1, 1, GH_NAME_META }, { 4, 176, 4, 1, 1, GH_NAME_SUPPORT },
			{ 5, 176, 3, 2, 2, GH_NAME_SUPPORT }, { 6, 176, 3, 2, 2, GH_NAME_SUPPORT },
			{ 7, 176, 4, 2, 2, GH_NAME_SUPPORT }, { 8, 176, 4, 2, 2, GH_NAME_SUPPORT },
			{ 9, 128, 5, 2, 2, GH_NAME_CHROME }, { 10, 128, 5, 2, 2, GH_NAME_CHROME },
			{ 11, 104, 6, 2, 2, GH_NAME_CHROME }, { 12, 104, 6, 2, 2, GH_NAME_CHROME },
			{ 13, 104, 6, 3, 3, GH_NAME_CHROME }, { 20, 104, 6, 4, 3, GH_NAME_CHROME },
			{ 48, 104, 6, 8, 3, GH_NAME_CHROME },
		};
		gh_geom g;
		int x, y, prev = 1 << 30;

		for (unsigned i = 0; i < sizeof want / sizeof *want; i++) {
			g = gh_layout(want[i].n, LEFT, TOP, 1024 - 2 * LEFT, FOOT - TOP, 24);
			NEED(g.still == want[i].still && g.cols == want[i].cols && g.rows == want[i].rows &&
			     g.win == want[i].win && g.name == want[i].name,
			     "home of %d: still %d, %d x %d (%d in sight), name %d; want %d, %d x %d (%d), %d",
			     want[i].n, g.still, g.cols, g.rows, g.win, g.name, want[i].still, want[i].cols,
			     want[i].rows, want[i].win, want[i].name);
		}
		/* a card of more than twelve keeps 1.0.6's grid: 129 x 140 cells
		 * from (65, 98), three rows in sight */
		for (int n = 13; n <= 48; n++) {
			g = gh_layout(n, LEFT, TOP, 1024 - 2 * LEFT, FOOT - TOP, 24);
			NEED(g.cw == 129 && g.ch == 140 && g.x == 65 && g.y == 98 && g.win == 3,
			     "home of %d: %d x %d cells from (%d, %d); want 1.0.6's 129 x 140 from (65, 98)",
			     n, g.cw, g.ch, g.x, g.y);
		}
		/* every grid in its room, on this canvas and a wider one; fewer
		 * systems never get a smaller still */
		for (int sw = 1024; sw <= 1365; sw += 341)
			for (int n = 1; n <= 48; n++) {
				int rows;

				g = gh_layout(n, LEFT, TOP, sw - 2 * LEFT, FOOT - TOP, 24);
				rows = g.rows < g.win ? g.rows : g.win;
				NEED(g.x >= LEFT && g.x + g.cols * g.cw + (g.cols - 1) * 24 <= sw - LEFT,
				     "home of %d on %d: the grid leaves its room across", n, sw);
				NEED(g.y >= TOP && g.y + rows * g.ch + (rows - 1) * 24 <= FOOT,
				     "home of %d on %d: the grid leaves its room down", n, sw);
				NEED(g.cols * g.rows >= n && g.cols * (g.rows - 1) < n, "home of %d on %d: %d x %d", n, sw, g.cols, g.rows);
				NEED(g.ch == g.still + GH_NAME_GAP + g.box, "home of %d: a cell is its still and its name", n);
				if (sw == 1024) NEED(g.still <= prev, "home of %d: a bigger still than one fewer has", n);
				if (sw == 1024) prev = g.still;
			}
		/* a short last row in the middle under the full ones */
		g = gh_layout(5, LEFT, TOP, 1024 - 2 * LEFT, FOOT - TOP, 24);
		gh_cell(&g, 3, &x, &y);
		NEED(x == g.x + (g.cw + 24) / 2 && y == g.y + g.ch + 24, "home of 5: the second row starts half a cell in");
		/* Up and Down: the nearest under or over where the column started */
		{
			int a0, a1, a2, yy;

			gh_cell(&g, 0, &a0, &yy); a0 += g.cw / 2;
			gh_cell(&g, 1, &a1, &yy); a1 += g.cw / 2;
			gh_cell(&g, 2, &a2, &yy); a2 += g.cw / 2;
			NEED(gh_row_step(&g, 0, 1, a0) == 3, "5: Down from the first lands on the first under it");
			NEED(gh_row_step(&g, 1, 1, a1) == 3, "5: Down from the middle lands on the first of two as near");
			NEED(gh_row_step(&g, 3, -1, a1) == 1, "5: Up again comes back to the middle");
			NEED(gh_row_step(&g, 2, 1, a2) == 4, "5: Down from the last lands on the last under it");
			NEED(gh_row_step(&g, 1, -1, a1) == -1 && gh_row_step(&g, 4, 1, a2) == -1, "5: the top and the foot stop");
		}
		g = gh_layout(20, LEFT, TOP, 1024 - 2 * LEFT, FOOT - TOP, 24);
		gh_cell(&g, 17, &x, &y);
		NEED(gh_row_step(&g, 17, 1, x + g.cw / 2) == 19, "20: Down from the last column onto the short row's nearer, its last");
		gh_cell(&g, 12, &x, &y);
		NEED(gh_row_step(&g, 12, 1, x + g.cw / 2) == 18, "20: Down from the first column onto the short row's first");
		g = gh_layout(3, LEFT, TOP, 1024 - 2 * LEFT, FOOT - TOP, 24);
		NEED(gh_row_step(&g, 1, 1, 0) == -1 && gh_row_step(&g, 1, -1, 0) == -1, "one row: Up and Down stop");
	}

	if (failed) { printf("gridrule: %d failed\n", failed); return 1; }
	printf("gridrule: every shape, mix and cell as DESIGN.md 11.47 says, and the home's steps as 11.1 does\n");
	return 0;
}
