/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "portload.h"

#include "blur.h"
#include "design.h"
#include "linebreak.h"
#include "theme.h"
#include "ui.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* What the panel shows while a port loads, which can be a minute (Stardew
 * Valley's Mono), saved for plastron's splash --still, which launch.sh starts
 * to hold it once this process has let go of the display, with Zyper's bounce
 * loop drawn over it at the corner res/loading/loading.txt names. Without it
 * the panel was black for that minute, and a player who took that for a hang
 * pressed POWER.
 *
 * DESIGN.md 11.11, my pick from the loading lab (2026-11-04: "2 with
 * backdrop"): option 2's layout on option 3's backdrop. The cover large on
 * the left with the focus lift; a column of words on the right - a band in
 * the system's color, the system's name, the game's name, and a line that
 * says the wait is a start and not a hang - centered on one height; Zyper
 * under the column; and behind it all the cover itself, blurred to fill the
 * screen under the theme's ground. In units. */
#define PORT_LOADING "/tmp/zyperos_loading.raw"

#define LOAD_COVER_AREA (368 * 512)  /* the cover's area, any shape (equal area) */
#define LOAD_COVER_CX   272          /* ...centered here */
#define LOAD_COVER_CY   376
#define LOAD_MARGIN     40           /* the cover from the screen's edges and the column */
#define LOAD_COL_X      536          /* the column of words, left aligned */
#define LOAD_COL_W      456
#define LOAD_MID        312          /* the block of words is centered on this */
#define LOAD_BAND_W     48           /* the system's band, DS_BAR high */
#define LOAD_GAP_BAND   20           /* the band to the system's capitals */
#define LOAD_GAP_SYS    28           /* the system's capitals to the name's */
#define LOAD_GAP_NAME   40           /* the name's last capitals to the line's */
#define LOAD_NAME_LINES 3
#define LOAD_NAME_STEP  64           /* one line of the name to the next */
#define LOAD_SAY_STEP   48           /* and of the line under it */
#define LOAD_SAY_A      "Starting."
#define LOAD_SAY_B      "This can take a minute."
/* Zyper's frame, where splash draws his loop: 120 x 116 panel pixels with its
 * top left at loading.txt's x 315 y 330, which is these units. Neither the
 * words nor the cover go in it: the block of words is at most 364 high (three
 * lines of the name, the line in two), so its capitals end at 494, and the
 * cover ends 40 short of the column, at 496. Only the backdrop is under him,
 * and the soft edge of the cover's lift where it reaches, a few levels at the
 * most. So splash must copy the still back under his frame before it draws
 * each one, and not fill it with the ground, which would show as a box on the
 * backdrop: that is plastron's, and comes with loading.txt's new corner. */
#define LOAD_ZYPER_X    504
#define LOAD_ZYPER_Y    528
#define LOAD_ZYPER_W    192
#define LOAD_ZYPER_H    186

/* The backdrop, option 3's: the cover a quarter larger than it takes to
 * cover the screen, blurred as the lab's CSS blur(40px) blurs it, under the
 * theme's **ground** from 70% at the top through 76% halfway to 90% at the
 * foot. Made on the CPU, small: the cover drawn into a target at BACK_SS
 * samples a texel, averaged to a texel a BACK_TEXEL units, blurred by
 * src/blur.c's three box passes (three of 5 texels, a Gaussian of 39 units)
 * with the scrim laid on, and drawn back over the screen by the texture's
 * linear filtering, which leaves nothing a blur this wide has in it. The
 * grid reads BACK_PAD texels past the screen on every side, as far as the
 * three boxes reach, so its edges are blurred from the cover there and not
 * from the nothing past the grid; the cover at a quarter more than fills the
 * screen always reaches that far. */
#define BACK_TEXEL 16
#define BACK_W     (ZYPEROS_SCREEN_W / BACK_TEXEL)   /* 64 x 48 over the screen */
#define BACK_H     (ZYPEROS_SCREEN_H / BACK_TEXEL)
#define BACK_RAD   2
#define BACK_PAD   (3 * BACK_RAD)
#define BACK_GW    (BACK_W + 2 * BACK_PAD)
#define BACK_GH    (BACK_H + 2 * BACK_PAD)
#define BACK_SS    4
#define BACK_FILL  1.25f                             /* the lab's scale over a cover's fill */

typedef struct {
	float c[3][BACK_GW * BACK_GH];   /* red, green, blue, 0-255, BACK_GW apart */
} back_grid;

/* The cover, `tw` x `th`, as the backdrop's blurred grid; NULL when it cannot
 * be read back, and the screen is then the ground with no backdrop. */
static back_grid *back_read(app *a, SDL_Texture *cover, int tw, int th)
{
	const int sw = BACK_GW * BACK_SS, sh = BACK_GH * BACK_SS;
	const ds_rgb g0 = theme()->ground;
	const float ar = (float)tw / (float)th, k = (float)BACK_SS / BACK_TEXEL;
	float fw = (float)ZYPEROS_SCREEN_W, fh, line[BACK_GW > BACK_GH ? BACK_GW : BACK_GH];
	back_grid *g = malloc(sizeof *g);
	Uint8 *px = malloc((size_t)sw * sh * 4);
	SDL_Texture *t = NULL;
	bool ok = false;
	int x, y, c;

	if (!g || !px || !cover || tw <= 0 || th <= 0) goto out;
	if ((float)ZYPEROS_SCREEN_H * ar > fw) fw = (float)ZYPEROS_SCREEN_H * ar;
	fw *= BACK_FILL;
	fh = fw / ar;
	t = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, sw, sh);
	if (!t || SDL_SetRenderTarget(a->r, t) != 0) goto out;
	SDL_SetRenderDrawColor(a->r, g0.r, g0.g, g0.b, 255);
	SDL_RenderClear(a->r);
	SDL_RenderCopyF(a->r, cover, NULL, &(SDL_FRect){
		((float)(BACK_PAD * BACK_TEXEL) + ((float)ZYPEROS_SCREEN_W - fw) / 2.0f) * k,
		((float)(BACK_PAD * BACK_TEXEL) + ((float)ZYPEROS_SCREEN_H - fh) / 2.0f) * k,
		fw * k, fh * k });
	ok = SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, px, sw * 4) == 0;
	if (!ok) goto out;

	for (y = 0; y < BACK_GH; y++)
		for (x = 0; x < BACK_GW; x++)
			for (c = 0; c < 3; c++) {
				unsigned sum = 0;
				int i, j;

				for (j = 0; j < BACK_SS; j++)
					for (i = 0; i < BACK_SS; i++)
						sum += px[((size_t)(y * BACK_SS + j) * sw + x * BACK_SS + i) * 4 + c];
				g->c[c][y * BACK_GW + x] = (float)sum / (BACK_SS * BACK_SS);
			}
	for (c = 0; c < 3; c++) blur_box3(g->c[c], BACK_GW, BACK_GH, BACK_RAD, line);
