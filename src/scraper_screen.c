/* SPDX-License-Identifier: GPL-3.0-only */
#include "scraper_screen.h"
#include "menu_pick.h"

#include "aout_route.h"
#include "app.h"
#include "artscrape.h"
#include "dlq.h"
#include "ds/flex.h"     /* the pages' bodies, laid out by rules */
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/page.h"
#include "favorites.h"
#include "keyhints.h"
#include "mediatex.h"
#include "net.h"
#include "panel.h"
#include "sgdb.h"
#include "signin_screen.h"
#include "snaps.h"
#include "snapshelf.h"
#include "sort.h"
#include "ss.h"
#include "ssfetch.h"
#include "ssrun.h"
#include "stats.h"
#include "uisound.h"
#include "usbdrive_screen.h"
#include "zyperdraw.h"

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

/* ---------- MENU > Scraper: one page (DESIGN.md 11.19) -------------------
 *
 * Tarik, 2026-11-13, over five rounds of shots: the old panel was "very
 * confusing" ("stuck on 0/523 while the game on the header is changing"),
 * "too verbose", its video part "an afterthought"; he asked to see "the
 * images coming in, more icons", and the scope: "Is it getting video for
 * all my roms? What is 1.4gb? Can i pick which ones gets videos? Or which
 * systems gets arts?" This page answers those on itself, in the look of
 * Flash Drive's page:
 *
 *   the plan     before a run: each system's icon with a cover mark and a
 *                video mark, the covers it will look for, the videos it
 *                will get and the room they take. A runs it, Y opens
 *                Systems, START Settings (the accounts, Video Snaps).
 *   Systems      one tile a system, its icon and two marks; A switches its
 *                covers, X its videos, Y asks which games get videos; the
 *                system in focus is said under the heading and at the foot.
 *   the run      Zyper beside the game; its system's icon, its source's
 *                mark and the bar under it; each cover found lands big and
 *                then joins the six before it, a video's play mark on it.
 *   done         the covers it found as a wall, the counts as marks, and
 *                the room said plainly: "Videos: 3 systems, 525 MB on the
 *                card".
 *
 * A system's own Box Art (its menu) opens the same page for it alone. The
 * run is artscrape.c's, stepped here a pass at a time as before; what it
 * found arrives by art_progress.found_seq, decoded off the frame
 * (src/mediatex.h). */

#define SCP_RING  14      /* arrivals kept: the run's grid shows 7, Done's wall 14 */
#define SCP_LAND  520     /* a cover's landing, ms */
#define SCP_COLS  3
#define SCP_ROWS  3       /* the Systems grid's rows on the screen */
#define SCP_MB_PER_VIDEO 2.5   /* a ScreenScraper snap, until the card's own say */
#define SCP_MB_PER_COVER 0.25  /* a cover, until the card's own say */
/* The page's frame, as Flash Drive's pages (DESIGN.md 11.18, src/ds/page.h):
 * what the page holds in the frame's body, laid out by rules in it
 * (scp_body), a line over the keys (the frame's line_y), and the keys on
 * every screen's foot */
/* A tree for a page's body (src/ds/flex.h): its root the screen, what it
 * holds inside the frame's body, `pad` in from it, laid out as `dir` with
 * `gap` between */
static int scp_body(ds_flex *F, const ds_page_frame *pf, ds_dir dir, int gap, int pad)
{
	return ds_flex_begin(F, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
	                     &(ds_box){ .dir = dir, .gap = gap,
	                                .pad = { pf->body.x + pad, pf->body.y + pad,
	                                         ZYPEROS_SCREEN_W - pf->body.x - pf->body.w + pad,
	                                         ZYPEROS_SCREEN_H - pf->body.y - pf->body.h + pad } });
}

/* What a run leaves free on the card, and when one stops for room */
#define SCP_RESERVE (200ULL * 1000 * 1000)
/* A run's pace before its own says (the 2026-11-13 logs: about 2 s a
 * ScreenScraper lookup, about 1 s a libretro try), and a video's download
 * over the Pixel 2's Wi-Fi */
#define SCP_S_SS    2.0
#define SCP_S_LR    1.0
#define SCP_S_VIDEO 2.0

typedef struct {
	char folder[CFG_STR], tag[8], name[CFG_STR], key[CFG_STR];
	int  miss;                       /* games with no cover */
	int  vneed[SNAPS_FOR_COUNT];     /* games with no video, by Videos for */
	int  names;                      /* games with a cover and no name: the details part's */
	int  both[SNAPS_FOR_COUNT];      /* of those, the ones with no video too */
	unsigned long long vroom;        /* its videos on the card */
	int  nvideos;
	bool cov, vid;
	SDL_Texture *icon;
} scp_sys;

typedef struct {
	SDL_Texture *tex;
	int w, h;
	unsigned seq;
	int src;                         /* ART_SRC_* */
	bool video;
	char folder[CFG_STR];
} scp_cover;

enum { SCP_PLAN, SCP_SYSTEMS, SCP_RUN, SCP_DONE };

typedef struct scp_view_ {
	app *a;
	bool fixture;                    /* a shot's: nothing read, nothing run */
	char only[CFG_STR];              /* a system's Box Art: that system alone */
	scp_sys s[CFG_MAX_SYSTEMS];
	int n;
	int where, focus;
	bool net, ss_in;
	double mb_video, mb_cover;
	unsigned long long free;         /* the card's room, 0 when it cannot tell */
	/* the run */
	systems_cfg cfg;
	/* Which kinds a run gets (the Downloads page's rows): the page's own
	 * runs get all three */
	bool in_cov, in_vid, in_names;
	art_progress p;
	unsigned seen;                   /* the arrival last taken */
	scp_cover ring[SCP_RING];          /* [0] the newest */
	int nring;
	unsigned land_at;                /* when [0] began to land */
	unsigned shot_now;               /* a shot's clock, else 0 */
	int done_found, done_videos, done_missing;
	char problem[160];
	/* its pace, for the time left: per game in the part it is in, before
	 * its own (s_cov, s_det), and the details part's games, guessed */
	double s_cov, s_det;
	int est_looks;
	bool pace_det;
	unsigned pace_t0, room_at;
	int pace_n0;
	/* A row of an ask (scp_ask) that A switches in place, the panel kept
	 * open: its place, -1 for none, and what switches it */
	int ask_switch;
	void (*ask_switch_fn)(struct scp_view_ *v, menu_row *tiles);
} scp_view;

/* Names and Details (Settings, 1.0.6): whether a run asks names, years and
 * synopses for covered games with none (artscrape.c, art_names). On unless
 * the player turned it off; kept in the device's database as scraper.names.
 * A shot turns it off with --scraper-names-off (main.c). */
int g_scp_names = -1;

static bool scp_names_on(void)
{
	if (g_scp_names < 0) {
		char v[8];

		g_scp_names = !(db_get_str(db_dev(), "scraper.names", v, sizeof v, NULL) &&
		                !strcmp(v, "off"));
	}
	return g_scp_names != 0;
}

static void scp_names_set(bool on)
{
	g_scp_names = on;
	db_set_str(db_dev(), "scraper.names", on ? "on" : "off");
}

/* ---- the run, as a lane of the downloads' queue (src/dlq.h) ---------------
 * The run is the queue's, not this page's: it goes on when the page closes
 * (B, "Continue in background") and the page shows it again when it opens.
 * So what the run reads is kept here rather than in the page: its systems
 * (art_begin's, and the videos' g_snap_sys), the system a Box Art run is
 * for, the covers it looks for (what the status corner's ring sweeps), and,
 * once it has ended, what it did. */
static systems_cfg g_art_sys;          /* the run's systems */
static char g_art_only[CFG_STR];       /* a Box Art run's system, or "" */
static int g_art_total;                /* covers to look for: the ring */
static art_progress g_art_last;        /* what the last run did, as it ended */
static unsigned g_scp_seen;            /* the lane's ends this page has shown */
/* A run that ended behind the screens: the shelves take its names, covers
 * and snaps on the shelf's next pass (scraper_after_run) */
static bool g_art_after;

/* A run's covers, by the Systems switch (kept as snaps_covers); the system of
 * a Box Art always */
static bool art_lane_rule(const char *folder)
{
	if (g_art_only[0]) return !strcmp(folder, g_art_only);
	return snaps_covers(folder);
}

/* Covers out of the run (the Downloads page's Covers row) */
static bool art_lane_none(const char *folder)
{
	(void)folder;
	return false;
}

static bool art_lane_begin(void)
{
	art_begin(&g_art_sys, P_ROMS, NULL);
	return true;
}

static int art_lane_step(unsigned now_ms)
{
	(void)now_ms;
	return art_step();
}

/* MENU > Scraper's value: what is still missing, counted on the systems with
 * covers on (g_scraper_val). As the page closes, and as a run ends. */
static void scp_menu_value(const systems_cfg *sys, const char *only)
{
	int i, miss = 0;

	if (only && only[0]) return;
	for (i = 0; i < sys->count; i++) {
		char dir[LIB_PATH * 2];

		if (!snaps_covers(sys->systems[i].folder)) continue;
		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, sys->systems[i].folder);
		miss += art_missing(dir, sys->systems[i].exts, NULL, NULL);
	}
	if (miss) snprintf(g_scraper_val, sizeof g_scraper_val, TN(S_SCRAPER_MISSING_N, miss), miss);
	else snprintf(g_scraper_val, sizeof g_scraper_val, "%s", T(S_SCRAPER_COMPLETE));
}

static void art_lane_done(dlq_end how)
{
	art_status(&g_art_last);
	if (how == DLQ_ENDED && !g_art_last.problem[0]) plat_side_lights_say(SIDE_LIGHTS_DONE);
	art_cancel();
	art_covers_rule(NULL);
	art_names_rule(NULL);
	g_snap_run_off = false;
	g_snap_sys = NULL;
	scp_menu_value(&g_art_sys, g_art_only);
	/* Its page open, the page takes it as it shows the end; closed, the
	 * shelf takes it on its next pass */
	if (!dlq_page_open(DLQ_ART)) g_art_after = true;
}

/* The covers part against the plan's count, then the details part against
 * its own (art_progress.dtotal), as the page's bar counts them */
static int art_lane_pct(void)
{
	art_progress p;

	art_status(&p);
	if (p.details) return p.dtotal > 0 ? (int)((long long)p.dlooked * 100 / p.dtotal) : 0;
	return g_art_total > 0 ? (int)((long long)p.looked * 100 / g_art_total) : 0;
}

/* Its card is set as it starts: P_ROMS is the device's, read at run time */
static dlq_lane_def ART_LANE = {
	.name = "box art", .slot = NET_SLOT_ART,
	.step = art_lane_step, .done = art_lane_done, .pct = art_lane_pct,
	.every_ms = 8,
};

void scraper_after_run(app *a)
{
	if (!g_art_after) return;
	g_art_after = false;
	free_all_textures(a);
	refresh_game_titles(a);
	snapshelf_media_changed();
}

