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
#include "link.h"
#include "p2.h"
#include "rahash.h"

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
/* Undo Load's (1.0.6): the game as it was just before the last state was
 * loaded into it, `<game>.undo.state`, written by every load the player asks
 * for (the in-game menu's and SELECT+L1's) and loaded by the Undo Load row.
 * Kept once: the next load writes over it. Named, like Auto, and none of the
 * player's slots. */
#define SLOT_UNDO (GM_SLOTS + 2)

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

#define GI_MAX 13         /* seven facts at most, Favorite, Add to Collection, Buttons and three actions */

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
	/* No set, and none will come: the launcher has no hash for this file
	 * (RA_HASH_NO_FILE) or for any of its system's (RA_HASH_NO_SYSTEM), so
	 * the row says which, quiet, rather than "not checked yet" and a play
	 * with Wi-Fi that would ask nothing (ra_hash_support). RA_HASH_OK else. */
	ra_hash_kind cheevos_no_hash;
	bool  has_art;         /* Cover and Video's value: "no cover" */
	bool  has_snap;        /* ...and "no video" */
	/* Art of its own that no fetch replaces: PortMaster's entry on the
	 * Ports shelf (src/pminstall.h, PM_COVER_LIGHT), so no Box Art row */
	bool  own_art;
	/* PortMaster's entry with PortMaster on the card: Reinstall PortMaster,
	 * in the place the Cover and Video row has on another game's page, so
	 * the page has no more rows than GI_MAX */
	bool  can_reinstall;
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
	/* Rename is offered (src/gamern.h): not for an arcade set, a BIOS set
	 * or a port, whose names are what runs them */
	bool  can_rename;
	/* The Favorite row: Y on this page (and A on the row) marks it, since
	 * Y on the shelf starts a game fresh (2026-11-08) */
	bool  can_favorite;
	bool  favorite;
	/* Add to Collection (src/collect.h, DESIGN.md 11.56), after Favorite:
	 * every game a favorite can be, its value how many collections it is in
	 * ("In 2"), none when it is in none */
	bool  can_collect;
	char  collected[24];
	/* The core the game starts on, while its shelf has more than one (the
	 * Game Boys', the N64's, the Dreamcast's): a fact, so a person asking
	 * for help can say which ran; "" for a shelf of one core */
	char  core[64];
	/* ...and steppable (core_game_step): its shelf offers more than one
	 * core here, so the game can have one of its own (2026-11-27) */
	bool  core_live;
	/* The game's file on the card (lib_file_shown), with the brackets its
	 * title leaves out, "(USA) (Rev 1)", "(Disc 2)", by which one copy is
	 * told from another (DESIGN.md 11.69); "" for an entry that is no file
	 * on the card (Install PortMaster, Splore), which has no File row */
	char  file[LIB_PATH + 1];
	/* How many discs it is (lib_discs), "3", as the Discs fact under File;
	 * "" for a game of one disc or none, which has no such row */
	char  discs[12];
	/* The game has a key of its own on the card (rms_game): its buttons
	 * differ from the system's, so the Buttons row shows them */
	bool  own_buttons;
} game_info;

/* `net` is whether there is a network, asked by the caller and passed in: the
 * two rows that need one say so instead of quietly doing nothing. */
int gi_rows(menu_row *out, const game_info *gi, bool net);

/* ---------- the in-game menu ---------------------------------------------- */

