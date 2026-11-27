/* SPDX-License-Identifier: GPL-3.0-only */
#include "ds/hover.h"
#include "link_screen.h"
#include "shelf_draw.h"
#include "lastboot.h"
#include "cheats_screen.h"
#include "homesys.h"

#include "app.h"
#include "artbadge.h"
#include "bgm.h"
#include "biosfile.h"
#include "boottheme.h"
#include "cheevos.h"
#include "clock.h"
#include "clock_screen.h"
#include "ctl_channel.h"
#include "dlq.h"
#include "emptyhome.h"
#include "esmedia.h"
#include "favorites.h"
#include "focusfx.h"
#include "gridrule.h"
#include "texload.h"
#include "look.h"
#include "mp4video.h"
#include "mp4c.h"
#include "notice.h"
#include "nowplaying_screen.h"
#include "panel.h"
#include "menumotion.h"
#include "place.h"
#include "powerscreens.h"
#include "rahash.h"
#include "shelfbuild.h"
#include "snaps.h"
#include "shots_screen.h"
#include "snapshelf.h"
#include "snapview.h"
#include "sort.h"
#include "stats.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"
#include "zypermark.h"

#include <SDL.h>
#include <SDL_image.h>
#ifdef ZYPEROS_DESK
#include "device/desk.h"
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* ---------- when the next frame is needed -------------------------------- */

/* The redraw clock: what each is, and redraw_at, redraw_now and
 * frames_wanted, are in src/app.h */
Uint32 g_redraw_at = 0;
Uint32 g_moved_at;
/* Spotlight's focused sprite's next atlas frame, when it is the only thing on
 * the home that will change by itself (focus_frame); else REDRAW_NEVER */
Uint32 g_focus_at = REDRAW_NEVER;

/* The clock the launcher's motion is measured on: the shelf's moves (through
 * cf_clock), the menus' focus plate and the give of their list, and the
 * background tint's follow. plat_now_ms, except in a shot that asks for a
 * time (--shot-at), where it stands still wherever the shot puts it, so the
 * frame drawn is the frame that far into a move, and the same one every time
 * it is asked for. */
bool     g_clock_held;
unsigned g_clock_at;

unsigned anim_now(void) { return g_clock_held ? g_clock_at : plat_now_ms(); }
Uint32 anim_ticks(void) { return anim_now(); }

/* A card made for a theme that is no longer on destroyed and its slot
 * emptied; art, and a card in the theme that is on, left as they are. An
 * empty slot is asked for again by whatever draws it, which makes it anew. */
static void drop_card(SDL_Texture **t)
{
	if (tex_stale(*t)) {
		SDL_DestroyTexture(*t);
		*t = NULL;
	}
}

/* The shelves' cards of the theme that was stay where they are: each slot
 * shows its card until the one of the theme that is on lands from a worker
 * (game_card_wanted), games' and albums' alike, so a turn leaves no hole in
 * a row. Now Playing's cover is made on the frame and goes at once. */
void theme_changed(app *a)
{
	g_theme_late = false;              /* told, whoever turned it */
	drop_card(&g_np.tex);
	g_np.done = g_np.tex != NULL;      /* np_cover's own rule: made, or ask */
	redraw_now();
}

/* Auto's clock, asked by a screen's own loop once a pass: true when the theme
 * changed, and then theme_changed has been told. Every loop that stays on
 * screen asks it - the menus through menu_run_body, the shelf's loop for
 * itself, and every screen that runs its own - so an edge of the Night
 * Hours arriving while one is up is drawn from its next frame, and the file
 * Diatom and portkeys read follows. A loop that draws every pass needs
 * nothing more; one
 * that draws only when something changed takes a true as a change. A screen
 * drawn once and held, a one-line message, asks before it is drawn.
 *
 * The one way in for the clock: a turn on_game_tick saw during a game
 * (g_theme_late) is a change here like the clock's own, so the in-game menu
 * and MP4 opened from it draw their cards in the theme that is on, and the
 * way back from the game asks this before its first frame. */
/* System Update's word that this version started (docs/updates.md): the
 * switch at boot (the image's update-apply.sh) puts a new ZyperOS folder on
 * trial, and a third start without this word goes back to the version
 * before by itself. The word is ten seconds of a launcher whose screens are
 * running, asked from every screen's loop through tick_theme below. */
static void tick_update_trial(void)
{
	static bool done;
	char p[512];

	if (done || plat_now_ms() - t_boot0 < 10000) return;
	done = true;
	snprintf(p, sizeof p, "%s/.trial", P_ROOT);
	if (unlink(p) == 0) fprintf(stderr, "update: this version started: off trial\n");
	/* And on a card with two system slots (docs/updates.md, layer 2), the
	 * system's word too: zyperos-update confirm keeps a new system U-Boot
	 * is counting starts of, and says when U-Boot went back */
	{
		FILE *f = fopen("/proc/cmdline", "r");
		char cmd[1024] = "";
		if (f) { if (!fgets(cmd, sizeof cmd, f)) cmd[0] = 0; fclose(f); }
		if (strstr(cmd, "zyperos.slot=")) up_spawn("confirm");
	}
}

/* The boot screen in the chosen theme and brightness (src/boottheme.h,
 * DESIGN.md 11.21): theme.txt on the boot partition, for the next start. Ten
 * seconds into a start, as tick_update_trial, and two seconds after the last
 * change of Appearance, the clock's zone or the brightness, so a held Left
 * or Right writes it once.
 * Written only when it differs from what this start's kernel line carried,
 * which is what the boot partition had: a normal start writes nothing. A
 * boot.scr from before the theme carries no words at all (System Update
 * does not write boot.scr; the installer does): then what was written last,
 * noted on the card, says whether it is there, so such a card is not
 * written to at every start. */
static unsigned g_boot_theme_at;

/* The language on the card too, P_USERDATA/LANG_FILE, its code and a line
 * end: plastron's init reads it when a system update's copy starts and hands
 * it to the splash, so "Updating ZyperOS on this card" is in it on a card
 * whose boot.scr is older than zylang (src/boottheme.h). Written only when it
 * says something else, so a normal start reads a few bytes 10 s in and
 * writes nothing. */
static void lang_file_keep(void)
{
	char path[512], had[32] = "", want[32];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", P_USERDATA, LANG_FILE);
	snprintf(want, sizeof want, "%s\n", lang_code(i18n_lang()));
	if ((f = fopen(path, "r"))) {
		if (!fgets(had, sizeof had, f)) had[0] = '\0';
		fclose(f);
	}
	if (!strcmp(had, want)) return;
	if ((f = fopen(path, "w"))) {
		fputs(want, f);
		fclose(f);
	}
}

/* And the brightness on the card too, P_USERDATA/BOOT_BRIGHT_FILE: the
 * backlight's own value at the player's rung (plat_brightness_raw, the
 * device's ladder, whatever its size) and a line end. plastron's init sets
 * the backlight to it as soon as the card is mounted, so the boot screen's
 * update and copy lines and a charger's start are at the player's light on a
 * card whose boot.scr is older than zybright (src/boottheme.h). Written only
 * when it says something else; none where the device gives no value. */
#define BOOT_BRIGHT_FILE "boot-brightness"

static void bright_file_keep(int bright)
{
	char path[512], had[32] = "", want[32];
	FILE *f;

	if (bright < 1) return;
	snprintf(path, sizeof path, "%s/%s", P_USERDATA, BOOT_BRIGHT_FILE);
	snprintf(want, sizeof want, "%d\n", bright);
	if ((f = fopen(path, "r"))) {
		if (!fgets(had, sizeof had, f)) had[0] = '\0';
		fclose(f);
	}
	if (!strcmp(had, want)) return;
	if ((f = fopen(path, "w"))) {
		fputs(want, f);
		fclose(f);
	}
}

void boot_theme_changed(void)
{
	g_boot_theme_at = plat_now_ms() ? plat_now_ms() : 1;
}

static void tick_boot_theme(void)
{
	static bool read, settled;
	static char said[BOOTTHEME_MAX];
	static int bright_seen = -2;
	char line[BOOTTHEME_MAX], file[BOOTTHEME_MAX], zone[64];
	unsigned now = plat_now_ms();
	const int bright = plat_brightness_raw(plat_brightness_get());
	int r;

	/* A brightness changed (MENU, the chord, a game's) is a change of the
	 * boot screen's words too, kept 2 s after the last one as the others are */
	if (bright != bright_seen) {
		if (bright_seen != -2) boot_theme_changed();
		bright_seen = bright;
	}
	if (now - t_boot0 < 10000) return;
	if (g_boot_theme_at && now - g_boot_theme_at < 2000) return;
	/* Asked once, and again only after a change: what it compares is the
	 * Appearance setting with Auto's hours (Night Mode's page) and the
	 * clock's zone, and everything that writes one calls
	 * boot_theme_changed. It was asked on every pass of every
	 * screen's loop, a database read each time, 65 a second on the home:
	 * most of the 530 us a pass that drew nothing spent (2026-11-07). */
	if (settled && !g_boot_theme_at) return;
	settled = true;
	if (!read) {
		FILE *f = fopen("/proc/cmdline", "r");
		char cmd[1024] = "";

		if (f) { if (!fgets(cmd, sizeof cmd, f)) cmd[0] = 0; fclose(f); }
		boottheme_from_cmdline(cmd, said, sizeof said);
		read = true;
	}
	db_get_str(db_lib(), "timezone", zone, sizeof zone, "America/New_York");
	/* Auto's hours, the Night Hours, with it: light from until, dark from
	 * from (theme_hours) */
	{
		int light_min, dark_min;

		theme_hours(&light_min, &dark_min);
		boottheme_words(appearance_id(theme_appearance()), dark_min, light_min, zone,
		                lang_code(i18n_lang()), bright, line, sizeof line, file, sizeof file);
	}
	g_boot_theme_at = 0;
	lang_file_keep();
	bright_file_keep(bright);
	if (boottheme_same(line, said)) return;
	{
		char note[512], last[BOOTTHEME_MAX] = "";
		FILE *f;

		snprintf(note, sizeof note, "%s/boot-theme", P_USERDATA);
		if (!said[0] && (f = fopen(note, "r"))) {
			if (!fgets(last, sizeof last, f)) last[0] = 0;
			last[strcspn(last, "\n")] = 0;
			fclose(f);
			if (!strcmp(last, line)) {
				snprintf(said, sizeof said, "%s", line);
				return;
			}
		}
		r = plat_boot_theme(file);
		if (r > 0) {
			fprintf(stderr, "boot theme: %s, for the next start\n", line);
			if ((f = fopen(note, "w"))) { fprintf(f, "%s\n", line); fclose(f); }
		} else if (r < 0) {
			fprintf(stderr, "boot theme: could not write %s on the boot partition\n", line);
		}
	}
	/* Once either way: a boot partition that will not take it is not asked
	 * again every frame */
	snprintf(said, sizeof said, "%s", line);
}

/* Date & Time's Auto, with its screen: true when it set a new zone */

bool tick_theme(app *a)
{
	bool turned;

	/* The downloads' lanes as this screen draws (src/dlq.h): not while a
	 * button is down, so a held direction scrolls without paying for a
	 * step; a resting screen steps them from its input wait instead */
	if (dlq_busy()) {
		int b;
		bool held = false;

		for (b = 0; b < IN_COUNT && !held; b++) held = a->in.down[b];
		if (!held) dlq_tick();
	}
	xs_tick_behind(a);
	dev_ssh_tick();
	/* NES Player 2's, in the screens over a paused game (its page, the
	 * in-game menu): on with the game, nothing on the shelf */
	p2_tick(plat_now_ms());
	/* The Link Cable's, the same way: its beat goes on while the game is
	 * paused, so a menu open is never a lost link */
	link_tick(plat_now_ms());
	if (!g_ctl_shelf_pass) ctl_poll(a, CTL_AT_SCREEN);
	tick_update_trial();
	/* A list's cursor showing a language, a zone, an accent or a theme that
	 * is not kept (src/ds/hover.h): what keeps the boot screen's words, the
	 * night's hours and Auto's zone waits for it to be undone, so none of
	 * them writes the shown one down */
	if (ds_hover_showing()) return false;
	tick_boot_theme();
	tick_update_daily(false);
	/* Night Mode's hour: the table, not the theme, so nothing to redraw */
	plat_night_tick();
	turned = theme_tick();
	/* A zone Auto found is another hour of the day: the corner's clock
	 * says it now, and Appearance's Auto follows it now, as it does a zone
	 * chosen on the row (clock_key) */
	if (tick_auto_zone()) {
		redraw_now();
		if (theme_refresh()) turned = true;
	}
	if (!turned && !g_theme_late) return false;
	/* A turn the game's tick saw is on Player 2's page already */
	if (turned) p2_theme_changed();
	theme_changed(a);
	return true;
}

void draw_background(app *a)
{
	SDL_Renderer *r = a->r;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	/* A wash of the focused system's color along the bottom edge, so the
	 * whole screen belongs to the machine you are looking at. Faint enough to
	 * read as light rather than as a panel. DESIGN.md 7's wash: added light
	 * in Salt & Pepper, as it always was, and in Beard, where light added to
	 * a light ground is gone, the color laid on with an ordinary blend. */
	if (a->screen == SCREEN_GAMES && is_mp4(&a->sys.systems[a->sys_cursor])) {
		SDL_Rect band = { 0, ZYPEROS_SCREEN_H - 240, ZYPEROS_SCREEN_W, 480 };
		ui_lift(r, &band, a->tint, UI_LIFT_WASH);
	}
}

/* Only the unobscured home schedules its animation. Menu backdrops are still. */
static bool g_home_live;
int g_home_phase = -1;

