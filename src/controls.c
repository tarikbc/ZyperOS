/* SPDX-License-Identifier: GPL-3.0-only */
/* See controls.h. The pages, and nothing else. */
#include "controls.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "padglyph.h"

static bool fn_volume;        /* see ctl_set_fn_volume */
static bool battery_lights;   /* see ctl_set_battery_lights */

void ctl_set_fn_volume(bool on) { fn_volume = on; }
void ctl_set_battery_lights(bool on) { battery_lights = on; }

/* The rows the home's pages and a game's In a game page both say, written
 * once so the two cannot come to disagree. A button is named as it is
 * printed, in every language; what it does is in the language that is on. */
static menu_row row_menu(void)   { return (menu_row){ "MENU", T(S_CONTROLS_INGAME_MENU), false }; }
static menu_row row_mp4(void)    { return (menu_row){ "SELECT", T(S_CONTROLS_MP4_MENU), false }; }
static menu_row row_save_off(void) { return (menu_row){ "POWER", T(S_CONTROLS_SAVE_OFF), false }; }
static menu_row row_volume(void)
{
	return (menu_row){ T(S_CONTROLS_KEY_VOLUME), T(S_CONTROLS_SOUND), false };
}
/* The Pixel 2's chord by the names its glyphs carry, MENU and VOL, as the
 * tutorial teaches it (DESIGN.md 12: chords with a plus) */
static menu_row row_brightness(void)
{
	return (menu_row){ fn_volume ? "MENU+VOL" : "F1/F2", T(S_CONTROLS_BRIGHTNESS), false };
}

static const str_id NAMES[CTL_PAGES] = {
	[CTL_MOVING]   = S_CONTROLS_PAGE_MOVING,
	[CTL_SHELF]    = S_CONTROLS_PAGE_SHELF,
	[CTL_GAME]     = S_CONTROLS_PAGE_GAME,
	[CTL_HOTKEYS]  = S_CONTROLS_PAGE_HOTKEYS,
	[CTL_MUSE]     = S_CONTROLS_PAGE_MUSE,
	[CTL_ANYWHERE] = S_CONTROLS_PAGE_ANYWHERE,
};

const char *ctl_page_name(ctl_page p)
{
	return T(NAMES[(unsigned)p < CTL_PAGES ? p : CTL_MOVING]);
}

/* A PAIR OF BUTTONS IS WRITTEN "L1/R1", with no air around the slash. Eric's,
 * 2026-09-20: spaced out it reads as two separate things rather than one
 * control with two ends, and it is not how he writes them.
 *
 * EVERY PAGE FITS, so none of them moves while it is being read.
 *
 * The panel holds six rows under a one-line heading, with the page count in
 * the heading and no footer (DESIGN.md 11.3), measured with the font by
 * --controls-fit: a cursorless list that does not fit scrolls itself, which
 * is right for a synopsis and wrong for a table you are trying to read a
 * button off. With a footer it held four, and the five-row pages here
 * scrolled until 2026-11-06.
 *
 * That ceiling is why the shelf is two pages. Its seven buttons split where
 * the hand does: the pad and the shoulders get you around, and the four face
 * buttons do something to what you have landed on. */
static int moving(menu_row *out)
{
	int n = 0;

	/* The d-pad's directions are not printed on it, so they are words */
	out[n++] = (menu_row){ T(S_CONTROLS_KEY_LEFT_RIGHT), T(S_CONTROLS_MOVE), false };
	out[n++] = (menu_row){ T(S_CONTROLS_KEY_UP_DOWN), T(S_CONTROLS_JUMP_LETTER), false };
	/* Three systems on the home, a screenful of games on a shelf (
	 * 2026-11-06), in one row as the guide has it */
	out[n++] = (menu_row){ "L1/R1", T(S_CONTROLS_JUMP_SYSTEMS), false };
	/* The songs, on the home and the shelf alike: MP4's while it plays
	 * behind them, else the menu music's. Here and not on the shelf's page,
	 * which the Pixel 2's L2+R2 fills. */
	out[n++] = (menu_row){ "L2/R2", T(S_CONTROLS_SONGS), false };
	return n;
}

static int shelf(menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "A", T(S_CONTROLS_OPEN_PLAY), false };
	out[n++] = (menu_row){ "B", T(S_CONTROLS_BACK), false };
	out[n++] = (menu_row){ "X", T(S_CONTROLS_DETAILS), false };
	out[n++] = (menu_row){ "Y", T(S_CONTROLS_FAVORITE), false };
	/* Here rather than on Anywhere, which is full: it works outside a game,
	 * and in one L2 and R2 are the game's */
	if (battery_lights)
		out[n++] = (menu_row){ "L2+R2", T(S_CONTROLS_BATTERY), false };
	return n;
}

static int game(menu_row *out)
{
	int n = 0;

	/* MENU is the in-game menu, which opens on release. Fast forward rode on
	 * its row as MENU+R1 until 2026-11-07, when it moved to SELECT+R2 and
	 * SELECT+L2, on the Hotkeys page after this one. Eric's wording,
	 * 2026-10-01, SELECT's with it. */
	out[n++] = row_menu();
	out[n++] = row_mp4();
	/* X and Y are Diatom's turbo on the nine two-button systems; L2 and R2
	 * are mGBA's own Turbo L and R, which work on GBA and do nothing on GB and
	 * GBC, which have no shoulders (found 2026-09-30, choosing a fast-forward
	 * button). One row for both, since the page is full. */
	out[n++] = (menu_row){ "X/Y/L2/R2", T(S_CONTROLS_TURBO_ROW), false };
	out[n++] = row_save_off();
	/* The exceptions as a note rather than a longer row: X and Y are the pad's
	 * own buttons on Genesis and SNES, and L2 and R2 only do anything on GBA.
	 * Kept short - a note runs the width of the panel and is cut at about
	 * thirty characters. An asterisk on both ends, the row's value and this
	 * line, so the two read as a footnote and its mark rather than as two
	 * separate claims. Eric's, 2026-09-20; reworded by him 2026-10-01. */
	out[n++] = MENU_NOTE(T(S_CONTROLS_NOTE_SOME));
	return n;
}

/* The hotkeys: SELECT held and a shoulder (diatom/src/chord.h), in a game
 * whatever the system. The quick slot is the in-game menu's Quick Slot row,
 * one a system. The note is the rule that keeps SELECT a game button: a
 * press with no shoulder after it is the game's. Shared with a game's own
 * Hotkeys page (hotkeys_for). */
static int hotkey_rows(menu_row *out, bool states, bool select_note)
{
	int n = 0;

	if (states) {
		out[n++] = (menu_row){ "SELECT+R1", T(S_CONTROLS_QUICK_SAVE), false };
		out[n++] = (menu_row){ "SELECT+L1", T(S_CONTROLS_QUICK_LOAD), false };
	}
	out[n++] = (menu_row){ "SELECT+R2", T(S_CONTROLS_FASTER), false };
	out[n++] = (menu_row){ "SELECT+L2", T(S_CONTROLS_SLOWER), false };
	if (select_note) out[n++] = MENU_NOTE(T(S_CONTROLS_NOTE_SELECT));
	return n;
}

static int hotkeys(menu_row *out)
{
	return hotkey_rows(out, true, true);
}