/* The in-game menu's rows by id. The ids say what a row is, not where it is:
 * which tab has it, and in what order, is gm_tab_rows's (below).
 *
 * Up to GM_ROWS, the rows every game's menu has, or had: Continue, Save,
 * Load, Quick Slot, Display, Shader, Controls, Achievements, Reset, Quit.
 * Quick Slot (2026-11-07) is the slot SELECT+R1 saves to and SELECT+L1 loads
 * from, 1 to GM_SLOTS, kept per system; a system whose core keeps no state
 * (controls.h, ctl_has_states) has no such row. Shader (2026-11-12) is how
 * the picture is drawn at its last step, kept for the game's system.
 * GM_CONTROLS is in no game's tab now: Controls is a tab of its own, whose
 * rows are src/ctltab.c's, and the read-only pages the row opened are behind
 * that tab's Hotkeys row (All Controls). Over a program whose buttons
 * ZyperOS does not hand it (OpenBOR, a port, PortMaster) it is the Controls
 * tab's one row, All Controls, those pages.
 *
 * After GM_ROWS, the rows only some games have, so that every other game's
 * rows keep their ids: a DS game's Screens (the layout), Swap Screens and
 * Stylus, on the Display tab after Shader; an N64 game's Pak (2026-11-07,
 * the Rumble Pak or the Controller Pak, kept for the game), which is a row
 * of the Controls tab (src/ctltab.c) and in no tab here; and Player 2
 * (2026-11-13, src/p2.h), an NES game's whose core the system table offers
 * it on, on the Game tab after Quick Slot; and Disc (2026-11-13, issue #102),
 * a game on more than one disc's, on the Game tab after Quick Slot too; and
 * Strength (2026-11-14, src/strength.h), how strongly an old screen's shader
 * is drawn, on the Display tab right under Shader while the shader is one;
 * and Undo Load (1.0.6, SLOT_UNDO), on the Game tab right under Load while
 * the game has a state from before its last load. */
typedef enum {
	GM_CONTINUE, GM_SAVE, GM_LOAD, GM_QUICK, GM_DISPLAY, GM_SHADER, GM_CONTROLS, GM_CHEEVOS,
	GM_RESET, GM_QUIT,
	GM_ROWS,
	GM_SCREENS = GM_ROWS, GM_SWAP, GM_STYLUS, GM_PAK, GM_PLAYER2, GM_DISC,
	GM_STRENGTH, GM_UNDO,
	/* On-Screen Stats over the game (src/hud.h), on or off, and A opens
	 * its page (src/hudmenu.h): the Display tab's last row */
	GM_HUD,
	/* Border (src/border.h), under Display, while the mode leaves room */
	GM_BORDER,
	/* Handheld Color, under Border while it is a Handheld frame that comes
	 * in more than one color (the Game Boy Color's) */
	GM_BORDER_COLOR,
	/* Cheats (src/cheats.h, DESIGN.md 11.66), on the Game tab right over
	 * Achievements: how many are on, and A opens the game's list */
	GM_CHEATS,
	/* Screen Colors (src/panelgamma.h, DESIGN.md 11.42), the whole
	 * screen's, on the Display tab right over On-Screen Stats where the
	 * display has a table: its value the look, and A opens MENU >
	 * Display's own page over the paused game (1.1.0) */
	GM_SCREEN_COLORS,
	/* Link Cable (src/link.h, DESIGN.md 11.72), a Game Boy or Game Boy
	 * Color game's on Gambatte, on the Game tab after Player 2's place:
	 * Off, Waiting, Linked or Lost, and A opens its page; quiet, Needs
	 * Wi-Fi, with no adapter in */
	GM_LINK,
	GM_MAX_ROWS
} gm_row;

/* The Player 2 row's value for its state: off (a start that failed too),
 * waiting for a friend, or one connected */
const char *gm_player2_value(p2_state st);

/* The Link Cable row's value for its state (src/link.h): Waiting while this
 * handheld hosts, Linked, Lost after the other one went; Off otherwise */
