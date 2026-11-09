/* A key legend, for the checks that hold one to its form: controls-check
 * (MENU > Controls' notes), bt-check (the Bluetooth screen's footer) and
 * menu-check (the Wi-Fi screen's). One copy, so the three cannot come to
 * disagree about what a legend is.
 *
 * Header-only and static, as a check is one file and a program of its own.
 */
#ifndef ZYPEROS_TOOLS_LEGEND_H
#define ZYPEROS_TOOLS_LEGEND_H

#include <string.h>

/* Every button on the Brick that ZyperOS reads, by the name the pages use.
 * D-pad axes are named as pairs because that is how they are pressed, and
 * written with no air around the slash - "L1/R1", not "L1 / R1". */
static const char *const BUTTONS[] = {
	"Up/Down", "Left/Right", "A", "B", "X", "Y", "L1/R1", "L2/R2", "X/Y/L2/R2",
	"MENU", "SELECT", "POWER", "Volume rocker", "F1/F2",
	"MENU+VOL", "L2+R2",
	/* Held a second, the way back to the home (tools/zyhome.c, 2026-11-09) */
	"MENU+START",
	/* One after the other: X in the in-game menu, its Quit (2026-11-08) */
	"MENU, X",
	/* The hotkeys in a game: SELECT held and a shoulder (2026-11-07) */
	"SELECT+R1", "SELECT+L1", "SELECT+R2", "SELECT+L2",
};
#define NBUTTONS ((int)(sizeof BUTTONS / sizeof BUTTONS[0]))

static int is_button(const char *s)
{
	int i;
	for (i = 0; i < NBUTTONS; i++) if (!strcmp(BUTTONS[i], s)) return 1;
	return 0;
}

/* A key legend is the one form of DESIGN.md 11.4, "A: go   Y: by game": a
 * button, a colon, a lowercase verb, and exactly three spaces between the
 * pairs. The buttons are the names above, so "L/R" and "B to close" are not
 * legends. */
static int legend_form(const char *s)
{
	char buf[128], *part, *next;

	if (strlen(s) >= sizeof buf) return 0;
	strcpy(buf, s);
	for (part = buf; part; part = next) {
		char *gap = strstr(part, "   "), *colon, *verb;

		if (gap) { *gap = '\0'; next = gap + 3; } else next = NULL;
		colon = strstr(part, ": ");
		if (!colon) return 0;
		*colon = '\0';
		if (!is_button(part)) return 0;
		verb = colon + 2;
		if (*verb < 'a' || *verb > 'z' || strstr(verb, "  ") ||
		    verb[strlen(verb) - 1] == ' ') return 0;
	}
	return 1;
}

#endif
