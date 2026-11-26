/* SPDX-License-Identifier: GPL-3.0-only */
/* The handhelds' faces in millimetres (src/handframe.h), from the LCD's
 * active area's top left, x right and y down. ZyperOS's own drawing: the
 * plastic, the lens, its window, its lines and the light, and no logo, no
 * maker's name and no printed text (where a logo sits, the plastic is
 * plain).
 *
 * Where each number comes from (DESIGN.md 11.59 lists them with links):
 *
 *   published   Nintendo's: each body's size (Nintendo Japan's spec tables
 *               and Nintendo UK's history pages) and the LCD's screen size
 *               (the Game Boy's 47 x 43 mm, the Advance's 61.2 x 40.8 mm)
 *   measured    from straight-on pictures, scaled by a published size:
 *               the Game Boy from a flatbed scan (Wikimedia Commons,
 *               "DMG-01.jpg"), scaled by its 148 mm height; the Color from
 *               a flatbed scan ("Game Boy Color (green).png"), by its
 *               133.5 mm height; both about +-0.5 mm. The Advance from
 *               Nintendo Japan's own front drawing of it (n08/hardware/gba/
 *               zu.gif) and a photo corrected for its perspective on the
 *               LCD's published size ("Game Boy Advance front.jpg"), about
 *               +-1.5 mm: the photo's heights carry the LCD's depth under
 *               the lens, so its widths are used and the drawing's heights
 *   colors      sampled from those pictures and from Nintendo's own
 *               (nintendo.co.jp's Color and Advance pages); Clear Purple
 *               from a studio photo ("Game-Boy-Color-Purple.jpg", Evan-Amos)
 *               beside the lens. Not under a grey card, so near, not exact.
 *               The Game Boy Color's Red (Berry) has no picture found:
 *               Nintendo's name only. */
#include "handframe.h"

#include <string.h>

#define RGB(r, g, b) { (r), (g), (b) }

/* A rounded rect with its radii: top left, top right, bottom right, bottom
 * left */
#define RR(x0, y0, x1, y1, tl, tr, br, bl) { (x0), (y0), (x1), (y1), { (tl), (tr), (br), (bl) }, 0, 0 }
#define DISC(cx, cy, d) RR((cx) - (d) / 2, (cy) - (d) / 2, (cx) + (d) / 2, (cy) + (d) / 2, \
                           (d) / 2, (d) / 2, (d) / 2, (d) / 2)

/* ---- Game Boy (DMG-01) ----------------------------------------------------
 *
 * LCD 47 x 43 mm (Nintendo UK's technical data; the 1989 manual, p. 12), 160
 * x 144: 0.294 x 0.299 mm a dot. Body 90 x 148 mm (Nintendo Japan, Nintendo
 * UK); the scan's face is 87.3 mm wide at 148 tall (the 90 takes in the
 * wheels on its sides). From the scan, the active area in the middle of the
 * window it shows through:
 *
 *   window   47.6 x 43.8 (a quarter millimetre round the active area)
 *   lens     75.0 x 57.2, its corners 2.55, 2.3, 11.5 (the bottom right, the
 *            Game Boy's own) and 2.6 (a seller lists the lens 75 x 57)
 *   lines    two, 0.47 mm each, magenta over blue 1.5 mm apart, from 3.5 mm
 *            inside the lens's left edge to the printed words' band, and from
 *            that band to 3.2 mm before its right edge
 *   light    2.26 across, 8.3 left of the active area, 14.7 down
 *   groove   across the top of the face, a dark line and the ridge's light
 *            one under it, 12.3 and 10.9 above the active area */
