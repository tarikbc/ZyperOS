/* SPDX-License-Identifier: GPL-3.0-only */
/* See controls.h. The pages, and nothing else. */
#include "controls.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"

static bool fn_volume;        /* see ctl_set_fn_volume */
static bool battery_lights;   /* see ctl_set_battery_lights */

void ctl_set_fn_volume(bool on) { fn_volume = on; }
void ctl_set_battery_lights(bool on) { battery_lights = on; }

/* The rows the home's pages and a game's In a game page both say, written
 * once so the two cannot come to disagree. A button is named as it is
 * printed, in every language; what it does is in the language that is on. */
static menu_row row_menu(void)   { return (menu_row){ "MENU", T(S_CONTROLS_MENU_FF), false }; }
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

	/* Fast forward rides on MENU's row, because a seventh row does not fit
	 * (PAGE_MAX in tools/controls-check.c). MENU+R1 steps 2x, 3x, 4x and back
	 * to normal, and MENU opens the menu on release so it can be held for it.
	 * Eric's wording, 2026-10-01, SELECT's with it. */
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

enum { P_A, P_B, P_X, P_Y, P_L1, P_R1, P_L2, P_R2, P_START, P_SELECT, P_COUNT };

/* The handheld's buttons, in the order a page lists them, by the names the
 * pages use and the names Diatom's maps use. */
static const char *const KEY[P_COUNT] = {
	"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT"
};
static const char *const SPEC[P_COUNT] = {
	"a", "b", "x", "y", "l1", "r1", "l2", "r2", "start", "select"
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
	{ "PS1", NULL, { [P_A] = SAYS(S_CONTROLS_CIRCLE), [P_B] = SAYS(S_CONTROLS_CROSS),
	                 [P_X] = SAYS(S_CONTROLS_TRIANGLE), [P_Y] = SAYS(S_CONTROLS_SQUARE),
	                 [P_L1] = BTN("L1"), [P_R1] = BTN("R1"), [P_L2] = BTN("L2"),
	                 [P_R2] = BTN("R2"), [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* NeoCD: the four buttons, and three of its presses at once */
	{ "NEOCD", NULL, { [P_A] = BTN("B"), [P_B] = BTN("A"), [P_X] = BTN("D"), [P_Y] = BTN("C"),
	                   [P_L1] = BTN("B+C"), [P_L2] = BTN("A+B"), [P_R2] = BTN("A+B+C"),
	                   [P_START] = BTN("Start"), [P_SELECT] = BTN("Select") } },
	/* Mupen64Plus-Next: B the N64's A, Y its B, L2 its Z; R2 held makes the
	 * face buttons the C buttons, which have a page of their own */
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
	/* fake-08: PICO-8's O at the bottom, X beside it */
	{ "PICO8", NULL, { [P_A] = SAYS_ON(S_CONTROLS_BUTTON, "X"),
	                   [P_B] = SAYS_ON(S_CONTROLS_BUTTON, "O"), [P_START] = BTN("Pause") } },
	/* DSperate as the DS's pad; L2 and R2 are Diatom's, on the page after
	 * (EXTRAS) */
	{ "NDS", NULL, { [P_A] = BTN("A"), [P_B] = BTN("B"), [P_X] = BTN("X"), [P_Y] = BTN("Y"),
	                 [P_L1] = BTN("L"), [P_R1] = BTN("R"), [P_START] = BTN("Start"),
	                 [P_SELECT] = BTN("Select") } },
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
	/* R2 held turns the face buttons into the C buttons, each the way it
	 * sits (Mupen64Plus-Next's defaults: C1 right, C2 left, C3 down, C4
	 * up); and the controller holds a Rumble Pak (src/db.c) */
	{ "N64", S_CONTROLS_C_BUTTONS, { { BTN("R2+X"), S_CONTROLS_C_UP },
	                                 { BTN("R2+A"), S_CONTROLS_C_RIGHT },
	                                 { BTN("R2+B"), S_CONTROLS_C_DOWN },
	                                 { BTN("R2+Y"), S_CONTROLS_C_LEFT } },
	  true, S_CONTROLS_RUMBLE },
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

/* The system's buttons as rows, before they are put on pages */
static int pad_rows(const ctl_game *g, crow *r)
{
	const ctl_pad *pd = pad_for(g->tag, g->ext);
	/* pairs: A/B 0, X/Y 1, L1/R1 2, L2/R2 3, START/SELECT 4 */
	static const int PAIR[P_COUNT] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4 };
	int to[P_COUNT], i, n = 0;
	bool turbo[P_COUNT] = { false };

	if (!pd) return 0;
	for (i = 0; i < P_COUNT; i++) to[i] = i;
	apply_map(g->map, to, turbo);
	apply_map(g->turbo, to, turbo);
	for (i = 0; i < P_COUNT; i++) {
		const ctl_label *lab = &pd->pad[to[i]];
		char said[sizeof r[n].val];

		if (!label_set(lab)) continue;
		label_say(lab, said, sizeof said);
		snprintf(r[n].key, sizeof r[n].key, "%s", KEY[i]);
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

/* Where the faces end, when they and the rest each fit a page */
static int face_cut(const crow *r, int n)
{
	int faces = 0;

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

int ctl_game_pages(const ctl_game *g)
{
	plan pl;

	make_plan(g, &pl);
	return pl.sys + (pl.extra ? 1 : 0) + 1;
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

static void heading(const ctl_game *g, const char *page, ctl_buf *b)
{
	/* A name too wide beside the page count gives way to the home's short
	 * one: "Controls: Game Boy Advance" is "Controls: GBA". The launcher
	 * measures (594 of the panel's width is left between two counts'
	 * room); without it, 19 characters, which every heading that fits
	 * measured is under but the Ataris (2026-11-06). */
	bool fits;

	snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), page);
	fits = g->fits ? g->fits(g->ctx, b->head) : strlen(b->head) <= 19;
	if (!fits && g->short_name && g->short_name[0])
		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), g->short_name);
}

int ctl_game_rows(const ctl_game *g, int page, menu_row *out, ctl_buf *b)
{
	plan pl;
	int pages, n = 0, i;

	make_plan(g, &pl);
	pages = pl.sys + (pl.extra ? 1 : 0) + 1;
	if (page < 0 || page >= pages) page = 0;

	if (page < pl.sys) {
		int from = pl.start[page], to = pl.start[page + 1];

		heading(g, g->name ? g->name : g->tag, b);
		for (i = from; i < to; i++, n++) {
			snprintf(b->key[n], sizeof b->key[n], "%s", pl.rows[i].key);
			snprintf(b->val[n], sizeof b->val[n], "%s", pl.rows[i].val);
			out[n] = (menu_row){ b->key[n], b->val[n], false };
		}
	} else if (pl.extra && page == pl.sys) {
		const ctl_extra *x = extra_for(g->tag);

		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), T(x->head));
		for (i = 0; i < (int)(sizeof x->rows / sizeof x->rows[0]) && label_set(&x->rows[i].key);
		     i++, n++) {
			label_say(&x->rows[i].key, b->key[n], sizeof b->key[n]);
			snprintf(b->val[n], sizeof b->val[n], "%s", T(x->rows[i].does));
			out[n] = (menu_row){ b->key[n], b->val[n], false };
		}
		if (x->has_note) out[n++] = MENU_NOTE(T(x->note));
	} else {
		snprintf(b->head, sizeof b->head, T(S_CONTROLS_HEADING), ctl_page_name(CTL_GAME));
		n = in_a_game(out);
	}
	return n;
}
