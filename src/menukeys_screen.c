/* SPDX-License-Identifier: GPL-3.0-only */
/* See menukeys_screen.h, and src/menukeys.h for the rules. */
#include "menukeys_screen.h"

#include "aout_route.h"
#include "db.h"
#include "design.h"
#include "i18n.h"
#include "levels.h"
#include "boost.h"
#include "mp4lock.h"
#include "notice.h"
#include "nowplaying_screen.h"
#include "panel.h"
#include "platform.h"
#include "shotname.h"
#include "shots_screen.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <SDL_image.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MK_DB_KEY  "menukeys"
/* The folder of the launcher's own shots, under Screenshots/ beside the
 * systems' (src/shotname.h), and the "game" its names begin with */
#define MK_SHOT_FOLDER SHOT_LAUNCHER_DIR
/* The notice, as long as a game's screenshot notice is up (main.c, on_hotkey) */
#define MK_NOTICE_MS 1500

static mk_table g_mk;
static app *g_app;
static bool g_cap, g_locked, g_tour;
static int g_caught = -1;
static unsigned g_unlocks, g_tour_locks;

/* src/hotkeys.h's keys as the handheld's buttons, in its order */
static const in_button KEY_IN[HK_KEYS] = {
	IN_ACCEPT, IN_BACK, IN_X, IN_Y, IN_L1, IN_R1, IN_L2, IN_R2, IN_START
};

int mk_key_of(in_button b)
{
	int k;

	for (k = 0; k < HK_KEYS; k++)
		if (KEY_IN[k] == b) return k;
	return -1;
}

in_button mk_in_of(int key)
{
	return key >= 0 && key < HK_KEYS ? KEY_IN[key] : IN_NONE;
}

const mk_table *mk_now(void) { return &g_mk; }

void mk_put(const mk_table *t)
{
	char s[96];

	g_mk = *t;
	mk_store(t, s, sizeof s);
	if (s[0]) db_set_str(db_dev(), MK_DB_KEY, s);
	else db_del(db_dev(), MK_DB_KEY);
}

void mk_capture(bool on)
{
	g_cap = on;
	g_caught = -1;
}

int mk_caught(void) { return g_caught; }

unsigned mk_unlocks(void) { return g_unlocks; }

void mk_tour(bool on) { g_tour = on; }

unsigned mk_tour_locks(void) { return g_tour_locks; }

/* ---- the screenshot -------------------------------------------------------- */

/* What the writer thread says: 0 nothing, 1 written, 2 failed */
static SDL_atomic_t g_saved;
static char g_saved_path[600];
static bool g_writing;
/* The notice up: its texture, made from the file notice_render_ex writes */
static SDL_Texture *g_note;
static SDL_Renderer *g_note_r;
static int g_note_w, g_note_h;
static Uint32 g_note_until;

typedef struct {
	SDL_Surface *s;
	char path[600];
} shot_job;

