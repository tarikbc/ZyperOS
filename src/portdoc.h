/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PORTDOC_H
#define ZYPEROS_PORTDOC_H

/* What a port says about itself, for the Controls pages of the in-game menu
 * over it (src/controls.h, ctl_game.doc; src/portmenu.h).
 *
 * A PortMaster port is a script at the top of Roms/Ports and a folder beside
 * it, and that folder says what it is in files of PortMaster's own layout:
 *
 *   port.json     "items" names the script and the folder; "attr" has the
 *                 port's title, its description (desc) and what to copy
 *                 where before it runs (inst)
 *   *.gptk        the keymap gptokeyb types keys from, a button a line
 *                 ("a = space"), which the script names after -c
 *   README*       the porter's notes; many have a "Controls" section, a line
 *                 a button ("A = Jump", "| A | Jump |")
 *
 * Read once, when the menu opens over the port: the menu's process is
 * started for it alone (src/main.c, port_menu_main). Nothing here is
 * trusted to be well formed: a file too big, a line too long, a button this
 * device does not have or a JSON that is not JSON is passed over, and the
 * page says only what was read cleanly. The handheld's buttons are as SDL
 * names them, which on the Pixel 2 are the caps that say so (A is the cap
 * printed A, tools/px2-input.c), and as PortMaster's control.txt for ZyperOS
 * leaves them: SELECT is its hotkey, there are no sticks, and MENU is
 * ZyperOS's (res/portmaster/control.txt).
 *
 * No SDL and no T(): the words a page says in the language that is on are
 * src/controls.c's. tools/portdoc-check.c holds the readers to fixtures. */

#include <stdbool.h>
#include <stddef.h>

/* The handheld's buttons a port can be given, in the order a page lists
 * them; the d-pad's four last */
typedef enum {
	PD_A, PD_B, PD_X, PD_Y, PD_L1, PD_R1, PD_L2, PD_R2, PD_START, PD_SELECT,
	PD_UP, PD_DOWN, PD_LEFT, PD_RIGHT, PD_BUTTONS
} pd_button;

/* A key gptokeyb types, as a page says it: a key's name as printed on a
 * keyboard ("Enter", "Esc", "Q", "F1"), or one of these, which are words
 * in the language that is on */
typedef enum {
	PD_NAME,            /* name[] */
	PD_SPACE,           /* the space bar */
	PD_ARROWS,          /* the d-pad's four as the four arrow keys */
	PD_CLICK_LEFT, PD_CLICK_RIGHT, PD_CLICK_MIDDLE,
	PD_UP_KEY, PD_DOWN_KEY, PD_LEFT_KEY, PD_RIGHT_KEY   /* one arrow key */
} pd_word;

typedef struct {
	pd_word word;
	char    name[24];   /* PD_NAME's; "" with PD_NAME is no key */
} pd_key;

/* The d-pad's directions on a row of the port's own words, as bits:
 * Up/Down, Left/Right or the whole d-pad are words in the page's language */
#define PD_DIR_UP    1
#define PD_DIR_DOWN  2
#define PD_DIR_LEFT  4
#define PD_DIR_RIGHT 8
#define PD_DIR_ALL   15

/* One line of a README's controls: the handheld's buttons as a page prints
 * them, "A", "L1/R1", "SELECT+START" - or, when `dirs` is set, the d-pad's
 * directions alone, which the page says in words - and what the port says
 * they do, in the port's own words (English, mostly) */
typedef struct {
	char     key[16];
	unsigned dirs;
	char     said[64];
} pd_row;

#define PD_ROWS 12
#define PD_TEXT 768

typedef struct {
	bool     portmaster;          /* PortMaster's own app, Roms/Ports/PortMaster.sh */
	char     title[64];           /* attr.title, else the script's name */
	char     desc[PD_TEXT];       /* attr.desc, as written ("" for none) */
	char     inst[PD_TEXT];       /* attr.inst, the same */
	pd_key   keys[PD_BUTTONS];    /* the .gptk's, each button's key */
	int      nkeys;               /* how many buttons type one; 0 for no keymap */
	pd_row   said[PD_ROWS];       /* the README's controls */
	int      nsaid;
	bool     gptokeyb;            /* the script runs gptokeyb, whose SELECT+START ends it */
} port_doc;

/* Everything the port's files say, for the port the script `script` (a full
 * path, Roms/Ports/<name>.sh) starts. False when it is no port of the Ports
 * shelf's layout; `out` is then the script's name and nothing else, which is
 * still a page's heading. */
bool port_doc_read(const char *script, port_doc *out);

/* ---- the readers, for tools/portdoc-check.c ---- */

/* port.json's text: its title, desc and inst into `out`, if its "items"
 * name `script_base` (the script's file name, "Wordle SDL.sh"); false when
 * they do not, or the text is no port.json */
bool pd_json(const char *json, size_t len, const char *script_base, port_doc *out);

/* A .gptk's text (gptokeyb's, or gptokeyb2's [controls]) into out->keys and
 * out->nkeys */
void pd_gptk(const char *text, port_doc *out);

/* A README's "Controls" section into out->said and out->nsaid */
void pd_readme(const char *text, port_doc *out);

/* What a script's text says of its port: the folder it works in
 * ("ports/wordlesdl", into dir), the keymap it hands gptokeyb ("wordle.gptk",
 * into gptk), and whether it runs gptokeyb at all */
void pd_script(const char *text, char *dir, size_t dn, char *gptk, size_t gn, bool *gptokeyb);

#endif