static int muse(menu_row *out)
{
	int n = 0;

	/* A on MP4's SHELF opens an album, which the shelf page already says.
	 * These are the player's own buttons, the ones a shelf does not have. */
	out[n++] = (menu_row){ "A",     T(S_CONTROLS_PLAY_PAUSE), false };
	out[n++] = (menu_row){ "L1/R1", T(S_CONTROLS_TRACK), false };
	/* Ten seconds a tap and further held - see seek_step in main.c. Said as
	 * what it does, since the numbers do not fit in a value. */
	out[n++] = (menu_row){ T(S_CONTROLS_KEY_LEFT_RIGHT), T(S_CONTROLS_SEEK), false };
	/* "Mode" rather than "Mode, on Now Playing": you press it on Now Playing,
	 * where the mark beside the track count changes in front of you. Eric's,
	 * 2026-09-20 - the rest of that sentence was saying what the screen was
	 * about to show anyway. "Or speed" since 2026-09-30: on a book, which
	 * plays in order, Y is its speed instead. */
	/* "Or view" since MP4 played video (2026-11-05): on a video, Y turns
	 * between full screen and the view with its controls. */
	out[n++] = (menu_row){ "Y",     T(S_CONTROLS_MODE), false };
	/* B is not here. It goes back in MP4 exactly as it does everywhere else,
	 * and the shelf's page already says so. */
	/* The lock screen, on X since 2026-11-05: every button blocked
	 * until SELECT and START are held, which the lock screen itself says. It
	 * took the row of the pocket lock's chord, F1+F2 held (MENU and both
	 * volume keys on the Pixel 2), which it replaced: one button anyone can
	 * find instead of three held together. */
	out[n++] = (menu_row){ "X",     T(S_CONTROLS_LOCK), false };
	return n;
}

static int anywhere(menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "SELECT", "MP4", false };   /* the player's name, in every language */
	/* MENU is here rather than on a shelf page because it is the same promise
	 * wherever you are: the settings for whatever is in front of you. In a
	 * game it is a different menu, and that page says so. */
	out[n++] = (menu_row){ "MENU",   T(S_CONTROLS_SETTINGS), false };
	out[n++] = row_volume();
	out[n++] = row_brightness();
	out[n++] = (menu_row){ "POWER",  T(S_CONTROLS_TURN_OFF), false };
	return n;
}

int ctl_rows(ctl_page p, menu_row *out)
{
	int n;

	switch (p) {
	case CTL_SHELF:    n = shelf(out);  break;
	case CTL_GAME:     n = game(out);        break;
	case CTL_HOTKEYS:  n = hotkeys(out);     break;
	case CTL_MUSE:     n = muse(out);        break;
	case CTL_ANYWHERE: n = anywhere(out);    break;
	default:           n = moving(out); break;
	}
	/* The page is one of several and says so at the heading's right, "2 / 5",
	 * as About does (controls_screen, ctl_page_count): every panel of pages,
	 * 2026-11-06 (docs/design/mock/controls-fit-lab.html). It said so
	 * in a footer, "Left/Right: more" under a rule (Eric's, 2026-09-20),
	 * which left four rows of room: In MP4, Anywhere and the Pixel 2's shelf
	 * page have five, and scrolled by themselves. Without it six fit. */
	return n;
}

/* ---------- in a game ------------------------------------------------------
 *
 * See controls.h. A system's buttons as its core names the RetroPad's,
 * measured 2026-11-06 by loading each pinned core in a linux/arm64 container
 * and keeping what RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS said for port 0,
 * and from libretro's documentation where a core says nothing before a real
 * game is loaded (Stella 2014, a5200, Opera, blueMSX, Mupen64Plus-Next,
 * Flycast, FinalBurn Neo, MAME 2003-Plus). Worded for the panel: the core's
 * "(FDS) Disk Side Change" is "Disk side", and a name a core gives a button
 * the system does not have - mGBA names L and R for the Game Boy too, Genesis
 * Plus GX a Genesis pad for the Master System - is left out. */

/* The d-pad last in the enum and first on a page: a system whose page says
 * what its d-pad is (the PICO-8's arrow buttons) has it as its first row. A
 * page says it only where the core makes the d-pad something to say; on the
 * rest it is the pad's own directions and goes without saying. */
enum { P_A, P_B, P_X, P_Y, P_L1, P_R1, P_L2, P_R2, P_START, P_SELECT, P_DPAD, P_COUNT };

/* The handheld's buttons, in the order a page lists them, by the names the
 * pages use and the names Diatom's maps use. The d-pad is words (DPAD_KEY),
 * and no map moves it. */
static const char *const KEY[P_COUNT] = {
	"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT", NULL
};
static const char *const SPEC[P_COUNT] = {
	"a", "b", "x", "y", "l1", "r1", "l2", "r2", "start", "select", "dpad"
};
static const int ORDER[P_COUNT] = {
	P_DPAD, P_A, P_B, P_X, P_Y, P_L1, P_R1, P_L2, P_R2, P_START, P_SELECT
};

/* A button as a core names it, the way a page says it: a name printed on
 * the system's own pad ("Start", "C", "II"), which is said as it is in
 * every language, as the handheld's own are; words ("Fire"); or words about
 * a printed name ("Button 1", "Turbo L"), a string of one %s. */
typedef struct {
	const char *name;
	str_id      words;
	bool        has_words;
} ctl_label;
#define BTN(s)          { (s), (str_id)0, false }
#define SAYS(id)        { NULL, (id), true }
#define SAYS_ON(id, s)  { (s), (id), true }

typedef struct {
	const char *tag;
	const char *ext;              /* only for a game of this extension */
	ctl_label   pad[P_COUNT];     /* what each RetroPad button is */
} ctl_pad;