static void draw_systems(app *a)
{

	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	char line[128];

	if (is_mp4(s)) {
		/* MP4 has albums, books and videos rather than games, each counted,
		 * and a kind the card has none of left out. */
		int albums = ml_count(&g_mp4, false), books = ml_count(&g_mp4, true);
		int videos = ml_count_kind(&g_mp4, ML_VIDEOS);
		char al[32] = "", bk[32] = "", vd[32] = "";
		const char *sep = T(S_SHELF_LIST_SEP);

		if (albums) snprintf(al, sizeof al, TN(S_SHELF_ALBUMS, albums), albums);
		if (books)  snprintf(bk, sizeof bk, TN(S_SHELF_BOOKS, books), books);
		if (videos) snprintf(vd, sizeof vd, TN(S_SHELF_VIDEOS, videos), videos);
		snprintf(line, sizeof line, "%s%s%s%s%s", al, al[0] && bk[0] ? sep : "", bk,
		         (al[0] || bk[0]) && vd[0] ? sep : "", vd);
	} else {
		/* Collections' entries are its collections: its games are counted
		 * apart, each once (src/collect.h) */
		int gc = a->sys_cursor == shelf_coll_at(a) ? shelf_coll_games()
		       : sys_games(&a->view[a->sys_cursor])->count;

		/* A shelf of one read "1 games". Favorites hits it first because it
		 * starts empty and grows one at a time, but any system with a single
		 * ROM has always said it. Screenshots counts its pictures. */
		if (shots_shelf(a, a->sys_cursor))
			snprintf(line, sizeof line, TN(S_SHELF_SCREENSHOTS, gc), gc);
		else if (gc > 0)
			snprintf(line, sizeof line, TN(S_SHELF_GAMES, gc), gc);
		else snprintf(line, sizeof line, "%s", T(S_SHELF_NO_GAMES));
	}
	home_item items[CFG_MAX_SYSTEMS];
	home_items(a, items);
	/* A menu over the home: the Grid holds a glide to a new shape until the
	 * menu goes, and starts it as the menu leaves (DESIGN.md 11.1) */
	a->home.covered = mm_depth() > 0;
	uint32_t now = g_home_phase >= 0 ? a->home.animation + (uint32_t)g_home_phase : anim_now();
	uint32_t wait = home_draw(&a->home, a->r, a->home_layout, items,
	                          a->sys.count, a->sys_cursor, line,
	                          is_mp4(s) ? T(S_SHELF_OPEN_MP4)
	                          : shots_shelf(a, a->sys_cursor) ? T(S_GAMES_OPEN) : T(S_SHELF_OPEN_GAMES), now,
	                          g_home_live || g_home_phase >= 0);
	/* The sprite's next frame alone is a focus frame's, not a whole one */
	if (g_home_live && wait != UINT32_MAX) {
		if (home_focus_alone(&a->home, a->home_layout, a->sys.count, a->sys_cursor, now))
			g_focus_at = plat_now_ms() + wait;
		else redraw_at(plat_now_ms() + wait);
	}
}

/* Milliseconds since the focused game last changed.
 *
 * The marquee's clock, and it lives here rather than in ui.c because only this
 * knows what "the subject" is. Moving the cursor - or changing shelves - is a
 * new subject and starts the wait again, which is what keeps a scan quiet. */
int shot_phase = -1;        /* --phase, for the shot harness only */
/* --focus-from MS: the whole frame at that phase, then a focus frame at
 * --phase's (focus_draw), for tools/home-shot-check.py to hold against the
 * whole frame at --phase */
int shot_focus_from = -1;

/* The marquee's clock. It restarts when the subject changes, so a newly chosen
 * name is read from its beginning rather than joined halfway through a scroll.
 *
 * A SLOT PER SUBJECT, not one clock. A menu draws over a live shelf and the
 * shelf asks for its phase every frame either way, so one shared clock would
 * be reset twice a frame by two different subjects and nothing would ever
 * move. Two keys because the shelf's subject is a pair - which system, and
 * which game within it - and either changing is a new subject.
 *
 * shot_phase freezes it at a chosen moment. A still of a moving thing needs
 * one; without it the only way to look at a marquee is to film it. That used
 * to be here and not in the menu's copy of this, which is what having two
 * copies costs. */

static int      mq_last_a[MQ_SLOTS], mq_last_b[MQ_SLOTS];
static unsigned mq_since[MQ_SLOTS];
static bool     mq_primed[MQ_SLOTS];

/* Start this slot's clock over on the next frame.
 *
 * The keys are what a slot normally restarts on - a new game, a new heading -
 * and they are the right rule for a marquee that lives on a screen you are
 * already looking at. They are the wrong rule for a screen you LEAVE and come
 * back to: the keys are identical, so the clock is not reset, and a synopsis
 * reopened resumed halfway down where it had been left. Reported 2026-09-17,
 * and the only way to read a long one from the top was to wait out the rest.
 *
 * So a screen that has just opened says so, rather than the clock trying to
 * infer it from a key that did not change. */
void mq_reset(int who)
{
	if (who >= 0 && who < MQ_SLOTS) mq_primed[who] = false;
}

unsigned mq_phase(int who, int key_a, int key_b)
{
	int      *last_a = mq_last_a, *last_b = mq_last_b;
	unsigned *since  = mq_since;
	bool     *primed = mq_primed;
	unsigned now = plat_now_ms();

	if (shot_phase >= 0) return (unsigned)shot_phase;

	if (!primed[who] || key_a != last_a[who] || key_b != last_b[who]) {
		primed[who] = true;
		last_a[who] = key_a;
		last_b[who] = key_b;
		since[who]  = now;
	}
	return now - since[who];
}

/* The clock a moving ROW runs on: the cursor's when the cursor put it in
 * motion, the screen's when nothing ever will. See MQ_FIXED.
 *
 * The heading is measured rather than compared, because mq_phase's keys are
 * two ints - and a length is enough to notice a different game, which is what
 * a new screen means here. */
unsigned mq_row_phase(bool selected, const char *heading, int n, int sel)
{
	if (selected) return mq_phase(MQ_MENU, sel, 0);
	return mq_phase(MQ_FIXED, heading ? (int)strlen(heading) : 0, n);
}

unsigned title_phase(app *a)
{
	return mq_phase(MQ_SHELF, a->sys_cursor, a->view[a->sys_cursor].cursor);
}


/* The badge_of on MP4's shelf (games_info, gg_info, gl_info): on All, what
 * each card is - an album, a book or a video - since the shelf mixes them;
 * on one kind alone the shelf says it and the cards carry none. */
static int album_badge(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];
	int al;

	if (mp4_kind_shown() != ML_ALL || !v->album || i < 0 || i >= v->list.count)
		return AB_NONE;
	al = v->album[i];
	if (al < 0 || al >= g_mp4.nalbums) return AB_NONE;
	switch (g_mp4.albums[al].kind) {
	case ML_BOOKS:  return AB_BOOK;
	case ML_VIDEOS: return AB_VIDEO;
	default:        return AB_ALBUM;
	}
}


/* Facts are cached for the selected ROM and refreshed after leaving the shelf.
 * File reads happen after selection motion settles, never for every frame.
 * The existing achievement reader remains on the main thread. MP4 keeps its
 * separate player renderer and never enters this path. */
game_page_state g_game_page;
int g_games_phase = -1;

static void game_page_facts(app *a, bool settled)
{
	sysview *v = &a->view[a->sys_cursor];
	if (v->list.count <= 0) { g_game_page.valid = false; return; }
	game_entry *g = &v->list.items[v->cursor];
	unsigned now = plat_now_ms();
	/* A folder (src/shelfdir.h) has no play time, state or achievements */
	if (g->folder) { g_game_page.valid = false; return; }
	bool same = g_game_page.sys == a->sys_cursor && g_game_page.cursor == v->cursor &&
	            !strcmp(g_game_page.file, g->file);
	if (!same || now - g_game_page.seen > 250) g_game_page.valid = false;
	g_game_page.seen = now;
	if (!settled || g_game_page.valid) return;
	int owner = shelf_owner(a, a->sys_cursor, v->cursor);
	const system_cfg *system = &a->sys.systems[owner];
	char path[LIB_PATH * 3];
	long seconds;
	g_game_page.sys = a->sys_cursor;
	g_game_page.cursor = v->cursor;
	snprintf(g_game_page.file, sizeof g_game_page.file, "%s", g->file);
	g_game_page.played[0] = '\0';
	/* A shot (DESIGN.md 11.68): its line is its system and when it was
	 * taken (shots_facts), none of its game's; nothing read */
	if (shots_shelf(a, a->sys_cursor)) {
		g_game_page.achievements = g_game_page.resume = g_game_page.crashed = false;
		g_game_page.earned = g_game_page.total = 0;
		g_game_page.needs[0] = g_game_page.cheats[0] = g_game_page.cheats_on[0] = '\0';
		g_game_page.valid = true;
		return;
	}
	stats_summarize(STATS_ALL, false, (long)time(NULL));
	if (stats_lookup(system->tag, g->file, &seconds, NULL))
		stats_format(seconds, g_game_page.played, sizeof g_game_page.played);
	state_path(a, owner, g, path, sizeof path);
	g_game_page.resume = !is_port(system) && file_nonempty(path) && access(path, R_OK) == 0;
	chv_path(P_ROMS, system->folder, g->name, path, sizeof path);
	g_game_page.achievements = chv_load(path) && chv_count() > 0;
	g_game_page.earned = chv_earned();
	g_game_page.total = chv_count();
	/* Firmware it cannot start without, said before A is pressed: a few
	 * stats here, as the row stops, and none in a frame. Only a file that
	 * is not there; a file of the wrong size is said when A is pressed,
	 * in full (src/biosfile.h). */
	g_game_page.needs[0] = '\0';
	if (!is_port(system) && bios_needs(system, g->file, lib_is_disc(g->file))) {
		char bios[CFG_STR * 2];
		bios_result br;

		snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
		if (bios_check_file(system, g->file, lib_is_disc(g->file), bios, &br) == BIOS_MISSING)
			bios_message(&br, g_game_page.needs, sizeof g_game_page.needs);
	}
	/* How many of its cheats are on, and whether it stopped last time with
	 * them on (the lab's shelf line; src/cheats_screen.h): a database read,
	 * and its .cht files only for a game that has some on */
	g_game_page.crashed = false;
	g_game_page.cheats[0] = g_game_page.cheats_on[0] = '\0';
	if (cheats_shelf_line(system, g->file, g_game_page.cheats_on, sizeof g_game_page.cheats_on,
	                      &g_game_page.crashed) > 0) {
		if (g_game_page.crashed) {
			snprintf(g_game_page.cheats, sizeof g_game_page.cheats, "%s · %s",
			         g_game_page.cheats_on, T(S_CHEATS_SHELF_CRASHED));
			/* joined on the line, so said to the host's check apart */
			ui_fit_drawn(T(S_CHEATS_SHELF_CRASHED));
		}
		else
			snprintf(g_game_page.cheats, sizeof g_game_page.cheats, "%s", g_game_page.cheats_on);
	}
	g_game_page.valid = true;
}
/* ---- the focused game's video snap (src/snapshelf.h) --------------------
 *
 * Stepped from draw_games, from every loop that draws the shelf: in front
 * only when it is the shelf's own loop drawing (g_home_live), the row is
 * still and the screen lit, so a menu opening over it fades the cover back
 * and stops the daemon, the same frame. The places that leave the shelf
 * without drawing it again - a game, MP4, the dark screen on the cable, the
 * power button, the systems row - call snap_off. */
static snapshelf g_snap_shelf;
float     g_shot_snap_k = -1;      /* --snap K, for a shot */
float     g_shot_snap_wait = -1;   /* --snap-wait F, for a shot */
float     g_shot_band = -1;        /* --band F, the Video view's band or the List view's details, for a shot */
static char      g_snap_asked[1024];
static bool      g_snap_held;             /* g_snap_asked is only its first picture */
static int       g_snap_vol;              /* and the sound it was asked with */
/* A sound the next ask takes in place of the setting's, -1 for the setting:
 * the Cover and Video page's picture plays muted, Watch at full */
int              g_snap_force_vol = -1;
/* `es_wait`: the game's folder had no EmulationStation index yet when its
 * file was worked out (src/esmedia.h), so it is worked out again once there
 * is one - the covers' worker builds it, a moment after the shelf asks.
 * `gen`: the card's media then (snapshelf_media_gen), so a run that brought
 * this game's snap has it worked out again (snapshelf_refocus) */
static struct {
	int sys, cursor;
	char file[LIB_PATH], path[LIB_PATH * 2];
	bool es_wait;
	unsigned gen;
} g_snap_focus;

/* Its sound as set, and none while MP4 plays somebody's music. Read when a
 * snap is asked for: on every frame it was two settings reads from the
 * database (snaps_sound) for a value only a new ask uses. */
static int snap_volume(void)
{
	int s;

	if (mp4c_heard()) return 0;
	s = snaps_sound();
	return s == SNAP_FULL ? 100 : s == SNAP_QUIET ? 25 : 0;
}

/* `hold`: its first picture only, while the bar fills (snapshelf.h). Let go,
 * the same snap keeps the picture it has: snapv_begin is for a new file
 * only, since it marks every picture on screen as the old one's. */
void snap_ask(const char *req, bool hold)
{
	bool fresh = strcmp(req, g_snap_asked) != 0;
	int vol;

	hold = hold && *req;
	if (!fresh && hold == g_snap_held) return;
	/* Let go with the sound it was held with: a new volume would be a new
	 * request to the daemon, which opens the file again */
	vol = fresh ? (g_snap_force_vol >= 0 ? g_snap_force_vol : snap_volume()) : g_snap_vol;
	g_snap_vol = vol;
	snprintf(g_snap_asked, sizeof g_snap_asked, "%s", req);
	g_snap_held = hold;
	if (fresh) {
		if (*req) snapv_begin();
		else snapv_release();
	}
	mp4c_snap(*req ? req : NULL, vol, hold);
	/* The menu music makes way only when a snap's sound starts */
	bgm_hold(BGM_HOLD_SNAP, *req && !hold && vol > 0);
}

/* For the shelf's loop: a snap is asked for and the daemon has a newer
 * picture of it than the one on screen */
static bool g_video_playing;

