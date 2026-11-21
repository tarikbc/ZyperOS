/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PORTMENU_H
#define ZYPEROS_PORTMENU_H

/* The in-game menu over a program of its own: OpenBOR, a port from the
 * Ports shelf, PortMaster itself - programs with no libretro core and so no
 * Diatom to pause them for the launcher's menu.
 *
 * launch.sh starts portkeys beside it with --menu (tools/portkeys.c), as for
 * any program a "run:" shelf or the Ports shelf starts (port_menu_for,
 * below). A MENU
 * tap there stops the port where it is (SIGSTOP), saves the frame on the
 * panel to PORT_MENU_STILL, lets go of the port's display, and starts the
 * launcher with PORT_MENU_ARG, the system's tag and the game. The launcher
 * takes the display, shows the in-game menu over that frame (src/main.c,
 * port_menu), and says by its exit status what was chosen. portkeys then
 * gives the display back to the port and lets it go on; for Quit or POWER,
 * launch.sh ends it (SIGUSR2, SIGUSR1).
 *
 * Any other exit status - the launcher could not start, or crashed - is
 * Continue: the game goes on, which is never the wrong thing to do with a
 * game nobody asked to end. */

#include <stdbool.h>
#include <string.h>

#define PORT_MENU_ARG      "--port-menu"
/* After the tag and the game: the program has not drawn yet, its loading
 * screen is what the menu is over, and Continue reads Keep Waiting (1.0.6) */
#define PORT_MENU_WAITING  "--waiting"

/* Which games get the menu, by the core that runs them: every game a shelf
 * hands to a program of its own. A "run:" script's: OpenBOR, which ZyperOS
 * ships, and the standalone Flycast 2.7, which a card can add
 * for the Dreamcast shelf's Core row. And the Ports
 * shelf's ("port"): every PortMaster port, and PortMaster's own app. The
 * launcher names the system's tag in the port request for these
 * (launch_port), and launch.sh gives portkeys --menu when it does.
 *
 * A port such as Stardew Valley takes most of the 1 GB, and the launcher
 * started beside it for the menu (86 MB in the VM, on llvmpipe) must never
 * be why the kernel kills the game, unsaved. So portkeys starts it as the
 * first thing the kernel would kill (PORT_MENU_OOM): if memory runs out
 * with the menu up, the menu goes, and a menu that ends without an answer
 * is Continue (above). */
static inline bool port_menu_for(const char *core)
{
	return core && (!strncmp(core, "run:", 4) || !strcmp(core, "port"));
}

/* Of those, the programs MP4 keeps playing over, as over a Diatom game:
 * your PICO-8, by its script (src/pico8.h's P8_CORE; written out here,
 * which portkeys includes too). Mine, 2026-11-20: "mp4 should keep playing
 * while pico 8 is running like we do on other systems". launch.sh stops MP4
 * for every other one, as a port such as Stardew Valley takes most of the
 * 1 GB; PICO-8 takes little of it. The launcher says so in the port request's
 * fifth field (PORT_KEEPS_MP4), hands MP4's queue over (src/mp4hand.h), and
 * launch.sh starts the follower beside the program (MP4_FOLLOW_ARG), which
 * portkeys stops while the in-game menu is up (--follow). SELECT in that
 * menu opens MP4. tools/pico8-mp4-check.py holds all of it. */
static inline bool port_keeps_mp4(const char *core)
{
	return core && !strcmp(core, "run:ZyperOS/pico8/PICO-8.sh");
}
#define PORT_KEEPS_MP4  "mp4"
#define MP4_FOLLOW_ARG  "--mp4-follow"

/* Of those, the programs whose picture follows the system's Display row:
 * your PICO-8, by its tag, whose script hands the row's mode to pico8_64
 * (sd/pixel2/PICO-8.sh, src/p8draw.h). The in-game menu over it has a
 * Display row (2026-11-20, "cover all those": the gap map's in-game
 * Display): PICO-8 cannot change its picture while it runs, so a mode
 * picked there asks first, the cursor on Cancel, as Reset Game's hotkey
 * does, and then the cart starts again in it (port_restart_ask,
 * src/ingame_screen.h). The launcher keeps the mode as display.<TAG>,
 * writes it to PORT_RESTART_FILE and exits PORT_MENU_RESTART; portkeys gives
 * the program its display back, lets it go on and sends launch.sh USR2, as
 * for Quit, and launch.sh's port_quit, finding the file, ends the program
 * and starts its script again with that mode (ZYPEROS_DISPLAY). portkeys,
 * MP4's follower and MP4 go on through it. tools/pico8-display-check.py
 * holds the menu's part and launch.sh's. */
