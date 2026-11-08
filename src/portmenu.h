/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PORTMENU_H
#define ZYPEROS_PORTMENU_H

/* The in-game menu over a program of its own: OpenBOR, which has no libretro
 * core and so no Diatom to pause it for the launcher's menu.
 *
 * launch.sh starts portkeys beside it with --menu (tools/portkeys.c), as for
 * any program a "run:" shelf starts (port_menu_for, below). A MENU
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
 * hands to a program of its own through a "run:" script. That is OpenBOR,
 * which ZyperOS ships, and DraStic and the standalone Flycast 2.7, which a
 * card can add for the Nintendo DS and Dreamcast shelves' Core row. The
 * launcher names the system's tag in the port request for these alone
 * (launch_port), and launch.sh gives portkeys --menu when it does.
 *
 * Not a port ("port", the Ports shelf): the mechanism is the same, but a
 * PortMaster game such as Stardew Valley takes most of the 1 GB, and the
 * launcher started beside it for the menu could push the kernel to kill the
 * game, unsaved. */
static inline bool port_menu_for(const char *core)
{
	return core && !strncmp(core, "run:", 4);
}

/* Of those, the programs whose buttons the Controls pages know, by tag:
 * OpenBOR, whose layout ships with it (src/controls.c). DraStic and Flycast
 * keep theirs in their own settings, which ZyperOS does not read, so over
 * them Controls is In a game alone; and the Nintendo DS's page of buttons is
 * DSperate's, not DraStic's. tools/controls-check.c holds this to the
 * table. */
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