bool shelf_video_playing(void)
{
	return g_video_playing;
}

bool snap_new_picture(void)
{
	return g_snap_asked[0] && snapv_pending();
}

static void fx_off(void);

void snap_off(void)
{
	memset(&g_snap_shelf, 0, sizeof g_snap_shelf);
	g_snap_focus.sys = -1;
	snap_ask("", false);
	fx_off();
}

static void shelf_snap(app *a, sysview *v, const game_entry *g, int owner, bool settled,
                       games_info *info)
{
	unsigned now = plat_now_ms();
	const char *req;
	float k = 0;
	int w = 0, h = 0;
	bool front = g_home_live && settled && g, hold = false;

	/* Under a page or a menu, which snapshelf holds the Video view's snap
	 * through (snapshelf.h) */
	g_snap_shelf.covered = !g_home_live;

	/* Which file, worked out when the focus moves rather than every frame */
	if (!g) {
		g_snap_focus.sys = -1;
		g_snap_focus.path[0] = '\0';
	} else if (snapshelf_refocus((snapshelf_at){ g_snap_focus.sys, g_snap_focus.cursor, g_snap_focus.file,
	                                            g_snap_focus.gen },
	                             (snapshelf_at){ a->sys_cursor, v->cursor, g->file, snapshelf_media_gen() }) ||
	           (g_snap_focus.es_wait && es_ready(a->sys.systems[owner].folder))) {
		const char *folder = a->sys.systems[owner].folder;
		bool on = snaps_play() && folder[0] && snaps_console_on(folder);

		g_snap_focus.sys = a->sys_cursor;
		g_snap_focus.cursor = v->cursor;
		g_snap_focus.gen = snapshelf_media_gen();
		snprintf(g_snap_focus.file, sizeof g_snap_focus.file, "%s", g->file);
		g_snap_focus.path[0] = '\0';
		g_snap_focus.es_wait = false;
		/* Its own snap, else EmulationStation's video if its folder has
		 * one (src/esmedia.h's game_media). ES's index is not built on the
		 * frame: asked again once the covers' worker has built it. */
		if (on && !game_media(P_ROMS, folder, g->file, g->name, ES_VIDEO, false,
		                      g_snap_focus.path, sizeof g_snap_focus.path))
			g_snap_focus.es_wait = !es_ready(folder);
	}
	req = snapshelf_step(&g_snap_shelf, g ? g_snap_focus.path : "", front,
	                     snapv_texture(NULL, NULL, NULL) != NULL, now, &hold, &k);
	snap_ask(req, hold);
	if (*req) snapv_update(a->r);
	if (snapshelf_moving(&g_snap_shelf)) redraw_now();
	/* While it waits, a frame when the wait is over. While it plays, nothing
	 * here: the shelf's loop draws when the daemon has put out a newer
	 * picture (snap_new_picture), so the shelf is drawn exactly as often
	 * as the snap changes - measured on the Pixel 2 2026-11-06, a timer
	 * here and a redraw on every upload drew 42 frames a second for a
	 * 30 fps snap, at 31% of a core. */
	/* While it waits, the bar under the cover fills (DESIGN.md 11.19), so
	 * the wait draws every frame - one second of them, once the shelf has
	 * stopped on a game that has a snap. Held full while the daemon brings
	 * the first picture, then it fades with the cover as it turns. */
	info->snap_wait = 0;
	if (g_snap_shelf.phase == SNS_WAIT && g_snap_focus.path[0]) {
		info->snap_wait = (float)(now - g_snap_shelf.t) / SNAPS_WAIT_MS;
		if (info->snap_wait > 1) info->snap_wait = 1;
		if (g_home_live) redraw_now();
	} else if (g_snap_shelf.phase == SNS_ASKED || g_snap_shelf.phase == SNS_IN) {
		info->snap_wait = 1;
	}
	info->snap = k > 0 ? snapv_texture(&w, &h, &info->snap_src) : NULL;
	/* A host shot of the turn (--snap K): the test picture, K of the way */
	if (g_shot_snap_k >= 0 && g) {
		if (!mp4v_texture(NULL)) mp4v_test_picture(a->r, 320, 240);
		info->snap = mp4v_texture(NULL);
		if (info->snap) SDL_SetTextureBlendMode(info->snap, SDL_BLENDMODE_BLEND);
		w = 320; h = 240;
		info->snap_src = (SDL_Rect){ 0, 0, 0, 0 };
		k = g_shot_snap_k;
	}
	/* A host shot of the wait (--snap-wait F): the bar F of the way */
	if (g_shot_snap_wait >= 0 && g) info->snap_wait = g_shot_snap_wait;
	info->snap_w = w;
	info->snap_h = h;
	info->snap_k = info->snap ? k : 0;
}

/* ---- the focused card with no art: one sheen, and its sharp frame ---------
 *
 * src/focusfx.h's timeline, on the cover row: the light once across the
 * card as the row stops on it, and for a played game the card with its frame
 * of play sharp, asked of the art workers then (SHELF_SHARP_SYS) and drawn
 * over the blurred one as it comes. One sharp card at a time, the focused
 * one's, dropped as the focus moves. */
static focusfx g_fx = { -1, -1, false, 0, 0 };
static SDL_Texture *g_fx_sharp;
static int g_fx_sharp_sys = -1, g_fx_sharp_idx = -1;
static int g_fx_seen_sys = -1, g_fx_seen_idx = -1;
static bool g_fx_played;
float g_shot_sheen = -1;     /* --sheen T, for a shot */
float g_shot_sharp = -1;     /* --sharp K, for a shot */

static void fx_drop_sharp(void)
{
	if (g_fx_sharp) SDL_DestroyTexture(g_fx_sharp);
	g_fx_sharp = NULL;
	g_fx_sharp_sys = g_fx_sharp_idx = -1;
}

static void fx_off(void)
{
	focusfx_reset(&g_fx);
	fx_drop_sharp();
	g_fx_seen_sys = g_fx_seen_idx = -1;
}

void shelf_focus_sharp_put(app *a, int sys, int idx, SDL_Surface *s, bool card, int theme_ix)
{
	if (s && card && sys == g_fx.sys && idx == g_fx.idx && g_fx.settled) {
		fx_drop_sharp();
		if ((g_fx_sharp = ui_card_upload(a->r, s, theme_ix))) {
			g_fx_sharp_sys = sys;
			g_fx_sharp_idx = idx;
			redraw_now();
		}
	}
	if (s) ui_card_free(s);
}

static void shelf_focusfx(app *a, sysview *v, const game_entry *g, int owner, bool settled,
                          unsigned now, games_info *info)
{
	int s = g ? a->sys_cursor : -1, i = g ? v->cursor : -1;
	const gg_art *e = g && v->tex[i] ? gg_art_of(&v->gart, i, v->tex[i], gg_key(g->file)) : NULL;
	bool generated = e && e->generated;
	focusfx_out o;

	/* Whether the card shows a frame of play, read once a focus */
	if (s != g_fx_seen_sys || i != g_fx_seen_idx) {
		char p[LIB_PATH * 2];
		struct stat st;

		g_fx_seen_sys = s;
		g_fx_seen_idx = i;
		g_fx_played = false;
		if (g) {
			card_frame_path(a, owner, g, p, sizeof p);
			g_fx_played = stat(p, &st) == 0 && st.st_size > 0;
			if (!g_fx_played) {
				preview_path(a, owner, g, p, sizeof p);
				g_fx_played = stat(p, &st) == 0 && st.st_size > 0;
			}
		}
	}
	if (g_fx_sharp && (g_fx_sharp_sys != s || g_fx_sharp_idx != i)) fx_drop_sharp();
	/* In front only when it is the shelf's own loop drawing, as the snap is:
	 * not behind a menu opened over the shelf, and in a shot only where
	 * --sheen or --sharp says (shelf_snap) */
	settled = settled && g_home_live;
	focusfx_step(&g_fx, s, i, generated, g_fx_played, settled, g_fx_sharp != NULL, now, &o);
	if ((settled || g_shot_sharp >= 0) && generated && g_fx_played && !g_fx_sharp) {
		/* Asked while it is not in hand: an ask in flight is ignored */
		ui_card_order card;

		ui_card_order_set(&card, g->title, a->sys.systems[owner].accent, false);
		game_card_order(a, owner, g, &card);
		card.sharp = true;
		if (!texload_want_or_draw(SHELF_SHARP_SYS(s), i, NULL, NULL, &card)) {
			/* No worker (a shot): made here */
			int w, h;

			if ((g_fx_sharp = ui_make_card_for(a->r, &card, &w, &h, NULL))) {
				g_fx_sharp_sys = s;
				g_fx_sharp_idx = i;
			}
		}
	}
	if (o.moving) redraw_now();
	info->sheen = o.sheen;
	info->sharp = g_fx_sharp && o.sharp > 0 ? g_fx_sharp : NULL;
	info->sharp_k = o.sharp;
	if (g_shot_sheen >= 0 && generated) info->sheen = g_shot_sheen;
	if (g_shot_sharp >= 0 && generated && g_fx_sharp) {
		info->sharp = g_fx_sharp;
		info->sharp_k = g_shot_sharp;
	}
}

/* The snaps a folder holds, by stem and sorted, for the mark on its covers
 * (DESIGN.md 11.19): one listing of its .media, taken when its shelf is first
 * drawn and again only when that folder has changed - looked at no more than
 * every two seconds - so a frame asks no file and no setting per card. The
 * Favorites shelf draws from many folders, so a few are kept. */
#define SNAP_NAMES_KEEP 8
static snap_names g_snap_names[SNAP_NAMES_KEEP];
static int g_snap_names_next;

int snap_name_cmp(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

const snap_names *snap_names_for(const char *folder)
{
	unsigned now = plat_now_ms();
	snap_names *s = NULL;
	char dir[CFG_STR * 4];
	struct stat st;
	struct dirent *e;
	DIR *d;
	int i, cap = 0;

	for (i = 0; i < SNAP_NAMES_KEEP; i++)
		if (g_snap_names[i].folder[0] && !strcmp(g_snap_names[i].folder, folder)) {
			s = &g_snap_names[i];
			break;
		}
	/* A run's snaps listed at once, whatever the folder's time says: a FAT
	 * card keeps it to 2 s (snapshelf_media_changed) */
	if (s && now - s->looked < 2000 && s->gen == snapshelf_media_gen()) return s;
	snprintf(dir, sizeof dir, "%s/%s/.media", P_ROMS, folder);
	if (stat(dir, &st) != 0) st.st_mtime = 0;
	if (!s) {
		s = &g_snap_names[g_snap_names_next];
		g_snap_names_next = (g_snap_names_next + 1) % SNAP_NAMES_KEEP;
		for (i = 0; i < s->n; i++) free(s->stems[i]);
		free(s->stems);
		memset(s, 0, sizeof *s);
		snprintf(s->folder, sizeof s->folder, "%s", folder);
		s->mtime = (time_t)-1;
	}
	s->looked = now;
	s->on = snaps_play() && snaps_console_on(folder);
	if (s->mtime == st.st_mtime && s->gen == snapshelf_media_gen()) return s;
	s->gen = snapshelf_media_gen();
	for (i = 0; i < s->n; i++) free(s->stems[i]);
	free(s->stems);
	s->stems = NULL;
	s->n = 0;
	s->mtime = st.st_mtime;
	if (!st.st_mtime || !(d = opendir(dir))) return s;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char *stem;

		if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ".mp4")) continue;
		if (s->n == cap) {
			char **w = realloc(s->stems, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *w);

			if (!w) break;
			s->stems = w;
		}
		if (!(stem = strndup(e->d_name, len - 4))) break;
		s->stems[s->n++] = stem;
	}
	closedir(d);
	if (s->n) qsort(s->stems, (size_t)s->n, sizeof *s->stems, snap_name_cmp);
	return s;
}

/* games_info.snap_of: whether the shelf's item `i` has a snap to show */
static bool game_has_snap(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];
	const snap_names *s;
	const char *folder, *name;
	int owner;

	if (i < 0 || i >= v->list.count || v->list.items[i].folder) return false;
	owner = shelf_owner(a, a->sys_cursor, i);
	folder = a->sys.systems[owner].folder;
	if (!folder[0] || is_mp4(&a->sys.systems[owner])) return false;
	s = snap_names_for(folder);
	name = v->list.items[i].name;
	if (!s->on) return false;
	/* The screensaver's question too (saver_pool_build) */
	return game_media_listed(folder, v->list.items[i].file, name, ES_VIDEO, s->stems, s->n);
}

/* The heading of a shelf: its system, and on a folder of its games the
 * folders down to it, "Game Boy Advance / Rom Hacks / Pokemon" */
static void shelf_heading(const system_cfg *s, const sysview *v, char *out, size_t n)
{
	const char *p = v->dir;
	size_t at = (size_t)snprintf(out, n, "%s", s->name);

	while (*p && at < n) {
		const char *slash = strchr(p, '/');
		int len = slash ? (int)(slash - p) : (int)strlen(p);
		int k = snprintf(out + at, n - at, " / %.*s", len, p);

		if (k < 0) break;
		at += (size_t)k;
		p += len + (slash ? 1 : 0);
	}
}

/* The list view's rows (src/gamelist.h), from the shelf on screen */
static const char *list_title(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];

	return i >= 0 && i < v->list.count ? v->list.items[i].title : "";
}

static int list_folder(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];

	return i >= 0 && i < v->list.count ? v->list.items[i].folder : 0;
}

static unsigned list_accent(void *ctx, int i)
{
	app *a = ctx;

	return a->sys.systems[shelf_owner(a, a->sys_cursor, i)].accent;
}

static bool list_fav(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];

	/* By the game's own system: on Recents its owner's (src/homesys.h) */
	return i >= 0 && i < v->list.count && !v->list.items[i].folder &&
	       fav_is(a->sys.systems[shelf_owner(a, a->sys_cursor, i)].tag, v->list.items[i].file);
}

