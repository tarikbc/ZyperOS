/* SPDX-License-Identifier: GPL-3.0-only */
/* What does MENU > Controls actually say?
 *
 *     make check-controls
 *
 * The page exists to be the one place on the device that lists every button.
 * That is a property a machine can hold: every button the Brick has is named
 * on some page, no page names one twice, and no page invents one the device
 * does not have. A page that quietly dropped SELECT would still draw, still
 * look right, and be the one thing this feature was built to prevent.
 *
 * The rest is what the rows have to be to fit: a name and something it does,
 * kept short enough not to scroll.
 *
 * No SDL, no device.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/config.h"
#include "../src/controls.h"
#include "../src/db.h"
#include "../src/portmenu.h"
#include "../src/padglyph.h"
#include "legend.h"   /* BUTTONS, is_button, legend_form */

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* The Brick's front function keys are FUNCTION with the volume keys on the
 * Pixel 2 (ctl_set_fn_volume): each device's pages name its own, and never
 * the other's. L2+R2 is the Pixel 2's battery level (ctl_set_battery_lights),
 * which the Brick has no lights for. */
static bool pixel;
static int other_devices(const char *b)
{
	return pixel ? !strcmp(b, "F1/F2")
	             : !strcmp(b, "MENU+VOL") || !strcmp(b, "L2+R2");
}

/* A ceiling on the value, so a page of rows sits still to be read.
 *
 * Not a measurement of these rows - a button's name is short, so they have
 * more room than the two rows this is drawn from. It is the conservative end
 * of what has been seen on the 800px panel at the shipped text size:
 * "12 KB in / 41 MB out", 20 characters beside the 11 of "Transferred", runs
 * past it and scrolls; "auto (speaker)" beside "Audio Output" sits inside it.
 * Under this and a row is safe whatever it is beside; over it and the shot is
 * what settles it. */
#define VALUE_MAX 22

/* Rows of TEXT a page can have - its buttons and any note - before the panel
 * runs out.
 *
 * Six, with the page count in the heading and no footer (DESIGN.md 11.3,
 * 2026-11-06). It counted a footer, "Left/Right: more" under a rule,
 * too, and five rows with that footer did not fit: In MP4, Anywhere and the
 * Pixel 2's shelf page scrolled by themselves. The launcher measures each
 * page with the font as well (--controls-fit, check-controls-shots). */
#define PAGE_MAX 6

/* A note runs the panel's width and is cut at about here: "On Genesis and
 * SNES, X and Y are the pad's own buttons" showed thirty characters of
 * fifty-four in a shot on 2026-09-19. */
#define NOTE_MAX 30

static void legends_are_one_form(void)
{
	printf("a key legend is one form\n");
	ck(legend_form("A: go   Y: by game   L1/R1: window"), "the form itself");
	ck(legend_form("Left/Right: more"), "a single legend");
	ck(!legend_form("A: go    Y: by game"), "four spaces between");
	ck(!legend_form("A: go  Y: by game"), "two spaces between");
	ck(!legend_form("A: go   L/R: window"), "L/R is not a button");
	ck(!legend_form("B to close"), "a verb with a \"to\" is not a legend");
	ck(!legend_form("A: Go"), "the verb is lowercase");
	ck(!legend_form("A: go "), "no trailing space");
}

/* How many pages name this button. A button belongs to as many pages as it has meanings - A opens a
 * game on the shelf and pauses a track in Muse - so what matters is that it is
 * on at least one. Written as "exactly one" first, which failed on eight
 * buttons and was the check being wrong about the feature rather than the
 * other way round. */
static int pages_naming(const char *button)
{
	int p, seen = 0;

	for (p = 0; p < CTL_PAGES; p++) {
		int here = 0;

		{
			menu_row rows[CTL_MAX_ROWS];
			int n = ctl_rows((ctl_page)p, rows), i;

			for (i = 0; i < n; i++)
				if (rows[i].label && !ROW_IS_NOTE(rows[i]) &&
				    !strcmp(rows[i].label, button))
					here = 1;
		}
		seen += here;
	}
	return seen;
}

