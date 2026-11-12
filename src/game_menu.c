/* The game info screen's rows. See src/game_menu.h. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game_menu.h"
#include "i18n.h"

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
		out[n++] = (menu_row){ T(S_GAMEMENU_SYNOPSIS),
		                       gi->has_synopsis ? NULL : T(S_GAMEMENU_SYNOPSIS_NONE),
		                       gi->has_synopsis };
	if (gi->scraped && gi->year[0])
		out[n++] = (menu_row){ T(S_GAMEMENU_YEAR),  gi->year,  false };
	if (gi->scraped && gi->genre[0])
		out[n++] = (menu_row){ T(S_GAMEMENU_GENRE), gi->genre, false };
	/* The count opens the same list the in-game menu opens - which is where it
	 * comes from. Reading "4/54" and having nowhere to go with it is the
	 * question half-answered: which four, and what are the other fifty. A game
	 * with no set has nothing to open and says so. */
	out[n++] = (menu_row){ T(S_SYSMENU_CHEEVOS), gi->cheevos, gi->has_cheevos };
	/* Favorite is back here, on 2026-11-08: Y on the shelf now starts a game
	 * fresh, so marking one moved to this page, Y on it or A on the row */
	if (gi->can_favorite)
		out[n++] = (menu_row){ T(S_CONTROLS_FAVORITE), gi->favorite ? MENU_KEY_Y_ON : MENU_KEY_Y_OFF, true };
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
	 * panel, so whether there is one is already answered by looking; Cover
	 * and Video below says what is missing, and its page shows the rest.
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
	/* Cover and Video (DESIGN.md 11.34), the page that shows the cover and
	 * the video snap there are beside what the sources offer. Live with no
	 * network too: what is on the card is still shown there, a video still
	 * plays, and Before is still put back. Its value is what is missing. It
	 * was Get or Replace Box Art, which fetched one cover nobody saw first
	 * (2026-11-09: "I don't know what it is changing to, I can't pick"). */
	(void)net;
	if (!gi->own_art)
		out[n++] = (menu_row){ T(S_MEDIA_ROW),
		                       !gi->has_art ? T(S_MEDIA_VALUE_NO_COVER)
		                       : !gi->has_snap ? T(S_MEDIA_VALUE_NO_VIDEO) : NULL, true };
	/* PortMaster's own page in its place, to put a fresh copy over the one
	 * on the card (DESIGN.md 11.33): the way back from a PortMaster that
	 * will not start, its ports and their saves kept */
	else if (gi->can_reinstall)
		out[n++] = (menu_row){ T(S_PORTMASTER_REINSTALL_TITLE), NULL, true };
	/* DELETE GAME LAST (DESIGN.md 11.31): the game, its saves and states, its
	 * cover and video. Never where the screen opens, and never the end of a
	 * stray press: A on it only asks, and the question opens on Cancel. No
	 * value: what it takes is said by the question, where it can be read
	 * whole. */
	if (gi->can_delete)
		out[n++] = (menu_row){ T(S_GAMEMENU_DELETE), NULL, true };
	return n;
}

/* The in-game rows, carrying the display mode's current label. */
const char *const gm_layout_names[GM_LAYOUTS]  = { "stacked", "focus", "single" };
/* "Top Focus" rather than "Focus": the row has to say which screen is the big
 * one, and swapped it is the bottom - which Swap Screens says right under it. */
const char *const gm_layout_labels[GM_LAYOUTS] = { "Stacked", "Top Focus", "Single" };
static const str_id GM_LAYOUT_TEXT[GM_LAYOUTS] = {
	S_GAMEMENU_LAYOUT_STACKED, S_GAMEMENU_LAYOUT_FOCUS, S_GAMEMENU_LAYOUT_SINGLE
};

const char *gm_layout_label(int layout)
{
	return T(GM_LAYOUT_TEXT[layout >= 0 && layout < GM_LAYOUTS ? layout : 0]);
}

int gm_layout_index(const char *name)
{
	int i;

	for (i = 0; name && i < GM_LAYOUTS; i++)
		if (!strcmp(name, gm_layout_names[i])) return i;
	return 0;
}

void gm_quick_key(const char *tag, char *out, size_t n)
{
	snprintf(out, n, "quickslot.%s", tag ? tag : "");
}

int gm_quick_slot(const char *stored)
{
	int v = 0;

	if (!stored || sscanf(stored, "%d", &v) != 1 || v < 1 || v > GM_SLOTS)
		return GM_QUICK_DEFAULT;
	return v;
}

int gm_quick_step(int slot, int d)
{
	if (slot < 1 || slot > GM_SLOTS) slot = GM_QUICK_DEFAULT;
	return ((slot - 1 + d) % GM_SLOTS + GM_SLOTS) % GM_SLOTS + 1;
}