static SDL_Texture *scp_icon(scp_view *v, int i)
{
	char path[CFG_STR * 2];

	if (i < 0 || i >= v->n) return NULL;
	if (!v->s[i].icon && v->s[i].key[0]) {
		snprintf(path, sizeof path, "%s/system-icons/%s-still.png", v->fixture ? "res" : P_ROOT, v->s[i].key);
		if ((v->s[i].icon = IMG_LoadTexture(v->a->r, path))) {
			SDL_SetTextureScaleMode(v->s[i].icon, SDL_ScaleModeNearest);
			SDL_SetTextureBlendMode(v->s[i].icon, SDL_BLENDMODE_BLEND);
		}
	}
	return v->s[i].icon;
}

static void scp_icon_draw(scp_view *v, int i, int cx, int cy, int size, Uint8 al)
{
	SDL_Texture *t = scp_icon(v, i);
	int w, h;

	if (!t) return;
	SDL_QueryTexture(t, NULL, NULL, &w, &h);
	if (w >= h) { h = h * size / w; w = size; } else { w = w * size / h; h = size; }
	SDL_SetTextureAlphaMod(t, al);
	SDL_RenderCopy(v->a->r, t, NULL, &(SDL_Rect){ cx - w / 2, cy - h / 2, w, h });
	SDL_SetTextureAlphaMod(t, 255);
}

static int scp_find(const scp_view *v, const char *folder)
{
	int i;

	for (i = 0; folder && i < v->n; i++)
		if (!strcmp(v->s[i].folder, folder)) return i;
	return -1;
}

static int scp_bsearch_cmp(const void *k, const void *e)
{
	return strcmp(*(const char *const *)k, *(const char *const *)e);
}

/* What every system has and wants, from the shelves already scanned: its
 * games with no cover (artscrape.c's art_missing, one listing of the
 * folder and one of its .media), its games with no video by each choice of
 * Videos for (one listing of the .mp4), the room its videos take, and its
 * two switches. When the page opens and after a run, never per frame. */
static void scp_count(scp_view *v)
{
	app *a = v->a;
	unsigned long long all_bytes = 0, cover_bytes = 0;
	int i, all_videos = 0, all_covers = 0;
	long long now = (long long)time(NULL);
	struct statvfs vfs;

	v->n = 0;
	v->net = net_online();
	v->ss_in = ss_signed_in();
	v->free = statvfs(P_ROMS, &vfs) == 0 ? (unsigned long long)vfs.f_bavail * vfs.f_frsize : 0;
	stats_summarize(STATS_ALL, false, (long)time(NULL));
	for (i = 0; i < a->sys.count && v->n < CFG_MAX_SYSTEMS; i++) {
		const system_cfg *sc = &a->sys.systems[i];
		const game_list *gl;
		scp_sys *s;
		char dir[LIB_PATH * 2], **mp4, **png;
		int k, nmp4, npng, ncb;
		db_titles titles = { 0 }, miss = { 0 };
		bool named;

		if (!sc->folder[0] || a->view[i].owner || is_muse(sc)) continue;
		if (v->only[0] && strcmp(sc->folder, v->only)) continue;
		gl = sys_games(&a->view[i]);
		if (!gl->count) continue;
		s = &v->s[v->n++];
		memset(s, 0, sizeof *s);
		snprintf(s->folder, sizeof s->folder, "%s", sc->folder);
		snprintf(s->tag, sizeof s->tag, "%s", sc->tag);
		snprintf(s->name, sizeof s->name, "%s", sc->name);
		snprintf(s->key, sizeof s->key, "%s", home_art_key(sc->tag));
		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, sc->folder);
		s->miss = art_missing(dir, sc->exts, NULL, NULL);
		s->cov = v->only[0] ? true : snaps_covers(sc->folder);
		s->vid = snaps_get(sc->folder);
		s->vroom = snaps_bytes(P_ROMS, sc->folder);
		mp4 = snaps_stems(P_ROMS, sc->folder, &nmp4);
		png = snaps_cover_stems(P_ROMS, sc->folder, &npng);
		s->nvideos = nmp4;
		all_bytes += s->vroom;
		all_videos += nmp4;
		cover_bytes += snaps_cover_bytes(P_ROMS, sc->folder, &ncb, 24);
		all_covers += ncb;
		/* The details part's games, as artscrape.c's details_count picks
		 * them: a cover, no name, and no miss in its 30 days */
		named = v->ss_in && ss_system_id(sc->folder);
		if (named) {
			db_titles_load(db_lib(), sc->folder, &titles);
			db_ss_misses_load(db_lib(), sc->folder, now - 30 * 86400LL, now + 86400, &miss);
		}
		for (k = 0; k < gl->count; k++) {
			const game_entry *g = &gl->items[k];
			const char *stem = g->name, *t;
			long secs = 0, last = 0;
			bool name = false;
			int w;

			if (named && ((npng && bsearch(&stem, png, (size_t)npng, sizeof *png, scp_bsearch_cmp)) ||
			              es_art_cover(dir, g->file, g->name)) && !db_titles_find(&miss, g->file)) {
				t = db_titles_find(&titles, g->file);
				name = !t || !*t;
			}
			if (name) s->names++;
			if (nmp4 && bsearch(&stem, mp4, (size_t)nmp4, sizeof *mp4, scp_bsearch_cmp)) continue;
			if (es_art_video(dir, g->file, g->name)) continue;
			for (w = 0; w < SNAPS_FOR_COUNT; w++) {
				bool in = w == SNAPS_FOR_ALL ||
				          (w == SNAPS_FOR_FAVORITES && fav_is(sc->tag, g->file)) ||
				          (w == SNAPS_FOR_PLAYED && stats_lookup(sc->tag, g->file, &secs, &last) &&
				           (secs > 0 || last > 0));

				if (!in) continue;
				s->vneed[w]++;
				if (name) s->both[w]++;
			}
		}
		if (named) {
			db_titles_free(&titles);
			db_titles_free(&miss);
		}
		snaps_stems_free(mp4, nmp4);
		snaps_stems_free(png, npng);
	}
	/* A video's size and a cover's as the card's own say, once it has a few */
	v->mb_video = all_videos >= 5 ? (double)all_bytes / all_videos / 1e6 : SCP_MB_PER_VIDEO;
	v->mb_cover = all_covers >= 5 ? (double)cover_bytes / all_covers / 1e6 : SCP_MB_PER_COVER;
	if (v->focus >= v->n) v->focus = v->n ? v->n - 1 : 0;
}

/* The plan's totals */
static void scp_totals(const scp_view *v, int *ncov, int *covers, int *nvid, int *videos)
{
	int i, which = snaps_for();

	*ncov = *covers = *nvid = *videos = 0;
	for (i = 0; i < v->n; i++) {
		if (v->s[i].cov) { (*ncov)++; *covers += v->s[i].miss; }
		if (v->s[i].vid) { (*nvid)++; *videos += v->s[i].vneed[which]; }
	}
}

static void scp_mb(double mb, char *out, size_t n)
{
	sys_menu_bytes((unsigned long long)(mb * 1e6), out, n);
}

/* A size's spaces as no-break spaces, in place: "525 MB" stays on one line */
static void scp_nbsp(char *s, size_t n)
{
	char out[64];
	size_t o = 0, i;

	for (i = 0; s[i] && o + 3 < sizeof out; i++) {
		if (s[i] == ' ') { out[o++] = '\xc2'; out[o++] = '\xa0'; }
		else out[o++] = s[i];
	}
	out[o] = '\0';
	snprintf(s, n, "%s", out);
}

/* What a run will do with the switches as they are: covers to look for,
 * videos to get (with an account), details lookups, the bytes all that
 * adds to the card and about how long it takes. The plan's line, the
 * room's check and the run's time left all read this. */
typedef struct {
	int covers, videos, looks;
	int names;          /* of the looks, names for covered games (Names and Details) */
	double bytes, secs;
} scp_need;

static void scp_needs(const scp_view *v, scp_need *o)
{
	int i, k = snaps_for();

	memset(o, 0, sizeof *o);
	for (i = 0; i < v->n; i++) {
		const scp_sys *s = &v->s[i];
		bool vid = s->vid && v->ss_in && v->in_vid;

		if (s->cov && v->in_cov) o->covers += s->miss;
		if (vid) o->videos += s->vneed[k];
		if (v->ss_in) {
			/* Names follow the system's covers switch; with the covers out
			 * of the run, its games with no cover are asked too */
			int nm = s->cov && scp_names_on() && v->in_names ? s->names + (v->in_cov ? 0 : s->miss) : 0;

			o->names += nm;
			o->looks += nm + (vid ? s->vneed[k] - (nm ? s->both[k] : 0) : 0);
		}
	}
	o->bytes = (o->covers * v->mb_cover + o->videos * v->mb_video) * 1e6;
	o->secs = o->covers * (v->ss_in ? SCP_S_SS : SCP_S_LR) + o->looks * SCP_S_SS + o->videos * SCP_S_VIDEO;
}

/* Will it fit, leaving SCP_RESERVE free? A card that cannot say: yes */
static bool scp_fits(const scp_view *v, const scp_need *n)
{
	return !v->free || n->bytes + (double)SCP_RESERVE <= (double)v->free;
}

/* A time as the corner's battery says one: "12 min", "1 h 20 min";
 * `round` takes a guess past 10 min to the nearest 5 */
static void scp_time(double secs, bool round, char *out, size_t n)
{
	int m = (int)((secs + 59.0) / 60.0);

	if (m < 1) m = 1;
	if (round && m > 10) m = (m + 2) / 5 * 5;
	if (m < 60) snprintf(out, n, T(S_CORNER_TIME_MIN), m);
	else if (m % 60 == 0) snprintf(out, n, T(S_CORNER_TIME_H), m / 60);
	else snprintf(out, n, T(S_CORNER_TIME_H_MIN), m / 60, m % 60);
}

/* ---- drawing ------------------------------------------------------------- */

static float scp_ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

static ui_glyph scp_src_glyph(int src)
{
	return src == ART_SRC_SG ? UI_GLYPH_GRID : src == ART_SRC_LR ? UI_GLYPH_BOOK : UI_GLYPH_CLOUD;
}

/* A cover arrived, whole in `box` (never cut, never stretched), in its
 * middle (`mid`) or at its foot; its source's mark on one corner and a
 * video's play mark on the other; a lime frame round the newest. A video
 * for a game with no cover of its own: a tile with the video's mark. */