/* The pixel folder the list's rows draw (src/folderart.h), in the shelf's
 * color or Folder Color's (look_folder_rgb), made again when the color or
 * the theme changes; nearest so
 * its pixels stay whole; let go with every other texture (free_all_textures) */
static unsigned g_folder_icon_rgb;
static int g_folder_icon_theme = -1;

static SDL_Texture *folder_icon(app *a, unsigned rgb)
{
	int w, h;

	if (g_folder_icon && g_folder_icon_rgb == rgb && g_folder_icon_theme == theme_index())
		return g_folder_icon;
	if (g_folder_icon) SDL_DestroyTexture(g_folder_icon);
	g_folder_icon = ui_make_folder_icon(a->r, rgb, &w, &h);
	g_folder_icon_rgb = rgb;
	g_folder_icon_theme = theme_index();
	return g_folder_icon;
}

/* The grid's (src/gamegrid.h): cover i's texture if it is in hand, without
 * asking for it; what the grid knows of it beside, when that was made for
 * the texture the slot holds now and the game in it */
static SDL_Texture *grid_peek(void *ctx, int i, int *w, int *h, float *cb)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];

	if (i < 0 || i >= v->list.count || !v->tex || !v->tex[i]) return NULL;
	*w = v->tw[i];
	*h = v->th[i];
	if (cb) *cb = v->cb[i];
	return v->tex[i];
}

static gg_art *grid_art(void *ctx, int i)
{
	app *a = ctx;
	sysview *v = &a->view[a->sys_cursor];

	if (i < 0 || i >= v->list.count || !v->tex) return NULL;
	return gg_art_of(&v->gart, i, v->tex[i], gg_key(v->list.items[i].file));
}

/* The grid's rows ahead (DESIGN.md 11.47): the covers of the rows a press
 * can slide in next - a row, a page, the other end - asked for once every
 * cover in sight is in, so they never wait behind them, and the nearest
 * first when there are more than the loader's queue takes. Asked farthest
 * first: the loader decodes the newest ask first. Not in a shot, which reads
 * each cover on the frame and has no next press. */
#define GRID_ASKS_AHEAD 16
static void grid_ask_ahead(app *a, sysview *v)
{
	int cols = gg_cols(&v->gm), rows[2 * (GG_AHEAD + 1)], nrows, n, idx[GRID_ASKS_AHEAD], k = 0;

	if (cols <= 0 || !v->tex || v->gm.holes || !texload_running()) return;
	nrows = (v->list.count + cols - 1) / cols;
	n = gg_rows_ahead((int)lroundf(v->gm.top_to), nrows, rows, 2 * (GG_AHEAD + 1));
	/* nearest row first into idx, then asked from its end */
	for (int r = n - 1; r >= 0 && k < GRID_ASKS_AHEAD; r--)
		for (int i = rows[r] * cols; i < (rows[r] + 1) * cols && i < v->list.count && k < GRID_ASKS_AHEAD; i++)
			if (!v->tex[i]) idx[k++] = i;
	while (k-- > 0) {
		int w, h;

		game_get_tex(a, idx[k], &w, &h, NULL);
	}
}

/* MP4's shelf (DESIGN.md 11.67): the games shelf's own three Views - the
 * cover row (games_draw), the grid (gg_draw) and the list (gl_draw), with
 * their focus, Highlight, motion and no-art cards - given an album's facts.
 * Each album is a cover and a name, as a game is, so they draw it as they
 * draw a game; what differs is what is said of it. The heading is MP4 and
 * the kind Show shows, the artist (an author, a series; album_fact) is the
 * focused album's fact where a Favorites game's system is, each album carries its
 * kind's badge on All, and A opens an album (plays a film). No play time,
 * achievements, resume or heart: those are a game's. The foot is B and
 * START, as MP4 has no X Details or Y on its shelf. */
/* What is said of album k under its name: its artist, whom the letter jump
 * goes by; a finished book says so instead; and a book or a folder of
 * videos with no author or series above it says nothing, rather than its
 * own name again. NULL for nothing. */
static const char *album_fact(const sysview *v, int k)
{
	const game_entry *g = k >= 0 && k < v->list.count ? &v->list.items[k] : NULL;
	int al = g && v->album ? v->album[k] : -1;

	if (!g || al < 0 || al >= g_mp4.nalbums) return NULL;
	if (g_mp4.albums[al].book) {
		if (g_book_done && g_book_done[al]) return T(S_BOOKS_FINISHED);
		if (!strcmp(g->name, g->title)) return NULL;
	} else if (g_mp4.albums[al].kind == ML_VIDEOS && !strcmp(g->name, g->title)) {
		return NULL;
	}
	return g->name[0] ? g->name : NULL;
}

static void draw_album_views(app *a, sysview *v, const system_cfg *s)
{
	static const str_id SHOW[ML_ALL + 1] = { S_SYSMENU_MP4_MUSIC, S_SYSMENU_MP4_AUDIOBOOKS,
	                                         S_SYSMENU_MP4_VIDEOS, S_SYSMENU_MP4_ALL };
	unsigned now = g_games_phase >= 0 ? v->row.moved + g_games_phase : plat_now_ms();
	const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
	int al = g && v->album ? v->album[v->cursor] : -1, kind = (int)mp4_kind_shown();
	bool film = al >= 0 && al < g_mp4.nalbums && g_mp4.albums[al].solo;
	const char *artist = album_fact(v, v->cursor);
	const char *a_word = T(film ? S_GAMES_PLAY : S_GAMES_OPEN);
	char head[CFG_STR + 64];
	key_hint foot[2];
	int nf = 0;
	uint32_t wait;

	/* "MP4 / Music", as a games shelf names the folder it is in, when the
	 * card has more than one kind */
	if (mp4_kinds() > 1 && kind >= 0 && kind <= ML_ALL)
		snprintf(head, sizeof head, "%s / %s", s->name, T(SHOW[kind]));
	else
		snprintf(head, sizeof head, "%s", s->name);
	foot[nf++] = (key_hint){ "B", T(S_GAMES_BACK) };
	if (v->list.count > 0)
		foot[nf++] = (key_hint){ "START", T(v->grid ? S_GAMES_TO_LIST : v->listed ? S_GAMES_TO_COVERS
		                                      : S_GAMES_TO_GRID) };
	if (v->grid) {
		/* Square cells, never measured: MP4 draws every cover square
		 * (cover_shape), the art crops to it, and an album with none is a
		 * square card */
		wait = gg_draw(&v->gm, a->r, &(gg_info){
			.heading = head, .count = v->list.count, .cursor = v->cursor, .accent = s->accent,
			.title_of = list_title, .folder_of = list_folder, .peek = grid_peek, .art_of = grid_art,
			.kept = 1.0f, .played = "", .origin = artist,
			.a_word = a_word, .foot = foot, .nfoot = nf, .badge_of = album_badge,
		}, game_get_tex, a, now, title_phase(a));
		if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
		grid_ask_ahead(a, v);
		evict_grid(v, gg_cols(&v->gm), false);
		g_title_caps = (SDL_Rect){ 0, 0, 0, 0 };
		return;
	}
	if (v->listed) {
		wait = gl_draw(&v->lm, a->r, &(gl_info){
			.heading = head, .count = v->list.count, .cursor = v->cursor, .accent = s->accent,
			.title_of = list_title, .folder_of = list_folder, .played = "", .origin = artist,
			.a_word = a_word, .foot = foot, .nfoot = nf, .badge_of = album_badge,
		}, game_get_tex, a, now, title_phase(a));
		if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
		evict_far(v, TEX_KEEP_NEAR);
		g_title_caps = (SDL_Rect){ 0, 0, 0, 0 };
		return;
	}
	{
		games_info info = {
			.system = head, .title = g ? g->title : "", .count = v->list.count,
			.cursor = v->cursor, .played = "", .origin = artist, .badge_of = album_badge,
			.a_word = a_word, .foot = foot, .nfoot = nf,
		};

		games_select(&v->row, v->cursor, v->list.count, 0, now);
		/* The no-art card's sweep as the row stops on it, as a game's */
		shelf_focusfx(a, v, g, a->sys_cursor, !games_moving(&v->row, now), now, &info);
		wait = games_draw(&v->row, a->r, &info, game_get_tex, a, now, title_phase(a));
		if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
		evict_far(v, TEX_KEEP_NEAR);
		g_title_caps = (SDL_Rect){ 64, 564, 656, 88 };
	}
}

/* On Recents, its system and when it was last played, "PICO-8 · today
 * 14:02", in the words the Screenshots shelf says when a shot was taken
 * (DESIGN.md 11.48; 1.1.0-rc.8: "the recents page should show when was it
 * last played"). When is the session's start, Recents' own order (Last
 * Played), so the line never says a game further along was played later.
 * A game whose last start it has not (none folded for a sort) has its
 * system alone. */
static const char *recents_facts(app *a, int owner, const game_entry *g)
{
	static char line[CFG_STR + 64];
	char when[48];

	if (g->last_played <= 0) return a->sys.systems[owner].name;
	stats_when(g->last_played, (long)time(NULL), g_clock24, when, sizeof when);
	snprintf(line, sizeof line, "%s \xc2\xb7 %s", a->sys.systems[owner].name, when);
	return line;
}

/* The line under the focused entry's name, first: its own system on a
 * shelf of many systems' games (Favorites, Recents, Collections), on
 * Recents with when it was last played, and on the Screenshots shelf its
 * system and when it was taken (DESIGN.md 11.68); NULL on a console's own
 * shelf, whose heading says it */
static const char *shelf_origin(app *a, int owner, const game_entry *g)
{
	if (!g || g->folder) return NULL;
	if (shots_shelf(a, a->sys_cursor)) return shots_facts(a, owner, g);
	if (owner != a->sys_cursor && !strcmp(a->sys.systems[a->sys_cursor].tag, HOMESYS_RECENT))
		return recents_facts(a, owner, g);
	return owner != a->sys_cursor ? a->sys.systems[owner].name : NULL;
}

/* A direction held on the shelf: a snap asked at once waits for its let-go,
 * so a scroll opens no file (DESIGN.md 11.57) */
static bool dir_held(const app *a)
{
	return a->in.down[IN_LEFT] || a->in.down[IN_RIGHT] || a->in.down[IN_UP] ||
	       a->in.down[IN_DOWN] || a->in.down[IN_L1] || a->in.down[IN_R1];
}

