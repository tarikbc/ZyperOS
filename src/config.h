/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CONFIG_H
#define ZYPEROS_CONFIG_H

#include <stdbool.h>

/* Fifteen systems on 2026-11-03, with the Atari 2600, Atari 7800, Game &
 * Watch and arcade shelves, and Favorites and Muse added at runtime: 17, one
 * past the 16 this was. Every one of those adds stops at the cap, so whichever
 * came last would have been left off the shelf with at most a line in the
 * log. 24 leaves room for Ports and the PlayStation and DS shelves on their
 * way. 12 was exact once too.
 *
 * And 24 was exact again on 2026-11-03: Neo Geo, Sega CD, 32X, Neo Geo CD
 * and PICO-8 made 23 rows, 25 with Favorites and Muse, so PICO-8 and Arcade
 * would have fallen off the end. 32 leaves room for the next few.
 * The next few were thirteen: ROGUE OS's other systems on 2026-11-04 made 37
 * rows, 39 with Favorites and Muse. 48 again leaves room. */
#define CFG_MAX_SYSTEMS 48
#define CFG_STR 256

/* A system's color when its line names none, or names something that is not
 * one: no color at all, rather than one picked for it. It was cyan, which
 * DESIGN.md 3.3 takes out of the system, and any color picked here would be
 * a system's color that no system has. So the screens decide what nothing
 * looks like, by role and in the theme that is on: a mark of it - a rail, a
 * menu's heading rule, a slot's dot - is **text-dim** (src/ui.h,
 * ui_sys_mark), and the light it lifts with is the same neutral.
 *
 * Above 0xFFFFFF, so it is never a color a line can name. */
#define CFG_NO_COLOR 0xFF000000u

typedef struct {
	char name[CFG_STR];   /* display name, e.g. "TurboGrafx-16" */
	char folder[CFG_STR]; /* ROM folder under Roms/            */
	char core[CFG_STR];   /* core short name, e.g. "mednafen_pce_fast" - the
	                       * one in use, of `cores` */
	/* The core field as written: one core, or several separated by commas,
	 * the first the default - "flycast2021,run:Emulators/Flycast/Flycast.sh"
	 * for the Dreamcast. The shelf's Core row chooses among them (src/main.c,
	 * core_step and core_load); everything else reads `core`. */
	char cores[CFG_STR];
	char tag[8];          /* saves, states and per-game configs hang off this,
	                       * so it is also the folder name under Saves/. Three
	                       * characters at most: this array is the limit, and a
	                       * longer tag is silently truncated into it. */
	char card[CFG_STR];   /* card art filename under ZyperOS/cards/ */
	unsigned accent;      /* 0xRRGGBB, tints the focus glow and the rail;
	                       * CFG_NO_COLOR when the line names none */
	char exts[CFG_STR];   /* which extensions in that folder are games; empty
	                       * means everything, which also means save files and
	                       * stray text files show up as games */
	/* Firmware a DISC image needs, resolved against Bios/. Empty for a system
	 * that needs none. Per-system and disc-only rather than per-system alone,
	 * because a PC Engine HuCard runs with no System Card and a CD does not -
	 * declaring it for the whole shelf would refuse the cartridges too. */
	char disc_bios[CFG_STR];
	/* The firmware above is needed by every game on the shelf, cartridges
	 * too, not by discs alone: a ColecoVision game does not start without
	 * the console's BIOS. From a `bios|tag|names` line, which also sets
	 * disc_bios. */
	bool bios_every;
	/* How much clock its games get: "light" or "medium" caps the CPU, the
	 * memory and the GPU for a system measured to keep its speed under that
	 * ceiling, empty is full. From a `power|tag|level` line; sent to Diatom
	 * with RUN, which steps a game back up at the first frame it can't keep.
	 * Ports and the standalone emulators always run at full. */
	char power[8];
	/* The buttons, where this system's core reads the pad its own way: a
	 * Diatom map (`a:b,b:a`, the SETMAP form), applied after RUN and
	 * joined with the system's turbo. Empty is the pad as it is. From a
	 * `map|tag|spec` line. */
	char map[96];
	/* The libretro controller type the core gets for port 0, 0 for its
	 * plain joypad; sent with RUN as controller=. From a
	 * `controller|tag|n` line. */
	unsigned controller;
	/* "dpad": the d-pad is the core's left analog stick, for a system its
	 * games play with a stick and the device has none (Diatom's stick.h);
	 * "toggle": R2 turns the d-pad into the stick and back, for a system
	 * whose games move with one or the other (the PSP's; src/main.c,
	 * stick_get). Sent with RUN as stick=. Empty is the d-pad as the d-pad.
	 * From a `stick|tag|dpad` or `stick|tag|toggle` line. */
	char stick[8];
} system_cfg;

typedef struct {
	system_cfg systems[CFG_MAX_SYSTEMS];
	int count;
} systems_cfg;


bool cfg_load_systems(const char *path, systems_cfg *out);

/* The RUN keys a game on `s` needs beyond the usual, tab separated, into
 * `out` ("" for none): two screens for a DS game (`dual`), the clock ceiling,
 * the controller type and the d-pad as a stick. */
/* Two system names in A to Z order (2026-11-06): case and accents do
 * not count ("Pokémon" is "pokemon") and a run of digits counts as its
 * number, so 3DO comes before 32X and Atari 2600 before Atari 5200 and Atari
 * Lynx. Negative, zero or positive, as strcmp. Pure, for the check. */
int cfg_name_cmp(const char *a, const char *b);

/* The systems in A to Z order by name, once, after cfg_load_systems: the
 * home's order since 2026-11-06, where it was systems.cfg's, which is by
 * release date (MENU > System Order keeps that as the other choice). A port
 * shelf stays last, as it was (docs/design/mock/controls-lab.html: Ports and
 * MP4 after the consoles); ties keep the file's order. Favorites and MP4 are
 * not in the file: the launcher puts them first and last. */
void cfg_sort_systems(systems_cfg *s);

void cfg_run_keys(const system_cfg *s, bool dual, char *out, int cap);

/* How many cores `s` lists, and the i-th of them into `out` (false past the
 * end). A system with one core lists one. */
int  cfg_core_count(const system_cfg *s);
bool cfg_core_at(const system_cfg *s, int i, char *out, int cap);

#endif
