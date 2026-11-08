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

/* The menu's process to the kernel when memory runs out: the first to go,
 * its oom_score_adj at the top of the scale */
#define PORT_MENU_OOM "1000"

/* Of those, the programs whose buttons the Controls pages know, by tag:
 * OpenBOR, whose layout ships with it (src/controls.c). Flycast 2.7 keeps
 * its own in its settings, which ZyperOS does not read, so over it Controls
 * is In a game alone; and the Dreamcast's page of buttons is flycast2021's.
 * A port's pages are its own files' instead (src/portdoc.h: its README,
 * its gptokeyb keymap, its port.json), and PortMaster's are its window's.
 * tools/controls-check.c holds this to the table. */
static inline bool port_menu_pads(const char *tag)
{
	return tag && !strcmp(tag, "OPENBOR");
}

/* The frame as the panel showed it when MENU was tapped, at the panel's own
 * size and turn: "ZPST", the width and the height as uint16 little-endian,
 * then width * height pixels of XRGB8888, row by row. In /tmp, which is
 * memory: 1.2 MB on the Pixel 2, written at every open. */
#define PORT_MENU_STILL    "/tmp/zyperos-port-still.raw"
#define PORT_MENU_MAGIC    "ZPST"

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

#endif