static const hf_model DMG = {
	.tag = "GB", .cols = 160, .lines = 144, .lcd_w = 47.0f, .lcd_h = 43.0f,
	.ncolors = 1,
	.color_name = { "gray" },
	.body = { RGB(190, 184, 182) }, .body_dark = { RGB(160, 154, 153) },
	.nparts = 10,
	.part = {
		/* The body's silhouette in its shade, and the face 1 mm inside it:
		 * the side turning away */
		{ RR(-20.26f, -20.14f, 67.08f, 127.86f, 3.0f, 3.0f, 3.0f, 3.0f), HF_BODY_DARK, RGB(0, 0, 0) },
		{ RR(-19.26f, -19.14f, 66.08f, 126.86f, 2.0f, 2.0f, 2.0f, 2.0f), HF_BODY, RGB(0, 0, 0) },
		/* The groove across the top */
		{ RR(-19.26f, -12.45f, 66.08f, -12.15f, 0, 0, 0, 0), HF_OWN, RGB(122, 112, 110) },
		{ RR(-19.26f, -11.05f, 66.08f, -10.75f, 0, 0, 0, 0), HF_OWN, RGB(214, 208, 206) },
		{ RR(-14.15f, -6.91f, 60.85f, 50.29f, 2.55f, 2.3f, 11.5f, 2.6f), HF_OWN, RGB(94, 90, 100) },
		/* The two lines, left and right of the words' band */
		{ RR(-10.60f, -4.55f, 9.15f, -4.08f, 0, 0, 0, 0), HF_OWN, RGB(124, 46, 86) },
		{ RR(-10.60f, -3.06f, 9.15f, -2.59f, 0, 0, 0, 0), HF_OWN, RGB(46, 32, 88) },
		{ RR(49.07f, -4.55f, 57.67f, -4.08f, 0, 0, 0, 0), HF_OWN, RGB(124, 46, 86) },
		{ RR(49.07f, -3.06f, 57.67f, -2.59f, 0, 0, 0, 0), HF_OWN, RGB(46, 32, 88) },
		/* The window, the LCD's own olive where the game is not */
		{ RR(-0.28f, -0.38f, 47.28f, 43.38f, 0, 0, 0, 0), HF_OWN, RGB(116, 120, 44) },
	},
	.lens = RR(-14.15f, -6.91f, 60.85f, 50.29f, 2.55f, 2.3f, 11.5f, 2.6f),
	.light = DISC(-8.32f, 14.67f, 2.26f),
};

/* The light is a part of its own, lit: its dark ring, then the red */
static hf_model dmg_lit(void)
{
	hf_model m = DMG;
	const hf_part ring = { DMG.light, HF_OWN, RGB(60, 51, 46) };
	const hf_part red = { DISC(-8.32f, 14.67f, 1.7f), HF_OWN, RGB(226, 42, 38) };

	m.part[m.nparts++] = ring;
	m.part[m.nparts++] = red;
	return m;
}

/* ---- Game Boy Color (CGB-001) ---------------------------------------------
 *
 * Body 78 x 133.5 mm (Nintendo Japan; Nintendo UK says 75 x 133). Nintendo's
 * screen size, 44 x 40 (or 39), takes in the black mask round the dots;
 * the scan's dots are 42.4 x 39.1 (RetroRGB measured 43 x 39): 0.265 x
 * 0.272 mm a dot. From the scan:
 *
 *   mask     1.0 mm of black round the dots
 *   lens     67.6 x 61.5 at its middle, its top corners 2.2, its bottom an
 *            arc standing 1.34 mm below its ends, a 4 mm fillet each side
 *   light    1.3 across, 9.1 left of the dots, 12.2 down
 *   body     76.5 wide at the lens (Nintendo's 78 is its widest) */