out:
	/* Back to the screen whichever way this ends, and before `t` goes: a
	 * target that failed to be set may still be the renderer's, and SDL
	 * destroying the target sets NULL, which on the Pixel 2 is the panel and
	 * not the screen texture everything is drawn to. */
	plat_draw_to_screen();
	if (t) SDL_DestroyTexture(t);
	free(px);
	if (!ok) {
		fprintf(stderr, "port: no backdrop: %s\n", cover ? SDL_GetError() : "no cover");
		free(g);
		return NULL;
	}
	return g;
}

/* The scrim's opacity `y` units down, 0-255: the gradient, and never under
 * `floor`. */
static float back_scrim(float y, float floor)
{
	const float h = (float)ZYPEROS_SCREEN_H / 2.0f;
	float a = y < h ? 0.70f + 0.06f * (y / h) : 0.76f + 0.14f * ((y - h) / h);

	a *= 255.0f;
	return a < floor ? floor : a;
}

/* The backdrop with its scrim at `floor` and up, into `tex` (BACK_W x BACK_H,
 * streaming), and drawn over the whole screen; the plain ground for none. */
static void back_draw(app *a, const back_grid *g, SDL_Texture *tex, float floor)
{
	const ds_rgb g0 = theme()->ground;
	const float gc[3] = { g0.r, g0.g, g0.b };
	Uint32 px[BACK_W * BACK_H];
	int x, y, c;

	if (!g || !tex) {
		SDL_SetRenderDrawColor(a->r, g0.r, g0.g, g0.b, 255);
		SDL_RenderClear(a->r);
		return;
	}
	for (y = 0; y < BACK_H; y++) {
		const float s = back_scrim(((float)y + 0.5f) * BACK_TEXEL, floor) / 255.0f;

		for (x = 0; x < BACK_W; x++) {
			Uint32 v = 255u << 24;   /* opaque */

			for (c = 0; c < 3; c++) {
				float p = g->c[c][(y + BACK_PAD) * BACK_GW + x + BACK_PAD];
				int q = (int)(p + (gc[c] - p) * s + 0.5f);

				q = q < 0 ? 0 : (q > 255 ? 255 : q);
				v |= (Uint32)q << (16 - 8 * c);
			}
			px[y * BACK_W + x] = v;
		}
	}
	SDL_UpdateTexture(tex, NULL, px, BACK_W * 4);
	SDL_RenderCopy(a->r, tex, NULL, NULL);
}