static void every_button_somewhere(void)
{
	char msg[128];
	int i;

	printf("every button is somewhere\n");
	for (i = 0; i < NBUTTONS; i++) {
		int seen = pages_naming(BUTTONS[i]);

		if (other_devices(BUTTONS[i])) {
			snprintf(msg, sizeof msg, "%s is on a page of the wrong device", BUTTONS[i]);
			ck(seen == 0, msg);
			continue;
		}

		/* X / Y is the turbo pair and X and Y are themselves: the game page
		 * naming the pair does not excuse the shelf page from naming X. */
		snprintf(msg, sizeof msg, "%s is on no page", BUTTONS[i]);
		ck(seen >= 1, msg);
	}
}

/* And no page says the same button twice, which is how a row gets edited in
 * place and its old wording left underneath. */
static void no_page_repeats(void)
{
	char msg[160];
	int p;

	printf("no page says a button twice\n");
	for (p = 0; p < CTL_PAGES; p++)
		{
			menu_row rows[CTL_MAX_ROWS];
			int n = ctl_rows((ctl_page)p, rows), i, j;

			for (i = 0; i < n; i++) {
				if (!rows[i].label || ROW_IS_NOTE(rows[i])) continue;
				for (j = i + 1; j < n; j++) {
					if (!rows[j].label || ROW_IS_NOTE(rows[j])) continue;
					snprintf(msg, sizeof msg, "%s names %s twice",
					         ctl_page_name((ctl_page)p), rows[i].label);
					ck(strcmp(rows[i].label, rows[j].label) != 0, msg);
				}
			}
		}
}

static void rows_fit(void)
{
	char msg[160];
	int p;

	printf("rows that sit still\n");
	for (p = 0; p < CTL_PAGES; p++)
		{
			menu_row rows[CTL_MAX_ROWS];
			int n = ctl_rows((ctl_page)p, rows), i;
			int rules = 0, notes = 0;

			snprintf(msg, sizeof msg, "%s: %d rows, room for %d",
			         ctl_page_name((ctl_page)p), n, CTL_MAX_ROWS);
			ck(n > 0 && n <= CTL_MAX_ROWS, msg);
			for (i = 0; i < n; i++) {
				if (!rows[i].label) { rules++; continue; }
				if (ROW_IS_NOTE(rows[i])) {
					notes++;
					snprintf(msg, sizeof msg,
					         "%s: a note of %d characters, cut after %d",
					         ctl_page_name((ctl_page)p),
					         (int)strlen(rows[i].label), NOTE_MAX);
					ck((int)strlen(rows[i].label) <= NOTE_MAX, msg);
					/* A note that names buttons is a legend, in its one form. */
					if (strstr(rows[i].label, ": ")) {
						snprintf(msg, sizeof msg, "%s: \"%s\" is not a legend",
						         ctl_page_name((ctl_page)p), rows[i].label);
						ck(legend_form(rows[i].label), msg);
					}
					continue;
				}
				snprintf(msg, sizeof msg, "%s: \"%s\" is not a button",
				         ctl_page_name((ctl_page)p), rows[i].label);
				ck(is_button(rows[i].label), msg);
				snprintf(msg, sizeof msg, "%s: \"%s\" says nothing",
				         ctl_page_name((ctl_page)p), rows[i].label);
				ck(rows[i].value != NULL, msg);
				if (!rows[i].value) continue;
				snprintf(msg, sizeof msg,
				         "%s: \"%s\" value is %d characters, over %d and it scrolls",
				         ctl_page_name((ctl_page)p), rows[i].label,
				         (int)strlen(rows[i].value), VALUE_MAX);
				ck((int)strlen(rows[i].value) <= VALUE_MAX, msg);
				/* Nothing here is selectable: it is a page to read, and a
				 * highlight on a row that does nothing when pressed is the
				 * thing the menu rules forbid. */
				snprintf(msg, sizeof msg, "%s: \"%s\" is live",
				         ctl_page_name((ctl_page)p), rows[i].label);
				ck(!rows[i].live, msg);
			}
			/* No footer: the page count is in the heading (DESIGN.md 11.3) */
			snprintf(msg, sizeof msg, "%s: a rule or a footer under it", ctl_page_name((ctl_page)p));
			ck(rules == 0 && (n < 1 || !rows[n - 1].label ||
			                  strcmp(rows[n - 1].label, "Left/Right: more")), msg);
			/* The panel's own ceiling. Over this and the page scrolls
			 * itself while somebody is reading it. */
			snprintf(msg, sizeof msg, "%s: %d rows of text, and %d fit the panel",
			         ctl_page_name((ctl_page)p), n - rules, PAGE_MAX);
			ck(n - rules <= PAGE_MAX, msg);
		}
}