static const hf_model CGB = {
	.tag = "GBC", .cols = 160, .lines = 144, .lcd_w = 42.4f, .lcd_h = 39.1f,
	.ncolors = 6,
	.color_name = { "yellow", "red", "purple", "green", "teal", "clear_purple" },
	.body = { RGB(241, 197, 54), RGB(184, 36, 64), RGB(90, 70, 205), RGB(112, 220, 41), RGB(43, 146, 176),
	          RGB(178, 156, 200) },
	.body_dark = { RGB(205, 164, 38), RGB(150, 26, 50), RGB(70, 52, 170), RGB(88, 182, 30), RGB(30, 118, 146),
	               RGB(150, 122, 176) },
	.nparts = 6,
	.part = {
		{ RR(-17.17f, -13.38f, 59.33f, 120.12f, 4.0f, 4.0f, 4.0f, 4.0f), HF_BODY_DARK, RGB(0, 0, 0) },
		{ RR(-16.37f, -12.58f, 58.53f, 119.32f, 3.2f, 3.2f, 3.2f, 3.2f), HF_BODY, RGB(0, 0, 0) },
		{ { -12.48f, -7.75f, 55.13f, 53.73f, { 2.2f, 2.2f, 4.0f, 4.0f }, 0, 1.34f }, HF_OWN, RGB(19, 23, 30) },
		{ RR(-1.0f, -1.0f, 43.4f, 40.1f, 0, 0, 0, 0), HF_OWN, RGB(9, 10, 13) },
		{ DISC(-9.1f, 12.2f, 1.3f), HF_OWN, RGB(40, 20, 22) },
		{ DISC(-9.1f, 12.2f, 0.9f), HF_OWN, RGB(226, 42, 38) },
	},
	.lens = { -12.48f, -7.75f, 55.13f, 53.73f, { 2.2f, 2.2f, 4.0f, 4.0f }, 0, 1.34f },
	.light = DISC(-9.1f, 12.2f, 1.3f),
};

/* ---- Game Boy Advance (AGB-001) -------------------------------------------
 *
 * LCD 61.2 x 40.8 mm, 240 x 160 (Nintendo Japan's spec table): 0.255 mm a
 * dot. Body 144.5 x 82 mm (Nintendo Japan). From Nintendo's front drawing
 * and the corrected photo, about +-1.5 mm:
 *
 *   lens     75.2 wide (7.0 each side of the dots; Retro Modding's shell
 *            window is about 77), 6.4 over the dots and 13.6 under them at
 *            its middle, its top and bottom gently arched (1.5 and 2.0),
 *            top fillets 3.7, bottom 8
 *   light    on the body right of the lens's top, 2 across, 76.5 right of
 *            the dots' left edge, 2.5 over their top
 *   body     centred across the dots, its middle 3.6 mm under theirs */
static const hf_model AGB = {
	.tag = "GBA", .cols = 240, .lines = 160, .lcd_w = 61.2f, .lcd_h = 40.8f,
	.ncolors = 1,
	.color_name = { "violet" },
	.body = { RGB(78, 60, 140) }, .body_dark = { RGB(60, 46, 112) },
	.nparts = 6,
	.part = {
		{ RR(-41.65f, -16.6f, 102.85f, 65.4f, 16.0f, 16.0f, 16.0f, 16.0f), HF_BODY_DARK, RGB(0, 0, 0) },
		{ RR(-40.65f, -15.6f, 101.85f, 64.4f, 15.0f, 15.0f, 15.0f, 15.0f), HF_BODY, RGB(0, 0, 0) },
		{ { -7.0f, -6.4f, 68.2f, 54.4f, { 3.7f, 3.7f, 8.0f, 8.0f }, 1.5f, 2.0f }, HF_OWN, RGB(28, 36, 44) },
		{ RR(-0.25f, -0.25f, 61.45f, 41.05f, 0, 0, 0, 0), HF_OWN, RGB(16, 20, 24) },
		{ DISC(76.5f, -2.5f, 2.0f), HF_OWN, RGB(30, 30, 40) },
		{ DISC(76.5f, -2.5f, 1.4f), HF_OWN, RGB(70, 200, 90) },
	},
	.lens = { -7.0f, -6.4f, 68.2f, 54.4f, { 3.7f, 3.7f, 8.0f, 8.0f }, 1.5f, 2.0f },
	.light = DISC(76.5f, -2.5f, 2.0f),
};

const hf_model *hf_model_for(const char *tag)
{
	static hf_model dmg;
	static int made;

	if (!made) { dmg = dmg_lit(); made = 1; }
	if (!tag) return NULL;
	if (!strcmp(tag, "GB")) return &dmg;
	if (!strcmp(tag, "GBC")) return &CGB;
	if (!strcmp(tag, "GBA")) return &AGB;
	return NULL;
}