/* WCAG 2's relative luminance of a byte of sRGB, a table made once. */
static float srgb_lin(Uint8 v)
{
	static float lut[256];
	static bool made;

	if (!made) {
		for (int i = 0; i < 256; i++) {
			float c = (float)i / 255.0f;

			lut[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
		}
		made = true;
	}
	return lut[v];
}

static float luminance(Uint8 r, Uint8 g, Uint8 b)
{
	return 0.2126f * srgb_lin(r) + 0.7152f * srgb_lin(g) + 0.0722f * srgb_lin(b);
}

static float contrast(float la, float lb)
{
	return la > lb ? (la + 0.05f) / (lb + 0.05f) : (lb + 0.05f) / (la + 0.05f);
}

/* The port loading screen's words and where they go: the cover's rect, the
 * band, and each role's lines with the top of their capitals. */
enum { LOAD_SYS, LOAD_NAME, LOAD_SAY, LOAD_ROLES };

/* What is held to a contrast over the backdrop, every pixel under it: the
 * three roles of words, owed DESIGN.md 3.2's 4.5:1 as text, which the scrim
 * rises for, and after them the band, owed 3:1 as a mark, which it does not:
 * a mark 48 by 6 is no reason to take away the backdrop, and the band's
 * color moves instead (load_band_fit). */
#define LOAD_BAND LOAD_ROLES
#define LOAD_HELD (LOAD_ROLES + 1)
/* How far the band's color may move toward **text**, in twentieths of the
 * way from the mark to it: step 0 is the mark as it is and step
 * LOAD_BAND_STEPS is text itself. */
#define LOAD_BAND_STEPS 20
static const float LOAD_OWED[LOAD_HELD] = { 4.5f, 4.5f, 4.5f, 3.0f };
static const char *const LOAD_HELD_NAME[LOAD_HELD] = {
	"text-dim (the system)", "text (the name)", "text-soft (the line)", "the band"
};
typedef struct {
	SDL_Rect cover;
	SDL_Rect band;
	char line[LOAD_ROLES][LOAD_NAME_LINES][LB_LINE];
	int  lines[LOAD_ROLES];
	int  top[LOAD_ROLES];            /* the first line's capitals start here */
} load_layout;

static TTF_Font *load_font(int role)
{
	return ui_font(role == LOAD_NAME ? UI_F_TITLE : UI_F_META);
}

static int load_step(int role)
{
	return role == LOAD_NAME ? LOAD_NAME_STEP : LOAD_SAY_STEP;
}

static SDL_Color load_color(int role)
{
	return role == LOAD_SYS ? UI_TEXT_DIM : role == LOAD_NAME ? UI_TEXT : UI_TEXT_SOFT;
}

/* Line `i` of `role` as ui_text draws it: the rect its texture covers. */
static SDL_Rect load_line_rect(const load_layout *L, int role, int i)
{
	TTF_Font *f = load_font(role);
	int top = L->top[role] + i * load_step(role);

	return (SDL_Rect){ LOAD_COL_X, ui_cap_y(f, top + ui_cap_height(f) / 2),
	                   ui_text_width(f, L->line[role][i]), TTF_FontHeight(f) };
}

/* The layout for a game called `title` of system `s`, its cover `tw` x `th`.
 *
 * The cover takes the lab's area whatever its shape, as the shelf's
 * equal_area does, centered at (272, 376); one wider than about 1.07:1 is
 * held to 448 wide, 40 short of the column, so it never reaches the words
 * or Zyper's frame, and one taller than about 1:1.8 to 672 high, 40 inside
 * the screen. The block of words is centered on LOAD_MID by its capitals:
 * the band, the system's name (systems.cfg's, "Ports" for a port) in
 * **meta** **text-dim**, the game's name in **title** in up to three
 * balanced lines, and the line in **meta** **text-soft**, broken after
 * "Starting." when it does not fit the column. Nothing is cut that fits:
 * the name is cut only past three lines or in a word wider than the column
 * (ui_balance_lines), the rest only wider than the column (ui_fit_text). */
static void load_layout_make(load_layout *L, const system_cfg *s, const char *title,
                             int tw, int th)
{
	TTF_Font *meta = ui_font(UI_F_META), *big = ui_font(UI_F_TITLE);
	const float maxw = 2.0f * (LOAD_COL_X - LOAD_MARGIN - LOAD_COVER_CX);
	const float maxh = 2.0f * (LOAD_COVER_CY - LOAD_MARGIN);
	float ar = tw > 0 && th > 0 ? (float)tw / (float)th : 1.0f;
	float w = sqrtf(LOAD_COVER_AREA * ar), h = sqrtf(LOAD_COVER_AREA / ar);
	int cm = ui_cap_height(meta), ct = ui_cap_height(big), total, y;

	if (w > maxw) { h *= maxw / w; w = maxw; }
	if (h > maxh) { w *= maxh / h; h = maxh; }
	L->cover = (SDL_Rect){ (int)lroundf(LOAD_COVER_CX - w / 2.0f),
	                       (int)lroundf(LOAD_COVER_CY - h / 2.0f),
	                       (int)lroundf(w), (int)lroundf(h) };

	ui_fit_text(meta, s->name, L->line[LOAD_SYS][0], LB_LINE, LOAD_COL_W);
	L->lines[LOAD_SYS] = 1;
	L->lines[LOAD_NAME] = ui_balance_lines(big, title, LOAD_COL_W, LOAD_NAME_LINES,
	                                       L->line[LOAD_NAME]);
	if (L->lines[LOAD_NAME] < 1) {
		L->line[LOAD_NAME][0][0] = '\0';
		L->lines[LOAD_NAME] = 1;
	}
	if (ui_text_width(meta, LOAD_SAY_A " " LOAD_SAY_B) <= LOAD_COL_W) {
		snprintf(L->line[LOAD_SAY][0], LB_LINE, "%s", LOAD_SAY_A " " LOAD_SAY_B);
		L->lines[LOAD_SAY] = 1;
	} else {
		ui_fit_text(meta, LOAD_SAY_A, L->line[LOAD_SAY][0], LB_LINE, LOAD_COL_W);
		ui_fit_text(meta, LOAD_SAY_B, L->line[LOAD_SAY][1], LB_LINE, LOAD_COL_W);
		L->lines[LOAD_SAY] = 2;
	}

	total = DS_BAR + LOAD_GAP_BAND + cm + LOAD_GAP_SYS +
	        (L->lines[LOAD_NAME] - 1) * LOAD_NAME_STEP + ct + LOAD_GAP_NAME +
	        (L->lines[LOAD_SAY] - 1) * LOAD_SAY_STEP + cm;
	y = LOAD_MID - total / 2;
	L->band = (SDL_Rect){ LOAD_COL_X, y, LOAD_BAND_W, DS_BAR };
	y += DS_BAR + LOAD_GAP_BAND;
	L->top[LOAD_SYS] = y;
	y += cm + LOAD_GAP_SYS;
	L->top[LOAD_NAME] = y;
	y += (L->lines[LOAD_NAME] - 1) * LOAD_NAME_STEP + ct + LOAD_GAP_NAME;
	L->top[LOAD_SAY] = y;
}

/* The lowest contrast luminance `lt` has with any pixel of `q`, in `px`, the
 * pixels of `box`; no lower than `worst`. */
static float load_worst_in(const Uint8 *px, const SDL_Rect *box, const SDL_Rect *q,
                           float lt, float worst)
{
	int x, y;

	for (y = q->y; y < q->y + q->h; y++)
		for (x = q->x; x < q->x + q->w; x++) {
			const Uint8 *p;
			float k;

			if (y < box->y || y >= box->y + box->h || x < box->x || x >= box->x + box->w)
				continue;
			p = px + ((size_t)(y - box->y) * box->w + (x - box->x)) * 4;
			k = contrast(lt, luminance(p[0], p[1], p[2]));
			if (k < worst) worst = k;
		}
	return worst;
}

/* How far the words would read on what is drawn under them now: for each
 * role, the lowest contrast its color has with any pixel its lines will
 * cover, read back from the frame before they are drawn. Into `worst`, one a
 * role; false when the frame cannot be read. */
static bool load_read_under(app *a, const load_layout *L, float worst[LOAD_ROLES])
{
	const SDL_Rect first = load_line_rect(L, LOAD_SYS, 0);
	const SDL_Rect last = load_line_rect(L, LOAD_SAY, L->lines[LOAD_SAY] - 1);
	const SDL_Rect box = { LOAD_COL_X, first.y, LOAD_COL_W, last.y + last.h - first.y };
	Uint8 *px = malloc((size_t)box.w * box.h * 4);
	int k, i;

	if (!px || SDL_RenderReadPixels(a->r, &box, SDL_PIXELFORMAT_RGBA32, px, box.w * 4) != 0) {
		free(px);
		return false;
	}
	for (k = 0; k < LOAD_ROLES; k++) {
		const SDL_Color c = load_color(k);
		const float lt = luminance(c.r, c.g, c.b);

		worst[k] = 21.0f;
		for (i = 0; i < L->lines[k]; i++) {
			const SDL_Rect q = load_line_rect(L, k, i);

			worst[k] = load_worst_in(px, &box, &q, lt, worst[k]);
		}
	}
	free(px);
	return true;
}

/* The band's color over what is drawn under it now: the system's mark,
 * `mark`, moved toward **text** a step of LOAD_BAND_STEPS at a time until it
 * has a mark's 3:1 against every pixel under the band, read back from the
 * frame, and at the most all the way to **text**. Toward text in both themes,
 * since text is what reads on each one's ground: the dark of Beard, the light
 * of Salt & Pepper. Its contrast into `worst` and the steps it took into
 * `moved`; `mark` as it is, and a `worst` of 0, when the frame cannot be
 * read. A mark that reads already does not move. */
static unsigned load_band_fit(app *a, const load_layout *L, unsigned mark, float *worst,
                              int *moved)
{
	const unsigned text = ui_rgb(theme()->text);
	Uint8 px[LOAD_BAND_W * DS_BAR * 4];
	unsigned c = mark;
	int n;

	*worst = 0.0f;
	*moved = 0;
	if (L->band.w * L->band.h * 4 > (int)sizeof px ||
	    SDL_RenderReadPixels(a->r, &L->band, SDL_PIXELFORMAT_RGBA32, px, L->band.w * 4) != 0)
		return mark;
	for (n = 0; n <= LOAD_BAND_STEPS; n++) {
		c = ui_mix(mark, text, (float)n / LOAD_BAND_STEPS);
		*worst = load_worst_in(px, &L->band, &L->band,
		                       luminance((Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c), 21.0f);
		*moved = n;
		if (*worst >= LOAD_OWED[LOAD_BAND]) break;
	}
	return c;
}

/* The still, drawn and left on the screen: game `i` of the shelf under the
 * cursor, a port or a "run:" game of system `s`, lifted in that system's
 * color as its band is.
 *
 * The words keep 4.5:1 and the band 3:1 over any cover. The lab's scrim does
 * that for **text**, but at its 70% a white cover under Salt & Pepper's
 * ground leaves **text-dim** at 2.4:1 and a black one under Beard's at 2.5:1;
 * held to 4.5:1 there it needs 87% and 93%, which over most covers hides the
 * backdrop it is for. So the scrim keeps the lab's gradient and is raised
 * only as far as this cover's words need: the frame is drawn and read back
 * under each line before the words go on - the backdrop, its scrim, and the
 * cover's lift where it reaches them - and while any is short the scrim's
 * floor comes up a step and the frame is drawn again. Most covers need no
 * step; a white one under Salt & Pepper or a dark one under Beard about ten.
 * A frame that cannot be read back is drawn with the scrim whole, on the
 * plain ground. The band does not raise the scrim: where it is short its
 * color moves toward **text** until it reads (load_band_fit), which also
 * holds a system's color that is pale on the plain ground itself, as
 * Nintendo DS's silver is on Beard.
 *
 * True when it held: every one of them at what it is owed, read back and
 * not assumed, and neither the words nor the cover in Zyper's frame, which
 * the layout does not let happen (LOAD_ZYPER_X). Otherwise it says on stderr
 * which fell short and by how much, and --portload fails the shot. */
bool port_loading_draw(app *a, const system_cfg *s, int i)
{
	sysview *v = &a->view[a->sys_cursor];
	const char *title = v->list.items[i].title;
	int o = shelf_owner(a, a->sys_cursor, i), tw = 0, th = 0, role, k, steps = 0;
	int moved = 0;
	float cb = 1.0f, floor = 0.0f, top;
	float worst[LOAD_HELD] = { 21.0f, 21.0f, 21.0f, 0.0f };
	SDL_Texture *cover = game_get_tex(a, i, &tw, &th, &cb), *tex = NULL;
	const unsigned mark = ui_sys_mark(s->accent);
	const SDL_Rect zyper = { LOAD_ZYPER_X, LOAD_ZYPER_Y, LOAD_ZYPER_W, LOAD_ZYPER_H };
	const Uint64 t0 = SDL_GetPerformanceCounter();
	unsigned band = mark;
	bool own = false, read = false, clear, held;
	back_grid *g;
	load_layout L;
	SDL_Rect lifted, last;

	/* The shelf's card when it has one for this theme, and otherwise one
	 * read or made now, as the shelf would with no worker: the shelf may not
	 * have drawn it yet, or holds the card of the theme that was. */
	if (!cover || tex_stale(cover)) {
		char art[LIB_PATH * 3], prev[LIB_PATH * 3];

		box_art_path(a->sys.systems[o].folder, v->list.items[i].name, art, sizeof art);
		preview_path(a, o, &v->list.items[i], prev, sizeof prev);
		cover = load_image(a->r, art, &tw, &th, &cb);
		if (!cover) cover = load_image(a->r, prev, &tw, &th, &cb);
		if (!cover) {
			cover = ui_make_card(a->r, title, a->sys.systems[o].accent, &tw, &th);
			cb = 1.0f;
		}
		own = cover != NULL;
	}
	/* As it is: the shelf's texture may be drawn faded elsewhere */
	if (cover) {
		SDL_SetTextureBlendMode(cover, SDL_BLENDMODE_BLEND);
		SDL_SetTextureAlphaMod(cover, 255);
		SDL_SetTextureColorMod(cover, 255, 255, 255);
	}

	load_layout_make(&L, s, title, tw, th);
	last = load_line_rect(&L, LOAD_SAY, L.lines[LOAD_SAY] - 1);
	clear = !SDL_HasIntersection(&L.cover, &zyper) && last.y + last.h <= LOAD_ZYPER_Y;
	if (!clear) fprintf(stderr, "port: the loading screen reaches Zyper's frame\n");
	lifted = L.cover;
	lifted.h = (int)lroundf((float)L.cover.h * (cb > 0.0f ? cb : 1.0f));
	plat_draw_to_screen();
	g = back_read(a, cover, tw, th);
	if (g) {
		tex = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		                        BACK_W, BACK_H);
		if (tex) {
			SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
		}
	}

	for (;;) {
		bool short_of = false;

		back_draw(a, g, tex, floor);
		ui_lift(a->r, &lifted, s->accent, UI_LIFT_FOCUS);
		if (cover) SDL_RenderCopy(a->r, cover, NULL, &L.cover);
		read = load_read_under(a, &L, worst);
		for (k = 0; k < LOAD_ROLES; k++)
			if (!read || worst[k] < LOAD_OWED[k]) short_of = true;
		if (!short_of || !g || !tex || floor >= 255.0f) break;
		/* A step up from where the gradient is at the first line: under
		 * that, a floor is not seen where the words are */
		top = (float)load_line_rect(&L, LOAD_SYS, 0).y;
		if (floor < back_scrim(top, 0.0f)) floor = back_scrim(top, 0.0f);
		floor = !read || floor + 6.0f > 255.0f ? 255.0f : floor + 6.0f;
		steps++;
	}

	if (read) band = load_band_fit(a, &L, mark, &worst[LOAD_BAND], &moved);
	ui_round_rect(a->r, &L.band, DS_BAR / 2,
	              ui_col((ds_rgb){ (Uint8)(band >> 16), (Uint8)(band >> 8), (Uint8)band }, 255));
	for (role = 0; role < LOAD_ROLES; role++) {
		TTF_Font *f = load_font(role);

		for (k = 0; k < L.lines[role]; k++) {
			SDL_Rect q = load_line_rect(&L, role, k);

			ui_text(a->r, f, L.line[role][k], q.x, q.y, -1, load_color(role));
		}
	}
	held = read && clear;
	if (read) {
		fprintf(stderr, "port: loading screen in %.0f ms, the scrim from %d%% at the "
		        "top (%d steps up); at their least the words at %.2f:1 (text-dim), "
		        "%.2f:1 (text), %.2f:1 (text-soft), the band at %.2f:1 (%d/%d of the "
		        "way to text)\n",
		        (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
		        (double)SDL_GetPerformanceFrequency(),
		        (int)lroundf(back_scrim(0.0f, floor) * 100.0f / 255.0f), steps,
		        worst[LOAD_SYS], worst[LOAD_NAME], worst[LOAD_SAY], worst[LOAD_BAND],
		        moved, LOAD_BAND_STEPS);
		for (k = 0; k < LOAD_HELD; k++)
			if (worst[k] < LOAD_OWED[k]) {
				/* A contrast is never under 1:1, so a band at 0 was not
				 * measured: load_band_fit could not read its pixels. */
				if (k == LOAD_BAND && worst[k] <= 0.0f)
					fprintf(stderr, "port: loading screen short: the band could "
					        "not be read back, owed %.1f:1\n", LOAD_OWED[k]);
				else
					fprintf(stderr, "port: loading screen short: %s at %.2f:1, owed "
					        "%.1f:1%s\n", LOAD_HELD_NAME[k], worst[k], LOAD_OWED[k],
					        k == LOAD_BAND && moved == LOAD_BAND_STEPS
					        ? ", even moved all the way to text" : "");
				held = false;
			}
	} else {
		fprintf(stderr, "port: loading screen short: nothing could be read back, so "
		        "the scrim is whole and nothing was measured\n");
	}
	if (tex) SDL_DestroyTexture(tex);
	free(g);
	if (own) SDL_DestroyTexture(cover);
	return held;
}

/* The still for the game under the cursor, written for splash. */
void port_loading_screen(app *a, const system_cfg *s)
{
	port_loading_draw(a, s, a->view[a->sys_cursor].cursor);
	unlink(PORT_LOADING);
	if (!plat_save_screen(PORT_LOADING)) plat_present();
}
