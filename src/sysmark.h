/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SYSMARK_H
#define ZYPEROS_SYSMARK_H

/* A system's color as a mark - a rail, a menu's heading rule, a slot's dot,
 * the rule under a generated card's title - in the part of it that is plain
 * C: the rule of DESIGN.md 3.3 that src/ui.c's ui_sys_mark and ui_sys_mark_in
 * draw every such mark through, so there is one copy of it.
 *
 * As systems.cfg has it in Salt & Pepper, and a quarter of the way toward
 * **text** on Beard, so a yellow like Game Boy Color's still reads on a light
 * ground. Two colors are not a system's own and are said by role instead:
 *
 *   lime, the **accent**, which Muse and Favorites wear because they are
 *   ZyperOS's, is **accent-ink**, lime's own mark (the fill's lime is 1.3:1
 *   on Beard's ground). Asked for by value, since a color has nothing else
 *   to say whose it is; tools/designdef.py --check holds the accent to one
 *   value in both themes, so the value asked about is the one they wear;
 *
 *   CFG_NO_COLOR (src/config.h), a system whose line names no color, is
 *   **text-dim** in both themes, a mark that says where and not whose.
 *
 * No SDL, so tools/sysmark-check.c holds all three without a window. */

#include "design.h"

/* The mark of `rgb` - 0xRRGGBB as systems.cfg gives it, or CFG_NO_COLOR - on
 * a ground of theme `theme_ix`, DS_LIGHT or DS_DARK; anything else is
 * DS_LIGHT. The color in as a system has it and not as a ds_rgb, because
 * "no color" is a value a system's line can have and a ds_rgb cannot. */
ds_rgb sysmark(unsigned rgb, int theme_ix);

#endif
