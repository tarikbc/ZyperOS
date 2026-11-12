/* What the game info screen contains, as rows rather than as pixels.
 *
 * Split out of main.c so a check can ask what this screen says about a game
 * without a card, a renderer or a device. Nothing here may include SDL -
 * tools/menu-check.c links it without one.
 *
 * gi_gather stays in main.c, because reading a card is exactly the half that
 * does not belong here. */
#ifndef GAME_MENU_H
#define GAME_MENU_H

#include <stdbool.h>

#include "menu.h"
#include "library.h"

/* Save slots. Six the player picks from, plus the resume slot the exit funnel
 * owns - which is NAMED "auto" and is not one of the numbered ones.
 *
 * Here rather than in main.c so a check can see it. The carousel used to map
 * its Auto entry to a hardcoded 9, MinUI's number for the same idea, and when
 * the resume slot was renamed on 2026-08-27 every use of the CONSTANT was
 * updated and that literal was not. Loading Auto from the in-game menu asked
 * for a `.9.state` for ten days, and the state_rejected that came back was
 * misread as a dead emulator - which started a second one over the top of the
 * first and wedged the display. A number nothing could test. */
#define GM_SLOTS  6
#define SLOT_AUTO (GM_SLOTS + 1)

/* The carousel shows Auto first, then the numbered slots. This is the only
 * place that mapping is written down. */
static inline int gm_slot_at(int carousel_index)
{
	return carousel_index == 0 ? SLOT_AUTO : carousel_index;
}

/* Which slot the Save carousel opens on: the first EMPTY one, or the LAST slot
 * when every slot is taken.
 *
 * The menu is A to open Save and A to choose, so a double-tap used to
 * overwrite slot 1 every time - the one press people make without looking,
 * aimed at the slot most likely to hold something they wanted.
 *
 * The full case landed on the OLDEST save first, on the reasoning that least
 * recent means least wanted. That is backwards. A save is often old BECAUSE it
 * is deliberately kept - the finished playthrough, the one before a point of
 * no return, the thing you set aside and stopped touching. Recency measures
 * activity, not value, and position measures intent: people put what matters
 * in a low slot and let scratch pile up in high ones.
 *
 * The last slot is also PREDICTABLE, which the oldest never is. "When full it
 * reuses the last slot" is a rule you can learn and plan around; "it reuses
 * whichever is oldest" moves the target and can land on the one save you were
 * protecting. Slot 1 stays five presses away and never chosen by accident.
 *
 * `have` is indexed 1..GM_SLOTS; index 0 is the Auto slot and Save cannot aim
 * at it. Pure, so tools/menu-check.c can enumerate the cases. */
static inline int gm_save_slot(const bool *have)
{
	int i;

	for (i = 1; i <= GM_SLOTS; i++)
		if (!have[i]) return i;
	return GM_SLOTS;
}

/* The quick slot: SELECT+R1 saves to it and SELECT+L1 loads it (Diatom's
 * chord.h), and the menu's Quick Slot row picks it, kept per system in the
 * device's database. Its key there, quickslot.<TAG>; the slot a stored value
 * says, 1 when there is none or it is no slot; and the slot `d` steps from
 * `slot`, round 1 to GM_SLOTS. Pure, for tools/menu-check.c. */
#define GM_QUICK_DEFAULT 1
void gm_quick_key(const char *tag, char *out, size_t n);
int  gm_quick_slot(const char *stored);
int  gm_quick_step(int slot, int d);

/* The N64's Pak: the values the row steps between, "rumble" (ZyperOS's
 * default: the Pixel 2 has a motor) and "memory", which both N64 cores take
 * for their player 1 pak option, and how the row and the Controls page name
 * each, the products' own names in every language. A value it does not know
 * is the Rumble Pak. gm_pak_option is that option's name on the core given,
 * NULL for a core with no pak: ParallelN64's parallel-n64-pak1 and
 * Mupen64Plus-Next's mupen64plus-pak1. Pure, for tools/menu-check.c. */
