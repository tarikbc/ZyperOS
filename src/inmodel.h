/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_INMODEL_H
#define ZYPEROS_INMODEL_H

/* The logical buttons (DESIGN.md 11.9, "One input model"): every button the
 * launcher knows (src/inbutton.h), and what each is everywhere it is named,
 * in ONE table: its id's name as the screens write it ("A", "SELECT", "L3",
 * src/ds/button.h's), desk mode's word for it (its control socket's "press
 * a", src/device/desk.c), the handheld's key it lights on the 3D drawing
 * (src/tdkey.h), and which stick and direction it is, for a stick's.
 *
 * What a device reports is its profile's (src/device/profile.h, dev_input):
 * its key codes and its sticks' axes, mapped onto these. The one engine that
 * turns those into presses, chords and what the hand holds is
 * src/inengine.h. No consumer reads a device's codes: a screen, the tutorial,
 * the hotkeys' capture, desk mode and the Simulator read these buttons.
 *
 * Pure and in this header alone: no SDL and nothing to link, so any check
 * that names a button has the table (tools/inmodel-check.c holds it). */

#include <stdbool.h>
#include <string.h>

#include "inbutton.h"
#include "tdkey.h"

/* Every mask of buttons (desk mode's, main.c's) is 32 bits */
_Static_assert(IN_COUNT <= 32, "a mask of the buttons is 32 bits");
/* The d-pad's four and the right stick's four go in one order: a stick's
 * direction d is its first button + d (src/stickpad.h's STICK_*) */
_Static_assert(IN_RIGHT == IN_LEFT + 1 && IN_UP == IN_LEFT + 2 && IN_DOWN == IN_LEFT + 3,
               "the d-pad's four in stickpad's order");
_Static_assert(IN_RS_RIGHT == IN_RS_LEFT + 1 && IN_RS_UP == IN_RS_LEFT + 2 && IN_RS_DOWN == IN_RS_LEFT + 3,
               "the right stick's four in stickpad's order");

/* What kind of control a button is */
typedef enum {
	IK_DPAD,       /* the d-pad's four, which the left stick moves too */
	IK_FACE,       /* A B X Y */
	IK_SHOULDER,   /* L1 R1 L2 R2 */
	IK_KEY,        /* SELECT START MENU */
	IK_LEVEL,      /* the volume and brightness keys */
	IK_POWER,
	IK_CLICK,      /* a stick pressed: L3 R3 */
	IK_STICK       /* a stick pushed, a direction */
} in_kind;

typedef struct {
	in_button   id;      /* its own index: tools/inmodel-check.c holds it */
	const char *name;    /* as screens name it: "A", "SELECT", "F1", "VOL+" */
	const char *word;    /* desk mode's word: "a", "select", "rsleft" */
	in_kind     kind;
	td_key      key;     /* the handheld's key it lights, TD_KEYS for none */
} in_info;

/* In src/inbutton.h's order, one row a button, each with its own id so
 * tools/inmodel-check.c can say a row is where its id is: a row left out or
 * put in twice is a count that is not IN_COUNT, which does not compile */
static const in_info IN_INFO[] = {
	{ IN_LEFT,     "LEFT",   "left",       IK_DPAD,     TD_DPAD   },
	{ IN_RIGHT,    "RIGHT",  "right",      IK_DPAD,     TD_DPAD   },
	{ IN_UP,       "UP",     "up",         IK_DPAD,     TD_DPAD   },
	{ IN_DOWN,     "DOWN",   "down",       IK_DPAD,     TD_DPAD   },
	{ IN_ACCEPT,   "A",      "a",          IK_FACE,     TD_A      },
	{ IN_BACK,     "B",      "b",          IK_FACE,     TD_B      },
	{ IN_X,        "X",      "x",          IK_FACE,     TD_X      },
	{ IN_Y,        "Y",      "y",          IK_FACE,     TD_Y      },
	{ IN_L1,       "L1",     "l1",         IK_SHOULDER, TD_L1     },
	{ IN_R1,       "R1",     "r1",         IK_SHOULDER, TD_R1     },
	{ IN_START,    "START",  "start",      IK_KEY,      TD_START  },
	{ IN_SELECT,   "SELECT", "select",     IK_KEY,      TD_SELECT },
	{ IN_MENU,     "MENU",   "menu",       IK_KEY,      TD_MENU   },
	{ IN_VOLUP,    "VOL+",   "volup",      IK_LEVEL,    TD_VOLUP  },
	{ IN_VOLDN,    "VOL-",   "voldown",    IK_LEVEL,    TD_VOLDN  },
	{ IN_BRIGHTUP, "F2",     "brightup",   IK_LEVEL,    TD_F2     },
	{ IN_BRIGHTDN, "F1",     "brightdown", IK_LEVEL,    TD_F1     },
	{ IN_POWER,    "POWER",  "power",      IK_POWER,    TD_POWER  },
	{ IN_L2,       "L2",     "l2",         IK_SHOULDER, TD_L2     },
	{ IN_R2,       "R2",     "r2",         IK_SHOULDER, TD_R2     },
	{ IN_L3,       "L3",     "l3",         IK_CLICK,    TD_L3     },
	{ IN_R3,       "R3",     "r3",         IK_CLICK,    TD_R3     },
	/* The right stick's cap is R3's key: pushed, it lights as pressed */
	{ IN_RS_LEFT,  "RS LEFT",  "rsleft",   IK_STICK,    TD_R3     },
	{ IN_RS_RIGHT, "RS RIGHT", "rsright",  IK_STICK,    TD_R3     },
	{ IN_RS_UP,    "RS UP",    "rsup",     IK_STICK,    TD_R3     },
	{ IN_RS_DOWN,  "RS DOWN",  "rsdown",   IK_STICK,    TD_R3     },
};
_Static_assert(sizeof IN_INFO / sizeof IN_INFO[0] == IN_COUNT, "one row for every button");

/* Every button's, IN_LEFT to IN_RS_DOWN, in their order; NULL for anything
 * else (IN_NONE, IN_COUNT) */
static inline const in_info *in_info_of(in_button b)
{
	return b >= 0 && b < IN_COUNT ? &IN_INFO[b] : (const in_info *)0;
}

/* The button named `name` (in_info's name), IN_NONE for none */
static inline in_button in_by_name(const char *name)
{
	int b;

	for (b = 0; name && b < IN_COUNT; b++)
		if (!strcmp(IN_INFO[b].name, name)) return (in_button)b;
	return IN_NONE;
}

/* The button desk mode calls `word`, IN_NONE for none */
static inline in_button in_by_word(const char *word)
{
	int b;

	for (b = 0; word && b < IN_COUNT; b++)
		if (!strcmp(IN_INFO[b].word, word)) return (in_button)b;
	return IN_NONE;
}

#endif