const char *gm_link_value(link_state st);

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
	/* the display mode's label; what each mode does is said in its list,
	 * beside a picture of it (menu_pick_hover), not under the row */
	const char *dmode;
	const char *shader;   /* the shader's label; NULL reads Off */
	/* Its strength, "50%", when it is an old screen; NULL: no Strength row */
	const char *strength;
	/* Border's value (src/border.h) while the display mode leaves room
	 * around the game, and no Super Game Boy border fills it; NULL: no row */
	const char *border;
	/* Handheld Color's value under it; NULL: no row */
	const char *border_color;
	/* On-Screen Stats, the player's (HUD_KEY), whatever the game */
	bool        hud;
	/* Screen Colors' look, as MENU > Display's row says it, where the
	 * display has a table (plat_screen_colors_here); NULL: no row */
	const char *screen_colors;
	int         earned;
	int         total;    /* 0: this game has no achievement set */
	/* ...because it was never played with a network: not asked yet */
	bool        unchecked;
	/* ...or because it never will have one, here: as game_info's */
	ra_hash_kind no_hash;
	/* Two screens in this game's frame, and how they are shown now. */
	bool        screens;
	int         layout;   /* into gm_layout_names */
	bool        swap;
	bool        stylus;
	/* A program of its own under the menu, not a game Diatom runs: OpenBOR,
	 * a port, your PICO-8 (src/portmenu.h). Paused by stopping it, so there
	 * is nothing to save or load, and no achievements; what more it answers
	 * to, the flags below say. */
	bool        port;
	/* ...that has not drawn yet: Continue reads Keep Waiting (1.0.6) */
	bool        waiting;
	/* ...whose picture follows the Display row (your PICO-8,
	 * src/portmenu.h's port_menu_display): the Display row, after Continue,
	 * `dmode` its value. A picks a mode from its list, which starts the
	 * program again after a question; Left and Right step nothing */
	bool        port_display;
	/* ...over which portkeys draws On-Screen Stats (your PICO-8,
	 * port_menu_overlays): its row after Display, as over a game, On or
	 * Off on Left and Right, A its page; portkeys reads it as the menu
	 * closes */
	bool        port_hud;
	/* ...that restarts a cart on a key of its own (your PICO-8, src/portmenu.h's
	 * port_resets) and has drawn: the Reset row, before Quit, asked first */
	bool        port_reset;
	/* ...whose buttons ZyperOS hands it as it starts (your PICO-8,
	 * src/p8pad.h): the Controls tab is a game's, src/ctltab.c's rows. Over
	 * any other program it is All Controls alone, the read-only pages */
	bool        port_buttons;
	/* The quick slot, 1 to GM_SLOTS; 0 where the system keeps no states */
	int         quick;
	/* The game has a state from before its last load (SLOT_UNDO): the Undo
	 * Load row */
	bool        undo;
	/* A Nintendo 64 game's Pak, as gm_pak_name says it; NULL for no row */
	const char *pak;
	/* Player 2 is offered on this game's core (p2_offered), and its row's
	 * value (gm_player2_value) */
	bool        player2;
	const char *player2_value;
	/* The Link Cable is offered on this game's core (link_offered), its
	 * row's value (gm_link_value), and whether it is quiet: no Wi-Fi
	 * adapter in, the value then Needs Wi-Fi */
	bool        link;
	const char *link_value;
	bool        link_quiet;
	/* The discs of a game on more than one (Diatom's DISC answer): how many
	 * (under 2: no Disc row), the one in the tray, 0 based, and the core's
	 * name for it, NULL or empty for none. disc_fits says whether a label
	 * fits the row's value column, measured by the caller with the font;
	 * NULL says it does. */
	int         discs;
	int         disc;
	const char *disc_label;
	bool      (*disc_fits)(const char *label);
	/* The Cheats row: there for every game Diatom runs (not a port), and
	 * how many of the game's cheats are on now, 0 for None */
	bool        cheats;
	int         cheats_on;
	/* A cheat has been on in this game since it started: its achievements
	 * stop until the next start with none (Diatom's cheats.h), and the
	 * Achievements row says Paused */
	bool        cheats_paused;
	/* Each option row's choices by its gm_row (menu.h's menu_choice), which
	 * the screen fills and gm_rows copies to its rows' places. NULL: none. */
	const menu_choice *choice;
} gm_ui;

/* The Disc row's value: the core's label when `fits` says it fits (NULL: it
 * does), else "Disc N of M", N from 1. Into `out`, which it returns. */
