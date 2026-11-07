/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_COVERFLOW_H
#define ZYPEROS_COVERFLOW_H

#include <SDL.h>
#include <stdbool.h>

/* Single-row Cover Flow: perspective-tilted cards with reflections,
 * rendered with SDL_RenderGeometry. Never draws more cards than there
 * are items. Any row with two or more items wraps. A single-item row
 * draws one centered card. */

#define CF_HALF_WINDOW 3
#define CF_WINDOW (2 * CF_HALF_WINDOW + 1)

/* How many cards either side of the cursor the CALLER guarantees are already
 * decoded, and so how far a move may usefully be drawn.
 *
 * This once said such a move CANNOT be drawn, because decoding happened on the
 * render path and seven cold cards in one frame is why a long jump showed two
 * still pictures. src/texload.c moved decoding onto a worker, so the frames
 * come out now whatever the cards are - the reason changed, the number did not.
 * A card past this window has no texture yet, and flying past empty slots says
 * less than cutting does.
 *
 * The other limit is the eye's. A row's move takes DS_MS_MOVE, 160 ms, which
 * is 9.6 frames at 60fps, so past about nine cards a move crosses more than one
 * card per frame on average, consecutive frames share no cards at all, and
 * there is no motion left to read. Eight is inside that and is what the
 * texture policy actually keeps, so it stays. It had more room when a move
 * took 240 ms: the eye's limit was fourteen cards then, and the shorter move
 * brought it down to just over this. coverflow.c asserts it stays under.
 * main.c's TEX_KEEP_NEAR is the guarantee and asserts it is not less. */
#define CF_WARM_CARDS 8

typedef struct {
	float size;       /* card height as a fraction of screen height */
	float aspect;     /* card width / card height */
	float step;       /* neighbor spacing as a fraction of card width */
	float side_scale; /* scale of fully off-center cards */
	float center_y;   /* card center y as a fraction of screen height */
	float tilt;       /* max yaw in radians */
	float reflect;    /* reflection height as a fraction of card height */
	/* Where every reflection has faded out by, as a fraction of the screen's
	 * height, or 0 for wherever the reflection ends: the top of the line of
	 * text under the row, so the words there - a count, a name - sit on the
	 * ground and never on the mirror image of a card. A reflection that would
	 * run past it fades out at it instead, sooner and steeper. */
	float floor;
	/* Give game covers equal area while preserving their aspect ratio.
	 * System cards and albums stay within their fixed frames. */
	bool equal_area;
	int side_alpha;   /* alpha of fully off-center cards (center is 255) */
	int strips;       /* vertical subdivisions per card */
} cf_layout;

extern const cf_layout CF_LAYOUT_GAMES;
extern const cf_layout CF_LAYOUT_ALBUMS;
/* Where the focused card's frame sits on screen, so the caller can lay text
 * out against it, or start a launch from it, without duplicating the
 * geometry. What lifts the card is cf_draw's to place (cf_lift_fn), under
 * the art as it is drawn and not this frame. */
void cf_focus_rect(const cf_layout *lay, int screen_w, int screen_h, SDL_Rect *out);

/* A move is a fixed-duration tween from `from` to `target` starting at `t0`,
 * rather than an ease toward a moving target. Distance changes the speed, not
 * the time: crossing the whole shelf takes exactly as long as stepping one
 * card, so the row never feels further away than it is. */
typedef struct {
	float pos;      /* continuous position, interpolated from -> target */
	float from;     /* where the move in flight started */
	float target;
	Uint32 t0;      /* when it started */
	bool active;    /* animation in flight */
	int last_cursor;
	bool primed;
	/* Which way the row last traveled, -1 or +1. Only a two-item ring needs
	 * it: there, both representatives of the other card sit exactly one step
	 * away, so which side it rests on is a genuine tie and the direction of
	 * travel is the only thing that can settle it sensibly. */
	int last_dir;
	/* A move longer than the warm window: the tween is drawn only as far as
	 * `cut_to` and then jumps to `target`.
	 *
	 * `cut_to` is a DRAWING endpoint and nothing else. An earlier version cut
	 * by overwriting `target` itself, which broke the moment a second move
	 * arrived mid-tween: cf_set_cursor_dir accumulates with `target += raw`,
	 * so the next press added its step to the glide endpoint rather than to
	 * the real destination, and holding a jump walked `pos` tens of cards away
	 * from `cursor`. The shelf then drew one part of the list under another
	 * part's title, and stayed there, because an unchanged cursor returns
	 * early and nothing ever put it back. */
	float cut_to;
	bool  cutting;
} coverflow;

/* The index a pending cut will land on, or -1 when none is pending. The caller
 * uses it to decode that window while the departure is still being drawn, so
 * the move lands on a card that is already there. */
int cf_landing(const coverflow *cf, int count);


/* Stage a horizontal move for the headless screenshot harness. */
void cf_stage(coverflow *cf, float from, float to, float u);

/* Whether a cut is in flight. The caller holds off evicting while it is, or it
 * would free the very cards the departure is still drawing: the cursor is
 * already at the destination and eviction is measured from the cursor. */
bool cf_cutting(const coverflow *cf);

/* Texture for item index; w/h receive its pixel size. May return NULL.
 *
 * `content_bottom` receives how far down the texture its opaque pixels reach,
 * as a fraction of height: 1.0 for art that fills its canvas, less for art
 * padded with transparency below the subject. The reflection is drawn from
 * there rather than from the card's edge - see draw_card. */
typedef SDL_Texture *(*cf_tex_fn)(void *ctx, int index, int *w, int *h,
                                  float *content_bottom);

/* What lifts a card off the ground, DESIGN.md 7, for cf_draw to put under a
 * card: `index` is the card's item, `art` the rect its whole texture covers
 * on screen (the bounds of it, turned as it is turned), `content` the same
 * down to where its opaque pixels stop (cf_tex_fn's content_bottom), and
 * `focus` how far it is in focus, 1 at the center falling to 0 a step out -
 * the weight the row already gives its size and its opacity. So the lift goes
 * with the card as the row moves, out of focus with the one leaving and into
 * it with the one arriving, and at rest is under the focused card alone.
 * Called only for a card with a texture and some focus, and before any card
 * is drawn. */
typedef void (*cf_lift_fn)(void *ctx, int index, const SDL_Rect *art,
                           const SDL_Rect *content, float focus);

/* The clock every move is measured on: SDL_GetTicks, or `now` once given
 * one, and SDL_GetTicks again for NULL. The shot harness gives it a clock
 * that stands still where a shot asks (--shot-at), so a frame part way
 * through a move is drawn the same every time it is asked for. */
void cf_clock(Uint32 (*now)(void));

void cf_reset(coverflow *cf, int cursor);
/* Move toward cursor (shortest path when wrapping), with the direction the
 * user actually pressed: -1, 0 or +1 (0 when the cursor moved by something
 * other than a press, such as a letter jump or a list rebuild). A ring of
 * two has both neighbors one step away, so it is the only size where the
 * shortest-path arithmetic cannot work out which way to turn. */
void cf_set_cursor_dir(coverflow *cf, int cursor, int count, int dir);
/* Step animation and draw. Returns true while still animating. `lift` may be
 * NULL; `ctx` is handed to it as it is to `get_tex`. */
bool cf_draw(coverflow *cf, SDL_Renderer *r, int screen_w, int screen_h,
             int count, cf_tex_fn get_tex, cf_lift_fn lift, void *ctx,
             const cf_layout *lay);

#endif
