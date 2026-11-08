/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PORTMENU_H
#define ZYPEROS_PORTMENU_H

/* The in-game menu over a program of its own: OpenBOR, which has no libretro
 * core and so no Diatom to pause it for the launcher's menu.
 *
 * launch.sh starts portkeys beside it with --menu (tools/portkeys.c). A MENU
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

/* The systems run by a program of their own that have the in-game menu over
 * them, by tag: OpenBOR. launch.sh gives portkeys --menu for its script
 * (sd/pixel2/launch.sh, run_port), and the Controls pages have its buttons
 * (src/controls.c); tools/controls-check.c holds the two to this. */
static inline bool port_menu_for(const char *tag)
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
