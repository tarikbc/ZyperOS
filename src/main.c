/* SPDX-License-Identifier: GPL-3.0-only
 *
 * ZyperOS - a custom firmware for the TrimUI Brick that plays eleven
 * 8-, 16- and 32-bit consoles.
 *
 * The whole design metric is speed. The launcher starts behind the boot
 * animation rather than after it, hands games to an emulator that is already
 * running rather than starting one, and never tears its own display down - so
 * coming back from a game is a frame, not a second and a half. Around that sit
 * RetroAchievements, box art the device fetches itself, a file server over
 * Wi-Fi, Bluetooth audio and play time - each one row in one menu, and none of
 * it in the way of starting a game.
 */
#include "hwreport.h"
#include "crashrec.h"   /* the launcher's own crash, in the log and crashlog's report */
#include "lastboot.h"
#include "atomic.h"
#include "autozone.h"
#include "coreopts.h"   /* coreopts_migrate: 1.0.6's defaults at run time */
#include "mp4keys.h"    /* mp4keys_migrate: MP4's settings under its name, once, from 1.1.0 */
#include "about.h"
#include "battery_eta.h"
#include "battery_poll.h"
#include "bgm.h"
#include "bgm_menu.h"
#include "blur.h"
#include "cardgen.h"
#include "cartlabel.h"
#include "logcap.h"
#include "cheevos.h"
#include "clock.h"
#include "config.h"
#include "ctl.h"
#include "ctl_channel.h"
#include "update_screen.h"
#include "search_screen.h"
#include "clock_screen.h"
#include "bgm_screen.h"
#include "gamedel_screen.h"
#include "gamern_screen.h"
#include "storage_screen.h"
#include "media_screen.h"
#include "menu_panel.h"
#include "confirmq.h"
#include "ds/confirm.h"
#include "nowplaying_looks.h"
#include "nowplaying_screen.h"
#include "bt.h"
#include "btvol.h"
#include "bt_menu.h"
#include "db.h"
#include "stats.h"
#include "sort.h"
#include "coverflow.h"
#include "library.h"
#include "nightkeep.h"
#include "platform.h"
#include "texload.h"
#include "screencard.h"
#include "tween.h"
#include "artshrink.h"
#include "artbadge.h"
#include "look.h"
#include "artscrape.h"
#include "taskhold.h"
#include "favorites.h"
#include "hare.h"
#include "xfer.h"
#include "home.h"
#include "hotkeys_screen.h"
#include "homesys_screen.h"
#include "games.h"
#include "hareart.h"
#include "hotspot.h"
#include "emptyhome.h"
#include "hotpanel.h"
#include "captive.h"
#include "p2.h"
#include "p2panel.h"
#include "p2share.h"
#include "spawn.h"
#include "webtheme.h"
#include "devmode.h"
#include "dl_screen.h"
#include "dlq.h"
#include "diskpopup.h"
#include "border_pic.h"
#include "handframe.h"
#include "dmodepreview.h"
#include "sshlink.h"
#include "mdns.h"
#include "linksteps.h"
#include "keyhints_draw.h"
#include "usblink.h"
#include "usblog.h"
#include "phonelink.h"
#include "chargedark.h"
#include "pace.h"
#include "idle.h"
#include "notice.h"
#include "randgame.h"
#include "saver.h"
#include "osdbar.h"
#include "rafetch.h"
#include "noticeq.h"
#include "rabadge.h"
#include "rahash.h"
#include "theme.h"
#include "boottheme.h"
#include "net.h"
#include "keyboard.h"
#include "logpack.h"
#include "wifi.h"
#include "audioout.h"
#include "uisound.h"
#include "menu_pick.h"
#include "menu.h"
#include "menumotion.h"
#include "message.h"
#include "menusnap.h"
#include "mp4c.h"
#include "mp4lock.h"
#include "menukeys_screen.h"
#include "mp4resume.h"   /* MP4 back after a power-off (DESIGN.md 11.67) */
#include "mp4hand.h"     /* MP4 handed over a program of its own (DESIGN.md 11.55) */
#include "alsatry.h"     /* --alsa-try: a port's ALSA chain opened as PICO-8 opens it */
#include "topbar.h"
#include "mp4frame.h"  /* MPF_TOO_BIG, for --video-trouble */
#include "mp4video.h"
#include "mp4art.h"
#include "mp4lib.h"
#include "sgdb.h"
#include "snaps.h"
#include "snapshelf.h"
#include "snapview.h"
#include "ss.h"
#include "ssrun.h"
#include "search.h"
#include "status.h"
#include "sys_menu.h"
#include "sysupdate.h"
#include "storage.h"
#include "storage_page.h"
#include "strength.h"
#include "artsrc.h"
#include "mediafetch.h"
#include "mediapage.h"
#include "mediatex.h"
#include "controls.h"
#include "ctltab_screen.h"
#include "achtab_screen.h"
#include "rahashjob.h"
#include "remapstore.h"
#include "tour.h"
#include "tourdraw.h"
#include "tourdevice.h"
#include "game_menu.h"
#include "pico8.h"
#include "portmenu.h"
#include "gamedel.h"
#include "gamerows.h"
#include "portinfo.h"
#include "dsv.h"
#include "dslang.h"
#include "corestate.h"
#include "dsresume.h"
#include "ui.h"
#include "ds/mark.h"    /* badges, switch marks, the progress bar */
#include "ds/page.h"    /* a page: its frame, heading, line over the keys, foot */
#include "ds/tiles.h"   /* a question in tiles, or a list */
#include "ds/button.h"  /* the buttons as this handheld prints them */
#include "wifi_menu.h"
#include "zyperrig.h"
#include "songpop.h"
#include "heat.h"
#include "lowbatt.h"
#include "charging.h"
#include "app.h"
#include "aout_route.h"
#include "eq.h"         /* eq_share_cut: your PICO-8 quiet while MP4 plays */
#include "bt_screen.h"
#include "panel.h"
#include "place.h"
#include "pminstall.h"
#include "portload.h"
#include "powerscreens.h"
#include "raqueue.h"
#include "shelfbuild.h"
#include "shelfdir.h"
#include "shots_screen.h"
#include "esmedia.h"
#include "gamern.h"
#include "stats_screen.h"
#include "ingame_screen.h"
#include "sysmenu_screen.h"
#include "launch_screen.h"
#include "saver_screen.h"
#include "mp4_screen.h"
#include "player2_screen.h"
#include "link_screen.h"
#include "tour_screen.h"
#include "devmode_screen.h"
#include "signin_screen.h"
#include "about_screen.h"
#include "usbdrive_screen.h"
#include "xfer_screen.h"
#include "radl_screen.h"
#include "scraper_screen.h"
#include "gameinfo_screen.h"
#include "lights_screen.h"
#include "shots.h"
#include "cheats_screen.h"
#include "menu_runner.h"
#include "shelf_keys.h"
#include "shelf_draw.h"
#include "zyperdraw.h"
#include "zypermark.h"
#include "klog.h"
#include "wdog.h"
#include "levels.h"
#ifdef ZYPEROS_DESK
#include "device/desk.h"
#endif

#include <SDL.h>
#include <SDL_image.h>
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

/* The row is allowed to glide only across cards this policy keeps decoded. If
 * TEX_KEEP_NEAR ever drops below it, a long move would animate over cold cards
 * and stall exactly as it did before CF_WARM_CARDS existed. */
_Static_assert(TEX_KEEP_NEAR >= CF_WARM_CARDS,
               "the row glides across cards the texture policy must keep");

volatile sig_atomic_t want_quit;
static void on_sigterm(int sig) { (void)sig; want_quit = 1; plat_terminate(); }

/* This process is the in-game menu over a port (src/portmenu.h, --port-menu),
 * started for it by portkeys while the port is stopped: the menu, its
 * Controls page and nothing else. */
bool g_port_menu;
/* ...over a program still starting, under its loading screen (PORT_MENU_WAITING) */
bool g_port_waiting;

/* Boot time is the one number a launcher cannot be careless about, so the
 * phases are timed and logged rather than guessed at. On the project this
 * grew out of, the phase everyone assumed was expensive turned out to be 8%
 * of startup -- which is only knowable by measuring. */
unsigned t_boot0;
static void t_mark(const char *what)
{
	fprintf(stderr, "boot: %-14s %5u ms\n", what, plat_now_ms() - t_boot0);
}

/* One number for the way back, because this path keeps being asked how fast it
 * is and a number is how the answer stays true. The five phase marks that took
 * it from 213ms to here are in the history if it ever needs breaking down
 * again; the budget they found was 5ms to black, 4ms of bookkeeping, and the
 * rest decoding the focused card's autosave preview off the SD card. */
unsigned t_back0;

/* Counts the frames begun. What starts a whole frame steps it - draw_shelf,
 * paused_backdrop, present_clear and Now Playing's np_draw, one of which
 * starts every frame a menu's panel can be drawn over or left for. The panel
 * drawn last remembers the count it was drawn at (g_snap), and is still the
 * one on screen only while the count has not moved since: a keyboard, a slot
 * strip or Now Playing drawn after it has moved it. */
/* A screen's one-button hint, B and its word (keyhints.h), for a row */
const char *b_legend(str_id word)
{
	return keyhints_legend((key_hint[]){ { "B", T(word) } }, 1);
}

/* The tutorial's START hint, as every hint (tour_hint_draw_set) */
static void tour_hint(SDL_Renderer *r, const char *key, const char *label, int x, int cy)
{
	key_hint h = { key, label };

	keyhints_draw(r, &h, 1, x, x + keyhints_width(key, label), cy, KEYHINTS_CENTER, false, 255);
}

unsigned g_frame_no;

/* One present of nothing but `c`, over the whole screen. */
void present_clear(app *a, ds_rgb c)
{
	g_frame_no++;
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(a->r, c.r, c.g, c.b, 255);
	SDL_RenderClear(a->r);
	plat_present();
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
}

/* Get off the game's last frame before doing anything slow.
 *
 * Everything between EXIT and the first frame of the shelf blocks and none of
 * it presents, so the panel holds whatever the launcher drew last - which
 * after a quit is the in-game menu, over a frame of the game that has ended.
 * Measured on the device: 123ms of it, 52 in the LED writes that ran here
 * then (since removed) and 69 rebuilding the focused card off the SD card.
 *
 * One present of the theme's ground, because the shelf that follows is drawn
 * on it: in Beard a black frame between a game and the light shelf is a flash
 * of a color the theme has nowhere else. This is not the buffer-establishing
 * that was tried and reverted: that guarded a stale back buffer, which a swap
 * cannot show. This gets a stale FRONT frame off the glass, which is
 * measurable and was measured. */
void present_ground(app *a)
{
	present_clear(a, theme()->ground);
}

/* Black, the one color here that is not the theme's, because it is Diatom's:
 * its pages start opaque black and it draws only the game's picture into
 * them, so the border around a game that does not fill the panel is this
 * black in both themes - the game is content, not part of the theme (DESIGN.md
 * 3.4). What resuming a game presents has to be the same black, or the border
 * would change color as Diatom cycles its pages (see where this is called). */
const ds_rgb DIATOM_PAGE = { 0, 0, 0 };

/* ---------- per-system display mode --------------------------------------- */

/* Every system's shader that has not chosen its own (SHADERS): the `shader`
 * key, which MENU's Shader row set for every system up to 1.0.6. That row
 * is the screens' own now (below) and nothing sets this; a system with no
 * choice of its own still plays with it, so a game looks as it did. */
int g_shader_all;

/* A shader by Diatom's name for it (SHADERS), or -1 */
static int shader_index(const char *name)
{
	int k;

	for (k = 0; k < SHADER_COUNT; k++)
		if (!strcmp(SHADERS[k].name, name)) return k;
	return -1;
}

/* Whether this device offers shader k to the system tagged `tag`: the old
 * screens only where Diatom draws them with a GPU (plat_gpu_shaders), and a
 * shader for some systems only to them (shader_for) */
static bool shader_offered(int k, const char *tag)
{
	return shader_for(k, tag) && (!SHADERS[k].gpu || plat_gpu_shaders());
}

/* The shader a step `d` comes to from k, round the ones this device offers
 * the system tagged `tag`: each Shader row steps with it */
int shader_step(int k, int d, const char *tag)
{
	int i;

	for (i = 0; i < SHADER_COUNT; i++) {
		k = (k + d + SHADER_COUNT) % SHADER_COUNT;
		if (shader_offered(k, tag)) break;
	}
	return k;
}

/* Keyed on the system's tag rather than its folder or its place in the list,
 * for the same reason saves are: renaming a ROM folder or reordering
 * systems.cfg then cannot quietly hand a system somebody else's setting.
 * The shaders too, each system's: its own shader.<TAG>, else the old
 * `shader` for every system, else Off; and their strength the same way
 * (src/strength.h). The screens' own shader with them (screen_shader_load). */
/* The mode a system plays at while its Display row was never set: Full
 * Screen, DMODES[0], for every system, Diatom's default too; but the PICO-8
 * shelf while it runs on your PICO-8 plays at Original Shape, the 480 x 480
 * your PICO-8 always drew at before its Display modes came (2026-11-20: a
 * shelf never set keeps the look it had, and only a pick of Full Screen
 * stretches it). fake08 on the same shelf keeps Full Screen. The default
 * follows the core: the Core row turned on the shelf moves it with it
 * (core_step, dmode_settle). */
int dmode_default(const system_cfg *s)
{
	int k;

	if (port_menu_display(s->tag) && !strncmp(s->core, RUN_PREFIX, strlen(RUN_PREFIX)))
		for (k = 0; k < DMODE_COUNT; k++)
			if (!strcmp(DMODES[k].name, "aspect")) return k;
	return 0;
}

void dmode_settle(app *a, int sys)
{
	if (!a->view[sys].dmode_kept) a->view[sys].dmode = dmode_default(&a->sys.systems[sys]);
}

/* Once: your PICO-8's row as every system's row was kept before 1.1.0,
 * when any change of any system's Display wrote all of them (display_save):
 * a "stretch" there was no pick of Full Screen, as your PICO-8 drew 480 x 480
 * at every mode but Sharp Pixels then, so it is let go of. From this one on
 * a row is written only for a pick. */
#define P8_DISPLAY_SETTLED "pico8.display_settled"

void display_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR];
	bool settled = db_has(db_dev(), P8_DISPLAY_SETTLED);
	int i, k;

	g_shader_all = 0;
	if (db_get_str(db_dev(), "shader", name, sizeof name, NULL) && (k = shader_index(name)) >= 0)
		g_shader_all = k;
	/* The systems off the home too (app.parked): their games start from
	 * Favorites and Recents in their own mode */
	for (i = 0; i < sys_kept(a); i++) {
		const char *tag = a->sys.systems[i].tag;

		snprintf(key, sizeof key, "shader.%s", tag);
		/* A shader for some systems only (shader_for) that names another,
		 * by the old every-system key or its own on an edited card, is that
		 * system's default instead: the old key's if that is for it, else
		 * Off */
		a->view[i].shader = shader_for(g_shader_all, tag) ? g_shader_all : 0;
		if (db_get_str(db_dev(), key, name, sizeof name, NULL) && (k = shader_index(name)) >= 0 &&
		    shader_for(k, tag))
			a->view[i].shader = k;
		a->view[i].strength = strength_read(db_dev(), a->sys.systems[i].tag);
		snprintf(key, sizeof key, "display.%s", a->sys.systems[i].tag);
		a->view[i].dmode_kept = false;
		if (db_get_str(db_dev(), key, name, sizeof name, NULL)) {
			if (!settled && port_menu_display(tag) && !strcmp(name, DMODES[0].name)) {
				db_del(db_dev(), key);
				fprintf(stderr, "display: %s's Full Screen from before 1.1.0 is no pick; its default\n", tag);
			} else {
				for (k = 0; k < DMODE_COUNT; k++)
					if (!strcmp(DMODES[k].name, name)) {
						a->view[i].dmode = k;
						a->view[i].dmode_kept = true;
						break;
					}
			}
		}
		dmode_settle(a, i);
	}
	if (!settled) db_set_str(db_dev(), P8_DISPLAY_SETTLED, "1");
	screen_shader_load();
}

/* One row per visible system, and nothing else touched.
 *
 * The file this replaces was rewritten whole, so it had to read itself back
 * first and carry forward the modes of systems that are not on the shelf right
 * now. That was not tidiness: empty systems are hidden, so "every system" meant
 * "every system with games in it today", and a plain rewrite would quietly
 * erase the mode of any system whose ROMs happened to be off the card. Take
 * the ROMs out, change one unrelated setting, put them back, and the mode you
 * chose is gone with no sign it existed.
 *
 * A row write cannot touch another row, so that whole pass is gone rather than
 * ported. It is the clearest thing the database bought. */
void display_save(app *a)
{
	char key[CFG_STR + 16];
	int i;

	for (i = 0; i < sys_kept(a); i++) {
		sysview *v = &a->view[i];

		/* A row for a pick, and none for a system still at its default:
		 * that one follows its core (dmode_default), and a row would fix
		 * it at whatever the core's default was when another system's
		 * Display changed */
		if (!v->dmode_kept && v->dmode == dmode_default(&a->sys.systems[i])) continue;
		snprintf(key, sizeof key, "display.%s", a->sys.systems[i].tag);
		db_set_str(db_dev(), key, DMODES[v->dmode].name);
		v->dmode_kept = true;
	}
}

/* ---------- the shader ------------------------------------------------------ */

/* System `sys`'s own shader, kept the moment it changes, as its display mode
 * is: the system menu's row and the in-game menu's. */
void shader_save(app *a, int sys)
{
	char key[CFG_STR + 16];

	snprintf(key, sizeof key, "shader.%s", a->sys.systems[sys].tag);
	db_set_str(db_dev(), key, SHADERS[a->view[sys].shader].name);
}

/* ---------- the screens' shader --------------------------------------------- */

/* MENU > Display > Shader from the home (src/screenfx.h, DESIGN.md 11.50):
 * ZyperOS's own screens' shader while no game is up, a SHADERS index, Off
 * to start with, and its strength, 100% to start with. Keys of their own,
 * launcher_shader and launcher_strength, so no system's shader moves: not
 * under "shader." nor "shader_strength.", which are the systems'. */
int g_screen_shader;
int g_screen_strength = STRENGTH_MAX;
/* What a list's cursor shows in their place (src/ds/hover.h): -1, none */
int g_screen_shown = -1, g_screen_shown_strength = -1;

/* The screens' choices: Off and the old screens this device draws.
 * Sharp-Shimmerless sizes a game's pixels evenly, and these screens are
 * drawn at the panel's own: it would change nothing, so it is not offered. */
bool screen_shader_offered(int k)
{
	return k == 0 || (k > 0 && k < SHADER_COUNT && SHADERS[k].gpu && plat_gpu_shaders() &&
	                  shader_for(k, NULL));
}

/* The screens' row's step, as shader_step is the systems' */
int screen_shader_step(int k, int d)
{
	int i;

	for (i = 0; i < SHADER_COUNT; i++) {
		k = (k + d + SHADER_COUNT) % SHADER_COUNT;
		if (screen_shader_offered(k)) break;
	}
	return k;
}

/* The device told what to draw: the shown one over the kept one, and Off
 * while a game is up (from RUN to its end, its in-game menu and what that
 * opens with it, and a port's menu): what is drawn then is over the game,
 * whose own shader is its frame's */
void screen_shader_apply(void)
{
	int k = g_screen_shown >= 0 ? g_screen_shown : g_screen_shader;
	int s = g_screen_shown_strength >= 0 ? g_screen_shown_strength : g_screen_strength;

	if (g_ctl_game.on || g_port_menu || !screen_shader_offered(k)) k = 0;
	plat_screen_shader(SHADERS[k].name, s);
}

void screen_shader_load(void)
{
	char name[CFG_STR];
	int k;

	g_screen_shader = 0;
	if (db_get_str(db_dev(), "launcher_shader", name, sizeof name, NULL) &&
	    (k = shader_index(name)) >= 0 && screen_shader_offered(k))
		g_screen_shader = k;
	g_screen_strength = strength_clamp(db_get_int(db_dev(), "launcher_strength", STRENGTH_MAX));
	screen_shader_apply();
}

void screen_shader_set(int k)
{
	g_screen_shader = screen_shader_offered(k) ? k : 0;
	db_set_str(db_dev(), "launcher_shader", SHADERS[g_screen_shader].name);
	screen_shader_apply();
}

void screen_strength_set(int pct)
{
	g_screen_strength = strength_clamp(pct);
	db_set_int(db_dev(), "launcher_strength", g_screen_strength);
	screen_shader_apply();
}

/* MENU's Strength row: there while the screens' shader is an old screen */
bool screen_strength_shown(void)
{
	return shader_has_strength(g_screen_shader);
}

const char *screen_strength_label(void)
{
	static char buf[16];

	strength_label(g_screen_strength, buf, sizeof buf);
	return buf;
}

/* System `sys`'s row's value, NULL when it has no row */
const char *strength_row_label(const app *a, int sys)
{
	static char buf[16];

	if (!shader_has_strength(a->view[sys].shader)) return NULL;
	strength_label(a->view[sys].strength, buf, sizeof buf);
	return buf;
}

/* ---------- per-system core ----------------------------------------------- */

/* A shelf whose core field lists more than one core, the first the default -
 * the Dreamcast: flycast2021, a libretro core run in Diatom with ZyperOS's own
 * in-game menu, and the standalone Flycast 2.7, a program run through a
 * script. The shelf's Core row chooses (core_step, from the system menu), and
 * the choice is kept per system under core.<TAG>, beside display.<TAG>.
 * Everything that runs a game reads s->core, so choosing is writing that
 * field and nothing else follows it. A shelf with one core keeps the row, as
 * a fact rather than a choice: it names the core.
 *
 * A core that is not on the card is passed over when there is no choice on
 * record: a card without flycast2021 plays its Dreamcast games on Flycast
 * 2.7, if that is there, without anyone visiting a menu, and the row offers
 * both. A choice on record is kept even if its core has gone, so taking
 * Flycast 2.7 off for a while does not lose it. A choice on record of a core
 * the shelf no longer lists is passed over the same way: the Nintendo DS's
 * core.NDS, which named DraStic's script on a card that chose it before
 * 2026-11-08, is never read, since the DS has one core now. */
bool core_present(const char *core)
{
	char path[CFG_STR * 2];

	if (!strncmp(core, "run:", 4)) {
		snprintf(path, sizeof path, "%s/%s", P_CARD, core + 4);
		return access(path, R_OK) == 0;
	}
	if (!strcmp(core, "port")) return true;
	snprintf(path, sizeof path, "%s/cores/%s_libretro.so", P_ROOT, core);
	return access(path, R_OK) == 0;
}

/* The files the player brings for `core` on `s` (config/systems.cfg's
 * brings lines: your PICO-8's pico8_64 and pico8.dat), each on the card;
 * true for a core that needs none. `missing`, n bytes, gets the first one
 * that is not there ("Bios/PICO-8/pico8_64"), when given. */
bool core_brought(const system_cfg *s, const char *core, char *missing, size_t n)
{
	const char *p = s->brings;

	if (missing && n) missing[0] = '\0';
	if (!s->brings_core[0] || strcmp(core, s->brings_core)) return true;
	while (*p) {
		size_t len = strcspn(p, ",");
		char path[CFG_STR * 2];

		snprintf(path, sizeof path, "%s/%.*s", P_CARD, (int)len, p);
		if (len && access(path, R_OK) != 0) {
			if (missing) snprintf(missing, n, "%.*s", (int)len, p);
			return false;
		}
		p += len;
		if (*p) p++;
	}
	return true;
}

/* A core that can run here: on the card, and with what the player brings */
bool core_ready(const system_cfg *s, const char *core)
{
	return core_present(core) && core_brought(s, core, NULL, 0);
}

/* The cores the shelf's Core row offers, in the core field's order: each
 * that can run here, and the one chosen whatever it is, so the row still
 * says it. Your PICO-8 is offered once its files are on the card, and on a
 * handheld whose ZyperOS has its script (the Pixel 2's). Their count; the
 * i-th into `out` with core_offered_at. */
int core_offered_count(const system_cfg *s)
{
	char alt[CFG_STR];
	int k, n = 0;

	for (k = 0; cfg_core_at(s, k, alt, sizeof alt); k++)
		if (!strcmp(alt, s->core) || core_ready(s, alt)) n++;
	return n;
}

bool core_offered_at(const system_cfg *s, int i, char *out, int cap)
{
	char alt[CFG_STR];
	int k;

	for (k = 0; cfg_core_at(s, k, alt, sizeof alt); k++)
		if (!strcmp(alt, s->core) || core_ready(s, alt)) {
			if (i-- == 0) {
				snprintf(out, (size_t)cap, "%s", alt);
				return true;
			}
		}
	return false;
}

/* ---------- your PICO-8, from a zip copied by hand (src/pico8.h) ---------- */

/* What the launcher says once the home is up about the zip it took, "" for
 * nothing (Tarik's pick, 2026-11-19: the zip as it comes, or its files) */
char g_p8_said[256];

void p8_take_now(void)
{
	char ver[32], zip[128];
	p8_take t = p8_take_zip(P_CARD, ver, sizeof ver, zip, sizeof zip);

	if (t == P8_TAKE_DONE)
		fprintf(stderr, "pico8: took your PICO-8 %s out of Bios/PICO-8/%s\n", ver[0] ? ver : "(no version)", zip);
	else if (t == P8_TAKE_NOT_RASPI)
		fprintf(stderr, "pico8: Bios/PICO-8/%s is not the PICO-8 for Raspberry Pi zip\n", zip);
	else if (t == P8_TAKE_FAILED)
		fprintf(stderr, "pico8: could not take PICO-8 out of Bios/PICO-8/%s\n", zip);
	p8_take_message(t, ver, zip, g_p8_said, sizeof g_p8_said);
}

/* What the Core row says: a libretro core by its short name, as every other
 * shelf's row always has, and a script by its own name - "Flycast" for
 * run:Emulators/Flycast/Flycast.sh - since the path is the card's business. */
const char *core_label(const char *core, char *buf, size_t n)
{
	const char *b, *dot;

	if (strncmp(core, "run:", 4) != 0) return core;
	b = strrchr(core, '/');
	b = b ? b + 1 : core + 4;
	dot = strrchr(b, '.');
	snprintf(buf, n, "%.*s", dot ? (int)(dot - b) : (int)strlen(b), b);
	return buf;
}

void core_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR], alt[CFG_STR];
	int i, k, n;

	/* The systems off the home too: a game of one starts from Favorites and
	 * Recents on the core chosen for it */
	for (i = 0; i < sys_kept(a); i++) {
		system_cfg *s = &a->sys.systems[i];
		bool set = false;

		if ((n = cfg_core_count(s)) < 2) continue;
		snprintf(key, sizeof key, "core.%s", s->tag);
		if (db_get_str(db_dev(), key, name, sizeof name, NULL))
			for (k = 0; k < n && !set; k++)
				if (cfg_core_at(s, k, alt, sizeof alt) && !strcmp(alt, name)) {
					snprintf(s->core, sizeof s->core, "%s", alt);
					set = true;
				}
		for (k = 0; k < n && !set; k++)
			if (cfg_core_at(s, k, alt, sizeof alt) && core_ready(s, alt)) {
				snprintf(s->core, sizeof s->core, "%s", alt);
				set = true;
			}
		fprintf(stderr, "core: %s runs on %s%s\n", s->name, s->core,
		        set ? "" : " (none of its cores is on the card)");
	}
	/* A Display never set goes by the core now chosen */
	for (i = 0; i < sys_kept(a); i++) dmode_settle(a, i);
}

/* ---------- a game's own core (2026-11-27) -------------------------------
 *
 * Tarik's decision with the Link Cable: a game can run on a core of its own
 * among its shelf's, over the shelf's Core row. Kept as core.<TAG>.<file>
 * (as a game's own buttons are map.<tag>.<file>); none, the shelf's choice.
 * The Link Cable's question on mGBA sets it to Gambatte for that game
 * alone; its details page's Core row steps it, Shelf's Choice first. */
static void core_game_key(const system_cfg *s, const char *file, char *key, size_t n)
{
	snprintf(key, n, "core.%s.%s", s->tag, file);
}

bool core_game_get(const system_cfg *s, const char *file, char *out, int cap)
{
	char key[CFG_STR + LIB_PATH + 16], name[CFG_STR], alt[CFG_STR];
	int k, n = cfg_core_count(s);

	if (!file || !*file || n < 2) return false;
	core_game_key(s, file, key, sizeof key);
	if (!db_get_str(db_dev(), key, name, sizeof name, NULL) || !name[0]) return false;
	/* Only one its shelf still offers, and on the card */
	for (k = 0; k < n; k++)
		if (cfg_core_at(s, k, alt, sizeof alt) && !strcmp(alt, name) && core_ready(s, alt)) {
			snprintf(out, (size_t)cap, "%s", alt);
			return true;
		}
	return false;
}

void core_game_set(const system_cfg *s, const char *file, const char *core)
{
	char key[CFG_STR + LIB_PATH + 16];

	core_game_key(s, file, key, sizeof key);
	if (core && *core) db_set_str(db_dev(), key, core);
	else db_del(db_dev(), key);
	fprintf(stderr, "core: %s/%s %s%s\n", s->folder, file, core && *core ? "runs on " : "goes by its shelf",
	        core && *core ? core : "");
}

/* The game's Core row stepped: Shelf's Choice, then each core the shelf
 * offers, round; what it is now into `out` ("" for the shelf's) */
void core_game_step(const system_cfg *s, const char *file, int d, char *out, int cap)
{
	char cur[CFG_STR] = "", alt[CFG_STR];
	int n = core_offered_count(s), k, at = 0;

	if (n < 2) { if (cap) out[0] = '\0'; return; }
	core_game_get(s, file, cur, sizeof cur);
	for (k = 0; k < n; k++)
		if (cur[0] && core_offered_at(s, k, alt, sizeof alt) && !strcmp(alt, cur)) at = k + 1;
	at = (at + d + n + 1) % (n + 1);
	if (at == 0 || !core_offered_at(s, at - 1, alt, sizeof alt)) alt[0] = '\0';
	core_game_set(s, file, alt);
	snprintf(out, (size_t)cap, "%s", alt);
}

/* The next core the shelf's Core row offers, or the previous one. */
void core_step(app *a, int sys, int d)
{
	system_cfg *s = &a->sys.systems[sys];
	char key[CFG_STR + 16], alt[CFG_STR];
	int n = core_offered_count(s), k, cur = 0;

	if (n < 2) return;
	for (k = 0; k < n; k++)
		if (core_offered_at(s, k, alt, sizeof alt) && !strcmp(alt, s->core)) cur = k;
	cur = (cur + d + n) % n;
	if (!core_offered_at(s, cur, alt, sizeof alt)) return;
	snprintf(s->core, sizeof s->core, "%s", alt);
	snprintf(key, sizeof key, "core.%s", s->tag);
	db_set_str(db_dev(), key, s->core);
	fprintf(stderr, "core: %s now runs on %s\n", s->name, s->core);
	dmode_settle(a, sys);
}

/* ---------- the Nintendo DS's game language ------------------------------- */

/* Game Language, the row under Core on the Nintendo DS shelf's menu
 * (src/dslang.h): Auto, the language of MENU > Language where the DS has it
 * and English where not, or one of the choices outright. Saved as
 * ds_language the moment it changes; the next game started runs in it
 * (src/platform.c sends it as the core's dsperate_language), and so does the
 * next Reset of the one running (mk/patches/dsperate-lr-language.patch). */
dsl_choice dslang_load(void)
{
	char v[16];

	db_get_str(db_dev(), DSLANG_SETTING, v, sizeof v, "auto");
	return dslang_parse(v);
}

/* Timing (src/pacing.h): the system's own, as the device's database keeps
 * it, and its systems.cfg word until the player picks */
pacing_choice pacing_load(const system_cfg *s)
{
	char key[32], v[16];
	pacing_choice own = pacing_parse(s->pace, PACING_EXACT);

	pacing_key(s->tag, key, sizeof key);
	db_get_str(db_dev(), key, v, sizeof v, pacing_id(own));
	return pacing_parse(v, own);
}

/* A system with a pace line, and Diatom to pace it: a program of its own
 * ("run:") or a port has its own timing */
bool pacing_row_shown(const app *a, int sys)
{
	const system_cfg *s = &a->sys.systems[sys];

	return s->pace[0] && strncmp(s->core, "run:", 4) != 0 && strcmp(s->core, "port") != 0;
}

/* Super Game Boy (src/sgbmode.h): the system's own, as the device's
 * database keeps it, Off until the player picks */
sgbm_choice sgbm_load(const system_cfg *s)
{
	char key[32], v[16];

	sgbm_key(s->tag, key, sizeof key);
	db_get_str(db_dev(), key, v, sizeof v, sgbm_id(SGBM_OFF));
	return sgbm_parse(v);
}

/* Border's row on a system's menu (src/border.h): its value while the
 * system's display mode leaves room around its games, by the size they
 * last said (border_room_kept), or in any mode where a handheld's frame is
 * drawn for it (the Game Boys: choosing one turns Display to Sharp Pixels);
 * NULL, no row, else, and on a shelf of no console (Favorites, MP4, ports) */
const char *border_row_value(const app *a, int sys)
{
	const system_cfg *s = &a->sys.systems[sys];
	bool room;

	if (!s->core[0] || is_mp4(s) || is_port(s) || a->view[sys].owner) return NULL;
	room = border_room_kept(a, sys);
	if (!border_row_shows(border_offer_room(a, sys, room), room)) return NULL;
	return T(border_label_id(border_load_room(a, sys, room)));
}

/* The Game Boy's and the Game Boy Color's shelves, while mGBA runs them */
bool sgb_row_shown(const app *a, int sys)
{
	const system_cfg *s = &a->sys.systems[sys];

	return sgbm_shelf(s->tag, s->core);
}

bool dslang_row_shown(const app *a, int sys)
{
	const system_cfg *s = &a->sys.systems[sys];

	return !strcmp(s->tag, DSLANG_TAG) && !strcmp(s->core, "dsperate");
}

/* What the row says for choice `c`: a language by its own name, and Auto by
 * the language it comes to - "Auto (English)" for a handheld in Português
 * (Brasil) - since a row that reads only "Auto" makes the player guess. */
const char *dslang_label(dsl_choice c, char *buf, size_t n)
{
	if (c == DSL_AUTO) {
		dsl_choice to = dslang_parse(dslang_value(i18n_lang()));

		snprintf(buf, n, T(S_SYSMENU_DSLANG_AUTO), lang_name(dslang_label_lang(to)));
	} else {
		snprintf(buf, n, "%s", lang_name(dslang_label_lang(c)));
	}
	return buf;
}

/* A DS game's resume, by the language it was left in (src/dsresume.h): for
 * the game about to start, `st` its auto state, the language it will run in
 * into `lang` ("" when that cannot be known, or the game is not a DS game on
 * DSperate), and true when its auto state was made in another one, so it
 * starts fresh instead. The language is the one the core gets: the last
 * dsperate_language the launcher sends, which is the row's unless the
 * library database has one of its own (src/platform.c). */
bool ds_resume_fresh(const system_cfg *s, const char *romfile, const char *st,
                     char *lang, size_t n)
{
	char fw[CFG_STR * 2], opt[32] = "", was[16];
	const char *eff, *rec;
	int i, k = plat_coreopt_count(s->tag);

	lang[0] = '\0';
	if (strcmp(s->tag, DSLANG_TAG) || strcmp(s->core, "dsperate")) return false;
	for (i = 0; i < k; i++) {
		const char *kv = plat_coreopt(s->tag, i);

		if (!strncmp(kv, DSLANG_KEY "=", sizeof DSLANG_KEY))
			snprintf(opt, sizeof opt, "%s", kv + sizeof DSLANG_KEY);
	}
	snprintf(fw, sizeof fw, "%s/Bios/firmware.bin", P_CARD);
	if (!(eff = dsr_effective(opt, fw))) return false;
	snprintf(lang, n, "%s", eff);
	if (access(st, F_OK) != 0) return false;
	rec = dsr_recorded(st, fw, was, sizeof was);
	if (!dsr_start_fresh(rec, eff)) return false;
	fprintf(stderr, "dslang: %s: its auto state was made in %s, the game runs in %s now: "
	        "starting it fresh\n", romfile, rec, eff);
	return true;
}

/* A game's resume, by the core that left it (src/corestate.h): true when its
 * auto state `st` was written by another core of the shelf than the one the
 * game starts on now, so it starts fresh instead of having Diatom refuse the
 * state without a word. The N64's: a game left on Mupen64Plus-Next, before
 * ParallelN64 was the default, or on whichever the Core row chose then. */
bool core_resume_fresh(const system_cfg *s, const char *romfile, const char *st)
{
	char who[96];

	if (cfg_core_count(s) < 2 || !cst_writer(st, who, sizeof who)) return false;
	if (!cst_other_core(who, s->core, s->cores)) return false;
	fprintf(stderr, "corestate: %s: its auto state was left by %s, the game runs on %s now: "
	        "starting it fresh\n", romfile, who, s->core);
	return true;
}

/* As the game ends: when its auto state was written in this run (it is not
 * the file `before` saw), the language it was written in goes beside it. The
 * state's own time and size, not the way the game ended: Diatom writes it at
 * a quit, at POWER and at a shutdown's TERM alike, and not after a crash. */
void ds_resume_record(const char *st, const char *lang, const struct stat *before,
                      bool had)
{
	struct stat now;

	if (!lang[0] || stat(st, &now) != 0) return;
	if (had && now.st_mtime == before->st_mtime && now.st_size == before->st_size &&
	    now.st_ino == before->st_ino)
		return;
	if (!dsr_record(st, lang))
		fprintf(stderr, "dslang: could not write the language beside %s\n", st);
}

/* ---------- two screens ---------------------------------------------------- */

/* A Nintendo DS game on a libretro core is one frame with both screens in it,
 * top over bottom; Diatom lays them out when told the frame holds two
 * (RUN screens=2, Diatom's src/screens.h). Keyed on the tag, not on the core:
 * any DS core hands over the same stacked frame. The layout is kept per
 * system, screens.<TAG>, single (the top screen alone) until chosen, and sent
 * after every RUN, as the display mode is. The swap and the stylus are the
 * game's, from the top screen and off: L2 and R2 change them in the game
 * (Diatom's screens.h), and the in-game menu reads them back from Diatom. */
/* The game running now is one: set at launch, read by the in-game menu. */
bool g_game_dual;

bool two_screens(const system_cfg *s)
{
	return !strcmp(s->tag, "NDS") && strncmp(s->core, "run:", 4) != 0 &&
	       strcmp(s->core, "port") != 0;
}

void screens_get(const char *tag, int *layout, bool *swap)
{
	char key[32], v[32];

	snprintf(key, sizeof key, "screens.%s", tag);
	*layout = db_get_str(db_dev(), key, v, sizeof v, NULL) ? gm_layout_index(v)
	                                                      : GM_LAYOUT_DEFAULT;
	*swap = false;
}

void screens_put(const char *tag, int layout, bool swap)
{
	char key[32];

	snprintf(key, sizeof key, "screens.%s", tag);
	db_set_str(db_dev(), key, gm_layout_names[layout]);
	plat_resident_line("SETSCREENS\tlayout=%s\tswap=%d", gm_layout_names[layout], swap ? 1 : 0);
}

/* ---------- the quick slot ------------------------------------------------- *
 *
 * SELECT+R1 saves a state to it and SELECT+L1 loads it (Diatom's chord.h),
 * and the in-game menu's Quick Slot row picks it: one of the six, for each
 * system, slot 1 until one is picked. Kept in the device's database beside
 * screens.<TAG>, under quickslot.<TAG>, so it stays on the card. */
/* The last system asked for and its slot: the in-game menu builds its rows
 * every frame, and the database is read once, not sixty times a second.
 * Written only through quick_slot_put, which keeps it. */
static struct { char tag[CFG_STR]; int slot; } quick_seen;

int quick_slot_get(const char *tag)
{
	char key[48], v[8];

	if (quick_seen.slot && !strcmp(quick_seen.tag, tag)) return quick_seen.slot;
	gm_quick_key(tag, key, sizeof key);
	snprintf(quick_seen.tag, sizeof quick_seen.tag, "%s", tag);
	quick_seen.slot = gm_quick_slot(db_get_str(db_dev(), key, v, sizeof v, NULL) ? v : NULL);
	return quick_seen.slot;
}

void quick_slot_put(const char *tag, int slot)
{
	char key[48], v[8];

	if (slot < 1 || slot > GM_SLOTS) slot = GM_QUICK_DEFAULT;
	gm_quick_key(tag, key, sizeof key);
	snprintf(v, sizeof v, "%d", slot);
	db_set_str(db_dev(), key, v);
	snprintf(quick_seen.tag, sizeof quick_seen.tag, "%s", tag);
	quick_seen.slot = slot;
}

/* The game being played, for the hotkeys: they come while this process waits
 * on the game and has no shelf to look at, so the launch leaves what they
 * need here (quick_bind). A slot's files are <stem>.<n>.state and .bmp, as
 * the menu's Save writes them (slot_state_path). `asked` is the slot of a
 * SAVE or LOAD sent and not yet answered, 0 for none, and `path` its state. */
static struct {
	char tag[CFG_STR];
	char name[CFG_STR];
	char file[LIB_PATH];  /* the game's, for what is kept per game (rms_stick_put) */
	system_cfg sys;       /* its system, whose stick default the key is kept against */
	char stem[LIB_PATH * 2];
	bool states;
	int  asked;
	bool load;
	char path[LIB_PATH * 2 + 16];
} g_quick;

/* ---------- the Nintendo 64's Pak ------------------------------------------ *
 *
 * What an N64 game's controller holds: the Rumble Pak, ZyperOS's default
 * (src/db.c: the Pixel 2 has a motor), or the Controller Pak, the memory
 * card some games save to - Mario Kart 64's ghosts, and games that save
 * nowhere else. Chosen per game in the in-game menu's Pak row, since it is
 * the game that needs one or the other, and kept in the library's database
 * as pak.<TAG>.<file>, beside the system's core options; a game never chosen
 * for has the system's (coreopt.N64.parallel-n64-pak1, or
 * coreopt.N64.mupen64plus-pak1 on the shelf's other core). Both cores read
 * their option again whenever one changes and swap the pak in the controller
 * there and then, so the row works while the game runs, and the Controller
 * Pak's contents live in the game's own save RAM, its .srm, whichever is in.
 * The choice is the game's, not the core's: it holds on either. */
bool pak_system(const system_cfg *s)
{
	return s && gm_pak_option(s->core) != NULL;
}

/* The system's own: its core's option, or the Rumble Pak */
static const char *pak_default(const system_cfg *s)
{
	const char *opt = gm_pak_option(s->core);
	size_t len = opt ? strlen(opt) : 0;
	int i, n = plat_coreopt_count(s->tag);

	for (i = 0; opt && i < n; i++) {
		const char *kv = plat_coreopt(s->tag, i);

		if (!strncmp(kv, opt, len) && kv[len] == '=')
			return gm_pak_value(kv + len + 1);
	}
	return gm_pak_value(NULL);
}

const char *pak_get(const system_cfg *s, const char *file)
{
	char key[CFG_STR + LIB_PATH + 8], v[16];

	gm_pak_key(s->tag, file, key, sizeof key);
	return db_get_str(db_lib(), key, v, sizeof v, NULL) ? gm_pak_value(v) : pak_default(s);
}

void pak_put(const char *tag, const char *file, const char *value)
{
	char key[CFG_STR + LIB_PATH + 8];

	gm_pak_key(tag, file, key, sizeof key);
	db_set_str(db_lib(), key, gm_pak_value(value));
}

/* ---------- D-pad as Stick: kept per game (src/remapstore.c) ---------------
 *
 * The handheld has no stick. A system with a stick|tag|mode line
 * (config/systems.cfg) can turn the d-pad into the core's left stick, game by
 * game: the N64 starts with it on, the Dreamcast and the PlayStation off.
 * The PSP's (stick|PSP|toggle) starts with the d-pad as the d-pad, and R2
 * turns it into the left stick and back (Diatom's RUN stick=toggle), as the
 * Pixel 2's own firmware and Deadstation Portable do it. The side a game was
 * left on is kept for that game, so a game that moves with the stick starts
 * with it the next time. */

/* ---------- per-system sort order ----------------------------------------- */

/* Keyed on the tag for the same reason display.<TAG> is, and stored beside it.
 * Loading does NOT re-sort: it runs before the scan on a rescan and after it
 * at startup, and the shelf is put in order by sort_all once both are done.
 *
 * MP4's shelf has orders of its own, by artist and by album, stored by their
 * own names under the same key - sort.MUSE. */
void sort_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		if (!db_get_str(db_dev(), key, name, sizeof name, NULL)) continue;
		a->view[i].sort = is_mp4(&a->sys.systems[i]) ? (int)ml_order_index(name)
		                                              : sort_index(name);
	}
	/* Favorites First beside it, favfirst.<TAG>: "on", or nothing as it
	 * ships */
	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "favfirst.%s", a->sys.systems[i].tag);
		a->view[i].fav_first = db_get_str(db_dev(), key, name, sizeof name, NULL) &&
		                       !strcmp(name, "on");
	}
}

/* MENU > View, per shelf like Sort By: "list", "grid" or none (the
 * covers), kept in the device's database by the system's tag (DESIGN.md
 * 11.39, 11.47) */
static bool view_is(const char *tag, const char *want)
{
	char key[CFG_STR + 16], v[16];

	snprintf(key, sizeof key, "view.%s", tag);
	return db_get_str(db_dev(), key, v, sizeof v, NULL) && !strcmp(v, want);
}

bool view_listed(const char *tag) { return view_is(tag, "list"); }
bool view_grid(const char *tag)   { return view_is(tag, "grid"); }
bool view_video(const char *tag)  { return view_is(tag, "video"); }

/* Recents' Card Picture (DESIGN.md 11.48): its covers, as it ships, or the
 * screen each game was left on (src/screencard.h), kept in the device's
 * database as RECENTS_PICTURE_KEY's "screen" */
bool g_recents_screen;

static void recents_picture_load(void)
{
	char v[16];

	g_recents_screen = db_get_str(db_dev(), RECENTS_PICTURE_KEY, v, sizeof v, NULL) &&
	                   !strcmp(v, "screen");
}

void recents_picture_turn(app *a)
{
	int r = shelf_recents_at(a);

	g_recents_screen = !g_recents_screen;
	db_set_str(db_dev(), RECENTS_PICTURE_KEY, g_recents_screen ? "screen" : "cover");
	/* Every card of it asked for again, the other picture */
	if (r >= 0) free_view_textures(&a->view[r]);
}

/* Recents' Last Screen for game `g` of system `o`: its resume point's
 * picture, written beside its state as the game ended, and its cover, into
 * `frame` and `cover` ("" for none). False when it has no resume point, and
 * the card is its cover, as with Cover. Asked only while the card has no
 * texture (game_card_wanted). */
/* Game `g` of system `o`'s cover on the card, its own or
 * EmulationStation's, into `cover`; "" for none */
static void game_cover_path(app *a, int o, const game_entry *g, char *cover, size_t cn)
{
	/* The one lookup the handheld and File Transfer's page share
	 * (src/esmedia.h, game_media): "" for none */
	game_media(P_ROMS, a->sys.systems[o].folder, g->file, g->name, ES_COVER, true, cover, cn);
}

static bool recents_screen_paths(app *a, int o, const game_entry *g, char *frame, size_t fn,
                                 char *cover, size_t cn)
{
	char st[LIB_PATH * 3];
	struct stat sb;

	state_path(a, o, g, st, sizeof st);
	preview_path(a, o, g, frame, fn);
	if (stat(st, &sb) != 0 || stat(frame, &sb) != 0 || sb.st_size <= 0) return false;
	game_cover_path(a, o, g, cover, cn);
	return true;
}

/* A card made from a picture of play with the game's cover small in its
 * corner (src/screencard.h), for entry `g` of shelf `s` (system `o`):
 * Recents' Last Screen, and every shot on the Screenshots shelf (DESIGN.md
 * 11.68: the shot, and its game's cover as SHOTS_CARD_COVER says). False
 * for a card of another kind. */
static bool screen_card_paths(app *a, int s, int o, const game_entry *g, char *frame, size_t fn,
                              char *cover, size_t cn)
{
	if (shots_shelf(a, s)) {
		struct stat sb;

		/* The game's own cover by its name (a shot's file is its
		 * picture's, so EmulationStation's, by the game's file, is not
		 * looked for) */
		shots_png(g, frame, fn);
		cover[0] = '\0';
		if (SHOTS_CARD_COVER) {
			box_art_path(a->sys.systems[o].folder, g->name, cover, cn);
			if (stat(cover, &sb) != 0 || sb.st_size <= 0) cover[0] = '\0';
		}
		return true;
	}
	return g_recents_screen && s == shelf_recents_at(a) && recents_screen_paths(a, o, g, frame, fn, cover, cn);
}

void view_load(app *a)
{
	recents_picture_load();
	for (int i = 0; i < a->sys.count; i++) {
		a->view[i].listed = view_listed(a->sys.systems[i].tag);
		a->view[i].grid = !a->view[i].listed && view_grid(a->sys.systems[i].tag);
		/* Never MP4's (view_turn) */
		a->view[i].video = !a->view[i].listed && !a->view[i].grid && view_video(a->sys.systems[i].tag) &&
		                   !is_mp4(&a->sys.systems[i]);
		a->view[i].lm.set = false;
		a->view[i].gm.set = false;
		a->view[i].vm.set = false;
	}
}

static void view_save(app *a, int sys)
{
	char key[CFG_STR + 16];

	snprintf(key, sizeof key, "view.%s", a->sys.systems[sys].tag);
	db_set_str(db_dev(), key, a->view[sys].listed ? "list" : a->view[sys].grid ? "grid"
	                          : a->view[sys].video ? "video" : "covers");
}

/* View turned on shelf `sys`, `dir` 1 or -1 round Covers, Grid, List and
 * Video, saved, the shelf on the same entry: START on the shelf goes
 * forward, and MENU's View row either way (2026-11-15: the grid between the
 * two; 2026-11-19: Video, the fourth) */
void view_turn(app *a, int sys, int dir)
{
	sysview *v = &a->view[sys];
	int k = view_index(v);

	/* MP4's shelf has no Video view: an album has no snap to play
	 * (DESIGN.md 11.67) */
	do k = (k + (dir < 0 ? VIEW_COUNT - 1 : 1)) % VIEW_COUNT;
	while (k == VIEW_VIDEO && is_mp4(&a->sys.systems[sys]));
	view_set(a, sys, k, true);
}

/* Shelf `sys` in view `k` (VIEW_COVERS, VIEW_GRID, VIEW_LIST, VIEW_VIDEO),
 * on the same entry; saved with `keep`, or only shown (View's list's cursor,
 * src/ds/hover.h) */
void view_set(app *a, int sys, int k, bool keep)
{
	sysview *v = &a->view[sys];

	v->listed = k == VIEW_LIST;
	v->grid = k == VIEW_GRID;
	v->video = k == VIEW_VIDEO;
	v->lm.set = false;
	v->gm.set = false;
	v->vm.set = false;
	if (keep) view_save(a, sys);
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
}

/* The home's layout turned, `dir` 1 or -1 round Spotlight, Gallery and Grid,
 * saved, the home on the same system: START on the home goes forward, as it
 * turns a shelf's View, and MENU's Home Screen row either way (2026-11-17:
 * "also implement start in the home menu with the systems") */
void home_turn(app *a, int dir)
{
	a->home_layout = (home_layout)(((int)a->home_layout + (dir < 0 ? HOME_LAYOUTS - 1 : 1)) % HOME_LAYOUTS);
	db_set_str(db_dev(), "home", home_id(a->home_layout));
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
	redraw_now();
}

/* The grid's shape kept for shelf `sys` (src/gamegrid.h): by its tag, and
 * the folder of it on screen, so each folder of a shelf keeps its own; 0
 * for none yet */
static void grid_shape_key(const app *a, int sys, char *key, size_t n)
{
	const sysview *v = &a->view[sys];

	snprintf(key, n, "grid.shape.%s%s%s", a->sys.systems[sys].tag, v->dir[0] ? "/" : "", v->dir);
}

float grid_shape_load(const app *a, int sys)
{
	char key[CFG_STR + LIB_PATH + 16], v[24];

	grid_shape_key(a, sys, key, sizeof key);
	return db_get_str(db_dev(), key, v, sizeof v, NULL) ? (float)atof(v) : 0;
}

void grid_shape_save(const app *a, int sys, float shape)
{
	char key[CFG_STR + LIB_PATH + 16], v[24];

	grid_shape_key(a, sys, key, sizeof key);
	snprintf(v, sizeof v, "%.3f", shape);
	db_set_str(db_dev(), key, v);
}

void sort_save(app *a)
{
	char key[CFG_STR + 16];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		db_set_str(db_dev(), key,
		           is_mp4(&a->sys.systems[i]) ? ml_order_name((ml_order)a->view[i].sort)
		                                       : SORTS[a->view[i].sort].name);
	}
}

/* Put every shelf in its own order. One pass over the systems, and the
 * session rows are folded once per system that needs them rather than once
 * per game - which is why this is a loop here and not a call inside scan_all.
 *
 * The favorites shelf is built elsewhere and keeps its own order: it is a
 * shelf of games from many systems and has no single tag to hang a setting
 * off. */
/* One shelf in its own order.
 *
 * Favorites through sort_apply_owned: its games are several systems', and each
 * one's owner has to come with it and its play time has to be read under its
 * own system's tag. This ran the plain sort on Favorites as well, which only
 * held because Favorites was always sorted by name, the order it is built in -
 * any other order would have launched some favorites under another system's
 * core.
 *
 * MP4 by its own orders, in mp4_order_view: its cards are albums, and which
 * album each card is has to move with it. */
void sort_shelf(app *a, int sys)
{
	sysview *v = &a->view[sys];

	v->nfav = 0;
	if (is_mp4(&a->sys.systems[sys])) { mp4_order_view(v); return; }
	/* Screenshots' three, each game's newest first (src/shots_screen.h) */
	if (shots_shelf(a, sys)) { shots_sort(a, sys); return; }
	/* The added times this order reads, each game's from its own system's
	 * folder (Favorites' from its owner's), where the scan left them unknown */
	if (sort_needs_added(v->sort))
		for (int i = v->nfold; i < v->list.count; i++)
			if (v->list.items[i].added == LIB_ADDED_UNKNOWN)
				v->list.items[i].added = lib_added(P_ROMS,
					a->sys.systems[v->owner ? v->owner[i] : sys].folder,
					v->list.items[i].file, v->list.items[i].dir);
	if (v->owner) {
		const char *tags[CFG_MAX_SYSTEMS];
		int i;

		/* Every owner's, the systems off the home too (app.parked) */
		for (i = 0; i < sys_kept(a) && i < CFG_MAX_SYSTEMS; i++)
			tags[i] = a->sys.systems[i].tag;
		/* Collections' top is its folders alone, A to Z (src/collect.h) */
		sort_apply_owned(v->list.items + v->nfold, v->owner + v->nfold,
		                 v->list.count - v->nfold, v->sort, tags);
		return;
	}
	/* A shelf's folders stay first, A to Z (src/shelfdir.h): the order is
	 * its games' */
	sort_apply(v->list.items + v->nfold, v->list.count - v->nfold, v->sort,
	           a->sys.systems[sys].tag);
	/* Favorites First (DESIGN.md 11.70): its favorites ahead of its other
	 * games, each part in that order, still after the folders */
	v->fav_from = v->nfold;
	v->nfav = v->fav_first && fav_first_offered(a, sys)
	        ? sort_favorites_first(v->list.items + v->nfold, v->list.count - v->nfold,
	                               a->sys.systems[sys].tag)
	        : 0;
	/* Splore first among the PICO-8 shelf's, whatever the order (Tarik's
	 * pick, 2026-11-19: the shelf's first entry), ahead of the favorites
	 * too: it is no game to favorite */
	if (!strcmp(a->sys.systems[sys].tag, P8_TAG)) {
		int at = v->nfold;

		splore_pin(v->list.items + v->nfold, v->list.count - v->nfold);
		if (v->nfav && at < v->list.count && splore_file(v->list.items[at].file) &&
		    !fav_is(a->sys.systems[sys].tag, v->list.items[at].file))
			v->fav_from++;
	}
}

/* Favorites First (DESIGN.md 11.70) is a console shelf's: Favorites is
 * all favorites, Recents goes by when, a collection by its list, and MP4's
 * albums and Screenshots' shots are no games to favorite */
bool fav_first_offered(app *a, int sys)
{
	return sys >= 0 && sys < a->sys.count && !is_mp4(&a->sys.systems[sys]) &&
	       !a->view[sys].owner && !shots_shelf(a, sys);
}

/* Shelf `sys` sorted again with the cursor on the game it was on, not on
 * the place: Sort By, Favorites First, and a favorite marked or taken away
 * while Favorites First is on. The textures are indexed by place, so they
 * are let go, and each card asks for its art where it now is. */
void sort_shelf_keep(app *a, int sys)
{
	sysview *v = &a->view[sys];
	char keep[LIB_PATH];
	int i, keep_owner = -1;

	keep[0] = '\0';
	if (v->cursor >= 0 && v->cursor < v->list.count) {
		snprintf(keep, sizeof keep, "%s", v->list.items[v->cursor].file);
		/* On Favorites a file name is not enough: the same one can be on
		 * the shelf twice, from two systems. */
		if (v->owner) keep_owner = v->owner[v->cursor];
	}
	sort_shelf(a, sys);
	free_view_textures(v);
	v->cursor = 0;
	for (i = 0; i < v->list.count; i++)
		if (!strcmp(v->list.items[i].file, keep) && (!v->owner || v->owner[i] == keep_owner)) {
			v->cursor = i;
			break;
		}
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
}

/* MENU > Favorites First on shelf `sys` turned over, kept by its tag, and
 * the shelf sorted again on the same game */
void fav_first_turn(app *a, int sys)
{
	char key[CFG_STR + 16];

	if (!fav_first_offered(a, sys)) return;
	a->view[sys].fav_first = !a->view[sys].fav_first;
	snprintf(key, sizeof key, "favfirst.%s", a->sys.systems[sys].tag);
	db_set_str(db_dev(), key, a->view[sys].fav_first ? "on" : "off");
	sort_shelf_keep(a, sys);
}

void sort_all(app *a)
{
	int i;

	for (i = 0; i < a->sys.count; i++) sort_shelf(a, i);
}

/* ---------- textures ----------------------------------------------------- */


/* How far down the surface its opaque pixels reach, as a fraction of height.
 *
 * Art here is squared and centered, so a console photographed low in its frame
 * carries a band of transparency underneath - 102 of 384 rows for the Genesis,
 * against 10 for a Game Boy. Reflecting from the card's edge mirrors that
 * emptiness too, which is why the same reflection setting produced a flush
 * reflection on one card and a gap of about 196px on another.
 *
 * Scanned bottom-up and stopping at the first row with anything in it, so the
 * cost is the padding rather than the image. Alpha over 8 rather than over 0:
 * a PNG's fully transparent region is not always exactly zero. */
static float content_bottom(SDL_Surface *s)
{
	int y, x;

	if (!s || s->h <= 0 || s->format->BytesPerPixel != 4) return 1.0f;
	for (y = s->h - 1; y >= 0; y--) {
		const Uint32 *row = (const Uint32 *)((Uint8 *)s->pixels + y * s->pitch);

		for (x = 0; x < s->w; x++)
			if ((row[x] >> 24) > 8)
				return (float)(y + 1) / (float)s->h;
	}
	return 1.0f;
}

/* A picture off the card, as ARGB8888, the one form everything here reads
 * pixels in; NULL if it is not there or will not decode. */
static SDL_Surface *load_surface(const char *path)
{
	/* A PICO-8 cart as its label (src/cartlabel.h) */
	return cart_label_load(path);
}

SDL_Texture *load_image(SDL_Renderer *r, const char *path, int *w, int *h,
                        float *cb)
{
	SDL_Surface *conv = load_surface(path);
	SDL_Texture *t;

	if (cb) *cb = 1.0f;
	if (!conv) return NULL;
	if (cb) *cb = content_bottom(conv);
	t = SDL_CreateTextureFromSurface(r, conv);
	cart_label_sharp(t, conv);
	SDL_FreeSurface(conv);
	if (t) SDL_QueryTexture(t, NULL, NULL, w, h);
	return t;
}


/* MP4: what the Music folder holds, read at boot and on every rescan.
 *
 * RECOGNIZED BY ITS TAG, not by what it lacks. Favorites is the shelf with no
 * core and no folder, and the system menu tells them apart that way; MP4 has
 * neither either, but its music is not a ROM folder at all, so an empty folder
 * is the honest value and the tag is what says which of the two it is. */
ml_lib g_mp4;
home_song g_home_song;
static int g_home_track = -1;
/* Where the scan read it from: the card, which holds Music and Audiobooks. */
char   g_mp4_root[CFG_STR * 2];
/* Bumped by every build of the library, so an MP4 screen can tell that MENU's
 * Rescan Folder replaced it while its menu was open - see mp4_menu. */
unsigned g_mp4_gen;

bool is_mp4(const system_cfg *s)
{
	return s && !strcmp(s->tag, "MUSE");
}

/* What is known about each album's cover: one per album in g_mp4, rebuilt
 * with it. ASKED carries when, because an answer can be lost with the daemon,
 * and an album is then asked about again rather than waited on for good.
 * The states, COV_*, and cover_state are in src/app.h. */
cover_state *g_cov;

/* ---- MP4: books ------------------------------------------------------------ */

/* Which kind MP4's shelf shows, music or books - and now videos, its
 * third kind (ml_kind). Eric's, 2026-09-27: one MP4 card and a Show row in
 * its menu, rather than a card for books beside it - two ways into one player
 * read as two players. Kept across restarts. Or all three on one shelf,
 * ML_ALL, which is where it starts (2026-11-09). */
int g_mp4_kind = ML_ALL;

/* Whether each album is a book listened to the end: one per album in g_mp4,
 * rebuilt with it, like g_cov. A finished book is marked and not forgotten,
 * and A on it starts it over. Eric's, 2026-09-27. */
bool *g_book_done;

/* The kind the shelf really shows: the one chosen, unless there is none of it
 * - then the first kind there is some of. All, on a card with one kind, is
 * that kind: the shelf and its menu say Albums, not All, when albums are all
 * there is. */
ml_kind mp4_kind_shown(void)
{
	int k;

	if (g_mp4_kind == ML_ALL && mp4_kinds() > 1) return ML_ALL;
	if (g_mp4_kind >= 0 && g_mp4_kind < ML_KINDS && ml_count_kind(&g_mp4, (ml_kind)g_mp4_kind))
		return (ml_kind)g_mp4_kind;
	for (k = 0; k < ML_KINDS; k++)
		if (ml_count_kind(&g_mp4, (ml_kind)k)) return (ml_kind)k;
	return ML_MUSIC;
}

bool mp4_books_shown(void) { return mp4_kind_shown() == ML_BOOKS; }

/* The kind MP4's menu has the rows of (sys_menu_mp4_rows): the shelf's,
 * but All on a card with no songs has the videos' - no Shuffle All, no Album
 * Art - as sys_menu_build makes of the same facts */
int mp4_rows_kind(void)
{
	ml_kind k = mp4_kind_shown();

	return k == ML_ALL && !ml_count_kind(&g_mp4, ML_MUSIC) ? ML_VIDEOS : k;
}

/* How many kinds the card has: Show is in the menu only when it is two or
 * more. */
int mp4_kinds(void)
{
	int k, n = 0;

	for (k = 0; k < ML_KINDS; k++)
		if (ml_count_kind(&g_mp4, (ml_kind)k)) n++;
	return n;
}

/* Where book `al`'s place is kept: the settings, under its folder. */
void book_key(int al, char *out, size_t n)
{
	const char *p = g_mp4.tracks[g_mp4.albums[al].first].path;
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "book.%.*s", slash ? (int)(slash - p) : 0, p);
}

/* A place is "<seconds>\t<file>", the file by its name rather than its number
 * so a file added to the folder later moves nothing, or "finished". */
static void book_write(int al, const char *track, double at)
{
	char key[LIB_PATH + 8], val[LIB_PATH + 32];
	const char *file = strrchr(track, '/');

	book_key(al, key, sizeof key);
	snprintf(val, sizeof val, "%.1f\t%s", at, file ? file + 1 : track);
	db_set_str(db_dev(), key, val);
	g_book_done[al] = false;
}

static void book_finish(int al)
{
	char key[LIB_PATH + 8];

	book_key(al, key, sizeof key);
	db_set_str(db_dev(), key, "finished");
	g_book_done[al] = true;
	fprintf(stderr, "mp4: finished %s\n", g_mp4.albums[al].name);
}

/* Where book `al` starts: its file, and `at` seconds into it. The beginning
 * for a book never started, or finished. */
int book_place(int al, double *at)
{
	const ml_album *b = &g_mp4.albums[al];
	char key[LIB_PATH + 8], val[LIB_PATH + 32];
	const char *tab;
	int i;

	*at = 0;
	book_key(al, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "");
	if (!(tab = strchr(val, '\t'))) return 0;
	for (i = 0; i < b->n; i++) {
		const char *p = g_mp4.tracks[b->first + i].path;
		const char *f = strrchr(p, '/');

		if (!strcmp(f ? f + 1 : p, tab + 1)) { *at = atof(val); return i; }
	}
	return 0;
}

/* KEEPING A BOOK'S PLACE, wherever MP4 is polled: on its screens, over the
 * shelf and the menus, and through a game, which is where a book is likely to
 * be heard.
 *
 * Written when the file changes, when it pauses or stops, and every thirty
 * seconds of listening in between, so a Brick that loses its battery goes back
 * half a minute at most. `now` writes whatever there is, for the power button
 * and for a new queue about to replace this one. */
static struct { int al, index; mu_state st; double at; } g_bk = { -1, -1, MU_OFF, 0 };

/* A VIDEO'S PLACE, kept the same way for each file: a film runs two hours and
 * is watched in pieces, as a book is listened to. Its own key per file, since
 * a folder of films or episodes is many places, and none kept for the first
 * and last minute - starting a film near its start is starting it, and a
 * place near the end would only replay the credits. my MP4, 2026-11-05. */
static struct { char track[LIB_PATH]; int index; mu_state st; double at, len; } g_vk = { "", -1, MU_OFF, 0, 0 };

static void video_key(const char *track, char *out, size_t n)
{
	snprintf(out, n, "video.%s", track);
}

static double video_place(const char *track)
{
	char key[LIB_PATH + 8], val[32];

	video_key(track, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "");
	return atof(val) > 0 ? atof(val) : 0;
}

static void video_write(const char *track, double at, double len)
{
	char key[LIB_PATH + 8], val[32] = "";

	video_key(track, key, sizeof key);
	if (at >= 60 && (len <= 0 || at < len - 60)) snprintf(val, sizeof val, "%.1f", at);
	db_set_str(db_dev(), key, val);
}

static void video_keep(bool now, bool ran_out)
{
	const mu_now *mn = mp4c_now();
	const char *track = mp4c_track(mn->index);

	if (!mp4c_is_videos() || !track) {
		g_vk.track[0] = '\0';
		return;
	}
	/* The file before this one, as it was left: a file that played out, or
	 * that was left in its last minute, starts from the top next time. */
	if (g_vk.track[0] && strcmp(g_vk.track, track)) {
		video_write(g_vk.track, ran_out || mn->index != g_vk.index ? g_vk.len : g_vk.at, g_vk.len);
		g_vk.track[0] = '\0';
	}
	if (ran_out) { video_write(track, mn->len, mn->len); g_vk.track[0] = '\0'; return; }
	if (mn->state != MU_PLAYING && mn->state != MU_PAUSED) return;
	if (now || strcmp(g_vk.track, track) || mn->state != g_vk.st || fabs(mn->at - g_vk.at) >= 30) {
		video_write(track, mn->at, mn->len);
		snprintf(g_vk.track, sizeof g_vk.track, "%s", track);
		g_vk.index = mn->index;
		g_vk.st = mn->state;
	}
	g_vk.at = mn->at;
	g_vk.len = mn->len;
}

/* MP4 BACK AFTER A POWER-OFF (src/mp4resume.h, DESIGN.md 11.67): what plays,
 * kept in one row the way a book's place is, by the same calls - when a
 * queue starts, when its file changes, when it pauses or plays, every thirty
 * seconds of playing, and at once at a power-off or a restart (book_keep's
 * `now`). The queue it is, set by whoever starts one (mp4_play): Shuffle
 * All's names no album. A queue that plays out to its end leaves none. */
static mp4resume_seen g_rs;
static mr_queue g_rs_queue = MR_ALBUM;

void resume_clear(void)
{
	db_del(db_dev(), MP4RESUME_KEY);
	g_rs = (mp4resume_seen){ 0 };
}

static void resume_keep(bool now, bool ran_out)
{
	const mu_now *mn = mp4c_now();
	const char *track = mp4c_track(mn->index);
	bool playing = mn->state == MU_PLAYING, heard = playing || mn->state == MU_PAUSED;
	double at = playing ? mp4c_at_live() : mn->at;
	mp4resume r = { .playing = playing, .queue = g_rs_queue, .at = at > 0 ? at : 0 };
	char v[MP4RESUME_PATH + 64];

	if (ran_out) { resume_clear(); return; }
	if (!mp4resume_due(&g_rs, heard, playing, track, at, now)) return;
	snprintf(r.track, sizeof r.track, "%s", track);
	if (mp4resume_format(&r, v, sizeof v)) db_set_str(db_dev(), MP4RESUME_KEY, v);
	mp4resume_wrote(&g_rs, playing, track, at);
}

void book_keep(bool now)
{
	const mu_now *mn = mp4c_now();
	const char *track;
	bool ran_out = mp4c_take_ran_out();
	int al;

	video_keep(now, ran_out);
	resume_keep(now, ran_out);
	if (!mp4c_is_book() || !g_book_done) { g_bk.al = -1; return; }
	track = mp4c_track(mn->index);
	al = ml_album_of(&g_mp4, track);
	if (al < 0 || !g_mp4.albums[al].book) { g_bk.al = -1; return; }
	if (ran_out) { book_finish(al); g_bk.al = -1; return; }
	if (mn->state != MU_PLAYING && mn->state != MU_PAUSED) return;
	if (now || al != g_bk.al || mn->index != g_bk.index || mn->state != g_bk.st ||
	    fabs(mn->at - g_bk.at) >= 30) {
		book_write(al, track, mn->at);
		g_bk.al = al;
		g_bk.index = mn->index;
		g_bk.st = mn->state;
		g_bk.at = mn->at;
	}
}

/* MP4's Quit on something paused (DESIGN.md 11.14): MP4 ends, where Quit on
 * something playing leaves it heard behind the home as SELECT does. A book's
 * or a video's place is written first, as for any stop, so the next play goes
 * on from it; the power-off row goes, a player quit being nothing to open at
 * the next start; and the queue stops, which takes MP4's mark and track out
 * of the top bar at once. */
void mp4_quit(void)
{
	book_keep(true);
	resume_clear();
	mp4c_stop();
}

/* MP4, polled: what the daemon said, and a book's place kept. Everything that
 * polls MP4 comes through here. */
void mp4_poll(void)
{
	mp4c_poll();
	book_keep(false);
	/* Menu music is quiet while MP4 is heard, or about to be */
	bgm_hold(BGM_HOLD_MP4, mp4c_heard());
	const mu_now *mn = mp4c_now();
	if (home_song_update(&g_home_song,
	                     mn->state == MU_PLAYING || mn->state == MU_PAUSED,
	                     mn->state == MU_PAUSED, mn->title) || mn->index != g_home_track) redraw_now();
	g_home_track = mn->index;
}

/* A book's speed, kept beside its place: the last one chosen on it, 1x for a
 * book never changed. Its own key rather than a third field in the place, so
 * a place written before speeds existed still reads. */
static void book_speed_key(int al, char *out, size_t n)
{
	const char *p = g_mp4.tracks[g_mp4.albums[al].first].path;
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "bookspeed.%.*s", slash ? (int)(slash - p) : 0, p);
}

double book_speed(int al)
{
	char key[LIB_PATH + 16], val[16];
	double x;

	book_speed_key(al, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "1");
	x = atof(val);
	return x >= 0.5 && x <= 2.0 ? x : 1.0;
}

/* Y on a book's Now Playing: the next speed, 1x to 2x in quarters and then
 * 0.75x before coming round - the speeds a listener reaches for, in the order
 * they reach for them. Set on the book as it plays and kept for it. */
void book_cycle_speed(void)
{
	static const double STEPS[] = { 1.0, 1.25, 1.5, 1.75, 2.0, 0.75 };
	int al = ml_album_of(&g_mp4, mp4c_track(mp4c_now()->index));
	double now = mp4c_speed(), next = STEPS[0];
	char key[LIB_PATH + 16], val[16];
	size_t k;

	for (k = 0; k < sizeof STEPS / sizeof STEPS[0]; k++)
		if (fabs(STEPS[k] - now) < 0.01) {
			next = STEPS[(k + 1) % (sizeof STEPS / sizeof STEPS[0])];
			break;
		}
	mp4c_set_speed(next);
	if (al < 0 || !g_mp4.albums[al].book) return;
	book_speed_key(al, key, sizeof key);
	snprintf(val, sizeof val, "%.2f", next);
	db_set_str(db_dev(), key, val);
}

/* A new queue. Whatever book it replaces has its place written first, since
 * the queue is the only record of where that was. */
void mp4_play(const char *const *paths, int n, int start, double at,
               bool book, double speed, const char *artist, const char *album)
{
	book_keep(true);
	g_bk.al = -1;
	g_vk.track[0] = '\0';
	/* A new queue: kept at once (resume_keep), Shuffle All's naming no album */
	g_rs = (mp4resume_seen){ 0 };
	g_rs_queue = !artist && !album ? MR_ALL : MR_ALBUM;
	mp4c_play(paths, n, start, at, book, speed, artist, album);
}

/* A queue of videos, from `start`, at its file's kept place. */
void mp4_play_videos(const char *const *paths, int n, int start,
                      const char *artist, const char *album)
{
	book_keep(true);
	g_bk.al = -1;
	g_vk.track[0] = '\0';
	g_rs = (mp4resume_seen){ 0 };
	g_rs_queue = MR_ALBUM;
	mp4c_play_videos(paths, n, start, start >= 0 && start < n ? video_place(paths[start]) : 0,
	                  artist, album);
}

/* What the row says played when the power went, back on Now Playing, paused
 * at its second, with no sound (mp4c_hold_next): an album's, a book's or a
 * series' files from the one it was on, or Shuffle All again from that song
 * (the songs after it a new order: the order is not kept, Shuffle All being
 * every song on the card). Nothing when the row says paused - it was not
 * heard - or when its file is not on the card any more, which takes the row
 * away. False when nothing was resumed. */
static bool resume_start(void)
{
	char v[MP4RESUME_PATH + 64];
	const char **paths = NULL;
	mp4resume r;
	int al, i, t, n = 0, start = -1;

	if (!db_get_str(db_dev(), MP4RESUME_KEY, v, sizeof v, NULL) || !mp4resume_parse(v, &r) || !r.playing)
		return false;
	al = ml_album_of(&g_mp4, r.track);
	if (al < 0 || !(paths = calloc((size_t)(g_mp4.ntracks > 0 ? g_mp4.ntracks : 1), sizeof *paths))) {
		fprintf(stderr, "mp4: %s is not on the card, nothing to resume\n", r.track);
		free(paths);
		resume_clear();
		return false;
	}
	if (r.queue == MR_ALL) {
		for (i = 0; i < g_mp4.nalbums; i++)
			if (g_mp4.albums[i].kind == ML_MUSIC)
				for (t = 0; t < g_mp4.albums[i].n; t++)
					paths[n++] = g_mp4.tracks[g_mp4.albums[i].first + t].path;
	} else {
		for (t = 0; t < g_mp4.albums[al].n; t++)
			paths[n++] = g_mp4.tracks[g_mp4.albums[al].first + t].path;
	}
	for (i = 0; i < n && start < 0; i++)
		if (!strcmp(paths[i], r.track)) start = i;
	if (start < 0) { free(paths); resume_clear(); return false; }
	mp4c_hold_next();
	if (r.queue == MR_ALL) {
		mp4_play(paths, n, start, r.at, false, 1.0, NULL, NULL);
	} else {
		const ml_album *b = &g_mp4.albums[al];
		const char *artist = b->solo ? "" : g_mp4.artists[mp4_artist_of(al)].name;

		if (b->kind == ML_VIDEOS) {
			book_keep(true);
			g_bk.al = -1;
			g_vk.track[0] = '\0';
			g_rs = (mp4resume_seen){ 0 };
			g_rs_queue = MR_ALBUM;
			mp4c_play_videos(paths, n, start, r.at, artist, b->name);
		} else {
			mp4_play(paths, n, start, r.at, b->book, b->book ? book_speed(al) : 1.0, artist, b->name);
		}
	}
	free(paths);
	fprintf(stderr, "mp4: resumed %s at %.1f, paused, after a power-off\n", r.track, r.at);
	return true;
}

/* A track's artist and album, as the shelf names them: for a queue drawn from
 * every album, Shuffle All's, which has no one album to name. */
static void mp4_track_names(const char *track, char *artist, size_t an,
                             char *album, size_t aln)
{
	int al = ml_album_of(&g_mp4, track);

	artist[0] = album[0] = '\0';
	if (al < 0) return;
	snprintf(artist, an, "%s", g_mp4.artists[mp4_artist_of(al)].name);
	snprintf(album, aln, "%s", g_mp4.albums[al].name);
}

/* The play modes as the player meets them: the name saved in the settings and
 * the mark. In order has no mark: it is what plays when nothing has been asked
 * for, and a mark meaning "nothing special" is noise.
 *
 * The name each mode was SHOWN by went with the notice that showed it - see
 * mp4_cycle_mode. Nothing spells a mode out now; the mark beside the track
 * count on Now Playing is the whole of it. */
const mp4_mode_name MP4_MODES[MUQ_MODES] = {
	[MUQ_IN_ORDER]   = { "in order",   -1 },
	[MUQ_REPEAT_ALL] = { "repeat all", UI_GLYPH_REPEAT },
	[MUQ_REPEAT_ONE] = { "repeat one", UI_GLYPH_REPEAT_ONE },
	[MUQ_SHUFFLE]    = { "shuffle",    UI_GLYPH_SHUFFLE },
};

/* MP4's side of this process: its daemon (none for a shot, which draws one
 * frame and exits), the names a queue's tracks go by, and the play modes the
 * player left (`mode`, a shot's --mode, for both). `route`: where its sound
 * goes is followed before each PLAY (aout_before_mp4), as the launcher and
 * the in-game menu over a port do; the follower beside a program of its own
 * keeps MP4 where the launcher left it (src/mp4hand.h). */
static void mp4_setup(bool shot, bool route, const char *mode)
{
	char bin[CFG_STR * 2], m[32];
	int k;

	snprintf(bin, sizeof bin, "%s/mp4d", P_ROOT);
	/* A shot starts no daemon: a cover it would ask for is drawn as missing
	 * instead - see mp4_covers_settle. */
	mp4c_init(shot ? "" : bin, P_CARD);
	mp4c_on_before_heard(route ? aout_before_mp4 : NULL);
	mp4c_names_by(mp4_track_names);
	/* The play mode the player left it in. */
	db_get_str(db_dev(), "mp4.mode", m, sizeof m, "in order");
	if (mode) snprintf(m, sizeof m, "%s", mode);
	for (k = 0; k < MUQ_MODES; k++)
		if (!strcmp(m, MP4_MODES[k].key)) mp4c_set_mode((muq_mode)k);
	/* Videos' own, MENU > Repeat on MP4's video shelves */
	db_get_str(db_dev(), "video.mode", m, sizeof m, "in order");
	if (mode) snprintf(m, sizeof m, "%s", mode);
	for (k = 0; k < MUQ_MODES; k++)
		if (!strcmp(m, MP4_MODES[k].key)) mp4c_set_video_mode((muq_mode)k);
}

/* MP4'S QUEUE HANDED OVER A PROGRAM OF ITS OWN (src/mp4hand.h, DESIGN.md
 * 11.55): your PICO-8 keeps MP4 playing, as a Diatom game does, and the
 * queue goes from process to process in this file. Written as the launcher
 * ends for the program (launch_port), as the in-game menu over it closes
 * with MP4 opened, and as the follower beside it ends; with no queue, a
 * file of no tracks, which still says MP4 is kept over this program. */
bool mp4_hand_save(void)
{
	mp4hand h;
	bool ok;

	if (!mp4c_hand_out(&h)) {
		memset(&h, 0, sizeof h);
		h.speed = 1.0;
		snprintf(h.root, sizeof h.root, "%s", P_CARD);
	}
	h.all = g_rs_queue == MR_ALL;
	ok = mp4hand_write(plat_flag(MP4HAND_PATH), &h);
	fprintf(stderr, "mp4: %s %d track%s, %s, at %d\n", ok ? "handed over" : "could not hand over",
	        h.n, h.n == 1 ? "" : "s", h.state == MH_PLAYING ? "playing" : h.state == MH_PAUSED ? "paused" : "stopped",
	        h.n ? h.pos + 1 : 0);
	mp4hand_free(&h);
	return ok;
}

/* YOUR PICO-8 QUIET WHILE MP4 PLAYS, as a Diatom game is (QUIET, DESIGN.md
 * 11.55): the equalizer's shared file's cut (eq_share_cut), which the
 * plugin already in every port's ALSA chain plays silence for, faded; no
 * stage of its own in the chain, which stays the one every port has. Said
 * by MP4's follower beside PICO-8 and by the in-game menu over it; 0 again
 * from the launcher as it starts and as it hands over (port_quiet_hand). */
static int g_port_quiet_said = -1;

/* Said when MP4's playing changes, or at once with `force`: by the follower
 * ten times a second, as the launcher says QUIET over a game, and by the
 * in-game menu over PICO-8 as it closes, before PICO-8 goes on */
static void port_quiet_follow(bool force)
{
	int q = mp4c_playing() ? 1 : 0;

	if (!force && q == g_port_quiet_said) return;
	if (!eq_share_cut(q)) {
		if (g_port_quiet_said != -2) fprintf(stderr, "quiet: no equalizer's file; PICO-8 heard\n");
		g_port_quiet_said = -2;
		return;
	}
	g_port_quiet_said = q;
	fprintf(stderr, "quiet: PICO-8 %s\n", q ? "quiet, MP4 plays" : "heard");
}

/* THE VOLUME BOOST OVER YOUR PICO-8 (src/boost.h, DESIGN.md 11.6), heard on
 * MP4 behind it. portkeys has the keys over PICO-8 and writes the boost's
 * gain into the equalizer's shared file, which the plugin in PICO-8's chain
 * reads; MP4 takes it only from a GAIN line on its socket (the launcher's
 * src/levels.c sends one with every step), and the launcher is gone. So the
 * follower, which is MP4's connection beside PICO-8, passes the file's gain
 * on at its ten a second (mp4c_gain says only a change): a step of the boost
 * over PICO-8 reaches the song playing as it does over a Diatom game. Before
 * this, with music over PICO-8 (PICO-8 quiet), the bar's cells moved and the
 * song stayed as loud as the launcher had left it. */
static void port_gain_follow(void)
{
	const int db = eq_share_gain_get();

	if (db >= 0) mp4c_gain(db);
}

/* As the launcher hands over to a program (launch_port) and as it starts:
 * your PICO-8 quiet from its first sound when MP4 plays (`pico8`), any other
 * program never, MP4 being stopped for it */
void port_quiet_hand(bool pico8)
{
	if (!pico8) {
		eq_share_cut(false);
		return;
	}
	g_port_quiet_said = -1;
	port_quiet_follow(true);
}

/* ...and taken on: the queue the file holds made this process's, where the
 * daemon says it is (mp4c_hand_in). True when a queue came with it. */
bool mp4_hand_take(void)
{
	mp4hand h;
	bool any;

	if (!mp4hand_read(plat_flag(MP4HAND_PATH), &h)) return false;
	/* No book_keep first, as a new queue has (mp4_play): this is the same
	 * queue, and the process that wrote the file kept its places as it did */
	mp4c_hand_in(&h);
	g_bk.al = -1;
	g_vk.track[0] = '\0';
	/* Kept at its first poll, as a new queue is (resume_keep) */
	g_rs = (mp4resume_seen){ 0 };
	g_rs_queue = h.all ? MR_ALL : MR_ALBUM;
	any = h.n > 0;
	fprintf(stderr, "mp4: took over %d track%s, %s, at %d\n", h.n, h.n == 1 ? "" : "s",
	        h.state == MH_PLAYING ? "playing" : h.state == MH_PAUSED ? "paused" : "stopped",
	        h.n ? h.pos + 1 : 0);
	mp4hand_free(&h);
	return any;
}

/* The cover on the Now Playing screen, kept between visits so that opening it
 * is not a decode: the album it is for and its texture. `done` means this is
 * what the album will show - its art, or the card made for one with none - and
 * nothing more needs asking. */
np_cover_state g_np = { .album = -1, .rgb = CFG_NO_COLOR };

/* The menus' panel pass (panel_pass_begin): a screen, kept rather than made
 * per frame. */
static SDL_Texture *g_panel_tex;

/* Game cards keep their theme tags; home art is theme-independent. */
bool tex_stale(SDL_Texture *t)
{
	return t && !cardgen_kept(ui_card_theme(t), theme_index());
}
bool g_loads_held;

/* Explicit home inputs. The renderer never changes the library or owns game art. */
void home_items(const app *a, home_item *items)
{
	for (int i = 0; i < a->sys.count; i++)
		items[i] = (home_item){ a->sys.systems[i].tag, a->sys.systems[i].name, a->sys.systems[i].accent };
}
void prime_home(app *a)
{
	if (a->screen != SCREEN_SYSTEMS || g_loads_held) return;
	home_item items[CFG_MAX_SYSTEMS];
	home_items(a, items);
	home_prepare(&a->home, a->r, P_ROOT, items, a->sys.count,
	             a->sys_cursor, plat_now_ms());
}

/* Where a game's autosave preview lives: the frame the player was looking at
 * when they stopped. The directory is named after the ROM's folder under
 * Roms/.
 *
 * The directory and the slot names are ZyperOS's own. Both were briefly
 * borrowed from the emulator ZyperOS replaced, and the borrowing cost more than
 * it saved: a name that describes another project invites a reader to treat
 * the contents as dead, and on 2026-08-28 the live autosave tree was deleted
 * by someone auditing the card for exactly that reason. Nothing was lost only
 * because states were being cleared for testing the same hour.
 *
 * Renamed with no migration path, deliberately - nothing has shipped, so there
 * are no cards in the world to be kind to. */
/* Six manual save slots and one autosave.
 *
 * Six because it is enough and it fits the carousel's dot rail without
 * crowding, not because any particular number is correct. An earlier eight was
 * copied from another launcher and had no reasoning of its own behind it.
 *
 * The resume slot is named rather than numbered on disk: `<game>.auto.state`
 * explains itself, where a number needs a reader to already know which slot
 * means auto. Its value is only an internal sentinel and must not collide with
 * 1..GM_SLOTS, so it is derived rather than picked. Both live in game_menu.h
 * now, with the carousel mapping beside them, so a check can reach all three.
 */

static const char *slot_name(int slot)
{
	static char buf[8];
	if (slot == SLOT_AUTO) return "auto";
	if (slot == SLOT_UNDO) return "undo";
	snprintf(buf, sizeof buf, "%d", slot);
	return buf;
}

/* The name a game's states and previews go by: its file's, which for a disc
 * game in a folder is the disc's ("Game (Disc 1).chd"). On a hooked shelf the
 * folder is the game and the file is whatever it held first - readme.txt for
 * two different ScummVM games - so the folder's name. And so for a PSN game:
 * a folder holding EBOOT.PBP, the same name in every such game's folder (a
 * PSP game's, a PlayStation classic's), whose states would all be one. */
static const char *persist_base(app *a, int s, const game_entry *g)
{
	const char *base = strrchr(g->file, '/');

	if (base && (lib_shelf_hooked(a->sys.systems[s].exts) || !strcasecmp(base + 1, "EBOOT.PBP")))
		return g->name;
	return base ? base + 1 : g->file;
}

/* The frame a game's card shows once it has been played (src/cardart.h):
 * the one Diatom keeps for it, <rom>.card.bmp, written once a minute into
 * its first play and never again (diatom/src/main.c, card_frame), beside
 * its other pictures */
void card_frame_path(app *a, int s, const game_entry *g, char *out, size_t n)
{
	snprintf(out, n, "%s/.zyperos/%s/%s.card.bmp",
	         P_SHARED, a->sys.systems[s].folder, persist_base(a, s, g));
}

void preview_path(app *a, int s, const game_entry *g, char *out, size_t n)
{
	snprintf(out, n, "%s/.zyperos/%s/%s.%s.bmp",
	         P_SHARED, a->sys.systems[s].folder, persist_base(a, s, g),
	         slot_name(SLOT_AUTO));
}

/* Where Diatom writes the frame it hands over, which is NOT the Auto card's
 * picture.
 *
 * Diatom writes its preview twice: before PAUSED, so the launcher has a frame
 * to dim and draw the menu over, and again at exit beside the state. Pointing
 * both at <rom>.auto.bmp meant every menu open overwrote the Auto card with
 * the current moment, while the card's timestamp still came from the state -
 * so the card contradicted itself and the picture was the convincing half.
 * Reported 2026-09-07: died in Contra, opened the menu, and the Auto card
 * showed the death while loading it went back two levels.
 *
 * One scratch file for the whole device rather than one per game: nothing
 * reads it after the launch that wrote it, and a per-game copy would leave
 * litter beside every save. */
void pause_preview_path(char *out, size_t n)
{
	snprintf(out, n, "%s/pause.bmp", P_USERDATA);
}

/* The Diatom transport takes explicit paths rather than a slot number, so the
 * state lives beside the preview it belongs to, named the same way.
 *
 * The autosave is named `auto`, not a number. Slots 1..GM_SLOTS are the
 * player's own,
 * reachable from the in-game menu, and are genuinely numbered; the resume slot
 * is not one of them and calling it 9 only meant something to someone who knew
 * which emulator picked that number. Each slot is a state and a preview named
 * alike, so a slot that has a picture has a game behind it. */
void slot_state_path(app *a, int s, const game_entry *g, int slot,
                     char *out, size_t n)
{
	snprintf(out, n, "%s/.zyperos/%s/%s.%s.state",
	         P_SHARED, a->sys.systems[s].folder, persist_base(a, s, g),
	         slot_name(slot));
}

void slot_preview_path(app *a, int s, const game_entry *g, int slot,
                       char *out, size_t n)
{
	snprintf(out, n, "%s/.zyperos/%s/%s.%s.bmp",
	         P_SHARED, a->sys.systems[s].folder, persist_base(a, s, g),
	         slot_name(slot));
}

void state_path(app *a, int s, const game_entry *g, char *out, size_t n)
{
	slot_state_path(a, s, g, SLOT_AUTO, out, n);
}

/* The hotkeys' copy of where this game's slots are (g_quick), at its launch:
 * slot_state_path's, less the slot */
void quick_bind(app *a, int s, const game_entry *g)
{
	const system_cfg *sc = &a->sys.systems[s];

	memset(&g_quick, 0, sizeof g_quick);
	g_quick.sys = *sc;
	snprintf(g_quick.tag, sizeof g_quick.tag, "%s", sc->tag);
	snprintf(g_quick.name, sizeof g_quick.name, "%s", sc->name);
	snprintf(g_quick.file, sizeof g_quick.file, "%s", g->file);
	snprintf(g_quick.stem, sizeof g_quick.stem, "%s/.zyperos/%s/%s",
	         P_SHARED, sc->folder, persist_base(a, s, g));
	g_quick.states = ctl_has_states(sc->tag);
}

/* The launcher owns the paths, so the launcher makes the directories - the
 * emulator writes where it is told and fails where it cannot. A missing mkdir
 * here once cost every preview on the card, silently, because writing to a
 * path in a directory that does not exist fails per-write and looks like
 * nothing happening. */
/* Where the two databases live, and the directories they need. Both callers
 * are in main(): --dump, which runs before video, and the boot path. Having
 * one of them create the directory and the other not is how --dump failed on a
 * fresh tree the first time it was tried. */
static bool db_paths_ready(char *dev, size_t ndev, char *lib, size_t nlib,
                           char *env, size_t nenv)
{
	char d[CFG_STR * 2];

	mkdir(P_USERDATA, 0755);
	mkdir(P_SHARED, 0755);
	snprintf(d, sizeof d, "%s/.zyperos", P_SHARED);
	mkdir(d, 0755);

	snprintf(dev, ndev, "%s/zyperos.db", P_USERDATA);
	snprintf(lib, nlib, "%s/.zyperos/library.db", P_SHARED);
	if (env) snprintf(env, nenv, "%s/boot.env", P_USERDATA);
	return true;
}

void persist_dir_ensure(app *a, int s)
{
	char d[LIB_PATH * 2];
	snprintf(d, sizeof d, "%s/.zyperos", P_SHARED);
	mkdir(d, 0755);
	snprintf(d, sizeof d, "%s/.zyperos/%s", P_SHARED, a->sys.systems[s].folder);
	mkdir(d, 0755);
}

/* Card art, in order of preference: the box art the user put in .media/, then
 * the autosave preview -- the last frame they saw, which for a game in
 * progress is a better card than any box -- then a generated slab. */
/* The system an entry on a shelf belongs to. Real shelves answer with
 * themselves; Favorites answers per entry. */
int shelf_owner(app *a, int sysidx, int item)
{
	sysview *v = &a->view[sysidx];
	if (v->owner && item >= 0 && item < v->list.count) return v->owner[item];
	return sysidx;
}

/* The view whose SYSTEM settings the game under the cursor belongs to.
 *
 * Favorites is a shelf of other systems' games, so the two kinds of setting
 * have to be told apart: a property of the SYSTEM - the core, the ROM folder,
 * the BIOS, the display mode - resolves through the game's owner, while a
 * property of the SHELF - its sort order, its cursor, its own card - belongs
 * to the shelf you are standing on.
 *
 * launch() had that right for three of the four and wrong for the display
 * mode, which it read off the shelf: a Genesis game set to integer on its own
 * shelf played stretched when it was started from Favorites, because the mode
 * it used was the one filed under FAV. Nothing showed it on this card, where
 * every system reads stretch and all the answers agree. Named as a function
 * rather than repeated at each site so the next per-system setting has one
 * obvious thing to call. */
sysview *owner_view(app *a)
{
	sysview *v = &a->view[a->sys_cursor];

	return &a->view[shelf_owner(a, a->sys_cursor, v->cursor)];
}

/* Where a game's box art lives.
 *
 * One definition because four places need the same file - the card, the info
 * screen, the replace action, and the return from a game - and the last of
 * those has to test exactly the file the card loads. Four hand-written copies
 * of the format string only have to drift once for it to ask about one file
 * and draw another. The File Transfer page's rows ask the same one
 * (game_media_own, src/esmedia.h). */
void box_art_path(const char *folder, const char *name, char *out, size_t n)
{
	game_media_own(P_ROMS, folder, name, ES_COVER, out, n);
}

/* Whether it is there. An empty file is not art - the rule the info screen
 * already used, now the rule for everyone. */
bool has_box_art(const char *folder, const char *name)
{
	char p[LIB_PATH * 3];
	struct stat st;

	box_art_path(folder, name, p, sizeof p);
	return stat(p, &st) == 0 && st.st_size > 0;
}

/* EmulationStation's picture for a game with no box art of its own
 * (src/esmedia.h), as the cover worker's second file: the key is the game's
 * folder, file and name, a tab between, found on the worker (TEXLOAD_LATE),
 * where the folder's gamelist.xml may be read the first time */
static void es_cover_key(const char *folder, const game_entry *g, char *out, size_t n)
{
	snprintf(out, n, "%s%s\t%s\t%s", TEXLOAD_LATE, folder, g->file, g->name);
}

/* The folder a computer's file names (--meta, --titles: systems.cfg's, as
 * tools/scrape-art.py writes them) as the card's shelf reads it: an
 * EmulationStation folder where that shelf reads one (DESIGN.md 11.40).
 * The card is read for it once. */
static const char *card_folder(const char *folder)
{
	static systems_cfg cfg;
	static bool read;
	static char out[CFG_STR];

	if (!read) {
		char path[CFG_STR * 2];

		read = true;
		snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
		if (cfg_load_systems(path, &cfg)) shelf_folders_from_card(&cfg);
	}
	for (int i = 0; i < cfg.count; i++)
		if (!strcmp(cfg.systems[i].home, folder)) {
			snprintf(out, sizeof out, "%s", cfg.systems[i].folder);
			return out;
		}
	return folder;
}

/* Box Art's question (art_es_media): `dir` is Roms/<folder> */
static bool es_art_has(const char *dir, const char *file, const char *stem, es_kind kind)
{
	char roms[LIB_PATH * 2], p[ES_PATH];
	const char *slash = strrchr(dir, '/');

	if (!slash) return false;
	snprintf(roms, sizeof roms, "%.*s", (int)(slash - dir), dir);
	return es_media(roms, slash + 1, file, stem, kind, true, p, sizeof p);
}

bool es_art_cover(const char *dir, const char *file, const char *stem)
{
	return es_art_has(dir, file, stem, ES_COVER);
}

bool es_art_video(const char *dir, const char *file, const char *stem)
{
	return es_art_has(dir, file, stem, ES_VIDEO);
}

static bool es_cover_late(const char *key, char *path, size_t n)
{
	char k[LIB_PATH * 3], *file, *name;

	snprintf(k, sizeof k, "%s", key);
	if (!(file = strchr(k, '\t')) || !(name = strchr(file + 1, '\t'))) return false;
	*file++ = '\0';
	*name++ = '\0';
	/* and a PICO-8 game's own cart, after the others (src/cartlabel.h) */
	return es_media(P_ROMS, k, file, name, ES_COVER, true, path, n) ||
	       game_media_cart(P_ROMS, k, file, path, n);
}

/* Whether game `i`'s slot on a shelf wants a card: it has none, or it holds a
 * generated card made for the theme that was, which stays on the shelf until
 * its replacement comes (theme_changed). Box art is no theme's and never is
 * stale. */
static bool game_card_wanted(const sysview *v, int i)
{
	return !v->tex[i] || tex_stale(v->tex[i]);
}

/* What the grid keeps of cover i of shelf s beside its texture (src/gamegrid.h):
 * from its picture `surf` before that is freed, or that it is the generated
 * card. Not for MP4's shelf, which has no grid. */
void grid_note(app *a, int s, int i, SDL_Surface *surf, bool generated)
{
	sysview *v = &a->view[s];

	/* MP4's shelf too, since it has the Grid (DESIGN.md 11.67) */
	if (i < 0 || i >= v->list.count || !v->tex[i]) return;
	gg_note(&v->gart, a->r, v->list.count, i, v->tex[i], gg_key(v->list.items[i].file),
	        surf, generated);
}

/* A cover read on the frame (no worker: a shot): its texture, where its art
 * stops, and what the grid keeps of it */
static SDL_Texture *load_cover(app *a, int s, int i, const char *path)
{
	sysview *v = &a->view[s];
	SDL_Surface *conv = load_surface(path);
	SDL_Texture *t;

	v->cb[i] = 1.0f;
	if (!conv) return NULL;
	v->cb[i] = content_bottom(conv);
	t = SDL_CreateTextureFromSurface(a->r, conv);
	cart_label_sharp(t, conv);
	if (t) {
		SDL_QueryTexture(t, NULL, NULL, &v->tw[i], &v->th[i]);
		v->tex[i] = t;
		grid_note(a, s, i, conv, false);
	}
	SDL_FreeSurface(conv);
	return t;
}

/* A game's card's system and frames of play (src/cardart.h): system `o`'s
 * tag and still art, and the frames to try, Diatom's card frame first and
 * the Auto slot's picture until there is one. Paths only: the worker that
 * paints the card opens them, so nothing here touches the card. */
void game_card_order(app *a, int o, const game_entry *g, ui_card_order *card)
{
	const system_cfg *sc = &a->sys.systems[o];
	char art[UI_CARD_PATH], frame[UI_CARD_FRAMES][UI_CARD_PATH];
	const char *frames[UI_CARD_FRAMES] = { frame[0], frame[1] };
	const char *key = home_art_key(sc->tag);

	art[0] = '\0';
	if (key && key[0]) snprintf(art, sizeof art, "%s/system-icons/%s-still.png", P_ROOT, key);
	card_frame_path(a, o, g, frame[0], sizeof frame[0]);
	preview_path(a, o, g, frame[1], sizeof frame[1]);
	ui_card_order_game(card, sc->tag, art, frames);
}

SDL_Texture *game_get_tex(void *ctx, int i, int *w, int *h, float *cb)
{
	app *a = ctx;
	int s = a->sys_cursor;
	sysview *v = &a->view[s];
	/* The game's own system, not the shelf's: on Favorites those differ, and
	 * every one of these three lookups would otherwise go to the wrong
	 * folder, the wrong state directory and the wrong accent. */
	int o = shelf_owner(a, s, i);

	char pm[LIB_PATH * 2], pmc[LIB_PATH * 3];

	if (is_mp4(&a->sys.systems[s]) && game_card_wanted(v, i)) {
		mp4_card_want(a, s, i);
	} else if (game_card_wanted(v, i) && v->list.items[i].folder) {
		/* A folder on the shelf (src/shelfdir.h): its own card, no art, in
		 * its system's color or Folder Color's (look_folder_rgb) */
		ui_card_order card;
		const unsigned frgb = look_folder_rgb(a->sys.systems[o].accent);

		ui_card_order_folder(&card, v->list.items[i].title, frgb, v->list.items[i].folder);
		if (!texload_want_or_draw(s, i, NULL, NULL, &card) && !g_loads_held) {
			if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = ui_make_folder_card(a->r, v->list.items[i].title, frgb,
			                                v->list.items[i].folder, &v->tw[i], &v->th[i]);
			v->cb[i] = 1.0f;
		}
	} else if (game_card_wanted(v, i) &&
	           own_cover_path(a, o, &v->list.items[i], theme_index(), pm, sizeof pm)) {
		/* An entry's own cover (PortMaster's, Splore's), in the theme that
		 * is on, made a card of that theme as a generated one is, so a
		 * change of theme asks for the other (src/shelfbuild.h) */
		if (!texload_want_art(s, i, pm, NULL, theme_index()) && !g_loads_held) {
			SDL_Surface *sf = IMG_Load(pm);

			if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = NULL;
			if (sf) {
				v->cb[i] = content_bottom(sf);
				v->tex[i] = ui_card_upload(a->r, sf, theme_index());
				if (v->tex[i]) { v->tw[i] = sf->w; v->th[i] = sf->h; }
				grid_note(a, s, i, sf, false);
				SDL_FreeSurface(sf);
			}
		}
	} else if (game_card_wanted(v, i) &&
	           screen_card_paths(a, s, o, &v->list.items[i], pm, sizeof pm, pmc, sizeof pmc)) {
		/* Recents' Last Screen (DESIGN.md 11.48), and a shot (11.68):
		 * made on a worker, art of no theme, so a change of theme keeps
		 * it; a game with no cover has its generated card small in the
		 * corner, as its shelf shows it, unless the card has no cover */
		ui_card_order card;
		const ui_card_order *co = shots_shelf(a, s) && !SHOTS_CARD_COVER ? NULL : &card;

		ui_card_order_set(&card, v->list.items[i].title, a->sys.systems[o].accent, false);
		game_card_order(a, o, &v->list.items[i], &card);
		if (!texload_want_screen(s, i, pm, pmc, co) && !g_loads_held) {
			SDL_Surface *sf = screen_card_make(pm, pmc, co, false);

			if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = NULL;
			if (sf) {
				v->cb[i] = 1.0f;
				if ((v->tex[i] = SDL_CreateTextureFromSurface(a->r, sf)))
					SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
				grid_note(a, s, i, sf, false);
				SDL_FreeSurface(sf);
			}
		}
	} else if (!is_mp4(&a->sys.systems[s]) && game_card_wanted(v, i)) {
		char art[LIB_PATH * 3];
		ui_card_order card;

		char es[LIB_PATH * 3];

		box_art_path(a->sys.systems[o].folder, v->list.items[i].name,
		             art, sizeof art);
		/* and EmulationStation's, for a game with none of its own */
		es_cover_key(a->sys.systems[o].folder, &v->list.items[i], es, sizeof es);
		/* Hand it to the worker and draw what the slot holds this frame:
		 * nothing, or the card of the theme that was until its replacement
		 * lands (theme_changed). Asking is cheap and ignores a card already
		 * in hand, so callers ask every frame. With the card to draw if
		 * there is no box art, so that is the worker's to draw as well. */
		ui_card_order_set(&card, v->list.items[i].title,
		                  a->sys.systems[o].accent, false);
		game_card_order(a, o, &v->list.items[i], &card);
		bool asked = texload_want_or_draw(s, i, art, es, &card);

		if (!asked && !g_loads_held) {
			/* No worker. Exactly what this did before, on the frame, over
			 * the card of the theme that was if there is one. */
			if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = load_cover(a, s, i, art);
			if (!v->tex[i] && es_cover_late(es + 1, es, sizeof es))
				v->tex[i] = load_cover(a, s, i, es);
			if (!v->tex[i]) {
				SDL_Surface *ground;

				v->tex[i] = ui_make_card_for(a->r, &card, &v->tw[i], &v->th[i], &ground);
				grid_note(a, s, i, ground, true);
				if (ground) SDL_FreeSurface(ground);
			}
		}
	}
	if (!v->tex[i]) {
		/* Still coming. cf_draw skips a card with no texture, so the slot is
		 * simply empty for a few frames instead of the shelf stopping. */
		*w = *h = 0;
		if (cb) *cb = 1.0f;
		return NULL;
	}
	*w = v->tw[i];
	*h = v->th[i];
	if (cb) *cb = v->cb[i];
	return v->tex[i];
}


/* One finished picture into its slot: texload_deliver's `put`. */
void shelf_put(void *ctx, texload_got *g)
{
	app *a = ctx;
	SDL_Surface *surf = g->surf;
	int s = g->sys, i = g->idx;
	sysview *v;

	/* The Screenshots viewer's picture (src/shots_screen.h), asked apart */
	if (s == SHOTS_VIEW_SYS) {
		shots_view_put(a, i, surf, g->card);
		return;
	}
	/* The focused card's sharp one (src/focusfx.h), asked for apart */
	if (s <= SHELF_SHARP_SYS(0)) {
		shelf_focus_sharp_put(a, SHELF_SHARP_SYS(0) - s, i, surf, g->card, g->theme);
		return;
	}

	if (home_accept(&a->home, a->r, s, i, &surf)) {
		if (surf) ui_card_free(surf);
		return;
	}
	if (s < 0 || s >= a->sys.count) { if (surf) ui_card_free(surf); return; }
	v = &a->view[s];
	/* The shelf may have moved on, or something else may have filled this
	 * slot while the decode was in the air. A slot holding a generated
	 * card of the theme that was is not filled: it is waiting for this. */
	if (i < 0 || i >= v->list.count || !game_card_wanted(v, i)) {
		if (surf) ui_card_free(surf);
		return;
	}
	/* The card of the theme that was, drawn until now, goes as this one
	 * comes in. */
	if (v->tex[i]) {
		SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = NULL;
	}
	if (surf && g->card) {
		/* The generated card, drawn on the worker: only its texture is
		 * made here, marked with its theme as one made here would be. */
		SDL_Surface *ground = ui_card_ground_take(surf);

		v->tex[i] = ui_card_upload(a->r, surf, g->theme);
		if (v->tex[i]) { v->tw[i] = surf->w; v->th[i] = surf->h; }
		v->cb[i] = 1.0f;
		grid_note(a, s, i, ground, true);
		if (ground) SDL_FreeSurface(ground);
		SDL_FreeSurface(surf);
		surf = NULL;
	}
	/* A cover is squared and given a card's corners, the way Now
	 * Playing draws it, so one album looks the same in both places. */
	if (surf && is_mp4(&a->sys.systems[s])) cover_shape(&surf);
	if (surf) {
		v->cb[i] = content_bottom(surf);
		/* Art drawn for one theme (PortMaster's cover) is kept as a card
		 * of that theme is, and gives way to the other's at a change */
		v->tex[i] = g->theme >= 0 ? ui_card_upload(a->r, surf, g->theme)
		                          : SDL_CreateTextureFromSurface(a->r, surf);
		cart_label_sharp(v->tex[i], surf);      /* a PICO-8 cart's label */
		if (v->tex[i])
			SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
		grid_note(a, s, i, surf, false);
		SDL_FreeSurface(surf);
	}
	if (!v->tex[i]) {
		/* No file could be read and no card came with it - the worker could
		 * not have the faces: the generated card, made here. */
		int o = shelf_owner(a, s, i);
		ui_card_order card;
		SDL_Surface *ground = NULL;

		if (is_mp4(&a->sys.systems[s])) {
			mp4_album_card_order(a, s, i, a->sys.systems[o].accent, &card);
		} else {
			ui_card_order_set(&card, v->list.items[i].title, a->sys.systems[o].accent, false);
			game_card_order(a, o, &v->list.items[i], &card);
		}
		v->tex[i] = v->list.items[i].folder
		          ? ui_make_folder_card(a->r, v->list.items[i].title,
		                                look_folder_rgb(a->sys.systems[o].accent),
		                                v->list.items[i].folder, &v->tw[i], &v->th[i])
		          : ui_make_card_for(a->r, &card, &v->tw[i], &v->th[i], &ground);
		v->cb[i] = 1.0f;
		grid_note(a, s, i, ground, !v->list.items[i].folder);
		if (ground) SDL_FreeSurface(ground);
	}
}

/* Finished art into textures, as much of it as fits in this frame's share
 * (texload_deliver): a landing or a fast scroll finishes a dozen at once,
 * and one frame making every texture was the frame the shelf hitched on. The
 * rest come in the next frames, which the loops draw while any is waiting
 * (texload_ready). */
void texload_drain(app *a)
{
	texload_deliver(theme_index(), shelf_put, a);
}

void evict_far(sysview *v, int keep)
{
	for (int i = 0; i < v->list.count; i++) {
		int d = abs(i - v->cursor);
		if (v->list.count >= CF_WINDOW)
			d = d > v->list.count / 2 ? v->list.count - d : d;
		if (v->tex[i] && d > keep) {
			SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = NULL;
			gg_drop(&v->gart, i);
		}
	}
}

/* The grid's eviction (DESIGN.md 11.47), by rows rather than by distance
 * from the cursor: what gg_row_kept keeps round the window the grid is
 * heading for, and every row the last frame drew, so a glide never loses a
 * cover it still shows and the rows asked ahead stay in hand. `in_sight`:
 * the rows in sight alone, for a game about to start, which gets the memory
 * of the rows ahead; they are asked for again once the grid is drawn. */
void evict_grid(sysview *v, int cols, bool in_sight)
{
	int nrows, top, drawn;

	if (cols <= 0 || !v->tex) return;
	nrows = (v->list.count + cols - 1) / cols;
	top = (int)lroundf(v->gm.top_to);
	drawn = (int)floorf(v->gm.top_drawn);
	for (int i = 0; i < v->list.count; i++) {
		int row = i / cols;
		bool keep = in_sight ? row >= top && row < top + GG_ROWS
		                     : gg_row_kept(row, top, nrows) || (row >= drawn && row <= drawn + GG_ROWS);

		if (!v->tex[i] || keep)
			continue;
		SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = NULL;
		gg_drop(&v->gart, i);
	}
}

#define PRIME_HALF_MAX (CF_HALF_WINDOW + 2)

/* Ask for the cards on shelf `s` around `center`, out to `half` either side.
 *
 * FARTHEST FIRST. The workers take the newest request first (texload.c), so
 * they decode in the reverse of the order asked, and the center has to be asked
 * for last to be decoded first. Both callers used to ask for it first or in the
 * middle, on the reasoning that the order asked is the order decoded - true of
 * the queue they were written against, and backwards since it went newest-first.
 * Measured 2026-09-14, cold after a reboot: entering a system put the focused
 * card up 70-160ms after A, only 15-35ms ahead of the rest of its row, when it
 * should have been the first to arrive.
 *
 * Each card is asked for once. On a shelf shorter than the window, wrapping
 * offers the same card from both sides, and the second ask would be refused as
 * already queued - leaving the card wherever the first ask put it.
 *
 * game_get_tex reads a->sys_cursor rather than taking a shelf, so it is swapped
 * around the calls. */
static void prime_around(app *a, int s, int center, int half)
{
	sysview *v = &a->view[s];
	int n = v->list.count, idx[2 * PRIME_HALF_MAX + 1], m = 0;
	int k, j, save = a->sys_cursor;

	if (n <= 0) return;
	if (half > PRIME_HALF_MAX) half = PRIME_HALF_MAX;
	for (k = 0; k <= half; k++) {
		int side;
		for (side = 1; side >= (k ? -1 : 1); side -= 2) {
			int i = center + k * side;
			if (n >= 2) {
				i %= n;
				if (i < 0) i += n;
			} else if (i != 0) {
				continue;
			}
			for (j = 0; j < m && idx[j] != i; j++) ;
			if (j == m) idx[m++] = i;
		}
	}
	a->sys_cursor = s;
	while (m-- > 0) {
		int w, h;
		if (game_card_wanted(v, idx[m])) game_get_tex(a, idx[m], &w, &h, NULL);
	}
	a->sys_cursor = save;
}

/* The visible row and two more either side, so the first scroll finds its
 * neighbors already there instead of sliding in blank. */
void prime_window(app *a, int s)
{
	prime_around(a, s, a->view[s].cursor, PRIME_HALF_MAX);
}

/* Ask for the whole window around `center`.
 *
 * This used to take a budget and spend one card per frame, because asking
 * meant decoding and a card was 7-27ms against a 16.7ms frame. Since
 * texload.c, asking only queues: game_get_tex hands the worker two paths and
 * returns, a card already in hand is ignored, so the whole window costs about
 * nothing and dribbling it out a frame at a time only made it land later. */
void prime_toward(app *a, int s, int center)
{
	prime_around(a, s, center, CF_HALF_WINDOW);
}


void free_view_textures(sysview *v)
{
	int k;

	texload_bump();
	for (k = 0; k < v->list.count; k++) {
		if (v->tex && v->tex[k]) {
			SDL_DestroyTexture(v->tex[k]);
			v->tex[k] = NULL;
		}
		if (v->tw) v->tw[k] = 0;
		if (v->th) v->th[k] = 0;
		if (v->cb) v->cb[k] = 0.0f;
	}
}

/* ---- folders on a shelf (src/shelfdir.h) ---- */

/* The list view's pixel folder (folder_icon, with draw_games) */
SDL_Texture *g_folder_icon;

void shelf_open_dir(app *a, int s, const char *dir, const char *select)
{
	sysview *v = &a->view[s];
	int i;

	/* Collections' folders are its collections (src/collect.h) */
	if (s == shelf_coll_at(a)) {
		texload_bump();
		if (!shelf_coll_show(a, s, dir)) return;
	} else {
		if (!v->all) return;
		texload_bump();
		shelf_show_dir(v, dir);
	}
	sort_shelf(a, s);
	v->cursor = 0;
	for (i = 0; select && i < v->list.count; i++)
		if (!strcmp(v->list.items[i].file, select)) { v->cursor = i; break; }
	v->lm.set = false;                   /* the list finds its window again */
	v->gm.set = false;                   /* ...and the grid its window and shape */
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
}

void shelf_reveal(app *a, int s, const char *file)
{
	sysview *v = &a->view[s];
	char dir[LIB_PATH];
	int k;

	if (v->all && (k = lib_find_entry(v->all, NULL, 0, file)) >= 0) {
		sd_dir_of(&v->all->items[k], dir, sizeof dir);
		if (strcmp(dir, v->dir)) { shelf_open_dir(a, s, dir, file); return; }
	}
	if ((k = lib_find_entry(&v->list, NULL, 0, file)) >= 0) v->cursor = k;
}

/* The folder on the shelf under the cursor, opened (A), or the shelf's own
 * folder left for the one it is in (B), its cursor on the folder it left */
void shelf_into(app *a, sysview *v)
{
	char dir[LIB_PATH];

	snprintf(dir, sizeof dir, "%s", v->list.items[v->cursor].file);
	fprintf(stderr, "shelf: into %s/%s\n", a->sys.systems[a->sys_cursor].folder, dir);
	shelf_open_dir(a, a->sys_cursor, dir, NULL);
}

void shelf_up(app *a, sysview *v)
{
	char was[LIB_PATH], up[LIB_PATH];

	snprintf(was, sizeof was, "%s", v->dir);
	sd_parent(was, up, sizeof up);
	shelf_open_dir(a, a->sys_cursor, up, was);
}

/* MP4's shelf onto another kind: all of them, music, books or videos, from
 * the first card. The textures go first, while the count is still the old
 * kind's, since every card is about to be a different album. */
static const char *const MP4_SHOW[ML_KINDS + 1] = { "music", "books", "videos", "all" };

void mp4_show(sysview *v, ml_kind kind)
{
	db_set_str(db_dev(), "mp4.show", MP4_SHOW[kind >= 0 && kind <= ML_ALL ? kind : ML_ALL]);
	mp4_show_only(v, kind);
}

void mp4_show_only(sysview *v, ml_kind kind)
{
	g_mp4_kind = kind;
	free_view_textures(v);
	mp4_order_view(v);
	v->cursor = 0;
	cf_reset(&v->cf, 0);
}

void free_all_textures(app *a)
{
	/* Anything in flight was asked for against the shelf as it was. */
	texload_bump();
	if (g_panel_tex) { SDL_DestroyTexture(g_panel_tex); g_panel_tex = NULL; }
	zyper_shadows_free();
	home_free(&a->home);
	/* Now Playing's cover is a texture of this renderer too, and the fallback
	 * that runs a game (launch) destroys the renderer after this: left,
	 * g_np.tex would dangle, and np_cover, which sees it made, would draw it.
	 * Forgotten, np_cover makes it again on the frame that next draws it. */
	np_forget();
	if (g_folder_icon) { SDL_DestroyTexture(g_folder_icon); g_folder_icon = NULL; }
	for (int i = 0; i < a->sys.count; i++) {
		for (int k = 0; k < a->view[i].list.count; k++)
			if (a->view[i].tex[k]) {
				SDL_DestroyTexture(a->view[i].tex[k]);
				a->view[i].tex[k] = NULL;
			}
	}
}


/* Whether the radio should come up at boot.
 *
 * Per-device rather than per-card, on the same split as brightness: it is what
 * THIS handheld was last doing. The shipped default seeds it once, so there is
 * no fallback chain left for launch.sh to walk.
 *
 * Written the moment it changes rather than at shutdown, because a handheld
 * is switched off by holding a button or by running the battery flat, and
 * neither of those is a chance to save anything. */
void wifi_pref_save(bool on)
{
	db_set_int(db_dev(), "wifi", on ? 1 : 0);
	/* launch.sh reads this one before the launcher exists, so the export has
	 * to follow the write rather than wait for a tidy shutdown. */
	db_write_boot_env();
}


/* Diatom watches this file, and it is rewritten per launch: it is the set
 * minus what has already been earned, which only this side knows. Runtime
 * scratch, so per-device rather than shared. */
void chv_active_path(char *out, size_t n)
{
	snprintf(out, n, "%s/cheevos-active.set", P_USERDATA);
}

/* Auto Off: how long without input before the device powers itself down.
 *
 * Seconds, 0 for off. Eric's ladder, and it is deliberately aggressive at the
 * short end - 30s will fire while you read a dialogue box. That is a sound
 * trade only because resume-into-game exists: powering off costs about two
 * seconds and puts you back in the same game with the menu up. Without that
 * it would be hostile. */
const int AUTO_OFF[] = { 0, 30, 60, 120, 300, 600 };

static int auto_off_load(void)
{
	int v = db_get_int(db_dev(), "autooff", -1);
	return v >= 0 ? v : 120;                  /* the default: two minutes */
}

void auto_off_save(int seconds)
{
	db_set_int(db_dev(), "autooff", seconds);
}

/* MENU > Screensaver (src/saver.h): the seconds with nothing pressed on the
 * shelf before the video snaps play, 0 for never. Stepped round on Auto Off's
 * ladder (saver_step); a minute to start with. */
int g_saver_s = SAVER_DEFAULT_S;

static int saver_load(void)
{
	int v = db_get_int(db_dev(), "saver", -1);

	return v >= 0 ? v : SAVER_DEFAULT_S;
}

void saver_save(int seconds)
{
	db_set_int(db_dev(), "saver", seconds);
}

/* MENU > Charge Limit (DESIGN.md 11.26; my picks in the charge limit
 * lab, 2026-11-06): the charge the battery stops at to keep it healthy for
 * longer, 0 for Off, a full charge, as a new card has. On a charger that can
 * stop short (plat_charge_limit_supported): plastron's rk817 driver turns it
 * off at the limit and on again 2 points under, so 80% holds between 78 and
 * 80%. The kernel forgets it at a power-off, so it is set at every start. */
const int CHARGE_LIMITS[] = { 0, 80, 85, 90 };
/* "Hold A for a full charge" on the charging screen: this charge goes to
 * 100%, and the limit is back when the cable comes out (charge_full_end).
 * In /tmp, so a power-off ends it too. */
#define CHG_FULL_ONCE "/tmp/zyperos_charge_full"
int g_charge_limit;

static int charge_limit_load(void)
{
	int v = db_get_int(db_dev(), "chargelimit", 0), k;

	for (k = 0; k < CHARGE_LIMITS_COUNT; k++)
		if (CHARGE_LIMITS[k] == v) return v;
	return 0;
}

/* The limit in force: 0 for none, Off or a full charge asked for */
int charge_limit_now(void)
{
	if (!g_charge_limit || access(CHG_FULL_ONCE, F_OK) == 0) return 0;
	return g_charge_limit;
}

void charge_limit_apply(void)
{
	int l = charge_limit_now();

	if (plat_charge_limit_supported()) plat_charge_limit_set(l ? l : 100);
}

/* A full charge, this time: the limit off until the cable comes out */
void charge_full_once(void)
{
	FILE *f = fopen(CHG_FULL_ONCE, "w");

	if (f) fclose(f);
	charge_limit_apply();
	fprintf(stderr, "charging: a full charge, this time\n");
}

bool g_full_shot;          /* --charging N tofull: asked for, in a shot */
bool charge_full_active(void) { return g_full_shot || access(CHG_FULL_ONCE, F_OK) == 0; }
int  charge_limit_saved(void) { return g_charge_limit; }

/* The cable out: a full charge asked for is over, and the limit back */
void charge_full_end(void)
{
	if (access(CHG_FULL_ONCE, F_OK) != 0) return;
	remove(CHG_FULL_ONCE);
	charge_limit_apply();
	fprintf(stderr, "charging: the full charge is over, the limit back\n");
}

/* ---------- what the account already has, read while the game runs ------- */

/* The answer to "which of this game's achievements does the account hold",
 * asked at launch and read as soon as it lands - from the game tick during a
 * game, from the main loop on the shelf.
 *
 * It used to be read only when the game ended, waiting there until it had
 * landed. So a game's first session after signing in showed 0 of N in its own
 * menu while the account held 13, and a quit straight after a launch on a slow
 * network sat out the request between the game's wait loop and the shelf's.
 * Now the menu is right about half a second in, and quitting never waits.
 *
 * The unlocks owed still go out after the answer, not before it: settling what
 * the account already holds first is what stops them being sent as duplicates.
 * So a quit that beats the answer leaves the sending to whichever comes first
 * - the answer landing, or something else needing the one async slot, which
 * gives up on the answer and sends anyway. Duplicates that causes are answered
 * as already held, which settles them just the same. */
long g_sync_game;                   /* the game asked about, 0 none */
static char g_sync_set[LIB_PATH * 2];      /* its set, to check the answer against */

/* A send waiting for the answer. */
bool g_sync_flush;
char g_sync_rom[LIB_PATH * 2], g_sync_tag[16];
int  g_sync_cur;

void sync_begin(long game, const char *set_path)
{
	ra_sync_begin(game);
	g_sync_game = ra_sync_pending() ? game : 0;
	snprintf(g_sync_set, sizeof g_sync_set, "%s", set_path ? set_path : "");
}

static void sync_flush_now(void)
{
	if (!g_sync_flush) return;
	g_sync_flush = false;
	ra_flush_start(g_sync_rom, g_sync_tag, g_sync_cur);
}

/* The answer, merged if it is in. Returns how many achievements it added to
 * what this device knew - nonzero means the list Diatom is watching still
 * holds some the account already has. Cheap when nothing has landed: a
 * waitpid that does not wait. */
int sync_poll(void)
{
	int ids[CHV_MAX], n, rc, added = 0;

	if (!g_sync_game) return 0;
	rc = ra_sync_poll(ids, CHV_MAX, &n);
	if (rc == 0) return 0;

	if (rc > 0 && n > 0) {
		/* Against the set the question was about. During a game that is the
		 * one loaded; on the shelf the info screen loads whichever set it is
		 * showing, and nothing on the shelf depends on which one that is. */
		if (chv_game() != g_sync_game) chv_load(g_sync_set);
		if (chv_game() == g_sync_game) added = ra_merge_unlocks(ids, n);
	}
	g_sync_game = 0;
	sync_flush_now();
	return added;
}

/* Something else needs the one async slot now - a launch, the box art
 * scraper. The answer is dropped, and the next launch of that game asks
 * again. */
void sync_abandon(void)
{
	if (g_sync_game) {
		ra_sync_abandon();
		g_sync_game = 0;
		fprintf(stderr, "ra: dropped an account answer that had not landed\n");
	}
	sync_flush_now();
}

/* Where the launcher's own work happens during a game. Ten times a second,
 * from inside the wait loop, and it must stay cheap: that loop is the power
 * button's watchdog.
 *
 * Its one job is the first play of a game. The set is being found behind the
 * game rather than in front of it, and when it lands the player is told the
 * only way anyone can be told during a game - over Diatom's overlay. */
char g_pending_set[LIB_PATH * 2];
char g_pending_active[LIB_PATH * 2];
/* ...whose hash is still being made, on its own thread (src/rahashjob.h) */
bool g_pending_hashing;
/* Auto crossed an edge of the Night Hours while a game ran, and the file
 * was rewritten for Diatom's bar but nobody has told the shelf: theme() is
 * the new theme and the cards kept are the old one's. tick_theme takes it as
 * a change like its own clock's, so the first screen to ask after on_game_tick set it
 * tells theme_changed - the in-game menu and MP4 opened from it, during the
 * game, or the way back from it (launch) before its first frame - and
 * theme_changed puts it down. */
bool g_theme_late;
/* The watch list Diatom was last handed and when, so an account answer that
 * lands early can trim it. 0 when there is no list to trim. */
#define WATCH_RETRIM_MS 5000
unsigned g_watch_ms;
char     g_watch_active[LIB_PATH * 2];
/* The set path of the game actually running, so a fetch that lands during a
 * DIFFERENT game can be recognized and dropped. Written at every launch. */
char g_game_set[LIB_PATH * 2];
/* And whether the launcher has a hash for it at all (ra_hash_support), for
 * the in-game menu's Achievements row. Written beside it. */
ra_hash_kind g_game_no_hash;

/* Auto Off while a game runs. Diatom holds the clock, because it owns the pad
 * and this process is blocked in plat_resident_wait; the launcher's part is
 * telling it the number and keeping that number true as the charger comes and
 * goes. See on_charger, which lives beside battery_level. */
int  g_idle_secs;         /* the Auto Off setting, 0 off */
bool g_idle_charging;     /* what was true when SETIDLE was last sent */

/* "Getting hot" (src/heat.h, DESIGN.md 11.23): the chip's temperature, read
 * every HEAT_POLL_MS by every screen through idle_due and by a game's tick,
 * and said where it can be seen - Zyper's pop-up on the home and the shelves
 * (heat_watch), a notice over a running game (on_game_tick). The words are
 * short enough for the pop-up's slab without scrolling. */
heat_state g_heat;

void heat_tick(void)
{
	heat_poll(&g_heat, plat_now_ms(), plat_chip_temp);
}

/* Said: one line in the log with the reading, pop-up or notice */
void heat_said(const char *where)
{
	fprintf(stderr, "heat: %d.%d C, getting hot (%s)\n", g_heat.mc / 1000,
	        (g_heat.mc % 1000) / 100, where);
}

/* The pop-ups over a game, further down */
static void notice_pump(void);
static void heat_notice(void);

static void on_game_tick(void)
{
	char msg[96];

	/* Developer Mode's SSH, which a game does not end (see dev_ssh_tick) */
	dev_ssh_tick();
	/* Night Mode's hour behind the game: Diatom loads its table, and this
	 * keeps the launcher's right for when it has the display back, and the
	 * nights in the file Diatom reads (platform.h) */
	plat_night_tick();
	/* System Update behind the game: its looks, a cut download going on,
	 * and its news as a notice */
	tick_update_daily(true);
	/* px2-open's status and quit, and an open that ends this game first */
	if (g_ctl_app) ctl_poll(g_ctl_app, CTL_AT_GAME);
	int rc;

	/* MP4, bounded: read what has arrived, and send at most one PLAY when a
	 * track ends - which is what keeps an album going through a game.
	 *
	 * And the game goes quiet while it plays - Diatom's QUIET, followed
	 * here at ten a second, which is how pausing the music or the album
	 * ending brings the game's sound back within a tenth of a second. Only a
	 * change is sent. */
	mp4_poll();
	plat_resident_quiet(mp4c_playing());
	/* The next pop-up, once the one on screen has had its time */
	notice_pump();
	/* A computer on the cable that has not connected: said over the game
	 * too, until the cable comes out (DESIGN.md 11.36) */
	replug_game_tick();
	/* NES Player 2 (src/p2.h): the hotspot coming up and p2d after it, and
	 * a friend joining or leaving said over the game. Bounded. */
	p2_tick(plat_now_ms());
	/* The Game Boy's Link Cable (src/link.h): the beat to the other
	 * handheld, and a lost link said over the game. Bounded. */
	link_tick(plat_now_ms());
	/* The chip's temperature, every HEAT_POLL_MS; "Getting hot" over the
	 * game when it passes the trip, ahead of any pop-up waiting */
	heat_tick();
	if (heat_take(&g_heat, plat_now_ms())) heat_notice();
	/* Auto's clock, for the file and for nothing else. Diatom reads the theme
	 * from THEME_FILE as its bar comes up, and the launcher is in this wait
	 * for the whole of a game, so a game that is on across 21:00 would have
	 * its bar in the theme of the hour it started in. theme_tick writes the
	 * file when the theme changes and draws nothing, and it looks at the
	 * clock once a minute, so it costs a compare here. The screens are told
	 * by the next one that asks, through tick_theme: g_theme_late. */
	if (theme_tick()) {
		g_theme_late = true;
		/* and NES Player 2's page, for its next load */
		p2_theme_changed();
	}
	/* The low-battery disc over the game, which Diatom draws as this process
	 * can draw nothing while a game runs, blinking when critical. Sent only
	 * on a change; the level is read every five seconds. */
	plat_resident_low_battery(battery_level());

	/* Play time. Writes at most one unsynced row and usually nothing, and
	 * never an fsync: measured on the card, an fsync is 1.56 ms median but
	 * 16.4 ms at the tail, which is a dropped frame. */
	stats_tick(plat_now_ms());

	/* The charger, followed rather than sampled once at launch. Plugging in
	 * mid-game has to stop the countdown and unplugging has to start a fresh
	 * one, and SETIDLE does both: Diatom restarts its clock on every one it
	 * receives.
	 *
	 * Which is also why this only sends on a CHANGE. Sending it every tick
	 * would restart that clock ten times a second and Auto Off would never
	 * fire at all. */
	/* A cable plugged in mid-game, or a headset that connected or walked out
	 * of range, moves the sound without leaving the game. That is the whole
	 * point of Diatom making this a state rather than a launch argument. */
	g_game_ticking = true;             /* see bt_volume_follow */
	aout_apply(false);
	g_game_ticking = false;
	aout_retry_in_game();

	/* Held on the charger, and while a long task runs behind the game: the
	 * same answer every screen gets (idle_held; 2026-11-11: Auto Off
	 * powered the Pixel down in the middle of an update) */
	if (g_idle_secs > 0) {
		bool ch = idle_held();

		if (ch != g_idle_charging) {
			g_idle_charging = ch;
			plat_resident_line("SETIDLE\tms=%d",
			                   ch ? 0 : g_idle_secs * 1000);
		}
	}

	/* The account's answer, as soon as it lands - see sync_poll.
	 *
	 * When it adds achievements this device did not know were earned, the list
	 * Diatom is watching still has them in it, and one of them firing reads as
	 * a fresh unlock. So the list is handed over again without them - but only
	 * early in a session. Reloading a set starts every partly-met achievement
	 * over (Diatom's diatom_cheevos_load re-initializes the runtime), and
	 * WATCH_RETRIM_MS is ten times the measured answer and still the opening of
	 * a session. Later than that the count is still put right; a repeat unlock
	 * is sent and answered as already held.
	 *
	 * Before the return below, which is about a set still being fetched: the
	 * two never share the slot, because this game's question is only asked
	 * once its set has arrived. */
	if (sync_poll() > 0 && g_watch_ms &&
	    plat_now_ms() - g_watch_ms <= WATCH_RETRIM_MS) {
		if (chv_write_active(g_watch_active))
			plat_resident_line("SETCHEEVOS\tpath=%s\tconsole=%d",
			                   g_watch_active, chv_console());
		else                           /* all of it held: watch nothing */
			plat_resident_line("SETCHEEVOS\tpath=\tconsole=%d", chv_console());
		g_watch_ms = 0;
		fprintf(stderr, "cheevos: the account held some already; watching the rest\n");
	}

	/* A first play's hash, made on its own thread (src/rahashjob.h): the
	 * set is asked for when it lands, for the set the job was started for.
	 * No hash: the file could not be read, or an .m3u's first disc has no
	 * reader, and its rows say "none" (chv_none_mark), as before. */
	if (g_pending_hashing) {
		char h[33], set[sizeof g_pending_set];
		int r = rhj_poll(h, set, sizeof set);

		if (r == 0) return;
		g_pending_hashing = false;
		if (r == 1) {
			/* The hash in the log, and the answer below: what a game that
			 * found no set was asked by (a DS game, 2026-11-17) */
			fprintf(stderr, "cheevos: first play, hash %s; asking RetroAchievements\n", h);
			snprintf(g_pending_set, sizeof g_pending_set, "%s", set);
			ra_fetch_begin(h, g_pending_set);
		} else {
			if (r == -1) {
				fprintf(stderr, "cheevos: first play, no hash for this file; marked none\n");
				chv_none_mark(set, true);
			}
			g_pending_set[0] = '\0';
		}
		return;
	}
	if (!g_pending_set[0]) return;

	/* Once. Calling it twice advances the state machine twice, which is a
	 * lookup answered and then thrown away. */
	rc = ra_fetch_step();
	if (rc == 0) return;                        /* still working */
	/* RA has nothing for this ROM: said so on its Achievements rows, "none"
	 * rather than "not checked yet" (chv_state_of). A fetch that failed
	 * says nothing, and the next launch with a network asks again. */
	if (rc == RA_FETCH_NOTHING) {
		fprintf(stderr, "cheevos: RetroAchievements has no game for that hash; marked none\n");
		chv_none_mark(g_pending_set, true);
	} else if (rc < 0) {
		fprintf(stderr, "cheevos: no set came (no network, or no answer); asked next launch\n");
	}
	if (rc < 0) { g_pending_set[0] = '\0'; return; }
	chv_none_mark(g_pending_set, false);        /* a set now: it has one */

	/* A FETCH CAN OUTLIVE THE GAME IT WAS FOR, and handing over the wrong set
	 * is not a cosmetic mistake. This runs on the game tick, so the game that
	 * started the fetch may have ended and another may be running - and below,
	 * SETCHEEVOS hands the set to Diatom mid-game, which its protocol makes
	 * the normal path precisely because a download can still be in flight.
	 *
	 * Nothing downstream would catch it. rcheevos would evaluate one game's
	 * conditions against another game's memory, and a condition that matched
	 * by coincidence would unlock an achievement the player never earned, in a
	 * game they were not playing, and on_cheevo_unlocked would queue it for
	 * submission to their account.
	 *
	 * Seen 2026-09-10: launching Advance Guardian Heroes drew the set notice
	 * for A Boy and His Blob, an NES game fetched earlier the same evening.
	 * The active set was not poisoned that time, which is luck, not a guard.
	 *
	 * Dropped rather than canceled: there is no ra_fetch_abort, and
	 * ra_fetch_step only runs while g_pending_set is set, so clearing it on
	 * game end - the fix this looked like it wanted - would abandon a live
	 * state machine with nobody to drive it. The fetch has already written the
	 * set to its own cache by now, so the next launch of THAT game finds it
	 * waiting. A stale fetch becomes a cache warm instead of a hazard. */
	if (strcmp(g_pending_set, g_game_set) != 0) {
		fprintf(stderr, "cheevos: set arrived for a game that is no longer "
		                "running; cached, not applied\n");
		g_pending_set[0] = '\0';
		return;
	}

	if (!chv_load(g_pending_set)) { g_pending_set[0] = '\0'; return; }
	sync_begin(chv_game(), g_pending_set);   /* read on a later tick - sync_poll */

	if (chv_write_active(g_pending_active)) {
		plat_resident_line("SETCHEEVOS	path=%s	console=%d",
		                   g_pending_active, chv_console());
		g_watch_ms = plat_now_ms();
		snprintf(g_watch_active, sizeof g_watch_active, "%s", g_pending_active);
		snprintf(msg, sizeof msg, T(S_CHEEVOS_TO_EARN), chv_count());
	} else {
		snprintf(msg, sizeof msg, T(S_CHEEVOS_ALL_EARNED), chv_count());
	}

	/* Rendering is safe here; presenting would not be. Same rule as an
	 * unlock notice, and the same queue. */
	notice_queue(NQ_SET, 0, 3500, msg, chv_game_title(), NULL, 0, 0);
	g_pending_set[0] = '\0';
}

/* ---------- the pop-ups over a game ------------------------------------- */

/* Rendered here, shown by Diatom (src/notice.h), one after another
 * (src/noticeq.h): Diatom holds one overlay and a second replaced the first.
 * The file is in /tmp, which is memory: a progress pop-up comes every few
 * seconds while a count moves, and each was a write to the card. */
#define NOTICE_FILE "/tmp/zyperos-notice.dtov"

noticeq g_nq;

/* What a notice carries besides its words (src/notice.h), by what it is
 * about: an unlock, and mastery, which is one, has its badge and Zyper; a
 * count moving, its badge; the set's arrival has neither. Where the card has
 * no art for a badge the plain one takes its place (src/noticeart.h). */
unsigned notice_look(nq_kind kind)
{
	switch (kind) {
	/* Zyper at its end unless the look has him off (DESIGN.md 3.5) */
	case NQ_UNLOCK:   return NOTICE_ICON | (look_zyper_off() ? 0 : NOTICE_ZYPER);
	case NQ_PROGRESS: return NOTICE_ICON;
	default:          return 0;
	}
}

/* The next one, if the one on screen has had its time. From the game tick,
 * and as soon as one is queued. */
static void notice_pump(void)
{
	nq_item it;

	if (!nq_next(&g_nq, plat_now_ms(), &it)) return;
	if (it.mark ? notice_render_mark(it.head, it.body, (notice_mark)it.mark,
	                                 NOTICE_FILE, theme_index())
	            : notice_render_ex(it.head, it.body, it.badge[0] ? it.badge : NULL,
	                               notice_look(it.kind), it.value, it.target,
	                               NOTICE_FILE, theme_index()))
		plat_resident_line("OVERLAY\tpath=%s\tms=%d", NOTICE_FILE, it.ms);
}

/* "Getting hot" over a game (DESIGN.md 11.23): the notice with a thermometer
 * where a badge goes, held **hold-heat**, ahead of every pop-up waiting.
 * Zyper is over a game only in an unlock's notice (DESIGN.md 10), not here. */
static void heat_notice(void)
{
	nq_item it;

	heat_said("over a game");
	memset(&it, 0, sizeof it);
	it.kind = NQ_ALERT;
	it.ms = DS_MS_HOLD_HEAT;
	it.mark = NOTICE_MARK_HEAT;
	snprintf(it.head, sizeof it.head, "%s", HEAT_HEAD);
	snprintf(it.body, sizeof it.body, "%s", HEAT_BODY);
	nq_push(&g_nq, &it);
	notice_pump();
}

void notice_queue(nq_kind kind, int id, int ms, const char *head,
                  const char *body, const char *badge, int value, int target)
{
	nq_item it;

	memset(&it, 0, sizeof it);
	it.kind = kind;
	it.id = id;
	it.ms = ms;
	it.value = value;
	it.target = target;
	snprintf(it.head, sizeof it.head, "%s", head ? head : "");
	snprintf(it.body, sizeof it.body, "%s", body ? body : "");
	if (badge && access(badge, R_OK) == 0) snprintf(it.badge, sizeof it.badge, "%s", badge);
	nq_push(&g_nq, &it);
	notice_pump();
}

/* Called from inside plat_resident_wait, mid-game, when the launcher owns
 * nothing and can draw nothing. Recording is all that happens here; the
 * telling happens the next time this process has the screen. */
static void on_cheevo_unlocked(int id)
{
	char p[CFG_STR * 2];
	const cheevo *c = NULL;
	int i;

	/* Recorded with when it was earned, in which mode (softcore, the only
	 * one), with the ROM's hash from the set (chv_note_unlock_at), and
	 * whether there was a network: an unlock earned offline is shown here
	 * the same as any other, and sent when one is back (ra_flush_watch). */
	if (!chv_note_unlock_at(id, (long)time(NULL), false, !net_online())) return;

	/* Written through immediately rather than at exit. A game ends by power
	 * button as often as by menu, and an achievement lost to a flat battery is
	 * one the player has to earn twice. */
	chv_earned_save();

	/* And say so, on screen, now. This process owns no display while a game
	 * runs, so it renders the line and Diatom composites it.
	 * Rendering is safe here; presenting would not be. */
	for (i = 0; i < chv_count(); i++)
		if (chv_at(i)->id == id) { c = chv_at(i); break; }

	{
		char head[64];

		/* Points on the context line, not the name: "First Blood!" is what
		 * the player wants to read, and "1 point" is what it was worth. */
		if (c && c->points > 0)
			snprintf(head, sizeof head, TN(S_CHEEVOS_UNLOCKED_POINTS, c->points),
			         c->points);
		else
			snprintf(head, sizeof head, "%s", T(S_CHEEVOS_UNLOCKED));

		/* With its badge, when the card has it (src/rabadge.h) */
		p[0] = '\0';
		if (c) rb_file(chv_source(), c->badge, p, sizeof p);
		notice_queue(NQ_UNLOCK, id, 4000, head,
		             c && c->title[0] ? c->title : T(S_CHEEVOS_UNLOCKED), p[0] ? p : NULL, 0, 0);
	}
	/* The last one of the set: mastered, said after it */
	if (chv_count() > 0 && chv_earned() == chv_count()) {
		char head[64];

		snprintf(head, sizeof head, T(S_CHEEVOS_MASTERED), chv_points_total());
		notice_queue(NQ_UNLOCK, 0, 4000, head, chv_game_title(), NULL, 0, 0);
	}
}

/* A count moving, from Diatom (state=progress): kept on the achievement for
 * the list, and said over the game - the name, a strip and "74 / 100" - when
 * its turn in the queue comes. Diatom already holds these to one every two
 * seconds per achievement. */
static void on_cheevo_progress(int id, const char *text, int value, int target)
{
	char p[CFG_STR * 2] = "", count[32];
	const cheevo *c = NULL;
	int i;

	if (!chv_note_progress(id, text)) return;
	for (i = 0; i < chv_count(); i++)
		if (chv_at(i)->id == id) { c = chv_at(i); break; }
	if (!c || c->earned || c->earned_now) return;
	/* One pop-up for a count kept at levels: the nearest one not earned */
	if (!chv_progress_said(id)) return;
	/* "74/100" as rcheevos writes it, spaced the way the list writes counts */
	if (strchr(text, '/') && !strchr(text, ' '))
		snprintf(count, sizeof count, "%.*s / %s", (int)(strchr(text, '/') - text),
		         text, strchr(text, '/') + 1);
	else
		snprintf(count, sizeof count, "%s", text);
	rb_file(chv_source(), c->badge, p, sizeof p);
	notice_queue(NQ_PROGRESS, id, 2500, c->title[0] ? c->title : T(S_CHEEVOS_PROGRESS), count,
	             p[0] ? p : NULL, value, target);
}

/* A pop-up that answers a press over the game: rendered here, composited by
 * Diatom, as an unlock notice is, and shorter, since it says a word or two.
 * Past the queue, at once, and the queue waits for it to go. */
static void popup_now(const char *head, const char *body)
{
	char p[CFG_STR * 2];

	snprintf(p, sizeof p, "%s", NOTICE_FILE);
	if (notice_render(head, body, p, theme_index()))
		plat_resident_line("OVERLAY\tpath=%s\tms=1500", p);
	nq_hold(&g_nq, plat_now_ms(), 1500);
}

void popup_say(const char *head, const char *body)
{
	popup_now(head, body ? body : "");
}

/* Called from inside plat_resident_wait when Diatom says the speed step
 * changed (SELECT+R2, SELECT+L2, Fast Forward), or that a game resumed fast:
 * in halves, 3 is 1.5x, and 0 is Max. The step, not the speed reached: on a
 * heavy SNES or GBA game the top step runs about 2.5x, and that figure moves
 * from scene to scene; what Max reaches, On-Screen Stats' Speed says. */
static void on_speed(int half)
{
	char body[24];

	speed_said(half, body, sizeof body);
	popup_now(T(S_POPUP_SPEED), body);
}

/* Called from inside plat_resident_wait for the player's hotkeys the
 * launcher carries out (src/hotkeys.h): a screenshot taken, its shutter
 * heard over the game at once (DESIGN.md 9: the one sound over a game, none
 * with UI Sounds Off), Diatom buzzing it itself; a screenshot Diatom wrote,
 * said over the game with the picture small beside "Screenshot saved" and
 * where it went; the next shader or display mode, stepped and kept as the
 * menu's rows do, and named over the game */
static app *g_hotkey_app;   /* main's, for on_hotkey's steps */

/* What the launcher draws over every screen (plat_set_overlay): the replug
 * message, and a screenshot's notice (src/menukeys_screen.h) */
static void every_overlay(SDL_Renderer *r)
{
	replug_overlay(r);
	mk_overlay(r);
}

static void on_hotkey(const char *key, const char *path)
{
	char p[CFG_STR * 2], body[CFG_STR * 2];

	if (!strcmp(key, "shot") && !path) {
		const uis_cue c = uisound_shot(uisound_setting());

		/* On the mix the game plays into ("default", a dmix on both
		 * handhelds), beside it: nothing of the game's sound is touched.
		 * The output is let go once the shutter ends, not kept the usual
		 * quiet spell over the game. */
		if (c.level > 0.0f) {
			uisound_cue(c);
			uisound_release();
		}
		fprintf(stderr, "hotkey: screenshot taken, %s\n",
		        c.level > 0.0f ? "its shutter" : "no shutter (UI Sounds Off)");
		return;
	}
	if (!strcmp(key, "shot") && path) {
		const char *folder = strstr(path, "/Screenshots/");
		const char *end = folder ? strrchr(path, '/') : NULL;

		/* "Screenshots/Nintendo 64", where the file is */
		if (folder && end > folder) snprintf(body, sizeof body, "%.*s", (int)(end - folder - 1), folder + 1);
		else snprintf(body, sizeof body, "%s", path);
		snprintf(p, sizeof p, "%s", NOTICE_FILE);
		if (notice_render_ex(T(S_HOTKEYS_SHOT_SAVED), body, path, NOTICE_PICTURE, 0, 0, p, theme_index()))
			plat_resident_line("OVERLAY\tpath=%s\tms=1500", p);
		nq_hold(&g_nq, plat_now_ms(), 1500);
		/* and the Screenshots shelf read again once the game ends */
		shots_taken();
		return;
	}
	if (!strcmp(key, "shader") || !strcmp(key, "display")) {
		const bool shader = key[1] == 'h';

		if (!g_hotkey_app) return;
		gm_hotkey_step(g_hotkey_app, shader, body, sizeof body);
		popup_now(T(shader ? S_HOTKEYS_POPUP_SHADER : S_HOTKEYS_POPUP_DISPLAY), body);
	}
}

/* Called from inside plat_resident_wait when a button given Stick On/Off
 * (the PSP's R2 by default, DESIGN.md 11.45) turned a game's d-pad into the
 * analog stick (on) or back: said over the game, and kept for the game
 * (rms_stick_put), as its D-pad as Stick row says. The answers to the Controls tab's own SETSTICKs
 * never come here (plat_resident_set_stick): its row said it already. */
static void on_stick(bool on)
{
	popup_now(T(S_POPUP_STICK), T(on ? S_POPUP_STICK_ON : S_POPUP_STICK_OFF));
	if (g_quick.tag[0] && g_quick.file[0]) rms_stick_put(&g_quick.sys, g_quick.file, on);
}

/* Called from inside plat_resident_wait as L1 or R1 moves a Famicom Disk
 * System game's disk (src/diskpopup.h): said over the game, as the speed
 * step is */
static void on_disk(const char *state, int disk, char side)
{
	const char *head;
	char body[64];

	if (disk_popup_words(state, disk, side, &head, body, sizeof body)) popup_now(head, body);
}

/* Called from inside plat_resident_wait for the quick slot (platform.h's
 * PLAT_QUICK_*). SELECT+R1 and SELECT+L1 ask, and the slot is this
 * process's to know (quick_slot_get), so the SAVE or LOAD goes back to
 * Diatom from here, a save with its picture for the slot strip, and the
 * pop-up says the slot when the answer comes. A slot with nothing in it is
 * said at once, with no LOAD: the strip says "Empty" for the same file. */
static void on_quick(int what, const char *path)
{
	char slot[32];

	if (what == PLAT_QUICK_ASK_SAVE || what == PLAT_QUICK_ASK_LOAD) {
		bool load = what == PLAT_QUICK_ASK_LOAD;
		int n;

		if (!g_quick.stem[0]) return;
		if (!g_quick.states) {
			popup_now(T(S_POPUP_NO_STATES), g_quick.name);
			return;
		}
		n = quick_slot_get(g_quick.tag);
		snprintf(slot, sizeof slot, T(S_SLOTS_SLOT), n);
		snprintf(g_quick.path, sizeof g_quick.path, "%s.%d.state", g_quick.stem, n);
		if (load && access(g_quick.path, R_OK) != 0) {
			popup_now(T(S_POPUP_SLOT_EMPTY), slot);
			return;
		}
		g_quick.asked = n;
		g_quick.load = load;
		if (load) {
			/* The game as it is, for Undo Load (SLOT_UNDO), first: Diatom
			 * writes it, then loads the slot over it. Its answer is no
			 * business of the pop-up's (another path). */
			plat_resident_line("SAVE\tpath=%s.undo.state", g_quick.stem);
			plat_resident_line("LOAD\tpath=%s", g_quick.path);
		}
		else
			plat_resident_line("SAVE\tpath=%s\tpreview=%s.%d.bmp", g_quick.path,
			                   g_quick.stem, n);
		return;
	}
	/* An answer to what the hotkeys asked; the menu's own are not said */
	if (!g_quick.asked || !path || strcmp(path, g_quick.path)) return;
	snprintf(slot, sizeof slot, T(S_SLOTS_SLOT), g_quick.asked);
	g_quick.asked = 0;
	switch (what) {
	case PLAT_QUICK_SAVED:       popup_now(T(S_POPUP_SAVED), slot); break;
	case PLAT_QUICK_LOADED:      popup_now(T(S_POPUP_LOADED), slot); break;
	case PLAT_QUICK_SAVE_FAILED: popup_now(T(S_POPUP_SAVE_FAILED), slot); break;
	default:                     popup_now(T(S_POPUP_LOAD_FAILED), slot); break;
	}
}

static unsigned g_wifi_asked_at;   /* when menu_wifi last forked, plat_now_ms */

/* ---------- the status corner ---------------------------------------------- */

/* MENU > Clock: the time in 24 hours, on the shelf's corner and in Date &
 * Time's row; 12-hour, the default, is what the launcher always was. Saved in
 * the device's database as Appearance is, under `clock`, "12h" or "24h". */
bool g_clock24;

static void clock24_load(void)
{
	char v[8];

	db_get_str(db_dev(), "clock", v, sizeof v, "12h");
	g_clock24 = !strcmp(v, "24h");
}

void clock24_save(bool on)
{
	g_clock24 = on;
	db_set_str(db_dev(), "clock", on ? "24h" : "12h");
}

/* MENU > USB Accessories on the Pixel 2: Ask, the default, or Automatic,
 * saved as "ask" or "auto" under `usb_accessories` and told to usbrole
 * (plat_usb_power_mode, src/usbpower.h) as the launcher starts and as it
 * changes */
bool g_usb_auto;
/* A plug that waits for an answer now (it may be asked about, usbrole
 * powers nothing by itself): the corner's USB mark and MENU > USB Device,
 * which asks again for it. 0 none. */
unsigned g_usb_wait;
/* --usb-menu: MENU's USB rows on a host shot, which has no usbrole */
bool shot_usb_menu;
bool shot_usb_said;     /* --usb-menu said: Automatic's message */
bool shot_usb_offer;    /* --usb-offer: Zyper's offer of Automatic */

/* An accessory powered when the handheld turned off, kept on the card for
 * the next start (usbpower_resume): read once there and gone */
bool g_usb_kept;

static const char *usb_kept_path(void)
{
	static char p[512];

	snprintf(p, sizeof p, "%s/usb-kept", P_USERDATA);
	return p;
}

static void usb_auto_load(void)
{
	char v[8];

	db_get_str(db_dev(), "usb_accessories", v, sizeof v, "ask");
	g_usb_auto = !strcmp(v, "auto");
	plat_usb_power_mode(g_usb_auto);
	/* Back from USB Drive with the cable in: that plug was answered */
	if (access("/tmp/zyperos_usbdrive_back", F_OK) == 0) {
		remove("/tmp/zyperos_usbdrive_back");
		usblink_skip_ask();
		fprintf(stderr, "usb: back from USB Drive: not asked again for this plug\n");
	}
	/* The brightness set on Flash Drive's page (usbdrive), kept as if set
	 * here: the same rungs, saved as the launcher saves its own */
	{
		FILE *f = fopen("/tmp/zyperos_usbdrive_bright", "r");
		int level;

		if (f) {
			if (fscanf(f, "%d", &level) == 1 && level >= 0 && level <= PLAT_BRIGHT_MAX) {
				plat_brightness_set(level);
				fprintf(stderr, "usb: the brightness set on Flash Drive's page: %d\n", level);
			}
			fclose(f);
			remove("/tmp/zyperos_usbdrive_bright");
		}
	}
	g_usb_kept = access(usb_kept_path(), F_OK) == 0;
	if (g_usb_kept) {
		remove(usb_kept_path());
		fprintf(stderr, "usb: an accessory was powered at the last power-off\n");
	}
}

/* Turning off: an accessory powered now (the port a host, no computer
 * seen) is kept for the next start */
void usb_kept_save(void)
{
	usb_power p;
	FILE *f;

	remove(usb_kept_path());
	if (!plat_usb_power(&p) || !p.host || p.computer) return;
	if ((f = fopen(usb_kept_path(), "w"))) {
		fprintf(f, "accessory plug %u\n", p.plug);
		fclose(f);
	}
}

void usb_auto_save(bool on)
{
	g_usb_auto = on;
	db_set_str(db_dev(), "usb_accessories", on ? "auto" : "ask");
	plat_usb_power_mode(on);
}

/* Zyper's offer of Automatic (usbpower_offer_after): the player's USB
 * Accessory answers counted under `usb_accessory_answers`, the offer's
 * answer kept under `usb_auto_offer` ("auto" or "ask"), and the offer due,
 * for the shelf to show once the question is over (usb_offer_show) */
bool g_usb_offer_due;

void usb_offer_count(void)
{
	int got = db_get_int(db_dev(), "usb_accessory_answers", 0);
	unsigned was = got > 0 ? (unsigned)got : 0, n;

	g_usb_offer_due = usbpower_offer_after(was, g_usb_auto, db_has(db_dev(), "usb_auto_offer"), &n);
	if (n != was) db_set_int(db_dev(), "usb_accessory_answers", (int)n);
}

const char *const BATT_IDS[BATT_MODES] = { "bars", "percentage", "time" };
int g_batt_mode;

static void batt_icon_load(void)
{
	char v[16];
	int k;

	db_get_str(db_dev(), "battery_icon", v, sizeof v, "bars");
	g_batt_mode = BATT_BARS;
	for (k = 0; k < BATT_MODES; k++)
		if (!strcmp(v, BATT_IDS[k])) g_batt_mode = k;
}

/* --status, for a shot: what the corner shows in place of the device's
 * clock, radio and battery, which a host does not have. Set by main's
 * argument loop, and only for a shot, as --theme is; `minutes` is
 * the time of day, -1 for a clock never set. `shot` is that a shot is being
 * taken at all: then the clock is --status's or none, since a shot that
 * showed the host's would change from one minute to the next, and shots are
 * compared byte for byte (check-flip). */
shot_status shot_st = { .minutes = -1, .h24 = -1, .eta = -1, .calc = -1, .icon = -1 };

/* Where the corner (its clock's text box and its marks), the clock alone, the
 * last panel and the games title's capitals were drawn this frame, for a
 * shot to hold the corner clear of them (shot_corner_clear). Zero width: not
 * drawn. */
SDL_Rect g_corner_box, g_clock_box, g_menu_panel, g_title_caps;
/* The last menu panel's body: below its heading, inside its pad, for what a
 * screen draws in it beside the rows (About's photo of Zyper) */
SDL_Rect g_menu_body;
static int      g_marks_w;   /* the marks' width as status_marks_width gave it */
/* The battery's number or time as wide as it wrote, before STATUS_PCT_MAX
 * held it to its room: a shot checks it fits (shot_corner_clear) */
int      g_pct_natural;

/* The screen was cleared: nothing recorded above is on it any more. */
void drawn_forget(void)
{
	g_corner_box = g_clock_box = g_menu_panel = g_title_caps = (SDL_Rect){ 0 };
	g_marks_w = 0;
	g_pct_natural = 0;
}

/* The critical blink, a second on and a second off, slow enough not to pull
 * at the eye: the fill goes in the second off, the outline stays. Returns
 * how long until it changes by itself, in ms, and tells the shelf's loop the
 * same, so a screen that only draws when something moves still blinks it.
 * Diatom blinks its disc over a game on the same rhythm. */
static unsigned status_blink(status_marks *m)
{
	Uint32 now, flip;

	if (m->batt < 0 || status_batt_level(m->batt, m->charging) < 2) return (unsigned)-1;
	now = plat_now_ms();
	flip = 1000 - now % 1000;
	redraw_at(now + flip);
	m->blink_off = (now / 1000) % 2 != 0;
	if (shot_st.set) m->blink_off = shot_st.m.blink_off;   /* a still, asked for */
	return flip;
}

/* Wi-Fi's signal in steps, from `wpa_cli signal_poll`, every two seconds
 * while connected, as the state is. A second fork, my call: neither
 * kernel fills /proc/net/wireless, which would have cost none (cfg80211's
 * wireless extensions are off in plastron's Pixel 2 config and unset in the
 * Brick's). Not on a frame that menu_wifi has just forked on, so the two
 * forks never stack into one 60 ms frame; and, as the state, not asked while
 * the shelf moves (the caller's). A driver that cannot say shows full: a
 * connection is true, a guess at its strength is not. */
static int status_signal(void)
{
	static unsigned next;
	static int steps = STATUS_WIFI_FULL;
	unsigned now = plat_now_ms();

	if ((next == 0 || (int)(now - next) >= 0) && now - g_wifi_asked_at >= 500) {
		char out[512];
		int dbm;

		next = now + 2000;
		steps = wifi_signal_poll(out, sizeof out) && status_rssi_dbm(out, &dbm)
		      ? status_wifi_steps(dbm) : STATUS_WIFI_FULL;
	}
	return steps;
}

/* What the marks show now. Wi-Fi from the menu's two-second cache, but not
 * asked while the shelf moves: the ask forks wpa_cli, about 30 ms out of
 * this 119 MB process, which mid-move is a frame dropped, so the mark keeps
 * what it last knew until the row rests. A headset from the file
 * launch.sh writes (aout_bt_sink), which costs no fork and is up to twenty
 * seconds late, which a mark can afford. The battery from battery_now. */
/* On the cable and held off at the charge limit: the corner's plug. The
 * gauge read every 2 s while the cable is in and a limit is set. */
static bool charge_held(void)
{
	static unsigned at;
	static bool primed, held;
	unsigned now = plat_now_ms();
	ch_reading r;

	if (!g_charge_limit) return false;
	if (!primed || now - at >= 2000) {
		primed = true;
		at = now;
		held = plat_charge_read(&r) && r.held;
	}
	return held;
}

/* What MENU > Battery Icon > Time Left writes in the corner: the kind of it
 * (src/battery_eta.h's batt_corner_kind), the minutes when it is a time, and
 * how long the dots have been going, for the animation's tick. */
typedef struct {
	corner_kind kind;
	int         minutes;
	unsigned    calc_ms;
} corner_view;

/* The gauge's files, read every 2 s (plat_charge_read: the charging screen's
 * own, so the one gauge is read the one way) and kept between. On the cable
 * the time is to full, or to the charge limit when one is set, and is two
 * estimates blended: the straight line from the gauge's first read, and the
 * charging screen's (src/charging.h: CC then CV, after its 30 s settle) as it
 * settles. Off the cable, the gauge's charge over its current from the first
 * read, blended into the falls' as that earns trust (battery_minutes_left).
 * A gauge with no current file (a simulator's, or a driver's with none)
 * gives the gauge's half nothing, and the falls carry on alone; on the cable
 * it gives the percentage, since nothing can ever come. */
static corner_view corner_view_now(const status_marks *m)
{
	static unsigned at, cable_since, calc_since;
	static bool primed, was, ok, calcing;
	static ch_reading r;
	static ch_eta eta;
	unsigned now = plat_now_ms();
	corner_in in = { .charging = m->charging, .minutes = -1 };
	corner_view v;

	if (shot_st.set) {
		/* A shot's own: eta= the minutes, calc[=N] the dots, full the charge */
		in.full = shot_st.full;
		in.held = m->held;
		in.minutes = shot_st.eta;
		in.expect = shot_st.calc >= 0;
		v.kind = m->batt < 0 ? CORNER_PCT : batt_corner_kind(&in);
		v.minutes = in.minutes;
		v.calc_ms = 0;
		return v;
	}
	if (m->batt < 0) return (corner_view){ CORNER_PCT, -1, 0 };
	if (!primed || m->charging != was) {
		/* The cable came or went: what was learned of the other is over */
		was = m->charging;
		eta = (ch_eta){ 0 };
		cable_since = now;
		primed = false;
	}
	if (!primed || now - at >= 2000) {
		primed = true;
		at = now;
		ok = plat_charge_read(&r);
		if (ok) {
			r.limit = charge_limit_now();
			if (m->charging) ch_eta_note(&eta, now, &r);
		}
	}
	if (m->charging) {
		if (ok && r.online) {
			int quick = batt_gauge_to_full(r.now_mah, r.full_mah, r.limit, r.ma);
			int slow = ch_eta_minutes(&eta, now, &r);

			in.full = r.full || r.pct >= 100;
			in.held = r.held;
			in.minutes = batt_eta_blend(quick > 0 ? quick : -1, slow > 0 ? slow : -1,
			                            batt_ramp(now - eta.first_at, CH_SETTLE_MS, CH_SETTLE_MS));
			/* Dots for the first CH_SETTLE_MS when a current can be read
			 * and none is going in yet; the percentage when none can be */
			in.expect = r.ma_known && now - cable_since < CH_SETTLE_MS;
		}
	} else {
		int quick = ok && !r.online
		          ? batt_gauge_left(r.now_mah, r.full_mah, r.ma, BATT_EMPTY_PCT) : -1;

		in.minutes = battery_minutes_left_with(quick);
		in.expect = m->batt > BATT_EMPTY_PCT;
	}
	v.kind = batt_corner_kind(&in);
	v.minutes = in.minutes;
	if (v.kind != CORNER_CALC) calcing = false;
	else if (!calcing) { calcing = true; calc_since = now; }
	v.calc_ms = calcing ? now - calc_since : 0;
	return v;
}

/* "45 min", "3 h", "3 h 20 min": to the minute under 10, to 5 under 10
 * hours and to the hour after (batt_eta_round), so a number changes when it
 * means something */
static void corner_time_text(int minutes, char *out, size_t n)
{
	int r = batt_eta_round(minutes);

	if (r < 60)            snprintf(out, n, T(S_CORNER_TIME_MIN), r);
	else if (r % 60 == 0)  snprintf(out, n, T(S_CORNER_TIME_H), r / 60);
	else                   snprintf(out, n, T(S_CORNER_TIME_H_MIN), r / 60, r % 60);
}

/* System Update's page is up: the corner's update mark stays off there */
bool g_up_shown;

static void status_gather(status_marks *m, bool moving)
{
	static int wifi = STATUS_WIFI_NONE;
	int pct;
	bool chg;

	if (shot_st.set) {
		*m = shot_st.m;
		return;
	}
	memset(m, 0, sizeof *m);
	if (!plat_has_wifi()) {
		wifi = STATUS_WIFI_NONE;
	} else if (!moving) {
		wifi_state ws = menu_wifi(NULL, 0);

		wifi = ws == WIFI_OFF       ? STATUS_WIFI_NONE
		     : ws == WIFI_CONNECTED ? status_signal() : STATUS_WIFI_IDLE;
	}
	m->wifi = wifi;
	m->bt = plat_has_bluetooth() && aout_bt_sink()[0];
	m->phones = plat_headphones_present();
	m->muted = plat_muted();
	m->batt = battery_now(&pct, &chg) ? pct : -1;
	m->charging = chg;
	m->held = chg && charge_held();
	xs_mark(m);
	m->usb = g_usb_wait != 0;
	/* System Update at work behind the screen, and how far its download is:
	 * not on its own page, which shows it whole */
	if (!g_up_shown && up_working()) {
		const su_view *v = up_view(false);

		m->update = true;
		m->update_pct = v->st.total ? (int)(v->st.done * 100 / v->st.total) : 0;
	} else if (dlq_behind()) {
		/* The downloads going on behind the screen (src/dlq.h): the same
		 * arrow into a tray, swept as far as the lanes have got; not on
		 * their own page, which shows the run whole */
		m->update = true;
		m->update_pct = dlq_pct();
	}
}

/* The time a shot shows: today (or --status's date=) at its time of day, into *t. False for
 * a shot with no time to show - no --status, or one with no time - which
 * then shows none, since the host's would change the shot every minute. Not
 * a shot: *t as it is, and true. */
bool shot_time(time_t *t)
{
	struct tm tm;

	if (!shot_st.shot) return true;
	if (!shot_st.set || shot_st.minutes < 0) return false;
	localtime_r(t, &tm);
	if (shot_st.year) {
		tm.tm_year = shot_st.year - 1900;
		tm.tm_mon = shot_st.mon - 1;
		tm.tm_mday = shot_st.mday;
	}
	tm.tm_hour = shot_st.minutes / 60;
	tm.tm_min = shot_st.minutes % 60;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	*t = mktime(&tm);
	return true;
}

/* The corner's clock at `t`, "10:08", or false while the clock is one nobody
 * set - and in a shot, unless --status gives a time. */
bool status_clock(time_t t, char *out, size_t n)
{
	if (!shot_time(&t)) return false;
	if (!clock_is_set(t)) return false;
	clock_short(t, g_clock24, out, n);
	return true;
}

/* The top bar on this screen, src/topbar.h: the one header, in one layout,
 * on every screen that has one - the home, the shelves and the menus over
 * them, every screen over a paused game, every MP4 screen. This half gathers
 * what it shows - the radios and the battery (status_gather), the clock in
 * its setting, the battery's words as MENU > Battery Icon says - and keeps
 * the frames coming that make it move: the blink, the next minute, Time
 * Left's minutes and its dots. In `theme`, an index - DS_THEME is a table per
 * file, so theme()'s pointer is not into this one - with `soft` the battery's
 * words' colour: **text-soft**, but **text** over a paused game, where a white
 * frame under scrim.game leaves text-soft at 4.4:1 and text holds 6.0:1.
 * Returns how long until it changes by itself, in ms, and tells the shelf's
 * loop the same. */
static topbar_drawn g_topbar;

unsigned draw_topbar(SDL_Renderer *r, int theme, SDL_Color soft, bool moving)
{
	topbar t = { .theme = theme, .soft = soft };
	struct timespec ts;
	char clk[16], pct[24] = "";
	unsigned wait;

	/* One reading of the clock for what it says and when it next changes */
	clock_gettime(CLOCK_REALTIME, &ts);
	status_gather(&t.marks, moving);
	wait = status_blink(&t.marks);
	/* MENU > Battery Icon > Percentage: "82%". Time Left writes "3 h 20 min"
	 * when it has one, else the same number, and asks for a frame every
	 * minute while it does, as the clock does, so the time counts down. */
	if (g_batt_mode != BATT_BARS && t.marks.batt >= 0) {
		corner_view cv = g_batt_mode == BATT_TIME ? corner_view_now(&t.marks)
		                                          : (corner_view){ CORNER_PCT, -1, 0 };

		if (cv.kind == CORNER_TIME) {
			unsigned minute = 60000;

			corner_time_text(cv.minutes, pct, sizeof pct);
			redraw_at(plat_now_ms() + minute);
			if (minute < wait) wait = minute;
		} else if (cv.kind == CORNER_HELD) {
			snprintf(pct, sizeof pct, "%s", T(S_CORNER_HELD));
		} else if (cv.kind == CORNER_FULL) {
			snprintf(pct, sizeof pct, "%s", T(S_CORNER_FULL));
		} else if (cv.kind == CORNER_CALC) {
			/* Three dots filling in turn where the time will go: a frame
			 * every CORNER_DOT_MS for the first minute of it, then only as
			 * the launcher draws anyway, so a gauge that never answers
			 * does not keep the screen awake */
			t.marks.calc = true;
			t.marks.calc_step = shot_st.set ? shot_st.calc : batt_corner_dots(plat_now_ms());
			if (!shot_st.set && cv.calc_ms < 60000u) {
				redraw_at(plat_now_ms() + CORNER_DOT_MS);
				if (CORNER_DOT_MS < wait) wait = CORNER_DOT_MS;
			}
		} else {
			snprintf(pct, sizeof pct, "%d%%", t.marks.batt > 100 ? 100 : t.marks.batt);
		}
		t.batt = pct;
	}
	if (status_clock(ts.tv_sec, clk, sizeof clk)) {
		/* The next minute. The shelf's one-second heartbeat redraws it
		 * anyway; this is the clock saying when it changes, as every
		 * other thing that moves by itself does. */
		unsigned left = 60000 - (unsigned)((ts.tv_sec % 60) * 1000 + ts.tv_nsec / 1000000);

		t.clock = clk;
		redraw_at(plat_now_ms() + left);
		if (left < wait) wait = left;
	}
	topbar_draw(r, &t, &g_topbar);
	g_corner_box = g_topbar.marks;
	g_clock_box = g_topbar.clock;
	g_marks_w = g_topbar.marks_w;
	g_pct_natural = g_topbar.batt_natural;
	return wait;
}

/* MP4's own shelf of albums. It has the home's header since 2026-11-09 (the
 * clock at the left, the rest at the right; draw_status), as every MP4 screen
 * has, but not the home's song line in it: MP4 is what plays there. */
bool mp4_shelf_shown(const app *a)
{
	return a->sys.count > 0 && a->screen == SCREEN_GAMES &&
	       is_mp4(&a->sys.systems[a->sys_cursor]);
}

void draw_status(app *a)
{
	/* Moving is any frame asked for within 50 ms, this frame or the last:
	 * a card's step, a marquee's travel, the tint easing to a new system, a
	 * menu over the shelf. A fork then would stop a frame of it. */
	bool moving = home_moving(&a->home, plat_now_ms()) || frames_wanted() ||
	              (a->sys.count > 0 && a->view[a->sys_cursor].cf.active);

	draw_topbar(a->r, theme_index(), UI_TEXT_SOFT, moving);
}

/* topbar_half, as the bar was drawn last: MP4's album title on its shelf,
 * and a video's title over its frame, stand in its row */
int header_half(void)
{
	return topbar_half(&g_topbar);
}


/* Auto's clock, asked by a screen's own loop once a pass: true when the theme
 * changed, and then theme_changed has been told. Every loop that stays on
 * screen asks it - the menus through menu_run_body, the shelf's loop for
 * itself, and every screen that runs its own - so a Night Hours edge arriving
 * while one is up is drawn from its next frame, and the file Diatom and
 * portkeys read follows. A loop that draws every pass needs nothing more; one
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

/* ---- the panel pass, and the panel on screen ---------------------------
 *
 * DESIGN.md 8.2: a panel arrives from 96% and clear and leaves the same way;
 * src/menumotion.h says when. To scale and fade as one - its text, its
 * plate, Zyper and his shadow - the panel is drawn into a texture the size of
 * the screen, at the place it is drawn at rest, and copied back about its
 * middle at the size and strength the motion says. Its lift goes straight on
 * the screen under it (menu_panel). At rest none of this runs, so a panel at
 * rest, and every shot of one, is the picture it always was. The texture
 * is g_panel_tex. */

/* `q` at `s` of its size about its middle */
SDL_FRect panel_scaled(const SDL_Rect *q, float s)
{
	float w = (float)q->w * s, h = (float)q->h * s;

	return (SDL_FRect){ (float)q->x + ((float)q->w - w) / 2.0f,
	                    (float)q->y + ((float)q->h - h) / 2.0f, w, h };
}

/* Drawing goes to the pass's texture, cleared to the frame's own **edge** at
 * no alpha: the copy's linear filtering then blends the frame into nothing
 * at its border, where a clear black would draw it a dark rim. False, and
 * drawing stays on the screen, when there is no texture to draw into; the
 * panel is then drawn whole where it would have faded, a cut and no worse. */
bool panel_pass_begin(app *a)
{
	const ds_rgb e = theme()->edge;

	static bool said;

	if (!g_panel_tex) {
		g_panel_tex = SDL_CreateTexture(a->r, PLAT_TARGET_FORMAT,
		                                SDL_TEXTUREACCESS_TARGET,
		                                ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		if (!g_panel_tex) {
			if (!said) fprintf(stderr, "menus: no panel pass: %s\n", SDL_GetError());
			said = true;
			return false;
		}
		SDL_SetTextureBlendMode(g_panel_tex, SDL_BLENDMODE_BLEND);
		/* Set on the texture, as every texture drawn scaled here sets it:
		 * the rim-free edge is linear filtering's, whatever the hint says */
		SDL_SetTextureScaleMode(g_panel_tex, SDL_ScaleModeLinear);
	}
	if (SDL_SetRenderTarget(a->r, g_panel_tex) != 0) {
		/* Wherever a failed switch left drawing, it goes back to the screen */
		plat_draw_to_screen();
		if (!said) fprintf(stderr, "menus: no panel pass: %s\n", SDL_GetError());
		said = true;
		return false;
	}
	SDL_SetRenderDrawColor(a->r, e.r, e.g, e.b, 0);
	SDL_RenderClear(a->r);
	return true;
}

/* Back on the screen, the panel's part of the texture copied about its
 * middle. The panel is opaque in its shape and clear outside it, so the copy
 * at `pose.alpha` is the panel at that opacity, exactly. */
void panel_pass_end(app *a, const SDL_Rect *panel, mm_pose pose)
{
	SDL_FRect d = panel_scaled(panel, pose.scale);

	plat_draw_to_screen();
	SDL_SetTextureAlphaMod(g_panel_tex, (Uint8)(pose.alpha * 255.0f + 0.5f));
	SDL_RenderCopyF(a->r, g_panel_tex, panel, &d);
}

/* THE PANEL ON SCREEN, KEPT WHOLE. What leaves on a submenu, a back or a
 * close is the panel the screen showed last, and the screen that draws it
 * leaving is never the one that drew it: a submenu draws its parent's, a
 * parent its submenu's once that loop has gone, the shelf the last one's. So
 * each panel drawn is copied (src/menusnap.h), the text of its rows and
 * Zyper's rig with it, to be drawn again by whoever comes next (snap_draw).
 * A few kilobytes a frame; a list too long to keep is not kept, and that
 * panel is cut rather than faded.
 *
 * Two copies. `g_snap` is the panel on screen. `g_snap_next` is the panel of
 * a screen still waiting for the one on screen to leave, which it draws
 * leaving in its own place: a press on it ends that leave and puts its own
 * panel at rest, and from then on its own is the panel on screen
 * (snap_promote), what the next motion takes off. */

static bool panel_is_tiles(int kind)
{
	return kind == PANEL_TILES || kind == PANEL_TILES_GRID || kind == PANEL_LIST;
}

/* The marks of the tiles tiles_show is showing, a ui_glyph a tile in the
 * rows' order, which snap_record keeps with the panel's copy */
static const int *g_tile_marks;
static int g_tile_marks_n;

static menu_snap  g_snaps[2];
menu_snap *g_snap = &g_snaps[0], *g_snap_next = &g_snaps[1];

/* A panel's arguments into `s`, as the screen on top drew them now: its
 * heading's caption too (menu_draw_caption), or NULL for none */
void snap_record(menu_snap *s, int kind, const char *heading,
                 const menu_row *rows, int n, int sel, int fixed_w,
                 unsigned accent, const unsigned *vcolors,
                 bool visits_all, int loop_at, const char *heading_note)
{
	if (menusnap_take(s, heading, rows, n, sel, fixed_w, accent, vcolors,
	                  visits_all, loop_at, g_menu_zyper,
	                  g_menu_zyper_at - anim_now()))
		menusnap_note(s, heading_note);
	s->level = mm_depth();
	s->frame = g_frame_no;
	s->kind  = kind;
	s->tab   = g_menu_tabs ? g_menu_tabs->at : -1;
	s->tab_count = g_menu_tabs ? g_menu_tabs->count : 0;
	s->tab_name  = g_menu_tabs ? g_menu_tabs->name : NULL;
	s->row_mark = (void *)g_row_mark;
	s->header = (void *)g_menu_header;
	s->header_h = g_menu_header_h;
	s->side = (void *)g_menu_side;
	s->side_w = g_menu_side_w;
	s->pin_foot = g_menu_pin_foot;
	if (panel_is_tiles(kind) || kind == PANEL_CONFIRM) {
		int i;

		for (i = 0; i < n && i < MENUSNAP_ROWS; i++)
			s->marks[i] = g_tile_marks && i < g_tile_marks_n ? g_tile_marks[i] : UI_GLYPH_COUNT;
	}
}

static void confirm_panel_draw(app *a, const char *heading, const menu_row *rows, const int *marks,
                               int sel, int width, bool live, mm_pose pose);

/* The panel on screen again, at `pose`, Zyper going on from where he was:
 * on screen in this frame too. */
void snap_draw(app *a, mm_pose pose)
{
	const zr_rig *z = g_menu_zyper;
	unsigned at = g_menu_zyper_at;
	const menu_snap *s = g_snap;

	if (!s->ok || !(pose.alpha > 0.0f)) return;
	g_snap->frame = g_frame_no;
	if (panel_is_tiles(s->kind)) {
		tiles_panel(a, s->kind, s->heading, s->rows, s->marks, s->n, s->sel, pose);
		return;
	}
	if (s->kind == PANEL_HOT) {
		hot_panel(a, s->heading, s->rows, s->n, pose);
		return;
	}
	if (s->kind == PANEL_P2) {
		p2_panel(a, s->heading, s->rows, s->n, s->accent, pose);
		return;
	}
	if (s->kind == PANEL_LINK) {
		link_panel(a, s->heading, s->rows, s->n, s->sel, s->accent, pose);
		return;
	}
	if (s->kind == PANEL_MOVE) {
		move_page(a, s->heading, s->rows, s->n, pose);
		return;
	}
	if (s->kind == PANEL_CONFIRM) {
		confirm_panel_draw(a, s->heading, s->rows, s->marks, s->sel, s->fixed_w, false, pose);
		return;
	}
	static menu_tabs rest;
	const menu_tabs *tabs = g_menu_tabs;

	g_menu_zyper = s->zyper ? &s->zy : NULL;
	g_menu_zyper_at = anim_now() + s->zy_ahead;
	/* the tab row as it was, at rest, with the names of its own set */
	rest = (menu_tabs){ s->tab, s->tab, 0, 0, s->tab_count, s->tab_name };
	g_menu_tabs = s->tab >= 0 && s->tab_name ? &rest : NULL;
	{
		void (*mark)(app *, const menu_row *, int, int, float) = g_row_mark;
		void (*head)(app *, const SDL_Rect *) = g_menu_header;
		void (*side)(app *, const menu_side *) = g_menu_side;
		int head_h = g_menu_header_h, side_w = g_menu_side_w;
		bool pin = g_menu_pin_foot;

		g_row_mark = (void (*)(app *, const menu_row *, int, int, float))s->row_mark;
		g_menu_header = (void (*)(app *, const SDL_Rect *))s->header;
		g_menu_header_h = s->header_h;
		g_menu_side = (void (*)(app *, const menu_side *))s->side;
		g_menu_side_w = s->side_w;
		g_menu_pin_foot = s->pin_foot;
		g_snap_drawing = true;
		menu_panel(a, s->heading, s->rows, s->n, s->sel, s->fixed_w, s->accent,
		           s->has_vcolors ? s->vcolors : NULL, s->visits_all, s->loop_at,
		           s->note, pose);
		g_snap_drawing = false;
		g_row_mark = mark;
		g_menu_header = head;
		g_menu_header_h = head_h;
		g_menu_side = side;
		g_menu_side_w = side_w;
		g_menu_pin_foot = pin;
	}
	g_menu_tabs = tabs;
	g_menu_zyper = z;
	g_menu_zyper_at = at;
}

/* The level of the panel on screen - the one drawn last, if nothing has
 * begun a frame since - or 0 for none. */
int snap_shown(void)
{
	return g_snap->ok && g_snap->frame == g_frame_no ? g_snap->level : 0;
}

/* Level `lv`'s own panel, kept as it waited, is now the one on screen; or,
 * with no copy of it, none is. The level of the panel on screen. */
static int snap_promote(int lv)
{
	menu_snap *t;

	if (!g_snap_next->ok || g_snap_next->level != lv) {
		g_snap->ok = false;
		return 0;
	}
	t = g_snap;
	g_snap = g_snap_next;
	g_snap_next = t;
	g_snap->frame = g_frame_no;
	return lv;
}

mm_frame g_pframe = { { 1.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f, false };

/* Every screen's panel, a menu's or the sequence card's. Until its own turn
 * comes, the panel on screen before it is drawn leaving in its place, and its
 * own kept aside; from then on its own is drawn, where the motion says, and
 * kept as the panel on screen. */
void panel_show(app *a, int kind, const char *heading,
                const menu_row *rows, int n, int sel, int fixed_w,
                unsigned accent, const unsigned *vcolors,
                bool visits_all, int loop_at, const char *heading_note)
{
	mm_frame f = g_pframe;

	g_pframe = PANEL_REST;
	if (!(f.panel.alpha > 0.0f)) {
		snap_draw(a, f.before);
		snap_record(g_snap_next, kind, heading, rows, n, sel, fixed_w, accent,
		            vcolors, visits_all, loop_at, heading_note);
		return;
	}
	snap_record(g_snap, kind, heading, rows, n, sel, fixed_w, accent, vcolors,
	            visits_all, loop_at, heading_note);
	if (panel_is_tiles(kind)) tiles_panel(a, kind, heading, rows, g_snap->marks, n, sel, f.panel);
	else if (kind == PANEL_HOT) hot_panel(a, heading, rows, n, f.panel);
	else if (kind == PANEL_P2) p2_panel(a, heading, rows, n, accent, f.panel);
	else if (kind == PANEL_LINK) link_panel(a, heading, rows, n, sel, accent, f.panel);
	else if (kind == PANEL_MOVE) move_page(a, heading, rows, n, f.panel);
	else if (kind == PANEL_CONFIRM) confirm_panel_draw(a, heading, rows, g_snap->marks, sel, fixed_w, true, f.panel);
	else menu_panel(a, heading, rows, n, sel, fixed_w, accent, vcolors,
	                visits_all, loop_at, heading_note, f.panel);
}

/* A question in tiles through panel_show: its style as the kind
 * (PANEL_TILES, PANEL_TILES_GRID or PANEL_LIST) and each tile's mark, a
 * ui_glyph in the rows' order (`nmarks` of them; UI_GLYPH_COUNT for none).
 * The rows are the tiles and then tiles_panel's notes. */
void tiles_show(app *a, int kind, const char *heading, const menu_row *rows, int n,
                const int *marks, int nmarks, int sel)
{
	g_tile_marks = marks;
	g_tile_marks_n = nmarks;
	panel_show(a, kind, heading, rows, n, sel, 0, MENU_ZYPEROS, NULL, false, 0, NULL);
	g_tile_marks = NULL;
	g_tile_marks_n = 0;
}

/* Existing panels have no heading caption and keep their exact layout. */
void menu_draw_ex(app *a, const char *heading, const menu_row *rows,
                  int n, int sel, int fixed_w, unsigned accent,
                  const unsigned *vcolors, bool visits_all, int loop_at)
{
	panel_show(a, PANEL_MENU, heading, rows, n, sel, fixed_w, accent, vcolors,
	           visits_all, loop_at, NULL);
}

/* A panel with a caption at the heading's right, `heading_note` - About's
 * page count - and otherwise menu_draw_ex's: it opens, closes and is kept
 * for the next screen to draw leaving as every menu's panel is. */
void menu_draw_caption(app *a, const char *heading, const menu_row *rows,
                       int n, int sel, int fixed_w, unsigned accent,
                       const unsigned *vcolors, bool visits_all, int loop_at,
                       const char *heading_note)
{
	panel_show(a, PANEL_MENU, heading, rows, n, sel, fixed_w, accent, vcolors,
	           visits_all, loop_at, heading_note);
}

void menu_draw(app *a, const char *heading, const menu_row *rows, int n,
               int sel, int fixed_w, unsigned accent)
{
	menu_draw_ex(a, heading, rows, n, sel, fixed_w, accent, NULL, false, 0);
}

void menu_draw_styled(app *a, const menu_style *st, const char *heading,
                      const menu_row *rows, int n, int sel)
{
	g_menu_tabs = st->tabs;
	g_row_mark = st->row_mark;
	g_menu_header = st->header;
	g_menu_header_h = st->header_h;
	g_menu_side = st->side;
	g_menu_side_w = st->side_w;
	g_menu_pin_foot = st->pin_foot;
	if (st->row_icon) g_row_icon = st->row_icon;
	menu_draw_ex(a, heading, rows, n, sel, st->fixed_w,
	             st->follow_tint ? a->tint : st->accent, st->vcolors, st->visits_all, 0);
	if (st->row_icon) g_row_icon = NULL;
	g_menu_side = NULL;
	g_menu_side_w = 0;
	g_menu_pin_foot = false;
	g_menu_header = NULL;
	g_row_mark = NULL;
	g_menu_tabs = NULL;
}

/* A one-line message, DESIGN.md 11.8: a panel holding one row, its text
 * **text-soft** - the message is the panel's content, and a failure is not
 * quieter than a success. The row's `live` is the caller's and is kept, for
 * whether a long message scrolls. */
void menu_message(app *a, const char *heading, menu_row row,
                  unsigned accent)
{
	static const unsigned msg[1] = { VCOL_MESSAGE };

	menu_draw_ex(a, heading, &row, 1, -1, 0, accent, msg, false, 0);
}

/* Video snaps come with a lookup for a game whose system has its videos on
 * (MENU > Scraper's Systems, snaps_get), and which Videos for takes: every
 * game, the favorites, or the games played (src/snaps.h). A favorite and a
 * game's play time are kept by its system's tag, so the systems a run
 * covers are handed here as it starts (g_snap_sys); without them only "all"
 * gets videos. The play times are folded once a run (stats_summarize). */
const systems_cfg *g_snap_sys;
/* Videos out of the run going now (the Downloads page's Videos row): none
 * come, whatever each system's switch says */
bool g_snap_run_off;

static bool snap_wanted(const char *folder, const char *file)
{
	const char *tag = NULL;
	int which, i;
	long secs = 0, last = 0;

	if (g_snap_run_off || !snaps_get(folder)) return false;
	if ((which = snaps_for()) == SNAPS_FOR_ALL) return true;
	for (i = 0; g_snap_sys && i < g_snap_sys->count; i++)
		if (!strcmp(g_snap_sys->systems[i].folder, folder)) { tag = g_snap_sys->systems[i].tag; break; }
	if (!tag) return false;
	if (which == SNAPS_FOR_PLAYED && !(stats_lookup(tag, file, &secs, &last) && (secs > 0 || last > 0)))
		return false;
	return snaps_for_game(which, fav_is(tag, file), which == SNAPS_FOR_PLAYED);
}

/* The rule back after a game's own fetch asked for its video alone
 * (Downloads' game panel) */
void ss_run_videos_default(void)
{
	ss_run_videos(snap_wanted);
}

/* MENU > Scraper's value, from the last count of missing covers: taken as
 * its page closes (scraper_page), never per frame */
char g_scraper_val[32];

/* The two menus behind MENU, their row indices and their buffers: src/sys_menu.h */

/* Build whichever menu the current screen calls for. Returns the row count, so
 * the input loop never needs to know which of the two it is driving. */
/* One answer per two seconds, for every row that needs it.
 *
 * wifi_status forks wpa_cli, and this launcher is a 119 MB process - forking it
 * copies page tables and costs about thirty milliseconds, not the seven a small
 * process pays. menu_build runs every frame, and the ZyperOS menu asked three
 * times: once cached for the Wi-Fi row, then twice more, uncached, for Over The
 * Hare and Box Art. Measured on the device that was 12 fps with the menu open,
 * against a loop that delays 8 ms - two thirds of every frame spent forking.
 *
 * The cache existed and said why; the two rows below it defeated it. So the
 * cache moves out here where it covers all of them, and nothing calls
 * wifi_status directly from a per-frame path again. */
wifi_state menu_wifi(char *ssid, int cap)
{
	static unsigned   next;
	static wifi_state st = WIFI_OFF;
	static char       name[WIFI_SSID_MAX];
	unsigned now = plat_now_ms();

	if (next == 0 || now >= next) {
		st = wifi_status(name, sizeof name, NULL, 0);
		next = now + 2000;
		g_wifi_asked_at = now;
	}
	if (ssid && cap) snprintf(ssid, (size_t)cap, "%s", name);
	return st;
}

/* A way out to the internet, for every row and page that fetches from it
 * (box art, a game's cover and video, System Update, achievements,
 * PortMaster): Wi-Fi joined, or a default route out on another link, a
 * phone's USB tethering among them (phonelink_way_out; the Pixel 9 Pro XL's,
 * 2026-11-13). Never the USB cable's link to a computer, which leads nowhere.
 * *metered: the way out is the phone's, so its mobile data, likely: nothing
 * large starts by itself over it. Read again at most twice a second, as the
 * menus ask every frame (menu_wifi's cache keeps Wi-Fi's cheap). */
bool net_internet(bool *metered)
{
	static unsigned at;
	static bool out, meter;
	unsigned now = plat_now_ms();

	if (!at || now - at >= 500u) {
		char route[4096];     /* about 30 routes; a line is 128 bytes */
		FILE *f = fopen("/proc/net/route", "r");
		size_t n = f ? fread(route, 1, sizeof route - 1, f) : 0;

		if (f) fclose(f);
		route[n] = '\0';
		out = phonelink_way_out(menu_wifi(NULL, 0) == WIFI_CONNECTED, route, &meter);
		at = now ? now : 1;
	}
	if (metered) *metered = out && meter;
	return out;
}

/* USB File Transfer has a link without Wi-Fi: the cable to a computer, or a
 * phone's tethering that has its address (only looked at: phone_link_due
 * set it up). MENU's build and the lists that name its rows ask the same. */
bool xfer_link_up(void)
{
	char pip[64];

	return plat_cable_link(NULL, 0) || (plat_phone_link(false, NULL, 0, pip, sizeof pip) && pip[0]);
}

/* Gather what the device currently is, and hand it to sys_menu_build.
 *
 * The split: this half is allowed to fork wpa_cli and read the app, the other
 * half is not allowed to do anything but produce rows. That is what lets
 * tools/menu-check.c state a whole device situation in a struct initializer
 * and ask what the menu says about it.
 *
 * Returns the row count, so the input loop never needs to know which of the
 * two menus it is driving. */
/* ---- System Update (src/sysupdate.h, docs/updates.md) -------------------
 *
 * The work is the update program's, zyperos-update in the ZyperOS folder:
 * this side starts it, reads the status file it keeps in .userdata/<device>/
 * update, and draws that. The program goes on when the screen is left. */

void up_path(char *buf, size_t n, const char *name)
{
	snprintf(buf, n, "%s/update/%s", P_USERDATA, name);
}

/* The card has the program: a card from before System Update has no row */
bool up_have(void)
{
	char p[512];
	snprintf(p, sizeof p, "%s/zyperos-update", P_ROOT);
	return access(p, X_OK) == 0;
}

/* At work now: it holds the lock file for as long as it runs */
bool up_running(void)
{
	char p[512];
	int fd;
	bool busy;

	up_path(p, sizeof p, "lock");
	fd = open(p, O_RDWR);
	if (fd < 0) return false;
	busy = flock(fd, LOCK_EX | LOCK_NB) != 0;
	close(fd);
	return busy;
}

bool up_read(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f) { buf[0] = 0; return false; }
	got = fread(buf, 1, n - 1, f);
	fclose(f);
	buf[got] = 0;
	return true;
}

void up_first_line(const char *path, char *buf, size_t n)
{
	up_read(path, buf, n);
	buf[strcspn(buf, "\r\n")] = 0;
}

/* A network to look with: a way out (net_internet: Wi-Fi, or a phone's
 * tethering), or a test release's source set on the card (docs/updates.md),
 * which may be on the cable */
static bool up_network(void)
{
	char p[512];

	if (net_internet(NULL)) return true;
	up_path(p, sizeof p, "source");
	return access(p, R_OK) == 0;
}

static su_view g_up;
unsigned g_up_read_at;

/* Everything the screen shows, read again at most four times a second: the
 * build asks every frame, and these files are on the card */
const su_view *up_view(bool now)
{
	unsigned t = plat_now_ms();
	char p[512], text[1024], sysv[64] = "";

	if (!now && g_up_read_at && t - g_up_read_at < 250) return &g_up;
	g_up_read_at = t ? t : 1;
	memset(&g_up, 0, sizeof g_up);
	up_path(p, sizeof p, "status");
	up_read(p, text, sizeof text);
	su_status_parse(text, &g_up.st);
	g_up.running = up_running();
	g_up.wifi = up_network();
	/* Over a phone's mobile data a download cut short waits for A
	 * (su_auto_step) */
	{
		bool metered = false;

		g_up.metered = net_internet(&metered) && metered;
	}
	snprintf(p, sizeof p, "%s/VERSION", P_ROOT);
	up_first_line(p, g_up.installed, sizeof g_up.installed);
	/* Roll Back only to a version the image would not copy straight over
	 * (update/apply.sh's rule): not older than the image's own */
	snprintf(p, sizeof p, "%s/ZyperOS.prev/VERSION", P_CARD);
	up_first_line(p, g_up.prev, sizeof g_up.prev);
	up_read("/etc/zyperos/system", text, sizeof text);
	sscanf(text, "version %63s", sysv);
	if (g_up.prev[0] && sysv[0] && su_version_cmp(g_up.prev, sysv) < 0) g_up.prev[0] = 0;
	/* On a card with two system slots, Roll Back goes to the system in the
	 * other slot (zyperos-update rollback decides; it says which) */
	if (g_up.st.other[0]) snprintf(g_up.prev, sizeof g_up.prev, "%s", g_up.st.other);
	up_path(p, sizeof p, "result");
	up_first_line(p, g_up.result, sizeof g_up.result);
	up_path(p, sizeof p, g_up.st.system ? "latest-system.txt" : "latest.txt");
	{
		FILE *f = fopen(p, "r");
		char line[256];
		while (f && fgets(line, sizeof line, f))
			if (!strncmp(line, "note ", 5)) { g_up.notes = true; break; }
		if (f) fclose(f);
	}
	if (!plat_battery(&g_up.battery, &g_up.charging)) { g_up.battery = 100; g_up.charging = true; }
	/* The card's free room beside what the update needs, while it says
	 * so: the page shows it as it changes */
	if (g_up.st.state == SU_SPACE) {
		struct statvfs vfs;

		if (!statvfs(P_CARD, &vfs)) g_up.card_free = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
	}
	if (g_up.st.time > 0) {
		time_t tt = (time_t)g_up.st.time;
		struct tm tm;
		localtime_r(&tt, &tm);
		strftime(g_up.checked, sizeof g_up.checked, g_clock24 ? "%H:%M" : "%I:%M %p", &tm);
		if (g_up.checked[0] == '0' && !g_clock24) memmove(g_up.checked, g_up.checked + 1, strlen(g_up.checked));
	}
	return &g_up;
}

/* At work on an update: downloading, checking what it fetched, copying the
 * folder or writing the system. A look (checking) is not: it is one small
 * request, and Auto Off may come. */
bool up_working(void)
{
	const su_view *v;

	if (!up_have()) return false;
	v = up_view(false);
	if (!v->running) return false;
	return v->st.state == SU_DOWNLOADING || v->st.state == SU_COPYING ||
	       v->st.state == SU_VERIFYING || v->st.state == SU_INSTALLING;
}

/* Start the program: check, download or rollback, on its own, as the device
 * name of the card's .userdata folder */
void up_spawn(const char *cmd)
{
	char prog[512], dev[64];
	const char *slash = strrchr(P_USERDATA, '/');
	char *argv[] = { prog, (char *)cmd, "--card", (char *)P_CARD, "--device", dev, NULL };

	snprintf(prog, sizeof prog, "%s/zyperos-update", P_ROOT);
	snprintf(dev, sizeof dev, "%s", slash ? slash + 1 : P_USERDATA);
	fprintf(stderr, "update: %s\n", cmd);
	plat_spawn_detached(argv, NULL, P_ROOT);
	g_up_read_at = 0;
}

/* System Update's news, said once per release (DESIGN.md 11.17): a new
 * release a look found, and a download done in the background. Zyper's
 * bubble on the shelf (the heat pop-up's, with these words), or a notice
 * over a game; one that comes over a menu waits for the shelf. */
static char g_up_news[2][160];

static void up_news(const char *key, const char *fmt, const char *version, bool in_game)
{
	char told[40] = "";

	db_get_str(db_dev(), key, told, sizeof told, "");
	if (!version[0] || !strcmp(told, version)) return;
	db_set_str(db_dev(), key, version);
	snprintf(g_up_news[0], sizeof g_up_news[0], fmt, version);
	snprintf(g_up_news[1], sizeof g_up_news[1], "%s", T(S_UPDATE_NEWS_WHERE));
	fprintf(stderr, "update: news: %s\n", g_up_news[0]);
	if (in_game) {
		nq_item it = { .kind = NQ_SET, .ms = 5000 };

		snprintf(it.head, sizeof it.head, "%s", T(S_SYSMENU_UPDATE));
		snprintf(it.body, sizeof it.body, "%s", g_up_news[0]);
		nq_push(&g_nq, &it);
		g_up_news[0][0] = '\0';
	}
}

/* This start reached the home: plastron's start marker on the card goes
 * (its crashlog makes it at every start, and reports one it finds at the
 * next, when the RAM kept nothing: src/lastboot.h). An unlink and no sync:
 * the kernel writes it back within seconds, and none of it is on the
 * frame's way. None on a computer, nor on a handheld without crashlog. */
static void start_reached_home(void)
{
	char p[512];

	snprintf(p, sizeof p, "%s/.plastron/starting.txt", P_CARD);
	if (unlink(p) == 0) fprintf(stderr, "start: the home reached, the start's marker removed\n");
}

/* The last start's restart (src/lastboot.h, DESIGN.md 11.70): this start's
 * summary read against the card's copy the first time the shelf comes round;
 * when it is new, said in Zyper's bubble as soon as nothing else of his is up,
 * and the card's copy written then, so it is said once. 0 not read yet, 1 to
 * be said, 2 said or nothing to say. */
static int      g_lb_state;
static lastboot g_lb_say;

static void lastboot_watch(void)
{
	char path[512], code[48];

	snprintf(path, sizeof path, "%s/%s", P_USERDATA, LASTBOOT_FILE);
	if (g_lb_state == 0) {
		const char *run_path = getenv("ZYPEROS_LASTBOOT_RUN");
		lastboot run, kept;

		g_lb_state = 2;
		if (!lastboot_read(run_path && *run_path ? run_path : LASTBOOT_RUN, &run)) return;
		lastboot_read(path, &kept);
		if (!lastboot_due(&run, &kept, (long)time(NULL), &g_lb_say)) {
			fprintf(stderr, "lastboot: %s%c said before\n", run.stage, run.reason);
			return;
		}
		g_lb_state = 1;
	}
	if (g_lb_state != 1 || g_heatpop.on || g_songpop.on) return;
	heatpop_show(plat_now_ms());
	lastboot_bubble(&g_lb_say);
	lastboot_code(&g_lb_say, code, sizeof code);
	fprintf(stderr, "lastboot: said %s (%s), %s\n", code, g_lb_say.time[0] ? g_lb_say.time : "no date",
	        lastboot_write(path, &g_lb_say) ? "kept for About" : "not kept: the card's copy would not write");
	g_lb_state = 2;
}

/* The shelf's pass: the news up in Zyper's bubble, when there is some and
 * nothing else of his is up */
static void up_news_watch(void)
{
	unsigned now = plat_now_ms();

	if (!g_up_news[0][0] || g_heatpop.on || g_songpop.on) return;
	heatpop_show(now);
	snprintf(g_heatpop.line1, sizeof g_heatpop.line1, "%s", g_up_news[0]);
	snprintf(g_heatpop.line2, sizeof g_heatpop.line2, "%s", g_up_news[1]);
	g_up_news[0][0] = '\0';
}

/* Unlocks that had waited - earned offline - on the account now: said once,
 * in Zyper's bubble on the shelf, as the update's news is. Counted up until
 * it is said, so two passes close together make one notice. */
static int g_ra_news;

static void ra_sent_news(int n)
{
	if (n <= 0) return;
	g_ra_news += n;
	fprintf(stderr, "ra: %d waiting unlock%s on the account now\n", n, n == 1 ? "" : "s");
}

static void ra_news_watch(void)
{
	unsigned now = plat_now_ms();

	if (g_ra_news <= 0 || g_heatpop.on || g_songpop.on || g_up_news[0][0]) return;
	heatpop_show(now);
	snprintf(g_heatpop.line1, sizeof g_heatpop.line1, TN(S_CHEEVOS_SENT, g_ra_news), g_ra_news);
	snprintf(g_heatpop.line2, sizeof g_heatpop.line2, "%s", T(S_CHEEVOS_SENT_WHERE));
	g_ra_news = 0;
}

/* A download that went on behind the screens has ended well: said once, in
 * Zyper's bubble on the shelf ("Downloads done", "41 covers · 150 achievement
 * sets"), as the update's news is; and the side lights' done (dlq's lanes) */
static void dl_news_watch(void)
{
	char l1[96], l2[160];

	if (g_heatpop.on || g_songpop.on || g_up_news[0][0]) return;
	if (!downloads_news(l1, sizeof l1, l2, sizeof l2)) return;
	heatpop_show(plat_now_ms());
	snprintf(g_heatpop.line1, sizeof g_heatpop.line1, "%s", l1);
	snprintf(g_heatpop.line2, sizeof g_heatpop.line2, "%s", l2);
}

/* The update's own clock, every 2 s, from every screen's loop through
 * tick_theme and from a game's tick: su_auto_step's looks and resumes
 * (src/sysupdate.h), and the news, once per release */
void tick_update_daily(bool in_game)
{
	static unsigned last;
	static su_auto au;
	unsigned t = plat_now_ms();
	const su_view *v;

	if (last && t - last < 2000) return;
	last = t ? t : 1;
	if (t - t_boot0 < 30000 || !up_have()) return;
	v = up_view(true);
	if (v->st.state == SU_UNSUPPORTED) return;
	/* The news: a release found, and one ready */
	if (!v->running && v->st.state == SU_AVAILABLE)
		up_news("update.told", T(S_UPDATE_NEWS_AVAILABLE), v->st.version, in_game);
	if (!v->running && v->st.state == SU_READY)
		up_news("update.ready_told", T(S_UPDATE_NEWS_READY), v->st.version, in_game);
	switch (su_auto_step(&au, v, t, (long long)time(NULL))) {
	case SU_AUTO_RESUME:
		fprintf(stderr, "update: the download was cut short: going on\n");
		up_spawn("download");
		break;
	case SU_AUTO_CHECK:
		up_spawn("check");
		break;
	default:
		break;
	}
}


/* ---------- MENU > Random Game -------------------------------------------- */

/* A game picked (src/randgame.h): from every system on the systems row's
 * menu, from the shelf's own games in a system's menu. False, with deny
 * heard, when there is none to pick. The menu closes on a pick and
 * pick_launch shows it and starts it. */
bool random_pick(app *a, bool all, int *shelf, int *game)
{
	/* The game picked last time, so the next is another */
	static int last_shelf = -1, last_game = -1;
	int count[CFG_MAX_SYSTEMS], i;
	bool use[CFG_MAX_SYSTEMS];

	for (i = 0; i < a->sys.count; i++) {
		const system_cfg *s = &a->sys.systems[i];

		/* Every game of the system, in its folders too: the pick is an
		 * index of sys_games, which pick_launch opens the folder of */
		count[i] = sys_games(&a->view[i])->count;
		/* Not MP4, whose cards are albums, nor - from every system -
		 * Favorites, whose games are on their own shelves too */
		use[i] = all ? !is_mp4(s) && !a->view[i].owner && !pm_only_offer(a, i)
		             : i == a->sys_cursor && !is_mp4(s);
	}
	/* Never an entry of a shelf's own that is no game (Splore's, PortMaster's
	 * while it is not on the card): picked again, a few times. By its own
	 * system's, on Recents too, where Splore is once it was opened */
	for (i = 0;; i++) {
		const game_list *gl;
		int own;

		if (i >= 8 || !rg_pick(count, use, a->sys.count, rg_random(), last_shelf, last_game,
		                       shelf, game)) {
			uisound_play(DS_SOUND_DENY);
			return false;
		}
		gl = sys_games(&a->view[*shelf]);
		if (*game < 0 || *game >= gl->count) break;
		own = a->view[*shelf].all ? *shelf : shelf_owner(a, *shelf, *game);
		if (!splore_is_entry(a, own, &gl->items[*game]) && !pm_is_offer(a, own, &gl->items[*game]))
			break;
	}
	last_shelf = *shelf;
	last_game = *game;
	uisound_play(DS_SOUND_REWARD);
	return true;
}

/* ---------- the WiFi screen ----------------------------------------------- */

/* One frame with a single line on it, drawn before each blocking call.
 *
 * wifi_up can take twenty seconds waiting out the stock service's retry loop,
 * a scan takes several, and an association takes up to twenty more. None of
 * that is asynchronous here, so the loop is not running and input is not read
 * while it happens. Painting the reason first is the difference between a
 * device that is working and a device that has hung: the picture is identical
 * otherwise, and the second reading is the one that gets a power cycle. */
/* One line on a panel over the shelf, for the moments something is happening
 * and there is nothing yet to show. The heading is a parameter because it was
 * not: this began as the Wi-Fi screen's own and said "Wi-Fi" over every
 * message it was given, so signing in to RetroAchievements and fetching a set
 * both announced themselves as Wi-Fi. */
void wait_panel(app *a, const char *heading, const char *msg)
{
	menu_row row = { msg, NULL, false };

	tick_theme(a);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_message(a, heading, row, MENU_ZYPEROS);
	plat_draw_osd(a->r);
	plat_present();
}

/* MENU CLOSES THE WHOLE MENU, from any depth in it. B is back one level.
 * Eric's call, 2026-09-23: MENU used to mean exactly what B meant, so from
 * Play Time or Wi-Fi it went back one screen instead of out, the same button
 * doing one thing to open the menu and another inside it.
 *
 * A menu screen that sees MENU raises this and leaves; every screen under it
 * then leaves on its next pass, until the one MENU opened is gone. It is put
 * down again only OUTSIDE the menus - the shelf's loop, game_menu, MP4 - and
 * never when a menu opens: a screen that opens a second one after the first
 * came back has to see it still up, or that second one would stay open. */
bool g_menu_closing;
/* The in-game menu is up, over a paused game (game_menu): nothing may close
 * it from outside, as a plugged-in computer closes the others */
bool g_in_game_menu;

/* ---- the interface sounds' hooks (DESIGN.md 9, src/uisound.h) ----------
 *
 * Each hook is one call where a press is decided, so the rule stays in
 * src/uisound.c and a screen only says what happened. */

/* Whether the last step was held against an end. One for the whole launcher:
 * input is flushed whenever a screen opens, so a hold never carries from one
 * list to another. */
uis_steps g_steps;

/* A step along a list or shelf. Wrapping is normal movement. */
void step_sound(app *a, in_button b, int from, int to)
{
	bool moved = from != to;

	uisound_cue(uisound_step(&g_steps, moved,
	                         !a->in.pressed[b]));
}

/* A list kept as a plain index (0 to n - 1) by a screen with its own loop,
 * stepped by the rule every menu has (menu_step_index) and heard */
int list_step_heard(app *a, in_button b, int n, int sel)
{
	int to = menu_step_index(n, sel, b == IN_UP ? -1 : +1, a->in.pressed[b]);

	step_sound(a, b, sel, to);
	return to;
}

/* A step that always lands somewhere new, and never goes round: a track
 * skipped, a seek. */
void moved_sound(app *a, in_button b)
{
	step_sound(a, b, 0, 1);
}

/* A step along a menu, heard: a fresh press goes round past either end, a
 * held direction stops at the end (DESIGN.md 11.3, my pick 2026-11-12), so
 * a thumb left on the d-pad never runs the list past where it wanted. */
int menu_step_heard(app *a, const menu_row *rows, int n, int sel,
                    int dir, in_button b)
{
	int to = a->in.pressed[b] ? menu_step_round(rows, n, sel, dir)
	                          : menu_step_sel(rows, n, sel, dir);

	step_sound(a, b, sel, to);
	return to;
}

int menu_step_quiet(app *a, const menu_row *rows, int n, int sel,
                    int dir, in_button b)
{
	int to = a->in.pressed[b] ? menu_step_round(rows, n, sel, dir)
	                          : menu_step_sel(rows, n, sel, dir);

	if (to == sel) step_sound(a, b, sel, to);
	return to;
}

/* MENU was pressed in a screen over this one: every screen is closing */
bool menu_closing(void)
{
	return g_menu_closing;
}

/* True when a menu screen should leave: B, MENU, or a MENU pressed deeper in.
 * The press that leaves is answered with back; the screens under it, which
 * leave on the flag with the press already flushed, are silent. */
bool menu_leaving(app *a)
{
	if (a->in.pressed[IN_MENU]) g_menu_closing = true;
	if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU])
		uisound_play(DS_SOUND_BACK);
	return a->in.pressed[IN_BACK] || g_menu_closing;
}

/* ---- a screen with a panel, opening and closing (DESIGN.md 8.2) ---------
 *
 * Every loop that draws a menu's panel goes through these, so none of them
 * carries a copy of the motion: panel_open as the screen opens, panel_frame
 * before each frame it draws, panel_close on its way out. Whatever it goes
 * back to draws its panel leaving: the screen under it in its own next
 * frames (menu_draw_ex), the shelf and MP4's shelf after theirs
 * (panel_leave_draw), the paused game before it is handed back
 * (panel_leave_run). src/menumotion.h has the rules.
 *
 * Nothing waits on any of it. Input is read and acted on as it always was,
 * and a press puts the panel where it belongs on the frame it is acted on. */

/* A press acting on level `lv`'s screen now (mm_settle): when `lv` was
 * still waiting for the panel on screen to leave, its own, kept as it waited,
 * is the panel on screen from now. The level of the panel on screen. */
int panel_settle(int lv)
{
	int shown = snap_shown(), on = mm_settle(lv, anim_now(), shown);

	return on != shown ? snap_promote(on) : on;
}

/* The screen opens, over the one on top, which the press that opened it
 * acted on (panel_settle). Its level, for the others. What an earlier screen
 * at this depth kept as it waited is none of this one's. */
int panel_open(void)
{
	int lv = mm_open(anim_now(), panel_settle(mm_depth()) > 0);

	g_snap_next->ok = false;
	return lv;
}

/* Before a frame of level `lv`'s screen. False when the frame is not to be
 * drawn at all: while MENU closes every screen, the ones under the screen it
 * was pressed in leave without drawing again, and the panel on screen leaves
 * once, over the shelf. Otherwise a press this frame puts the panel at rest,
 * the next menu_draw_ex draws it where the motion says, and `*scrim` is the
 * scrim's strength for the backdrop under it. */
bool panel_frame(app *a, int lv, float *scrim)
{
	int b;

	if (g_menu_closing) return false;
	for (b = 0; b < IN_COUNT; b++)
		if (a->in.pressed[b]) { mm_press(lv); break; }
	g_pframe = mm_at(lv, anim_now());
	*scrim = g_pframe.scrim;
	return true;
}

/* Level `lv`'s screen has gone, and the panel on screen leaves over what
 * comes next - unless `cut`: quit, power, a game stopped leave nothing to see
 * go. The press that closed it is not handed on to the screen under it,
 * which would take it for its own and put its panel at rest. */
void panel_close_at(app *a, int lv, bool cut, unsigned now)
{
	int shown = cut ? 0 : snap_shown();

	/* A deeper panel is on screen: the screen came back from a submenu and
	 * was still waiting for it to leave, or MENU closed the screens over it
	 * without drawing them again. The press that closes it acts on it. */
	if (shown > lv) shown = panel_settle(lv);
	mm_close(lv, now, shown, g_menu_closing);
	g_snap_next->ok = false;
	memset(a->in.pressed, 0, sizeof a->in.pressed);
	if (mm_leaving(anim_now()).moving) redraw_now();
}

void panel_close(app *a, int lv, bool cut)
{
	panel_close_at(a, lv, cut, anim_now());
}

/* MP4's shelf, which has no panel and is drawn over everything: the panel on
 * screen leaves over it, and is given back as it opened when it closes.
 * Nothing MP4 does closes the menu under it (mp4_open), MENU included. */
int panel_base(void)
{
	return mm_base(anim_now(), panel_settle(mm_depth()) > 0);
}

void panel_base_close(int lv)
{
	mm_close(lv, anim_now(), 0, false);
}

/* MENU closed the screens over the one on top, and stops there: MP4's
 * menu, which MENU closes and nothing under it (mp4_menu). The screen on top
 * comes back as after a back (mm_return). */
void panel_return(void)
{
	mm_return(mm_depth(), anim_now(), snap_shown());
	g_snap_next->ok = false;
}

/* After a base's own frame - the shelf's, MP4's shelf's - the last panel
 * closed onto it leaving, and its scrim, while they last. A press there has
 * acted on the base already, and takes them off at once. True while they
 * are going, for the caller to draw the next frame. */
bool panel_leave_draw(app *a, int game)
{
	mm_frame f;
	int b;

	for (b = 0; b < IN_COUNT; b++)
		if (a->in.pressed[b]) { mm_leaving_cut(); break; }
	f = mm_leaving(anim_now());
	if (!f.moving) return false;
	ui_scrim_k(a->r, game, f.scrim);
	snap_draw(a, f.before);
	return true;
}

/* The same over a base with no loop of its own to draw it in: the paused
 * game, once the in-game menu has closed and before the game is handed back
 * (game_menu). `backdrop` draws what is under it at a scrim's strength.
 *
 * It reads input, as every loop does, and nothing waits on it. Any press
 * ends it there and then, and the game goes on at once: what is pressed is
 * the game's, and Diatom starts reading its own buttons afresh on RESUME. The
 * power button and Auto Off it hands back to the caller, which owns what they
 * mean over a paused game (gm_power): once the game is resumed the wait loop
 * drains the power key's queue as stale, and a press read here and not acted
 * on would be lost. */
leave_end panel_leave_run(app *a, void (*backdrop)(app *, void *, float),
                          void *ctx)
{
	leave_end how = LEAVE_GONE;
	mm_frame f;
	int b;

	for (;;) {
		plat_input_poll(&a->in);
		if (a->in.quit_requested || want_quit) {
			a->running = false;
			how = LEAVE_QUIT;
			break;
		}
		if (a->in.pressed[IN_POWER] || idle_due(a)) { how = LEAVE_POWER; break; }
		for (b = 0; b < IN_COUNT; b++)
			if (a->in.pressed[b]) { mm_leaving_cut(); break; }
		f = mm_leaving(anim_now());
		if (!f.moving) break;
		backdrop(a, ctx, f.scrim);
		snap_draw(a, f.before);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	mm_leaving_cut();
	return how;
}

/* ---- a question with two answers (DESIGN.md 11.3, src/confirmq.h) --------
 *
 * Every question the launcher asks with two answers is one of src/confirmq.h's
 * and is asked here, on src/ds/confirm.h's panel: the heading (the action),
 * the question wrapped to the panel, the two answers side by side, the safe
 * one on the left with the cursor on it, so a stray press of the button that
 * opened the panel cannot also confirm it. Left and Right move between them
 * and go round; a held direction stops at the end. A takes the answer under
 * the cursor and B the safe one, each pressing (**press**) before the panel
 * closes (**exit**); a press during it ends it at once. MENU, POWER and quit
 * leave with nothing done.
 *
 * Power is handled here rather than left to the caller. A confirm can sit on
 * screen indefinitely, which makes it exactly the kind of place the device
 * gets put down and the power button pressed. Over `over`'s backdrop, POWER
 * is its rule (the in-game menu's paused game, where POWER stops the game);
 * with a `guard` it is a question that came by itself (ask_settle): a button
 * down as it opened, or pressed in its first half second, counts only once
 * let go. */

/* The panel being drawn now: where its fill is, which answer presses and how
 * far, and when it opened (its question's scroll, the last resort). A copy of
 * the panel (snap_draw) is drawn at rest, its press kept in its marks. */
static float g_cq_fill;
static int g_cq_pressed = -1;
static float g_cq_press;
static unsigned g_cq_t0;

/* The press kept in a copy's first mark: the answer + 1 in the low bits, how
 * far in thousandths above them */
#define CQ_MARK_PRESS(i, k) ((i) < 0 ? 0 : ((i) + 1) | ((int)((k) * 1000.0f + 0.5f) << 4))

static bool confirm_pass_begin(void *ctx) { return panel_pass_begin(ctx); }
static void confirm_pass_end(void *ctx, const SDL_Rect *panel, mm_pose pose) { panel_pass_end(ctx, panel, pose); }

static ds_confirm confirm_of(const char *heading, const menu_row *rows, const int *marks, int sel,
                             int width)
{
	ds_confirm c = { heading, rows[0].label, { rows[1].label, rows[2].label },
	                 { (unsigned)marks[1], (unsigned)marks[2] }, width, sel, (float)sel, -1, 0.0f,
	                 anim_now() - g_cq_t0 };

	if (marks[0] & 15) {
		c.pressed = (marks[0] & 15) - 1;
		c.press = (float)(marks[0] >> 4) / 1000.0f;
	}
	return c;
}

static void confirm_panel_draw(app *a, const char *heading, const menu_row *rows, const int *marks,
                               int sel, int width, bool live, mm_pose pose)
{
	const ds_pass pass = { panel_scaled, confirm_pass_begin, confirm_pass_end, a };
	ds_confirm c = confirm_of(heading, rows, marks, sel, width);

	if (live) {
		c.fill = g_cq_fill;
		c.pressed = g_cq_pressed;
		c.press = g_cq_press;
	}
	ds_confirm_draw(a->r, &c, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, MENU_TOP, MENU_AVAIL_H, pose, &pass);
}

/* The question's rows and marks, as panel_show keeps them: the question, the
 * left answer, the right; B's glyph on the answer B gives, warning on the one
 * that loses something */
static void confirm_rows_of(cq_id q, const char *text, menu_row rows[3], int marks[3])
{
	const cq_def *d = cq_of(q);

	rows[0] = (menu_row){ text ? text : "", NULL, false };
	rows[1] = (menu_row){ T(d->left), NULL, true };
	rows[2] = (menu_row){ T(d->right), NULL, true };
	marks[0] = 0;
	marks[1] = d->flags & CQ_B_NONE ? 0 : DS_CONFIRM_B;
	marks[2] = d->flags & CQ_LOSES ? DS_CONFIRM_WARN : 0;
}

void confirm_show(app *a, cq_id q, const char *text, int sel)
{
	menu_row rows[3];
	int marks[3];

	confirm_rows_of(q, text, rows, marks);
	marks[0] = CQ_MARK_PRESS(g_cq_pressed, g_cq_press);
	{
		/* A question of two sentences is drawn as one; each said drawn to
		 * the host's check, which knows them apart */
		const char *one, *two;

		if (cq_parts(text, &one, &two)) {
			ui_fit_drawn(one);
			ui_fit_drawn(two);
		}
	}
	g_tile_marks = marks;
	g_tile_marks_n = 3;
	panel_show(a, PANEL_CONFIRM, T(cq_of(q)->heading), rows, 3, sel, menu_std_width(a), MENU_ZYPEROS,
	           NULL, false, 0, NULL);
	g_tile_marks = NULL;
	g_tile_marks_n = 0;
}

void confirm_lay(app *a, cq_id q, const char *text, ds_confirm_lay *l)
{
	menu_row rows[3];
	int marks[3];
	ds_confirm c;

	confirm_rows_of(q, text, rows, marks);
	c = confirm_of(T(cq_of(q)->heading), rows, marks, 0, menu_std_width(a));
	ds_confirm_layout(&c, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, MENU_TOP, MENU_AVAIL_H, l);
}

void confirm_preview(app *a, cq_id q, const char *text)
{
	g_cq_fill = 0.0f;
	g_cq_pressed = -1;
	g_cq_t0 = anim_now();
	confirm_show(a, q, text, 0);
}

/* The keys this pass, as src/confirmq.h's rule reads them: the strongest */
static cq_key confirm_key(app *a, bool *fresh)
{
	static const struct { in_button b; cq_key k; } DIRS[] = {
		{ IN_LEFT, CQK_LEFT }, { IN_RIGHT, CQK_RIGHT }, { IN_UP, CQK_UP }, { IN_DOWN, CQK_DOWN },
	};
	size_t i;

	*fresh = true;
	if (a->in.pressed[IN_POWER]) return CQK_POWER;
	if (a->in.pressed[IN_MENU]) return CQK_MENU;
	if (a->in.pressed[IN_BACK]) return CQK_B;
	if (a->in.pressed[IN_ACCEPT]) return CQK_A;
	for (i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++)
		if (in_repeat(&a->in, DIRS[i].b)) {
			*fresh = a->in.pressed[DIRS[i].b];
			return DIRS[i].k;
		}
	return CQK_NONE;
}

static in_button confirm_button(cq_key k)
{
	return k == CQK_LEFT ? IN_LEFT : k == CQK_RIGHT ? IN_RIGHT : k == CQK_UP ? IN_UP : IN_DOWN;
}

int confirm_ask_ex(app *a, cq_id q, const char *text, const menu_style *over, void *ctx,
                   usbpower_guard *guard)
{
	const cq_def *d = cq_of(q);
	ds_confirm_lay lay;
	cq_state st;
	tween fill;
	int lv = panel_open(), answer = CQ_NONE;
	unsigned press_t0 = 0;
	bool pressing = false, cut = false;
	float k;

	confirm_lay(a, q, text, &lay);
	st = cq_start(lay.side);
	tween_snap(&fill, 0.0f);
	g_cq_fill = 0.0f;
	g_cq_pressed = -1;
	g_cq_press = 0.0f;
	g_cq_t0 = anim_now();
	for (;;) {
		unsigned now;

		plat_input_poll(&a->in);
		if (guard) ask_settle(a, guard);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; answer = CQ_NONE; cut = true; break; }
		now = anim_now();
		if (pressing) {
			/* The answer pressing, then the panel goes: a press ends it at
			 * once (8.2, every motion is interruptible); POWER is POWER */
			bool any = false;
			int b;

			for (b = 0; b < IN_COUNT; b++) any |= a->in.pressed[b];
			if (a->in.pressed[IN_POWER]) {
				if (over && over->on_power) over->on_power(a, ctx);
				else power_off(a);
				answer = CQ_NONE;
				cut = true;
				break;
			}
			g_cq_press = ds_out((float)(now - press_t0) / (float)DS_MS_PRESS);
			if (any || now - press_t0 >= DS_MS_PRESS) {
				g_cq_press = 1.0f;
				break;
			}
		} else {
			bool fresh;
			cq_key key = idle_due(a) ? CQK_POWER : confirm_key(a, &fresh);
			cq_step s;

			if (key == CQK_POWER) {
				if (over && over->on_power) over->on_power(a, ctx);
				else power_off(a);
				cut = true;
				break;
			}
			if (key == CQK_MENU) {
				menu_leaving(a);   /* every screen closes, heard as a back */
				break;
			}
			s = cq_press(d, st, key, fresh);
			if (key >= CQK_LEFT && key <= CQK_DOWN && (s.moved || s.edge))
				step_sound(a, confirm_button(key), st.sel, s.sel);
			st.sel = s.sel;
			if (s.done) {
				answer = s.answer;
				uisound_play(key == CQK_A ? DS_SOUND_ACCEPT : DS_SOUND_BACK);
				if (s.press < 0) break;
				pressing = true;
				press_t0 = now;
				g_cq_pressed = s.press;
				g_cq_press = 0.0f;
			}
		}
		tween_to(&fill, (float)st.sel, DS_MS_QUICK, now);
		g_cq_fill = tween_at(&fill, now);
		if (tween_moving(&fill, now) || pressing) redraw_now();

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		if (over && over->backdrop) {
			over->backdrop(a, ctx, k);
		} else {
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
		}
		confirm_show(a, q, text, st.sel);
#ifdef ZYPEROS_DESK
		/* tools/desk's `state`: the question and the answer under the cursor,
		 * as the menu runner says its panels (src/menu_runner.c) */
		desk_note_focus(T(d->heading), st.sel, T(st.sel ? d->right : d->left));
#endif
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	/* The copy that leaves keeps the press where it got to */
	if (pressing && g_snap->ok && g_snap->kind == PANEL_CONFIRM)
		g_snap->marks[0] = CQ_MARK_PRESS(g_cq_pressed, g_cq_press);
	g_cq_pressed = -1;
	panel_close(a, lv, cut || !a->running || (over && plat_run_power_pressed()));
	return a->running ? answer : CQ_NONE;
}

bool confirm_ask(app *a, cq_id q, const char *text)
{
	return confirm_ask_ex(a, q, text, NULL, NULL, NULL) == CQ_RIGHT;
}

bool confirm_ask_over(app *a, cq_id q, const char *text, const menu_style *over, void *ctx)
{
	return confirm_ask_ex(a, q, text, over, ctx, NULL) == CQ_RIGHT;
}


/* The button hints under a question's tiles (keyhints.h), packed into one
 * string so they travel as the legend row and in the panel's snapshot:
 * Left/Right move, A chooses, and B's own answer */
const char *tiles_hints(const char *b_word)
{
	static char packed[4][256];
	static int turn;
	key_hint h[3] = {
		{ KEYHINT_LEFT_RIGHT, T(S_KEYHINT_MOVE) },
		{ "A", T(S_KEYHINT_CHOOSE) },
		{ "B", b_word },
	};
	char *out = packed[turn++ % 4];

	keyhints_pack(h, 3, out, sizeof packed[0]);
	return out;
}

static const char *tiles_hints_charge_only(void) { return tiles_hints(T(S_KEYHINT_CHARGE_ONLY)); }

/* A list's (choice_list_panel): A chooses and B's own answer; Up and Down
 * move, as in every list, so no hint says so */
const char *list_hints(const char *b_word)
{
	static char packed[256];
	key_hint h[2] = { { "A", T(S_KEYHINT_CHOOSE) }, { "B", b_word } };

	keyhints_pack(h, 2, packed, sizeof packed);
	return packed;
}

/* The tiles' button hints shown: off while a question that came by itself
 * takes no button yet */
bool g_tiles_hints = true;

/* A question that came by itself, this pass (usbpower_guard): a button
 * down when it appeared, or pressed in its first half second, counts only
 * once let go and pressed again; POWER always. Its hints from then. */
void ask_settle(app *a, usbpower_guard *g)
{
	unsigned down = 0, pressed = 0, ok;
	int b;

	for (b = 0; b < IN_COUNT && b < 32; b++) {
		if (a->in.down[b]) down |= 1u << b;
		if (a->in.pressed[b]) pressed |= 1u << b;
	}
	ok = usbpower_guard_pass(g, plat_now_ms(), down, pressed);
	g_tiles_hints = usbpower_guard_hints(g, plat_now_ms());
	for (b = 0; b < IN_COUNT && b < 32; b++) {
		if (b == IN_POWER) continue;
		if (!(ok & (1u << b))) a->in.pressed[b] = false;
		/* Held over: no repeat from it either */
		if (g->held & (1u << b)) a->in.down[b] = false;
	}
}

void ask_guard_start(app *a, usbpower_guard *g)
{
	unsigned down = 0;
	int b;

	for (b = 0; b < IN_COUNT && b < 32; b++)
		if (a->in.down[b] && b != IN_POWER) down |= 1u << b;
	usbpower_guard_start(g, plat_now_ms(), down);
}

/* A question in tiles at `pose` (src/ds/tiles.c), through the menus' panel
 * pass: its style from its kind (tiles_show), its legend the Charge Only
 * hints where no note gives one (Computer Connected's), its hints off while a
 * question that came by itself takes no button yet (ask_settle) */
static bool tiles_pass_begin(void *ctx) { return panel_pass_begin(ctx); }
static void tiles_pass_end(void *ctx, const SDL_Rect *panel, mm_pose pose) { panel_pass_end(ctx, panel, pose); }

void tiles_panel(app *a, int kind, const char *heading, const menu_row *tiles,
                 const int *marks, int n, int sel, mm_pose pose)
{
	const ds_pass pass = { panel_scaled, tiles_pass_begin, tiles_pass_end, a };

	ds_tiles_draw(a->r, &(ds_tiles){
		.style = kind == PANEL_TILES_GRID ? DS_TILES_GRID : kind == PANEL_LIST ? DS_TILES_LIST : DS_TILES_ROW,
		.heading = heading, .rows = tiles, .n = n, .marks = marks, .sel = sel,
		.legend = tiles_hints_charge_only(), .hints = g_tiles_hints }, pose, &pass);
}



/* Refresh in place, including copies on Favorites. Sort only after saving
 * each shelf's selected file and owner; titles are never selection keys. */
void refresh_game_titles(app *a)
{
	int si, i;

	/* The systems off the home too, whose games Favorites copies again: the
	 * systems' own shelves first (pass 0), then the shelves of games from
	 * many (pass 1), which take each game's title from its system's */
	for (int pass = 0; pass < 2; pass++)
	for (si = 0; si < sys_kept(a); si++) {
		sysview *v = &a->view[si];
		char file[LIB_PATH];
		int owner, selected;
		bool changed = false;

		if ((v->owner != NULL) != (pass == 1)) continue;
		/* Not Screenshots: an entry's file is its picture's, and its title
		 * is its game's as the shelf was read (DESIGN.md 11.68) */
		if (is_mp4(&a->sys.systems[si]) || shots_shelf(a, si) || v->list.count <= 0) continue;
		owner = shelf_owner(a, si, v->cursor);
		snprintf(file, sizeof file, "%s", v->list.items[v->cursor].file);
		if (!v->owner) {
			/* A system's own shelf: every game of it, two of one title told
			 * apart again across all of them (lib_titles_apart), and on a
			 * shelf with folders the folder's games as copies of those, so
			 * a game is named alike in every folder, Search and Favorites. A
			 * folder's name is its own (src/shelfdir.h). */
			game_list *all = sys_games(v);
			uint64_t was = lib_titles_digest(all);

			for (i = 0; i < all->count; i++)
				lib_refresh_title(db_lib(), a->sys.systems[si].folder, &all->items[i]);
			lib_titles_apart(all);
			changed = lib_titles_digest(all) != was;
			if (v->all && lib_titles_copy(&v->list, v->all)) changed = true;
		} else {
			/* Favorites, Recents, Collections: each game as its system named
			 * it, told apart there (pass 0) */
			for (i = 0; i < v->list.count; i++) {
				int o = shelf_owner(a, si, i), at;
				game_entry *e = &v->list.items[i];
				const game_list *sys;

				if (e->folder) continue;
				sys = o >= 0 && o < sys_kept(a) && !a->view[o].owner ? sys_games(&a->view[o]) : NULL;
				if (sys && (at = lib_find_entry(sys, NULL, 0, e->file)) >= 0) {
					if (strcmp(e->title, sys->items[at].title)) changed = true;
					memcpy(e->title, sys->items[at].title, sizeof e->title);
					e->apart = sys->items[at].apart;
				} else if (lib_refresh_title(db_lib(), a->sys.systems[o].folder, e)) {
					changed = true;
				}
			}
		}
		if (!changed) continue;
		free_view_textures(v);
		sort_shelf(a, si);
		selected = lib_find_entry(&v->list, v->owner, owner, file);
		if (selected >= 0) v->cursor = selected;
		cf_reset(&v->cf, v->cursor);
	}
}

/* The approved Details layout: system information, then storage and the
 * battery controller's capacity estimate. The same rows serve --about shots. */
int menu_measure(const menu_row *rows, int n, const char *heading);

/* The widest row of one built menu. */
int menu_measure(const menu_row *rows, int n, const char *heading)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fh = ui_font(UI_F_LABEL);
	int gap = menu_row_h(), w = 0, i;
	bool two_col = false;

	/* The same three row kinds menu_draw knows about. This function is a
	 * second copy of that measuring pass and the two must agree, or a panel is
	 * sized by one rule and drawn by another. */
	for (i = 0; i < n; i++)
		if (rows[i].value && !ROW_IS_NOTE(rows[i]) && !ROW_IS_HR(rows[i])) two_col = true;
	for (i = 0; i < n; i++) {
		int rw;

		if (!rows[i].label) continue;          /* a rule measures nothing */
		/* buttons for a label (MENU_KEYS_MARK) are their glyphs' width */
		rw = menu_keys_is(rows[i].label) ? ui_button_width(menu_keys_names(rows[i].label), MENU_KEYS_SIZE)
		                                 : ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			rw += gap + menu_value_w(fm, rows[i].value);
		if (rw > w) w = rw;
	}
	if (heading) {
		int hw = ui_text_width(fh, heading);
		if (hw > w) w = hw;
	}
	return w;
}

/* One width for both shelf menus, every system, and every value their rows can
 * cycle to. The panel is a frame the lists sit inside rather than something
 * that resizes to whatever is selected: without this, cycling display mode from
 * "Fill" to "Integer tall" widens the slab under the cursor, and walking from
 * the systems row into a system resizes it again.
 *
 * Measured across all of that rather than picked, so a longer label, another
 * system or another font widens the frame instead of overflowing it.
 * Cached because it is a few hundred text measurements and none of its inputs
 * change while a menu is open. */
/* Where a console shelf's menu has the row `which`, or -1: the enum's numbers
 * are the full list's, and a shelf's list is what sys_menu_system_rows makes
 * of it (no Box Art without a radio, Game Language only on the DS). */
static int sm_pos(const app *a, int sys, sm_row which)
{
	sys_ui dev = { .no_wifi = !plat_has_wifi(), .online = net_internet(NULL),
	               .dslang = dslang_row_shown(a, sys) ? "" : NULL,
	               .sgb = sgb_row_shown(a, sys) ? "" : NULL,
	               .border = border_row_value(a, sys),
	               .border_color = border_color_value(a, sys),
	               .pace = pacing_row_shown(a, sys) ? "" : NULL,
	               .no_strength = !shader_has_strength(a->view[sys].shader) };
	sm_row ids[SM_ROWS];
	int n = sys_menu_system_rows(&dev, ids), k;

	for (k = 0; k < n; k++)
		if (ids[k] == which) return k;
	return -1;
}

static int menu_shelf_width(app *a)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int w, n, i, k;

	if (a->menu_w) return a->menu_w;

	/* MENU's every tab, so the panel is one width on all of them, and on
	 * the tab with Shader every value of it, so stepping it does not resize
	 * the panel under it */
	{
		int t, at = g_tabs.at, mw;

		w = 0;
		for (t = 0; t < g_tabs.count; t++) {
			g_tabs.at = t;
			n = menu_build(a, SCREEN_SYSTEMS, a->sys_cursor, rows, &bufs, &heading);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
			for (k = 0; k < n; k++)
				if (rows[k].label == T(S_GAMEMENU_SHADER)) break;
			if (k < n) {
				const char *keep = rows[k].value;
				int j;

				for (j = 0; j < SHADER_COUNT; j++) {
					if (!screen_shader_offered(j)) continue;
					rows[k].value = shader_label(j);
					mw = menu_measure(rows, n, heading);
					if (mw > w) w = mw;
				}
				rows[k].value = keep;
				/* And Strength under it, there or not, at its widest */
				if (n < MENU_MAX_ROWS) {
					rows[n] = (menu_row){ T(S_GAMEMENU_STRENGTH), "100%", true };
					mw = menu_measure(rows, n + 1, heading);
					if (mw > w) w = mw;
				}
			}
			/* And Appearance at each of its three, Auto the longest in
			 * most languages ("Automatisch") */
			for (k = 0; k < n; k++)
				if (rows[k].label == T(S_SYSMENU_APPEARANCE)) break;
			if (k < n) {
				const char *keep = rows[k].value;
				int j;

				for (j = 0; j < APPEARANCES; j++) {
					rows[k].value = appearance_name((appearance)j);
					mw = menu_measure(rows, n, heading);
					if (mw > w) w = mw;
				}
				rows[k].value = keep;
			}
			/* And Night Mode's row, the tab's last, at its longest mode
			 * (night_row_widest) */
			for (k = 0; k < n; k++)
				if (rows[k].label == T(S_SYSMENU_NIGHT_MODE)) break;
			if (k < n && night_row_widest(rows + k)) {
				mw = menu_measure(rows, k + 1, heading);
				if (mw > w) w = mw;
			}
		}
		g_tabs.at = at;
	}

	/* Every value that can be cycled on this menu, at its widest, so the
	 * panel does not resize under the row being cycled. Sort By joined
	 * Display Mode in that category the moment it stopped being a
	 * placeholder - "Recently Added" is 187px wider than "Name".
	 *
	 * MP4's menu and Favorites' are their own rows, Sort By second and no
	 * Display Mode, so the enum's row numbers are only the rest's. Written
	 * through them on MP4's, this measured Rescan Folder carrying a sort
	 * order, a row no menu has. */
	for (i = 0; i < a->sys.count; i++) {
		bool mp4 = is_mp4(&a->sys.systems[i]);
		bool own  = mp4 || a->view[i].owner;
		/* Screenshots' Sort By and View are its first two (SM_SHOT_ROWS) */
		bool shots = i == shelf_shots_at(a);
		int  srow = shots ? 0 : own ? 1 : sm_pos(a, i, SM_SORT), show = -1, lookrow = -1;
		int  drow = own ? -1 : sm_pos(a, i, SM_DISPLAY);
		int  hrow = own ? -1 : sm_pos(a, i, SM_SHADER);
		int  crow = own ? -1 : sm_pos(a, i, SM_CORE);
		int  lrow = own ? -1 : sm_pos(a, i, SM_DSLANG);
		int  prow = own ? -1 : sm_pos(a, i, SM_PACE);
		int  grow = own ? -1 : sm_pos(a, i, SM_SGB);
		int  brow = own ? -1 : sm_pos(a, i, SM_BORDER);
		int  bcrow = own ? -1 : sm_pos(a, i, SM_BORDER_COLOR);
		int  vrow = mp4 ? -1 : shots ? 1 : a->view[i].owner ? 3 : sm_pos(a, i, SM_VIEW);
		int  ffrow = own || shots ? -1 : sm_pos(a, i, SM_FAVFIRST);

		n = menu_build(a, SCREEN_GAMES, i, rows, &bufs, &heading);
		/* Recents' Card Picture at the longer of its two (DESIGN.md 11.48) */
		if (i == shelf_recents_at(a) && n > SM_FAV_ROWS) {
			const char *keep = rows[SM_FAV_ROWS].value;
			int mw;

			rows[SM_FAV_ROWS].value = T(S_SYSMENU_CARD_PICTURE_SCREEN);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[SM_FAV_ROWS].value = T(S_SYSMENU_CARD_PICTURE_COVER);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[SM_FAV_ROWS].value = keep;
		}
		if (mp4) {
			sm_mp4_row ids[SM_MP4_ROWS];
			int nn = sys_menu_mp4_rows(mp4_rows_kind(), mp4_kinds() > 1,
			                            !plat_has_wifi() && !net_internet(NULL), ids);

			for (k = 0; k < nn; k++) {
				if (ids[k] == SMM_SORT) srow = k;
				if (ids[k] == SMM_SHOW) show = k;
				if (ids[k] == SMM_LOOK) lookrow = k;
			}
			/* The longer of Show's two values, so turning it over does not
			 * change the menu's width under the cursor. */
			if (show >= 0) {
				int mw;

				rows[show].value = "Audiobooks";
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			/* And Player Look's each look, for the same reason */
			if (lookrow >= 0 && bufs.choice[lookrow].labels) {
				const char *keep = rows[lookrow].value;

				for (k = 0; k < bufs.choice[lookrow].n; k++) {
					int mw;

					rows[lookrow].value = bufs.choice[lookrow].labels[k];
					mw = menu_measure(rows, n, heading);
					if (mw > w) w = mw;
				}
				rows[lookrow].value = keep;
			}
		}
		/* Favorites First at the longer of On and Off (DESIGN.md 11.70) */
		if (ffrow >= 0 && ffrow < n) {
			const char *keep = rows[ffrow].value;

			for (k = 0; k < 2; k++) {
				int mw;
				rows[ffrow].value = T(k ? S_MENU_ON_TITLE : S_MENU_OFF_TITLE);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[ffrow].value = keep;
		}
		for (k = 0; drow >= 0 && k < DMODE_COUNT; k++) {
			int mw;
			rows[drow].value = dmode_label(k);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
		}
		if (drow >= 0) rows[drow].value = dmode_label(0);
		/* And each shader, for the same reason */
		for (k = 0; hrow >= 0 && k < SHADER_COUNT; k++) {
			int mw;
			if (!shader_offered(k, a->sys.systems[i].tag)) continue;
			rows[hrow].value = shader_label(k);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
		}
		if (hrow >= 0) rows[hrow].value = shader_label(0);
		/* And Strength under it, there or not */
		if (hrow >= 0 && n < MENU_MAX_ROWS) {
			int mw;
			rows[n] = (menu_row){ T(S_GAMEMENU_STRENGTH), "100%", true };
			mw = menu_measure(rows, n + 1, heading);
			if (mw > w) w = mw;
		}
		/* And each core a shelf can choose, for the same reason. */
		if (crow >= 0 && cfg_core_count(&a->sys.systems[i]) > 1) {
			char alt[CFG_STR], lb[CFG_STR];
			const char *keep = rows[crow].value;

			for (k = 0; cfg_core_at(&a->sys.systems[i], k, alt, sizeof alt); k++) {
				int mw;
				rows[crow].value = core_label(alt, lb, sizeof lb);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[crow].value = keep;
		}
		/* And each language the Nintendo DS's game can speak, Auto too */
		if (lrow >= 0) {
			char lb[64];
			const char *keep = rows[lrow].value;

			for (k = 0; k < DSL_CHOICES; k++) {
				int mw;
				rows[lrow].value = dslang_label((dsl_choice)k, lb, sizeof lb);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[lrow].value = keep;
		}
		/* Border's kinds */
		if (brow >= 0 && brow < n) {
			const char *keep = rows[brow].value;

			for (k = 0; k < BORDER_KINDS; k++) {
				int mw;
				rows[brow].value = T(border_label_id((border_kind)k));
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[brow].value = keep;
		}
		/* Handheld Color's colors */
		if (bcrow >= 0 && bcrow < n) {
			const hf_model *m = hf_model_for(a->sys.systems[i].tag);
			const char *keep = rows[bcrow].value;

			for (k = 0; m && k < m->ncolors; k++) {
				int mw;
				rows[bcrow].value = border_color_label(m->color_name[k]);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[bcrow].value = keep;
		}
		/* Super Game Boy's two */
		if (grow >= 0 && grow < n) {
			const char *keep = rows[grow].value;

			for (k = 0; k < SGBM_CHOICES; k++) {
				int mw;
				rows[grow].value = T(sgbm_label_id((sgbm_choice)k));
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[grow].value = keep;
		}
		/* Timing's two */
		if (prow >= 0 && prow < n) {
			const char *keep = rows[prow].value;

			for (k = 0; k < PACING_CHOICES; k++) {
				int mw;
				rows[prow].value = T(pacing_label_id((pacing_choice)k));
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[prow].value = keep;
		}
		/* View's three values, the longest kept */
		if (vrow >= 0 && vrow < n) {
			const char *keep = rows[vrow].value;
			int mw;

			rows[vrow].value = T(S_SYSMENU_VIEW_COVERS);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[vrow].value = T(S_SYSMENU_VIEW_GRID);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[vrow].value = T(S_SYSMENU_VIEW_LIST);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[vrow].value = T(S_SYSMENU_VIEW_VIDEO);
			if ((mw = menu_measure(rows, n, heading)) > w) w = mw;
			rows[vrow].value = keep;
		}
		for (k = 0; k < (mp4 ? ML_ORDERS : SORT_COUNT); k++) {
			int mw;
			/* Every order a shelf of either kind offers: Collections' two
			 * too (src/sort.h), so its menu is no wider than this */
			rows[srow].value = mp4 ? ml_order_label((ml_order)k, false) : sort_label(k);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
			if (mp4) {
				int kind;

				for (kind = ML_BOOKS; kind < ML_KINDS; kind++) {
					rows[srow].value = ml_order_label_kind((ml_order)k, (ml_kind)kind);
					mw = menu_measure(rows, n, heading);
					if (mw > w) w = mw;
				}
			}
		}
	}
	/* A system's menu keeps its lists' words in one pool for every shelf
	 * (sysmenu_screen.c's menu_choices), and the loop above built every
	 * shelf's: the last with a Border row wrote its words over the shown
	 * one's. A Game Boy's Border list opened before the width was measured
	 * said the Advance's three words over its own five ("Off, Handheld,
	 * Handheld, Whole, Handheld, Whole, Your Own" on the Brick, where the
	 * game's menu said "Off, Blur, Handheld, Handheld, Whole, Your Own").
	 * So the shown shelf's menu is built again, last. */
	menu_build(a, a->screen == SCREEN_SYSTEMS ? SCREEN_SYSTEMS : SCREEN_GAMES, a->sys_cursor, rows, &bufs,
	           &heading);
	a->menu_w = w;
	return w;
}

/* The width every menu reached from the shelf uses.
 *
 * Menus sized themselves to their own rows, so the panel changed size on
 * nearly every screen. Measured on the device 2026-09-08: the ZyperOS menu drew
 * a 794px panel, Play Time 791, Bluetooth 737, Wi-Fi 640 - so opening Wi-Fi
 * from the menu shrank the panel by 154px, about 15% of the display, and grew
 * it again on the way out.
 *
 * Sizing to content was right while a label that did not fit was cut, because
 * cutting a name is a loss. A long label scrolls now, so a panel no longer has
 * to grow to hold one, and the trade is a little empty air in a narrow menu
 * against a panel that stops jumping.
 *
 * 800px of PANEL, stated rather than measured, so it does not drift when a
 * row's text changes. What menu_draw takes is the content width and it adds
 * menu_pad() on each side, so the pad comes off here - by calling the same
 * function menu_draw calls, because the pad follows the menu font and a
 * hardcoded number is wrong the day the font changes.
 *
 * The floor is not decoration. menu_shelf_width is the widest row in the
 * shelf menus, and a panel narrower than that sets one of them scrolling. In
 * Josefin Sans it was "Bluetooth" and the paired headset's name - a row that
 * grows with a device nobody here chose the name of - measured on the device
 * at 699, and the panel drew at 800. Lexend sets words about 8% wider, and
 * "Audio Output" and "auto (speaker)" alone are 741 (700 in Josefin,
 * measured 2026-11-04) against the 692 of content 800 leaves, so the shelf
 * menus draw about 850 wide: within DESIGN.md 5's rule that content stops at
 * the screen less two insets and two paddings (868), and not the 800 it
 * names. max() is the whole reason a longer name costs air instead of a cut,
 * and the reason the panel is wider now rather than a row scrolling.
 *
 * Not for the one-row notices - "Scanning...", "Could not start". Those are
 * messages, and a message stretched to menu width reads as a menu that failed
 * to load. Not for the in-game menu either, which sits over a paused game. */
int menu_std_width(app *a)
{
	int content = 800 - menu_pad() * 2;
	int floor   = menu_shelf_width(a);
	/* No wider than menu_draw makes a panel: a language whose rows run long
	 * (German) measured past it, and About's story, wrapped at that, slid */
	int most    = menu_most_w();
	int w       = floor > content ? floor : content;

	return w < most ? w : most;
}

int      shot_press = SHOT_STEP;
int             shot_lv;
bool     shot_pressed;
unsigned shot_t0;             /* the press, on the held clock */

/* --zyper [works|fails]: the top menu's --open, for Zyper in it, with a
 * success or a failure at 500 ms, once the opening hop is down. */
static int      shot_zyper_did;      /* 1 works, -1 fails, 0 neither */

/* Zyper's moment in a --menu shot, on --shot-at's clock: from the menu's
 * open with --open and --zyper, and otherwise from a second before the press,
 * the menu having opened then. 1000, at rest before any blink can come,
 * without --shot-at. */
static unsigned shot_zyper_ms(void)
{
	if (!g_clock_held) return 1000;
	return (shot_press == SHOT_OPEN ? 0u : 1000u) + (anim_now() - shot_t0);
}

/* How the shelf's menu is drawn from `screen`: ZyperOS's own on the systems
 * row, the top one, which is the one Zyper is in, and a system's on a game
 * list, which is where it gains rows that belong to that system. The menu
 * and its --menu shot both take it from here. */
menu_style zyperos_menu_style(app *a, int screen)
{
	return (menu_style){
		/* Measured across every system and every display mode, so the panel
		 * does not resize while Display Mode is being cycled on it. */
		.fixed_w     = menu_std_width(a),
		.accent      = MENU_ZYPEROS,
		/* On a game shelf this menu is about THAT system, so it wears the
		 * shelf's color - which tick_tint is still moving while it is open. */
		.follow_tint = screen != SCREEN_SYSTEMS,
		.zyper       = screen == SCREEN_SYSTEMS,
		/* The ZyperOS menu's tabs; a system's menu is one list */
		.tabs        = screen != SCREEN_GAMES ? &g_tabs : NULL,
		/* and the themes' chips on Display's Theme row */
		.row_mark    = screen != SCREEN_GAMES ? look_row_mark : NULL,
	};
}

void zyperos_menu_draw(app *a, int sel)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading);
	const menu_style st = zyperos_menu_style(a, a->screen);
	unsigned zms = shot_zyper_ms();
	float k = 1.0f;
	zr_rig zy;

	/* Where it is in its open, as menu_run_body asks; at rest without one */
	if (shot_lv) panel_frame(a, shot_lv, &k);
	ui_scrim_k(a->r, 0, k);
	/* Zyper, on the shot's own clock from 0, as menu_run_body keeps him */
	if (st.zyper) {
		zr_init(&zy, 0, 1);
		zr_open(&zy, 0);
		if (shot_zyper_did && zms >= 500) {
			zr_tick(&zy, 500);
			zr_press(&zy, 500);
			menu_zyper_answer(&zy, 500, shot_zyper_did > 0 ? MENU_CHANGED : MENU_FAILED);
		}
		zr_tick(&zy, zms);
		g_menu_zyper = &zy;
		g_menu_zyper_at = zms;
	}
	g_menu_tabs = st.tabs;
	g_row_mark = st.row_mark;
	menu_draw(a, heading, rows, n, sel, st.fixed_w, st.follow_tint ? a->tint : st.accent);
	g_row_mark = NULL;
	g_menu_tabs = NULL;
	g_menu_zyper = NULL;
}

int sysmenu_build(void *ctx, menu_row *rows, int max,
                  const char **heading)
{
	sysmenu_ctx *c = ctx;
	int n = menu_build(c->a, c->screen, c->a->sys_cursor, rows, &c->bufs,
	                   heading);

	return n > max ? max : n;
}

/* Left and right cycle the value on a row that has one; A opens whatever the
 * row leads to. Everything else the runner has already dealt with. */
/* ---------- a game picked in a menu --------------------------------------- */

/* A game Random Game or Search Games picked inside a menu, started once the
 * menu has gone (zyperos_menu_for): the shelf is put on it, the way the
 * systems row and A get there, shown for `hold_ms` - Random Game's moment to
 * see what came up, in which B or MENU stays on the shelf at the game - and
 * launched as A there would. Shared by the two, word for word. */
game_pick g_pick = { -1, -1, 0 };

void pick_launch(app *a)
{
	int shelf = g_pick.shelf, game = g_pick.game, was = a->sys_cursor;
	Uint32 until = plat_now_ms() + g_pick.hold_ms;

	char file[LIB_PATH];

	g_pick.shelf = -1;
	if (shelf < 0 || shelf >= a->sys.count || game < 0 ||
	    game >= sys_games(&a->view[shelf])->count)
		return;
	/* The game by its file: the index is of every game of the system, and
	 * the shelf may show one folder of them (src/shelfdir.h) */
	snprintf(file, sizeof file, "%s", sys_games(&a->view[shelf])->items[game].file);
	fprintf(stderr, "pick: %s/%s\n", a->sys.systems[shelf].folder, file);
	if (a->screen == SCREEN_GAMES && shelf != was)
		evict_far(&a->view[was], TEX_KEEP_FAR);
	a->sys_cursor = shelf;
	home_reset(&a->home, shelf, a->sys.count, plat_now_ms());
	if (a->view[shelf].all) shelf_reveal(a, shelf, file);
	else a->view[shelf].cursor = game;
	enter_system(a);
	do {
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER]) { power_off(a); return; }
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			memset(&a->in, 0, sizeof a->in);
			return;
		}
		tick_tint(a);
		render(a);
		ui_frame_rest(8);
	} while ((int)(until - plat_now_ms()) > 0 && a->running && !want_quit);
	memset(&a->in, 0, sizeof a->in);
	launch(a);
}

/* Copy the paused frame's preview beside a manual save, so the slot strip can
 * show what is inside each slot. The pause preview IS the frame the save
 * serializes - Diatom wrote it on the way into the menu - so a straight copy
 * is the truthful thumbnail, no protocol round trip needed. */
/* The SOURCE is opened first, and the destination only if that worked.
 *
 * Opening both up front truncates the destination whenever the source is
 * missing - so a copy that cannot happen destroys the file it was going to
 * replace. Harmless while the only caller copied a preview a pause had just
 * written; not harmless once the Auto card is written this way, where the
 * thing being overwritten is the last good picture of a save. */
void copy_file(const char *from, const char *to)
{
	FILE *a, *b;
	char buf[16384];
	size_t n;

	if (!from || !*from || !to || !*to) return;
	if (!(a = fopen(from, "rb"))) return;
	if (!(b = fopen(to, "wb"))) { fclose(a); return; }

	while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
	fclose(a);
	fclose(b);
}


/* By the step, so a step moved in src/tour.h keeps its name */
const char *const TOUR_STEP_NAMES[TOUR_STEPS] = {
	[TOUR_LANG] = "lang", [TOUR_WELCOME] = "welcome", [TOUR_MOVE] = "move", [TOUR_A] = "a",
	[TOUR_B] = "b", [TOUR_X] = "x", [TOUR_Y] = "y", [TOUR_MENU] = "menu",
	[TOUR_QUIT] = "quit", [TOUR_LOCK] = "lock", [TOUR_UNLOCK] = "unlock",
	[TOUR_SELECT] = "select", [TOUR_VOL] = "vol", [TOUR_BRIGHT] = "bright",
	[TOUR_POWER] = "power", [TOUR_THEME] = "theme", [TOUR_HOME] = "home",
	[TOUR_SOUNDS] = "sounds", [TOUR_MUSIC] = "music", [TOUR_FINISH] = "finish" };

/* The start's scan off the main thread (main says why): from the saved
 * library when there is one (src/savedlib.h), except for a shot, which shows
 * the card it is given */
static int scan_thread(void *ctx)
{
	scan_all_start(ctx, !shot_path);
	t_mark("library scan");
	return 0;
}

/* MP4 in the in-game menu over a program of its own that keeps it playing
 * (your PICO-8; src/mp4hand.h): SELECT there opens it as over a Diatom
 * game, its shelf and Now Playing. Made ready at the first SELECT, not with
 * the menu, which opens in the time it always did: the daemon, MP4's card
 * read (its albums, the books' places), the covers' loader, where its sound
 * goes, and the queue the file hands over. False over any other program:
 * MP4 is stopped for those (launch.sh), and SELECT stays nothing there. */
static bool g_port_mp4;          /* this program keeps MP4 (the file is there) */
static bool g_port_mp4_up;       /* made ready, so the file is written back */

bool port_mp4_ready(app *a)
{
	if (!g_port_menu || !g_port_mp4) return false;
	if (g_port_mp4_up) return true;
	g_port_mp4_up = true;
	g_aout_policy = aout_load();
	g_channels = channels_load();
	mp4_setup(false, true, NULL);
	texload_start();
	mp4_shelf_alone(a);
	mp4_hand_take();
	fprintf(stderr, "port menu: MP4 ready, %d tracks on the card\n", g_mp4.ntracks);
	return true;
}

/* The follower's lines into the log as "port: " lines, ZyperOS's own, which
 * plastron's splash passes over (its portout.h): the follower starts while
 * the program's loading screen shows the program's latest line at its foot,
 * and "mp4: took over 3 tracks" there would be read as PICO-8's. stderr into
 * a pipe, and a thread that writes each line to the log with the prefix;
 * follow_log_end lets the last lines through before the process ends. */
static int g_follow_log = -1;
static SDL_Thread *g_follow_log_thread;

static int follow_log_run(void *ctx)
{
	FILE *in = fdopen((int)(intptr_t)ctx, "r");
	char line[1024];

	while (in && fgets(line, sizeof line, in))
		dprintf(g_follow_log, "port: %s%s", line, strchr(line, '\n') ? "" : "\n");
	if (in) fclose(in);
	return 0;
}

static void follow_log_start(void)
{
	int p[2];

	if (pipe(p) < 0) return;
	if ((g_follow_log = dup(2)) < 0 || dup2(p[1], 2) < 0) {
		if (g_follow_log >= 0) close(g_follow_log);
		g_follow_log = -1;
		close(p[0]);
		close(p[1]);
		return;
	}
	close(p[1]);
	setvbuf(stderr, NULL, _IOLBF, 0);
	g_follow_log_thread = SDL_CreateThread(follow_log_run, "mp4-follow-log", (void *)(intptr_t)p[0]);
	if (!g_follow_log_thread) {
		dup2(g_follow_log, 2);
		close(p[0]);
	}
}

static void follow_log_end(void)
{
	if (g_follow_log < 0) return;
	fflush(stderr);
	dup2(g_follow_log, 2);              /* the pipe's last writer gone: EOF */
	if (g_follow_log_thread) SDL_WaitThread(g_follow_log_thread, NULL);
	g_follow_log_thread = NULL;
}

/* --mp4-follow (src/mp4hand.h): MP4 looked after beside a program of its own
 * that keeps it playing, your PICO-8, as the launcher looks after it beside
 * a Diatom game (on_game_tick): ten times a second, the daemon's word read,
 * the next track sent when one ends, a book's place and the resume row kept
 * (book_keep). launch.sh starts it with the program and ends it (TERM) after
 * it; portkeys stops it (SIGSTOP) while the in-game menu is up, the menu
 * then being the one that talks to the daemon, and lets it go on after. A
 * file the menu wrote meanwhile (another inode or time) is taken again.
 * Where MP4's sound goes stays where the launcher left it: this process
 * reads no jack and no headset (no route). No display, no input, no sound of
 * its own. The file is written as it ends, for the launcher after. */
static volatile sig_atomic_t g_follow_cont;
static void on_sigcont(int sig) { (void)sig; g_follow_cont = 1; }

/* The file's bytes, to tell a queue the menu wrote from the one taken: a
 * file written again in the same second at the same length looks the same
 * to stat. NULL with none. */
static char *hand_bytes(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	char *b = NULL;
	long len;

	*n = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
	    (b = malloc((size_t)len + 1)) && fread(b, 1, (size_t)len, f) == (size_t)len) {
		*n = (size_t)len;
	} else {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

static int mp4_follow_main(void)
{
	static app a;
	char path[CFG_STR * 2], hand[CFG_STR * 2];
	char *taken;
	size_t taken_n;
	struct stat seen, st;

	signal(SIGTERM, on_sigterm);
	signal(SIGINT, on_sigterm);
	/* Let go on after the in-game menu (portkeys's SIGCONT): the file
	 * looked at at once */
	signal(SIGCONT, on_sigcont);
	follow_log_start();
	paths_init();
	{
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];

		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		if (!db_init(dev, lib, env))
			fprintf(stderr, "mp4 follow: no settings database; no places kept\n");
	}
	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &a.sys))
		fprintf(stderr, "mp4 follow: no usable %s; the queue alone\n", path);
	mp4_setup(false, false, NULL);
	mp4_shelf_alone(&a);
	/* A copy: plat_flag's answer is one of a few it hands out in turn */
	snprintf(hand, sizeof hand, "%s", plat_flag(MP4HAND_PATH));
	if (stat(hand, &seen) != 0) memset(&seen, 0, sizeof seen);
	taken = hand_bytes(hand, &taken_n);
	mp4_hand_take();
	fprintf(stderr, "mp4 follow: beside the program%s\n", taken ? "" : ", with no queue handed over");
	port_quiet_follow(true);
	while (!want_quit) {
		struct timespec nap = { 0, 100 * 1000000L };
		bool look = g_follow_cont;

		/* The in-game menu's MP4 wrote the file as it closed: taken before
		 * the daemon is read, as the menu's connection replaced this one
		 * and the next word is about the menu's queue. Looked at as this
		 * goes on after the menu, and whenever stat says it changed. */
		g_follow_cont = 0;
		if (stat(hand, &st) == 0 &&
		    (look || st.st_ino != seen.st_ino || st.st_mtime != seen.st_mtime ||
		     st.st_size != seen.st_size)) {
			size_t n;
			char *now = hand_bytes(hand, &n);

			seen = st;
			if (now && (!taken || n != taken_n || memcmp(now, taken, n))) {
				free(taken);
				taken = now;
				taken_n = n;
				fprintf(stderr, "mp4 follow: the queue again, from the in-game menu\n");
				mp4_hand_take();
				port_quiet_follow(true);
			} else {
				free(now);
			}
		}
		mp4_poll();
		/* PICO-8 quiet while MP4 plays, heard again at a pause or the end */
		port_quiet_follow(false);
		/* And the boost's steps over PICO-8, portkeys's, on MP4 */
		port_gain_follow();
		nanosleep(&nap, NULL);
	}
	free(taken);
	/* PICO-8 has ended: no cut left for the next program */
	eq_share_cut(false);
	book_keep(true);
	mp4_hand_save();
	db_shutdown();
	follow_log_end();
	return 0;
}

/* --port-played (src/stats.h, "a program of its own"): the session of the
 * program that just ended closed in the library database, from the note the
 * launcher left as it handed the program over, and the note removed. launch.sh
 * runs it the moment the program ends, POWER and Auto Off too, before the
 * power-off, so a cart played on your PICO-8 reaches Recents and Played
 * however it ended. Nothing more: no systems.cfg, no display, no input. */
static int port_played_main(void)
{
	char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];
	bool closed;

	paths_init();
	db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
	if (!db_init(dev, lib, env)) {
		fprintf(stderr, "port: no library database; its play time is not kept\n");
		return 1;
	}
	closed = stats_handed_back(plat_flag(STATS_PORT_NOTE), stats_mono_ms());
	db_shutdown();
	fprintf(stderr, "port: %s\n", closed ? "its play time kept" : "no play time to keep");
	return 0;
}

/* --port-menu TAG GAME (src/portmenu.h): the in-game menu over a program of
 * its own (OpenBOR, the standalone Flycast, a port from the Ports
 * shelf and PortMaster itself, whose GAME is the port's script), and only
 * that. What it needs of the launcher's start, in the same order: the
 * settings, the port's system from systems.cfg (its name and accent, and its
 * buttons for the Controls page; a port's, what its files say), the
 * language and theme, the interface
 * sounds, the display (taken as from the splash: portkeys has let go of it),
 * the buttons and the faces. No scan of the card, no Diatom, no MP4, no menu
 * music, no File Transfer: the port has the memory, and the menu wants none
 * of them. Auto Off is off: a port has no state to come back to after it,
 * and the battery run flat is still POWER (idle_due). */
static int port_menu_main(int argc, char **argv)
{
	static app a;
	const char *tag = argc > 2 ? argv[2] : "", *game = argc > 3 ? argv[3] : "";
	const char *dot = strrchr(game, '.');
	const system_cfg *s = NULL;
	char path[CFG_STR * 2];
	int how, i;

	signal(SIGTERM, on_sigterm);
	signal(SIGINT, on_sigterm);
	t_boot0 = plat_now_ms();
	/* --device ID after the game: a handheld's profile on a computer, as
	 * the launcher's own --device, for a check of what the menu shows there
	 * (Screen Colors where the display has a table); a device build has its
	 * own alone, and finds no other */
	for (i = 4; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--device") && !plat_profile_select(argv[i + 1]))
			fprintf(stderr, "port menu: no profile \"%s\"\n", argv[i + 1]);
	paths_init();
	{
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2], tz[64];

		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		if (!db_init(dev, lib, env))
			fprintf(stderr, "port menu: no settings database; the shipped defaults\n");
		db_get_str(db_lib(), "timezone", tz, sizeof tz, "America/New_York");
		plat_clock_zone(tz);
	}
	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &a.sys)) {
		fprintf(stderr, "port menu: no usable %s\n", path);
		return PORT_MENU_CONTINUE;
	}
	for (i = 0; i < a.sys.count; i++)
		if (!strcmp(a.sys.systems[i].tag, tag)) {
			a.sys_cursor = i;
			s = &a.sys.systems[i];
			break;
		}
	if (!s) {
		fprintf(stderr, "port menu: no system tagged %s\n", tag);
		return PORT_MENU_CONTINUE;
	}
	g_port_menu = true;
	g_port_waiting = argc > 4 && !strcmp(argv[4], PORT_MENU_WAITING);
	/* MP4 kept over this program (src/mp4hand.h): SELECT opens it */
	g_port_mp4 = access(plat_flag(MP4HAND_PATH), F_OK) == 0;
	/* Its buttons when ZyperOS knows them (OpenBOR's); over the standalone
	 * Flycast, In a game alone (port_menu_pads) */
	g_port_ctl = (ctl_game){ port_menu_pads(s->tag) ? s->tag : "", s->name,
	                         home_label(s->tag, s->name), s->map, plat_turbo_map(s->tag),
	                         dot ? dot + 1 : "", ctl_head_fits, &a, true, NULL, NULL, false,
	                         CTL_STICK_TABLE };
	/* A port's pages: what its README, its keymap and its port.json say,
	 * or PortMaster's own buttons over PortMaster (src/portdoc.h) */
	if (!strcmp(s->core, "port")) {
		static port_doc doc;

		port_doc_read(game, &doc);
		g_port_ctl.doc = &doc;
		fprintf(stderr, "port menu: %s%s: %d of its README's controls, %d keys, %s, %s\n",
		        doc.title, doc.portmaster ? " (PortMaster)" : "", doc.nsaid, doc.nkeys,
		        doc.gptokeyb ? "gptokeyb" : "no gptokeyb",
		        doc.inst[0] || doc.desc[0] ? "an About" : "no About");
	}
	{
		char ap[16], lc[16], v[16], dir[CFG_STR * 2];

		db_get_str(db_dev(), "language", lc, sizeof lc, "en");
		i18n_set(lang_parse(lc));
		night_migrate_db();
		db_get_str(db_dev(), "appearance", ap, sizeof ap, appearance_id(APPEAR_LIGHT));
		look_load();
		theme_init(appearance_parse(ap));
		IMG_Init(IMG_INIT_PNG);
		db_get_str(db_dev(), "uisounds", v, sizeof v, uisound_setting_id(DS_VOICE_DEFAULT));
		uisound_set(uisound_setting_parse(v));
		snprintf(dir, sizeof dir, "%s/res/sounds", P_ROOT);
		uisound_init(dir);
	}
	if (!plat_video_init()) {
		fprintf(stderr, "port menu: video init failed\n");
		return PORT_MENU_CONTINUE;
	}
	a.r = plat_renderer();
	tour_hint_draw_set(keyhints_width, tour_hint);
	plat_input_init();
	plat_settings_init();
	levels_init();   /* the boosts past the top kept from before (src/levels.c) */
	if (!ui_init(a.r, P_FONT)) fprintf(stderr, "port menu: font init failed\n");
	ui_fonts_for(i18n_lang());
	page_fonts_init(P_FONT);
	clock24_load();
	batt_icon_load();
	a.tint = s->accent;
	a.auto_off = 0;
	a.running = true;
	/* The systems' Display rows, for the row over your PICO-8, by the core
	 * each runs on (a row never set goes by it) */
	display_load(&a);
	core_load(&a);
	/* No start again asked for yet: a file left from before is no answer */
	unlink(plat_flag(PORT_RESTART_FILE));
	fprintf(stderr, "port menu: %s, ready in %u ms\n", s->name, plat_now_ms() - t_boot0);

	/* Your PICO-8's cart, as its shelf names it, for its Controls list and
	 * its hotkeys (This Game's keys): the game's path in the shelf's folder;
	 * Splore's entry for Splore and for PICO-8's own start */
	{
		static char file[LIB_PATH], title[LIB_PATH];
		char dir[CFG_STR * 2];
		const char *base = strrchr(game, '/');
		size_t dn;

		snprintf(dir, sizeof dir, "%s/%s/", P_ROMS, s->folder);
		dn = strlen(dir);
		snprintf(file, sizeof file, "%s", !strncmp(game, dir, dn) ? game + dn : base ? base + 1 : game);
		if (!file[0]) snprintf(file, sizeof file, "%s", SPLORE_FILE);
		snprintf(title, sizeof title, "%s", splore_file(file) ? SPLORE_NAME : base ? base + 1 : file);
		if (!splore_file(file) && (base = strrchr(title, '.'))) title[base - title] = '\0';
		g_port_file = file;
		g_port_title = title;
	}

	/* The port is stopped under it: the volume keys are its own, as over a
	 * paused game (src/levels.h), and portkeys reads the cells back */
	levels_game_paused(true);
	/* Next Display over your PICO-8 (portkeys's hotkey): its question
	 * alone, no menu; and Reset Game's, the same way */
	if (argc > 4 && !strcmp(argv[4], PORT_MENU_DISPLAY_ARG) && port_menu_display(s->tag))
		how = port_display_next(&a);
	else if (argc > 4 && !strcmp(argv[4], PORT_MENU_RESET_ARG) && port_resets(s->tag))
		how = port_reset_ask(&a);
	else
		how = port_menu(&a);
	levels_game_paused(false);
	fprintf(stderr, "port menu: %s\n", how == PORT_MENU_QUIT ? "Quit" :
	        how == PORT_MENU_POWER ? "POWER" : how == PORT_MENU_RESTART ? "start again" :
	        how == PORT_MENU_RESET ? "Reset" : "Continue");
	/* Over your PICO-8, what portkeys reads again as the menu ends: the
	 * hotkeys in force, which its Controls may have changed; the shot's
	 * folder stays the one the hand-over named (src/portmenu.h) */
	if (port_hotkeys(s->tag)) p8_keys_save(s, g_port_file, NULL);
	/* MP4 opened from it: its queue back to the follower, as it is now */
	if (g_port_mp4_up) {
		book_keep(true);
		mp4_hand_save();
		texload_stop();
		/* And PICO-8 quiet or heard for what MP4 does now, before it goes
		 * on: Continue's QUIET over a Diatom game (gm_resume) */
		port_quiet_follow(true);
	}
	/* The window closed, which puts back on the panel the frame SDL found
	 * there - the port's - before the port has the display again */
	uisound_release();
	plat_input_quit();
	ui_quit();
	plat_video_quit();
	db_shutdown();
	return how;
}

/* --dmode NAME: every system shows that display mode (a shot of the row
 * and its caption) */
static const char *shot_dmode;

int main(int argc, char *argv[])
{
	app a = { 0 };
	char path[CFG_STR * 2];
	char startup[CFG_STR];
	bool hand_back = false;

	/* A crash says where it was, in the log and in a record the next start
	 * makes a report of (src/crashrec.h), in every one of this binary's
	 * modes */
	crashrec_install(CRASHREC_PATH, ZYPEROS_VERSION);
	/* The canvas of the handheld built for (src/canvas.h); --device below
	 * makes it another's on the host */
	canvas_init(plat_profile());
	if (argc > 1 && !strcmp(argv[1], PORT_MENU_ARG)) return port_menu_main(argc, argv);
	if (argc > 1 && !strcmp(argv[1], MP4_FOLLOW_ARG)) return mp4_follow_main();
	if (argc > 1 && !strcmp(argv[1], STATS_PORT_PLAYED_ARG)) return port_played_main();
	if (argc > 1 && !strcmp(argv[1], ALSA_TRY_ARG)) return alsa_try_main(argc, argv);
	/* The launcher before this one crashed: its record made a report, as a
	 * kernel panic's is, by plastron's crashlog (the Pixel 2's) */
	if (crashrec_waiting(CRASHREC_PATH) && access(CRASHLOG, X_OK) == 0) {
		char *cl[] = { CRASHLOG, "--launcher", CRASHREC_PATH, NULL };

		fprintf(stderr, "crash: the launcher before this one crashed: %s\n",
		        plat_spawn_detached(cl, NULL, "/") ? "its record handed to crashlog" : "crashlog would not start");
	}
#if defined(__linux__) && !defined(ZYPEROS_DESK)
	/* The risky parts' lines in the kernel's log too, which outlives a
	 * freeze, and on the card at once (klog.h) */
	klog_open(NULL);
#endif

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
			shot_path = argv[++i];
			/* A shot puts no window on the desktop of whoever is using the
			 * computer, whether or not the caller asked (src/device/brick.c,
			 * plat_video_init): a hidden window the software renderer
			 * draws, on SDL's dummy video driver, which needs no OpenGL.
			 * Before any SDL call, since the driver is chosen at the first.
			 * A variable the caller set stays as it set it: the check
			 * scripts set both, as they did before. */
			setenv("ZYPEROS_HEADLESS", "1", 0);
			setenv("SDL_VIDEODRIVER", "dummy", 0);
		} else if (!strcmp(argv[i], "--screen") && i + 1 < argc)
			shot_screen = strcmp(argv[++i], "games") == 0 ? SCREEN_GAMES
			                                              : SCREEN_SYSTEMS;
		/* --menu [row] draws the ZyperOS menu over whichever screen --screen
		 * asked for, so the panel can be looked at without a device. The row
		 * is its number or its label ("Clock"), which stays the same row when
		 * one is added above it. */
		else if (!strcmp(argv[i], "--menu")) {
			shot_menu = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_menu_sel = atoi(argv[++i]);
			else if (i + 1 < argc && argv[i + 1][0] != '-')
				shot_menu_label = argv[++i];
		}
		else if (!strcmp(argv[i], "--menu-steps") && i + 1 < argc)
			shot_menu_steps = atoi(argv[++i]);
		/* --menu-tab N: MENU on tab N (pm_tab, 0 Games to 5 About), and
		 * --menu-tab-from M MS on its way there from tab M, MS into the
		 * move (DESIGN.md 11.3) */
		else if (!strcmp(argv[i], "--look") && i + 1 < argc)
			shot_look = argv[++i];
		/* --homesys-page ROW: MENU > Display > Home Systems' page, the
		 * cursor on ROW (src/homesys_screen.c) */
		else if (!strcmp(argv[i], "--homesys-page") && i + 1 < argc)
			shot_homesys = atoi(argv[++i]);
		/* --lights-page ROW: MENU > Side Lights' page, the cursor on ROW
		 * (src/lights_screen.c) */
		else if (!strcmp(argv[i], "--lights-page") && i + 1 < argc)
			shot_lights = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--look-page") && i + 1 < argc) {
			shot_look_page = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_look_row = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--menu-tab") && i + 1 < argc)
			g_tabs.at = g_tabs.from = atoi(argv[++i]) % PT_TABS;
		else if (!strcmp(argv[i], "--menu-tab-from") && i + 2 < argc) {
			shot_tab_from = atoi(argv[++i]) % PT_TABS;
			shot_tab_ms = atoi(argv[++i]);
		}
		/* --slots <sel> [aspect] draws one frame of the save/load carousel
		 * over synthetic frames, which is the only way to look at it without
		 * a game running on a device. */
		/* --jump N applies N letter-jumps before the shot (negative for up),
		 * so the d-pad's behavior on a real library can be checked without a
		 * device or a hand on it. */
		else if (!strcmp(argv[i], "--jump") && i + 1 < argc) {
			shot_jump = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--folder") && i + 1 < argc) shot_folder = argv[++i];
		else if (!strcmp(argv[i], "--shelf-press") && i + 1 < argc) shot_shelf_press = argv[++i];
		else if (!strcmp(argv[i], "--game") && i + 1 < argc) shot_game = argv[++i];
		else if (!strcmp(argv[i], "--view") && i + 1 < argc) {
			shot_view = argv[++i];
			if (strcmp(shot_view, "list") && strcmp(shot_view, "covers") && strcmp(shot_view, "grid") &&
			    strcmp(shot_view, "video")) {
				fprintf(stderr, "--view needs list, grid, video or covers\n");
				return 2;
			}
		}
		/* --keyboard [layer] [text] [title] draws one frame of the text-entry
		 * panel, for the same reason --menu and --slots exist: it is dense, and
		 * laying it out against a screenshot beats a round trip to a device.
		 * --kb-ss-user titles it as the ScreenScraper sign-in does, in the
		 * shot's language. */
		else if (!strcmp(argv[i], "--cheevos-screen")) {
			shot_cheevos = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_cheevos_sel = atoi(argv[++i]);
			/* A game name, so the heading can be looked at with one of the
			 * long ones rather than only the short one that fit. */
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_cheevos_game = argv[++i];
		}
		else if (!strcmp(argv[i], "--cheevo-card") && i + 1 < argc)
			shot_cheevo_card = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cheevos-badge") && i + 2 < argc) {
			shot_badge_set = argv[++i];
			shot_badge_name = argv[++i];
		}
		else if (!strcmp(argv[i], "--synopsis")) shot_syn = 1;
		/* --nowplaying ALBUM [TRACK [SECONDS]] draws Now Playing for an album
		 * of the card's Music folder, by its number in the scan, without a
		 * daemon: the cover has to be on the card already, or the album is
		 * drawn as one with none. --paused draws it paused. */
		else if (!strcmp(argv[i], "--nowplaying") && i + 1 < argc) {
			shot_np = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_np_track = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_np_at = (float)atof(argv[++i]);
		}
		else if (!strcmp(argv[i], "--paused")) shot_np_paused = 1;
		/* --np-look cover|classic|spectrum...: Now Playing and MENU's
		 * Player Look in that look, for the shot only, never saved */
		else if (!strcmp(argv[i], "--np-look") && i + 1 < argc) np_look_shot(mp4l_from_key(argv[++i]));
		/* --np-tape pinstripe|band|metal|...: Cassette's tape style, for
		 * the shot only, never saved */
		else if (!strcmp(argv[i], "--np-tape") && i + 1 < argc) np_tape_shot(mp4t_from_key(argv[++i]));
		/* --np-tape-said: the style's name in its pill, as Up or Down
		 * leaves it */
		else if (!strcmp(argv[i], "--np-tape-said")) npl_tape_said();
		/* --np-bench N: N frames of the look timed before the shot */
		else if (!strcmp(argv[i], "--np-bench") && i + 1 < argc) shot_np_bench = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--locked")) shot_locked = 1;
		else if (!strcmp(argv[i], "--video")) {
			/* "controls": with the controls up; "hidden" ("full" before
			 * the one view) or nothing: the picture alone, as it is 3 s
			 * after the last press and under the lock */
			shot_video = 0;
			if (i + 1 < argc && !strcmp(argv[i + 1], "controls")) { shot_video = 1; i++; }
			else if (i + 1 < argc && (!strcmp(argv[i + 1], "hidden") || !strcmp(argv[i + 1], "full"))) i++;
		}
		else if (!strcmp(argv[i], "--video-size") && i + 1 < argc)
			sscanf(argv[++i], "%dx%d", &shot_video_w, &shot_video_h);
		else if (!strcmp(argv[i], "--video-trouble") && i + 1 < argc) {
			i++;
			shot_video_trouble = !strcmp(argv[i], "too-big") ? MPF_TOO_BIG
			                   : !strcmp(argv[i], "no-picture") ? MPF_NO_PICTURE : MPF_FINE;
		}
		else if (!strcmp(argv[i], "--video-at") && i + 2 < argc) {
			shot_video_k = (float)atof(argv[++i]);
			shot_video_grow = (float)atof(argv[++i]);
		}
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc) shot_mode = argv[++i];
		else if (!strcmp(argv[i], "--sysmove") && i + 1 < argc)
			shot_sysmove = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--gamemove") && i + 1 < argc)
			shot_gamemove = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--shot-at") && i + 1 < argc) {
			shot_at = atoi(argv[++i]);
			if (shot_at < 0) shot_at = 0;
		}
		else if (!strcmp(argv[i], "--launch")) shot_launch = 1;
		else if (!strcmp(argv[i], "--info")) shot_info = 1;
		/* The details page with the cursor on Delete Game, and its question
		 * over the shelf (DESIGN.md 11.31) */
		else if (!strcmp(argv[i], "--info-delete")) shot_info = 2;
		else if (!strcmp(argv[i], "--info-rename")) shot_info = 3;
		/* Collections (src/collect_screen.h): the details page on Add to
		 * Collection, its list with the cursor on row K, and Delete
		 * Collection's question on the Collections shelf */
		else if (!strcmp(argv[i], "--info-collect")) shot_info = 5;
		else if (!strcmp(argv[i], "--collect-page") && i + 1 < argc) shot_collect_page = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--collect-delete")) shot_collect_delete = 1;
		else if (!strcmp(argv[i], "--collect-said") && i + 1 < argc) shot_collect_said = atoi(argv[++i]);
		/* Delete Screenshot's question on the Screenshots shelf (DESIGN.md 11.68) */
		else if (!strcmp(argv[i], "--shots-delete")) shot_shots_delete = 1;
		else if (!strcmp(argv[i], "--shots-said")) shot_shots_said = 1;
		else if (!strcmp(argv[i], "--shots-delete-now")) shot_shots_delete_now = 1;
		else if (!strcmp(argv[i], "--zip-said")) shot_zip_said = 1;
		else if (!strcmp(argv[i], "--info-art")) shot_info_art = true;
		/* The Buttons page a game with its own buttons opens from its details */
		else if (!strcmp(argv[i], "--info-buttons")) shot_info = 4;
		/* ...and the details with the cursor on its Buttons row, which a
		 * scraped game's facts and its File put below the first screenful */
		else if (!strcmp(argv[i], "--info-on-buttons")) shot_info = 6;
		else if (!strcmp(argv[i], "--delete-ask")) shot_delete_ask = 1;
		/* --gm-said K: Delete Game's and Rename's messages (shots.c's
		 * GM_SAID: 0 the delete's wait, 1-6 Delete Game's, 7-10 Rename's) */
		else if (!strcmp(argv[i], "--gm-said") && i + 1 < argc) shot_gm_said = atoi(argv[++i]);
		/* The game under the cursor deleted as Delete Game's yes deletes it,
		 * then the shot: the shelf after, and what went on stderr */
		else if (!strcmp(argv[i], "--delete-now")) shot_delete_now = 1;
		/* --osd volume|brightness|power LEVEL [+CELLS]: the settings bar at
		 * that level, on top of the rest of the shot; power is POWER's hold,
		 * LEVEL segments of OSDBAR_POWER_STEPS lit (MENU > Power Button on
		 * Hold); +CELLS, the boost past the top open with that many filled
		 * (src/boost.h). */
		else if (!strcmp(argv[i], "--osd") && i + 2 < argc) {
			shot_osd_kind = !strcmp(argv[i + 1], "brightness") ? OSDBAR_BRIGHT
			              : !strcmp(argv[i + 1], "power") ? OSDBAR_POWER : OSDBAR_VOLUME;
			shot_osd_val = atoi(argv[i + 2]);
			i += 2;
			if (i + 1 < argc && argv[i + 1][0] == '+') shot_osd_boost = atoi(argv[++i] + 1);
		}
		/* --theme light|dark: the shot in that theme, whatever the saved
		 * Appearance is, so a screen can be looked at in both without a
		 * device and without changing the setting. Auto is not offered: a
		 * shot of a screen is a shot of one theme, and Auto's would be the
		 * theme of the minute it was taken in. An unknown word stops the
		 * run, since a picture in the other theme than the one asked for is
		 * what a typo would otherwise give and nobody would see why. */
		else if (!strcmp(argv[i], "--song") && i + 1 < argc) shot_song = argv[++i];
		else if (!strcmp(argv[i], "--song-paused")) shot_song_paused = true;
		else if (!strcmp(argv[i], "--home")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "--home needs spotlight, gallery or grid\n"); return 2;
			}
			const char *value = argv[++i];
			if (strcmp(value, "spotlight") && strcmp(value, "gallery") && strcmp(value, "grid")) {
				fprintf(stderr, "--home needs spotlight, gallery or grid\n"); return 2;
			}
			shot_home = home_parse(value);
		}
		/* --ways usb|wifi|both|none[,brick]: which of the empty home's ways
		 * can start, and a Brick-like device (no shared port, Wi-Fi built in)
		 * with ",brick"; the shot draws them as the device would */
		else if (!strcmp(argv[i], "--cheer") && i + 1 < argc) shot_cheer = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--songpop") && i + 1 < argc) {
			shot_songpop = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_songpop_ms = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--ways") && i + 1 < argc) {
			const char *w = argv[++i];

			g_eh_shot = !strncmp(w, "usb", 3) ? 1 << EH_USB : !strncmp(w, "wifi", 4) ? 1 << EH_WIFI
			          : !strncmp(w, "both", 4) ? (1 << EH_USB) | (1 << EH_WIFI) : 0;
			if (strstr(w, ",brick")) g_eh_shot |= 4;
		}
		else if (!strcmp(argv[i], "--lang") && i + 1 < argc) shot_lang = argv[++i];
		else if (!strcmp(argv[i], "--theme") && i + 1 < argc) {
			i++;
			if (!strcmp(argv[i], "light")) shot_theme = APPEAR_LIGHT;
			else if (!strcmp(argv[i], "dark")) shot_theme = APPEAR_DARK;
			else {
				fprintf(stderr, "--theme: %s is not light or dark\n", argv[i]);
				return 2;
			}
		}
		/* --status "WORDS": what the shelf's status corner shows, for a
		 * host with no radio and no battery. Words, in any order:
		 * H:MM (the time of day; none is a clock never set), 24h or 12h,
		 * wifi=off|idle|1|2|3, bt, batt=PCT, chg, and blinkoff for a
		 * critical battery caught in its second off, eta=MIN for what
		 * MENU > Battery Icon > Time Left writes, icon=bars|percentage|time
		 * for the setting itself, date=YYYY-MM-DD for the day (today
		 * without it). */
		else if (!strcmp(argv[i], "--status") && i + 1 < argc) {
			if (!shot_status_parse(argv[++i])) {
				fprintf(stderr, "--status: cannot read \"%s\"\n", argv[i]);
				return 2;
			}
		}
		/* --gamemenu [row] [dual]: the menu over a paused game, that row
		 * selected, with a Nintendo DS game's three screen rows when dual */
		else if (!strcmp(argv[i], "--ingame")) shot_ingame = 1;
		else if (!strcmp(argv[i], "--handheld-box")) {
			shot_handheld_box = "all";
			if (i + 1 < argc && (!strcmp(argv[i + 1], "tab") || !strcmp(argv[i + 1], "tour")))
				shot_handheld_box = argv[++i];
		}
		else if (!strcmp(argv[i], "--ask") && i + 1 < argc) {
			shot_ask = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_ask_item = argv[++i];
		}
		else if (!strcmp(argv[i], "--confirm-fit")) shot_confirm_fit = true;
		else if (!strcmp(argv[i], "--controls-fit")) {
			shot_controls_fit = "all";
			if (i + 1 < argc && (!strcmp(argv[i + 1], "game") || !strcmp(argv[i + 1], "home")))
				shot_controls_fit = argv[++i];
		}
		else if (!strcmp(argv[i], "--gamecontrols")) {
			shot_gamecontrols = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_gamecontrols = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= 'A' && argv[i + 1][0] <= 'Z')
				shot_gamecontrols_tag = argv[++i];
		}
		else if (!strcmp(argv[i], "--gamemenu")) {
			shot_gamemenu = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_gamemenu_sel = atoi(argv[++i]);
			if (i + 1 < argc && !strcmp(argv[i + 1], "dual")) {
				g_game_dual = true;
				i++;
			} else if (i + 1 < argc && !strcmp(argv[i + 1], "port")) {
				g_port_menu = true;
				i++;
			}
		}
		/* --gm-tab N: the in-game menu's tab, 0 Game, 1 Display, 2 Controls */
		else if (!strcmp(argv[i], "--gm-tab") && i + 1 < argc) shot_gamemenu_tab = atoi(argv[++i]);
		/* --dmode NAME: every system's display mode, for a shot of its row
		 * and its caption (display.<TAG>'s value: stretch, aspect, ...) */
		else if (!strcmp(argv[i], "--dmode") && i + 1 < argc) shot_dmode = argv[++i];
		/* The Controls tab's (shot_draw_ctltab) */
		else if (!strcmp(argv[i], "--gm-all")) shot_gm_all = true;
		else if (!strcmp(argv[i], "--gm-pick")) shot_gm_pick = true;
		else if (!strcmp(argv[i], "--gm-ask")) shot_gm_ask = true;
		else if (!strcmp(argv[i], "--gm-keys") && i + 1 < argc) shot_gm_keys = atoi(argv[++i]);
		/* --gm-discs N [LABEL]: a game on N discs, disc 2 in the tray, the
		 * core calling it LABEL (none: the row says Disc 2 of N) */
		else if (!strcmp(argv[i], "--gm-discs") && i + 1 < argc) {
			g_shot_discs = atoi(argv[++i]);
			g_gm_disc.index = g_shot_discs > 1 ? 1 : 0;
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) g_shot_disc_label = argv[++i];
		}
		/* --gm-undo: the game has a state from before its last load, so
		 * the Game tab has Undo Load */
		else if (!strcmp(argv[i], "--gm-undo")) g_shot_undo = true;
		/* --gm-rumble: the game's core asked for the motor (PAUSED
		 * rumble=1), so its Controls tab has Rumble */
		else if (!strcmp(argv[i], "--gm-rumble")) g_shot_rumble = true;
		/* --eq-phones: the Equalizer page as with headphones in */
		else if (!strcmp(argv[i], "--eq-phones")) g_shot_eq_phones = true;
		else if (!strcmp(argv[i], "--gm-tab-from") && i + 1 < argc) shot_gm_from = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--gm-press") && i + 1 < argc) shot_gm_press = argv[++i];
		else if (!strcmp(argv[i], "--gm-choose")) shot_gm_choose = 1;
		/* --gm-restart: over your PICO-8 (--gamemenu N port on its
		 * shelf), the question a Display mode asks, --pick-at's mode */
		else if (!strcmp(argv[i], "--gm-restart")) {
			shot_gm_restart = 1;
			/* --gm-restart buttons: the question for new buttons */
			if (i + 1 < argc && !strcmp(argv[i + 1], "buttons")) shot_gm_restart = 2, i++;
		}
		/* --gm-reset: over your PICO-8, Reset's question */
		else if (!strcmp(argv[i], "--gm-reset")) shot_gm_reset = 1;
		/* --hud-page ROW: the On-Screen Stats page (src/hudmenu_screen.h)
		 * over the paused game, the cursor on ROW, with --gamemenu;
		 * --hud-items, its Shows list instead; --hud-pick ROW, that row's
		 * list of choices; --hud-shows WORDS, the items it shows */
		else if (!strcmp(argv[i], "--hud-page") && i + 1 < argc) shot_hud_page = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cheats")) shot_cheats = true;
		else if (!strcmp(argv[i], "--cheats-page") && i + 1 < argc) shot_cheats_page = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cheats-ask")) shot_cheats_ask = true;
		else if (!strcmp(argv[i], "--cheats-query") && i + 1 < argc) shot_cheats_query = argv[++i];
		else if (!strcmp(argv[i], "--cheats-on-first")) shot_cheats_on_first = true;
		else if (!strcmp(argv[i], "--cheats-note")) shot_cheats_note = true;
		else if (!strcmp(argv[i], "--cheats-crash")) shot_cheats_crash = true;
		else if (!strcmp(argv[i], "--cheats-crashed")) g_shot_cheats_crashed = true;
		else if (!strcmp(argv[i], "--hud-items")) shot_hud_items = true;
		else if (!strcmp(argv[i], "--hud-pick") && i + 1 < argc) shot_hud_pick = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hud-shows") && i + 1 < argc) shot_hud_shows = argv[++i];
		else if (!strcmp(argv[i], "--hud-alt")) shot_hud_alt = true;
		/* --pick-at K: an option list's cursor on choice K; --gm-source
		 * WxH[@ASPECT]: the core's picture the in-game menu's frame stands
		 * for, which the Display list's picture draws with */
		else if (!strcmp(argv[i], "--pick-at") && i + 1 < argc) shot_pick_at = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--pick-en") && i + 1 < argc) shot_pick_en = argv[++i];
		else if (!strcmp(argv[i], "--expect-en") && i + 1 < argc) shot_expect_en = argv[++i];
		else if (!strcmp(argv[i], "--gm-source") && i + 1 < argc) {
			const char *v = argv[++i], *at = strchr(v, '@');

			if (sscanf(v, "%dx%d", &shot_gm_src_w, &shot_gm_src_h) != 2) shot_gm_src_w = shot_gm_src_h = 0;
			shot_gm_aspect = at ? atof(at + 1) : 4.0 / 3.0;
		}
		else if (!strcmp(argv[i], "--rows-report")) shot_rows_report = 1;
		else if (!strcmp(argv[i], "--hover-sweep")) shot_hover_sweep = 1;
		else if (!strcmp(argv[i], "--backdrop") && i + 1 < argc) shot_backdrop = argv[++i];
		else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
			i++;
			shot_key = !strcmp(argv[i], "left")  ? IN_LEFT
			           : !strcmp(argv[i], "right") ? IN_RIGHT
			           : !strcmp(argv[i], "l1")    ? IN_L1
			           : !strcmp(argv[i], "r1")    ? IN_R1
			           : !strcmp(argv[i], "a")     ? IN_ACCEPT : -1;
			if (shot_key < 0) {
				fprintf(stderr, "--press: %s is not left, right, l1, r1 or a\n", argv[i]);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--flip")) shot_flip = 1;
		else if (!strcmp(argv[i], "--tour") && i + 1 < argc) {
			int k;

			i++;
			for (k = 0; k < TOUR_STEPS && strcmp(TOUR_STEP_NAMES[k], argv[i]); k++) ;
			if (k == TOUR_STEPS && strcmp(argv[i], "skip")) {
				fprintf(stderr, "--tour: %s is no step\n", argv[i]);
				return 2;
			}
			shot_tour = k == TOUR_STEPS ? -1 : k;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_tour_ms = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_tour_done = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--tour-wrong") && i + 1 < argc) {
			int k;

			i++;
			/* By tour_key_name, any case: up ... volume, brightness, power */
			for (k = 0; k < TK_KEYS && strcasecmp(tour_key_name((tour_key)k), argv[i]); k++) ;
			if (k == TK_KEYS) {
				fprintf(stderr, "--tour-wrong: %s is no button\n", argv[i]);
				return 2;
			}
			shot_tour_wrong = k;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_tour_wrong_ms = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--fn-volume")) shot_fn_volume = 1;
		/* --device ID: wear that handheld's profile (src/device/profile.h),
		 * its buttons, chords and answers, for shots and checks on a computer;
		 * a device build has its own alone */
		else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
			if (!plat_profile_select(argv[++i])) {
				char ids[96];

				plat_profile_ids(ids, sizeof ids);
				fprintf(stderr, "--device: no profile \"%s\" (%s)\n", argv[i], ids);
				return 2;
			}
			/* Its panel's shape is the canvas's (src/canvas.h) */
			canvas_init(plat_profile());
		}
		else if (!strcmp(argv[i], "--tour-pick") && i + 1 < argc) {
			shot_tour_pick = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_tour_pick_ms = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--tour-music")) shot_tour_music = true;
		else if (!strcmp(argv[i], "--tour-held") && i + 1 < argc) shot_tour_held = atoi(argv[++i]);
		/* --port-waiting: with --gamemenu N port, the menu over a program
		 * still starting (Keep Waiting) */
		else if (!strcmp(argv[i], "--port-waiting")) g_port_waiting = true;
		/* --portload-later: the still with the line after a minute */
		else if (!strcmp(argv[i], "--portload-later")) g_port_loading_later = true;
		else if (!strcmp(argv[i], "--portload") && i + 1 < argc)
			shot_portload = argv[++i];
		/* --pico8-page: the page A opens on Splore while your PICO-8 is not
		 * on the card (src/pico8.h) */
		else if (!strcmp(argv[i], "--pico8-page")) g_p8_shot = true;
		else if (!strcmp(argv[i], "--pico8-said") && i + 1 < argc) g_p8_said_shot = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--portmaster")) {
			/* The Install PortMaster page, as ZYPEROS_PM_STATUS's file
			 * says, with any of running, nonet, nopython, installed; and
			 * Reinstall PortMaster's with repair, or failed (the error
			 * ZYPEROS_PM_WHY says, a missing module when it says none) */
			g_pm_shot = 0;
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				const char *w = argv[++i];
				if (strstr(w, "running"))   g_pm_shot |= PM_SHOT_RUNNING;
				if (strstr(w, "nonet"))     g_pm_shot |= PM_SHOT_NONET;
				if (strstr(w, "nopython"))  g_pm_shot |= PM_SHOT_NOPYTHON;
				if (strstr(w, "installed")) g_pm_shot |= PM_SHOT_INSTALLED;
				if (strstr(w, "repair"))    g_pm_shot |= PM_SHOT_REPAIR;
				if (strstr(w, "failed"))    g_pm_shot |= PM_SHOT_FAILED;
			}
		}
		else if (!strcmp(argv[i], "--storage-sort") && i + 1 < argc) {
			const char *w = argv[++i];

			g_store_sort_shot = !strcmp(w, "az") ? ST_SORT_AZ : !strcmp(w, "last") ? ST_SORT_LAST : ST_SORT_SIZE;
		}
		else if (!strcmp(argv[i], "--media")) {
			shot_media = "page";
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_media = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') shot_media_tile = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--media-art") && i + 1 < argc) shot_media_art = argv[++i];
		else if (!strcmp(argv[i], "--storage")) {
			shot_storage = "main";
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_storage = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				i++;
				if (argv[i][0] >= '0' && argv[i][0] <= '9') shot_storage_sel = atoi(argv[i]);
				else shot_storage_row = argv[i];
			}
		}
		else if (!strcmp(argv[i], "--ranews") && i + 1 < argc) {
			shot_ranews = atoi(argv[++i]);
			if (shot_heatpop < 0) shot_heatpop = 3000;
		}
		/* --achtab STATE: the in-game menu's Achievements tab (--gm-tab 3) in
		 * one state, a made-up set for list and all: list, all (every one
		 * earned), not-system, not-file, signed-out, waiting, unchecked,
		 * none */
		else if (!strcmp(argv[i], "--achtab") && i + 1 < argc) {
			static const char *const names[] = {
				"list", "not-system", "not-file", "signed-out", "waiting",
				"unchecked", "none", "paused", "all"
			};
			const char *v = argv[++i];
			int k;

			for (k = 0; k < (int)(sizeof names / sizeof names[0]); k++)
				if (!strcmp(v, names[k])) shot_achtab = k;
			if (shot_achtab < 0) {
				fprintf(stderr, "--achtab: no state %s\n", v);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--cheevos-waiting") && i + 1 < argc)
			shot_cheevos_waiting = atoi(argv[++i]);
		/* The Achievements rows of a game never played with a network, in
		 * the in-game menu (--gamemenu) and the game's details (--info) */
		else if (!strcmp(argv[i], "--cheevos-unchecked")) g_shot_unchecked = true;
		/* ...and of one with no hash here: "file" or "system" */
		else if (!strcmp(argv[i], "--cheevos-no-hash") && i + 1 < argc) {
			i++;
			g_shot_no_hash = !strcmp(argv[i], "system") ? RA_HASH_NO_SYSTEM
			                                            : RA_HASH_NO_FILE;
		}
		/* --lastboot SUMMARY: Zyper's bubble saying the last start's
		 * restart, SUMMARY as crashlog writes it with ";" for its line
		 * ends ("stage=K1;reason=W;kernel_time=0.62") */
		else if (!strcmp(argv[i], "--lastboot") && i + 1 < argc) {
			shot_lastboot = argv[++i];
			if (shot_heatpop < 0) shot_heatpop = 3000;
		}
		else if (!strcmp(argv[i], "--upnews") && i + 1 < argc) {
			shot_upnews = argv[++i];
			if (shot_heatpop < 0) shot_heatpop = 3000;
		}
		/* What's New scrolled N lines (System Update's page) */
		else if (!strcmp(argv[i], "--update-scroll") && i + 1 < argc) {
			g_up_scroll = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--update")) {
			shot_update = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_update = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--music-page")) {
			shot_music_page = 0;
			if (i + 1 < argc && !strcmp(argv[i + 1], "foot")) {
				shot_music_page = -1;
				i++;
			} else if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_music_page = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_music_steps = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--language")) {
			shot_language = -1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_language = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--about-scroll") && i + 1 < argc) {
			shot_about_scroll = (float)atof(argv[++i]);
			if (shot_about_scroll < 0) shot_about_scroll = 0;
			if (shot_about_scroll > 1) shot_about_scroll = 1;
		}
		else if (!strcmp(argv[i], "--about")) {
			shot_about = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] < '0' + ABOUT_PAGES &&
			    !argv[i + 1][1])
				shot_about = atoi(argv[++i]);
		}
		/* --snap K: the games shelf with its focused cover K of the way
		 * into a video snap (a test picture), 0 to 1 */
		else if (!strcmp(argv[i], "--snap") && i + 1 < argc) {
			g_shot_snap_k = (float)atof(argv[++i]);
			if (g_shot_snap_k < 0) g_shot_snap_k = 0;
			if (g_shot_snap_k > 1) g_shot_snap_k = 1;
		}
		/* --saver [cover]: the screensaver, over a test picture; --saver-alpha F
		 * how far the clip has faded up, 0 to 1 */
		else if (!strcmp(argv[i], "--saver")) {
			g_shot_saver = i + 1 < argc && !strcmp(argv[i + 1], "cover") ? (i++, 1) : 0;
		}
		else if (!strcmp(argv[i], "--saver-pic") && i + 1 < argc) {
			g_shot_saver_pic = argv[++i];
		}
		else if (!strcmp(argv[i], "--saver-alpha") && i + 1 < argc) {
			g_shot_saver_alpha = (float)atof(argv[++i]);
		}
		/* --sheen T: the games shelf with the light T of the way across its
		 * focused card with no art; --sharp K its frame of play K of the
		 * way from blurred to sharp (src/focusfx.h), 0 to 1 */
		else if (!strcmp(argv[i], "--sheen") && i + 1 < argc) {
			g_shot_sheen = fminf(1, fmaxf(0, (float)atof(argv[++i])));
		}
		else if (!strcmp(argv[i], "--sharp") && i + 1 < argc) {
			g_shot_sharp = fminf(1, fmaxf(0, (float)atof(argv[++i])));
		}
		/* --snap-wait F: the games shelf with the bar under its focused
		 * cover F of the way through the wait for its snap, 0 to 1 */
		else if (!strcmp(argv[i], "--band") && i + 1 < argc) {
			g_shot_band = (float)atof(argv[++i]);
			if (g_shot_band < 0) g_shot_band = 0;
			if (g_shot_band > 1) g_shot_band = 1;
		}
		else if (!strcmp(argv[i], "--snap-wait") && i + 1 < argc) {
			g_shot_snap_wait = (float)atof(argv[++i]);
			if (g_shot_snap_wait < 0) g_shot_snap_wait = 0;
			if (g_shot_snap_wait > 1) g_shot_snap_wait = 1;
		}
		else if (!strcmp(argv[i], "--scraper")) {
			shot_scraper = 3;
			if (i + 1 < argc && argv[i + 1][0] >= '3' && argv[i + 1][0] <= '4')
				shot_scraper = atoi(argv[++i]);
		}
		/* --scraper-names-off: Names and Details off, as if switched */
		else if (!strcmp(argv[i], "--scraper-names-off")) g_scp_names = 0;
		/* --radl-page STATE: MENU > Achievements' page (radl_page_shot) */
		else if (!strcmp(argv[i], "--radl-page") && i + 1 < argc)
			shot_radl_page = atoi(argv[++i]);
		/* --downloads STATE: MENU > Downloads (downloads_shot) */
		else if (!strcmp(argv[i], "--downloads") && i + 1 < argc)
			shot_downloads = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--scraper-page") && i + 1 < argc) {
			shot_scraper_page = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_scraper_page_t = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--sgdb")) {
			shot_sgdb = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '2')
				shot_sgdb = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--hotkeys") && i + 1 < argc) shot_hotkeys = atoi(argv[++i]);
		/* MENU's chords: the lock over the screen, the unlock held K of
		 * the way (0 to 1); the screenshot's notice */
		else if (!strcmp(argv[i], "--menu-lock") && i + 1 < argc) shot_menu_lock = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--menu-shot-notice")) shot_menu_notice = true;
		else if (!strcmp(argv[i], "--hk-game")) shot_hk_game = true;
		else if (!strcmp(argv[i], "--hk-ask") && i + 1 < argc) shot_hk_ask = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hk-scope") && i + 1 < argc) shot_hk_scope = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hk-capture") && i + 1 < argc) shot_hk_capture = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hk-key") && i + 1 < argc) shot_hk_key = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--glyph-warm")) shot_glyph_warm = true;
		else if (!strcmp(argv[i], "--controls")) {
			shot_controls = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_controls = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--phase") && i + 1 < argc)
			shot_phase = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--focus-from") && i + 1 < argc)
			shot_focus_from = atoi(argv[++i]);
		/* --zyper [works|fails]: Zyper in the top menu's --menu shot,
		 * --shot-at's time after MENU opened it (--open), and a success or a
		 * failure at 500 ms. A still of him needs a moment, as a marquee's
		 * does, and the moment is the one clock every shot keeps: a time
		 * here, as --zyper once took, is not his. */
		else if (!strcmp(argv[i], "--zyper")) {
			shot_press = SHOT_OPEN;
			if (i + 1 < argc && !strcmp(argv[i + 1], "works")) { shot_zyper_did = 1; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "fails")) { shot_zyper_did = -1; i++; }
			else if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
				fprintf(stderr, "--zyper takes its time from --shot-at: "
				        "--zyper --shot-at %s\n", argv[i + 1]);
				return 2;
			}
		}
		/* --open, --close, --swap: what --shot-at presses on --menu's top
		 * menu - MENU opening it on the shelf, B closing it onto the shelf,
		 * or A on its Controls row, the menu leaving and MENU > Controls
		 * (--controls' page, 0 by default) arriving - for frame strips of
		 * the menus' motion (DESIGN.md 8.2). Without --shot-at, the menu at
		 * rest. */
		else if (!strcmp(argv[i], "--open"))  shot_press = SHOT_OPEN;
		else if (!strcmp(argv[i], "--close")) shot_press = SHOT_CLOSE;
		else if (!strcmp(argv[i], "--swap")) {
			shot_press = SHOT_SWAP;
			if (shot_controls < 0) shot_controls = 0;
		}
		/* --poweroff: one frame of the power-off animation, --shot-at's time
		 * in, which the device otherwise shows only as it switches itself
		 * off */
		else if (!strcmp(argv[i], "--poweroff")) {
			shot_poweroff = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
				fprintf(stderr, "--poweroff takes its time from --shot-at: "
				        "--poweroff --shot-at %s\n", argv[i + 1]);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--usb-ask")) {
			shot_usb = 0;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_usb = atoi(argv[++i]);
			if (i + 1 < argc && !strcmp(argv[i + 1], "plain")) { shot_usb_dev = false; i++; }
			/* "drive": with USB Drive, as an image with usbdrive asks it */
			if (i + 1 < argc && !strcmp(argv[i + 1], "drive")) { shot_usb_drive = true; i++; }
		}
		/* --usb-drive N: USB Drive's picture N (connected, finishing, failed) */
		else if (!strcmp(argv[i], "--usb-drive") && i + 1 < argc) {
			shot_usbdrive = atoi(argv[++i]);
			/* The tiles' numbers: a copy to the card, one from it, both
			 * ways, or the widest */
			if (i + 1 < argc) {
				static const char *const V[] = { "idle", "in", "out", "both", "wide", "bright" };
				int v;

				for (v = 0; v < 6; v++)
					if (!strcmp(argv[i + 1], V[v])) { shot_usbdrive_vals = v; i++; break; }
			}
		}
		/* MENU's USB rows on the host, which has no usbrole: Ask, or
		 * Automatic with "auto"; "said" for Automatic's message */
		/* The replug message up, as on the device: drawn over the frame
		 * where the overlay goes (plat_draw_osd), for the home's focus
		 * check (tools/home-shot-check.py) */
		else if (!strcmp(argv[i], "--replug")) {
			g_replug.on = true;
			g_replug.plug = 1;
			g_replug.say = USBPOWER_SAY_COMPUTER;
			if (i + 1 < argc && !strcmp(argv[i + 1], "phone")) { g_replug.say = USBPOWER_SAY_PHONE; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "iphone")) { g_replug.say = USBPOWER_SAY_IPHONE; i++; }
		}
		else if (!strcmp(argv[i], "--usb-menu")) {
			shot_usb_menu = true;
			if (i + 1 < argc && !strcmp(argv[i + 1], "auto")) { g_usb_auto = true; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "said")) { g_usb_auto = true; shot_usb_said = true; i++; }
		}
		/* Zyper's offer of Automatic after a second USB Accessory */
		else if (!strcmp(argv[i], "--usb-offer")) shot_usb_offer = true;
		else if (!strcmp(argv[i], "--usb-power")) {
			shot_power = UP_COMPUTER;
			if (i + 1 < argc && !strcmp(argv[i + 1], "said")) { shot_power = UP_TILES; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "phone")) { shot_power = UP_TILES + 1; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "iphone")) { shot_power = UP_TILES + 2; i++; }
			else if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '2')
				shot_power = atoi(argv[++i]);
		}
		/* --datetime [K [MONTH]]: Date & Time's page K, on the 3rd of MONTH
		 * (1 to 12; October without it) */
		else if (!strcmp(argv[i], "--datetime")) {
			shot_datetime = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_datetime = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
				shot_datetime_mon = atoi(argv[++i]);
				if (shot_datetime_mon < 1 || shot_datetime_mon > 12) shot_datetime_mon = 10;
			}
		}
		else if (!strcmp(argv[i], "--dev-page")) {
			shot_dev = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_dev = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--xfer-ask")) {
			shot_xfer_ask = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_xfer_ask = atoi(argv[++i]);
			if (i + 1 < argc && !strcmp(argv[i + 1], "game")) { shot_xfer_game = true; i++; }
		}
		else if (!strcmp(argv[i], "--player2") && i + 1 < argc) {
			static char qr[160];
			const char *kind = argv[++i];
			p2_view *v = &g_shot_p2_view;

			g_shot_p2 = true;
			memset(v, 0, sizeof *v);
			v->st = P2_WAITING;
			v->link = P2_HOTSPOT;
			v->players = 2;
			v->ssid = "ZyperOS-7A2B";
			v->pass = "q7mx-3kpa";
			captive_wifi_qr(qr, sizeof qr, v->ssid, v->pass);
			v->qr = qr;
			v->url = P2_HOTSPOT_ADDR "/play";
			if (!strcmp(kind, "connected"))      { v->st = P2_CONNECTED; v->guests = 1; }
			else if (!strcmp(kind, "cable"))     { v->link = P2_CABLE; v->url = "zyperos.local/play"; }
			else if (!strcmp(kind, "off"))       v->st = P2_OFF;
			else if (!strcmp(kind, "off-cable")) { v->st = P2_OFF; v->link = P2_CABLE; }
			else if (!strcmp(kind, "none"))      { v->st = P2_OFF; v->link = P2_NO_LINK; }
			else if (!strcmp(kind, "failed"))    v->st = P2_FAILED;
			else if (!strcmp(kind, "starting"))  v->starting = true;
			else if (!strcmp(kind, "closing"))   v->closing = true;
			else if (!strcmp(kind, "closing-cable")) {
				v->closing = true;
				v->link = P2_CABLE;
				v->url = "zyperos.local/play";
			}
		}
		/* --link KIND: the Link Cable's page over a paused Game Boy game
		 * (src/link_screen.h's kinds: tiles, no-adapter, starting,
		 * waiting, looking, found, joining, linked, lost, failed,
		 * stopping, failed-network, -join, -busy, -game, -code); with --gamemenu the in-game menu, its row saying it */
		else if (!strcmp(argv[i], "--link") && i + 1 < argc) {
			if (!link_shot_parse(argv[++i])) {
				fprintf(stderr, "--link: no such kind %s\n", argv[i]);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--hotspot")) {
			shot_hot = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_ssid   = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_pass   = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_phones = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_now    = argv[++i];
		}
		/* --hotspot-in: the hotspot once someone came in (the news) */
		else if (!strcmp(argv[i], "--hotspot-in")) shot_hot_in = 1;
		/* --hare-addr ADDR: the way in's address (a long one, for the fit) */
		else if (!strcmp(argv[i], "--hare-addr") && i + 1 < argc) shot_steps_addr = argv[++i];
		else if (!strcmp(argv[i], "--hare-steps") && i + 2 < argc) {
			shot_steps_way = argv[++i];
			shot_steps_state = argv[++i];
			if (i + 1 < argc && !strcmp(argv[i + 1], "noname")) { shot_steps_noname = true; i++; }
		}
		/* --hare-move idle|in|out|both|closing|finish|gone: File Transfer's
		 * tiles, with --hare or --hotspot-in (the shot's MV) */
		else if (!strcmp(argv[i], "--hare-move") && i + 1 < argc) shot_hare_move = argv[++i];
		else if (!strcmp(argv[i], "--hare")) {
			shot_hare = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_addr = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_who  = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_head = argv[++i];
		}
		else if (!strcmp(argv[i], "--wait") && i + 1 < argc) {
			shot_wait = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_wait_msg = argv[++i];
		}
		else if (!strcmp(argv[i], "--notice") && i + 1 < argc) {
			shot_notice = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_notice_head = argv[++i];
		}
		else if (!strcmp(argv[i], "--notice-badge") && i + 1 < argc) {
			shot_notice_badge = argv[++i];
		}
		else if (!strcmp(argv[i], "--notice-progress") && i + 2 < argc) {
			shot_notice_value = atoi(argv[++i]);
			shot_notice_target = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--notice-kind") && i + 1 < argc) {
			const char *k = argv[++i];

			shot_notice_kind = !strcmp(k, "unlock") ? NQ_UNLOCK
			                 : !strcmp(k, "progress") ? NQ_PROGRESS : NQ_SET;
		}
		else if (!strcmp(argv[i], "--notice-heat")) shot_notice_mark = NOTICE_MARK_HEAT;
		else if (!strcmp(argv[i], "--popup") && i + 1 < argc) {
			shot_popup = argv[++i];
			if (!strcmp(shot_popup, "speed") && i + 1 < argc && argv[i + 1][0] != '-')
				shot_popup_half = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--charging") && i + 1 < argc) {
			shot_charging = atoi(argv[++i]);
			if (shot_charging < 0) shot_charging = 0;
			if (i + 1 < argc && !strcmp(argv[i + 1], "hold")) { shot_charging_kind = 1; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "unplugged")) { shot_charging_kind = 2; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "off")) { shot_charging_kind = 3; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "pending")) { shot_charging_kind = 4; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "kept")) { shot_charging_kind = 5; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "limit")) { shot_charging_kind = 6; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "keptfull")) { shot_charging_kind = 7; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "soon")) { shot_charging_kind = 8; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "tofull")) { shot_charging_kind = 9; i++; }
		}
		else if (!strcmp(argv[i], "--heatpop") && i + 1 < argc) {
			shot_heatpop = atoi(argv[++i]);
			if (shot_heatpop < 0) shot_heatpop = 0;
		}
		else if (!strcmp(argv[i], "--lowbatt") && i + 1 < argc) {
			shot_lowbatt = atoi(argv[++i]);
			if (shot_lowbatt < 0) shot_lowbatt = 0;
			if (i + 1 < argc && !strcmp(argv[i + 1], "charging")) { shot_lowbatt_kind = 1; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "off")) { shot_lowbatt_kind = 2; i++; }
		}
		else if (!strcmp(argv[i], "--keyboard")) {
			shot_kb = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '2')
				shot_kb_layer = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_kb_text = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_kb_title = argv[++i];
		}
		else if (!strcmp(argv[i], "--kb-ss-user")) shot_kb_ss_user = 1;
		else if (!strcmp(argv[i], "--kb-link")) shot_kb_link = 1;
		else if (!strcmp(argv[i], "--kb-rename-shown")) {
			shot_kb = 1;
			shot_kb_rename_shown = 1;
			shot_kb_text = "";
			if (i + 1 < argc && !strcmp(argv[i + 1], "empty")) { shot_kb_rename_empty = 1; i++; }
		}
		/* --kbsel ROW COL puts the keyboard's selection on a key, so the
		 * action row can be looked at selected key by key. */
		else if (!strcmp(argv[i], "--kbsel") && i + 2 < argc) {
			shot_kb_row = atoi(argv[i + 1]);
			shot_kb_col = atoi(argv[i + 2]);
			i += 2;
		}
		else if (!strcmp(argv[i], "--slots")) {
			shot_slots = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_slot_sel = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_slot_aspect = (float)atof(argv[++i]);
		}
		else if (!strcmp(argv[i], "--slot-when") && i + 2 < argc) {
			shot_slot_mon = atoi(argv[++i]);
			shot_slot_hour = atoi(argv[++i]);
			if (shot_slot_mon < 1 || shot_slot_mon > 12 || shot_slot_hour < 0 || shot_slot_hour > 23)
				shot_slot_mon = 0;
		}
		else if (!strcmp(argv[i], "--slot-save")) shot_slot_save = 1;
	}
	/* --status is for a shot, as --theme is: a launcher started
	 * with it and no --shot draws the device's own corner. And a shot draws
	 * a clock only when --status gives one (status_clock). */
	shot_st.shot = shot_path != NULL;
	if (!shot_path) shot_st.set = false;

	/* --wifi prints what the radio can see and exits. The wifi module talks
	 * to firmware that only exists on the device, so it cannot be exercised
	 * from the host at all, and a flag that reports its view is the cheapest
	 * way to tell "the machinery is wrong" from "the network is". */
	if (argc > 1 && !strcmp(argv[1], "--wifi")) {
		wifi_net nets[WIFI_MAX_NETS];
		char ssid[WIFI_SSID_MAX], ip[64];
		int n, i;
		wifi_state st;

		printf("bringing the supplicant up ...\n");
		printf("  wifi_up: %s\n", wifi_up() ? "ok" : "FAILED");
		st = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
		printf("  status: %s  ssid=[%s] ip=[%s]\n",
		       st == WIFI_CONNECTED  ? "connected"  :
		       st == WIFI_CONNECTING ? "connecting" :
		       st == WIFI_IDLE       ? "idle"       : "off", ssid, ip);
		n = wifi_scan(nets, WIFI_MAX_NETS);
		printf("  scan: %d network(s)\n", n);
		for (i = 0; i < n; i++)
			printf("    %-32s %4d dBm  %-9s %s\n", nets[i].ssid,
			       nets[i].signal, nets[i].secured ? "secured" : "open",
			       nets[i].known ? "known" : "");
		return n < 0 ? 1 : 0;
	}

	signal(SIGTERM, on_sigterm);
	signal(SIGINT, on_sigterm);
	/* SSH sessions a launcher before this one left open, if it died with USB
	 * File Transfer's SSH running: nothing must outlive that screen. */
	if (!shot_path) sshlink_stop();

	/* Nothing on the device can read a database - there is no sqlite3 binary -
	 * so losing the ability to SEE a setting would be a real loss where losing
	 * the ability to edit one is the point. Runs before video: it is a
	 * question asked over adb, not a screen. */
	/* Repair, not configuration. A wrong shipped default is permanent on a
	 * device that already seeded it - seeding fills absent keys only, and it
	 * cannot tell "the player chose this" from "we seeded it by mistake" - so
	 * there has to be a way to correct one in place. There is no sqlite3 on
	 * the device to do it with.
	 *
	 * It refuses a key that does not already exist, in either scope. That is
	 * what keeps it a repair tool: a typo cannot invent a setting that looks
	 * real, is written faithfully, and is read by nothing. */
	if (argc > 2 && !strcmp(argv[1], "--set")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];
		char key[CFG_STR];
		const char *eq = strchr(argv[2], '=');
		db *target;

		if (!eq || eq == argv[2]) {
			fprintf(stderr, "usage: zyperos.elf --set key=value\n");
			return 2;
		}
		snprintf(key, sizeof key, "%.*s", (int)(eq - argv[2]), argv[2]);
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		if (!db_init(dev, lib, env)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		target = db_has(db_dev(), key) ? db_dev()
		       : db_has(db_lib(), key) ? db_lib() : NULL;
		if (!target) {
			fprintf(stderr, "no setting named '%s' - --dump lists them\n", key);
			db_shutdown();
			return 1;
		}
		if (!db_set_str(target, key, eq + 1)) {
			fprintf(stderr, "could not write '%s'\n", key);
			db_shutdown();
			return 1;
		}
		/* Two of these keys are exported for launch.sh, and a value the
		 * database holds but boot.env does not is a setting that takes effect
		 * everywhere except at boot. Setting wifi=1 and rebooting into a
		 * radio that stays down is how that was found. */
		db_write_boot_env();
		printf("%s = %s\n", key, eq + 1);
		db_shutdown();
		return 0;
	}

	/* --meta <file>: fill the card's games table from scraped rows.
	 *
	 * The scrape runs on a computer for now, and from there the card's
	 * database cannot simply be written: the launcher holds it open
	 * in WAL mode for as long as the device is on, and swapping the file under
	 * a live SQLite handle is how a card loses its favorites and its play time.
	 * Measured on this device: a 76 KB library.db beside a 161 KB -wal.
	 *
	 * So the rows travel as a file and the writing happens HERE, in a second
	 * process, where SQLite arbitrates between the two the way it is built to.
	 * Runs before video, like --set and --dump: it is a question asked over
	 * adb, not a screen, and nothing here presents.
	 *
	 * A record is a tab-separated header - folder, file, year, publisher,
	 * developer, players, genres, esrb, note, and the synopsis length in bytes
	 * - then that many bytes of synopsis and a newline. COUNTED RATHER THAN
	 * ESCAPED, because the synopsis is the one field with newlines in it and 42
	 * of the 78 replies this card's games got have them; an escape is a second
	 * thing to get right at both ends. */
	if (argc > 2 && !strcmp(argv[1], "--meta")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], head[1024];
		FILE *f = fopen(argv[2], "rb");
		int wrote = 0, bad = 0;

		if (!f) {
			fprintf(stderr, "cannot read %s\n", argv[2]);
			return 1;
		}
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the library database\n");
			fclose(f);
			return 1;
		}
		while (fgets(head, sizeof head, f)) {
			game_meta m = { 0 };
			char *fld[10], *p = head;
			size_t want, take;
			long len;
			int i;

			for (i = 0; i < 9; i++) {
				char *t = strchr(p, '\t');

				if (!t) break;
				*t = '\0';
				fld[i] = p;
				p = t + 1;
			}
			if (i < 9) { bad++; continue; }
			fld[9] = p;
			/* A length that is not a length means the file is not what it says
			 * and every byte after it is at an unknown offset. Stop, rather
			 * than write whatever the rest happens to parse as.
			 *
			 * Checked digit by digit rather than left to strtol, which reads
			 * "nine" as 0 - a perfectly valid length, and the row lands with
			 * its synopsis quietly gone and the reader none the wiser. */
			for (p = fld[9]; *p >= '0' && *p <= '9'; p++)
				;
			if (p == fld[9] || (*p != '\n' && *p != '\r' && *p != '\0')) {
				bad++;
				break;
			}
			len = strtol(fld[9], NULL, 10);
			if (len < 0 || len > 1 << 20) { bad++; break; }
			want = (size_t)len;
			take = want < sizeof m.synopsis ? want : sizeof m.synopsis - 1;
			if (fread(m.synopsis, 1, take, f) != take) { bad++; break; }
			m.synopsis[take] = '\0';
			for (want -= take; want > 0; want--)
				if (fgetc(f) == EOF) break;
			fgetc(f);                            /* the record's newline */
			snprintf(m.year,      sizeof m.year,      "%s", fld[2]);
			snprintf(m.publisher, sizeof m.publisher, "%s", fld[3]);
			snprintf(m.developer, sizeof m.developer, "%s", fld[4]);
			snprintf(m.players,   sizeof m.players,   "%s", fld[5]);
			snprintf(m.genres,    sizeof m.genres,    "%s", fld[6]);
			snprintf(m.esrb,      sizeof m.esrb,      "%s", fld[7]);
			snprintf(m.note,      sizeof m.note,      "%s", fld[8]);
			if (db_game_set(db_lib(), card_folder(fld[0]), fld[1], &m)) wrote++;
			else bad++;
		}
		fclose(f);
		printf("%d written, %d rejected\n", wrote, bad);
		db_shutdown();
		return bad && !wrote ? 1 : 0;
	}

	/* --titles <file>: display titles for the card's games, from a computer.
	 *
	 * --meta's companion, written for the same reason and in the same second
	 * process: the launcher holds the library database open in WAL mode, so
	 * a title made on a computer (a LaunchBox or No-Intro name for a ROM
	 * whose file is called "060 Advance Wars 2.gba") is written here, where
	 * SQLite arbitrates, rather than into a copy of the file.
	 *
	 * One line a game: folder, file and title, separated by tabs, the file
	 * as the shelf keys it (a disc folder's "Folder/Game.m3u"). Only the
	 * title changes: the row's year, genres and synopsis are read first and
	 * written back as they were, and a game with no row gets one holding the
	 * title alone. An empty title is skipped rather than clearing one. */
	if (argc > 2 && !strcmp(argv[1], "--titles")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], line[2048];
		FILE *f = fopen(argv[2], "rb");
		int wrote = 0, bad = 0;

		if (!f) {
			fprintf(stderr, "cannot read %s\n", argv[2]);
			return 1;
		}
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the library database\n");
			fclose(f);
			return 1;
		}
		while (fgets(line, sizeof line, f)) {
			char *folder = line, *file, *title, *end;
			game_meta m;

			line[strcspn(line, "\r\n")] = '\0';
			if (!line[0]) continue;
			if (!(file = strchr(folder, '\t')) || !(title = strchr(file + 1, '\t'))) {
				bad++;
				continue;
			}
			*file++ = '\0';
			*title++ = '\0';
			if ((end = strchr(title, '\t'))) *end = '\0';
			if (!folder[0] || !file[0] || !title[0]) { bad++; continue; }
			folder = (char *)card_folder(folder);
			db_game_get(db_lib(), folder, file, &m);
			snprintf(m.title, sizeof m.title, "%s", title);
			if (db_game_set(db_lib(), folder, file, &m)) wrote++;
			else bad++;
		}
		fclose(f);
		printf("%d titles written, %d rejected\n", wrote, bad);
		db_shutdown();
		return bad && !wrote ? 1 : 0;
	}

	/* --scrape [folder] runs the box art scraper with no video at all.
	 *
	 * The same art_begin/art_step the screen drives, stepped from here instead
	 * of from a frame loop, so a long run does not depend on somebody holding
	 * the device - and so that re-scraping just a few games is a five-minute
	 * job with a command rather than a menu to navigate.
	 *
	 * Headless, so it must NOT touch video: the live launcher is presenting
	 * while this runs, and two presenters wedge the display engine. It also
	 * takes the one async network slot, which is per process, so the launcher
	 * is unaffected either way.
	 *
	 * The shrinker runs inline here rather than on a worker - art_shrink falls
	 * back to doing it on the spot when no thread was started - which is what
	 * a run with nothing to draw wants anyway. */
	if (argc > 1 && !strcmp(argv[1], "--scrape")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], cp[CFG_STR * 2], path[CFG_STR * 2];
		const char *only_sys = argc > 2 ? argv[2] : NULL;
		systems_cfg all, one;
		art_progress st;
		char seen[128] = "";
		int r;

		paths_init();
		snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
		if (!cfg_load_systems(path, &all)) {
			fprintf(stderr, "no usable %s\n", path);
			return 1;
		}
		/* The folders the shelves read, EmulationStation's where they read
		 * one, and its covers and videos counted as covers and videos */
		shelf_folders_from_card(&all);
		art_es_media(es_art_cover, es_art_video);
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the databases\n");
			return 1;
		}
		snprintf(cp, sizeof cp, "%s/cacert.pem", P_ROOT);
		net_set_ca_path(cp);
		ss_creds_load();
		sgdb_key_load();
		ss_run_videos(snap_wanted);
		IMG_Init(IMG_INIT_PNG);

		if (only_sys) {
			int i;

			memset(&one, 0, sizeof one);
			for (i = 0; i < all.count; i++)
				if (!strcmp(all.systems[i].folder, only_sys) || !strcmp(all.systems[i].home, only_sys)) {
					one.systems[0] = all.systems[i];
					one.count = 1;
					break;
				}
			if (!one.count) {
				fprintf(stderr, "no shelf called %s\n", only_sys);
				return 1;
			}
		}
		fprintf(stderr, "scrape: %s, ScreenScraper %s, SteamGridDB %s\n",
		        only_sys ? only_sys : "every shelf",
		        ss_signed_in() ? "signed in" : "not signed in",
		        sgdb_have_key() ? "with a key" : "no key");
		art_begin(only_sys ? &one : &all, P_ROMS, NULL);
		while ((r = art_step()) == 1) {
			art_status(&st);
			/* One line per thing it turns to, not per step: a step is mostly
			 * a poll of a request already in flight. Keyed on what it says it
			 * is doing rather than on the counters, because the interesting
			 * case - a game asked about and not found - moves no counter. */
			if (strcmp(st.now, seen)) {
				snprintf(seen, sizeof seen, "%s", st.now);
				fprintf(stderr, "scrape: %-44s  %d found, %d missing, %d had one, %d names saved\n",
				        st.now, st.found, st.missing, st.skipped, st.named);
			}
			usleep(20000);
		}
		art_status(&st);
		fprintf(stderr, "scrape: done - %d found, %d missing, %d already had one, %d names saved%s%s\n",
		        st.found, st.missing, st.skipped, st.named,
		        st.problem[0] ? " - " : "", st.problem);
		return 0;
	}

	if (argc > 1 && !strcmp(argv[1], "--dump")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2];
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		printf("device  %s\n", dev);
		db_dump(db_dev(), stdout);
		printf("library %s\n", lib);
		db_dump(db_lib(), stdout);
		db_shutdown();
		return 0;
	}

	/* --game-rows TAG FILE: one game's rows in the library database (its
	 * play sessions, its favorite, its Pak; src/gamerows.h) on stdout, and
	 * --game-rows-put TAG FILE: that game's rows made what stdin holds. For
	 * px2-snap (tools/px2-snap.sh), which keeps a game's saves and states and
	 * these beside them. Any number of arguments is answered here, never
	 * by a second launcher. */
	if (argc > 1 && (!strcmp(argv[1], "--game-rows") || !strcmp(argv[1], "--game-rows-put"))) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2];
		bool put = !strcmp(argv[1], "--game-rows-put");
		int n;

		if (argc != 4 || !argv[2][0] || !argv[3][0]) {
			fprintf(stderr, "usage: zyperos.elf %s TAG FILE\n", argv[1]);
			return 2;
		}
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		n = put ? gamerows_put(db_lib(), argv[2], argv[3], stdin)
		        : gamerows_dump(db_lib(), argv[2], argv[3], stdout);
		db_shutdown();
		if (n < 0) {
			fprintf(stderr, put ? "game-rows: not one game's rows for %s %s; nothing changed\n"
			                    : "game-rows: could not read the rows of %s %s\n",
			        argv[2], argv[3]);
			return 1;
		}
		if (put) fprintf(stderr, "game-rows: %d row(s) for %s %s\n", n, argv[2], argv[3]);
		return 0;
	}

	/* --coreopts TAG: the core options the launcher sends a game of the system
	 * TAG (systems.cfg's tag field, "NDS"), one key=value a line in the order
	 * they are sent, with the language the card has saved in force: the
	 * Nintendo DS's language is made from it (src/dslang.h), and
	 * tools/dslang-check.sh holds that it is. */
	if (argc > 2 && !strcmp(argv[1], "--coreopts")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], lc[16];
		int i;

		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		db_get_str(db_dev(), "language", lc, sizeof lc, "en");
		i18n_set(lang_parse(lc));
		for (i = 0; i < plat_coreopt_count(argv[2]); i++)
			puts(plat_coreopt(argv[2], i));
		db_shutdown();
		return 0;
	}

	t_boot0 = plat_now_ms();
	paths_init();

	/* Before anything reads a setting. Two databases, because the files this
	 * replaced kept per-device and per-card data apart on purpose - see db.h.
	 * A failure here is fatal rather than silently defaulted: every setting
	 * the launcher has would be a fallback, and the player would find their
	 * volume, brightness and account quietly reset with nothing said. */
	{
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		/* NOT fatal, and the reason is what "fatal" costs here. launch.sh
		 * restarts the launcher when it exits, and five exits inside five
		 * seconds each makes it give up and call poweroff - so returning 1
		 * from this point turns a settings problem into a device that will
		 * not boot. It is also BEFORE plat_resident_ready() below, which is
		 * what stops a running game so that only one process is presenting;
		 * exiting here skips that entirely.
		 *
		 * Every getter takes what to say instead, and db_dev()/db_lib()
		 * return NULL harmlessly, so the launcher comes up on the shipped
		 * defaults with no favorites, no turbo and no core options. Worse
		 * than working, far better than off - and it says so where anyone
		 * looking at the log will find it. */
		if (!db_init(dev, lib, env)) {
			fprintf(stderr, "settings: cannot open the database at %s\n", dev);
			fprintf(stderr, "settings: continuing on the shipped defaults - "
			                "favorites, turbo and core options will be absent, "
			                "and nothing set here will be remembered\n");
		}
		/* MP4's settings under its own name, from the "muse." a handheld
		 * kept them under before 1.1.0 (src/mp4keys.h): before anything
		 * reads them, and in a shot too, whose card may be an old one */
		if (mp4keys_migrate(db_dev()))
			fprintf(stderr, "mp4: settings moved under mp4., once\n");
		/* Rewritten at every boot, not only on change: it is derived, so a
		 * card that lost it or never had one gets a correct one for free. */
		db_write_boot_env();
		/* Developer Mode's file for plastron's S40usbgadget, the same way */
		if (!shot_path) usblink_sync(db_dev(), P_USERDATA);
		if (!shot_path) usblink_release_dev(devmode_unlocked(db_dev()));
	}
	/* Local time is the card's chosen zone, in this process, from here on,
	 * and Diatom's with each game (resident.c's RUN). The system itself stays
	 * on UTC. Before it, 1.0.5's default: Auto on, once for a card from
	 * before (src/autozone.h), never in a shot, whose cards are scratch. */
	{
		char tz[64];

		if (!shot_path && autozone_migrate(db_lib()))
			fprintf(stderr, "clock: auto zone %s, 1.0.5's default, once\n",
			        db_get_int(db_lib(), AUTOZONE_KEY, 0) == 1 ? "on" : "not on");
		/* And 1.0.6's: the core options 1.0.5 seeded, given at run time
		 * now by the device the card is in (src/coreopts.h) */
		if (!shot_path && coreopts_migrate(db_lib()))
			fprintf(stderr, "coreopts: the seeded defaults left the library, once\n");
		db_get_str(db_lib(), "timezone", tz, sizeof tz, "America/New_York");
		plat_clock_zone(tz);
	}
	t_mark("databases");

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &a.sys)) {
		fprintf(stderr, "no usable %s\n", path);
		return 1;
	}
	system_order_load();
	system_order_apply(&a.sys);
	/* Before the scan, which titles each game by it */
	game_names_load();
	power_button_load();
	homesys_load();

	/* Before the scan, because the scan builds the Favorites shelf out of
	 * them and a shelf cannot be built from a list that has not been read. */
	fav_load();

	/* A program of its own that ended with its session still open, which
	 * launch.sh closes as it ends (--port-played): closed here, now, before
	 * stats_recover would call it lost and Recents is built. A shot reads
	 * the card as it is. */
	if (!shot_path && stats_handed_back(plat_flag(STATS_PORT_NOTE), stats_mono_ms()))
		fprintf(stderr, "port: its play time kept at the launcher's start\n");
	/* Close any session the device never saw end - a power cut, a flat
	 * battery - at its last checkpoint, before anything reads a total. See
	 * stats.h: short by up to the checkpoint interval beats lost entirely. */
	stats_recover();

	{	char cp[CFG_STR * 2];
		chv_earned_load();
		plat_resident_on_unlock(on_cheevo_unlocked);
		plat_resident_on_progress(on_cheevo_progress);
		plat_resident_on_speed(on_speed);
		plat_resident_on_stick(on_stick);
		plat_resident_on_disk(on_disk);
		plat_resident_on_quick(on_quick);
		plat_resident_on_hotkey(on_hotkey);
		g_hotkey_app = &a;
		plat_resident_on_tick(on_game_tick);
		plat_resident_on_battery_empty(battery_empty);
		/* The downloads' lanes go on behind every screen while it rests
		 * (src/dlq.h); tick_theme steps them as one draws. What each step
		 * brings is kept for MENU > Downloads, open or not. */
		plat_on_input_wait(dlq_wait);
		dlq_on_step(downloads_stepped);
		p2_setup();
		link_setup();
		heat_init(&g_heat);

		/* Without this every HTTPS request fails verification, because the
		 * device has no trust store of its own - res/ssl/README.md. */
		a.auto_off = auto_off_load();
		g_saver_s = saver_load();
		g_charge_limit = charge_limit_load();
		/* A full charge asked for ends with the cable out, here too */
		if (!on_charger()) remove(CHG_FULL_ONCE);
		charge_limit_apply();
		g_aout_policy = aout_load();
		g_channels = channels_load();

		snprintf(cp, sizeof cp, "%s/cacert.pem", P_ROOT);
		net_set_ca_path(cp);
		ra_creds_load();
		ss_creds_load();
		sgdb_key_load();
		ss_run_videos(snap_wanted);
		mp4_setup(shot_path != NULL, true, shot_mode);
	}
	t_mark("mp4+accounts");

	/* Scan every system now, not when one is opened: it is three directory
	 * reads, it happens behind the boot animation, and it means walking into
	 * a system is a frame rather than a wait.
	 *
	 * On a thread of its own, while this one starts the display, the input,
	 * the sounds and the faces, which need nothing from it: the shelves are
	 * first wanted after the join below ("scan joined"). On my card
	 * (8,844 games, exFAT) the scan was 514 ms of a 900 ms start and the
	 * display's start 190 ms, one after the other (2026-11-07). The
	 * databases are shared with this thread, and db_open makes every
	 * connection serialized for that; nothing here reads a.sys, a.view or
	 * the MP4 library until the join.
	 *
	 * The folders' listings come from the saved library when there is one
	 * (src/savedlib.h), and the card is read again once the shelf is up
	 * (after_start_step): after a cold boot, 742 ms to the first frame on
	 * My card became 506-521 ms (2026-11-07). */
	SDL_Thread *scanner = SDL_CreateThread(scan_thread, "zyperos-scan", &a);

	if (!scanner) scan_thread(&a);

	/* The theme, from the saved Appearance - or from --theme, for a shot,
	 * which is asked to be one theme or the other whatever the device has
	 * saved. theme_init also writes THEME_FILE (src/themefile.h) for the two
	 * programs that draw the volume and brightness bar without this one,
	 * Diatom over a game and portkeys over a port, so the file says the
	 * theme that is on from the first frame and not one that was.
	 *
	 * After the databases are open, because that is where the choice is
	 * kept, and before ui_init and the first thing drawn, so whatever asks
	 * theme() for its colors has an answer. A /tmp that cannot be written to
	 * is a line in the log and nothing more, as theme.h says. */
	{
		char ap[16];

		/* A shot writes no theme file. It is run beside a launcher on a
		 * development host as often as not, and THEME_FILE is that
		 * launcher's word: a shot in the other theme must not turn its
		 * bar over. */
		if (shot_path) theme_set_file(NULL);
		/* A 1.0.5 device's Auto hours, once a card: kept as the Night
		 * Hours, which Auto turns by since 1.1.0 (nightkeep.h) */
		night_migrate_db();
		db_get_str(db_dev(), "appearance", ap, sizeof ap,
		           appearance_id(APPEAR_LIGHT));
		/* The language, MENU > Language's, or --lang's for a shot: every
		 * string from here on is in it (src/i18n.h). English until a first
		 * start's step or the menu says otherwise. Its faces once ui_init
		 * has opened them, below. */
		{
			char lc[16];

			db_get_str(db_dev(), "language", lc, sizeof lc, "en");
			i18n_set(lang_parse(shot_path && shot_lang ? shot_lang : lc));
		}
		look_load();
		/* --look's, for a shot: ACCENT,LIGHT,DARK,CORNERS,ZYPER as the
		 * database keeps them ("blue,cool,deepblue,square,off"), and
		 * FOLDERS and HIGHLIGHT after them if given (",teal,border";
		 * System Colors and Bar without) */
		if (shot_path && shot_look) {
			char w[7][16] = { "", "", "", "", "", "", "" };
			look l;

			sscanf(shot_look, "%15[^,],%15[^,],%15[^,],%15[^,],%15[^,],%15[^,],%15s",
			       w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
			l = look_parse(w[0], w[1], w[2], w[3], w[4]);
			l.folder = look_folder_parse(w[5]);
			l.focus = look_focus_parse(w[6]);
			look_set(&l);
			look_bar_colors();
		}
		/* A shot of a card on Auto keeps Auto on its row and in its list,
		 * drawn in --theme's theme (theme_show), so the shot is the same
		 * frame at any hour of the day */
		if (shot_path && shot_theme >= 0 && appearance_parse(ap) == APPEAR_AUTO) {
			theme_init(APPEAR_AUTO);
			theme_show(shot_theme == APPEAR_DARK ? DS_DARK : DS_LIGHT);
		} else {
			theme_init(shot_path && shot_theme >= 0 ? (appearance)shot_theme
			                                        : appearance_parse(ap));
		}
	}
	{
		char value[24];
		db_get_str(db_dev(), "home", value, sizeof value, "spotlight");
		a.home_layout = shot_path && shot_home >= 0 ? (home_layout)shot_home : home_parse(value);
	}
	t_mark("theme");

	/* After IMG_Init: the worker calls IMG_Load. Before the display, because
	 * neither needs it - decoding is CPU work, and making textures of what it
	 * decodes waits for the renderer, on this thread, in texload_drain - and
	 * because the threads would otherwise sit idle through video, input and
	 * settings. Home textures and game covers use the same worker.
	 *
	 * Not for --shot. A shot draws the shelf exactly once, and with the workers
	 * running every card on that one frame is only queued - so the PNG came out
	 * with the title, the count and the rail and not a single card, while the
	 * log line named the focused game as though it had been drawn. With no
	 * worker, game_get_tex decodes on the frame the way it always did, which is
	 * what a tool for checking one frame wants: the right picture, not a fast
	 * one. */
	IMG_Init(IMG_INIT_PNG);
	texload_set_late(es_cover_late);
	art_es_media(es_art_cover, es_art_video);
	if (!shot_path) texload_start();
	/* This boot's zyperos.log held to about 2 MB (src/logcap.h) */
	if (!shot_path) logcap_start();

	/* The interface sounds, which read their files on a thread of their own,
	 * so the first frame waits for none of them. Not for --shot, which
	 * presses nothing, nor a headless run (ZYPEROS_HEADLESS, a check's): no
	 * business opening the host's audio. */
	{
		char v[16], dir[CFG_STR * 2];

		db_get_str(db_dev(), "uisounds", v, sizeof v,
		           uisound_setting_id(DS_VOICE_DEFAULT));
		uisound_set(uisound_setting_parse(v));
		snprintf(dir, sizeof dir, "%s/res/sounds", P_ROOT);
		if (!shot_path && !getenv("ZYPEROS_HEADLESS")) uisound_init(dir);
	}
	/* Menu music, the same way: settings now, the folder read on its own
	 * thread, and nothing heard until the shelf's first pass (src/bgm.h) */
	{
		char v[16], track[512], dir[CFG_STR * 2], builtin[CFG_STR * 2];

		db_get_str(db_dev(), "bgm", v, sizeof v, "on");
		bgm_set_on(bgm_on_parse(v));
		db_get_str(db_dev(), "bgm.volume", v, sizeof v, "");
		bgm_set_volume(bgm_volume_parse(v));
		db_get_str(db_dev(), "bgm.track", track, sizeof track, "");
		bgm_set_track(track);
		db_get_str(db_dev(), "bgm.popup", v, sizeof v, "on");
		g_song_popup = bgm_on_parse(v);
		db_get_str(db_dev(), "bgm.builtin", v, sizeof v, "on");
		bgm_set_builtin(bgm_on_parse(v));
		bgm_dirs(dir, builtin, sizeof dir);
		/* The card's own folder, there to be filled from a computer or File
		 * Transfer's page, on a card older than it too (mk/payload.sh makes
		 * it on a new one) */
		if (!shot_path) mkdir(dir, 0755);
		if (!shot_path && !getenv("ZYPEROS_HEADLESS")) bgm_init(dir, builtin);
	}

	/* BEFORE the first frame this process ever draws: if a previous launcher
	 * died while a game was running, the resident is still presenting through
	 * fbdev right now, and drawing the shelf over it is the two-presenter
	 * case that wedges the display engine in-kernel (Diatom's handoff spike;
	 * it cost a power cycle to prove, twice). plat_resident_ready() connects,
	 * and on READY state=running it stops the game and drains to EXIT - so by
	 * the time video comes up, only one presenter exists. The check at launch
	 * time was too late by definition: this process draws long before the
	 * player launches anything. */
	plat_resident_ready();
	t_mark("diatom connect");

	if (!plat_video_init()) {
		fprintf(stderr, "video init failed\n");
		if (scanner) SDL_WaitThread(scanner, NULL);
		return 1;
	}
	t_mark("video");
	a.r = plat_renderer();
	tour_hint_draw_set(keyhints_width, tour_hint);
	t_mark("renderer");
	/* The replug message, over every screen (DESIGN.md 11.36), and a
	 * screenshot's notice (src/menukeys.h) */
	plat_set_overlay(every_overlay);
	plat_input_init();
	/* MENU's chords on every screen: a screenshot, the lock */
	mk_init(&a);
	t_mark("input");
	/* Nothing is handed in any more. The two-tier lookup this replaces - a
	 * shipped default and the player's saved level - is one key each in the
	 * database, seeded once and overwritten by a nudge. That is also the end
	 * of a bug it kept reintroducing: reapplying the config afterwards put
	 * the shipped default ahead of the level the player last chose. */
	plat_settings_init();
	levels_init();   /* the boosts past the top kept from before (src/levels.c) */
	t_mark("settings");

	/* Before ui_init, which is where the sizes are decided; it persists across
	 * the ui_quit/ui_init pair the standalone-emulator fallback goes through. */
	/* The size the player chose beats the shipped default, the same way a
	 * saved brightness does. Read before ui_init, which is when the scale is
	 * applied. */
	/* The buttons as this handheld prints them, before anything is drawn */
	ds_keys_set(plat_profile()->buttons, plat_profile()->n_buttons);
	if (!ui_init(a.r, P_FONT)) fprintf(stderr, "font init failed\n");
	/* The GL renderer and version, for the log pack's hardware report
	 * (src/hwreport.h): read once, while the renderer's context is current */
	hw_note_gl();
	/* The language's faces: Lexend's, opened above, or 简体中文's, opened now */
	ui_fonts_for(i18n_lang());
	page_fonts_init(P_FONT);
	/* ui_init on its own, between "settings" and here: the fonts, the glow
	 * and Beard's three shadow textures, every one made before the first
	 * frame (src/ui.c says why), and made again after every fallback game. */
	t_mark("ui");

	/* The library, read meanwhile (scan_thread): what follows needs it */
	if (scanner) SDL_WaitThread(scanner, NULL);
	t_mark("scan joined");
	/* After the scan, because it indexes by system, and before anything can
	 * launch, because the mode has to reach Diatom with the first RUN. */
	display_load(&a);
	if (shot_dmode) {
		int k, v;

		for (k = 0; k < DMODE_COUNT && strcmp(DMODES[k].name, shot_dmode); k++) ;
		for (v = 0; k < DMODE_COUNT && v < a.sys.count; v++) {
			a.view[v].dmode = k;
			a.view[v].dmode_kept = true;
		}
	}
	/* A PICO-8 zip the player copied into Bios/PICO-8, before the cores are
	 * chosen: once taken, your PICO-8 plays the carts from this start on */
	p8_take_now();
	core_load(&a);
	sort_load(&a);
	view_load(&a);
	sort_all(&a);
	t_mark("scan");

	/* MENU > Clock, which the corner's first frame is drawn in */
	clock24_load();
	batt_icon_load();
	if (!shot_path) usb_auto_load();
	if (shot_path && shot_st.h24 >= 0) g_clock24 = shot_st.h24;
	if (shot_path && shot_st.icon >= 0) g_batt_mode = shot_st.icon;
	a.sys_cursor = 0;
	db_get_str(db_lib(), "startup_system", startup, sizeof startup, "");
	if (startup[0])
		for (int i = 0; i < a.sys.count; i++)
			if (strcasecmp(a.sys.systems[i].name, startup) == 0) {
				a.sys_cursor = i;
				break;
			}
	restore_place(&a);
	/* After restore_place, so it wins: `.last` says where the shelf was and
	 * this says what was actually being played. Before the card priming
	 * below, because that loads textures for whatever the cursor is on, and
	 * a fallback to the shelf should find the right ones there. */
	a.resume_menu = playing_restore(&a);
	/* Back from a port: on its game list, as a Diatom game comes back. */
	if (!access(plat_flag(PORT_RETURN), F_OK)) {
		unlink(plat_flag(PORT_RETURN));
		if (!a.resume_menu && a.view[a.sys_cursor].list.count > 0) {
			a.screen = SCREEN_GAMES;
			cf_reset(&a.view[a.sys_cursor].cf, a.view[a.sys_cursor].cursor);
		games_reset(&a.view[a.sys_cursor].row, a.view[a.sys_cursor].cursor, a.view[a.sys_cursor].list.count, plat_now_ms());
		}
	}
	/* MENU+START (tools/zyhome.c): the home, whatever was running and
	 * whatever the records say */
	if (!access(plat_flag(CTL_HOME_FLAG), F_OK)) {
		unlink(plat_flag(CTL_HOME_FLAG));
		a.resume_menu = false;
		playing_clear();
		a.screen = SCREEN_SYSTEMS;
		fprintf(stderr, "zyhome: started on the home\n");
	}
	home_reset(&a.home, a.sys_cursor, a.sys.count, plat_now_ms());
	a.tint = a.sys.systems[a.sys_cursor].accent;
	t_mark("restore place");

	/* Home art decodes on texload's workers. Finished art wakes the loop. */
	prime_window(&a, a.sys_cursor);
	prime_home(&a);
	t_mark("card assets");

	a.running = true;
	last_tint_ms = plat_now_ms();

	if (shot_path) {
		if (shot_screen >= 0) a.screen = (screen_id)shot_screen;
		home_asks = HOME_STILLS;          /* every still at once: no loader in a shot */
		if (shot_view) {
			a.view[a.sys_cursor].listed = !strcmp(shot_view, "list");
			a.view[a.sys_cursor].grid = !strcmp(shot_view, "grid");
			a.view[a.sys_cursor].video = !strcmp(shot_view, "video");
		}
		if (shot_folder) shelf_open_dir(&a, a.sys_cursor, shot_folder, NULL);
		if (shot_game) shelf_reveal(&a, a.sys_cursor, shot_game);
		for (const char *k = shot_shelf_press; k && *k; ) {
			static const struct { const char *w; in_button b; } KEYS[] = {
				{ "a", IN_ACCEPT }, { "b", IN_BACK }, { "x", IN_X }, { "y", IN_Y },
				{ "up", IN_UP }, { "down", IN_DOWN }, { "left", IN_LEFT },
				{ "right", IN_RIGHT }, { "l1", IN_L1 }, { "r1", IN_R1 }, { "start", IN_START },
			};
			size_t len = strcspn(k, ",");
			unsigned j;

			for (j = 0; j < sizeof KEYS / sizeof KEYS[0]; j++)
				if (strlen(KEYS[j].w) == len && !strncmp(KEYS[j].w, k, len)) break;
			if (j == sizeof KEYS / sizeof KEYS[0]) {
				fprintf(stderr, "--shelf-press: %.*s is no key\n", (int)len, k);
				return 2;
			}
			memset(&a.in, 0, sizeof a.in);
			a.in.pressed[KEYS[j].b] = true;
			a.screen = SCREEN_GAMES;
			update_games(&a);
			memset(&a.in, 0, sizeof a.in);
			fprintf(stderr, "shelf-press: %s -> %s [%s] %d/%d\n", KEYS[j].w,
			        a.view[a.sys_cursor].dir[0] ? a.view[a.sys_cursor].dir : "top",
			        a.view[a.sys_cursor].list.count ? a.view[a.sys_cursor].list.items[a.view[a.sys_cursor].cursor].file : "",
			        a.view[a.sys_cursor].cursor + 1, a.view[a.sys_cursor].list.count);
			k += len + (k[len] == ',');
		}
		if (shot_jump) {
			sysview *v = &a.view[a.sys_cursor];
			int k, dir = shot_jump > 0 ? 1 : -1;
			for (k = 0; k < (shot_jump < 0 ? -shot_jump : shot_jump); k++)
				if (v->list.count > 0) v->cursor = shelf_letter_jump(v, dir);
		}
		/* A shelf nobody has moved sits at position 0, wherever its cursor
		 * was restored to. On the device that never reaches the screen:
		 * update_games runs cf_set_cursor_dir every pass, which snaps an
		 * unmoved shelf to its cursor. A shot draws one frame and never runs
		 * that pass, so without this it drew the list's first card under the
		 * selected game's title. */
		cf_reset(&a.view[a.sys_cursor].cf, a.view[a.sys_cursor].cursor);
		games_reset(&a.view[a.sys_cursor].row, a.view[a.sys_cursor].cursor, a.view[a.sys_cursor].list.count, plat_now_ms());

		if (shot_song) home_song_update(&g_home_song, true, shot_song_paused, shot_song);
		g_home_phase = shot_phase >= 0 ? shot_phase : 0;
		if (shot_sysmove != 0.0f && a.sys.count > 1) {
			unsigned now = plat_now_ms();
			home_reset(&a.home, a.sys_cursor, a.sys.count, now);
			a.sys_cursor = (a.sys_cursor + 1) % a.sys.count;
			home_select(&a.home, a.sys_cursor, a.sys.count, 1, now);
			g_home_phase = (int)(shot_sysmove * DS_MS_MOVE);
		}

		/* The games shelf, with the cursor ALREADY at the destination - which
		 * is the state the device is in for the whole of a move, and the reason
		 * anything drawn from the cursor arrives early. */
		if (shot_gamemove != 0.0f) {
			sysview *v = &a.view[a.sys_cursor];

			if (v->list.count > 1) {
				int was = v->cursor;

				v->cursor = (v->cursor + 1) % v->list.count;
				cf_stage(&v->cf, (float)was, (float)was + 1.0f, shot_gamemove);
				games_reset(&v->row, was, v->list.count, plat_now_ms());
				games_select(&v->row, v->cursor, v->list.count, 1, v->row.moved);
				g_games_phase = (int)(shot_gamemove * DS_MS_MOVE);
			}
		}
		/* --delete-now: Delete Game's yes on the game under the cursor, and
		 * the shot is the shelf after it (tools/gamedel-shot-check.py) */
		if (shot_delete_now && a.sys.count > 0 && a.view[a.sys_cursor].list.count > 0) {
			sysview *v = &a.view[a.sys_cursor];
			char said[192];
			bool gone = game_delete_now(&a, shelf_owner(&a, a.sys_cursor, v->cursor),
			                            &v->list.items[v->cursor], false, said, sizeof said);

			fprintf(stderr, "delete: %s. %s\n", gone ? "deleted" : "kept", said);
		}
		/* --shots-delete-now: Delete Screenshot's yes on the shot under the
		 * cursor (DESIGN.md 11.68) */
		if (shot_shots_delete_now && shots_shelf(&a, a.sys_cursor) && a.view[a.sys_cursor].list.count > 0)
			shots_delete_now(&a, &a.view[a.sys_cursor]);
		if (a.screen == SCREEN_GAMES) prime_window(&a, a.sys_cursor);
		if (shot_at >= 0 && !shot_launch && !shot_portload && !shot_poweroff &&
		    (!shot_kb || shot_press != SHOT_STEP))
			shot_motion(&a);
		if (shot_portload) shot_portload_take(&a);
		else take_shot(&a);
		goto done;
	}

	/* Dev instrumentation, same standing as --shot: launch one game with no
	 * buttons pressed, so the resident path can be exercised over adb with
	 * nobody holding the device. ZYPEROS_AUTOLAUNCH="TAG<tab>rom-filename";
	 * pair with ZYPEROS_AUTOSTOP_S to end the game on a clock. */
	{
		const char *auto_spec = getenv("ZYPEROS_AUTOLAUNCH");
		if (auto_spec && strchr(auto_spec, '\t')) {
			char tag[64], file[LIB_PATH];
			const char *bar = strchr(auto_spec, '\t');
			int si, gi, found = 0;
			snprintf(tag, sizeof tag, "%.*s", (int)(bar - auto_spec), auto_spec);
			snprintf(file, sizeof file, "%s", bar + 1);
			for (si = 0; si < a.sys.count && !found; si++) {
				if (strcmp(a.sys.systems[si].tag, tag) != 0) continue;
				for (gi = 0; gi < a.view[si].list.count; gi++) {
					const char *b = strrchr(a.view[si].list.items[gi].file, '/');
					b = b ? b + 1 : a.view[si].list.items[gi].file;
					if (strcmp(b, file) == 0) {
						a.sys_cursor = si;
						a.view[si].cursor = gi;
						found = 1;
						break;
					}
				}
			}
			if (found) {
				fprintf(stderr, "autolaunch: %s / %s\n", tag, file);
				launch(&a);
			} else {
				fprintf(stderr, "autolaunch: no %s / %s in the library\n", tag, file);
			}
			goto done;
		}
	}

	/* px2-open's commands, from here on: a game resumed at boot, the
	 * tutorial and the shelf (src/ctl.h) */
	ctl_start(&a);

	/* Nothing pressed during the boot is honored. How long that takes is the
	 * device's to say - see plat_input_settle. */
	plat_input_settle(&a.in);

	/* A start a charger made shows the charging screen first (DESIGN.md
	 * 11.25): held POWER goes on with this start, unplugged turns it off */
	if (a.running && !charge_boot_gate(&a)) goto done;

	/* A battery too low to play says so first, before a resume, the
	 * tutorial or the shelf, and powers off (DESIGN.md 11.24); with a
	 * charger plugged in meanwhile the start goes on below. After the
	 * settle, so the press that switched the handheld on is no POWER. */
	if (a.running && !lowbatt_gate(&a)) goto done;

	/* Back from a program of its own that kept MP4 playing (your PICO-8):
	 * its queue taken on, where the daemon is, and the file gone with it
	 * (src/mp4hand.h). Whatever comes next, the music goes on. */
	/* No cut left in the equalizer's file from a PICO-8 that ended under
	 * MP4 (its follower killed before it could clear it) */
	if (!shot_path) port_quiet_hand(false);
	hand_back = !shot_path && access(plat_flag(MP4HAND_PATH), F_OK) == 0;
	if (hand_back) {
		mp4_hand_take();
		unlink(plat_flag(MP4HAND_PATH));
	}

	/* Straight back into the game, before the shelf is ever drawn. After the
	 * input grace above, so a wake press replayed by the boot does not land in
	 * the menu that is about to open. */
	if (a.resume_menu && a.running) {
		/* And MP4 where it was, when the power went while it played over the
		 * game (DESIGN.md 11.67): held under it, paused at its second, with
		 * no screen of its own - the game comes first - so that SELECT in
		 * the menu the game comes back with is Now Playing on it, as it was
		 * before the power-off. Mine, 1.1.0-rc.3: "when i clicked select in
		 * the menu after powering on again it didnt show the video playing
		 * that video paused". Before the launch, which only returns when
		 * the game ends. */
		if (!shot_path && !hand_back) resume_start();
		a.screen = SCREEN_GAMES;
		launch(&a);
	}
	/* Or, on a handheld that has never shown it, the tutorial, before the
	 * shelf is ever drawn (DESIGN.md 11.10). Not on a start straight back
	 * into a game: that handheld has been played, and it runs at the next
	 * start that comes to the shelf. */
	else if (a.running && tour_wanted(db_dev())) {
		fprintf(stderr, "tour: %s is not set, so the tutorial comes first\n", TOUR_DONE_KEY);
		tour_run(&a, true);
	}
	/* Or MP4, where it was when the power went while it played: Now
	 * Playing, paused at its second, over the shelf the start put back
	 * (DESIGN.md 11.67). After the tutorial, which comes first; under a
	 * game resumed at the start, above, it is held with no screen. Not back
	 * from a program that kept MP4 playing: no power-off came. */
	else if (a.running && !shot_path && !hand_back && resume_start()) {
		mp4_resume_open(&a);
	}
	/* Or, back from a PortMaster that closed with an error, its page with
	 * what happened and Reinstall PortMaster */
	else if (a.running && !shot_path) {
		pm_after_run(&a);
	}
	/* Its record read or not, it is this start's alone */
	if (!shot_path) unlink(PM_RAN);

	/* Drawing only when something changed. See g_redraw_at.
	 *
	 * The loop still runs every IDLE_POLL_MS when nothing is drawn, so the power
	 * button, Auto Off, the headphone jack and the Bluetooth sink are all still
	 * checked at the same rate as before - only the draw and the present are
	 * skipped. 16ms keeps the worst case from a press to its first frame where
	 * it was, one refresh.
	 *
	 * Why a poll and not a wait on the input devices: input arrives through two
	 * roads, SDL's own events and three raw evdev descriptors, and a held
	 * button produces no event at all while it repeats - in_repeat works from
	 * the clock. A wait that slept until the next event would have stopped key
	 * repeat dead. Polling costs a handful of syscalls a frame, against a full
	 * draw and a present. */
	const Uint32 IDLE_POLL_MS = 16;
	/* A gap this long between two passes means something else had the screen -
	 * a menu, a game, the art scraper - and whatever it drew is not the shelf.
	 * Catching it here covers every one of them without a list to keep. */
	const Uint32 AWAY_MS = 100;
	/* Drawn at least this often regardless. A backstop, not a mechanism: if some
	 * animation is ever added without telling g_redraw_at, it shows as a screen
	 * updating once a second, which is visibly wrong, rather than one that has
	 * silently frozen. */
	const Uint32 HEARTBEAT_MS = 1000;
	/* A still shelf rests longer between passes: until the next thing due -
	 * a frame, a focus frame, the heartbeat - and at most this, where a pass
	 * every IDLE_POLL_MS was 58 wake-ups a second on the home with nothing
	 * happening (2026-11-07). Only while nothing is under way that this loop
	 * follows by looking rather than by being woken: art decoding
	 * (texload_ready), MP4 playing (mp4_poll answers a track's end with the
	 * next), File Transfer, a pop-up or the volume bar; a held button draws
	 * every pass anyway. A press ends the rest at once all the same:
	 * plat_input_wait polls the buttons' own devices. Under AWAY_MS, which a
	 * gap between two passes would read as another screen's. */
	const Uint32 IDLE_REST_MS = 80;
	/* How long the shelf sits still, nothing pressed and no art decoding,
	 * before the memory goes down to its slower clock. See the end of the
	 * loop. */
	const Uint32 RAM_IDLE_MS = 2000;
	Uint32 last_pass = 0, last_render = 0, last_busy = plat_now_ms();
	charge_dark dark = { 0 };    /* the screen off on the charger: src/chargedark.h */

	/* At full, whatever a launcher before this one left it at. */
	g_ram_idle = true;
	ram_idle_set(false);
	/* An update the page brought over the cable that is installed now. The
	 * card's empty .media folders go once the shelf is up (after_start_step). */
	up_cable_tidy();

	/* A test of MENU > Downloads (dl_test): the page at once, and the steps
	 * behind it once it closes */
	if (getenv("ZYPEROS_DOWNLOADS_TEST")) {
		downloads_test(&a);
		a.running = false;
	}
	/* A test of MENU > Scraper's page (scp_test): the page at once */
	if (getenv("ZYPEROS_SCRAPER_TEST")) {
		scraper_page(&a, NULL);
		a.running = false;
	}
#if defined(__linux__) && !defined(ZYPEROS_DESK)
	/* The watchdog, from the main loop on (wdog.h): on a handheld only,
	 * never in a shot */
	if (!shot_path && a.running) wdog_start(NULL, plat_backlight);
#endif
	while (a.running) {
		plat_input_poll(&a.in);
		if (a.in.quit_requested || want_quit) break;
		after_start_step(&a, last_render != 0);
		/* What became of a PICO-8 zip copied by hand, once the home is up */
		if (g_p8_said[0] && last_render) {
			char said[sizeof g_p8_said];

			snprintf(said, sizeof said, "%s", g_p8_said);
			g_p8_said[0] = 0;
			result_message(&a, "PICO-8", said);
			redraw_now();
		}

		/* The memory back at full before a press is acted on, whatever it
		 * opens: a menu, a game, the next shelf's art. Games set their own
		 * clocks from there (Diatom's power profiles), and a port runs at
		 * full. */
		if (g_ram_idle) {
			for (int b = 0; b < IN_COUNT; b++)
				if (a.in.pressed[b] || a.in.down[b]) {
					ram_idle_set(false);
					last_busy = plat_now_ms();
					break;
				}
		}

		/* Followed here as well as during a game. Diatom is resident and takes
		 * SETAUDIO while idle, so a cable plugged in at the shelf moves the
		 * sound before the next launch rather than at it. */
		aout_apply(false);

		/* An account answer that outlived its game, merged when it lands, and
		 * then whatever was waiting on it sent. */
		sync_poll();

		/* Unlocks sent in the background: apply what the account answered.
		 * And a network that came back with unlocks waiting sends them, from
		 * here only - never during a game (ra_flush_watch). */
		ra_flush_poll();
		ra_flush_watch(plat_now_ms());
		ra_sent_news(ra_flush_take_sent());

		/* What MP4 said since the last pass - including END, which is how
		 * the next track of an album starts while nobody is on its screen -
		 * and the covers it was asked for by MP4's shelf, which that shelf
		 * has to be drawn again to ask the worker for. */
		mp4_poll();
		if (cover_answers()) redraw_now();

		/* Albums straight in Music/ filed under their artists, from what the
		 * player says their tags are; the shelf read again once anything
		 * moved. See file_album. */
		mp4_file_albums();
		if (g_mp4_moved && !flat_albums_waiting()) {
			g_mp4_moved = false;
			rescan_all(&a);
			redraw_now();
		}
		/* Files that arrived over a File Transfer behind the shelf, once
		 * they have been still a moment, and the shelf after one ended */
		if (xs_shelf_due()) {
			rescan_all(&a);
			redraw_now();
		}

		/* Auto Off is the same line as the power button, on every screen
		 * that draws. Diatom watches it during a game, because it owns the
		 * pad then; everywhere else the launcher can see input itself.
		 *
		 * The charger HOLDS the clock, inside idle_due. It used to gate only
		 * the shot, which let the countdown run to zero while plugged in and
		 * sit there expired - so unplugging powered the device off in the
		 * same instant, however long it had been on the cable. Found on the
		 * device 2026-08-30 at a 30s timeout. Counting the charger as
		 * activity is also what the setting says: unplugging starts a whole
		 * fresh countdown, because until then the timeout was not running. */
		/* On the cable, Auto Off's time turns the screen off instead, and
		 * while it is off every button - power too - only lights it again
		 * (src/chargedark.h). Ahead of the power check for that reason. */
		{
			bool any = false;

			for (int b = 0; b < IN_COUNT && !any; b++)
				any = a.in.pressed[b] || a.in.down[b];
			switch (charge_dark_poll(&dark, a.auto_off, plat_now_ms(), any,
			                         on_charger(), mp4c_playing())) {
			case CHARGE_GO_DARK:
				saver_stop(&a, "the screen went dark on the cable");
				snap_off(); plat_backlight(false); continue;
			case CHARGE_DARK:
				/* Dark on the cable, and still moving files behind it */
				/* Developer Mode on a release image holds its link and its
				 * SSH with the screen dark: the cable is the very reason it
				 * is dark (Auto Off on the charger), and a computer plugged
				 * in then, or a launcher restarted in the dark, must find
				 * both. The question never comes from here (Developer Mode
				 * asks none). */
				if (!usblink_dev_image() && usblink_developer(db_dev()))
					usblink_follow(db_dev(), plat_now_ms());
				dev_ssh_tick();
				if (g_xs.on) { xs_tick(&a); SDL_Delay(xs_moving() ? 4 : 50); }
				else SDL_Delay(100);
				continue;
			case CHARGE_WAKE:
				plat_backlight(true);
				plat_input_flush();
				memset(&a.in, 0, sizeof a.in);
				redraw_now();
				continue;
			case CHARGE_LIT:     break;
			}
		}

		/* USB's questions (usb_questions), after the dark screen's turn,
		 * so they wait for the screen to be lit rather than opening in the
		 * dark */
		usb_questions(&a);
		/* Zyper's offer of Automatic, due from a USB Accessory answer
		 * here or in MENU > USB Device: here, so never over a game */
		if (g_usb_offer_due && a.running) usb_offer_show(&a);
		/* A phone's USB tethering with an address: File Transfer opens on
		 * it, once a link (phonelink.h) */
		if (phone_link_due(g_xs.on)) {
			saver_stop(&a, "a phone shared its network");
			xfer_screen_way(&a, 2);
		}

		if (a.in.pressed[IN_POWER] || idle_due(&a)) { power_off(&a); break; }

		levels_keys(&a.in, BOOST_HIDE);

		/* Auto's clock: the Night Hours by the handheld, looked at once a
		 * minute (theme_tick says why not every pass). Here for the shelf;
		 * menu_run_body asks for every menu, every screen that runs a loop
		 * of its own asks through tick_theme - the keyboard's through its
		 * panel, keyboard_panel - a message asks before it is drawn, and
		 * on_game_tick asks for the file while a game runs and leaves
		 * g_theme_late for tick_theme to take. What does not ask is over in
		 * a moment or draws nothing: the launch zoom, the power-off, the
		 * frame a port loads behind, and MP4's lock, whose panel is dark
		 * until the screen it returns to asks for itself. */
		/* px2-open's commands on the shelf, where an open is A on its game
		 * (src/ctl.h), and tick_theme's turn then not a screen's */
		if (ctl_shelf_turn(&a)) {
			if (!a.running) break;
			continue;
		}
		/* The screensaver (src/saver.h): after the delay with nothing
		 * pressed it takes the shelf's place, and goes on taking it until a
		 * button. After idle_due above, which is why it can never keep the
		 * device on past Auto Off; after px2-open's commands, which a
		 * screensaver must not hold up. */
		if (saver_pass(&a)) continue;
		g_ctl_shelf_pass = true;
		tick_theme(&a);
		g_ctl_shelf_pass = false;
		/* A Scraper run that ended behind the screens may have named games
		 * and brought covers and snaps: the shelves take them here, on the
		 * shelf's own pass, as the page's close does (scraper_screen.c) */
		scraper_after_run(&a);
		/* A screenshot MENU+L1 took (src/menukeys_screen.c) on its group,
		 * read again here on the shelf's own pass, as after a game */
		if (shots_take_new()) refresh_shots_shelf(&a);

		/* Back on the shelf, so any menu MENU closed is closed: put the flag
		 * down before anything here can open another. See menu_leaving. And
		 * no screen is open over the shelf, whatever a way out that ended
		 * the launcher left counted (src/menumotion.h). */
		g_menu_closing = false;
		mm_reset();
		/* On the shelf: menu music may play, back from a game or for the
		 * first time since the launcher started */
		bgm_hold(BGM_HOLD_START | BGM_HOLD_GAME, false);
		/* ...and Zyper says each new song while it is up here */
		song_watch();
		/* ...and "Getting hot", when the chip has passed the trip */
		heat_watch();
		/* ...and that ZyperOS had to restart the last start, once */
		lastboot_watch();
		up_news_watch();
		ra_news_watch();
		dl_news_watch();

		/* SELECT is MP4, on the shelf and on every screen but a running
		 * game - see mp4_open. Before the shelf's own keys, which do not use
		 * it, so every layout gets it from this one line. */
		if (a.in.pressed[IN_SELECT]) { mp4_open(&a, NULL, NULL); continue; }

		/* MENU on the shelf is ZyperOS's own menu, the counterpart to the one
		 * MENU opens in a game. It draws over the shelf and returns here.
		 * Both handles its own, because there MENU and B mean two different
		 * menus and the screen cannot say which. */
		if (a.in.pressed[IN_MENU]) { zyperos_menu(&a); continue; }
		/* L2/R2, the menu music's tracks, on either screen */
		song_keys(&a);
		if (a.screen == SCREEN_SYSTEMS) update_systems(&a);
		else update_games(&a);
		if (!a.running) break;
		empty_watch(&a, a.screen == SCREEN_SYSTEMS && no_games(&a));

		{
			Uint32 now = plat_now_ms();
			/* Held counts as touched, not only pressed: a held direction
			 * repeats from the clock and moves the shelf every 90ms. */
			bool touched = false, away = now - last_pass > AWAY_MS;
			int b;

			for (b = 0; b < IN_COUNT && !touched; b++)
				touched = a.in.pressed[b] || a.in.down[b];
			last_pass = now;

			/* texload_ready because finished art is installed while drawing:
			 * a loop that never drew would never find it. */
			bool focus_due = now >= g_focus_at;

			if (touched || away || texload_ready() || now >= g_redraw_at ||
			    now - last_render >= HEARTBEAT_MS || snap_new_picture() ||
			    /* A whole frame where a focus frame cannot draw: something
			     * it does not draw again is up, or the screen does not
			     * keep the last frame's picture to draw over (the Brick) */
			    (focus_due && !(focus_ok(&a) && plat_screen_kept()))) {
				tick_tint(&a);
				render(&a);
				/* With the first frame up, Diatom maps its cores now (its
				 * PREMAP). Mapped the moment it listened, they were read off
				 * the card beside this process's GPU libraries and firmware,
				 * and the first frame came about 0.4 s later on most Brick
				 * boots (measured 2026-10-18). A Diatom that predates it maps
				 * them at once anyway; none at all, and this does nothing. */
				if (!last_render) {
					t_mark("first frame");
					plat_resident_line("PREMAP");
					start_reached_home();
				}
				last_render = plat_now_ms();
			} else if (focus_due) {
				/* Only the home's sprite has moved on: its square alone.
				 * last_render stays, so the heartbeat's whole frame comes. */
				focus_frame(&a);
			} else {
				/* Until a button moves, or IDLE_POLL_MS: a press is drawn
				 * the moment it comes, not up to 16 ms later */
				Uint32 rest = IDLE_POLL_MS;

				if (texload_idle() && !mp4c_playing() && !g_xs.on && !g_songpop.on &&
				    !g_heatpop.on && plat_osd_until() == UINT32_MAX) {
					Uint32 due = g_redraw_at < g_focus_at ? g_redraw_at : g_focus_at;

					if (last_render + HEARTBEAT_MS < due) due = last_render + HEARTBEAT_MS;
					rest = due > now ? due - now : 1;
					if (rest > IDLE_REST_MS) rest = IDLE_REST_MS;
				}
				plat_input_wait(xs_moving() ? 4 : rest);
			}

			/* The memory down to its slower clock once nothing has been
			 * pressed and no art decoded for RAM_IDLE_MS, and back at full as
			 * soon as art is wanted again; a press raised it at the top. A
			 * still shelf, its heartbeat and a scrolling title need nothing
			 * like the fast clock, and art loads as fast as before, because
			 * it never loads at the slow one. On the Pixel 2 an idle shelf
			 * drew 69 mW more with the DDR at 1056 MHz than at 328
			 * (2026-11-04). */
			/* The song pop-up too: a song changes by itself on a shelf
			 * nobody has touched for minutes, and Zyper's hop is drawn
			 * every frame for its two seconds */
			if (touched || !texload_idle() || g_songpop.on || xs_moving()) last_busy = now;
			ram_idle_set(now - last_busy >= RAM_IDLE_MS);
		}
	}
	/* A launcher stopped while the shelf sat still leaves nothing slow
	 * behind for whatever runs next, and nothing dark. */
	ram_idle_set(false);
	if (dark.dark) plat_backlight(true);
	/* A File Transfer behind the shelf ends with the launcher */
	xs_end();

done:
	wdog_stop();                         /* the way out is long, and nothing draws */
	ctl_stop();
	/* The downloads' requests with the launcher: the next run starts from
	 * what is missing */
	dlq_stop_all();
	free_all_textures(&a);
	/* First, and before IMG_Quit: the worker is inside IMG_Load, and the
	 * views it decodes into are freed just below. The resizer too, for the
	 * same IMG_Quit - it is in IMG_Load and IMG_SavePNG. */
	texload_stop();
	art_shrink_stop();
	/* Told, not waited for - see ra_flush_stop. */
	ra_flush_stop();
	for (int i = 0; i < sys_kept(&a); i++) {
		free(a.view[i].tex); free(a.view[i].tw); free(a.view[i].th);
		lib_free(&a.view[i].list);
	}
	page_fonts_quit();
	ui_quit();
	IMG_Quit();
	plat_input_quit();
	plat_video_quit();
	uisound_quit();
	bgm_quit();
	SDL_Quit();

	/* Close the databases, which is what checkpoints their WAL and removes
	 * it. Without this the launcher never called sqlite3_close on a normal
	 * exit, so a checkpoint only ever happened when SQLite's own 1000-page
	 * autocheckpoint fired mid-write. zyperos.db had crossed that and held 20
	 * keys; library.db had not, and its .db file was one empty page with the
	 * schema and every row living in library.db-wal. Measured 2026-09-07.
	 *
	 * That is not a growth problem - the WAL sits at its high-water mark
	 * either way. It is a portability one: a .db that means nothing without
	 * its sidecar is lost by any backup, card swap or cleanup that treats a
	 * -wal as scratch, and it reseeds silently rather than failing. Closing
	 * here leaves a complete file behind on every clean exit.
	 *
	 * A battery pull still leaves a WAL, which is what WAL recovery is for. */
	db_shutdown();
	return shot_failed ? 3 : 0;
}