static const ctl_pad PADS[] = {
	/* Stella 2014: B is the stick's button; the shoulders set the two
	 * difficulty switches, L1/R1 to A and L2/R2 to B */
	{ "A2600", NULL, { [P_B] = SAYS(S_CONTROLS_FIRE),
	                   [P_L1] = SAYS(S_CONTROLS_LEFT_SWITCH_A),
	                   [P_R1] = SAYS(S_CONTROLS_RIGHT_SWITCH_A),
	                   [P_L2] = SAYS(S_CONTROLS_LEFT_SWITCH_B),
	                   [P_R2] = SAYS(S_CONTROLS_RIGHT_SWITCH_B),
	                   [P_START] = BTN("Reset"), [P_SELECT] = BTN("Select") } },
	/* gw: SELECT steps through the game's menu (Game A, Game B, the
	 * time), START shows the game's help; A and B are its buttons */
	{ "GW", NULL, { [P_A] = SAYS(S_CONTROLS_GAME_BUTTON), [P_B] = SAYS(S_CONTROLS_GAME_BUTTON),
	                [P_START] = SAYS(S_CONTROLS_HELP), [P_SELECT] = SAYS(S_CONTROLS_GAME_MENU) } },
	/* a5200, its on-screen keypad (the default): L1 shows it, A presses
	 * the key it is on */
	{ "A5200", NULL, { [P_A] = SAYS_ON(S_CONTROLS_FIRE_N, "1"),
	                   [P_B] = SAYS_ON(S_CONTROLS_FIRE_N, "2"),
	                   [P_X] = SAYS_ON(S_CONTROLS_KEYPAD, "#"),
	                   [P_Y] = SAYS_ON(S_CONTROLS_KEYPAD, "*"),
	                   [P_L1] = SAYS(S_CONTROLS_SHOW_KEYPAD),
	                   [P_R1] = SAYS_ON(S_CONTROLS_KEYPAD, "0"),
	                   [P_L2] = SAYS_ON(S_CONTROLS_KEYPAD, "3"),
	                   [P_R2] = SAYS_ON(S_CONTROLS_KEYPAD, "1"),
	                   [P_START] = BTN("Start"), [P_SELECT] = BTN("Pause") } },
	{ "VECTREX", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                     [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                     [P_X] = SAYS_ON(S_CONTROLS_BUTTON, "3"),
	                     [P_Y] = SAYS_ON(S_CONTROLS_BUTTON, "4") } },
	/* FCEUmm: L and R turn and swap a Famicom Disk System disk, which only
	 * an .fds game has */
	{ "NES", "fds", { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = SAYS(S_CONTROLS_DISK_SIDE),
	                  [P_R1] = SAYS(S_CONTROLS_EJECT_DISK), [P_START] = BTN("Start"),
	                  [P_SELECT] = BTN("Select") } },
	{ "NES", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Select") } },
	/* An MSX joystick has two buttons; blueMSX puts the keyboard on the
	 * rest, which a game reads its own way */
	{ "MSX2", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                  [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "2") } },
	/* ProSystem: B is the stick's left button, A its right one */
	{ "A7800", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                   [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"), [P_X] = BTN("Reset"),
	                   [P_L1] = SAYS(S_CONTROLS_LEFT_DIFFICULTY),
	                   [P_R1] = SAYS(S_CONTROLS_RIGHT_DIFFICULTY),
	                   [P_START] = BTN("Pause"), [P_SELECT] = BTN("Select") } },
	/* Genesis Plus GX's two-button pad, buttons 1 and 2, START the pause */
	{ "SMS", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                 [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"), [P_START] = BTN("Pause") } },
	{ "GG", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"), [P_START] = BTN("Start") } },
	{ "GB", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_START] = BTN("Start"),
	                [P_SELECT] = BTN("Select") } },
	{ "GBC", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Select") } },
	/* mGBA's own Turbo L and R on L2 and R2 */
	{ "GBA", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = BTN("L"), [P_R1] = BTN("R"),
	                 [P_L2] = SAYS_ON(S_CONTROLS_TURBO, "L"),
	                 [P_R2] = SAYS_ON(S_CONTROLS_TURBO, "R"),
	                 [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* The six-button pad: A, B, C along the bottom, X, Y, Z above */
	{ "MD", NULL, { [P_A] = BTN("C"), [P_B] = BTN("B"), [P_X] = BTN("Y"), [P_Y] = BTN("A"),
	                [P_L1] = BTN("X"), [P_R1] = BTN("Z"), [P_START] = BTN("Start"),
	                [P_SELECT] = BTN("Mode") } },
	/* Beetle PCE Fast's pad; III to VI are the six-button pad's, which X and
	 * Y's turbo takes, so the two-button pad is the one said */
	{ "PCE", NULL, { [P_A] = BTN("I"), [P_B] = BTN("II"), [P_START] = BTN("Run"),
	                 [P_SELECT] = BTN("Select") } },
	{ "LYNX", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = BTN("Option 1"),
	                  [P_R1] = BTN("Option 2"), [P_START] = BTN("Pause"),
	                  [P_SELECT] = SAYS(S_CONTROLS_TURN_SCREEN) } },
	/* FinalBurn Neo's Neo Geo pad, as the Neo Geo CD's: A at the bottom */
	{ "NEOGEO", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_X] = BTN("D"), [P_Y] = BTN("C"),
	                    [P_START] = BTN("Start"), [P_SELECT] = SAYS(S_CONTROLS_COIN) } },
	{ "SFC", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_X] = BTN("X"), [P_Y] = BTN("Y"),
	                 [P_L1] = BTN("L"), [P_R1] = BTN("R"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Select") } },
	{ "SEGACD", NULL, { [P_A] = BTN("C"), [P_B] = BTN("B"), [P_X] = BTN("Y"), [P_Y] = BTN("A"),
	                    [P_L1] = BTN("X"), [P_R1] = BTN("Z"), [P_START] = BTN("Start"),
	                    [P_SELECT] = BTN("Mode") } },
	{ "32X", NULL, { [P_A] = BTN("C"), [P_B] = BTN("B"), [P_X] = BTN("Y"), [P_Y] = BTN("A"),
	                 [P_L1] = BTN("X"), [P_R1] = BTN("Z"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Mode") } },
	/* Opera: the 3DO's P is play and pause, its X stop */
	{ "3DO", NULL, { [P_A] = BTN("C"), [P_B] = BTN("B"), [P_Y] = BTN("A"), [P_L1] = BTN("L"),
	                 [P_R1] = BTN("R"), [P_START] = SAYS(S_CONTROLS_PLAY),
	                 [P_SELECT] = SAYS(S_CONTROLS_STOP) } },
	/* YabaSanshiro by position, the south button the Saturn's A; with
	 * map|SATURN each cap is the button it names, C and Z on L1 and R1 */
	{ "SATURN", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_X] = BTN("Y"), [P_Y] = BTN("X"),
	                    [P_L1] = BTN("C"), [P_R1] = BTN("Z"), [P_L2] = BTN("L"),
	                    [P_R2] = BTN("R"), [P_START] = BTN("Start") } },
	/* Beetle VB: the right d-pad's up and left, on L2 and R2; its down and
	 * right are on L3 and R3, which the handheld does not have */
	{ "VB", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = BTN("L"), [P_R1] = BTN("R"),
	                [P_L2] = SAYS(S_CONTROLS_RIGHT_PAD_UP),
	                [P_R2] = SAYS(S_CONTROLS_RIGHT_PAD_LEFT),
	                [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* The faces are shapes, drawn as the pad has them (padglyph.h), in every
	 * language: words until 2026-11-07 */
	{ "PS1", NULL, { [P_A] = BTN(PAD_CIRCLE), [P_B] = BTN(PAD_CROSS),
	                 [P_X] = BTN(PAD_TRIANGLE), [P_Y] = BTN(PAD_SQUARE),
	                 [P_L1] = BTN("L1"), [P_R1] = BTN("R1"), [P_L2] = BTN("L2"),
	                 [P_R2] = BTN("R2"), [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* NeoCD: the four buttons, and three of its presses at once */
	{ "NEOCD", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_X] = BTN("D"), [P_Y] = BTN("C"),
	                   [P_L1] = BTN("B+C"), [P_L2] = BTN("A+B"), [P_R2] = BTN("A+B+C"),
	                   [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* Mupen64Plus-Next: B the N64's A, Y its B, L2 its Z. The core makes the
	 * face buttons its C buttons while its R2 is held, and map|N64 puts that
	 * R2 on SELECT, which the N64 has none of (and takes R2's own away): the
	 * C buttons have a page of their own */
	{ "N64", NULL, { [P_B] = BTN("A"), [P_Y] = BTN("B"), [P_L1] = BTN("L"), [P_R1] = BTN("R"),
	                 [P_L2] = BTN("Z"), [P_START] = BTN("Start") } },
	/* Beetle NeoPop: the south button is the Pocket's A */
	{ "NGP", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_START] = BTN("Option") } },
	{ "NGPC", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_START] = BTN("Option") } },
	/* Beetle WonderSwan: the d-pad is the X pad, the shoulders the Y pad */
	{ "WS", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = SAYS(S_CONTROLS_Y_LEFT),
	                [P_R1] = SAYS(S_CONTROLS_Y_RIGHT), [P_L2] = SAYS(S_CONTROLS_Y_DOWN),
	                [P_R2] = SAYS(S_CONTROLS_Y_UP), [P_START] = BTN("Start"),
	                [P_SELECT] = SAYS(S_CONTROLS_TURN_SCREEN) } },
	{ "WSC", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_L1] = SAYS(S_CONTROLS_Y_LEFT),
	                 [P_R1] = SAYS(S_CONTROLS_Y_RIGHT), [P_L2] = SAYS(S_CONTROLS_Y_DOWN),
	                 [P_R2] = SAYS(S_CONTROLS_Y_UP), [P_START] = BTN("Start"),
	                 [P_SELECT] = SAYS(S_CONTROLS_TURN_SCREEN) } },
	/* flycast2021 by position, the triggers on L2 and R2; map|DC makes each
	 * cap the button it names and puts the triggers on L1 and R1 */
	{ "DC", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_X] = BTN("Y"), [P_Y] = BTN("X"),
	                [P_L2] = SAYS_ON(S_CONTROLS_TRIGGER, "L"),
	                [P_R2] = SAYS_ON(S_CONTROLS_TRIGGER, "R"), [P_START] = BTN("Start") } },
	/* The same core's arcades: button 1 at the bottom, SELECT the coin */
	{ "NAOMI", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                   [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                   [P_X] = SAYS_ON(S_CONTROLS_BUTTON, "4"),
	                   [P_Y] = SAYS_ON(S_CONTROLS_BUTTON, "3"),
	                   [P_L1] = SAYS_ON(S_CONTROLS_BUTTON, "5"),
	                   [P_R1] = SAYS_ON(S_CONTROLS_BUTTON, "6"),
	                   [P_START] = BTN("Start"), [P_SELECT] = SAYS(S_CONTROLS_COIN) } },
	{ "AW", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                [P_X] = SAYS_ON(S_CONTROLS_BUTTON, "4"),
	                [P_Y] = SAYS_ON(S_CONTROLS_BUTTON, "3"),
	                [P_R1] = SAYS_ON(S_CONTROLS_BUTTON, "5"),
	                [P_START] = BTN("Start"), [P_SELECT] = SAYS(S_CONTROLS_COIN) } },
	/* FreeJ2ME-Plus: a phone's keys; map|J2ME puts OK/Fire on the A cap */
	{ "J2ME", NULL, { [P_A] = BTN("Num 9"), [P_B] = BTN("Num 7"), [P_X] = BTN("Num 0"),
	                  [P_Y] = SAYS(S_CONTROLS_FIRE), [P_L1] = BTN("Num 1"),
	                  [P_R1] = BTN("Num 3"), [P_L2] = BTN("Num *"), [P_R2] = BTN("Num #"),
	                  [P_START] = SAYS(S_CONTROLS_RIGHT_SOFT),
	                  [P_SELECT] = SAYS(S_CONTROLS_LEFT_SOFT) } },
	/* ScummVM with config/coreopts.cfg's buttons (src/db.c) */
	{ "SCUMMVM", NULL, { [P_A] = SAYS(S_CONTROLS_CLICK), [P_B] = SAYS(S_CONTROLS_RIGHT_CLICK),
	                     [P_X] = SAYS(S_CONTROLS_SPACE), [P_Y] = SAYS(S_CONTROLS_SKIP_ESC),
	                     [P_L1] = SAYS(S_CONTROLS_RETURN),
	                     [P_R1] = SAYS(S_CONTROLS_SLOW_POINTER),
	                     [P_R2] = SAYS(S_CONTROLS_SLOW_POINTER),
	                     [P_START] = SAYS(S_CONTROLS_SCUMMVM_MENU),
	                     [P_SELECT] = SAYS(S_CONTROLS_KEYBOARD) } },
	/* PokeMini: its own turbo on X, the shake on L1, C on R1 */
	{ "PKMINI", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_X] = SAYS_ON(S_CONTROLS_TURBO, "A"),
	                    [P_L1] = SAYS(S_CONTROLS_SHAKE), [P_R1] = BTN("C"),
	                    [P_SELECT] = SAYS(S_CONTROLS_POWER) } },
	/* fake-08: PICO-8's arrow buttons on the d-pad, its O at the bottom
	 * and X beside it, START its pause menu - every button it reads
	 * (fake-08's libretro.cpp at the pin: the d-pad, B, A and START) */
	{ "PICO8", NULL, { [P_DPAD] = SAYS(S_CONTROLS_ARROWS),
	                   [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "X"),
	                   [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "O"),
	                   [P_START] = SAYS(S_CONTROLS_PAUSE_MENU) } },
	/* DSperate as the DS's pad; L2 and R2 are Diatom's, on the page after
	 * (EXTRAS) */
	{ "NDS", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_X] = BTN("X"), [P_Y] = BTN("Y"),
	                 [P_L1] = BTN("L"), [P_R1] = BTN("R"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Select") } },
	/* OpenBOR's own layout for a pad SDL knows (its sdl/control.c), on the
	 * buttons SDL names A, B, X and Y, which on the Pixel 2 are the caps that
	 * say so; a module can change them in OpenBOR's options */
	{ "OPENBOR", NULL, { [P_A] = SAYS(S_CONTROLS_ATTACK), [P_B] = SAYS(S_CONTROLS_JUMP),
	                     [P_X] = SAYS_ON(S_CONTROLS_ATTACK_N, "2"),
	                     [P_Y] = SAYS(S_CONTROLS_SPECIAL),
	                     [P_L1] = SAYS_ON(S_CONTROLS_ATTACK_N, "3"),
	                     [P_R1] = SAYS_ON(S_CONTROLS_ATTACK_N, "4"),
	                     [P_START] = SAYS(S_CONTROLS_PAUSE),
	                     [P_SELECT] = SAYS(S_CONTROLS_SCREENSHOT) } },
	/* FinalBurn Neo and MAME 2003-Plus: a game's first button at the bottom,
	 * as mainline MAME's pad layout has it */
	{ "ARCADE", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                    [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                    [P_X] = SAYS_ON(S_CONTROLS_BUTTON, "4"),
	                    [P_Y] = SAYS_ON(S_CONTROLS_BUTTON, "3"),
	                    [P_L1] = SAYS_ON(S_CONTROLS_BUTTON, "5"),
	                    [P_R1] = SAYS_ON(S_CONTROLS_BUTTON, "6"),
	                    [P_START] = BTN("Start"), [P_SELECT] = SAYS(S_CONTROLS_COIN) } },
	{ "MAME", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "2"),
	                  [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "1"),
	                  [P_X] = SAYS_ON(S_CONTROLS_BUTTON, "4"),
	                  [P_Y] = SAYS_ON(S_CONTROLS_BUTTON, "3"),
	                  [P_L1] = SAYS_ON(S_CONTROLS_BUTTON, "5"),
	                  [P_R1] = SAYS_ON(S_CONTROLS_BUTTON, "6"),
	                  [P_START] = BTN("Start"), [P_SELECT] = SAYS(S_CONTROLS_COIN) } },
};
#define NPADS ((int)(sizeof PADS / sizeof PADS[0]))