static void scp_cover_draw(app *a, const scp_cover *c, SDL_FRect box, Uint8 al, bool lit, bool mid)
{
	const ds_theme *t = theme();
	float sw, sh, sc;
	SDL_Rect q;
	int d;

	if (al == 0) return;
	if (c->tex && c->w > 0 && c->h > 0) {
		sc = box.w / (float)c->w;
		if ((float)c->h * sc > box.h) sc = box.h / (float)c->h;
		sw = c->w * sc;
		sh = c->h * sc;
	} else {
		sw = box.w * 0.8f;
		sh = sw;
	}
	q = (SDL_Rect){ (int)(box.x + (box.w - sw) / 2), (int)(mid ? box.y + (box.h - sh) / 2 : box.y + box.h - sh),
	                (int)sw, (int)sh };
	if (lit) ui_round_rect(a->r, &(SDL_Rect){ q.x - 7, q.y - 7, q.w + 14, q.h + 14 }, 12, ui_col(t->accent, al));
	else ui_round_rect(a->r, &(SDL_Rect){ q.x - 2, q.y - 2, q.w + 4, q.h + 4 }, 4, ui_col(t->edge, al));
	if (c->tex) {
		SDL_SetTextureAlphaMod(c->tex, al);
		SDL_RenderCopy(a->r, c->tex, NULL, &q);
		SDL_SetTextureAlphaMod(c->tex, 255);
	} else {
		ui_round_rect(a->r, &q, 4, ui_col(t->raised, al));
		/* Two sizes, never the frame's own: a cover landing or moving
		 * changes its width every frame, and a glyph a size is a texture
		 * in ui.c's cache of 32 */
		ui_glyph_draw(a->r, UI_GLYPH_FILM, q.x + q.w / 2, q.y + q.h / 2, q.w >= 200 ? 96 : 64,
		              ui_col(t->accent_ink, al));
	}
	d = q.w > 120 ? 40 : 30;
	ds_badge(a->r, q.x + q.w - d / 3, q.y + q.h - d / 3, d, scp_src_glyph(c->src), false, al);
	if (c->video) ds_badge(a->r, q.x + d / 3, q.y + q.h - d / 3, d, UI_GLYPH_PLAY, true, al);
}

/* The counts as marks, centered on `cx`: covers, videos, games not found
 * when `miss` is not below 0, and the time left beside a clock when `eta`
 * is not NULL */
typedef struct { ui_glyph g; const char *s; SDL_Color c, ink; } scp_stat;

static void scp_stats(app *a, int cx, int cy, int covers, int videos, int miss, const char *eta)
{
	const ds_theme *t = theme();
	TTF_Font *f = ui_font(UI_F_MENU);
	scp_stat st[4];
	char num[3][16];
	int i, w = 0, x, ns = 0;

	snprintf(num[0], sizeof num[0], "%d", covers);
	snprintf(num[1], sizeof num[1], "%d", videos);
	snprintf(num[2], sizeof num[2], "%d", miss);
	st[ns++] = (scp_stat){ UI_GLYPH_COVER, num[0], ui_col(t->accent_ink, 255), UI_TEXT };
	st[ns++] = (scp_stat){ UI_GLYPH_FILM, num[1], ui_col(t->accent_ink, 255), UI_TEXT };
	if (miss >= 0) st[ns++] = (scp_stat){ UI_GLYPH_CANCEL, num[2], UI_TEXT_DIM, UI_TEXT_DIM };
	if (eta) st[ns++] = (scp_stat){ UI_GLYPH_CLOCK, eta, UI_TEXT_SOFT, UI_TEXT_SOFT };
	for (i = 0; i < ns; i++) w += 44 + 8 + ui_text_width(f, st[i].s) + (i < ns - 1 ? 40 : 0);
	x = cx - w / 2;
	for (i = 0; i < ns; i++) {
		ui_glyph_draw(a->r, st[i].g, x + 22, cy, 44, st[i].c);
		x += 52;
		ui_text(a->r, f, st[i].s, x, ui_cap_y(f, cy), -1, st[i].ink);
		ui_fit_drawn(st[i].s);
		x += ui_text_width(f, st[i].s) + 40;
	}
}

/* The heading beside Zyper: the page's, or a system's for its Box Art */
static const char *scp_title(const scp_view *v)
{
	return v->only[0] && v->n ? v->s[0].name : T(S_SCRAPER_HEADING);
}

/* The plan's body: a slot a system, the two marks' width (their middles 17
 * either side of the icon's, 28 across), an icon's band over its marks', and
 * the two tiles' height */
#define SCP_SLOT_W       62
#define SCP_BAND         56
#define SCP_PLAN_TILE_H  220

/* The plan: what a run will do, before it starts. Each system's icon over
 * its two marks; Covers and Videos as Flash Drive's two tiles, each with
 * what it will add to the card; and over the keys about how long it takes
 * and the room, in **warning** when it will not fit */