/* The shelf has one fixed set of axes. */
static void horizontal_controls(void)
{
	menu_row rows[CTL_MAX_ROWS];
	int n = ctl_rows(CTL_MOVING, rows);

	ck(n > 2, "the moving page has controls");
	ck(!strcmp(rows[0].label, "Left/Right") && !strcmp(rows[0].value, "Move"),
	   "left and right move along the row");
	ck(!strcmp(rows[1].label, "Up/Down") && !strcmp(rows[1].value, "Letter, or 3 systems"),
	   "up and down jump by letter, or 3 systems by release date");
}

static void pages_are_named(void)
{
	int p, q;

	printf("the pages\n");
	ck(CTL_PAGES == 6, "six pages");
	for (p = 0; p < CTL_PAGES; p++) {
		ck(ctl_page_name((ctl_page)p) && ctl_page_name((ctl_page)p)[0],
		   "every page is named");
		for (q = p + 1; q < CTL_PAGES; q++)
			ck(strcmp(ctl_page_name((ctl_page)p), ctl_page_name((ctl_page)q)) != 0,
			   "two pages share a name");
	}
	/* Out of range reads as the first page rather than past the table. */
	ck(!strcmp(ctl_page_name((ctl_page)CTL_PAGES), ctl_page_name(CTL_MOVING)),
	   "a page number out of range is the first one");
}

/* ---------- in a game: the in-game menu's Controls ------------------------ */

/* A system's turbo as a new card has it (src/db.c), which is what the page
 * is given on a card nobody has changed */
static const char *turbo_of(const char *tag)
{
	size_t n, i;
	const db_default *d = db_defaults(DB_LIBRARY, &n);
	char key[32];

	snprintf(key, sizeof key, "turbo.%s", tag);
	for (i = 0; i < n; i++)
		if (!strcmp(d[i].key, key)) return d[i].value;
	return "";
}

/* What an in-game page may name at the left: a button, a pair or chord of
 * them ("L1/R1", "R2+X", "MENU+VOL"), the four faces, or the d-pad, alone or
 * in a chord ("SELECT+D-pad", the N64's own D-pad) */
static int ingame_key(const char *k)
{
	static const char *const ONE[] = {
		"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT", "MENU", "VOL",
		"D-pad",
	};
	char buf[32], *p, *save = NULL;

	if (!strcmp(k, "D-pad") || is_button(k)) return 1;
	if (strlen(k) >= sizeof buf) return 0;
	strcpy(buf, k);
	for (p = strtok_r(buf, "/+", &save); p; p = strtok_r(NULL, "/+", &save)) {
		size_t i;
		int ok = 0;

		for (i = 0; i < sizeof ONE / sizeof ONE[0]; i++)
			if (!strcmp(p, ONE[i])) ok = 1;
		if (!ok) return 0;
	}
	return 1;
}

/* Every row on every page of a game, and a few things each system's must say */
typedef struct { char key[32], val[48]; } seen_row;

static int game_rows(const ctl_game *g, seen_row *all, int cap, char heads[][48])
{
	int pages = ctl_game_pages(g), p, k = 0;

	for (p = 0; p < pages; p++) {
		menu_row rows[CTL_MAX_ROWS];
		ctl_buf b;
		int n = ctl_game_rows(g, p, rows, &b), i;

		if (heads) snprintf(heads[p], 48, "%s", b.head);
		for (i = 0; i < n && k < cap; i++)
			if (rows[i].label && !ROW_IS_NOTE(rows[i])) {
				snprintf(all[k].key, sizeof all[k].key, "%s", rows[i].label);
				snprintf(all[k].val, sizeof all[k].val, "%s", rows[i].value ? rows[i].value : "");
				k++;
			} else if (rows[i].label && ROW_IS_NOTE(rows[i])) {
				snprintf(all[k].key, sizeof all[k].key, "(note)");
				snprintf(all[k].val, sizeof all[k].val, "%s", rows[i].label);
				k++;
			}
	}
	return k;
}