/* Whether a pad has a button there */
static bool label_set(const ctl_label *l)
{
	return l->name || l->has_words;
}

/* A label as the page says it, in the language that is on, into out */
static void label_say(const ctl_label *l, char *out, size_t n)
{
	if (!l->has_words) snprintf(out, n, "%s", l->name ? l->name : "");
	else if (!l->name) snprintf(out, n, "%s", T(l->words));
	else snprintf(out, n, T(l->words), l->name);
}

/* A page a system adds after its buttons */
typedef struct {
	const char *tag;
	str_id      head;
	struct { ctl_label key; str_id does; } rows[5];
	bool        has_note;
	str_id      note;
} ctl_extra;

static const ctl_extra EXTRAS[] = {
	/* SELECT held turns the face buttons into the C buttons, each the way
	 * it sits (Mupen64Plus-Next's defaults: C1 right, C2 left, C3 down, C4
	 * up): the core's own R2 held does it, and map|N64 puts that on SELECT,
	 * which the N64 has none of (R2 until 2026-11-07, SELECT's shoulders
	 * being the hotkeys'). Under them, the Pak the controller holds and where
	 * it is changed, "MENU > Pak: Rumble Pak" (ctl_game.accessory): it said
	 * "Rumble Pak, not Controller Pak" until 2026-11-07, when the Pak became
	 * the player's to choose, per game, and that read as a riddle. */
	{ "N64", S_CONTROLS_C_BUTTONS, { { BTN("SELECT+X"), S_CONTROLS_C_UP },
	                                 { BTN("SELECT+A"), S_CONTROLS_C_RIGHT },
	                                 { BTN("SELECT+B"), S_CONTROLS_C_DOWN },
	                                 { BTN("SELECT+Y"), S_CONTROLS_C_LEFT } },
	  true, S_CONTROLS_PAK_WHERE },
	/* Diatom's (screens.h): L2 the other screen, R2 the stylus, and while
	 * it is on the d-pad moves it and A touches. MENU+L1 switched the
	 * screens too until 2026-11-06 (My note: L2 does it). The d-pad is not
	 * printed on, so it is words. */
	{ "NDS", S_CONTROLS_TWO_SCREENS, { { BTN("L2"), S_CONTROLS_OTHER_SCREEN },
	                                   { BTN("R2"), S_CONTROLS_STYLUS },
	                                   { SAYS(S_CONTROLS_KEY_DPAD), S_CONTROLS_STYLUS_MOVES },
	                                   { BTN("A"), S_CONTROLS_STYLUS_TOUCH } },
	  false, (str_id)0 },
};
#define NEXTRAS ((int)(sizeof EXTRAS / sizeof EXTRAS[0]))

