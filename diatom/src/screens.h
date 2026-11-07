/* Two screens in one frame, and a stylus for the one that is touched.
 *
 * A Nintendo DS core hands over ONE frame with both screens in it, the top
 * screen over the bottom: DSperate's is 256x384, two 256x192 screens. Shown as
 * it comes, that is a tall strip in the middle of a 4:3 panel - 320x480 at
 * best on the GKD Pixel 2's 640x480. So the frame is cut in two and each half
 * placed on its own, by a LAYOUT:
 *
 *   stacked  both, one over the other, scaled as one picture by the display
 *            mode: 320x480 at 1.25x on the Pixel 2 with `aspect`
 *   focus    one large, the other a quarter of its size beside it, at the
 *            bottom: 512x384 (2x, whole) and 128x96 on the Pixel 2
 *   single   one screen alone, scaled by the display mode: 640x480 exactly,
 *            2.5x, since a DS screen is 4:3 like the panel
 *
 * SWAP puts the bottom screen first: on top in stacked, the large one in
 * focus, the one shown in single. The stylus does the same while it is on,
 * in focus and single, so the screen being touched is never the small one or
 * the hidden one. A game starts with the top screen alone (single, no swap),
 * the way a DS is mostly played: looking at the top screen.
 *
 * Diatom keeps no core list, and does not decide that a frame holds two
 * screens: the launcher says so with RUN screens=2, because it knows which
 * shelf the game came from. Everything here is geometry and pixels, for any
 * core with the same stacked frame (melonDS DS's default is the same).
 *
 * THE STYLUS. A handheld with no touch panel and no stick - the Pixel 2 has
 * neither - touches the bottom screen with a cursor the d-pad moves. Stylus
 * mode is a toggle (R2, see THE BUTTONS below): while it is on, the d-pad is
 * the cursor's and not the game's, the cursor is drawn over the bottom
 * screen, and A is the stylus down. The libretro pointer it produces spans
 * the whole frame (-0x7fff..0x7fff on both axes), so the bottom screen is its
 * lower half, y >= 0 - what dsperate-lr reads.
 *
 * THE BUTTONS. A DS has L and R; the Pixel 2 has L1, R1, L2 and R2. The
 * shoulders are the DS's, the second pair the screens':
 *
 *   L1  the DS's L
 *   R1  the DS's R
 *   L2  the other screen in front - shown in single, large in focus, on top
 *       in stacked (scr_switch)
 *   R2  stylus mode on, which brings the bottom screen in front; R2 again
 *       turns it off and the screen that was in front comes back
 *   A   the stylus down while stylus mode is on, the DS's A otherwise
 *
 * So no DS button is lost: d-pad, A, B, X, Y, L, R, Start and Select all
 * reach the game, and only the d-pad and A are lent to the stylus while it is
 * on. scr_own_keys is the set Diatom keeps. ROGUE OS's DraStic had the same:
 * the screens on L2, the stylus on R2 and A as its tap.
 *
 * The cursor is drawn INTO THE FRAME, at the bottom screen's own pixels, just
 * before it is presented and taken out again just after: so it scales with
 * the screen under any layout, every port gets it without drawing anything,
 * and the frame the host keeps for previews and dupes never carries it.
 *
 * After plorpOS's src/stylus.c in its fork of Diatom (thwonp/diatom, MIT),
 * which does the same for its single-screen ports; the layouts after its
 * Layout rows. Written again here rather than copied.
 *
 * No libretro, no SDL, no port: test/screens_test.c drives all of it. */
#ifndef DIATOM_SCREENS_H
#define DIATOM_SCREENS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diatom.h"

typedef enum {
	SCR_STACKED = 0,
	SCR_FOCUS,
	SCR_SINGLE,
	SCR_LAYOUTS
} scr_layout;

extern const char *const scr_layout_names[SCR_LAYOUTS];
/* -1 for a name that is not one. */
int scr_layout_index(const char *name);

/* Where each screen goes. `frame_w` x `frame_h` is the whole frame, both
 * screens; `bottom_first` is swap (and the stylus, for focus and single).
 * Returns how many planes were written to `out`: 2, or 1 for single. Planes
 * are in drawing order, and none is empty. */
int scr_planes(scr_layout layout, bool bottom_first, diatom_scale_mode mode,
               int frame_w, int frame_h, int surf_w, int surf_h,
               diatom_plane out[DIATOM_PLANES_MAX]);

/* The rect the planes cover together, for DISPLAY rect=: a launcher draws the
 * paused frame there, and the preview (scr_compose) is that rect's picture. */
diatom_rect scr_bounds(const diatom_plane *p, int n);

/* The picture the planes make, at the scale of the largest one's source, for
 * the preview: what a stacked layout shows at 1.25x is the frame itself, a
 * single screen is 256x192, and focus is 320x192 with the small screen
 * sampled down to 64x48. Nearest, black where no plane is. `bytes_pp` is 2
 * (RGB565) or 4 (XRGB8888). Returns a malloc'd buffer of *w x *h pixels,
 * packed (pitch w * bytes_pp), or NULL. */
void *scr_compose(const void *frame, size_t pitch, int bytes_pp,
                  const diatom_plane *p, int n, int *w, int *h);

/* ---- the stylus -------------------------------------------------------- */

/* A new game: off, the cursor in the middle of a `w` x `h` screen. */
void sty_reset(int w, int h);
void sty_set(bool on);
bool sty_on(void);

/* Every frame while on: the d-pad (STY_* bits) and the stylus button. The
 * cursor moves a pixel a frame, then two, then four, the longer a direction
 * is held: a fine touch and a crossing of the screen in about a second both
 * on one d-pad. */
#define STY_UP    1u
#define STY_DOWN  2u
#define STY_LEFT  4u
#define STY_RIGHT 8u
void sty_frame(unsigned dirs, bool down);

/* Put the cursor at a pixel, clamped to the screen. */
void sty_place(int x, int y);

/* Where the cursor is, in the bottom screen's pixels, and whether it is down. */
void sty_cursor(int *x, int *y, bool *down);

/* The libretro pointer: true while down and on. */
bool sty_pointer(int16_t *x, int16_t *y);

/* ---- the buttons ------------------------------------------------------- */

/* L2: the other screen in front. In focus and single the stylus keeps the
 * bottom screen in front while it is on, so with the stylus on L2 goes to the
 * top screen and the stylus goes off - there is nothing to touch up there -
 * leaving the swap at the top screen. In stacked both screens show, and L2
 * only swaps them. */
void scr_switch(scr_layout layout, bool *swap, bool *stylus);

/* The buttons Diatom keeps from a DS game this frame (DIATOM_BIT mask): L2 and
 * R2 always, the d-pad and A as well while the stylus is on. */
uint32_t scr_own_keys(bool stylus);

/* The cursor into the bottom half of a frame (`frame_h` is both screens), and
 * back out. Each stamp is undone by the next unstamp; a stamp with the
 * stylus off does nothing. */
void sty_stamp(void *frame, size_t pitch, int bytes_pp, int frame_w, int frame_h);
void sty_unstamp(void *frame, size_t pitch, int bytes_pp);

#endif