static int says(const seen_row *all, int n, const char *key, const char *val)
{
	int i;

	for (i = 0; i < n; i++)
		if ((!key || !strcmp(all[i].key, key)) && (!val || !strcmp(all[i].val, val)))
			return 1;
	return 0;
}

static void game_page_fits(const ctl_game *g, int page)
{
	menu_row rows[CTL_MAX_ROWS];
	ctl_buf b;
	char msg[200];
	int n = ctl_game_rows(g, page, rows, &b), i, j, rules = 0, notes = 0, keys = 0;

	/* a ceiling only: the launcher measures each heading beside its page
	 * count with the font (--controls-fit), and a system's name gives way to
	 * its short one where it does not fit */
	snprintf(msg, sizeof msg, "%s p%d: heading \"%s\" over 21 characters", g->tag, page, b.head);
	ck(strlen(b.head) <= 21 && !strncmp(b.head, "Controls: ", 10), msg);
	for (i = 0; i < n; i++) {
		if (!rows[i].label) { rules++; continue; }
		if (ROW_IS_NOTE(rows[i])) {
			notes++;
			snprintf(msg, sizeof msg, "%s p%d: note \"%s\" over %d", g->tag, page,
			         rows[i].label, NOTE_MAX);
			ck((int)strlen(rows[i].label) <= NOTE_MAX, msg);
			continue;
		}
		keys++;
		snprintf(msg, sizeof msg, "%s p%d: \"%s\" is not a button", g->tag, page, rows[i].label);
		ck(ingame_key(rows[i].label), msg);
		snprintf(msg, sizeof msg, "%s p%d: \"%s\" says \"%s\", over %d", g->tag, page,
		         rows[i].label, rows[i].value ? rows[i].value : "", VALUE_MAX);
		ck(rows[i].value && (int)strlen(rows[i].value) <= VALUE_MAX, msg);
		/* the button and its value side by side: "START/SELECT  Right/Left
		 * soft key", 31, ran past the panel on the Pixel 2 */
		snprintf(msg, sizeof msg, "%s p%d: \"%s  %s\" is %d characters, over 26", g->tag,
		         page, rows[i].label, rows[i].value ? rows[i].value : "",
		         (int)(strlen(rows[i].label) + (rows[i].value ? strlen(rows[i].value) : 0)));
		ck(rows[i].value && strlen(rows[i].label) + strlen(rows[i].value) <= 26, msg);
		snprintf(msg, sizeof msg, "%s p%d: \"%s\" is live", g->tag, page, rows[i].label);
		ck(!rows[i].live, msg);
		for (j = i + 1; j < n; j++) {
			if (!rows[j].label || ROW_IS_NOTE(rows[j])) continue;
			snprintf(msg, sizeof msg, "%s p%d names %s twice", g->tag, page, rows[i].label);
			ck(strcmp(rows[i].label, rows[j].label) != 0, msg);
		}
	}
	/* Six rows, or five and a note: a row more sends a list with no cursor
	 * past the panel, where it scrolls itself (measured with the font,
	 * --controls-fit). No footer: the page count is in the heading. */
	snprintf(msg, sizeof msg, "%s p%d: %d rows and %d notes, room for 6, or 5 and 1", g->tag,
	         page, keys, notes);
	ck(keys >= 1 && keys + notes <= 6 && notes <= 1, msg);
	snprintf(msg, sizeof msg, "%s p%d: a rule or a footer", g->tag, page);
	ck(rules == 0, msg);
}