/* A page's rows: six, with the page count in the heading and no footer
 * (DESIGN.md 11.3), five with a note under them. A row more sends the page
 * past the panel, and a list with no cursor that does not fit scrolls itself
 * (menu_draw), which is wrong for a table read a button at a time. Measured
 * with the font, --controls-fit, 2026-11-06. */
#define LINES 6
#define VAL_MAX 22    /* tools/controls-check.c VALUE_MAX */
/* A row's button and value together, in characters: what sits on one line of
 * the standard panel beside each other. "START/SELECT  Start/Select" (24)
 * fits; "START/SELECT  Right/Left soft key" (31) ran past it on the Pixel 2,
 * 2026-11-06. The launcher measures every page with the font too
 * (--controls-fit, tools/controls-shots-check.sh). */
#define ROW_CHARS 26

static const ctl_pad *pad_for(const char *tag, const char *ext)
{
	int i;

	if (!tag) return NULL;
	for (i = 0; i < NPADS; i++) {
		if (strcmp(PADS[i].tag, tag)) continue;
		if (PADS[i].ext && (!ext || strcasecmp(PADS[i].ext, ext))) continue;
		return &PADS[i];
	}
	return NULL;
}

static const ctl_extra *extra_for(const char *tag)
{
	int i;

	for (i = 0; tag && i < NEXTRAS; i++)
		if (!strcmp(EXTRAS[i].tag, tag)) return &EXTRAS[i];
	return NULL;
}

bool ctl_game_known(const char *tag)
{
	return pad_for(tag, NULL) != NULL || pad_for(tag, "fds") != NULL;
}

bool ctl_has_states(const char *tag)
{
	static const char *const NONE[] = { "SCUMMVM", "J2ME" };
	size_t i;

	if (!tag) return false;
	for (i = 0; i < sizeof NONE / sizeof NONE[0]; i++)
		if (!strcmp(tag, NONE[i])) return false;
	return true;
}

static int spec_index(const char *s, size_t n)
{
	int i;

	for (i = 0; i < P_COUNT; i++)
		if (strlen(SPEC[i]) == n && !strncmp(SPEC[i], s, n)) return i;
	return -1;
}

/* A Diatom map, "x:a~3,l1:l2": each pair sends a button to another, the
 * "~n" a turbo. Later pairs win, as in Diatom (env.c). A pair naming a
 * button this list does not have (the d-pad, "none") is passed over. */
static void apply_map(const char *spec, int *to, bool *turbo)
{
	const char *p = spec;

	while (p && *p) {
		const char *end = strchr(p, ','), *colon, *tilde;
		size_t len = end ? (size_t)(end - p) : strlen(p);
		int from, dst;

		colon = memchr(p, ':', len);
		if (colon) {
			size_t tl;

			tilde = memchr(colon, '~', len - (size_t)(colon - p));
			tl = (tilde ? (size_t)(tilde - colon) : len - (size_t)(colon - p)) - 1;
			from = spec_index(p, (size_t)(colon - p));
			dst  = spec_index(colon + 1, tl);
			if (from >= 0 && dst >= 0) {
				to[from] = dst;
				turbo[from] = tilde != NULL;
			}
		}
		p = end ? end + 1 : NULL;
	}
}

bool ctl_pair_value(const char *a, const char *b, char *out, int cap)
{
	size_t la = strlen(a), lb = strlen(b), pre = 0, suf = 0, i;
	int n;

	if (!strcmp(a, b)) {
		n = snprintf(out, (size_t)cap, "%s", a);
		return n >= 0 && n < cap;
	}
	/* The words they share in front, up to a space: "Turbo A", "Turbo B" */
	for (i = 0; i < la && i < lb && a[i] == b[i]; i++)
		if (a[i] == ' ') pre = i + 1;
	/* or behind, from a space: "L trigger", "R trigger" */
	for (i = 1; i <= la && i <= lb && a[la - i] == b[lb - i]; i++)
		if (a[la - i] == ' ') suf = i;
	if (pre && pre < la && pre < lb)
		n = snprintf(out, (size_t)cap, "%s/%s", a, b + pre);
	else if (suf && suf < la && suf < lb)
		n = snprintf(out, (size_t)cap, "%.*s/%s", (int)(la - suf), a, b);
	else
		n = snprintf(out, (size_t)cap, "%s/%s", a, b);
	return n >= 0 && n < cap;
}

/* A value has room for the longest a language says: VAL_MAX is English's,
 * and what pairs two rows, in bytes */
typedef struct { char key[16]; char val[64]; int pair; } crow;

/* How two rows may become one */
enum { MERGE_ANY, MERGE_CLEAR, MERGE_PLAIN };

/* Rows i and i+1 as one, "L1/R1" and "X/Z", when both are there and are a
 * pair, the value fits, and it reads at a glance as `how` asks. For the
 * faces: MERGE_CLEAR takes the same names ("A/B  A/B") or words they share
 * ("X/Y  Turbo A/B"), MERGE_PLAIN names longer than a letter as well
 * ("Circle/Cross"). Two letters, "X/Y  Y/A", are a puzzle and never one. */