static inline bool port_menu_display(const char *tag)
{
	return tag && !strcmp(tag, "PICO8");
}
/* Of those, the programs portkeys draws the low-battery disc and On-Screen
 * Stats over, as Diatom draws them over a game: your PICO-8. Both stay up,
 * so portkeys keeps a copy of the program's display for as long as they
 * show, and its master with it; PICO-8.sh execs pico8_64, which opens its
 * display once, where a port's script can start one program after another,
 * each wanting the master (tools/portkeys.c, "the disc and the stats line") */
static inline bool port_menu_overlays(const char *tag)
{
	return tag && !strcmp(tag, "PICO8");
}
/* One line, the display.<TAG> word to start again in, written whole */
#define PORT_RESTART_FILE  "/tmp/zyperos_port_restart"
/* After the tag and the game: the Next Display hotkey over your PICO-8
 * (portkeys, src/hotkeys.h). No menu: the next mode, round, and the same
 * question over the frame, then PORT_MENU_RESTART or PORT_MENU_CONTINUE */
#define PORT_MENU_DISPLAY_ARG "--display"

/* Where the menu over your PICO-8 was left, for the next MENU over the same
 * cart, as a game's in-game menu opens where it was closed (DESIGN.md 11.28,
 * "Where it opens"): each menu over PICO-8 is a process of its own, where a
 * game's is the launcher's and remembers it (g_gm_mem). One line, the tab,
 * the row each tab was on (its gm_row or ct_row) and Applies To's scope:
 * "tab row0 row1 row2 row3 scope", written whole and renamed in as the menu
 * ends. The launcher removes it as it hands a cart over (launch_port), so a
 * cart's first MENU opens on Game, on Continue; a start again (a Display
 * mode, new buttons) keeps it. A file it cannot read is none. */
#define PORT_MENU_MEM "/tmp/zyperos-port-menu-mem"

/* The menu's process to the kernel when memory runs out: the first to go,
 * its oom_score_adj at the top of the scale */
#define PORT_MENU_OOM "1000"

/* Of those, the programs whose buttons the Controls pages know, by tag:
 * OpenBOR, whose layout ships with it (src/controls.c), and your PICO-8. Flycast 2.7 keeps
 * its own in its settings, which ZyperOS does not read, so over it Controls
 * is In a game alone; and the Dreamcast's page of buttons is flycast2021's.
 * A port's pages are its own files' instead (src/portdoc.h: its README,
 * its gptokeyb keymap, its port.json), and PortMaster's are its window's.
 * tools/controls-check.c holds this to the table. */
static inline bool port_menu_pads(const char *tag)
{
	/* OpenBOR's, and your PICO-8's: the PICO-8 shelf's row is fake08's
	 * layout, which is PICO-8's own, O at the bottom and X beside it
	 * (sd/pixel2/PICO-8.sh keeps your PICO-8 so) */
	return tag && (!strcmp(tag, "OPENBOR") || !strcmp(tag, "PICO8"));
}

/* The frame as the panel showed it when MENU was tapped, at the panel's own
 * size and turn: "ZPST", the width and the height as uint16 little-endian,
 * then width * height pixels of XRGB8888, row by row. In /tmp, which is
 * memory: 1.2 MB on the Pixel 2, written at every open. */
#define PORT_MENU_STILL    "/tmp/zyperos-port-still.raw"
#define PORT_MENU_MAGIC    "ZPST"

/* There while the menu is up over a port that is still starting (portkeys's
 * menu_waiting): the lines going into the log then are the menu's and
 * portkeys's, not the port's, and plastron's splash, which draws the port's
 * latest line on its loading screen, passes over them (launch.sh gives it
 * this path as SPLASH_OUT_PAUSE; its splash/src/portout.h). An empty file. */
#define PORT_MENU_UP       "/tmp/zyperos_menu_up"

/* "Getting hot" over a port (DESIGN.md 11.23). The launcher renders the
 * notices Diatom shows over a game; for a port it is not running, so it
 * renders this one as it hands over to the port (launch_port), in both
 * themes, and portkeys shows it when the chip passes the trip, by src/heat.h's
 * rules. Diatom's overlay format (src/notice.h): "DTOV", the width and the
 * height as uint16 little-endian, then BGRA rows, straight alpha. */