static int mkdirs(const char *dir)
{
	char p[600], *s;

	snprintf(p, sizeof p, "%s", dir);
	for (s = p + 1; *s; s++) {
		if (*s != '/') continue;
		*s = 0;
		if (mkdir(p, 0755) && errno != EEXIST) return -1;
		*s = '/';
	}
	return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

/* Off the frame's thread: a PNG of 1024 x 768 takes a few hundred ms on the
 * Pixel 2. Written beside its name and renamed into place, so the
 * Screenshots shelf never reads half of one. */
static int shot_write(void *arg)
{
	shot_job *j = arg;
	char tmp[620];
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.part", j->path);
	ok = IMG_SavePNG(j->s, tmp) == 0 && rename(tmp, j->path) == 0;
	if (!ok) {
		fprintf(stderr, "screenshot: %s not written: %s\n", j->path, SDL_GetError());
		unlink(tmp);
	} else {
		fprintf(stderr, "screenshot: %s\n", j->path);
	}
	SDL_FreeSurface(j->s);
	SDL_free(j);
	SDL_AtomicSet(&g_saved, ok ? 1 : 2);
	return 0;
}

/* The frame, read back as it was presented (plat_grab_next): its file named
 * as the hotkey names a game's, "ZyperOS 2026-11-22 10.04.31.png", in
 * Screenshots/ZyperOS, and the shutter heard at once */
static void shot_grabbed(SDL_Surface *frame)
{
	char dir[512], name[128];
	time_t now = time(NULL);
	struct tm tm;
	shot_job *j;
	SDL_Thread *th;
	long when;

	if (!frame) {
		uisound_play(DS_SOUND_DENY);
		g_writing = false;
		return;
	}
	localtime_r(&now, &tm);
	when = shot_secs(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	snprintf(dir, sizeof dir, "%s/%s/%s", P_CARD, SHOT_DIR, MK_SHOT_FOLDER);
	if (when < 0 || !shot_file_name(MK_SHOT_FOLDER, when, name, sizeof name) || mkdirs(dir) ||
	    !(j = SDL_malloc(sizeof *j))) {
		fprintf(stderr, "screenshot: no file for it in %s\n", dir);
		SDL_FreeSurface(frame);
		uisound_play(DS_SOUND_DENY);
		g_writing = false;
		return;
	}
	j->s = frame;
	snprintf(j->path, sizeof j->path, "%s/%s", dir, name);
	snprintf(g_saved_path, sizeof g_saved_path, "%s", j->path);
	{
		const uis_cue c = uisound_shot(uisound_setting());

		if (c.level > 0.0f) uisound_cue(c);
	}
	SDL_AtomicSet(&g_saved, 0);
	if (!(th = SDL_CreateThread(shot_write, "screenshot", j))) shot_write(j);
	else SDL_DetachThread(th);
	/* A screen that draws only when something changes draws to say it */
	redraw_at(plat_now_ms() + 100);
}

static void shoot(void)
{
	if (g_writing) return;           /* one at a time */
	g_writing = true;
	plat_grab_next(shot_grabbed);
	/* The frame it reads is the next one presented */
	redraw_now();
}

/* The notice made from its file: "Screenshot saved" over Screenshots/ZyperOS,
 * the picture small beside them, as a game's (DESIGN.md 11.7) */
static void note_make(SDL_Renderer *r)
{
	char dt[300], body[64];
	unsigned char hd[8];
	FILE *f;

	snprintf(dt, sizeof dt, "/tmp/zyperos-mk-notice-%ld.dtov", (long)getpid());
	snprintf(body, sizeof body, "%s/%s", SHOT_DIR, MK_SHOT_FOLDER);
	if (g_note) SDL_DestroyTexture(g_note);
	g_note = NULL;
	if (!notice_render_ex(T(S_HOTKEYS_SHOT_SAVED), body, g_saved_path, NOTICE_PICTURE, 0, 0, dt, theme_index()))
		return;
	if ((f = fopen(dt, "rb"))) {
		if (fread(hd, 1, 8, f) == 8 && !memcmp(hd, "DTOV", 4)) {
			int w = hd[4] | hd[5] << 8, h = hd[6] | hd[7] << 8;
			SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);

			if (s && fread(s->pixels, 4, (size_t)w * h, f) == (size_t)w * h &&
			    (g_note = SDL_CreateTextureFromSurface(r, s))) {
				SDL_SetTextureBlendMode(g_note, SDL_BLENDMODE_BLEND);
				g_note_w = w;
				g_note_h = h;
				g_note_r = r;
			}
			if (s) SDL_FreeSurface(s);
		}
		fclose(f);
	}
	unlink(dt);
}

/* Where a notice stands over a screen: centered at the foot, as the shots
 * place a game's (src/shots.c) */
static void note_draw(SDL_Renderer *r, Uint8 alpha)
{
	SDL_Rect at = { (ZYPEROS_SCREEN_W - g_note_w) / 2, ZYPEROS_SCREEN_H - g_note_h - ZYPEROS_SCREEN_H / 24,
	                g_note_w, g_note_h };

	SDL_SetTextureAlphaMod(g_note, alpha);
	SDL_RenderCopy(r, g_note, NULL, &at);
}

void mk_overlay(SDL_Renderer *r)
{
	Uint32 now = plat_now_ms();
	int said = g_writing ? SDL_AtomicGet(&g_saved) : 0;

	if (g_writing) {
		if (said == 1) {
			/* ...and on the Screenshots group, read again on the shelf's
			 * next pass (main.c) */
			shots_taken();
			note_make(r);
			g_note_until = now + MK_NOTICE_MS;
			g_writing = false;
		} else if (said == 2) {
			uisound_play(DS_SOUND_DENY);
			g_writing = false;
		} else {
			redraw_at(now + 100);    /* looked at again until it is written */
		}
	}
	if (!g_note || g_note_r != r) return;
	if ((Sint32)(now - g_note_until) >= 0) {
		SDL_DestroyTexture(g_note);
		g_note = NULL;
		return;
	}
	{
		Uint32 left = g_note_until - now;
		float k = left < DS_MS_FADE_OUT ? (float)left / DS_MS_FADE_OUT : 1.0f;

		note_draw(r, (Uint8)(255 * k));
		redraw_at(left < DS_MS_FADE_OUT ? now + 16 : g_note_until - DS_MS_FADE_OUT);
	}
}

/* ---- the lock -------------------------------------------------------------- */

/* Locked until opened: false then, true when Auto Off came instead, which
 * on the shelf turns the handheld off here and over a game (`game`) is the
 * caller's, which stops the game first */
static bool lock_run(app *a, bool game)
{
	mp4lock l = { 0 };
	bool idle = false;
	bool dark = false, panel_was = false;
	int drawn_k = -1;
	Uint32 osd_due = UINT32_MAX;

	g_locked = true;
	fprintf(stderr, "lock: locked (MENU's chord%s)\n", game ? ", over a game" : "");
	l.hold_ms = g_mk.hold_ms;
	mp4lock_engage(&l, plat_now_ms(), a->in.down[MP4_UNLOCK_A] && a->in.down[MP4_UNLOCK_B]);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!want_quit && a->running) {
		bool lit, awake, vol = false;

		plat_input_poll(&a->in);
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		/* Auto Off as on MP4's lock: no button, so the lock is not in its way */
		if (idle_due(a)) {
			plat_backlight_dim(false);
			plat_backlight(true);
			idle = true;
			if (!game) power_off(a);
			break;
		}
		if (mp4lock_step(&l, plat_now_ms(), a->in.down, a->in.pressed)) break;
		/* The volume keys get through, with their bar (mp4lock_passes) */
		if (levels_keys(&a->in, BOOST_FILL)) vol = true;
		lit = mp4lock_lit(&l);
		awake = mp4lock_awake(&l);
		if (!awake) {
			/* Dark: nothing drawn */
			if (!dark) { plat_backlight(false); dark = true; }
			panel_was = false;
			osd_due = UINT32_MAX;
			SDL_Delay(30);
			continue;
		}
		if (vol || lit != panel_was || (osd_due != UINT32_MAX && (int)(plat_now_ms() - osd_due) >= 0) ||
		    (int)(mp4lock_progress(&l) * 100) != drawn_k) {
			tick_theme(a);
			draw_background(a);
			if (lit) {
				ui_scrim(a->r, 0);
				lock_panel(a, &l);
			}
			plat_draw_osd(a->r);
			osd_due = plat_osd_until();
			plat_present();
			drawn_k = (int)(mp4lock_progress(&l) * 100);
			/* Lit after the frame, at the lowest rung, as MP4's lock */
			if (dark) { plat_backlight_dim(true); plat_backlight(true); dark = false; }
		}
		panel_was = lit;
		SDL_Delay(8);
	}
	plat_backlight_dim(false);
	plat_backlight(true);
	fprintf(stderr, "lock: unlocked\n");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	g_locked = false;
	g_unlocks++;
	redraw_now();
	return idle;
}