static bool pair_rows(crow *r, int *n, int pair, int how)
{
	int i;

	for (i = 0; i + 1 < *n; i++) {
		char v[64], k[16];
		bool same, shared, letters;

		if (r[i].pair != pair || r[i + 1].pair != pair) continue;
		snprintf(k, sizeof k, "%s/%s", r[i].key, r[i + 1].key);
		if (!ctl_pair_value(r[i].val, r[i + 1].val, v, (int)sizeof v) ||
		    (int)strlen(v) > VAL_MAX || (int)(strlen(k) + strlen(v)) > ROW_CHARS)
			return false;
		same = !strcmp(r[i].key, r[i].val) && !strcmp(r[i + 1].key, r[i + 1].val);
		shared = strlen(v) != strlen(r[i].val) + 1 + strlen(r[i + 1].val);
		letters = strlen(r[i].val) <= 2 || strlen(r[i + 1].val) <= 2;
		if (pair <= 1 && !same && !shared &&
		    (how == MERGE_CLEAR || letters))
			return false;
		snprintf(r[i].key, sizeof r[i].key, "%s", k);
		snprintf(r[i].val, sizeof r[i].val, "%s", v);
		r[i].pair = pair + 100;              /* paired, and never again */
		memmove(&r[i + 1], &r[i + 2], (size_t)(*n - i - 2) * sizeof *r);
		(*n)--;
		return true;
	}
	return false;
}

/* The four faces as one, when each is the button it names (Saturn, DS) */
static void quad_faces(crow *r, int *n)
{
	int i;

	for (i = 0; i + 1 < *n; i++) {
		if (r[i].pair != 100 || r[i + 1].pair != 101 ||
		    strcmp(r[i].key, r[i].val) || strcmp(r[i + 1].key, r[i + 1].val))
			continue;
		snprintf(r[i].key, sizeof r[i].key, "A/B/X/Y");
		snprintf(r[i].val, sizeof r[i].val, "A/B/X/Y");
		memmove(&r[i + 1], &r[i + 2], (size_t)(*n - i - 2) * sizeof *r);
		(*n)--;
		return;
	}
}

static bool is_face(const crow *r)
{
	return r->pair == 0 || r->pair == 1 || r->pair == 100 || r->pair == 101 ||
	       !strcmp(r->key, "A/B/X/Y");
}

/* A port's keymap as a pad (src/portdoc.h): each button the key gptokeyb
 * types for it, a key's printed name or words for the rest; the d-pad's
 * four one row, "Arrow keys" when they are the four arrows, else their keys
 * in the order up, down, left, right ("W/S/A/D"). Into *pd, whose names
 * point into the doc and into dpad[], the caller's. */
static const str_id KEY_WORD[] = {
	[PD_SPACE]        = S_CONTROLS_SPACE,
	[PD_ARROWS]       = S_CONTROLS_KEY_ARROWS,
	[PD_CLICK_LEFT]   = S_CONTROLS_CLICK,
	[PD_CLICK_RIGHT]  = S_CONTROLS_RIGHT_CLICK,
	[PD_CLICK_MIDDLE] = S_CONTROLS_MIDDLE_CLICK,
	[PD_UP_KEY]       = S_CONTROLS_ARROW_UP,
	[PD_DOWN_KEY]     = S_CONTROLS_ARROW_DOWN,
	[PD_LEFT_KEY]     = S_CONTROLS_ARROW_LEFT,
	[PD_RIGHT_KEY]    = S_CONTROLS_ARROW_RIGHT,
};

static bool key_set(const pd_key *k)
{
	return k->word != PD_NAME || k->name[0];
}

static ctl_label key_label(const pd_key *k)
{
	if (k->word == PD_NAME) return (ctl_label)BTN(k->name);
	return (ctl_label)SAYS(KEY_WORD[k->word]);
}

static void keymap_pad(const port_doc *d, ctl_pad *pd, char *dpad, size_t dn)
{
	static const int TO[PD_SELECT + 1] = {
		[PD_A] = P_A, [PD_B] = P_B, [PD_X] = P_X, [PD_Y] = P_Y, [PD_L1] = P_L1,
		[PD_R1] = P_R1, [PD_L2] = P_L2, [PD_R2] = P_R2, [PD_START] = P_START,
		[PD_SELECT] = P_SELECT
	};
	const pd_key *w = &d->keys[PD_UP];
	int b, set = 0;

	memset(pd, 0, sizeof *pd);
	pd->tag = "";
	for (b = 0; b <= PD_SELECT; b++)
		if (key_set(&d->keys[b])) pd->pad[TO[b]] = key_label(&d->keys[b]);
	for (b = 0; b < 4; b++) set += key_set(&w[b]);
	if (!set) return;
	if (w[0].word == PD_UP_KEY && w[1].word == PD_DOWN_KEY && w[2].word == PD_LEFT_KEY &&
	    w[3].word == PD_RIGHT_KEY) {
		pd->pad[P_DPAD] = (ctl_label)SAYS(S_CONTROLS_KEY_ARROWS);
		return;
	}
	/* The four by their keys, in the order up, down, left, right: a name as
	 * printed, a word in the language that is on, "-" for none */
	dpad[0] = '\0';
	for (b = 0; b < 4; b++) {
		const char *name = !key_set(&w[b]) ? "-"
		                 : w[b].word == PD_NAME ? w[b].name : T(KEY_WORD[w[b].word]);
		size_t at = strlen(dpad);

		snprintf(dpad + at, dn - at, "%s%s", b ? "/" : "", name);
	}
	pd->pad[P_DPAD] = (ctl_label)BTN(dpad);
}

/* The system's buttons as rows, before they are put on pages */
static int pad_rows(const ctl_game *g, crow *r)
{
	/* pairs: A/B 0, X/Y 1, L1/R1 2, L2/R2 3, START/SELECT 4; the d-pad
	 * none */
	static const int PAIR[P_COUNT] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5 };
	ctl_pad keymap;
	char dpad[48];
	const ctl_pad *pd = pad_for(g->tag, g->ext);
	int to[P_COUNT], k, n = 0;
	bool turbo[P_COUNT] = { false };

	/* A port's are its keymap's, and only the keymap's */
	if (g->doc) {
		if (!g->doc->nkeys || g->doc->portmaster) return 0;
		keymap_pad(g->doc, &keymap, dpad, sizeof dpad);
		pd = &keymap;
	}
	if (!pd) return 0;
	for (k = 0; k < P_COUNT; k++) to[k] = k;
	apply_map(g->map, to, turbo);
	apply_map(g->turbo, to, turbo);
	for (k = 0; k < P_COUNT; k++) {
		const int i = ORDER[k];
		const ctl_label *lab = &pd->pad[to[i]];
		char said[sizeof r[n].val];

		if (!label_set(lab)) continue;
		label_say(lab, said, sizeof said);
		snprintf(r[n].key, sizeof r[n].key, "%s", KEY[i] ? KEY[i] : T(S_CONTROLS_KEY_DPAD));
		if (turbo[i]) snprintf(r[n].val, sizeof r[n].val, T(S_CONTROLS_TURBO), said);
		else snprintf(r[n].val, sizeof r[n].val, "%s", said);
		r[n].pair = PAIR[i];
		n++;
	}
	return n;
}

