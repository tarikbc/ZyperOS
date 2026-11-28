/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DIATOM_PADMAP_H
#define DIATOM_PADMAP_H

/* A handheld's keys as Diatom's buttons, from the launcher's one table of
 * them (ZyperOS's src/device/<id>_input.h, src/device/devinput.h): the code
 * each key sends and the logical button it is, the same table the launcher
 * reads, so a key is the same button over a game as on the shelf. This says
 * only what each logical button is to Diatom: a game's button (DIATOM_BTN_*),
 * a level key (DIATOM_LEVEL_KEY_*, never a core's), or neither (POWER, the
 * launcher's; the right stick's pushes, which a game reads as its analog
 * stick). Every logical button, no default: one left out does not compile
 * clean (-Wswitch). */
#include <stddef.h>
#include <stdint.h>

#include "../../src/device/devinput.h"
#include "diatom_port.h"

static inline int padmap_btn(in_button b)
{
	switch (b) {
	case IN_UP:       return DIATOM_BTN_UP;
	case IN_DOWN:     return DIATOM_BTN_DOWN;
	case IN_LEFT:     return DIATOM_BTN_LEFT;
	case IN_RIGHT:    return DIATOM_BTN_RIGHT;
	case IN_ACCEPT:   return DIATOM_BTN_A;
	case IN_BACK:     return DIATOM_BTN_B;
	case IN_X:        return DIATOM_BTN_X;
	case IN_Y:        return DIATOM_BTN_Y;
	case IN_L1:       return DIATOM_BTN_L1;
	case IN_R1:       return DIATOM_BTN_R1;
	case IN_L2:       return DIATOM_BTN_L2;
	case IN_R2:       return DIATOM_BTN_R2;
	case IN_SELECT:   return DIATOM_BTN_SELECT;
	case IN_START:    return DIATOM_BTN_START;
	case IN_MENU:     return DIATOM_BTN_MENU;
	case IN_L3:       return DIATOM_BTN_L3;
	case IN_R3:       return DIATOM_BTN_R3;
	case IN_VOLUP:
	case IN_VOLDN:
	case IN_BRIGHTUP:
	case IN_BRIGHTDN:
	case IN_POWER:
	case IN_RS_LEFT:
	case IN_RS_RIGHT:
	case IN_RS_UP:
	case IN_RS_DOWN:
	case IN_COUNT:
	case IN_NONE:     break;
	}
	return -1;
}

static inline uint32_t padmap_level(in_button b)
{
	switch (b) {
	case IN_VOLUP:    return DIATOM_LEVEL_KEY_VOLUME_UP;
	case IN_VOLDN:    return DIATOM_LEVEL_KEY_VOLUME_DOWN;
	case IN_BRIGHTUP: return DIATOM_LEVEL_KEY_BRIGHTNESS_UP;
	case IN_BRIGHTDN: return DIATOM_LEVEL_KEY_BRIGHTNESS_DOWN;
	default:          return 0;
	}
}

/* A key code and the game's button it is */
typedef struct { int code; int btn; } padmap_row;

/* `keys`' rows that are a game's buttons, into `out` (room for `n`): how
 * many */
static inline size_t padmap_build(const dev_code *keys, size_t n, padmap_row *out)
{
	size_t i, m = 0;

	for (i = 0; i < n; i++) {
		const int btn = padmap_btn(keys[i].b);

		if (btn >= 0) out[m++] = (padmap_row){ keys[i].code, btn };
	}
	return m;
}

#endif