static void draw_games(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	if (is_mp4(s)) { snap_off(); draw_album_views(a, v, s); return; }
	unsigned now = g_games_phase >= 0 ? v->row.moved + g_games_phase : plat_now_ms();
	char head[LIB_PATH + CFG_STR + 64];

	shelf_heading(s, v, head, sizeof head);
	if (v->video) {
		/* MENU > View > Video (DESIGN.md 11.57): the focused game's snap on
		 * the whole screen, asked the frame the focus comes rather than
		 * after the shelf's rest (my pick, 2026-11-19); not while a
		 * direction is held, so a scroll opens no file */
		const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
		int owner = g ? shelf_owner(a, a->sys_cursor, v->cursor) : a->sys_cursor;
		bool held = dir_held(a);
		games_info si = { 0 };
		vv_info vi;
		unsigned wait;
		bool shots = shots_shelf(a, a->sys_cursor);
		key_hint sfoot[4];
		int snf = 0;
		const char *saw = NULL;

		games_select(&v->row, v->cursor, v->list.count, 0, now);
		game_page_facts(a, !games_moving(&v->row, now));
		g_snap_shelf.at_once = true;
		shelf_snap(a, v, g && !g->folder && !shots ? g : NULL, owner, !held, &si);
		g_snap_shelf.at_once = false;
		/* A shot (DESIGN.md 11.68): its picture where the snap goes, whole
		 * on the screen, its card above the band until it is in */
		if (shots && g) {
			si.snap = shots_view_tex(a, v, &si.snap_w, &si.snap_h);
			si.snap_src = (SDL_Rect){ 0, 0, 0, 0 };
			si.snap_k = si.snap ? 1 : 0;
			shots_hints(a, v, true, sfoot, &snf, &saw);
		}
		/* A page or a menu over the view keeps its band up, as a press
		 * does: the snap holds under it (snapshelf's `covered`) and would
		 * otherwise rest the band down behind the page */
		if (!g_home_live) snaprest_touch(&v->vm, now);
		vi = (vv_info){
			.heading = head, .title = g ? g->title : NULL,
			/* The heart as the cover row's: not on Favorites, nor a shot */
			.favorite = g && !g->folder && !shots && strcmp(s->tag, HOMESYS_FAV) && !is_mp4(s) &&
			            fav_is(a->sys.systems[owner].tag, g->file),
			.count = v->list.count, .cursor = v->cursor,
			.accent = g ? a->sys.systems[owner].accent : s->accent,
			.played = g_game_page.valid ? g_game_page.played : "",
			.origin = shelf_origin(a, owner, g),
			.has_achievements = g_game_page.valid && g_game_page.achievements,
			.cheats = g_game_page.valid ? g_game_page.cheats : "",
			.resumable = g_game_page.valid && g_game_page.resume,
			.install = pm_is_offer(a, owner, g),
			.howto = splore_is_offer(a, owner, g),
			.opens = splore_is_entry(a, owner, g) && !splore_is_offer(a, owner, g),
			.inside = v->dir[0] != '\0',
			.earned = g_game_page.earned, .total = g_game_page.total,
			.folder = g ? g->folder : 0,
			.start_word = T(S_GAMES_TO_COVERS),
			.snap = si.snap, .snap_w = si.snap_w, .snap_h = si.snap_h,
			.snap_src = si.snap_src, .snap_k = si.snap_k,
			.playing = si.snap && si.snap_k >= 1,
			.band = g_shot_band,
			.foot = shots ? sfoot : NULL, .nfoot = snf,
		};
		/* Somebody watching a snap holds the screensaver; a still shot
		 * does not */
		g_video_playing = vi.playing && !shots;
		wait = vv_draw(&v->vm, &v->row, a->r, &vi, game_get_tex, a, now, title_phase(a));
		if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
		evict_far(v, TEX_KEEP_NEAR);
		g_title_caps = (SDL_Rect){ 0, 0, 0, 0 };
		return;
	}
	if (v->listed || v->grid) {
		/* MENU > View > List (DESIGN.md 11.39): the focused game's video
		 * snap plays in its cover's place (2026-11-13: "the list mode should
		 * also play the video preview"), asked the frame the focus comes, as
		 * the Video view's is, not while a direction is held (my pick in the
		 * List View lab, 2026-11-23); on the grid when it has stopped on it,
		 * after hold-snap */
		const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
		int owner = g ? shelf_owner(a, a->sys_cursor, v->cursor) : a->sys_cursor;
		bool settled = v->grid ? !gg_moving(&v->gm, now) : !gl_moving(&v->lm, now);
		games_info si = { 0 };
		gl_info li;
		unsigned wait;
		bool shots = shots_shelf(a, a->sys_cursor);
		key_hint sfoot[4];
		int snf = 0;
		const char *saw = NULL;

		game_page_facts(a, settled);
		g_snap_shelf.at_once = !v->grid;
		shelf_snap(a, v, g && !g->folder && !shots ? g : NULL, owner, v->grid ? settled : !dir_held(a), &si);
		g_snap_shelf.at_once = false;
		if (shots) shots_hints(a, v, false, sfoot, &snf, &saw);
		if (v->grid) {
			/* MENU > View > Grid (DESIGN.md 11.47): the shape kept for
			 * this shelf read as it opens, what its covers measured kept
			 * once it settles, for its next opening */
			gg_info gi;

			if (!v->gm.set) { v->grid_kept = grid_shape_load(a, a->sys_cursor); v->grid_saved = false; }
			gi = (gg_info){
				.heading = head, .count = v->list.count, .cursor = v->cursor,
				.accent = s->accent, .accent_of = v->owner ? list_accent : NULL,
				.title_of = list_title, .folder_of = list_folder,
				.fav_of = !strcmp(s->tag, HOMESYS_FAV) || is_mp4(s) || shots ? NULL : list_fav,
				.peek = grid_peek, .art_of = grid_art, .kept = v->grid_kept,
				.played = g_game_page.valid ? g_game_page.played : "",
				.origin = shelf_origin(a, owner, g),
				.has_achievements = g_game_page.valid && g_game_page.achievements,
				.cheats = g_game_page.valid ? g_game_page.cheats : "",
				.resumable = g_game_page.valid && g_game_page.resume,
				.install = pm_is_offer(a, owner, g),
				.needs = g_game_page.valid ? g_game_page.needs : "",
				.howto = splore_is_offer(a, owner, g),
				.opens = splore_is_entry(a, owner, g) && !splore_is_offer(a, owner, g),
				.inside = v->dir[0] != '\0',
				.earned = g_game_page.earned, .total = g_game_page.total,
				.snap = si.snap, .snap_w = si.snap_w, .snap_h = si.snap_h,
				.snap_src = si.snap_src, .snap_k = si.snap_k, .snap_wait = si.snap_wait,
				.has_snap = g && !shots && game_has_snap(a, v->cursor),
				.start_word = T(S_GAMES_TO_LIST),
				.a_word = saw, .foot = shots ? sfoot : NULL, .nfoot = snf,
			};
			wait = gg_draw(&v->gm, a->r, &gi, game_get_tex, a, now, title_phase(a));
			if (v->gm.settled && !v->grid_saved) {
				if (fabsf(v->gm.measured - v->grid_kept) > .005f)
					grid_shape_save(a, a->sys_cursor, v->gm.measured);
				v->grid_saved = true;
			}
			if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
			/* the rows that can slide in next, asked and kept; the rows a
			 * glide still shows kept until it ends */
			grid_ask_ahead(a, v);
			evict_grid(v, gg_cols(&v->gm), false);
			g_title_caps = (SDL_Rect){ 0, 0, 0, 0 };
			return;
		}
		li = (gl_info){
			.heading = head, .count = v->list.count, .cursor = v->cursor,
			.accent = s->accent, .accent_of = v->owner ? list_accent : NULL,
			.title_of = list_title, .folder_of = list_folder,
			/* Not on Favorites, where every game is one; on Recents, yes;
			 * not on a shot */
			.fav_of = !strcmp(s->tag, HOMESYS_FAV) || is_mp4(s) || shots ? NULL : list_fav,
			.folder_icon = v->nfold ? folder_icon(a, look_folder_rgb(s->accent)) : NULL,
			.played = g_game_page.valid ? g_game_page.played : "",
			.origin = shelf_origin(a, owner, g),
			.has_achievements = g_game_page.valid && g_game_page.achievements,
			/* the list's panel is narrow: the count, and the crash on a line
			 * of its own in its short words */
			.cheats = g_game_page.valid ? g_game_page.cheats_on : "",
			.cheats_crashed = g_game_page.valid && g_game_page.crashed,
			.resumable = g_game_page.valid && g_game_page.resume,
			.install = pm_is_offer(a, owner, g),
			.needs = g_game_page.valid ? g_game_page.needs : "",
				.howto = splore_is_offer(a, owner, g),
				.opens = splore_is_entry(a, owner, g) && !splore_is_offer(a, owner, g),
			.inside = v->dir[0] != '\0',
			.earned = g_game_page.earned, .total = g_game_page.total,
			.snap = si.snap, .snap_w = si.snap_w, .snap_h = si.snap_h,
			.snap_src = si.snap_src, .snap_k = si.snap_k,
			.has_snap = g && !shots && game_has_snap(a, v->cursor),
			.details = g_shot_band,
			.a_word = saw, .foot = shots ? sfoot : NULL, .nfoot = snf,
		};
		/* A page or a menu over the list keeps its details up, as a press
		 * does: the snap holds under it (snapshelf's `covered`) */
		if (!g_home_live) snaprest_touch(&v->lm.details, now);
		wait = gl_draw(&v->lm, a->r, &li, game_get_tex, a, now, title_phase(a));
		if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
		evict_far(v, TEX_KEEP_NEAR);
		g_title_caps = (SDL_Rect){ 0, 0, 0, 0 };
		return;
	}
	games_select(&v->row, v->cursor, v->list.count, 0, now);
	game_page_facts(a, !games_moving(&v->row, now));
	const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
	int owner = g ? shelf_owner(a, a->sys_cursor, v->cursor) : a->sys_cursor;
	bool folder = g && g->folder, shots = shots_shelf(a, a->sys_cursor);
	key_hint sfoot[4];
	int snf = 0;
	const char *saw = NULL;

	if (shots) shots_hints(a, v, false, sfoot, &snf, &saw);
	games_info info = {
		.system = head, .title = g ? g->title : T(S_SHELF_NO_GAMES),
		.count = v->list.count, .cursor = v->cursor,
		.played = g_game_page.valid ? g_game_page.played : "",
		.has_achievements = g_game_page.valid && g_game_page.achievements,
		.cheats = g_game_page.valid ? g_game_page.cheats : "",
		.earned = g_game_page.earned, .total = g_game_page.total,
		.resumable = g_game_page.valid && g_game_page.resume,
		/* PortMaster's entry while it is not on the card: A installs it */
		.install = pm_is_offer(a, owner, g),
		.needs = g_game_page.valid ? g_game_page.needs : "",
				.howto = splore_is_offer(a, owner, g),
				.opens = splore_is_entry(a, owner, g) && !splore_is_offer(a, owner, g),
		.origin = shelf_origin(a, owner, g),
		/* No video snap's mark on a shot, and no snap */
		.snap_of = shots ? NULL : game_has_snap,
		/* The heart before the name, not on Favorites (2026-11-09), and on
		 * Recents by the game's own system; not on a shot */
		.favorite = g && !folder && !shots && strcmp(s->tag, HOMESYS_FAV) && !is_mp4(s) &&
		            fav_is(a->sys.systems[owner].tag, g->file),
		/* A folder of games (src/shelfdir.h): A opens it */
		.folder = folder ? g->folder : 0,
		.inside = v->dir[0] != '\0',
		.a_word = saw, .foot = shots ? sfoot : NULL, .nfoot = snf,
	};
	shelf_snap(a, v, folder || shots ? NULL : g, owner, !games_moving(&v->row, now), &info);
	shelf_focusfx(a, v, folder ? NULL : g, owner, !games_moving(&v->row, now), now, &info);
	unsigned wait = games_draw(&v->row, a->r, &info, game_get_tex, a, now, title_phase(a));
	if (wait != UINT32_MAX) redraw_at(plat_now_ms() + wait);
	evict_far(v, TEX_KEEP_NEAR);
	g_title_caps = (SDL_Rect){64,564,656,88};
}

/* The shelf, without presenting it: the options menu draws over a live one, so
 * the cards keep their tint easing behind the panel rather than freezing into
 * a still. */
/* Nothing on the card at all.
 *
 * Hiding empty systems has an edge that hiding one system does not: if every
 * system is empty the shelf is simply blank, and a blank screen is what a
 * broken launcher looks like. Someone who has just written a card, or put the
 * ROMs one directory too deep, needs to be told where the games go rather
 * than left to conclude the device is dead. The path is named because that is
 * the actual question being asked. */
/* ---------- the empty home (src/emptyhome.h) ------------------------------ */

/* No game on the card: nothing on the home but MP4, if that */
bool no_games(const app *a)
{
	/* The Ports shelf with PortMaster's entry alone, not installed yet, is
	 * no game: the card still has none to play (src/pminstall.h) */
	for (int i = 0; i < a->sys.count; i++)
		if (!is_mp4(&a->sys.systems[i]) && !pm_only_offer(a, i)) return false;
	return true;
}

/* What the empty home says, read again twice a second: the port and the
 * radio change under it as the cable or the adapter go in and out.
 * g_eh_shot is --ways's stand-in for a shot: -1 none, else EH_* bits, with
 * 4 for a Brick-like device (no shared port, Wi-Fi built in). */
static eh_state g_eh;
int g_eh_shot = -1;
static void eh_poll(bool force)
{
	static unsigned at, wifi_at;
	static bool wifi_net;
	static char last_addr[32];
	unsigned now = plat_now_ms();
	eh_state s;
	char addr[32] = "";

	if (!force && g_eh.cable_addr[0] && now - at < 500u) return;
	at = now;
	memset(&s, 0, sizeof s);
	s.music = g_mp4.ntracks > 0;
	if (g_eh_shot >= 0) {
		s.ok[EH_USB] = g_eh_shot & (1 << EH_USB);
		s.ok[EH_WIFI] = g_eh_shot & (1 << EH_WIFI);
		s.shared_port = !(g_eh_shot & 4);
		s.builtin_wifi = !s.shared_port;
		snprintf(s.cable_addr, sizeof s.cable_addr, "%s", s.shared_port ? "zyperos.local" : "10.55.0.2");
		s.drive = s.shared_port;
	} else {
		/* One port for the computer and the Wi-Fi adapter (the Pixel 2's,
		 * usblink.h), or a port that links on its own beside Wi-Fi of the
		 * device's own (the Brick's): the profile says which */
		s.shared_port = plat_profile()->usb_shared_port;
		s.builtin_wifi = plat_profile()->wifi == DEV_WIFI_BUILTIN;
		s.ok[EH_USB] = plat_cable_link(addr, sizeof addr);
		if (addr[0]) snprintf(last_addr, sizeof last_addr, "%s", addr);
		/* The Pixel 2 answers zyperos.local on the cable (mdns.h), a name
		 * to type where 10.42.0.1 is numbers to copy */
		snprintf(s.cable_addr, sizeof s.cable_addr, "%s",
		         s.shared_port && last_addr[0] ? "zyperos.local" : last_addr);
		s.drive = usb_drive_offered();
		/* Wi-Fi File Transfer shows its address on a network, or makes its
		 * own where it can (xfer_wants_hotspot): only the first needs
		 * wpa_cli, which forks, so it is asked every two seconds */
		if (plat_has_wifi()) {
			if (hotspot_available()) {
				s.ok[EH_WIFI] = true;
			} else {
				if (!wifi_at || now - wifi_at >= 2000u) {
					char ip[64] = "";

					wifi_at = now;
					wifi_net = wifi_status(NULL, 0, ip, sizeof ip) == WIFI_CONNECTED && ip[0];
				}
				s.ok[EH_WIFI] = wifi_net;
			}
		}
	}
	if (memcmp(&s, &g_eh, sizeof s)) redraw_now();
	g_eh = s;
}

/* The games the home now has, Favorites and MP4 aside */
int games_on_card(const app *a)
{
	int i, n = 0;

	for (i = 0; i < a->sys.count; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (is_mp4(sc) || (!sc->core[0] && !sc->folder[0])) continue;
		n += sys_games_c(&a->view[i])->count - pm_shelf_offers(a, i);
	}
	return n;
}

void cheer_zyper(SDL_Renderer *r, float cx, float cy, float h, float alpha, void *ctx)
{
	cheer_ctx *c = ctx;
	zr_pose p = zr_pose_at(&c->z, c->now);

	zyper_shadow_draw(r, cx, cy, h, p.tilt, alpha);
	draw_zyper_pose(r, cx, cy, h, &p, zyper_brow_lift(p.tilt), 0.0f, (Uint8)(alpha * 255 + 0.5f));
}

/* The first games are on the card: the home stands on the first shelf of
 * them. The rescan keeps the cursor on what it was on, which with no games
 * was MP4 when the card has music (found on the Pixel 2, 2026-11-06). */
static void empty_arrived(app *a)
{
	for (int i = 0; i < a->sys.count; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (is_mp4(sc) || (!sc->core[0] && !sc->folder[0]) || pm_only_offer(a, i)) continue;
		a->sys_cursor = i;
		a->screen = SCREEN_SYSTEMS;
		a->tint = sc->accent;
		home_reset(&a->home, i, a->sys.count, plat_now_ms());
		break;
	}
}

/* The first games are on the card (My pick in the empty home lab): the
 * cheer over the home they made, with the tutorial's reward sound, Zyper's
 * hop and pant; any button ends it early, and is not passed on */