const char *gm_pak_option(const char *core);
const char *gm_pak_value(const char *stored);          /* "rumble" | "memory" */
const char *gm_pak_other(const char *value);           /* the one it steps to */
const char *gm_pak_name(const char *value);            /* "Rumble Pak" ... */
void gm_pak_key(const char *tag, const char *file, char *out, size_t n);

#define GI_MAX 7          /* four facts at most, Favorite and two actions */

/* The genre line, from the scrape. Sized from the card rather than guessed:
 * measured 2026-09-17 over 1,704 rows, the whole comma-separated value has a
 * median of 15 characters, a 90th percentile of 36 and a longest of 63 - one
 * game in three carries a second genre and one in forty a third. 80 holds all
 * of them with room, which is what stops this being the row that truncates. */
#define GI_GENRE_MAX 80

/* Everything worth saying about one game, gathered once.
 *
 * Gathered rather than watched: this screen is a still. It reads the card, the
 * save directory and the achievement set when it opens, and again after an
 * action changes one of them. Polling would mean a stat storm every frame for
 * numbers that only move when the player does something. */
typedef struct {
	char  cheevos[64];
	char  year[8];         /* from the scrape, empty when it said nothing */
	char  genre[GI_GENRE_MAX];   /* the same, comma separated */
	bool  has_cheevos;     /* there is a set to open, not just a count to read */
	/* No set because the game was never played with a network: the row says
	 * "not checked yet", and A on it says how it gets one (chv_state_of) */
	bool  cheevos_unchecked;
	bool  has_art;         /* Cover and Video's value: "no cover" */
	bool  has_snap;        /* ...and "no video" */
	/* Art of its own that no fetch replaces: PortMaster's entry on the
	 * Ports shelf (src/pminstall.h, PM_COVER_LIGHT), so no Box Art row */
	bool  own_art;
	/* Whether this game has a row in the card's games table at all, and
	 * whether that row carries a synopsis. Two flags rather than one, because
	 * they answer different questions: a game nobody has scraped shows neither
	 * row, and a game that was scraped and came back without prose says so
	 * rather than looking unscraped. */
	bool  scraped;
	bool  has_synopsis;
	/* Delete Game is offered (src/gamedel.h): everywhere but Ports, whose
	 * games are scripts beside folders nothing says are theirs */
	bool  can_delete;
	/* The Favorite row: Y on this page (and A on the row) marks it, since
	 * Y on the shelf starts a game fresh (2026-11-08) */
	bool  can_favorite;
	bool  favorite;
} game_info;

/* `net` is whether there is a network, asked by the caller and passed in: the
 * two rows that need one say so instead of quietly doing nothing. */
int gi_rows(menu_row *out, const game_info *gi, bool net);

/* ---------- the in-game menu ---------------------------------------------- */

/* GM_ROWS is every game's menu. A game with two screens - a Nintendo DS
 * game on DSperate, whose one frame Diatom cuts into its top and bottom
 * screens - has three rows more, after Display and Shader, where the
 * picture is already the subject: Screens (the layout), Swap Screens and
 * Stylus. Their
 * ids come after GM_ROWS so that every other game's rows are still their own
 * ids, in order; gm_row_ids says which rows a menu has, in the order shown. */
/* Controls, after Display (2026-11-06, docs/design/mock/controls-lab.html):
 * every control this game has, on its system (src/controls.h). After a DS
 * game's three rows, which belong with Display. Eight rows is one more than
 * the panel shows, so Quit is under the arrow, one Up from Continue.
 *
 * Quick Slot, after Load (2026-11-07): the slot SELECT+R1 saves to and
 * SELECT+L1 loads from, 1 to GM_SLOTS, kept per system; left, right and A
 * step it. With Save and Load, where states are. A system whose core keeps
 * no state (controls.h, ctl_has_states) has no quick slot and no row, so
 * its menu has a row fewer: gm_row_ids says which rows a menu has. */
