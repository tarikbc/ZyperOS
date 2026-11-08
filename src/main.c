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
#include "atomic.h"
#include "about.h"
#include "battery_eta.h"
#include "battery_poll.h"
#include "bgm.h"
#include "bgm_menu.h"
#include "blur.h"
#include "cardgen.h"
#include "cheevos.h"
#include "clock.h"
#include "config.h"
#include "bt.h"
#include "btvol.h"
#include "bt_menu.h"
#include "db.h"
#include "stats.h"
#include "sort.h"
#include "coverflow.h"
#include "library.h"
#include "platform.h"
#include "texload.h"
#include "tween.h"
#include "artshrink.h"
#include "artscrape.h"
#include "favorites.h"
#include "hare.h"
#include "xfer.h"
#include "home.h"
#include "games.h"
#include "hareart.h"
#include "hotspot.h"
#include "emptyhome.h"
#include "hotpanel.h"
#include "captive.h"
#include "sshlink.h"
#include "usblink.h"
#include "chargedark.h"
#include "idle.h"
#include "notice.h"
#include "randgame.h"
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
#include "menu.h"
#include "menumotion.h"
#include "message.h"
#include "menusnap.h"
#include "musec.h"
#include "mp4lock.h"
#include "mp4video.h"
#include "museart.h"
#include "muselib.h"
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
#include "controls.h"
#include "tour.h"
#include "tourdraw.h"
#include "game_menu.h"
#include "gamedel.h"
#include "dsv.h"
#include "ui.h"
#include "wifi_menu.h"
#include "zyperrig.h"
#include "songpop.h"
#include "heat.h"
#include "lowbatt.h"
#include "charging.h"
#include "app.h"
#include "aout_route.h"
#include "bt_screen.h"
#include "panel.h"
#include "place.h"
#include "portload.h"
#include "powerscreens.h"
#include "raqueue.h"
#include "shelfbuild.h"
#include "stats_screen.h"
#include "zyperdraw.h"
#include "zypermark.h"

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

/* The battery's low and critical warnings, 10% and 5%, are src/status.h's
 * (STATUS_BATT_LOW, STATUS_BATT_CRITICAL), which the shelf's battery mark and
 * Diatom's disc over a game both go by. Critical blinks: the last minutes
 * before BATT_EMPTY_PCT ends the game and powers off, which a steady mark gave
 * no warning of. */
/* BATT_EMPTY_PCT, where the device powers off, is src/status.h's too:
 * portkeys powers off there over a port. */
#define TEX_KEEP_NEAR   8   /* card textures kept around the cursor, in view  */
#define TEX_KEEP_FAR    4   /* ...and around the cursor of a system you left  */

/* The row is allowed to glide only across cards this policy keeps decoded. If
 * TEX_KEEP_NEAR ever drops below it, a long move would animate over cold cards
 * and stall exactly as it did before CF_WARM_CARDS existed. */
_Static_assert(TEX_KEEP_NEAR >= CF_WARM_CARDS,
               "the row glides across cards the texture policy must keep");

/* How a Muse screen was left: back one level, Muse closed altogether, or the
 * library rebuilt under it by Rescan Folder, so every album index it held is
 * stale and the shelf has to find itself again. */
typedef enum { MUSE_BACK, MUSE_CLOSE, MUSE_REBUILT } muse_exit;
/* Muse's shelf borrows the games shelf's cards and its letter jump, both of
 * which live down with the shelf code. */
static void muse_shelf_screen(app *a, bool now);
static int  shelf_letter_jump(sysview *v, int dir);
/* Whether a shelf is Muse's; sorting needs to know, well before Muse's code. */
static bool is_muse(const system_cfg *s);
static bool is_port(const system_cfg *s);
/* The pocket lock's gate and MENU's way in, defined with the lock itself. */
static bool lock_allowed(void);
static void muse_ask_lock(void);
static void muse_ask_shuffle(void);
/* Muse's menu opens it, and the menu is up here. */
static void album_art_screen(app *a);
/* Same reason: Wi-Fi File Transfer is a screen up here. */
static void xfer_screen_way(app *a, int way);

volatile sig_atomic_t want_quit;
static void on_sigterm(int sig) { (void)sig; want_quit = 1; plat_terminate(); }

/* Boot time is the one number a launcher cannot be careless about, so the
 * phases are timed and logged rather than guessed at. On the project this
 * grew out of, the phase everyone assumed was expensive turned out to be 8%
 * of startup -- which is only knowable by measuring. */
static unsigned t_boot0;
static void t_mark(const char *what)
{
	fprintf(stderr, "boot: %-14s %5u ms\n", what, plat_now_ms() - t_boot0);
}

/* One number for the way back, because this path keeps being asked how fast it
 * is and a number is how the answer stays true. The five phase marks that took
 * it from 213ms to here are in the history if it ever needs breaking down
 * again; the budget they found was 5ms to black, 4ms of bookkeeping, and the
 * rest decoding the focused card's autosave preview off the SD card. */
static unsigned t_back0;

/* Counts the frames begun. What starts a whole frame steps it - draw_shelf,
 * paused_backdrop, present_clear and Now Playing's np_draw, one of which
 * starts every frame a menu's panel can be drawn over or left for. The panel
 * drawn last remembers the count it was drawn at (g_snap), and is still the
 * one on screen only while the count has not moved since: a keyboard, a slot
 * strip or Now Playing drawn after it has moved it. */
static unsigned g_frame_no;

/* One present of nothing but `c`, over the whole screen. */
static void present_clear(app *a, ds_rgb c)
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
static void present_ground(app *a)
{
	present_clear(a, theme()->ground);
}

/* Black, the one color here that is not the theme's, because it is Diatom's:
 * its pages start opaque black and it draws only the game's picture into
 * them, so the border around a game that does not fill the panel is this
 * black in both themes - the game is content, not part of the theme (DESIGN.md
 * 3.4). What resuming a game presents has to be the same black, or the border
 * would change color as Diatom cycles its pages (see where this is called). */
static const ds_rgb DIATOM_PAGE = { 0, 0, 0 };

/* ---------- per-system display mode --------------------------------------- */

/* Keyed on the system's tag rather than its folder or its place in the list,
 * for the same reason saves are: renaming a ROM folder or reordering
 * systems.cfg then cannot quietly hand a system somebody else's setting. */
void display_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR];
	int i, k;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "display.%s", a->sys.systems[i].tag);
		if (!db_get_str(db_dev(), key, name, sizeof name, NULL)) continue;
		for (k = 0; k < DMODE_COUNT; k++)
			if (!strcmp(DMODES[k].name, name)) { a->view[i].dmode = k; break; }
	}
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
static void display_save(app *a)
{
	char key[CFG_STR + 16];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "display.%s", a->sys.systems[i].tag);
		db_set_str(db_dev(), key, DMODES[a->view[i].dmode].name);
	}
}

/* ---------- per-system core ----------------------------------------------- */

/* A shelf whose core field lists more than one core, the first the default -
 * the Nintendo DS: DSperate, a libretro core run in Diatom with ZyperOS's own
 * in-game menu, and DraStic, a standalone program run through a script. The
 * shelf's Core row chooses (core_step, from the system menu), and the choice
 * is kept per system under core.<TAG>, beside display.<TAG>. Everything
 * that runs a game reads s->core, so choosing is writing that field and
 * nothing else follows it.
 *
 * A core that is not on the card is passed over when there is no choice on
 * record: a card without DSperate's core plays its DS games on DraStic, if
 * that is there, without anyone visiting a menu, and the row offers both. A
 * choice on record is kept even if its core has gone, so taking DraStic off
 * for a while does not lose it. */
static bool core_present(const char *core)
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

/* What the Core row says: a libretro core by its short name, as every other
 * shelf's row always has, and a script by its own name - "DraStic" for
 * run:Emulators/DraStic/DraStic.sh - since the path is the card's business. */
static const char *core_label(const char *core, char *buf, size_t n)
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

	for (i = 0; i < a->sys.count; i++) {
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
			if (cfg_core_at(s, k, alt, sizeof alt) && core_present(alt)) {
				snprintf(s->core, sizeof s->core, "%s", alt);
				set = true;
			}
		fprintf(stderr, "core: %s runs on %s%s\n", s->name, s->core,
		        set ? "" : " (none of its cores is on the card)");
	}
}

/* The next core in the shelf's list, or the previous one. */
static void core_step(app *a, int sys, int d)
{
	system_cfg *s = &a->sys.systems[sys];
	char key[CFG_STR + 16], alt[CFG_STR];
	int n = cfg_core_count(s), k, cur = 0;

	if (n < 2) return;
	for (k = 0; k < n; k++)
		if (cfg_core_at(s, k, alt, sizeof alt) && !strcmp(alt, s->core)) cur = k;
	cur = (cur + d + n) % n;
	if (!cfg_core_at(s, cur, alt, sizeof alt)) return;
	snprintf(s->core, sizeof s->core, "%s", alt);
	snprintf(key, sizeof key, "core.%s", s->tag);
	db_set_str(db_dev(), key, s->core);
	fprintf(stderr, "core: %s now runs on %s\n", s->name, s->core);
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
static bool g_game_dual;

static bool two_screens(const system_cfg *s)
{
	return !strcmp(s->tag, "NDS") && strncmp(s->core, "run:", 4) != 0 &&
	       strcmp(s->core, "port") != 0;
}

static void screens_get(const char *tag, int *layout, bool *swap)
{
	char key[32], v[32];

	snprintf(key, sizeof key, "screens.%s", tag);
	*layout = db_get_str(db_dev(), key, v, sizeof v, NULL) ? gm_layout_index(v)
	                                                      : GM_LAYOUT_DEFAULT;
	*swap = false;
}

static void screens_put(const char *tag, int layout, bool swap)
{
	char key[32];

	snprintf(key, sizeof key, "screens.%s", tag);
	db_set_str(db_dev(), key, gm_layout_names[layout]);
	plat_resident_line("SETSCREENS\tlayout=%s\tswap=%d", gm_layout_names[layout], swap ? 1 : 0);
}

/* ---------- per-system sort order ----------------------------------------- */

/* Keyed on the tag for the same reason display.<TAG> is, and stored beside it.
 * Loading does NOT re-sort: it runs before the scan on a rescan and after it
 * at startup, and the shelf is put in order by sort_all once both are done.
 *
 * Muse's shelf has orders of its own, by artist and by album, stored by their
 * own names under the same key - sort.MUSE. */
void sort_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		if (!db_get_str(db_dev(), key, name, sizeof name, NULL)) continue;
		a->view[i].sort = is_muse(&a->sys.systems[i]) ? (int)ml_order_index(name)
		                                              : sort_index(name);
	}
}

static void sort_save(app *a)
{
	char key[CFG_STR + 16];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		db_set_str(db_dev(), key,
		           is_muse(&a->sys.systems[i]) ? ml_order_name((ml_order)a->view[i].sort)
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
 * Muse by its own orders, in muse_order_view: its cards are albums, and which
 * album each card is has to move with it. */
void sort_shelf(app *a, int sys)
{
	sysview *v = &a->view[sys];

	if (is_muse(&a->sys.systems[sys])) { muse_order_view(v); return; }
	/* The added times this order reads, each game's from its own system's
	 * folder (Favorites' from its owner's), where the scan left them unknown */
	if (sort_needs_added(v->sort))
		for (int i = 0; i < v->list.count; i++)
			if (v->list.items[i].added == LIB_ADDED_UNKNOWN)
				v->list.items[i].added = lib_added(P_ROMS,
					a->sys.systems[v->owner ? v->owner[i] : sys].folder,
					v->list.items[i].file);
	if (v->owner) {
		const char *tags[CFG_MAX_SYSTEMS];
		int i;

		for (i = 0; i < a->sys.count && i < CFG_MAX_SYSTEMS; i++)
			tags[i] = a->sys.systems[i].tag;
		sort_apply_owned(v->list.items, v->owner, v->list.count, v->sort, tags);
		return;
	}
	sort_apply(v->list.items, v->list.count, v->sort, a->sys.systems[sys].tag);
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
	SDL_Surface *raw = IMG_Load(path), *conv;

	if (!raw) return NULL;
	conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	return conv;
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
	SDL_FreeSurface(conv);
	if (t) SDL_QueryTexture(t, NULL, NULL, w, h);
	return t;
}


/* Muse: what the Music folder holds, read at boot and on every rescan.
 *
 * RECOGNIZED BY ITS TAG, not by what it lacks. Favorites is the shelf with no
 * core and no folder, and the system menu tells them apart that way; Muse has
 * neither either, but its music is not a ROM folder at all, so an empty folder
 * is the honest value and the tag is what says which of the two it is. */
ml_lib g_muse;
static home_song g_home_song;
static int g_home_track = -1;
/* Where the scan read it from: the card, which holds Music and Audiobooks. */
char   g_muse_root[CFG_STR * 2];
/* Bumped by every build of the library, so a Muse screen can tell that MENU's
 * Rescan Folder replaced it while its menu was open - see muse_menu. */
unsigned g_muse_gen;

static bool is_muse(const system_cfg *s)
{
	return s && !strcmp(s->tag, "MUSE");
}

/* What is known about each album's cover: one per album in g_muse, rebuilt
 * with it. ASKED carries when, because an answer can be lost with the daemon,
 * and an album is then asked about again rather than waited on for good.
 * The states, COV_*, and cover_state are in src/app.h. */
cover_state *g_cov;

/* ---- Muse: books ------------------------------------------------------------ */

/* Which kind Muse's shelf shows, music or books - and now videos, MP4's
 * third kind (ml_kind). Eric's, 2026-09-27: one Muse card and a Show row in
 * its menu, rather than a card for books beside it - two ways into one player
 * read as two players. Kept across restarts. */
int g_muse_kind;

/* Whether each album is a book listened to the end: one per album in g_muse,
 * rebuilt with it, like g_cov. A finished book is marked and not forgotten,
 * and A on it starts it over. Eric's, 2026-09-27. */
bool *g_book_done;

/* The kind the shelf really shows: the one chosen, unless there is none of it
 * - then the first kind there is some of. */
ml_kind muse_kind_shown(void)
{
	int k;

	if (g_muse_kind >= 0 && g_muse_kind < ML_KINDS && ml_count_kind(&g_muse, (ml_kind)g_muse_kind))
		return (ml_kind)g_muse_kind;
	for (k = 0; k < ML_KINDS; k++)
		if (ml_count_kind(&g_muse, (ml_kind)k)) return (ml_kind)k;
	return ML_MUSIC;
}

static bool muse_books_shown(void) { return muse_kind_shown() == ML_BOOKS; }

/* How many kinds the card has: Show is in the menu only when it is two or
 * more. */
static int muse_kinds(void)
{
	int k, n = 0;

	for (k = 0; k < ML_KINDS; k++)
		if (ml_count_kind(&g_muse, (ml_kind)k)) n++;
	return n;
}

/* Where book `al`'s place is kept: the settings, under its folder. */
void book_key(int al, char *out, size_t n)
{
	const char *p = g_muse.tracks[g_muse.albums[al].first].path;
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
	fprintf(stderr, "muse: finished %s\n", g_muse.albums[al].name);
}

/* Where book `al` starts: its file, and `at` seconds into it. The beginning
 * for a book never started, or finished. */
static int book_place(int al, double *at)
{
	const ml_album *b = &g_muse.albums[al];
	char key[LIB_PATH + 8], val[LIB_PATH + 32];
	const char *tab;
	int i;

	*at = 0;
	book_key(al, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "");
	if (!(tab = strchr(val, '\t'))) return 0;
	for (i = 0; i < b->n; i++) {
		const char *p = g_muse.tracks[b->first + i].path;
		const char *f = strrchr(p, '/');

		if (!strcmp(f ? f + 1 : p, tab + 1)) { *at = atof(val); return i; }
	}
	return 0;
}

/* KEEPING A BOOK'S PLACE, wherever Muse is polled: on its screens, over the
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
	const mu_now *mn = musec_now();
	const char *track = musec_track(mn->index);

	if (!musec_is_videos() || !track) {
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

void book_keep(bool now)
{
	const mu_now *mn = musec_now();
	const char *track;
	bool ran_out = musec_take_ran_out();
	int al;

	video_keep(now, ran_out);
	if (!musec_is_book() || !g_book_done) { g_bk.al = -1; return; }
	track = musec_track(mn->index);
	al = ml_album_of(&g_muse, track);
	if (al < 0 || !g_muse.albums[al].book) { g_bk.al = -1; return; }
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

/* Muse, polled: what the daemon said, and a book's place kept. Everything that
 * polls Muse comes through here. */
void muse_poll(void)
{
	musec_poll();
	book_keep(false);
	/* Menu music is quiet while Muse is heard, or about to be */
	bgm_hold(BGM_HOLD_MUSE, musec_heard());
	const mu_now *mn = musec_now();
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
	const char *p = g_muse.tracks[g_muse.albums[al].first].path;
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "bookspeed.%.*s", slash ? (int)(slash - p) : 0, p);
}

static double book_speed(int al)
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
static void book_cycle_speed(void)
{
	static const double STEPS[] = { 1.0, 1.25, 1.5, 1.75, 2.0, 0.75 };
	int al = ml_album_of(&g_muse, musec_track(musec_now()->index));
	double now = musec_speed(), next = STEPS[0];
	char key[LIB_PATH + 16], val[16];
	size_t k;

	for (k = 0; k < sizeof STEPS / sizeof STEPS[0]; k++)
		if (fabs(STEPS[k] - now) < 0.01) {
			next = STEPS[(k + 1) % (sizeof STEPS / sizeof STEPS[0])];
			break;
		}
	musec_set_speed(next);
	if (al < 0 || !g_muse.albums[al].book) return;
	book_speed_key(al, key, sizeof key);
	snprintf(val, sizeof val, "%.2f", next);
	db_set_str(db_dev(), key, val);
}

/* A new queue. Whatever book it replaces has its place written first, since
 * the queue is the only record of where that was. */
static void muse_play(const char *const *paths, int n, int start, double at,
                      bool book, double speed, const char *artist, const char *album)
{
	book_keep(true);
	g_bk.al = -1;
	g_vk.track[0] = '\0';
	musec_play(paths, n, start, at, book, speed, artist, album);
}

/* A queue of videos, from `start`, at its file's kept place. */
static void muse_play_videos(const char *const *paths, int n, int start,
                             const char *artist, const char *album)
{
	book_keep(true);
	g_bk.al = -1;
	g_vk.track[0] = '\0';
	musec_play_videos(paths, n, start, start >= 0 && start < n ? video_place(paths[start]) : 0,
	                  artist, album);
}

/* A track's artist and album, as the shelf names them: for a queue drawn from
 * every album, Shuffle All's, which has no one album to name. */
static void muse_track_names(const char *track, char *artist, size_t an,
                             char *album, size_t aln)
{
	int al = ml_album_of(&g_muse, track);

	artist[0] = album[0] = '\0';
	if (al < 0) return;
	snprintf(artist, an, "%s", g_muse.artists[muse_artist_of(al)].name);
	snprintf(album, aln, "%s", g_muse.albums[al].name);
}

/* The play modes as the player meets them: the name saved in the settings and
 * the mark. In order has no mark: it is what plays when nothing has been asked
 * for, and a mark meaning "nothing special" is noise.
 *
 * The name each mode was SHOWN by went with the notice that showed it - see
 * muse_cycle_mode. Nothing spells a mode out now; the mark beside the track
 * count on Now Playing is the whole of it. */
static const struct { const char *key; int glyph; } MUSE_MODES[MUQ_MODES] = {
	[MUQ_IN_ORDER]   = { "in order",   -1 },
	[MUQ_REPEAT_ALL] = { "repeat all", UI_GLYPH_REPEAT },
	[MUQ_REPEAT_ONE] = { "repeat one", UI_GLYPH_REPEAT_ONE },
	[MUQ_SHUFFLE]    = { "shuffle",    UI_GLYPH_SHUFFLE },
};

/* The cover on the Now Playing screen, kept between visits so that opening it
 * is not a decode: the album it is for and its texture. `done` means this is
 * what the album will show - its art, or the card made for one with none - and
 * nothing more needs asking. */
static struct {
	int          album;
	bool         done;
	SDL_Texture *tex;
} g_np = { .album = -1 };

/* The menus' panel pass (panel_pass_begin): a screen, kept rather than made
 * per frame. */
static SDL_Texture *g_panel_tex;

/* Game cards keep their theme tags; home art is theme-independent. */
bool tex_stale(SDL_Texture *t)
{
	return t && !cardgen_kept(ui_card_theme(t), theme_index());
}
static bool g_loads_held;

/* Explicit home inputs. The renderer never changes the library or owns game art. */
static void home_items(const app *a, home_item *items)
{
	for (int i = 0; i < a->sys.count; i++)
		items[i] = (home_item){ a->sys.systems[i].tag, a->sys.systems[i].name };
}
static void prime_home(app *a)
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
	snprintf(buf, sizeof buf, "%d", slot);
	return buf;
}

/* The name a game's states and previews go by: its file's, which for a disc
 * game in a folder is the disc's ("Game (Disc 1).chd"). On a hooked shelf the
 * folder is the game and the file is whatever it held first - readme.txt for
 * two different ScummVM games - so the folder's name. */
static const char *persist_base(app *a, int s, const game_entry *g)
{
	const char *base = strrchr(g->file, '/');

	if (base && lib_shelf_hooked(a->sys.systems[s].exts)) return g->name;
	return base ? base + 1 : g->file;
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
static void pause_preview_path(char *out, size_t n)
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
static void slot_state_path(app *a, int s, const game_entry *g, int slot,
                            char *out, size_t n)
{
	snprintf(out, n, "%s/.zyperos/%s/%s.%s.state",
	         P_SHARED, a->sys.systems[s].folder, persist_base(a, s, g),
	         slot_name(slot));
}

static void slot_preview_path(app *a, int s, const game_entry *g, int slot,
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

static void persist_dir_ensure(app *a, int s)
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
static sysview *owner_view(app *a)
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
 * and draw another. */
void box_art_path(const char *folder, const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/%s/.media/%s.png", P_ROMS, folder, name);
}

/* Whether it is there. An empty file is not art - the rule the info screen
 * already used, now the rule for everyone. */
static bool has_box_art(const char *folder, const char *name)
{
	char p[LIB_PATH * 3];
	struct stat st;

	box_art_path(folder, name, p, sizeof p);
	return stat(p, &st) == 0 && st.st_size > 0;
}

/* Whether game `i`'s slot on a shelf wants a card: it has none, or it holds a
 * generated card made for the theme that was, which stays on the shelf until
 * its replacement comes (theme_changed). Box art is no theme's and never is
 * stale. */
static bool game_card_wanted(const sysview *v, int i)
{
	return !v->tex[i] || tex_stale(v->tex[i]);
}

/* Muse's half of the card machinery, defined with the rest of Muse. */
static void muse_card_want(app *a, int s, int i);
static void cover_shape(SDL_Surface **s);

SDL_Texture *game_get_tex(void *ctx, int i, int *w, int *h, float *cb)
{
	app *a = ctx;
	int s = a->sys_cursor;
	sysview *v = &a->view[s];
	/* The game's own system, not the shelf's: on Favorites those differ, and
	 * every one of these three lookups would otherwise go to the wrong
	 * folder, the wrong state directory and the wrong accent. */
	int o = shelf_owner(a, s, i);

	if (is_muse(&a->sys.systems[s]) && game_card_wanted(v, i)) {
		muse_card_want(a, s, i);
	} else if (!is_muse(&a->sys.systems[s]) && game_card_wanted(v, i)) {
		char art[LIB_PATH * 3];
		ui_card_order card;

		box_art_path(a->sys.systems[o].folder, v->list.items[i].name,
		             art, sizeof art);
		/* Hand it to the worker and draw what the slot holds this frame:
		 * nothing, or the card of the theme that was until its replacement
		 * lands (theme_changed). Asking is cheap and ignores a card already
		 * in hand, so callers ask every frame. With the card to draw if
		 * there is no box art, so that is the worker's to draw as well. */
		ui_card_order_set(&card, v->list.items[i].title,
		                  a->sys.systems[o].accent, false);
		bool asked = texload_want_or_draw(s, i, art, NULL, &card);

		if (!asked && !g_loads_held) {
			/* No worker. Exactly what this did before, on the frame, over
			 * the card of the theme that was if there is one. */
			if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = load_image(a->r, art, &v->tw[i], &v->th[i], &v->cb[i]);
			if (!v->tex[i])
				v->tex[i] = ui_make_card(a->r, v->list.items[i].title,
				                         a->sys.systems[o].accent,
				                         &v->tw[i], &v->th[i]);
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
static void shelf_put(void *ctx, texload_got *g)
{
	app *a = ctx;
	SDL_Surface *surf = g->surf;
	int s = g->sys, i = g->idx;
	sysview *v;

	if (home_accept(&a->home, a->r, s, i, surf)) {
		if (surf) SDL_FreeSurface(surf);
		return;
	}
	if (s < 0 || s >= a->sys.count) { if (surf) SDL_FreeSurface(surf); return; }
	v = &a->view[s];
	/* The shelf may have moved on, or something else may have filled this
	 * slot while the decode was in the air. A slot holding a generated
	 * card of the theme that was is not filled: it is waiting for this. */
	if (i < 0 || i >= v->list.count || !game_card_wanted(v, i)) {
		if (surf) SDL_FreeSurface(surf);
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
		v->tex[i] = ui_card_upload(a->r, surf, g->theme);
		if (v->tex[i]) { v->tw[i] = surf->w; v->th[i] = surf->h; }
		v->cb[i] = 1.0f;
		SDL_FreeSurface(surf);
		surf = NULL;
	}
	/* A cover is squared and given a card's corners, the way Now
	 * Playing draws it, so one album looks the same in both places. */
	if (surf && is_muse(&a->sys.systems[s])) cover_shape(&surf);
	if (surf) {
		v->cb[i] = content_bottom(surf);
		v->tex[i] = SDL_CreateTextureFromSurface(a->r, surf);
		if (v->tex[i])
			SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
		SDL_FreeSurface(surf);
	}
	if (!v->tex[i]) {
		/* No file could be read and no card came with it - the worker could
		 * not have the faces: the generated card, made here. */
		int o = shelf_owner(a, s, i);
		v->tex[i] = is_muse(&a->sys.systems[s])
		          ? ui_make_cover(a->r, v->list.items[i].title,
		                          a->sys.systems[o].accent, &v->tw[i], &v->th[i])
		          : ui_make_card(a->r, v->list.items[i].title,
		                         a->sys.systems[o].accent,
		                         &v->tw[i], &v->th[i]);
		v->cb[i] = 1.0f;
	}
}

/* Finished art into textures, as much of it as fits in this frame's share
 * (texload_deliver): a landing or a fast scroll finishes a dozen at once,
 * and one frame making every texture was the frame the shelf hitched on. The
 * rest come in the next frames, which the loops draw while any is waiting
 * (texload_ready). */
static void texload_drain(app *a)
{
	texload_deliver(theme_index(), shelf_put, a);
}

static void evict_far(sysview *v, int keep)
{
	for (int i = 0; i < v->list.count; i++) {
		int d = abs(i - v->cursor);
		if (v->list.count >= CF_WINDOW)
			d = d > v->list.count / 2 ? v->list.count - d : d;
		if (v->tex[i] && d > keep) {
			SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = NULL;
		}
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
static void prime_window(app *a, int s)
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
static void prime_toward(app *a, int s, int center)
{
	prime_around(a, s, center, CF_HALF_WINDOW);
}


static void free_view_textures(sysview *v)
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

/* Muse's shelf onto another kind: music, books or videos, from the first
 * card. The textures go first, while the count is still the old kind's, since
 * every card is about to be a different album. */
static const char *const MUSE_SHOW[ML_KINDS] = { "music", "books", "videos" };

static void muse_show(sysview *v, ml_kind kind)
{
	g_muse_kind = kind;
	db_set_str(db_dev(), "muse.show", MUSE_SHOW[kind < ML_KINDS ? kind : ML_MUSIC]);
	free_view_textures(v);
	muse_order_view(v);
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
static void wifi_pref_save(bool on)
{
	db_set_int(db_dev(), "wifi", on ? 1 : 0);
	/* launch.sh reads this one before the launcher exists, so the export has
	 * to follow the write rather than wait for a tidy shutdown. */
	db_write_boot_env();
}


/* Diatom watches this file, and it is rewritten per launch: it is the set
 * minus what has already been earned, which only this side knows. Runtime
 * scratch, so per-device rather than shared. */
static void chv_active_path(char *out, size_t n)
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
static const int AUTO_OFF[] = { 0, 30, 60, 120, 300, 600 };
#define AUTO_OFF_COUNT ((int)(sizeof AUTO_OFF / sizeof AUTO_OFF[0]))

static int auto_off_load(void)
{
	int v = db_get_int(db_dev(), "autooff", -1);
	return v >= 0 ? v : 120;                  /* the default: two minutes */
}

static void auto_off_save(int seconds)
{
	db_set_int(db_dev(), "autooff", seconds);
}

/* MENU > Charge Limit (DESIGN.md 11.26; my picks in the charge limit
 * lab, 2026-11-06): the charge the battery stops at to keep it healthy for
 * longer, 0 for Off, a full charge, as a new card has. On a charger that can
 * stop short (plat_charge_limit_supported): plastron's rk817 driver turns it
 * off at the limit and on again 2 points under, so 80% holds between 78 and
 * 80%. The kernel forgets it at a power-off, so it is set at every start. */
static const int CHARGE_LIMITS[] = { 0, 80, 85, 90 };
#define CHARGE_LIMITS_COUNT ((int)(sizeof CHARGE_LIMITS / sizeof CHARGE_LIMITS[0]))
/* "Hold A for a full charge" on the charging screen: this charge goes to
 * 100%, and the limit is back when the cable comes out (charge_full_end).
 * In /tmp, so a power-off ends it too. */
#define CHG_FULL_ONCE "/tmp/zyperos_charge_full"
static int g_charge_limit;

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

static void charge_limit_apply(void)
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
static long g_sync_game;                   /* the game asked about, 0 none */
static char g_sync_set[LIB_PATH * 2];      /* its set, to check the answer against */

/* A send waiting for the answer. */
static bool g_sync_flush;
static char g_sync_rom[LIB_PATH * 2], g_sync_tag[16];
static int  g_sync_cur;

static void sync_begin(long game, const char *set_path)
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
static int sync_poll(void)
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
static void sync_abandon(void)
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
static char g_pending_set[LIB_PATH * 2];
static char g_pending_active[LIB_PATH * 2];
/* Auto crossed 7:00 or 19:00 while a game ran, and the file was rewritten
 * for Diatom's bar but nobody has told the shelf: theme() is the new theme
 * and the cards kept are the old one's. tick_theme takes it as a change like
 * its own clock's, so the first screen to ask after on_game_tick set it
 * tells theme_changed - the in-game menu and Muse opened from it, during the
 * game, or the way back from it (launch) before its first frame - and
 * theme_changed puts it down. */
static bool g_theme_late;
/* The watch list Diatom was last handed and when, so an account answer that
 * lands early can trim it. 0 when there is no list to trim. */
#define WATCH_RETRIM_MS 5000
static unsigned g_watch_ms;
static char     g_watch_active[LIB_PATH * 2];
/* The set path of the game actually running, so a fetch that lands during a
 * DIFFERENT game can be recognized and dropped. Written at every launch. */
static char g_game_set[LIB_PATH * 2];

/* Auto Off while a game runs. Diatom holds the clock, because it owns the pad
 * and this process is blocked in plat_resident_wait; the launcher's part is
 * telling it the number and keeping that number true as the charger comes and
 * goes. See on_charger, which lives beside battery_level. */
static int  g_idle_secs;         /* the Auto Off setting, 0 off */
static bool g_idle_charging;     /* what was true when SETIDLE was last sent */

/* "Getting hot" (src/heat.h, DESIGN.md 11.23): the chip's temperature, read
 * every HEAT_POLL_MS by every screen through idle_due and by a game's tick,
 * and said where it can be seen - Zyper's pop-up on the home and the shelves
 * (heat_watch), a notice over a running game (on_game_tick). The words are
 * short enough for the pop-up's slab without scrolling. */
static heat_state g_heat;
#define HEAT_HEAD T(S_HEAT_HEAD)
#define HEAT_BODY T(S_HEAT_SLOWING)

void heat_tick(void)
{
	heat_poll(&g_heat, plat_now_ms(), plat_chip_temp);
}

/* Said: one line in the log with the reading, pop-up or notice */
static void heat_said(const char *where)
{
	fprintf(stderr, "heat: %d.%d C, getting hot (%s)\n", g_heat.mc / 1000,
	        (g_heat.mc % 1000) / 100, where);
}

/* The pop-ups over a game, further down */
static void notice_pump(void);
static void heat_notice(void);
static void notice_queue(nq_kind kind, int id, int ms, const char *head,
                         const char *body, const char *badge, int value, int target);

static void on_game_tick(void)
{
	char msg[96];

	int rc;

	/* Muse, bounded: read what has arrived, and send at most one PLAY when a
	 * track ends - which is what keeps an album going through a game.
	 *
	 * And the game goes quiet while it plays - Diatom's QUIET, followed
	 * here at ten a second, which is how pausing the music or the album
	 * ending brings the game's sound back within a tenth of a second. Only a
	 * change is sent. */
	muse_poll();
	plat_resident_quiet(musec_playing());
	/* The next pop-up, once the one on screen has had its time */
	notice_pump();
	/* The chip's temperature, every HEAT_POLL_MS; "Getting hot" over the
	 * game when it passes the trip, ahead of any pop-up waiting */
	heat_tick();
	if (heat_take(&g_heat, plat_now_ms())) heat_notice();
	/* Auto's clock, for the file and for nothing else. Diatom reads the theme
	 * from THEME_FILE as its bar comes up, and the launcher is in this wait
	 * for the whole of a game, so a game that is on across 19:00 would have
	 * its bar in the theme of the hour it started in. theme_tick writes the
	 * file when the theme changes and draws nothing, and it looks at the
	 * clock once a minute, so it costs a compare here. The screens are told
	 * by the next one that asks, through tick_theme: g_theme_late. */
	if (theme_tick()) g_theme_late = true;
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

	if (g_idle_secs > 0) {
		bool ch = on_charger();

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

	if (!g_pending_set[0]) return;

	/* Once. Calling it twice advances the state machine twice, which is a
	 * lookup answered and then thrown away. */
	rc = ra_fetch_step();
	if (rc == 0) return;                        /* still working */
	if (rc < 0) { g_pending_set[0] = '\0'; return; }  /* RA has never seen it */

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

static noticeq g_nq;

/* The next one, if the one on screen has had its time. From the game tick,
 * and as soon as one is queued. */
static void notice_pump(void)
{
	nq_item it;

	if (!nq_next(&g_nq, plat_now_ms(), &it)) return;
	if (it.mark ? notice_render_mark(it.head, it.body, (notice_mark)it.mark,
	                                 NOTICE_FILE, theme_index())
	            : notice_render_ex(it.head, it.body, it.badge[0] ? it.badge : NULL,
	                               it.value, it.target, NOTICE_FILE, theme_index()))
		plat_resident_line("OVERLAY\tpath=%s\tms=%d", NOTICE_FILE, it.ms);
}

/* "Getting hot" over a game (DESIGN.md 11.23): the notice with a thermometer
 * where a badge goes, held **hold-heat**, ahead of every pop-up waiting.
 * Zyper is never over a game (DESIGN.md 10). */
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

static void notice_queue(nq_kind kind, int id, int ms, const char *head,
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

	if (!chv_note_unlock(id)) return;

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
		if (c) rb_path(chv_source(), c->badge, p, sizeof p);
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
	/* "74/100" as rcheevos writes it, spaced the way the list writes counts */
	if (strchr(text, '/') && !strchr(text, ' '))
		snprintf(count, sizeof count, "%.*s / %s", (int)(strchr(text, '/') - text),
		         text, strchr(text, '/') + 1);
	else
		snprintf(count, sizeof count, "%s", text);
	rb_path(chv_source(), c->badge, p, sizeof p);
	notice_queue(NQ_PROGRESS, id, 2500, c->title[0] ? c->title : T(S_CHEEVOS_PROGRESS), count,
	             p[0] ? p : NULL, value, target);
}

/* Called from inside plat_resident_wait when Diatom says the fast-forward step
 * changed, or that a game resumed fast. Drawn as an unlock notice is -
 * rendered here, composited there - and shorter, since it says a word. The
 * step, not the speed reached: on a heavy SNES or GBA game 3x and 4x both run
 * about 2.5x, and that figure moves from scene to scene. */
static void on_speed(int x)
{
	char p[CFG_STR * 2], body[16];

	if (x > 1) snprintf(body, sizeof body, "%dx", x);
	else       snprintf(body, sizeof body, "%s", T(S_POPUP_FF_OFF));
	/* Past the queue, at once - it answers a press - and the queue waits for
	 * it to go */
	snprintf(p, sizeof p, "%s", NOTICE_FILE);
	if (notice_render(T(S_POPUP_FAST_FORWARD), body, p, theme_index()))
		plat_resident_line("OVERLAY\tpath=%s\tms=1500", p);
	nq_hold(&g_nq, plat_now_ms(), 1500);
}

static wifi_state menu_wifi(char *ssid, int cap);   /* with the menus, below */
static unsigned g_wifi_asked_at;   /* when menu_wifi last forked, plat_now_ms */

/* ---------- the status corner ---------------------------------------------- */

/* MENU > Clock: the time in 24 hours, on the shelf's corner and in Date &
 * Time's row; 12-hour, the default, is what the launcher always was. Saved in
 * the device's database as Appearance is, under `clock`, "12h" or "24h". */
static bool g_clock24;

static void clock24_load(void)
{
	char v[8];

	db_get_str(db_dev(), "clock", v, sizeof v, "12h");
	g_clock24 = !strcmp(v, "24h");
}

static void clock24_save(bool on)
{
	g_clock24 = on;
	db_set_str(db_dev(), "clock", on ? "24h" : "12h");
}

/* MENU > Battery Icon: the corner's battery as the mark alone ("bars", the
 * default), with its charge written beside it ("percentage"), my ask of
 * 2026-11-05, or with the time left written there ("time"), my ask of
 * 2026-11-07. Read with the clock's setting; draw_corner_at writes the text.
 * The time left is the estimate in src/battery_eta.h off the cable and the
 * time to full (or to the charge limit) on it; where there is none yet - the
 * first minutes after the start, a cable just plugged in, a charger whose
 * gauge cannot say - it writes the percentage, so the corner is never empty
 * of a number. */
enum { BATT_BARS, BATT_PCT, BATT_TIME, BATT_MODES };
static const char *const BATT_IDS[BATT_MODES] = { "bars", "percentage", "time" };
static int g_batt_mode;

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
static struct {
	bool         shot;
	bool         set;
	int          minutes;
	int          h24;          /* -1 for the saved setting */
	int          eta;          /* minutes left for Time Left, -1 for none */
	int          icon;         /* BATT_BARS.., -1 for the saved setting */
	status_marks m;
} shot_st = { .minutes = -1, .h24 = -1, .eta = -1, .icon = -1 };

/* Where the corner (its clock's text box and its marks), the clock alone, the
 * last panel and the games title's capitals were drawn this frame, for a
 * shot to hold the corner clear of them (shot_corner_clear). Zero width: not
 * drawn. */
static SDL_Rect g_corner_box, g_clock_box, g_menu_panel, g_title_caps;
/* The last menu panel's body: below its heading, inside its pad, for what a
 * screen draws in it beside the rows (About's photo of Zyper) */
static SDL_Rect g_menu_body;
static int      g_marks_w;   /* the marks' width as status_marks_width gave it */
/* The battery's number or time as wide as it wrote, before STATUS_PCT_MAX
 * held it to its room: a shot checks it fits (shot_corner_clear) */
static int      g_pct_natural;

/* The screen was cleared: nothing recorded above is on it any more. */
static void drawn_forget(void)
{
	g_corner_box = g_clock_box = g_menu_panel = g_title_caps = (SDL_Rect){ 0 };
	g_marks_w = 0;
	g_pct_natural = 0;
}

static bool status_same(const status_marks *a, const status_marks *b)
{
	return a->wifi == b->wifi && a->bt == b->bt && a->batt == b->batt &&
	       a->charging == b->charging && a->held == b->held &&
	       a->blink_off == b->blink_off && a->pct_w == b->pct_w &&
	       a->xfer == b->xfer && a->xfer_moving == b->xfer_moving &&
	       a->xfer_pct == b->xfer_pct;
}

/* The marks as a texture: painted by status_marks_render when what they show,
 * the theme or the renderer changes - a minute's clock does not touch it, a
 * blink does, once a second - and blitted every frame at the row's right end.
 * One for the launcher, as the volume bar's is. */
static void status_blit(SDL_Renderer *r, const status_marks *m, int theme, int dx, int dy)
{
	/* 156 for all four marks (File Transfer's too) and the room of the
	 * battery's number or time, and the gap between them */
	static uint32_t      px[(STATUS_PAD * 2 + 156 + STATUS_PCT_GAP + STATUS_PCT_MAX) *
	                        (STATUS_PAD * 2 + STATUS_ROW)];
	static SDL_Texture  *tex;
	static SDL_Renderer *tex_for;
	static status_marks  painted;
	static int           painted_theme = -1, tw, th;
	int w, h, mw = status_marks_width(m);

	if (!mw) return;
	status_marks_size(m, 1.0f, &w, &h);
	if ((size_t)w * h > sizeof px / sizeof px[0]) return;
	if (tex_for != r || w != tw || h != th) {
		if (tex) SDL_DestroyTexture(tex);
		tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		tex_for = NULL;
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		tex_for = r;
		tw = w;
		th = h;
		painted_theme = -1;
	}
	if (theme != painted_theme || !status_same(m, &painted)) {
		status_marks_render(px, w, h, w, 1.0f, theme, m);
		SDL_UpdateTexture(tex, NULL, px, w * 4);
		painted = *m;
		painted_theme = theme;
	}
	SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){ STATUS_RIGHT - mw - STATUS_PAD + dx,
	                                          STATUS_TOP - STATUS_PAD + dy, w, h });
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

/* MENU > Battery Icon > Time Left, on the cable: the minutes to full, or to
 * the charge limit when one is set, from the gauge's current and charge
 * (src/charging.h: the charging screen's estimate, the same numbers). Read
 * every 2 s while the cable is in; -1 where the charger's gauge cannot say,
 * while the current settles (CH_SETTLE_MS) and when it is held at the limit
 * or full, and the corner writes the percentage. */
static int charge_minutes(bool charging)
{
	static unsigned at;
	static bool primed;
	static ch_eta eta;
	static int minutes = -1;
	unsigned now = plat_now_ms();

	if (!charging) {
		/* Out of the cable: the next one starts from nothing */
		primed = false;
		eta = (ch_eta){ 0 };
		minutes = -1;
		return -1;
	}
	if (!primed || now - at >= 2000) {
		ch_reading r;

		primed = true;
		at = now;
		minutes = -1;
		if (plat_charge_read(&r)) {
			r.limit = charge_limit_now();
			ch_eta_note(&eta, now, &r);
			minutes = r.held ? -1 : ch_eta_minutes(&eta, now, &r);
			if (minutes == 0) minutes = -1;
		}
	}
	return minutes;
}

/* The minutes Time Left shows in the corner, or -1: a shot's own (--status
 * eta=) or what the cable or the discharge says */
static int corner_minutes(const status_marks *m)
{
	if (m->batt < 0) return -1;
	if (shot_st.set) return shot_st.eta;
	return m->charging ? charge_minutes(true) : battery_minutes_left();
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

/* File Transfer behind the shelf, for the corner's mark (xs_*) */
static void xs_mark(status_marks *m);

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
	m->batt = battery_now(&pct, &chg) ? pct : -1;
	m->charging = chg;
	m->held = chg && charge_held();
	/* The time to full forgets the cable the moment it is out, whatever the
	 * setting, so a cable plugged in later starts its estimate afresh */
	if (!chg) charge_minutes(false);
	xs_mark(m);
}

/* The time a shot shows: today at --status's time of day, into *t. False for
 * a shot with no time to show - no --status, or one with no time - which
 * then shows none, since the host's would change the shot every minute. Not
 * a shot: *t as it is, and true. */
static bool shot_time(time_t *t)
{
	struct tm tm;

	if (!shot_st.shot) return true;
	if (!shot_st.set || shot_st.minutes < 0) return false;
	localtime_r(t, &tm);
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

/* The status corner, DESIGN.md 11.12, my pick in the status lab: the
 * marks at the right end of a row 24 tall from y 24, and the clock in
 * **meta** **text-soft** 16 to their left, its capitals on the row's middle.
 * In `theme`, an index - DS_THEME is a table per file, so theme()'s pointer is
 * not into this one. Returns how long until it changes by itself, in ms (the
 * blink, the next minute), and tells the shelf's loop the same.
 *
 * On the shelf, Now Playing and every screen over a paused game (the menu,
 * the save slots, the achievements; paused_backdrop): the first two in the
 * theme, the last in Salt & Pepper, since scrim.game is dark in both themes
 * (see slot_draw). My call, 2026-11-04: the whole corner on all of them,
 * where the low-battery dot was before. `clock` is the clock's color:
 * **text-soft**, but **text** over a paused game, where a white frame under
 * scrim.game leaves text-soft at 4.4:1 and text holds 6.0:1. */
/* The home and games pages' header (`compact`): the clock stands at its
 * left end, x 64, where the ZyperOS wordmark was (2026-11-05: "remove
 * zyperOS from the top left and put the time there instead"), in **action**
 * **text**; the marks keep the right end. Elsewhere it is beside the marks. */
#define HEADER_CLOCK_X 64
static bool g_clock_left;

static unsigned draw_corner_at(SDL_Renderer *r, int theme, SDL_Color clock,
                               bool moving, bool compact)
{
	status_marks m;
	struct timespec ts;
	char clk[16], pct[24];
	int right = compact ? 960 : STATUS_RIGHT, dy = compact ? 28 : 0;
	int x = right, mw;
	unsigned wait;

	/* One reading of the clock for what it says and when it next changes */
	clock_gettime(CLOCK_REALTIME, &ts);
	status_gather(&m, moving);
	wait = status_blink(&m);
	/* MENU > Battery Icon > Percentage: room left of the mark for "82%", in
	 * the clock's font and digits in cells, so 9% to 10% moves only itself.
	 * Time Left writes "3 h 20 min" there when it has one, else the same
	 * number, and asks for a frame every minute while it does, as the clock
	 * does, so the time counts down with it. */
	if (g_batt_mode != BATT_BARS && m.batt >= 0) {
		TTF_Font *f = compact ? page_font(PAGE_CHROME) : ui_font(UI_F_META);
		int left = g_batt_mode == BATT_TIME ? corner_minutes(&m) : -1;

		if (left > 0) {
			unsigned minute = 60000;

			corner_time_text(left, pct, sizeof pct);
			redraw_at(plat_now_ms() + minute);
			if (minute < wait) wait = minute;
		} else {
			snprintf(pct, sizeof pct, "%d%%", m.batt > 100 ? 100 : m.batt);
		}
		m.pct_w = g_pct_natural = ui_text_tabular_width(f, pct);
		if (m.pct_w > STATUS_PCT_MAX) m.pct_w = STATUS_PCT_MAX;
	} else {
		g_pct_natural = 0;
	}
	status_blit(r, &m, theme, right - STATUS_RIGHT, dy);
	if (m.pct_w > 0) {
		TTF_Font *f = compact ? page_font(PAGE_CHROME) : ui_font(UI_F_META);
		/* In the mark's own color: text-soft, or warning when it is low */
		SDL_Color c = status_batt_level(m.batt, m.charging)
		            ? ui_col(DS_THEME[theme < 0 || theme >= DS_THEMES ? DS_LIGHT : theme].warning, 255)
		            : clock;

		ui_text_tabular(r, f, pct, right - STATUS_BATT_W - STATUS_PCT_GAP,
		                ui_cap_y(f, STATUS_MID + dy), 1, c);
	}
	mw = status_marks_width(&m);
	if (mw) x -= mw + STATUS_CLOCK_GAP;
	g_corner_box = (SDL_Rect){ right - mw, STATUS_TOP + dy, mw, STATUS_ROW };
	g_marks_w = mw;
	g_clock_left = compact;
	if (status_clock(ts.tv_sec, clk, sizeof clk)) {
		TTF_Font *f = compact ? page_font(PAGE_ACTION) : ui_font(UI_F_META);
		unsigned left;
		int w;

		/* Digits in cells, so 9:59 to 10:00 moves nothing but the hour */
		if (compact) {
			w = ui_text_tabular(r, f, clk, HEADER_CLOCK_X, ui_cap_y(f, STATUS_MID + dy),
			                    -1, ui_col(DS_THEME[theme < 0 || theme >= DS_THEMES
			                                        ? DS_LIGHT : theme].text, 255));
			g_clock_box = (SDL_Rect){ HEADER_CLOCK_X, STATUS_TOP + dy, w, STATUS_ROW };
		} else {
			w = ui_text_tabular(r, f, clk, x, ui_cap_y(f, STATUS_MID + dy), 1, clock);
			g_clock_box = (SDL_Rect){ x - w, STATUS_TOP + dy, w, STATUS_ROW };
			g_corner_box.x = x - w;
			g_corner_box.w = right - g_corner_box.x;
		}
		/* The next minute. The shelf's one-second heartbeat redraws it
		 * anyway; this is the clock saying when it changes, as every
		 * other thing that moves by itself does. */
		left = 60000 - (unsigned)((ts.tv_sec % 60) * 1000 + ts.tv_nsec / 1000000);
		redraw_at(plat_now_ms() + left);
		if (left < wait) wait = left;
	}
	return wait;
}


static unsigned draw_corner(SDL_Renderer *r, int theme, SDL_Color clock, bool moving)
{
	return draw_corner_at(r, theme, clock, moving, false);
}

/* MP4's own shelf of albums, which keeps its corner at the right and has no
 * home header. The home with MP4 focused is still the home: its clock stays
 * at the left, where it was a step before. */
static bool muse_shelf_shown(const app *a)
{
	return a->sys.count > 0 && a->screen == SCREEN_GAMES &&
	       is_muse(&a->sys.systems[a->sys_cursor]);
}

static void draw_status(app *a)
{
	/* Moving is any frame asked for within 50 ms, this frame or the last:
	 * a card's step, a marquee's travel, the tint easing to a new system, a
	 * menu over the shelf. A fork then would stop a frame of it. */
	bool moving = home_moving(&a->home, plat_now_ms()) || frames_wanted() ||
	              (a->sys.count > 0 && a->view[a->sys_cursor].cf.active);

	draw_corner_at(a->r, theme_index(), UI_TEXT_SOFT, moving, !muse_shelf_shown(a));
}

/* ---------- when the next frame is needed -------------------------------- */

/* The redraw clock: what each is, and redraw_at, redraw_now and
 * frames_wanted, are in src/app.h */
Uint32 g_redraw_at = 0;
Uint32 g_moved_at;
/* Spotlight's focused sprite's next atlas frame, when it is the only thing on
 * the home that will change by itself (focus_frame); else REDRAW_NEVER */
static Uint32 g_focus_at = REDRAW_NEVER;

/* The clock the launcher's motion is measured on: the shelf's moves (through
 * cf_clock), the menus' focus plate and the give of their list, and the
 * background tint's follow. plat_now_ms, except in a shot that asks for a
 * time (--shot-at), where it stands still wherever the shot puts it, so the
 * frame drawn is the frame that far into a move, and the same one every time
 * it is asked for. */
static bool     g_clock_held;
static unsigned g_clock_at;

static unsigned anim_now(void) { return g_clock_held ? g_clock_at : plat_now_ms(); }
static Uint32 anim_ticks(void) { return anim_now(); }

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
static void theme_changed(app *a)
{
	g_theme_late = false;              /* told, whoever turned it */
	drop_card(&g_np.tex);
	g_np.done = g_np.tex != NULL;      /* np_cover's own rule: made, or ask */
	redraw_now();
}

/* Auto's clock, asked by a screen's own loop once a pass: true when the theme
 * changed, and then theme_changed has been told. Every loop that stays on
 * screen asks it - the menus through menu_run_body, the shelf's loop for
 * itself, and every screen that runs its own - so 7:00 or 19:00 arriving
 * while one is up is drawn from its next frame, and the file Diatom and
 * portkeys read follows. A loop that draws every pass needs nothing more; one
 * that draws only when something changed takes a true as a change. A screen
 * drawn once and held, a one-line message, asks before it is drawn.
 *
 * The one way in for the clock: a turn on_game_tick saw during a game
 * (g_theme_late) is a change here like the clock's own, so the in-game menu
 * and Muse opened from it draw their cards in the theme that is on, and the
 * way back from the game asks this before its first frame. */
/* System Update's word that this version started (docs/updates.md): the
 * switch at boot (the image's update-apply.sh) puts a new ZyperOS folder on
 * trial, and a third start without this word goes back to the version
 * before by itself. The word is ten seconds of a launcher whose screens are
 * running, asked from every screen's loop through tick_theme below. */
static void tick_update_daily(void);   /* with System Update's screen */
static void up_spawn(const char *cmd);

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

/* The boot screen in the chosen theme (src/boottheme.h, DESIGN.md 11.21):
 * theme.txt on the boot partition, for the next start. Ten seconds into a
 * start, as tick_update_trial, and two seconds after the last change of
 * Appearance or the clock's zone, so a held Left or Right writes it once.
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

static void boot_theme_changed(void)
{
	g_boot_theme_at = plat_now_ms() ? plat_now_ms() : 1;
}

static void tick_boot_theme(void)
{
	static bool read, settled;
	static char said[BOOTTHEME_MAX];
	char line[BOOTTHEME_MAX], file[BOOTTHEME_MAX], zone[64];
	unsigned now = plat_now_ms();
	int r;

	if (now - t_boot0 < 10000) return;
	if (g_boot_theme_at && now - g_boot_theme_at < 2000) return;
	/* Asked once, and again only after a change: what it compares is the
	 * Appearance setting and the clock's zone, and everything that writes
	 * either calls boot_theme_changed. It was asked on every pass of every
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
	boottheme_words(appearance_id(theme_appearance()), zone, lang_code(i18n_lang()),
	                line, sizeof line, file, sizeof file);
	g_boot_theme_at = 0;
	lang_file_keep();
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

/* File Transfer behind the shelf, driven from every screen's loop through
 * tick_theme (see xs_tick) */
static void xs_tick_behind(app *a);

bool tick_theme(app *a)
{
	xs_tick_behind(a);
	tick_update_trial();
	tick_boot_theme();
	tick_update_daily();
	if (!theme_tick() && !g_theme_late) return false;
	theme_changed(a);
	return true;
}

static void draw_background(app *a)
{
	SDL_Renderer *r = a->r;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	/* A wash of the focused system's color along the bottom edge, so the
	 * whole screen belongs to the machine you are looking at. Faint enough to
	 * read as light rather than as a panel. DESIGN.md 7's wash: added light
	 * in Salt & Pepper, as it always was, and in Beard, where light added to
	 * a light ground is gone, the color laid on with an ordinary blend. */
	if (a->screen == SCREEN_GAMES && is_muse(&a->sys.systems[a->sys_cursor])) {
		SDL_Rect band = { 0, ZYPEROS_SCREEN_H - 240, ZYPEROS_SCREEN_W, 480 };
		ui_lift(r, &band, a->tint, UI_LIFT_WASH);
	}
}


/* On a games shelf, every card in the shelf's color: a game is drawn in its
 * system's accent, and on Favorites and Muse that is ZyperOS's lime. Under
 * the art's pixels and not its canvas: a cover padded with clear rows below
 * it (content_bottom) would otherwise stand on a grey band of nothing. */
static void game_lift(void *ctx, int i, const SDL_Rect *art,
                      const SDL_Rect *content, float focus)
{
	app *a = ctx;

	(void)i;
	(void)art;
	ui_lift_k(a->r, content, a->sys.systems[a->sys_cursor].accent,
	          UI_LIFT_FOCUS, focus);
}

/* Only the unobscured home schedules its animation. Menu backdrops are still. */
static bool g_home_live;
static int g_home_phase = -1;

static void draw_systems(app *a)
{

	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	char line[128];


	if (is_muse(s)) {
		/* MP4 has albums, books and videos rather than games, each counted,
		 * and a kind the card has none of left out. */
		int albums = ml_count(&g_muse, false), books = ml_count(&g_muse, true);
		int videos = ml_count_kind(&g_muse, ML_VIDEOS);
		char al[32] = "", bk[32] = "", vd[32] = "";
		const char *sep = T(S_SHELF_LIST_SEP);

		if (albums) snprintf(al, sizeof al, TN(S_SHELF_ALBUMS, albums), albums);
		if (books)  snprintf(bk, sizeof bk, TN(S_SHELF_BOOKS, books), books);
		if (videos) snprintf(vd, sizeof vd, TN(S_SHELF_VIDEOS, videos), videos);
		snprintf(line, sizeof line, "%s%s%s%s%s", al, al[0] && bk[0] ? sep : "", bk,
		         (al[0] || bk[0]) && vd[0] ? sep : "", vd);
	} else {
		int gc = a->view[a->sys_cursor].list.count;

		/* A shelf of one read "1 games". Favorites hits it first because it
		 * starts empty and grows one at a time, but any system with a single
		 * ROM has always said it. */
		if (gc > 0)
			snprintf(line, sizeof line, TN(S_SHELF_GAMES, gc), gc);
		else snprintf(line, sizeof line, "%s", T(S_SHELF_NO_GAMES));
	}
	home_item items[CFG_MAX_SYSTEMS];
	home_items(a, items);
	uint32_t now = g_home_phase >= 0 ? a->home.animation + (uint32_t)g_home_phase : anim_now();
	uint32_t wait = home_draw(&a->home, a->r, a->home_layout, items,
	                          a->sys.count, a->sys_cursor, line,
	                          is_muse(s) ? T(S_SHELF_OPEN_MP4) : T(S_SHELF_OPEN_GAMES), now,
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
static int shot_phase = -1;        /* --phase, for the shot harness only */
/* --focus-from MS: the whole frame at that phase, then a focus frame at
 * --phase's (focus_draw), for tools/home-shot-check.py to hold against the
 * whole frame at --phase */
static int shot_focus_from = -1;

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
/* Two note slots because a panel can carry two footer notes and both may be
 * long enough to move. Sharing one would be the bug this enum exists to
 * prevent, a step smaller: each would see the other's key every frame and
 * neither would ever get past the opening hold. */
#define MQ_NOTES 2
/* MQ_FIXED is for rows that move because nothing can ever select them, as
 * distinct from the ones that move because something just did.
 *
 * The other slots are keyed on the cursor, which is right when the row that
 * scrolls is the row you landed on: a new selection is a new subject and its
 * text should start from the beginning. It is wrong for a fact. The game
 * screen's Genre row is unselectable and scrolls whenever it is long, and
 * stepping between Synopsis and Cheevos was restarting it - a cursor move that
 * has nothing to do with the row it interrupted. Reported 2026-09-17, the day
 * Genre became the first long permanently-unselectable row on a screen that
 * also has a cursor.
 *
 * Keyed on the SCREEN instead - its heading and how many rows it has - which
 * is the same value for every such row in a frame, so they still share one
 * clock and still move together rather than resetting each other.
 *
 * It replaced a slot of its own for VALUES, which was the wrong axis to split
 * on: a label and a value never move in the same row - the wider of the two
 * takes the room and the other is laid out around it - so what needed separate
 * clocks was never the two columns. It was the selected row against the rest,
 * and only one row is ever selected. */
enum { MQ_SHELF, MQ_MENU, MQ_FIXED, MQ_NOTE0, MQ_NOTE1, MQ_VSCROLL,
       MQ_HEAD, MQ_NP, MQ_SLOTS };

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
static void mq_reset(int who)
{
	if (who >= 0 && who < MQ_SLOTS) mq_primed[who] = false;
}

static unsigned mq_phase(int who, int key_a, int key_b)
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
static unsigned mq_row_phase(bool selected, const char *heading, int n, int sel)
{
	if (selected) return mq_phase(MQ_MENU, sel, 0);
	return mq_phase(MQ_FIXED, heading ? (int)strlen(heading) : 0, n);
}

static unsigned title_phase(app *a)
{
	return mq_phase(MQ_SHELF, a->sys_cursor, a->view[a->sys_cursor].cursor);
}

/* The top of the games title's line, and of the heart beside it. 56, under
 * the status corner (DESIGN.md 11.12), which stands from y 24 to 48 at the
 * right end: a long title slides under it, not through it, its capitals 9
 * units below the row (measured on a host shot, 57) and its ascenders 5. It
 * was 40 before the corner, and the box art below was placed against that
 * with room to spare; the descenders are still 17 over the focused box. */
#define TITLE_TOP 56


static void draw_game_text(app *a, sysview *v, const system_cfg *s, int idx)
{
	const system_cfg *gs = &a->sys.systems[
		shelf_owner(a, a->sys_cursor, idx)];
	char count[64];

	if (v->list.count > 0) {
		game_entry *g = &v->list.items[idx];
		/* A title that fits is centered, as before. One that does not slides,
		 * because truncating it destroys the rest of the name permanently and
		 * sliding it only delays it - see ui_text_marquee.
		 *
		 * THE HEART HAS A FIXED SEAT, far left, whether or not this game
		 * has one. It used to be placed off the title's own width, which put
		 * it somewhere new for every game and - once titles could slide -
		 * would have moved it at the moment a title started sliding. A mark
		 * that jumps when the thing beside it grows is not a mark, it is more
		 * motion.
		 *
		 * Its width is reserved on BOTH sides, so the box stays centered on the
		 * screen and a short title is centered exactly where it always was.
		 * The cost is a narrower box, so a few more titles slide; sliding is
		 * the thing that made a narrow box acceptable. */
		TTF_Font *ft2 = ui_font(UI_F_TITLE);
		int line = ui_font_line(UI_F_TITLE);
		int margin = line / 2;
		int hrad = line * 2 / 5;
		int lead = margin + hrad * 2 + hrad;      /* edge, heart, its gap */
		int boxx = lead;
		int boxw = ZYPEROS_SCREEN_W - lead * 2;
		int tw = ui_text_width(ft2, g->title);
		int tx = ZYPEROS_SCREEN_W / 2;
		bool slides = tw > boxw;
		/* Asked for EVERY frame, not only when something slides. It is a
		 * clock that has to watch the cursor to know when to restart, and it
		 * cannot watch it on frames nobody calls it. Reading it only inside
		 * the sliding branch meant a short title in between - which needs no
		 * marquee and so never called this - left the last long one still
		 * recorded as current; coming back to it was not a change, and it
		 * carried on mid-scroll instead of starting over. */
		unsigned phase = title_phase(a);
		/* The one mark left on a card.
		 *
		 * There was a second, a dot on the right, for a game with an autosave
		 * - A continues it rather than starting it. Removed 2026-08-30: it
		 * only ever drew beside the CENTERED title, so it was never the
		 * scan-the-shelf signal it looked like, and it said one bit with no
		 * legend about the one game you could already ask about directly. X
		 * says "resume + 3" now, which is the same fact with a number on it. */
		if (fav_is(gs->tag, g->file)) {
			/* Centered on the title's capitals, where ui_line_y puts them in
			 * its line, not on the line's middle: the line keeps a
			 * descender's depth under them that most titles never use, so a
			 * mark at its middle sits visibly below the letters. */
			int dy = TITLE_TOP + line * 2 / 5;

			/* In **favorite**, DESIGN.md 11.1, and not the game's system's
			 * color: pink is love (DESIGN.md 1), and a heart in Game Boy
			 * Color's yellow said "this system" where it means "you kept
			 * this". */
			draw_heart(a->r, (float)(margin + hrad), (float)dy, (float)hrad,
			           ui_col(theme()->favorite, 255));
		}
		/* Its capitals, for a shot's check that they stay under the corner */
		g_title_caps = (SDL_Rect){ slides ? boxx : tx - tw / 2,
		                           TITLE_TOP + line * 2 / 5 - ui_cap_height(ft2) / 2,
		                           slides ? boxw : tw, ui_cap_height(ft2) };
		if (slides) {
			ui_text_marquee(a->r, ft2, g->title, boxx,
			                ui_line_y(UI_F_TITLE, TITLE_TOP), boxw, phase, UI_TEXT);
			/* Not "a title is sliding, so keep drawing". It ping-pongs for
			 * as long as the game is focused, with 1.4s of stillness at the
			 * start and 0.9s at the far end, so ask for the moment it next
			 * moves and sleep through the holds. The phase clock is
			 * plat_now_ms, the same one the main loop compares against.
			 *
			 * Only the holds can be skipped; the travel between them is
			 * motion and has to be drawn. Measured on the Brick 2026-09-13,
			 * idle for 30s on Advanced Busterhawk Gleylancer: 17.4% of a core,
			 * against 1.7% on a title that fits and 26.6% before any of this.
			 *
			 * DECIDED, left that way. Scrolling once and resting would bring a
			 * long title down near 1.7%, and it is not worth doing, because
			 * this is not idle. A marquee only runs while its title is focused
			 * and on screen, so someone looking at one is reading it - the cost
			 * is the feature doing its job while it is being used. And a device
			 * set down on a long title with nobody looking is ended by Auto Off,
			 * so there is no long unattended stretch of it to save. */
			redraw_at(plat_now_ms() + ui_pingpong_wait(tw - boxw, phase));
		} else {
			ui_text(a->r, ft2, g->title, tx, ui_line_y(UI_F_TITLE, TITLE_TOP), 0,
			        UI_TEXT);
		}
		/* An album is two names, and on Muse's shelf the artist is the one
		 * the letter jump goes by - so it is said, a step quieter, under the
		 * album's. Fitted rather than slid: the album's title already slides
		 * on this clock, and two lines moving at once is a lot of motion. */
		if (is_muse(s)) {
			char afit[192];
			const char *under = g->name;
			int al = v->album && idx >= 0 && idx < v->list.count ? v->album[idx] : -1;

			/* A book says it is finished there, and says nothing rather than
			 * its own name again when it is a folder with no author above it;
			 * nor does a folder of videos with no series above it. */
			if (al >= 0 && g_muse.albums[al].book) {
				if (g_book_done && g_book_done[al]) under = T(S_BOOKS_FINISHED);
				else if (!strcmp(g->name, g->title)) under = "";
			} else if (al >= 0 && g_muse.albums[al].kind == ML_VIDEOS &&
			           !strcmp(g->name, g->title)) {
				under = "";
			}
			ui_fit_text(ui_font(UI_F_MENU), under, afit, sizeof afit, boxw);
			ui_text(a->r, ui_font(UI_F_MENU), afit, tx,
			        ui_line_y(UI_F_MENU, TITLE_TOP + line + 4), 0, UI_TEXT_DIM);
		}
		/* The position sits below the horizontal row, above its rail. */
		snprintf(count, sizeof count, "%d / %d", idx + 1, v->list.count);
		ui_text(a->r, ui_font(UI_F_META), count,
		        ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_META, 690), 0, UI_TEXT_DIM);
	} else {
		char nfit[192];

		TTF_Font *ft = ui_font(UI_F_TITLE);
		int nw;

		ui_fit_text(ft, s->name, nfit, sizeof nfit, ZYPEROS_SCREEN_W - 48);
		nw = ui_text(a->r, ft, nfit, ZYPEROS_SCREEN_W / 2,
		             ui_line_y(UI_F_TITLE, TITLE_TOP), 0, UI_TEXT);
		g_title_caps = (SDL_Rect){ (ZYPEROS_SCREEN_W - nw) / 2,
		                           TITLE_TOP + ui_font_line(UI_F_TITLE) * 2 / 5
		                           - ui_cap_height(ft) / 2, nw, ui_cap_height(ft) };
	}
}

static void draw_albums(app *a)
{

	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	bool albums = is_muse(s);
	const cf_layout *row = albums ? &CF_LAYOUT_ALBUMS : &CF_LAYOUT_GAMES;

	if (cf_draw(&v->cf, a->r, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, v->list.count,
	            game_get_tex, game_lift, a,
	            row))
		redraw_now();
	/* Warm where the move is about to cut to, one card per frame, underneath
	 * the departure that is still being drawn. Eviction waits: the cursor is
	 * already at the destination and evict_far measures from the cursor, so
	 * running it now would free the very cards the departure is drawing. */
	{
		int land = cf_landing(&v->cf, v->list.count);
		if (land >= 0) prime_toward(a, a->sys_cursor, land);
	}
	if (!cf_cutting(&v->cf)) evict_far(v, TEX_KEEP_NEAR);

	draw_game_text(a, v, s, v->cursor);
	ui_rail(a->r, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, v->cf.pos, v->list.count,
		 s->accent, NULL, NULL);
}

/* Facts are cached for the selected ROM and refreshed after leaving the shelf.
 * File reads happen after selection motion settles, never for every frame.
 * The existing achievement reader remains on the main thread. Muse keeps its
 * separate player renderer and never enters this path. */
static struct {
	int sys, cursor;
	unsigned seen;
	bool valid, resume, achievements;
	int earned, total;
	char played[32], file[LIB_PATH];
} g_game_page;
static int g_games_phase = -1;

static void game_page_facts(app *a, bool settled)
{
	sysview *v = &a->view[a->sys_cursor];
	if (v->list.count <= 0) { g_game_page.valid = false; return; }
	game_entry *g = &v->list.items[v->cursor];
	unsigned now = plat_now_ms();
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
	stats_summarize(STATS_ALL, false, (long)time(NULL));
	if (stats_lookup(system->tag, g->file, &seconds, NULL))
		stats_format(seconds, g_game_page.played, sizeof g_game_page.played);
	state_path(a, owner, g, path, sizeof path);
	g_game_page.resume = !is_port(system) && file_nonempty(path) && access(path, R_OK) == 0;
	chv_path(P_ROMS, system->folder, g->name, path, sizeof path);
	g_game_page.achievements = chv_load(path) && chv_count() > 0;
	g_game_page.earned = chv_earned();
	g_game_page.total = chv_count();
	g_game_page.valid = true;
}
/* ---- the focused game's video snap (src/snapshelf.h) --------------------
 *
 * Stepped from draw_games, from every loop that draws the shelf: in front
 * only when it is the shelf's own loop drawing (g_home_live), the row is
 * still and the screen lit, so a menu opening over it fades the cover back
 * and stops the daemon, the same frame. The places that leave the shelf
 * without drawing it again - a game, Muse, the dark screen on the cable, the
 * power button, the systems row - call snap_off. */
static snapshelf g_snap_shelf;
static float     g_shot_snap_k = -1;      /* --snap K, for a shot */
static float     g_shot_snap_wait = -1;   /* --snap-wait F, for a shot */
static char      g_snap_asked[1024];
static bool      g_snap_held;             /* g_snap_asked is only its first picture */
static int       g_snap_vol;              /* and the sound it was asked with */
static struct { int sys, cursor; char file[LIB_PATH], path[LIB_PATH * 2]; } g_snap_focus;

/* Its sound as set, and none while Muse plays somebody's music. Read when a
 * snap is asked for: on every frame it was two settings reads from the
 * database (snaps_sound) for a value only a new ask uses. */
static int snap_volume(void)
{
	int s;

	if (musec_heard()) return 0;
	s = snaps_sound();
	return s == SNAP_FULL ? 100 : s == SNAP_QUIET ? 25 : 0;
}

/* `hold`: its first picture only, while the bar fills (snapshelf.h). Let go,
 * the same snap keeps the picture it has: snapv_begin is for a new file
 * only, since it marks every picture on screen as the old one's. */
static void snap_ask(const char *req, bool hold)
{
	bool fresh = strcmp(req, g_snap_asked) != 0;
	int vol;

	hold = hold && *req;
	if (!fresh && hold == g_snap_held) return;
	/* Let go with the sound it was held with: a new volume would be a new
	 * request to the daemon, which opens the file again */
	vol = fresh ? snap_volume() : g_snap_vol;
	g_snap_vol = vol;
	snprintf(g_snap_asked, sizeof g_snap_asked, "%s", req);
	g_snap_held = hold;
	if (fresh) {
		if (*req) snapv_begin();
		else snapv_release();
	}
	musec_snap(*req ? req : NULL, vol, hold);
	/* The menu music makes way only when a snap's sound starts */
	bgm_hold(BGM_HOLD_SNAP, *req && !hold && vol > 0);
}

/* For the shelf's loop: a snap is asked for and the daemon has a newer
 * picture of it than the one on screen */
static bool snap_new_picture(void)
{
	return g_snap_asked[0] && snapv_pending();
}

void snap_off(void)
{
	memset(&g_snap_shelf, 0, sizeof g_snap_shelf);
	g_snap_focus.sys = -1;
	snap_ask("", false);
}

static void shelf_snap(app *a, sysview *v, const game_entry *g, int owner, bool settled,
                       games_info *info)
{
	unsigned now = plat_now_ms();
	const char *req;
	float k = 0;
	int w = 0, h = 0;
	bool front = g_home_live && settled && g, hold = false;

	/* Which file, worked out when the focus moves rather than every frame */
	if (!g) {
		g_snap_focus.sys = -1;
		g_snap_focus.path[0] = '\0';
	} else if (g_snap_focus.sys != a->sys_cursor || g_snap_focus.cursor != v->cursor ||
	           strcmp(g_snap_focus.file, g->file)) {
		const char *folder = a->sys.systems[owner].folder;
		struct stat st;

		g_snap_focus.sys = a->sys_cursor;
		g_snap_focus.cursor = v->cursor;
		snprintf(g_snap_focus.file, sizeof g_snap_focus.file, "%s", g->file);
		g_snap_focus.path[0] = '\0';
		if (snaps_play() && folder[0] && snaps_console_on(folder) &&
		    snaps_path(P_ROMS, folder, g->name, g_snap_focus.path, sizeof g_snap_focus.path) &&
		    (stat(g_snap_focus.path, &st) != 0 || st.st_size <= 0))
			g_snap_focus.path[0] = '\0';
	}
	req = snapshelf_step(&g_snap_shelf, g ? g_snap_focus.path : "", front,
	                     snapv_texture(NULL, NULL) != NULL, now, &hold, &k);
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
	info->snap = k > 0 ? snapv_texture(&w, &h) : NULL;
	/* A host shot of the turn (--snap K): the test picture, K of the way */
	if (g_shot_snap_k >= 0 && g) {
		if (!mp4v_texture()) mp4v_test_picture(a->r, 320, 240);
		info->snap = mp4v_texture();
		if (info->snap) SDL_SetTextureBlendMode(info->snap, SDL_BLENDMODE_BLEND);
		w = 320; h = 240;
		k = g_shot_snap_k;
	}
	/* A host shot of the wait (--snap-wait F): the bar F of the way */
	if (g_shot_snap_wait >= 0 && g) info->snap_wait = g_shot_snap_wait;
	info->snap_w = w;
	info->snap_h = h;
	info->snap_k = info->snap ? k : 0;
}

/* The snaps a folder holds, by stem and sorted, for the mark on its covers
 * (DESIGN.md 11.19): one listing of its .media, taken when its shelf is first
 * drawn and again only when that folder has changed - looked at no more than
 * every two seconds - so a frame asks no file and no setting per card. The
 * Favorites shelf draws from many folders, so a few are kept. */
#define SNAP_NAMES_KEEP 8
typedef struct {
	char folder[CFG_STR];
	time_t mtime;
	unsigned looked;
	bool on;
	char **stems;
	int n;
} snap_names;
static snap_names g_snap_names[SNAP_NAMES_KEEP];
static int g_snap_names_next;

static int snap_name_cmp(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

static const snap_names *snap_names_for(const char *folder)
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
	if (s && now - s->looked < 2000) return s;
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
	if (s->mtime == st.st_mtime) return s;
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

	if (i < 0 || i >= v->list.count) return false;
	owner = shelf_owner(a, a->sys_cursor, i);
	folder = a->sys.systems[owner].folder;
	if (!folder[0] || is_muse(&a->sys.systems[owner])) return false;
	s = snap_names_for(folder);
	name = v->list.items[i].name;
	return s->on && s->n &&
	       bsearch(&name, s->stems, (size_t)s->n, sizeof *s->stems, snap_name_cmp);
}

static void draw_games(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	if (is_muse(s)) { snap_off(); draw_albums(a); return; }
	unsigned now = g_games_phase >= 0 ? v->row.moved + g_games_phase : plat_now_ms();
	games_select(&v->row, v->cursor, v->list.count, 0, now);
	game_page_facts(a, !games_moving(&v->row, now));
	const game_entry *g = v->list.count ? &v->list.items[v->cursor] : NULL;
	int owner = g ? shelf_owner(a, a->sys_cursor, v->cursor) : a->sys_cursor;
	games_info info = {
		.system = s->name, .title = g ? g->title : T(S_SHELF_NO_GAMES),
		.count = v->list.count, .cursor = v->cursor,
		.played = g_game_page.valid ? g_game_page.played : "",
		.has_achievements = g_game_page.valid && g_game_page.achievements,
		.earned = g_game_page.earned, .total = g_game_page.total,
		.resumable = g_game_page.valid && g_game_page.resume,
		.favorite = g && fav_is(a->sys.systems[owner].tag, g->file),
		.origin = g && owner != a->sys_cursor ? a->sys.systems[owner].name : NULL,
		.snap_of = game_has_snap
	};
	shelf_snap(a, v, g, owner, !games_moving(&v->row, now), &info);
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
static bool no_games(const app *a)
{
	for (int i = 0; i < a->sys.count; i++)
		if (!is_muse(&a->sys.systems[i])) return false;
	return true;
}

/* What the empty home says, read again twice a second: the port and the
 * radio change under it as the cable or the adapter go in and out.
 * g_eh_shot is --ways's stand-in for a shot: -1 none, else EH_* bits, with
 * 4 for a Brick-like device (no shared port, Wi-Fi built in). */
static eh_state g_eh;
static int g_eh_shot = -1;
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
	s.music = g_muse.ntracks > 0;
	if (g_eh_shot >= 0) {
		s.ok[EH_USB] = g_eh_shot & (1 << EH_USB);
		s.ok[EH_WIFI] = g_eh_shot & (1 << EH_WIFI);
		s.shared_port = !(g_eh_shot & 4);
		s.builtin_wifi = !s.shared_port;
		snprintf(s.cable_addr, sizeof s.cable_addr, "%s", s.shared_port ? "10.42.0.1" : "10.55.0.2");
	} else {
		/* A port the launcher asks about is the Pixel 2's, which the Wi-Fi
		 * adapter takes too (usblink.h); the Brick's links on its own */
		s.shared_port = plat_usb_peer() != USB_PEER_UNKNOWN;
		s.builtin_wifi = !s.shared_port;
		s.ok[EH_USB] = plat_cable_link(addr, sizeof addr);
		if (addr[0]) snprintf(last_addr, sizeof last_addr, "%s", addr);
		snprintf(s.cable_addr, sizeof s.cable_addr, "%s", last_addr);
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
static int games_on_card(const app *a)
{
	int i, n = 0;

	for (i = 0; i < a->sys.count; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (is_muse(sc) || (!sc->core[0] && !sc->folder[0])) continue;
		n += a->view[i].list.count;
	}
	return n;
}

/* Zyper in the cheer, posed by the rig at the cheer's clock */
typedef struct { zr_rig z; unsigned now; } cheer_ctx;
static void cheer_zyper(SDL_Renderer *r, float cx, float cy, float h, float alpha, void *ctx)
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

		if (is_muse(sc) || (!sc->core[0] && !sc->folder[0])) continue;
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
static void empty_keys(app *a)
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
static void empty_watch(app *a, bool empty)
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

void draw_shelf(app *a)
{
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
	if (no_games(a)) { snap_off(); draw_no_games(a); }
	else if (a->screen == SCREEN_SYSTEMS) { snap_off(); draw_systems(a); }
	else draw_games(a);
	draw_status(a);
	if (a->screen == SCREEN_SYSTEMS) g_title_caps = (SDL_Rect){0};
	if (!muse_shelf_shown(a))
		home_header(a->r, &g_home_song, g_corner_box.w ? g_corner_box.x : STATUS_RIGHT);
}

/* ---------- the song pop-up ------------------------------------------------ */

static void moved_sound(app *a, in_button b);   /* with the steps' sounds, below */

/* Zyper saying the menu music's new song, on the home and the games shelf
 * (DESIGN.md 11.22, src/songpop.h; 2026-11-06). The main loop's alone:
 * it is put away when another screen has had the display (SONG_GAP_MS), when
 * the music stops being heard, and it is never drawn over a menu, a game or
 * Muse. With MP4 playing behind the shelf, L2/R2 move MP4 instead, and the
 * pop-up says MP4's track (g_songpop_mp4). */
static songpop  g_songpop;
static zr_rig   g_songpop_zyper;
static unsigned g_songpop_drawn;       /* when render last drew it */
static unsigned g_song_seen;           /* bgm_song's seq last looked at */
static bool     g_song_popup = true;   /* MENU > Menu Music > Song Pop-up */
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
static unsigned song_hold(const char *head, const char *body)
{
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	int hover = fh ? ui_text_width(fh, head) - song_text_w() : 0;
	int bover = fb ? ui_text_width(fb, body) - song_text_w() : 0;
	unsigned reach = ui_pingpong_reach(hover > bover ? hover : bover);

	return reach > DS_MS_HOLD_LONG ? reach : DS_MS_HOLD_LONG;
}

static void chapter_label(int i, char *out, size_t n);   /* with Now Playing, below */

/* What MP4 is playing, in the pop-up's two lines (songpop_mp4_words): a
 * chapter as Now Playing names it */
static void mp4_song_words(char *head, size_t hn, char *body, size_t bn)
{
	const mu_now *mn = musec_now();
	char chap[128] = "";

	if (musec_is_book() && musec_chapters() > 1)
		chapter_label(musec_chapter_now() < 0 ? 0 : musec_chapter_now(), chap, sizeof chap);
	songpop_mp4_words(mn->title, mn->artist, mn->album, chap, musec_is_book(),
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
static void song_watch(void)
{
	unsigned now = plat_now_ms();
	char head[BGM_TAG_MAX], body[BGM_TAG_MAX];
	bool was = g_songpop.on, heard;
	bgm_song s;

	bgm_song_now(&s);
	if (s.seq == g_song_seen && !was) return;
	/* MP4's pop-up goes when MP4 is no longer heard: paused by a headset's
	 * button, or its queue run out */
	heard = g_songpop_mp4 ? musec_heard() : bgm_audible();
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
static void song_popup_draw(app *a, unsigned now)
{
	const ds_theme *t = theme();
	TTF_Font *fh = ui_font(UI_F_META), *fb = ui_font(UI_F_LABEL);
	const int padx = song_pad_x(), pady = song_pad_y(), zw = song_zyper_w();
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
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	zyper_shadow_draw(a->r, zcx, zcy, SONG_ZYPER_H, p.tilt, k);
	draw_zyper_pose(a->r, zcx, zcy, SONG_ZYPER_H, &p, zyper_brow_lift(p.tilt), 0.0f,
	                (Uint8)(255 * k + 0.5f));
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
#define HEATPOP_LINE1   T(S_HEAT_ZYPER)
#define HEATPOP_LINE2   T(S_HEAT_SLOWING)

static struct {
	bool     on;
	unsigned at;       /* when he started up */
	unsigned drawn;    /* render's last frame of it */
	zr_rig   rig;
} g_heatpop;

/* Its times, from `at`: up, the bubble in, held, out */
#define HEATPOP_IN_END   (DS_MS_MOVE + DS_MS_QUICK)
#define HEATPOP_OUT_AT   (HEATPOP_IN_END + DS_MS_HOLD_HEAT)
#define HEATPOP_END      (HEATPOP_OUT_AT + (DS_MS_FADE_OUT > DS_MS_EXIT ? DS_MS_FADE_OUT : DS_MS_EXIT))

static void heatpop_show(unsigned now)
{
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

static void heatpop_draw(app *a, unsigned now)
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

		mh = heatpop_mark_h();
		mw = (mh + 1) / 2;
		w1 = ui_text_width(f1, HEATPOP_LINE1);
		w2 = ui_text_width(f2, HEATPOP_LINE2);
		w = HEATPOP_PAD_X + mw + HEATPOP_GAP + (w1 > w2 ? w1 : w2) + HEATPOP_PAD_X;
		h = HEATPOP_PAD_Y + l1 + l2 + HEATPOP_PAD_Y;
		/* Sliding up the last few units as it fades in, as the tutorial's */
		x = HEATPOP_BUBBLE_X;
		y = HEATPOP_BUBBLE_Y + (int)(6.0f * (1.0f - op) + 0.5f);
		ui_round_rect(a->r, &(SDL_Rect){ x, y, w, h }, DS_RADIUS_PANEL, slab);
		/* The tail, toward his mouth */
		v[0].position = (SDL_FPoint){ (float)x + 2.0f, (float)(y + h - 58) };
		v[1].position = (SDL_FPoint){ (float)x + 2.0f, (float)(y + h - 28) };
		v[2].position = (SDL_FPoint){ (float)x - 18.0f, (float)(y + h - 30) };
		for (i = 0; i < 3; i++) { v[i].color = slab; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
		SDL_RenderGeometry(a->r, NULL, v, 3, NULL, 0);
		heatpop_mark(a->r, x + HEATPOP_PAD_X, y + (h - mh) / 2, mh, ink.a);
		ui_text(a->r, f1, HEATPOP_LINE1, x + HEATPOP_PAD_X + mw + HEATPOP_GAP,
		        ui_line_y(UI_F_LABEL, y + HEATPOP_PAD_Y), -1, ink);
		ui_text(a->r, f2, HEATPOP_LINE2, x + HEATPOP_PAD_X + mw + HEATPOP_GAP,
		        ui_line_y(UI_F_META, y + HEATPOP_PAD_Y + l1), -1, ink);
	}
	g_heatpop.drawn = now;
	redraw_now();
}

/* Once a pass of the main loop, beside song_watch: the chip read, and Zyper
 * up when it has passed the trip. Put away when another screen had the
 * display meanwhile, as the song pop-up is; the charger's dark shelf skips
 * the pass, so a warning due then waits for the light, as one due over a
 * menu waits for the shelf. */
static void heat_watch(void)
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
 * chapter (musec_next, musec_prev) - with the pop-up saying what plays now.
 * R2 on the last track of a queue that does not go round moves nothing, and
 * is heard as the edge. */
static void mp4_song_step(app *a, int dir)
{
	const mu_now *mn = musec_now();
	const int index = mn->index;
	const double at = mn->at;

	if (dir > 0) musec_next(); else musec_prev();
	step_sound(a, dir > 0 ? IN_R2 : IN_L2, 0, mn->index != index || mn->at != at);
	mp4_song_said(plat_now_ms());
}

/* L2 and R2 on the home and the games shelf: the menu music's track before
 * and the next, Zyper saying it as it starts (song_watch). Heard as a step
 * that moves; a press with no music to move, its music off or quiet for a
 * snap, is denied. On one track the setting moves, and is kept. While MP4
 * is heard - the menu music quiet for it - they are MP4's (mp4_song_step);
 * paused or stopped, they are the menu music's again. */
static void song_keys(app *a)
{
	int dir = a->in.pressed[IN_R2] ? 1 : a->in.pressed[IN_L2] ? -1 : 0;
	char track[256];

	if (!dir) return;
	if (musec_heard()) { mp4_song_step(a, dir); return; }
	if (!bgm_audible()) { uisound_play(DS_SOUND_DENY); return; }
	moved_sound(a, dir > 0 ? IN_R2 : IN_L2);
	if (bgm_step(dir, track, sizeof track)) db_set_str(db_dev(), "bgm.track", track);
}

static bool panel_leave_draw(app *a, int game);   /* with the menus, below */

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
 * pop-up, the volume bar. A whole frame still comes at least once a second
 * (HEARTBEAT_MS counts whole frames only). */
static bool focus_ok(const app *a)
{
	return a->screen == SCREEN_SYSTEMS && a->sys.count > 0 && !no_games(a) &&
	       !g_songpop.on && !g_heatpop.on && plat_osd_until() == UINT32_MAX;
}

/* False, drawing nothing, when the sprite is not one a focus frame can draw */
static bool focus_draw(app *a)
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

static void focus_frame(app *a)
{
	if (focus_draw(a)) plat_present();
	else redraw_now();                 /* a whole frame, then */
}

static void render(app *a)
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
	if (a->sys.count > 0 && a->screen == SCREEN_GAMES && is_muse(&a->sys.systems[a->sys_cursor]) && a->tint != a->sys.systems[a->sys_cursor].accent)
		redraw_now();
	redraw_at(plat_osd_until());
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
static unsigned last_tint_ms;
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
static void launch_frame(app *a, SDL_Texture *card, const SDL_Rect *from,
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

static void anim_launch(app *a)
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
static void poweroff_zyper(app *a, unsigned t, float cx, float cy, float h, float from,
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

static void poweroff_frame(app *a, unsigned t)
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

static void xs_end(void);

/* The power-off with the send-off `anim` plays: anim_poweroff, or the
 * low-battery screen's from where Zyper already stands */
void power_off_with(app *a, void (*anim)(app *a))
{
	/* A File Transfer behind the shelf, and its hotspot, go with it */
	xs_end();
	snap_off();
	remember_place(a);
	book_keep(true);
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
	if (charge_instead(a)) return;
	power_off_with(a, anim_poweroff);
}

/* ---------- menus --------------------------------------------------------- */

/* Both menus in ZyperOS are the same shape - a short list on a slab, over a
 * paused game frame or over the shelf - so they are drawn by one function and
 * cannot drift apart. The slab is sized to its own widest row with the same
 * padding on every side, rather than to a number picked once and left behind
 * by the next label someone adds. */
/* menu_row and the row kinds: src/menu.h. */

/* The unit every menu measurement is in. Row height, padding and the gap
 * between the two columns are all cut from it, so the whole panel scales with
 * the type rather than with a set of numbers that have to be retuned together. */
/* Two colors, `t` of the way from a to b. */
static SDL_Color col_mix(SDL_Color a, SDL_Color b, float t)
{
	if (t <= 0.0f) return a;
	if (t >= 1.0f) return b;
	return (SDL_Color){ (Uint8)(a.r + (b.r - a.r) * t),
	                    (Uint8)(a.g + (b.g - a.g) * t),
	                    (Uint8)(a.b + (b.b - a.b) * t),
	                    (Uint8)(a.a + (b.a - a.a) * t) };
}

/* DESIGN.md 5's one number, 1.5 menu lines. The line is the menu's em, 49
 * (ui_font_line): Lexend's line skip, 62, would make it 93 where the mock
 * kit's row is 77 and the panel two rows shorter than the rows it was drawn
 * with. */
static int menu_row_h(void) { return ui_font_line(UI_F_MENU) * 3 / 2; }
/* The air between the panel edge and its content, on each side. menu_draw
 * draws it and menu_std_width subtracts it, and a panel width means nothing
 * unless both use the same number. */
static int menu_pad(void)   { return menu_row_h() * 3 / 4; }

/* The air between the panel and the screen edge, on every side. */
#define MENU_MARGIN 24
/* ...but the top, where the status corner stands (DESIGN.md 11.12) on every
 * screen a panel opens over - the shelf, Now Playing, a paused game: a panel
 * starts a grid step under the corner's row, and a list that would reach
 * higher scrolls in its window instead. Centered as before when it is short
 * enough; only a tall one moves down. */
#define MENU_TOP    96
/* The height a panel can have between the two. */
#define MENU_AVAIL_H (ZYPEROS_SCREEN_H - MENU_TOP - MENU_MARGIN)

/* How many row_h rows menu_draw can show beside `fixed` other full rows,
 * `rules` rules and `notes` notes, before it starts windowing.
 *
 * It windows from the TOP of the list, which is right for a plain menu and
 * wrong for a screen whose last rows are a footer: Play Time overflowed by
 * 42px and menu_draw answered by scrolling the footer off the bottom, so the
 * totals and the key legend were never on screen together. A screen that
 * builds furniture around a list has to know how much list is left, and this
 * is that arithmetic in one place rather than a constant tuned to one set of
 * fonts - a face with other line heights makes a hardcoded row count wrong
 * again. */
int menu_list_fit(int fixed, int rules, int notes)
{
	int pad = menu_pad(), row_h = menu_row_h();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int avail = MENU_AVAIL_H
	          - (head_h + pad / 2)          /* heading down to the first row */
	          - pad;                        /* and the panel's bottom air */

	avail -= fixed * row_h;
	avail -= rules * (pad / 2 + 2);
	avail -= notes * (ui_font_line(UI_F_MENU) + pad / 3);
	return avail < row_h ? 1 : avail / row_h;
}

/* How many BODY lines fit in a panel that is nothing but body - a synopsis
 * card. The same arithmetic menu_list_fit does, asked the other way round: that
 * one reserves notes and answers in rows, and a page with no rows on it at all
 * would get the wrong answer from it.
 *
 * Here rather than in the caller because the two must agree about the heading,
 * the padding and the note height, and a screen that measured itself would
 * drift from the panel that draws it the first time either is retuned. */
static int menu_note_h(void)
{
	return ui_font_line(UI_F_MENU) + menu_pad() / 3;
}

static int menu_notes_fit(void)
{
	int pad = menu_pad();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int note_h = menu_note_h();
	int avail = MENU_AVAIL_H - (head_h + pad / 2) - pad;

	return note_h > 0 && avail > note_h ? avail / note_h : 1;
}

/* Zyper beside the heading, in the one menu that has him: set by
 * menu_run_body around that menu's own menu_draw and NULL everywhere else,
 * so a screen it opens - Wi-Fi, About, a sign-in - draws none, and neither
 * does a message drawn from its key handler. And by snap_draw, around that
 * menu's panel drawn again as it leaves (a copy of his rig, kept with it), so
 * he goes with the panel. `g_menu_zyper_at` is the moment his pose is for. */
static const zr_rig *g_menu_zyper;
static unsigned      g_menu_zyper_at;

#define MENU_ZYPER_H 64.0f

/* DESIGN.md 10: 64 units tall at the right end of the heading band, his
 * resting extent `pad` in from the panel's right edge like the rows' text,
 * and standing as high as he can and still hop inside the panel: his top
 * ZR_REACH under the frame's inner edge, so a hop at the top of a breath
 * brings his ears to the frame and no further. That is the Zyper lab's
 * place, his middle 94 on a panel at 33, a few units under the middle of
 * the heading's capitals and over all of them; centered on them, a hop put
 * an ear over the frame. His foot is 16 units above the rule, so he never
 * reaches a row. Drawn after the heading; his shadow first, in Beard, which
 * stays on the panel while he hops. The high brow follows his tilt, as
 * tools/zyperdef.py draws it. */
static void menu_zyper_draw(app *a, const SDL_Rect *panel, int pad)
{
	const float cx = panel->x + panel->w - pad - MENU_ZYPER_H * ZYPER_W / ZYPER_H / 2.0f;
	const float cy = panel->y + UI_PANEL_BORDER + ZR_REACH + MENU_ZYPER_H / 2.0f;
	zr_pose p;

	if (!g_menu_zyper) return;
	p = zr_pose_at(g_menu_zyper, g_menu_zyper_at);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	zyper_shadow_draw(a->r, cx, cy, MENU_ZYPER_H, p.tilt, 1.0f);
	draw_zyper_pose(a->r, cx, cy, MENU_ZYPER_H, &p, zyper_brow_lift(p.tilt), 0.0f, 255);
}

/* The panel pass, below the drawer that uses it */
static bool panel_pass_begin(app *a);
static void panel_pass_end(app *a, const SDL_Rect *panel, mm_pose pose);
static SDL_FRect panel_scaled(const SDL_Rect *q, float s);

/* The roles a row of menu_draw_ex's `vcolors` (below) names. VCOL_EARNED is
 * the value in **accent-ink**, lime as a mark: an earned achievement. On the
 * focus fill it is **on-accent**, like every other value. */
#define VCOL_EARNED  1u
/* The row is a one-line message (menu_message): its label in **text-soft**,
 * as the mock draws one, whatever `live` says. `live` stays the caller's,
 * since menu_row_moves reads it to decide whether a long message scrolls. */
#define VCOL_MESSAGE 2u

/* `fixed_w` is the content width to use, or 0 to size to these rows. The shelf
 * menus pass a width measured across both of them so the panel never resizes;
 * the in-game menu has no values to cycle and sizes to itself. */
/* `accent` is whose menu it is: a system's color, which is its heading rule
 * and the light it is lifted with in Salt & Pepper, or MENU_ZYPEROS for a menu
 * that is no system's. The shelf's own menu passes MENU_ZYPEROS because that
 * menu is ZyperOS, not whichever card is under the cursor; a menu that
 * belongs to a system passes that system's color, CFG_NO_COLOR for one whose
 * line names none. It is never the focus, which is lime on every menu
 * (DESIGN.md 3.3). */
/* `vcolors` is an optional array parallel to `rows`: a nonzero entry is one
 * of the VCOL_ roles above, which says what color that row is drawn in. A
 * ROLE and never a color: a color worked out before a screen's loop is the
 * theme of the moment the screen opened, and Auto turns the theme under a
 * screen that is still open - an earned value in Salt & Pepper's lime on
 * Beard's surface was 1.3:1. menu_draw_ex looks the role up in the theme on
 * every frame. It is not a field on menu_row because few screens have any
 * use for it, and putting it there made all 69 initializers in the launcher
 * declare that they do not care. NULL, which is what menu_draw passes, is
 * the ordinary rule. */
/* `visits_all` says the cursor can rest on EVERY row, whatever their `live`
 * says - which is only true of a list that uses `live` for something other
 * than selectability. The achievement list is the one: there `live` means
 * EARNED, and the cursor walks earned and unearned alike. Everywhere else the
 * two meanings coincide and this is false. */
/* `loop_at` is where the rows start repeating: rows[loop_at] is a second copy
 * of rows[0], laid out by the caller with a rule between the copies, so a body
 * too tall for its panel can scroll THROUGH and come round rather than running
 * backwards. 0 for every screen that does not do that, which is all but one.
 *
 * The caller passes it rather than this inferring it, because only the caller
 * knows the content is a repeat - and getting it wrong does not look like a
 * bug, it looks like a scroll that jumps. */
/* ui_text_marquee for a menu, which also says when the row travels. A
 * menu's loop draws every frame and asks the redraw clock for nothing, so
 * without this the status corner would not know a row was moving and could
 * stop one of its frames with a fork (frames_wanted): the shelf's marquee
 * says the same with its own redraw_at. */
static void menu_marquee(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
                         int w, unsigned phase, SDL_Color col)
{
	int over = f && s ? ui_text_width(f, s) - w : 0;

	ui_text_marquee(r, f, s, x, y, w, phase, col);
	if (over > 0) redraw_at(plat_now_ms() + ui_pingpong_wait(over, phase));
}

/* `pose` is where the panel is in its open or its close (src/menumotion.h):
 * at rest, {1, 1}, it is drawn straight on the screen as it always was;
 * anywhere else, through the panel pass below. menu_draw_ex and
 * menu_draw_caption are how every screen asks for one; this is the drawing.
 * `heading_note` is a caption at the heading's right, or NULL. */
/* A picture at the start of each two-column row, the label after it: the
 * achievement list's badges (My pick in the achievements lab,
 * 2026-11-05). Set by that screen around its own draw and NULL otherwise;
 * NULL for a row leaves its square empty, so the labels stay in one column. */
static SDL_Texture *(*g_row_icon)(int row);

/* A body that holds still and is walked by hand (About's story): `>= 0` is the
 * pixels the caller has scrolled it by, in place of the ping-pong or the loop
 * the cursorless body runs by itself. Set around that screen's own draw and -1
 * everywhere else. Out come how far it can go (`g_menu_scroll_span`, from the
 * last draw) and the height of one line (`g_menu_scroll_line`), so the caller
 * stops at the ends and steps by a line without measuring anything. */
static int g_menu_scroll_hold = -1;
static int g_menu_scroll_span, g_menu_scroll_line;

/* A row's height in a panel: a rule's, a note's, or a row's (ROW_H) */
static int panel_row_h(menu_row r, int rule_h, int note_h, int row_h)
{
	return !r.label ? rule_h : (ROW_IS_NOTE(r) ? note_h : row_h);
}

static void menu_panel(app *a, const char *heading, const menu_row *rows,
                       int n, int sel, int fixed_w, unsigned accent,
                       const unsigned *vcolors, bool visits_all, int loop_at,
                       const char *heading_note, mm_pose pose)
{
	const ds_theme *thm = theme();
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	/* The focused row's text, DESIGN.md 11.3: **on-accent**, its label whole
	 * and its value at the 72% the mock draws a focused value at
	 * (docs/design/mock/kit.css, .mrow.sel span), which on the lime still
	 * clears 6:1. */
	const SDL_Color on = ui_col(thm->on_accent, 255);
	const SDL_Color on_quiet = ui_col(thm->on_accent, 184);
	/* The heading's rule and the list's: **edge**, or in a system's menu that
	 * system's color as a mark (ui_sys_mark), which for a system with no
	 * color is **text-dim**, as its other marks are. */
	const unsigned rule_rgb = accent == MENU_ZYPEROS ? ui_rgb(thm->edge)
	                                                 : ui_sys_mark(accent);
	const ds_rgb rule = { (Uint8)(rule_rgb >> 16), (Uint8)(rule_rgb >> 8),
	                      (Uint8)rule_rgb };
	TTF_Font *fm = ui_font(UI_F_MENU), *fh = ui_font(UI_F_LABEL);
	int row_h = menu_row_h();
	int pad = menu_pad();
	/* A rule is 2px of bar plus the gap the heading's rule gets below it, so
	 * the footer sits off the list by the same amount the first row sits off
	 * the heading. Giving it a whole row_h left it swimming.
	 *
	 * A note is one line of text and nothing else. row_h is 1.5x the line, so a
	 * note in a full row carried half a row of air underneath it and then the
	 * panel's own pad on top of that, which read as the footer floating off the
	 * bottom border. */
	int rule_h = pad / 2 + 2;
	int gap = row_h;                 /* between the label and value columns */
	int note_h = ui_font_line(UI_F_MENU) + pad / 3;
	/* A row's height. A function under the macro, so its row is read once:
	 * as a macro that read `r` up to four times, the scroll below wrote
	 * ROW_H(rows[--lo]) and stepped lo three rows a pass, to rows[-2] -
	 * which crashed the launcher whenever DOWN was held in MENU while the
	 * list still eased (2026-11-05). tools/rowmacro-check.py holds it. */
#define ROW_H(r) panel_row_h((r), rule_h, note_h, row_h)
	/* A row's text is centered on its capitals (ui_cap_y), not on its box.
	 * The box reserves a descender's depth below the baseline that labels
	 * like "Wi-Fi" and "Bluetooth" never use, so centering the box leaves
	 * the visible line riding high in its row and reads as a highlight
	 * sitting too low. That was put right by half the descent, which held
	 * for a face whose ascent was three quarters of an em; Lexend's is a
	 * whole one, and the capitals are the same middle in any face. */
	/* The heading band runs from the panel's top edge down to the rule, and
	 * the heading is centered inside it rather than hung a fixed distance from
	 * the top - otherwise retuning the heading's size moves it off center,
	 * which is exactly what happened when it grew. `content_off` is the panel
	 * top to the first row, so a panel with no heading just pads instead. */
	int notes_drawn = 0;   /* which marquee slot the next long note takes */
	int scroll = 0;        /* pixels the body is lifted by, cursorless */
	int scroll_h = 0;      /* the body viewport's height, 0 if nothing scrolls */
	int scroll_span = 0;
	int scroll_split = 0;  /* first pinned row: the rule, or n if there is none */
	int scroll_foot = 0;   /* the pinned rows' total height */
	/* THE LIST GIVES WHEN THE WINDOW SCROLLS: the rows start where they
	 * were and slide the row's height into place in **quick**, on **out**.
	 * See the give below. */
	float win_off = 0.0f;

	/* Where the highlight actually is. The text colors are weighed by how
	 * much of it covers each row rather than by `i == sel`, so a row lights
	 * as the plate arrives and dims as it leaves, and the two rows either
	 * side of a move cross over correctly without anyone tracking which row
	 * was left. */
	float plate_y = -1e9f, plate_h = 0.0f;
	int line_head   = ui_font_line(UI_F_LABEL);
	/* A heading may carry a second line, separated by a newline. Nothing else
	 * passes one; the achievements screen does, because its heading is a game
	 * title AND two counts, and on one line that was wider than the panel and
	 * got both its ends clipped. Splitting keeps both rather than choosing. */
	const char *head2 = heading ? strchr(heading, '\n') : NULL;
	char head1[192];
	int head_lines = head2 ? 2 : 1;
	int head_h      = heading ? line_head * head_lines + pad : 0;
	int content_off = heading ? head_h + pad / 2 : pad;
	bool two_col = false;
	int content_w = 0, i, k, rows_h = 0;
	SDL_Rect panel;
	int cx, content_x, content_y;
	/* The list can outgrow the screen from either end - a menu is meant to
	 * gain rows - so the panel is capped to the screen and the rows window
	 * around the selection when they do not all fit. A menu that runs off the
	 * top is worse than one that scrolls. */
	const int margin = MENU_MARGIN;
	int vis = n, first = 0;

	/* A rule's sentinel is no value: a list with a rule in it and no values,
	 * Delete Game's question over its answers, stays a centered list */
	for (i = 0; i < n; i++)
		if (rows[i].value && !ROW_IS_NOTE(rows[i]) && !ROW_IS_HR(rows[i])) two_col = true;
	for (i = 0; i < n; i++) {
		int w;

		if (!rows[i].label) continue;          /* a rule measures nothing */
		w = ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			w += gap + ui_text_width(fm, rows[i].value);
		if (rows[i].value == MENU_TICK) w += ui_cap_height(fm) * 6 / 5;
		if (w > content_w) content_w = w;
	}
	if (head2) {
		size_t n1 = (size_t)(head2 - heading);

		if (n1 >= sizeof head1) n1 = sizeof head1 - 1;
		memcpy(head1, heading, n1);
		head1[n1] = '\0';
		head2++;                                    /* past the newline */
	} else if (heading) {
		snprintf(head1, sizeof head1, "%s", heading);
	}

	if (heading) {
		int w = ui_text_width(fh, head1);
		int w2 = head2 ? ui_text_width(fh, head2) : 0;

		if (w2 > w) w = w2;
		if (w > content_w) content_w = w;
	}
	if (fixed_w > 0) content_w = fixed_w;
	if (content_w > ZYPEROS_SCREEN_W - margin * 2 - pad * 2)
		content_w = ZYPEROS_SCREEN_W - margin * 2 - pad * 2;

	{
		int sum = 0;
		for (i = 0; i < n; i++) sum += ROW_H(rows[i]);
		rows_h = sum;
	}
	if (content_off + rows_h + pad > MENU_AVAIL_H) {
		int avail = MENU_AVAIL_H - content_off - pad;

		if (sel >= 0) {
			/* THE WINDOW ONLY MOVES WHEN THE CURSOR WOULD LEAVE IT.
			 *
			 * It used to center itself on the selection, which meant almost
			 * every press scrolled the list: the highlight stayed put in the
			 * middle of the panel and the rows jumped under it. That is the
			 * wrong thing to move. The cursor is what the player is aiming,
			 * so the cursor travels and the list holds still until it runs
			 * out of window.
			 *
			 * Static, and keyed on `n` like the highlight's own state, since
			 * menu_draw keeps nothing between frames and every screen shares
			 * it. A different row count is a different menu and starts at the
			 * top rather than inheriting someone else's scroll. */
			static int win_n = -1, win_first;

			vis = avail / row_h;
			if (vis < 1) vis = 1;
			if (vis > n) vis = n;

			if (win_n != n) win_first = 0;
			win_n = n;
			win_first = menu_window_first(rows, n, sel, vis, win_first);
			first = win_first;
		} else {
			/* NO CURSOR MEANS NOTHING CAN SCROLL IT, so it scrolls itself.
			 * Dropping rows off the bottom is what a cursor makes safe: you
			 * can always walk down to them. A card has no walk, so the rows
			 * all stay and the window moves over them instead, on the same
			 * ping-pong a long title runs sideways.
			 *
			 * ONLY THE BODY MOVES. Past the rule is the footer, and on the
			 * achievement card that is the points and whether it is earned -
			 * the two facts a reader wants WHILE reading the description, not
			 * after waiting out a scroll to reach them. The rule is already
			 * where this menu divides a list from its footer, so it is the
			 * split here too and nothing new has to be declared. */
			int body = 0, j;

			/* MENU_RULE only: a MENU_HR is punctuation inside the body,
			 * and treating it as the footer split would pin half a synopsis
			 * to the bottom of the panel. */
			scroll_split = n;
			for (j = 0; j < n; j++)
				if (!rows[j].label && !ROW_IS_HR(rows[j])) {
					scroll_split = j;
					break;
				}
			for (j = scroll_split; j < n; j++) scroll_foot += ROW_H(rows[j]);
			for (j = 0; j < scroll_split; j++) body += ROW_H(rows[j]);

			scroll_h = avail - scroll_foot;
			if (scroll_h < row_h) scroll_h = row_h;
			scroll_span = body - scroll_h;
			if (scroll_span < 0) scroll_span = 0;
			g_menu_scroll_span = scroll_span;
			g_menu_scroll_line = note_h;
			/* Walked by hand: nothing travels, so nothing asks for a frame.
			 * The soft edges below say which way there is more. */
			if (g_menu_scroll_hold >= 0) {
				scroll = g_menu_scroll_hold < scroll_span ? g_menu_scroll_hold : scroll_span;
			} else {
			/* Moving while it travels, for the status corner's forks
			 * (frames_wanted): a looping card always, a ping-pong out of
			 * its holds */
			if (loop_at > 0 && loop_at < n) redraw_now();
			else redraw_at(plat_now_ms() +
			               ui_pingpong_wait(scroll_span, mq_phase(MQ_VSCROLL, n, rows_h)));
			if (loop_at > 0 && loop_at < n) {
				/* One lap is everything before the repeat, so the wrap lands
				 * on the same picture the lap started from. What keeps the
				 * bottom of the viewport covered through a whole lap is that
				 * the second copy is as tall as the first, which is only
				 * worth scrolling at all when it is taller than the panel. */
				int lap = 0;

				for (j = 0; j < loop_at; j++) lap += ROW_H(rows[j]);
				scroll = ui_scrollthrough(lap,
				                          mq_phase(MQ_VSCROLL, n, rows_h));
			} else {
				scroll = ui_pingpong(scroll_span,
				                     mq_phase(MQ_VSCROLL, n, rows_h));
			}
			}
		}
	}

	panel.w = content_w + pad * 2;
	{   /* the heights actually on screen, not vis * row_h */
		int sum = 0, j, last = first + vis - 1;

		for (j = first; j < first + vis && j < n; j++) sum += ROW_H(rows[j]);
		/* A scrolling card is as tall as its viewport plus its pinned footer.
		 * Summing the rows would size the panel to text that is deliberately
		 * not all on it. */
		panel.h = content_off + (scroll_h ? scroll_h + scroll_foot : sum) + pad;
		/* A panel that ends in a footer gets a smaller bottom pad. The full
		 * pad is there to keep list ITEMS off the border; a note is already
		 * held off the list by its rule and is meant to sit low.
		 *
		 * The gap under the footer text is pad/6 from centering plus whatever
		 * bottom pad is left. Set by eye on the device 2026-09-04, not derived:
		 * the full pad gives 1.17 and floats, 0.50 is too tight, and 0.75 is
		 * where Eric called it. 5/12 is the subtraction that leaves 0.75.
		 *
		 * A body row is not a footer. A synopsis short enough to fit without
		 * scrolling ends in prose, which is the panel's content and wants the
		 * same pad under it that a list does. */
		if (!scroll_h && last >= 0 && last < n && rows[last].label &&
		    ROW_IS_NOTE(rows[last]) && !ROW_IS_BODY(rows[last]))
			panel.h -= pad * 5 / 12;
	}
	panel.x = (ZYPEROS_SCREEN_W - panel.w) / 2;
	panel.y = (ZYPEROS_SCREEN_H - panel.h) / 2;
	if (panel.y < MENU_TOP) panel.y = MENU_TOP;
	g_menu_panel = panel;   /* for a shot's check of the corner */
	cx = panel.x + panel.w / 2;
	content_x = panel.x + pad;
	content_y = panel.y + content_off;
	g_menu_body = (SDL_Rect){ content_x, content_y, panel.w - pad * 2,
	                          panel.y + panel.h - pad - content_y };

	/* DESIGN.md 11.3: **surface** in an **edge** frame, lifted off what is
	 * behind it - a shadow on Beard, light in the system's color in Salt &
	 * Pepper, or a neutral light for a menu that is no system's. The frame is
	 * never the system's color: a menu says whose it is on its heading rule. */
	/* Moving, the lift goes straight on the screen at the panel's size and
	 * its strength, and the panel into the pass: a shadow or a glow drawn
	 * into a clear texture and copied out with its alpha would be faded
	 * twice over. */
	if (posed) {
		SDL_FRect f = panel_scaled(&panel, pose.scale);
		SDL_Rect q = { (int)(f.x + 0.5f), (int)(f.y + 0.5f),
		               (int)(f.w + 0.5f), (int)(f.h + 0.5f) };

		ui_lift_k(a->r, &q, accent == MENU_ZYPEROS ? CFG_NO_COLOR : accent,
		          UI_LIFT_PANEL, pose.alpha);
		posed = panel_pass_begin(a);
	} else {
		ui_lift(a->r, &panel, accent == MENU_ZYPEROS ? CFG_NO_COLOR : accent,
		        UI_LIFT_PANEL);
	}
	ui_panel(a->r, &panel, DS_RADIUS_PANEL);

	if (heading) {
		/* Centered in the band by its ink, on the same reasoning as the rows:
		 * the em box carries descender depth that "ZyperOS" and "NES" do not use.
		 * This read "mostly do not" while the heading was "PlayOS", whose y was
		 * the exception; the shift is by font metrics rather than by the string,
		 * so nothing here changed with the name, it just got exactly true. */
		/* Center the INK between the panel's top and the rule, computed from
		 * the font rather than nudged by a fraction of the descent.
		 *
		 * The heading is capitals and lowercase with nothing below the
		 * baseline, so what should sit in the middle of that band is cap-top
		 * to baseline - not the em box, which carries a descender's depth of
		 * empty space at the bottom. Centering the box left "Wi-Fi" visibly
		 * high on the scanning screen.
		 *
		 * cap is ui_cap_height's, the height of an 'H' above the baseline,
		 * and a line's height when there is no font to ask. */
		int asc = fh ? TTF_FontAscent(fh) : line_head;
		int cap = fh ? ui_cap_height(fh) : asc;
		int block, hy;

		/* cap-to-baseline for the first line, plus a whole line for a second */
		block = cap + (head_lines - 1) * line_head;
		/* From the panel's INNER edge, not panel.y. The border is a visible
		 * frame and the eye reads the space inside it, so centering against
		 * the outer edge is arithmetically right and looks high by exactly
		 * the border's width - measured on the Wi-Fi panel as 18px above the
		 * ink against 32 below, which is what Eric saw. */
		hy = panel.y + UI_PANEL_BORDER
		     + (head_h - UI_PANEL_BORDER - block) / 2 - (asc - cap);

		/* Same rule for the heading, and for the same reason: art_screen and
		 * notice.c each fit their own before handing it over, which worked
		 * and meant two callers had to remember. */
		{
			/* A HEADING THAT DOES NOT FIT SCROLLS, like every other line in
			 * this launcher that does not fit. It used to be cut with an
			 * ellipsis and left there, which on the achievement list meant a
			 * game's own name was the one thing on screen you could not read:
			 * "Castlevania III - Dracula's C...". The panel is a fixed width
			 * shared by every menu, and a game title is whatever it is.
			 *
			 * Left-aligned while it moves, for the reason a long note is:
			 * text wider than the box has nothing left to center.
			 *
			 * Both lines share one slot, so when a heading has two they move
			 * together rather than taking turns resetting each other's clock -
			 * the same lesson the value marquee learned on Wi-Fi File Transfer. */
			char h1[192], h2[192];
			TTF_Font *meta = ui_font(UI_F_META);
			int inset = heading_note ? ui_text_width(meta, heading_note) + 2 * DS_GRID : 0;
			int title_room = content_w - 2 * inset;
			int title_x = content_x + inset;
			int hw = ui_text_width(fh, head1);
			unsigned ph;

			if (title_room < 1) title_room = 1;
			ph = mq_phase(MQ_HEAD, (int)strlen(head1), title_room);
			/* Page information belongs to the panel. Reserve equal space
			 * around its centered title so the quiet count cannot overlap it. */
			if (heading_note)
				ui_text(a->r, meta, heading_note, content_x + content_w,
				        ui_cap_y(meta, hy + asc - cap / 2), 1, UI_TEXT_DIM);

			if (hw <= title_room) {
				ui_fit_text(fh, head1, h1, sizeof h1, title_room);
				ui_text(a->r, fh, h1, cx, hy, 0, UI_TEXT_SOFT);
			} else {
				menu_marquee(a->r, fh, head1, title_x, hy, title_room,
				                ph, UI_TEXT_SOFT);
			}
			if (head2) {
				if (ui_text_width(fh, head2) <= title_room) {
					ui_fit_text(fh, head2, h2, sizeof h2, title_room);
					ui_text(a->r, fh, h2, cx, hy + line_head, 0, UI_TEXT_SOFT);
				} else {
					menu_marquee(a->r, fh, head2, title_x,
					                hy + line_head, title_room, ph,
					                UI_TEXT_SOFT);
				}
			}
		}
		SDL_SetRenderDrawColor(a->r, rule.r, rule.g, rule.b, 255);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x, panel.y + head_h,
		                                      content_w, DS_RULE });
		menu_zyper_draw(a, &panel, pad);
	}

	/* THE HIGHLIGHT SLIDES. It used to be drawn inside the row loop at
	 * i == sel, which put it wherever the cursor was that frame and made it
	 * snap. Here it is a rect of its own, moving from where it was toward
	 * where it belongs, and drawn BEFORE the rows so it stays under all of
	 * them - inside the loop, a plate that had moved up would cover the text
	 * of the row already drawn above it.
	 *
	 * The state is static because menu_draw has none and every screen shares
	 * it. `n` is the key: a different row count is a different menu, and the
	 * plate snaps rather than travelling across from wherever the last menu
	 * left it. Two menus of the SAME length in a row will slide between
	 * themselves, which is wrong but invisible - both are lists of the same
	 * shape and the plate is already close to where it should be. */
	if (vis < n) {
		/* THE GIVE: a scroll of one row starts the rows where they were, a
		 * row back, and slides them into place in **quick** on **out**, from
		 * wherever they are when it comes (tween_kick). Two chained decays
		 * did this before, with the give cut to 0.55 of a row so a held
		 * button never left the list far behind the cursor; a move in
		 * **quick** that takes over from where the rows are is in place
		 * before the next repeat (tools/tween-check.c), so it starts from
		 * where the rows really were.
		 *
		 * And nothing clips it. A clip to a row, so that one row past each
		 * end of the window would be enough to draw, jumped the rows by
		 * whatever it clipped: a quarter of a row at a press every 30 ms. The
		 * rows past the window are drawn as far as the give reaches instead
		 * (below), and the give stays within a row and a half even then: at
		 * the d-pad's repeat it is a row and a thousandth. */
		static int   wn = -1, wfirst = -1;
		static tween give;
		unsigned nw = anim_now();

		if (wn != n) tween_snap(&give, 0.0f);
		else if (wfirst != first) {
			int a0 = first < wfirst ? first : wfirst;
			int b0 = first < wfirst ? wfirst : first;
			float d = 0.0f;
			int j2;

			/* A STEP GIVES, A JUMP DOES NOT. Wrapping from the last row to
			 * the first moves the window the whole length of the list, and a
			 * shoulder button pages it by eight; easing across either drags
			 * rows in from the far end and reads as the list skipping. Only
			 * a single row's move is softened. */
			if (b0 - a0 > 1) {
				tween_snap(&give, 0.0f);
			} else {
				for (j2 = a0; j2 < b0 && j2 < n; j2++) d += ROW_H(rows[j2]);
				tween_kick(&give, (first > wfirst) ? d : -d, DS_MS_QUICK, nw);
			}
		}
		wn = n; wfirst = first;
		win_off = tween_at(&give, nw);
		if (tween_moving(&give, nw)) redraw_now();    /* the list still easing: moving */
	}

	if (sel >= 0 && sel < n && rows[sel].label) {
		static int   pl_n = -1, pl_sel = -1;
		static tween pl_y, pl_h;
		/* A MOVE IN **QUICK** THAT TAKES OVER FROM WHERE THE PLATE IS,
		 * DESIGN.md 8.2's focus fill (src/tween.h). REPEAT_RATE_MS is 90 and
		 * a timed slide used to take 150, so holding the d-pad meant the plate
		 * never finished a move: it trailed a row behind, and on the wrap -
		 * which snaps - it jumped from halfway to the top. The last row before
		 * the wrap was being selected and never visibly highlighted, which is
		 * what it looked like from the outside. Two chained decays replaced
		 * it, at 96% of the way within one repeat.
		 *
		 * This is on its row within one repeat, 99.9% of the way
		 * (tools/tween-check.c), and a press mid-move goes on from where the
		 * plate is rather than starting over, so under repeat it keeps up and
		 * after a single press it eases.
		 *
		 * It goes where its row will REST, not where the row is drawn this
		 * frame: on a scroll the list gives under it (win_off above), and a
		 * plate chasing a row that is still sliding would be sent somewhere
		 * new every frame. At the window's edge it holds still and the rows
		 * slide into it. */
		int ty_sel = content_y - scroll, jj;
		unsigned nowp = anim_now();
		float tgt_h = (float)ROW_H(rows[sel]);
		SDL_Rect plate;

		for (jj = first; jj < sel && jj < n; jj++) ty_sel += ROW_H(rows[jj]);

		if (pl_n != n) {                       /* another menu: no journey */
			tween_snap(&pl_y, (float)ty_sel);
			tween_snap(&pl_h, tgt_h);
		} else if (pl_sel != sel) {
			/* A step is a move to the next SELECTABLE row, whatever its
			 * index. Distance cannot tell a wrap from a step in a menu whose
			 * top rows are placeholders - the cursor goes 6 to 3 there, and
			 * a step past one placeholder goes 3 to 5. What separates them is
			 * whether anything selectable lies between. */
			int lo2 = sel < pl_sel ? sel : pl_sel;
			int hi2 = sel < pl_sel ? pl_sel : sel;
			bool jump = false;
			int q;

			for (q = lo2 + 1; q < hi2; q++)
				if (rows[q].label && rows[q].live) { jump = true; break; }
			if (jump) {
				tween_snap(&pl_y, (float)ty_sel);
				tween_snap(&pl_h, tgt_h);
			}
		}
		pl_n = n; pl_sel = sel;
		tween_to(&pl_y, (float)ty_sel, DS_MS_QUICK, nowp);
		tween_to(&pl_h, tgt_h, DS_MS_QUICK, nowp);
		/* the plate still on its way: moving */
		if (tween_moving(&pl_y, nowp) || tween_moving(&pl_h, nowp))
			redraw_now();

		/* The **accent** fill, DESIGN.md 11.3: lime on every menu, whoever's
		 * it is, and never the system's color. On a dark red system a plate
		 * in the system's color read as a stain on the row rather than a
		 * highlight under it, and with the system's color on the heading rule
		 * the two competed; lime is ZyperOS's, and it means the focus
		 * wherever it is.
		 *
		 * Flat, with no radial glow under it. The glow was brightest at the
		 * row's midpoint and fell off toward both ends, which put a soft blob
		 * behind the middle of every highlighted row and read as a smudge
		 * rather than as a selection. */
		plate_y = tween_at(&pl_y, nowp);
		plate_h = tween_at(&pl_h, nowp);
		plate.x = panel.x + pad / 2;
		plate.y = (int)(plate_y + 0.5f);
		plate.w = panel.w - pad;
		plate.h = (int)(plate_h + 0.5f);
		ui_round_rect(a->r, &plate, DS_RADIUS_ROW, ui_col(thm->accent, 255));
	}

	notes_drawn = 0;
	if (scroll_h)
		SDL_RenderSetClipRect(a->r, &(SDL_Rect){ panel.x, content_y,
		                                         panel.w, scroll_h });
	/* Clipped to the panel, and ONE ROW BEYOND THE WINDOW at each end, or as
	 * many as the give reaches past it. Offsetting the rows opens a gap at
	 * one edge and pushes the far row past the other; without the extra rows
	 * the gap is empty, and without the clip the overflow draws outside the
	 * panel. Leaving either out is what made the first attempt at this look
	 * like the list was wrapping. */
	int lo = first > 0 ? first - 1 : first, hi = first + vis;

	if (vis < n) {
		SDL_Rect rc = { panel.x + UI_PANEL_BORDER, content_y,
		                panel.w - UI_PANEL_BORDER * 2,
		                panel.y + panel.h - pad - content_y };
		float cover;

		SDL_RenderSetClipRect(a->r, &rc);
		cover = lo < first ? ROW_H(rows[lo]) : 0;
		while (lo > 0 && cover < win_off) {
			lo--;
			cover += ROW_H(rows[lo]);
		}
		cover = hi < n ? ROW_H(rows[hi]) : 0;
		while (hi + 1 < n && cover < -win_off) {
			hi++;
			cover += ROW_H(rows[hi]);
		}
	}
	for (k = 0, i = lo; i < n && i <= hi; k++, i++) {
		int y, ty, j;
		float sel_w;
		SDL_Color lc, vc;

		if (scroll_h && i >= scroll_split) {
			/* Pinned. Measured from the bottom of the viewport rather than
			 * from the top of the rows, so the footer does not move when the
			 * body does. The clip comes off here and stays off. */
			if (i == scroll_split) SDL_RenderSetClipRect(a->r, NULL);
			y = content_y + scroll_h;
			for (j = scroll_split; j < i; j++) y += ROW_H(rows[j]);
		} else {
			y = content_y - scroll + (int)win_off;
			if (i >= first) for (j = first; j < i; j++) y += ROW_H(rows[j]);
			else            for (j = i; j < first; j++) y -= ROW_H(rows[j]);
		}
		/* Centered in ITS OWN row, not in row_h: a note is shorter. */
		ty = ui_cap_y(fm, y + ROW_H(rows[i]) / 2);

		/* The same bar as the heading's, in the same color and width. If
		 * one is ever retuned the other follows, which is the point. */
		if (!rows[i].label) {
			SDL_SetRenderDrawColor(a->r, rule.r, rule.g, rule.b, 255);
			SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x,
			                                      y + rule_h - DS_RULE,
			                                      content_w, DS_RULE });
			continue;
		}

		/* A placeholder row still highlights - it is a real place on the list -
		 * and stays one step quieter than a working row, which is the whole
		 * signal that it does nothing yet. One step, though, not two: most of
		 * this list is placeholders, and ranking them against an unselected
		 * working row as well left the entire menu reading as grayed out. */
		/* `w` is how selected this row is RIGHT NOW, not whether it is the
		 * one the cursor points at: 1 once the plate has arrived, 0 before it
		 * sets off, and the two rows crossfade past each other in between. */
		{
			/* How much of the plate is over this row, 0 to 1. */
			float top = y > plate_y ? (float)y : plate_y;
			float bot = (float)(y + ROW_H(rows[i])) < plate_y + plate_h
			            ? (float)(y + ROW_H(rows[i])) : plate_y + plate_h;
			float w = (bot - top) / (float)ROW_H(rows[i]);

			if (w < 0.0f) w = 0.0f;
			if (w > 1.0f) w = 1.0f;

			/* Off the fill a working row is **text-soft** and a placeholder
			 * one step quieter, and a message is **text-soft** either way;
			 * on the fill every label is **on-accent**, whole (DESIGN.md
			 * 11.3: the focused row's label and value are on-accent, and the
			 * mock draws a focused placeholder no quieter). */
			lc = col_mix(rows[i].live || (vcolors && vcolors[i] == VCOL_MESSAGE)
			             ? UI_TEXT_SOFT : UI_TEXT_DIM, on, w);
			sel_w = w;
		}
		/* A value is **text-dim** off the fill, or **accent-ink** for an
		 * earned achievement, and on the fill it is **on-accent**, whatever
		 * color it had: accent-ink on lime is a darker lime on lime, which
		 * says nothing. It used to take the panel's accent as the plate
		 * arrived, and a system's red on the lime would be no better. */
		vc = col_mix(vcolors && vcolors[i] == VCOL_EARNED
		             ? ui_col(thm->accent_ink, 255) : UI_TEXT_DIM,
		             on_quiet, sel_w);

		/* Trimmed to the panel, HERE, rather than by every caller.
		 *
		 * content_w is capped to the screen above, but nothing used to fit
		 * the text to it - the label was drawn from the left edge and the
		 * value right-aligned to the right, so a row wider than the cap drew
		 * them straight through each other and out past the border. Seen on
		 * the game info screen: "File" and "Alex Kidd in Miracle World
		 * (World) (Sega Ages).zip" overlapping in the middle of the panel.
		 *
		 * Callers were each remembering to call ui_fit_text, or forgetting.
		 * A rule about how text fits a panel belongs in the thing that draws
		 * the panel, where a screen written next year gets it for free.
		 *
		 * The VALUE yields first: labels are short and fixed ("File", "Size"),
		 * values are whatever the card happens to hold. */
		if (ROW_IS_NOTE(rows[i])) {
			char note[192];

			/* A note that fits is centered, which is what a caption wants.
			 * One that does not SCROLLS rather than being cut: a note is a
			 * sentence, and Play Time's footer lost the last of four facts
			 * to an ellipsis - "longest 56m 30s · t..." - where the reader
			 * cannot even tell which word went. Same ping-pong and the same
			 * faded edges a long title gets on the shelf.
			 *
			 * Left-aligned in that case, not centered: the text is wider
			 * than the box, so centering it has nothing left to center.
			 *
			 * A body row is left-aligned whether it fits or not. It is one
			 * line of a paragraph, and a paragraph needs one left edge. */
			if (ui_text_width(fm, rows[i].label) <= content_w) {
				bool body = ROW_IS_BODY(rows[i]);

				ui_fit_text(fm, rows[i].label, note, sizeof note, content_w);
				ui_text(a->r, fm, note, body ? content_x : cx, ty,
				        body ? -1 : 0, lc);
			} else {
				int slot = MQ_NOTE0 + (notes_drawn < MQ_NOTES - 1
				                       ? notes_drawn : MQ_NOTES - 1);

				menu_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w, mq_phase(slot, sel, i), lc);
			}
			notes_drawn++;
		} else if (two_col) {
			char lbl[192], val[192];
			/* g_row_icon's square, three quarters of the row, and the row
			 * drawn after it as if it started there; put back below */
			int icon_x0 = content_x, icon_w0 = content_w;

			if (g_row_icon) {
				int isz = row_h * 3 / 4;
				SDL_Texture *it = g_row_icon(i);

				if (it) SDL_RenderCopy(a->r, it, NULL, &(SDL_Rect){
					content_x, y + (ROW_H(rows[i]) - isz) / 2, isz, isz });
				content_x += isz + pad / 2;
				content_w -= isz + pad / 2;
			}
			int lw = ui_text_width(fm, rows[i].label);
			int vw = rows[i].value ? ui_text_width(fm, rows[i].value) : 0;
			int room;

			/* WHICHEVER IS LONGER IS THE CONTENT, and the content is what
			 * moves. The other one is a caption, and a caption keeps its
			 * natural width.
			 *
			 * On the play-time list the label is a game name and the value a
			 * duration, so the name scrolls and the number stays put; a
			 * duration pushed off the row it exists to report is no use to
			 * anyone. On the game info screen it is the other way round -
			 * "File" is the caption and the filename is the content - and
			 * the same rule sends the filename to the marquee without
			 * needing to know which screen it is on.
			 *
			 * The condition used to ask whether the label fit in what the
			 * value left, which answers a different question: with a long
			 * value that subtraction goes negative, so a two-letter label
			 * failed it and took the long-LABEL path. The filename was then
			 * drawn ellipsized across the full width with "File" printed on
			 * top of it, which is what sent me looking.
			 *
			 * Only the selected row moves, OR one the cursor can never reach
			 * - menu_run_body steps off any row whose `live` is false, so on
			 * a screen of facts, waiting for selection waits forever. Rows
			 * long enough to move are rare either way, and a page where
			 * everything is moving will not sit still to be read. */
			/* See menu_row_moves. The rule lives in menu.c because the
			 * last time it quietly meant something else, nothing could see
			 * it: the achievement list scrolled a title or cut it depending
			 * on whether the player had earned it. */
			bool moves = menu_row_moves(rows[i], i == sel, visits_all);

			if (lw + (vw ? gap + vw : 0) <= content_w) {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				if (rows[i].value)
					ui_text(a->r, fm, rows[i].value,
					        content_x + content_w, ty, 1, vc);
				/* The choice that is on: a tick, a capital's height and a
				 * fifth, where the value's right edge is */
				if (rows[i].value == MENU_TICK) {
					int ts = ui_cap_height(fm) * 6 / 5;

					ui_glyph_draw(a->r, UI_GLYPH_CHECK, content_x + content_w - ts / 2,
					              y + ROW_H(rows[i]) / 2, ts, vc);
				}
			} else if (vw > lw) {
				/* The VALUE is the long one. The label keeps its width and
				 * the value takes everything left of the right edge. */
				ui_text(a->r, fm, rows[i].label, content_x, ty, -1, lc);
				room = content_w - lw - gap;
				if (room <= 0) {
					/* No room for it at all; the label alone is the row. */
				} else if (moves) {
					/* One slot for every long value on the screen, never one
					 * per row: keyed per row, a screen of facts with two of
					 * them reset the clock twice a frame and neither ever
					 * moved. Wi-Fi File Transfer showed it 2026-09-14 - "Transferred"
					 * and "Now" both too long, and two captures a second apart
					 * identical. Sharing the clock, they all move.
					 *
					 * WHICH clock depends on why this row moves, not on what
					 * is in it. See MQ_FIXED. */
					menu_marquee(a->r, fm, rows[i].value,
					                content_x + content_w - room, ty, room,
					                mq_row_phase(i == sel, heading, n, sel), vc);
				} else {
					ui_fit_text(fm, rows[i].value, val, sizeof val, room);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
			} else {
				/* The LABEL is the long one: a game's name, an SSID. */
				room = content_w - (vw ? vw + gap : 0);
				if (rows[i].value) {
					ui_fit_text(fm, rows[i].value, val, sizeof val, content_w);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
				if (room <= 0) {
					/* Nothing left for a name. Better a cut label than none. */
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				} else if (moves) {
					menu_marquee(a->r, fm, rows[i].label, content_x, ty,
					                room,
					                mq_row_phase(i == sel, heading, n, sel), lc);
				} else {
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, room);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				}
			}
			content_x = icon_x0;
			content_w = icon_w0;
		} else {
			char lbl[192];

			/* A centered list's row, under the two-column rule: the row that
			 * moves slides when it does not fit, rather than losing its end
			 * to an ellipsis. Left-aligned while it slides, the way a long
			 * note is - wider than the box, it has nothing to be centered in.
			 *
			 * This branch only ever cut. Found on Muse's track list
			 * 2026-09-19: "Bird Dream Of The Olympus Mons" ended in "M..."
			 * - and slid the moment a track of that album was playing,
			 * because the "playing" value put the list in two columns. */
			if (ui_text_width(fm, rows[i].label) > content_w &&
			    menu_row_moves(rows[i], i == sel, visits_all)) {
				menu_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w,
				                mq_row_phase(i == sel, heading, n, sel), lc);
			} else {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, cx, ty, 0, lc);
			}
		}
	}
	if (vis < n) SDL_RenderSetClipRect(a->r, NULL);

	if (scroll_h) {
		/* Faded where the text runs out of viewport, and faded by laying the
		 * PANEL'S OWN FILL over it, which is the opposite of what the shelf
		 * does. There the background is a coverflow with a vignette, so a
		 * gradient painted on top would show as a band and the glyphs have to
		 * be faded instead. Here the surface under the text is one flat color,
		 * ui_panel's **surface**, so the cheap way is also the right one - as
		 * long as it is the surface of the theme that is on: the old panel's
		 * own color, kept here, was a dark band across a Beard panel.
		 *
		 * A side fades only while something is hidden on it, so a card sitting
		 * at the top of its travel has a hard top edge and a soft bottom one,
		 * and says by that which way it is about to move. */
		const int fade = row_h;
		int e;

		SDL_RenderSetClipRect(a->r, NULL);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		for (e = 0; e < fade; e++) {
			Uint8 al = (Uint8)(252 - 252 * e / fade);

			SDL_SetRenderDrawColor(a->r, thm->surface.r, thm->surface.g,
			                       thm->surface.b, al);
			if (scroll > 0)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
			if (scroll < scroll_span)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + scroll_h - 1 - e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
		}
		SDL_RenderSetClipRect(a->r, NULL);
	}

	/* A small triangle in **text-dim** where the list carries on (DESIGN.md
	 * 11.3). Hung just off the rows rather than centered in the padding: with
	 * a heading above, the padding is already spoken for by the separator, and
	 * the indicator belongs to the list in any case. */
	if (vis < n && !scroll_h) {
		/* It points the way the list continues, which three dots could not
		 * say. Drawn as rows because there is no filled-triangle primitive
		 * and this needs no texture. */
		const int tw = row_h / 3, th = tw / 2, off = 8;

		SDL_SetRenderDrawColor(a->r, thm->text_dim.r, thm->text_dim.g,
		                       thm->text_dim.b, 255);
		for (k = 0; k < th; k++) {
			int up_w   = tw * (k + 1) / th;      /* apex at the top */
			int down_w = tw * (th - k) / th;     /* apex at the bottom */

			if (first > 0)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - up_w / 2,
				                   content_y - off - th + k, up_w, 1 });
			if (first + vis < n)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - down_w / 2,
				                   content_y + vis * row_h + off + k, down_w, 1 });
		}
	}
	if (posed) panel_pass_end(a, &panel, pose);
}

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
static SDL_FRect panel_scaled(const SDL_Rect *q, float s)
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
static bool panel_pass_begin(app *a)
{
	const ds_rgb e = theme()->edge;

	static bool said;

	if (!g_panel_tex) {
		g_panel_tex = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_ARGB8888,
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
static void panel_pass_end(app *a, const SDL_Rect *panel, mm_pose pose)
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
enum { PANEL_MENU, PANEL_HOT, PANEL_TILES };   /* which drawer a copy is for */

static menu_snap  g_snaps[2];
static menu_snap *g_snap = &g_snaps[0], *g_snap_next = &g_snaps[1];

static void tiles_panel(app *a, const char *heading, const menu_row *tiles, int n,
                        int sel, mm_pose pose);
static void hot_panel(app *a, const char *heading, const menu_row *rows, int n,
                      mm_pose pose);

/* A panel's arguments into `s`, as the screen on top drew them now: its
 * heading's caption too (menu_draw_caption), or NULL for none */
static void snap_record(menu_snap *s, int kind, const char *heading,
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
}

/* The panel on screen again, at `pose`, Zyper going on from where he was:
 * on screen in this frame too. */
static void snap_draw(app *a, mm_pose pose)
{
	const zr_rig *z = g_menu_zyper;
	unsigned at = g_menu_zyper_at;
	const menu_snap *s = g_snap;

	if (!s->ok || !(pose.alpha > 0.0f)) return;
	g_snap->frame = g_frame_no;
	if (s->kind == PANEL_TILES) {
		tiles_panel(a, s->heading, s->rows, s->n, s->sel, pose);
		return;
	}
	if (s->kind == PANEL_HOT) {
		hot_panel(a, s->heading, s->rows, s->n, pose);
		return;
	}
	g_menu_zyper = s->zyper ? &s->zy : NULL;
	g_menu_zyper_at = anim_now() + s->zy_ahead;
	menu_panel(a, s->heading, s->rows, s->n, s->sel, s->fixed_w, s->accent,
	           s->has_vcolors ? s->vcolors : NULL, s->visits_all, s->loop_at,
	           s->note, pose);
	g_menu_zyper = z;
	g_menu_zyper_at = at;
}

/* The level of the panel on screen - the one drawn last, if nothing has
 * begun a frame since - or 0 for none. */
static int snap_shown(void)
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

/* What the next panel drawn is to be, which panel_frame says and panel_show
 * puts back at rest as it takes it: a panel drawn by anything else, a message
 * or a shot, is drawn at rest. */
#define PANEL_REST ((mm_frame){ { 1.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f, false })
static mm_frame g_pframe = { { 1.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f, false };

/* Every screen's panel, a menu's or the sequence card's. Until its own turn
 * comes, the panel on screen before it is drawn leaving in its place, and its
 * own kept aside; from then on its own is drawn, where the motion says, and
 * kept as the panel on screen. */
static void panel_show(app *a, int kind, const char *heading,
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
	if (kind == PANEL_TILES) tiles_panel(a, heading, rows, n, sel, f.panel);
	else if (kind == PANEL_HOT) hot_panel(a, heading, rows, n, f.panel);
	else menu_panel(a, heading, rows, n, sel, fixed_w, accent, vcolors,
	                visits_all, loop_at, heading_note, f.panel);
}

/* Existing panels have no heading caption and keep their exact layout. */
static void menu_draw_ex(app *a, const char *heading, const menu_row *rows,
                         int n, int sel, int fixed_w, unsigned accent,
                         const unsigned *vcolors, bool visits_all, int loop_at)
{
	panel_show(a, PANEL_MENU, heading, rows, n, sel, fixed_w, accent, vcolors,
	           visits_all, loop_at, NULL);
}

/* A panel with a caption at the heading's right, `heading_note` - About's
 * page count - and otherwise menu_draw_ex's: it opens, closes and is kept
 * for the next screen to draw leaving as every menu's panel is. */
static void menu_draw_caption(app *a, const char *heading, const menu_row *rows,
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

/* A one-line message, DESIGN.md 11.8: a panel holding one row, its text
 * **text-soft** - the message is the panel's content, and a failure is not
 * quieter than a success. The row's `live` is the caller's and is kept, for
 * whether a long message scrolls. */
static void menu_message(app *a, const char *heading, menu_row row,
                         unsigned accent)
{
	static const unsigned msg[1] = { VCOL_MESSAGE };

	menu_draw_ex(a, heading, &row, 1, -1, 0, accent, msg, false, 0);
}

/* What a message says when the network a screen needs is not there, one
 * sentence in every screen (DESIGN.md 12). A row that is dead for the same
 * reason says "needs Wi-Fi" in its value instead. */
#define MSG_NEEDS_WIFI T(S_PANEL_NEEDS_WIFI)

/* Video snaps come with a lookup when MENU > Scraper > Video Snaps says to,
 * for the consoles left on (ss_run_videos asks this per console). */
static bool snap_wanted(const char *folder)
{
	return snaps_fetch() && snaps_console_on(folder);
}

/* MENU > Scraper's value, from the last count of missing covers: taken when
 * its screen opens and after a run (scraper_count), never per frame */
static char g_scraper_val[32];

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
static wifi_state menu_wifi(char *ssid, int cap)
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

static void up_path(char *buf, size_t n, const char *name)
{
	snprintf(buf, n, "%s/update/%s", P_USERDATA, name);
}

/* The card has the program: a card from before System Update has no row */
static bool up_have(void)
{
	char p[512];
	snprintf(p, sizeof p, "%s/zyperos-update", P_ROOT);
	return access(p, X_OK) == 0;
}

/* At work now: it holds the lock file for as long as it runs */
static bool up_running(void)
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

static bool up_read(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f) { buf[0] = 0; return false; }
	got = fread(buf, 1, n - 1, f);
	fclose(f);
	buf[got] = 0;
	return true;
}

static void up_first_line(const char *path, char *buf, size_t n)
{
	up_read(path, buf, n);
	buf[strcspn(buf, "\r\n")] = 0;
}

/* A network to look with: Wi-Fi, or a test release's source set on the card
 * (docs/updates.md), which may be on the cable */
static bool up_network(void)
{
	char ss[WIFI_SSID_MAX], p[512];
	if (menu_wifi(ss, sizeof ss) == WIFI_CONNECTED) return true;
	up_path(p, sizeof p, "source");
	return access(p, R_OK) == 0;
}

static su_view g_up;
static unsigned g_up_read_at;

/* Everything the screen shows, read again at most four times a second: the
 * build asks every frame, and these files are on the card */
static const su_view *up_view(bool now)
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
	if (g_up.st.time > 0) {
		time_t tt = (time_t)g_up.st.time;
		struct tm tm;
		localtime_r(&tt, &tm);
		strftime(g_up.checked, sizeof g_up.checked, g_clock24 ? "%H:%M" : "%I:%M %p", &tm);
		if (g_up.checked[0] == '0' && !g_clock24) memmove(g_up.checked, g_up.checked + 1, strlen(g_up.checked));
	}
	return &g_up;
}

/* Start the program: check, download or rollback, on its own, as the device
 * name of the card's .userdata folder */
static void up_spawn(const char *cmd)
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

/* Once a day on a network, a look at the newest release (the update lab's
 * pick): one small request, nothing downloaded, and only MENU's row says
 * what it found. Asked from every screen's loop through tick_theme. */
static void tick_update_daily(void)
{
	static unsigned last;
	unsigned t = plat_now_ms();
	const su_view *v;

	if (last && t - last < 60000) return;
	last = t ? t : 1;
	if (t - t_boot0 < 30000 || !up_have()) return;
	v = up_view(true);
	if (v->running || !v->wifi || v->st.state == SU_UNSUPPORTED) return;
	if (v->st.time && time(NULL) - (time_t)v->st.time < 24 * 3600) return;
	up_spawn("check");
}

/* MENU's File Transfer row while it runs behind the shelf (xs_*) */
static const char *xs_row_value(void);
static bool xs_row_cable(void);

static int menu_build(app *a, screen_id screen, int sys,
                      menu_row *out, menu_bufs *b, const char **heading)
{
	char ss[WIFI_SSID_MAX];
	sys_ui u = { 0 };

	/* Asked once. The cache is why - see menu_wifi - and three rows used to
	 * ask separately, which is how it got expensive in the first place. */
	u.wifi  = menu_wifi(ss, sizeof ss);
	u.ssid  = ss;
	u.games = screen == SCREEN_GAMES;
	u.no_wifi = !plat_has_wifi();
	u.no_bt   = !plat_has_bluetooth();
	u.cable_xfer = plat_cable_link(NULL, 0);
	u.no_developer = !usblink_dev_image();
	u.developer  = usblink_developer(db_dev());
	u.hotspot = hotspot_available();     /* two stats, no fork */
	/* File Transfer behind the shelf: its row says so, under its name */
	u.xfer = xs_row_value();
	u.xfer_cable = xs_row_cable();
	u.no_lights = !plat_battery_lights();
	u.no_charge_limit = !plat_charge_limit_supported();
	u.charge_limit = g_charge_limit;

	if (u.games) {
		const system_cfg *sc = &a->sys.systems[sys];

		static char corebuf[CFG_STR];

		u.sys_name   = sc->name;
		u.sys_core   = core_label(sc->core, corebuf, sizeof corebuf);
		u.core_choice = cfg_core_count(sc) > 1;
		/* By what it HAS, not by its name or its position: a shelf with no
		 * core and no folder is the built one, and a check can hand that
		 * over without inventing a flag day. */
		u.muse       = is_muse(sc);
		u.fav        = !u.muse && sc->core[0] == '\0' && sc->folder[0] == '\0';
		u.game_count = a->view[sys].list.count;
		u.dmode      = dmode_label(a->view[sys].dmode);
		u.muse_books = u.muse && muse_books_shown();
		u.muse_videos = u.muse && muse_kind_shown() == ML_VIDEOS;
		u.muse_both  = u.muse && muse_kinds() > 1;
		u.muse_playing = u.muse && lock_allowed();
		u.sort       = u.muse ? ml_order_label_kind((ml_order)a->view[sys].sort, muse_kind_shown())
		                      : sort_label(a->view[sys].sort);
	} else {
		aout_state ao = aout_now();

		u.ra_in     = ra_signed_in();
		u.ra_name   = u.ra_in ? ra_user() : NULL;
		/* A check the page started may have ended after it closed */
		sgdb_try_poll();
		u.scraper   = g_scraper_val[0] ? g_scraper_val : NULL;
		{
			static char upv[48];
			u.update = up_have() ? su_menu_value(up_view(false), upv, sizeof upv) : NULL;
		}
		u.appearance = appearance_name(theme_appearance());
		u.home = home_name(a->home_layout);
		u.order = g_order_release ? T(S_PANEL_ORDER_RELEASE) : T(S_PANEL_ORDER_AZ);
		u.battery = g_batt_mode == BATT_TIME ? T(S_PANEL_BATTERY_TIME)
		          : g_batt_mode == BATT_PCT ? T(S_PANEL_BATTERY_PERCENTAGE) : T(S_PANEL_BATTERY_BARS);
		u.ui_sounds  = uisound_setting_name(uisound_setting());
		u.menu_music = bgm_menu_value(bgm_on(), bgm_count());
		u.auto_off  = a->auto_off;
		{
			static char lv[16];
			u.lights = u.no_lights ? NULL : side_lights_value(plat_side_lights(), lv, sizeof lv);
		}
		{
			static char clk[32];
			time_t t = time(NULL);

			/* In a shot, --status's time or none, as the corner's */
			if (shot_time(&t)) clock_label(t, g_clock24, clk, sizeof clk);
			else               clk[0] = '\0';
			u.clock = clk[0] ? clk : NULL;
		}
		u.clock_24h = g_clock24;
		u.audio_policy = ao.policy;
		u.audio_dest   = aout_actual(&ao);
		u.audio_channels = g_channels;

		/* The connected headset's name.
		 *
		 * Cached for two seconds, the same way the Wi-Fi row is and for the
		 * same reason: this is rebuilt every menu frame and the answer costs
		 * a fork. The first version read /tmp/zyperos_btsink to avoid that,
		 * which is what made this row say "not connected" while the Bluetooth
		 * screen said the opposite - launch.sh writes that file on a
		 * twenty-second poll. One source for both now. */
		{
			static char btname[BT_NAME_MAX];
			static unsigned next_bt;
			unsigned now = plat_now_ms();

			if (next_bt == 0 || now >= next_bt) {
				bt_device bd[BT_MAX];
				int nb = bt_bonded(bd, BT_MAX), i;

				next_bt = now + 2000;
				btname[0] = '\0';
				if (nb > 0 && bt_status() == BT_READY) {
					bt_mark_connected_now(bd, nb);
					for (i = 0; i < nb; i++)
						if (bd[i].connected) {
							snprintf(btname, sizeof btname, "%s", bd[i].name);
							break;
						}
				}
			}
			u.bt_name = btname[0] ? btname : NULL;
		}
	}
	return sys_menu_build(&u, out, b, heading);
}


/* ---------- MENU > Random Game -------------------------------------------- */

/* A game picked (src/randgame.h): from every system on the systems row's
 * menu, from the shelf's own games in a system's menu. False, with deny
 * heard, when there is none to pick. The menu closes on a pick and
 * pick_launch shows it and starts it. */
static bool random_pick(app *a, bool all, int *shelf, int *game)
{
	/* The game picked last time, so the next is another */
	static int last_shelf = -1, last_game = -1;
	int count[CFG_MAX_SYSTEMS], i;
	bool use[CFG_MAX_SYSTEMS];

	for (i = 0; i < a->sys.count; i++) {
		const system_cfg *s = &a->sys.systems[i];

		count[i] = a->view[i].list.count;
		/* Not Muse, whose cards are albums, nor - from every system -
		 * Favorites, whose games are on their own shelves too */
		use[i] = all ? !is_muse(s) && !a->view[i].owner
		             : i == a->sys_cursor && !is_muse(s);
	}
	if (!rg_pick(count, use, a->sys.count, rg_random(), last_shelf, last_game,
	             shelf, game)) {
		uisound_play(DS_SOUND_DENY);
		return false;
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
 * down again only OUTSIDE the menus - the shelf's loop, game_menu, Muse - and
 * never when a menu opens: a screen that opens a second one after the first
 * came back has to see it still up, or that second one would stay open. */
static bool g_menu_closing;

/* ---- the interface sounds' hooks (DESIGN.md 9, src/uisound.h) ----------
 *
 * Each hook is one call where a press is decided, so the rule stays in
 * src/uisound.c and a screen only says what happened. */

/* Whether the last step was held against an end. One for the whole launcher:
 * input is flushed whenever a screen opens, so a hold never carries from one
 * list to another. */
static uis_steps g_steps;

/* A step along a list or shelf. Wrapping is normal movement. */
void step_sound(app *a, in_button b, int from, int to)
{
	bool moved = from != to;

	uisound_cue(uisound_step(&g_steps, moved,
	                         !a->in.pressed[b]));
}

/* A step that always lands somewhere new, and never goes round: a track
 * skipped, a seek. */
static void moved_sound(app *a, in_button b)
{
	step_sound(a, b, 0, 1);
}

/* menu_step_sel, heard. */
static int menu_step_heard(app *a, const menu_row *rows, int n, int sel,
                           int dir, in_button b)
{
	int to = menu_step_sel(rows, n, sel, dir);

	step_sound(a, b, sel, to);
	return to;
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
 * frames (menu_draw_ex), the shelf and Muse's shelf after theirs
 * (panel_leave_draw), the paused game before it is handed back
 * (panel_leave_run). src/menumotion.h has the rules.
 *
 * Nothing waits on any of it. Input is read and acted on as it always was,
 * and a press puts the panel where it belongs on the frame it is acted on. */

/* A press acting on level `lv`'s screen now (mm_settle): when `lv` was
 * still waiting for the panel on screen to leave, its own, kept as it waited,
 * is the panel on screen from now. The level of the panel on screen. */
static int panel_settle(int lv)
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
static void panel_close_at(app *a, int lv, bool cut, unsigned now)
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

/* Muse's shelf, which has no panel and is drawn over everything: the panel on
 * screen leaves over it, and is given back as it opened when it closes.
 * Nothing Muse does closes the menu under it (muse_open), MENU included. */
static int panel_base(void)
{
	return mm_base(anim_now(), panel_settle(mm_depth()) > 0);
}

static void panel_base_close(int lv)
{
	mm_close(lv, anim_now(), 0, false);
}

/* MENU closed the screens over the one on top, and stops there: Muse's
 * menu, which MENU closes and nothing under it (muse_menu). The screen on top
 * comes back as after a back (mm_return). */
static void panel_return(void)
{
	mm_return(mm_depth(), anim_now(), snap_shown());
	g_snap_next->ok = false;
}

/* After a base's own frame - the shelf's, Muse's shelf's - the last panel
 * closed onto it leaving, and its scrim, while they last. A press there has
 * acted on the base already, and takes them off at once. True while they
 * are going, for the caller to draw the next frame. */
static bool panel_leave_draw(app *a, int game)
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

/* How panel_leave_run ended: the panel gone, or a press that ended it at
 * once, which is the same to the caller; the power button or Auto Off; or a
 * quit. */
typedef enum { LEAVE_GONE, LEAVE_POWER, LEAVE_QUIT } leave_end;

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
static leave_end panel_leave_run(app *a, void (*backdrop)(app *, void *, float),
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

/* A yes/no panel, drawn like wait_panel but answerable.
 *
 * Defaults to "no": the selection starts on the safe row, so a stray press of
 * the button that opened the panel cannot also confirm it. Returns false for
 * BACK, for MENU, and for a power-off, because everything except a deliberate
 * press on the destructive row means "not that".
 *
 * Power is handled here rather than left to the caller. A confirm can sit on
 * screen indefinitely, which makes it exactly the kind of place the device gets
 * put down and the power button pressed. */
/* The question is `nq` rows - one dead row, or a paragraph's lines (Delete
 * Game's, DESIGN.md 11.31) - and the two answers go after it in `rows`,
 * which holds nq + 2. `fixed_w` is the panel's content width, 0 to size it
 * to the rows. */
static bool confirm_rows(app *a, const char *heading, menu_row *rows, int nq,
                         const char *yes_label, int fixed_w, int lv)
{
	int sel = nq + 1;
	float k;

	rows[nq]     = (menu_row){ yes_label, NULL, true };
	rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };

	for (;;) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return false; }
		if (menu_leaving(a)) return false;

		/* Only the two answerable rows are reachable; the rows above are the
		 * question. */
		{
			in_button b = in_repeat(&a->in, IN_UP)   ? IN_UP
			            : in_repeat(&a->in, IN_DOWN) ? IN_DOWN : IN_NONE;
			int to = b == IN_UP ? nq : nq + 1;

			/* Up is the answer row, Down is Cancel, and neither goes round */
			if (b != IN_NONE) {
				step_sound(a, b, sel, to);
				sel = to;
			}
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			return sel == nq;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw(a, heading, rows, nq + 2, sel, fixed_w, MENU_ZYPEROS);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
}

static bool confirm_body(app *a, const char *heading, const char *msg,
                         const char *yes_label, int lv)
{
	menu_row rows[3];

	rows[0] = (menu_row){ msg, NULL, false };
	return confirm_rows(a, heading, rows, 1, yes_label, 0, lv);
}

static bool confirm_panel(app *a, const char *heading, const char *msg,
                          const char *yes_label)
{
	int lv = panel_open();
	bool yes = confirm_body(a, heading, msg, yes_label, lv);

	panel_close(a, lv, !a->running);
	return yes;
}

/* ---- the menu runner -------------------------------------------------- */

/* Wi-Fi is the largest menu: the switch, WIFI_MAX_NETS networks, a rule and a
 * note. Distinct from MENU_MAX_ROWS, which is the system menu's own count and
 * is much smaller. */
/* And the Video Snaps page, a row a console (sys_menu_snaps_rows). */
#define MENU_RUN_ROWS (WIFI_MAX_NETS + 8 > SN_ROWS ? WIFI_MAX_NETS + 8 : SN_ROWS)
/* Every ZyperOS menu row has to fit through the runner too, or the bottom of
 * the menu is cut off with no sign of it. */
_Static_assert(MENU_RUN_ROWS >= MENU_MAX_ROWS, "MENU_RUN_ROWS < MENU_MAX_ROWS");

/* How a screen ended, for the callers that have to act differently. The
 * in-game menu is the one that does: B and MENU mean Continue there, and it
 * has to send RESUME - but the runner owns those two buttons and flushes the
 * input on the way out, so asking a->in afterwards would always say no. */
typedef enum {
	MENU_LEFT_BACK,     /* B or MENU, the ordinary way out */
	MENU_LEFT_SCREEN,   /* the screen's own handler said so */
	MENU_LEFT_GONE      /* quit, power, or nothing left to show */
} menu_exit;

/* Fill `rows`, name the screen, and return how many rows. MUST NOT touch the
 * renderer or the event loop, and the part that actually decides what the rows
 * SAY must be a separate function in a file the check can link - src/wifi_menu.c
 * and src/sys_menu.c are those files. The design has failed if that stops
 * being true.
 *
 * Reading the device from here is allowed and unavoidable: a row that reports
 * the network has to ask every frame. What makes it checkable is that the
 * asking is confined to this half - wifi.c is stubbed on a host build, and the
 * system menu's asking lives in menu_build rather than in sys_menu_build.
 *
 * Called every frame, so it is also where live state is reflected - there is no
 * separate "refresh".
 *
 * The heading comes from here because it is a function of the same state the
 * rows are: the system menu is titled with the system whose rows it is showing.
 * Passing it to the runner separately meant keeping two things in step that are
 * really one thing. */
typedef int (*menu_build_fn)(void *ctx, menu_row *rows, int max,
                             const char **heading);

/* A result can be reached through Muse over a paused game. Keep that
 * menu's backdrop and power rule through nested menus, until it returns.
 * A stop inside a result must unwind even if its caller returns FAILED or
 * CHANGED, rather than DONE. */
static const menu_style *g_message_over;
static void *g_message_ctx;
static bool g_message_stopped;

/* Read every frame through the caller's pointer, so a screen that keeps its
 * style inside its own context can change it between frames. The info screen
 * does: its accent belongs to the system that OWNS the game, and toggling a
 * favorite on the Favorites shelf can hand it a different one. */

/* A key the runner did not consume, and the row it happened on. Free to open a
 * confirm, a keyboard or a wait panel: each runs its own loop and returns.
 * `sel` indexes the rows the last build produced. */
typedef menu_result (*menu_key_fn)(app *a, void *ctx, in_button key, int sel);

/* Zyper's answer to how a press went, in the menu he is in (`z`, NULL
 * elsewhere): a hop and a pant for something changed, the cocked head for a
 * failure, nothing for the rest. menu_send answers with it and so does a
 * --zyper shot, so the rule the menu keeps is the one a shot shows. */
static void menu_zyper_answer(zr_rig *z, unsigned now, menu_result r)
{
	if (!z) return;
	if (r == MENU_CHANGED) zr_works(z, now);
	else if (r == MENU_FAILED) zr_fails(z, now);
}

/* A key to the screen, and Zyper's answer to it: any press drops what he had
 * booked, and the answer comes from the moment the handler returns - which,
 * for a sign-in, is when its screen has gone and he can be seen again. */
static menu_result menu_send(app *a, zr_rig *z, menu_key_fn on_key, void *ctx,
                             in_button key, int sel)
{
	menu_result r;

	if (z) zr_press(z, plat_now_ms());
	r = on_key(a, ctx, key, sel);
	menu_zyper_answer(z, plat_now_ms(), r);
	return r;
}

/* Left or right, given to the screen, and answered (DESIGN.md 9). A row whose
 * value steps round always moves, so it is heard on the press, before the
 * screen's handler, which may take a while (MENU_SND_RING); a row that plays
 * its own is left to it (MENU_SND_OWN). Any other - a value that stops at its
 * ends, or a row with nothing to step - can only be heard after: move when
 * its value changed, edge when it did not, the rows built again to see, as
 * the next frame would build them. Each goes through menu_send, so Zyper
 * answers a side key as he answers any other. */
static menu_result menu_side_key(app *a, zr_rig *z, menu_build_fn build,
                                 menu_key_fn on_key, void *ctx,
                                 const menu_row *rows, int sel,
                                 unsigned how, in_button b)
{
	static menu_row now[MENU_RUN_ROWS];
	const char *heading;
	char was[128];
	menu_result r;
	int n;

	if (how & MENU_SND_OWN) return menu_send(a, z, on_key, ctx, b, sel);
	if (how & MENU_SND_RING) {
		moved_sound(a, b);
		return menu_send(a, z, on_key, ctx, b, sel);
	}
	snprintf(was, sizeof was, "%s",
	         rows[sel].value && !ROW_IS_NOTE(rows[sel]) ? rows[sel].value : "");
	r = menu_send(a, z, on_key, ctx, b, sel);
	if (r == MENU_DONE || !a->running || want_quit || g_message_stopped) return r;
	n = build(ctx, now, MENU_RUN_ROWS, &heading);
	if (n > MENU_RUN_ROWS) n = MENU_RUN_ROWS;
	uisound_cue(uisound_step(&g_steps,
	                         sel >= n || strcmp(was, now[sel].value && !ROW_IS_NOTE(now[sel])
	                                                 ? now[sel].value : ""), !a->in.pressed[b]));
	return r;
}

/* The loop itself. Called only through menu_run below, which owns the flush on
 * either side of it. */
static menu_exit menu_run_body(app *a, const menu_style *st,
                               menu_build_fn build, menu_key_fn on_key,
                               void *ctx, int lv)
{
	menu_row rows[MENU_RUN_ROWS];
	const char *heading = NULL;
	int sel = st->start_sel, n = 0, b;
	float k;
	/* Zyper's moves, in the menu that has him, on this loop's clock: it
	 * draws every pass, so the breath and the blinks need nothing more */
	zr_rig zy, *z = st->zyper ? &zy : NULL;

	if (z) {
		unsigned t = plat_now_ms();

		zr_init(z, t, t);
		zr_open(z, t);
	}
	while (a->running && !want_quit && !g_message_stopped) {
		if (z) zr_tick(z, plat_now_ms());
		n = build(ctx, rows, MENU_RUN_ROWS, &heading);
		if (n > MENU_RUN_ROWS) n = MENU_RUN_ROWS;
		if (n <= 0) return MENU_LEFT_GONE;

		/* The list is rebuilt every frame and can shrink under the cursor -
		 * a rescan finding fewer networks, a row going away. Clamp first,
		 * then land on something live. */
		if (sel >= n) sel = n - 1;
		if (sel < 0) sel = 0;
		if (!rows[sel].live) {
			sel = menu_step_sel(rows, n, sel, +1);
			if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, -1);
		}

		plat_input_poll(&a->in);
		/* Where the sound goes, followed in the menus too: a headset connected
		 * from the Bluetooth screen, or a cable plugged in, only took the sound
		 * once the menus were left - seen 2026-09-26. It sends only on a change.
		 * And Muse is read here as well: its answers - where it now is, and the
		 * end of a track, which is when the next is sent - waited for the menus
		 * to be left, so the row read stale and an album stopped at the end of a
		 * song while a menu was open. #45. */
		muse_poll();
		aout_apply(false);
		/* Auto's clock, in every screen this runs - the menus, and the
		 * in-game menu over a paused game too. 7:00 or 19:00 arriving while
		 * one is open is drawn from the next frame, which this loop draws
		 * every pass, and the file Diatom and portkeys read follows. */
		tick_theme(a);
		if (a->in.quit_requested) { a->running = false; return MENU_LEFT_GONE; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (!st->on_power) { power_off(a); return MENU_LEFT_GONE; }
			if (st->on_power(a, ctx) == MENU_DONE) return MENU_LEFT_GONE;
		}
		if (menu_leaving(a))
			return MENU_LEFT_BACK;
		/* The in-game menu too: its game is paused underneath, and Muse
		 * borrows its backdrop and its power rule for as long as it is up.
		 * Rebuilt on the way back, because anything may have changed while
		 * Muse covered it - a Bluetooth headset connecting, for one. */
		if (a->in.pressed[IN_SELECT]) {
			if (muse_open(a, st, ctx)) return MENU_LEFT_GONE;
			continue;
		}

		/* A move is a press too: it drops what Zyper had booked */
		if (in_repeat(&a->in, IN_UP)) {
			sel = menu_step_heard(a, rows, n, sel, -1, IN_UP);
			if (z) zr_press(z, plat_now_ms());
		}
		if (in_repeat(&a->in, IN_DOWN)) {
			sel = menu_step_heard(a, rows, n, sel, +1, IN_DOWN);
			if (z) zr_press(z, plat_now_ms());
		}

		/* Volume and brightness keep working here, as they do everywhere.
		 *
		 * They were missed when this runner was written, so the Wi-Fi screen
		 * spent a day as the one screen in the launcher where the volume keys
		 * did nothing. That is the exact failure this runner exists to make
		 * impossible, arriving by the one route it could still come from: the
		 * runner forgetting, rather than a screen forgetting. Every other loop
		 * in this file carries these four lines by hand. */
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Left and right reach the screen on REPEAT, not just on press, so a
		 * row whose value cycles can be held down. Everything else is edge
		 * triggered: holding A on a row that opens a screen should open it
		 * once. Sent before the press loop below, which skips them. */
		if (on_key && in_repeat(&a->in, IN_LEFT))
			if (menu_side_key(a, z, build, on_key, ctx, rows, sel,
			                  menu_sound_at(st->sound, n, sel), IN_LEFT) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (on_key && in_repeat(&a->in, IN_RIGHT))
			if (menu_side_key(a, z, build, on_key, ctx, rows, sel,
			                  menu_sound_at(st->sound, n, sel), IN_RIGHT) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (!a->running || want_quit || g_message_stopped) return MENU_LEFT_GONE;

		/* Everything the runner did not consume goes to the screen, so a
		 * screen-specific key needs no runner change to exist. */
		for (b = 0; b < IN_COUNT; b++) {
			if (!a->in.pressed[b]) continue;
			if (b == IN_LEFT || b == IN_RIGHT) continue;
			if (b == IN_UP || b == IN_DOWN || b == IN_BACK || b == IN_MENU
			    || b == IN_POWER || b == IN_VOLUP || b == IN_VOLDN
			    || b == IN_BRIGHTUP || b == IN_BRIGHTDN) continue;
			/* A, answered before the screen has it, since what it opens
			 * runs its own loop: accept, or deny on a row that can do
			 * nothing, or nothing with A; a row that plays its own is
			 * left to its screen. */
			if (b == IN_ACCEPT && !(menu_sound_at(st->sound, n, sel) & MENU_SND_OWN))
				uisound_play(on_key && rows[sel].live &&
				             !(menu_sound_at(st->sound, n, sel) & MENU_SND_NO_A)
				             ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
			if (on_key && menu_send(a, z, on_key, ctx, (in_button)b, sel) == MENU_DONE)
				return MENU_LEFT_SCREEN;
			if (!a->running || want_quit || g_message_stopped) return MENU_LEFT_GONE;
			/* The screen it opened was left with MENU: out of this one too,
			 * without drawing it again first. */
			if (g_menu_closing) return MENU_LEFT_BACK;
			/* A handler may have run a modal and changed everything under
			 * us; rebuild before drawing rather than drawing stale rows. */
			break;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		if (st->backdrop) {
			st->backdrop(a, ctx, k);
		} else {
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
		}
		g_menu_zyper = z;
		g_menu_zyper_at = plat_now_ms();
		menu_draw(a, heading, rows, n, sel, st->fixed_w,
		          st->follow_tint ? a->tint : st->accent);
		g_menu_zyper = NULL;
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	return MENU_LEFT_GONE;
}

/* One screen, one loop. Owns polling, power, idle, quit, back, volume,
 * brightness, the cursor and the whole frame, so that no screen can forget any
 * of them - which every menu defect this project has had came down to.
 *
 * The flush is here rather than in the body: the press that opens a screen must
 * not also close it, and the press that closes one must not arrive again at
 * whatever is underneath. This file does that by hand about twenty times, and
 * the body has nine ways out, so putting it at each of them would eventually
 * mean missing one. */
static menu_exit menu_run(app *a, const menu_style *st,
                          menu_build_fn build, menu_key_fn on_key, void *ctx)
{
	menu_exit how;
	int lv;
	const menu_style *previous = g_message_over;
	void *previous_ctx = g_message_ctx;

	if (!previous) g_message_stopped = false;
	if (!previous || st->backdrop || st->on_power) {
		g_message_over = st;
		g_message_ctx = ctx;
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	/* And it opens and closes (panel_open): quit and power, and nothing left
	 * to show, leave nothing to see go. */
	lv = panel_open();
	how = menu_run_body(a, st, build, on_key, ctx, lv);
	panel_close(a, lv, how == MENU_LEFT_GONE);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	g_message_over = previous;
	g_message_ctx = previous_ctx;
	return how;
}

/* A result, unlike wait_panel's single frame before a blocking operation.
 * Its hold starts after the panel arrives, on the same wall clock. Its
 * close completes here, so a result cannot hand queued keys to its parent.
 * The parent's own arrival remains the parent's to draw. */
static bool timed_message(app *a, const char *heading, const menu_row *rows,
                           int n, int fixed_w, unsigned accent)
{
	message_time clock;
	message_phase phase;
	bool closed = false, power = a->in.pressed[IN_POWER];
	bool quit = a->in.quit_requested;
	int lv = panel_open();
	unsigned now = anim_now();
	mm_frame f = mm_at(lv, now);
	static const unsigned colors[1] = { VCOL_MESSAGE };

	if (!g_message_over) g_message_stopped = false;
	message_begin(&clock, now, n, f.moving
	              ? DS_MS_QUICK + (f.before.alpha > 0.0f ? DS_MS_EXIT : 0) : 0);
	/* The opening press is already consumed. Poll the queue rather than
	 * flushing it: POWER or quit during a blocking call must still act. */
	memset(&a->in, 0, sizeof a->in);
	snap_record(g_snap_next, PANEL_MENU, heading, rows, n, -1, fixed_w,
	            accent, n == 1 ? colors : NULL, false, 0, NULL);
	for (;;) {
		bool press = false, back;
		int b;
		float k;

		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		tick_theme(a);
		now = anim_now();
		quit = quit || a->in.quit_requested || want_quit || !a->running;
		power = power || a->in.pressed[IN_POWER] || idle_due(a);
		for (b = 0; b < IN_COUNT; b++)
			if (a->in.pressed[b]) { press = true; break; }
		back = menu_leaving(a);
		phase = message_step(&clock, now, press, back, power, quit);
		if (phase == MESSAGE_QUIT || phase == MESSAGE_POWER) {
			g_message_stopped = true;
			if (phase == MESSAGE_QUIT) a->running = false;
			else if (g_message_over && g_message_over->on_power)
				g_message_over->on_power(a, g_message_ctx);
			else power_off(a);
			if (!closed) panel_close(a, lv, true);
			mm_leaving_cut();
			break;
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		if (phase != MESSAGE_SHOW && !closed) {
			if (back) panel_settle(lv);
			panel_close_at(a, lv, false, clock.close_at);
			closed = true;
		}
		/* MENU leaves every parent without another frame. The outermost
		 * owner draws this panel's exit once, as for every other submenu. */
		if (g_menu_closing) break;
		if (phase == MESSAGE_DONE) {
			/* A press during the exit ends it now. Do not leave a second
			 * copy of that exit for the parent to draw after returning. */
			if (press) { mm_press(mm_depth()); mm_leaving_cut(); }
			break;
		}
		if (closed) {
			f = mm_at(mm_depth(), now);
			if (!(f.before.alpha > 0.0f)) f = mm_leaving(now);
			k = f.scrim;
		} else {
			if (!panel_frame(a, lv, &k)) continue;
		}
		tick_tint(a);
		if (g_message_over && g_message_over->backdrop)
			g_message_over->backdrop(a, g_message_ctx, k);
		else { draw_shelf(a); ui_scrim_k(a->r, 0, k); }
		if (closed) snap_draw(a, f.before);
		else menu_draw_ex(a, heading, rows, n, -1, fixed_w, accent,
		                  n == 1 ? colors : NULL, false, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	/* All queued events went through the poll above, including on the last
	 * frame. Clear held/repeat state too; do not blindly drain a new POWER. */
	memset(&a->in, 0, sizeof a->in);
	/* The owner may return immediately too (Box Art). The result's exit
	 * already finished; its saved panel must not leave a second time. */
	if (!g_menu_closing && g_snap->level == lv) g_snap->ok = false;
	return !g_message_stopped && !g_menu_closing;
}

static bool result_message(app *a, const char *heading, const char *text)
{
	menu_row row = { text, NULL, false };

	return timed_message(a, heading, &row, 1, 0, MENU_ZYPEROS);
}

/* The shelf under the keyboard and the sign-in messages. kb_prompt's loop is
 * src/keyboard.c's, which knows no app and so no theme_changed; it calls this
 * every pass, and Auto's clock is asked here for it. */
typedef struct {
	app *a;
	int level;
	bool closed, shown;
} keyboard_transition;

static kb_pose keyboard_panel(void *ctx, kb_stage stage, bool pressed)
{
	keyboard_transition *tr = ctx;
	app *a = tr->a;
	const kb_pose gone = {1, 0, false};
	mm_frame f;
	unsigned now = anim_now();

	if (stage == KB_PANEL_OPEN) {
		tr->level = panel_open();
		return gone;
	}
	if (stage == KB_PANEL_LEAVE || stage == KB_PANEL_CUT) {
		if (!tr->closed) {
			int shown = stage == KB_PANEL_CUT ? 0 :
			            tr->shown ? tr->level : snap_shown();

			mm_close(tr->level, now, shown, false);
			g_snap_next->ok = false;
			tr->closed = true;
		}
		if (stage == KB_PANEL_CUT) {
			mm_press(mm_depth());
			mm_leaving_cut();
			tr->shown = false;
		}
		return gone;
	}
	if (stage == KB_PANEL_FINISH) {
		/* No keyboard text or image ever enters g_snap. Its private
		 * textures die in kb_prompt before this callback returns. */
		mm_leaving_cut();
		tr->shown = false;
		return gone;
	}
	if (tr->closed) {
		if (pressed) {
			mm_press(mm_depth());
			mm_leaving_cut();
			tr->shown = false;
		}
		f = mm_at(mm_depth(), now);
		if (!(f.before.alpha > 0.0f)) f = mm_leaving(now);
		if (!tr->shown || !(f.before.alpha > 0.0f)) return gone;
	} else {
		if (pressed) mm_press(tr->level);
		f = mm_at(tr->level, now);
	}

	ui_sounds_follow();
	tick_theme(a);
	draw_shelf(a);
	ui_scrim_k(a->r, 0, f.scrim);
	if (tr->closed)
		return (kb_pose){f.before.scale, f.before.alpha, true};
	if (f.before.alpha > 0.0f) snap_draw(a, f.before);
	if (f.panel.alpha > 0.0f) tr->shown = true;
	return (kb_pose){f.panel.scale, f.panel.alpha, f.moving};
}

static bool keyboard_idle(void *ctx)
{
	return idle_due(((keyboard_transition *)ctx)->a);
}

static kb_result keyboard_prompt(app *a, const char *title, char *buf, int cap)
{
	keyboard_transition tr = { .a = a };
	kb_result result = kb_prompt(a->r, &a->in, title, buf, cap,
	                              keyboard_panel, keyboard_idle, &tr);
	if (a->in.quit_requested) a->running = false;
	memset(&a->in, 0, sizeof a->in);
	return result;
}

static menu_result wifi_key(app *a, void *ctx, in_button key, int sel)
{
	wifi_ui *w = ctx;
	char status[96], ssid[WIFI_SSID_MAX], ip[64];
	int k = sel - 1;                    /* row 0 is the switch */
	bool net_row = sel >= 1 && sel <= w->n;

	/* Y scans, with the radio on; X forgets, on a network that was joined.
	 * Anywhere else they can do nothing, and say so. */
	if (key == IN_Y) uisound_play(w->on ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
	if (key == IN_X)
		uisound_play(w->on && net_row && w->nets[k].known ? DS_SOUND_ACCEPT
		                                                  : DS_SOUND_DENY);
	if (key == IN_Y && w->on) { wifi_begin_scan(w); return MENU_STAY; }

	/* Forget a saved network. Only saved rows offer it - there is nothing to
	 * forget about one the device has never joined, and a button that silently
	 * does nothing on some rows is the same defect as a menu entry wired to
	 * nothing.
	 *
	 * Forgetting the CONNECTED network is allowed, deliberately. Refusing would
	 * be safer - it drops the device off the LAN, taking Wi-Fi File Transfer and ssh
	 * with it, and a handheld has no keyboard to climb back out with. But
	 * wanting to forget the network you are on is reasonable, and the honest
	 * place to say what it costs is the confirm. */
	if (key == IN_X && w->on && net_row && w->nets[k].known) {
		char cur[WIFI_SSID_MAX], msg[160];
		bool joined = wifi_status(cur, sizeof cur, NULL, 0) == WIFI_CONNECTED
		              && !strcmp(cur, w->nets[k].ssid);

		if (joined)
			snprintf(msg, sizeof msg, T(S_WIFI_FORGET_JOINED), w->nets[k].ssid);
		else
			snprintf(msg, sizeof msg, T(S_WIFI_FORGET_ASK), w->nets[k].ssid);

		if (confirm_panel(a, T(S_MENU_WIFI), msg, T(S_WIFI_FORGET))) {
			snprintf(status, sizeof status,
			         wifi_forget(w->nets[k].ssid) ? T(S_WIFI_FORGOT)
			                                      : T(S_WIFI_FORGET_FAILED),
			         w->nets[k].ssid);
			if (!result_message(a, T(S_MENU_WIFI), status)) return MENU_STAY;
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (key != IN_ACCEPT) return MENU_STAY;

	/* The switch. Saved on every change rather than on the way out: the way out
	 * of a handheld is often the power button. */
	if (sel == 0) {
		if (w->on) {
			wifi_down();
			wifi_pref_save(false);
			w->on = false;
			w->n = 0;
		} else {
			wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_TURNING_ON));
			w->on = wifi_up();
			wifi_pref_save(w->on);
			if (!w->on) {
				if (!result_message(a, T(S_MENU_WIFI), T(S_WIFI_DID_NOT_COME_UP)))
					return MENU_STAY;
			}
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (net_row) {
		char psk[80] = "";
		bool ok;

		/* A saved network already has its passphrase in wpa_supplicant.conf,
		 * so asking again would be asking the user to retype something the
		 * device is holding. An open network has none to ask for. */
		if (w->nets[k].secured && !w->nets[k].known) {
			kb_result kr = keyboard_prompt(a, w->nets[k].ssid, psk, (int)sizeof psk);
			if (kr == KB_POWER) { power_off(a); return MENU_DONE; }
			if (kr != KB_ACCEPT) return MENU_STAY;
		}

		wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_CONNECTING));
		ok = wifi_connect(w->nets[k].ssid, psk[0] ? psk : NULL);
		/* Wiped as soon as it has been handed over. It still exists in the
		 * supplicant's config, which is the point, but there is no reason for
		 * a copy to sit in the launcher's stack afterwards. */
		memset(psk, 0, sizeof psk);

		if (ok) {
			/* Connecting is turning it on, whatever the switch said. */
			wifi_pref_save(true);
			w->on = true;
			wifi_status(ssid, sizeof ssid, ip, sizeof ip);
			snprintf(status, sizeof status, T(S_WIFI_CONNECTED_TO), ssid);
		} else {
			snprintf(status, sizeof status, T(S_WIFI_CONNECT_FAILED), w->nets[k].ssid);
		}
		if (!result_message(a, T(S_MENU_WIFI), status)) return MENU_STAY;
		/* Out on success, because the job is done and the menu row behind this
		 * screen already names the network - staying in the list makes you back
		 * out by hand to see the result. On failure stay, because the next
		 * thing wanted is another try or another network, and both are here. */
		if (ok) return MENU_DONE;
		wifi_begin_scan(w);
	}
	return MENU_STAY;
}

static void wifi_screen(app *a)
{
	wifi_ui w = { 0 };

	/* Entering does not switch the radio on. It used to, which made the screen
	 * impossible to leave in the off state: you opened it to turn wifi OFF and
	 * the act of opening it turned wifi on. */
	w.on = wifi_status(NULL, 0, NULL, 0) != WIFI_OFF;
	if (w.on) wifi_begin_scan(&w);

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS,
	                           .fixed_w = menu_std_width(a) },
	         wifi_build, wifi_key, &w);
}


/* ---------- About --------------------------------------------------------- */

#ifndef ZYPEROS_VERSION
#define ZYPEROS_VERSION "0.0"       /* set by the makefiles from Makefile's VERSION */
#endif

/* Facts about the machine, which is a different thing from settings. The
 * address in particular has nowhere else to live: the Wi-Fi screen names the
 * network, and short of asking the router there is no way to find out what
 * address the device took. */
/* Signing in to RetroAchievements. Two prompts and a request; the account
 * screen is deliberately not a screen, because there is nothing to look at
 * until there is something to say.
 *
 * The password is used to get a token and then wiped. Nothing stores it -
 * ra.cfg holds the token, which is what every later call uses anyway.
 *
 * Says how it went, for Zyper in the top menu: MENU_CHANGED signed in,
 * MENU_FAILED refused or with no network, MENU_STAY when it was left at a
 * prompt. */
static menu_result ra_signin_screen(app *a)
{
	char user[RA_USER_MAX] = "", pass[96] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ra_user());

	if (!net_online()) {
		row = (menu_row){ MSG_NEEDS_WIFI, NULL, false };
		timed_message(a, "RetroAchievements", &row, 1, 0, MENU_ZYPEROS);
		return MENU_FAILED;
	}

	kr = keyboard_prompt(a, T(S_CHEEVOS_RA_USER), user, (int)sizeof user);
	if (kr == KB_POWER) { power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !user[0]) return MENU_STAY;

	kr = keyboard_prompt(a, T(S_PANEL_PASSWORD), pass, (int)sizeof pass);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return MENU_STAY; }

	wait_panel(a, "RetroAchievements", T(S_PANEL_SIGNING_IN));
	ok = ra_sign_in(user, pass, err, sizeof err);
	/* Wiped the moment it has been used, the same as the Wi-Fi passphrase.
	 * The token it bought is the thing worth keeping. */
	memset(pass, 0, sizeof pass);

	if (ok) {
		ra_creds_save();
		snprintf(err, sizeof err, T(S_PANEL_SIGNED_IN_AS), ra_user());
	} else if (!err[0]) {
		snprintf(err, sizeof err, "%s", T(S_PANEL_SIGN_IN_FAILED));
	}

	row = (menu_row){ err, NULL, ok };
	timed_message(a, "RetroAchievements", &row, 1, 0, MENU_ZYPEROS);
	return ok ? MENU_CHANGED : MENU_FAILED;
}

/* The ScreenScraper account. The same shape as the screen above, and the
 * differences are the interesting part.
 *
 * There is nothing to trade the password for. RetroAchievements returns a
 * token and the password is dropped here and never stored; ScreenScraper signs
 * every request with the password itself, so what ss_sign_in keeps is a
 * reusable credential and the only "sign-in" available is asking them whether
 * they answer to it. The password is still wiped from THIS screen's stack the
 * moment it has been handed over, because a buffer that outlives its use is
 * the one thing a screen can get wrong on its own.
 *
 * The third state is the one RetroAchievements does not have: a build made
 * without SS_DEVID cannot sign anyone in, and says so rather than taking a
 * password it has no way to use. See src/ss.h. It says how it went as the
 * screen above does, and that third state is a failure too. */
static menu_result ss_signin_screen(app *a)
{
	char user[SS_USER_MAX] = "", pass[SS_PASS_MAX] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ss_user());

	if (!ss_have_dev() || !net_online()) {
		row = (menu_row){ !ss_have_dev()
		                  ? T(S_SCRAPER_SS_NO_KEY)
		                  : MSG_NEEDS_WIFI, NULL, false };
		timed_message(a, "ScreenScraper", &row, 1, 0, MENU_ZYPEROS);
		return MENU_FAILED;
	}

	kr = keyboard_prompt(a, T(S_SCRAPER_SS_USER), user, (int)sizeof user);
	if (kr == KB_POWER) { power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !user[0]) return MENU_STAY;

	kr = keyboard_prompt(a, T(S_PANEL_PASSWORD), pass, (int)sizeof pass);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return MENU_STAY; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return MENU_STAY; }

	wait_panel(a, "ScreenScraper", T(S_PANEL_SIGNING_IN));
	ok = ss_sign_in(user, pass, err, sizeof err);
	memset(pass, 0, sizeof pass);

	/* ss_sign_in stores the account itself, because it is the half that knows
	 * whether the reply was real. Nothing to save here. */
	if (ok) snprintf(err, sizeof err, T(S_PANEL_SIGNED_IN_AS), ss_user());
	else if (!err[0]) snprintf(err, sizeof err, "%s", T(S_PANEL_SIGN_IN_FAILED));

	row = (menu_row){ err, NULL, ok };
	timed_message(a, "ScreenScraper", &row, 1, 0, MENU_ZYPEROS);
	return ok ? MENU_CHANGED : MENU_FAILED;
}

/* MENU > SteamGridDB: the key for Box Art's last source (src/sgdb.h).
 *
 * A key rather than an account, so a screen rather than two prompts: the key
 * masked to its last four, what is known about it, and a row to take it off.
 * Typing one checks it once against their API; a key they refuse is not kept,
 * and the keyboard comes back with what was typed, since a 32-character key
 * typed on a D-pad is wrong by one letter more often than it is wrong. A key
 * that got no answer is kept, for the next Box Art run to check.
 *
 * The rows are sys_menu_sgdb_rows's, so tools/menu-check.c can hold them. */
static const char *sgdb_status(void)
{
	return sgdb_key_state() == SGDB_WORKS ? "works"
	     : sgdb_key_state() == SGDB_REFUSED ? "refused" : "not checked";
}

/* The same, as the Status row says it, in the language that is on:
 * sgdb_status's words are the page's, which compares them */
static const char *sgdb_status_shown(void)
{
	return sgdb_key_state() == SGDB_WORKS ? T(S_SCRAPER_SGDB_WORKS)
	     : sgdb_key_state() == SGDB_REFUSED ? T(S_SCRAPER_SGDB_REFUSED)
	     : T(S_SCRAPER_SGDB_NOT_CHECKED);
}

static int sgdb_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static char masked[32];
	sg_row ids[SG_ROWS];

	(void)ctx;
	(void)max;
	*heading = "SteamGridDB";
	sgdb_key_masked(masked, sizeof masked);
	return sys_menu_sgdb_rows(sgdb_have_key() ? masked : NULL, sgdb_status_shown(),
	                          rows, ids);
}

/* The check, while it runs: the wait panel every frame and the power button
 * read, which a blocking request would not. Still RUNNING when the device
 * was turned off under it. */
static sgdb_try sgdb_wait(app *a)
{
	sgdb_try t;

	while ((t = sgdb_try_poll()) == SGDB_TRY_RUNNING) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER]) { power_off(a); break; }
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		wait_panel(a, "SteamGridDB", sgdb_try_said());
		ui_frame_rest(16);
	}
	memset(&a->in, 0, sizeof a->in);
	return t;
}

static menu_result sgdb_enter(app *a)
{
	char key[SGDB_KEY_MAX * 2] = "";
	menu_result said = MENU_STAY;

	if (!net_online()) {
		result_message(a, "SteamGridDB", MSG_NEEDS_WIFI);
		return MENU_FAILED;
	}
	for (;;) {
		kb_result kr = keyboard_prompt(a, T(S_SCRAPER_SGDB_API_KEY), key, (int)sizeof key);
		sgdb_try t;

		if (kr == KB_POWER) { memset(key, 0, sizeof key); power_off(a); return MENU_STAY; }
		if (kr != KB_ACCEPT || !key[0]) break;
		t = sgdb_try_begin(key);
		if (t == SGDB_TRY_RUNNING) t = sgdb_wait(a);
		if (t == SGDB_TRY_RUNNING || !a->running || want_quit) break;
		if (!result_message(a, "SteamGridDB", sgdb_try_said())) break;
		if (t == SGDB_TRY_WORKS || t == SGDB_TRY_KEPT) { said = MENU_CHANGED; break; }
		/* Refused, or not a key: the keyboard again, with what was typed */
		said = MENU_FAILED;
	}
	memset(key, 0, sizeof key);
	return said;
}

static menu_result sgdb_key(app *a, void *ctx, in_button key, int sel)
{
	menu_result *said = ctx, r = MENU_STAY;
	menu_row rows[SG_ROWS];
	sg_row ids[SG_ROWS];
	int n;

	if (key != IN_ACCEPT) return MENU_STAY;
	n = sys_menu_sgdb_rows(sgdb_have_key() ? "" : NULL, NULL, rows, ids);
	if (sel < 0 || sel >= n) return MENU_STAY;
	if (ids[sel] == SG_KEY) {
		r = sgdb_enter(a);
	} else if (ids[sel] == SG_REMOVE &&
	           confirm_panel(a, "SteamGridDB",
	                         T(S_SCRAPER_UNLINK_KEY_ASK),
	                         T(S_SCRAPER_UNLINK))) {
		sgdb_key_forget();
		result_message(a, "SteamGridDB", T(S_SCRAPER_UNLINKED));
		r = MENU_CHANGED;
	}
	if (r == MENU_CHANGED || r == MENU_FAILED) *said = r;
	return r;
}

/* Says how it went, for Zyper in the top menu, as the sign-ins do: the last
 * thing done here that changed something or failed. */
static menu_result sgdb_screen(app *a)
{
	menu_result said = MENU_STAY;

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a) },
	         sgdb_build, sgdb_key, &said);
	return said;
}

/* Wi-Fi File Transfer: the address, the PIN, and what is happening.
 *
 * The whole screen is a waiting room. It exists so somebody can read two
 * things off a handheld and type them into a laptop, and then watch enough to
 * know it is working. Everything real happens in hare.c and in a browser
 * somewhere else on the network.
 *
 * The server lives exactly as long as this screen. Closing it stops
 * listening, forgets the PIN and drops every session - which is the honest
 * answer to "how long is my ROM folder on the network for". */
/* Bytes, in a unit a person reads rather than the one the counter is in.
 *
 * The shot harness was handed "41 MB out" as a literal while this screen could
 * only ever print kilobytes - a picture of a screen that did not exist, which
 * is the exact failure the shared row builder was meant to stop. Sharing the
 * layout is not enough if the fixture claims the content can be something it
 * cannot.
 *
 * Decimal, matching human() in res/web/app.js. These two describe the same
 * bytes to the same person a foot apart, so they have to divide by the same
 * thing, and decimal is what every file manager but Windows Explorer shows -
 * 1024 under a KB/MB label is the one combination that is wrong everywhere. */
static void human_bytes(char *out, size_t n, unsigned long long b)
{
	if (b < 1000ull)              snprintf(out, n, "%llu B", b);
	else if (b < 1000ull * 1000)  snprintf(out, n, "%llu KB", b / 1000);
	else if (b < 1000ull * 1000 * 1000)
		snprintf(out, n, "%.1f MB", (double)b / (1000 * 1000));
	else snprintf(out, n, "%.1f GB", (double)b / (1000ull * 1000 * 1000));
}

/* The address and the PIN go in the HEADING, not in rows.
 *
 * They were four rows of equal weight, and rendered it was obvious that the
 * two things this screen exists to be read off were in the dimmest color the
 * launcher has: menu_draw paints a value UI_TEXT_DIM unless its row is both
 * live and selected, and nothing on a status screen is either. The heading is
 * UI_F_LABEL at UI_TEXT_SOFT - larger and brighter - and takes two lines when
 * it contains a newline, which is exactly two things worth copying.
 *
 * Everything else is status and belongs below the rule. */
static void hare_head(char *out, size_t n, const char *addr, const char *pin)
{
	snprintf(out, n, "%s\nPIN %s", addr, pin);
}

static int hare_rows(menu_row *out, const char *who, const char *ssh,
                     const char *moved, const char *now, const char *legend)
{
	int n = 0;

	out[n++] = (menu_row){ T(S_XFER_BROWSERS), who, false };
	/* Over the cable, SSH beside the browsers (sshlink.h): who is signed in,
	 * or why nobody can be. */
	if (ssh) out[n++] = (menu_row){ "SSH", ssh, false };
	/* "In / out", not "Transferred": the longest label on the panel beside
	 * "12 KB in / 41 MB out" scrolled on every real session - just browsing
	 * moves bytes both ways, so both numbers are never zero - and a still
	 * caught it mid-word. Rendered 2026-09-30, this fits even at
	 * "1.2 GB / 413.5 MB". */
	out[n++] = (menu_row){ T(S_XFER_IN_OUT), moved, false };
	out[n++] = (menu_row){ T(S_XFER_NOW),    now,   false };
	/* The key legend (DESIGN.md 11.4): B hides it, Y stops it (xs_*) */
	if (legend) {
		out[n++] = MENU_RULE;
		out[n++] = MENU_NOTE(legend);
	}
	return n;
}

/* One frame of it, for the shot harness. The rows come from the same
 * function the screen uses, so a picture of this cannot quietly stop being a
 * picture of that. */
static void hare_preview(app *a, const char *addr, const char *pin,
                         const char *who, const char *ssh, const char *moved,
                         const char *now)
{
	menu_row rows[7];
	char head[192];
	int n = hare_rows(rows, who, ssh, moved, now, T(S_PANEL_B_CLOSE));

	hare_head(head, sizeof head, addr, pin);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
}

/* Wi-Fi File Transfer as the handheld's own network (src/hotspot.h): a
 * screen of its own (src/hotpanel.c draws it), opened and closed as a panel
 * is, the whole screen being the panel: from 96% and clear over the shelf.
 * The rows carry its text so the copy kept as it
 * leaves (snap_record) draws it whole: the code's text as the first label,
 * then the network, the password, the PIN, the phones and the news. */
static void hot_panel(app *a, const char *heading, const menu_row *rows, int n,
                      mm_pose pose)
{
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	SDL_Rect screen = { 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H };
	hotpanel p;

	if (n < 6) return;
	p = (hotpanel){ heading, rows[0].label, rows[1].value, rows[2].value,
	                rows[3].value, rows[4].value, rows[5].value,
	                n > 6 ? rows[6].label : NULL };
	if (posed) posed = panel_pass_begin(a);
	hotpanel_draw(a->r, &p, plat_now_ms());
	if (posed) panel_pass_end(a, &screen, pose);
}

static void hot_rows(menu_row *rows, char *phones, size_t pn, bool starting,
                     int joined, const char *pin, const char *now)
{
	if (starting)         snprintf(phones, pn, "%s", T(S_HOTPANEL_STARTING));
	else if (joined <= 0) snprintf(phones, pn, "%s", T(S_XFER_WAITING));
	else                  snprintf(phones, pn, TN(S_HOTPANEL_JOINED, joined), joined);
	rows[0] = (menu_row){ hotspot_qr_text(),     NULL,               false };
	rows[1] = (menu_row){ T(S_HOTPANEL_NETWORK), hotspot_ssid(),     false };
	rows[2] = (menu_row){ T(S_PANEL_PASSWORD),   hotspot_password(), false };
	rows[3] = (menu_row){ "PIN",                 pin,                false };
	rows[4] = (menu_row){ T(S_HOTPANEL_PHONES),  phones,             false };
	rows[5] = (menu_row){ T(S_XFER_NOW),         now,                false };
}

static void hot_show(app *a, const char *name, const char *pin, bool starting,
                     const char *now, const char *legend)
{
	menu_row rows[7];
	char phones[32];

	hot_rows(rows, phones, sizeof phones, starting, hotspot_phones(), pin, now);
	rows[6] = MENU_NOTE(legend);
	panel_show(a, PANEL_HOT, name, rows, 7, -1, 0, 0u, NULL, false, 0, NULL);
}

/* One frame of it for the shot harness (--hotspot), from the rows the screen
 * builds, with a network and password given rather than made. */
static void hot_preview(app *a, const char *ssid, const char *pass,
                        const char *phones, const char *now)
{
	char qr[160];
	menu_row rows[7];

	captive_wifi_qr(qr, sizeof qr, ssid, pass);
	rows[0] = (menu_row){ qr,         NULL,   false };
	rows[1] = (menu_row){ T(S_HOTPANEL_NETWORK), ssid,   false };
	rows[2] = (menu_row){ T(S_PANEL_PASSWORD),   pass,   false };
	rows[3] = (menu_row){ "PIN",                 "4071", false };
	rows[4] = (menu_row){ T(S_HOTPANEL_PHONES),  phones, false };
	rows[5] = (menu_row){ T(S_XFER_NOW),         now,    false };
	rows[6] = MENU_NOTE(T(S_PANEL_B_CLOSE));
	draw_shelf(a);
	ui_scrim(a->r, 0);
	panel_show(a, PANEL_HOT, XFER_NAME_WIFI, rows, 7, -1, 0, 0u, NULL, false, 0, NULL);
}

/* Download logs, from Wi-Fi File Transfer: see logpack.h. The names to hide are
 * gathered here, at the moment of the download, because only the launcher
 * knows them - every saved network rather than just the one it is on, since
 * an older boot's log can name another; every paired headset; both account
 * names. about.txt carries the first questions asked about any report. */
static app *g_logs_app;

static bool pack_logs(char *path, size_t pn, char *name, size_t nn)
{
	wifi_net   nets[WIFI_MAX_NETS];
	bt_device  bt[BT_MAX];
	const char *secrets[WIFI_MAX_NETS + BT_MAX + 5];
	char       now_ssid[WIFI_SSID_MAX] = "", ip[64], logs[CFG_STR * 2];
	char       folder[64], about[4096], err[128], stamp[32];
	int        n = 0, saved, nb, i;
	size_t     at = 0;
	time_t     t = time(NULL);
	struct tm  tm;
	struct statvfs vf;
	double     up = 0;
	FILE      *f;

	saved = wifi_known(nets, WIFI_MAX_NETS);
	for (i = 0; i < saved; i++) secrets[n++] = nets[i].ssid;
	if (wifi_status(now_ssid, sizeof now_ssid, ip, sizeof ip) != WIFI_OFF && now_ssid[0])
		secrets[n++] = now_ssid;
	nb = bt_bonded(bt, BT_MAX);
	for (i = 0; i < nb; i++) secrets[n++] = bt[i].name;
	if (ra_signed_in() && ra_user()) secrets[n++] = ra_user();
	if (ss_signed_in() && ss_user()) secrets[n++] = ss_user();
	/* The SteamGridDB key never goes into a log (src/sgdb.h); masked all the
	 * same, so a line nobody meant to write cannot carry it out. */
	if (sgdb_secret()) secrets[n++] = sgdb_secret();

	localtime_r(&t, &tm);
	strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tm);
	strftime(folder, sizeof folder, "zyperos-logs-%Y-%m-%d-%H%M", &tm);
	if ((f = fopen("/proc/uptime", "r"))) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }

#define ABOUT(...) do { \
		int k_ = snprintf(about + at, sizeof about - at, __VA_ARGS__); \
		if (k_ > 0 && (size_t)k_ < sizeof about - at) at += (size_t)k_; \
	} while (0)
	ABOUT("ZyperOS %s\n", ZYPEROS_VERSION);
	ABOUT("Packed %s, up %.0f minutes\n", stamp, up / 60);
	if (statvfs(P_CARD, &vf) == 0)
		ABOUT("Card: %.1f GB free of %.1f GB\n",
		      (double)vf.f_bavail * vf.f_frsize / 1e9, (double)vf.f_blocks * vf.f_frsize / 1e9);
	ABOUT("Auto Off: %d s\n", g_logs_app ? g_logs_app->auto_off : -1);
	ABOUT("Wi-Fi: %s\n", now_ssid[0] ? now_ssid : "not connected");
	ABOUT("Paired headsets: %d\n", nb);
	ABOUT("\nShelves:\n");
	for (i = 0; g_logs_app && i < g_logs_app->sys.count; i++) {
		const system_cfg *sc = &g_logs_app->sys.systems[i];

		if (is_muse(sc))
			ABOUT("  %-22s %d albums, %d books, %d tracks\n", sc->name,
			      ml_count(&g_muse, false), ml_count(&g_muse, true), g_muse.ntracks);
		else
			ABOUT("  %-22s %d\n", sc->name, g_logs_app->view[i].list.count);
	}
#undef ABOUT

	snprintf(logs, sizeof logs, "%s/logs", P_USERDATA);
	snprintf(name, nn, "%s.tar.gz", folder);
	snprintf(path, pn, "/tmp/%s", name);
	if (!logpack_build(logs, about, secrets, n, folder, path, err, sizeof err)) {
		fprintf(stderr, "logs: could not pack them: %s\n", err);
		return false;
	}
	fprintf(stderr, "logs: packed %s, %d names masked\n", name, n);
	return true;
}

/* A folder about to be deleted whole from Wi-Fi File Transfer, or a folder or file
 * about to be renamed there: if Muse is playing or paused on that file or on
 * one in that folder, stop Muse first. The shelf is rebuilt when the screen
 * closes, as for any change there; this is only so the player is not left
 * holding a file that is gone. */
static void muse_before_delete(const char *abs)
{
	const char *p = musec_path();
	char full[LIB_PATH * 2];
	size_t n = strlen(abs);

	if (!p[0]) return;
	snprintf(full, sizeof full, "%s/%s", P_CARD, p);
	if (!strncmp(full, abs, n) && (full[n] == '/' || !full[n])) {
		fprintf(stderr, "muse: stopped, what it played is being moved or deleted\n");
		musec_stop();
	}
}

/* Rows of the settings keyed "<kind><folder>" or "<kind><folder>/...", which
 * a folder renamed with everything in it takes along. Gathered first and
 * changed after, so the rows are not rewritten under the read. */
typedef struct key_move { struct key_move *next; char *key, *val; } key_move;
typedef struct { size_t n; key_move *list; } key_moves;

static bool key_under(const char *key, const char *value, void *ctx)
{
	key_moves *m = ctx;
	key_move *k;

	/* "Album" and "Album/CD1", not "Album 2" */
	if (key[m->n] && key[m->n] != '/') return true;
	if (!(k = calloc(1, sizeof *k))) return false;
	k->key = strdup(key);
	k->val = strdup(value);
	k->next = m->list;
	m->list = k;
	return true;
}

static void keys_follow(db *d, const char *kind, const char *frel, const char *trel)
{
	char prefix[LIB_PATH + 16], nkey[LIB_PATH * 2 + 32];
	key_moves m = { 0, NULL };
	key_move *k;

	if (snprintf(prefix, sizeof prefix, "%s%s", kind, frel) >= (int)sizeof prefix) return;
	m.n = strlen(prefix);
	db_each_prefix(d, prefix, key_under, &m);
	while ((k = m.list)) {
		m.list = k->next;
		if (k->key && k->val &&
		    snprintf(nkey, sizeof nkey, "%s%s%s", kind, trel, k->key + m.n) < (int)sizeof nkey) {
			db_del(d, k->key);
			db_set_str(d, nkey, k->val);
		}
		free(k->key);
		free(k->val);
		free(k);
	}
}

/* A book's place names its file ("<seconds>\t<file>"): when that file was
 * renamed, the place follows. With `to` NULL the file's new name is the one
 * Tidy names would have given it, if that is there now. */
static void book_file_follows(const char *folder, const char *from, const char *to)
{
	char key[LIB_PATH + 300], val[LIB_PATH + 32], tidy[256], there[LIB_PATH * 2 + 300];
	char *tab;

	snprintf(key, sizeof key, "book.%s", folder);
	if (!db_get_str(db_dev(), key, val, sizeof val, "") || !(tab = strchr(val, '\t'))) return;
	if (from && strcmp(tab + 1, from)) return;
	if (!to) {
		if (!ml_tidy_file(tab + 1, tidy, sizeof tidy)) return;
		snprintf(there, sizeof there, "%s/%s/%s", P_CARD, folder, tidy);
		if (access(there, F_OK) != 0) return;
		to = tidy;
	}
	snprintf(tab + 1, sizeof val - (size_t)(tab + 1 - val), "%s", to);
	db_set_str(db_dev(), key, val);
}

/* The transfer page renamed a music or audiobook folder or file, by hand or
 * with Tidy names, which may also have renamed the files in the folder: what
 * is kept under the old names follows. An album's cover, from the .media
 * beside it; what Album Art remembers of it and a book's place, keyed by the
 * folder's path, for it and any folder in it; and the shelf held in memory,
 * until it is read again on the way out of the screen, so the page's lists
 * find the album's cover meanwhile. */
static void muse_after_rename(const char *from, const char *to, bool tidied)
{
	size_t card = strlen(P_CARD), fl;
	const char *frel, *trel, *fbase = strrchr(from, '/'), *tbase = strrchr(to, '/');
	static const char *const EXT[] = { "jpg", "png" };
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 300];
	struct stat st;
	int k;

	if (strncmp(from, P_CARD, card) || from[card] != '/' ||
	    strncmp(to, P_CARD, card) || to[card] != '/' || !fbase || !tbase)
		return;
	frel = from + card + 1;
	trel = to + card + 1;
	fl = strlen(frel);
	/* A file renamed by hand: its track, and a book's place if it was there */
	if (stat(to, &st) == 0 && !S_ISDIR(st.st_mode)) {
		for (k = 0; k < g_muse.ntracks; k++)
			if (!strcmp(g_muse.tracks[k].path, frel))
				snprintf(g_muse.tracks[k].path, sizeof g_muse.tracks[k].path, "%s", trel);
		/* A rename names a sibling, so the folder is the same on both */
		snprintf(cfrom, sizeof cfrom, "%.*s", (int)(fbase - frel), frel);
		book_file_follows(cfrom, fbase + 1, tbase + 1);
		fprintf(stderr, "muse: %s is now %s\n", frel, trel);
		return;
	}
	/* Each track at its new path. Tidy names renamed a file in the folder
	 * itself by ml_tidy_file unless that name was taken, so the same rule
	 * finds it again; anything else kept its name. */
	for (k = 0; k < g_muse.ntracks; k++) {
		ml_track *t = &g_muse.tracks[k];
		char tidy[256], np[LIB_PATH], there[LIB_PATH * 2 + 300];
		const char *file;

		if (strncmp(t->path, frel, fl) || t->path[fl] != '/') continue;
		file = t->path + fl + 1;
		if (tidied && !strchr(file, '/') && ml_tidy_file(file, tidy, sizeof tidy)) {
			snprintf(there, sizeof there, "%s/%s/%s", P_CARD, trel, tidy);
			if (access(there, F_OK) == 0) file = tidy;
		}
		if (snprintf(np, sizeof np, "%s/%s", trel, file) < (int)sizeof np)
			snprintf(t->path, sizeof t->path, "%s", np);
	}
	if (strcmp(frel, trel)) {
		for (k = 0; k < 2; k++) {
			snprintf(cfrom, sizeof cfrom, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         fbase + 1, EXT[k]);
			if (access(cfrom, F_OK) != 0) continue;
			snprintf(cto, sizeof cto, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         tbase + 1, EXT[k]);
			rename(cfrom, cto);
		}
		keys_follow(db_lib(), "museart.", frel, trel);
		keys_follow(db_dev(), "book.", frel, trel);
	}
	if (tidied) book_file_follows(trel, NULL, NULL);
	if (strcmp(frel, trel)) fprintf(stderr, "muse: %s is now %s\n", frel, trel);
}

/* Album covers for the page, down with Muse's Album Art, and the covers the
 * shelf draws, down with Muse's shelf. */
static bool  cover_file(int al, char *out, size_t n);
static void  cover_recheck(int al);
static bool  cover_answers(void);
static char *albums_wanted(size_t *len, char *why, size_t wn);
static char *album_pick(int id, const char *json, size_t n, size_t *len,
                        char *why, size_t wn);
static void  xfer_after_write(const char *abs);

/* No network to be on, and an adapter that can be one: the screen makes the
 * handheld its own network (src/hotspot.h). On a network means an address,
 * as the screen needs it; a join still waiting for one is not. */
static bool xfer_wants_hotspot(void)
{
	char ip[64];

	if (!hotspot_available()) return false;
	return wifi_status(NULL, 0, ip, sizeof ip) != WIFI_CONNECTED || !ip[0];
}

/* The consoles as the transfer page sees their folders: every system the card
 * knows, from systems.cfg itself and not the launcher's shelves, which leave
 * out a system with no games - so an empty Atomiswave folder had no color on
 * the page, and BGM, no system at all, had nothing (2026-11-06). Read
 * when File Transfer opens. */
static systems_cfg g_xfer_sys;
static bool        g_xfer_sys_ok;

static const system_cfg *xfer_sys_of(const char *folder)
{
	int i;

	for (i = 0; g_xfer_sys_ok && i < g_xfer_sys.count; i++)
		if (!strcmp(g_xfer_sys.systems[i].folder, folder)) return &g_xfer_sys.systems[i];
	return NULL;
}

/* A console's color for its folder, as hare_set_folder_color asks */
static unsigned xfer_folder_color(const char *folder)
{
	const system_cfg *s = xfer_sys_of(folder);

	return s ? s->accent : CFG_NO_COLOR;
}

/* And its pixel art, as hare_set_folder_icon asks: the shelf's own for the
 * tag (home_art_key), or the menu music's for Roms/BGM (src/bgm.h) */
static const char *xfer_folder_icon(const char *folder)
{
	const system_cfg *s = xfer_sys_of(folder);

	if (s) return home_art_key(s->tag);
	if (!strcmp(folder, "BGM")) return "BGM";
	return NULL;
}

/* The server, and over the cable SSH with it: both end with the screen, and
 * the cable goes back to charging only unless Developer Mode keeps it. */
static void xfer_stop(bool cable)
{
	sshlink_stop();
	hare_set_ssh(NULL);
	hare_stop();
	hotspot_stop();     /* nothing when the screen was not the hotspot */
	if (cable) usblink_after_transfer(db_dev());
}

static void xfer_screen(app *a);
static void xfer_screen_way(app *a, int way);

/* Three choices side by side, DESIGN.md 11.18: a panel (11.3) holding the
 * heading, its rule, a tile per choice - **raised**, **radius.card**, its mark
 * and its name in two lines, the focused one the **accent** fill in
 * **on-accent** - and the key legend (11.4) under them. Tiles' names are
 * their labels, a newline between the two lines, and their mark is read
 * off the name (tile_glyph). Drawn at `pose` as every panel is.
 *
 * The tiles are the rows up to the first note (MENU_NOTE). Three notes after
 * them are File Transfer's question while files move (DESIGN.md 11.14,
 * My pick 2026-11-06): a line under the rule saying what moves, a line
 * under the tiles saying what the focused one does, and the legend. */
/* 272 holds "Developer" at the menu's size (242) with 12 either side, and
 * three of them with the gaps and pads are 944, inside the screen's insets */
#define TILE_W   272
#define TILE_H   232
#define TILE_GAP 24
#define TILE_PAD 40

/* A tile's mark, by its name: usb_ask's tiles and the two questions'
 * (xs_words), compared in the language that is on, or in English, which
 * the shot harness's --usb-ask names them in */
static ui_glyph tile_glyph(const char *label)
{
	static const struct { str_id name; ui_glyph g; } MARKS[] = {
		{ S_XFER_TILE_CHARGE_ONLY,   UI_GLYPH_CHARGE },
		{ S_XFER_TILE_FILE_TRANSFER, UI_GLYPH_TRANSFER },
		{ S_XFER_TILE_KEEP_GOING,    UI_GLYPH_ARROWS },
		{ S_XFER_TILE_NOT_NOW,       UI_GLYPH_ARROWS },
		{ S_XFER_TILE_STOP_AFTER,    UI_GLYPH_FINISH },
		{ S_XFER_TILE_CANCEL_NOW,    UI_GLYPH_CANCEL },
		{ S_XFER_TILE_CANCEL_PLAY,   UI_GLYPH_CANCEL },
		{ S_XFER_TILE_PLAY_AFTER,    UI_GLYPH_PLAY },
	};
	size_t i;

	for (i = 0; i < sizeof MARKS / sizeof MARKS[0]; i++)
		if (!strcmp(label, T(MARKS[i].name)) || !strcmp(label, T_en(MARKS[i].name)))
			return MARKS[i].g;
	return UI_GLYPH_TERMINAL;
}

static void tiles_panel(app *a, const char *heading, const menu_row *tiles, int n,
                        int sel, mm_pose pose)
{
	const char *legend = T(S_XFER_LEGEND_CHARGE_ONLY);
	const char *sub = NULL, *what = NULL;
	const ds_theme *thm = theme();
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int lh = ui_font_line(UI_F_LABEL), lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f;
	SDL_Rect panel, rule;
	int i, top, tiles_y, all = n;
	char fit[160];

	for (n = 0; n < all && !ROW_IS_NOTE(tiles[n]); n++) {}
	if (all - n == 3) {
		sub = tiles[n].label;
		what = tiles[n + 1].label;
		legend = tiles[n + 2].label;
	} else if (all - n == 1) {
		legend = tiles[n].label;
	}
	if (n < 1) return;
	panel.w = n * TILE_W + (n - 1) * TILE_GAP + 2 * TILE_PAD;
	panel.h = 32 + lh + 24 + DS_RULE + (sub ? 20 + ls : 0) + 32 + TILE_H +
	          (what ? 24 + ls : 0) + 24 + ls + 32;
	panel.x = (ZYPEROS_SCREEN_W - panel.w) / 2;
	panel.y = (ZYPEROS_SCREEN_H - panel.h) / 2;
	if (posed) {
		SDL_FRect fr = panel_scaled(&panel, pose.scale);
		SDL_Rect q = { (int)(fr.x + 0.5f), (int)(fr.y + 0.5f),
		               (int)(fr.w + 0.5f), (int)(fr.h + 0.5f) };

		ui_lift_k(a->r, &q, CFG_NO_COLOR, UI_LIFT_PANEL, pose.alpha);
		posed = panel_pass_begin(a);
	} else {
		ui_lift(a->r, &panel, CFG_NO_COLOR, UI_LIFT_PANEL);
	}
	ui_panel(a->r, &panel, DS_RADIUS_PANEL);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);

	top = panel.y + 32;
	ui_text(a->r, fh, heading, panel.x + panel.w / 2, ui_line_y(UI_F_LABEL, top), 0,
	        UI_TEXT_SOFT);
	rule = (SDL_Rect){ panel.x + TILE_PAD, top + lh + 24, panel.w - 2 * TILE_PAD, DS_RULE };
	SDL_SetRenderDrawColor(a->r, thm->edge.r, thm->edge.g, thm->edge.b, 255);
	SDL_RenderFillRect(a->r, &rule);
	tiles_y = rule.y + DS_RULE + 32;
	/* What moves, in **meta** **text-dim** under the rule */
	if (sub) {
		ui_fit_text(fs, sub, fit, sizeof fit, panel.w - 2 * TILE_PAD);
		ui_text(a->r, fs, fit, panel.x + panel.w / 2, ui_line_y(UI_F_META, rule.y + DS_RULE + 20),
		        0, UI_TEXT_DIM);
		tiles_y += 20 + ls;
	}

	for (i = 0; i < n; i++) {
		SDL_Rect t = { panel.x + TILE_PAD + i * (TILE_W + TILE_GAP), tiles_y, TILE_W, TILE_H };
		bool on = i == sel;
		SDL_Color ink = on ? ui_col(thm->on_accent, 255) : UI_TEXT_SOFT;
		const char *label = tiles[i].label ? tiles[i].label : "";
		const char *nl = strchr(label, '\n');
		ui_glyph g = tile_glyph(label);
		char one[64], two[64];
		TTF_Font *f = fm;
		int lf = lm;

		ui_round_rect(a->r, &t, DS_RADIUS_CARD, ui_col(on ? thm->accent : thm->raised, 255));
		ui_glyph_draw(a->r, g, t.x + t.w / 2, t.y + 56, 72, ink);
		snprintf(one, sizeof one, "%.*s", nl ? (int)(nl - label) : (int)strlen(label), label);
		snprintf(two, sizeof two, "%s", nl ? nl + 1 : "");
		/* The menu's size where both lines fit the tile, else the least
		 * there is: nothing is cut off (DESIGN.md 1) */
		if (ui_text_width(f, one) > t.w - 24 || ui_text_width(f, two) > t.w - 24) {
			f = fs;
			lf = ls;
		}
		{
			/* Two lines under the mark, the smaller size centered where
			 * the menu's two would be */
			int y = t.y + 104 + (lm - lf);

			ui_text(a->r, f, one, t.x + t.w / 2, ui_line_y(f == fs ? UI_F_META : UI_F_MENU, y),
			        0, ink);
			if (two[0])
				ui_text(a->r, f, two, t.x + t.w / 2,
				        ui_line_y(f == fs ? UI_F_META : UI_F_MENU, y + lf), 0, ink);
		}
	}
	/* What the focused one does, in **meta** **text-soft** under the tiles */
	if (what) {
		ui_fit_text(fs, what, fit, sizeof fit, panel.w - 2 * TILE_PAD);
		ui_text(a->r, fs, fit, panel.x + panel.w / 2, ui_line_y(UI_F_META, tiles_y + TILE_H + 24),
		        0, UI_TEXT_SOFT);
		tiles_y += 24 + ls;
	}
	ui_text(a->r, fs, legend, panel.x + panel.w / 2,
	        ui_line_y(UI_F_META, tiles_y + TILE_H + 24), 0, UI_TEXT_DIM);
	if (posed) panel_pass_end(a, &panel, pose);
}

/* A computer on the cable, asked once per plug over the shelf (usblink.h):
 * Charge Only, File Transfer, or on a development image Developer (SSH), as
 * three tiles side by side (DESIGN.md 11.18; my pick in the USB lab,
 * 2026-11-05), starting on Charge Only, his pick too, so a quick A keeps it
 * charging. Left/Right move, A chooses; B, MENU or the cable coming out
 * keep charging only. */
static void usb_ask(app *a)
{
	menu_row tiles[3];
	bool dev = usblink_dev_image();
	int n = 0, sel = 0, choice = -1, lv;
	float k;

	tiles[n++] = (menu_row){ T(S_XFER_TILE_CHARGE_ONLY), NULL, true };
	tiles[n++] = (menu_row){ T(S_XFER_TILE_FILE_TRANSFER), NULL, true };
	if (dev) tiles[n++] = (menu_row){ T(S_XFER_TILE_DEVELOPER), NULL, true };
	lv = panel_open();
	while (choice < 0) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { panel_close(a, lv, true); power_off(a); return; }
		if (menu_leaving(a)) choice = 0;
		if (plat_usb_peer() != USB_PEER_COMPUTER) choice = 0;   /* unplugged */
		if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_RIGHT)) {
			in_button b = in_repeat(&a->in, IN_LEFT) ? IN_LEFT : IN_RIGHT;

			moved_sound(a, b);
			sel = (sel + (b == IN_LEFT ? -1 : 1) + n) % n;
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			choice = sel;
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		panel_show(a, PANEL_TILES, T(S_XFER_COMPUTER_CONNECTED), tiles, n, sel, 0, MENU_ZYPEROS,
		           NULL, false, 0, NULL);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	if (choice == 1) {
		xfer_screen(a);
	} else if (choice == 2) {
		menu_row row = { T(S_XFER_DEV_LINK_ON), NULL, false };

		usblink_hold(true);
		timed_message(a, T(S_XFER_DEV_HEADING), &row, 1, 0, MENU_ZYPEROS);
	} else {
		usblink_hold(false);
	}
}

/* The File Transfer page's SteamGridDB key (hare_set_sgdb). What goes back
 * is whether there is one, its last four, what is known about it, whether a
 * check is running, and the sentence for the last thing done - never the key.
 * The check runs in sgdb.c's thread, and xfer_screen's loop settles it. */
static void up_jstr(char *out, size_t n, const char *s);   /* with the updates, below */

static char *xfer_sgdb(int op, const char *body, size_t n, size_t *len,
                       char *why, size_t wn)
{
	char key[SGDB_KEY_MAX * 4], tail[8], *json, jsaid[400];
	const char *said, *outcome;
	sgdb_try t = SGDB_TRY_IDLE;
	int k;

	if (op == HARE_SGDB_KEY) {
		size_t m = n < sizeof key - 1 ? n : sizeof key - 1;

		memcpy(key, body, m);
		key[m] = '\0';
		t = sgdb_try_begin(key);
		memset(key, 0, sizeof key);
	} else if (op == HARE_SGDB_REMOVE) {
		sgdb_key_forget();
	}
	if (op != HARE_SGDB_REMOVE) t = sgdb_try_poll();
	said = op == HARE_SGDB_REMOVE ? T(S_XFER_KEY_REMOVED) : sgdb_try_said();
	outcome = t == SGDB_TRY_WORKS ? "works" : t == SGDB_TRY_KEPT ? "kept"
	        : t == SGDB_TRY_REFUSED ? "refused" : t == SGDB_TRY_BAD ? "bad" : "";
	sgdb_key_tail(tail, sizeof tail);
	if (!(json = malloc(512))) {
		snprintf(why, wn, "%s", T(S_XFER_ERR_MEMORY));
		return NULL;
	}
	/* Every value here is fixed text or letters and digits, but the sentence,
	 * which is in the language that is on: escaped, and room for it */
	up_jstr(jsaid, sizeof jsaid, said ? said : "");
	k = snprintf(json, 512,
	             "{\"key\":%s,\"tail\":\"%s\",\"state\":\"%s\",\"checking\":%s,"
	             "\"outcome\":\"%s\",\"said\":%s}",
	             sgdb_have_key() ? "true" : "false", tail, sgdb_status(),
	             t == SGDB_TRY_RUNNING ? "true" : "false", outcome, jsaid);
	if (k < 0 || k >= 512) {
		free(json);
		snprintf(why, wn, "%s", T(S_XFER_ERR_COULD_NOT_SAY));
		return NULL;
	}
	*len = (size_t)k;
	return json;
}

/* ---- File Transfer, open or behind the shelf ----------------------------
 *
 * 2026-11-06: "maybe instead of closing the channel it should
 * minimize? so transfer continues while we can operate the zyperOS? if gaming
 * affects it, prohibit gaming while transfering." So the server is a session
 * of its own and its screen is a view of it (DESIGN.md 11.14, my picks
 * in the lab, 2026-11-06): B closes it when nothing moves, and while files
 * move it asks - keep going behind the shelf, stop after the file on its way,
 * or cancel now, where the half-written file is a .part file deleted with its
 * connection and never a broken game on the shelf. MENU, while files move,
 * keeps them going and goes to the shelf, as MENU goes there from anything.
 *
 * Behind the shelf every screen's loop drives it through tick_theme, the
 * shelf's own on every pass, and the corner's mark says it is on (status.h).
 * A game cannot run beside it - a game takes the loop that moves the files,
 * and a port restarts the launcher - so launch asks first (xs_before_game).
 * Half an hour with no signed-in browser asking anything, behind the shelf,
 * ends it, and so do ten wrong PINs: a PIN that waits on the network for days
 * is a PIN somebody can work at. */
#define XS_QUIET_MS   (30u * 60u * 1000u)
#define XS_LINK_MS    10000u            /* the cable gone this long ends it */
#define XS_SHELF_MS   3000u             /* the files still this long: rescan */
#define XS_RESTART_MS 1500u             /* the page's restart answered first */
#define XS_WRONG_MAX  10                /* wrong PINs that end it behind the shelf */
#define XS_LEGEND     T(S_PANEL_B_CLOSE)
#define XS_LEGEND_MOV T(S_XFER_LEGEND_MOVING)
#define XS_LEGEND_END T(S_XFER_LEGEND_HIDE)

typedef struct {
	bool        on;             /* the server is up */
	bool        shown;          /* its screen is open */
	bool        cable, hot;
	bool        hot_failed, ssh_shown;
	bool        rescan_due;     /* ended behind the shelf with the shelf changed */
	const char *name;
	char        pin[32], login[96], addr[96];
	unsigned long long in, out;
	hare_stats  st;
	hotspot_state hs;
	unsigned    addr_at, moved_at, link_lost_at, restart_at;
	int         busy;
	side_lights_batch lit;      /* files arriving, for the side lights */
} xfer_sess;
static xfer_sess g_xs;

static void xs_stop(app *a, bool panels);

/* ---- System Update over the cable (hare_set_update) ----------------------
 *
 * The page brings a release's files from the computer's internet, where the
 * handheld on the cable has none (docs/updates.md, "Over the USB cable"): the
 * signed list and the pack, and for a card with two system slots the
 * system's list and pack too. They wait in <userdata>/update/cable/, and the
 * update program reads them there as it reads any release (--source
 * file://), so nothing is trusted for having come over the cable: the list's
 * signature is checked against the image's own keys, and every file's
 * SHA-256 against the list. An image with no release key takes none, as
 * System Update on Wi-Fi does, and the page says so. */
static void up_cable_dir(char *buf, size_t n) { up_path(buf, n, "cable"); }

static const char *up_device(void)
{
	const char *slash = strrchr(P_USERDATA, '/');

	return slash ? slash + 1 : P_USERDATA;
}

/* A release's file for this device, by the names docs/updates.md gives them:
 * the two lists exactly, and a pack with its version in its name */
static bool up_cable_name_ok(const char *name)
{
	const char *dev = up_device();
	char want[96];
	size_t n = strlen(name), sn;
	const char *p;

	snprintf(want, sizeof want, "ZyperOS-%s-update.txt", dev);
	if (!strcmp(name, want)) return true;
	snprintf(want, sizeof want, "ZyperOS-%s-system.txt", dev);
	if (!strcmp(name, want)) return true;
	if (strncmp(name, "ZyperOS-v", 9) || !xfer_name_ok(name)) return false;
	for (p = name + 9; *p; p++)
		if (!isalnum((unsigned char)*p) && !strchr(".-_+", *p)) return false;
	snprintf(want, sizeof want, "-%s-update.zpk", dev);
	sn = strlen(want);
	if (n > 9 + sn && !strcmp(name + n - sn, want)) return true;
	snprintf(want, sizeof want, "-%s-system.zpk", dev);
	sn = strlen(want);
	return n > 9 + sn && !strcmp(name + n - sn, want);
}

/* The update the page brought over the cable, once the update program is done
 * with it (xfer_update): a release is a few hundred MB of the card */
static void up_cable_tidy(void)
{
	char dir[512], p[1024];
	struct dirent *e;
	DIR *d;

	if (up_running()) return;
	up_cable_dir(dir, sizeof dir);
	if (!(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p) remove(p);
	}
	closedir(d);
	rmdir(dir);
}

/* System Update's own rule (docs/updates.md, "Keys"): an image with no
 * /etc/zyperos/system, or no key to check a list with, takes no update. The
 * same overrides as the program's, for the checks and the VM. */
static bool up_supported(char *why, size_t wn)
{
	const char *keys = getenv("ZYPEROS_UPDATE_KEYS") ? getenv("ZYPEROS_UPDATE_KEYS")
	                 : "/etc/zyperos/update-keys";
	const char *sys = getenv("ZYPEROS_UPDATE_SYSTEM") ? getenv("ZYPEROS_UPDATE_SYSTEM")
	                : "/etc/zyperos/system";
	struct dirent *e;
	bool key = false;
	DIR *d;

	if (!up_have()) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_PROGRAM));
		return false;
	}
	if (access(sys, R_OK) != 0) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_SYSTEM_UPDATE));
		return false;
	}
	if ((d = opendir(keys))) {
		while (!key && (e = readdir(d))) {
			size_t n = strlen(e->d_name);

			key = n > 4 && !strcmp(e->d_name + n - 4, ".pem");
		}
		closedir(d);
	}
	if (!key) {
		snprintf(why, wn, "%s", T(S_SYSUPDATE_NO_RELEASE_KEY));
		return false;
	}
	return true;
}

/* Start the program on the files the page brought */
static void up_spawn_cable(const char *cmd)
{
	char prog[512], dev[64], dir[512], src[600];
	char *argv[] = { prog, (char *)cmd, "--card", (char *)P_CARD, "--device", dev,
	                 "--source", src, NULL };

	snprintf(prog, sizeof prog, "%s/zyperos-update", P_ROOT);
	snprintf(dev, sizeof dev, "%s", up_device());
	up_cable_dir(dir, sizeof dir);
	snprintf(src, sizeof src, "file://%s/", dir);
	fprintf(stderr, "update: %s, the files from the cable\n", cmd);
	plat_spawn_detached(argv, NULL, P_ROOT);
	g_up_read_at = 0;
}

/* Text for JSON: what the program says in its messages, escaped */
static void up_jstr(char *out, size_t n, const char *s)
{
	size_t o = 0;

	if (n < 3) { if (n) out[0] = '\0'; return; }
	out[o++] = '"';
	for (; *s && o + 7 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
		else if (c < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
		else out[o++] = (char)c;
	}
	out[o++] = '"';
	out[o] = '\0';
}

static const char *const SU_NAME[] = {
	"idle", "unsupported", "checking", "uptodate", "available", "needs-system",
	"copying", "downloading", "verifying", "installing", "ready", "stopped",
	"error", "battery", "space", "rollback", "too-big",
};

/* What the page shows: whether updates work here and why not, the versions,
 * the card's slots, what the program is doing, and the files waiting */
static char *up_state_json(size_t *len, char *why, size_t wn)
{
	const su_view *v = up_view(true);
	char sysv[64] = "", text[256] = "", dir[512], no[200] = "";
	char msg[400], res[200], files[900] = "[", cmd[1024] = "", nwhy[400];
	bool on = up_supported(no, sizeof no), slots;
	struct dirent *e;
	DIR *d;
	char *json;
	int k;
	FILE *f;

	if ((f = fopen(getenv("ZYPEROS_UPDATE_SYSTEM") ? getenv("ZYPEROS_UPDATE_SYSTEM")
	                                                : "/etc/zyperos/system", "r"))) {
		if (fgets(text, sizeof text, f)) sscanf(text, "version %63s", sysv);
		fclose(f);
	}
	if ((f = fopen("/proc/cmdline", "r"))) {
		if (!fgets(cmd, sizeof cmd, f)) cmd[0] = '\0';
		fclose(f);
	}
	slots = strstr(cmd, "zyperos.slot=") != NULL;
	up_cable_dir(dir, sizeof dir);
	if ((d = opendir(dir))) {
		while ((e = readdir(d))) {
			size_t used = strlen(files);

			if (!up_cable_name_ok(e->d_name)) continue;
			if (used + strlen(e->d_name) + 4 >= sizeof files) break;
			snprintf(files + used, sizeof files - used, "%s\"%s\"", used > 1 ? "," : "", e->d_name);
		}
		closedir(d);
	}
	strcat(files, "]");
	up_jstr(msg, sizeof msg, v->st.message);
	up_jstr(res, sizeof res, v->result);
	up_jstr(nwhy, sizeof nwhy, no);
	if (!(json = malloc(4096))) { snprintf(why, wn, "%s", T(S_XFER_ERR_MEMORY)); return NULL; }
	k = snprintf(json, 4096,
	             "{\"on\":%s,\"why\":%s,\"device\":\"%s\",\"installed\":\"%s\",\"system\":\"%s\","
	             "\"slots\":%s,\"running\":%s,\"state\":\"%s\",\"version\":\"%s\",\"kind\":\"%s\","
	             "\"download\":%llu,\"done\":%llu,\"total\":%llu,\"files_done\":%d,\"files_total\":%d,"
	             "\"message\":%s,\"result\":%s,\"time\":%lld,\"battery\":%d,\"charging\":%s,"
	             "\"files\":%s}",
	             on ? "true" : "false", nwhy, up_device(), v->installed, sysv,
	             slots ? "true" : "false", v->running ? "true" : "false",
	             (unsigned)v->st.state < sizeof SU_NAME / sizeof SU_NAME[0] ? SU_NAME[v->st.state] : "idle",
	             v->st.version, v->st.system ? "system" : "folder",
	             v->st.download, v->st.done, v->st.total, v->st.files_done, v->st.files_total,
	             msg, res, v->st.time, v->battery, v->charging ? "true" : "false", files);
	if (k < 0 || k >= 4096) {
		free(json);
		snprintf(why, wn, "%s", T(S_XFER_ERR_COULD_NOT_SAY));
		return NULL;
	}
	*len = (size_t)k;
	return json;
}

static char *up_dup(const char *s, size_t *len)
{
	char *p = strdup(s);

	if (p) *len = strlen(p);
	return p;
}

/* The page's update routes (hare_set_update). Every name it sends is checked
 * here, and every byte by the update program. */
static char *xfer_update(int op, const char *name, char *path, size_t pn,
                         size_t *len, char *why, size_t wn)
{
	char dir[512], list[700];

	switch (op) {
	case HARE_UPDATE_FILE:
		if (!up_supported(why, wn)) return NULL;
		if (up_running()) { snprintf(why, wn, "%s", T(S_SYSUPDATE_BUSY)); return NULL; }
		if (!up_cable_name_ok(name)) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NOT_UPDATE_FILE));
			return NULL;
		}
		up_path(dir, sizeof dir, "");
		mkdir(dir, 0755);
		up_cable_dir(dir, sizeof dir);
		mkdir(dir, 0755);
		if (snprintf(path, pn, "%s/%s", dir, name) >= (int)pn) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NAME_TOO_LONG));
			return NULL;
		}
		return up_dup("{}", len);
	case HARE_UPDATE_CHECK:
	case HARE_UPDATE_INSTALL:
		if (!up_supported(why, wn)) return NULL;
		if (up_running()) { snprintf(why, wn, "%s", T(S_SYSUPDATE_BUSY_ALREADY)); return NULL; }
		up_cable_dir(dir, sizeof dir);
		snprintf(list, sizeof list, "%s/ZyperOS-%s-update.txt", dir, up_device());
		if (access(list, R_OK) != 0) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_SEND_FILES_FIRST));
			return NULL;
		}
		up_spawn_cable(op == HARE_UPDATE_CHECK ? "check" : "download");
		return up_state_json(len, why, wn);
	case HARE_UPDATE_RESTART:
		if (up_view(true)->st.state != SU_READY) {
			snprintf(why, wn, "%s", T(S_SYSUPDATE_NONE_READY));
			return NULL;
		}
		g_xs.restart_at = plat_now_ms() ? plat_now_ms() : 1;
		return up_dup("{\"restarting\":true}", len);
	default:
		return up_state_json(len, why, wn);
	}
}


/* Files on their way: uploads and downloads the server is in the middle of */
static bool xs_moving(void)
{
	return g_xs.on && hare_inflight() > 0;
}

/* Every empty .media in the consoles' folders, gone: rmdir takes only an
 * empty folder. Box Art and the Scraper made one before each try, and the
 * card itself came with one in every console until mk/payload.sh stopped
 * (2026-11-06). As File Transfer ends, and once a start's shelf is up
 * (after_start_thread). */
static void media_tidy(void)
{
	DIR *d = opendir(P_ROMS);
	struct dirent *e;
	char p[LIB_PATH * 2];

	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s/.media", P_ROMS, e->d_name) < (int)sizeof p)
			rmdir(p);
	}
	if (d) closedir(d);
}

/* After a start's first frame, on a thread of their own: the card read again
 * against the shelves the saved library put up (lib_check, src/shelfbuild.h),
 * and media_tidy, which was before the first frame and is not wanted there:
 * each console's .media with box art in it is a folder the scan never reads,
 * and rmdir has to look inside it to find it is not empty, which after a cold
 * boot is a read off the card for each one, 150-195 ms on my card (35
 * folders, 2026-11-07). Both read the card, so both wait until the art the
 * first frame asked for is in (after_start_step). */
static struct {
	lib_check   *check;
	bool         begun, started;
	unsigned     at;
	SDL_atomic_t done;
} g_after;

static int after_start_thread(void *unused)
{
	(void)unused;
	if (g_after.check) lib_check_run(g_after.check);
	media_tidy();
	SDL_AtomicSet(&g_after.done, 1);
	return 0;
}

/* A shelf pass's share of it: begun at the first pass after the first frame,
 * started once texload is idle or 3 s on (art that never settles is no
 * reason to wait for ever), and the check applied here, on this thread, when
 * it is done. `drawn`: the first frame is up. */
static void after_start_step(app *a, bool drawn)
{
	unsigned now;

	if (!drawn) return;
	now = plat_now_ms();
	if (!g_after.begun) {
		g_after.begun = true;
		g_after.at = now;
		g_after.check = lib_check_begin();
		return;
	}
	if (!g_after.started) {
		SDL_Thread *t;

		if (!texload_idle() && now - g_after.at < 3000) return;
		g_after.started = true;
		t = SDL_CreateThread(after_start_thread, "zyperos-after", NULL);
		if (t) SDL_DetachThread(t);
		else after_start_thread(NULL);
		return;
	}
	if (g_after.check && SDL_AtomicGet(&g_after.done)) {
		if (lib_check_apply(a, g_after.check)) redraw_now();
		lib_check_free(g_after.check);
		g_after.check = NULL;
	}
}

/* Open the server: the hooks, the hotspot when there is no network, the PIN,
 * SSH over the cable. False when it could not, having said why. */
static bool xs_open(app *a, int way)
{
	const bool  cable = way < 0 ? plat_cable_link(NULL, 0) : way == 1;
	const char *name  = cable ? XFER_NAME_CABLE : XFER_NAME_WIFI;
	const bool  hot   = !cable && xfer_wants_hotspot();
	bool        due   = g_xs.rescan_due;
	char        ip[64], path[512];

	memset(&g_xs, 0, sizeof g_xs);
	g_xs.rescan_due = due;
	g_xs.cable = cable;
	g_xs.hot = hot;
	g_xs.name = name;
	g_xs.hs = HOTSPOT_OFF;
	g_logs_app = a;
	/* The link on, since USB charges only until asked (usblink.h); and a
	 * fresh one each time this opens, since a computer can drop it while the
	 * device believes it is still up (plat_cable_reconnect) */
	if (cable) {
		usblink_for_transfer();
		plat_cable_reconnect();
	}
	hare_set_logs(pack_logs);
	hare_set_before_delete(muse_before_delete);
	hare_set_after_rename(muse_after_rename);
	/* Box art fetched by the page is USB File Transfer's alone (hareart.h);
	 * a cover uploaded into a shelf's .media is shrunk on every device. */
	hareart_init(&a->sys, P_ROMS);
	singles_cover();                /* back, if it was taken off the card */
	if (cable) {
		hare_set_art(hareart_wanted, hareart_match, hareart_crc, hareart_nointro);
		hare_set_albums(albums_wanted, album_pick);
	} else {
		hare_set_art(NULL, NULL, NULL, NULL);
		hare_set_albums(NULL, NULL);
	}
	hare_set_after_write(xfer_after_write);
	/* The SteamGridDB key, pasted on the page: over Wi-Fi it is checked at
	 * once; over the cable, which has no internet, it is kept unchecked */
	hare_set_sgdb(xfer_sgdb);
	/* Updates through the computer, where the handheld has no internet of
	 * its own: over the cable (docs/updates.md) */
	hare_set_update(cable ? xfer_update : NULL);
	/* The page's name, the one this screen shows: USB File Transfer over
	 * the cable, Wi-Fi File Transfer over Wi-Fi and the hotspot */
	hare_set_cable(cable);
	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	g_xfer_sys_ok = cfg_load_systems(path, &g_xfer_sys);
	hare_set_folder_color(xfer_folder_color);
	hare_set_folder_icon(xfer_folder_icon);
	snprintf(path, sizeof path, "%s/system-icons", P_ROOT);
	hare_set_icons(path);
	if (hot) {
		wait_panel(a, name, T(S_XFER_STARTING_HOTSPOT));
		/* Up before the code is shown, so a phone that scans it at once can
		 * join: a second or two, and hotspot_poll gives up at fifteen */
		if (hotspot_start())
			while ((g_xs.hs = hotspot_poll()) == HOTSPOT_STARTING) SDL_Delay(50);
		if (g_xs.hs != HOTSPOT_UP) {
			menu_row row = { T(S_XFER_HOTSPOT_FAILED), NULL, false };

			hotspot_stop();
			timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
			return false;
		}
	}
	if (!hare_start(P_ROMS, P_CARD, P_SHARED, P_WEB)) {
		menu_row row = { T(S_XFER_COULD_NOT_START), NULL, false };

		if (hot) hotspot_stop();
		/* Over **scrim.menu**, as every message is (DESIGN.md 11.8). */
		timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
		return false;
	}
	/* Every name a phone looks up is the handheld on its own network, so a
	 * request for another host is its internet check (src/captive.h). */
	if (hot) {
		/* The redirect names the port too when the server is not on 80 (a
		 * phone's check still goes to 80, but a hand-typed address may not) */
		char self[32];

		if (hare_port() == 80) snprintf(self, sizeof self, "%s", hotspot_addr());
		else snprintf(self, sizeof self, "%s:%d", hotspot_addr(), hare_port());
		hare_set_captive(self);
	}
	snprintf(g_xs.pin, sizeof g_xs.pin, "%s", hare_pin());
	/* SSH over the same cable, behind the same PIN (sshlink.h) */
	if (cable && plat_cable_link(ip, sizeof ip)) {
		char keys[600];

		snprintf(keys, sizeof keys, "%s/ssh", P_USERDATA);
		snprintf(g_xs.login, sizeof g_xs.login, "root@%s", ip);
		sshlink_start(ip, g_xs.pin, keys);
	}
	g_xs.on = true;
	return true;
}

/* The server's slice of a frame, whichever screen is up: bounded, like
 * everything in a frame (httpd.h). */
static void xs_tick(app *a)
{
	unsigned now = plat_now_ms();
	char ssid[WIFI_SSID_MAX], ip[64];

	if (!g_xs.on) return;
	g_xs.busy = hare_poll();
	hare_status(&g_xs.st);
	/* A batch of files in: the side lights say so (src/sidelights.h) */
	if (side_lights_batch_done(&g_xs.lit, g_xs.st.uploads > 0, now))
		plat_side_lights_say(SIDE_LIGHTS_DONE);
	/* A key the page sent, settled the frame its check ends */
	sgdb_try_poll();
	if (g_xs.cable) {
		sshlink_note_web(hare_wrong_pins());
		sshlink_poll();
		/* The page shows the command only while SSH listens */
		if (g_xs.ssh_shown != sshlink_on()) {
			g_xs.ssh_shown = sshlink_on();
			hare_set_ssh(g_xs.ssh_shown ? g_xs.login : NULL);
		}
	}
	g_xs.in  += g_xs.st.in;
	g_xs.out += g_xs.st.out;
	if (g_xs.st.in || g_xs.st.out || hare_inflight() > 0) g_xs.moved_at = now ? now : 1;
	/* The hotspot's DNS answers and its start, every frame (bounded) */
	if (g_xs.hot && (g_xs.hs = hotspot_poll()) == HOTSPOT_FAILED) g_xs.hot_failed = true;

	/* Two seconds, because wifi_status forks wpa_cli and this runs every
	 * frame. Same cache the About screen uses. Over a cable the address
	 * never changes, so it is always shown, and the browser row says
	 * whether anyone is on the other end. */
	if (g_xs.hot) {
		/* Its own address, which never changes; port 80 needs no colon */
		if (hare_port() == 80) snprintf(g_xs.addr, sizeof g_xs.addr, "%s", hotspot_addr());
		else snprintf(g_xs.addr, sizeof g_xs.addr, "%s:%d", hotspot_addr(), hare_port());
	} else if (!g_xs.addr[0] || now - g_xs.addr_at > 2000u) {
		bool up = g_xs.cable ? plat_cable_link(ip, sizeof ip)
		        : wifi_status(ssid, sizeof ssid, ip, sizeof ip) == WIFI_CONNECTED && ip[0];

		g_xs.addr_at = now;
		if (up) {
			/* Port 80 needs no colon, and the address is being copied by
			 * hand off a three-inch screen - so it is only shown when it is
			 * not the one everybody assumes. */
			if (hare_port() == 80) snprintf(g_xs.addr, sizeof g_xs.addr, "%s", ip);
			else snprintf(g_xs.addr, sizeof g_xs.addr, "%s:%d", ip, hare_port());
			g_xs.link_lost_at = 0;
		} else {
			snprintf(g_xs.addr, sizeof g_xs.addr, "%s",
			         g_xs.cable ? T(S_XFER_CABLE_UNPLUGGED) : T(S_XFER_WIFI_GONE));
			if (!g_xs.link_lost_at) g_xs.link_lost_at = now ? now : 1;
		}
	}

	/* Auto Off must not shut the device down in the middle of a 900MB
	 * upload, and it also must not shut it down while somebody is reading
	 * a listing on the other side of the room. Bytes moving is obvious
	 * activity; a connected browser is a person at the other end of it,
	 * which is the same claim a button press makes and no weaker for
	 * arriving over a network. Neither suspends Auto Off - close the tab
	 * and the countdown resumes. */
	if (g_xs.busy || g_xs.st.clients > 0 || sshlink_sessions() > 0 ||
	    (g_xs.hot && hotspot_phones() > 0))
		a->idle.since_ms = now;

	/* The page asked for a restart to install an update (xfer_update): a
	 * moment for its answer to go out, then as System Update's Restart */
	if (g_xs.restart_at && now - g_xs.restart_at > XS_RESTART_MS) {
		g_xs.restart_at = 0;
		xfer_stop(g_xs.cable);
		g_xs.on = false;
		uisound_play(DS_SOUND_ACCEPT);
		remember_place(a);
		book_keep(true);
		plat_request_reboot();
		plat_backlight(false);
		a->running = false;
		return;
	}

	/* Behind the shelf: the stop the question asked for, once the files on
	 * their way are in; the hotspot gone; the cable out; and half an hour of
	 * nobody asking anything. On its screen, the screen does these. */
	if (!g_xs.shown) {
		bool quiet = hare_inflight() == 0 && sshlink_sessions() == 0 &&
		             !(g_xs.hot && hotspot_phones() > 0);

		/* Nobody is at the screen to read the PIN behind the shelf, so a
		 * run of wrong ones is somebody else guessing: it ends */
		bool guessed = hare_wrong_pins() >= XS_WRONG_MAX;

		if ((hare_closing() && hare_inflight() == 0) || g_xs.hot_failed || guessed ||
		    (g_xs.link_lost_at && now - g_xs.link_lost_at > XS_LINK_MS) ||
		    (quiet && hare_quiet_ms() > XS_QUIET_MS)) {
			fprintf(stderr, "xfer: closed behind the shelf (%s)\n",
			        hare_closing() ? "after the files" : g_xs.hot_failed ? "the hotspot stopped"
			        : guessed ? "wrong PINs" : g_xs.link_lost_at ? "the link went"
			        : "nobody asked for 30 min");
			xs_stop(a, false);
		}
	}
}

/* From tick_theme: behind the shelf only, since the screen drives it itself */
static void xs_tick_behind(app *a)
{
	if (g_xs.on && !g_xs.shown) xs_tick(a);
}

/* The shelf's turn: read again once the files that arrived behind it have
 * been still a moment, and once a session that ended behind it changed it.
 * Not as each file lands: a batch of twenty would rescan twenty times, and
 * every one would stall the loop that is still receiving. */
static bool xs_shelf_due(void)
{
	if (g_xs.rescan_due) {
		g_xs.rescan_due = false;
		return true;
	}
	if (g_xs.on && !g_xs.shown && hare_shelf_changed() && hare_inflight() == 0 &&
	    plat_now_ms() - g_xs.moved_at > XS_SHELF_MS) {
		hare_shelf_seen();
		return true;
	}
	return false;
}

static bool xs_row_cable(void) { return g_xs.cable; }

/* The corner's mark: on behind the shelf, and the ring as far as the
 * upload on its way (status.h). Not while its own screen is up. */
static void xs_mark(status_marks *m)
{
	m->xfer = g_xs.on && !g_xs.shown;
	m->xfer_moving = m->xfer && xs_moving();
	m->xfer_pct = m->xfer_moving && g_xs.st.up_total > 0
	            ? (int)(g_xs.st.up_got * 100 / g_xs.st.up_total) : 0;
}

/* MENU's row while it runs: what it is doing */
static const char *xs_row_value(void)
{
	if (!g_xs.on) return NULL;
	if (hare_closing()) return T(S_XFER_ROW_CLOSING);
	if (g_xs.st.uploads > 0) return T(S_XFER_ROW_RECEIVING);
	if (g_xs.st.sends > 0) return T(S_XFER_ROW_SENDING);
	return T(S_XFER_ROW_ON);
}

/* End it: the server, SSH, the hotspot; the empty .media its covers left;
 * and the shelf read again if anything on it moved - now, with `panels`, or
 * on the shelf's next pass (xs_shelf_due) when it ends behind another
 * screen, which a rescan must not change under. */
static void xs_stop(app *a, bool panels)
{
	bool changed;

	if (!g_xs.on) return;
	/* Asked before hare_stop, which is what owning the server means: after
	 * it, there is nobody left to ask. */
	changed = hare_shelf_changed();
	if (panels && g_xs.hot) {
		if (g_xs.hot_failed) {
			menu_row row = { T(S_XFER_HOTSPOT_STOPPED), NULL, false };

			timed_message(a, g_xs.name, &row, 1, 0, MENU_ZYPEROS);
		}
		/* The hotspot down and the Wi-Fi the player had back up, which
		 * takes the supplicant a second or two to answer (xfer_stop) */
		wait_panel(a, g_xs.name, T(S_XFER_CLOSING_HOTSPOT));
	}
	xfer_stop(g_xs.cable);
	g_xs.on = false;
	g_xs.shown = false;
	media_tidy();
	up_cable_tidy();
	/* The point of the whole feature is getting ROMs onto the card, and the
	 * shelf is scanned once at startup - so without this the file lands,
	 * the screen says it landed, and the game is not there. */
	if (changed) {
		if (panels) {
			wait_panel(a, g_xs.name, T(S_XFER_SCANNING));
			rescan_all(a);
		} else {
			g_xs.rescan_due = true;
		}
	}
}

/* The power going, or the launcher: whatever is open goes with it, the
 * hotspot too, or Wi-Fi stays off under a hostapd nobody stops */
static void xs_end(void)
{
	if (!g_xs.on) return;
	xfer_stop(g_xs.cable);
	g_xs.on = false;
	g_xs.shown = false;
}

/* The two questions' words (DESIGN.md 11.14): the tiles, two lines each, and
 * what each does, said under them while it is focused. The shot harness's
 * --xfer-ask draws these same ones. Each is three IDs, read through T() as
 * it is asked, so XS_STOP_L and the rest are arrays of strings as they were. */
static const str_id XS_WORDS[4][3] = {
	{ S_XFER_TILE_KEEP_GOING, S_XFER_TILE_STOP_AFTER, S_XFER_TILE_CANCEL_NOW },
	{ S_XFER_KEEP_GOING_SAYS, S_XFER_STOP_AFTER_SAYS, S_XFER_HALF_DELETED },
	{ S_XFER_TILE_NOT_NOW, S_XFER_TILE_PLAY_AFTER, S_XFER_TILE_CANCEL_PLAY },
	{ S_XFER_NOT_NOW_SAYS, S_XFER_PLAY_AFTER_SAYS, S_XFER_HALF_DELETED },
};

static const char *const *xs_words(int which)
{
	static const char *w[4][3];
	int i;

	for (i = 0; i < 3; i++) w[which][i] = T(XS_WORDS[which][i]);
	return w[which];
}
#define XS_STOP_L xs_words(0)
#define XS_STOP_W xs_words(1)
#define XS_STOP_LEGEND T(S_XFER_LEGEND_KEEP_GOING)
#define XS_GAME_L xs_words(2)
#define XS_GAME_W xs_words(3)
#define XS_GAME_LEGEND T(S_XFER_LEGEND_NOT_NOW)

/* A question while files move (DESIGN.md 11.14): three tiles over the shelf,
 * as Computer Connected's (My pick in the lab, 2026-11-06), a line under
 * the rule saying what moves, and under the tiles what the focused one does.
 * The server keeps moving the files all the while. Returns the choice, -1
 * for B, or -2 when the files finished while it was up and it no longer has
 * anything to ask about. `what` may hold one %s, the file on its way. */
static int xs_ask(app *a, const char *heading, const char *const *labels,
                  const char *const *what, int n, const char *legend)
{
	menu_row rows[7];
	char line[160], sub[192];
	int sel = 0, choice = -3, lv, i;
	float k;

	if (n > 4) n = 4;
	lv = panel_open();
	while (choice == -3) {
		xs_tick(a);
		if (!g_xs.on || !xs_moving()) { choice = -2; break; }
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; choice = -1; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			panel_close(a, lv, true);
			xs_end();
			power_off(a);
			return -1;
		}
		/* B: the first answer, which loses nothing; MENU: the same, and on
		 * to the shelf */
		if (menu_leaving(a)) { choice = -1; break; }
		if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_RIGHT)) {
			in_button b = in_repeat(&a->in, IN_LEFT) ? IN_LEFT : IN_RIGHT;

			moved_sound(a, b);
			sel = (sel + (b == IN_LEFT ? -1 : 1) + n) % n;
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			choice = sel;
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		for (i = 0; i < n; i++) rows[i] = (menu_row){ labels[i], NULL, true };
		if (g_xs.st.file[0])
			snprintf(sub, sizeof sub,
			         T(g_xs.st.uploads > 0 ? S_XFER_RECEIVING : S_XFER_SENDING), g_xs.st.file);
		else
			snprintf(sub, sizeof sub, "%s",
			         T(g_xs.st.uploads > 0 ? S_XFER_RECEIVING_A_FILE : S_XFER_SENDING_A_FILE));
		snprintf(line, sizeof line, what[sel],
		         g_xs.st.file[0] ? g_xs.st.file : T(S_XFER_THE_FILE));
		rows[n]     = MENU_NOTE(sub);
		rows[n + 1] = MENU_NOTE(line);
		rows[n + 2] = MENU_NOTE(legend);
		panel_show(a, PANEL_TILES, heading, rows, n + 3, sel, 0, MENU_ZYPEROS,
		           NULL, false, 0, NULL);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(4);
	}
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return choice;
}

/* B while files move: keep going behind the shelf, stop after the file on
 * its way, or cancel now. 0, 1, 2; -1 for B again, which keeps going too. */
static int xs_stop_ask(app *a)
{
	return xs_ask(a, T(S_XFER_STILL_MOVING), XS_STOP_L, XS_STOP_W, 3, XS_STOP_LEGEND);
}

/* A game asked for while File Transfer is on (My note: "if gaming affects it,
 * prohibit gaming while transfering"; it does - the game takes the loop that
 * moves the files, and a port restarts the launcher). With nothing moving,
 * it closes for the game and says so; with files on their way, the player
 * says: not now, after the file on its way, or cancel and play. True when
 * the game may start. */
static bool xs_before_game(app *a)
{
	const char *name = g_xs.name;
	int c;

	if (!g_xs.on) return true;
	c = xs_moving() ? xs_ask(a, T(S_XFER_STILL_MOVING), XS_GAME_L, XS_GAME_W, 3, XS_GAME_LEGEND)
	                 : -2;
	if (!a->running) return false;
	if (c == 0 || c == -1) return false;
	if (c == 1) {
		/* After the file on its way: the page is told nothing new starts,
		 * and this waits for it, B going back with it still to close */
		menu_row rows[3];
		char line[192];
		int lv = panel_open();
		float k;

		hare_close_after();
		while (g_xs.on && xs_moving()) {
			xs_tick(a);
			plat_input_poll(&a->in);
			ui_sounds_follow();
			if (a->in.quit_requested) { a->running = false; break; }
			if (menu_leaving(a)) break;
			if (!panel_frame(a, lv, &k)) continue;
			tick_tint(a);
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
			if (g_xs.st.file[0])
				snprintf(line, sizeof line, T(S_XFER_FINISHING), g_xs.st.file);
			else
				snprintf(line, sizeof line, "%s", T(S_XFER_FINISHING_THE_FILE));
			rows[0] = MENU_NOTE(line);
			rows[1] = MENU_RULE;
			rows[2] = MENU_NOTE(T(S_XFER_B_BACK_TO_SHELF));
			menu_draw(a, name, rows, 3, -1, menu_std_width(a), MENU_ZYPEROS);
			plat_draw_osd(a->r);
			plat_present();
			ui_frame_rest(4);
		}
		panel_close(a, lv, !a->running);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		if (!a->running || xs_moving()) return false;
		xs_stop(a, false);
		return true;
	}
	/* Cancel and play (2), or nothing was moving (-2): closed for the game,
	 * the shelf read again after it if anything arrived */
	xs_stop(a, false);
	if (c == -2) {
		menu_row row = { T(S_XFER_CLOSED_FOR_GAME), NULL, false };

		timed_message(a, name, &row, 1, 0, MENU_ZYPEROS);
	}
	return true;
}

/* The way the device has now: over the cable when it links, else over the
 * air, as MENU's row says - or the one already open */
static void xfer_screen(app *a)
{
	xfer_screen_way(a, -1);
}

/* `way` 1 over the cable, 0 over the air, -1 whichever the device has: the
 * empty home starts the one its button names (src/emptyhome.h). Already on,
 * behind the shelf, it is that one, shown again with the same PIN. */
static void xfer_screen_way(app *a, int way)
{
	char       who[64], moved[64], head[192], now_line[160];
	menu_row   rows[7];
	int        nrows, lv;
	unsigned   cover_at = 0, cover_next = 0;   /* the embedded covers, below */
	bool       done = false, stop = false;
	float      k;

	if (!g_xs.on && !xs_open(a, way)) return;
	g_xs.shown = true;
	lv = panel_open();

	while (!done && !want_quit && a->running && g_xs.on) {
		unsigned now = plat_now_ms();

		/* The launcher's slice of the transfer. Bounded, like everything else
		 * in a frame - see httpd.h. */
		xs_tick(a);
		if (!a->running || !g_xs.on) break;

		/* Every album's embedded cover copied out to .media, where the page
		 * can show it and Album Art can judge it: an album the shelf never
		 * drew had none there, so the page showed nothing and asked
		 * MusicBrainz over a cover the files already carried (Eric,
		 * 2026-10-05). cover_file asks Muse as the shelf does, one album
		 * each tenth of a second, again for any Muse has not answered. */
		muse_poll();
		cover_answers();
		muse_file_albums();             /* read again on the way out, if moved */
		if (g_cov && g_muse.nalbums > 0 && now - cover_at >= 100u) {
			char pic[LIB_PATH * 2 + 8];

			int al = (int)(cover_next++ % (unsigned)g_muse.nalbums);

			cover_at = now;
			cover_recheck(al);
			cover_file(al, pic, sizeof pic);
		}

		if (g_xs.st.clients == 0) snprintf(who, sizeof who, "%s", T(S_XFER_WAITING));
		else snprintf(who, sizeof who, TN(S_XFER_CONNECTED, g_xs.st.clients), g_xs.st.clients);

		if (g_xs.in || g_xs.out) {
			char hin[24], hout[24];

			human_bytes(hin,  sizeof hin,  g_xs.in);
			human_bytes(hout, sizeof hout, g_xs.out);
			snprintf(moved, sizeof moved, "%s / %s", hin, hout);
		} else {
			snprintf(moved, sizeof moved, "%s", T(S_XFER_NOTHING_YET));
		}
		/* Closing after the file on its way: the Now row says which */
		if (hare_closing() && g_xs.st.file[0])
			snprintf(now_line, sizeof now_line, T(S_XFER_CLOSING_AFTER), g_xs.st.file);
		else if (hare_closing())
			snprintf(now_line, sizeof now_line, "%s", T(S_XFER_CLOSING_AFTER_THIS_FILE));
		else
			snprintf(now_line, sizeof now_line, "%s",
			         g_xs.st.last[0] ? g_xs.st.last : T(S_XFER_WAITING));

		hare_head(head, sizeof head, g_xs.addr, g_xs.pin);
		nrows = hare_rows(rows, who, g_xs.cable ? sshlink_status() : NULL, moved, now_line,
		                  hare_closing() ? XS_LEGEND_END : xs_moving() ? XS_LEGEND_MOV : XS_LEGEND);

		plat_input_poll(&a->in);
		ui_sounds_follow();
		/* The launcher stopping: the hotspot too (xfer_stop), or Wi-Fi stays
		 * off under a hostapd nobody stops until the next transfer screen */
		if (a->in.quit_requested) { xs_end(); a->running = false; return; }

		/* Powering off stops the hotspot as every way out does (xfer_stop):
		 * on the hotspot, a second or two for the supplicant to come back */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			xs_end();
			power_off(a);
			return;
		}
		/* The files the question let finish are in: closed */
		if (hare_closing() && hare_inflight() == 0) { stop = true; done = true; }
		/* B: closed when nothing moves; while files move, the question.
		 * MENU keeps them moving and goes to the shelf. Once it is closing
		 * after the file on its way, B hides it and it closes behind the
		 * shelf when that file is in. */
		if (!done && menu_leaving(a)) {
			done = true;
			if (g_menu_closing || hare_closing()) {
				/* hidden, still running */
			} else if (!xs_moving()) {
				stop = true;
			} else {
				int c = xs_stop_ask(a);

				if (!a->running || !g_xs.on) return;
				if (c == 1) { hare_close_after(); done = false; }  /* the screen closes it */
				else if (c == 2 || c == -2) stop = true;
				/* 0 or B: keep going, hidden */
			}
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		if (g_xs.hot) hot_show(a, g_xs.name, g_xs.pin, g_xs.hs == HOTSPOT_STARTING, now_line,
		                       hare_closing() ? XS_LEGEND_END : xs_moving() ? XS_LEGEND_MOV : XS_LEGEND);
		else menu_draw(a, head, rows, nrows, -1, menu_std_width(a), MENU_ZYPEROS);
		plat_draw_osd(a->r);
		plat_present();
		/* Shorter than the usual 8ms: this loop is also the server's, and a
		 * transfer moves POLL_BUDGET per pass. */
		ui_frame_rest(4);
	}
	panel_close(a, lv, false);
	g_xs.shown = false;
	/* The hotspot failing ends it as Y does; anything else that ended the
	 * loop left it running behind the shelf */
	if (g_xs.on && (stop || g_xs.hot_failed)) xs_stop(a, true);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* Box art, from libretro. See artscrape.h for the matching rule and why it is
 * that one.
 *
 * Starts as soon as the screen opens rather than asking again. The player has
 * already pressed A on a row called Box Art; a confirmation would be a second
 * question with the same answer. It is safe to start because it is safe to
 * stop: existing art is kept, and known titles need no metadata request.
 * A stopped run can continue with the next request tomorrow.
 *
 * Deliberately NOT automatic. It is a network request on someone's behalf,
 * and a launcher that quietly fetches two hundred images the first time a
 * card is inserted has made that decision for them. */
/* The heading and rows, in one place so a shot cannot picture a screen that
 * does not exist - the lesson from --hare, where the fixture claimed a byte
 * count the code could not produce. */
static void art_head(char *out, size_t n, const char *now)
{
	char fit[128];

	/* Trimmed to fit. A ROM name is as long as somebody's dump of it -
	 * "Legend of Zelda, The - A Link to the Past (USA)" and worse - and this
	 * is a heading, which menu_draw centers rather than wraps, so a long one
	 * ran off both sides of the panel. Three quarters of the screen leaves
	 * the panel a margin it can keep. */
	ui_fit_text(ui_font(UI_F_LABEL), now, fit, sizeof fit,
	            ZYPEROS_SCREEN_W * 3 / 4);
	snprintf(out, n, "%s\n%s", T(S_SCRAPER_BOX_ART), fit);
}

static int art_rows(menu_row *out, const char *where, const char *counts,
                    bool working)
{
	out[0] = (menu_row){ T(S_SCRAPER_SYSTEMS), where,  false };
	out[1] = (menu_row){ T(S_SCRAPER_ART),     counts, false };
	out[2] = (menu_row){ working ? T(S_PANEL_B_STOP) : T(S_PANEL_B_CLOSE), NULL, false };
	return 3;
}

static void art_preview(app *a, const char *now, const char *where,
                        const char *counts, bool working)
{
	menu_row rows[3];
	char head[192];
	int n = art_rows(rows, where, counts, working);

	art_head(head, sizeof head, now);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS);
}

/* game_info and gi_rows: src/game_menu.h */

static void gi_gather(app *a, int owner, const game_entry *g, game_info *gi)
{
	const system_cfg *s = &a->sys.systems[owner];
	char p[LIB_PATH * 3];
	struct stat st;

	memset(gi, 0, sizeof *gi);
	gi->can_delete = gd_shelf_deletes(s->core);

	/* The save slots are not read here any more. The row that showed them went
	 * on 2026-09-17 - the carousel shows them when you load, with the frames
	 * rather than a count - and with it went a stat of every slot plus the
	 * autosave each time this screen opened. */
	box_art_path(s->folder, g->name, p, sizeof p);
	gi->has_art = (stat(p, &st) == 0 && st.st_size > 0);

	/* Loading the set clobbers whatever set is loaded, which is nobody's
	 * during shelf browsing - no game is running. It stays loaded after this
	 * returns, which is what lets the Cheevos row open the list: the same set
	 * this count was taken from is the one that screen reads. */
	chv_path(P_ROMS, s->folder, g->name, p, sizeof p);
	gi->has_cheevos = chv_load(p) && chv_count() > 0;
	/* Its badges, behind the screen, if the card lacks them */
	if (gi->has_cheevos) rb_fetch_loaded();
	/* The count alone, as DESIGN.md 12 writes one and as the in-game menu's
	 * Cheevos row has it (src/game_menu.c): the row opens the list, whose
	 * heading has the points. "3 / 36 · 25 / 415 points" beside "Cheevos"
	 * is wider in Lexend than the room the std width leaves a value, and
	 * would be cut with an ellipsis whenever the row is not selected. */
	if (gi->has_cheevos)
		snprintf(gi->cheevos, sizeof gi->cheevos, "%d / %d",
		         chv_earned(), chv_count());
	else
		snprintf(gi->cheevos, sizeof gi->cheevos, "%s", T(S_CHEEVOS_NONE));

	/* What a scrape left on the card, if anything. Read here with the rest
	 * rather than when the synopsis card opens: this is one indexed row, the
	 * screen already reads the save directory and the achievement set, and a
	 * row that appears a frame after the panel does reads as a glitch. */
	{
		game_meta m;

		if (db_game_get(db_lib(), s->folder, g->file, &m)) {
			gi->scraped = true;
			gi->has_synopsis = m.synopsis[0] != '\0';
			snprintf(gi->year, sizeof gi->year, "%.4s", m.year);
			/* The scrape's own English names, in its own order - but
			 * joined with a comma AND A SPACE, which it does not do. That is
			 * typesetting rather than an opinion about somebody else's
			 * taxonomy: "Platform,Shoot'em Up" reads as one word with a
			 * stumble in it. One in three games has a second genre. */
			{
				const char *g = m.genres;
				size_t k = 0;

				while (*g && k + 2 < sizeof gi->genre) {
					gi->genre[k++] = *g;
					if (*g++ == ',' && *g != ' ') gi->genre[k++] = ' ';
				}
				gi->genre[k] = '\0';
			}
		}
	}
}

/* Refresh in place, including copies on Favorites. Sort only after saving
 * each shelf's selected file and owner; titles are never selection keys. */
static void refresh_game_titles(app *a)
{
	int si, i;

	for (si = 0; si < a->sys.count; si++) {
		sysview *v = &a->view[si];
		char file[LIB_PATH];
		int owner, selected;
		bool changed = false;

		if (is_muse(&a->sys.systems[si]) || v->list.count <= 0) continue;
		owner = shelf_owner(a, si, v->cursor);
		snprintf(file, sizeof file, "%s", v->list.items[v->cursor].file);
		for (i = 0; i < v->list.count; i++) {
			int o = shelf_owner(a, si, i);
			if (lib_refresh_title(db_lib(), a->sys.systems[o].folder, &v->list.items[i]))
				changed = true;
		}
		if (!changed) continue;
		free_view_textures(v);
		sort_shelf(a, si);
		selected = lib_find_entry(&v->list, v->owner, owner, file);
		if (selected >= 0) v->cursor = selected;
		cf_reset(&v->cf, v->cursor);
	}
}

/* `only` names one system's folder, or NULL for the whole library. Says how
 * it went, for Zyper in the top menu: MENU_CHANGED when the run was seen
 * through to its end with nothing to report, MENU_FAILED when there was no
 * network or the run ended on a problem, MENU_STAY when it was left early.
 *
 * One system is worth having for the FIRST run on a card, where it is the
 * difference between twenty images and a hundred and eighty. It is worth much
 * less afterwards: a full card with stored names needs no network requests.
 * A card with covers but missing names asks only for the missing metadata. */
static menu_result art_run(app *a, const char *only, const char *one,
                           const char *stem, const char *one_file, unsigned accent,
                           int lv)
{
	menu_row     rows[3];
	art_progress p;
	char         counts[160], where[64], head[192];
	int          nrows = 3;
	bool         done = false, working = true;
	float        k;
	/* Whether the one game already had a cover, so a miss can say it kept it
	 * rather than read as though the cover went. */
	bool         had = one && stem && only && has_box_art(only, stem);

	if (!net_online()) {
		menu_row row = { MSG_NEEDS_WIFI, NULL, false };

		timed_message(a, T(S_SCRAPER_BOX_ART), &row, 1, 0, accent);
		return MENU_FAILED;
	}

	/* The scraper needs the one async slot, and an account answer from a game
	 * just quit may still be holding it. */
	sync_abandon();

	/* SCREENSCRAPER FIRST, FOR ONE GAME. That is the settled order: them
	 * when the player has an account, libretro when they do not and libretro
	 * again when they cannot answer - a miss, a refusal, or a name that does
	 * not survive the check.
	 *
	 * Only the single-game path for now. The bulk run is the same idea over
	 * 1,708 games and a different conversation about time: measured at 5.5
	 * seconds a game it is an overnight job, where libretro fetches one index
	 * per system and then only what is missing.
	 *
	 * It brings the text with it. A cover fetched without the year and the
	 * synopsis would mean asking again later for what was in hand now. */
	if (one && stem && only && ss_signed_in()) {
		const system_cfg *sys = NULL;
		char dir[LIB_PATH * 2];
		int i;

		for (i = 0; i < a->sys.count; i++)
			if (!strcmp(a->sys.systems[i].folder, only)) { sys = &a->sys.systems[i]; break; }
		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, only);
		if (sys && ss_run_begin(only, one_file, stem, dir, sys->exts)) {
			bool ss_working = true;

			while (ss_working && !want_quit && a->running) {
				int st = ss_run_step();

				plat_input_poll(&a->in);
				ui_sounds_follow();
				if (a->in.quit_requested) { ss_run_cancel(); a->running = false; return MENU_STAY; }
				if (a->in.pressed[IN_POWER] || idle_due(a)) {
					ss_run_cancel();
					power_off(a);
					return MENU_STAY;
				}
				if (menu_leaving(a)) {
					ss_run_cancel();
					plat_input_flush();
					memset(&a->in, 0, sizeof a->in);
					return MENU_STAY;
				}
				if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
				if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
				if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
				if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

				if (st == 0) {
					/* Got it. Say so on the same panel the libretro run
					 * uses, so the two sources look like one feature. */
					menu_row row[2];

					art_head(head, sizeof head, one);
					row[0] = (menu_row){ T(S_SCRAPER_BOX_ART),
					                     T(had ? S_SCRAPER_REPLACED : S_SCRAPER_FOUND), false };
					row[1] = (menu_row){ T(S_PANEL_B_CLOSE), NULL, false };
					timed_message(a, head, row, 2, menu_std_width(a), accent);
					ss_run_cancel();
					free_all_textures(a);
					return MENU_CHANGED;
				}
				if (st < 0) { ss_run_cancel(); ss_working = false; break; }

				art_head(head, sizeof head, one);
				rows[0] = (menu_row){ T(S_SCRAPER_BOX_ART), ss_run_where(), false };
				rows[1] = (menu_row){ T(S_PANEL_B_STOP), NULL, false };
				if (!panel_frame(a, lv, &k)) continue;
				tick_tint(a);
				tick_theme(a);
				draw_shelf(a);
				ui_scrim_k(a->r, 0, k);
				menu_draw(a, head, rows, 2, -1, menu_std_width(a), accent);
				plat_draw_osd(a->r);
				plat_present();
				ui_frame_rest(8);
			}
			if (!a->running || want_quit) return MENU_STAY;
		}
	}

	if (only) {
		/* A one-entry library rather than a filter inside artscrape.c. The
		 * scraper already takes the list it should work on; handing it a
		 * shorter list is the same operation, and it keeps "which systems"
		 * a question the caller answers. */
		static systems_cfg one;
		int i;

		one.count = 0;
		for (i = 0; i < a->sys.count; i++)
			if (!strcmp(a->sys.systems[i].folder, only)) {
				one.systems[0] = a->sys.systems[i];
				one.count = 1;
				break;
			}
		art_begin(&one, P_ROMS, stem);
	} else {
		art_begin(&a->sys, P_ROMS, NULL);
	}

	while (!done && !want_quit && a->running) {
		/* One step per frame. A step is one request STARTED or one poll of
		 * the request already running - never a wait for one, because this
		 * loop is where the power button is read. */
		if (working && art_step() == 0) {
			working = false;
			plat_side_lights_say(SIDE_LIGHTS_DONE);
		}
		art_status(&p);

		{
			/* Three facts, each a count of its own, between " · " */
			char fo[40], mi[40], al[40];

			snprintf(fo, sizeof fo, TN(S_SCRAPER_FOUND_N, p.found), p.found);
			snprintf(mi, sizeof mi, TN(S_SCRAPER_MISSING_N, p.missing), p.missing);
			snprintf(al, sizeof al, TN(S_SCRAPER_ALREADY_N, p.skipped), p.skipped);
			snprintf(counts, sizeof counts, "%s · %s · %s", fo, mi, al);
		}
		if (p.named) {
			char nm[40];
			size_t used = strlen(counts);

			snprintf(nm, sizeof nm, TN(S_SCRAPER_NAMES_N, p.named), p.named);
			snprintf(counts + used, sizeof counts - used, " · %s", nm);
		}
		snprintf(where, sizeof where, "%d / %d", p.systems_done, p.systems);
		if (one) {
			/* Asked about one game, answer about one game: the run is that
			 * game alone (art_begin's `only`), and "1 found, 19 already" would
			 * be a true statement about work the player did not ask for. */
			art_head(head, sizeof head, one);
			rows[0] = (menu_row){ T(S_SCRAPER_BOX_ART),
			                      working ? T(S_SCRAPER_FETCHING)
			                      : p.found ? T(had ? S_SCRAPER_REPLACED : S_SCRAPER_FOUND)
			                      : had ? T(S_SCRAPER_NOT_FOUND_KEPT)
			                            : T(S_SCRAPER_NOT_FOUND), false };
			rows[1] = (menu_row){ working ? T(S_PANEL_B_STOP) : T(S_PANEL_B_CLOSE),
			                      NULL, false };
			nrows = 2;
		} else {
			/* Running: what it is doing. Finished: what went wrong, or
			 * that it is done - never the last game it happened to fetch,
			 * which is what this said before and reads as still working
			 * on it. */
			art_head(head, sizeof head,
			         working ? (p.now[0] ? p.now : T(S_SCRAPER_STARTING))
			         : p.problem[0] ? p.problem : T(S_SCRAPER_DONE));
			nrows = art_rows(rows, where, counts, working);
		}

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { art_cancel(); a->running = false; return MENU_STAY; }

		/* A fetch in flight is not somebody in the room, so it does not hold
		 * the idle clock the way a connected browser does in Wi-Fi File Transfer -
		 * there, a person was at the other end. Here the device is talking to
		 * itself, and a library left scraping unattended on battery is
		 * exactly what Auto Off is for. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			art_cancel();
			power_off(a);
			return MENU_STAY;
		}
		if (menu_leaving(a)) {
			art_cancel();
			done = true;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw(a, head, rows, nrows, -1, menu_std_width(a), accent);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}

	art_cancel();
	/* The cards on the shelf are textures already uploaded from whatever art
	 * existed when the shelf was built. New art on the card changes nothing
	 * until they are dropped and read again. */
	free_all_textures(a);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* Finished, as against stopped: the run's own word on how it went */
	if (working) return MENU_STAY;
	return p.problem[0] ? MENU_FAILED : MENU_CHANGED;
}

/* The run above as one screen with a panel, its every way out a close; and
 * one exit for title refresh, including a canceled cover, a miss that falls
 * through to libretro, and a partial bulk run. Do not sort while the run
 * still holds pointers to the selected entry's strings: art_run has returned
 * here, and the shelf the panel leaves over shows the new names. */
static menu_result art_screen(app *a, const char *only, const char *one,
                              const char *stem, const char *one_file, unsigned accent)
{
	int lv = panel_open();
	menu_result r = art_run(a, only, one, stem, one_file, accent, lv);

	refresh_game_titles(a);
	panel_close(a, lv, !a->running);
	return r;
}

/* ---------- MENU > Scraper ------------------------------------------------
 *
 * Box Art, its three sources and Video Snaps, behind one MENU row: my
 * picks in the Scraper lab, 2026-11-06 (DESIGN.md 11.19). One list: Fetch
 * Missing Art and Video Snaps, a rule, then ScreenScraper, libretro and
 * SteamGridDB in the order a run asks them, each opening a page of its own.
 *
 * THE RUN GOES ON IN THIS SAME PANEL. Fetch Missing Art steps the scraper
 * from the build, one art_step a frame as art_run does, so the panel that
 * was asked is the one that answers: its first row becomes the progress and
 * each source counts what it finds. B stops it and closes the screen, as
 * Box Art's B always has. art_step touches neither the renderer nor the
 * event loop, which is all a build may not do. */


typedef struct {
	app        *a;
	scraper_ui  u;
	char        ss_name[SS_USER_MAX], sgdb[32], snaps[48], head[192];
	menu_result said;
} scraper_ctx;

/* Video Snaps' value: on or off and the room they take */
static unsigned long long snaps_total(app *a)
{
	unsigned long long sum = 0;
	int i;

	for (i = 0; i < a->sys.count; i++)
		if (a->sys.systems[i].folder[0] && !is_muse(&a->sys.systems[i]))
			sum += snaps_bytes(P_ROMS, a->sys.systems[i].folder);
	return sum;
}

static void scraper_snaps_value(scraper_ctx *c)
{
	char sz[24];

	sys_menu_bytes(snaps_total(c->a), sz, sizeof sz);
	snprintf(c->snaps, sizeof c->snaps, "%s · %s",
	         snaps_fetch() || snaps_play() ? T(S_SCRAPER_ON) : T(S_SCRAPER_OFF), sz);
	c->u.snaps = c->snaps;
}

static void scraper_count(scraper_ctx *c)
{
	c->u.missing = art_count_missing(&c->a->sys, P_ROMS);
	if (c->u.missing == 0)
		snprintf(g_scraper_val, sizeof g_scraper_val, "%s", T(S_SCRAPER_COMPLETE));
	else snprintf(g_scraper_val, sizeof g_scraper_val, TN(S_SCRAPER_MISSING_N, c->u.missing),
	              c->u.missing);
}

static int scraper_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static scraper_bufs b;
	scraper_ctx *c = ctx;
	sc_row ids[SC_ROWS];
	char fit[128];

	(void)max;
	/* What can change under the screen: a link, a key the page sent */
	sgdb_try_poll();
	c->u.net = menu_wifi(NULL, 0) == WIFI_CONNECTED;
	c->u.ss_have = ss_have_dev();
	c->u.ss_in = ss_signed_in();
	snprintf(c->ss_name, sizeof c->ss_name, "%s", ss_user());
	c->u.ss_name = c->ss_name;
	sgdb_key_masked(c->sgdb, sizeof c->sgdb);
	c->u.sgdb = !sgdb_have_key() ? NULL
	          : sgdb_key_state() == SGDB_REFUSED ? T(S_SCRAPER_KEY_REFUSED) : c->sgdb;

	*heading = T(S_SCRAPER_HEADING);
	if (c->u.running) {
		art_progress p;
		int r = art_step();

		art_status(&p);
		c->u.done = p.found + p.missing;
		if (c->u.done > c->u.total) c->u.total = c->u.done;
		c->u.ss_found = p.ss_found;
		c->u.lr_found = p.lr_found;
		c->u.sg_found = p.sg_found;
		c->u.not_found = p.missing;
		if (r != 1) {
			/* Over: the counts stay, the shelf takes the new art; the side
			 * lights say so when it ran to its end */
			if (r == 0 && !p.problem[0]) plat_side_lights_say(SIDE_LIGHTS_DONE);
			c->u.running = false;
			c->u.ran = true;
			c->said = p.problem[0] ? MENU_FAILED : MENU_CHANGED;
			art_cancel();
			free_all_textures(c->a);
			scraper_count(c);
			scraper_snaps_value(c);
			if (p.problem[0]) {
				ui_fit_text(ui_font(UI_F_LABEL), p.problem, fit, sizeof fit,
				            ZYPEROS_SCREEN_W * 3 / 4);
				snprintf(c->head, sizeof c->head, "%s\n%s", T(S_SCRAPER_HEADING), fit);
				*heading = c->head;
			}
		}
	} else if (c->u.ran && c->head[0]) {
		*heading = c->head;
	}
	return sys_menu_scraper_rows(&c->u, &b, rows, ids);
}

static menu_result ss_page(app *a);
static menu_result snaps_screen(app *a);

static menu_result scraper_key(app *a, void *ctx, in_button key, int sel)
{
	scraper_ctx *c = ctx;
	scraper_bufs b;
	menu_row rows[SC_ROWS];
	sc_row ids[SC_ROWS];
	menu_result r = MENU_STAY;
	int n;

	if (key != IN_ACCEPT || c->u.running) return MENU_STAY;
	n = sys_menu_scraper_rows(&c->u, &b, rows, ids);
	if (sel < 0 || sel >= n || !rows[sel].live) return MENU_STAY;
	switch (ids[sel]) {
	case SC_FETCH:
		if (!net_online()) {
			result_message(a, T(S_SCRAPER_HEADING), MSG_NEEDS_WIFI);
			return MENU_FAILED;
		}
		/* The one async slot, which an account answer may still hold */
		sync_abandon();
		art_begin(&a->sys, P_ROMS, NULL);
		c->u.running = true;
		c->u.ran = false;
		c->u.total = c->u.missing > 0 ? c->u.missing : 0;
		c->u.done = c->u.ss_found = c->u.lr_found = c->u.sg_found = 0;
		c->u.not_found = 0;
		c->head[0] = '\0';
		return MENU_STAY;
	case SC_SNAPS:  r = snaps_screen(a); scraper_snaps_value(c); break;
	case SC_SS:     r = ss_page(a); break;
	case SC_SGDB:   r = sgdb_screen(a); break;
	default: break;
	}
	return r;
}

static menu_result scraper_screen(app *a)
{
	scraper_ctx c;

	memset(&c, 0, sizeof c);
	c.a = a;
	c.said = MENU_STAY;
	scraper_count(&c);
	scraper_snaps_value(&c);
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a) },
	         scraper_build, scraper_key, &c);
	/* Left while it ran: B, MENU, the power button or Auto Off */
	if (c.u.running) {
		art_cancel();
		free_all_textures(a);
	}
	refresh_game_titles(a);
	return c.said;
}

/* ScreenScraper's page: the account, whether it works, Unlink. Linking is
 * the sign-in it has always had (ss_signin_screen): user, password, a check. */
static int ss_page_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static char status[48];
	ssp_row ids[SSP_ROWS];

	(void)ctx;
	(void)max;
	*heading = "ScreenScraper";
	if (ss_run_left() >= 0)
		snprintf(status, sizeof status, TN(S_SCRAPER_SS_LEFT_TODAY, ss_run_left()),
		         ss_run_left());
	else snprintf(status, sizeof status, "%s", T(S_SCRAPER_SS_LINKED));
	return sys_menu_ss_rows(ss_have_dev(), ss_signed_in() ? ss_user() : NULL, status,
	                        rows, ids);
}

static menu_result ss_page_key(app *a, void *ctx, in_button key, int sel)
{
	menu_result *said = ctx, r = MENU_STAY;
	menu_row rows[SSP_ROWS];
	ssp_row ids[SSP_ROWS];
	int n;

	if (key != IN_ACCEPT) return MENU_STAY;
	n = sys_menu_ss_rows(ss_have_dev(), ss_signed_in() ? ss_user() : NULL, NULL, rows, ids);
	if (sel < 0 || sel >= n) return MENU_STAY;
	if (ids[sel] == SSP_ACCOUNT) {
		r = ss_signin_screen(a);
	} else if (ids[sel] == SSP_UNLINK &&
	           confirm_panel(a, "ScreenScraper", T(S_SCRAPER_UNLINK_ACCOUNT_ASK),
	                         T(S_SCRAPER_UNLINK))) {
		/* The password is wiped from memory and from the device's settings */
		ss_creds_clear();
		ss_creds_save();
		result_message(a, "ScreenScraper", T(S_SCRAPER_UNLINKED));
		r = MENU_CHANGED;
	}
	if (r == MENU_CHANGED || r == MENU_FAILED) *said = r;
	return r;
}

static menu_result ss_page(app *a)
{
	menu_result said = MENU_STAY;

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a) },
	         ss_page_build, ss_page_key, &said);
	return said;
}

/* The Video Snaps page: the three switches, then every console a run could
 * fetch snaps for (ScreenScraper has an id for it) with the room its snaps
 * take, and Delete All. The sizes are read when the page opens. */
typedef struct {
	app          *a;
	snaps_console c[CFG_MAX_SYSTEMS];
	char          folder[CFG_MAX_SYSTEMS][CFG_STR];
	int           nc;
	unsigned long long total;
	unsigned char sound[SN_ROWS];      /* each row's sound class, by place */
	menu_result   said;
} snaps_ctx;

static void snaps_read(snaps_ctx *s)
{
	app *a = s->a;
	int i;

	s->nc = 0;
	s->total = 0;
	for (i = 0; i < a->sys.count && s->nc < CFG_MAX_SYSTEMS; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (!sc->folder[0] || is_muse(sc) || !ss_system_id(sc->folder)) continue;
		snprintf(s->folder[s->nc], sizeof s->folder[0], "%s", sc->folder);
		s->c[s->nc].name = sc->name;
		s->c[s->nc].on = snaps_console_on(sc->folder);
		s->c[s->nc].bytes = snaps_bytes(P_ROMS, sc->folder);
		s->total += s->c[s->nc].bytes;
		s->nc++;
	}
}

static int snaps_rows_now(snaps_ctx *s, menu_row *rows, sn_row *ids, int *con)
{
	static snaps_bufs b;
	snaps_ui u = { .fetch = snaps_fetch(), .play = snaps_play(), .ss_in = ss_signed_in(),
	               .ss_have = ss_have_dev(),
	               .sound = snaps_sound(), .c = s->c, .nc = s->nc, .total = s->total };

	return sys_menu_snaps_rows(&u, &b, rows, ids, con);
}

static int snaps_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	snaps_ctx *s = ctx;
	sn_row ids[SN_ROWS];
	int con[SN_ROWS], n, i;

	(void)max;
	*heading = T(S_SNAPS_HEADING);
	n = snaps_rows_now(s, rows, ids, con);
	/* Every switch steps round with Left and Right, and is heard so */
	for (i = 0; i < n; i++)
		s->sound[i] = ids[i] == SN_DELETE || ids[i] == SN_NONE ? 0 : MENU_SND_RING;
	return n;
}

static menu_result snaps_key(app *a, void *ctx, in_button key, int sel)
{
	snaps_ctx *s = ctx;
	menu_row rows[SN_ROWS];
	sn_row ids[SN_ROWS];
	int con[SN_ROWS], n, d = key == IN_LEFT ? -1 : key == IN_RIGHT ? 1 : key == IN_ACCEPT ? 1 : 0;

	n = snaps_rows_now(s, rows, ids, con);
	if (!d || sel < 0 || sel >= n || !rows[sel].live) return MENU_STAY;
	switch (ids[sel]) {
	case SN_FETCH: snaps_set_fetch(!snaps_fetch()); break;
	case SN_PLAY:  snaps_set_play(!snaps_play()); break;
	case SN_SOUND: snaps_set_sound((snaps_sound() + d + 3) % 3); break;
	case SN_CONSOLE:
		if (con[sel] >= 0) {
			s->c[con[sel]].on = !s->c[con[sel]].on;
			snaps_set_console(s->folder[con[sel]], s->c[con[sel]].on);
		}
		break;
	case SN_DELETE: {
		char sz[24], msg[160];
		unsigned long long gone = 0;
		int i;

		if (key != IN_ACCEPT) return MENU_STAY;
		sys_menu_bytes(s->total, sz, sizeof sz);
		snprintf(msg, sizeof msg, T(S_SNAPS_DELETE_ASK), sz);
		if (!confirm_panel(a, T(S_SNAPS_HEADING), msg, T(S_SNAPS_DELETE))) return MENU_STAY;
		for (i = 0; i < s->nc; i++) gone += snaps_delete(P_ROMS, s->folder[i]);
		snaps_read(s);
		sys_menu_bytes(gone, sz, sizeof sz);
		snprintf(msg, sizeof msg, T(S_SNAPS_GIVEN_BACK), sz);
		result_message(a, T(S_SNAPS_HEADING), msg);
		s->said = MENU_CHANGED;
		return MENU_CHANGED;
	}
	default: return MENU_STAY;
	}
	s->said = MENU_CHANGED;
	return MENU_STAY;
}

static menu_result snaps_screen(app *a)
{
	static snaps_ctx s;

	memset(&s, 0, sizeof s);
	s.a = a;
	s.said = MENU_STAY;
	snaps_read(&s);
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = s.sound },
	         snaps_build, snaps_key, &s);
	return s.said;
}

/* MENU > Side Lights: a switch for each part of what the lights show
 * (src/sidelights.h, sys_menu_lights_rows). Each change goes to the card at
 * once and the gauge reads it again, so the lights follow the row. */
static int lights_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	unsigned char *sound = ctx;
	side_light_part ids[LP_COUNT];
	int n, i;

	(void)max;
	*heading = T(S_PANEL_SIDE_LIGHTS);
	n = sys_menu_lights_rows(plat_side_lights(), rows, ids);
	for (i = 0; i < n; i++) sound[i] = MENU_SND_RING;
	return n;
}

static menu_result lights_key(app *a, void *ctx, in_button key, int sel)
{
	menu_row rows[LP_COUNT];
	side_light_part ids[LP_COUNT];
	side_lights on = plat_side_lights();
	int n = sys_menu_lights_rows(on, rows, ids);

	(void)a; (void)ctx;
	if (key != IN_LEFT && key != IN_RIGHT && key != IN_ACCEPT) return MENU_STAY;
	if (sel < 0 || sel >= n) return MENU_STAY;
	plat_side_lights_set(side_lights_flip(on, ids[sel]));
	return MENU_STAY;
}

/* A language on from the next frame: its words (src/i18n.h) and its faces
 * (src/ui.h), every panel measured again for them, and kept: the setting,
 * and the boot screen's words (tick_boot_theme) */
static void language_apply(app *a, lang l, bool keep)
{
	i18n_set(l);
	ui_fonts_for(l);
	ui_text_memo_clear();
	a->menu_w = 0;
	/* The one shelf whose name is a word and not a system's: Favorites,
	 * named when the shelves were built (src/shelfbuild.c) */
	if (a->sys.count > 0 && !strcmp(a->sys.systems[0].tag, "FAV"))
		snprintf(a->sys.systems[0].name, sizeof a->sys.systems[0].name, "%s",
		         T(S_LIBRARY_FAVORITES));
	if (!keep) return;
	db_set_str(db_dev(), "language", lang_code(l));
	boot_theme_changed();
	fprintf(stderr, "language: %s\n", lang_code(l));
}

/* MENU > Language (DESIGN.md 11.30; my pick in the languages lab,
 * 2026-11-07): the five, each in its own words and its own face, a tick on
 * the one that is on and the cursor on it. A chooses it and goes back to the
 * menu, which is drawn in it from the next frame; B leaves it as it was. */
static int language_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	unsigned char *sound = ctx;
	int i, on = (int)i18n_lang();

	*heading = T(S_MENU_LANGUAGE);
	for (i = 0; i < LANG_COUNT && i < max; i++) {
		/* Each name in its own face: ui_text draws Chinese in the Chinese
		 * face whatever is on (src/ui.h) */
		rows[i] = (menu_row){ lang_name((lang)i), i == on ? MENU_TICK : NULL, true };
		sound[i] = 0;
	}
	return i;
}

static menu_result language_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	if (key != IN_ACCEPT || sel < 0 || sel >= LANG_COUNT) return MENU_STAY;
	if ((lang)sel != i18n_lang()) language_apply(a, (lang)sel, true);
	return MENU_DONE;
}

static void language_screen(app *a)
{
	static unsigned char sound[LANG_COUNT];

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = sound, .start_sel = (int)i18n_lang() },
	         language_build, language_key, sound);
}

static void lights_screen(app *a)
{
	static unsigned char sound[LP_COUNT];

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a), .sound = sound },
	         lights_build, lights_key, sound);
}

/* One game, and the two things you can do to it from here.
 *
 * X on the shelf, because Y is already favorite and the pair reads as a
 * unit - look at this one, or mark it. Nothing here is reachable during a
 * game: the in-game menu is about the session, this is about the file.
 *
 * A still, not a live view. Everything is read when the screen opens and
 * again after an action changes something, rather than every frame - these
 * are numbers that only move when the player moves them, and the alternative
 * is a stat storm at 120 Hz for a panel nobody is interacting with. */
/* The info screen's context. owner, g and v all move when a favorite is
 * toggled, so they live here where the key handler can put them back. */
typedef struct {
	app        *a;
	sysview    *v;
	game_entry *g;
	int         owner;
	game_info   gi;
	menu_style  st;   /* the accent follows the OWNING system, which can move */
	/* Delete Game took the game: the page closes, and this is said after */
	bool        deleted;
	char        said[192];
} info_ctx;

static int info_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	info_ctx *c = ctx;
	int n;

	(void)max;
	*heading = c->g->title;
	/* menu_wifi, not wifi_status. This loop runs every frame and wifi_status
	 * forks wpa_cli out of a 119 MB process; this screen was doing it about
	 * 125 times a second for a row that says "needs Wi-Fi". The system menu
	 * was measured at 12 fps for the same mistake. */
	n = gi_rows(rows, &c->gi, menu_wifi(NULL, 0) == WIFI_CONNECTED);
	return n > max ? max : n;
}

/* Defined with the achievement card, which is where wrap_text lives and which
 * this is the same gesture as: a row that opens into something to read. */
static void synopsis_screen(app *a, const char *title, const char *text,
                            unsigned accent);

/* Delete Game, below with the wrap it needs (DESIGN.md 11.31) */
static bool game_delete(app *a, int owner, const game_entry *g, char *said, size_t n);

/* The achievement list, which the in-game menu opens over a paused frame and
 * this screen opens over the shelf. `over_shelf` says which, rather than the
 * absence of a frame saying it: in-game, a capture that failed also leaves no
 * frame, and drawing the shelf under a paused game would be a lie about where
 * the player is. */
static void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf);

static menu_result info_key(app *a, void *ctx, in_button key, int sel)
{
	info_ctx *c = ctx;
	menu_row  rows[GI_MAX];
	const char *heading;
	int n = info_build(c, rows, GI_MAX, &heading);

	/* X closes it, the same button that opened it. */
	if (key == IN_X) { uisound_play(DS_SOUND_BACK); return MENU_DONE; }
	if (key != IN_ACCEPT || sel >= n || !rows[sel].live) return MENU_STAY;

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_SYNOPSIS))) {
		game_meta m;

		/* Read again here rather than carried in game_info: it is up to 4 KB
		 * of prose for a row that is usually not opened, and the gatherer runs
		 * on every action this screen takes. */
		if (db_game_get(db_lib(), a->sys.systems[c->owner].folder, c->g->file, &m)
		    && m.synopsis[0])
			synopsis_screen(a, c->g->title, m.synopsis, c->st.accent);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_SYSMENU_CHEEVOS))) {
		/* The set the gatherer loaded to count this row is still the loaded
		 * one, so the list has what it needs without reloading it. Over the
		 * shelf rather than a paused frame: no game is running here. */
		cheevos_screen(a, NULL, true);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_DELETE))) {
		/* Gone: this page describes a game that is not there, so it closes
		 * onto the shelf as it is now, and the message follows it. Still
		 * here (Cancel, a BIOS, a refusal): the page stays, as it was. */
		c->said[0] = '\0';
		if (game_delete(a, c->owner, c->g, c->said, sizeof c->said)) {
			c->deleted = true;
			return MENU_DONE;
		}
		if (c->said[0]) result_message(a, T(S_GAMEMENU_DELETE), c->said);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	{
		/* Replace, or get: the ordinary scrape, run over this one game and
		 * nothing else. Nothing is deleted first. It used to be - remove, then
		 * fill in - and a game libretro has no art for lost the cover it had,
		 * which for a translation, homebrew or a hand-added cover is one the
		 * scraper can never bring back. The new one is renamed over the old
		 * only once it has arrived whole. */
		art_screen(a, a->sys.systems[c->owner].folder, c->g->title, c->g->name,
		           c->g->file, a->sys.systems[c->owner].accent);
		/* The title sort moved entries, but retained this shelf's selection. */
		c->g = &c->v->list.items[c->v->cursor];
		c->owner = shelf_owner(a, a->sys_cursor, c->v->cursor);
	}
	gi_gather(a, c->owner, c->g, &c->gi);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return MENU_STAY;
}

static void game_info_screen(app *a)
{
	info_ctx c = { 0 };

	c.a = a;
	c.v = &a->view[a->sys_cursor];
	if (c.v->list.count == 0) return;
	c.owner = shelf_owner(a, a->sys_cursor, c.v->cursor);
	c.g = &c.v->list.items[c.v->cursor];
	gi_gather(a, c.owner, c.g, &c.gi);
	c.st.accent = a->sys.systems[c.owner].accent;
	/* Without this the panel sized itself to its own content, and its heading
	 * is a GAME TITLE - so it drew 976px wide beside every other menu's 800.
	 * info_preview next door was given the shared width and this was not,
	 * which is the same one-of-two-paths miss as every other bug of the day. */
	c.st.fixed_w = menu_std_width(a);

	menu_run(a, &c.st, info_build, info_key, &c);

	/* The card may now hold art it did not before. */
	free_all_textures(a);
	if (c.deleted && c.said[0] && a->running)
		result_message(a, T(S_GAMEMENU_DELETE), c.said);
}

/* One frame of it, from the real gatherer where possible: the fixture is the
 * game actually under the cursor, so a shot is a picture of this library
 * rather than of numbers somebody typed. */
static void info_preview(app *a, bool net, bool on_delete)
{
	sysview   *v = &a->view[a->sys_cursor];
	menu_row   rows[GI_MAX];
	game_info  gi;
	int        owner, n, sel;

	if (v->list.count == 0) return;
	owner = shelf_owner(a, a->sys_cursor, v->cursor);
	gi_gather(a, owner, &v->list.items[v->cursor], &gi);
	n = gi_rows(rows, &gi, net);
	/* Where the screen actually opens, asked the way menu_run_body asks it
	 * rather than hardcoded. It was a literal 5, which was the Box Art action
	 * until the rows were reordered and then was Saves - so the shot showed a
	 * highlight on a dead row, which the real screen cannot do. */
	sel = rows[0].live ? 0 : menu_step_sel(rows, n, 0, +1);
	/* --info-delete: the cursor on Delete Game, by its label as info_key
	 * finds it */
	for (int i = 0; on_delete && i < n; i++)
		if (!strcmp(rows[i].label, T(S_GAMEMENU_DELETE))) sel = i;

	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, v->list.items[v->cursor].title, rows, n, sel, menu_std_width(a),
	          a->sys.systems[owner].accent);
}

/* The approved Details layout: system information, then storage and the
 * battery controller's capacity estimate. The same rows serve --about shots. */
static int menu_measure(const menu_row *rows, int n, const char *heading);

/* wrap_text, syn_layout and their sizes are further down, with the synopsis */
#define CHV_WRAP_COLS  96
#define SYN_WRAP_LINES 80
/* The blank lines and the rule between the two copies of a synopsis */
#define SYN_LOOP_GAP 5
#define SYN_ROWS (SYN_WRAP_LINES * 2 + SYN_LOOP_GAP)
static int syn_layout(const char *text, int fixed,
                      char (*lines)[CHV_WRAP_COLS], menu_row *rows,
                      int *loop_at);

/* ---- Delete Game (DESIGN.md 11.31, src/gamedel.h) -------------------------
 *
 * The details page's last row. A asks first, in the confirm panel every
 * destructive row in the launcher asks with (confirm_rows): the question,
 * then Delete and Cancel, the cursor on Cancel, B the same as Cancel. The
 * question is a paragraph, which no other confirm has needed: it names the
 * game, says what goes and that it cannot be undone, so it is wrapped to the
 * panel and read left to right, a BODY row a line, in text-soft.
 *
 * Nothing is read or deleted until the yes: no work per frame, and the
 * listing of the other games and the plan are built once, on that press,
 * under one frame that says so (11.8). */
#define DEL_ASK_LINES 12

static int wrap_text(TTF_Font *f, const char *src, int w,
                     char out[][CHV_WRAP_COLS], int max);

/* How many of the question's lines fit over its two answers before the
 * panel windows (menu_list_fit's arithmetic): a window would leave the top
 * of the question where no cursor can go */
static int delete_ask_fit(bool *rule)
{
	int pad = menu_pad();
	int note_h = ui_font_line(UI_F_MENU) + pad / 3;
	int avail = MENU_AVAIL_H - (ui_font_line(UI_F_LABEL) + pad + pad / 2) - pad
	          - 2 * menu_row_h();
	int n = note_h > 0 && avail > note_h ? avail / note_h : 1;

	/* And whether a rule still fits under that many (menu_panel's rule_h) */
	*rule = avail - n * note_h >= pad / 2 + 2;
	return n;
}

/* The question's rows and its heading, shared by the screen and
 * --delete-ask; `rows` holds DEL_ASK_LINES + 2, for the answers
 * confirm_rows puts after them. Lines centered, as the one-line question of
 * every other confirm is, and quiet as it is (a dead row): the answers are
 * what the eye goes to. A rule between the two when there is room, the
 * heading's own (MENU_HR), so four lines of question do not read on into
 * Delete.
 *
 * A name too long for the question to fit names the game in the heading
 * instead, as the details page under it does, and the question says "this
 * game": a name may slide in a heading, a question may not be cut. */
static int delete_ask_rows(app *a, const char *title, char (*lines)[CHV_WRAP_COLS],
                           menu_row *rows, const char **heading)
{
	char q[LIB_NAME + 512];
	bool rule;
	int n, i, fit = delete_ask_fit(&rule), w = menu_std_width(a);
	TTF_Font *f = ui_font(UI_F_MENU);

	snprintf(q, sizeof q, T(S_GAMEMENU_DELETE_ASK), title);
	n = wrap_text(f, q, w, lines, DEL_ASK_LINES);
	*heading = T(S_GAMEMENU_DELETE);
	if (n > fit) {
		n = wrap_text(f, T(S_GAMEMENU_DELETE_ASK_THIS), w, lines, DEL_ASK_LINES);
		*heading = title;
	}
	for (i = 0; i < n; i++)
		rows[i] = (menu_row){ lines[i], MENU_NOTE_MARK, false };
	if (n < fit || rule) rows[n++] = MENU_HR;
	return n;
}

/* One frame of the question over the shelf, Cancel focused, for a shot */
static void delete_ask_preview(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
	menu_row rows[DEL_ASK_LINES + 2];
	const char *heading;
	int nq;

	if (v->list.count == 0) return;
	nq = delete_ask_rows(a, v->list.items[v->cursor].title, lines, rows, &heading);
	rows[nq]     = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
	rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, heading, rows, nq + 2, nq + 1, menu_std_width(a), MENU_ZYPEROS);
}

/* The yes: the plan, held against every other game on the shelves, run, the
 * rows forgotten and the shelf put right in place. True when the game went;
 * `said` is the message either way, empty for none. `wait` draws the frame
 * that says it is happening (not in a shot). */
static bool game_delete_now(app *a, int owner, const game_entry *g, bool wait,
                            char *said, size_t n)
{
	const system_cfg *s = &a->sys.systems[owner];
	char tag[sizeof s->tag], folder[CFG_STR], core[CFG_STR], exts[CFG_STR];
	char file[LIB_PATH], name[LIB_NAME], card_saves[CFG_STR * 2], card_states[CFG_STR * 2];
	gd_card card;
	gd_game gg;
	gd_other *others = NULL;
	gd_plan *plan;
	gd_done done;
	int no = 0, cap = 0, i, k;

	/* Copies: the shelf's entries move once the game is off it */
	snprintf(tag, sizeof tag, "%s", s->tag);
	snprintf(folder, sizeof folder, "%s", s->folder);
	snprintf(core, sizeof core, "%s", s->core);
	snprintf(exts, sizeof exts, "%s", s->exts);
	snprintf(file, sizeof file, "%s", g->file);
	snprintf(name, sizeof name, "%s", g->name);
	snprintf(card_saves, sizeof card_saves, "%s/Saves", P_CARD);
	snprintf(card_states, sizeof card_states, "%s/.zyperos", P_SHARED);
	said[0] = '\0';
	if (wait) wait_panel(a, T(S_GAMEMENU_DELETE), T(S_GAMEMENU_DELETING));

	/* Every other game on the card's own shelves - not Favorites', whose
	 * games are on them too, and not MP4's albums */
	for (i = 0; i < a->sys.count; i++)
		if (!a->view[i].owner && !is_muse(&a->sys.systems[i])) cap += a->view[i].list.count;
	if (cap > 0 && !(others = malloc((size_t)cap * sizeof *others))) cap = 0;
	for (i = 0; i < a->sys.count && others; i++) {
		const system_cfg *o = &a->sys.systems[i];
		bool hooked = lib_shelf_hooked(o->exts);

		if (a->view[i].owner || is_muse(o) || !o->folder[0]) continue;
		for (k = 0; k < a->view[i].list.count && no < cap; k++) {
			const game_entry *e = &a->view[i].list.items[k];

			if (!strcmp(o->folder, folder) && !strcmp(e->file, file)) continue;
			others[no++] = (gd_other){ o->folder, e->file, e->name, hooked };
		}
	}

	card = (gd_card){ P_ROMS, card_saves, card_states };
	gg = (gd_game){ folder, tag, core, exts, file, name };
	if (!(plan = malloc(sizeof *plan)) || (cap > 0 && !others)) {
		free(others);
		free(plan);
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_FAILED));
		return false;
	}
	if (!gd_plan_make(&card, &gg, others, no, plan)) {
		fprintf(stderr, "delete: %s %s refused (%s): %s\n", tag, file,
		        plan->why == GD_BIOS ? "a BIOS set" : plan->why == GD_OUTSIDE
		        ? "outside its folders" : "too many paths", plan->bad);
		if (plan->why == GD_BIOS) snprintf(said, n, T(S_GAMEMENU_DELETE_BIOS), name);
		else snprintf(said, n, "%s", plan->why == GD_OUTSIDE ? T(S_GAMEMENU_DELETE_OUTSIDE)
		                                                     : T(S_GAMEMENU_DELETE_FAILED));
		free(others);
		free(plan);
		return false;
	}
	free(others);

	/* Its video may be the one playing on the shelf behind the panel */
	snap_off();
	gd_plan_run(plan, &done);
	/* What went, and what would not: the paths with nothing there are
	 * the rest of the plan, and say nothing */
	for (i = 0; i < plan->n; i++)
		if (done.went[i] != GD_NOT_THERE)
			fprintf(stderr, "delete: %s %s %s\n", done.went[i] == GD_WENT ? "deleted" : "KEPT, would not go:",
			        plan->kind[i] == GD_FILE ? "file" : "folder", plan->path[i]);
	fprintf(stderr, "delete: %s %s: %d files and %d folders, %llu bytes, %d still there, "
	        "%d shared and kept\n", tag, file, done.files, done.folders, done.bytes,
	        done.failed, plan->kept);
	free(plan);
	if (!done.game_gone) {
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_FAILED));
		return false;
	}
	fprintf(stderr, "delete: %d library rows\n", gd_forget(folder, tag, file));
	shelf_drop_game(a, tag, file);
	if (done.failed) {
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_PARTLY));
	} else {
		char sz[24];

		sys_menu_bytes(done.bytes, sz, sizeof sz);
		snprintf(said, n, T(S_GAMEMENU_DELETED), sz);
	}
	return true;
}

/* A on Delete Game: a BIOS set says why it stays; anything else asks first.
 * True when the game went (game_delete_now). */
static bool game_delete(app *a, int owner, const game_entry *g, char *said, size_t n)
{
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
	menu_row rows[DEL_ASK_LINES + 2];
	const char *heading;
	int nq, lv;
	bool yes;

	said[0] = '\0';
	if (gd_is_bios(g->file)) {
		snprintf(said, n, T(S_GAMEMENU_DELETE_BIOS), g->name);
		return false;
	}
	nq = delete_ask_rows(a, g->title, lines, rows, &heading);
	lv = panel_open();
	yes = confirm_rows(a, heading, rows, nq, T(S_GAMEMENU_DELETE_YES), menu_std_width(a), lv);
	panel_close(a, lv, !a->running);
	if (!yes || !a->running) return false;
	return game_delete_now(a, owner, g, true, said, n);
}

/* Six pages: the system, its details, the name, and three of thanks (
 * 2026-11-06: "another page ... telling the story of the name that it's a
 * tribute to my dog that passed away, and a thank you page to the Open
 * source repos we got stuff"). The story is the README's own words, laid
 * out as a synopsis is (syn_layout): taller than the panel, it walks. */
#define ABOUT_PAGES 6
/* Zyper's photo on The name's page, at the body's right, the story wrapped
 * in the column left of it (2026-11-06: "can we put a photo of him in
 * that section?"). res/zyper.png, beside the launcher on the card: 300 x 400
 * with radius.card's corners already cut, and no camera data. */
#define ABOUT_PHOTO_W 300
#define ABOUT_PHOTO_H 400
#define ABOUT_PHOTO_GAP 40
#define ABOUT_THANKS_PAGES 3
#define ABOUT_THANKS_ROWS 6
#define ABOUT_THANKS_LAST 4
/* The README's Credits, a page at a time: who, and what of theirs ZyperOS
 * runs on, short enough that no row has to scroll. The last page has four
 * and the note that every license itself is in Licenses/ on the card. A
 * name is the same in every language and is written here; a word is an ID,
 * its name NULL, read through T() as About opens (about_pages). */
typedef struct {
	const char *who;
	str_id      who_s;
	const char *what;
	str_id      what_s;
} about_thanks;
static const about_thanks ABOUT_THANKS[ABOUT_THANKS_PAGES][ABOUT_THANKS_ROWS] = {
	{ { "Eric Reinsmidt", 0, "TortOS, Diatom", 0 },
	  { "plastron", 0, NULL, S_ABOUT_THANKS_PLASTRON },
	  { "ROCKNIX", 0, NULL, S_ABOUT_THANKS_ROCKNIX },
	  { "Buildroot", 0, NULL, S_ABOUT_THANKS_BUILDROOT },
	  { "libretro", 0, NULL, S_ABOUT_THANKS_LIBRETRO },
	  { "PortMaster", 0, NULL, S_ABOUT_THANKS_PORTMASTER } },
	{ { "plorpOS", 0, "DSperate, FAKE-08", 0 },
	  { "OpenBOR Team", 0, "OpenBOR", 0 },
	  { "BenJelter", 0, NULL, S_ABOUT_THANKS_BENJELTER },
	  { NULL, S_ABOUT_THANKS_MENU_MUSIC, "OpenGameArt", 0 },
	  { "Lexend", 0, NULL, S_ABOUT_THANKS_LEXEND },
	  { "SDL, Mesa", 0, NULL, S_ABOUT_THANKS_SDL } },
	{ { "FFmpeg, SQLite", 0, NULL, S_ABOUT_THANKS_FFMPEG },
	  { "minimp3, stb_vorbis", 0, NULL, S_ABOUT_THANKS_MINIMP3 },
	  { "Linux, U-Boot", 0, NULL, S_ABOUT_THANKS_LINUX },
	  { "Temurin, DejaVu", 0, NULL, S_ABOUT_THANKS_TEMURIN } },
};

typedef struct {
	char ver[48], addr[80], batt[32], up[48];
	menu_row system[4], details[ABOUT_DETAIL_ROWS];
	char story_lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
	menu_row story[SYN_ROWS], thanks[ABOUT_THANKS_PAGES][ABOUT_THANKS_ROWS + 2];
	int nstory, story_loop;
	int story_scroll;        /* pixels the story is scrolled by, by hand */
	SDL_Texture *photo;
	about_info info;
	about_values values;
	bool primed;
	unsigned refreshed;
	int width;
} about_view;

/* The story laid out for the panel, and the thanks pages, the last with its
 * note; once, when About opens: neither changes while it is open */
static void about_pages(app *a, about_view *v)
{
	char path[CFG_STR * 2];
	int i, p, wrap = menu_std_width(a);

	/* No photo (a host shot's card, say) is the story at the full width */
	snprintf(path, sizeof path, "%s/zyper.png", P_ROOT);
	if (!v->photo && (v->photo = IMG_LoadTexture(a->r, path)))
		SDL_SetTextureBlendMode(v->photo, SDL_BLENDMODE_BLEND);
	if (v->photo) wrap -= ABOUT_PHOTO_W + ABOUT_PHOTO_GAP;
	/* Laid out once, not twice: it holds still and is walked by hand */
	v->nstory = syn_layout(T(S_ABOUT_STORY), wrap, v->story_lines, v->story, &v->story_loop);
	if (v->story_loop > 0) v->nstory = v->story_loop - SYN_LOOP_GAP;
	v->story_loop = 0;
	for (p = 0; p < ABOUT_THANKS_PAGES; p++) {
		int rows = p < ABOUT_THANKS_PAGES - 1 ? ABOUT_THANKS_ROWS : ABOUT_THANKS_LAST;

		for (i = 0; i < rows; i++) {
			const about_thanks *t = &ABOUT_THANKS[p][i];

			v->thanks[p][i] = (menu_row){ t->who ? t->who : T(t->who_s),
			                              t->what ? t->what : T(t->what_s), false };
		}
	}
	i = ABOUT_THANKS_LAST;
	v->thanks[2][i++] = MENU_RULE;
	v->thanks[2][i] = MENU_NOTE(T(S_ABOUT_LICENSES));
}

/* One width for every page, grown only if a value needs it */
static int about_width(app *a, const about_view *v)
{
	int w = menu_std_width(a), m, i;

	if ((m = menu_measure(v->details, ABOUT_DETAIL_ROWS, T(S_ABOUT_HEADING))) > w) w = m;
	if ((m = menu_measure(v->system, 4, T(S_ABOUT_HEADING))) > w) w = m;
	for (i = 0; i < ABOUT_THANKS_PAGES - 1; i++)
		if ((m = menu_measure(v->thanks[i], ABOUT_THANKS_ROWS, T(S_ABOUT_THANKS))) > w) w = m;
	if ((m = menu_measure(v->thanks[2], ABOUT_THANKS_LAST, T(S_ABOUT_THANKS))) > w) w = m;
	return w;
}

static void about_refresh(about_view *v, unsigned now)
{
	int pct;
	bool charging;

	if (!v->primed || now - v->refreshed >= 2000) {
		char ssid[WIFI_SSID_MAX], ip[64];
		wifi_state ws = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
		v->primed = true;
		v->refreshed = now;
		snprintf(v->addr, sizeof v->addr, "%s", ws == WIFI_CONNECTED
		         ? (ip[0] ? ip : T(S_ABOUT_NO_ADDRESS_YET)) : T(S_ABOUT_NOT_CONNECTED));
		about_read(&v->info, P_CARD, plat_battery_info_path());
		about_detail_rows(&v->info, v->details, &v->values);
	}
	/* Use the shared cable cache, so About follows charger changes as the
	 * status corner does instead of waiting for the storage refresh. */
	if (!battery_now(&pct, &charging))
		snprintf(v->batt, sizeof v->batt, "%s", T(S_ABOUT_UNKNOWN));
	else if (charging)
		snprintf(v->batt, sizeof v->batt, T(S_ABOUT_BATTERY_CHARGING), pct);
	else
		snprintf(v->batt, sizeof v->batt, "%d%%", pct);
	snprintf(v->ver, sizeof v->ver, "%s", ZYPEROS_VERSION);
	snprintf(v->up, sizeof v->up, T(S_ABOUT_UPTIME), now / 3600000, (now / 60000) % 60);
	v->system[0] = (menu_row){ T(S_ABOUT_VERSION), v->ver, false };
	v->system[1] = (menu_row){ T(S_ABOUT_ADDRESS), v->addr, false };
	v->system[2] = (menu_row){ T(S_ABOUT_BATTERY), v->batt, false };
	v->system[3] = (menu_row){ T(S_ABOUT_AWAKE_FOR), v->up, false };
}

static void about_draw(app *a, const about_view *v, int page)
{
	const menu_row *rows = v->system;
	const char *heading = T(S_ABOUT_HEADING);
	char count[16];
	int n = 4;

	int loop_at = 0;

	switch (page) {
	case 1: rows = v->details; n = ABOUT_DETAIL_ROWS; break;
	case 2:
		rows = v->story; n = v->nstory; loop_at = v->story_loop;
		heading = T(S_ABOUT_THE_NAME);
		break;
	case 3: rows = v->thanks[0]; n = ABOUT_THANKS_ROWS; heading = T(S_ABOUT_THANKS); break;
	case 4: rows = v->thanks[1]; n = ABOUT_THANKS_ROWS; heading = T(S_ABOUT_THANKS); break;
	case 5: rows = v->thanks[2]; n = ABOUT_THANKS_LAST + 2; heading = T(S_ABOUT_THANKS); break;
	}
	/* The panel draws posed into a pass of its own while it opens; the
	 * photo comes once it is at rest, rather than unscaled over it */
	bool rest = g_pframe.panel.scale == 1.0f && g_pframe.panel.alpha == 1.0f;

	snprintf(count, sizeof count, "%d / %d", page + 1, ABOUT_PAGES);
	/* The story holds still where Up and Down have put it */
	g_menu_scroll_hold = page == 2 ? v->story_scroll : -1;
	if (page != 2) g_menu_scroll_span = 0;
	menu_draw_caption(a, heading, rows, n, -1, v->width, MENU_ZYPEROS, NULL, false, loop_at,
	                  count);
	g_menu_scroll_hold = -1;
	if (page == 2 && v->photo && rest && g_menu_body.h > 0) {
		/* At the body's right, middle of its height, smaller only if the
		 * body is shorter than the photo */
		int h = g_menu_body.h < ABOUT_PHOTO_H ? g_menu_body.h : ABOUT_PHOTO_H;
		int w = h * ABOUT_PHOTO_W / ABOUT_PHOTO_H;
		SDL_Rect r = { g_menu_body.x + g_menu_body.w - w,
		               g_menu_body.y + (g_menu_body.h - h) / 2, w, h };

		SDL_RenderCopy(a->r, v->photo, NULL, &r);
	}
}

static void about_screen(app *a)
{
	about_view v = { 0 };
	int page = 0;
	bool done = false;
	int lv = panel_open();
	float k;

	while (!done && !want_quit && a->running) {
		unsigned now = plat_now_ms();
		bool refresh = !v.primed || now - v.refreshed >= 2000;
		about_refresh(&v, now);
		if (!v.nstory) about_pages(a, &v);
		/* Every page keeps one width. Grow only if their values need it,
		 * as the shared menu renderer does, instead of clipping a value. */
		if (refresh) v.width = about_width(a, &v);
		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		if (in_repeat(&a->in, IN_LEFT)) {
			moved_sound(a, IN_LEFT);
			page = (page + ABOUT_PAGES - 1) % ABOUT_PAGES;
		}
		if (in_repeat(&a->in, IN_RIGHT)) {
			moved_sound(a, IN_RIGHT);
			page = (page + 1) % ABOUT_PAGES;
		}
		/* The story: Up and Down walk it a line at a time, held they repeat,
		 * and it stops at the top and at the bottom. It never moves by itself. */
		if (page == 2 && g_menu_scroll_line > 0) {
			int was = v.story_scroll, lim = g_menu_scroll_span;

			if (in_repeat(&a->in, IN_UP)) {
				v.story_scroll = was > g_menu_scroll_line ? was - g_menu_scroll_line : 0;
				step_sound(a, IN_UP, was, v.story_scroll);
			}
			if (in_repeat(&a->in, IN_DOWN)) {
				was = v.story_scroll;
				v.story_scroll = was + g_menu_scroll_line < lim ? was + g_menu_scroll_line : lim;
				step_sound(a, IN_DOWN, was, v.story_scroll);
			}
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		about_draw(a, &v, page);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
	if (v.photo) SDL_DestroyTexture(v.photo);
}

/* MENU > Controls: what every button does, a page per place. Read-only like
 * About, and paged with left and right the way Play Time changes its window -
 * the whole list at once is thirty rows nobody reads. */
static void controls_screen(app *a)
{
	ctl_page page = CTL_SHELF;
	bool done = false;
	int lv = panel_open();
	float k;

	ctl_set_fn_volume(plat_brightness_on_volume());
	ctl_set_battery_lights(plat_battery_lights());

	while (!done && !want_quit && a->running) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80], count[16];
		int n = ctl_rows(page, rows);

		snprintf(head, sizeof head, T(S_PANEL_CONTROLS), ctl_page_name(page));
		/* which page of how many, at the heading's right (DESIGN.md 11.3) */
		snprintf(count, sizeof count, "%d / %d", (int)page + 1, CTL_PAGES);

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		/* SELECT opens Muse here as it does on every other menu screen -
		 * which is one of the things this page exists to tell you. */
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		if (in_repeat(&a->in, IN_RIGHT)) {
			step_sound(a, IN_RIGHT, page, (page + 1) % CTL_PAGES);
			page = (ctl_page)((page + 1) % CTL_PAGES);
		}
		if (in_repeat(&a->in, IN_LEFT)) {
			step_sound(a, IN_LEFT, page, (page + CTL_PAGES - 1) % CTL_PAGES);
			page = (ctl_page)((page + CTL_PAGES - 1) % CTL_PAGES);
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw_caption(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS, NULL,
		                  false, 0, count);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
}

/* ---------- the tutorial -------------------------------------------------- */

/* DESIGN.md 11.10: the first thing a new handheld shows, and again from MENU >
 * Show Tutorial. The steps are src/tour.c's and the screens src/tourdraw.c's;
 * this is the loop that reads the buttons, plays the sounds, turns the real
 * volume and brightness, and lends the drawing Zyper and the heart. */

/* Zyper as the tutorial poses him, by the menu's draw_zyper_pose: the high
 * brow following his tilt (zyper_brow_lift) as the menu's does, and on Beard
 * his shadow first, where he stands, so a hop or a breath lifts him off it. */
static void tour_zyper_draw(void *ctx, float cx, float cy, float h, const tour_zyper *z)
{
	app *a = ctx;

	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	if (z->shadow > 0.0f)
		zyper_shadow_draw(a->r, cx, cy, h, z->pose.tilt, z->shadow * (1.0f - z->dim));
	draw_zyper_pose(a->r, cx, cy, h, &z->pose, zyper_brow_lift(z->pose.tilt), z->dim,
	                z->alpha);
}

static void tour_heart_draw(void *ctx, float cx, float cy, float rad, SDL_Color c)
{
	draw_heart(((app *)ctx)->r, cx, cy, rad, c);
}

/* The power step's stage plays the power-off's own frames, small. */
static void tour_poweroff_draw(void *ctx, unsigned t, float cx, float cy, float h,
                               float from_x, Uint8 alpha)
{
	poweroff_zyper((app *)ctx, t, cx, cy, h, from_x, alpha);
}

/* A sound the tutorial asks for (DESIGN.md 9): accept for the welcome's and
 * the finish's A, move or edge for the first move, reward for each lesson,
 * deny for a wrong press and back for START; TOUR_QUIET for none. */
static void tour_sound(int s)
{
	if (s != TOUR_QUIET) uisound_play((ds_sound)s);
}

/* "Show Tutorial is in MENU." over the shelf, after a skip at the first
 * start: for hold-short, in in move and out in fade-out, as a one-line
 * message is (8.2, 11.8). Any press but the levels' puts it away at once,
 * from where it is, and is taken by it: nothing waits on a message. */
static void tour_skip_message(app *a)
{
	const char *s = T(S_TOURRUN_SKIP_MESSAGE);
	const uint32_t at = plat_now_ms();
	uint32_t out = at + DS_MS_HOLD_SHORT, now;

	while (a->running && !want_quit &&
	       (int32_t)((now = plat_now_ms()) - out) < DS_MS_FADE_OUT) {
		float op;
		int b;

		plat_input_poll(&a->in);
		/* As tour_run follows them, and menu_run_body */
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		for (b = 0; b < IN_COUNT; b++)
			if (a->in.pressed[b] && b != IN_VOLUP && b != IN_VOLDN &&
			    b != IN_BRIGHTUP && b != IN_BRIGHTDN && (int32_t)(now - out) < 0) {
				uisound_play(DS_SOUND_BACK);
				out = now;
			}
		op = tour_ease(TC_OUT, now, at, DS_MS_MOVE)
		   * (1.0f - fminf(1.0f, fmaxf(0.0f, (float)(int32_t)(now - out) / DS_MS_FADE_OUT)));
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		tour_message(a->r, s, op);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(6);
	}
	redraw_now();
}

/* The tutorial, from its welcome until it is finished or skipped, or POWER
 * turns the handheld off in the middle of it, which leaves it to run again
 * at the next start. `first`: the first start's, which after a skip says
 * where to find it again; from the menu, the player knows. */
/* The tutorial's choices (DESIGN.md 11.10): the settings as they are, as
 * tour_choice_name's indexes, for its steps to start on. */
static void tour_settings(const app *a, int at[TOUR_CHOICES])
{
	at[0] = theme_index() == DS_DARK ? 1 : 0;
	at[1] = a->home_layout == HOME_GALLERY ? 1 : 0;
	at[2] = uisound_setting();
	at[3] = bgm_on() ? 0 : 1;
}

/* Choice `c`'s `v` applied, to be seen and heard as it is looked at, and
 * with `save` written to the key MENU's row writes, as it is kept. The home
 * screen's layout is only previewed until it is kept. */
static void tour_apply(app *a, int c, int v, bool save)
{
	if (c == 0) {
		const appearance k = v ? APPEAR_DARK : APPEAR_LIGHT;
		const int was = theme_index();

		theme_init(k);
		if (save) {
			db_set_str(db_dev(), "appearance", appearance_id(k));
			boot_theme_changed();
		}
		if (theme_index() != was) theme_changed(a);
	} else if (c == 1 && save) {
		a->home_layout = v ? HOME_GALLERY : HOME_SPOTLIGHT;
		db_set_str(db_dev(), "home", home_id(a->home_layout));
		home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
	} else if (c == 2) {
		uisound_set(v);
		if (save) db_set_str(db_dev(), "uisounds", uisound_setting_id(v));
	} else if (c == 3) {
		/* Heard while On is looked at: at the first start the music waits
		 * for the shelf, which comes after the tutorial, so it is let go */
		bgm_set_on(v == 0);
		if (v == 0) bgm_hold(BGM_HOLD_START, false);
		if (save) db_set_str(db_dev(), "bgm", bgm_on_id(v == 0));
	}
}

static void tour_run(app *a, bool first)
{
	const tour_hooks hk = { tour_zyper_draw, tour_heart_draw, tour_poweroff_draw, a, P_ROOT };
	tour t;
	bool saved = false, shown = false, kept[TOUR_CHOICES] = { false };
	int snd[IN_COUNT], n, i, at[TOUR_CHOICES], looked[TOUR_CHOICES];
	tour_step was_step = TOUR_WELCOME;
	/* What Appearance was, Auto too, for a theme looked at and not kept */
	const appearance look_was = theme_appearance();

	bool lang_kept = false;

	tour_start(&t, plat_now_ms(), plat_brightness_on_volume());
	tour_lang_from(&t, (int)i18n_lang());
	tour_settings(a, at);
	tour_choices_from(&t, at);
	memcpy(looked, at, sizeof looked);
	while (!t.over && a->running && !want_quit) {
		uint32_t now;

		plat_input_poll(&a->in);
		/* Where the sound goes, and Muse's answers, as menu_run_body follows
		 * them: a cable or a headset during the tour is heard at once - its
		 * sounds' route is set here before its first press, at boot too -
		 * and an album playing under MENU > Show Tutorial goes on to its
		 * next track (#45). */
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		now = plat_now_ms();
		if (idle_due(a)) { power_off(a); break; }
		/* POWER is the tutorial's in its own step, so it can be tried, and
		 * anywhere else turns the handheld off as on every screen. */
		if (a->in.pressed[IN_POWER]) {
			if (!tour_takes_power(&t)) { power_off(a); break; }
			tour_sound(tour_press(&t, TK_POWER, now));
		}
		/* The levels' keys work in every step, as on every screen, and
		 * answer their own. */
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		tour_set_music(&t, bgm_count() > 0);
		n = tour_buttons(&t, a->in.pressed, now, snd);
		/* A choice looked at is applied before its press is heard, so the
		 * sounds' step plays in the voice it lands on; a kept one is saved */
		{
			const int c = tour_choice_of(t.step);

			/* The menu music plays from the moment its step comes on, on
			 * the On it starts on (My note: "have the menu music play the
			 * music when it's on in onboarding") */
			if (t.step != was_step && t.step == TOUR_MUSIC) tour_apply(a, c, t.pick[c], false);
			was_step = t.step;
			if (c >= 0 && t.pick[c] != looked[c]) {
				tour_apply(a, c, t.pick[c], false);
				looked[c] = t.pick[c];
			}
			if (c >= 0 && t.done && !kept[c]) {
				tour_apply(a, c, t.kept[c], true);
				kept[c] = true;
			}
			/* Skipped with one looked at and not kept: what was saved */
			if (c >= 0 && t.over && !t.done && looked[c] != t.kept[c]) {
				if (c == 0) {
					const int was = theme_index();

					theme_init(look_was);
					if (theme_index() != was) theme_changed(a);
				} else {
					tour_apply(a, c, t.kept[c], false);
				}
				looked[c] = t.kept[c];
			}
		}
		for (i = 0; i < n; i++) tour_sound(snd[i]);
		{
			/* The Pixel 2's L2 and R2 come only as taps on release:
			 * held, the handheld has the platform's word for them */
			bool held[IN_COUNT];

			memcpy(held, a->in.down, sizeof held);
			held[IN_L2] = held[IN_L2] || plat_tap_held(IN_L2);
			held[IN_R2] = held[IN_R2] || plat_tap_held(IN_R2);
			tour_hands(&t, held, plat_menu_held(), now);
		}
		tour_tick(&t, now);
		/* The language step (DESIGN.md 11.30): the screen in the language
		 * under the cursor from the frame it lands on, going round or
		 * moved, and the one kept saved once A leaves the step or START
		 * skips the rest */
		if (t.step == TOUR_LANG && !t.over) {
			const lang f = (lang)tour_lang_focus(&t, now);

			if (f != i18n_lang()) language_apply(a, f, false);
		} else if (!lang_kept) {
			language_apply(a, (lang)tour_lang_kept(&t), true);
			lang_kept = true;
		}
		tick_theme(a);
		/* Seen once past the power step's press, so the second POWER in
		 * its reward, which turns the handheld off, leaves it seen */
		if (!saved && tour_seen(&t)) {
			tour_save(db_dev(), &t);
			saved = true;
		}
		if (t.over) break;
		/* A whole frame, which the menu it was opened from is not on: that
		 * panel is gone, not left for the shelf to draw leaving */
		g_frame_no++;
		tour_draw(a->r, &t, now, &hk);
		plat_draw_osd(a->r);
		plat_present();
		/* Said once it is on the screen, which check-tour-shots waits for */
		if (!shown) fprintf(stderr, "tour: %s, on the screen\n", first ? "the first start's" : "again");
		shown = true;
		ui_frame_rest(6);
	}
	tour_draw_free();
	if (!saved) tour_save(db_dev(), &t);
	if (t.over && t.skipped && first) tour_skip_message(a);
	redraw_now();
}

/* The widest row of one built menu. */
static int menu_measure(const menu_row *rows, int n, const char *heading)
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
		rw = ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			rw += gap + ui_text_width(fm, rows[i].value);
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
static int menu_shelf_width(app *a)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int w, n, i, k;

	if (a->menu_w) return a->menu_w;

	n = menu_build(a, SCREEN_SYSTEMS, a->sys_cursor, rows, &bufs, &heading);
	w = menu_measure(rows, n, heading);

	/* Every value that can be cycled on this menu, at its widest, so the
	 * panel does not resize under the row being cycled. Sort By joined
	 * Display Mode in that category the moment it stopped being a
	 * placeholder - "Recently Added" is 187px wider than "Name".
	 *
	 * Muse's menu and Favorites' are their own rows, Sort By second and no
	 * Display Mode, so the enum's row numbers are only the rest's. Written
	 * through them on Muse's, this measured Rescan Folder carrying a sort
	 * order, a row no menu has. */
	for (i = 0; i < a->sys.count; i++) {
		bool muse = is_muse(&a->sys.systems[i]);
		bool own  = muse || a->view[i].owner;
		int  srow = own ? 1 : SM_SORT, show = -1;

		n = menu_build(a, SCREEN_GAMES, i, rows, &bufs, &heading);
		if (muse) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int nn = sys_menu_muse_rows(muse_kind_shown(), muse_kinds() > 1,
			                            !plat_has_wifi(), ids);

			for (k = 0; k < nn; k++) {
				if (ids[k] == SMM_SORT) srow = k;
				if (ids[k] == SMM_SHOW) show = k;
			}
			/* The longer of Show's two values, so turning it over does not
			 * change the menu's width under the cursor. */
			if (show >= 0) {
				int mw;

				rows[show].value = "Audiobooks";
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
		}
		for (k = 0; !own && k < DMODE_COUNT; k++) {
			int mw;
			rows[SM_DISPLAY].value = dmode_label(k);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
		}
		if (!own) rows[SM_DISPLAY].value = dmode_label(0);
		/* And each core a shelf can choose, for the same reason. */
		if (!own && cfg_core_count(&a->sys.systems[i]) > 1) {
			char alt[CFG_STR], lb[CFG_STR];
			const char *keep = rows[SM_CORE].value;

			for (k = 0; cfg_core_at(&a->sys.systems[i], k, alt, sizeof alt); k++) {
				int mw;
				rows[SM_CORE].value = core_label(alt, lb, sizeof lb);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
			rows[SM_CORE].value = keep;
		}
		for (k = 0; k < (muse ? ML_ORDERS : SORT_COUNT); k++) {
			int mw;
			rows[srow].value = muse ? ml_order_label((ml_order)k, false) : sort_label(k);
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
			if (muse) {
				int kind;

				for (kind = ML_BOOKS; kind < ML_KINDS; kind++) {
					rows[srow].value = ml_order_label_kind((ml_order)k, (ml_kind)kind);
					mw = menu_measure(rows, n, heading);
					if (mw > w) w = mw;
				}
			}
		}
	}
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
	int most    = ZYPEROS_SCREEN_W - MENU_MARGIN * 2 - menu_pad() * 2;
	int w       = floor > content ? floor : content;

	return w < most ? w : most;
}

/* ---------- MENU > Search Games ---------------------------------------- */

/* The query on the on-screen keyboard, then what it finds (src/search.h) as a
 * list - the title, and the system it is on. A on one picks it: the menu
 * closes, and pick_launch puts the shelf on it and starts it, as A there
 * would. B on the list goes back to the keyboard with the query still in it,
 * to change it; B on the keyboard back to the menu. The query is kept for
 * the next search. Every system's shelf but Muse's, whose cards are albums,
 * and Favorites', whose games are on their own systems' shelves too. */
typedef struct {
	app       *a;
	sr_item    hit[SR_SHOWN];
	int        n, found, pick;
	char       heading[160], note[160];
} search_view;

static int search_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	search_view *v = ctx;
	int i, n = 0;

	*heading = v->heading;
	/* The system by its name when that is short, else by its tag - "GBA",
	 * "A7800", "PS1": on the Pixel 2, "Game Boy Advance" left a title nine
	 * letters ("036 The L...") */
	for (i = 0; i < v->n && n < max - 2; i++) {
		const system_cfg *sc = &v->a->sys.systems[v->hit[i].shelf];

		rows[n++] = (menu_row){ v->hit[i].title,
		                        strlen(sc->name) <= 8 || !sc->tag[0] ? sc->name : sc->tag,
		                        true };
	}
	rows[n++] = MENU_RULE;
	if (v->found > v->n)
		snprintf(v->note, sizeof v->note, T(S_SEARCH_SOME_OF), v->n, v->found);
	else
		snprintf(v->note, sizeof v->note, TN(S_SEARCH_FOUND, v->found), v->found);
	rows[n++] = MENU_NOTE(v->note);
	return n;
}

static menu_result search_key(app *a, void *ctx, in_button key, int sel)
{
	search_view *v = ctx;

	(void)a;
	if (key != IN_ACCEPT || sel < 0 || sel >= v->n) return MENU_STAY;
	v->pick = sel;
	return MENU_DONE;
}

/* True with the game picked in *shelf and *game: the menu is to close */
static bool search_screen(app *a, int *shelf, int *game)
{
	static char query[64];
	search_view v;
	sr_item *items;
	int i, k, n = 0, total = 0;
	bool picked = false;

	for (i = 0; i < a->sys.count; i++) total += a->view[i].list.count;
	if (total <= 0 || !(items = malloc((size_t)total * sizeof *items))) {
		uisound_play(DS_SOUND_DENY);
		return false;
	}
	for (i = 0; i < a->sys.count; i++) {
		const sysview *sv = &a->view[i];

		if (is_muse(&a->sys.systems[i]) || sv->owner) continue;
		for (k = 0; k < sv->list.count; k++)
			items[n++] = (sr_item){ sv->list.items[k].title, sv->list.items[k].name, i, k };
	}
	memset(&v, 0, sizeof v);
	v.a = a;
	while (!picked && a->running && !want_quit && !g_menu_closing) {
		menu_style st = { .fixed_w = menu_std_width(a), .accent = MENU_ZYPEROS };
		kb_result kr = keyboard_prompt(a, T(S_SEARCH_TITLE), query, (int)sizeof query);

		if (kr == KB_POWER) { power_off(a); break; }
		if (kr != KB_ACCEPT || !query[0]) break;
		v.found = sr_search(query, items, n, v.hit, SR_SHOWN);
		v.n = v.found < SR_SHOWN ? v.found : SR_SHOWN;
		if (!v.n) {
			char msg[192];

			uisound_play(DS_SOUND_DENY);
			snprintf(msg, sizeof msg, T(S_SEARCH_NONE), query);
			result_message(a, T(S_SEARCH_TITLE), msg);
			continue;
		}
		snprintf(v.heading, sizeof v.heading, T(S_SEARCH_HEADING), query);
		v.pick = -1;
		menu_run(a, &st, search_build, search_key, &v);
		if (v.pick >= 0) {
			*shelf = v.hit[v.pick].shelf;
			*game = v.hit[v.pick].game;
			picked = true;
		}
	}
	free(items);
	return picked;
}

/* Date & Time (src/clock.h). Time Zone first, then each row is one part of
 * the clock, changed with left and right and set the moment it changes: the
 * system clock and the hardware clock both, so it holds through a power-off.
 * Time Zone steps through CLOCK_ZONES and is the library database's
 * "timezone", which the Brick's launch.sh reads at boot as well. The rows
 * are clock_rows', so tools/clock-check.c holds their order. */
typedef struct {
	clock_bufs b;
	char zone[64];   /* a city, or a zone id not on the list as it is */
} clock_ui;

static int clock_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	clock_ui *c = ctx;
	char id[64];
	int z, n;

	*heading = T(S_DATETIME_TITLE);
	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	z = clock_zone_find(id);
	snprintf(c->zone, sizeof c->zone, "%s", z >= 0 ? CLOCK_ZONES[z].city : id);
	n = clock_rows(time(NULL), g_clock24, c->zone, rows, max, &c->b);
	if (n + 3 <= max) {
		rows[n++] = MENU_RULE;
		rows[n++] = MENU_NOTE(T(S_DATETIME_CHANGE));
		/* Only where it is true: the Brick's ntpd sets the clock again once
		 * Wi-Fi connects, so a time set here lasts until then */
		if (plat_has_wifi()) rows[n++] = MENU_NOTE(T(S_DATETIME_WIFI_SETS));
	}
	return n;
}

static menu_result clock_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)ctx;
	if (!d) return MENU_STAY;
	if (clock_row_field(sel) >= 0) {
		plat_clock_set(clock_step(time(NULL), (clock_field)clock_row_field(sel), d));
	} else if (sel == CLOCK_ROW_ZONE) {
		char id[64];
		int z;

		db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
		z = clock_zone_step(id, d);
		db_set_str(db_lib(), "timezone", CLOCK_ZONES[z].id);
		plat_clock_zone(CLOCK_ZONES[z].id);
		boot_theme_changed();
	}
	/* Either way the same instant is another hour of the day, and Auto is
	 * to follow it now and not at its next look a minute on: a player who
	 * has just set the zone to where they are, to see the shelf in the theme
	 * of that hour, is looking at the shelf. */
	if (theme_refresh()) theme_changed(a);
	return MENU_STAY;
}

static void clock_screen(app *a)
{
	clock_ui c;
	menu_style st = {
		/* Fixed, so the panel does not change size as "May" becomes
		 * "September" under a held key */
		.fixed_w = menu_std_width(a),
		.accent  = MENU_ZYPEROS,
	};

	memset(&c, 0, sizeof c);
	menu_run(a, &st, clock_build, clock_key, &c);
}

/* MENU > Menu Music: its rows are src/bgm_menu.c's, its player src/bgm_out.c.
 * Every change is heard as it is made - the music plays behind this screen -
 * and kept in the device database beside UI Sounds. The folder is read here
 * as the screen opens, and the player told to read it again, so tracks
 * copied over Wi-Fi or USB since the launcher started are in the list. */
typedef struct {
	bgm_bufs b;
	bgm_list list;
	bool     builtin;      /* list is ZyperOS's own tracks */
	char     now[256];
	char     now_name[BGM_TAG_MAX];   /* ...as the song pop-up names it */
} bgm_view;

static int bgm_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	bgm_view *v = ctx;
	bgm_ui u;

	bgm_now(v->now, sizeof v->now);
	/* Playing names the song as the pop-up does, from its tags, when the
	 * player's news is the file playing */
	{
		bgm_song s;
		char head[BGM_TAG_MAX];

		bgm_song_now(&s);
		v->now_name[0] = '\0';
		if (v->now[0] && !strcmp(s.file, v->now))
			bgm_song_label(&s.tags, s.file, head, sizeof head, v->now_name, sizeof v->now_name);
	}
	/* A track set from the other folder - one of ZyperOS's, before the
	 * card had its own - is not what plays: the player shuffles instead
	 * (src/bgm_out.c, wanted_file), so the row says so */
	u = (bgm_ui){ bgm_on(), bgm_volume(),
	              bgm_find(&v->list, bgm_track()) >= 0 ? bgm_track() : "",
	              v->now, v->list.n, v->builtin, g_song_popup, v->now_name };
	return bgm_menu_build(&u, rows, max, &v->b, heading);
}

static menu_result bgm_key(app *a, void *ctx, in_button key, int sel)
{
	bgm_view *v = ctx;
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	char num[8];

	(void)a;
	if (sel == BGM_ROW_ON && (d || key == IN_ACCEPT)) {
		bgm_set_on(!bgm_on());
		db_set_str(db_dev(), "bgm", bgm_on_id(bgm_on()));
	} else if (sel == BGM_ROW_VOLUME && d) {
		bgm_set_volume(bgm_volume_step(bgm_volume(), d));
		snprintf(num, sizeof num, "%d", bgm_volume());
		db_set_str(db_dev(), "bgm.volume", num);
	} else if (sel == BGM_ROW_TRACK && d) {
		int at = bgm_track_step(bgm_find(&v->list, bgm_track()), v->list.n, d);
		const char *t = at < 0 ? "" : v->list.names[at];

		bgm_set_track(t);
		db_set_str(db_dev(), "bgm.track", t);
	} else if (sel == BGM_ROW_POPUP && (d || key == IN_ACCEPT)) {
		g_song_popup = !g_song_popup;
		db_set_str(db_dev(), "bgm.popup", bgm_on_id(g_song_popup));
	} else if (sel == BGM_ROW_NOW && key == IN_ACCEPT) {
		bgm_skip();
	}
	return MENU_STAY;
}

static void bgm_screen(app *a)
{
	char dir[CFG_STR * 2], builtin[CFG_STR * 2];
	bgm_view v;
	menu_style st = {
		/* Fixed, so the panel does not change size as one track's name
		 * gives way to a longer one */
		.fixed_w = menu_std_width(a),
		.accent  = MENU_ZYPEROS,
	};

	memset(&v, 0, sizeof v);
	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, BGM_DIR);
	snprintf(builtin, sizeof builtin, "%s/%s", P_ROOT, BGM_BUILTIN_DIR);
	v.builtin = bgm_scan_either(dir, builtin, &v.list) == builtin;
	bgm_rescan();
	st.sound = v.b.sound;
	menu_run(a, &st, bgm_build, bgm_key, &v);
	bgm_free(&v.list);
}

/* MENU > System Update (src/sysupdate.h, docs/updates.md): the full page,
 * My pick in the update lab (2026-11-05), DESIGN.md 11.17. Zyper at the
 * left, the versions in **title**, the line under them in **meta**, the
 * release's What's New, one bar (or one line where it goes), and the
 * buttons at the foot. The work is the update program's, which goes on when
 * B leaves; this page starts it and draws what it says. */
#define UP_X       90                         /* the page's left and right edge */
#define UP_W       (ZYPEROS_SCREEN_W - 2 * UP_X)
#define UP_NOTES   3

/* What's New: the release's own points, the list's "note" lines, signed with
 * it; the first UP_NOTES */
static int up_notes(char out[UP_NOTES][120])
{
	char p[512], line[256];
	FILE *f;
	int n = 0;

	/* A system update's notes are in its own list */
	up_path(p, sizeof p, "latest-system.txt");
	if (access(p, R_OK)) up_path(p, sizeof p, "latest.txt");
	f = fopen(p, "r");
	while (f && n < UP_NOTES && fgets(line, sizeof line, f)) {
		if (strncmp(line, "note ", 5)) continue;
		line[strcspn(line, "\n")] = 0;
		snprintf(out[n++], 120, "%s", line + 5);
	}
	if (f) fclose(f);
	return n;
}

/* The arrow between the versions, drawn: the faces have no U+2192. A shaft
 * and a head, `w` long, centered on (x, mid), in **text-dim**. */
static void up_arrow(SDL_Renderer *r, int x, int mid, int w)
{
	SDL_Color c = UI_TEXT_DIM;
	SDL_Vertex v[3];
	int t = 6, hw = 16, hh = 15, i;
	SDL_Rect shaft = { x, mid - t / 2, w - hw + 2, t };

	ui_round_rect(r, &shaft, t / 2, c);
	v[0].position = (SDL_FPoint){ (float)(x + w - hw), (float)(mid - hh) };
	v[1].position = (SDL_FPoint){ (float)(x + w), (float)mid };
	v[2].position = (SDL_FPoint){ (float)(x + w - hw), (float)(mid + hh) };
	for (i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
	SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

static void update_page_draw(app *a, const su_page *p, char notes[UP_NOTES][120], int nnotes)
{
	const ds_theme *thm = theme();
	SDL_Renderer *r = a->r;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fl = ui_font(UI_F_LABEL),
	         *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	SDL_Color warn = ui_col(thm->warning, 255);
	char buf[160];
	int x, i, w, y;

	draw_background(a);
	ui_text(r, fl, T(S_UPDATEPAGE_TITLE), ZYPEROS_SCREEN_W / 2, ui_cap_y(fl, 66), 0, UI_TEXT_SOFT);
	draw_zyper(r, 158.0f, 232.0f, 150.0f, ZYPER_TILT_DEG, 1.0f, 0.0f, 255);

	/* 0.1.0 → 0.2.0, or the installed version alone */
	x = 270;
	x += ui_text(r, ft, p->from, x, ui_cap_y(ft, 200), -1, UI_TEXT);
	if (p->to[0]) {
		up_arrow(r, x + 24, 200, 52);
		ui_text(r, ft, p->to, x + 100, ui_cap_y(ft, 200), -1, UI_TEXT);
	}
	/* The line under the versions wraps to a second line before it is cut
	 * (a long sentence in French or Portuguese); the second sits under it,
	 * clear of What's New at 348 */
	{
		char sl[2][LB_LINE];
		int n = ui_balance_lines(fs, p->sub, ZYPEROS_SCREEN_W - 272 - UP_X, 2, sl), k;
		for (k = 0; k < n; k++)
			ui_text(r, fs, sl[k], 272, ui_cap_y(fs, 270 + k * 38), -1, p->sub_warn ? warn : UI_TEXT_DIM);
	}

	/* A problem, in full: **menu** **text**, up to three balanced lines from
	 * y 348 every 52, a **warning** bar beside them, where What's New goes */
	if (p->warn[0]) {
		char lines[4][LB_LINE];
		int n = ui_balance_lines(fm, p->warn, UP_W - 34, 4, lines), k;
		SDL_Rect mark = { UP_X, 348 - 30, 8, (n - 1) * 52 + 60 };

		ui_round_rect(r, &mark, 4, warn);
		for (k = 0; k < n; k++)
			ui_text(r, fm, lines[k], UP_X + 34, ui_cap_y(fm, 348 + k * 52), -1, UI_TEXT);
	}
	/* What's New: a point at most two lines, four lines in all, in **menu**
	 * from y 348, a line every 52, clear of the bar at 572 */
	for (i = 0, y = 348; p->notes && i < nnotes && y <= 348 + 3 * 52; i++) {
		char lines[2][LB_LINE];
		int n = ui_balance_lines(fm, notes[i], UP_W - 40, 2, lines), k;

		ui_text(r, fm, "\xc2\xb7", UP_X, ui_cap_y(fm, y), -1, UI_TEXT_DIM);
		for (k = 0; k < n && y <= 348 + 3 * 52; k++, y += 52)
			ui_text(r, fm, lines[k], UP_X + 40, ui_cap_y(fm, y), -1, UI_TEXT_SOFT);
	}

	/* The bar, track with accent-ink over it, and its two words under it */
	if (p->bar) {
		SDL_Rect tr = { UP_X, 572, UP_W, 18 };
		w = (int)((float)UP_W * p->frac + 0.5f);
		ui_round_rect(r, &tr, 9, ui_col(thm->track, 255));
		if (w > 0) {
			SDL_Rect fi = { UP_X, 572, w < 18 ? 18 : w, 18 };
			ui_round_rect(r, &fi, 9, ui_col(thm->accent_ink, 255));
		}
		ui_text(r, fs, p->bar_left, UP_X, ui_cap_y(fs, 626), -1, UI_TEXT_DIM);
		ui_text(r, fs, p->bar_right, UP_X + UP_W, ui_cap_y(fs, 626), 1, UI_TEXT_DIM);
	} else if (p->line[0]) {
		/* Up to three lines, centered, the last where the one line stood */
		char ll[3][LB_LINE];
		int n = ui_balance_lines(fs, p->line, UP_W, 3, ll), k;
		for (k = 0; k < n; k++)
			ui_text(r, fs, ll[k], ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, 600 - (n - 1 - k) * 38), 0,
			        p->line_warn ? warn : UI_TEXT_DIM);
	}

	/* The buttons, glyph and word, centered as a row at the foot */
	{
		int gs = 48, gap = 12, between = 40, total = 0, cx;
		for (i = 0; i < p->nkeys; i++)
			total += ui_button_width(p->keys[i].button, gs) + gap + ui_text_width(fs, p->keys[i].label);
		total += between * (p->nkeys > 1 ? p->nkeys - 1 : 0);
		cx = (ZYPEROS_SCREEN_W - total) / 2;
		for (i = 0; i < p->nkeys; i++) {
			int bw = ui_button_width(p->keys[i].button, gs);
			ui_button(r, p->keys[i].button, cx + bw / 2, 714, gs, UI_BUTTON_PLAIN);
			cx += bw + gap;
			cx += ui_text(r, fs, p->keys[i].label, cx, ui_cap_y(fs, 714), -1, UI_TEXT_SOFT) + between;
		}
	}
}

/* The button's action on this page, or SUA_NONE */
static su_action update_page_key(const su_page *p, const char *button)
{
	int i;
	for (i = 0; i < p->nkeys; i++)
		if (!strcmp(p->keys[i].button, button)) return p->keys[i].act;
	return SUA_NONE;
}

static void update_screen(app *a)
{
	const su_view *v = up_view(true);
	char notes[UP_NOTES][120];
	int nnotes = up_notes(notes);
	unsigned notes_at = plat_now_ms();

	/* Opened with a network and nothing in hand: it looks now, one small
	 * request (a look within the last ten minutes stands) */
	if (!v->running && v->wifi &&
	    (v->st.state == SU_IDLE || v->st.state == SU_UPTODATE || v->st.state == SU_ERROR) &&
	    (!v->st.time || time(NULL) - (time_t)v->st.time > 600))
		up_spawn("check");
	while (!want_quit && a->running) {
		su_page p;
		su_action act = SUA_NONE;

		v = up_view(false);
		su_page_build(v, &p);
		/* What's New comes with the list a look keeps: read again now and
		 * then, not every frame */
		if (plat_now_ms() - notes_at > 1000) { nnotes = up_notes(notes); notes_at = plat_now_ms(); }

		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		if (a->in.pressed[IN_ACCEPT])      act = update_page_key(&p, "A");
		else if (a->in.pressed[IN_Y])      act = update_page_key(&p, "Y");
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}
		if ((a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_Y]) && act == SUA_NONE)
			uisound_play(DS_SOUND_DENY);
		switch (act) {
		case SUA_CHECK:
			uisound_play(DS_SOUND_ACCEPT);
			up_spawn("check");
			break;
		case SUA_DOWNLOAD:
			uisound_play(DS_SOUND_ACCEPT);
			up_spawn("download");
			break;
		case SUA_ROLLBACK: {
			char msg[192], prog[512], dev[64];
			const char *slash = strrchr(P_USERDATA, '/');
			char *argv[] = { prog, "rollback", "--card", (char *)P_CARD, "--device", dev, NULL };

			uisound_play(DS_SOUND_ACCEPT);
			snprintf(msg, sizeof msg, T(S_UPDATEPAGE_ROLLBACK_ASK), v->prev);
			if (confirm_panel(a, T(S_UPDATEPAGE_ROLLBACK), msg, T(S_UPDATEPAGE_ROLLBACK))) {
				snprintf(prog, sizeof prog, "%s/zyperos-update", P_ROOT);
				snprintf(dev, sizeof dev, "%s", slash ? slash + 1 : P_USERDATA);
				plat_run(argv, NULL, P_ROOT);
				g_up_read_at = 0;
			}
			break;
		}
		case SUA_RESTART:
			/* As power_off, but the device comes back: launch.sh reboots,
			 * and the image's switch puts the new version in before
			 * anything runs */
			uisound_play(DS_SOUND_ACCEPT);
			remember_place(a);
			book_keep(true);
			plat_request_reboot();
			plat_backlight(false);
			a->running = false;
			return;
		case SUA_BACK:
			uisound_play(DS_SOUND_BACK);
			return;
		default:
			break;
		}

		tick_tint(a);
		tick_theme(a);
		update_page_draw(a, &p, notes, nnotes);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
}

/* What --shot-at presses at 0 in a --menu shot (shot_motion): a step onto
 * its row; or with --open, --close and --swap, the top menu opening on the
 * shelf, closing onto it, or giving way to MENU > Controls. `shot_lv` is the
 * level the menu motion's frames are drawn at, 0 when there is none, and
 * `shot_pressed` says the press has come. */
enum { SHOT_STEP, SHOT_OPEN, SHOT_CLOSE, SHOT_SWAP };
static int      shot_press = SHOT_STEP;
static int      shot_lv;
static bool     shot_pressed;
static unsigned shot_t0;             /* the press, on the held clock */

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
static menu_style zyperos_menu_style(app *a, int screen)
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
	};
}

static void zyperos_menu_draw(app *a, int sel)
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
	menu_draw(a, heading, rows, n, sel, st.fixed_w, st.follow_tint ? a->tint : st.accent);
	g_menu_zyper = NULL;
}

/* The system menu's context. It holds the app rather than a copy of what was
 * on screen when it opened, so `a->screen` and `a->sys_cursor` are read live
 * exactly as the hand-written loop read them. Neither can change while the
 * menu is up; reading them is simply one fewer thing that could drift. */
typedef struct {
	app      *a;
	menu_bufs bufs;   /* the built rows point into this, so it outlives them */
	/* Which of the two menus to build. Normally the screen you are on, but
	 * Both has no screens to be on: there MENU means the firmware and B
	 * means this system, and neither can be inferred from where you are. */
	int       screen;
} sysmenu_ctx;

static int sysmenu_build(void *ctx, menu_row *rows, int max,
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
static struct { int shelf, game; unsigned hold_ms; } g_pick = { -1, -1, 0 };

static void launch(app *a);

static void pick_launch(app *a)
{
	int shelf = g_pick.shelf, game = g_pick.game, was = a->sys_cursor;
	Uint32 until = plat_now_ms() + g_pick.hold_ms;

	g_pick.shelf = -1;
	if (shelf < 0 || shelf >= a->sys.count || game < 0 ||
	    game >= a->view[shelf].list.count)
		return;
	fprintf(stderr, "pick: %s/%s\n", a->sys.systems[shelf].folder,
	        a->view[shelf].list.items[game].file);
	if (a->screen == SCREEN_GAMES && shelf != was)
		evict_far(&a->view[was], TEX_KEEP_FAR);
	a->sys_cursor = shelf;
	home_reset(&a->home, shelf, a->sys.count, plat_now_ms());
	a->view[shelf].cursor = game;
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

static menu_result sysmenu_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)ctx;

	if (a->screen == SCREEN_GAMES) {
		/* Muse's menu is its own rows - see SM_MUSE_ROWS - and which is
		 * where depends on what its shelf shows, so the enum's row numbers
		 * below mean nothing on it: sys_menu_muse_rows says. Sort By goes on
		 * to the Sort By below like any shelf's. Album Art is a dead row off
		 * the network, and the runner does not land on dead rows. */
		if (is_muse(&a->sys.systems[a->sys_cursor])) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int n = sys_menu_muse_rows(muse_kind_shown(), muse_kinds() > 1,
			                           !plat_has_wifi(), ids);
			sm_muse_row id = sel >= 0 && sel < n ? ids[sel] : SMM_COUNT;

			if (id == SMM_SORT) {
				sel = SM_SORT;
			} else if (id == SMM_LOCK) {
				/* Closes the menu; the screen underneath locks. */
				if (key != IN_ACCEPT) return MENU_STAY;
				muse_ask_lock();
				return MENU_DONE;
			} else if (id == SMM_SHUFFLE) {
				/* Closes the menu; Now Playing opens on the first song. */
				if (key != IN_ACCEPT) return MENU_STAY;
				muse_ask_shuffle();
				return MENU_DONE;
			} else if (id == SMM_SHOW) {
				/* The kinds the card has, round in either direction; A goes
				 * forward, as right does. */
				if (d || key == IN_ACCEPT) {
					int k = muse_kind_shown(), step = d < 0 ? ML_KINDS - 1 : 1, t;

					for (t = 0; t < ML_KINDS; t++) {
						k = (k + step) % ML_KINDS;
						if (ml_count_kind(&g_muse, (ml_kind)k)) break;
					}
					muse_show(&a->view[a->sys_cursor], (ml_kind)k);
				}
				return MENU_STAY;
			} else {
				if (key != IN_ACCEPT) return MENU_STAY;
				if (id == SMM_ART) { album_art_screen(a); return MENU_STAY; }
				if (id != SMM_RESCAN) return MENU_STAY;
				wait_panel(a, "MP4", T(S_LIBRARY_SCANNING));
				rescan_all(a);
				return MENU_DONE;
			}
		}
		/* Favorites' menu is its own rows too, Games and Sort By - see
		 * SM_FAV_ROWS - so its Sort By is row 1 and not SM_SORT. Nothing here
		 * knew that from the day the menu was cut to two rows, and left and
		 * right on Favorites' Sort By did nothing at all. */
		if (a->view[a->sys_cursor].owner)
			sel = sel == 1 ? SM_RANDOM : sel == 2 ? SM_SORT : SM_GAMES;
		/* And a console's rows lose Box Art on a device with no radio, which
		 * moves Rescan Folder up one: which row is which comes from the same
		 * list the build used. */
		else if (!is_muse(&a->sys.systems[a->sys_cursor])) {
			sys_ui dev = { .no_wifi = !plat_has_wifi() };
			sm_row ids[SM_ROWS];
			int n = sys_menu_system_rows(&dev, ids);

			sel = sel >= 0 && sel < n ? (int)ids[sel] : SM_ROWS;
		}
		/* The core, on a shelf that lists more than one: left, right or A
		 * steps to the next, saved at once like the display mode. The next
		 * game launched from here runs on it. */
		if ((d || key == IN_ACCEPT) && sel == SM_CORE &&
		    cfg_core_count(&a->sys.systems[a->sys_cursor]) > 1) {
			core_step(a, a->sys_cursor, d ? d : 1);
			return MENU_STAY;
		}
		/* Display mode, saved the moment it changes because there is no
		 * confirm step to hang the write off. */
		if (d && sel == SM_DISPLAY) {
			sysview *v = &a->view[a->sys_cursor];

			v->dmode = (v->dmode + d + DMODE_COUNT) % DMODE_COUNT;
			display_save(a);
			return MENU_STAY;
		}
		/* Sort order, on the same left/right idiom.
		 *
		 * The cursor follows the GAME, not its index. Re-sorting under a
		 * cursor left where it sat means closing the menu onto a different
		 * game than the one that was selected when it opened - the shelf
		 * would appear to have jumped on its own. Which game you were
		 * looking at is the thing that survives a reorder; where it happened
		 * to sit in the old order is not. On Muse's shelf the same goes for
		 * the album, whose folder is its `file`. */
		if (d && sel == SM_SORT) {
			sysview *v = &a->view[a->sys_cursor];
			char keep[LIB_PATH];
			int i, keep_owner = -1;

			keep[0] = '\0';
			if (v->cursor >= 0 && v->cursor < v->list.count) {
				snprintf(keep, sizeof keep, "%s",
				         v->list.items[v->cursor].file);
				/* On Favorites a file name is not enough: the same one can
				 * be on the shelf twice, from two systems. */
				if (v->owner) keep_owner = v->owner[v->cursor];
			}

			v->sort = is_muse(&a->sys.systems[a->sys_cursor])
			        ? (v->sort + d + ML_ORDERS) % ML_ORDERS
			        : sort_step(v->sort, d);
			sort_shelf(a, a->sys_cursor);
			sort_save(a);

			/* The textures are indexed by position, so they moved with
			 * nothing. Dropping them lets the next frame fetch each card's
			 * art for where it now sits. */
			free_view_textures(v);

			v->cursor = 0;
			for (i = 0; i < v->list.count; i++)
				if (!strcmp(v->list.items[i].file, keep) &&
				    (!v->owner || v->owner[i] == keep_owner)) {
					v->cursor = i;
					break;
				}
			cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
			return MENU_STAY;
		}
		if (key != IN_ACCEPT) return MENU_STAY;
		/* One of this shelf's games: the menu closes and it starts */
		if (sel == SM_RANDOM) {
			if (!random_pick(a, false, &g_pick.shelf, &g_pick.game)) return MENU_STAY;
			g_pick.hold_ms = 700;
			return MENU_DONE;
		}
		/* The system's own color, not the menu's. This acts on the shelf you
		 * are looking at, and menu_draw already follows that rule everywhere
		 * else. */
		if (sel == SM_BOXART)
			art_screen(a, a->sys.systems[a->sys_cursor].folder, NULL, NULL,
			           NULL, a->sys.systems[a->sys_cursor].accent);
		if (sel == SM_RESCAN) {
			wait_panel(a, a->sys.systems[a->sys_cursor].name, T(S_LIBRARY_SCANNING));
			rescan_all(a);
			/* Close, rather than redraw this menu over a shelf that may have
			 * just lost the system it was built for. Acting and closing is
			 * also what pressing it means. */
			return MENU_DONE;
		}
		return MENU_STAY;
	}

	/* The row's identity rather than its position: a device with no radio has
	 * fewer of them, and the same list the build used says which is which. */
	{
		sys_ui dev = { .no_wifi = !plat_has_wifi(), .no_bt = !plat_has_bluetooth(),
		               .cable_xfer = plat_cable_link(NULL, 0),
		               .no_developer = !usblink_dev_image(),
		               .no_lights = !plat_battery_lights(),
		               .no_charge_limit = !plat_charge_limit_supported(),
		               /* the same rule as the build's (menu_build): a card with
		                * the update program has the row */
		               .update = up_have() ? "" : NULL };
		pm_row ids[PM_ROWS];
		int n = sys_menu_zyperos_rows(&dev, ids);

		sel = sel >= 0 && sel < n ? (int)ids[sel] : PM_ROWS;
	}

	/* Auto Off, on the left/right idiom Display mode uses. A step past either
	 * end changes nothing, and Zyper is told only of a change. */
	if (d && sel == PM_SLEEP) {
		int k, at = 0, was = a->auto_off;

		for (k = 0; k < AUTO_OFF_COUNT; k++)
			if (AUTO_OFF[k] == a->auto_off) { at = k; break; }
		at += d;
		if (at < 0) at = 0;
		if (at >= AUTO_OFF_COUNT) at = AUTO_OFF_COUNT - 1;
		a->auto_off = AUTO_OFF[at];
		auto_off_save(a->auto_off);
		/* From now, not from whenever the last countdown began: choosing 30s
		 * should not inherit two minutes of an old one already spent. */
		a->idle.since_ms = plat_now_ms();
		return a->auto_off != was ? MENU_CHANGED : MENU_STAY;
	}
	/* System Order: the shelves read again in the other order, the cursor
	 * kept on its system by tag (rescan_all), as File Transfer reads them
	 * when it closes. Once per press, never per frame. */
	if (sel == PM_ORDER && (d || key == IN_ACCEPT)) {
		g_order_release = !g_order_release;
		db_set_str(db_dev(), "systems.order", g_order_release ? "release" : "az");
		rescan_all(a);
		redraw_now();
		return MENU_CHANGED;
	}
	/* The home layout is independent of Appearance and the selected system. */
	if (sel == PM_HOME && (d || key == IN_ACCEPT)) {
		a->home_layout = a->home_layout == HOME_SPOTLIGHT ? HOME_GALLERY : HOME_SPOTLIGHT;
		db_set_str(db_dev(), "home", home_id(a->home_layout));
		home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
		redraw_now();
		return MENU_CHANGED;
	}
	/* Charge Limit, round Off, 80%, 85% and 90% as Side Lights goes round its
	 * four, A going forward: saved, and the kernel told at once, so the
	 * charger stops or goes on by its next reading */
	if (sel == PM_CHARGE && (d || key == IN_ACCEPT)) {
		int k, at = 0;

		for (k = 0; k < CHARGE_LIMITS_COUNT; k++)
			if (CHARGE_LIMITS[k] == g_charge_limit) at = k;
		at = (at + (d ? d : 1) + CHARGE_LIMITS_COUNT) % CHARGE_LIMITS_COUNT;
		g_charge_limit = CHARGE_LIMITS[at];
		db_set_int(db_dev(), "chargelimit", g_charge_limit);
		charge_limit_apply();
		redraw_now();
		return MENU_CHANGED;
	}
	/* Light, Dark, Auto, stepped either way with A going forward, like the
	 * choices, and applied the moment it changes: the theme is resolved
	 * again here, saved as its word, and the theme file the bar's other
	 * programs read is rewritten by theme_init - so the next frame is drawn
	 * from the new theme(), and the next bar over a game or a port is drawn
	 * in it. Saved even when the theme it comes to is the one already on:
	 * Auto at noon is Beard and so is Light, and they are still two
	 * settings. */
	if (sel == PM_APPEARANCE && (d || key == IN_ACCEPT)) {
		appearance k = appearance_step(theme_appearance(), d ? d : 1);
		int was = theme_index();

		theme_init(k);
		db_set_str(db_dev(), "appearance", appearance_id(k));
		boot_theme_changed();
		if (theme_index() != was) theme_changed(a);
		return MENU_CHANGED;
	}

	/* On or off, so left, right and A all turn it over. Off lets the link
	 * go at once unless something holds it, and the next computer is asked
	 * (src/usblink.h); the card's file says so for the next boot too. */
	if (sel == PM_DEVELOPER && (d || key == IN_ACCEPT)) {
		usblink_set_developer(db_dev(), P_USERDATA, !usblink_developer(db_dev()));
		return MENU_CHANGED;
	}

	/* 12-hour or 24-hour, two positions, so left, right and A all turn it
	 * over. The shelf's corner and the row above it read g_clock24 as they
	 * are drawn, so the next frame is in the new one. */
	if (sel == PM_HOURS && (d || key == IN_ACCEPT)) {
		clock24_save(!g_clock24);
		return MENU_CHANGED;
	}
	/* Bars, Percentage or Time Left: three positions round, as Charge Limit
	 * goes, Right and A forward and Left back, saved as its word */
	if (sel == PM_BATTERY && (d || key == IN_ACCEPT)) {
		g_batt_mode = (g_batt_mode + (d < 0 ? BATT_MODES - 1 : 1)) % BATT_MODES;
		db_set_str(db_dev(), "battery_icon", BATT_IDS[g_batt_mode]);
		redraw_now();
		return MENU_CHANGED;
	}

	/* Two positions, so left and right and A all do the same thing: there is
	 * nothing to step through, only something to turn off and on. */
	if (sel == PM_AUDIO && (d || key == IN_ACCEPT)) {
		g_aout_policy = g_aout_policy == AOUT_AUTO ? AOUT_SPEAKER : AOUT_AUTO;
		aout_save(g_aout_policy);
		/* Forced: the resolved DEVICE may not have changed - pinning Speaker
		 * with a cable in is still the codec - but the setting did, and the
		 * next unplug must act on the new policy rather than on a cached
		 * device string that happens to match. */
		aout_apply(true);
		return MENU_CHANGED;
	}

	/* Four positions, stepped either way like the card sets, A going forward.
	 * Applied at once, to Muse and to Diatom if they are there to be told, so
	 * a player trying them over music hears each as it is chosen. */
	if (sel == PM_CHANNELS && (d || key == IN_ACCEPT)) {
		g_channels = (aout_channels)((g_channels + (d ? d : 1) + AOUT_CHANNELS_COUNT)
		                             % AOUT_CHANNELS_COUNT);
		channels_save(g_channels);
		aout_apply(false);
		return MENU_CHANGED;
	}

	/* Wood, Glass, Bubble, Off, stepped either way like Audio Channels, A
	 * going forward, and heard as each is chosen: the row is MENU_SND_OWN, so
	 * the runner plays nothing for it and its press is played here, in the
	 * voice it comes to, before the setting is written. Off is silent. */
	if (sel == PM_SOUNDS && (d || key == IN_ACCEPT)) {
		int k = uisound_setting_step(uisound_setting(), d ? d : 1);

		uisound_set(k);
		if (d) moved_sound(a, key);
		else   uisound_play(DS_SOUND_ACCEPT);
		db_set_str(db_dev(), "uisounds", uisound_setting_id(k));
		return MENU_STAY;
	}

	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case PM_WIFI:         wifi_screen(a); break;
	/* A game from every system: the menu closes and it starts */
	case PM_RANDOM:
		if (!random_pick(a, true, &g_pick.shelf, &g_pick.game)) break;
		g_pick.hold_ms = 700;
		return MENU_DONE;
	case PM_XFER:         xfer_screen(a); break;
	case PM_LIGHTS:       lights_screen(a); break;
	case PM_LANGUAGE:     language_screen(a); return MENU_CHANGED;
	/* These three say how they went, which is Zyper's to answer */
	case PM_SCRAPER:      return scraper_screen(a);
	case PM_ACHIEVEMENTS: return ra_signin_screen(a);
	case PM_BT:           bt_screen(a); break;
	case PM_CLOCK:        clock_screen(a); break;
	case PM_MUSIC:        bgm_screen(a); break;
	case PM_STATS:        if (stats_screen(a)) return MENU_DONE; break;
	case PM_CONTROLS:     controls_screen(a); break;
	/* Closes the menu: the tutorial ends on the shelf, where its finish
	 * says to start playing. */
	case PM_TOUR:         tour_run(a, false); return MENU_DONE;
	case PM_UPDATE:       update_screen(a); break;
	case PM_ABOUT:        about_screen(a); break;
	/* A game by its name: the menu closes once one is picked, and it starts */
	case PM_SEARCH:
		if (!search_screen(a, &g_pick.shelf, &g_pick.game)) break;
		g_pick.hold_ms = 0;
		return MENU_DONE;
	default: break;
	}
	return MENU_STAY;
}

static void zyperos_menu_for(app *a, int screen)
{
	sysmenu_ctx c;

	uisound_play(DS_SOUND_ACCEPT);                /* a menu opening */
	memset(&c, 0, sizeof c);
	c.a = a;
	c.screen = screen;
	menu_style st = zyperos_menu_style(a, screen);
	/* Each row's sound class, which the build sets beside the rows */
	st.sound = c.bufs.sound;

	g_pick.shelf = -1;
	menu_run(a, &st, sysmenu_build, sysmenu_key, &c);
	/* A game Random Game or Search Games picked, once the menu has gone */
	if (g_pick.shelf >= 0 && a->running && !want_quit) pick_launch(a);
	g_pick.shelf = -1;
}

static void zyperos_menu(app *a)
{
	zyperos_menu_for(a, a->screen);
}

/* ---------- launching ----------------------------------------------------- */

/* The in-game menu - the launcher's, over Diatom's pause.
 *
 * MENU in a game makes Diatom write a preview of the frame, stop presenting,
 * and say PAUSED. From that word the display is ours: the menu is that frame
 * dimmed, with the rows a player expects - Continue, Save, Load, Reset, Quit
 * - drawn with the launcher's own font and glow. The menu is the launcher's,
 * not something patched into the emulator.
 *
 * Every row acts through one protocol line. Save and Load use the same autosave
 * paths the launch handed over, so the autosave funnel stays one thing. */
/* Display sits with the things you do to the game rather than the things you do
 * to a save, because it is the one row whose effect you judge by looking at the
 * game behind the menu. */
/* gm_row, gm_ui and gm_rows: src/game_menu.h */

/* The paused frame, drawn where the game actually is.
 *
 * Diatom reports its rect with every DISPLAY message (put there so a launcher
 * need not recompute it from geometry it does not have), and the preview it
 * writes is the CORE'S frame - 256x224 for an NES, no display mode applied.
 * Stretching that to the panel, which is what this used to do, showed a game
 * that looked nothing like the one paused underneath at any mode that does
 * not fill the screen, and made the picture jump size the moment MENU was
 * pressed.
 *
 * Falls back to filling the panel when Diatom has not said - the standalone
 * path, and the first moments of a launch. */
static void draw_paused_frame(app *a, SDL_Texture *bg)
{
	SDL_Rect r;

	if (!bg) return;
	if (plat_resident_rect(&r)) SDL_RenderCopy(a->r, bg, NULL, &r);
	else                        SDL_RenderCopy(a->r, bg, NULL, NULL);
}

/* What every screen over a paused game sits on - the in-game menu, the save
 * slots, the achievements: the frame where the game is, on **ink** where it
 * is not, under **scrim.game**, which is as heavy in Beard as in Salt &
 * Pepper because the game is content and not part of the theme (DESIGN.md
 * 3.4). The in-game menu had the shelf's lighter scrim, and the slots and
 * achievements beside it the heavy one, over the same paused game. The
 * scrim at `k` of its strength, which is 1 but while the in-game menu opens
 * or closes (panel_frame).
 *
 * And the status corner over it all, as on the shelf (DESIGN.md 11.12): this
 * process draws these screens, so it draws the corner (in the game, Diatom
 * draws its disc). In Salt & Pepper, as scrim.game is dark in both themes,
 * with the clock in **text**, since a white frame under the scrim leaves
 * text-soft at 4.4:1. Each of these screens' loops draws every frame, so the
 * blink needs nothing more. */
static void paused_backdrop(app *a, SDL_Texture *bg, float k)
{
	const ds_rgb ink = theme()->ink;

	g_frame_no++;
	SDL_SetRenderDrawColor(a->r, ink.r, ink.g, ink.b, 255);
	SDL_RenderClear(a->r);
	drawn_forget();
	draw_paused_frame(a, bg);
	ui_scrim_k(a->r, 1, k);
	draw_corner(a->r, DS_DARK, ui_col(DS_THEME[DS_DARK].text, 255),
	            frames_wanted());
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

/* The slot carousel: Auto plus the manual slots for Load, the manual slots for
 * Save. One slot at a
 * time, large - the paused frame at a size you can actually read - with the
 * save's own timestamp under it and a dot rail for where you are. Left and
 * right cycle; Load skips slots with nothing behind them, Save cannot aim at
 * Auto, which belongs to the exit funnel alone. */
typedef struct {
	SDL_Texture *thumb[GM_SLOTS + 1];   /* [0]=Auto, [1..GM_SLOTS] */
	int have[GM_SLOTS + 1];
	char when[GM_SLOTS + 1][40];
	/* Every slot of one game holds the same machine's frame, so one aspect
	 * describes them all and the picture can be framed exactly rather than
	 * dropped into a fixed box with bars down its sides. 4:3 until a slot
	 * with a picture in it says otherwise. */
	float aspect;
	int saving;
} slot_view;

/* A save's moment as the strip shows it, "Aug 23 9:21:05 AM": the month's
 * short name and the order of the parts are the language's (slots.when) */
static void slot_when(char *out, size_t n, const struct tm *tm)
{
	static const str_id MON[12] = {
		S_SLOTS_JAN, S_SLOTS_FEB, S_SLOTS_MAR, S_SLOTS_APR, S_SLOTS_MAY, S_SLOTS_JUN,
		S_SLOTS_JUL, S_SLOTS_AUG, S_SLOTS_SEP, S_SLOTS_OCT, S_SLOTS_NOV, S_SLOTS_DEC,
	};
	int h12 = tm->tm_hour % 12;
	const char *mon = T(MON[tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0]);

	/* In the clock MENU > Clock chose, as the status corner is */
	if (g_clock24) {
		snprintf(out, n, T(S_SLOTS_WHEN_24), mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		return;
	}
	if (!h12) h12 = 12;
	snprintf(out, n, T(S_SLOTS_WHEN), mon, tm->tm_mday, h12, tm->tm_min, tm->tm_sec,
	         T(tm->tm_hour < 12 ? S_SLOTS_AM : S_SLOTS_PM));
}

/* Drawing only, so one frame of it can be rendered by --shot without a game
 * running or a device in hand.
 *
 * IN SALT & PEPPER'S ROLES, WHATEVER THEME IS ON. There is no panel here: the
 * heading, the picture, its name and time and the dots sit straight on
 * **scrim.game**, which is ink at 73% over a paused game in both themes, so
 * the ground they are read against is dark in Beard too. Beard's charcoal on
 * it had nothing to stand out from. The in-game menu beside it is a panel,
 * which brings its own surface and needs none of this. */
static void slot_draw(app *a, const slot_view *sv, int sel)
{
	const ds_theme *dk = &DS_THEME[DS_DARK];
	/* `area` is the layout slot the picture is fitted into; nothing is ever
	 * drawn to it. The border is the picture's own edge - a 240x160 GBA frame
	 * and a 256x224 NES frame are different shapes, and neither should be
	 * padded out into the same rectangle. */
	const SDL_Rect area = { (ZYPEROS_SCREEN_W - 700) / 2, 129, 700, 451 };
	const int bw = UI_PANEL_BORDER;
	SDL_Rect img = area, frame;
	char slotname[64];
	int line_menu = ui_font_line(UI_F_MENU), line_meta = ui_font_line(UI_F_META);
	int i;

	/* The heading sits close to the top edge so the picture gets the middle of
	 * the screen. Everything below hangs off the IMAGE rather than off `area`,
	 * so a frame shorter than the layout slot pulls its own caption up with it
	 * instead of leaving a gap.
	 *
	 * The margins are mirrored: the heading's top sits as far from the top of
	 * the screen as the marker rail's bottom sits from the bottom of it, and
	 * the heading is centered in the gap above the frame. That fixes every
	 * number here to one another rather than to taste, so changing the image
	 * size moves the rest to match instead of drifting into something.
	 *
	 * **text-soft**, as every heading is (DESIGN.md 11.3); it was the one
	 * heading in **text-dim**. */
	ui_text(a->r, ui_font(UI_F_LABEL), sv->saving ? T(S_SLOTS_SAVE_TO) : T(S_SLOTS_LOAD_FROM),
	        ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_LABEL, 39), 0,
	        ui_col(dk->text_soft, 255));

	if ((float)area.w / sv->aspect <= (float)area.h) {
		img.w = area.w;
		img.h = (int)(area.w / sv->aspect + 0.5f);
	} else {
		img.h = area.h;
		img.w = (int)(area.h * sv->aspect + 0.5f);
	}
	img.x = area.x + (area.w - img.w) / 2;
	img.y = area.y + (area.h - img.h) / 2;
	frame = (SDL_Rect){ img.x - bw, img.y - bw, img.w + bw * 2, img.h + bw * 2 };

	/* A thumbnail's lift, DESIGN.md 7 (focus-small), in the system's color:
	 * Salt & Pepper's, a glow, on this ground in both themes - Beard's ink
	 * shadow on ink at 73% lifts nothing. */
	ui_lift_in(a->r, &frame, a->tint, UI_LIFT_FOCUS_SMALL, DS_DARK);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	/* Opaque, matching the menu panel's border: DESIGN.md's frame, 3 units of
	 * **edge**. It was 12 of the system's color when the panel's was, and a
	 * system's color for decoration is what DESIGN.md 3.3 says it never is;
	 * the dot under the picture says whose game it is. The thumbnail itself
	 * is drawn at full brightness below - it never was darkened, only the
	 * frame around it.
	 *
	 * Rounded outside, square inside: the corner radius belongs to the chrome,
	 * and rounding the image would mean clipping the game's own pixels. */
	ui_round_rect(a->r, &frame, bw, ui_col(dk->edge, 255));
	if (sv->thumb[sel]) {
		SDL_SetTextureColorMod(sv->thumb[sel], 255, 255, 255);
		SDL_RenderCopy(a->r, sv->thumb[sel], NULL, &img);
	} else {
		/* An empty slot is the same frame with nothing in it, so the strip
		 * does not change shape as you cycle past one. */
		SDL_SetRenderDrawColor(a->r, dk->ground.r, dk->ground.g, dk->ground.b,
		                       238);
		SDL_RenderFillRect(a->r, &img);
		ui_text(a->r, ui_font(UI_F_MENU), T(S_SLOTS_EMPTY), img.x + img.w / 2,
		        ui_cap_y(ui_font(UI_F_MENU), img.y + img.h / 2), 0,
		        ui_col(dk->text_dim, 255));
	}

	if (sel == 0) snprintf(slotname, sizeof slotname, "%s", T(S_SLOTS_AUTO));
	else          snprintf(slotname, sizeof slotname, T(S_SLOTS_SLOT), sel);
	ui_text(a->r, ui_font(UI_F_MENU), slotname, ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_MENU, img.y + img.h + 36), 0, ui_col(dk->text, 255));
	ui_text(a->r, ui_font(UI_F_META),
	        sv->have[sel] ? sv->when[sel] : (sv->saving ? "\xE2\x80\x94" : ""),
	        ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_META, img.y + img.h + 42 + line_menu), 0,
	        ui_col(dk->text_dim, 255));

	/* The dot rail: where you are among the slots, without showing every
	 * picture. The one you are on in the system's color as a mark - Salt &
	 * Pepper's mark, on this ground - a slot you can land on in **text-dim**,
	 * held back so it never matches the mark of a system with no color, and
	 * one you cannot in faint **track**. Three steps that stay apart over a
	 * dark game and a bright one, which the scrim leaves at ink to mid grey. */
	{
		int dots = GM_SLOTS + 1, dw = 36;
		int x0 = (ZYPEROS_SCREEN_W - dots * dw) / 2 + dw / 2;
		unsigned m = ui_sys_mark_in(a->tint, DS_DARK);
		ds_rgb here = { (Uint8)(m >> 16), (Uint8)(m >> 8), (Uint8)m };
		int y  = img.y + img.h + 62 + line_menu + line_meta;

		/* One size for every marker. Sizing the selected one larger meant the
		 * rail changed shape as you cycled, and color already says which slot
		 * you are on - two signals for one fact, one of them moving. */
		for (i = 0; i < dots; i++) {
			int can = sv->saving ? i >= 1 : sv->have[i];
			int r2  = 12;
			SDL_Rect d = { x0 + i * dw - r2, y - r2, r2 * 2, r2 * 2 };
			SDL_Color c = i == sel ? ui_col(here, 255)
			            : can      ? ui_col(dk->text_dim, 150)
			                       : ui_col(dk->track, 90);
			ui_round_rect(a->r, &d, r2 / 2, c);
		}
	}
}

/* Returns the chosen slot (SLOT_AUTO, or 1..GM_SLOTS) or 0 for backed out. */
static int slot_strip(app *a, SDL_Texture *bg, int saving)
{
	slot_view sv = { .aspect = 4.0f / 3.0f, .saving = saving };
	sysview *v = &a->view[a->sys_cursor];
	game_entry *g = &v->list.items[v->cursor];
	/* The game's own system, not the shelf: from Favorites the shelf is
	 * Favorites, which has no folder, and the slots came out loose in .zyperos/
	 * - a second set the system's shelf never saw. Reported 2026-09-24. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	int i, sel = -1, chosen = 0, done = 0;
	bool   have_b[GM_SLOTS + 1] = { false };
	char pth[LIB_PATH * 2];

	for (i = 0; i <= GM_SLOTS; i++) {
		int slot = gm_slot_at(i);
		struct stat st;

		slot_state_path(a, o, g, slot, pth, sizeof pth);
		sv.have[i] = (stat(pth, &st) == 0 && st.st_size > 0);
		if (sv.have[i]) {
			have_b[i] = true;
			/* The state's own mtime: when this moment was captured. The
			 * device clock is only as good as the device clock, and showing
			 * what the filesystem says beats pretending to know better. */
			struct tm *tm = localtime(&st.st_mtime);
			if (tm) {
				/* "Aug 23 9:21:05 AM". Built from the fields rather than with
				 * strftime's "%-I", which drops the leading zero but is a GNU
				 * extension and does nothing on the BSD libc the host build
				 * links against - it would have read right on the device and
				 * wrong in every screenshot. The month is the language's, as
				 * the menu's words are, not the C library's locale. */
				slot_when(sv.when[i], sizeof sv.when[i], tm);
			}

			slot_preview_path(a, o, g, slot, pth, sizeof pth);
			SDL_Surface *sf = IMG_Load(pth);
			if (sf) {
				sv.thumb[i] = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
		}
		/* Loading starts on the newest thing there is to load, which is
		 * almost always Auto. Saving is decided after the loop, below,
		 * because it needs to know about every slot before it can choose. */
		if (sel < 0 && !saving && sv.have[i]) sel = i;
	}

	/* Save lands on the first empty slot, or the last one when there is none -
	 * the rule and its reasoning are in game_menu.h, where a check can reach
	 * them. */
	if (saving) sel = gm_save_slot(have_b);

	for (i = 0; i <= GM_SLOTS; i++) {
		int tw = 0, th = 0;
		if (!sv.thumb[i]) continue;
		SDL_QueryTexture(sv.thumb[i], NULL, NULL, &tw, &th);
		if (tw > 0 && th > 0) { sv.aspect = (float)tw / (float)th; break; }
	}
	if (sel < 0) sel = saving ? 1 : -1;
	if (sel < 0) done = -1;                     /* nothing to load at all */

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_RIGHT)) {
			int dir = in_repeat(&a->in, IN_RIGHT) ? 1 : -1, next = sel;
			do {
				next = (next + dir + GM_SLOTS + 1) % (GM_SLOTS + 1);
			} while ((saving ? next == 0 : !sv.have[next]) && next != sel);
			step_sound(a, dir > 0 ? IN_RIGHT : IN_LEFT, sel, next);
			sel = next;
		}
		if (menu_leaving(a)) done = -1;
		/* SLOT_AUTO, not 9. Commit 4208619 renamed the resume slot from
		 * MinUI's number to `auto` and replaced it everywhere the constant
		 * was used - but here the old number was written out rather than
		 * referenced, so it survived the rename. Loading Auto from the menu
		 * has asked for a `.9.state` that has not existed since 2026-08-27,
		 * and every one of those was a state_rejected. */
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			chosen = gm_slot_at(sel);
			done = 1;
		}
		/* This screen used to ignore the power button outright - the one
		 * screen in the launcher that did. Stop the game and close with
		 * nothing chosen; game_menu sees the flag and closes behind us. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = -1;
		}

		tick_theme(a);
		paused_backdrop(a, bg, 1.0f);
		slot_draw(a, &sv, sel);
		plat_present();
		ui_frame_rest(8);
	}

	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
	return done == 1 ? chosen : 0;
}

/* Change the mode, keep it, tell the running game, and take back the rect so
 * the backdrop behind the menu redraws where the game is about to be. */
static void gm_cycle_display(app *a, int d)
{
	/* The OWNER's, so a mode chosen while playing a favorite is set for the
	 * system that game belongs to - which is what a per-system mode means.
	 * Set on the Favorites shelf it would have been filed under FAV and worn
	 * by every game launched from there, whatever machine it came from. */
	sysview *v = owner_view(a);

	v->dmode = (v->dmode + d + DMODE_COUNT) % DMODE_COUNT;
	display_save(a);
	plat_resident_line("SETDISPLAY\tmode=%s", DMODES[v->dmode].name);
	plat_resident_sync_rect(150);
}

/* What the in-game menu needs from the app, handed to gm_rows to become rows.
 * Same split as everywhere else. */
static int gm_build(app *a, menu_row *out, gm_bufs *b)
{
	gm_ui u;

	u.dmode  = dmode_label(a->view[a->sys_cursor].dmode);
	u.earned = chv_earned();
	u.total  = chv_count();
	u.screens = g_game_dual;
	if (u.screens) {
		const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor,
		                                      a->view[a->sys_cursor].cursor)];

		screens_get(s->tag, &u.layout, &u.swap);
		/* The swap and the stylus as Diatom last said: L2 and R2 in the
		 * game turn them, so this process only knows them from SCREENS
		 * swap= and STYLUS on=. */
		u.swap   = plat_resident_swap();
		u.stylus = plat_resident_stylus();
	}
	return gm_rows(&u, out, b);
}

/* The list itself, over the paused frame. menu_draw already windows a list
 * longer than the screen, which a set of 166 certainly is.
 *
 * Earned rows are drawn live and unearned quiet - the same distinction
 * menu_draw makes for a placeholder, and it reads correctly here: what you
 * have is bright, what is still ahead of you is not. */
/* 255 characters at ~30 to a line is nine, and a line of capitals is half
 * that. Twelve is past the worst real description with room over; the card
 * scrolls when the lines outrun the panel, so depth costs height rather
 * than text. */
#define CHV_WRAP_LINES 12

/* The code point that starts at p, and its bytes in *len */
static unsigned wrap_cp(const char *p, int *len)
{
	const unsigned char *u = (const unsigned char *)p;

	*len = 1;
	if (u[0] < 0xC0) return u[0];
	if (u[0] < 0xE0 && (u[1] & 0xC0) == 0x80) { *len = 2; return (u[0] & 0x1Fu) << 6 | (u[1] & 0x3F); }
	if (u[0] < 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
		*len = 3;
		return (u[0] & 0x0Fu) << 12 | (u[1] & 0x3Fu) << 6 | (u[2] & 0x3F);
	}
	return u[0];
}

/* Whether a line may end at byte `i` of `s` inside a run with no spaces:
 * src/linebreak.c's rule for Chinese, which has none - between two
 * characters when either is Chinese, but not before a mark that closes
 * (，。！…) nor after one that opens (（「“). `i` is where a character
 * starts. */
static bool wrap_cjk_break(const char *s, int i)
{
	static const unsigned closes[] = { 0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF01, 0xFF1F, 0xFF1A,
	                                   0xFF1B, 0xFF09, 0x300D, 0x300F, 0x300B, 0x3009, 0x3011,
	                                   0x201D, 0x2019, 0x2026, 0x30FC, 0x00B7 };
	static const unsigned opens[] = { 0xFF08, 0x300C, 0x300E, 0x300A, 0x3008, 0x3010,
	                                  0x201C, 0x2018 };
	unsigned prev, next;
	int j = i - 1, len;
	size_t k;

	if (i <= 0 || !s[i]) return false;
	while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;
	prev = wrap_cp(s + j, &len);
	next = wrap_cp(s + i, &len);
#define WRAP_CJK(c) (((c) >= 0x2E80 && (c) <= 0x9FFF) || ((c) >= 0xF900 && (c) <= 0xFAFF) || \
                     ((c) >= 0xFF00 && (c) <= 0xFFEF))
	if (!WRAP_CJK(prev) && !WRAP_CJK(next)) return false;
#undef WRAP_CJK
	for (k = 0; k < sizeof closes / sizeof closes[0]; k++) if (next == closes[k]) return false;
	for (k = 0; k < sizeof opens / sizeof opens[0]; k++) if (prev == opens[k]) return false;
	return true;
}

/* Break `src` into lines no wider than `w`, at whitespace, and in Chinese,
 * which has none, between its characters (wrap_cjk_break). Returns how many.
 *
 * menu_draw marquees a note that does not fit, which is right for a footer
 * carrying four facts and wrong for a sentence: the reader has to wait for the
 * text to come back around to re-read a clause. Three still lines beat one
 * moving one, so a description is wrapped here rather than handed over long.
 *
 * A word wider than the panel is cut instead of hunted for a space, because
 * the alternative is a line that overflows and there is nowhere else for it to
 * go. Measuring per prefix is quadratic in the line, which nobody will notice
 * at 191 characters, once, when the card opens.
 *
 * LINE BREAKS IN THE SOURCE ARE PARAGRAPHS, and this used to walk straight past
 * them: only ' ' broke a line and only ' ' was skipped, so a '\n' rode into the
 * middle of a line and SDL_ttf drew it as nothing - a hole two characters wide
 * after "Gunstar-9!!" with the next paragraph running on behind it. Of the 78
 * ScreenScraper synopses on this card 47 carry a break and 42 carry a blank
 * line; splitting at every break gives 191 pieces with a median of 276
 * characters, which is paragraphs rather than a source that hard-wraps. So any
 * run of whitespace containing a break ends the paragraph and gets one empty
 * row, however many blank lines the run actually held. Never at the top, where
 * it would only push the first line down off the rule. */
static int wrap_text(TTF_Font *f, const char *src, int w,
                     char out[][CHV_WRAP_COLS], int max)
{
	int n = 0;

	while (n < max) {
		char buf[CHV_WRAP_COLS];
		int take = 0, fit = 0, i, len;
		bool para = false;

		while (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t') {
			if (*src == '\n' || *src == '\r') para = true;
			src++;
		}
		if (!*src) break;
		if (para && n > 0) {
			out[n++][0] = '\0';
			if (n >= max) break;
		}
		for (i = 1; src[i - 1] && src[i - 1] != '\n' && src[i - 1] != '\r' &&
		            src[i - 1] != '\t'; i++) {
			if (i >= (int)sizeof buf) break;
			/* Inside a character's bytes: measured, and cut, only at its
			 * end, so no line ends in half a character */
			if (((unsigned char)src[i] & 0xC0) == 0x80) continue;
			memcpy(buf, src, (size_t)i);
			buf[i] = '\0';
			if (ui_text_width(f, buf) > w) break;
			fit = i;
			if (src[i] == ' ' || src[i] == '\n' || src[i] == '\r' ||
			    src[i] == '\t' || src[i] == '\0' || wrap_cjk_break(src, i)) take = i;
		}
		if (!take) {                    /* one word longer than the panel */
			wrap_cp(src, &len);
			take = fit > 0 ? fit : len;
			if (take >= (int)sizeof buf) take = sizeof buf - 1;
		}
		memcpy(out[n], src, (size_t)take);
		out[n][take] = '\0';
		n++;
		src += take;
	}
	return n;
}

/* One achievement, opened with A from the list.
 *
 * The list has room for a title and a number, and a title is a NAME RATHER
 * THAN AN INSTRUCTION - it says which achievement, not what to do for it. The
 * description that says has been in memory the whole time, parsed beside the
 * title by src/cheevos.c (parse_meta) and shown nowhere.
 *
 * Drawn with sel = -1, so nothing highlights. This is a card rather than a
 * list: there is nothing here to choose between, and a cursor would invite
 * pressing A on a row that does not answer.
 *
 * Returns true if the caller should close too, which is what MENU and power
 * mean anywhere else in the launcher. */
/* What the two achievement screens sit on.
 *
 * In a game that is the paused frame under a heavy dim, which is what makes a
 * menu read as being over something that has stopped. On the shelf it is the
 * shelf under the SAME lighter dim the game details screen uses, so opening
 * Cheevos from that screen does not darken the room on the way. The scrim at
 * `k` of its strength (panel_frame). */
static void chv_backdrop(app *a, SDL_Texture *bg, bool over_shelf, float k)
{
	if (!over_shelf) { paused_backdrop(a, bg, k); return; }
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

static bool cheevo_detail_screen(app *a, SDL_Texture *bg, bool over_shelf,
                                 const cheevo *c)
{
	char lines[CHV_WRAP_LINES][CHV_WRAP_COLS];
	menu_row rows[CHV_WRAP_LINES + 3];
	unsigned vcols[CHV_WRAP_LINES + 3] = { 0 };
	char pts[24];
	bool got = c->earned || c->earned_now;
	int fixed = menu_std_width(a);
	int n = 0, nl, i, done = 0, lv;
	bool close_all = false;
	float k;

	nl = wrap_text(ui_font(UI_F_MENU),
	               c->desc[0] ? c->desc : T(S_CHEEVOLIST_NO_DESCRIPTION),
	               fixed, lines, CHV_WRAP_LINES);
	/* Live, unlike an ordinary note. A caption under a setting is a caption;
	 * here the description IS the content, and UI_TEXT_DIM is too quiet to be
	 * the only thing on the card worth reading. */
	for (i = 0; i < nl; i++)
		rows[n++] = (menu_row){ lines[i], MENU_NOTE_MARK, true };
	rows[n++] = MENU_RULE;
	snprintf(pts, sizeof pts, "%d", c->points);
	rows[n++] = (menu_row){ T(S_CHEEVOLIST_POINTS), pts, true };
	rows[n]   = (menu_row){ T(S_CHEEVOLIST_STATUS),
	                        got ? T(S_CHEEVOLIST_EARNED) : T(S_CHEEVOLIST_NOT_EARNED), true };
	vcols[n]  = got ? VCOL_EARNED : 0u;
	n++;

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_BACK);
			done = 1;
		}
		if (a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			done = 1; close_all = true; g_menu_closing = true;
		}
		/* Only over the shelf. Over a game this card is the in-game menu's,
		 * and Muse would need that menu's power rule, which this screen does
		 * not have to give it. SELECT from the in-game menu itself works. */
		if (over_shelf && a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* Same reasoning as the list below: this screen is not inside
		 * plat_resident_wait, so nothing else is watching power for it. Over
		 * the shelf there is no game to stop, and stopping one is not what
		 * power means there. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
			close_all = true;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		chv_backdrop(a, bg, over_shelf, k);
		menu_draw_ex(a, c->title, rows, n, -1, fixed, a->tint, vcols, false, 0);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return close_all;
}

/* The synopsis, opened with A from the game info screen.
 *
 * The same shape as the achievement card above and for the same reason: there
 * is nothing here to choose between, so it is drawn with no cursor and scrolls
 * itself. A synopsis averages 670 characters and the longest on the test card
 * is 2,221, so most of them fit and the long ones walk.
 *
 * Over the shelf rather than a paused frame: this is reached from the shelf,
 * where the info screen already dims the same way. */
/* Room for the text twice over, plus the four blanks and the rule between the
 * copies. The rows are the wrapped lines POINTED AT twice, not wrapped twice,
 * so the line buffer stays SYN_WRAP_LINES - this is the only array that has to
 * grow, and 165 menu_rows is four kilobytes. */

/* The rows a synopsis is drawn as, and where they start repeating.
 *
 * Shared by the screen and by --synopsis, which used to build its own: the
 * harness then drew a card the device does not draw, which is the one thing a
 * harness must never do. Returns the row count; `rows` must hold SYN_ROWS. */
static int syn_layout(const char *text, int fixed,
                      char (*lines)[CHV_WRAP_COLS], menu_row *rows,
                      int *loop_at)
{
	int n, i, k;

	*loop_at = 0;
	n = wrap_text(ui_font(UI_F_MENU), text, fixed, lines, SYN_WRAP_LINES);
	for (i = 0; i < n; i++)
		rows[i] = (menu_row){ lines[i], MENU_BODY_MARK, true };

	/* A SYNOPSIS TOO TALL FOR THE PANEL IS LAID OUT TWICE, with two blank
	 * lines, a rule and two blank lines between the copies, and menu_draw is
	 * told where the repeat starts.
	 *
	 * Both spacings were looked at on the device 2026-09-17. The rule carries
	 * air of its own - it is the bar the heading uses, with the heading's gap
	 * under it - and on its own it reads as a line ruled THROUGH the prose,
	 * with the last sentence and the first pressed against it. The blank lines
	 * are what make it the end of something.
	 *
	 * That is what turns the scroll from a ping-pong into a scroll-through.
	 * Running backwards is fine for a row of text too wide for its column,
	 * where the rewind is obviously a rewind; over fifteen lines of prose it
	 * means reading the end in reverse to get back to the beginning. Coming
	 * round instead needs somewhere to come round FROM, which is the rule: it
	 * says the text ended and is starting again, rather than leaving a reader
	 * to work out that the sentence they are on is one they have read.
	 *
	 * Only when it does not fit. A short synopsis sits still, and doubling it
	 * would make a page that fits scroll for no reason. */
	if (n <= menu_notes_fit()) return n;

	k = n;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_HR;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	*loop_at = k;
	for (i = 0; i < n && k < SYN_ROWS; i++)
		rows[k++] = (menu_row){ lines[i], MENU_BODY_MARK, true };
	return k;
}

/* ---- Muse: covers --------------------------------------------------------- */

bool file_nonempty(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && st.st_size > 0;
}

/* Album `al`'s cover, when there is one on the card: true, with where in
 * `out`.
 *
 * The first time an album is asked about, a cover already on the card - from
 * an earlier visit - is found by looking. Otherwise the daemon is asked for the
 * one the music carries, and this answers false until cover_answers hears
 * back, a few milliseconds later. */
static bool cover_file(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	cover_state *c;
	unsigned now = plat_now_ms();

	if (!g_cov || al < 0 || al >= g_muse.nalbums) return false;
	c = &g_cov[al];
	ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
	if (!base[0]) return false;
	/* Singles' is its own, the one put there for it or one put in its place,
	 * never a song's: its songs have nothing to do with one another, and the
	 * first one's picture became the album's (2026-10-05). So it is looked
	 * for each time and Muse is never asked. */
	if (g_muse.albums[al].singles) {
		snprintf(out, n, "%s.jpg", base);
		if (file_nonempty(out)) return true;
		snprintf(out, n, "%s.png", base);
		return file_nonempty(out);
	}
	if (c->st == COV_JPG || c->st == COV_PNG) {
		snprintf(out, n, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		return true;
	}
	if (c->st == COV_FOLDER) return ml_folder_image(g_muse_root, &g_muse, al, out, n);
	if (c->st == COV_NONE) return false;
	if (c->st == COV_ASKED && now - c->asked_ms < 5000) return false;

	snprintf(out, n, "%s.jpg", base);
	if (file_nonempty(out)) { c->st = COV_JPG; return true; }
	snprintf(out, n, "%s.png", base);
	if (file_nonempty(out)) { c->st = COV_PNG; return true; }
	/* A video's picture beside it before one of its frames: the daemon
	 * answers every video with a frame when it carries no cover (src/muse/
	 * cover.c), so the folder would never be looked at after it. */
	if (g_muse.albums[al].kind == ML_VIDEOS &&
	    ml_folder_image(g_muse_root, &g_muse, al, out, n)) {
		c->st = COV_FOLDER;
		return true;
	}
	if (musec_cover_ask(g_muse.tracks[g_muse.albums[al].first].path, base)) {
		c->st = COV_ASKED;
		c->asked_ms = now;
	}
	return false;
}

/* What is known about album `al`'s cover, forgotten when the file it names is
 * no longer on the card, so cover_file looks again and asks Muse for the one
 * the music carries. The transfer screen's: files there come and go under the
 * launcher, and a cover moved off the card stayed "a JPG" until a restart, so
 * its embedded picture was never copied out again (2026-10-05). */
static void cover_recheck(int al)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	cover_state *c;

	if (!g_cov || al < 0 || al >= g_muse.nalbums) return;
	c = &g_cov[al];
	if (c->st == COV_JPG || c->st == COV_PNG) {
		ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
		snprintf(p, sizeof p, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		if (!file_nonempty(p)) c->st = COV_UNKNOWN;
	} else if (c->st == COV_FOLDER &&
	           !ml_folder_image(g_muse_root, &g_muse, al, p, sizeof p)) {
		c->st = COV_UNKNOWN;
	}
}

/* What the daemon said about the covers it was asked for. Matched by path, not
 * by album number, so an answer that lands after a rescan finds the album it
 * was about or none at all. True when anything landed, so a shelf that only
 * draws when something changes knows it has. */
static bool cover_answers(void)
{
	char base[LIB_PATH * 2], file[LIB_PATH * 2 + 8], b[LIB_PATH * 2];
	bool landed = false;
	size_t fl;
	int i;

	while (musec_cover_take(base, sizeof base, file, sizeof file)) {
		landed = true;
		fl = strlen(file);
		for (i = 0; g_cov && i < g_muse.nalbums; i++) {
			char pic[LIB_PATH * 2 + 8];

			ml_cover_base(g_muse_root, &g_muse, i, b, sizeof b);
			if (strcmp(b, base)) continue;
			/* Nothing in the files: a picture in the album's folder, which
			 * is where a book's cover usually is, before the card. */
			g_cov[i].st = fl > 0 ? (fl > 4 && !strcmp(file + fl - 4, ".png") ? COV_PNG : COV_JPG)
			            : ml_folder_image(g_muse_root, &g_muse, i, pic, sizeof pic)
			            ? COV_FOLDER : COV_NONE;
			break;
		}
	}
	return landed;
}

/* A cover's shape, wherever it is drawn: square, and square-cornered - Eric's
 * call, 2026-09-18; a sleeve is a square. An ARGB8888 surface, replaced when
 * it had to be cropped.
 *
 * Square by taking the middle of it. Covers are square nearly always, and the
 * ones that are not are a scan with a few pixels of scanner either side - the
 * first card has one, 245 by 225 - which cropping loses and stretching would
 * put into the picture. */
static void cover_shape(SDL_Surface **ps)
{
	SDL_Surface *s = *ps, *sq;
	int side = s->w < s->h ? s->w : s->h;

	if (s->w != s->h &&
	    (sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32,
	                                         SDL_PIXELFORMAT_ARGB8888))) {
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(s, &(SDL_Rect){ (s->w - side) / 2, (s->h - side) / 2,
		                                side, side }, sq, NULL);
		SDL_FreeSurface(s);
		*ps = sq;
	}
}

/* A cover as Now Playing draws it. NULL when the file will not decode. */
static SDL_Texture *load_cover(SDL_Renderer *r, const char *path)
{
	SDL_Surface *raw = IMG_Load(path), *s;
	SDL_Texture *t;

	if (!raw) return NULL;
	s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!s) return NULL;
	cover_shape(&s);
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

void np_forget(void)
{
	if (g_np.tex) SDL_DestroyTexture(g_np.tex);
	g_np.tex = NULL;
	g_np.album = -1;
	g_np.done = false;
}

/* Bring the Now Playing cover up to date with album `al`, which may be -1 for
 * nothing. Called every frame; it costs a comparison once the cover is in. */
static void np_cover(app *a, int al)
{
	char path[LIB_PATH * 2 + 8];
	int w, h;

	if (al != g_np.album) { np_forget(); g_np.album = al; }
	if (g_np.done || al < 0) return;
	if (cover_file(al, path, sizeof path)) {
		unsigned t0 = plat_now_ms();

		g_np.tex = load_cover(a->r, path);
		fprintf(stderr, "muse: cover for %s in %u ms\n", g_muse.albums[al].name,
		        plat_now_ms() - t0);
		/* There, and not a picture: the card, as for an album with none. */
		if (!g_np.tex) g_cov[al].st = COV_NONE;
	}
	if (!g_np.tex && g_cov[al].st == COV_NONE) {
		g_np.tex = ui_make_cover(a->r, g_muse.albums[al].name, ZYPEROS_ACCENT,
		                         &w, &h);
	}
	g_np.done = g_np.tex != NULL;
}

/* Card `i` on Muse's shelf - game_get_tex's half for Muse. Its album's cover
 * when the card holds one, decoded on the worker like any box art; the
 * generated card when the music carries none, drawn on the worker too;
 * nothing yet while the daemon is being asked, which cover_answers settles a
 * few milliseconds later. Asked for while the slot is empty or holds the card
 * of the theme that was, which it shows until its replacement lands, as a
 * game's slot does. */
static void muse_card_want(app *a, int s, int i)
{
	sysview *v = &a->view[s];
	char path[LIB_PATH * 2 + 8];
	ui_card_order card;
	int al;

	if (!v->album || i < 0 || i >= v->list.count) return;
	al = v->album[i];
	if (al < 0 || al >= g_muse.nalbums) return;
	if (cover_file(al, path, sizeof path)) {
		/* A cover that will not decode is the card in the shelf's color,
		 * as shelf_put has always made it */
		ui_card_order_set(&card, v->list.items[i].title,
		                  a->sys.systems[shelf_owner(a, s, i)].accent, true);
		if (texload_want_or_draw(s, i, path, NULL, &card)) return;
		/* No worker - a shot - so here, as game_get_tex does without one. */
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = load_cover(a->r, path);
		if (v->tex[i]) {
			SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
			v->cb[i] = 1.0f;
			return;
		}
		g_cov[al].st = COV_NONE;             /* there, and not a picture */
	}
	if (g_cov && g_cov[al].st == COV_NONE) {
		ui_card_order_set(&card, v->list.items[i].title, ZYPEROS_ACCENT, true);
		if (texload_want_or_draw(s, i, NULL, NULL, &card)) return;
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = ui_make_cover(a->r, v->list.items[i].title, ZYPEROS_ACCENT,
		                          &v->tw[i], &v->th[i]);
		v->cb[i] = 1.0f;
	}
}

/* ---- Muse: the play mode --------------------------------------------------- */

/* Y ON NOW PLAYING, AND NOWHERE ELSE IN MUSE. Eric's call, 2026-09-19.
 *
 * Y used to cycle the mode on every Muse screen, and because a mark on its own
 * is a new shape to learn, each press named the mode on a pill in the middle
 * of the screen for 1400 ms. On Now Playing that pill said what the mark
 * beside the track count was already saying, a moment later and larger.
 *
 * The mode is now set where it is shown. That leaves the shelf and the lists
 * with nothing to announce, so the pill is gone with them, and Y is free there
 * the way X is. The cost is that the mode cannot be changed before something
 * plays, since Now Playing is a screen about a track - and it is kept across
 * restarts, so it is not a thing anyone sets often. */
static void muse_cycle_mode(void)
{
	muq_mode m = (muq_mode)((musec_mode() + 1) % MUQ_MODES);

	musec_set_mode(m);
	db_set_str(db_dev(), "muse.mode", MUSE_MODES[m].key);
}

/* ---- Muse: what it is open over ------------------------------------------ */

/* NULL is the shelf. The in-game menu hands over its style for as long as Muse
 * is up, because a game is paused underneath: the list is drawn over that
 * game's frame rather than a shelf it is not on, and power stops the game
 * rather than the device - which is what that menu's power rule does, and why
 * it has one. Muse is modal and never opens itself, so one of each is enough. */
static const menu_style *g_muse_over;
static void             *g_muse_over_ctx;
static bool              g_muse_gone;    /* that rule ended what was underneath */

static void muse_backdrop(app *a, float k)
{
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

/* The power button or Auto Off, inside Muse. It closes Muse either way; what
 * else it does is up to whatever Muse is open over. */
static void muse_power(app *a)
{
	if (g_muse_over && g_muse_over->on_power) {
		if (g_muse_over->on_power(a, g_muse_over_ctx) == MENU_DONE)
			g_muse_gone = true;
		return;
	}
	power_off(a);
}

/* ---- Muse: Now Playing ---------------------------------------------------- */

/* How far one step of left or right seeks: ten seconds a tap, and further the
 * longer it is held - a minute a step after a second, five after three.
 * Eric's, 2026-09-29, for books: at ten seconds a step, eleven steps a second,
 * crossing a seventeen-hour book took nine minutes of holding the d-pad, and
 * now takes about twenty seconds. A song is over long before the steps grow,
 * so music is as it was. */
static double seek_step(const in_state *in, in_button b)
{
	Uint32 held = SDL_GetTicks() - in->down_since[b];

	if (in->pressed[b] || held < 1000) return 10;
	return held < 3000 ? 60 : 300;
}

static void mmss(char *out, size_t n, double sec)
{
	int t = sec > 0 ? (int)(sec + 0.5) : 0;

	/* Hours past the hour: a seventeen-hour book read "-1014:46" in minutes. */
	if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	else           snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* The cover where the shelf puts the card it is looking at, in the same kind
 * of light, and the words beside it. Its own screen and not a panel over the
 * shelf: it is the one screen left open for as long as an album lasts, and
 * what is behind a panel is a picture of games. */
#define NP_SIDE 432                              /* the cover, on screen */
#define NP_X    80
#define NP_Y    ((ZYPEROS_SCREEN_H - NP_SIDE) / 2 - 28)
#define NP_TX   (NP_X + NP_SIDE + 56)            /* the words beside it */
#define NP_TW   (ZYPEROS_SCREEN_W - NP_TX - 64)

/* Whether chapter `i` has a title worth reading, and not only its number -
 * Dungeon Crawler Carl's thirty-seven are "001" to "037" - or none. */
static bool chapter_titled(int i)
{
	const char *t = musec_chapter_title(i);
	const char *p;

	for (p = t; *p && (isdigit((unsigned char)*p) || *p == ' '); p++) { }
	return t[0] && *p;
}

/* A chapter as Now Playing names it: its own title when it has one worth
 * reading, "Chapter 3" when the title is only its number or missing. */
static void chapter_label(int i, char *out, size_t n)
{
	if (chapter_titled(i)) snprintf(out, n, "%s", musec_chapter_title(i));
	else                   snprintf(out, n, T(S_MUSE_CHAPTER), i + 1);
}

/* One line of the words, sliding when it is wider than the column. `y` is
 * the top of its line (ui_line_y). */
static void np_line(SDL_Renderer *r, ui_font_role role, const char *s, int y,
                    unsigned phase, SDL_Color col, unsigned *wait)
{
	TTF_Font *f = ui_font(role);
	unsigned w = ui_pingpong_wait(ui_text_width(f, s) - NP_TW, phase);

	ui_text_marquee(r, f, s, NP_TX, ui_line_y(role, y), NP_TW, phase, col);
	if (w < *wait) *wait = w;
}

static void np_legend(char *out, size_t n, const char *const *parts, int np, int w);

/* Returns how long until the picture would change by itself, in ms - 0 while
 * a line is sliding - so the loop can leave the screen alone until then. */
static unsigned np_draw(app *a, const mu_now *mn, const char *next)
{
	SDL_Renderer *r = a->r;
	SDL_Rect cov = { NP_X, NP_Y, NP_SIDE, NP_SIDE };
	TTF_Font *fs = ui_font(UI_F_META);
	/* Muse's lime for everything that is not the cover or the words, the
	 * same its shelf and its menus wear: the light around the cover and along
	 * the foot, and as **accent-ink**, lime as a mark, the bar, the mode's
	 * mark and the word for a stopped track. It was the cover's own most
	 * vivid color once, which made one mark two colors a screen apart; Eric's
	 * call, 2026-09-19. */
	unsigned rgb = ZYPEROS_ACCENT, wait = (unsigned)-1, phase;
	SDL_Color acc = ui_col(theme()->accent_ink, 255);
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);
	int book = musec_is_book() ? ml_album_of(&g_muse, musec_track(mn->index)) : -1;
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[200], fit[200];
	int y, ty, bar, fill, left, right;

	/* A book played to its end says so, rather than that it stopped. */
	if (mn->state == MU_STOPPED && book >= 0 && g_book_done && g_book_done[book])
		state = T(S_MUSE_FINISHED);

	g_frame_no++;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The shelf's wash along the foot, and the lift a cover in Muse gets:
	 * light in Salt & Pepper, and on Beard the wash laid on and a shadow
	 * under the cover (DESIGN.md 7, focus-small). */
	ui_lift(r, &(SDL_Rect){ 0, ZYPEROS_SCREEN_H - 240, ZYPEROS_SCREEN_W, 480 },
	        rgb, UI_LIFT_WASH);
	ui_lift(r, &cov, rgb, UI_LIFT_FOCUS_SMALL);
	if (g_np.tex) SDL_RenderCopy(r, g_np.tex, NULL, &cov);

	/* Title, artist and album slide together when they are too long, on one
	 * clock that starts over with each track. `y` is the top of each line,
	 * and every line is placed by it (ui_line_y). */
	phase = mq_phase(MQ_NP, mn->index, (int)strlen(mn->title));
	y = NP_Y + 36;
	{
		/* Where in the queue, and the mode it is playing in beside it. The
		 * count is the order being HEARD, so shuffled it still runs 1 to n.
		 *
		 * The mark half again the text's height, centered on it: at the
		 * text's own height the 1 in repeat one and shuffle's crossing were
		 * too small to read at arm's length. Eric, 2026-09-19.
		 *
		 * Centered on the ink, not the em box: the box keeps a descender's
		 * depth below the baseline that "1 of 12" never uses, so its middle
		 * sat the mark 3.5px low on the device. On the capitals' middle,
		 * which is where ui_line_y puts them in the line. */
		int th = ui_font_line(UI_F_META), gs = th * 3 / 2, gx = NP_TX;
		/* No mark on a book: it plays in order, whatever the mode. */
		int g = musec_is_book() ? -1 : MUSE_MODES[musec_mode()].glyph;

		ty = ui_line_y(UI_F_META, y);

		/* A book with chapters counts those instead of its one file. */
		if (musec_is_book() && musec_chapters() > 1) {
			snprintf(line, sizeof line, T(S_MUSE_CHAPTER_OF),
			         musec_chapter_now() + 1, musec_chapters());
			gx += ui_text(r, fs, line, NP_TX, ty, -1, UI_TEXT_DIM) + 18;
		} else if (mn->count > 1) {
			snprintf(line, sizeof line, "%d / %d", mn->index + 1, mn->count);
			/* Not tabular: it changes with the track, not every second,
			 * and a 1 in a fixed-width cell read as "1 of  14". */
			gx += ui_text(r, fs, line, NP_TX, ty, -1, UI_TEXT_DIM) + 18;
		}
		if (g >= 0) ui_glyph_draw(r, (ui_glyph)g, gx + gs / 2, y + th * 2 / 5, gs, acc);
		/* A book's speed where music's mode mark would be, and only when it
		 * is not the ordinary one. */
		if (musec_is_book() && fabs(musec_speed() - 1.0) > 0.01) {
			snprintf(line, sizeof line, "%gx", musec_speed());
			ui_text(r, fs, line, gx, ty, -1, acc);
		}
	}
	/* Room under it for the larger mark and for the title to stand clear of
	 * the line above - which read as one block with it at 6px. */
	y += ui_font_line(UI_F_META) + 24;
	{
		/* A chapter with a title of its own is what is playing; one that
		 * is only a number leaves the book's title there. */
		char chap[128];
		const char *top = mn->title;

		if (musec_is_book() && musec_chapters() > 1) {
			int ci = musec_chapter_now() < 0 ? 0 : musec_chapter_now();

			/* The book's own "Chapter 3" is only a number too */
			chapter_label(ci, chap, sizeof chap);
			if (chapter_titled(ci) && strncmp(chap, "Chapter ", 8) != 0) top = chap;
		}
		np_line(r, UI_F_TITLE, top, y, phase, UI_TEXT, &wait);
	}
	y += ui_font_line(UI_F_TITLE) + 10;
	np_line(r, UI_F_MENU, mn->artist, y, phase, UI_TEXT_SOFT, &wait);
	y += ui_font_line(UI_F_MENU);
	np_line(r, UI_F_MENU, mn->album, y, phase, UI_TEXT_DIM, &wait);

	/* Where in the track. What is left rather than the whole length on the
	 * right, because the question is usually how long until the next one. */
	bar = NP_Y + NP_SIDE - 132;
	ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, NP_TW, UI_BAR_H }, UI_BAR_H / 2,
	              ui_col(theme()->track, 255));
	fill = (int)(NP_TW * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, fill, UI_BAR_H }, UI_BAR_H / 2,
		              state ? ui_fade(acc, 0.45f) : acc);
	/* Tabular, so the clock ticking does not move what sits beside it. */
	mmss(t0, sizeof t0, mn->at);
	ty = ui_line_y(UI_F_META, bar + 16);
	left = NP_TX + ui_text_tabular(r, fs, t0, NP_TX, ty, -1, UI_TEXT_SOFT);
	right = NP_TX + NP_TW;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, NP_TX + NP_TW, ty, 1, UI_TEXT_SOFT);
	}
	/* Between the two times rather than on the column's middle, which a book's
	 * hours pushed them into: "16:54:47Stopped". */
	if (state) ui_text(r, fs, state, (left + right) / 2, ty, 0, acc);

	/* The last line, its line ending where the cover does. */
	if (next && next[0]) {
		snprintf(line, sizeof line, T(S_MUSE_NEXT), next);
		ui_fit_text(fs, line, fit, sizeof fit, NP_TW);
		ui_text(r, fs, fit, NP_TX,
		        ui_line_y(UI_F_META, NP_Y + NP_SIDE - ui_font_line(UI_F_META)), -1,
		        UI_TEXT_DIM);
	}

	/* The keys, as much of them as fits the screen (np_legend): English's
	 * longest, a book's, is about 950 of the 1008 */
	{
		const char *parts[4] = {
			state ? T(S_MUSE_HINT_PLAY) : T(S_MUSE_HINT_PAUSE),
			!musec_is_book() ? T(S_MUSE_HINT_TRACK)
			: musec_chapters() > 1 ? T(S_MUSE_HINT_CHAPTER) : T(S_MUSE_HINT_FILE),
			T(S_MUSE_HINT_SEEK),
			musec_is_book() ? T(S_MUSE_HINT_SPEED) : T(S_MUSE_HINT_MODE),
		};

		/* All four, on two lines where a language's words do not fit one:
		 * a key left out is a cut (DESIGN.md 12) */
		np_legend(line, sizeof line, parts, 4, ZYPEROS_SCREEN_W - 2 * DS_GRID);
		if (strlen(line) < strlen(parts[0]) + strlen(parts[1]) + strlen(parts[2]) +
		                   strlen(parts[3]) + 9) {
			char more[200];
			int k = 0;

			while (k < 4 && strstr(line, parts[k])) k++;
			np_legend(more, sizeof more, parts + k, 4 - k, ZYPEROS_SCREEN_W - 2 * DS_GRID);
			ui_text(r, fs, line, ZYPEROS_SCREEN_W / 2,
			        ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - 72 - 46), 0, UI_TEXT_DIM);
			ui_text(r, fs, more, ZYPEROS_SCREEN_W / 2,
			        ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - 72), 0, UI_TEXT_DIM);
		} else {
			ui_text(r, fs, line, ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - 72),
			        0, UI_TEXT_DIM);
		}
	}
	{
		unsigned corner = draw_corner(r, theme_index(), UI_TEXT_SOFT,
		                              wait <= 50 || frames_wanted());
		if (corner < wait) wait = corner;
	}
	return wait;
}

/* ---- MP4: the lock screen -------------------------------------------------
 *
 * mine, 2026-11-05, in place of Muse's pocket lock (#51, Eric's design,
 * 2026-09-30), whose chord - F1+F2 held for a second, FUNCTION and both volume
 * keys on the Pixel 2 - it replaced. X on any MP4 screen while something
 * plays, or MENU's Lock Screen, locks. While locked every button but the
 * volume keys is blocked, POWER included, and SELECT and START held together
 * for hold-unlock unlock: the rules are src/mp4lock.h's, and why those.
 *
 * The lock screen - Now Playing under the scrim, and over it a panel saying
 * it is locked and how to open it, with a bar the hold fills - shows for
 * hold-lock-lit as it locks, and then only while the pair is held. Music:
 * otherwise dark, the backlight out, whatever is pressed (My pick, "dark
 * until unlocked") - a book playing drew about 470 mA with it on and 300 with
 * it off, measured before #51 was built - except the volume keys, which
 * light the lock screen with the volume bar over it for as long as the bar
 * shows, and then let it go dark again. A video keeps playing in sight,
 * since that is what it was locked to watch, and the volume bar comes over
 * the picture alone.
 *
 * A headset's play, pause and skip still work: musec_poll reads them, and it
 * runs here as everywhere. Auto Off still powers off a locked player once the
 * music has stopped long enough - it is no button. */

static bool g_lock_asked;                /* MENU's Lock Screen, for muse_menu */

static void muse_ask_lock(void) { g_lock_asked = true; }

/* SHUFFLE ALL: every song on the card in a random order, from MENU on the
 * music shelf. mine, 2026-11-05: shuffle across the whole library, not one
 * album at a time. Music only - a book shuffled is not how anyone listens to
 * one, and a film is not a song - Singles with the rest.
 *
 * The mode becomes shuffle and is kept, as with Y on Now Playing: shuffle is
 * a switch on every player that has one, and the next album played is
 * shuffled too until Y moves it on. One queue of every track, so "3 / 1200"
 * counts the songs heard, and Now Playing names each song's own album and
 * artist (muse_track_names) where an album's queue names its one. */
static bool g_shuffle_asked;             /* MENU's Shuffle All, for muse_menu */

static void muse_ask_shuffle(void) { g_shuffle_asked = true; }

static bool muse_shuffle_all(void)
{
	const char **paths = calloc((size_t)(g_muse.ntracks > 0 ? g_muse.ntracks : 1), sizeof *paths);
	int i, t, n = 0;

	if (!paths) return false;
	for (i = 0; i < g_muse.nalbums; i++)
		if (g_muse.albums[i].kind == ML_MUSIC)
			for (t = 0; t < g_muse.albums[i].n; t++)
				paths[n++] = g_muse.tracks[g_muse.albums[i].first + t].path;
	if (n > 0) {
		musec_set_mode(MUQ_SHUFFLE);
		db_set_str(db_dev(), "muse.mode", MUSE_MODES[MUQ_SHUFFLE].key);
		/* Shuffled, the start plays first and the rest follow in a random
		 * order (musequeue.h), so the start is drawn at random too. */
		muse_play(paths, n, (int)((plat_now_ms() * 2654435761u >> 7) % (unsigned)n), 0,
		          false, 1.0, NULL, NULL);
		fprintf(stderr, "mp4: shuffle all, %d songs\n", n);
	}
	free(paths);
	return n > 0;
}

/* Whether a lock can happen here: something playing, and MP4 not opened over
 * a paused game, whose power rule belongs to the game. */
static bool lock_allowed(void)
{
	return musec_playing() && !g_muse_over;
}

/* The volume and brightness keys on an MP4 screen, and X. True when the
 * screen should lock. */
static bool muse_keys(app *a)
{
	in_state *in = &a->in;

	if (in_repeat(in, IN_VOLUP)) plat_volume_nudge(+1);
	if (in_repeat(in, IN_VOLDN)) plat_volume_nudge(-1);
	if (in_repeat(in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
	if (in_repeat(in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
	if (in->pressed[IN_X]) {
		if (lock_allowed()) { uisound_play(DS_SOUND_ACCEPT); return true; }
		uisound_play(DS_SOUND_DENY);       /* nothing playing to lock over */
	}
	return false;
}

/* The padlock, drawn: a ring for the shackle and a rounded body over its
 * lower half. On a panel of `ground`, which the ring is cut out with. */
#define LOCK_MARK 84
static void lock_mark(SDL_Renderer *r, int cx, int top, SDL_Color ink, SDL_Color ground)
{
	int s = LOCK_MARK, sw = s * 46 / 100, t = s / 9;
	SDL_Rect shackle = { cx - sw / 2, top, sw, s * 62 / 100 };
	SDL_Rect hole = { shackle.x + t, shackle.y + t, sw - 2 * t, shackle.h };
	SDL_Rect body = { cx - s * 36 / 100, top + s * 38 / 100, s * 72 / 100, s * 62 / 100 };

	ui_round_rect(r, &shackle, sw / 2, ink);
	ui_round_rect(r, &hole, hole.w / 2, ground);
	ui_round_rect(r, &body, DS_RADIUS_ROW * s / 96, ink);
}

/* The panel over the dimmed screen: the mark, "Locked", how to open it, and
 * the bar the hold fills. In the theme's colors, over a video or not. */
static void lock_panel(app *a, const mp4lock *l)
{
	SDL_Renderer *r = a->r;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_META);
	SDL_Color ink = ui_col(theme()->accent_ink, 255), text = UI_TEXT, dim = UI_TEXT_DIM;
	SDL_Color panel = ui_col(theme()->surface, 255);
	int pw = 720, ph = 360, px = (ZYPEROS_SCREEN_W - pw) / 2, py = (ZYPEROS_SCREEN_H - ph) / 2;
	int y = py + 40, cx = ZYPEROS_SCREEN_W / 2, gs = 52, x, w;
	/* "Hold %s to unlock": the words either side of the pair, which is
	 * drawn, their spaces off */
	const char *fmt = T(S_MP4_HOLD_UNLOCK), *at = strstr(fmt, "%s");
	const char *plus = "+", *tail = at ? at + 2 : "";
	char hold[64];
	int hn = at ? (int)(at - fmt) : (int)strlen(fmt);
	float k = mp4lock_progress(l);

	while (hn > 0 && fmt[hn - 1] == ' ') hn--;
	snprintf(hold, sizeof hold, "%.*s", hn, fmt);
	while (*tail == ' ') tail++;

	ui_panel(r, &(SDL_Rect){ px, py, pw, ph }, DS_RADIUS_PANEL);
	lock_mark(r, cx, y, ink, panel);
	y += LOCK_MARK + 20;
	ui_text(r, ft, T(S_MP4_LOCKED), cx, ui_line_y(UI_F_TITLE, y), 0, text);
	y += ui_font_line(UI_F_TITLE) + 20;
	/* "Hold (SELECT) + (START) to unlock", the buttons drawn (DESIGN.md
	 * 11.9), the pair mp4lock.h unlocks with. */
	w = (hold[0] ? ui_text_width(fm, hold) + 14 : 0) + ui_button_width("SELECT", gs) + 12 +
	    ui_text_width(fm, plus) + 12 + ui_button_width("START", gs) +
	    (tail[0] ? 14 + ui_text_width(fm, tail) : 0);
	x = cx - w / 2;
	{
		int ty = ui_line_y(UI_F_META, y + (gs - ui_font_line(UI_F_META)) / 2), mid = y + gs / 2;

		if (hold[0]) x += ui_text(r, fm, hold, x, ty, -1, dim) + 14;
		ui_button(r, "SELECT", x + ui_button_width("SELECT", gs) / 2, mid, gs, UI_BUTTON_PLAIN);
		x += ui_button_width("SELECT", gs) + 12;
		x += ui_text(r, fm, plus, x, ty, -1, dim) + 12;
		ui_button(r, "START", x + ui_button_width("START", gs) / 2, mid, gs, UI_BUTTON_PLAIN);
		x += ui_button_width("START", gs) + 14;
		if (tail[0]) ui_text(r, fm, tail, x, ty, -1, dim);
	}
	y += gs + 28;
	/* The hold, as the one bar every hold in the system is (UI_BAR_H). */
	{
		SDL_Rect bar = { cx - 200, y, 400, UI_BAR_H };

		ui_round_rect(r, &bar, UI_BAR_H / 2, ui_col(theme()->track, 255));
		bar.w = (int)(400 * k + 0.5f);
		if (bar.w > 0) ui_round_rect(r, &bar, UI_BAR_H / 2, ink);
	}
}

static void np_draw_video(app *a, const mu_now *mn, float k);
static void np_next_name(char *next, size_t n);

/* Locked, until SELECT and START are held, or Auto Off. See above. */
static void muse_locked(app *a)
{
	mp4lock l = { 0 };
	bool panel_was = true, dark = false;
	unsigned shown_serial = 0;
	int drawn_k = -1, drawn_sec = -1;
	Uint32 osd_due = UINT32_MAX;            /* the volume bar's next look */

	fprintf(stderr, "mp4: locked\n");
	mp4lock_engage(&l, plat_now_ms(), a->in.down[MP4_UNLOCK_A] && a->in.down[MP4_UNLOCK_B]);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!want_quit && a->running) {
		bool video, lit, awake, panel, fresh = false, vol = false;
		const mu_now *mn;

		muse_screen_poll();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		/* Auto Off, once the music has stopped long enough: no button, so
		 * the lock does not stand in its way. The light first, so the
		 * power-off animation is seen. */
		if (idle_due(a)) {
			plat_backlight(true);
			muse_power(a);
			break;
		}
		if (mp4lock_step(&l, plat_now_ms(), a->in.down, a->in.pressed)) break;
		/* The volume keys, the two that get through (mp4lock_passes): the
		 * same steps and the same bar as unlocked, repeating when held. */
		if (in_repeat(&a->in, IN_VOLUP)) { plat_volume_nudge(+1); vol = true; }
		if (in_repeat(&a->in, IN_VOLDN)) { plat_volume_nudge(-1); vol = true; }
		mn = musec_now();
		video = musec_video();
		lit = mp4lock_lit(&l);
		awake = mp4lock_awake(&l);
		/* The lock panel: over music whenever the screen is on, the volume
		 * bar over it; over a video only as it locks and while the pair is
		 * held, so a volume press brings the bar alone over the picture. */
		panel = video ? lit : awake;
		musec_video_show(video);
		if (video) fresh = mp4v_update(a->r);

		if (!video && !awake) {
			/* Dark: nothing drawn, a present would be work for nobody. */
			if (!dark) { plat_backlight(false); dark = true; }
			panel_was = false;
			osd_due = UINT32_MAX;
			SDL_Delay(30);
			continue;
		}
		/* Drawn for a new picture, the ring moving, the light coming on, a
		 * volume step or the bar moving, or a new second on the clock - not
		 * every pass of a lit screen. */
		if (fresh || vol || panel != panel_was || shown_serial == 0 ||
		    (osd_due != UINT32_MAX && (int)(plat_now_ms() - osd_due) >= 0) ||
		    (int)(mp4lock_progress(&l) * 100) != drawn_k || (int)(mn->at) != drawn_sec) {
			char next[128] = "";

			tick_theme(a);
			if (video) {
				np_draw_video(a, mn, 0);
			} else {
				np_next_name(next, sizeof next);
				np_draw(a, mn, next);
			}
			if (panel) {
				ui_scrim(a->r, 0);
				lock_panel(a, &l);
			}
			/* Asked after the draw, which retires a bar whose time is up. */
			plat_draw_osd(a->r);
			osd_due = plat_osd_until();
			plat_present();
			shown_serial++;
			drawn_k = (int)(mp4lock_progress(&l) * 100);
			drawn_sec = (int)mn->at;
			/* The light after the frame, so it comes on to the lock screen
			 * rather than to whatever the panel held last. */
			if (dark) { plat_backlight(true); dark = false; }
		}
		panel_was = panel;
		SDL_Delay(video || awake ? 8 : 30);
	}
	musec_video_show(false);
	plat_backlight(true);
	fprintf(stderr, "mp4: unlocked\n");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* MENU, from any Muse screen: Muse's own menu, the one its shelf has always
 * had. One button, one meaning, wherever in Muse it is pressed - it used to
 * close Muse from Now Playing and the tracks, and open this on the shelf.
 * Eric's call, 2026-09-23.
 *
 * Rescan Folder inside it rebuilds the library, and every album index with it,
 * so a screen holding one cannot carry on: MUSE_REBUILT tells it to let go,
 * and the shelf finds itself again the way it does after its own MENU. */
static int g_np_depth;                   /* Now Playing is open, under whatever is */

static muse_exit muse_now_screen(app *a);

static muse_exit muse_menu(app *a)
{
	unsigned gen = g_muse_gen;
	bool by_menu;

	zyperos_menu_for(a, SCREEN_GAMES);
	/* MENU inside it closed it, and only it: Muse is still up, and may itself
	 * be over a menu SELECT opened it from, which has to stay open. So the
	 * screen under it comes back as after B. */
	by_menu = g_menu_closing;
	g_menu_closing = false;
	if (by_menu) panel_return();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* Lock Screen closed the menu to lock from the screen underneath. */
	if (g_lock_asked) {
		g_lock_asked = false;
		if (lock_allowed()) muse_locked(a);
	}
	if (want_quit || !a->running || g_message_stopped) return MUSE_CLOSE;
	/* Shuffle All closed it to play: Now Playing, on whatever screen of
	 * MP4's MENU was pressed - on Now Playing itself, it just carries on
	 * with the new song (g_np_depth). */
	if (g_shuffle_asked) {
		g_shuffle_asked = false;
		if (muse_shuffle_all() && !g_np_depth) {
			muse_exit how = muse_now_screen(a);

			if (how != MUSE_BACK) return how;
		}
	}
	return g_muse_gen != gen ? MUSE_REBUILT : MUSE_BACK;
}

/* What the mode will actually play next, for "Next:". Repeat one says so
 * with its mark, and "Next:" naming the same song reads as a mistake. In a
 * book with chapters, the next chapter, until the last. */
static void np_next_name(char *next, size_t n)
{
	const mu_now *mn = musec_now();
	const char *nt;

	next[0] = '\0';
	if (musec_is_book() && musec_chapters() > 1 &&
	    musec_chapter_now() + 1 < musec_chapters())
		chapter_label(musec_chapter_now() + 1, next, n);
	else if ((musec_is_book() || musec_is_videos() || musec_mode() != MUQ_REPEAT_ONE) &&
	         (nt = musec_upcoming()))
		ml_track_name(strrchr(nt, '/') ? strrchr(nt, '/') + 1 : nt, next, (int)n);
	(void)mn;
}

/* ---- MP4: video -----------------------------------------------------------
 *
 * A file with a picture plays on the same Now Playing, in one of two views
 * Y turns between, kept across restarts (`mp4.view`):
 *
 *   full screen   the picture as large as the screen takes it, on black,
 *                 and the controls over its foot only for hold-controls
 *                 after a press, or while paused - fading in over quick and
 *                 out over fade-out
 *   with controls the picture in a 16:9 frame on the theme's ground, the
 *                 title above it and the bar and keys below, always there
 *
 * The picture itself is the GPU's: the daemon's newest frame uploaded as YUV
 * (src/mp4video.c) and drawn UNDER the screen at the panel's own resolution
 * (plat_video_under), so on the Pixel 2 a 640x360 video is pixel for pixel.
 * The screen is presented when a new frame arrives - the video's own rate,
 * 24 to 30 times a second - and otherwise when the controls change. */

enum { NPV_FULL, NPV_CONTROLS };
static int g_np_view = -1;               /* -1 until read from the settings */

static int np_view(void)
{
	if (g_np_view < 0) {
		char v[16];

		db_get_str(db_dev(), "mp4.view", v, sizeof v, "full");
		g_np_view = !strcmp(v, "controls") ? NPV_CONTROLS : NPV_FULL;
	}
	return g_np_view;
}

static void np_view_toggle(void)
{
	g_np_view = np_view() == NPV_FULL ? NPV_CONTROLS : NPV_FULL;
	db_set_str(db_dev(), "mp4.view", g_np_view == NPV_FULL ? "full" : "controls");
}

/* The 16:9 frame of the view with controls, and the column its words share. */
#define NPV_X 64
#define NPV_W (ZYPEROS_SCREEN_W - 2 * NPV_X)
#define NPV_Y 96
#define NPV_H (NPV_W * 9 / 16)

/* The key legend, as much of it as fits `w`: the parts are dropped from the
 * end, so they are listed most needed first. */
static void np_legend(char *out, size_t n, const char *const *parts, int np, int w)
{
	TTF_Font *f = ui_font(UI_F_META);
	int k;

	for (; np > 0; np--) {
		out[0] = '\0';
		for (k = 0; k < np; k++) {
			size_t at = strlen(out);

			snprintf(out + at, n - at, "%s%s", k ? "   " : "", parts[k]);
		}
		if (ui_text_width(f, out) <= w) return;
	}
	out[0] = '\0';
}

/* The bar, and with `times` the times under it - "1:26", what is left, and
 * the state when it is not playing - from y, `w` wide at x, in alpha `a`.
 * Without, the state alone, over the bar's right end. */
static void np_video_bar(SDL_Renderer *r, const mu_now *mn, int x, int y, int w, Uint8 a,
                         bool times)
{
	TTF_Font *fs = ui_font(UI_F_META);
	SDL_Color acc = ui_col(theme()->accent_ink, a), soft = UI_TEXT_SOFT;
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[24];
	int fill, ty, left, right;

	soft.a = a;
	ui_round_rect(r, &(SDL_Rect){ x, y, w, UI_BAR_H }, UI_BAR_H / 2, ui_col(theme()->track, a));
	fill = (int)(w * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ x, y, fill, UI_BAR_H }, UI_BAR_H / 2,
		              state ? ui_fade(acc, 0.45f) : acc);
	if (!times) return;
	mmss(t0, sizeof t0, mn->at);
	ty = ui_line_y(UI_F_META, y + 16);
	left = x + ui_text_tabular(r, fs, t0, x, ty, -1, soft);
	right = x + w;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, x + w, ty, 1, soft);
	}
	if (state) ui_text(r, fs, state, (left + right) / 2, ty, 0, acc);
}

/* One frame of the video view, the controls at `k` (0 hidden, 1 shown; the
 * view with controls always has them). */
static void np_draw_video(app *a, const mu_now *mn, float k)
{
	SDL_Renderer *r = a->r;
	SDL_Texture *tex = mp4v_texture();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	bool full = np_view() == NPV_FULL;
	SDL_Rect box = full ? (SDL_Rect){ 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H }
	                    : (SDL_Rect){ NPV_X, NPV_Y, NPV_W, NPV_H };
	SDL_Rect pic = mp4v_fit(box);
	ds_rgb g = full ? DS_THEME[DS_DARK].ink : theme()->ground;
	const char *parts[5];
	char legend[200], fit[200];
	int np = 0;
	Uint8 al;

	if (!full) k = 1;
	al = (Uint8)(255 * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5f);
	plat_video_under(tex, &pic, (SDL_Color){ g.r, g.g, g.b, 255 });
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

	parts[np++] = mn->state == MU_PLAYING ? T(S_MUSE_HINT_PAUSE) : T(S_MUSE_HINT_PLAY);
	parts[np++] = T(S_MUSE_HINT_SEEK);
	parts[np++] = T(S_MP4_HINT_LOCK);
	parts[np++] = full ? T(S_MP4_HINT_CONTROLS) : T(S_MP4_HINT_FULL_SCREEN);
	if (mn->count > 1) parts[np++] = T(S_MP4_HINT_VIDEO);
	np_legend(legend, sizeof legend, parts, np, ZYPEROS_SCREEN_W - 2 * NPV_X);

	if (full) {
		/* The controls over the picture's foot, on the theme's surface, all
		 * of it fading as one. */
		SDL_Rect band = { 0, ZYPEROS_SCREEN_H - 200, ZYPEROS_SCREEN_W, 200 };
		SDL_Color text = UI_TEXT, dim = UI_TEXT_DIM;

		SDL_Color soft = UI_TEXT_SOFT;
		char t0[16], t1[16], when[40];
		int tw;

		if (al == 0) return;
		ui_round_rect(r, &band, 0, ui_col(theme()->surface, (Uint8)(al * 0.92f)));
		text.a = dim.a = soft.a = al;
		/* The title and, at its right, where in the video: "1:26 / 2:32:38". */
		mmss(t0, sizeof t0, mn->at);
		mmss(t1, sizeof t1, mn->len);
		snprintf(when, sizeof when, mn->len > 0 ? "%s / %s" : "%s", t0, t1);
		tw = ui_text_tabular(r, fs, when, NPV_X + NPV_W,
		                     ui_line_y(UI_F_META, band.y + 30), 1, soft);
		/* Paused, or stopped at the end, before the time in accent-ink. */
		if (mn->state != MU_PLAYING) {
			SDL_Color acc = ui_col(theme()->accent_ink, al);
			const char *st = mn->state == MU_PAUSED ? T(S_MUSE_PAUSED) : T(S_MUSE_STOPPED);

			tw += 24 + ui_text(r, fs, st, NPV_X + NPV_W - tw - 24,
			                   ui_line_y(UI_F_META, band.y + 30), 1, acc);
		}
		ui_fit_text(fm, mn->title, fit, sizeof fit, NPV_W - tw - 32);
		ui_text(r, fm, fit, NPV_X, ui_line_y(UI_F_MENU, band.y + 22), -1, text);
		np_video_bar(r, mn, NPV_X, band.y + 100, NPV_W, al, false);
		ui_text(r, fs, legend, ZYPEROS_SCREEN_W / 2,
		        ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - 62), 0, dim);
		return;
	}
	/* With controls: the title where the shelf's heading is, the clock and
	 * marks in their corner, the frame, then the bar and the keys. */
	ui_fit_text(fm, mn->title, fit, sizeof fit, NPV_W - 300);
	ui_text(r, fm, fit, NPV_X, ui_line_y(UI_F_MENU, 26), -1, UI_TEXT);
	draw_corner(r, theme_index(), UI_TEXT_SOFT, true);
	if (!tex) {
		/* Before the first picture: the frame, empty, so nothing jumps. */
		SDL_Color ink = { 0, 0, 0, 255 };

		ui_round_rect(r, &box, 0, ink);
	}
	np_video_bar(r, mn, NPV_X, NPV_Y + NPV_H + 24, NPV_W, 255, true);
	ui_text(r, fs, legend, ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_META, ZYPEROS_SCREEN_H - 62), 0, UI_TEXT_DIM);
}

/* MUSE_CLOSE for SELECT and the power button, which leave Muse altogether;
 * MUSE_BACK for B, which is the album's tracks underneath; MUSE_REBUILT when
 * MENU's Rescan Folder replaced the library. MENU otherwise comes back here. */
static muse_exit muse_now_screen_run(app *a)
{
	struct {
		mu_state st;
		int at, len, index, count, mode, chapter, speed;
		int battery_pct;
		bool charging, video;
		char title[128], artist[128], album[128];
		SDL_Texture *tex;
	} shown, drawn;
	unsigned due = 0, ctl_until = 0, ctl_at = 0;
	float ctl = 1, ctl_drawn = -1;
	muse_exit how = MUSE_BACK;

	mq_reset(MQ_NP);
	memset(&drawn, 0, sizeof drawn);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	ctl_until = plat_now_ms() + DS_MS_HOLD_CONTROLS;
	ctl_at = plat_now_ms();

	while (!want_quit && a->running) {
		const mu_now *mn;
		char next[128] = "";
		unsigned now;
		bool touched = false, video, fresh = false;
		int b;

		muse_screen_poll();
		cover_answers();
		mn = musec_now();
		video = musec_video();
		/* Pictures are decoded only while somebody looks at them. */
		musec_video_show(video);
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_SELECT]) uisound_play(DS_SOUND_BACK);
		if (a->in.pressed[IN_BACK]) break;
		if (a->in.pressed[IN_SELECT]) { how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_MENU]) {
			musec_video_show(false);
			how = muse_menu(a);
			if (how != MUSE_BACK) break;
			memset(&drawn, 0, sizeof drawn);     /* the menu drew over it */
			continue;
		}

		/* A, and only A. X did the same thing here and pausing in the list,
		 * which made one button mean two things a screen apart; X is the lock
		 * now, on every MP4 screen. Eric's call, 2026-09-19; X, 2026-11-05. */
		if (a->in.pressed[IN_ACCEPT])       { uisound_play(DS_SOUND_ACCEPT); musec_toggle(); }
		/* The mode, on music; on a book, which plays in order whatever the
		 * mode says, its speed; on a video, the view. */
		if (a->in.pressed[IN_Y]) {
			uisound_play(DS_SOUND_ACCEPT);
			if (video)                 np_view_toggle();
			else if (musec_is_book())  book_cycle_speed();
			else if (!musec_is_videos()) muse_cycle_mode();
		}
		if (in_repeat(&a->in, IN_L1))       { moved_sound(a, IN_L1); musec_prev(); }
		if (in_repeat(&a->in, IN_R1))       { moved_sound(a, IN_R1); musec_next(); }
		if (in_repeat(&a->in, IN_LEFT))     { moved_sound(a, IN_LEFT); musec_seek_by(-seek_step(&a->in, IN_LEFT)); }
		if (in_repeat(&a->in, IN_RIGHT))    { moved_sound(a, IN_RIGHT); musec_seek_by(+seek_step(&a->in, IN_RIGHT)); }
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) { how = MUSE_CLOSE; break; }
			memset(&drawn, 0, sizeof drawn);   /* the screen is back: draw it */
			ctl_drawn = -1;
			continue;
		}

		for (b = 0; b < IN_COUNT && !touched; b++)
			touched = a->in.pressed[b] || a->in.down[b];
		/* A new theme is a change like any other here: every pixel of it. */
		if (tick_theme(a)) touched = true;
		now = plat_now_ms();

		if (video) {
			/* The controls: up for hold-controls after any press, and for
			 * as long as it is paused; in over quick, out over fade-out. */
			float want, step;

			fresh = mp4v_update(a->r);
			for (b = 0; b < IN_COUNT; b++)
				if (a->in.pressed[b]) { ctl_until = now + DS_MS_HOLD_CONTROLS; break; }
			want = mn->state != MU_PLAYING || (int)(ctl_until - now) > 0 ? 1.0f : 0.0f;
			step = (float)(now - ctl_at) / (want > ctl ? DS_MS_QUICK : DS_MS_FADE_OUT);
			ctl = want > ctl ? (ctl + step > want ? want : ctl + step)
			                 : (ctl - step < want ? want : ctl - step);
			ctl_at = now;
			if (fresh || touched || ctl != ctl_drawn || drawn.at != (int)(mn->at + 0.5) ||
			    drawn.st != mn->state || !drawn.video || (int)(now - due) >= 0) {
				Uint32 osd;

				np_draw_video(a, mn, ctl);
				plat_draw_osd(a->r);
				osd = plat_osd_until();
				plat_present();
				ctl_drawn = ctl;
				memset(&drawn, 0, sizeof drawn);
				drawn.video = true;
				drawn.at = (int)(mn->at + 0.5);
				drawn.st = mn->state;
				due = now + 1000;
				if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			}
			/* A frame is due every 33 to 42 ms; looking every 4 catches it
			 * within a few, which LEAD in src/muse/vid.c allows for. */
			SDL_Delay(4);
			continue;
		}

		/* The queue's track, not musec_path: that goes blank when an album
		 * ends, and the screen should still show the album that just did. */
		np_cover(a, ml_album_of(&g_muse, musec_track(mn->index)));
		np_next_name(next, sizeof next);

		/* Drawn when what it shows has changed, not every pass. This is the
		 * screen left open for the length of an album, and a present a frame
		 * for forty minutes of one still picture is battery spent on nothing.
		 * The clock is kept in whole seconds, which is all it shows. */
		memset(&shown, 0, sizeof shown);
		shown.st = mn->state;
		shown.at = (int)(mn->at + 0.5);
		shown.len = (int)(mn->len + 0.5);
		shown.index = mn->index;
		shown.count = mn->count;
		memcpy(shown.title, mn->title, sizeof shown.title);
		memcpy(shown.artist, mn->artist, sizeof shown.artist);
		memcpy(shown.album, mn->album, sizeof shown.album);
		shown.tex = g_np.tex;
		shown.mode = musec_is_book() ? -1 : (int)musec_mode();
		shown.chapter = musec_chapter_now();
		shown.speed = (int)(musec_speed() * 100 + 0.5);
		/* Now Playing has its own draw clock, separate from g_redraw_at.
		 * Include the battery even when paused and no playback time moves. */
		if (!battery_now(&shown.battery_pct, &shown.charging)) shown.battery_pct = -1;

		if (touched || memcmp(&shown, &drawn, sizeof shown) || (int)(now - due) >= 0) {
			unsigned wait = np_draw(a, mn, next);
			Uint32 osd;

			/* Asked after the draw, which is what retires a line whose
			 * time is up - asked before, it names a moment already past. */
			plat_draw_osd(a->r);
			osd = plat_osd_until();
			plat_present();
			drawn = shown;
			/* At least once a second whatever happens, which is also how
			 * often the clock changes. The volume line needs one more frame
			 * at the moment it is due to go. */
			due = now + (wait < 1000 ? wait : 1000);
			if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			SDL_Delay(8);
		} else {
			SDL_Delay(16);
		}
	}

	musec_video_show(false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* Now Playing, counted while it is open, so MENU's Shuffle All knows to
 * carry on in it rather than open another on top (muse_menu). */
static muse_exit muse_now_screen(app *a)
{
	muse_exit how;

	g_np_depth++;
	how = muse_now_screen_run(a);
	g_np_depth--;
	return how;
}

/* ---- Muse: an album's tracks ---------------------------------------------- */

/* The album the queue is on, and the row its track sits at. -1 when nothing
 * is loaded, or when what is loaded is not on the card any more. */
static int muse_playing_album(int *row)
{
	const char *t = musec_track(musec_now()->index);
	int al = ml_album_of(&g_muse, t), i;

	if (row) *row = 0;
	if (al < 0) return -1;
	if (row)
		for (i = 0; i < g_muse.albums[al].n; i++)
			if (!strcmp(g_muse.tracks[g_muse.albums[al].first + i].path, t))
				*row = i;
	return al;
}

/* One album's tracks, over the shelf its cover sits on.
 *
 * THIS USED TO BE THE WHOLE OF MUSE: artists, then their albums, then tracks,
 * with Now Playing as a row at the top, because the covers it browsed by had
 * no shelf of their own yet. They have one now - see muse_shelf_screen - and
 * SELECT lands there, so the two levels above this one had nothing left to
 * do. Eric's, 2026-09-20: the rest of the launcher is pictures, and a list
 * was the wrong front door for the one screen that is all covers.
 *
 * A plays a track and opens Now Playing; on the track already playing it just
 * opens Now Playing, rather than starting it over. B goes back to the shelf.
 * SELECT closes Muse from here, and MENU is Muse's menu, as everywhere in Muse.
 * L1 and R1 are the previous and next track, left and right seek ten seconds.
 * X and Y are free. Leaving does not stop the music - that is what the daemon
 * is for.
 *
 * `now` opens straight onto Now Playing, with these tracks underneath, so B
 * from there lands on the album it is playing. Returns how it was left, so
 * the shelf can close too when SELECT closed Muse - it used to be told
 * nothing, and SELECT from Now Playing landed on the shelf instead. */
static muse_exit muse_tracks(app *a, int album, bool now)
{
	int sel = 0, done = 0, art, i, lv;
	float k;
	muse_exit how = MUSE_BACK;
	menu_row *rows;
	char (*vals)[24];
	char heading[300], note[300], t0[16], t1[16];
	const ml_album *al;

	if (album < 0 || album >= g_muse.nalbums || g_muse.nartists == 0) return MUSE_BACK;
	al = &g_muse.albums[album];
	art = muse_artist_of(album);
	rows = calloc((size_t)al->n + 4, sizeof *rows);
	vals = calloc((size_t)al->n + 4, sizeof *vals);
	if (!rows || !vals) { free(rows); free(vals); return MUSE_BACK; }

	/* On the track that is playing, when it is one of these. */
	if (muse_playing_album(&i) == album) sel = i;

	if (now) {
		how = muse_now_screen(a);
		if (how != MUSE_BACK) done = 1;
		else if (muse_playing_album(&i) == album) sel = i;
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* Opened from here, after a Now Playing it may have opened onto */
	lv = done ? 0 : panel_open();

	while (!done && !want_quit && a->running) {
		const mu_now *mn;
		bool loaded, open_now = false;
		int n = 0, items;
		const char *playing;

		muse_screen_poll();
		cover_answers();
		mn = musec_now();
		loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;
		playing = musec_path();
		plat_input_poll(&a->in);

		/* The album over its artist - and the album alone when there is no
		 * artist over it, a folder of tracks or of episodes straight in
		 * Music or Videos, which would say its name twice. */
		if (!g_muse.artists[art].name[0] || !strcmp(g_muse.artists[art].name, al->name))
			snprintf(heading, sizeof heading, "%s", al->name);
		else
			snprintf(heading, sizeof heading, "%s\n%s", al->name,
			         g_muse.artists[art].name);
		for (i = 0; i < al->n; i++, n++) {
			const ml_track *t = &g_muse.tracks[al->first + i];
			/* Every row a value, if only an empty one: a list with a value
			 * anywhere is laid out in two columns, left-aligned, and one
			 * without is centered. Without this the list sat centered until
			 * one of its tracks played and then jumped left, every name
			 * moving the moment A was pressed. Eric, 2026-09-19. */
			const char *v = "";

			if (playing[0] && !strcmp(playing, t->path))
				v = mn->state == MU_PAUSED ? T(S_MUSE_ROW_PAUSED) : T(S_MUSE_ROW_PLAYING);
			rows[n] = (menu_row){ t->name, v, true };
		}
		items = n;
		if (sel >= items) sel = items ? items - 1 : 0;

		/* What is playing, under a rule, even when it is another album's -
		 * the list is a way to reach music, and the music is the point. */
		if (loaded) {
			char facts[sizeof note];

			mmss(t0, sizeof t0, mn->at);
			mmss(t1, sizeof t1, mn->len);
			snprintf(facts, sizeof facts, "%s · %s · %s / %s", mn->title,
			         mn->artist[0] ? mn->artist : mn->album, t0, t1);
			snprintf(note, sizeof note, mn->state == MU_PAUSED ? T(S_MUSE_PAUSED_NOTE) : "%s",
			         facts);
			rows[n++] = MENU_RULE;
			rows[n++] = MENU_NOTE(note);
		}

		if (items > 0) {
			if (in_repeat(&a->in, IN_UP)) {
				step_sound(a, IN_UP, sel, sel > 0 ? sel - 1 : sel);
				if (sel > 0) sel--;
			}
			if (in_repeat(&a->in, IN_DOWN)) {
				step_sound(a, IN_DOWN, sel, sel < items - 1 ? sel + 1 : sel);
				if (sel < items - 1) sel++;
			}
		}
		if (in_repeat(&a->in, IN_LEFT))     { moved_sound(a, IN_LEFT); musec_seek_by(-seek_step(&a->in, IN_LEFT)); }
		if (in_repeat(&a->in, IN_RIGHT))    { moved_sound(a, IN_RIGHT); musec_seek_by(+seek_step(&a->in, IN_RIGHT)); }
		if (in_repeat(&a->in, IN_L1))       { moved_sound(a, IN_L1); musec_prev(); }
		if (in_repeat(&a->in, IN_R1))       { moved_sound(a, IN_R1); musec_next(); }
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) { how = MUSE_CLOSE; break; }
			continue;
		}

		if (a->in.pressed[IN_ACCEPT])
			uisound_play(items > 0 ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
		if (a->in.pressed[IN_ACCEPT] && items > 0) {
			/* The track already playing is not started over: A on it is the
			 * way to its Now Playing, the same as the row. */
			if (!loaded || strcmp(playing, g_muse.tracks[al->first + sel].path)) {
				const char **paths = calloc((size_t)al->n, sizeof *paths);

				if (paths) {
					for (i = 0; i < al->n; i++)
						paths[i] = g_muse.tracks[al->first + i].path;
					/* A book's file from its start: choosing one is choosing
					 * where to listen from, and the place moves with it. An
					 * episode from where it was left. */
					if (al->kind == ML_VIDEOS)
						muse_play_videos(paths, al->n, sel, g_muse.artists[art].name,
						                 al->name);
					else
						muse_play(paths, al->n, sel, 0, al->book,
						          al->book ? book_speed(album) : 1.0,
						          g_muse.artists[art].name, al->name);
					free(paths);
				}
			}
			open_now = true;
		}
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_SELECT]) uisound_play(DS_SOUND_BACK);
		if (a->in.pressed[IN_BACK]) done = 1;
		if (a->in.pressed[IN_SELECT]) { how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); how = MUSE_CLOSE; break; }
		/* After a rescan `al` is gone with the old library: not one more
		 * frame from it. */
		if (a->in.pressed[IN_MENU]) {
			how = muse_menu(a);
			if (how != MUSE_BACK) break;
			continue;
		}

		/* Back from Now Playing onto the track it had reached, which may be
		 * several past the one chosen. */
		if (open_now) {
			how = muse_now_screen(a);
			if (how != MUSE_BACK) break;
			if (muse_playing_album(&i) == album) sel = i;
			continue;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		muse_backdrop(a, k);
		menu_draw(a, heading, rows, n, sel, menu_std_width(a), ZYPEROS_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}

	if (lv) panel_close(a, lv, !a->running);
	free(rows);
	free(vals);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* SELECT, from anywhere but a running game - the shelf, every menu, and the
 * in-game menu, where the game waits paused underneath: Muse, and straight to
 * Now Playing when something is playing or paused. Nothing on a card with no
 * music, which is also a card with no Muse on its shelf.
 *
 * `over` is the menu it opened over, or NULL for the shelf and the screens
 * that draw it. True when that menu's power rule ended it too. */
bool muse_open(app *a, const menu_style *over, void *ctx)
{
	snap_off();
	const mu_now *mn = musec_now();

	/* SELECT with nothing on the card to play: a press nothing can act on. */
	if (g_muse.ntracks == 0) { uisound_play(DS_SOUND_DENY); return false; }
	uisound_play(DS_SOUND_ACCEPT);
	g_muse_over = over;
	g_muse_over_ctx = ctx;
	g_muse_gone = false;
	muse_shelf_screen(a, mn->state == MU_PLAYING || mn->state == MU_PAUSED);
	g_muse_over = NULL;
	g_muse_over_ctx = NULL;
	/* Nothing Muse did closes the menu it may have been opened over. */
	g_menu_closing = false;
	return g_muse_gone || g_message_stopped;
}

/* A on a book: where it was left, straight onto Now Playing with its files
 * underneath. The beginning for a book never started or finished; nothing new
 * for the book already playing or paused, which A only opens. */
static muse_exit muse_book(app *a, int al)
{
	const mu_now *mn = musec_now();
	bool loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;

	if (!loaded || muse_playing_album(NULL) != al) {
		const ml_album *b = &g_muse.albums[al];
		const char **paths = calloc((size_t)b->n, sizeof *paths);
		double at;
		int i, start = book_place(al, &at);

		if (!paths) return MUSE_BACK;
		for (i = 0; i < b->n; i++) paths[i] = g_muse.tracks[b->first + i].path;
		fprintf(stderr, "muse: %s from file %d at %.1f\n", b->name, start + 1, at);
		muse_play(paths, b->n, start, at, true, book_speed(al),
		          g_muse.artists[muse_artist_of(al)].name, b->name);
		free(paths);
	}
	return muse_tracks(a, al, true);
}

/* A on a film: where it was left, straight onto the player, and B from there
 * back to the shelf - a list of one file would be a screen with nothing to
 * choose. Nothing new for the film already playing or paused, which A only
 * opens. */
static muse_exit muse_film(app *a, int al)
{
	const mu_now *mn = musec_now();
	bool loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;

	if (!loaded || muse_playing_album(NULL) != al) {
		const char *p = g_muse.tracks[g_muse.albums[al].first].path;

		muse_play_videos(&p, 1, 0, "", g_muse.albums[al].name);
	}
	return muse_now_screen(a);
}

/* A on card `k` of Muse's shelf: its album's tracks, over the shelf, or a
 * book where it was left - and a film, which is one file, straight onto the
 * player; a folder of videos lists its episodes like an album. */
static muse_exit muse_album(app *a, const sysview *v, int k)
{
	if (!v->album || k < 0 || k >= v->list.count) return MUSE_BACK;
	if (g_muse.albums[v->album[k]].book) return muse_book(a, v->album[k]);
	if (g_muse.albums[v->album[k]].solo) return muse_film(a, v->album[k]);
	return muse_tracks(a, v->album[k], false);
}

/* Muse's shelf again after its menu, whose Rescan Folder rebuilds every view
 * on the card, so the view being held is found again rather than trusted.
 * NULL when there is no Muse left to show. */
static sysview *muse_shelf_refind(app *a)
{
	sysview *v;
	int i, muse = -1;

	for (i = 0; i < a->sys.count; i++)
		if (is_muse(&a->sys.systems[i])) { muse = i; break; }
	if (muse < 0 || a->view[muse].list.count <= 0) return NULL;
	a->sys_cursor = muse;
	a->screen = SCREEN_GAMES;
	v = &a->view[muse];
	if (v->cursor >= v->list.count) v->cursor = v->list.count - 1;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	return v;
}

/* Which card on Muse's shelf is that album, or -1. */
static int muse_card_of(const sysview *v, int album)
{
	int k;

	for (k = 0; v->album && k < v->list.count; k++)
		if (v->album[k] == album) return k;
	return -1;
}

/* MUSE'S SHELF, OPENED ON TOP OF WHATEVER WAS ON SCREEN. Eric's, 2026-09-20.
 *
 * SELECT used to open a list of artists, because this shelf belongs to the
 * main loop - it is the games shelf, drawn for Muse's own view - and SELECT
 * can be pressed inside a menu, on Play Time, or in the in-game menu over a
 * paused game, none of which the main loop is driving. A list could draw over
 * any of those and the shelf could not, so the front door to a screen made
 * entirely of covers was text. The rest of the launcher is pictures.
 *
 * So the shelf is driven from here as well: the same view, the same cards, the
 * same movement, and draw_shelf doing the drawing - the one loop in this file
 * that borrows another screen's. What it does NOT borrow is update_games'
 * input, because B has to come back here rather than walk out to the systems
 * row, and MENU has to survive a Rescan rebuilding every view underneath it.
 *
 * The screen and the cursor are put back on the way out, so whatever opened
 * Muse is still there when it closes. */
static void muse_shelf_screen(app *a, bool now)
{
	int prev_screen = a->screen, prev_sys = a->sys_cursor;
	in_button back = IN_LEFT, fwd = IN_RIGHT;
	in_button jup = IN_UP, jdn = IN_DOWN;
	sysview *v;
	int muse = -1, i, playing, base;
	bool done = false;

	for (i = 0; i < a->sys.count; i++)
		if (is_muse(&a->sys.systems[i])) { muse = i; break; }
	if (muse < 0 || a->view[muse].list.count <= 0) return;

	a->sys_cursor = muse;
	enter_system(a);                      /* the screen, the coverflow, the window */
	v = &a->view[muse];
	base = panel_base();

	/* On the album that is playing, so SELECT lands where the music is - on
	 * the books when it is a book. */
	playing = muse_playing_album(NULL);
	if (playing >= 0 && g_muse.albums[playing].kind != muse_kind_shown())
		muse_show(v, g_muse.albums[playing].kind);
	if (playing >= 0) {
		int k = muse_card_of(v, playing);

		if (k >= 0) { v->cursor = k; cf_reset(&v->cf, k); }
	}
	if (now && playing >= 0) {
		/* A film is its player, with no list of one under it. */
		muse_exit how = g_muse.albums[playing].solo ? muse_now_screen(a)
		                                            : muse_tracks(a, playing, true);

		if (how == MUSE_CLOSE || g_muse_gone) done = true;
		else if (how == MUSE_REBUILT) {
			sysview *nv = muse_shelf_refind(a);

			if (nv) v = nv; else done = true;
		}
	}

	while (!done && !want_quit && a->running) {
		int n = v->list.count, dir = 0;

		muse_screen_poll();
		cover_answers();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); break; }
		/* B and SELECT both leave, which is the rule everywhere in Muse: one
		 * button in, the same button out, and B for the level below. */
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_SELECT]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}

		if (n > 0) {
			in_button sb = IN_NONE;          /* the step's button, for its sound */
			int was = v->cursor;

			if (in_repeat(&a->in, back)) { v->cursor = (v->cursor - 1 + n) % n; dir = -1; sb = back; }
			if (in_repeat(&a->in, fwd))  { v->cursor = (v->cursor + 1) % n; dir = +1; sb = fwd; }
			if (in_repeat(&a->in, jdn))  { v->cursor = shelf_letter_jump(v, +1); sb = jdn; }
			if (in_repeat(&a->in, jup))  { v->cursor = shelf_letter_jump(v, -1); sb = jup; }
			if (in_repeat(&a->in, IN_L1)) { v->cursor = ((v->cursor - CF_WINDOW) % n + n) % n; sb = IN_L1; }
			if (in_repeat(&a->in, IN_R1)) { v->cursor = (v->cursor + CF_WINDOW) % n; sb = IN_R1; }
			cf_set_cursor_dir(&v->cf, v->cursor, n, dir);
			if (sb != IN_NONE)
				step_sound(a, sb, was, v->cursor);
		}
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) break;
			continue;
		}

		if (a->in.pressed[IN_ACCEPT]) {
			/* SELECT from the tracks or Now Playing closes all of Muse, and
			 * a rescan from their MENU leaves this shelf to be found again. */
			muse_exit how;

			uisound_play(DS_SOUND_ACCEPT);
			how = muse_album(a, v, v->cursor);

			if (how == MUSE_CLOSE || g_muse_gone) break;
			if (how == MUSE_REBUILT) {
				sysview *nv = muse_shelf_refind(a);

				if (!nv) break;
				v = nv;
			}
		} else if (a->in.pressed[IN_MENU]) {
			/* Muse's own menu. Sort By reorders this shelf and Rescan
			 * Folder rebuilds it, so it is found again either way. */
			sysview *nv;

			if (muse_menu(a) == MUSE_CLOSE) break;
			if (!(nv = muse_shelf_refind(a))) break;
			v = nv;
		}

		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}

	panel_base_close(base);
	evict_far(v, TEX_KEEP_FAR);
	a->screen = prev_screen;
	a->sys_cursor = prev_sys;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- Muse: Album Art -------------------------------------------------------- */

/* The artist an album is under, and its folder relative to the Music folder. */
int muse_artist_of(int al)
{
	int ar;

	for (ar = 0; ar < g_muse.nartists - 1; ar++)
		if (al < g_muse.artists[ar].first + g_muse.artists[ar].n) break;
	return ar;
}

void muse_album_dir(int al, char *out, size_t n)
{
	const char *p = g_muse.albums[al].n > 0 ? g_muse.tracks[g_muse.albums[al].first].path : "";
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "%.*s", slash ? (int)(slash - p) : 0, p);
}

/* Which albums Album Art asks about: every one with no cover on the card, or
 * with one smaller on its short side than the shelf draws it - the album
 * frame's height, 461px - which the service has not already supplied. Eric's
 * rule, 2026-09-19. A cover the service gave is remembered, because its own
 * can be small too - La Strada's is 360px - and asking again would only fetch
 * the same one; an album it could not find is asked about on every run. */
static int museart_jobs(museart_job *jobs, int max)
{
	int crisp = (int)(ZYPEROS_SCREEN_H * CF_LAYOUT_ALBUMS.size + 0.5f);
	int i, n = 0;

	for (i = 0; i < g_muse.nalbums && n < max; i++) {
		char dir[LIB_PATH], key[LIB_PATH + 16], p[LIB_PATH * 2 + 8];
		museart_job *j = &jobs[n];
		int w = 0, h = 0;
		bool have;

		/* Not a book: MusicBrainz knows records, and a book's cover is its
		 * own file or the picture in its folder. Nor Singles, which is no
		 * record at all and has its own cover. */
		if (g_muse.albums[i].n <= 0 || g_muse.albums[i].kind != ML_MUSIC ||
		    g_muse.albums[i].singles) continue;
		muse_album_dir(i, dir, sizeof dir);
		snprintf(key, sizeof key, "museart.%s", dir);
		ml_cover_base(g_muse_root, &g_muse, i, j->base, sizeof j->base);
		snprintf(p, sizeof p, "%s.jpg", j->base);
		have = file_nonempty(p);
		if (!have) { snprintf(p, sizeof p, "%s.png", j->base); have = file_nonempty(p); }
		/* Remembered only while the cover it gave is on the card: one deleted
		 * since is asked about again, where the memory alone kept every such
		 * album off the list for good (2026-10-05). */
		if (have && db_has(db_lib(), key)) continue;
		if (have && museart_image_size(p, &w, &h) && (w < h ? w : h) >= crisp) continue;
		snprintf(j->artist, sizeof j->artist, "%s", g_muse.artists[muse_artist_of(i)].name);
		snprintf(j->album, sizeof j->album, "%s", g_muse.albums[i].name);
		j->tracks = g_muse.albums[i].n;
		j->id = i;
		n++;
	}
	return n;
}

/* A cover arrived for album `al`: remember where it came from, and drop the old
 * one from the shelf and from Now Playing so both draw the new one. The whole
 * worker queue is disowned, not just this card, because a decode of the OLD
 * file may already be in flight and would otherwise land on top. */
static void album_art_landed(app *a, int al, const char *rg)
{
	char dir[LIB_PATH], key[LIB_PATH + 16];
	int s;

	if (al < 0 || al >= g_muse.nalbums) return;
	muse_album_dir(al, dir, sizeof dir);
	snprintf(key, sizeof key, "museart.%s", dir);
	db_set_str(db_lib(), key, rg[0] ? rg : "fetched");
	if (g_cov) g_cov[al].st = COV_JPG;
	for (s = 0; s < a->sys.count; s++) {
		sysview *v = &a->view[s];
		int k;

		if (!is_muse(&a->sys.systems[s]) || !v->tex || !v->album) continue;
		for (k = 0; k < v->list.count; k++)
			if (v->album[k] == al && v->tex[k]) {
				SDL_DestroyTexture(v->tex[k]);
				v->tex[k] = NULL;
			}
	}
	texload_bump();
	if (g_np.album == al) np_forget();
}

/* ---- Muse: album covers from the web page (USB File Transfer) ------------
 *
 * Album Art's rules, answered for the page at the other end of the cable,
 * which asks MusicBrainz and the Cover Art Archive itself: the Pixel has no
 * network. Which albums want a cover is museart_jobs, which release a reply
 * names is museart_pick, and a cover that lands is remembered as Album Art
 * remembers its own. See hare_set_albums. */

/* Where an album's cover goes, as a path the page uploads to: the card's
 * Music/<artist>/.media/<album>.jpg is music/<artist>/.media/<album>.jpg. */
static bool album_cover_path(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	size_t root = strlen(g_muse_root);

	ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
	if (strncmp(base, g_muse_root, root) || strncmp(base + root, "/Music/", 7))
		return false;
	return snprintf(out, n, "music/%s.jpg", base + root + 7) < (int)n;
}

/* The album a pick answered and its release group, until its cover lands.
 * One at a time: the page uploads each cover straight after its pick. */
static int  g_pick_al = -1;
static char g_pick_rg[64];

static char *albums_wanted(size_t *len, char *why, size_t wn)
{
	museart_job *jobs = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1),
	                           sizeof *jobs);
	hj o = { 0 };
	bool first = true;
	int n, i, k;

	if (!jobs) { snprintf(why, wn, "out of memory"); return NULL; }
	n = museart_jobs(jobs, g_muse.nalbums);
	hj_lit(&o, "{\"albums\":[");
	for (i = 0; i < n; i++) {
		char artists[MUSEART_TRIES][128], albums[MUSEART_TRIES][128];
		char url[1024], cover[LIB_PATH * 2];
		int tries = museart_tries(jobs[i].artist, jobs[i].album, artists, albums);

		if (!album_cover_path(jobs[i].id, cover, sizeof cover)) continue;
		if (!first) hj_lit(&o, ",");
		first = false;
		hj_lit(&o, "{\"id\":");
		hj_int(&o, jobs[i].id);
		hj_lit(&o, ",\"artist\":");
		hj_str(&o, jobs[i].artist);
		hj_lit(&o, ",\"album\":");
		hj_str(&o, jobs[i].album);
		/* The searches in Album Art's order, each asked only if the one
		 * before named nothing. */
		hj_lit(&o, ",\"searches\":[");
		for (k = 0; k < tries; k++) {
			if (!museart_search_url(artists[k], albums[k], url, sizeof url)) continue;
			if (k) hj_lit(&o, ",");
			hj_str(&o, url);
		}
		hj_lit(&o, "]}");
	}
	hj_lit(&o, "]}");
	free(jobs);
	return hj_done(&o, len, why, wn);
}

static char *album_pick(int id, const char *json, size_t n, size_t *len,
                        char *why, size_t wn)
{
	char rg[64] = "", cover[LIB_PATH * 2] = "", base[LIB_PATH * 2], *slash;
	hj o = { 0 };

	if (id < 0 || id >= g_muse.nalbums || g_muse.albums[id].kind != ML_MUSIC) {
		snprintf(why, wn, "no such album");
		return NULL;
	}
	if (museart_pick(json, n, g_muse.albums[id].n, rg, sizeof rg) &&
	    album_cover_path(id, cover, sizeof cover)) {
		/* The .media folder, which an album whose files never carried a
		 * cover does not have: the upload needs it to exist. */
		ml_cover_base(g_muse_root, &g_muse, id, base, sizeof base);
		if ((slash = strrchr(base, '/'))) { *slash = '\0'; mkdir(base, 0755); }
		g_pick_al = id;
		snprintf(g_pick_rg, sizeof g_pick_rg, "%s", rg);
	} else {
		rg[0] = cover[0] = '\0';
	}
	hj_lit(&o, "{\"rg\":");
	hj_str(&o, rg);
	hj_lit(&o, ",\"cover\":");
	hj_str(&o, cover);
	hj_lit(&o, "}");
	return hj_done(&o, len, why, wn);
}

/* Every upload, from the transfer screen: a game's cover is shrunk, and the
 * cover of the album last picked is taken as Album Art takes one, the PNG the
 * files carried removed and the release group remembered. */
static void xfer_after_write(const char *abs)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];

	hareart_after_write(abs);
	if (g_pick_al < 0 || g_pick_al >= g_muse.nalbums) return;
	ml_cover_base(g_muse_root, &g_muse, g_pick_al, base, sizeof base);
	snprintf(p, sizeof p, "%s.jpg", base);
	if (strcmp(p, abs)) return;
	snprintf(p, sizeof p, "%s.png", base);
	remove(p);
	if (g_logs_app) album_art_landed(g_logs_app, g_pick_al, g_pick_rg);
	g_pick_al = -1;
}

/* Album Art, from Muse's menu: MusicBrainz and the Cover Art Archive, for the
 * albums museart_jobs chooses. The covers change on the shelf behind the panel
 * as they arrive. B stops. */
static void album_art_screen(app *a)
{
	museart_job *jobs;
	museart_status st;
	menu_row rows[3];
	char head[300], fit[200], where[64], counts[160];
	bool working, done = false;
	int n, lv;
	float k;

	if (!net_online()) {
		menu_row row = { MSG_NEEDS_WIFI, NULL, false };

		timed_message(a, T(S_MUSE_ART_TITLE), &row, 1, 0, ZYPEROS_ACCENT);
		return;
	}
	jobs = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1), sizeof *jobs);
	if (!jobs) return;
	n = museart_jobs(jobs, g_muse.nalbums);
	/* The one async slot, which an account answer from a game just quit may
	 * still be holding - the same reason Box Art gives it up first. */
	sync_abandon();
	working = museart_begin(jobs, n, "/tmp/zyperos-musicbrainz.json");
	free(jobs);
	fprintf(stderr, "album art: %d album%s to ask about\n", n, n == 1 ? "" : "s");

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();
	while (!done && !want_quit && a->running) {
		if (working && museart_step(plat_now_ms()) == 0) {
			working = false;
			museart_status_get(&st);
			fprintf(stderr, "album art: %d found, %d not found, %d failed%s%s\n",
			        st.found, st.missing, st.failed, st.problem[0] ? " - " : "",
			        st.problem);
		}
		museart_status_get(&st);
		if (st.landed >= 0) album_art_landed(a, st.landed, st.rg);

		ui_fit_text(ui_font(UI_F_LABEL),
		            working ? (st.now[0] ? st.now : T(S_MUSE_ART_STARTING))
		            : st.problem[0] ? st.problem
		            : n == 0 ? T(S_MUSE_ART_ALL_CRISP) : T(S_MUSE_ART_DONE),
		            fit, sizeof fit, ZYPEROS_SCREEN_W * 3 / 4);
		snprintf(head, sizeof head, "%s\n%s", T(S_MUSE_ART_TITLE), fit);
		snprintf(where, sizeof where, "%d / %d", st.done, st.n);
		if (st.failed)
			snprintf(counts, sizeof counts, T(S_MUSE_ART_COUNTS_FAILED),
			         st.found, st.missing, st.failed);
		else
			snprintf(counts, sizeof counts, T(S_MUSE_ART_COUNTS), st.found, st.missing);
		rows[0] = (menu_row){ T(S_MUSE_ART_ALBUMS), where,  false };
		rows[1] = (menu_row){ T(S_MUSE_ART_COVERS), counts, false };
		rows[2] = (menu_row){ working ? T(S_MUSE_ART_STOP) : T(S_MUSE_ART_CLOSE), NULL, false };

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { museart_cancel(); a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			museart_cancel();
			power_off(a);
			return;
		}
		if (menu_leaving(a)) done = true;
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Idle does not count while covers are arriving: it is the device
		 * doing what it was asked, and powering off halfway loses the rest. */
		if (working) a->idle.since_ms = plat_now_ms();

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw(a, head, rows, 3, -1, menu_std_width(a), ZYPEROS_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
	museart_cancel();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void synopsis_screen(app *a, const char *title, const char *text,
                            unsigned accent)
{
	char (*lines)[CHV_WRAP_COLS];
	menu_row *rows;
	int fixed = menu_std_width(a);
	int n, done = 0, loop_at = 0, lv;
	float k;

	if (!text || !*text) return;
	lines = calloc(SYN_WRAP_LINES, sizeof *lines);
	rows  = calloc(SYN_ROWS, sizeof *rows);
	if (!lines || !rows) { free(lines); free(rows); return; }

	n = syn_layout(text, fixed, lines, rows, &loop_at);

	/* Opened, so its scroll starts at the top. Without this, closing a long
	 * synopsis and opening it again resumed halfway down: the clock's keys -
	 * the row count and the panel height - are identical on the way back in,
	 * so nothing told it a reader had arrived. */
	mq_reset(MQ_VSCROLL);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit && a->running) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) break;
		if (a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_X]) uisound_play(DS_SOUND_BACK);
		if (menu_leaving(a) || a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_X]) done = 1;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* Nothing else watches power for this screen, the same as every other
		 * loop the launcher runs outside plat_resident_wait. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw_ex(a, title, rows, n, -1, fixed, accent, NULL, false, loop_at);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	free(lines);
	free(rows);
}

/* The list's badge for row `row`: chv_at(row)'s, greyed until earned, or
 * NULL when the card has none (src/rabadge.h). menu_panel's g_row_icon while
 * the list is drawn. */
static SDL_Renderer *g_badge_r;

static SDL_Texture *cheevo_row_badge(int row)
{
	const cheevo *c = chv_at(row);

	return c ? rb_texture(g_badge_r, chv_source(), c->badge,
	                      !(c->earned || c->earned_now)) : NULL;
}

/* Whether any achievement in the set has a badge on the card: a set with
 * none keeps its rows as they were, with no empty squares */
static bool cheevo_badges_here(void)
{
	int i;

	for (i = 0; i < chv_count(); i++)
		if (rb_have(chv_source(), chv_at(i)->badge)) return true;
	return false;
}

static void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf)
{
	int n = chv_count(), sel = 0, i, done = 0, lv;
	float k;
	menu_row *rows;
	unsigned *vcols;
	char (*vals)[32];
	char heading[192];
	bool badges = cheevo_badges_here();

	if (n <= 0) return;
	rows  = calloc((size_t)n, sizeof *rows);
	vals  = calloc((size_t)n, sizeof *vals);
	vcols = calloc((size_t)n, sizeof *vcols);
	if (!rows || !vals || !vcols) { free(rows); free(vals); free(vcols); return; }

	for (i = 0; i < n; i++) {
		const cheevo *c = chv_at(i);

		/* A count moving, "74 / 100", where the points are, until it is
		 * earned (on_cheevo_progress) */
		if (!c->earned && !c->earned_now && c->progress[0] && strchr(c->progress, '/'))
			snprintf(vals[i], sizeof vals[i], "%.*s / %s",
			         (int)(strchr(c->progress, '/') - c->progress), c->progress,
			         strchr(c->progress, '/') + 1);
		else if (!c->earned && !c->earned_now && c->progress[0])
			snprintf(vals[i], sizeof vals[i], "%s", c->progress);
		else
			snprintf(vals[i], sizeof vals[i], "%d", c->points);
		rows[i].label = c->title;
		rows[i].value = vals[i];
		rows[i].live  = c->earned || c->earned_now;
		/* Said by color on EVERY earned row, not only the one under the
		 * cursor. live already lifts the label one step, which alone was too
		 * little to scan a hundred-entry set by. */
		vcols[i] = rows[i].live ? VCOL_EARNED : 0u;
	}
	/* Two lines: the game on one, the counts on the other.
	 *
	 * On one line this was "Hagane: The Final Conflict   0/36   0/415 points",
	 * 1031 pixels against a 778 pixel panel - and menu_draw centers a heading,
	 * so it lost BOTH ends: the H and the word "points". Dropping the title
	 * fixed the clipping and threw away something worth keeping. Eric's
	 * suggestion, and it is better than either.
	 *
	 * The counts as DESIGN.md 12 writes them, "3 / 36 · 25 / 415 points".
	 * With "cheevos" after the first it was 818 wide in Lexend, more than
	 * the panel's content, and the line scrolled; at 585 it fits with room
	 * for a set of hundreds, and the list under it says what it counts. */
	snprintf(heading, sizeof heading, T(S_CHEEVOLIST_HEADING),
	         chv_game_title(), chv_earned(), n,
	         chv_points_earned(), chv_points_total());

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		if (in_repeat(&a->in, IN_UP)) {
			step_sound(a, IN_UP, sel, sel > 0 ? sel - 1 : sel);
			if (sel > 0) sel--;
		}
		if (in_repeat(&a->in, IN_DOWN)) {
			step_sound(a, IN_DOWN, sel, sel < n - 1 ? sel + 1 : sel);
			if (sel < n - 1) sel++;
		}
		/* A set runs to well over a hundred entries, so the shoulder buttons
		 * page it the same way they page a shelf. */
		if (in_repeat(&a->in, IN_L1)) {
			step_sound(a, IN_L1, sel, sel > 8 ? sel - 8 : 0);
			sel = sel > 8 ? sel - 8 : 0;
		}
		if (in_repeat(&a->in, IN_R1)) {
			step_sound(a, IN_R1, sel, sel < n - 9 ? sel + 8 : n - 1);
			sel = sel < n - 9 ? sel + 8 : n - 1;
		}
		if (menu_leaving(a)) done = 1;
		if (over_shelf && a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* A used to close this screen. It opens the achievement instead,
		 * which is the only thing on it there was ever anything more to say
		 * about; BACK and MENU still close, so nothing lost a way out. */
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
		if (a->in.pressed[IN_ACCEPT] && cheevo_detail_screen(a, bg, over_shelf, chv_at(sel)))
			done = 1;
		/* Power still stops the game from in here. Not trapping it would
		 * make this screen the one place in the launcher that ignores it.
		 *
		 * The note is what was missing: stopping the game is not the same as
		 * powering off, and only the evdev watchdog inside plat_resident_wait
		 * sets that flag - a loop which is not running while this screen is
		 * up. So power here stopped the game and then went back to the shelf,
		 * with the menu still drawn over it until something was pressed.
		 *
		 * Opened from the game details screen there is no game to stop, and
		 * power means what it means everywhere else on the shelf. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		chv_backdrop(a, bg, over_shelf, k);
		/* visits_all: every achievement can be opened, earned or not, so
		 * `live` here is about color and not about reach. Each row with its
		 * badge when the card has the set's (g_row_icon). */
		if (badges || (rb_landed() && (badges = cheevo_badges_here()))) {
			g_badge_r = a->r;
			g_row_icon = cheevo_row_badge;
		}
		menu_draw_ex(a, heading, rows, n, sel, menu_std_width(a), a->tint,
		             vcols, true, 0);
		g_row_icon = NULL;
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	rb_textures_free();
	free(rows);
	free(vals);
	free(vcols);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* What the in-game Controls page needs to know about the game on the
 * shelf's cursor, which is the one running: its system (through Favorites
 * too, shelf_owner), that system's map and turbo, and the file's extension
 * (an .fds game turns its disk on L1 and R1). */
/* A Controls heading fits centered between two page counts' room, as
 * panel_show keeps it (DESIGN.md 11.3) */
static bool ctl_head_fits(void *ctx, const char *head)
{
	app *a = ctx;
	int inset = ui_text_width(ui_font(UI_F_META), "4 / 4") + 2 * DS_GRID;

	return ui_text_width(ui_font(UI_F_LABEL), head) <= menu_std_width(a) - 2 * inset;
}

static ctl_game game_controls_of(app *a)
{
	sysview *sv = &a->view[a->sys_cursor];
	int o = shelf_owner(a, a->sys_cursor, sv->cursor);
	const system_cfg *s = &a->sys.systems[o];
	const char *file = sv->list.count > 0 ? sv->list.items[sv->cursor].file : "";
	const char *dot = strrchr(file, '.');

	return (ctl_game){ s->tag, s->name, home_label(s->tag, s->name), s->map,
	                   plat_turbo_map(s->tag), dot ? dot + 1 : "", ctl_head_fits, a };
}

/* The in-game menu's Controls (DESIGN.md 11.28): every control this game
 * has, a page at a time over the paused game, Left/Right to turn them, as
 * MENU > Controls is on the home. Read-only. The rows are made when a page
 * turns, never per frame and never while the game runs. */
static void game_controls_screen(app *a, SDL_Texture *bg)
{
	ctl_game g = game_controls_of(a);
	menu_row rows[CTL_MAX_ROWS];
	ctl_buf b;
	char count[16] = "";
	int page = 0, pages, n, lv, done = 0;
	float k;

	ctl_set_fn_volume(plat_brightness_on_volume());
	pages = ctl_game_pages(&g);
	n = ctl_game_rows(&g, page, rows, &b);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		int to = page;

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (menu_leaving(a)) done = 1;
		if (in_repeat(&a->in, IN_RIGHT)) {
			to = (page + 1) % pages;
			step_sound(a, IN_RIGHT, page, to);
		}
		if (in_repeat(&a->in, IN_LEFT)) {
			to = (page + pages - 1) % pages;
			step_sound(a, IN_LEFT, page, to);
		}
		if (to != page) {
			page = to;
			n = ctl_game_rows(&g, page, rows, &b);
		}
		/* which page of how many, at the heading's right (DESIGN.md 11.3) */
		if (pages > 1) snprintf(count, sizeof count, "%d / %d", page + 1, pages);
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		/* Power stops the game from here, as from the achievements */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		paused_backdrop(a, bg, k);
		menu_draw_caption(a, b.head, rows, n, -1, menu_std_width(a), a->tint, NULL, false, 0,
		                  count[0] ? count : NULL);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* Measured across every mode label, so cycling the row does not resize the
 * panel under the cursor - the same reason the shelf menus have a fixed width. */
static int gm_width(app *a)
{
	menu_row rows[GM_MAX_ROWS];
	gm_bufs b;
	int w = 0, k, n;

	n = gm_build(a, rows, &b);
	for (k = 0; k < DMODE_COUNT; k++) {
		int mw;
		rows[GM_DISPLAY].value = dmode_label(k);
		mw = menu_measure(rows, n, NULL);
		if (mw > w) w = mw;
	}
	rows[GM_DISPLAY].value = dmode_label(0);
	/* A DS game's Screens row, right under Display, at each layout. */
	for (k = 0; g_game_dual && k < GM_LAYOUTS; k++) {
		int mw;
		rows[GM_DISPLAY + 1].value = gm_layout_label(k);
		mw = menu_measure(rows, n, NULL);
		if (mw > w) w = mw;
	}
	return w;
}

/* The DS rows: the layout steps through the three, the swap and the stylus
 * turn over, each sent to the game at once. The layout is kept for the
 * system; the swap and the stylus belong to this game. Diatom answers with the
 * new rect and a new paused picture for it (plat_resident_sync_screens), so
 * the backdrop behind the menu is the layout just chosen. Returns whether it
 * heard back, which is when the picture is worth loading again. */
static bool gm_screens(app *a, gm_row id, int d)
{
	const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor,
	                                      a->view[a->sys_cursor].cursor)];
	int lay;
	bool sw;

	screens_get(s->tag, &lay, &sw);
	sw = plat_resident_swap();
	if (id == GM_SCREENS)   screens_put(s->tag, (lay + d + GM_LAYOUTS) % GM_LAYOUTS, sw);
	else if (id == GM_SWAP) screens_put(s->tag, lay, !sw);
	else if (id == GM_STYLUS) {
		bool on = !plat_resident_stylus();

		plat_resident_line("SETSTYLUS\ton=%d", on ? 1 : 0);
		plat_resident_note_stylus(on);
	}
	return plat_resident_sync_screens(300);
}

/* The in-game menu's context. `resume` is what the epilogue turns on: whether
 * the player is going back to the game or the game is over. */
typedef struct {
	app         *a;
	SDL_Texture *bg;       /* the paused frame, as Diatom last wrote it */
	gm_bufs      bufs;
	menu_style   st;
	bool         resume;
} gm_ctx;

static int gm_menu_build(void *ctx, menu_row *rows, int max,
                         const char **heading)
{
	gm_ctx *c = ctx;

	(void)max;
	*heading = NULL;       /* no title: the game behind it is the title */
	return gm_build(c->a, rows, &c->bufs);
}

/* The paused game, not the shelf. Diatom reports its rect with every DISPLAY
 * message, so the preview lands where the game actually is. */
static void gm_backdrop(app *a, void *ctx, float k)
{
	gm_ctx *c = ctx;

	/* the corner with it (paused_backdrop) */
	paused_backdrop(a, c->bg, k);
}

/* Power here stops the GAME. The device is not going anywhere, and the wait
 * loop under this menu has to be told or it goes straight back to waiting on
 * a game nobody is running. */
static menu_result gm_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

static menu_result gm_key(app *a, void *ctx, in_button key, int sel)
{
	gm_ctx *c = ctx;

	/* A row's place to its id: a DS game has three rows more after Display,
	 * and the same list the build used says which row is which. */
	{
		gm_ui u = { .screens = g_game_dual };
		gm_row ids[GM_MAX_ROWS];
		int n = gm_row_ids(&u, ids);

		sel = sel >= 0 && sel < n ? (int)ids[sel] : GM_MAX_ROWS;
	}
	if ((sel == GM_SCREENS || sel == GM_SWAP || sel == GM_STYLUS) &&
	    (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		if (gm_screens(a, (gm_row)sel, key == IN_LEFT ? -1 : 1)) {
			const char *pv = plat_resident_last_preview();
			SDL_Surface *sf = pv && *pv ? IMG_Load(pv) : NULL;

			if (sf) {
				if (c->bg) SDL_DestroyTexture(c->bg);
				c->bg = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
		}
		return MENU_STAY;
	}

	/* Applied to the running game at once, not on resume: the whole point of
	 * this row being here rather than on the shelf is judging the mode against
	 * the game it is being applied to. Diatom takes SETDISPLAY while paused,
	 * answers with the new rect, and the backdrop behind this menu redraws
	 * into it. */
	if (sel == GM_DISPLAY && (key == IN_LEFT || key == IN_RIGHT)) {
		gm_cycle_display(a, key == IN_RIGHT ? 1 : -1);
		return MENU_STAY;
	}
	if (key != IN_ACCEPT) return MENU_STAY;

	switch ((gm_row)sel) {
	case GM_CONTINUE:
		c->resume = true;
		return MENU_DONE;
	case GM_SAVE:
	case GM_LOAD: {
		int slot = slot_strip(a, c->bg, sel == GM_SAVE);

		if (slot) {
			sysview *sv = &a->view[a->sys_cursor];
			int o = shelf_owner(a, a->sys_cursor, sv->cursor);   /* see slot_strip */
			char sp[LIB_PATH * 2], pp[LIB_PATH * 2];

			slot_state_path(a, o, &sv->list.items[sv->cursor],
			                slot, sp, sizeof sp);
			if (sel == GM_SAVE) {
				plat_resident_line("SAVE\tpath=%s", sp);
				/* The paused frame is what the save holds, and Diatom already
				 * wrote it as the pause preview: copy it beside the state so
				 * the strip can show what is inside the slot. */
				slot_preview_path(a, o, &sv->list.items[sv->cursor],
				                  slot, pp, sizeof pp);
				copy_file(plat_resident_last_preview(), pp);
			} else {
				plat_resident_line("LOAD\tpath=%s", sp);
			}
			c->resume = true;
			return MENU_DONE;
		}
		/* Backed out: fall through to the menu, still paused. */
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		break;
	}
	/* A cycles it forward as well as left and right. Every other row in this
	 * menu is something A does, so a row that only answered to left and right
	 * was a row that looked broken. */
	case GM_DISPLAY:
		gm_cycle_display(a, +1);
		break;
	case GM_CONTROLS:
		game_controls_screen(a, c->bg);
		break;
	case GM_CHEEVOS:
		cheevos_screen(a, c->bg, false);
		break;
	case GM_RESET:
		plat_resident_line("RESET");
		c->resume = true;
		return MENU_DONE;
	case GM_QUIT:
		/* The wait loop carries on until EXIT arrives; quitting is asking,
		 * not tearing down. */
		plat_resident_line("STOP");
		return MENU_DONE;
	default: break;
	}

	/* A nested screen may have been the one that took the power press or ran
	 * the clock out - the slot strip and the achievements list both stop the
	 * game and close themselves, which would leave this menu drawn over a game
	 * that had already been told to stop. Only those two set the flag, and
	 * both are reached from right here, so this is where it is asked. */
	if (plat_run_power_pressed()) return MENU_DONE;
	return MENU_STAY;
}

/* The next in-game menu was asked for by the launcher, not by a press: the
 * PAUSE a game resumed from a shutdown is sent with (launch). */
static bool g_menu_unasked;

static void game_menu(app *a)
{
	gm_ctx c = { 0 };
	const char *pv = plat_resident_last_preview();

	/* The menu opening over the paused game: MENU was pressed in it - unless
	 * the launcher asked for it, at a resume from a shutdown. */
	if (!g_menu_unasked) uisound_play(DS_SOUND_ACCEPT);
	g_menu_unasked = false;
	c.a = a;
	if (pv && *pv) {
		SDL_Surface *sf = IMG_Load(pv);

		if (sf) {
			c.bg = SDL_CreateTextureFromSurface(a->r, sf);
			SDL_FreeSurface(sf);
		}
	}
	/* Measured across every mode label, so cycling Display does not resize
	 * the panel underneath the cursor. */
	c.st.fixed_w     = gm_width(a);
	c.st.sound       = c.bufs.sound;
	c.st.follow_tint = true;
	c.st.backdrop    = gm_backdrop;
	c.st.on_power    = gm_power;

	/* B and MENU are the runner's, and both mean Continue here. The runner has
	 * to be the one to say so: it flushes the input on its way out, so asking
	 * a->in afterwards would always say no button was pressed. MENU from a
	 * screen this menu opened - the save slots, Achievements - comes back as
	 * MENU_LEFT_BACK too, so it is Continue from there as well. */
	g_menu_closing = false;
	if (menu_run(a, &c.st, gm_menu_build, gm_key, &c) == MENU_LEFT_BACK)
		c.resume = true;
	g_menu_closing = false;

	/* Its close, over the paused game, while the display is still this
	 * process's: before the pages are handed back below, and never after
	 * RESUME, when Diatom is drawing. Ending the game instead leaves nothing
	 * to see go, and nothing for the shelf to draw later. */
	if (c.resume) {
		leave_end how = panel_leave_run(a, gm_backdrop, &c);

		/* Power or Auto Off as it closed: what they mean in the menu */
		if (how == LEAVE_POWER) gm_power(a, &c);
		if (how != LEAVE_GONE) c.resume = false;
	}
	mm_leaving_cut();

	if (c.bg) SDL_DestroyTexture(c.bg);

	/* Asked to quit with the menu open. Leaving here without saying anything
	 * strands the game: it is paused, waiting on this socket, and the wait
	 * loop below would go straight back to waiting on IT. Each on the other,
	 * and neither reachable by a signal. Seen on the device 2026-08-29.
	 *
	 * STOP rather than RESUME - the process is going away, and a game that
	 * ends writes its state on the way out. */
	if ((want_quit || !a->running) && !c.resume) plat_resident_line("STOP");

	if (c.resume) {
		/* Hand the pages back the way Diatom expects to find them.
		 *
		 * Diatom does not repaint the area outside its picture every frame -
		 * its pages start opaque black and it writes only the rect - which is
		 * sound while it owns the framebuffer and false the moment this
		 * process has drawn a full-screen menu into the same pages. At any
		 * display mode that does not fill the panel, resuming showed the game
		 * correctly sized with this menu still surrounding it, and flickering:
		 * Diatom cycles three pages and only the two this process presents
		 * into had been dirtied, so the border alternated menu, menu, black at
		 * the refresh rate.
		 *
		 * Twice because this process alternates two pages and one present only
		 * clears the one it lands on. Before RESUME and never after:
		 * afterwards Diatom is drawing, and this would be a second presenter. */
		present_clear(a, DIATOM_PAGE);
		present_clear(a, DIATOM_PAGE);
		/* SELECT in this menu opens Muse, so the music may have started or
		 * stopped while the game was paused. Said before the game moves, not
		 * a tick after it. */
		plat_resident_quiet(musec_playing());
		/* And the volume and the mute switch, both of which this menu had.
		 *
		 * On RESUME Diatom forgets its volume and reads it back from the
		 * codec. With the switch down in this menu that read finds the gain
		 * this side wrote for the cut and takes it for level 0, so putting
		 * the switch back up in the game brought the sound back at 0: silent
		 * until a volume key. Sending the level after RESUME, as every RUN
		 * does, leaves Diatom nothing to guess. The mute goes first, or the
		 * level would turn the sound back on with the switch still down. */
		plat_resident_line("SETMUTE\ton=%d", plat_muted() ? 1 : 0);
		/* Continue's back is the last interface sound until the menu
		 * opens again: the output is let go the moment it ends. */
		uisound_release();
		plat_resident_line("RESUME");
		{
			int vol = plat_volume_get();

			if (vol >= 0)
				plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d",
				                   vol, PLAT_VOL_MAX + 1);
		}
	}
	/* Nothing presents from here: the next frame on screen is the game's. */
}

/* Bring the resident emulator back.
 *
 * With one process per game a core that segfaults takes down that game and
 * nothing else. With a resident emulator it takes down the emulator, and every
 * launch for the rest of the session would fall back to the slow path -- until
 * the launcher itself exits and launch.sh starts a new one. So the launcher
 * starts one too, once the fallback game has given the display back. Never
 * while a game is running: two GL contexts and a launcher is one more than
 * this device should be asked for. */
static void respawn_resident(app *a)
{
	char elf[CFG_STR * 2], save[CFG_STR * 2], bios[CFG_STR * 2];
	char cores[CFG_STR * 2];
	char *argv[10];
	(void)a;

	if (plat_resident_ready()) return;

	/* The resident is Diatom. It maps every core in the directory it is given
	 * and keeps them, so the directory is the only thing handed over and
	 * nothing here changes when a system is added. */
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	if (access(elf, X_OK) != 0) return;
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
	/* --cores here as well as in launch.sh. There are two places that start
	 * the resident emulator - the boot script and this - and a flag added to
	 * only one of them means every restart after the first silently loses it.
	 * That is exactly what happened: the log showed a premapped boot, then a
	 * dlopen on every launch once this path had restarted it. */
	snprintf(cores, sizeof cores, "%s/cores", P_ROOT);
	argv[0] = elf;
	argv[1] = (char *)"--socket";
	argv[2] = (char *)plat_resident_socket();
	argv[3] = (char *)"--cores";  argv[4] = cores;
	argv[5] = (char *)"--save";   argv[6] = save;
	argv[7] = (char *)"--system"; argv[8] = bios;
	argv[9] = NULL;
	fprintf(stderr, "resident emulator is gone, starting diatom\n");
	plat_spawn_detached(argv, NULL, P_ROOT);
}

/* The Ports shelf. A port is a Linux program with a script of its own - a
 * PortMaster port, DraStic, anything that is not a libretro core - and it
 * wants the display, the buttons and most of the memory, which this process
 * and the resident Diatom are holding. So it does not run under the launcher
 * at all: the launcher names the script in PORT_REQUEST and exits, and
 * launch.sh stops Diatom, runs the script, and starts both again
 * (sd/pixel2/launch.sh). Coming back costs a cold start, which a port's own
 * start dwarfs.
 *
 * A shelf whose core is "run:<script>" is the same handoff for a standalone
 * emulator: the script, under the card, is run with the game as its one
 * argument - Nintendo DS through DraStic, which is no libretro core. The
 * request is then the script and the game, a tab between them. */
#define PORT_CORE    "port"
#define RUN_PREFIX   "run:"
#define PORT_REQUEST "/tmp/zyperos_port"
/* Left by launch_port for the launcher launch.sh starts after the port, so it
 * opens on the game list the port was started from rather than on the systems
 * shelf - which is where a game in Diatom comes back to, since this process
 * never stops for one. Under /tmp, so a boot never sees one: a power-off in the
 * middle of a port starts on the shelf, as every boot does. */
#define PORT_RETURN  "/tmp/zyperos_port_return"

static bool is_port(const system_cfg *s)
{
	return !strcmp(s->core, PORT_CORE) ||
	       !strncmp(s->core, RUN_PREFIX, strlen(RUN_PREFIX));
}

static void launch_port(app *a, const system_cfg *s, const char *file)
{
	char path[LIB_PATH * 2];
	FILE *f;
	bool ok;

	snprintf(path, sizeof path, "%s/%s/%s", P_ROMS, s->folder, file);
	/* Written whole, then renamed into place: launch.sh reads it the moment
	 * this process is gone, and half a path would run nothing. */
	f = fopen(PORT_REQUEST ".tmp", "w");
	if (!strncmp(s->core, RUN_PREFIX, strlen(RUN_PREFIX)))
		ok = f && fprintf(f, "%s/%s\t%s\n", P_CARD,
		                  s->core + strlen(RUN_PREFIX), path) > 0;
	else
		ok = f && fprintf(f, "%s\n", path) > 0;
	if (f && fclose(f) != 0) ok = false;
	if (!ok || rename(PORT_REQUEST ".tmp", PORT_REQUEST) != 0) {
		fprintf(stderr, "port: couldn't write %s\n", PORT_REQUEST);
		unlink(PORT_REQUEST ".tmp");
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
		result_message(a, s->name, T(S_LAUNCH_COULDNT_START));
		return;
	}
	/* A's answer, now that the port will start, and before remember_place
	 * waits on the card. */
	if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
	bgm_hold(BGM_HOLD_GAME, true);              /* faded under the loading screen */
	fprintf(stderr, "port: %s\n", path);
	/* The shelf comes back on this game, and an audiobook keeps its place:
	 * launch.sh stops Muse too, as the port needs the sound card. */
	remember_place(a);
	{
		FILE *r = fopen(PORT_RETURN, "w");
		if (r) fclose(r);
	}
	book_keep(true);
	anim_launch(a);
	port_loading_screen(a, s);
	a->running = false;
}

/* A shelf's disc bios in Bios/: any one of its names, which config/systems.cfg
 * lists with commas (the Saturn core takes any of three). The first is the
 * one a refusal names. */
static bool disc_bios_found(const char *bios, const char *names)
{
	const char *p = names;

	while (*p) {
		size_t n = strcspn(p, ",");
		char fw[CFG_STR * 3];

		snprintf(fw, sizeof fw, "%s/%.*s", bios, (int)n, p);
		if (n && access(fw, R_OK) == 0) return true;
		p += n;
		if (*p) p++;
	}
	return false;
}

static void launch(app *a)
{
	/* The game has the screen, and the CPU: no snap under it */
	snap_off();
	sysview *v = &a->view[a->sys_cursor];
	/* The game's system, not the shelf's. Launching a favorite off the
	 * Favorites shelf otherwise loads whatever core the shelf claims, which
	 * is none, and looks for the ROM under a folder that does not exist. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	const system_cfg *s = &a->sys.systems[o];
	char core[CFG_STR * 2], elf[CFG_STR * 2], rom[LIB_PATH * 2];
	char st[LIB_PATH * 2], pv[LIB_PATH * 2], apv[LIB_PATH * 2];
	char save[CFG_STR * 2], bios[CFG_STR * 2];
	char active[LIB_PATH * 2] = "";
	char set[LIB_PATH * 2];
	int  console = 0;
	bool first_play = false;
	/* 18 fixed entries plus NULL, seven more for a DS game's two screens,
	 * then two per core option. Sized off the loader's own cap so the two
	 * cannot drift apart: the previous 20 was already 18 full, and a silent
	 * bound check would have dropped every option rather than failing
	 * loudly. */
	char *argv[27 + 2 * 32];
	bool resident = false, want_menu;
	unsigned waited;
	int n = 0;

	if (v->list.count == 0) return;
	/* Not beside File Transfer: the game takes the loop that moves the files
	 * (2026-11-06). Closed for it, or the player says when. */
	if (!xs_before_game(a)) return;

	snprintf(core, sizeof core, "%s/cores/%s_libretro.so", P_ROOT, s->core);
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	/* Captured once, beside the path it goes into: play time is keyed on the
	 * launch path the same way favorites are, and reading the cursor again
	 * further down would be reading it at a different moment. */
	const char *romfile = v->list.items[v->cursor].file;

	if (is_port(s)) {
		launch_port(a, s, romfile);
		return;
	}

	snprintf(rom, sizeof rom, "%s/%s/%s", P_ROMS, s->folder, romfile);
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);

	/* A DS game: two screens in one frame, a DraStic save to take over the
	 * first time, and frames left undrawn when they run late - DSperate's
	 * 3D is the heaviest thing a Cortex-A35 is asked to draw, and a skipped
	 * drawing keeps the game at its speed where a late frame slows it. */
	g_game_dual = two_screens(s);
	if (g_game_dual) dsv_import(save, romfile);
	{
		/* The RUN keys this game needs beyond the usual: a DS game's two
		 * screens, the system's clock ceiling and controller type, and its
		 * button map (config.h: power, controller, map) */
		char keys[96];

		cfg_run_keys(s, g_game_dual, keys, (int)sizeof keys);
		if (keys[0]) plat_resident_run_keys(keys);
		plat_resident_map(s->map);
	}

	/* A disc that needs firmware, before anything tries to run it.
	 *
	 * Diatom checks this too and answers bios_missing, but that answer has
	 * never been reachable: it acts on a `firmware=` key the launcher has
	 * never sent. So the core was asked to load a CD with no System Card,
	 * refused it the way it refuses a corrupt ROM, and the shelf came back
	 * with nothing said - which is a long way from "needs Bios/syscard3.pce".
	 *
	 * Disc images only. A HuCard needs no System Card, and refusing every
	 * cartridge on the shelf because a CD would have needed one is a worse
	 * failure than the one being fixed. */
	if (s->disc_bios[0] && lib_is_disc(v->list.items[v->cursor].file)) {
		if (!disc_bios_found(bios, s->disc_bios)) {
			char msg[CFG_STR + 96];

			snprintf(msg, sizeof msg, T(S_LAUNCH_NEEDS_BIOS),
				 (int)strcspn(s->disc_bios, ","), s->disc_bios);
			if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
			result_message(a, v->list.items[v->cursor].title, msg);
			return;
		}
	}

	/* A's answer, once nothing is left that can refuse the launch: the
	 * work below reads files and is not to be waited for. Only for A: a game
	 * resumed at boot, or started by ZYPEROS_AUTOLAUNCH, had no press. */
	if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);

	/* The autosave story, by path rather than by convention: the state and the
	 * preview live beside each other, the launch hands both over, and a game
	 * always comes up where it was left. */
	state_path(a, o, &v->list.items[v->cursor], st, sizeof st);
	/* The scratch, not the Auto card - see pause_preview_path. The card is
	 * written from this only when the game actually ends, below. */
	pause_preview_path(pv, sizeof pv);
	persist_dir_ensure(a, o);

	/* Achievements, if this game has any. Most of a library does not, and that
	 * is not a failure: chv_load says so by returning false and everything
	 * below carries on with console 0 and no set, which is what Diatom reads
	 * as "this game has none". */
	{
		chv_path(P_ROMS, s->folder, v->list.items[v->cursor].name,
		         set, sizeof set);
		/* Every launch, not just a first play: this is what a late fetch is
		 * checked against, and it has to name the game on screen now rather
		 * than the last one that went looking for a set. */
		snprintf(g_game_set, sizeof g_game_set, "%s", set);

		/* The async slot, cleared of an account answer from the last game that
		 * never landed, before this launch asks anything of its own. And no
		 * watch list to trim until this game has handed one over. */
		sync_abandon();
		g_watch_ms = 0;

		/* Fetch it if this game has never been played here. Only then: a
		 * cached set costs nothing and this is the launch path, so the delay
		 * is paid once per game rather than every time. Failing is ordinary -
		 * offline, not signed in, or a game RetroAchievements has never seen -
		 * and the launch carries on without. */
		/* Never played here, so there is no set to hand over. The game starts
		 * anyway and the set is found behind it - see ra_fetch_begin. Nothing
		 * in front of the launch. */
		first_play = ra_signed_in() && access(set, R_OK) != 0 && net_online();

		if (chv_load(set)) {
			/* Reconcile with the account BEFORE deciding what to watch.
			 * Without this the launcher filters against what this device
			 * happens to have seen, which is not the same question and does
			 * not look different: measured 2026-08-29 as 3 of 40 for Contra
			 * against the site's own 13. */
			/* The account's answer, without the launch waiting for it - read
			 * while the game runs, see sync_poll. On a first play there is
			 * nothing to ask about yet; that question is asked once the set
			 * arrives. */
			if (!first_play) sync_begin(chv_game(), set);


			/* No "startsession" request, deliberately. It drives the "currently
			 * playing" indicator on the website and nothing on the device, and
			 * it is another 320ms request - which is the exact trade this whole
			 * path exists to refuse. */

			chv_active_path(active, sizeof active);
			if (chv_write_active(active)) {
				console = chv_console();
			} else {
				/* Everything in the set is already earned. Nothing to watch,
				 * and sending an empty file would have Diatom log a set with
				 * no achievements in it every launch. */
				active[0] = '\0';
			}
		}
	}

	/* The pop-ups start with this game; the first is what is earned so far,
	 * once the game is running (the tick shows it), when the set was here
	 * before the launch - a first play says it when the set arrives */
	nq_clear(&g_nq);
	if (active[0] || (chv_count() > 0 && !first_play)) {
		nq_item it;

		memset(&it, 0, sizeof it);
		it.kind = NQ_SET;
		it.ms = 3000;
		snprintf(it.head, sizeof it.head, T(S_CHEEVOLIST_UNLOCKED), chv_earned(), chv_count());
		snprintf(it.body, sizeof it.body, "%s", chv_game_title());
		nq_push(&g_nq, &it);
	}

	remember_place(a);
	/* Before the launch, not after: a device that loses power during the load
	 * was still playing this game. */
	playing_set(a);

	/* Read and cleared together. Leaving it set would have the NEXT game
	 * launched in this session open with its menu up too, which is the sort
	 * of thing that looks like a haunting rather than a bug. */
	want_menu = a->resume_menu;
	a->resume_menu = false;

	/* The output is let go the moment A's accept ends, well before the game
	 * makes a sound of its own. */
	uisound_release();
	/* Menu music fades out under the launch; the shelf lets it go again */
	bgm_hold(BGM_HOLD_GAME, true);

	/* Whether this game should be heard - quiet while Muse plays, Diatom's
	 * QUIET. Stated before the RUN goes out, which sends it. */
	plat_resident_quiet(musec_playing());
	/* And whether the battery is low, so the disc is over its first frame. */
	plat_resident_low_battery(battery_level());

	/* A game resumed at boot waited 71 and 47 ms for the resident on the
	 * Pixel 2 (2026-10-05, a DAC plugged in), where it had been falling back
	 * unseen. A second is ten times that, and only ever spent on a Diatom
	 * that is running but stuck; one that isn't running falls back at once. */
	if (plat_resident_await(1000, &waited)) {
		if (waited)
			fprintf(stderr, "resident emulator answered after %u ms\n", waited);
		/* The emulator is already up, holding its context and every core,
		 * so this is ~200ms rather than ~1100. Nothing here is torn
		 * down -- this process keeps its own context through the whole game,
		 * which is why coming back is a frame rather than a second and a
		 * half. The two do overlap for the length of this animation, while
		 * the game loads behind it; after that only the emulator draws,
		 * because this process is blocked - except when the player opens
		 * the in-game menu, which is drawn HERE now, over the frame the
		 * emulator hands us on the way into its pause. */
		aout_before_launch();
		if (plat_resident_send(s->tag, core, rom, st, st, pv,
		                       console, active[0] ? active : NULL)) {
			int r;

			/* The list Diatom was just handed, for an early answer to trim. */
			if (active[0]) {
				g_watch_ms = plat_now_ms();
				snprintf(g_watch_active, sizeof g_watch_active, "%s", active);
			}

			/* Play time starts here, and costs a clock read. Nothing is
			 * opened or written - the launch path does no I/O for this, on
			 * purpose, because a millisecond bought here is the first of
			 * several. See stats.h. */
			stats_begin(s->tag, romfile, plat_now_ms());

			/* Straight after RUN and the levels, and for the same reason: the
			 * mode is Diatom's own global and survives from the last game, so
			 * a system that has never been set would otherwise inherit
			 * whatever the previous one chose. Ordered on the same socket, so
			 * it lands before the first frame. */
			plat_resident_line("SETDISPLAY\tmode=%s",
			                   DMODES[a->view[o].dmode].name);
			/* The DS layout, for the same reason and in the same place: the
			 * layout is Diatom's own global too. */
			if (g_game_dual) {
				int lay;
				bool sw;

				screens_get(s->tag, &lay, &sw);
				plat_resident_line("SETSCREENS\tlayout=%s\tswap=%d",
				                   gm_layout_names[lay], sw ? 1 : 0);
			}

			/* Auto Off. Out here rather than inside the chv_load branch
			 * above, which is where the first version put it and is the same
			 * mistake the paragraph below describes: that branch is the one a
			 * game with no achievements never takes, so Auto Off only ever
			 * worked for games that happened to have a cached set.
			 *
			 * Unless on the charger - the feature exists to stop a game
			 * running unattended on battery, and plugged in there is no
			 * battery reason and no cost to leaving it. on_game_tick keeps
			 * this true if the cable changes mid-game. */
			g_idle_secs = a->auto_off;
			g_idle_charging = on_charger();
			plat_resident_line("SETIDLE\tms=%d",
			                   (g_idle_secs > 0 && !g_idle_charging)
			                       ? g_idle_secs * 1000 : 0);

			/* The first play of this game: nothing was cached, so there is a
			 * set to go and find. Out here and not inside the chv_load branch
			 * above, which is the mistake the first version made - that
			 * branch is precisely the one a first play does not take.
			 *
			 * Hashing is local work and happens after RUN, so it overlaps the
			 * game's own startup rather than delaying it. The two requests
			 * then run behind the game and on_game_tick hands the set over
			 * when they land. */
			if (first_play) {
				char h[33];

				if (ra_hash_rom(rom, s->tag, h)) {
					chv_active_path(g_pending_active, sizeof g_pending_active);
					snprintf(g_pending_set, sizeof g_pending_set, "%s", set);
					ra_fetch_begin(h, set);
				}
			}
			/* Coming back from a shutdown: the game loads and the menu is
			 * already up, so nothing is handed control of a game the player
			 * may not have meant to resume. Ordered on the same socket, so it
			 * lands before the first frame the player could act on. */
			if (want_menu) plat_resident_line("PAUSE");
			g_menu_unasked = want_menu;
			/* No launch animation, and it is a display-safety rule, not a
			 * taste call: Diatom presents through fbdev, this process
			 * through GL, and the handoff spike's one invariant is that
			 * they never present concurrently - a 190ms overlap that is
			 * harmless GL-on-GL is the exact case that wedges the display
			 * engine when one side is fbdev. A warm launch is 26 to 44 ms from
			 * RUN to RUNNING, so there is nothing to animate over anyway; the
			 * shelf simply holds until the game's first frame replaces it. */
			for (;;) {
				r = plat_resident_wait();
				if (r == RES_PAUSED) { game_menu(a); continue; }
				break;
			}
			resident = (r == RES_EXIT);

			/* The Auto card's picture, written ONLY here - at the same moment
			 * Diatom wrote the state it belongs to, so the two cannot drift.
			 * Between exits the card keeps the previous session's frame, which
			 * is exactly what resuming would give you. */
			if (resident) {
				char ap[LIB_PATH * 2];

				preview_path(a, o, &v->list.items[v->cursor],
				             ap, sizeof ap);
				copy_file(plat_resident_last_preview(), ap);
			}

			/* The game is over, so this is the one write that is synced -
			 * on the path where the player is waiting for the shelf rather
			 * than for a game to start. */
			stats_end(NULL, plat_now_ms());

			/* Whatever was earned is sent now, behind the shelf rather than in
			 * front of it - see ra_flush_start. It used to be sent right here,
			 * blocking, on the reasoning that the wait loop above is the power
			 * button's watchdog and so the first safe moment was after it. True
			 * of that loop, and not the only place the watchdog matters: this
			 * spot is between that loop and the shelf's, so the button was
			 * read by neither while it waited. Whatever will not send stays
			 * queued. */
			/* After the account's answer, so anything it already held is
			 * settled before deciding what is owed - otherwise the flush would
			 * cheerfully submit a dozen duplicates. The answer has usually been
			 * in since half a second into the game. If it is still on its way,
			 * the sending waits for it rather than the shelf - see sync_poll.
			 *
			 * The game is passed rather than read later: by the time a waiting
			 * send starts, the info screen may have loaded another set. */
			g_watch_ms = 0;
			sync_poll();
			if (g_sync_game) {
				g_sync_flush = true;
				snprintf(g_sync_rom, sizeof g_sync_rom, "%s", rom);
				snprintf(g_sync_tag, sizeof g_sync_tag, "%s", s->tag);
				g_sync_cur = chv_game();
				fprintf(stderr, "ra: the account's answer is still on its way; "
				                "sending waits for it\n");
			} else {
				ra_flush_start(rom, s->tag, chv_game());
			}
			/* The set's badges, behind the shelf, for the next time it is
			 * played or opened (src/rabadge.h) */
			rb_fetch_loaded();

			/* RES_DEAD means it stopped answering -- it died, or the game
			 * never started. Say so by falling through to the path that
			 * runs it the slow way, rather than fading the shelf back up
			 * as though it had run. */
			if (!resident)
				fprintf(stderr, "resident emulator stopped answering, falling back\n");
		} else {
			fprintf(stderr, "resident emulator did not answer, falling back\n");
		}
	} else {
		/* Silent until 2026-10-05, which is how a resumed game running in
		 * the fallback went unnoticed. */
		fprintf(stderr, "no resident emulator to connect to (waited %u ms), falling back\n",
		        waited);
	}

	if (!resident) {
		/* One game per process, the old way: the fallback for a resident that
		 * is missing or has died. Diatom standalone IS the one-shot mode -
		 * same binary, no socket - so the fallback stopped being a different
		 * emulator and became the same one held differently. It has to take
		 * the display, so this side tears its own down first. */
		anim_launch(a);
		free_all_textures(a);
		page_fonts_quit();
		ui_quit();
		plat_input_quit();
		plat_video_quit();

		argv[n++] = elf;
		argv[n++] = (char *)"--core";            argv[n++] = core;
		argv[n++] = (char *)"--rom";             argv[n++] = rom;
		argv[n++] = (char *)"--save";            argv[n++] = save;
		argv[n++] = (char *)"--system";          argv[n++] = bios;
		argv[n++] = (char *)"--display";
		argv[n++] = (char *)DMODES[a->view[o].dmode].name;
		/* The channels too: this path is rare, but a player who hears with
		 * one ear should not lose half a game to it. It plays on the codec,
		 * so the cable decides between headphones and the speaker. */
		{
			aout_state cs = aout_now();

			argv[n++] = (char *)"--channels";
			argv[n++] = (char *)aout_channels_wire(
				aout_channels_for(g_channels, aout_dest_of(&cs, "")));
		}
		argv[n++] = (char *)"--load-state";      argv[n++] = st;
		argv[n++] = (char *)"--state-on-exit";   argv[n++] = st;
		/* A DS game's screens, as RUN and SETSCREENS give them to the
		 * resident one. */
		if (g_game_dual) {
			int lay;
			bool sw;

			screens_get(s->tag, &lay, &sw);
			argv[n++] = (char *)"--screens";   argv[n++] = (char *)"2";
			argv[n++] = (char *)"--frameskip"; argv[n++] = (char *)"late";
			argv[n++] = (char *)"--layout";    argv[n++] = (char *)gm_layout_names[lay];
			if (sw) argv[n++] = (char *)"--swap";
		}
		/* The Auto card's own path here, not the scratch the resident mode
		 * uses. Standalone has no in-game menu - the launcher is torn down -
		 * so nothing can overwrite it mid-session, and it is written once at
		 * exit beside the state. The resident path needs the indirection
		 * because a pause writes the preview too; this one does not. */
		preview_path(a, o, &v->list.items[v->cursor], apv, sizeof apv);
		argv[n++] = (char *)"--preview-on-exit"; argv[n++] = apv;
		/* Same opinions as the resident path gets over SETOPT, so a game plays
		 * the same whether the resident was up or the fallback ran it. */
		{
			int ci;
			for (ci = 0; ci < plat_coreopt_count(s->tag) && n < (int)(sizeof argv / sizeof argv[0]) - 3; ci++) {
				argv[n++] = (char *)"--core-option";
				argv[n++] = (char *)plat_coreopt(s->tag, ci);
			}
		}
		argv[n] = NULL;
		fprintf(stderr, "diatom exited %d\n", plat_run(argv, NULL, P_ROOT));

		if (!plat_video_init() || !plat_input_init()) { a->running = false; return; }
		a->r = plat_renderer();
		ui_init(a->r, P_FONT);
		page_fonts_init(P_FONT);
		a->menu_w = 0;   /* fonts reopened: remeasure the panel */
		prime_home(a);
		prime_window(a, a->sys_cursor);
		/* The display is ours again and no game is running: the safe moment
		 * to put a resident emulator back, so the NEXT launch is fast. */
		respawn_resident(a);
	}

	t_back0 = plat_now_ms();
	present_ground(a);

	/* The game has just written a fresh autosave preview. Throw the card's
	 * texture away so it picks that up - but only if the preview is what the
	 * card is drawn FROM.
	 *
	 * Box art comes first in the order a card finds its picture, so for a game
	 * that has it this used to rebuild the card from a file that had not
	 * changed, into a pixel-identical texture. That was always wasted work, and
	 * it was invisible while decoding blocked the frame: the card was back
	 * before anything drew. Since decoding moved to a worker, the card is
	 * genuinely absent for a few frames, and the one you just quit popped in
	 * while every card beside it was already there. The question was never
	 * "did the preview change", it is "is this card showing the preview". */
	{
		game_entry *g = &v->list.items[v->cursor];
		int o = shelf_owner(a, a->sys_cursor, v->cursor);

		if (v->tex[v->cursor] && !has_box_art(a->sys.systems[o].folder, g->name)) {
			SDL_DestroyTexture(v->tex[v->cursor]);
			v->tex[v->cursor] = NULL;
		}
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* The idle clock is not restarted here, though it has been stopped for
	 * the whole session. idle_due notices the gap on its own, which is what
	 * stops this from being a list of places to remember. */

	/* A power-off from outside ends the game the same way: the system's
	 * shutdown TERMs this process and Diatom together (plastron's zyperos
	 * stop), and Diatom saves the game and its state on its way out - the
	 * kernel's at 115 C (an orderly power-off, src/heat.h), `poweroff` from
	 * a shell. That is a power-off mid-game,
	 * as POWER is, so the marker stays and the next start goes back into the
	 * game; it was cleared below as a return to the shelf. */
	if (access(ZYPEROS_POWEROFF_FLAG, F_OK) == 0 || want_quit) {
		a->running = false;
		return;
	}
	/* Power was pressed during the game. The emulator no longer handles that
	 * key itself, so the press arrived here. */
	if (plat_run_power_pressed()) { power_off(a); return; }

	/* Past both power checks, so this is a real return to the shelf rather
	 * than a shutdown. Everything above leaves the marker standing, which is
	 * what makes the next boot able to tell them apart. */
	playing_clear();


	tick_theme(a);
	render(a);
	fprintf(stderr, "exit: back in %u ms\n", plat_now_ms() - t_back0);
}

/* ---------- input --------------------------------------------------------- */

void enter_system(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	/* Muse too: its cards are albums, and opening one is the games shelf's
	 * own A - see update_games. */
	if (v->list.count == 0) return;
	a->screen = SCREEN_GAMES;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	prime_window(a, a->sys_cursor);
}

/* Y on a card: a favorite added is a reward, one taken away goes back, and a
 * list already full (FAV_MAX) is a press nothing can act on. */
static void fav_toggle_heard(const char *tag, const char *file)
{
	bool was = fav_is(tag, file), now = fav_toggle(tag, file);

	uisound_play(now ? DS_SOUND_REWARD : was ? DS_SOUND_BACK : DS_SOUND_DENY);
}

/* Start on a system's cards when the systems row moves onto it, not when A is
 * pressed.
 *
 * Entering used to be the first time anything asked for a system's covers, so
 * the games shelf drew with empty slots until they arrived. Measured
 * 2026-09-14, cold after a reboot, across six systems: the focused card 70-160ms
 * after A, the whole row 95-177ms. Short, and visible. Leaving a system keeps
 * the cards around its cursor, which is why only the first entry showed it.
 *
 * Starting here gives the decode however long it takes to reach the system and
 * press A, and that is enough: re-measured the same way, four systems entered
 * after a reboot all had every card already loaded on arrival.
 *
 * Whatever was queued for the system just passed is dropped first, so a
 * held direction does not decode a row for every system it crosses, and the one
 * the row stops on is not left waiting behind them.
 *
 * The visible row only; entering asks for two more either side. They stay after
 * the row moves on, which is less than already stays: a system entered and left
 * keeps nine. */
static void load_ahead(app *a)
{
	texload_forget();
	prime_toward(a, a->sys_cursor, a->view[a->sys_cursor].cursor);
}

/* Where up (-1) or down (+1) goes on the home with the order on A to Z */
static int home_letter_at(const app *a, int dir)
{
	unsigned char keys[CFG_MAX_SYSTEMS];
	int i, n = a->sys.count < CFG_MAX_SYSTEMS ? a->sys.count : CFG_MAX_SYSTEMS;

	for (i = 0; i < n; i++) keys[i] = home_group_key(a->sys.systems[i].tag, a->sys.systems[i].core,
		                         a->sys.systems[i].name);
	return home_letter_jump(keys, n, a->sys_cursor, dir);
}

static void update_systems(app *a)
{
	int n = a->sys.count;

	/* No games: the empty home's keys (and MP4 alone is no shelf to move
	 * on). No systems at all also means the modulo below divides by zero:
	 * the first press of left or right took the launcher down with SIGFPE
	 * the moment empty systems started being hidden. */
	if (no_games(a)) { empty_keys(a); return; }

	/* The direction is carried through, not inferred from the cursor: on a
	 * shelf of two, moving from either card to the other is one step in BOTH
	 * directions, and only the press says which. */
	in_button back = IN_LEFT;
	in_button fwd  = IN_RIGHT;
	in_button sb = IN_NONE;              /* the step's button, for its sound */
	int dir = 0, was = a->sys_cursor;

	if (in_repeat(&a->in, back)) { a->sys_cursor = (a->sys_cursor - 1 + n) % n; dir = -1; sb = back; }
	if (in_repeat(&a->in, fwd))  { a->sys_cursor = (a->sys_cursor + 1) % n; dir = +1; sb = fwd; }
	/* L1/R1 jump three systems, round the ends as a step goes (home_jump),
	 * the row moving all the way in the press's direction */
	if (in_repeat(&a->in, IN_L1)) { a->sys_cursor = home_jump(a->sys_cursor, n, -1); dir = -1; sb = IN_L1; }
	if (in_repeat(&a->in, IN_R1)) { a->sys_cursor = home_jump(a->sys_cursor, n, +1); dir = +1; sb = IN_R1; }
	/* Up and down do the same, one group at a time: three systems by release
	 * date, and an initial by A to Z (home_letter_jump). Built on the press,
	 * from the names in memory - nothing is read. */
	if (in_repeat(&a->in, IN_UP) || in_repeat(&a->in, IN_DOWN)) {
		int d = in_repeat(&a->in, IN_DOWN) ? +1 : -1;

		a->sys_cursor = g_order_release ? home_jump(a->sys_cursor, n, d)
		                                : home_letter_at(a, d);
		dir = d; sb = d > 0 ? IN_DOWN : IN_UP;
	}
	if (a->sys_cursor == was) dir = 0;
	if (dir) load_ahead(a);
	if (sb != IN_NONE) step_sound(a, sb, was, a->sys_cursor);
	/* Muse's card opens Muse, the one SELECT opens - see update_games. */
	if (a->in.pressed[IN_ACCEPT]) {
		if (is_muse(&a->sys.systems[a->sys_cursor])) muse_open(a, NULL, NULL);
		else { uisound_play(DS_SOUND_ACCEPT); enter_system(a); }
	}
	home_select(&a->home, a->sys_cursor, n, dir, plat_now_ms());
}

/* The first alphanumeric character of a name, upper-cased, or '\0' for a name
 * with none at all - which groups those few together rather than giving each
 * one a group of its own. Leading articles and punctuation are deliberately
 * NOT skipped: lib_scan sorts the shelf by its displayed title with strcasecmp,
 * so grouping that disagreed with the order the cards are drawn in would make
 * the jump land somewhere that looks arbitrary. "The Legend of Zelda" files
 * under T here because it sits under T on the shelf. */
static char shelf_initial(const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c >= 'a' && c <= 'z') return (char)(c - 32);
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (char)c;
	}
	return '\0';
}

/* The first game sharing idx's initial, walking backwards from it. */
/* What the letter jump reads: the title, or on Muse's shelf the artist. */
static const char *shelf_key(const sysview *v, int i)
{
	return v->jump_by_name ? v->list.items[i].name : v->list.items[i].title;
}

static int shelf_group_start(sysview *v, int idx)
{
	int n = v->list.count, i = idx;
	char c = shelf_initial(shelf_key(v, idx));

	for (;;) {
		int p = (i - 1 + n) % n;
		if (p == idx) return idx;             /* one initial, the whole shelf */
		if (shelf_initial(shelf_key(v, p)) != c) break;
		i = p;
	}
	return i;
}

/* Up and down cross the shelf one initial at a time, which on a long library
 * is the difference between forty presses and two. Down lands on the first
 * game of the next initial. Up lands on the first game of THIS one, and moves
 * to the previous initial only when the cursor is already there - so from any
 * group's first game, up and down are exact inverses. */
static int shelf_letter_jump(sysview *v, int dir)
{
	int n = v->list.count, i, cur = v->cursor;
	char c0 = shelf_initial(shelf_key(v, cur));

	if (n <= 1) return cur;
	if (dir > 0) {
		for (i = 1; i < n; i++) {
			int k = (cur + i) % n;
			if (shelf_initial(shelf_key(v, k)) != c0) return k;
		}
		return cur;                           /* every name starts alike */
	}
	i = shelf_group_start(v, cur);
	if (i != cur) return i;
	/* Already at the top of the group. If what precedes it shares the initial
	 * then the shelf is one group and there is nowhere to go - the same answer
	 * down gives, rather than shuffling back by one. */
	i = (cur - 1 + n) % n;
	if (shelf_initial(shelf_key(v, i)) == c0) return cur;
	return shelf_group_start(v, i);
}

static void update_games(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	int n = v->list.count;
	if (n <= 0) { a->screen = SCREEN_SYSTEMS; return; }
	/* MUSE HAS ONE DOOR. Its card used to open its shelf HERE, in the main
	 * loop, as if it were a console: a second Muse beside the one SELECT
	 * opens, so SELECT on it opened that one on top, and the two looked
	 * alike. Eric's call, 2026-09-23: the card opens the same Muse SELECT
	 * does. Whatever lands the main loop on Muse's shelf - A on its card, a
	 * jump to it, a screen put back at boot - hands over to muse_open, which
	 * comes back to the card on the systems row. */
	if (is_muse(&a->sys.systems[a->sys_cursor])) {
		a->screen = SCREEN_SYSTEMS;
		muse_open(a, NULL, NULL);
		return;
	}
	/* The two axes trade roles with the shelf rather than one of them being
	 * reassigned onto the other: stepping always runs along the row, and the
	 * letter jump always runs across it. Turning the shelf turns the d-pad
	 * with it and nothing is left doing two jobs. */
	in_button back = IN_LEFT, fwd = IN_RIGHT;
	in_button jup = IN_UP, jdn = IN_DOWN;
	in_button sb = IN_NONE;              /* the step's button, for its sound */
	int dir = 0, was = v->cursor;

	if (in_repeat(&a->in, back)) { v->cursor = (v->cursor - 1 + n) % n; dir = -1; sb = back; }
	if (in_repeat(&a->in, fwd))  { v->cursor = (v->cursor + 1) % n; dir = +1; sb = fwd; }
	if (in_repeat(&a->in, jdn))  { v->cursor = shelf_letter_jump(v, +1); sb = jdn; }
	if (in_repeat(&a->in, jup))  { v->cursor = shelf_letter_jump(v, -1); sb = jup; }
	/* L1/R1 jump a screenful, so a long shelf is crossable. Wrapped the long
	 * way round on purpose: C's % truncates toward zero, so on a shelf of
	 * three games the obvious `(cursor - CF_WINDOW + n*2) % n` lands on -1 and
	 * the draw walks off the front of the list. */
	if (in_repeat(&a->in, IN_L1))    { v->cursor = ((v->cursor - CF_WINDOW) % n + n) % n; sb = IN_L1; }
	if (in_repeat(&a->in, IN_R1))    { v->cursor = (v->cursor + CF_WINDOW) % n; sb = IN_R1; }
	if (sb != IN_NONE)
		step_sound(a, sb, was, v->cursor);
	/* HERE, AND NOT AT THE END OF THE PASS, because four of the handlers below
	 * return without reaching the end.
	 *
	 * A repeat fires from the clock every REPEAT_RATE_MS while a direction is
	 * held, and the loop passes at about 60Hz while any button is down, so
	 * roughly one pass in five moves the cursor. Press A on one of those passes
	 * and the shelf was never told: `target` stays a card behind `cursor`, the
	 * game runs, the render on the way back snaps the cards to that stale
	 * target, and the NEXT pass through here finally notices and animates the
	 * difference. Which is exactly the report - the shelf comes back showing a
	 * game off to the side and then slides across to the right one - and the
	 * one-in-five is why it only happened sometimes.
	 *
	 * X and Y are the same shape: the info screen and a favorite toggle both
	 * return from here too. B is the only one that got away with it, because
	 * enter_system resets this coverflow on the way back in.
	 *
	 * The call is unconditional and `dir` may be 0: a letter jump and L1/R1
	 * move the cursor without a direction, and cf_set_cursor_dir works out the
	 * rest itself. It returns immediately when the cursor has not moved. */
	games_select(&v->row, v->cursor, n, dir, plat_now_ms());
	if (a->in.pressed[IN_BACK]) {
		uisound_play(DS_SOUND_BACK);
		a->screen = SCREEN_SYSTEMS;
		evict_far(v, TEX_KEEP_FAR);
		return;
	}
	/* X opens the game. Y marks it. The pair sits together because they are
	 * the two things you do to a card without launching it. */
	if (a->in.pressed[IN_X] && n > 0) { uisound_play(DS_SOUND_ACCEPT); game_info_screen(a); return; }
	/* Y favorites what is under the cursor. Written through immediately:
	 * there is no confirm step to hang the save off, and the alternative is
	 * losing the choice to a flat battery. */
	if (a->in.pressed[IN_Y] && n > 0) {
		fav_toggle_heard(a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)].tag,
		                 v->list.items[v->cursor].file);
		fav_save();
		/* The shelf follows immediately. Everything below this line may have
		 * moved - the system indices, sys_cursor, and v itself - so nothing
		 * from before it can be reused. */
		refresh_favorites_shelf(a);
		return;
	}
	if (a->in.pressed[IN_ACCEPT]) { launch(a); return; }
}

/* ---------- main ---------------------------------------------------------- */

/* --shot <file.png> [--screen games|systems] draws one frame, writes it out
 * and exits. This is how the shelf gets looked at without a device in hand:
 * the host build renders exactly what the handheld renders. */
static const char *shot_path;
static int shot_screen = -1;
static int shot_menu, shot_menu_sel;
static int shot_kb, shot_kb_layer;
static keyboard_transition shot_keyboard;
static bool shot_keyboard_panel;
/* --kbsel ROW COL: the keyboard's selected key, rows 0-3 the grid and row 4
 * the action row, with COL its key (src/kbnav.h). It opens on the letters'
 * first key, as kb_prompt does. */
static int shot_kb_row = 1, shot_kb_col;
static const char *shot_notice;
static const char *shot_notice_head = "Unlocked · 5 points";
/* --notice-badge PNG and --notice-progress VALUE TARGET: the same notice with
 * a badge, or as a progress notice (notice_render_ex) */
static const char *shot_notice_badge;
static int shot_notice_value, shot_notice_target;
/* --notice-heat: the notice with the thermometer where a badge goes, as
 * "Getting hot" is over a game (DESIGN.md 11.23) */
static int shot_notice_mark;
/* --lowbatt MS [charging|off]: the low-battery start screen MS ms in
 * (DESIGN.md 11.24); with charging, a charger plugged in at 2.4 s; with off,
 * MS ms into its send-off */
static int shot_lowbatt = -1, shot_lowbatt_kind;
/* --heatpop MS: Zyper's "Getting hot" on the home, MS ms in (DESIGN.md 11.23) */
static int shot_heatpop = -1;
/* --charging MS [hold|unplugged|off|pending|kept|limit|keptfull]: the
 * charging screen MS ms in (DESIGN.md 11.25), a wall charger putting 1480 mA
 * into a 62% battery; with hold, POWER held half way; unplugged, the cable
 * out; off, MS ms into its send-off; pending, the charger not named yet;
 * kept, held at an 85% charge limit (11.26); limit, charging toward it;
 * keptfull, held at 100%, charged full before the limit was set */
static int shot_charging = -1, shot_charging_kind;
static int shot_cheevos;
static int shot_cheevos_sel;
/* --ingame: --cheevos-screen as the in-game menu opens it, over a paused
 * game (a made-up frame), rather than over the shelf */
static int shot_ingame;
/* How far between two systems the shelf is caught, 0 at rest. The shot is one
 * settled frame, so anything that only happens DURING a move - the label
 * crossfade, a card mid-slide - cannot be looked at without this. */
static float shot_sysmove;
/* The same for the games shelf, which is the one with a long list under it.
 * Eleven systems make a rail whose segments tile the track; three hundred games
 * make an 18px segment moving two and a half pixels a card, and the two do not
 * look alike at any fraction of a move. */
static float shot_gamemove;
/* --shot-at MS: a motion started for the shot and drawn MS milliseconds in,
 * on a clock held still for it (anim_now, cf_clock), the one clock of every
 * shot that moves: with --menu, the focus plate moving onto the menu's row
 * from the row above it (the list gives too when the window scrolls), or with
 * --open, --close or --swap the top menu opening on the shelf, closing onto
 * it, or giving way to MENU > Controls; with --zyper, Zyper in the top menu
 * that long after it opened; with --launch, the zoom into the focused game;
 * with --poweroff, the power-off; otherwise the shelf on screen stepping one
 * card forward. Frame strips are made from these, a shot for each time. -1,
 * without it, is a shot at rest, drawn as it always was. Unlike --sysmove
 * and --gamemove, which stage a fraction of a move, this presses: the plate,
 * the menu and the shelf are told to move as the buttons tell them, and the
 * frames between are drawn as the device draws them, so whatever follows
 * over time - the plate, the list's give, the label's crossfade, the
 * background tint - is where it is that far in. */
static int shot_at = -1;
/* --launch: the launch zoom at --shot-at's time (0 without it), of the
 * focused game of the focused system, from either screen: the systems screen
 * has the same game focused that its shelf would open on, and the shot reads
 * its card on the frame, as a shot reads every card. */
static int shot_launch;
static const char *shot_cheevos_game = "Hagane: The Final Conflict";
/* --cheevos-badge SET NAME: every row with that badge (src/rabadge.h), the
 * ones not earned greyed, as the list draws them */
static const char *shot_badge_set, *shot_badge_name;
static bool shot_badge_got[8];

static SDL_Texture *shot_row_badge(int row)
{
	return row >= 0 && row < 8 ? rb_texture(g_badge_r, shot_badge_set, shot_badge_name,
	                                        !shot_badge_got[row]) : NULL;
}
static int shot_syn;       /* --synopsis: the card a scraped game opens */
static int shot_info;
static int shot_delete_ask, shot_delete_now;

static int shot_controls = -1;
/* --sgdb [0|1|2]: MENU > SteamGridDB with no key, a key that works, or one
 * they refused - a stand-in key, masked as a real one is */
static int shot_sgdb = -1;
/* --scraper [0-5]: MENU > Scraper at rest, running, after a run; the Video
 * Snaps page; ScreenScraper's page; at rest with no network. Stand-in
 * counts, as a run would have. */
static int shot_scraper = -1;
static int shot_about = -1;
/* --about-scroll F: the story page scrolled F (0 to 1) of the way down, as
 * Up and Down would leave it. Left alone it is the top, where it opens. */
static float shot_about_scroll = -1;
/* --language [ROW]: MENU > Language's panel over the shelf, the cursor on
 * ROW (the language that is on when none is given) */
static int shot_language = -2;
/* --update: MENU > System Update's page as the status file in
 * ZYPEROS_USERDATA/update says (the update lab's states) */
static int shot_update = -1;
static int shot_art;
static const char *shot_art_now = "Legend of Zelda, The - A Link to the Past (USA)";
static int shot_hare;
static int shot_usb = -1;   /* --usb-ask [N]: the question, tile N focused */
/* --xfer-ask [N] [game]: File Transfer's question while files move, tile N
 * focused; with "game", the one a game asked for gets */
static int shot_xfer_ask = -1;
static bool shot_xfer_game;
static int shot_hot;
static const char *shot_hot_ssid   = "ZyperOS-7A2B";
static const char *shot_hot_pass   = "q7mx-3kpa";
static const char *shot_hot_phones = "1 joined";
static const char *shot_hot_now    = "receiving Contra (USA).zip";
static const char *shot_hare_addr = "192.168.1.42";
static const char *shot_hare_who  = "1 connected";
static const char *shot_hare_head = "receiving Contra (USA).zip";
static const char *shot_wait, *shot_wait_msg;   /* NULL: library.scanning's */
static int shot_osd_kind, shot_osd_val; /* --osd volume|brightness LEVEL */
/* --tour STEP [MS [DONE]]: one frame of the tutorial, src/tour.h's step by
 * its name (or "skip", the shelf with the message a skip leaves), MS into
 * it, its press landed DONE ms ago or not at all (-1). --tour-wrong KEY [MS]
 * a press of the wrong button KEY, MS ago; --fn-volume the Pixel 2's keys,
 * whose brightness is MENU+VOL. --tour-pick N [MS]: in a choice step, the
 * focus moved to choice N MS ago (default long since); --tour-music a card
 * with menu music, so the strip counts its step. */
static int shot_tour = -2;          /* -2 none, -1 the skip message */
static int shot_tour_ms = 2000, shot_tour_done = -1;
static int shot_tour_wrong = -1, shot_tour_wrong_ms = 300;
static int shot_tour_pick = -1, shot_tour_pick_ms = 5000;
static bool shot_tour_music;
static int shot_fn_volume;
static const char *const TOUR_STEP_NAMES[TOUR_STEPS] = {
	"lang", "welcome", "move", "a", "b", "y", "x", "menu", "select", "vol", "bright", "power",
	"theme", "home", "sounds", "music", "finish" };

/* --theme light|dark: the theme the shot is drawn in, whatever the saved
 * Appearance is; -1 for the saved one. */
static int shot_theme = -1;
static int shot_home = -1;
/* --lang CODE: the language the shot is drawn in (src/i18n.h; xx, the
 * pseudo-locale, on a host), whatever MENU > Language saved; NULL for it */
static const char *shot_lang;
/* --cheer MS: the cheer for the first games, MS into it, over the home */
static int shot_cheer = -1;
/* --songpop "HEAD|BODY" [MS]: Zyper's pop-up with a song (DESIGN.md 11.22),
 * MS into its life, 1500 by default: in, his hop and pant over, held */
static const char *shot_songpop;
static int shot_songpop_ms = 1500;
static const char *shot_song;
static bool shot_song_paused;
/* --status's words into shot_st, the corner's stand-ins; false on a word it
 * does not know. Wi-Fi is off and the battery has no reading until a word
 * says otherwise, as on a host. */
static bool shot_status_parse(const char *spec)
{
	char buf[128], *w, *save;

	shot_st.set = true;
	shot_st.m = (status_marks){ .wifi = STATUS_WIFI_NONE, .batt = -1 };
	snprintf(buf, sizeof buf, "%s", spec);
	for (w = strtok_r(buf, " ,", &save); w; w = strtok_r(NULL, " ,", &save)) {
		int h, mi;

		if (sscanf(w, "%d:%d", &h, &mi) == 2 && h >= 0 && h < 24 && mi >= 0 && mi < 60)
			shot_st.minutes = h * 60 + mi;
		else if (!strcmp(w, "24h")) shot_st.h24 = 1;
		else if (!strcmp(w, "12h")) shot_st.h24 = 0;
		else if (!strcmp(w, "wifi=off")) shot_st.m.wifi = STATUS_WIFI_NONE;
		else if (!strcmp(w, "wifi=idle")) shot_st.m.wifi = STATUS_WIFI_IDLE;
		else if (!strncmp(w, "wifi=", 5) && w[5] >= '1' && w[5] <= '3' && !w[6])
			shot_st.m.wifi = w[5] - '0';
		else if (!strcmp(w, "bt")) shot_st.m.bt = true;
		else if (!strncmp(w, "batt=", 5)) shot_st.m.batt = atoi(w + 5);
		else if (!strcmp(w, "chg")) shot_st.m.charging = true;
		/* on the cable, held at the charge limit: the plug */
		else if (!strcmp(w, "held")) shot_st.m.charging = shot_st.m.held = true;
		else if (!strcmp(w, "blinkoff")) shot_st.m.blink_off = true;
		/* what MENU > Battery Icon > Time Left writes: eta=200 is 3 h 20 min,
		 * on the cable the time to full; without it, the percentage */
		else if (!strncmp(w, "eta=", 4) && w[4] >= '0' && w[4] <= '9') shot_st.eta = atoi(w + 4);
		/* MENU > Battery Icon for this shot, not the saved one: icon=bars,
		 * icon=percentage or icon=time */
		else if (!strncmp(w, "icon=", 5)) {
			int k;

			for (k = 0; k < BATT_MODES && strcmp(w + 5, BATT_IDS[k]); k++) ;
			if (k == BATT_MODES) return false;
			shot_st.icon = k;
		}
		/* File Transfer behind the shelf: xfer=on, nothing moving; xfer=62,
		 * files moving and the ring that far round */
		else if (!strcmp(w, "xfer=on")) shot_st.m.xfer = true;
		else if (!strncmp(w, "xfer=", 5) && w[5] >= '0' && w[5] <= '9') {
			shot_st.m.xfer = shot_st.m.xfer_moving = true;
			shot_st.m.xfer_pct = atoi(w + 5);
		}
		else return false;
	}
	return true;
}

/* --flip: the shot drawn whole in the theme --theme starts it in, so every
 * card on it is made in that theme, and then the theme turned to the other
 * one the way Auto turns it (shot_flip_theme), and the shot taken there.
 * `make check-flip` holds it against a shot started in the second theme: a
 * card the turn left in the old theme, and the settled frame did not replace,
 * is the difference. */
static int shot_flip;
/* Set by take_shot when the frame it drew fails a check of its own (see
 * shot_cards_have_feet), and the run then exits 3: a shot is also how
 * `make check-flip` looks at the systems row, the games shelf and Now
 * Playing's cover, and a picture that is wrong in a way a byte comparison of
 * two wrong pictures cannot see is one it must not pass. */
static int shot_failed;
/* --portload NAME: the still a port's loading screen leaves for splash, for
 * the game called NAME, drawn to the shot's file in place of a shelf. */
static const char *shot_portload;
/* --poweroff: the power-off animation's frame --shot-at's time in (0
 * without it), in place of a shelf */
static int shot_poweroff;
static const char *shot_kb_text = "correct horse";
/* The title was hardcoded to "Wi-Fi password", so the one other thing that
 * uses this keyboard - signing in to RetroAchievements - could not be
 * rendered at all. A harness narrower than the thing it checks. */
static const char *shot_kb_title = "Wi-Fi password";
static int shot_slots, shot_slot_sel;
static int shot_jump;               /* letter-jumps to apply before drawing */
/* --nowplaying ALBUM [TRACK [SECONDS]], and --paused with it. */
static int   shot_np = -1, shot_np_track, shot_np_paused;
/* --locked: the lock screen over it, its hold three fifths done. --video
 * [full|controls]: Now Playing as a video, a test picture for the frame. */
static int   shot_locked, shot_video = -1;
static float shot_np_at = 83.0f;
/* --mode NAME plays whatever the shot shows in that play mode: the mark beside
 * the track count is drawn from it. */
static const char *shot_mode;
static float shot_slot_aspect = 4.0f / 3.0f;

/* A stand-in for a paused game frame, at whatever shape was asked for: the
 * slot carousel frames the picture to its own aspect, so looking at that
 * without a device means being able to hand it one. */
static SDL_Texture *fake_frame(SDL_Renderer *r, float aspect)
{
	int h = 224, w = (int)(224 * aspect + 0.5f);
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	int x, y;

	if (!s) return NULL;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
			int checker = ((x / 16) + (y / 16)) & 1;
			px[x] = SDL_MapRGBA(s->format,
			                    (Uint8)(30 + 180 * x / w),
			                    (Uint8)(40 + 150 * y / h),
			                    (Uint8)(checker ? 150 : 90), 255);
		}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	return t;
}

static void shot_draw_slots(app *a)
{
	slot_view sv = { .aspect = shot_slot_aspect, .saving = 0 };
	int i;

	for (i = 0; i <= GM_SLOTS; i++) {
		/* An afternoon in August, a slot an hour apart */
		struct tm tm = { .tm_mon = 7, .tm_mday = 20 + i, .tm_hour = 13 + i,
		                 .tm_min = i * 7, .tm_sec = i * 9 };

		/* Slot 5 left empty, so the empty state is in the picture too. */
		if (i == 5) continue;
		sv.have[i] = 1;
		slot_when(sv.when[i], sizeof sv.when[i], &tm);
		sv.thumb[i] = fake_frame(a->r, shot_slot_aspect);
	}
	ui_scrim(a->r, 1);
	slot_draw(a, &sv, shot_slot_sel);
	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
}

/* --gamemenu: the menu over a paused game, as gm_backdrop and the runner draw
 * it, on a made-up frame: the one place the status corner is drawn in Salt &
 * Pepper whatever the theme. On a games shelf, whose system and game it
 * builds its rows for. */
static int shot_gamemenu, shot_gamemenu_sel;

static void shot_draw_gamemenu(app *a)
{
	menu_row rows[GM_STYLUS + 1];
	gm_bufs b;
	SDL_Texture *bg = fake_frame(a->r, 4.0f / 3);
	int n;

	gm_backdrop(a, &(gm_ctx){ .a = a, .bg = bg }, 1.0f);
	n = gm_build(a, rows, &b);
	menu_draw(a, NULL, rows, n, shot_gamemenu_sel < n ? shot_gamemenu_sel : n - 1,
	          gm_width(a), a->tint);
	if (bg) SDL_DestroyTexture(bg);
}

/* --gamecontrols [page] [TAG]: the in-game menu's Controls page over a
 * paused game, page `page`, for the game under the cursor on the shelf of
 * the system tagged TAG (the shelf the shot is on when none is given), as
 * game_controls_screen draws it. --fn-volume names the Pixel 2's
 * brightness chord. */
static int shot_gamecontrols = -1;
static const char *shot_gamecontrols_tag;

static void shot_draw_gamecontrols(app *a)
{
	SDL_Texture *bg = fake_frame(a->r, 4.0f / 3);
	menu_row rows[CTL_MAX_ROWS];
	ctl_buf b;
	ctl_game g;
	char count[16];
	int i, n, pages;

	for (i = 0; shot_gamecontrols_tag && i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, shot_gamecontrols_tag)) {
			a->sys_cursor = i;
			a->tint = a->sys.systems[i].accent;
			break;
		}
	g = game_controls_of(a);
	ctl_set_fn_volume(shot_fn_volume || plat_brightness_on_volume());
	n = ctl_game_rows(&g, shot_gamecontrols, rows, &b);
	pages = ctl_game_pages(&g);
	snprintf(count, sizeof count, "%d / %d",
	         shot_gamecontrols < pages ? shot_gamecontrols + 1 : 1, pages);
	paused_backdrop(a, bg, 1.0f);
	menu_draw_caption(a, b.head, rows, n, -1, menu_std_width(a), a->tint, NULL, false, 0,
	                  pages > 1 ? count : NULL);
	if (bg) SDL_DestroyTexture(bg);
}

/* --controls-fit [game|home]: every Controls page measured with the font
 * against the panel that draws it - the home's five on both devices, and
 * every system's in a game (an .fds game's too), or one of the two sets. A
 * row wider than the panel is cut or slides; a page taller than it scrolls
 * itself, which is wrong for a table read a button at a time. A
 * "CONTROLS-FIT:" line on stderr for each page that does not fit, and a count
 * of the pages read (tools/controls-shots-check.sh). */
static const char *shot_controls_fit;

static int controls_fit_one(app *a, const char *where, const char *head,
                            const char *count, const menu_row *rows, int n)
{
	int w = menu_std_width(a), mw = menu_measure(rows, n, head), i;
	int rws = 0, rules = 0, notes = 0, room, bad = 0;
	/* the heading is centered with the count's room kept free either side
	 * of it (panel_show) */
	int inset = count ? ui_text_width(ui_font(UI_F_META), count) + 2 * DS_GRID : 0;
	int hw = ui_text_width(ui_font(UI_F_LABEL), head);

	if (hw > w - 2 * inset) {
		fprintf(stderr, "CONTROLS-FIT: %s: heading \"%s\" %d wide, room for %d beside \"%s\"\n",
		        where, head, hw, w - 2 * inset, count ? count : "");
		bad = 1;
	}

	for (i = 0; i < n; i++) {
		if (!rows[i].label) rules++;
		else if (ROW_IS_NOTE(rows[i])) notes++;
		else rws++;
	}
	room = menu_list_fit(0, rules, notes);
	if (mw > w) {
		fprintf(stderr, "CONTROLS-FIT: %s: %d wide, the panel holds %d\n", where, mw, w);
		bad = 1;
	}
	if (rws > room) {
		fprintf(stderr, "CONTROLS-FIT: %s: %d rows and %d notes, room for %d rows\n",
		        where, rws, notes, room);
		bad = 1;
	}
	return bad;
}

static void controls_fit_report(app *a)
{
	static systems_cfg all;
	char path[CFG_STR * 2], where[96], head[80], count[16];
	menu_row rows[CTL_MAX_ROWS];
	bool home = strcmp(shot_controls_fit, "game") != 0;
	bool game = strcmp(shot_controls_fit, "home") != 0;
	int dev, p, i, e, pages = 0, bad = 0;

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &all)) all.count = 0;
	for (dev = 0; dev < 2; dev++) {
		ctl_set_fn_volume(dev == 1);
		ctl_set_battery_lights(dev == 1);
		for (p = 0; home && p < CTL_PAGES; p++) {
			int n = ctl_rows((ctl_page)p, rows);

			snprintf(head, sizeof head, "Controls: %s", ctl_page_name((ctl_page)p));
			snprintf(count, sizeof count, "%d / %d", p + 1, CTL_PAGES);
			snprintf(where, sizeof where, "%s, the home's %s", dev ? "Pixel 2" : "Brick",
			         ctl_page_name((ctl_page)p));
			bad += controls_fit_one(a, where, head, count, rows, n);
			pages++;
		}
		for (i = 0; game && i < all.count; i++) {
			const system_cfg *s = &all.systems[i];

			for (e = 0; e < 2; e++) {
				ctl_game g = { s->tag, s->name, home_label(s->tag, s->name), s->map,
				               plat_turbo_map(s->tag), e ? "fds" : "", ctl_head_fits, a };
				int np = ctl_game_known(s->tag) ? ctl_game_pages(&g) : 0;

				if (e && strcmp(s->tag, "NES")) continue;
				for (p = 0; p < np; p++) {
					ctl_buf b;
					int n = ctl_game_rows(&g, p, rows, &b);

					snprintf(where, sizeof where, "%s, %s%s page %d",
					         dev ? "Pixel 2" : "Brick", s->tag, e ? " (fds)" : "", p);
					snprintf(count, sizeof count, "%d / %d", p + 1, np);
					bad += controls_fit_one(a, where, b.head, np > 1 ? count : NULL, rows, n);
					pages++;
				}
			}
		}
	}
	fprintf(stderr, "controls-fit: %d pages measured, %d do not fit\n", pages, bad);
}

/* --press left|right|a: that key on --menu's row before the menu is drawn,
 * as the player would press it, through the menu's own handler - for a row
 * that changes a setting (Clock, Appearance, Auto Off), not one that opens a
 * screen. The setting is saved as it would be, in the card the shot runs on. */
static int shot_key = -1;   /* not shot_press, which is --shot-at's */
/* --menu's row by its label, found when the menu is built */
static const char *shot_menu_label;

/* The status corner clear of what stands near it on the frame just drawn
 * (DESIGN.md 11.12): drawn when --status gives it something to show, and
 * no clock unless --status gives one; its clock 16 from
 * its marks and its marks at x 1000;
 * the home sprite below it; and a games title's
 * capitals and a panel's top a grid step under its row. The corner's box is
 * its clock's text box and its marks, a little wider than their ink. NULL
 * when it is clear, else what it meets. */
static const char *shot_corner_clear(app *a)
{
	static char why[200];
	const SDL_Rect c = g_corner_box;
	const int under = g_corner_box.y + g_corner_box.h + DS_GRID;
	const int corner_right = g_corner_box.x + g_corner_box.w;
	const int clock_right = g_marks_w ? corner_right - g_marks_w - STATUS_CLOCK_GAP
	                                  : corner_right;
	SDL_Rect card;

	if (g_clock_box.w && !shot_st.set) {
		snprintf(why, sizeof why, "a clock drawn with no --status to give one: "
		         "the host's, which changes from one minute to the next");
		return why;
	}
	if (c.w <= 0) {
		/* --status gave it something to show, and every screen a shot can
		 * draw stands under the corner */
		if (shot_st.set && (shot_st.minutes >= 0 || status_marks_width(&shot_st.m)))
			return "no corner drawn, though --status gave it something to show";
		return NULL;
	}
	if (g_clock_box.w && g_clock_left && g_clock_box.x != HEADER_CLOCK_X) {
		snprintf(why, sizeof why, "the header's clock starts at x %d, not at %d",
		         g_clock_box.x, HEADER_CLOCK_X);
		return why;
	}
	if (g_clock_box.w && !g_clock_left && g_clock_box.x + g_clock_box.w != clock_right) {
		snprintf(why, sizeof why, "the clock ends at x %d, not at %d, %d from its marks",
		         g_clock_box.x + g_clock_box.w, clock_right, STATUS_CLOCK_GAP);
		return why;
	}
#ifdef I18N_PSEUDO
	/* The pseudo-locale is 40% longer on purpose, and meant to overflow */
	if (i18n_lang() != LANG_PSEUDO)
#endif
	if (g_pct_natural > STATUS_PCT_MAX) {
		snprintf(why, sizeof why, "the battery's text is %d units wide, over the %d "
		         "its room holds: it would run into the marks", g_pct_natural, STATUS_PCT_MAX);
		return why;
	}
	if (a->sys.count > 0 && a->screen == SCREEN_SYSTEMS) {
		card = a->home_layout == HOME_GALLERY ? (SDL_Rect){320,128,384,384}
		                                        : (SDL_Rect){536,112,448,448};
		if (SDL_HasIntersection(&c, &card)) {
			snprintf(why, sizeof why, "the corner (x %d..%d) meets the focused card "
			         "(x %d..%d)", c.x, c.x + c.w, card.x, card.x + card.w);
			return why;
		}
	}
	if (g_title_caps.w > 0 && g_title_caps.x < c.x + c.w &&
	    g_title_caps.x + g_title_caps.w > c.x && g_title_caps.y < under) {
		snprintf(why, sizeof why, "the title's capitals start at y %d, under %d",
		         g_title_caps.y, under);
		return why;
	}
	if (g_menu_panel.w > 0 && g_menu_panel.x < c.x + c.w &&
	    g_menu_panel.x + g_menu_panel.w > c.x && g_menu_panel.y < under) {
		snprintf(why, sizeof why, "a panel's top is at y %d, under %d", g_menu_panel.y,
		         under);
		return why;
	}
	return NULL;
}

/* Now Playing without a daemon, for --shot. A cover not yet on the card is
 * drawn as missing rather than asked for: there is nobody to ask. */
static void np_shot(app *a)
{
	mu_now mn = { 0 };
	const ml_album *al;
	char next[128] = "";
	int ar, t;

	if (shot_np < 0 || shot_np >= g_muse.nalbums) return;
	al = &g_muse.albums[shot_np];
	t = shot_np_track < al->n ? shot_np_track : al->n - 1;
	for (ar = 0; ar < g_muse.nartists - 1; ar++)
		if (shot_np < g_muse.artists[ar].first + g_muse.artists[ar].n) break;
	mn.state = shot_np_paused ? MU_PAUSED : MU_PLAYING;
	snprintf(mn.title, sizeof mn.title, "%s", g_muse.tracks[al->first + t].name);
	snprintf(mn.artist, sizeof mn.artist, "%s", g_muse.artists[ar].name);
	snprintf(mn.album, sizeof mn.album, "%s", al->name);
	mn.at = shot_np_at;
	mn.len = 236.0;
	mn.index = t;
	mn.count = al->n;
	if (t + 1 < al->n)
		snprintf(next, sizeof next, "%s", g_muse.tracks[al->first + t + 1].name);

	{
		char base[LIB_PATH * 2], path[LIB_PATH * 2 + 8];

		ml_cover_base(g_muse_root, &g_muse, shot_np, base, sizeof base);
		snprintf(path, sizeof path, "%s.jpg", base);
		if (!file_nonempty(path)) {
			snprintf(path, sizeof path, "%s.png", base);
			if (!file_nonempty(path) && g_cov)
				g_cov[shot_np].st = ml_folder_image(g_muse_root, &g_muse, shot_np,
				                                    path, sizeof path)
				                  ? COV_FOLDER : COV_NONE;
		}
	}
	np_cover(a, shot_np);
	if (shot_video >= 0) {
		g_np_view = shot_video;
		mp4v_test_picture(a->r, 640, 360);
		mn.vw = 640;
		mn.vh = 360;
		np_draw_video(a, &mn, 1);
	} else {
		np_draw(a, &mn, next);
	}
	if (shot_locked) {
		mp4lock l = { 0 };

		mp4lock_engage(&l, 1000, false);
		l.held_from = 1000;
		l.now = 1000 + DS_MS_HOLD_UNLOCK * 3 / 5;
		ui_scrim(a->r, 0);
		lock_panel(a, &l);
	}
}

/* For a shot: every cover not on the card is a cover the music does not
 * carry, since there is no daemon to ask - so those albums draw their
 * generated card rather than an empty slot. */
static void muse_covers_settle(void)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	int i;

	for (i = 0; g_cov && i < g_muse.nalbums; i++) {
		ml_cover_base(g_muse_root, &g_muse, i, base, sizeof base);
		snprintf(p, sizeof p, "%s.jpg", base);
		if (file_nonempty(p)) continue;
		snprintf(p, sizeof p, "%s.png", base);
		if (!file_nonempty(p))
			g_cov[i].st = ml_folder_image(g_muse_root, &g_muse, i, p, sizeof p)
			            ? COV_FOLDER : COV_NONE;
	}
}

/* Everything a shot draws, in the theme that is on. */
static void shot_compose(app *a)
{
	drawn_forget();
	if (shot_keyboard_panel) {
		kb_pose pose = keyboard_panel(&shot_keyboard, KB_PANEL_FRAME, false);

		if (shot_keyboard.closed && !pose.moving) {
			draw_shelf(a);
			zyperos_menu_draw(a, shot_menu_sel);
		} else {
			kb_preview_panel(a->r, shot_kb_title, shot_kb_text,
			                 shot_kb_layer, shot_kb_row, shot_kb_col, pose);
		}
		return;
	}
	if (shot_poweroff) {
		poweroff_frame(a, shot_at > 0 ? (unsigned)shot_at : 0u);
		return;
	}
	muse_covers_settle();
	if (shot_menu && shot_menu_label) {
		menu_row rows[MENU_MAX_ROWS];
		menu_bufs b;
		const char *heading;
		lang was = i18n_lang();
		int n, k;

		/* The name as typed is English's, and the row is the same row in
		 * every language: found in English, drawn in the shot's */
		i18n_set(LANG_EN);
		n = menu_build(a, a->screen, a->sys_cursor, rows, &b, &heading);
		for (k = 0; k < n && !(rows[k].label && !strcmp(rows[k].label, shot_menu_label)); k++) ;
		i18n_set(was);
		if (k == n) {
			fprintf(stderr, "--menu: no row called %s\n", shot_menu_label);
			shot_failed = 1;
		}
		shot_menu_sel = k < n ? k : 0;
		shot_menu_label = NULL;   /* found once, for --flip's second draw too */
	}
	if (shot_menu && shot_key >= 0) {
		sysmenu_ctx c;

		memset(&c, 0, sizeof c);
		c.a = a;
		c.screen = a->screen;
		sysmenu_key(a, &c, (in_button)shot_key, shot_menu_sel);
		shot_key = -1;   /* once: --flip composes the shot more than once */
	}
	draw_shelf(a);
	if (shot_language > -2) {
		static unsigned char snd[LANG_COUNT];
		menu_row rows[LANG_COUNT];
		const char *heading;
		int n = language_build(snd, rows, LANG_COUNT, &heading);

		ui_scrim(a->r, 0);
		menu_draw(a, heading, rows, n, shot_language >= 0 ? shot_language : (int)i18n_lang(),
		          menu_std_width(a), MENU_ZYPEROS);
		return;
	}
	if (shot_update >= 0) {
		su_page p;
		char notes[UP_NOTES][120];
		int nn = up_notes(notes);

		su_page_build(up_view(true), &p);
		update_page_draw(a, &p, notes, nn);
		return;
	}
	if (shot_about >= 0) {
		static about_view v;
		static bool made;

		/* Once: a shot that draws more than one frame keeps the photo */
		if (!made) {
			made = true;
			about_refresh(&v, 2700000u);
			about_pages(a, &v);
			v.width = about_width(a, &v);
			if (shot_about_scroll >= 0) {
				int over = v.nstory - menu_notes_fit();

				v.story_scroll = over > 0 ? (int)(shot_about_scroll * over * menu_note_h()) : 0;
			}
		}
		ui_scrim(a->r, 0);
		about_draw(a, &v, shot_about);
		return;
	}
	/* After --close's press the menu is the shelf's to draw leaving, and
	 * after --swap's it has given way to the page below */
	if (shot_menu) {
		if (shot_pressed && shot_press == SHOT_CLOSE) panel_leave_draw(a, 0);
		else if (!shot_pressed || shot_press != SHOT_SWAP)
			zyperos_menu_draw(a, shot_menu_sel);
	}
	if (shot_slots) shot_draw_slots(a);
	if (shot_gamemenu && a->sys.count > 0) shot_draw_gamemenu(a);
	if (shot_gamecontrols >= 0 && a->sys.count > 0) shot_draw_gamecontrols(a);
	if (shot_controls_fit) controls_fit_report(a);
	/* Over **scrim.menu**, as the screens they picture are: kb_prompt's
	 * backdrop, wait_panel and the achievement list opened over the shelf. */
	if (shot_cheevos && shot_ingame) {
		/* as opened from the in-game menu: over a paused game */
		SDL_Texture *bg = fake_frame(a->r, 4.0f / 3);

		paused_backdrop(a, bg, 1.0f);
		if (bg) SDL_DestroyTexture(bg);
	} else if (shot_kb || shot_wait || shot_cheevos) {
		ui_scrim(a->r, 0);
	}
	if (shot_cheer >= 0) {
		cheer_ctx c;

		zr_init(&c.z, 0, 0);
		zr_works(&c.z, 0);
		c.now = (unsigned)shot_cheer;
		eh_cheer_draw(a->r, games_on_card(a), (unsigned)shot_cheer, cheer_zyper, &c);
	}
	if (shot_kb) {
		if (shot_at >= 0)
			kb_preview_press(a->r, shot_kb_title, shot_kb_text,
			                 shot_kb_layer, shot_kb_row, shot_kb_col, (unsigned)shot_at);
		else kb_preview(a->r, shot_kb_title, shot_kb_text,
		                shot_kb_layer, shot_kb_row, shot_kb_col);
	}
	/* The in-game notice is not drawn by this process - Diatom composites it
	 * over the game - so it cannot be screenshotted like the rest. Rendered
	 * to its wire format and read straight back, which is the same pixels
	 * Diatom will show and therefore the thing worth looking at. */
	/* The achievements list, with sample data. It is only reachable from the
	 * in-game menu, so without this it can only be judged on a device with a
	 * game running - and it took two rounds of that to notice its heading was
	 * clipped at both ends. */
	/* The one-line panel behind "Scanning...", "Connecting...", "Signing
	 * in..." and "Looking up this game...". Five flows use it and none of them
	 * could be rendered. */
	if (shot_wait) {
		menu_row row = { shot_wait_msg ? shot_wait_msg : T(S_LIBRARY_SCANNING), NULL, false };

		menu_message(a, shot_wait, row, MENU_ZYPEROS);
	}
	if (shot_info) info_preview(a, true, shot_info == 2);
	if (shot_delete_ask) delete_ask_preview(a);
	if (shot_controls >= 0 && (shot_press != SHOT_SWAP || shot_pressed)) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80], count[16];
		ctl_page p = (ctl_page)(shot_controls % CTL_PAGES);
		float k = 1.0f;
		int n;

		ctl_set_fn_volume(plat_brightness_on_volume());
		ctl_set_battery_lights(plat_battery_lights());
		n = ctl_rows(p, rows);

		snprintf(head, sizeof head, "Controls: %s", ctl_page_name(p));
		snprintf(count, sizeof count, "%d / %d", (int)p + 1, CTL_PAGES);
		/* --swap's page, arriving over the menu that gave way to it */
		if (shot_press == SHOT_SWAP && shot_lv) panel_frame(a, shot_lv, &k);
		ui_scrim_k(a->r, 0, k);
		menu_draw_caption(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS, NULL, false,
		                  0, count);
	}
	if (shot_sgdb >= 0) {
		static const char *const st[] = { NULL, "works", "refused" };
		menu_row rows[SG_ROWS];
		sg_row ids[SG_ROWS];
		int n = sys_menu_sgdb_rows(shot_sgdb ? "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b"
		                                     : NULL, st[shot_sgdb], rows, ids);

		ui_scrim(a->r, 0);
		menu_draw(a, "SteamGridDB", rows, n, 0, menu_std_width(a), MENU_ZYPEROS);
	}
	if ((shot_scraper >= 0 && shot_scraper <= 2) || shot_scraper == 5) {
		static scraper_bufs b;
		scraper_ui u = { .net = true, .missing = 142, .snaps = "on \xc2\xb7 1.4 GB",
		                 .ss_have = true, .ss_in = true, .ss_name = "tarik",
		                 .sgdb = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b" };
		menu_row rows[SC_ROWS];
		sc_row ids[SC_ROWS];
		const char *head = "Scraper";
		int n;

		if (shot_scraper == 1) {
			u.running = true; u.done = 31; u.total = 142;
			u.ss_found = 12; u.lr_found = 17; u.sg_found = 2;
		} else if (shot_scraper == 5) {
			/* Offline: the first row's value is "needs Wi-Fi", the longest
			 * it has in most languages (check-i18n-shots) */
			u.net = false;
		} else if (shot_scraper == 2) {
			u.ran = true; u.missing = 9;
			u.ss_found = 61; u.lr_found = 58; u.sg_found = 14; u.not_found = 9;
		}
		n = sys_menu_scraper_rows(&u, &b, rows, ids);
		ui_scrim(a->r, 0);
		menu_draw(a, head, rows, n, 0, menu_std_width(a), MENU_ZYPEROS);
	} else if (shot_scraper == 3) {
		static snaps_bufs b;
		snaps_console c[5] = { { "SNES", true, 212000000ull }, { "Game Boy", true, 96000000ull },
		                       { "PlayStation", false, 0 }, { "Genesis", true, 154000000ull },
		                       { "NES", true, 188000000ull } };
		snaps_ui u = { .fetch = true, .play = true, .ss_in = true, .ss_have = true,
		               .sound = SNAP_MUTED,
		               .c = c, .nc = 5, .total = 650000000ull };
		menu_row rows[SN_ROWS];
		sn_row ids[SN_ROWS];
		int con[SN_ROWS], n = sys_menu_snaps_rows(&u, &b, rows, ids, con);

		ui_scrim(a->r, 0);
		menu_draw(a, "Video Snaps", rows, n, 4, menu_std_width(a), MENU_ZYPEROS);
	} else if (shot_scraper == 4) {
		menu_row rows[SSP_ROWS];
		ssp_row ids[SSP_ROWS];
		int n = sys_menu_ss_rows(true, "tarik", "19796 left today", rows, ids);

		ui_scrim(a->r, 0);
		menu_draw(a, "ScreenScraper", rows, n, 0, menu_std_width(a), MENU_ZYPEROS);
	}
	if (shot_np >= 0) np_shot(a);
	if (shot_art)
		art_preview(a, shot_art_now, "4 / 10",
		            "37 found · 2 missing · 61 already", true);
	if (shot_usb >= 0) {
		/* The question a computer gets, at rest, all three tiles */
		menu_row tiles[3] = { { "Charge\nOnly", NULL, true }, { "File\nTransfer", NULL, true },
		                      { "Developer\n(SSH)", NULL, true } };

		ui_scrim(a->r, 0);
		tiles_panel(a, "Computer Connected", tiles, 3, shot_usb % 3, PANEL_REST.panel);
	}
	if (shot_xfer_ask >= 0) {
		/* The question while files move, from the same words xs_ask shows */
		const char *const *l = shot_xfer_game ? XS_GAME_L : XS_STOP_L;
		const char *const *w = shot_xfer_game ? XS_GAME_W : XS_STOP_W;
		int k = shot_xfer_ask % 3;
		menu_row rows[6] = {
			{ l[0], NULL, true }, { l[1], NULL, true }, { l[2], NULL, true },
			MENU_NOTE("Receiving Tobu Tobu Girl.gb"), MENU_NOTE(w[k]),
			MENU_NOTE(shot_xfer_game ? XS_GAME_LEGEND : XS_STOP_LEGEND),
		};

		ui_scrim(a->r, 0);
		tiles_panel(a, "Files are still moving", rows, 6, k, PANEL_REST.panel);
	}
	if (shot_hare)
		hare_preview(a, shot_hare_addr, "4071", shot_hare_who,
		             /* the cable's address is USB File Transfer, with SSH */
		             !strcmp(shot_hare_addr, "10.42.0.1") ? "1 signed in" : NULL,
		             "12 KB / 41 MB", shot_hare_head);
	if (shot_hot)
		hot_preview(a, shot_hot_ssid, shot_hot_pass, shot_hot_phones, shot_hot_now);
	/* The synopsis card, from the card's own games table - so a shot of it is a
	 * picture of real scraped text rather than a fixture. */
	if (shot_syn) {
		sysview *v = &a->view[a->sys_cursor];
		const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)];
		game_meta m;

		if (v->list.count > 0 &&
		    db_game_get(db_lib(), s->folder, v->list.items[v->cursor].file, &m) &&
		    m.synopsis[0]) {
			char lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
			menu_row rows[SYN_ROWS];
			int fixed = menu_std_width(a);
			int loop_at = 0;
			int nl = syn_layout(m.synopsis, fixed, lines, rows, &loop_at);

			ui_scrim(a->r, 0);
			menu_draw_ex(a, v->list.items[v->cursor].title, rows, nl, -1, fixed,
			             s->accent, NULL, false, loop_at);
		}
	}
	if (shot_cheevos) {
		/* Real titles, and the long ones are the point: a set's names run past
		 * the panel often, and this shot drew eight short ones - so the screen
		 * could be looked at without a device and still not show the thing
		 * worth looking at. Two of the three below are real RetroAchievements
		 * titles from sets on this card. */
		static const struct { const char *t; int p; bool got; } sample[] = {
			{ "The Path to Disaster", 5, true },
			{ "Hold On To Your Potatoes, We're In For A Bumpy Ride", 10, true },
			{ "Violated Heavens", 10, true },
			{ "I Am Not Left Handed Either, But I Can Still Beat You", 25, false },
			{ "Koma Faction's Fall", 25, false },
			{ "Not So Disaster", 10, false },
			{ "Storming The Fortress", 10, false },
			{ "Blazing through the Skies", 10, false },
		};
		int cn = (int)(sizeof sample / sizeof sample[0]), ci;
		menu_row rows[8];
		unsigned vcols[8] = { 0 };
		char vals[8][16], head[192];

		for (ci = 0; ci < cn; ci++) {
			snprintf(vals[ci], sizeof vals[ci], "%d", sample[ci].p);
			rows[ci] = (menu_row){ sample[ci].t, vals[ci], sample[ci].got };
			vcols[ci] = sample[ci].got ? VCOL_EARNED : 0u;
		}
		snprintf(head, sizeof head, T(S_CHEEVOLIST_HEADING),
		         shot_cheevos_game, 3, 36, 25, 415);
		/* Drawn the way cheevos_screen draws it - menu_draw_ex, the earned
		 * colors, the shared width - because a shot that composes the panel
		 * differently from the screen is a picture of something that does not
		 * exist. It used to call menu_draw with a width of 0. */
		if (shot_badge_set) {
			for (ci = 0; ci < cn; ci++) shot_badge_got[ci] = sample[ci].got;
			g_badge_r = a->r;
			g_row_icon = shot_row_badge;
		}
		menu_draw_ex(a, head, rows, cn, shot_cheevos_sel, menu_std_width(a),
		             a->tint, vcols, true, 0);
		g_row_icon = NULL;
	}
	if (shot_notice) {
		char dt[512];
		FILE *nf;

		snprintf(dt, sizeof dt, "%s.dtov", shot_path);
		if ((shot_notice_mark
		     ? notice_render_mark(shot_notice_head, shot_notice, (notice_mark)shot_notice_mark,
		                          dt, theme_index())
		     : notice_render_ex(shot_notice_head, shot_notice, shot_notice_badge,
		                        shot_notice_value, shot_notice_target, dt, theme_index())) &&
		    (nf = fopen(dt, "rb"))) {
			unsigned char hd[8];
			if (fread(hd, 1, 8, nf) == 8 && !memcmp(hd, "DTOV", 4)) {
				int nw = hd[4] | (hd[5] << 8), nh = hd[6] | (hd[7] << 8);
				SDL_Surface *ns = SDL_CreateRGBSurfaceWithFormat(
					0, nw, nh, 32, SDL_PIXELFORMAT_ARGB8888);
				if (ns && fread(ns->pixels, 4, (size_t)nw * nh, nf)
				          == (size_t)nw * nh) {
					SDL_Rect at = { (ZYPEROS_SCREEN_W - nw) / 2,
					                ZYPEROS_SCREEN_H - nh - ZYPEROS_SCREEN_H / 24,
					                nw, nh };
					SDL_Texture *nt = SDL_CreateTextureFromSurface(a->r, ns);
					if (nt) {
						SDL_SetTextureBlendMode(nt, SDL_BLENDMODE_BLEND);
						SDL_RenderCopy(a->r, nt, NULL, &at);
						SDL_DestroyTexture(nt);
					}
				}
				if (ns) SDL_FreeSurface(ns);
			}
			fclose(nf);
			remove(dt);
		}
	}
	/* The song pop-up, on a clock of its own so the frame is the same each
	 * time: shown at T0, drawn --songpop's MS into it */
	if (shot_songpop) {
		const unsigned T0 = 100000;
		const char *bar = strchr(shot_songpop, '|');
		char head[BGM_TAG_MAX], body[BGM_TAG_MAX];

		snprintf(head, sizeof head, "%.*s", bar ? (int)(bar - shot_songpop) : 0, shot_songpop);
		snprintf(body, sizeof body, "%s", bar ? bar + 1 : shot_songpop);
		zr_init(&g_songpop_zyper, T0, 1);
		if (!head[0]) snprintf(head, sizeof head, "%s", BGM_SONG_HEAD_NONE);
		songpop_show(&g_songpop, head, body, song_hold(head, body), T0);
		zr_works(&g_songpop_zyper, T0);
		song_popup_draw(a, T0 + (unsigned)shot_songpop_ms);
	}
	if (shot_heatpop >= 0) {
		const unsigned T0 = 100000;

		heatpop_show(T0);
		zr_init(&g_heatpop.rig, T0, 1);
		zr_works(&g_heatpop.rig, T0 + DS_MS_MOVE);
		heatpop_draw(a, T0 + (unsigned)shot_heatpop);
	}
	/* The volume or brightness bar over whatever else was asked for, held
	 * until it has slid all the way in. */
	if (shot_osd_kind) {
		plat_osd_show(shot_osd_kind, shot_osd_val,
		              shot_osd_kind == 1 ? PLAT_BRIGHT_MAX : PLAT_VOL_MAX);
		SDL_Delay(OSDBAR_IN_MS + 10);
		plat_draw_osd(a->r);
	}
}

/* The row --shot-at's plate leaves for --menu's: the nearest row above it
 * the cursor can rest on, or below it when there is none above. */
static int shot_menu_from(app *a, int sel)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading), i;

	for (i = (sel < n ? sel : n) - 1; i >= 0; i--)
		if (rows[i].label && rows[i].live) return i;
	for (i = sel + 1; i < n; i++)
		if (rows[i].label && rows[i].live) return i;
	return sel;
}

/* The row of the top menu called `label` in English, or -1: the same row in
 * every language, found in English as --menu's is */
static int shot_menu_row(app *a, const char *label)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	lang was = i18n_lang();
	int n, i;

	i18n_set(LANG_EN);
	n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading);
	for (i = 0; i < n; i++)
		if (rows[i].label && !strcmp(rows[i].label, label)) break;
	i18n_set(was);
	return i < n ? i : -1;
}

/* --shot-at: the press, and the frames after it up to the shot's time.
 *
 * The clock is held at the press and moved on a frame at a time, 60 a second
 * as on the device, each frame drawn and thrown away, and then to the shot's
 * time exactly, which take_shot draws. The tweens would land in the same
 * place from the press alone; the frames are for what follows from one frame
 * to the next - the tint, which steps by the time since the last frame, and
 * the static state the menu keeps between draws. */
static void shot_motion(app *a)
{
	unsigned t0 = plat_now_ms(), t;

	g_clock_held = true;
	g_clock_at = t0;
	shot_t0 = t0;
	cf_clock(anim_ticks);
	/* The tint's last step is now, or the first frame after the press would
	 * step it by however long the shot took to get here, and the shot would
	 * be a picture of the setup's speed. */
	last_tint_ms = t0;
	if (shot_kb && shot_press != SHOT_STEP) {
		/* The same bridge as the real loop: --open from the shelf,
		 * --swap from its menu, --close back to that menu. */
		shot_keyboard = (keyboard_transition){ .a = a };
		if (shot_press != SHOT_OPEN) {
			shot_lv = panel_open();
			mm_press(shot_lv);
			draw_shelf(a);
			zyperos_menu_draw(a, shot_menu_sel);
		}
		keyboard_panel(&shot_keyboard, KB_PANEL_OPEN, false);
		if (shot_press == SHOT_CLOSE) {
			keyboard_panel(&shot_keyboard, KB_PANEL_FRAME, true);
			keyboard_panel(&shot_keyboard, KB_PANEL_LEAVE, false);
		}
		shot_keyboard_panel = true;
	} else if (shot_menu && shot_press != SHOT_STEP) {
		/* Opened on the shelf, as MENU opens it, by panel_open as menu_run
		 * does: --open's press. The others' menu is at rest when theirs
		 * comes - B, whose panel the shelf then draws leaving, or A on its
		 * Controls row, whose page opens over it. */
		shot_lv = panel_open();
		if (shot_press != SHOT_OPEN) {
			int c = shot_press == SHOT_SWAP ? shot_menu_row(a, "Controls") : -1;

			if (c >= 0) shot_menu_sel = c;
			mm_press(shot_lv);
			shot_compose(a);
			if (shot_press == SHOT_CLOSE) panel_close(a, shot_lv, false);
			else shot_lv = panel_open();
		}
		shot_pressed = true;
	} else if (shot_menu) {
		int to = shot_menu_sel;

		shot_menu_sel = shot_menu_from(a, to);
		shot_compose(a);                    /* at rest on the row above */
		shot_menu_sel = to;                 /* the press, at the next draw */
	} else if (a->screen == SCREEN_GAMES) {
		sysview *v = &a->view[a->sys_cursor];

		shot_compose(a);
		if (v->list.count > 1) {
			v->cursor = (v->cursor + 1) % v->list.count;
			cf_set_cursor_dir(&v->cf, v->cursor, v->list.count, +1);
		}
	} else {
		shot_compose(a);
		if (a->sys.count > 1) {
			a->sys_cursor = (a->sys_cursor + 1) % a->sys.count;
			home_select(&a->home, a->sys_cursor, a->sys.count, +1, anim_now());
		}
	}
	for (t = 0; t < (unsigned)shot_at; t += 1000 / 60) {
		g_clock_at = t0 + t;
		tick_tint(a);
		shot_compose(a);
	}
	g_clock_at = t0 + (unsigned)shot_at;
	tick_tint(a);
}

/* --launch: the zoom's frame at --shot-at's time, as anim_launch draws it.
 * It asks for the card itself, after draining whatever has landed, as
 * anim_launch does, rather than counting on the prime_window the shot runs
 * for the games screen alone: a shot has no worker, so the ask reads the
 * card on the frame, from either screen. */
static void shot_launch_frame(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	SDL_Rect from;
	int tw = 0, th = 0;
	SDL_Texture *card;
	float k = shot_at > 0 ? (float)shot_at / DS_MS_LAUNCH : 0.0f;

	texload_drain(a);
	card = v->list.count ? game_get_tex(a, v->cursor, &tw, &th, NULL) : NULL;
	cf_focus_rect(&CF_LAYOUT_GAMES, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, &from);
	launch_frame(a, card, &from, k > 1.0f ? 1.0f : k);
}

/* --flip's turn: the whole shot drawn once in the theme it started in, then
 * Auto set a minute before the edge that ends that theme - 7:00 for Salt &
 * Pepper, 19:00 for Beard, today, where the same theme is still on - and its
 * clock read at the edge itself. theme_tick says the theme changed and
 * theme_changed is told, which is tick_theme's body with the clock's time
 * handed in: the path 7:00 and 19:00 take on a handheld, and not one written
 * for the shot.
 *
 * Then the frame right after the turn, as the device draws it: there every
 * card is asked of a worker and nothing is read on the frame, so a card the
 * turn took away is a hole in the row until a worker hands its replacement
 * over. A shot has no worker and reads every card on the frame, so this one
 * frame is drawn with the reads held (g_loads_held), and a slot in view with
 * no card in it fails the shot (BLANK): on the systems row, and on a shelf of
 * games, where the row is what was drawn - not under Now Playing, which
 * covers it, and not on Muse's shelf, whose cards are made on the frame. The
 * shot itself is the frame after, every card read in the theme that is on. */
static void shot_flip_theme(app *a)
{
	time_t now = time(NULL), edge;
	struct tm tm;
	sysview *v = &a->view[a->sys_cursor];
	int n, i, k;

	shot_compose(a);
	localtime_r(&now, &tm);
	tm.tm_hour = theme_index() == DS_DARK ? 7 : 19;
	tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;
	edge = mktime(&tm);
	theme_init_at(APPEAR_AUTO, edge - 60);
	if (theme_tick_at(edge)) theme_changed(a);
	else fprintf(stderr, "--flip: the theme did not turn at %02d:00\n", tm.tm_hour);

	g_loads_held = true;
	shot_compose(a);
	g_loads_held = false;
	/* The games in view: the cursor's and CF_HALF_WINDOW either side,
	 * wrapping as prime_around does. Every one was read before the turn, by
	 * prime_window at the start and the frame drawn then, so an empty one is
	 * one the turn emptied. */
	if (a->screen != SCREEN_GAMES || shot_np >= 0 || v->list.count <= 0 ||
	    is_muse(&a->sys.systems[a->sys_cursor]))
		return;
	for (k = -CF_HALF_WINDOW; k <= CF_HALF_WINDOW; k++) {
		n = v->list.count;
		i = n >= 2 ? ((v->cursor + k) % n + n) % n : 0;
		if (!v->tex[i]) {
			fprintf(stderr, "  BLANK: %s had no card on the frame after the "
			        "turn, so the shelf had a hole until its card came back\n",
			        v->list.items[i].title);
			shot_failed = 1;
		}
	}
}

/* Every card in a slot has a foot: the fraction of its height its pixels
 * reach (content_bottom), which is where cf_draw ends the lift under it and
 * starts its reflection. A card in a slot with a foot of 0 is drawn with a
 * shadow, a glow and a reflection 0 units high at its top - a thin line above
 * a card on Beard and nothing at all on Salt & Pepper - which is what every
 * system with no card file was drawn with until its generated card was given
 * one. The name of the first card without one, or NULL. */
static const char *shot_cards_have_feet(const app *a)
{
	for (int i = 0; i < a->sys.count; i++) {
		const sysview *v = &a->view[i];

		for (int k = 0; v->tex && v->cb && k < v->list.count; k++)
			if (v->tex[k] && !(v->cb[k] > 0.0f)) return v->list.items[k].title;
	}
	return NULL;
}

/* --portload: the game by the title the shelf shows or its file's name, on
 * a port's or a "run:" system's shelf before any other, drawn as
 * launch_port draws it and written to the shot. Nothing is started and
 * nothing is written for splash. The shot fails (exit 3) when the screen
 * says it fell short - a line or the band under its contrast, or the words
 * or the cover in Zyper's frame - and when there is no such game. */
static void shot_portload_take(app *a)
{
	SDL_Surface *out;
	int pass, si, gi = -1;

	for (pass = 0; pass < 2 && gi < 0; pass++)
		for (si = 0; si < a->sys.count && gi < 0; si++) {
			const sysview *v = &a->view[si];
			int k;

			if (pass == 0 && !is_port(&a->sys.systems[si])) continue;
			for (k = 0; k < v->list.count; k++)
				if (!strcmp(v->list.items[k].title, shot_portload) ||
				    !strcmp(v->list.items[k].name, shot_portload)) {
					a->sys_cursor = si;
					gi = k;
					break;
				}
		}
	if (gi < 0) {
		fprintf(stderr, "--portload: no game called %s\n", shot_portload);
		shot_failed = 1;
		return;
	}
	a->screen = SCREEN_GAMES;
	a->view[a->sys_cursor].cursor = gi;
	/* The screen's own checks: everything at its contrast, and nothing in
	 * Zyper's frame */
	if (!port_loading_draw(a, &a->sys.systems[shelf_owner(a, a->sys_cursor, gi)], gi))
		shot_failed = 1;
	out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, 32,
	                                     SDL_PIXELFORMAT_RGBA32);
	if (!out) return;
	SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, out->pixels, out->pitch);
	plat_present();
	IMG_SavePNG(out, shot_path);
	SDL_FreeSurface(out);
	fprintf(stderr, "wrote %s  [portload] %s\n", shot_path,
	        a->view[a->sys_cursor].list.items[gi].title);
}

/* --tour's frame, at a clock fixed for the shot, so the same arguments draw
 * the same picture: the loops, Zyper's breath and the confetti are all read
 * from it. */
static void shot_tour_draw(app *a)
{
	const uint32_t now = 1000000u;
	const tour_hooks hk = { tour_zyper_draw, tour_heart_draw, tour_poweroff_draw, a, P_ROOT };
	tour t;
	int at[TOUR_CHOICES];

	if (shot_tour < 0) {
		const float ms = (float)shot_tour_ms;

		draw_shelf(a);
		tour_message(a->r, T(S_TOURRUN_SKIP_MESSAGE),
		             tour_ease(TC_OUT, (uint32_t)shot_tour_ms, 0, DS_MS_MOVE)
		             * (1.0f - fminf(1.0f, fmaxf(0.0f, (ms - DS_MS_HOLD_SHORT) / DS_MS_FADE_OUT))));
	} else {
		tour_pose(&t, (tour_step)shot_tour, now, shot_tour_ms, shot_tour_done,
		          shot_fn_volume || plat_brightness_on_volume());
		tour_lang_from(&t, (int)i18n_lang());
		tour_set_music(&t, shot_tour_music);
		tour_settings(a, at);
		tour_choices_from(&t, at);
		if (shot_tour_pick >= 0)
			tour_pose_pick(&t, shot_tour_pick, now, shot_tour_pick_ms);
		/* The language step is drawn in the language under its cursor */
		if (t.step == TOUR_LANG) {
			const lang f = (lang)tour_lang_focus(&t, now);

			if (f != i18n_lang()) language_apply(a, f, false);
		}
		if (shot_tour_done >= 0 && tour_choice_of(t.step) >= 0)
			t.kept[tour_choice_of(t.step)] = t.pick[tour_choice_of(t.step)];
		if (shot_tour_wrong >= 0)
			tour_pose_wrong(&t, (tour_key)shot_tour_wrong, now, shot_tour_wrong_ms);
		tour_draw(a->r, &t, now, &hk);
	}
	if (shot_osd_kind) {
		plat_osd_show(shot_osd_kind, shot_osd_val,
		              shot_osd_kind == 1 ? PLAT_BRIGHT_MAX : PLAT_VOL_MAX);
		SDL_Delay(OSDBAR_IN_MS + 10);
		plat_draw_osd(a->r);
	}
	tour_draw_free();
}

/* --charging's frame, on a clock of its own, the estimate settled */
static void shot_charging_draw(app *a)
{
	const unsigned T0 = 100000;
	unsigned now = T0 + (unsigned)shot_charging;

	memset(&g_chg, 0, sizeof g_chg);
	g_chg.shown_at = T0;
	ch_screen_start(&g_chg.s, T0);
	/* kept: held at an 85% limit; limit: charging toward it; keptfull: held,
	 * but charged full before the limit was set */
	ch_fake_parse(shot_charging_kind == 5 ? "85 -5 4100 wall 1 1"
	              : shot_charging_kind == 7 ? "100 -5 4290 wall 1 1" : "62 1480 4120 wall 1", &g_chg.r);
	if (shot_charging_kind >= 5) g_chg.r.limit = 85;
	/* pending: the PHY has not named the charger yet */
	if (shot_charging_kind == 4) g_chg.r.kind = CH_PENDING;
	g_chg.valid = true;
	g_chg.eta = (ch_eta){ true, T0 - 60000, T0, 1480.0 };
	ch_words(&g_chg.r, ch_eta_minutes(&g_chg.eta, T0, &g_chg.r), false, g_chg.speed,
	         sizeof g_chg.speed, g_chg.when, sizeof g_chg.when);
	if (shot_charging_kind == 1) {
		ch_screen_tick(&g_chg.s, now - DS_MS_HOLD_UNLOCK / 2, true, true, true);
		ch_screen_tick(&g_chg.s, now, false, true, true);
	} else if (shot_charging_kind == 2)
		ch_screen_tick(&g_chg.s, now, false, false, false);
	if (shot_charging_kind == 3) {
		/* the cable out, its hold, then the send-off */
		unsigned t = (unsigned)shot_charging > POWEROFF_DIM ? POWEROFF_DIM : (unsigned)shot_charging;

		ch_screen_tick(&g_chg.s, T0 + 500, false, false, false);
		ch_screen_tick(&g_chg.s, T0 + 500 + DS_MS_HOLD_SHORT, false, false, false);
		chg_draw(a, T0 + 1000, ds_out((float)t / POWEROFF_DIM), 1.0f - (float)t / POWEROFF_DIM);
		return;
	}
	chg_draw(a, now, 0.0f, 1.0f);
}

/* --lowbatt's frame, on a clock of its own so it is the same each time */
static void shot_lowbatt_draw(app *a)
{
	const unsigned T0 = 100000, PLUG = 2400;
	unsigned now = T0 + (unsigned)shot_lowbatt;
	float bar_k = 1.0f;

	g_lb.pct = 2;
	lowbatt_start(&g_lb.l, T0);
	zr_init(&g_lb.rig, T0, 1);
	zr_fails(&g_lb.rig, T0);
	if (shot_lowbatt_kind == 2) {
		/* the send-off, from the end of the hold */
		now = T0 + DS_MS_HOLD_LOW_BATTERY;
		zr_fails(&g_lb.rig, now - DS_MS_HOLD_ZYPER_FAIL / 2);
		zr_tick(&g_lb.rig, now);
		g_lb.pose = zr_pose_at(&g_lb.rig, now);
		lowbatt_off_frame(a, &g_lb.pose, (unsigned)shot_lowbatt, now);
		return;
	}
	if (shot_lowbatt_kind == 1 && shot_lowbatt >= (int)PLUG) {
		lowbatt_tick(&g_lb.l, T0 + PLUG, false, true, NULL);
		zr_fails(&g_lb.rig, T0 + PLUG - DS_MS_HOLD_ZYPER_FAIL / 2);
		zr_press(&g_lb.rig, T0 + PLUG);
		zr_works(&g_lb.rig, T0 + PLUG);
		bar_k = now - (T0 + PLUG) >= DS_MS_FADE_OUT ? 0.0f
		      : 1.0f - (float)(now - (T0 + PLUG)) / DS_MS_FADE_OUT;
	} else if (shot_lowbatt > (int)DS_MS_HOLD_ZYPER_FAIL / 2) {
		zr_fails(&g_lb.rig, now - DS_MS_HOLD_ZYPER_FAIL / 2);
	}
	zr_tick(&g_lb.rig, now);
	g_lb.pose = zr_pose_at(&g_lb.rig, now);
	lowbatt_draw(a, now, &g_lb.pose, 0.0f, 1.0f, bar_k);
}

static void take_shot(app *a)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W,
	                                                  ZYPEROS_SCREEN_H, 32,
	                                                  SDL_PIXELFORMAT_RGBA32);

	if (shot_launch) {
		shot_launch_frame(a);
	} else if (shot_lowbatt >= 0) {
		shot_lowbatt_draw(a);
	} else if (shot_charging >= 0) {
		shot_charging_draw(a);
	} else if (shot_tour > -2) {
		shot_tour_draw(a);
	} else if (shot_focus_from >= 0) {
		int at = g_home_phase;

		g_home_phase = shot_focus_from;
		shot_compose(a);
		g_home_phase = at;
		if (!focus_draw(a)) {
			fprintf(stderr, "focus: not for this sprite, a whole frame\n");
			shot_compose(a);
		}
	} else {
		if (shot_flip) shot_flip_theme(a);
		shot_compose(a);
	}
	if (out) {
		/* Read BEFORE presenting: the backbuffer is invalid afterwards. */
		SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32,
		                     out->pixels, out->pitch);
		plat_present();
		IMG_SavePNG(out, shot_path);
		SDL_FreeSurface(out);
		/* Say what was drawn, not just that something was: a tool whose whole
		 * job is rendering one state is far more useful when the state it
		 * chose is in the output beside the filename. */
		{
			sysview *v = &a->view[a->sys_cursor];
			const char *what = a->screen == SCREEN_GAMES && v->list.count > 0
			                 ? v->list.items[v->cursor].title
			                 : a->sys.systems[a->sys_cursor].name;
			/* Checked rather than assumed. This line once named a focused
			 * game on a shot with no cards in it. A card drawn on the frame
			 * always ends up with a texture - box art, a preview, or the
			 * generated card - so one without means the picture is not what
			 * the name beside it claims, and that is worth saying loudly. */
			const char *note =
				a->screen == SCREEN_GAMES && v->list.count > 0 &&
				v->tex && !v->tex[v->cursor]
				? "  (NO ART: the focused card had no texture on this frame)" : "";
			const char *footless = shot_cards_have_feet(a);

			if (shot_tour > -2)
				fprintf(stderr, "wrote %s  [tour] %s\n", shot_path,
				        shot_tour < 0 ? "skip" : TOUR_STEP_NAMES[shot_tour]);
			else
				fprintf(stderr, "wrote %s  [%s] %s%s\n", shot_path,
				        a->screen == SCREEN_GAMES ? "games" : "systems", what, note);
			if (footless) {
				fprintf(stderr, "  NO FOOT: %s's card has a content bottom of 0, so "
				        "nothing is lifted or reflected under it\n", footless);
				shot_failed = 1;
			}
			{
				const char *crowded = shot_corner_clear(a);

				if (crowded) {
					fprintf(stderr, "  CORNER: %s\n", crowded);
					shot_failed = 1;
				}
			}
		}
	}
}

/* The start's scan off the main thread (main says why): from the saved
 * library when there is one (src/savedlib.h), except for a shot, which shows
 * the card it is given */
static int scan_thread(void *ctx)
{
	scan_all_start(ctx, !shot_path);
	t_mark("library scan");
	return 0;
}

int main(int argc, char *argv[])
{
	app a = { 0 };
	char path[CFG_STR * 2];
	char startup[CFG_STR];

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
		/* --slots <sel> [aspect] draws one frame of the save/load carousel
		 * over synthetic frames, which is the only way to look at it without
		 * a game running on a device. */
		/* --jump N applies N letter-jumps before the shot (negative for up),
		 * so the d-pad's behavior on a real library can be checked without a
		 * device or a hand on it. */
		else if (!strcmp(argv[i], "--jump") && i + 1 < argc) {
			shot_jump = atoi(argv[++i]);
		}
		/* --keyboard [layer] [text] draws one frame of the text-entry panel,
		 * for the same reason --menu and --slots exist: it is dense, and
		 * laying it out against a screenshot beats a round trip to a device. */
		else if (!strcmp(argv[i], "--cheevos-screen")) {
			shot_cheevos = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_cheevos_sel = atoi(argv[++i]);
			/* A game name, so the heading can be looked at with one of the
			 * long ones rather than only the short one that fit. */
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_cheevos_game = argv[++i];
		}
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
		else if (!strcmp(argv[i], "--locked")) shot_locked = 1;
		else if (!strcmp(argv[i], "--video")) {
			shot_video = NPV_FULL;
			if (i + 1 < argc && !strcmp(argv[i + 1], "controls")) { shot_video = NPV_CONTROLS; i++; }
			else if (i + 1 < argc && !strcmp(argv[i + 1], "full")) i++;
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
		else if (!strcmp(argv[i], "--delete-ask")) shot_delete_ask = 1;
		/* The game under the cursor deleted as Delete Game's yes deletes it,
		 * then the shot: the shelf after, and what went on stderr */
		else if (!strcmp(argv[i], "--delete-now")) shot_delete_now = 1;
		/* --osd volume|brightness LEVEL: the settings bar at that level, on
		 * top of the rest of the shot. */
		else if (!strcmp(argv[i], "--osd") && i + 2 < argc) {
			shot_osd_kind = !strcmp(argv[i + 1], "brightness") ? 1 : 2;
			shot_osd_val = atoi(argv[i + 2]);
			i += 2;
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
				fprintf(stderr, "--home needs spotlight or gallery\n"); return 2;
			}
			const char *value = argv[++i];
			if (strcmp(value, "spotlight") && strcmp(value, "gallery")) {
				fprintf(stderr, "--home needs spotlight or gallery\n"); return 2;
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
		 * for the setting itself. */
		else if (!strcmp(argv[i], "--status") && i + 1 < argc) {
			if (!shot_status_parse(argv[++i])) {
				fprintf(stderr, "--status: cannot read \"%s\"\n", argv[i]);
				return 2;
			}
		}
		/* --gamemenu [row] [dual]: the menu over a paused game, that row
		 * selected, with a Nintendo DS game's three screen rows when dual */
		else if (!strcmp(argv[i], "--ingame")) shot_ingame = 1;
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
			}
		}
		else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
			i++;
			shot_key = !strcmp(argv[i], "left")  ? IN_LEFT
			           : !strcmp(argv[i], "right") ? IN_RIGHT
			           : !strcmp(argv[i], "a")     ? IN_ACCEPT : -1;
			if (shot_key < 0) {
				fprintf(stderr, "--press: %s is not left, right or a\n", argv[i]);
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
		else if (!strcmp(argv[i], "--tour-pick") && i + 1 < argc) {
			shot_tour_pick = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_tour_pick_ms = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--tour-music")) shot_tour_music = true;
		else if (!strcmp(argv[i], "--portload") && i + 1 < argc)
			shot_portload = argv[++i];
		else if (!strcmp(argv[i], "--update")) {
			shot_update = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_update = atoi(argv[++i]);
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
		/* --snap-wait F: the games shelf with the bar under its focused
		 * cover F of the way through the wait for its snap, 0 to 1 */
		else if (!strcmp(argv[i], "--snap-wait") && i + 1 < argc) {
			g_shot_snap_wait = (float)atof(argv[++i]);
			if (g_shot_snap_wait < 0) g_shot_snap_wait = 0;
			if (g_shot_snap_wait > 1) g_shot_snap_wait = 1;
		}
		else if (!strcmp(argv[i], "--scraper")) {
			shot_scraper = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '5')
				shot_scraper = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--sgdb")) {
			shot_sgdb = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '2')
				shot_sgdb = atoi(argv[++i]);
		}
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
		else if (!strcmp(argv[i], "--art")) {
			shot_art = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_art_now = argv[++i];
		}
		else if (!strcmp(argv[i], "--usb-ask")) {
			shot_usb = 0;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_usb = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--xfer-ask")) {
			shot_xfer_ask = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_xfer_ask = atoi(argv[++i]);
			if (i + 1 < argc && !strcmp(argv[i + 1], "game")) { shot_xfer_game = true; i++; }
		}
		else if (!strcmp(argv[i], "--hotspot")) {
			shot_hot = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_ssid   = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_pass   = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_phones = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hot_now    = argv[++i];
		}
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
		else if (!strcmp(argv[i], "--notice-heat")) shot_notice_mark = NOTICE_MARK_HEAT;
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
			if (db_game_set(db_lib(), fld[0], fld[1], &m)) wrote++;
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
				if (!strcmp(all.systems[i].folder, only_sys)) {
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
		/* Rewritten at every boot, not only on change: it is derived, so a
		 * card that lost it or never had one gets a correct one for free. */
		db_write_boot_env();
		/* Developer Mode's file for plastron's S40usbgadget, the same way */
		if (!shot_path) usblink_sync(db_dev(), P_USERDATA);
	}
	/* Local time is the card's chosen zone, in this process, from here on:
	 * the Brick's launch.sh points the system at it too, but the Pixel 2's
	 * system has no other way to know it */
	{
		char tz[64];

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

	/* Before the scan, because the scan builds the Favorites shelf out of
	 * them and a shelf cannot be built from a list that has not been read. */
	fav_load();

	/* Close any session the device never saw end - a power cut, a flat
	 * battery - at its last checkpoint, before anything reads a total. See
	 * stats.h: short by up to the checkpoint interval beats lost entirely. */
	stats_recover();

	{	char cp[CFG_STR * 2];
		chv_earned_load();
		plat_resident_on_unlock(on_cheevo_unlocked);
		plat_resident_on_progress(on_cheevo_progress);
		plat_resident_on_speed(on_speed);
		plat_resident_on_tick(on_game_tick);
		plat_resident_on_battery_empty(battery_empty);
		heat_init(&g_heat);

		/* Without this every HTTPS request fails verification, because the
		 * device has no trust store of its own - res/ssl/README.md. */
		a.auto_off = auto_off_load();
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
		{
			char bin[CFG_STR * 2], music[CFG_STR * 2];

			snprintf(bin, sizeof bin, "%s/muse", P_ROOT);
			snprintf(music, sizeof music, "%s", P_CARD);
			/* A shot starts no daemon: it draws one frame and exits, and a
			 * cover it would ask for is drawn as missing instead - see
			 * muse_covers_settle. */
			musec_init(shot_path ? "" : bin, music);
			musec_on_before_heard(aout_before_muse);
			musec_names_by(muse_track_names);
		}
		/* The play mode the player left it in. */
		{
			char m[32];
			int k;

			db_get_str(db_dev(), "muse.mode", m, sizeof m, "in order");
			if (shot_mode) snprintf(m, sizeof m, "%s", shot_mode);
			for (k = 0; k < MUQ_MODES; k++)
				if (!strcmp(m, MUSE_MODES[k].key)) musec_set_mode((muq_mode)k);
		}
	}
	t_mark("muse+accounts");

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
		theme_init(shot_path && shot_theme >= 0 ? (appearance)shot_theme
		                                        : appearance_parse(ap));
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
	if (!shot_path) texload_start();

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
		char v[16], track[256], dir[CFG_STR * 2], builtin[CFG_STR * 2];

		db_get_str(db_dev(), "bgm", v, sizeof v, "on");
		bgm_set_on(bgm_on_parse(v));
		db_get_str(db_dev(), "bgm.volume", v, sizeof v, "");
		bgm_set_volume(bgm_volume_parse(v));
		db_get_str(db_dev(), "bgm.track", track, sizeof track, "");
		bgm_set_track(track);
		db_get_str(db_dev(), "bgm.popup", v, sizeof v, "on");
		g_song_popup = bgm_on_parse(v);
		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, BGM_DIR);
		snprintf(builtin, sizeof builtin, "%s/%s", P_ROOT, BGM_BUILTIN_DIR);
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
	t_mark("renderer");
	plat_input_init();
	t_mark("input");
	/* Nothing is handed in any more. The two-tier lookup this replaces - a
	 * shipped default and the player's saved level - is one key each in the
	 * database, seeded once and overwritten by a nudge. That is also the end
	 * of a bug it kept reintroducing: reapplying the config afterwards put
	 * the shipped default ahead of the level the player last chose. */
	plat_settings_init();
	t_mark("settings");

	/* Before ui_init, which is where the sizes are decided; it persists across
	 * the ui_quit/ui_init pair the standalone-emulator fallback goes through. */
	/* The size the player chose beats the shipped default, the same way a
	 * saved brightness does. Read before ui_init, which is when the scale is
	 * applied. */
	if (!ui_init(a.r, P_FONT)) fprintf(stderr, "font init failed\n");
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
	core_load(&a);
	sort_load(&a);
	sort_all(&a);
	t_mark("scan");

	/* MENU > Clock, which the corner's first frame is drawn in */
	clock24_load();
	batt_icon_load();
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
	if (!access(PORT_RETURN, F_OK)) {
		unlink(PORT_RETURN);
		if (!a.resume_menu && a.view[a.sys_cursor].list.count > 0) {
			a.screen = SCREEN_GAMES;
			cf_reset(&a.view[a.sys_cursor].cf, a.view[a.sys_cursor].cursor);
		games_reset(&a.view[a.sys_cursor].row, a.view[a.sys_cursor].cursor, a.view[a.sys_cursor].list.count, plat_now_ms());
		}
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

	/* Straight back into the game, before the shelf is ever drawn. After the
	 * input grace above, so a wake press replayed by the boot does not land in
	 * the menu that is about to open. */
	if (a.resume_menu && a.running) {
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
	 * (texload_ready), MP4 playing (muse_poll answers a track's end with the
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
	bool ram_idle = false;
	charge_dark dark = { 0 };    /* the screen off on the charger: src/chargedark.h */

	/* At full, whatever a launcher before this one left it at. */
	plat_ram_idle(false);
	/* An update the page brought over the cable that is installed now. The
	 * card's empty .media folders go once the shelf is up (after_start_step). */
	up_cable_tidy();

	while (a.running) {
		plat_input_poll(&a.in);
		if (a.in.quit_requested || want_quit) break;
		after_start_step(&a, last_render != 0);

		/* The memory back at full before a press is acted on, whatever it
		 * opens: a menu, a game, the next shelf's art. Games set their own
		 * clocks from there (Diatom's power profiles), and a port runs at
		 * full. */
		if (ram_idle) {
			for (int b = 0; b < IN_COUNT; b++)
				if (a.in.pressed[b] || a.in.down[b]) {
					plat_ram_idle(false);
					ram_idle = false;
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

		/* Unlocks sent in the background: apply what the account answered. */
		ra_flush_poll();

		/* What Muse said since the last pass - including END, which is how
		 * the next track of an album starts while nobody is on its screen -
		 * and the covers it was asked for by Muse's shelf, which that shelf
		 * has to be drawn again to ask the worker for. */
		muse_poll();
		if (cover_answers()) redraw_now();

		/* Albums straight in Music/ filed under their artists, from what the
		 * player says their tags are; the shelf read again once anything
		 * moved. See file_album. */
		muse_file_albums();
		if (g_muse_moved && !flat_albums_waiting()) {
			g_muse_moved = false;
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
			                         on_charger(), musec_playing())) {
			case CHARGE_GO_DARK: snap_off(); plat_backlight(false); continue;
			case CHARGE_DARK:
				/* Dark on the cable, and still moving files behind it */
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

		/* USB charges only until asked: a computer plugged in gets its
		 * question here, once per plug (usblink.h). After the dark screen's
		 * turn, so it waits for the screen to be lit rather than opening
		 * in the dark. */
		if (usblink_follow(db_dev(), plat_now_ms())) usb_ask(&a);

		if (a.in.pressed[IN_POWER] || idle_due(&a)) { power_off(&a); break; }

		if (in_repeat(&a.in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a.in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a.in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a.in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Auto's clock: 7:00 and 19:00 by the handheld, looked at once a
		 * minute (theme_tick says why not every pass). Here for the shelf;
		 * menu_run_body asks for every menu, every screen that runs a loop
		 * of its own asks through tick_theme - the keyboard's through its
		 * panel, keyboard_panel - a message asks before it is drawn, and
		 * on_game_tick asks for the file while a game runs and leaves
		 * g_theme_late for tick_theme to take. What does not ask is over in
		 * a moment or draws nothing: the launch zoom, the power-off, the
		 * frame a port loads behind, and Muse's lock, whose panel is dark
		 * until the screen it returns to asks for itself. */
		tick_theme(&a);

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

		/* SELECT is Muse, on the shelf and on every screen but a running
		 * game - see muse_open. Before the shelf's own keys, which do not use
		 * it, so every layout gets it from this one line. */
		if (a.in.pressed[IN_SELECT]) { muse_open(&a, NULL, NULL); continue; }

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
			    (focus_due && !focus_ok(&a))) {
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

				if (texload_idle() && !musec_playing() && !g_xs.on && !g_songpop.on &&
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
			if (ram_idle != (now - last_busy >= RAM_IDLE_MS)) {
				ram_idle = !ram_idle;
				plat_ram_idle(ram_idle);
			}
		}
	}
	/* A launcher stopped while the shelf sat still leaves nothing slow
	 * behind for whatever runs next, and nothing dark. */
	if (ram_idle) plat_ram_idle(false);
	if (dark.dark) plat_backlight(true);
	/* A File Transfer behind the shelf ends with the launcher */
	xs_end();

done:
	free_all_textures(&a);
	/* First, and before IMG_Quit: the worker is inside IMG_Load, and the
	 * views it decodes into are freed just below. The resizer too, for the
	 * same IMG_Quit - it is in IMG_Load and IMG_SavePNG. */
	texload_stop();
	art_shrink_stop();
	/* Told, not waited for - see ra_flush_stop. */
	ra_flush_stop();
	for (int i = 0; i < a.sys.count; i++) {
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