#define PORT_HEAT_LIGHT    "/tmp/zyperos-port-heat-light.dtov"
#define PORT_HEAT_DARK     "/tmp/zyperos-port-heat-dark.dtov"

/* The launcher's exit status for each answer */
#define PORT_MENU_CONTINUE 10
#define PORT_MENU_QUIT     11
#define PORT_MENU_POWER    12
/* Reset asked and answered Reset (your PICO-8's: port_resets): portkeys
 * gives the program its display and its keys back, then CTRL-R on its
 * keyboard, PICO-8's "Reload / Run / Restart cartridge" */
#define PORT_MENU_RESET    13
/* A Display mode over your PICO-8 (port_menu_display): the program starts
 * again with it */
#define PORT_MENU_RESTART  14

/* ---- your PICO-8's hotkeys, Reset and Auto Off ------------------------------
 *
 * Over your PICO-8 the hotkeys a player sets are SELECT held and one more
 * button, as over a Diatom game (src/hotkeys.h, DESIGN.md 11.61), judged by
 * portkeys with Diatom's own rule (diatom/src/chord.c, tools/pkhot.c), and
 * never the cart's, as over a Diatom game: while a hotkey has a key,
 * portkeys holds the joypad and gives PICO-8 a pad of its own, fed every
 * other press at once (tools/pkvpad.h). Screenshot, Reset Game and Quit are
 * portkeys's to do; Next Shader and Next Display step the system's rows as
 * the menu's do. Quick Save, Quick Load, Faster and Slower do nothing: PICO-8
 * has no states and no speed, so their keys are PICO-8's under SELECT, as a
 * core's that keeps no state are (chord_turn's `live`). Auto Off over it too
 * (portkeys's idle clock), as over a game; no other program of its own has
 * either: a port has its own hotkeys (gptokeyb's), and a long install or a
 * cutscene is no player gone. */
static inline bool port_hotkeys(const char *tag)
{
	return tag && !strcmp(tag, "PICO8");
}
/* Over which the menu has Reset, asked first (PORT_MENU_RESET): your
 * PICO-8, which restarts a cart on CTRL-R. OpenBOR and a port have no such
 * key ZyperOS can send. */
static inline bool port_resets(const char *tag)
{
	return port_hotkeys(tag);
}
/* After the tag and the game, in place of PORT_MENU_WAITING: Reset Game's
 * hotkey. The launcher asks the Reset question alone over the still (Diatom
 * pauses for it the same way, PAUSED for=reset) and says PORT_MENU_RESET or
 * PORT_MENU_CONTINUE */
#define PORT_MENU_RESET_ARG "--reset"

/* What the launcher hands portkeys for your PICO-8, written as it hands over
 * (launch_port) and again as the in-game menu ends, where a Hotkeys change
 * may have been made: tab-separated lines, a word and its value.
 *
 *   hotkeys  the table in force for the cart (hks_get: This Game, PICO-8,
 *            Every Game, ZyperOS's own) in Diatom's RUN form, hk_run's
 *            "save:r1,load:l1,shot:y", "none" for none at all
 *   shots    where a screenshot goes, Screenshots/PICO-8/<the cart's name>,
 *            or .../PICO-8 for Splore and PICO-8's own start; portkeys adds
 *            the time and ".png", as Diatom does with RUN shots=
 *
 * Written whole and renamed in. Read by portkeys at its start and after each
 * menu; a missing file is no hotkeys. */
#define PORT_KEYS_FILE "/tmp/zyperos-port-keys"

/* "Screenshot saved" over your PICO-8, in both themes, for portkeys to show
 * with the shot itself in it (DESIGN.md 11.61's pop-up): the launcher cannot
 * draw while PICO-8 runs, so it renders the slab with the folder under the
 * heading and an empty square where the picture goes (notice.h's
 * NOTICE_SLOT, its place after the pixels), and portkeys lays the shot
 * there. DTOV as the heat notice's, with that trailer. */
#define PORT_SHOT_LIGHT "/tmp/zyperos-port-shot-light.dtov"
#define PORT_SHOT_DARK  "/tmp/zyperos-port-shot-dark.dtov"
/* And "Shader / CRT", Next Shader's pop-up (DESIGN.md 11.61), one for each
 * shader your PICO-8 is offered (src/p8fx.h's p8fx_shaders) in each theme,
 * rendered when the hotkey has a key: the SHADERS name, then "light" or
 * "dark". portkeys shows the one for the shader it stepped to. */
#define PORT_SHADER_POPUP "/tmp/zyperos-port-shader-%s-%s.dtov"

#endif
