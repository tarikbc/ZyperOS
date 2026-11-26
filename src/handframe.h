/* SPDX-License-Identifier: GPL-3.0-only */
/* A handheld's frame around its game: Border's Handheld and Handheld, Whole
 * (src/border.h, DESIGN.md 11.59), ZyperOS's own drawing of the real one at
 * the game's own scale.
 *
 * The scale is the game's: one game pixel is one dot of the handheld's LCD,
 * so a millimetre of the face is (panel pixels a game pixel) / (the LCD's dot
 * pitch), across and down each by its own pitch, never stretched. A model
 * (src/handframe_models.c) is the face in millimetres from the LCD's active
 * area's top left corner, every number with its source there; this file
 * draws it at a placement:
 *
 *   Handheld          the game at its full Sharp Pixels size (the largest
 *                     whole step that fits: 3x for a Game Boy on the Pixel 2),
 *                     where Sharp Pixels puts it, the frame cut where the
 *                     panel ends; a power light in the lens that the panel
 *                     would cut is drawn nearer the screen, the least it
 *                     takes to be whole (hf_light_dx)
 *   Handheld, Whole   smaller by whole steps until the whole lens and some
 *                     body fit (2x on the Pixel 2, 3x on the Brick), placed
 *                     so the lens is in the middle of the panel
 *
 * Every straight edge is on whole pixels (each edge's millimetre place,
 * rounded once), so a line is one crisp pixel row at both resolutions; only a
 * corner's arc is smoothed, by how much of each pixel it covers. Drawn once
 * into a BGRA picture the panel's size (border_pic.c writes it for Diatom),
 * never in a frame.
 *
 * Plain C, no SDL: tools/handframe-check.c holds the placement, the pixel
 * edges and the colors; tools/handframe-sheet.py draws the check sheet. */
#ifndef ZYPEROS_HANDFRAME_H
#define ZYPEROS_HANDFRAME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t r, g, b; } hf_rgb;

/* A rounded rectangle on the face, in millimetres from the LCD's active
 * area's top left (x right, y down), its corners' radii top left, top right,
 * bottom right, bottom left. Its top or bottom edge may be an arc instead
 * of straight (the Game Boy Color's lens's bottom, the Advance's top and
 * bottom): `sag_top`, `sag_bot` how far the arc's middle stands out past
 * where its ends are, y0 and y1 being the arc's middle; the corners beside
 * an arc are then fillets of their radius between the side and the arc. */
typedef struct {
	float x0, y0, x1, y1;
	float r[4];
	float sag_top, sag_bot;
} hf_rrect;

/* What a part of the face is filled with: one of the model's own colors, or
 * the body's color of the variant drawn (a Game Boy Color's Yellow, Teal...)
 * or its shade (the body's edge, where it turns away) */
typedef enum { HF_OWN, HF_BODY, HF_BODY_DARK } hf_paint;

typedef struct {
	hf_rrect shape;
	hf_paint paint;
	hf_rgb   color;     /* HF_OWN's */
} hf_part;

#define HF_PARTS 16
#define HF_COLORS 8

typedef struct {
	const char *tag;          /* the system it frames: "GB", "GBC", "GBA" */
	int   cols, lines;        /* the game's picture: 160 x 144 */
	float lcd_w, lcd_h;       /* the LCD's active area, mm */
	/* The body's colors, by name ("Gray"; a Game Boy Color's six): the
	 * first is the default. HF_BODY_DARK is each one's shade. */
	int         ncolors;
	const char *color_name[HF_COLORS];
	hf_rgb      body[HF_COLORS], body_dark[HF_COLORS];
	/* The face over the body, drawn in this order: the lens, its window,
	 * its lines, the light */
	int     nparts;
	hf_part part[HF_PARTS];
	/* What Handheld, Whole centres on the panel: the lens */
	hf_rrect lens;
	/* The power light's ring: the parts inside it are the light, drawn
	 * nearer the screen where the panel would cut it (hf_light_dx) */
	hf_rrect light;
} hf_model;

typedef enum { HF_CUT, HF_WHOLE } hf_kind;

/* Handheld, Whole's least body round the lens on each side, mm */
#define HF_WHOLE_BODY_MM 1.5f

/* The least face round a light drawn nearer the screen, mm: from the panel's
 * edge and from the game */
#define HF_LIGHT_ROOM_MM 1.0f

/* Where the game goes for `kind` on a surface `w` x `h`: its rect, a whole
 * number of panel pixels a game pixel (`*scale`). False where the game does
 * not fit at 1x, and Whole where the whole lens fits only under 2x (the
 * Advance on the Pixel 2's 640x480: 2x is its full size). */
bool hf_place(const hf_model *m, hf_kind kind, int w, int h, int *scale, int *gx, int *gy, int *gw, int *gh);

/* How far across, in pixels, the light is drawn from its place on the face
 * at this placement: 0 where it is whole on the panel with HF_LIGHT_ROOM_MM
 * round it, or where it is not in the lens (the Advance's, on the body past
 * the lens, off the panel whole); else the least it takes to be whole on the
 * panel with that room, toward the game, and 0 again where that would not
 * leave the room between it and the game. A Game Boy's light is 8.3 mm left
 * of its screen: with the game in the middle at its full size the Pixel 2
 * showed a third of it and the Brick none (Reported on Discord, 2026-11-22:
 * "the Gameboy red light awkwardly off screen"). Only the light moves; the
 * rest of the face stays true to scale. */
int hf_light_dx(const hf_model *m, int w, int gx, int gy, int gw, int gh);

/* The frame, `w` x `h` BGRA, opaque, around the game at gx, gy, gw x gh,
 * the body in color `color` (0 the first), the light where hf_light_dx
 * puts it */
void hf_render(const hf_model *m, int color, int w, int h, int gx, int gy, int gw, int gh, uint8_t *bgra);

/* A part's millimetre rect at a placement, in whole pixels: each edge's place
 * rounded once (so two parts that share an edge in millimetres share it in
 * pixels) */
void hf_px_rect(const hf_model *m, const hf_rrect *r, int gx, int gy, int gw, int gh,
                int *x0, int *y0, int *x1, int *y1);

/* The model for a system's tag, or NULL: the frames drawn so far */
const hf_model *hf_model_for(const char *tag);

#endif