/* Shader, right after Display (2026-11-12): how the picture is drawn at its
 * last step, Off or a shader by its name (src/app.h SHADERS), kept for the
 * game's system and sent to the game at once. Every game's, so it is one of
 * GM_ROWS, and Controls is a row further down.
 *
 * Pak, a Nintendo 64 game's (2026-11-07): what its controller holds, the
 * Rumble Pak or the Controller Pak (the memory card some games save to),
 * kept for the game. Right before Controls, whose C buttons page says which
 * one is in. Its id comes after the screens' rows, as theirs do. */
typedef enum {
	GM_CONTINUE, GM_SAVE, GM_LOAD, GM_QUICK, GM_DISPLAY, GM_SHADER, GM_CONTROLS, GM_CHEEVOS,
	GM_RESET, GM_QUIT,
	GM_ROWS,
	GM_SCREENS = GM_ROWS, GM_SWAP, GM_STYLUS, GM_PAK,
	GM_MAX_ROWS
} gm_row;

/* The layouts Diatom's SETSCREENS takes, in the Screens row's order, and how
 * the row says them. */
#define GM_LAYOUTS 3
extern const char *const gm_layout_names[GM_LAYOUTS];    /* stacked, focus, single */
extern const char *const gm_layout_labels[GM_LAYOUTS];   /* Stacked, Top Focus, Single */
/* The same in the language that is on, which is what a row shows; the table
 * above is English. Past the ends, the first. */
const char *gm_layout_label(int layout);
int gm_layout_index(const char *name);                   /* 0 for one it does not know */
/* A system with no layout on record: the top screen alone. */
#define GM_LAYOUT_DEFAULT 2                              /* single */

/* What the in-game menu needs to know, gathered by the caller. */
typedef struct {
	const char *dmode;    /* the display mode's label */
	const char *shader;   /* the shader's label; NULL reads Off */
	int         earned;
	int         total;    /* 0: this game has no achievement set */
	/* ...because it was never played with a network: not asked yet */
	bool        unchecked;
	/* Two screens in this game's frame, and how they are shown now. */
	bool        screens;
	int         layout;   /* into gm_layout_names */
	bool        swap;
	bool        stylus;
	/* A program of its own under the menu, not a game Diatom runs: OpenBOR
	 * (src/portmenu.h). Paused by stopping it, so there is nothing to save,
	 * load, reset or show differently, and no achievements. */
	bool        port;
	/* The quick slot, 1 to GM_SLOTS; 0 where the system keeps no states */
	int         quick;
	/* A Nintendo 64 game's Pak, as gm_pak_name says it; NULL for no row */
	const char *pak;
} gm_ui;

/* The rows this menu has, in the order shown: GM_ROWS of them, or
 * GM_MAX_ROWS with the screens' three, a row fewer with no quick slot, one
 * more with an N64 game's Pak; over a port, Continue, Controls and Quit. */
int gm_row_ids(const gm_ui *u, gm_row *ids);
/* Where row `id` is in that list, or -1 when the menu has none */
int gm_row_at(const gm_ui *u, gm_row id);
/* The row X does from anywhere on the menu's page, as A on it does: Quit,
 * in every in-game menu (a game's, a port's, PortMaster's), so leaving a
 * game is one press and not a walk to the last row. Its row carries X's
 * glyph at the right (MENU_KEY_X) to teach it. Quit asks nothing first,
 * so neither does X. No row of the page answered X before
 * (2026-11-08). */
gm_row gm_x_row(void);

/* Where the Cheevos row's text lives; the caller owns it, because a row holds
 * a pointer rather than a copy. And each row's sound class (MENU_SND_*,
 * menu.h), set by gm_rows by the row's id, for the menu runner. */
typedef struct {
	char cheevos[48];
	char quick[8];
	unsigned char sound[GM_MAX_ROWS];
} gm_bufs;

int gm_rows(const gm_ui *u, menu_row *out, gm_bufs *b);

#endif