/* A game's pages: the system's button pages, its own page, In a game. */
typedef struct {
	int sys;                  /* pages of buttons */
	int start[P_COUNT + 1];   /* each one's first row, and the end */
	bool extra;
	crow rows[P_COUNT];
	int nrows;
} plan;

/* Where the faces end, when they and the rest each fit a page; a d-pad row
 * before them goes with them */
static int face_cut(const crow *r, int n)
{
	int faces = n > 0 && r[0].pair == 5 ? 1 : 0;

	while (faces < n && is_face(&r[faces])) faces++;
	/* two at least: a page of one row is no page */
	return faces >= 2 && faces <= LINES && n - faces <= LINES ? faces : 0;
}

/* The buttons onto pages of LINES rows, in this order of preference, each
 * step only while they do not fit: START/SELECT paired, then the shoulders
 * (and the other shoulder pair with them, so L1/R1 and L2/R2 read alike);
 * then the faces where pairing them reads at a glance ("Turbo A/B"), the
 * four as one when each is its own name; then two pages, the faces on one
 * and the rest on the other; then faces with longer names paired
 * ("Circle/Cross"); then pages of LINES. The most used buttons stay apart
 * longest, and no pair is a puzzle. */
static void make_plan(const ctl_game *g, plan *pl)
{
	crow *r = pl->rows;
	int i, cut = 0, at;

	memset(pl, 0, sizeof *pl);
	pl->extra = extra_for(g->tag) != NULL;
	pl->nrows = pad_rows(g, r);
	if (pl->nrows > LINES) pair_rows(r, &pl->nrows, 4, MERGE_ANY);
	if (pl->nrows > LINES) pair_rows(r, &pl->nrows, 2, MERGE_ANY);
	if (pl->nrows > LINES) pair_rows(r, &pl->nrows, 3, MERGE_ANY);
	for (i = 0; i < pl->nrows; i++) {
		if (r[i].pair == 102) pair_rows(r, &pl->nrows, 3, MERGE_ANY);
		if (r[i].pair == 103) pair_rows(r, &pl->nrows, 2, MERGE_ANY);
	}
	if (pl->nrows > LINES) pair_rows(r, &pl->nrows, 1, MERGE_CLEAR);
	if (pl->nrows > LINES) pair_rows(r, &pl->nrows, 0, MERGE_CLEAR);
	if (pl->nrows > LINES) quad_faces(r, &pl->nrows);
	if (pl->nrows > LINES && !(cut = face_cut(r, pl->nrows))) {
		pair_rows(r, &pl->nrows, 1, MERGE_PLAIN);
		pair_rows(r, &pl->nrows, 0, MERGE_PLAIN);
		if (pl->nrows > LINES) cut = face_cut(r, pl->nrows);
	}
	if (pl->nrows <= 0) return;
	if (pl->nrows <= LINES) cut = pl->nrows;
	else if (!cut) cut = pl->nrows <= LINES * 2 ? (pl->nrows + 1) / 2 : LINES;
	pl->start[pl->sys++] = 0;
	for (at = cut; at < pl->nrows; at += LINES) pl->start[pl->sys++] = at;
	pl->start[pl->sys] = pl->nrows;
}

/* A game's pages, in order: each a kind, and which of its kind it is.
 *
 * A system's: its buttons, a page of its own (the extra), Hotkeys, In a
 * game. A program of its own (a "run:" one, src/portmenu.h) has no Hotkeys:
 * SELECT's chords are Diatom's, and it is not Diatom's game.
 *
 * A port's (ctl_game.doc): what its README says, its keymap (the "buttons"
 * pages, from the keymap's pad), In a game, and About last, the only page
 * that is prose. PortMaster's own app: its buttons, and Anywhere. */
enum { PG_SYS, PG_EXTRA, PG_HOTKEYS, PG_INGAME, PG_SAID, PG_PM, PG_ABOUT };
typedef struct { int kind, at; } page_ref;
#define PAGES_MAX 16

static int said_pages(const port_doc *d)
{
	return d->nsaid > 0 ? (d->nsaid + LINES - 1) / LINES : 0;
}

static int page_list(const ctl_game *g, const plan *pl, page_ref *out)
{
	int n = 0, i;

	if (g->doc && g->doc->portmaster) {
		out[n++] = (page_ref){ PG_PM, 0 };
		out[n++] = (page_ref){ PG_INGAME, 0 };
		return n;
	}
	for (i = 0; g->doc && i < said_pages(g->doc); i++) out[n++] = (page_ref){ PG_SAID, i };
	for (i = 0; i < pl->sys; i++) out[n++] = (page_ref){ PG_SYS, i };
	if (pl->extra) out[n++] = (page_ref){ PG_EXTRA, 0 };
	if (!g->port) out[n++] = (page_ref){ PG_HOTKEYS, 0 };
	out[n++] = (page_ref){ PG_INGAME, 0 };
	if (g->doc && (g->doc->inst[0] || g->doc->desc[0])) out[n++] = (page_ref){ PG_ABOUT, 0 };
	return n;
}

int ctl_game_pages(const ctl_game *g)
{
	plan pl;
	page_ref pr[PAGES_MAX];

	make_plan(g, &pl);
	return page_list(g, &pl, pr);
}

bool ctl_game_prose(const ctl_game *g, int page)
{
	plan pl;
	page_ref pr[PAGES_MAX];
	int n;

	make_plan(g, &pl);
	n = page_list(g, &pl, pr);
	return page >= 0 && page < n && pr[page].kind == PG_ABOUT;
}
/* Whether SELECT is one of the game's buttons, after its map: then the
 * Hotkeys page says a press of it alone is still the game's. Not on a
 * system with none (the N64's goes to its C buttons, the PICO-8 has none),
 * where it would be a rule about nothing. */
static bool select_is_games(const ctl_game *g)
{
	const ctl_pad *pd = pad_for(g->tag, g->ext);
	int to[P_COUNT], k;
	bool turbo[P_COUNT] = { false };

	if (!pd) return false;
	for (k = 0; k < P_COUNT; k++) to[k] = k;
	apply_map(g->map, to, turbo);
	return label_set(&pd->pad[to[P_SELECT]]);
}

/* In a game's own page: what works whatever the system, the hotkeys */
static int in_a_game(menu_row *out)
{
	int n = 0;

	/* SELECT is the game's own in a game; MP4 is in the menu this page is
	 * read from, which says so on the home's In a game page */
	out[n++] = row_menu();
	out[n++] = row_volume();
	out[n++] = row_brightness();
	out[n++] = row_save_off();
	return n;
}

/* The same over a program of its own (OpenBOR, a port): MENU is the menu
 * alone, and POWER ends the game, which has no state to come back to. A
 * port whose script runs gptokeyb has PortMaster's own way out as well:
 * SELECT, its hotkey here (res/portmaster/control.txt), with START, which
 * ends the game, and the shelf comes back. */
static int in_a_port(const ctl_game *g, menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "MENU", T(S_CONTROLS_MENU_PORT), false };
	if (g->doc && g->doc->gptokeyb && !g->doc->portmaster)
		out[n++] = (menu_row){ "SELECT+START", T(S_CONTROLS_QUIT_GAME), false };
	out[n++] = row_volume();
	out[n++] = row_brightness();
	out[n++] = (menu_row){ "POWER", T(S_CONTROLS_QUIT_OFF), false };
	return n;
}