const char *gm_disc_value(const gm_ui *u, char *out, size_t n, bool (*fits)(const char *));
/* The disc `d` (+1 or -1) steps to from `disc` of `discs`, round */
int gm_disc_step(int disc, int discs, int d);

/* The in-game menu is in tabs, MENU's tab row (DESIGN.md
 * 11.45): Game, Display and Controls,
 * L1 and R1 going round them. gm_tab_name is a tab's name in the language
 * that is on, the first for a tab past the ends.
 *
 * gm_tab_rows says which rows a tab has, in the order shown:
 *   Game      Continue, Save, Load, Quick Slot (a system that keeps states),
 *             Disc (a game on more than one disc), Player 2 (an NES game),
 *             Cheats, Achievements, Reset, Quit
 *   Display   Display, Shader, Strength (an old screen's shader), and a DS
 *             game's Screens, Swap Screens, Stylus
 *   Controls  none from here: its rows are src/ctltab.c's
 *   Achievements  none from here either: the game's list, or the sentence
 *             that says why there is none, are src/achtab.c's (1.0.5).
 *             The Game tab's Achievements row stays, its count at a
 *             glance, and A on it turns to this tab, the one L1 would
 * GM_PAK is in no tab: Pak is one of the Controls tab's rows.
 *
 * Over a program of its own (u->port: your PICO-8, OpenBOR, a port,
 * PortMaster, the standalone Flycast) the same tabs, the rows the stopped
 * program can answer to in a game's places, and no Achievements tab
 * (gm_tab_count), as none earns any (1.1.0-rc.8, mine: "why pico 8 menu has
 * no tabs? we should use the same component as in other systems", "openbor
 * is also missing the menu as we have in other systems", "yeah have the
 * same menu in ports as well"):
 *   Game      Continue (Keep Waiting while it starts), Reset (your PICO-8
 *             once it has drawn: u->port_reset), Quit
 *   Display   your PICO-8's picture rows (u->port_display): Display, Border
 *             and Strength where they would show in a game, Shader; Screen
 *             Colors (a display with a table), the panel's, over any
 *             program; On-Screen Stats where portkeys draws it (u->port_hud)
 *   Controls  src/ctltab.c's over your PICO-8 (u->port_buttons); over any
 *             other program All Controls (GM_CONTROLS), its read-only pages
 * What a program cannot do is not there at all, not greyed: Save, Load,
 * Undo Load, Quick Slot, Disc, Player 2, Cheats, Achievements. */
typedef enum { GT_GAME, GT_DISPLAY, GT_CONTROLS, GT_ACHIEVEMENTS, GT_TABS } gm_tab;

/* How many tabs L1 and R1 go round, from GT_GAME: all of them over a game,
 * all but Achievements over a program of its own (the last, so the others
 * keep their places) */
int gm_tab_count(const gm_ui *u);
/* Whether the Controls tab's rows are src/ctltab.c's, the buttons to change:
 * a game's, and your PICO-8's; else they are gm_tab_rows's All Controls */
bool gm_ctltab(const gm_ui *u);

/* On-Screen Stats' switch in the device's database, kept per player and
 * not per game: 1 draws the line over every game (src/hud.h, Diatom's
 * SETHUD); what it shows and how are its page's (src/hudmenu.h) */
#define HUD_KEY "hud"
const char *gm_tab_name(int t);
int gm_tab_rows(const gm_ui *u, gm_tab t, gm_row *ids);
/* Where row `id` is in tab `t`'s list, or -1 when that tab has none */
int gm_row_at(const gm_ui *u, gm_tab t, gm_row id);
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
	char cheevos[64];   /* as game_info's: a value in any language, whole */
	char quick[8];
	char disc[160];
	char cheats[32];
	unsigned char sound[GM_MAX_ROWS];
	menu_choice choice[GM_MAX_ROWS];   /* by place, from gm_ui's choice */
} gm_bufs;

/* Tab `t`'s rows, built from gm_tab_rows's list */
int gm_rows(const gm_ui *u, gm_tab t, menu_row *out, gm_bufs *b);

#endif