void mk_lock(app *a)
{
	(void)lock_run(a, false);
}

bool mk_lock_game(app *a)
{
	return lock_run(a, true);
}

void mk_lock_shot(app *a, float k)
{
	mp4lock l = { 0 };

	mp4lock_engage(&l, 1000, false);
	if (k > 0) {
		l.armed = true;
		l.held_from = 1000;
		l.hold_ms = 1000;
		l.now = 1000 + (unsigned)(k * 1000);
	}
	draw_background(a);
	ui_scrim(a->r, 0);
	lock_panel(a, &l);
}

void mk_notice_shot(app *a)
{
	snprintf(g_saved_path, sizeof g_saved_path, "%s", "tools/fixtures/screenshot.png");
	note_make(a->r);
	if (g_note) {
		note_draw(a->r, 255);
		SDL_DestroyTexture(g_note);
		g_note = NULL;
	}
}

/* ---- the hooks ------------------------------------------------------------- */

static bool take(in_button b)
{
	int k = mk_key_of(b);

	if (g_cap) {
		if (!mk_key_ok(k)) return false;
		g_caught = k;
		return true;
	}
	return mk_act_on(&g_mk, k) >= 0;
}

static void run(in_state *st, in_button b)
{
	int act;

	if (g_cap || g_locked || !g_app) return;
	act = mk_act_on(&g_mk, mk_key_of(b));
	if (act == MK_SHOT) {
		shoot();
	} else if (act == MK_LOCK && g_tour) {
		/* the tutorial's: its lesson locks a stand-in, never the handheld */
		g_tour_locks++;
	} else if (act == MK_LOCK) {
		mk_lock(g_app);
		if (st != &g_app->in) memset(st, 0, sizeof *st);
	}
}

void mk_init(app *a)
{
	char s[96] = "";

	g_app = a;
	db_get_str(db_dev(), MK_DB_KEY, s, sizeof s, "");
	if (!mk_load(s, &g_mk)) fprintf(stderr, "menukeys: '%s' not read, ZyperOS's own\n", s);
	plat_set_menu_keys(take, run);
}