static void in_a_game(void)
{
	systems_cfg sys;
	char msg[200];
	int i, pages_seen = 0;

	printf("in a game: the in-game menu's Controls\n");
	ck(cfg_load_systems("config/systems.cfg", &sys), "config/systems.cfg reads");
	for (i = 0; i < sys.count; i++) {
		const system_cfg *s = &sys.systems[i];
		bool resident = strncmp(s->core, "run:", 4) && strcmp(s->core, "port");
		int e;

		if (!resident && !port_menu_pads(s->tag)) {
			/* a standalone emulator or a port has no page of its buttons,
			 * but OpenBOR, whose layout ships with it (src/portmenu.h) */
			snprintf(msg, sizeof msg, "%s runs outside Diatom and has a page", s->tag);
			ck(!ctl_game_known(s->tag), msg);
			continue;
		}
		snprintf(msg, sizeof msg, "%s (%s) has no page of its buttons", s->name, s->core);
		ck(ctl_game_known(s->tag), msg);
		for (e = 0; e < 2; e++) {
			ctl_game g = { s->tag, s->name, s->tag, s->map, turbo_of(s->tag),
			               e ? "fds" : "zip", NULL, NULL, !resident };
			seen_row all[64];
			char heads[CTL_GAME_PAGES + 1][48];
			int pages = ctl_game_pages(&g), p, n;

			if (e && strcmp(s->tag, "NES")) continue;
			snprintf(msg, sizeof msg, "%s: %d pages, 2 to %d", s->tag, pages, CTL_GAME_PAGES);
			ck(pages >= 2 && pages <= CTL_GAME_PAGES, msg);
			if (pages < 1 || pages > CTL_GAME_PAGES) continue;
			for (p = 0; p < pages; p++) game_page_fits(&g, p);
			n = game_rows(&g, all, 64, heads);
			snprintf(msg, sizeof msg, "%s: the last page is In a game", s->tag);
			ck(!strcmp(heads[pages - 1], "Controls: In a game"), msg);
			/* A program of its own (OpenBOR, src/portmenu.h) has no
			 * Hotkeys: SELECT's chords are Diatom's, not the program's */
			if (!resident) {
				snprintf(msg, sizeof msg, "%s runs outside Diatom and has a Hotkeys page", s->tag);
				ck(!says(all, n, "SELECT+R1", NULL) && !says(all, n, "SELECT+R2", NULL), msg);
			} else {
				snprintf(msg, sizeof msg, "%s: the page before it is Hotkeys", s->tag);
				ck(pages >= 2 && !strcmp(heads[pages - 2], "Controls: Hotkeys"), msg);
				/* The speed on every system; a quick save and load where its
				 * core keeps a state, and nowhere else */
				snprintf(msg, sizeof msg, "%s: SELECT+R2/L2 are not Faster and Slower", s->tag);
				ck(says(all, n, "SELECT+R2", "Faster") && says(all, n, "SELECT+L2", "Slower"), msg);
				snprintf(msg, sizeof msg, "%s: quick save %s, its core %s states", s->tag,
				         says(all, n, "SELECT+R1", "Quick save") ? "said" : "not said",
				         ctl_has_states(s->tag) ? "keeps" : "keeps no");
				ck(says(all, n, "SELECT+R1", "Quick save") == ctl_has_states(s->tag) &&
				   says(all, n, "SELECT+L1", "Quick load") == ctl_has_states(s->tag), msg);
				/* The rule's note where SELECT is a game button, and only there */
				{
					ctl_game sel = g;
					bool has_select = false;
					int sp, sn;

					sel.map = s->map;
					for (sp = 0; sp < pages - 2; sp++) {
						menu_row rr[CTL_MAX_ROWS];
						ctl_buf bb;

						sn = ctl_game_rows(&sel, sp, rr, &bb);
						for (int r = 0; r < sn; r++)
							if (rr[r].label && !ROW_IS_NOTE(rr[r]) &&
							    (strstr(rr[r].label, "SELECT") == rr[r].label ||
							     strstr(rr[r].label, "/SELECT")) &&
							    !strstr(rr[r].label, "SELECT+"))
								has_select = true;
					}
					snprintf(msg, sizeof msg, "%s: the SELECT note %s, SELECT %s a game button",
					         s->tag, says(all, n, "(note)", "SELECT alone stays the game's")
					         ? "said" : "not said", has_select ? "is" : "is not");
					ck(says(all, n, "(note)", "SELECT alone stays the game's") == has_select, msg);
				}
			}
			/* Turbo where the card has it, and nowhere else */
			{
				int k, turbo = 0;

				for (k = 0; k < n; k++)
					/* not counting PokeMini's own, on X */
					if (!strncmp(all[k].val, "Turbo ", 6) && strcmp(s->tag, "PKMINI"))
						turbo = 1;
				snprintf(msg, sizeof msg, "%s: turbo said %s, the card %s", s->tag,
				         turbo ? "yes" : "no", turbo_of(s->tag)[0] ? "has it" : "has none");
				ck(turbo == (turbo_of(s->tag)[0] != '\0'), msg);
			}
			/* No page names MENU+L1, gone from DS games 2026-11-06, nor
			 * MENU+R1, fast forward until 2026-11-07 (SELECT+R2 now), nor
			 * R2+X, the N64's C buttons until then (SELECT+X now) */
			snprintf(msg, sizeof msg, "%s: MENU+L1, MENU+R1 or R2+X is on a page", s->tag);
			ck(!says(all, n, "MENU+L1", NULL) && !says(all, n, "MENU+R1", NULL) &&
			   !says(all, n, "R2+X", NULL), msg);
			for (int r = 0; r < n; r++) {
				snprintf(msg, sizeof msg, "%s: \"%s\" says MENU+R1 or FF", s->tag, all[r].val);
				ck(!strstr(all[r].val, "+R1") && !strstr(all[r].val, "FF"), msg);
			}
			pages_seen += pages;
		}
	}
	ck(pages_seen > 80, "pages for every system");

	/* What a few of them have to say, from the cores and the maps */
	{
		static const struct { const char *tag, *map, *ext, *key, *val; } MUST[] = {
			/* Genesis Plus GX's six-button pad: the A cap is the C button */
			{ "MD",   "", "md", "A", "C" },
			/* Beetle NeoPop reads the south button as the Pocket's A, so the
			 * turbo on X, x:a, is a turbo B */
			{ "NGPC", "", "ngc", "X", "Turbo B" },
			/* map|DC puts the triggers on L1 and R1 */
			{ "DC",   "a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1", "chd", "L1/R1", "L/R trigger" },
			{ "DC",   "a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1", "chd", "A", "A" },
			/* and without the map, the Dreamcast's A would be on the B cap */
			{ "DC",   "", "chd", "A", "B" },
			/* FDS games turn their disk */
			{ "NES",  "", "fds", "L1/R1", "Disk side/Eject disk" },
			/* SELECT held makes the face buttons the C buttons, as map|N64
			 * puts the core's R2 on it */
			{ "N64",  "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y", "z64", "SELECT+X", "C up" },
			{ "N64",  "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y", "z64", "SELECT+Y", "C left" },
			{ "N64",  "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y", "z64", "L2", "Z" },
			/* each cap the N64 button it names (map|N64's a:b,b:y) */
			{ "N64",  "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y", "z64", "A/B", "A/B" },
			/* Under the C buttons, which Pak is in and where it is chosen:
			 * the Rumble Pak until a game takes the Controller Pak */
			{ "N64",  "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y", "z64", "(note)", "MENU > Pak: Rumble Pak" },
			/* Every button the PICO-8 reads: its arrows, O and X, its
			 * pause menu */
			{ "PICO8", "", "p8", "D-pad", "Arrows" },
			{ "PICO8", "", "p8", "A", "Button X" },
			{ "PICO8", "", "p8", "B", "Button O" },
			{ "PICO8", "", "p8", "START", "Pause menu" },
			/* The PlayStation's faces as their shapes (padglyph.h) */
			{ "PS1",  "", "chd", "A", PAD_CIRCLE },
			{ "PS1",  "", "chd", "B", PAD_CROSS },
			{ "PS1",  "", "chd", "X", PAD_TRIANGLE },
			{ "PS1",  "", "chd", "Y", PAD_SQUARE },
			/* The hotkeys, and the rule's note where SELECT is the game's */
			{ "SFC",  "", "sfc", "SELECT+R1", "Quick save" },
			{ "SFC",  "", "sfc", "SELECT+L1", "Quick load" },
			{ "SFC",  "", "sfc", "(note)", "SELECT alone stays the game's" },
			{ "SCUMMVM", "", "scummvm", "SELECT+R2", "Faster" },
			{ "NDS",  "", "nds", "L2", "Other screen" },
			{ "NDS",  "", "nds", "R2", "Stylus on/off" },
			{ "ARCADE", "", "zip", "START/SELECT", "Start/Coin" },
			{ "J2ME", "a:y,y:a", "jar", "A", "Fire" },
			/* too wide as one row, so two */
			{ "J2ME", "a:y,y:a", "jar", "START", "Right soft key" },
			{ "LYNX", "", "lnx", "SELECT", "Turn the screen" },
		};
		size_t k;

		for (k = 0; k < sizeof MUST / sizeof MUST[0]; k++) {
			ctl_game g = { MUST[k].tag, MUST[k].tag, MUST[k].tag, MUST[k].map,
			               turbo_of(MUST[k].tag), MUST[k].ext };
			seen_row all[64];
			int n = game_rows(&g, all, 64, NULL);

			snprintf(msg, sizeof msg, "%s: %s should say %s", MUST[k].tag, MUST[k].key,
			         MUST[k].val);
			ck(says(all, n, MUST[k].key, MUST[k].val), msg);
		}
	}

	/* A long name gives way to the home's short one, beside the page count */
	{
		ctl_game g = { "NGPC", "Neo Geo Pocket Color", "NGPC", "", "", "ngc", NULL, NULL };
		menu_row rows[CTL_MAX_ROWS];
		ctl_buf b;

		ctl_game_rows(&g, 0, rows, &b);
		ck(!strcmp(b.head, "Controls: NGPC"), "a long system name gives way to its short one");
		g.name = "Game Boy Advance";
		g.tag = "GBA";
		g.short_name = "GBA";
		ctl_game_rows(&g, 0, rows, &b);
		ck(!strcmp(b.head, "Controls: GBA"), "Game Boy Advance too, beside a page count");
		g.name = "Genesis";
		g.tag = "MD";
		ctl_game_rows(&g, 0, rows, &b);
		ck(!strcmp(b.head, "Controls: Genesis"), "and a name that fits stays");
	}
	/* OpenBOR, a program of its own with the in-game menu (src/portmenu.h):
	 * its buttons as OpenBOR lays out a pad, and an In a game page with no
	 * hotkeys, where POWER ends the game rather than saving it */
	{
		ctl_game g = { "OPENBOR", "OpenBOR", "OpenBOR", "", "", "pak", NULL, NULL, true };
		seen_row all[64];
		int n = game_rows(&g, all, 64, NULL);

		ck(port_menu_for("run:ZyperOS/openbor/OpenBOR.sh") &&
		   port_menu_for("run:Emulators/Flycast/Flycast.sh") && port_menu_for("port") &&
		   !port_menu_for("dsperate") && !port_menu_for("flycast2021"),
		   "the in-game menu over every \"run:\" program and every port, and not over a Diatom core");
		ck(port_menu_pads("OPENBOR") && !port_menu_pads("NDS") && !port_menu_pads("DC") &&
		   !port_menu_pads("PORTS"),
		   "OpenBOR's buttons on its Controls page; Flycast 2.7's are its own");
		ck(says(all, n, "A", "Attack") && says(all, n, "B", "Jump") &&
		   says(all, n, "X", "Attack 2") && says(all, n, "Y", "Special") &&
		   says(all, n, "L1/R1", "Attack 3/4") && says(all, n, "START", "Pause") &&
		   says(all, n, "SELECT", "Screenshot"),
		   "OpenBOR: A attacks, B jumps, X Attack 2, Y the special, L1/R1 Attack 3/4, START pauses, SELECT a screenshot");
		ck(says(all, n, "MENU", "Menu") && says(all, n, "POWER", "Quit and turn off") &&
		   !says(all, n, "POWER", "Save and turn off"),
		   "OpenBOR's In a game: MENU the menu, POWER quits and turns off");
		ck(says(all, n, "MENU, X", "Quit"), "and X in the menu is its Quit");
		g.port = false;
		n = game_rows(&g, all, 64, NULL);
		ck(says(all, n, "POWER", "Save and turn off"), "a Diatom game's POWER saves");
		ck(says(all, n, "MENU, X", "Quit"), "a Diatom game's X in the menu is Quit too");
	}
	/* The standalone Flycast under the menu: its buttons are its own
	 * settings', so In a game alone, with no hotkeys */
	{
		ctl_game g = { "", "Dreamcast", "Dreamcast", "", "", "chd", NULL, NULL, true };
		seen_row all[64];
		int n = game_rows(&g, all, 64, NULL);

		ck(ctl_game_pages(&g) == 1 && says(all, n, "MENU", "Menu") &&
		   says(all, n, "POWER", "Quit and turn off") && !says(all, n, "A", NULL),
		   "over Flycast 2.7: In a game alone, MENU the menu, POWER quits");
	}
	/* A game that took the Controller Pak says so, with where to change it */
	{
		ctl_game g = { "N64", "Nintendo 64", "N64", "select:r2,r2:none", "", "z64", NULL, NULL,
		               false, "Controller Pak" };
		seen_row all[64];
		int n = game_rows(&g, all, 64, NULL);

		ck(says(all, n, "(note)", "MENU > Pak: Controller Pak"),
		   "N64: a game with the Controller Pak says it is in, and where");
		ck(!says(all, n, "(note)", "Rumble Pak, not Controller Pak"),
		   "and the riddle it replaced is gone");
	}

	/* A system it does not know is In a game alone */
	{
		ctl_game g = { "NOPE", "Nope", "Nope", "", "", "bin", NULL, NULL };
		menu_row rows[CTL_MAX_ROWS];
		ctl_buf b;
		int n;

		ck(ctl_game_pages(&g) == 2, "an unknown system has Hotkeys and In a game alone");
		n = ctl_game_rows(&g, 0, rows, &b);
		ck(n == 4 && !strcmp(b.head, "Controls: Hotkeys"),
		   "Hotkeys first: quick save and load, faster and slower, no SELECT note");
		n = ctl_game_rows(&g, 1, rows, &b);
		ck(n == 5 && !strcmp(b.head, "Controls: In a game"),
		   "and In a game has no footer to turn");
	}
}