static void scp_plan_draw(scp_view *v)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int i, ncov, covers, nvid, videos, shown, more, slot0, tile0;
	static ds_flex F;
	SDL_Rect sr[9];
	char cb[16], cl[96], vb[24], vl[128], cplus[48] = "", vplus[48] = "", sz[32], fr[32], eta[48], room[128];
	const char *K[4] = { "A", "Y", "START", "B" };
	const char *L[4] = { T(S_SCRAPER_KEY_FETCH), T(S_SCRAPER_SYSTEMS), T(S_HOME_SETTINGS), T(S_SCRAPER_KEY_CLOSE) };
	scp_need need;

	scp_totals(v, &ncov, &covers, &nvid, &videos);
	scp_needs(v, &need);
	/* The heading's second line: "needs Wi-Fi", else about how long */
	if (!v->net) ds_page_head(a->r, &pf, DS_HEAD_SUB, scp_title(v), T(S_MENU_NEEDS_WIFI), UI_GLYPH_COUNT, 0);
	else if (need.covers + need.videos + need.looks > 0) {
		scp_time(need.secs, true, sz, sizeof sz);
		snprintf(eta, sizeof eta, T(S_SCRAPER_ABOUT_TIME), sz);
		ds_page_head(a->r, &pf, DS_HEAD_SUB, scp_title(v), eta, UI_GLYPH_CLOCK, 0);
	} else {
		ds_page_head(a->r, &pf, DS_HEAD_SUB, scp_title(v), NULL, UI_GLYPH_COUNT, 0);
	}
	/* The body, laid out by rules (src/ds/flex.h): nine slots across, the
	 * first's marks at the page's left side and the ninth's at its right
	 * (SCP_SLOT_W, the two marks' width), each an icon's band over its
	 * marks' (SCP_BAND); and the two tiles under them, 8 apart */
	{
		const int root = scp_body(&F, &pf, DS_COL, 0, 0);
		const int cell = (pf.body.w - SCP_SLOT_W) / 8;
		const int slots = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .gap = cell - SCP_SLOT_W,
		                                                   .h = 2 * SCP_BAND });
		int tiles;

		for (i = 0; i < 9; i++) {
			int id = ds_flex_add(&F, slots, &(ds_box){ .w = SCP_SLOT_W, .h = 2 * SCP_BAND });

			if (!i) slot0 = id;
		}
		tiles = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .justify = DS_J_BETWEEN,
		                                         .pad = { 0, 8, 0, 0 }, .h = 8 + SCP_PLAN_TILE_H });
		tile0 = ds_flex_add(&F, tiles, &(ds_box){ .w = DS_PAGE_TILE_W, .h = SCP_PLAN_TILE_H });
		ds_flex_add(&F, tiles, &(ds_box){ .w = DS_PAGE_TILE_W, .h = SCP_PLAN_TILE_H });
		ds_flex_solve(&F);
		for (i = 0; i < 9; i++) sr[i] = ds_flex_rect(&F, slot0 + i);
	}
	/* The systems, as icons with their two marks: nine at most, and how
	 * many more, from the page's left side */
	shown = v->n > 9 ? 8 : v->n;
	more = v->n - shown;
	for (i = 0; i < shown; i++) {
		int cx = sr[i].x + SCP_SLOT_W / 2, iy = sr[i].y + SCP_BAND / 2, my = iy + SCP_BAND;

		scp_icon_draw(v, i, cx, iy, 60, v->s[i].cov || v->s[i].vid ? 255 : 110);
		ds_switch(a->r, cx - 17, my, 28, UI_GLYPH_COVER, v->s[i].cov);
		ds_switch(a->r, cx + 17, my, 28, UI_GLYPH_FILM, v->s[i].vid);
	}
	if (more > 0) {
		char m[16];

		snprintf(m, sizeof m, "+%d", more);
		ui_text(a->r, fm, m, sr[8].x + SCP_SLOT_W / 2, ui_cap_y(fm, sr[8].y + SCP_BAND / 2), 0, UI_TEXT_SOFT);
	}
	snprintf(cb, sizeof cb, "%d", covers);
	snprintf(cl, sizeof cl, TN(S_SCRAPER_ON_N_SYSTEMS, ncov), ncov);
	if (covers) {
		scp_mb(covers * v->mb_cover, sz, sizeof sz);
		snprintf(cplus, sizeof cplus, "+%s", sz);
	}
	if (nvid) {
		snprintf(vb, sizeof vb, "%d", videos);
		if (!v->ss_in) snprintf(vl, sizeof vl, "%s", T(S_SYSMENU_SN_NEEDS_SS));
		else {
			snprintf(vl, sizeof vl, TN(S_SCRAPER_ON_N_SYSTEMS, nvid), nvid);
			if (videos) {
				scp_mb(videos * v->mb_video, sz, sizeof sz);
				snprintf(vplus, sizeof vplus, "+%s", sz);
			}
		}
	} else {
		snprintf(vb, sizeof vb, "%s", T(S_MENU_OFF_TITLE));
		snprintf(vl, sizeof vl, "%s", T(S_SCRAPER_VIDEOS_OFF_ALL));
	}
	{
		struct { ui_glyph g; const char *name, *big, *plus, *line; bool on; } TL[2] = {
			{ UI_GLYPH_COVER, T(S_SCRAPER_PAGE_COVERS), cb, cplus, cl, ncov > 0 },
			{ UI_GLYPH_FILM, T(S_SCRAPER_PAGE_VIDEOS), vb, vplus, vl, nvid > 0 },
		};

		for (i = 0; i < 2; i++) {
			SDL_Rect q = ds_flex_rect(&F, tile0 + i);

			ui_round_rect(a->r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
			ui_glyph_draw(a->r, TL[i].g, q.x + UD_PAD + 28, q.y + 50, 56,
			              TL[i].on ? ui_col(t->accent_ink, 255) : UI_TEXT_DIM);
			ui_text(a->r, fm, TL[i].name, q.x + UD_PAD + 72, ui_cap_y(fm, q.y + 50), -1, UI_TEXT);
			ui_text(a->r, ft, TL[i].big, q.x + UD_PAD, ui_cap_y(ft, q.y + 128), -1, TL[i].on ? UI_TEXT : UI_TEXT_DIM);
			/* What it adds to the card, on the number's row: a few figures
			 * leave the row room for it */
			if (TL[i].plus[0])
				ui_text(a->r, fm, TL[i].plus, q.x + q.w - UD_PAD, ui_cap_y(fm, q.y + 128), 1, UI_TEXT_SOFT);
			ui_text(a->r, fs, TL[i].line, q.x + UD_PAD, ui_cap_y(fs, q.y + 180), -1, UI_TEXT_SOFT);
		}
	}
	/* The room: the two tiles' figures added up, and the card's */
	if (need.covers + need.videos + need.looks > 0) {
		ds_line_item it[2];
		char tf[64], nmb[64];

		sys_menu_bytes((unsigned long long)need.bytes, sz, sizeof sz);
		snprintf(tf, sizeof tf, T(S_SCRAPER_TO_FETCH), sz);
		if (v->free) {
			char f2[48];

			sys_menu_bytes(v->free, fr, sizeof fr);
			snprintf(f2, sizeof f2, T(S_STORAGE_FREE), fr);
			snprintf(room, sizeof room, "%s \xc2\xb7 %s", tf, f2);
		} else {
			snprintf(room, sizeof room, "%s", tf);
		}
		it[0] = (ds_line_item){ UI_GLYPH_TO_CARD, room, scp_fits(v, &need) ? DS_TONE_INFO : DS_TONE_WARN };
		/* The names and details a run asks, apart from the covers: the long
		 * part on a card whose covers came from elsewhere (1.0.6), before
		 * the room it takes */
		if (need.names) {
			snprintf(nmb, sizeof nmb, TN(S_SCRAPER_NAMES_N, need.names), need.names);
			it[1] = it[0];
			it[0] = (ds_line_item){ UI_GLYPH_BOOK, nmb, DS_TONE_INFO };
			ds_page_line(a->r, &pf, it, 2);
		} else {
			ds_page_line(a->r, &pf, it, 1);
		}
	}
	if (v->only[0]) {
		const char *K1[3] = { "A", "START", "B" };
		const char *L1[3] = { T(S_SCRAPER_KEY_FETCH), T(S_HOME_SETTINGS), T(S_SCRAPER_KEY_CLOSE) };

		ds_page_foot(a->r, &pf, K1, L1, 3, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
	} else {
		ds_page_foot(a->r, &pf, K, L, 4, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
	}
}

/* Systems: a tile a system, three to a row across the page, its icon and
 * two marks; the one in focus ringed in **accent**, the ring inside the
 * page's sides, and said under the heading and over the keys */
#define SCP_GRID_RING 6
#define SCP_GRID_GAP  24
#define SCP_GRID_ROWG 18
#define SCP_GRID_H    100

static void scp_systems_draw(scp_view *v)
{
	app *a = v->a;
	const ds_theme *t = theme();
	ds_page_frame pf = page_top(a, 1.0f);
	int i, top, slot0 = 0;
	static ds_flex F;
	/* three across in the body, the ring inside its sides */
	const int w = (pf.body.w - 2 * SCP_GRID_RING - (SCP_COLS - 1) * SCP_GRID_GAP) / SCP_COLS, h = SCP_GRID_H;
	char cov[64], vid[64], sz[32];
	const char *K[4] = { "A", "X", "Y", "B" };
	const char *L[4] = { T(S_SCRAPER_PAGE_COVERS), T(S_SCRAPER_PAGE_VIDEOS), T(S_SCRAPER_VIDEOS_FOR), T(S_SCRAPER_DONE) };
	const scp_sys *f;
	ds_line_item it[2];
	int need;

	if (!v->n) {
		ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SCRAPER_SYSTEMS), NULL, UI_GLYPH_COUNT, 0);
		ds_page_foot(a->r, &pf, K + 3, L + 3, 1, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
		return;
	}
	f = &v->s[v->focus];
	ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SCRAPER_SYSTEMS), f->name, UI_GLYPH_COUNT, 0);
	/* The grid, laid out by rules (src/ds/flex.h): SCP_ROWS rows of three
	 * tiles, the ring's width in from the body's sides */
	{
		const int root = scp_body(&F, &pf, DS_COL, 0, SCP_GRID_RING);
		const int grid = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .wrap = true, .gap = SCP_GRID_GAP,
		                                                  .line_gap = SCP_GRID_ROWG });

		for (i = 0; i < SCP_ROWS * SCP_COLS; i++) {
			int id = ds_flex_add(&F, grid, &(ds_box){ .w = w, .h = h });

			if (!i) slot0 = id;
		}
		ds_flex_solve(&F);
	}
	/* Three rows on the screen, the focus's among them */
	top = v->focus / SCP_COLS - (SCP_ROWS - 1);
	if (top < 0) top = 0;
	if (top > v->focus / SCP_COLS) top = v->focus / SCP_COLS;
	for (i = top * SCP_COLS; i < v->n && i < (top + SCP_ROWS) * SCP_COLS; i++) {
		SDL_Rect q = ds_flex_rect(&F, slot0 + i - top * SCP_COLS);
		bool any = v->s[i].cov || v->s[i].vid;

		if (i == v->focus)
			ui_round_rect(a->r, &(SDL_Rect){ q.x - SCP_GRID_RING, q.y - SCP_GRID_RING, q.w + 2 * SCP_GRID_RING,
			                                q.h + 2 * SCP_GRID_RING }, DS_RADIUS_CARD + SCP_GRID_RING,
			              ui_col(t->accent, 255));
		ui_round_rect(a->r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
		scp_icon_draw(v, i, q.x + 20 + 44, q.y + h / 2, 88, any ? 255 : 110);
		ds_switch(a->r, q.x + w - 20 - 18, q.y + 28, 36, UI_GLYPH_COVER, v->s[i].cov);
		ds_switch(a->r, q.x + w - 20 - 18, q.y + h - 28, 36, UI_GLYPH_FILM, v->s[i].vid);
	}
	/* The system in focus: the covers it misses, and its videos still to
	 * fetch - the same figures the plan adds up */
	if (f->cov) snprintf(cov, sizeof cov, TN(S_SYSMENU_SC_MISSING, f->miss), f->miss);
	else snprintf(cov, sizeof cov, "%s", T(S_SCRAPER_OFF));
	need = f->vneed[snaps_for()];
	if (!f->vid) snprintf(vid, sizeof vid, "%s", T(S_SCRAPER_OFF));
	else if (!v->ss_in) snprintf(vid, sizeof vid, "%s", T(S_SYSMENU_SN_NEEDS_SS));
	else if (!need) snprintf(vid, sizeof vid, "%s", T(S_SCRAPER_COMPLETE));
	else {
		scp_mb(need * v->mb_video, sz, sizeof sz);
		snprintf(vid, sizeof vid, T(S_SCRAPER_TO_FETCH), sz);
	}
	it[0] = (ds_line_item){ UI_GLYPH_COVER, cov, f->cov ? DS_TONE_ON : DS_TONE_OFF };
	it[1] = (ds_line_item){ UI_GLYPH_FILM, vid, f->vid ? DS_TONE_ON : DS_TONE_OFF };
	ds_page_line(a->r, &pf, it, 2);
	ds_page_foot(a->r, &pf, K, L, 4, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

static SDL_FRect scp_lerp(SDL_FRect p, SDL_FRect q, float e)
{
	return (SDL_FRect){ p.x + (q.x - p.x) * e, p.y + (q.y - p.y) * e, p.w + (q.w - p.w) * e, p.h + (q.h - p.h) * e };
}

/* The time a run has left: the part it is in at its own pace, once it has
 * one (six games and 15 s), else the guess (s_cov, s_det); and in the
 * covers part, the details part's games after it at the guess */
static double scp_eta(scp_view *v, unsigned now, int looked, int total)
{
	bool det = v->p.details;
	double spg = det ? v->s_det : v->s_cov, left;

	if (det != v->pace_det || looked < v->pace_n0) {
		v->pace_det = det;
		v->pace_t0 = now;
		v->pace_n0 = looked;
	}
	if (looked - v->pace_n0 >= 6 && now - v->pace_t0 >= 15000)
		spg = (now - v->pace_t0) / 1000.0 / (looked - v->pace_n0);
	left = (total - looked) * spg;
	if (!det) left += v->est_looks * v->s_det;
	return left > 0 ? left : 0;
}

/* The run: the game beside Zyper; its system, its source and how far under
 * it; the newest cover landing big at the page's left, the six before it
 * moving one place along to its right; the counts and the time left over
 * the keys */
#define SCP_BIG   360   /* the newest's square, from the page's left side */
#define SCP_CELL  168   /* the six before it, three by two to the right side */
#define SCP_CELL_GAP 24 /* the square to the six, and their rows */

static void scp_run_draw(scp_view *v, unsigned now)
{
	app *a = v->a;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int hx = pf.head_x, m, looked, total, si, sq, cell0 = 0;
	float e = v->nring && v->ring[0].tex ? scp_ease((float)(now - v->land_at) / SCP_LAND) : 1.0f;
	char fit[160], cnt[48], eta[48];
	static ds_flex F;
	SDL_FRect cell[6], big;
	const char *K[2] = { "B", "Y" }, *L[2] = { T(S_UPDATE_BACK_GOES_ON), T(S_SCRAPER_KEY_STOP) };

	/* Which part: covers (the plan's count), then the details of games
	 * with a cover (their own count, art_progress.dtotal) */
	if (v->p.details) {
		looked = v->p.dlooked;
		total = v->p.dtotal;
	} else {
		int i;

		looked = v->p.looked;
		for (total = 0, i = 0; i < v->n; i++) if (v->s[i].cov || v->only[0]) total += v->s[i].miss;
	}
	if (total < looked) total = looked;
	/* The body, laid out by rules (src/ds/flex.h): the newest's square at
	 * the page's left side, and the six before it three by two in what is
	 * left, spread to its right side */
	{
		const int root = scp_body(&F, &pf, DS_ROW, SCP_CELL_GAP, 0);
		const int grid_w = pf.body.w - SCP_BIG - SCP_CELL_GAP;
		int grid, k;

		sq = ds_flex_add(&F, root, &(ds_box){ .w = SCP_BIG, .h = SCP_BIG });
		grid = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .wrap = true, .w = grid_w,
		                                        .gap = (grid_w - 3 * SCP_CELL) / 2, .line_gap = SCP_CELL_GAP });
		for (k = 0; k < 6; k++) {
			int id = ds_flex_add(&F, grid, &(ds_box){ .w = SCP_CELL, .h = SCP_CELL });

			if (!k) cell0 = id;
		}
		ds_flex_solve(&F);
		for (k = 0; k < 6; k++) {
			SDL_Rect q = ds_flex_rect(&F, cell0 + k);

			cell[k] = (SDL_FRect){ (float)q.x, (float)q.y, (float)q.w, (float)q.h };
		}
		{   /* the newest inside its square by its lit frame's width */
			SDL_Rect q = ds_flex_rect(&F, sq);

			big = (SDL_FRect){ (float)(q.x + 8), (float)(q.y + 8), (float)(q.w - 16), (float)(q.h - 16) };
		}
	}
	ui_fit_text(ft, v->p.now, fit, sizeof fit, pf.x1 - hx);
	ui_text(a->r, ft, fit, hx, ui_cap_y(ft, DS_PAGE_TITLE_Y), -1, UI_TEXT);
	si = scp_find(v, v->p.folder_now);
	scp_icon_draw(v, si, hx + 26, DS_PAGE_SUB_Y, 52, 255);
	if (v->p.src != ART_SRC_NONE) ds_badge(a->r, hx + 80, DS_PAGE_SUB_Y, DS_BADGE, scp_src_glyph(v->p.src), false, 255);
	snprintf(cnt, sizeof cnt, "%d / %d", looked, total);
	{
		int w = pf.x1 - hx - 120 - ui_text_width(fs, cnt) - 24;

		ds_bar(a->r, hx + 120, DS_PAGE_SUB_Y - UI_BAR_H / 2, w,
		       total > 0 ? (int)((long long)w * (looked > total ? total : looked) / total) : 0, 255);
	}
	ui_text(a->r, fs, cnt, pf.x1, ui_cap_y(fs, DS_PAGE_SUB_Y), 1, UI_TEXT_DIM);
	/* The six before the newest, three by two; the one that was big comes
	 * in first */
	for (m = 6; m >= 0; m--) {
		int k = m + 1;
		SDL_FRect to = cell[m < 6 ? m : 5];
		SDL_FRect from = m == 0 ? big : cell[m - 1];

		if (k >= v->nring) continue;
		if (m == 6) scp_cover_draw(a, &v->ring[k], from, (Uint8)(255 * (1.0f - e)), false, true);
		else scp_cover_draw(a, &v->ring[k], scp_lerp(from, to, e), 255, false, true);
	}
	if (v->nring) {
		SDL_FRect b = { big.x + big.w * 0.06f * (1 - e), big.y + 40 * (1 - e) + big.h * 0.06f * (1 - e),
		                big.w * (0.88f + 0.12f * e), big.h * (0.88f + 0.12f * e) };

		scp_cover_draw(a, &v->ring[0], b, (Uint8)(255 * e), true, true);
	}
	scp_time(scp_eta(v, now, looked, total), false, eta, sizeof eta);
	scp_stats(a, ZYPEROS_SCREEN_W / 2, pf.line_y, v->p.found, v->p.snaps, -1, eta);
	ds_page_foot(a->r, &pf, K, L, 2, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

/* Done: what came, as a wall across the page; the counts as marks; the room
 * said plainly over the keys */
#define SCP_WALL_H   140   /* a cover's height in the wall */
#define SCP_WALL_GAP 20    /* between them, and the wall to the counts */
static void scp_done_draw(scp_view *v)
{
	app *a = v->a;
	TTF_Font *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int m, i, nv = 0, n, wall, said;
	unsigned long long room = 0;
	char line[160], sz[32];
	static ds_flex F;
	SDL_Rect wr, sr;
	float gap = SCP_WALL_GAP, cw, ch = SCP_WALL_H;
	const char *K[2] = { "A", "B" }, *L[2] = { T(S_SCRAPER_KEY_AGAIN), T(S_SCRAPER_KEY_CLOSE) };

	ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SCRAPER_ALL_DONE), v->problem, UI_GLYPH_COUNT, 0);
	/* The body, laid out by rules (src/ds/flex.h): the wall, two rows of
	 * seven, and the counts' line under it; the covers in the wall share
	 * its width (in fractions of a unit, as they always have) */
	{
		const int root = scp_body(&F, &pf, DS_COL, SCP_WALL_GAP, 0);

		wall = ds_flex_add(&F, root, &(ds_box){ .h = 2 * SCP_WALL_H + SCP_WALL_GAP });
		said = ds_flex_add(&F, root, &(ds_box){ .h = DS_LINE_META });
		ds_flex_solve(&F);
		wr = ds_flex_rect(&F, wall);
		sr = ds_flex_rect(&F, said);
		cw = (wr.w - 6 * gap) / 7;
	}
	n = v->nring > 14 ? 14 : v->nring;
	for (m = 0; m < n; m++) {
		int k = n - 1 - m;     /* oldest first */

		scp_cover_draw(a, &v->ring[k], (SDL_FRect){ wr.x + (m % 7) * (cw + gap), wr.y + (m / 7) * (ch + gap),
		                                            cw, ch }, 255, false, false);
	}
	scp_stats(a, ZYPEROS_SCREEN_W / 2, sr.y + sr.h / 2, v->done_found, v->done_videos, v->done_missing, NULL);
	for (i = 0; i < v->n; i++) if (v->s[i].vid) { nv++; room += v->s[i].vroom; }
	if (nv) {
		/* no account: no video came, so say why instead of "0 KB" */
		if (!v->ss_in) snprintf(line, sizeof line, "%s: %s", T(S_SCRAPER_PAGE_VIDEOS), T(S_SYSMENU_SN_NEEDS_SS));
		else {
			scp_mb(room / 1e6, sz, sizeof sz);
			snprintf(line, sizeof line, TN(S_SCRAPER_VIDEOS_ON_CARD, nv), nv, sz);
		}
		ui_text(a->r, fs, line, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, pf.line_y), 0, UI_TEXT_SOFT);
	}
	ds_page_foot(a->r, &pf, K, L, 2, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

static void scp_draw(scp_view *v, unsigned now)
{
	switch (v->where) {
	case SCP_SYSTEMS: scp_systems_draw(v); break;
	case SCP_RUN:     scp_run_draw(v, now); break;
	case SCP_DONE:    scp_done_draw(v); break;
	default:         scp_plan_draw(v); break;
	}
}

/* The page under a panel opened from it (g_base_page) */
static void scp_base(app *a, void *ctx)
{
	(void)a;
	scp_draw(ctx, anim_now());
}

static void scp_test(scp_view *v, app *a);

/* ---- the questions -------------------------------------------------------- */

/* Tiles over the page, as Computer Connected's (PANEL_TILES), or a list
 * (PANEL_LIST), each tile with its mark: the one picked, or -1 */
static int scp_ask(scp_view *v, int kind, const char *heading, const char *sub, menu_row *tiles,
                   const int *marks, int n, int sel)
{
	app *a = v->a;
	key_hint hh[3] = { { KEYHINT_LEFT_RIGHT, T(S_KEYHINT_MOVE) }, { "A", T(S_KEYHINT_CHOOSE) },
	                   { "B", T(S_KEYHINT_CLOSE) } };
	char legend[256];
	int lv, choice = -2;
	float k;

	if (kind == PANEL_LIST) snprintf(legend, sizeof legend, "%s", list_hints(T(S_KEYHINT_CLOSE)));
	else keyhints_pack(hh, 3, legend, sizeof legend);
	/* A line under the heading takes the panel's three notes: it, none
	 * under the tiles, and the keys */
	if (sub) {
		tiles[n] = MENU_NOTE(sub);
		tiles[n + 1] = MENU_NOTE(NULL);
		tiles[n + 2] = MENU_NOTE(legend);
	} else {
		tiles[n] = MENU_NOTE(legend);
	}
	lv = panel_open();
	while (choice == -2) {
		plat_input_poll(&a->in);
		scp_test(v, a);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; choice = -1; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { choice = -1; break; }
		if (menu_leaving(a) || a->in.pressed[IN_BACK]) choice = -1;
		{
			/* Left and Right along tiles; Up and Down too, for a list */
			bool back = in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_UP);
			bool on = in_repeat(&a->in, IN_RIGHT) || in_repeat(&a->in, IN_DOWN);

			if (back || on) {
				moved_sound(a, back ? IN_LEFT : IN_RIGHT);
				sel = (sel + (back ? -1 : 1) + n) % n;
			}
		}
		if (a->in.pressed[IN_ACCEPT] && v->ask_switch >= 0 && sel == v->ask_switch &&
		    v->ask_switch_fn) {
			/* A choice of two switches where it is: the panel stays */
			moved_sound(a, IN_RIGHT);
			v->ask_switch_fn(v, tiles);
		} else if (a->in.pressed[IN_ACCEPT]) { uisound_play(DS_SOUND_ACCEPT); choice = sel; }
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		scp_draw(v, anim_now());
		ui_scrim_k(a->r, 0, k);
		tiles_show(a, kind, heading, tiles, n + (sub ? 3 : 1), marks, n, sel);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return choice;
}

/* Videos for's choices: for each, the videos still to fetch and their
 * size, added up over the systems with videos on - which the line under
 * the heading says, so a system's own figure (Systems' foot) is not taken
 * for the whole (2026-11-14, on the Pixel 2: "in the videos for popup it's
 * saying 1.9gb for all games, when collapsed it says 883mb") */
static void scp_for_rows(const scp_view *v, menu_row *tiles, char *sub, size_t subn)
{
	static char val[SNAPS_FOR_COUNT][96];
	int i, k, nvid = 0;

	tiles[0] = (menu_row){ T(S_SCRAPER_ALL_GAMES), val[0], true };
	tiles[1] = (menu_row){ T(S_LIBRARY_FAVORITES), val[1], true };
	tiles[2] = (menu_row){ T(S_SCRAPER_PLAYED), val[2], true };
	for (i = 0; i < v->n; i++) if (v->s[i].vid) nvid++;
	for (k = 0; k < SNAPS_FOR_COUNT; k++) {
		int n = 0;
		char sz[32], tf[64];

		for (i = 0; i < v->n; i++) if (v->s[i].vid) n += v->s[i].vneed[k];
		scp_mb(n * v->mb_video, sz, sizeof sz);
		scp_nbsp(sz, sizeof sz);
		snprintf(tf, sizeof tf, T(S_SCRAPER_TO_FETCH), sz);
		/* a number and its unit never apart on the tile's two lines */
		snprintf(val[k], sizeof val[k], "%d\xc2\xa0\xc2\xb7\xc2\xa0%s", n, tf);
	}
	if (nvid) snprintf(sub, subn, TN(S_SCRAPER_VIDEOS_FOR_SYSTEMS, nvid), nvid);
	else snprintf(sub, subn, "%s", T(S_SCRAPER_VIDEOS_OFF_ALL));
}

/* Videos for's marks: a screen with a play triangle for all games, a heart
 * for the favorites, a play triangle for the ones played; and Settings':
 * the two sources by their own marks, never their logos - a cloud for
 * ScreenScraper, a grid for SteamGridDB - and the videos' screen */
static const int SCP_FOR_MARKS[SNAPS_FOR_COUNT] = { UI_GLYPH_FILM, UI_GLYPH_HEART, UI_GLYPH_PLAY };
static const int SCP_SETTINGS_MARKS[4] = { UI_GLYPH_CLOUD, UI_GLYPH_GRID, UI_GLYPH_FILM, UI_GLYPH_BOOK };

/* Which games get videos */
static void scp_videos_for(scp_view *v)
{
	menu_row tiles[SNAPS_FOR_COUNT + 3];
	char sub[128];
	int c;

	scp_for_rows(v, tiles, sub, sizeof sub);
	c = scp_ask(v, PANEL_TILES, T(S_SCRAPER_VIDEOS_FOR), sub, tiles, SCP_FOR_MARKS, SNAPS_FOR_COUNT, snaps_for());
	if (c >= 0) snaps_set_for(c);
}

/* Names and Details' row, switched by A where it is (scp_ask) */
static void scp_names_switch(scp_view *v, menu_row *tiles)
{
	scp_names_set(!scp_names_on());
	tiles[3].value = scp_names_on() ? T(S_SCRAPER_ON) : T(S_SCRAPER_OFF);
	scp_count(v);
}

/* Settings: the two accounts and Video Snaps, each its page as before, and
 * Names and Details, a choice of two that A switches in place (1.0.6) */
static void scp_settings(scp_view *v)
{
	static char ss[64], sg[32], vs[48];
	menu_row tiles[5] = {
		{ "ScreenScraper", ss, true },
		{ "SteamGridDB", sg, true },
		{ T(S_SCRAPER_PAGE_VIDEOS), vs, true },
		{ T(S_SCRAPER_NAMES_DETAILS), scp_names_on() ? T(S_SCRAPER_ON) : T(S_SCRAPER_OFF), true },
	};
	char sz[24];
	unsigned long long room = 0;
	int i, c;

	if (!ss_have_dev()) snprintf(ss, sizeof ss, "%s", T(S_SYSMENU_NOT_IN_BUILD));
	else if (ss_signed_in()) snprintf(ss, sizeof ss, T(S_SYSMENU_SC_LINKED_AS), ss_user());
	else snprintf(ss, sizeof ss, "%s", T(S_SYSMENU_SC_LINK));
	if (!sgdb_have_key()) snprintf(sg, sizeof sg, "%s", T(S_SYSMENU_SC_LINK));
	else if (sgdb_key_state() == SGDB_REFUSED) snprintf(sg, sizeof sg, "%s", T(S_SCRAPER_KEY_REFUSED));
	else sgdb_key_masked(sg, sizeof sg);
	for (i = 0; i < v->n; i++) room += v->s[i].vroom;
	sys_menu_bytes(room, sz, sizeof sz);
	snprintf(vs, sizeof vs, "%s \xc2\xb7 %s", snaps_play() ? T(S_SCRAPER_ON) : T(S_SCRAPER_OFF), sz);
	v->ask_switch = 3;
	v->ask_switch_fn = scp_names_switch;
	c = scp_ask(v, PANEL_LIST, T(S_HOME_SETTINGS), NULL, tiles, SCP_SETTINGS_MARKS, 4, 0);
	v->ask_switch = -1;
	if (c == 0) ss_page(v->a);
	else if (c == 1) sgdb_screen(v->a);
	else if (c == 2) snaps_screen(v->a);
	if (c >= 0) scp_count(v);
}

/* ---- the run --------------------------------------------------------------- */

static void scp_ring_free(scp_view *v)
{
	int i;

	for (i = 0; i < v->nring; i++)
		if (v->ring[i].tex) SDL_DestroyTexture(v->ring[i].tex);
	v->nring = 0;
}

static void scp_start(scp_view *v)
{
	int i, k;

	scp_ring_free(v);
	memset(&v->cfg, 0, sizeof v->cfg);
	for (i = 0; i < v->a->sys.count; i++) {
		const system_cfg *sc = &v->a->sys.systems[i];

		k = scp_find(v, sc->folder);
		if (k < 0 || !(v->only[0] || (v->s[k].cov && (v->in_cov || (v->in_names && scp_names_on()))) ||
		               (v->s[k].vid && v->in_vid)))
			continue;
		if (v->cfg.count < CFG_MAX_SYSTEMS) v->cfg.systems[v->cfg.count++] = *sc;
	}
	{
		/* the time left's guesses, until the run's own pace says */
		scp_need need;

		scp_needs(v, &need);
		v->s_cov = v->ss_in ? SCP_S_SS : SCP_S_LR;
		v->s_det = SCP_S_SS + (need.looks ? (double)need.videos / need.looks : 0.0) * SCP_S_VIDEO;
		v->est_looks = need.looks;
		v->pace_det = false;
		v->pace_t0 = anim_now();
		v->pace_n0 = 0;
		v->room_at = 0;
	}
	g_art_sys = v->cfg;
	snprintf(g_art_only, sizeof g_art_only, "%s", v->only);
	for (g_art_total = 0, i = 0; v->in_cov && i < v->n; i++) if (v->s[i].cov || v->only[0]) g_art_total += v->s[i].miss;
	g_snap_sys = &g_art_sys;
	/* The kinds this run gets: covers by the Systems switch, or none; names
	 * by that same switch even with the covers out; videos, or none */
	art_covers_rule(v->in_cov ? art_lane_rule : art_lane_none);
	art_names(scp_names_on() && v->in_names);
	art_names_rule(v->in_cov ? NULL : art_lane_rule);
	g_snap_run_off = !v->in_vid;
	/* Its own network slot (src/dlq.h): an account's answer after a game,
	 * on the screens' slot, goes on beside it */
	ART_LANE.card = P_ROMS;
	if (!dlq_start(DLQ_ART, &ART_LANE, art_lane_begin)) {
		art_covers_rule(NULL);
		art_names_rule(NULL);
		g_snap_run_off = false;
		g_snap_sys = NULL;
		return;
	}
	memset(&v->p, 0, sizeof v->p);
	v->seen = 0;
	v->problem[0] = '\0';
	v->where = SCP_RUN;
}

/* Back on a run that went on behind the screens: its view from now, its
 * pace counted again from here */
static void scp_rejoin(scp_view *v)
{
	scp_need need;

	scp_needs(v, &need);
	v->s_cov = v->ss_in ? SCP_S_SS : SCP_S_LR;
	v->s_det = SCP_S_SS + (need.looks ? (double)need.videos / need.looks : 0.0) * SCP_S_VIDEO;
	v->est_looks = need.looks;
	v->pace_det = false;
	v->pace_t0 = anim_now();
	art_status(&v->p);
	v->pace_n0 = v->p.details ? v->p.dlooked : v->p.looked;
	v->seen = v->p.found_seq;
	v->problem[0] = '\0';
	v->where = SCP_RUN;
}

/* What arrived: a new tile in the ring, its picture asked off the frame */
static void scp_take_arrival(scp_view *v)
{
	scp_cover *c;

	if (v->p.found_seq == v->seen) return;
	v->seen = v->p.found_seq;
	if (v->nring == SCP_RING) {
		if (v->ring[SCP_RING - 1].tex) SDL_DestroyTexture(v->ring[SCP_RING - 1].tex);
		v->nring--;
	}
	memmove(&v->ring[1], &v->ring[0], (size_t)v->nring * sizeof v->ring[0]);
	v->nring++;
	c = &v->ring[0];
	memset(c, 0, sizeof *c);
	c->seq = v->seen;
	c->src = v->p.found_src;
	c->video = v->p.found_video;
	snprintf(c->folder, sizeof c->folder, "%s", v->p.found_folder);
	if (v->p.found_path[0]) mt_ask((int)c->seq, v->p.found_path, 360);
	else v->land_at = anim_now();     /* a video with no cover: lands as a tile */
}

static bool scp_take_pictures(scp_view *v)
{
	SDL_Surface *s;
	int key, i;
	bool any = false;

	while (mt_take(&key, &s)) {
		for (i = 0; i < v->nring; i++) {
			if (v->ring[i].seq != (unsigned)key || v->ring[i].tex || !s) continue;
			v->ring[i].tex = SDL_CreateTextureFromSurface(v->a->r, s);
			v->ring[i].w = s->w;
			v->ring[i].h = s->h;
			if (i == 0) v->land_at = anim_now();
			any = true;
		}
		if (s) SDL_FreeSurface(s);
	}
	return any;
}

/* The run is over - by its end, stopped, or the card nearly full; here or
 * behind the screens - and the page shows what it did, from what the lane
 * kept as it ended (art_lane_done) */
static void scp_finish(scp_view *v)
{
	v->p = g_art_last;
	v->done_found = v->p.found;
	v->done_videos = v->p.snaps;
	v->done_missing = v->p.missing;
	if (dlq_last(DLQ_ART) == DLQ_FULL) snprintf(v->problem, sizeof v->problem, "%s", T(S_SCRAPER_NO_ROOM));
	else snprintf(v->problem, sizeof v->problem, "%s", v->p.problem);
	g_scp_seen = dlq_ends(DLQ_ART);
	free_all_textures(v->a);
	{
		int i;

		/* The room said plainly at the end is the card's own, now */
		for (i = 0; i < v->n; i++) v->s[i].vroom = snaps_bytes(P_ROMS, v->s[i].folder);
	}
	v->where = SCP_DONE;
}

/* ---- a test of the page on a host -------------------------------------------
 * ZYPEROS_SCRAPER_TEST: the launcher opens the page at once and plays a
 * script, commas between its steps: a button (A B X Y START UP DOWN LEFT
 * RIGHT), wait=MS, run=MS (until the run is over, or MS), shot=PATH (the
 * page as drawn now). Each step said on stderr with the page's state; the
 * launcher quits at the script's end. The real loop, network and all: what
 * a shot's fixture cannot show. */
static const char *g_scp_test;
static unsigned g_scp_until, g_scp_run_until;

static void scp_test(scp_view *v, app *a)
{
	static const struct { const char *name; in_button b; } BTN[] = {
		{ "A", IN_ACCEPT }, { "B", IN_BACK }, { "X", IN_X }, { "Y", IN_Y }, { "START", IN_START },
		{ "UP", IN_UP }, { "DOWN", IN_DOWN }, { "LEFT", IN_LEFT }, { "RIGHT", IN_RIGHT },
	};
	char tok[512];
	unsigned now = plat_now_ms();
	size_t n, i;

	if (!g_scp_test) return;
	if (now < g_scp_until) return;
	if (g_scp_run_until && v->where == SCP_RUN && now < g_scp_run_until) return;
	g_scp_run_until = 0;
	if (!*g_scp_test) { want_quit = 1; return; }
	n = strcspn(g_scp_test, ",");
	snprintf(tok, sizeof tok, "%.*s", (int)n, g_scp_test);
	g_scp_test += n + (g_scp_test[n] == ',');
	g_scp_until = now + 400;
	for (i = 0; i < sizeof BTN / sizeof BTN[0]; i++)
		if (!strcmp(tok, BTN[i].name)) a->in.pressed[BTN[i].b] = true;
	if (!strncmp(tok, "wait=", 5)) g_scp_until = now + (unsigned)atoi(tok + 5);
	if (!strncmp(tok, "run=", 4)) g_scp_run_until = now + (unsigned)atoi(tok + 4);
	if (!strncmp(tok, "shot=", 5)) {
		SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, 32,
		                                                  SDL_PIXELFORMAT_RGBA32);

		scp_draw(v, anim_now());
		if (out) {
			SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, out->pixels, out->pitch);
			IMG_SavePNG(out, tok + 5);
			SDL_FreeSurface(out);
		}
	}
	fprintf(stderr, "scraper-test: %s where=%d systems=%d focus=%d arrived=%d found=%d looked=%d details=%d\n",
	        tok, v->where, v->n, v->focus, v->nring, v->p.found, v->p.looked, v->p.details);
}