static void empty_cheer(app *a)
{
	int games = games_on_card(a);
	unsigned t0 = anim_now(), t;
	cheer_ctx c;

	if (games <= 0) return;
	zr_init(&c.z, t0, t0);
	zr_works(&c.z, t0);
	uisound_play(DS_SOUND_REWARD);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (a->running && !want_quit && (t = anim_now() - t0) < eh_cheer_ms()) {
		bool any = false;

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		for (int b = 0; b < IN_COUNT && !any; b++) any = a->in.pressed[b];
		if (any) break;
		c.now = anim_now();
		tick_tint(a);
		draw_shelf(a);
		eh_cheer_draw(a->r, games, t, cheer_zyper, &c);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	redraw_now();
}

/* A and Y on the empty home: the way, if it can start; deny if not */
void empty_keys(app *a)
{
	int w = a->in.pressed[IN_ACCEPT] ? EH_USB : a->in.pressed[IN_Y] ? EH_WIFI : -1;

	if (w < 0) return;
	eh_poll(true);
	if (!g_eh.ok[w]) {
		uisound_play(DS_SOUND_DENY);
		return;
	}
	uisound_play(DS_SOUND_ACCEPT);
	/* The screen rescans the card on its way out when anything changed, and
	 * the home is then the games' (draw_shelf asks no_games every frame) */
	xfer_screen_way(a, w == EH_USB ? 1 : 0);
	if (a->running && !no_games(a)) {
		empty_arrived(a);
		empty_cheer(a);
	}
}

/* Games that come by another way - a card reader, SSH, the file manager of
 * another screen - reach the empty home too: Roms/ and every folder in it
 * are looked at every two seconds, and a change rescans. Each folder's
 * time changes when a file goes in or out of it. */
void empty_watch(app *a, bool empty)
{
	static unsigned at;
	static unsigned long long seen;
	static bool have;
	unsigned now = plat_now_ms();
	unsigned long long sig = 1469598103934665603ull;
	struct stat st;
	DIR *d;
	struct dirent *e;

	if (!empty) { have = false; return; }
	if (have && now - at < 2000u) return;
	at = now;
	if (stat(P_ROMS, &st) == 0) sig = sig * 1099511628211ull ^ (unsigned long long)st.st_mtime;
	if ((d = opendir(P_ROMS))) {
		while ((e = readdir(d))) {
			char path[CFG_STR * 2 + 260];

			if (e->d_name[0] == '.') continue;
			snprintf(path, sizeof path, "%s/%s", P_ROMS, e->d_name);
			if (stat(path, &st) == 0)
				sig = (sig ^ (unsigned long long)st.st_mtime ^ (unsigned long long)st.st_size) * 1099511628211ull;
		}
		closedir(d);
	}
	if (have && sig != seen) {
		fprintf(stderr, "empty home: Roms changed, scanning\n");
		rescan_all(a);
		redraw_now();
		if (!no_games(a)) {
			empty_arrived(a);
			empty_cheer(a);
		}
	}
	seen = sig;
	have = true;
}

/* Zyper on the empty home: still, his shadow under him on Beard */
static void empty_zyper(SDL_Renderer *r, float cx, float cy, float h)
{
	zyper_shadow_draw(r, cx, cy, h, ZYPER_TILT_DEG, 1.0f);
	draw_zyper(r, cx, cy, h, ZYPER_TILT_DEG, 1.0f, 0.0f, 255);
}

static void draw_no_games(app *a)
{
	eh_poll(false);
	eh_draw(a->r, &g_eh, empty_zyper);
}

/* While MENU > Storage's page is up, the shelf's place under every panel it
 * opens - a question, "Deleting...", a result - is the page's own, so the
 * question is asked over what it is about (storage_screen) */
void (*g_base_page)(app *, void *);
void *g_base_ctx;

void draw_shelf(app *a)
{
	if (g_base_page) {
		g_frame_no++;
		texload_drain(a);
		g_base_page(a, g_base_ctx);
		return;
	}
	g_frame_no++;
	/* Install whatever the workers finished, before anything is drawn.
	 *
	 * Here rather than in the main loop, because the main loop is one of
	 * seventeen places that draw the shelf - every menu and overlay shows it
	 * behind itself, each from its own loop. Draining only in the main loop
	 * meant that while any of those was open, a card not yet decoded stayed
	 * blank for as long as the screen was up, and finished decodes piled into
	 * the result ring and were discarded once it filled. Everything that draws
	 * the shelf wants its textures, so the thing that draws the shelf drains. */
	texload_drain(a);
	prime_home(a);
	draw_background(a);
	g_video_playing = false;           /* the Video view's, below, says otherwise */
	if (no_games(a)) { snap_off(); draw_no_games(a); }
	else if (a->screen == SCREEN_SYSTEMS) { snap_off(); draw_systems(a); }
	else draw_games(a);
	draw_status(a);
	if (a->screen == SCREEN_SYSTEMS) g_title_caps = (SDL_Rect){0};
	/* Not on MP4's own shelf, which is what plays there (mp4_shelf_shown) */
	if (!mp4_shelf_shown(a))
	{
		/* The song's name slides when it is wider than its room, on its
		 * own clock, started over by a new song */
		uint32_t hash = 5381, w;
		const char *c;

		for (c = g_home_song.title; *c; c++) hash = hash * 33 + (unsigned char)*c;
		w = home_header(a->r, &g_home_song, g_corner_box.w ? g_corner_box.x : STATUS_RIGHT,
		                mq_phase(MQ_SONG, (int)hash, g_home_song.active));
		if (w != UINT32_MAX) redraw_at(plat_now_ms() + w);
	}
}

/* ---------- the song pop-up ------------------------------------------------ */

/* Zyper saying the menu music's new song, on the home and the games shelf
 * (DESIGN.md 11.22, src/songpop.h; 2026-11-06). The main loop's alone:
 * it is put away when another screen has had the display (SONG_GAP_MS), when
 * the music stops being heard, and it is never drawn over a menu, a game or
 * MP4's screens. With MP4 playing behind the shelf, L2/R2 move MP4 instead, and the
 * pop-up says MP4's track (g_songpop_mp4). */
songpop  g_songpop;
zr_rig   g_songpop_zyper;
static unsigned g_songpop_drawn;       /* when render last drew it */
static unsigned g_song_seen;           /* bgm_song's seq last looked at */
bool     g_song_popup = true;   /* MENU > Menu Music > Song Pop-up */
static bool     g_songpop_mp4;         /* the words up are MP4's, not the menu music's */

/* A new song the shelf first sees this long after it began began under
 * something else - a menu, the tutorial - and is not said late */
#define SONG_FRESH_MS 1500
/* The main loop coming round this long after the pop-up's last frame: some
 * other screen had the display meanwhile, and the pop-up went with the shelf */
#define SONG_GAP_MS   250
/* Its top edge: MENU_TOP, a panel's, clear of the header and the volume bar
 * over it (DESIGN.md 11.12) */
#define SONG_TOP      96
/* Zyper as tall as in the menus (DESIGN.md 10) */
#define SONG_ZYPER_H  64.0f
/* The slab at its widest is the standard panel's (DESIGN.md 5), and a song
 * wider than its line there scrolls. Its padding is the notice's
 * (src/notice.c): a third of the menu line at the sides, a sixth above and
 * below. */
static int song_pad_x(void)  { return ui_font_line(UI_F_MENU) / 3; }
static int song_pad_y(void)  { return ui_font_line(UI_F_MENU) / 6; }
static int song_zyper_w(void) { return (int)(SONG_ZYPER_H * ZYPER_W / ZYPER_H + 0.5f); }
static int song_text_w(void) { return DS_PANEL_W - 3 * song_pad_x() - song_zyper_w(); }

/* How long it stays after its words change: **hold-long**, a two-line
 * message's, or until a name too long for it has scrolled to its end and
 * rested there, if that is longer */
unsigned song_hold(const char *head, const char *body)
{
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	int hover = fh ? ui_text_width(fh, head) - song_text_w() : 0;
	int bover = fb ? ui_text_width(fb, body) - song_text_w() : 0;
	unsigned reach = ui_pingpong_reach(hover > bover ? hover : bover);

	return reach > DS_MS_HOLD_LONG ? reach : DS_MS_HOLD_LONG;
}

/* What MP4 is playing, in the pop-up's two lines (songpop_mp4_words): a
 * chapter as Now Playing names it */
static void mp4_song_words(char *head, size_t hn, char *body, size_t bn)
{
	const mu_now *mn = mp4c_now();
	char chap[128] = "";

	if (mp4c_is_book() && mp4c_chapters() > 1)
		chapter_label(mp4c_chapter_now() < 0 ? 0 : mp4c_chapter_now(), chap, sizeof chap);
	songpop_mp4_words(mn->title, mn->artist, mn->album, chap, mp4c_is_book(),
	                  head, hn, body, bn);
}

/* The pop-up with MP4's words, after an L2 or R2 moved it: up as the press
 * lands, with the file's name, which is all MP4 knows of a track it has only
 * just asked for, then its tags a moment later, when they come (song_watch) */
static void mp4_song_said(unsigned now)
{
	char head[BGM_TAG_MAX], body[BGM_TAG_MAX];

	if (!g_song_popup) return;
	mp4_song_words(head, sizeof head, body, sizeof body);
	if (!g_songpop.on) zr_init(&g_songpop_zyper, now, now ^ 0x4d5034u);
	g_songpop_mp4 = true;
	songpop_show(&g_songpop, head, body, song_hold(head, body), now);
	zr_works(&g_songpop_zyper, now);
	g_songpop_drawn = now;
	redraw_now();
}

/* While MP4's pop-up is up its words follow MP4's: a new track's tags, the
 * next chapter a book reaches, the track after one that ended. Changed in
 * place; no second hop. */
static void mp4_song_follow(unsigned now)
{
	char head[BGM_TAG_MAX], body[BGM_TAG_MAX];

	mp4_song_words(head, sizeof head, body, sizeof body);
	if (!strcmp(head, g_songpop.head) && !strcmp(body, g_songpop.body)) return;
	songpop_show(&g_songpop, head, body, song_hold(head, body), now);
	redraw_now();
}

/* Whatever the player says was news since the last look, said: once a pass of
 * the main loop, after the music is let go on the shelf */
void song_watch(void)
{
	unsigned now = plat_now_ms();
	char head[BGM_TAG_MAX], body[BGM_TAG_MAX];
	bool was = g_songpop.on, heard;
	bgm_song s;

	bgm_song_now(&s);
	if (s.seq == g_song_seen && !was) return;
	/* MP4's pop-up goes when MP4 is no longer heard: paused by a headset's
	 * button, or its queue run out */
	heard = g_songpop_mp4 ? mp4c_heard() : bgm_audible();
	if (was && (now - g_songpop_drawn > SONG_GAP_MS || !heard || !g_song_popup))
		songpop_hide(&g_songpop);
	if (g_songpop.on && g_songpop_mp4) mp4_song_follow(now);
	if (s.seq == g_song_seen) return;
	g_song_seen = s.seq;
	bgm_song_label(&s.tags, s.file, head, sizeof head, body, sizeof body);
	/* One line a song, what the tags gave, pop-up or not: the way to see why
	 * a pop-up says what it says, on a card nobody here can look at */
	fprintf(stderr, "bgm: %s: %s / %s\n", s.file, head, body);
	if (!g_song_popup || !bgm_audible() || now - s.at > SONG_FRESH_MS) return;
	if (!g_songpop.on) zr_init(&g_songpop_zyper, now, now ^ s.seq);
	g_songpop_mp4 = false;
	songpop_show(&g_songpop, head, body, song_hold(head, body), now);
	/* He hops and pants as each song comes; a hop under way runs on */
	zr_works(&g_songpop_zyper, now);
	g_songpop_drawn = now;
	redraw_now();
}

/* The pop-up at `now`: DESIGN.md 11.7's notice slab, **surface** at 85% with
 * **radius.panel**, Zyper where an achievement's badge is, and beside him
 * where the song is from in **meta** **text-dim** over the song in **label**
 * **text**, a name too wide for the panel scrolling. Centered, its top
 * SONG_TOP, sliding down into place as it fades in. Every frame while it is
 * up: Zyper moves the whole time. */
void song_popup_draw(app *a, unsigned now)
{
	const ds_theme *t = theme();
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	/* With Zyper off (DESIGN.md 3.5) the slab is the two lines alone: his
	 * width and the pad after him come off */
	const int padx = song_pad_x(), pady = song_pad_y(),
	          zw = look_zyper_off() ? -padx : song_zyper_w();
	const int l1 = ui_font_line(UI_F_META), l2 = ui_font_line(UI_F_LABEL);
	int tw, bw, hw, h, w, x, y, tx;
	float k, rise, zcx, zcy;
	zr_pose p;

	if (!fb || !songpop_at(&g_songpop, now, &k, &rise)) return;
	if (!fh) fh = fb;
	hw = ui_text_width(fh, g_songpop.head);
	bw = ui_text_width(fb, g_songpop.body);
	tw = hw > bw ? hw : bw;
	if (tw > song_text_w()) tw = song_text_w();
	/* The notice's two lines, and the quarter of the body's descender room
	 * taken off the bottom */
	h = l1 + l2 + pady * 2 - l2 / 12;
	w = padx + zw + padx + tw + padx;
	x = (ZYPEROS_SCREEN_W - w) / 2;
	y = SONG_TOP - (int)(rise + 0.5f);
	tx = x + padx + zw + padx;

	/* Lifted as a menu panel is (**elevation.panel**): over the shelf, not
	 * over a game as the notice is, it is a layer laid on what it covers */
	ui_lift_k(a->r, &(SDL_Rect){ x, y, w, h }, CFG_NO_COLOR, UI_LIFT_PANEL, k);
	ui_round_rect(a->r, &(SDL_Rect){ x, y, w, h }, DS_RADIUS_PANEL,
	              ui_col(t->surface, (Uint8)(217 * k + 0.5f)));
	/* The small line is a game's or an artist's name: as long as the song's
	 * at most, and scrolled like it past that */
	ui_text_marquee(a->r, fh, g_songpop.head, tx, ui_line_y(UI_F_META, y + pady),
	                tw, now - g_songpop.changed_at, ui_fade(ui_col(t->text_dim, 255), k));
	ui_text_marquee(a->r, fb, g_songpop.body, tx, ui_line_y(UI_F_LABEL, y + pady + l1),
	                tw, now - g_songpop.changed_at, ui_fade(ui_col(t->text, 255), k));

	/* Zyper standing in the slab's middle at its left, his shadow first on
	 * Beard; his hop takes him up over its edge */
	zcx = x + padx + zw / 2.0f;
	zcy = y + h / 2.0f;
	zr_tick(&g_songpop_zyper, now);
	p = zr_pose_at(&g_songpop_zyper, now);
	if (!look_zyper_off()) {
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		zyper_shadow_draw(a->r, zcx, zcy, SONG_ZYPER_H, p.tilt, k);
		draw_zyper_pose(a->r, zcx, zcy, SONG_ZYPER_H, &p, zyper_brow_lift(p.tilt), 0.0f,
		                (Uint8)(255 * k + 0.5f));
	}
	g_songpop_drawn = now;
	redraw_now();
}

/* ---------- "Getting hot" on the home ------------------------------------ */

/* Zyper saying it himself, in his bubble, as in the tutorial (DESIGN.md 10,
 * 11.10 and 11.23; my pick in the battery and heat lab, 2026-11-06, with
 * his note: a thermometer in it too). On the home and the shelves, never
 * over a menu or a game: he comes up from under the bottom edge at the left
 * in **move**, hopping and panting - a dog cools off that way - and his
 * bubble comes in beside him in **quick** as he lands: "I'm getting hot!" in
 * **label** over "Slowing down to cool off." in **meta**, both in **ground**
 * on **text**, the thermometer at their left in **ground** with its mercury
 * in **warning**, a tail toward him. Held **hold-heat**, then the bubble out
 * over **fade-out** and he goes down in **exit**. It takes no press, and
 * goes with the shelf when another screen takes the display. */
#define HEATPOP_ZYPER_H 220.0f
#define HEATPOP_ZYPER_X 200.0f    /* his middle */
#define HEATPOP_ZYPER_Y 632.0f    /* his middle, standing */
#define HEATPOP_DROP    260.0f    /* how far under that he starts: out of sight */
#define HEATPOP_BUBBLE_X 334      /* the bubble's left, its tail out of it */
#define HEATPOP_BUBBLE_Y 452      /* its top */
#define HEATPOP_PAD_X   30
#define HEATPOP_PAD_Y   18
#define HEATPOP_GAP     18        /* the thermometer to the words */
#define HEATPOP_LINE1   (g_heatpop.line1[0] ? g_heatpop.line1 : T(S_HEAT_ZYPER))
#define HEATPOP_LINE2   (g_heatpop.line1[0] ? g_heatpop.line2 : T(S_HEAT_SLOWING))

heatpop_state g_heatpop;

/* Its times, from `at`: up, the bubble in, held, out */
#define HEATPOP_IN_END   (DS_MS_MOVE + DS_MS_QUICK)
#define HEATPOP_OUT_AT   (HEATPOP_IN_END + DS_MS_HOLD_HEAT)
#define HEATPOP_END      (HEATPOP_OUT_AT + (DS_MS_FADE_OUT > DS_MS_EXIT ? DS_MS_FADE_OUT : DS_MS_EXIT))

void heatpop_show(unsigned now)
{
	g_heatpop.line1[0] = g_heatpop.line2[0] = '\0';
	g_heatpop.on = true;
	g_heatpop.at = now;
	g_heatpop.drawn = now;
	zr_init(&g_heatpop.rig, now, now ^ 0x4e47u);
	zr_works(&g_heatpop.rig, now + DS_MS_MOVE);
	redraw_now();
}

/* The thermometer as a texture, `h` tall, made again for a theme or a
 * renderer it was not made for */
static void heatpop_mark(SDL_Renderer *r, int x, int y, int h, Uint8 alpha)
{
	static SDL_Texture  *tex;
	static SDL_Renderer *tex_for;
	static int           made = -1, mw, mh;
	const ds_theme *t = theme();

	if (tex_for != r || made != theme_index() || mh != h) {
		SDL_Surface *s = notice_heat_mark(h, t->ground, t->warning);

		if (tex) SDL_DestroyTexture(tex);
		tex = NULL;
		tex_for = r;
		made = theme_index();
		mh = h;
		if (!s) return;
		mw = s->w;
		tex = SDL_CreateTextureFromSurface(r, s);
		SDL_FreeSurface(s);
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	}
	if (!tex) return;
	SDL_SetTextureAlphaMod(tex, alpha);
	SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){ x, y, mw, mh });
}