static void pairs_said_as_one(void)
{
	static const struct { const char *a, *b, *want; } P[] = {
		{ "Turbo A", "Turbo B", "Turbo A/B" },
		{ "L trigger", "R trigger", "L/R trigger" },
		{ "Right soft key", "Left soft key", "Right/Left soft key" },
		{ "Game button", "Game button", "Game button" },
		{ "C", "B", "C/B" },
		{ "Button 5", "Button 6", "Button 5/6" },
		{ "Disk side", "Eject disk", "Disk side/Eject disk" },
	};
	char out[32], msg[120];
	size_t i;

	printf("two values said as one\n");
	for (i = 0; i < sizeof P / sizeof P[0]; i++) {
		snprintf(msg, sizeof msg, "\"%s\" and \"%s\" are \"%s\"", P[i].a, P[i].b, P[i].want);
		ck(ctl_pair_value(P[i].a, P[i].b, out, (int)sizeof out) && !strcmp(out, P[i].want), msg);
	}
	ck(!ctl_pair_value("Right soft key", "Left soft key", out, 8), "too long does not fit");
}

int main(void)
{
	printf("controls: what MENU > Controls says\n");
	pages_are_named();
	legends_are_one_form();
	horizontal_controls();
	for (int d = 0; d < 2; d++) {
		pixel = d == 1;
		ctl_set_fn_volume(pixel);
		ctl_set_battery_lights(pixel);
		printf("on the %s:\n", pixel ? "Pixel 2" : "Brick");
		every_button_somewhere();
		no_page_repeats();
		rows_fit();
	}
	pairs_said_as_one();
	ctl_set_fn_volume(false);
	in_a_game();

	if (fails) {
		printf("\n%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