/* ---- the page -------------------------------------------------------------- */

static void scp_page(app *a, const char *only)
{
	scp_view *v = calloc(1, sizeof *v);
	int base;
	unsigned beat = 0;
	bool dirty = true;
	art_progress last;

	if (!v) return;
	v->a = a;
	v->in_cov = v->in_vid = v->in_names = true;
	if (only) snprintf(v->only, sizeof v->only, "%s", only);
	scp_count(v);
	/* A run behind the screens is shown where it is; one that ended while
	 * the page was closed, as done (src/dlq.h) */
	if (dlq_running(DLQ_ART)) scp_rejoin(v);
	else if (dlq_ends(DLQ_ART) != g_scp_seen) scp_finish(v);
	g_art_after = false;
	dlq_page(DLQ_ART, true);
	mt_start();
	base = panel_base();
	g_base_page = scp_base;
	g_base_ctx = v;
	memset(&last, 0, sizeof last);
	g_scp_test = getenv("ZYPEROS_SCRAPER_TEST");
	while (!want_quit && a->running) {
		unsigned now = anim_now();
		bool anim;

		plat_input_poll(&a->in);
		scp_test(v, a);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			power_off(a);              /* the run with everything else */
			break;
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		for (int b = 0; b < IN_COUNT; b++) if (a->in.pressed[b] || a->in.down[b]) dirty = true;
		if (a->in.pressed[IN_MENU]) {
			menu_leaving(a);           /* a run goes on behind the screens */
			break;
		}

		if (v->where == SCP_RUN) {
			/* The lane steps here once a frame, as the run always did
			 * here, and behind every screen once the page has closed. The
			 * card's room is the queue's rule now (DLQ_FLOOR). */
			dlq_tick();
			if (!dlq_running(DLQ_ART)) {
				scp_finish(v);
				dirty = true;
			} else {
				art_status(&v->p);
				scp_take_arrival(v);
				if (scp_take_pictures(v)) dirty = true;
				if (v->p.found_seq != last.found_seq || v->p.looked != last.looked ||
				    v->p.dlooked != last.dlooked || v->p.src != last.src || strcmp(v->p.now, last.now))
					dirty = true;
				last = v->p;
				if (a->in.pressed[IN_BACK]) {
					/* Continue in background: the page closes and the run
					 * goes on, the status corner's ring showing it */
					uisound_play(DS_SOUND_BACK);
					break;
				}
				if (a->in.pressed[IN_Y]) {
					uisound_play(DS_SOUND_BACK);
					dlq_stop(DLQ_ART);
					scp_finish(v);
					dirty = true;
				}
			}
		} else if (a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			if (v->where == SCP_SYSTEMS) { v->where = SCP_PLAN; dirty = true; }
			else break;
		} else if (v->where == SCP_SYSTEMS && v->n) {
			in_button b = in_repeat(&a->in, IN_LEFT) ? IN_LEFT : in_repeat(&a->in, IN_RIGHT) ? IN_RIGHT
			            : in_repeat(&a->in, IN_UP) ? IN_UP : in_repeat(&a->in, IN_DOWN) ? IN_DOWN : IN_NONE;
			scp_sys *f = &v->s[v->focus];

			if (b != IN_NONE) {
				int to = v->focus + (b == IN_LEFT ? -1 : b == IN_RIGHT ? 1 : b == IN_UP ? -SCP_COLS : SCP_COLS);

				if (to >= 0 && to < v->n) { moved_sound(a, b); v->focus = to; }
			}
			if (a->in.pressed[IN_ACCEPT]) {
				uisound_play(DS_SOUND_ACCEPT);
				f->cov = !f->cov;
				snaps_set_covers(f->folder, f->cov);
			} else if (a->in.pressed[IN_X]) {
				uisound_play(DS_SOUND_ACCEPT);
				f->vid = !f->vid;
				snaps_set_get(f->folder, f->vid);
			} else if (a->in.pressed[IN_Y]) {
				scp_videos_for(v);
				dirty = true;
			}
		} else if (v->where == SCP_PLAN || v->where == SCP_DONE) {
			if (a->in.pressed[IN_ACCEPT]) {
				scp_need need;

				if (v->where == SCP_DONE) scp_count(v);
				scp_needs(v, &need);
				if (!v->net) {
					uisound_play(DS_SOUND_DENY);
					result_message(a, scp_title(v), MSG_NEEDS_WIFI);
				} else if (!scp_fits(v, &need)) {
					/* not started: the plan's line says it in **warning**,
					 * and Y's videos or Storage make the room */
					uisound_play(DS_SOUND_DENY);
					result_message(a, scp_title(v), T(S_SCRAPER_NO_ROOM));
				} else {
					uisound_play(DS_SOUND_ACCEPT);
					scp_start(v);
				}
			} else if (a->in.pressed[IN_Y] && v->where == SCP_PLAN && !v->only[0]) {
				uisound_play(DS_SOUND_ACCEPT);
				v->where = SCP_SYSTEMS;
			} else if (a->in.pressed[IN_START] && v->where == SCP_PLAN) {
				scp_settings(v);
				dirty = true;
			}
		}
		if (g_menu_closing) break;

		/* A frame when something changed, a cover is landing, or once a
		 * second for the clock; between, the loop waits for a button */
		anim = v->where == SCP_RUN && v->nring && now - v->land_at < SCP_LAND + 40;
		if (now - beat >= 1000) { beat = now; dirty = true; }
		if (!dirty && !anim) {
			plat_input_wait(v->where == SCP_RUN ? 16 : 80);
			continue;
		}
		dirty = false;
		tick_tint(a);
		tick_theme(a);
		scp_draw(v, now);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	/* MENU's Scraper row says what is still missing, counted as the page
	 * closes on the systems with covers on (and again as a run that goes on
	 * behind the screens ends) */
	if (!v->only[0]) {
		systems_cfg *on = calloc(1, sizeof *on);
		int i, k;

		for (i = 0; on && i < v->n; i++) {
			for (k = 0; k < a->sys.count && strcmp(a->sys.systems[k].folder, v->s[i].folder); k++) {}
			if (k < a->sys.count && on->count < CFG_MAX_SYSTEMS) on->systems[on->count++] = a->sys.systems[k];
		}
		if (on) scp_menu_value(on, NULL);
		free(on);
	}
	dlq_page(DLQ_ART, false);
	mt_forget();
	mt_stop();
	mm_close(base, anim_now(), 0, g_menu_closing);
	g_base_page = NULL;
	g_base_ctx = NULL;
	scp_ring_free(v);
	for (int i = 0; i < v->n; i++) if (v->s[i].icon) SDL_DestroyTexture(v->s[i].icon);
	free(v);
}

/* The page, from MENU or a system's Box Art, and every scrape's exit: the
 * shelves' names from the library at once, with no restart (the run may
 * have named games). tools/title-refresh-check.c runs this as it is, with
 * the page stubbed. */
void scraper_page(app *a, const char *only)
{
	scp_page(a, only);
	refresh_game_titles(a);
	/* and the focused game's snap looked for again: the run may have
	 * brought it, under the shelf's own focus */
	snapshelf_media_changed();
}

menu_result scraper_screen(app *a)
{
	scraper_page(a, NULL);
	return MENU_STAY;
}

/* ---- for the Downloads page (src/dl_screen.h) -----------------------------
 * The page's own plan and run, with the kinds the Downloads page has in: a
 * view counted as the page counts it, never drawn. */
static scp_view *scp_plan_view(app *a, scraper_kinds k)
{
	scp_view *v = calloc(1, sizeof *v);

	if (!v) return NULL;
	v->a = a;
	v->in_cov = k.covers;
	v->in_vid = k.videos;
	v->in_names = k.names;
	scp_count(v);
	return v;
}

static void scp_plan_free(scp_view *v)
{
	int i;

	for (i = 0; i < v->n; i++) if (v->s[i].icon) SDL_DestroyTexture(v->s[i].icon);
	free(v);
}

void scraper_plan_of(app *a, scraper_kinds k, scraper_counts *out)
{
	scp_view *v = scp_plan_view(a, k);
	scp_need need;
	int i;

	memset(out, 0, sizeof *out);
	if (!v) return;
	scp_needs(v, &need);
	out->covers = need.covers;
	out->videos = need.videos;
	out->names = need.names;
	out->looks = need.looks;
	out->bytes = need.bytes;
	out->secs = need.secs;
	out->net = v->net;
	out->ss_in = v->ss_in;
	out->fits = scp_fits(v, &need);
	for (i = 0; i < v->n; i++) {
		if (v->s[i].cov) out->cov_systems++;
		if (v->s[i].vid) out->vid_systems++;
	}
	scp_plan_free(v);
}

bool scraper_run(app *a, scraper_kinds k)
{
	scp_view *v;
	scp_need need;
	bool ok;

	if (dlq_running(DLQ_ART)) return true;
	if (!(v = scp_plan_view(a, k))) return false;
	scp_needs(v, &need);
	ok = v->net && scp_fits(v, &need) && (need.covers || need.videos || need.looks);
	if (ok) scp_start(v);
	ok = ok && dlq_running(DLQ_ART);
	scp_plan_free(v);
	return ok;
}

bool scraper_names_on(void) { return scp_names_on(); }
void scraper_names_set(bool on) { scp_names_set(on); }

void scraper_time_text(double secs, char *out, size_t n)
{
	scp_time(secs, true, out, n);
}

/* ---- a shot's page (--scraper-page STATE [T]) ------------------------------
 * The real drawing on stand-in systems, without a card or a run: 0 the plan
 * with videos off, 1 with three systems' on, 2 with one, 3 Systems, 4 a long
 * name in focus, 5 Videos for, 6 Settings, 7 the run at T ms, 8 done, 9
 * done with one system's videos, 10 offline, 11 a system's Box Art, 12
 * done with videos asked for and no ScreenScraper account, 13 the plan
 * with too little room on the card, 14 Videos for with one system's
 * videos on, 15 done after the card filled. Covers
 * from ZYPEROS_LOOK_COVERS (paths split by ':') and their systems'
 * indices from ZYPEROS_LOOK_SYS, when a shot has them. */
int shot_scraper_page = -1, shot_scraper_page_t = 7180;

/* What a run says it is doing, past a cover's game, and why it passed a
 * system over or stopped a source (art_status's now and problem): shot as
 * --scraper-page 17 K (the run, K one of the three lines) and 16 K (done,
 * K one of the reasons) */
static const str_id SCP_SHOT_WHY[] = {
	S_ART_WHY_NO_FOLDER, S_ART_WHY_NOT_IN_TABLE, S_ART_WHY_NO_CURL, S_ART_WHY_INDEX_FAILED,
	S_ART_WHY_INDEX_BAD,   /* the first five a system's own: "NES: ..." */
	S_ART_SS_QUOTA, S_ART_SS_TOO_MANY, S_ART_SS_BUSY, S_ART_SS_NO_ANSWER,
	S_ART_SG_REFUSED, S_ART_SG_BUSY, S_ART_SG_NO_ANSWER,
};

static void scp_fixture(scp_view *v, app *a, int state, int t)
{
	static const struct { const char *folder, *name, *key; bool cov; int miss, vneed; unsigned long long room; } SYS[9] = {
		{ "NES", "NES", "NES", true, 120, 80, 0 },
		{ "SNES", "SNES", "SNES", true, 96, 60, 0 },
		{ "Game Boy", "Game Boy", "GB", true, 41, 30, 0 },
		{ "Game Boy Advance", "Game Boy Advance", "GBA", true, 58, 44, 0 },
		{ "Genesis", "Genesis", "GENESIS", true, 77, 70, 0 },
		{ "PlayStation", "PlayStation", "PS1", true, 92, 85, 0 },
		{ "Nintendo 64", "Nintendo 64", "N64", true, 39, 30, 0 },
		{ "Neo Geo Pocket Color", "Neo Geo Pocket Color", "NGPC", false, 12, 10, 0 },
		{ "Arcade", "Arcade", "ARCADE", false, 28, 25, 0 },
	};
	int why = -1, pass = -1;
	unsigned vid;

	if (state == 16) why = t, state = 8;
	else if (state == 17) pass = t, state = 7, t = 7180;
	vid = state == 0 || state == 10 ? 0u : state == 2 || state == 9 || state == 14 ? 1u
	             : (1u << 0) | (1u << 1) | (1u << 4);
	int i;

	memset(v, 0, sizeof *v);
	v->a = a;
	v->fixture = true;
	v->in_cov = v->in_vid = v->in_names = true;
	v->net = state != 10;
	v->ss_in = state != 12;     /* videos asked for, no account */
	v->mb_video = SCP_MB_PER_VIDEO;
	v->mb_cover = SCP_MB_PER_COVER;
	v->free = state == 13 ? 400000000ULL : 11200000000ULL;
	for (i = 0; i < 9; i++) {
		scp_sys *s = &v->s[v->n++];

		snprintf(s->folder, sizeof s->folder, "%s", SYS[i].folder);
		snprintf(s->name, sizeof s->name, "%s", SYS[i].name);
		snprintf(s->key, sizeof s->key, "%s", SYS[i].key);
		s->cov = SYS[i].cov;
		s->miss = SYS[i].miss;
		s->vneed[SNAPS_FOR_ALL] = SYS[i].vneed;
		s->vneed[SNAPS_FOR_FAVORITES] = SYS[i].vneed / 8;
		s->vneed[SNAPS_FOR_PLAYED] = SYS[i].vneed * 3 / 10;
		s->names = SYS[i].vneed / 2;
		for (int w = 0; w < SNAPS_FOR_COUNT; w++) s->both[w] = s->vneed[w] / 4;
		s->vid = (vid >> i) & 1u;
	}
	if (state == 11) {
		v->s[0] = v->s[1];
		v->n = 1;
		snprintf(v->only, sizeof v->only, "%s", v->s[0].folder);
		v->s[0].vid = true;
	}
	v->focus = state == 4 ? 7 : 4;
	v->where = state == 3 || state == 4 || state == 5 || state == 14 ? SCP_SYSTEMS : state == 7 ? SCP_RUN
	         : state == 8 || state == 9 || state == 12 || state == 15 ? SCP_DONE : SCP_PLAN;
	if (state == 15) snprintf(v->problem, sizeof v->problem, "%s", T(S_SCRAPER_NO_ROOM));
	if (state == 7 || state == 8 || state == 9 || state == 12 || state == 15) {
		const char *env = getenv("ZYPEROS_LOOK_COVERS"), *sy = getenv("ZYPEROS_LOOK_SYS");
		char buf[8192], *p, *save = NULL, names[SCP_RING][96];
		int sys[SCP_RING], nc = 0, L, k;

		memset(sys, 0, sizeof sys);
		for (k = 0; sy && *sy && k < SCP_RING; k++) {
			sys[k] = atoi(sy);
			sy = strchr(sy, ',');
			if (sy) sy++;
		}
		snprintf(buf, sizeof buf, "%s", env ? env : "");
		L = state == 7 ? t / 1100 + 1 : SCP_RING;
		for (p = strtok_r(buf, ":", &save); p && nc < SCP_RING; p = strtok_r(NULL, ":", &save)) {
			const char *b = strrchr(p, '/');
			char *dot;

			snprintf(names[nc], sizeof names[nc], "%s", b ? b + 1 : p);
			if ((dot = strrchr(names[nc], '.'))) *dot = '\0';
			if (nc < L) {
				/* newest first: the cover found last at [0] */
				scp_cover *c;

				memmove(&v->ring[1], &v->ring[0], (size_t)v->nring * sizeof v->ring[0]);
				v->nring++;
				c = &v->ring[0];
				memset(c, 0, sizeof *c);
				c->tex = IMG_LoadTexture(a->r, p);
				if (c->tex) SDL_QueryTexture(c->tex, NULL, NULL, &c->w, &c->h);
				c->seq = (unsigned)nc + 1;
				c->src = nc % 5 == 3 ? ART_SRC_SG : nc % 3 == 1 ? ART_SRC_LR : ART_SRC_SS;
				snprintf(c->folder, sizeof c->folder, "%s", SYS[sys[nc] % 9].folder);
				c->video = ((vid >> (sys[nc] % 9)) & 1u) && nc % 2 == 0;
				snprintf(v->p.now, sizeof v->p.now, "%s", names[nc]);
				snprintf(v->p.folder_now, sizeof v->p.folder_now, "%s", SYS[sys[nc] % 9].folder);
				v->p.src = c->src;
				if (c->video) v->p.snaps++;
			}
			nc++;
		}
		v->p.looked = 37 + 2 * L;
		v->p.found = 31 + L;
		v->land_at = 1000000;
		v->shot_now = v->land_at + (unsigned)(state == 7 ? t % 1100 : 2000);
		{
			/* the time left from the guesses, as a run's first seconds */
			scp_need need;

			scp_needs(v, &need);
			v->s_cov = SCP_S_SS;
			v->s_det = SCP_S_SS + (need.looks ? (double)need.videos / need.looks : 0.0) * SCP_S_VIDEO;
			v->est_looks = need.looks;
			v->pace_t0 = v->shot_now;
			v->pace_n0 = v->p.looked;
		}
		v->done_found = 412;
		v->done_videos = 38;
		v->done_missing = 9;
		for (i = 0; i < 9; i++)
			if (v->s[i].vid) v->s[i].vroom = (unsigned long long)(v->s[i].vneed[SNAPS_FOR_ALL] * SCP_MB_PER_VIDEO * 1e6);
	}
	if (why >= 0 && why < (int)(sizeof SCP_SHOT_WHY / sizeof SCP_SHOT_WHY[0])) {
		if (why < 5) snprintf(v->problem, sizeof v->problem, T(S_ART_PROBLEM), "NES", T(SCP_SHOT_WHY[why]));
		else snprintf(v->problem, sizeof v->problem, "%s", T(SCP_SHOT_WHY[why]));
	}
	if (pass == 0) snprintf(v->p.now, sizeof v->p.now, T(S_ART_NOW_CATALOG), "NES");
	else if (pass == 1) snprintf(v->p.now, sizeof v->p.now, T(S_ART_NOW_NO_ART), "Tetris");
	else if (pass == 2) snprintf(v->p.now, sizeof v->p.now, T(S_ART_NOW_CHECKSUMS), "NES");
	if (pass >= 0) snprintf(v->p.folder_now, sizeof v->p.folder_now, "%s", "NES");
}

void scraper_page_shot(app *a)
{
	static scp_view v;
	int st = shot_scraper_page;

	scp_fixture(&v, a, st, shot_scraper_page_t);
	if (st == 5 || st == 14) {
		/* As scp_videos_for asks it */
		menu_row tiles[SNAPS_FOR_COUNT + 3];
		key_hint hh[3] = { { KEYHINT_LEFT_RIGHT, T(S_KEYHINT_MOVE) }, { "A", T(S_KEYHINT_CHOOSE) },
		                   { "B", T(S_KEYHINT_CLOSE) } };
		static char legend[256], sub[128];

		scp_for_rows(&v, tiles, sub, sizeof sub);
		keyhints_pack(hh, 3, legend, sizeof legend);
		tiles[SNAPS_FOR_COUNT] = MENU_NOTE(sub);
		tiles[SNAPS_FOR_COUNT + 1] = MENU_NOTE(NULL);
		tiles[SNAPS_FOR_COUNT + 2] = MENU_NOTE(legend);
		scp_draw(&v, v.shot_now);
		ui_scrim(a->r, 0);
		tiles_panel(a, PANEL_TILES, T(S_SCRAPER_VIDEOS_FOR), tiles, SCP_FOR_MARKS, SNAPS_FOR_COUNT + 3, 1,
		            PANEL_REST.panel);
	} else if (st == 6) {
		/* As scp_settings words them: a linked account, a key, videos on */
		static char ss[64], vs[48];
		menu_row tiles[5] = {
			{ "ScreenScraper", ss, true },
			{ "SteamGridDB", "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b", true },
			{ T(S_SCRAPER_PAGE_VIDEOS), vs, true },
			{ T(S_SCRAPER_NAMES_DETAILS), scp_names_on() ? T(S_SCRAPER_ON) : T(S_SCRAPER_OFF), true },
		};
		char sz[24];

		snprintf(ss, sizeof ss, T(S_SYSMENU_SC_LINKED_AS), "tarik");
		sys_menu_bytes(1400000000ull, sz, sizeof sz);
		snprintf(vs, sizeof vs, "%s \xc2\xb7 %s", T(S_SCRAPER_ON), sz);
		tiles[4] = MENU_NOTE(list_hints(T(S_KEYHINT_CLOSE)));
		scp_draw(&v, v.shot_now);
		ui_scrim(a->r, 0);
		tiles_panel(a, PANEL_LIST, T(S_HOME_SETTINGS), tiles, SCP_SETTINGS_MARKS, 5, 0, PANEL_REST.panel);
	} else {
		scp_draw(&v, v.shot_now);
	}
	for (int i = 0; i < v.nring; i++) if (v.ring[i].tex) SDL_DestroyTexture(v.ring[i].tex);
	for (int i = 0; i < v.n; i++) if (v.s[i].icon) SDL_DestroyTexture(v.s[i].icon);
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

menu_result ss_page(app *a)
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
	menu_choice   choice[SN_ROWS];     /* and its choices: Sound's three (menu.h) */
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
	snaps_ui u = { .play = snaps_play(), .ss_in = ss_signed_in(), .ss_have = ss_have_dev(),
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
	/* Every switch steps round with Left and Right, and is heard so; Sound,
	 * three of them, opens its list on A (menu_choose) */
	for (i = 0; i < n; i++) {
		static const char *sound_lab[3];

		s->sound[i] = ids[i] == SN_DELETE || ids[i] == SN_NONE ? 0 : MENU_SND_RING;
		s->choice[i] = (menu_choice){ 0 };
		if (ids[i] == SN_SOUND) {
			sound_lab[0] = T(S_SYSMENU_SN_MUTED);
			sound_lab[1] = T(S_SYSMENU_SN_QUIET);
			sound_lab[2] = T(S_SYSMENU_SN_FULL);
			s->choice[i] = (menu_choice){ .n = 3, .cur = (unsigned char)(snaps_sound() % 3), .labels = sound_lab };
		}
	}
	return n;
}

/* --rows-report: Video Snaps' rows (menu_rows_print) */
void snaps_rows_report(app *a)
{
	static snaps_ctx s;
	menu_row rows[SN_ROWS];
	const char *h;
	int n;

	memset(&s, 0, sizeof s);
	s.a = a;
	snaps_read(&s);
	n = snaps_build(&s, rows, SN_ROWS, &h);
	menu_rows_print("snaps", rows, n, s.sound, s.choice);
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

menu_result snaps_screen(app *a)
{
	static snaps_ctx s;

	memset(&s, 0, sizeof s);
	s.a = a;
	s.said = MENU_STAY;
	snaps_read(&s);
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = s.sound, .choices = s.choice },
	         snaps_build, snaps_key, &s);
	return s.said;
}
