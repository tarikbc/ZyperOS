/* The game info screen's rows. See src/game_menu.h. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game_menu.h"

int gi_rows(menu_row *out, const game_info *gi, bool net)
{
	int n = 0;

	/* THE GAME FIRST, THEN WHAT YOU CAN DO ABOUT IT. Eric's order, 2026-09-17,
	 * and it reverses the one this screen shipped with a day earlier.
	 *
	 * That first order led with what the cursor could act on, which is the
	 * right rule for a menu of actions and the wrong one here: this screen is
	 * opened to find out what a game IS. So the three that describe it come
	 * first - what it is about, when it came out, what kind of thing it is -
	 * and the two that do something follow.
	 *
	 * Synopsis leads and is still selectable, because the prose does not fit
	 * on a row and the row is how you get to it.
	 *
	 * Cheevos stays, below them, for the reason it is worth opening BEFORE a
	 * game rather than during one: seeing what there is to shoot for is part
	 * of deciding whether to start. Its count is also the one thing here that
	 * changes while you play.
	 *
	 * All three scraped rows are absent until a scrape has spoken for this
	 * game. Before that they would say "unknown" on every game on the card,
	 * which is a row asking to be ignored rather than a fact. A scrape that
	 * came back without a year or a genre simply says nothing; one that came
	 * back without prose says "none", the way the in-game Cheevos row does,
	 * because the difference between "not looked up" and "looked up, nothing
	 * there" is worth a word. */
	if (gi->scraped)
		out[n++] = (menu_row){ "Synopsis", gi->has_synopsis ? NULL : "none",
		                       gi->has_synopsis };
	if (gi->scraped && gi->year[0])
		out[n++] = (menu_row){ "Year",  gi->year,  false };
	if (gi->scraped && gi->genre[0])
		out[n++] = (menu_row){ "Genre", gi->genre, false };
	/* The count opens the same list the in-game menu opens - which is where it
	 * comes from. Reading "4/54" and having nowhere to go with it is the
	 * question half-answered: which four, and what are the other fifty. A game
	 * with no set has nothing to open and says so. */
	out[n++] = (menu_row){ "Cheevos", gi->cheevos, gi->has_cheevos };
	/* WHAT THIS SCREEN NO LONGER SAYS, and why, because each was removed on a
	 * reason rather than to make the list shorter:
	 *
	 * File. The panel is headed by the game's name and the shelf behind it is
	 * showing the same game, so the row spent its width repeating what was
	 * already on screen twice over, plus a region tag and an extension. What
	 * goes with it is the one place the exact bytes of a filename could be
	 * read, which is a real loss on the day a scrape misses - and still not
	 * worth a permanent row that every player reads past every time.
	 *
	 * Size. The size of a cartridge ROM has no consequence on a card with room
	 * for a thousand of them, and it is not a number anybody acts on.
	 *
	 * Box Art, as a count of bytes. The cover is on the shelf behind this
	 * panel, so whether there is one is already answered by looking; the only
	 * part worth saying is whether the action below gets one or replaces one,
	 * which that row says in its own label.
	 *
	 * Favorite. Y does it from the shelf, on both screens that have a cursor,
	 * and a second way to do the same thing is a second thing to keep working.
	 * The row also had to rebuild the Favorites shelf under its own cursor and
	 * decide whether the game it was describing had moved out from under it.
	 *
	 * Saves. The save carousel shows them when you load, which is the moment
	 * anyone cares, and it shows the actual frames rather than a count. This
	 * row was the only one here about your copy rather than the game, and it
	 * cost a stat of the whole slot set every time the screen opened.
	 *
	 * Players, and the rest of the scrape. The card carries publisher,
	 * developer, players, genres, ESRB and their score out of 20, and only
	 * genre is here. Measured over the 1,705 rows on 2026-09-17: ESRB is empty
	 * on 53% of them, which makes it a row that is blank half the time;
	 * players is 1-player on 1,008 of them and moot on a handheld nobody else
	 * is holding; publisher and developer run to 214 and 417 distinct values
	 * of pure trivia. They stay stored - re-scraping to add a column is an
	 * hour - and stay off this screen. */
	out[n++] = (menu_row){ gi->has_art ? "Replace Box Art" : "Get Box Art",
	                       net ? NULL : "needs Wi-Fi", net };
	return n;
}

/* The in-game rows, carrying the display mode's current label. */
const char *const gm_layout_names[GM_LAYOUTS]  = { "stacked", "focus", "single" };
/* "Top Focus" rather than "Focus": the row has to say which screen is the big
 * one, and swapped it is the bottom - which Swap Screens says right under it. */
const char *const gm_layout_labels[GM_LAYOUTS] = { "Stacked", "Top Focus", "Single" };

int gm_layout_index(const char *name)
{
	int i;

	for (i = 0; name && i < GM_LAYOUTS; i++)
		if (!strcmp(name, gm_layout_names[i])) return i;
	return 0;
}

int gm_row_ids(const gm_ui *u, gm_row *ids)
{
	int n = 0, i;

	for (i = 0; i < GM_ROWS; i++) {
		ids[n++] = (gm_row)i;
		if (i == GM_DISPLAY && u->screens) {
			ids[n++] = GM_SCREENS;
			ids[n++] = GM_SWAP;
			ids[n++] = GM_STYLUS;
		}
	}
	return n;
}

int gm_rows(const gm_ui *u, menu_row *out, gm_bufs *b)
{
	static const char *label[GM_MAX_ROWS] = {
		"Continue", "Save", "Load", "Display", "Controls", "Cheevos", "Reset", "Quit",
		"Screens", "Swap Screens", "Stylus"
	};
	menu_row full[GM_MAX_ROWS];
	gm_row ids[GM_MAX_ROWS];
	int i, n;

	for (i = 0; i < GM_MAX_ROWS; i++) full[i] = (menu_row){ label[i], NULL, true };
	full[GM_DISPLAY].value = u->dmode;

	/* Most of a library has no set, and a row that says so plainly is better
	 * than one that is missing: "none" answers the question the player opened
	 * the menu to ask. Drawn quiet, and does nothing when chosen - so the
	 * cursor steps over it rather than resting on a row that ignores A. */
	if (u->total > 0)
		snprintf(b->cheevos, sizeof b->cheevos, "%d / %d", u->earned, u->total);
	else {
		snprintf(b->cheevos, sizeof b->cheevos, "none");
		full[GM_CHEEVOS].live = false;
	}
	full[GM_CHEEVOS].value = b->cheevos;
	full[GM_SCREENS].value = gm_layout_labels[u->layout >= 0 && u->layout < GM_LAYOUTS
	                                          ? u->layout : 0];
	full[GM_SWAP].value    = u->swap ? "on" : "off";
	full[GM_STYLUS].value  = u->stylus ? "on" : "off";

	n = gm_row_ids(u, ids);
	for (i = 0; i < n; i++) {
		out[i] = full[ids[i]];
		/* Display steps round on left and right and on A; so do the three
		 * for two screens. Every other row is plain. */
		b->sound[i] = ids[i] == GM_DISPLAY || ids[i] == GM_SCREENS ||
		              ids[i] == GM_SWAP || ids[i] == GM_STYLUS ? MENU_SND_RING : 0;
	}
	return n;
}
