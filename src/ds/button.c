/* SPDX-License-Identifier: GPL-3.0-only */
/* A button as this handheld prints it: src/ds/button.h. */
#include "button.h"

#include <string.h>

static const dev_button *g_table;
static int g_n;

void ds_keys_set(const dev_button *table, int n)
{
	g_table = n > 0 ? table : NULL;
	g_n = g_table ? n : 0;
}

/* The ids' names, as screens write them: what the face buttons, shoulders
 * and the keys under the screen are called on every handheld, and F1 and F2
 * for the brightness keys of a handheld that has its own */
static const struct { const char *name; in_button id; } NAMES[] = {
	{ "A", IN_ACCEPT }, { "B", IN_BACK }, { "X", IN_X }, { "Y", IN_Y },
	{ "L1", IN_L1 }, { "R1", IN_R1 }, { "L2", IN_L2 }, { "R2", IN_R2 },
	{ "L3", IN_L3 }, { "R3", IN_R3 },
	{ "SELECT", IN_SELECT }, { "START", IN_START }, { "MENU", IN_MENU },
	{ "POWER", IN_POWER }, { "F1", IN_BRIGHTDN }, { "F2", IN_BRIGHTUP },
	{ "VOL+", IN_VOLUP }, { "VOL-", IN_VOLDN },
	{ "UP", IN_UP }, { "DOWN", IN_DOWN }, { "LEFT", IN_LEFT }, { "RIGHT", IN_RIGHT },
};

in_button ds_key_id(const char *name)
{
	size_t i;

	if (!name) return IN_NONE;
	for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
		if (!strcmp(NAMES[i].name, name)) return NAMES[i].id;
	return IN_NONE;
}

/* The glyphs the launcher drew before handhelds had tables, and still draws
 * for a button a table leaves out: A B X Y round with their letter, MENU
 * round with three dots, every other a pill with its name */
static ds_key classic(const char *name)
{
	if (name[0] && !name[1] && strchr("ABXY", name[0]))
		return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_NONE, name };
	if (!strcmp(name, "MENU")) return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_DOTS, name };
	return (ds_key){ DS_KEY_PILL, DS_KEY_MARK_NONE, name };
}

ds_key ds_key_of(const char *name)
{
	const dev_button *b = NULL;
	in_button id;
	int i;

	if (!name) name = "";
	/* The d-pad's two hints are the d-pad, on every handheld */
	if (!strcmp(name, "< >")) return (ds_key){ DS_KEY_DPAD_LR, DS_KEY_MARK_NONE, name };
	if (!strcmp(name, "^ v")) return (ds_key){ DS_KEY_DPAD_UD, DS_KEY_MARK_NONE, name };
	if (!strcmp(name, "DPAD")) return (ds_key){ DS_KEY_DPAD, DS_KEY_MARK_NONE, name };
	if (!strcmp(name, "VOL")) return (ds_key){ DS_KEY_ROCKER, DS_KEY_MARK_NONE, name };
	id = ds_key_id(name);
	for (i = 0; id != IN_NONE && i < g_n; i++)
		if (g_table[i].id == id) b = &g_table[i];
	if (!b || !b->label) return classic(name);
	if (b->cap == DEV_CAP_STICK || b->icon == DEV_ICON_STICK)
		return (ds_key){ DS_KEY_STICK, DS_KEY_MARK_NONE, b->label };
	if (b->cap != DEV_CAP_ROUND) return (ds_key){ DS_KEY_PILL, DS_KEY_MARK_NONE, b->label };
	switch (b->icon) {
	case DEV_ICON_DOTS:   return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_DOTS, b->label };
	case DEV_ICON_SQUARE: return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_SQUARE, b->label };
	case DEV_ICON_PLAY:   return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_PLAY, b->label };
	default: break;
	}
	/* A round key with one or two letters is a face with them (A; the Pixel
	 * 2's SE and ST); with a word (the Brick's MENU and POWER), the word needs
	 * a pill's room, as a key's word always had */
	if (b->label[0] && (!b->label[1] || !b->label[2]))
		return (ds_key){ DS_KEY_FACE, DS_KEY_MARK_NONE, b->label };
	return (ds_key){ DS_KEY_PILL, DS_KEY_MARK_NONE, b->label };
}