/* The thermometer's height: the two lines', less a little */
static int heatpop_mark_h(void)
{
	return ui_font_line(UI_F_LABEL) + ui_font_line(UI_F_META) - 8;
}

void heatpop_draw(app *a, unsigned now)
{
	const ds_theme *t = theme();
	TTF_Font *f1 = ui_font(UI_F_LABEL), *f2 = ui_font(UI_F_META);
	unsigned since;
	float up = 1.0f, op = 1.0f, cy;
	int mh, mw, w1, w2, w, h, x, y;
	zr_pose p;

	if (!g_heatpop.on) return;
	since = now - g_heatpop.at;
	if (since >= HEATPOP_END || !f1 || !f2) { g_heatpop.on = false; return; }
	/* Up from under the edge, decelerating; down at the end, accelerating */
	if (since < DS_MS_MOVE) up = ds_out((float)since / DS_MS_MOVE);
	else if (since >= HEATPOP_OUT_AT)
		up = since - HEATPOP_OUT_AT >= DS_MS_EXIT ? 0.0f
		   : 1.0f - ds_in((float)(since - HEATPOP_OUT_AT) / DS_MS_EXIT);
	/* The bubble in as he lands, out over fade-out */
	if (since < DS_MS_MOVE) op = 0.0f;
	else if (since < HEATPOP_IN_END) op = ds_out((float)(since - DS_MS_MOVE) / DS_MS_QUICK);
	else if (since >= HEATPOP_OUT_AT)
		op = since - HEATPOP_OUT_AT >= DS_MS_FADE_OUT ? 0.0f
		   : 1.0f - (float)(since - HEATPOP_OUT_AT) / DS_MS_FADE_OUT;

	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	cy = HEATPOP_ZYPER_Y + HEATPOP_DROP * (1.0f - up);
	zr_tick(&g_heatpop.rig, now);
	p = zr_pose_at(&g_heatpop.rig, now);
	zyper_shadow_draw(a->r, HEATPOP_ZYPER_X, cy, HEATPOP_ZYPER_H, p.tilt, up);
	draw_zyper_pose(a->r, HEATPOP_ZYPER_X, cy, HEATPOP_ZYPER_H, &p, zyper_brow_lift(p.tilt),
	                0.0f, 255);

	if (op > 0.0f) {
		SDL_Color ink = ui_fade(ui_col(t->ground, 255), op), slab = ui_fade(ui_col(t->text, 255), op);
		SDL_Vertex v[3];
		int i, l1 = ui_font_line(UI_F_LABEL), l2 = ui_font_line(UI_F_META);

		/* News (System Update's) has no thermometer, and its lines are
		 * balanced in the room right of him, the first onto two, the
		 * second onto three (the last start's restart says a code and
		 * where to send the logs), the bubble growing upward from where
		 * the heat's stands */
		char a1[2][LB_LINE], a2[3][LB_LINE];
		int n1 = 1, n2 = 1, k, room = ZYPEROS_SCREEN_W - 24 - HEATPOP_BUBBLE_X - 2 * HEATPOP_PAD_X;

		mh = heatpop_mark_h();
		mw = g_heatpop.line1[0] ? -HEATPOP_GAP : (mh + 1) / 2;
		if (g_heatpop.line1[0]) {
			n1 = ui_balance_lines(f1, HEATPOP_LINE1, room, 2, a1);
			n2 = ui_balance_lines(f2, HEATPOP_LINE2, room, 3, a2);
			ui_fit_cut(HEATPOP_LINE1, a1, n1);
			ui_fit_cut(HEATPOP_LINE2, a2, n2);
			for (k = 0, w1 = 0; k < n1; k++) if (ui_text_width(f1, a1[k]) > w1) w1 = ui_text_width(f1, a1[k]);
			for (k = 0, w2 = 0; k < n2; k++) if (ui_text_width(f2, a2[k]) > w2) w2 = ui_text_width(f2, a2[k]);
		} else {
			snprintf(a1[0], LB_LINE, "%s", HEATPOP_LINE1);
			snprintf(a2[0], LB_LINE, "%s", HEATPOP_LINE2);
			w1 = ui_text_width(f1, a1[0]);
			w2 = ui_text_width(f2, a2[0]);
		}
		w = HEATPOP_PAD_X + mw + HEATPOP_GAP + (w1 > w2 ? w1 : w2) + HEATPOP_PAD_X;
		h = HEATPOP_PAD_Y + n1 * l1 + n2 * l2 + HEATPOP_PAD_Y;
		/* Sliding up the last few units as it fades in, as the tutorial's */
		x = HEATPOP_BUBBLE_X;
		y = HEATPOP_BUBBLE_Y - (n1 - 1) * l1 - (n2 - 1) * l2 + (int)(6.0f * (1.0f - op) + 0.5f);
		ui_round_rect(a->r, &(SDL_Rect){ x, y, w, h }, DS_RADIUS_PANEL, slab);
		/* The tail, toward his mouth */
		v[0].position = (SDL_FPoint){ (float)x + 2.0f, (float)(y + h - 58) };
		v[1].position = (SDL_FPoint){ (float)x + 2.0f, (float)(y + h - 28) };
		v[2].position = (SDL_FPoint){ (float)x - 18.0f, (float)(y + h - 30) };
		for (i = 0; i < 3; i++) { v[i].color = slab; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
		SDL_RenderGeometry(a->r, NULL, v, 3, NULL, 0);
		if (!g_heatpop.line1[0]) heatpop_mark(a->r, x + HEATPOP_PAD_X, y + (h - mh) / 2, mh, ink.a);
		if (g_heatpop.line1[0]) {           /* drawn whole, in pieces: said so */
			ui_fit_drawn(HEATPOP_LINE1);
			ui_fit_drawn(HEATPOP_LINE2);
		}
		for (k = 0; k < n1; k++)
			ui_text(a->r, f1, a1[k], x + HEATPOP_PAD_X + mw + HEATPOP_GAP,
			        ui_line_y(UI_F_LABEL, y + HEATPOP_PAD_Y + k * l1), -1, ink);
		for (k = 0; k < n2; k++)
			ui_text(a->r, f2, a2[k], x + HEATPOP_PAD_X + mw + HEATPOP_GAP,
			        ui_line_y(UI_F_META, y + HEATPOP_PAD_Y + n1 * l1 + k * l2), -1, ink);
	}
	g_heatpop.drawn = now;
	redraw_now();
}

/* The bubble's two lines for the last start's restart (src/lastboot.h,
 * DESIGN.md 11.70): his "I had to restart!", then what stopped, its code and
 * where to send the logs */
void lastboot_bubble(const lastboot *lb)
{
	char code[48];

	lastboot_code(lb, code, sizeof code);
	snprintf(g_heatpop.line1, sizeof g_heatpop.line1, "%s", T(S_LASTBOOT_ZYPER));
	snprintf(g_heatpop.line2, sizeof g_heatpop.line2,
	         T(lastboot_froze(lb) ? S_LASTBOOT_FROZE : S_LASTBOOT_START), code);
}

/* Once a pass of the main loop, beside song_watch: the chip read, and Zyper
 * up when it has passed the trip. Put away when another screen had the
 * display meanwhile, as the song pop-up is; the charger's dark shelf skips
 * the pass, so a warning due then waits for the light, as one due over a
 * menu waits for the shelf. */
void heat_watch(void)
{
	unsigned now = plat_now_ms();

	if (g_heatpop.on && now - g_heatpop.drawn > SONG_GAP_MS) g_heatpop.on = false;
	heat_tick();
	if (g_heatpop.on || !heat_take(&g_heat, now)) return;
	heat_said("on the shelf");
	heatpop_show(now);
}

/* L2 and R2 with MP4 heard behind the shelf: what its own L1 and R1 do on Now
 * Playing - the track before and the next, or in a book with chapters the
 * chapter (mp4c_next, mp4c_prev) - with the pop-up saying what plays now.
 * R2 on the last track of a queue that does not go round moves nothing, and
 * is heard as the edge. */
static void mp4_song_step(app *a, int dir)
{
	const mu_now *mn = mp4c_now();
	const int index = mn->index;
	const double at = mn->at;

	if (dir > 0) mp4c_next(); else mp4c_prev();
	step_sound(a, dir > 0 ? IN_R2 : IN_L2, 0, mn->index != index || mn->at != at);
	mp4_song_said(plat_now_ms());
}

/* L2 and R2 on the home and the games shelf: the menu music's track before
 * and the next, Zyper saying it as it starts (song_watch). Heard as a step
 * that moves; a press with no music to move, its music off or quiet for a
 * snap, is denied. On one track the setting moves, and is kept. While MP4
 * is heard - the menu music quiet for it - they are MP4's (mp4_song_step);
 * paused or stopped, they are the menu music's again. */
void song_keys(app *a)
{
	int dir = a->in.pressed[IN_R2] ? 1 : a->in.pressed[IN_L2] ? -1 : 0;
	char track[512];

	if (!dir) return;
	if (mp4c_heard()) { mp4_song_step(a, dir); return; }
	if (!bgm_audible()) { uisound_play(DS_SOUND_DENY); return; }
	moved_sound(a, dir > 0 ? IN_R2 : IN_L2);
	if (bgm_step(dir, track, sizeof track)) db_set_str(db_dev(), "bgm.track", track);
}

/* A focus frame: on a still Spotlight home, the focused machine's next atlas
 * frame drawn over the picture the last whole frame left in the screen's
 * texture - ground laid where any of its frames can reach, the sprite, and
 * the row's sprites it reaches drawn again over it - and presented. Nothing
 * else on the home is drawn, and none of it changes: tools/home-shot-check.py
 * has a focus frame and a whole one of the same moment byte for byte the same,
 * for every tile of every system's animation.
 *
 * The sprite moves at about 11 frames a second for as long as the home is
 * up, and a whole frame is some 40 draws the GL driver takes 4.4 ms of CPU to
 * run on the Pixel 2: 6% of a core for a machine that waits (2026-11-07).
 * Not while anything that is not redrawn here is on screen: the song or heat
 * pop-up, the volume bar. Nor where the screen does not keep the last
 * frame's picture (plat_screen_kept: the Brick draws on its window). A whole
 * frame still comes at least once a second (HEARTBEAT_MS counts whole frames
 * only). */
bool focus_ok(const app *a)
{
	return a->screen == SCREEN_SYSTEMS && a->sys.count > 0 && !no_games(a) &&
	       !g_songpop.on && !g_heatpop.on && plat_osd_until() == UINT32_MAX &&
	       /* the replug message: a focus frame drew the sprite over it
	        * (2026-11-10, on the device) */
	       !replug_shown() &&
	       /* the screens' shader in the frame itself (the host's): a focus
	        * frame would be drawn over the shaded picture */
	       !plat_screen_shader_in_frame();
}

/* False, drawing nothing, when the sprite is not one a focus frame can draw */
bool focus_draw(app *a)
{
	home_item items[CFG_MAX_SYSTEMS];
	uint32_t now = g_home_phase >= 0 ? a->home.animation + (uint32_t)g_home_phase : anim_now();
	uint32_t wait;
	SDL_BlendMode was;
	SDL_Rect ground[HOME_FOCUS_GROUNDS];
	int n;

	g_focus_at = REDRAW_NEVER;
	if (!home_focus_alone(&a->home, a->home_layout, a->sys.count, a->sys_cursor, now) ||
	    !(n = home_focus_ground(&a->home, a->sys.count, a->sys_cursor, now, ground)))
		return false;
	home_items(a, items);
	/* The ground draw_background lays, under what the sprite's frames touch
	 * and under the row's sprites they reach, which are drawn again over it */
	SDL_GetRenderDrawBlendMode(a->r, &was);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderFillRects(a->r, ground, n);
	SDL_SetRenderDrawBlendMode(a->r, was);
	wait = home_draw_focus(&a->home, a->r, items, a->sys.count, a->sys_cursor, now, true);
	if (wait != UINT32_MAX) g_focus_at = plat_now_ms() + wait;
	return true;
}

void focus_frame(app *a)
{
	if (focus_draw(a)) plat_present();
	else redraw_now();                 /* a whole frame, then */
}

/* The shelf drawn by a loop that is its own, as render's is: MP4's shelf
 * (src/mp4_screen.c), whose cover row's no-art card sweeps as a games
 * shelf's does, live and not behind a menu */
void draw_shelf_live(app *a)
{
	g_home_live = true;
	draw_shelf(a);
	g_home_live = false;
}

void render(app *a)
{
	g_redraw_at = REDRAW_NEVER;
	g_focus_at = REDRAW_NEVER;
	g_home_live = true;
	draw_shelf(a);
	g_home_live = false;
	/* The last menu closed, leaving over the shelf: drawn every frame until
	 * it has gone (DESIGN.md 8.2) */
	if (panel_leave_draw(a, 0)) redraw_now();
	/* Under the volume bar, which comes first where they meet */
	song_popup_draw(a, plat_now_ms());
	heatpop_draw(a, plat_now_ms());
	plat_draw_osd(a->r);
	/* The tint lands exactly on its target now (see tick_tint), so this is a
	 * test that ends rather than one that is true forever. */
	if (a->sys.count > 0 && a->screen == SCREEN_GAMES && is_mp4(&a->sys.systems[a->sys_cursor]) && a->tint != a->sys.systems[a->sys_cursor].accent)
		redraw_now();
	redraw_at(plat_osd_until());
#ifdef ZYPEROS_DESK
	/* tools/desk's `state`: where the shelf's cursor is, as drawn */
	if (a->sys.count > 0) {
		sysview *v = &a->view[a->sys_cursor];
		bool games = a->screen == SCREEN_GAMES;

		desk_note_focus(games ? "games" : "systems", games ? v->cursor : a->sys_cursor,
		                games && v->list.count > 0 ? v->list.items[v->cursor].title
		                                           : a->sys.systems[a->sys_cursor].tag);
	}
#endif
	plat_present();
}

/* Ease the background tint toward the focused system rather than snapping: the
 * color is meant to feel like the light the machine gives off, and light does
 * not cut. Called from every loop that draws the shelf.
 *
 * And then land it. ui_mix truncates, so an ease this gentle stops short on any
 * channel that is rising: at 60fps each step is about 14% of the gap, and 14% of
 * 7 is under one, so it came to rest up to 7 units below its target and stayed
 * there for good. That never showed, but it meant "has the tint arrived" was
 * never true - and a launcher that only redraws while something is still
 * changing would have redrawn at the refresh rate forever. Within 12 on every
 * channel it takes the target outright: under 5% of a channel, on a dim wash, at
 * the very tail of an ease nobody watches finish. 12 rather than 7 because a
 * faster frame means a smaller step and a wider stall; dt is what sets it.
 *
 * A system with no color is not eased to or from: CFG_NO_COLOR is no color to
 * be part of the way to, so the tint takes it, or leaves it, at once, and is
 * CFG_NO_COLOR while it is on - which every reader of the tint draws by role
 * (ui_sys_mark, ui_lift), never by its bytes. */
unsigned last_tint_ms;
void tick_tint(app *a)
{
	unsigned now = anim_now();
	unsigned target = a->sys.systems[a->sys_cursor].accent, next;
	float dt = (float)(now - last_tint_ms) / 1000.0f;
	int dr, dg, db;

	last_tint_ms = now;
	if (target == CFG_NO_COLOR || a->tint == CFG_NO_COLOR) {
		a->tint = target;
		return;
	}
	if (dt > 0.1f) dt = 0.1f;
	next = ui_mix(a->tint, target, 1.0f - expf(-dt * 9.0f));
	dr = (int)((next >> 16) & 255) - (int)((target >> 16) & 255);
	dg = (int)((next >> 8) & 255) - (int)((target >> 8) & 255);
	db = (int)(next & 255) - (int)(target & 255);
	a->tint = (abs(dr) <= 12 && abs(dg) <= 12 && abs(db) <= 12) ? target : next;
}

/* ---------- transitions --------------------------------------------------- */

/* Starting a game the slow way: the focused card comes at you and the screen
 * goes with it.
 *
 * Only where nothing else is presenting yet and this process lets go of the
 * display straight after: the fallback, when there is no resident emulator and
 * Diatom runs as a process of its own, and a port, which gets its loading
 * screen next (launch_port). A normal launch has no animation at all, and that
 * is a display-safety rule rather than a taste call - see the resident path in
 * launch(): Diatom presents through fbdev and this process through GL, and the
 * two presenting at once wedges the display engine. This comment used to say
 * the zoom played while the resident loaded the ROM, which stopped being true
 * when that rule was made. */
/* One frame of it, `k` of the way through, 0 to 1: the card from `from`
 * scaled up on **in**, accelerating away, and fading out on **fade**, which is
 * linear. */
void launch_frame(app *a, SDL_Texture *card, const SDL_Rect *from,
                  float k)
{
	float scale = 1.0f + ds_in(k) * 2.2f;
	SDL_Rect dst;

	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(a->r);
	if (!card) return;
	dst.w = (int)(from->w * scale);
	dst.h = (int)(from->h * scale);
	dst.x = ZYPEROS_SCREEN_W / 2 - dst.w / 2;
	dst.y = ZYPEROS_SCREEN_H / 2 - dst.h / 2;
	SDL_SetTextureAlphaMod(card, (Uint8)(255 * (1.0f - k)));
	SDL_RenderCopy(a->r, card, NULL, &dst);
	SDL_SetTextureAlphaMod(card, 255);
}

void anim_launch(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	unsigned t0 = plat_now_ms(), now;
	SDL_Rect from;
	int tw = 0, th = 0;
	SDL_Texture *card;

	from = (SDL_Rect){320,212,GAMES_COVER_W,GAMES_COVER_H};
	card = v->list.count ? game_get_tex(a, v->cursor, &tw, &th, NULL) : NULL;
	if (card) {
		SDL_FRect box = games_cover_rect(tw, th, 0);
		from = (SDL_Rect){(int)box.x, (int)box.y, (int)box.w, (int)box.h};
	}

	while ((now = plat_now_ms()) - t0 < DS_MS_LAUNCH) {
		/* Asked again every frame until it is there. Card art decodes on a
		 * worker, so A pressed straight after a letter jump can start this
		 * before the focused card has arrived - and asking once, as this did,
		 * zoomed an empty frame for the whole animation. Now a card that lands
		 * partway through joins the zoom where it is. */
		if (!card && v->list.count) {
			/* This card first, out of its turn and whatever the frame has
			 * spent: it is the one the zoom is waiting for. */
			texload_deliver_slot(a->sys_cursor, v->cursor, theme_index(),
			                     shelf_put, a);
			texload_drain(a);
			card = game_get_tex(a, v->cursor, &tw, &th, NULL);
		}
		launch_frame(a, card, &from, (float)(now - t0) / DS_MS_LAUNCH);
		plat_present();
		ui_frame_rest(6);
	}
	/* Where the zoom was going: the card gone and the ground it was on, the
	 * frame whatever comes next comes up from - Diatom taking the panel on
	 * the fallback, the loading screen in front of a port. */
	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(a->r);
	plat_present();
}

/* One frame of it, `t` ms in: the loop below draws these, and --poweroff
 * draws one for a shot. On Beard he casts his shadow (zyper_shadow_draw),
 * which travels in with him and goes into the ground as he dims. */
/* Its Zyper, `t` ms in, `h` tall, standing at (cx, cy) once he is in, in
 * from `from` on the left, at `alpha`; over whatever is drawn under him. The
 * tutorial's power step plays it small on its stage (tourdraw.c). */
void poweroff_zyper(app *a, unsigned t, float cx, float cy, float h, float from,
                    Uint8 alpha)
{
	float x = cx, tilt = ZYPER_TILT_DEG, tongue = 1.0f, dim = 0.0f;

	if (t < POWEROFF_IN) {
		/* In from the left, decelerating onto the center. */
		float k = (float)t / (float)POWEROFF_IN;
		float e = ds_out(k);
		x = from + (cx - from) * e;
	} else if (t < POWEROFF_IN + POWEROFF_HEAD) {
		/* The tongue in and the head straight, accelerating, like a
		 * flinch: the two together are him paying attention. */
		float k = (float)(t - POWEROFF_IN) / (float)POWEROFF_HEAD;
		tongue = 1.0f - ds_in(k);
		tilt = ZYPER_TILT_DEG * (1.0f - ds_in(k));
	} else {
		float k = (float)(t - POWEROFF_IN - POWEROFF_HEAD) / (float)POWEROFF_DIM;
		if (k > 1.0f) k = 1.0f;
		tongue = 0.0f;
		tilt = 0.0f;
		dim = ds_out(k);
	}
	zyper_shadow_draw(a->r, x, cy, h, tilt, (1.0f - dim) * alpha / 255.0f);
	draw_zyper(a->r, x, cy, h, tilt, tongue, dim, alpha);
}

void poweroff_frame(app *a, unsigned t)
{
	SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(a->r);
	/* his height 230, the old shell's about; in from 35% of the width left */
	poweroff_zyper(a, t, ZYPEROS_SCREEN_W / 2.0f, ZYPEROS_SCREEN_H / 2.0f, 230.0f,
	               -ZYPEROS_SCREEN_W * 0.35f, 255);
}

void anim_poweroff(app *a)
{
	unsigned t0 = plat_now_ms(), now;

	while ((now = plat_now_ms()) - t0 < POWEROFF_MS) {
		poweroff_frame(a, now - t0);
		plat_present();
		ui_frame_rest(6);
	}
	/* Land on the closed state exactly, in case the loop exited a frame early,
	 * and then deliberately no clear: the dimmed head is the last thing on
	 * screen and stays there while the device powers down. */
	poweroff_frame(a, POWEROFF_MS);
	plat_present();
}

/* The power-off with the send-off `anim` plays: anim_poweroff, or the
 * low-battery screen's from where Zyper already stands */
void power_off_with(app *a, void (*anim)(app *a))
{
	/* The downloads going on behind the screens stop: the next run starts
	 * from what is missing */
	dlq_stop_all();
	/* A File Transfer behind the shelf, and its hotspot, go with it */
	xs_end();
	snap_off();
	remember_place(a);
	book_keep(true);
	/* Asked for: a game left running continues at the next start */
	playing_mark_clean();
	/* ...and an accessory powered now is powered again then, unasked */
	usb_kept_save();
	plat_request_poweroff();
	anim(a);
	/* And dark the moment it ends. The rest of the shutdown is not instant -
	 * on the GKD Pixel 2 it waits for the programs on the card to go before
	 * unmounting it - and the shell stayed lit through all of it. Eric's
	 * call, 2026-10-02, for both devices. */
	plat_backlight(false);
	a->running = false;
}

/* POWER, Auto Off, the battery empty: saved, the send-off, and off - or, on
 * a charger, the charging screen in place of black (DESIGN.md 11.25) */
void power_off(app *a)
{
	/* Off is off, the charging screen in its place too */
	dlq_stop_all();
	if (charge_instead(a)) return;
	power_off_with(a, anim_poweroff);
}