/* PortMaster's own buttons, as its window reads them (its pylibs, 2026.06:
 * a list moves on Up/Down and a page at a time on L1/R1, A enters, installs
 * or shows a port, B goes back and on the main menu asks to quit, X filters
 * the list of every port, Y uninstalls an installed one). It swaps A and B,
 * and X and Y, only on a system that asks it to, which ZyperOS does not. */
static int portmaster_rows(menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ T(S_CONTROLS_KEY_UP_DOWN), T(S_CONTROLS_MOVE), false };
	out[n++] = (menu_row){ "L1/R1", T(S_CONTROLS_PM_PAGE), false };
	out[n++] = (menu_row){ "A", T(S_CONTROLS_PM_CHOOSE), false };
	out[n++] = (menu_row){ "B", T(S_CONTROLS_BACK), false };
	out[n++] = (menu_row){ "X", T(S_CONTROLS_PM_FILTERS), false };
	out[n++] = (menu_row){ "Y", T(S_CONTROLS_PM_UNINSTALL), false };
	return n;
}

/* A README row's d-pad directions, in the language that is on */
static const char *dirs_said(unsigned d)
{
	switch (d) {
	case PD_DIR_UP:                  return T(S_CONTROLS_KEY_UP);
	case PD_DIR_DOWN:                return T(S_CONTROLS_KEY_DOWN);
	case PD_DIR_LEFT:                return T(S_CONTROLS_KEY_LEFT);
	case PD_DIR_RIGHT:               return T(S_CONTROLS_KEY_RIGHT);
	case PD_DIR_UP | PD_DIR_DOWN:    return T(S_CONTROLS_KEY_UP_DOWN);
	case PD_DIR_LEFT | PD_DIR_RIGHT: return T(S_CONTROLS_KEY_LEFT_RIGHT);
	default:                         return T(S_CONTROLS_KEY_DPAD);
	}
}

static bool head_fits(const ctl_game *g, const char *head)
{
	return g->fits ? g->fits(g->ctx, head) : strlen(head) <= 19;
}

/* The heading, `fmt` with the page's name or the system's in it. A name too
 * wide beside the page count gives way to the home's short one:
 * "Controls: Game Boy Advance" is "Controls: GBA". The launcher measures (594
 * of the panel's width is left between two counts' room); without it, 19
 * characters, which every heading that fits measured is under but the
 * Ataris (2026-11-06). A port's title has no short one, and is cut, a
 * character at a time, with an ellipsis, until it fits. */
static void heading_as(const ctl_game *g, const char *fmt, const char *page, ctl_buf *b)
{
	char name[64];
	size_t n;
	int len = snprintf(b->head, sizeof b->head, fmt, page);

	if (len >= 0 && len < (int)sizeof b->head && head_fits(g, b->head)) return;
	if (!g->doc && g->short_name && g->short_name[0]) {
		snprintf(b->head, sizeof b->head, fmt, g->short_name);
		return;
	}
	if (!g->doc) return;
	snprintf(name, sizeof name, "%s", page);
	n = strlen(name);
	/* room for the ellipsis' three bytes and the end */
	if (n > sizeof name - 4) n = sizeof name - 4;
	while (n > 1) {
		/* a character off the end, and the spaces before it */
		do n--; while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80);
		while (n > 0 && name[n - 1] == ' ') n--;
		if (!n) break;
		memcpy(name + n, "\xe2\x80\xa6", 4);                 /* …, and the end */
		len = snprintf(b->head, sizeof b->head, fmt, name);
		if (len >= 0 && len < (int)sizeof b->head && head_fits(g, b->head)) return;
	}
}

static void heading(const ctl_game *g, const char *page, ctl_buf *b)
{
	heading_as(g, T(S_CONTROLS_HEADING), page, b);
}

int ctl_game_rows(const ctl_game *g, int page, menu_row *out, ctl_buf *b)
{
	plan pl;
	page_ref pr[PAGES_MAX];
	int pages, n = 0, i;
	const char *title = g->doc ? g->doc->title : g->name ? g->name : g->tag;

	make_plan(g, &pl);
	pages = page_list(g, &pl, pr);
	if (page < 0 || page >= pages) page = 0;

	b->note[0] = '\0';
	switch (pr[page].kind) {
	case PG_SAID: {
		/* What the port's README says each does, in its own words */
		int from = pr[page].at * LINES, to = from + LINES;

		heading(g, title, b);
		for (i = from; i < to && i < g->doc->nsaid; i++, n++) {
			const pd_row *r = &g->doc->said[i];

			out[n] = (menu_row){ r->dirs ? dirs_said(r->dirs) : r->key, r->said, false };
		}
		break;
	}
	case PG_SYS: {
		int from = pl.start[pr[page].at], to = pl.start[pr[page].at + 1];

		/* A port's: the keys its keymap types, under "Keys: Wordle" */
		if (g->doc) heading_as(g, T(S_CONTROLS_KEYS_HEADING), title, b);
		else heading(g, title, b);
		for (i = from; i < to; i++, n++) {
			snprintf(b->key[n], sizeof b->key[n], "%s", pl.rows[i].key);
			snprintf(b->val[n], sizeof b->val[n], "%s", pl.rows[i].val);
			out[n] = (menu_row){ b->key[n], b->val[n], false };
		}
		break;
	}
	case PG_EXTRA: {
		const ctl_extra *x = extra_for(g->tag);

		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), T(x->head));
		for (i = 0; i < (int)(sizeof x->rows / sizeof x->rows[0]) && label_set(&x->rows[i].key);
		     i++, n++) {
			label_say(&x->rows[i].key, b->key[n], sizeof b->key[n]);
			snprintf(b->val[n], sizeof b->val[n], "%s", T(x->rows[i].does));
			out[n] = (menu_row){ b->key[n], b->val[n], false };
		}
		if (x->has_note && x->note == S_CONTROLS_PAK_WHERE) {
			/* the menu path to the Pak row, and what it says now */
			snprintf(b->note, sizeof b->note, T(S_CONTROLS_PAK_WHERE), T(S_GAMEMENU_PAK),
			         g->accessory ? g->accessory : "Rumble Pak");
			out[n++] = MENU_NOTE(b->note);
		} else if (x->has_note) {
			out[n++] = MENU_NOTE(T(x->note));
		}
		break;
	}
	case PG_HOTKEYS:
		/* No quick save or load where the core keeps no state: SELECT+R1
		 * and SELECT+L1 are the game's there */
		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), ctl_page_name(CTL_HOTKEYS));
		n = hotkey_rows(out, ctl_has_states(g->tag), select_is_games(g));
		break;
	case PG_PM:
		heading(g, title, b);
		n = portmaster_rows(out);
		break;
	case PG_ABOUT:
		/* What to copy where, then what the port is: whole paragraphs, which
		 * the launcher wraps to the panel (ctl_game_prose) */
		heading_as(g, T(S_CONTROLS_ABOUT_HEADING), title, b);
		if (g->doc->inst[0]) out[n++] = MENU_BODY(g->doc->inst);
		if (g->doc->desc[0]) out[n++] = MENU_BODY(g->doc->desc);
		break;
	default:
		/* Anywhere's words in PortMaster, which is no game */
		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING),
		         ctl_page_name(g->doc && g->doc->portmaster ? CTL_ANYWHERE : CTL_GAME));
		n = g->port ? in_a_port(g, out) : in_a_game(out);
		break;
	}
	return n;
}