const char *gm_pak_option(const char *core)
{
	if (!core) return NULL;
	if (!strcmp(core, "parallel_n64")) return "parallel-n64-pak1";
	if (!strcmp(core, "mupen64plus_next")) return "mupen64plus-pak1";
	return NULL;
}

const char *gm_pak_value(const char *stored)
{
	return stored && !strcmp(stored, "memory") ? "memory" : "rumble";
}

const char *gm_pak_other(const char *value)
{
	return !strcmp(gm_pak_value(value), "memory") ? "rumble" : "memory";
}

const char *gm_pak_name(const char *value)
{
	return !strcmp(gm_pak_value(value), "memory") ? "Controller Pak" : "Rumble Pak";
}

/* In the library's database, beside the system's core options: pak.<TAG>.<the
 * game's file, as its shelf names it> */
void gm_pak_key(const char *tag, const char *file, char *out, size_t n)
{
	snprintf(out, n, "pak.%s.%s", tag ? tag : "", file ? file : "");
}

int gm_row_ids(const gm_ui *u, gm_row *ids)
{
	int n = 0, i;

	/* Over a port: what a stopped program can still be asked */
	if (u->port) {
		ids[n++] = GM_CONTINUE;
		ids[n++] = GM_CONTROLS;
		ids[n++] = GM_QUIT;
		return n;
	}
	for (i = 0; i < GM_ROWS; i++) {
		if (i == GM_QUICK && !u->quick) continue;
		ids[n++] = (gm_row)i;
		if (i == GM_DISPLAY && u->screens) {
			ids[n++] = GM_SCREENS;
			ids[n++] = GM_SWAP;
			ids[n++] = GM_STYLUS;
		}
		if (i == GM_DISPLAY && u->pak) ids[n++] = GM_PAK;
	}
	return n;
}

gm_row gm_x_row(void)
{
	return GM_QUIT;
}

int gm_row_at(const gm_ui *u, gm_row id)
{
	gm_row ids[GM_MAX_ROWS];
	int n = gm_row_ids(u, ids), i;

	for (i = 0; i < n; i++)
		if (ids[i] == id) return i;
	return -1;
}

int gm_rows(const gm_ui *u, menu_row *out, gm_bufs *b)
{
	static const str_id label[GM_MAX_ROWS] = {
		S_GAMEMENU_CONTINUE, S_GAMEMENU_SAVE, S_GAMEMENU_LOAD, S_GAMEMENU_QUICK,
		S_GAMEMENU_DISPLAY, S_SYSMENU_CONTROLS, S_SYSMENU_CHEEVOS, S_GAMEMENU_RESET,
		S_GAMEMENU_QUIT, S_GAMEMENU_SCREENS, S_GAMEMENU_SWAP, S_GAMEMENU_STYLUS,
		S_GAMEMENU_PAK
	};
	menu_row full[GM_MAX_ROWS];
	gm_row ids[GM_MAX_ROWS];
	int i, n;

	for (i = 0; i < GM_MAX_ROWS; i++) full[i] = (menu_row){ T(label[i]), NULL, true };
	full[GM_DISPLAY].value = u->dmode;
	/* The slot's number alone: the row says what it is */
	snprintf(b->quick, sizeof b->quick, "%d", u->quick);
	full[GM_QUICK].value = b->quick;

	/* Most of a library has no set, and a row that says so plainly is better
	 * than one that is missing: "none" answers the question the player opened
	 * the menu to ask. Drawn quiet, and does nothing when chosen - so the
	 * cursor steps over it rather than resting on a row that ignores A. */
	if (u->total > 0)
		snprintf(b->cheevos, sizeof b->cheevos, "%d / %d", u->earned, u->total);
	else {
		snprintf(b->cheevos, sizeof b->cheevos, "%s", T(S_GAMEMENU_CHEEVOS_NONE));
		full[GM_CHEEVOS].live = false;
	}
	full[GM_CHEEVOS].value = b->cheevos;
	full[GM_SCREENS].value = gm_layout_label(u->layout);
	full[GM_SWAP].value    = u->swap ? T(S_MENU_ON) : T(S_MENU_OFF);
	full[GM_STYLUS].value  = u->stylus ? T(S_MENU_ON) : T(S_MENU_OFF);
	full[GM_PAK].value     = u->pak;
	/* X quits from anywhere on the page (gm_x_row), and Quit says so */
	full[GM_QUIT].value    = MENU_KEY_X;

	n = gm_row_ids(u, ids);
	for (i = 0; i < n; i++) {
		out[i] = full[ids[i]];
		/* Display steps round on left and right and on A; so do the quick
		 * slot and the three for two screens. Every other row is plain. */
		b->sound[i] = ids[i] == GM_DISPLAY || ids[i] == GM_QUICK || ids[i] == GM_SCREENS ||
		              ids[i] == GM_SWAP || ids[i] == GM_STYLUS || ids[i] == GM_PAK
		              ? MENU_SND_RING : 0;
	}
	return n;
}
