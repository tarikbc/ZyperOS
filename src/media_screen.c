/* SPDX-License-Identifier: GPL-3.0-only */
#include "media_screen.h"

#include "aout_route.h"
#include "app.h"
#include "artscrape.h"
#include "artshrink.h"
#include "artsrc.h"
#include "battery_eta.h"
#include "esmedia.h"
#include "gamedel_screen.h"
#include "mediafetch.h"
#include "mediapage.h"
#include "mediatex.h"
#include "menumotion.h"
#include "musec.h"
#include "net.h"
#include "panel.h"
#include "snaps.h"
#include "snapshelf.h"
#include "snapview.h"
#include "storage_screen.h"
#include "sys_menu.h"
#include "topbar.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- a game's Cover and Video (DESIGN.md 11.34) ----------------------------
 *
 * The details page's row, Cover and Video, opens a page of its own: the
 * cover on the shelf (Now) beside what the sources offer (Found), each named
 * with its source; A looks closer at one, big beside Now, and A again makes
 * it the cover, the old one kept as Before, which A puts back. Under a rule
 * the game's video snap, its picture playing muted while a row of it has the
 * cursor: Watch it with its sound, Download or Replace it, Delete it.
 * *(2026-11-09: "Currently I just see 'Trocar capa' which is confusing. I
 * don't know what it is changing to, I can't pick, and I don't know what
 * goes on with the video preview." My picks in the Cover and Video lab,
 * the same day.)*
 *
 * What the sources offer comes from src/mediafetch.h, a step a frame; the
 * pictures are decoded off the frame by src/mediatex.h; src/mediapage.h
 * draws the page this builds. */

enum { MK_NOW = 50, MK_BEFORE = 51, MK_BIG = 100 };   /* mediatex keys; 0.. a tile */
enum { MR_WATCH, MR_GET, MR_DELETE };                  /* a video row's job */

typedef struct {
	app        *a;
	char        folder[CFG_STR], file[LIB_PATH], stem[LIB_NAME], title[LIB_NAME], exts[CFG_STR];
	unsigned    sys_rgb;
	bool        net;
	mpg_page    pg;
	/* The cover on the shelf, Before's, and each offer's small copy */
	SDL_Texture *now_tex, *before_tex, *tex[MF_MAX], *big_tex;
	int          now_w, now_h, before_w, before_h, tw[MF_MAX], th[MF_MAX], big_w, big_h;
	int          big_key;           /* the picture big_tex is, MK_BIG + tile */
	bool         asked[MF_MAX], now_gen, have_now, have_before;
	/* No cover of its own, and EmulationStation's in its place on the shelf
	 * (src/esmedia.h): shown as Now, credited to EmulationStation. Not
	 * have_now: nothing here moves or deletes it, and a cover picked
	 * here is ZyperOS's own, which comes first. */
	bool         now_es;
	art_source   now_as, before_as;
	/* The cursor: a tile of the strip, or a video row */
	int          focus, row;
	bool         big;
	int          row_job[MPG_ROWS];
	/* The video snap on the card */
	char         snap[LIB_PATH * 3];
	bool         has_snap;
	long         snap_bytes;
	int          snap_secs;
	int          video_seen;        /* the download's end, once said */
	/* The words the page shows, which it points into */
	char         cap1[160], cap2[256], credit[96], vals[MPG_ROWS][64], lines[3][96], vnote[256];
	/* Something on the card changed: the shelf's textures go at the end */
	bool         changed;
	bool         shot;              /* a fixture: nothing is asked, nothing written */
	mf_state     fix;
	as_source    fix_asking;
	unsigned     opened;
} media_view;

static const str_id MEDIA_KIND[AK_KINDS] = {
	S_MEDIA_KIND_BOX, S_MEDIA_KIND_BOX3D, S_MEDIA_KIND_TITLE, S_MEDIA_KIND_SHOT, S_MEDIA_KIND_FAN
};

static const char *media_region(const char *code)
{
	static const struct { const char *code; str_id id; } R[] = {
		{ "us", S_MEDIA_REGION_US }, { "eu", S_MEDIA_REGION_EU }, { "jp", S_MEDIA_REGION_JP },
		{ "wor", S_MEDIA_REGION_WOR }, { "br", S_MEDIA_REGION_BR }, { "kr", S_MEDIA_REGION_KR },
		{ "au", S_MEDIA_REGION_AU },
	};
	size_t i;

	for (i = 0; i < sizeof R / sizeof R[0]; i++)
		if (!strcmp(code, R[i].code)) return T(R[i].id);
	return NULL;
}

/* "Box Front · USA", or the kind alone where the region is none we name */
static void media_kind_line(const art_source *as, char *out, size_t n)
{
	const char *reg = media_region(as->region);

	if (as->kind < 0 || as->kind >= AK_KINDS) { out[0] = '\0'; return; }
	if (reg) snprintf(out, n, "%s · %s", T(MEDIA_KIND[as->kind]), reg);
	else snprintf(out, n, "%s", T(MEDIA_KIND[as->kind]));
}

/* "ScreenScraper", "SteamGridDB · Midgarsormr": the source, and who drew it */
static void media_source_line(const art_source *as, char *out, size_t n)
{
	if (as->src == AS_SG && as->by[0]) snprintf(out, n, "%s · %s", artsrc_name(as->src), as->by);
	else snprintf(out, n, "%s", artsrc_name(as->src));
}

static const mf_state *media_state(media_view *v)
{
	return v->shot ? &v->fix : mf_get();
}

static as_source media_asking(media_view *v)
{
	return v->shot ? v->fix_asking : mf_asking();
}

static void media_tex_free(SDL_Texture **t)
{
	if (*t) SDL_DestroyTexture(*t);
	*t = NULL;
}

/* The cover and Before's, asked to be read again, and the video's facts:
 * when the page opens and after anything changes them */
static void media_read_card(media_view *v)
{
	char p[LIB_PATH * 3];
	struct stat st;

	media_tex_free(&v->now_tex);
	media_tex_free(&v->before_tex);
	v->now_gen = false;
	v->have_now = mf_media_path(P_ROMS, v->folder, v->stem, ".png", p, sizeof p) && art_is_cover(p);
	v->now_es = !v->have_now && !v->shot &&
	            es_media(P_ROMS, v->folder, v->file, v->stem, ES_COVER, true, p, sizeof p);
	if (v->have_now || v->now_es) {
		mt_ask(MK_NOW, p, MF_FULL_PX);
	} else {
		v->now_tex = ui_make_card(v->a->r, v->title, v->sys_rgb, &v->now_w, &v->now_h);
		v->now_gen = true;
	}
	v->have_before = mf_before_path(P_ROMS, v->folder, v->stem, p, sizeof p) && art_is_cover(p);
	if (v->have_before) mt_ask(MK_BEFORE, p, MF_FULL_PX);
	if (!v->shot) {
		artsrc_get(v->folder, v->stem, &v->now_as);
		artsrc_get_before(v->folder, v->stem, &v->before_as);
	}
	v->has_snap = snaps_path(P_ROMS, v->folder, v->stem, v->snap, sizeof v->snap) &&
	              stat(v->snap, &st) == 0 && st.st_size > 0;
	v->snap_bytes = v->has_snap ? (long)st.st_size : 0;
	v->snap_secs = v->has_snap ? mf_mp4_seconds(v->snap) : -1;
	if (!v->has_snap) v->snap[0] = '\0';
}

/* Pictures the worker has finished, made textures */
static void media_take(media_view *v)
{
	SDL_Surface *s;
	int key;

	while (mt_take(&key, &s)) {
		SDL_Texture *t = s ? SDL_CreateTextureFromSurface(v->a->r, s) : NULL;
		int w = s ? s->w : 0, h = s ? s->h : 0;

		if (s) SDL_FreeSurface(s);
		if (key == MK_NOW) {
			media_tex_free(&v->now_tex);
			v->now_tex = t; v->now_w = w; v->now_h = h;
		} else if (key == MK_BEFORE) {
			media_tex_free(&v->before_tex);
			v->before_tex = t; v->before_w = w; v->before_h = h;
		} else if (key >= MK_BIG) {
			if (key != v->big_key) { if (t) SDL_DestroyTexture(t); continue; }
			media_tex_free(&v->big_tex);
			v->big_tex = t; v->big_w = w; v->big_h = h;
		} else if (key >= 0 && key < MF_MAX) {
			media_tex_free(&v->tex[key]);
			v->tex[key] = t; v->tw[key] = w; v->th[key] = h;
		} else if (t) {
			SDL_DestroyTexture(t);
		}
		redraw_now();
	}
}

/* The strip's tiles: Before first, when there is one, then the offers */
static int media_tiles(media_view *v)
{
	return (v->have_before ? 1 : 0) + media_state(v)->n;
}

static int media_cand(media_view *v, int tile)
{
	return tile - (v->have_before ? 1 : 0);
}

/* The source lines under "Nothing found." */
static const char *media_q_word(mf_qstate q)
{
	switch (q) {
	case MFQ_UNKNOWN:   return T(S_MEDIA_SRC_UNKNOWN);
	case MFQ_OFF:       return T(S_MEDIA_SRC_NOT_LINKED);
	case MFQ_NO_ANSWER: return T(S_MEDIA_SRC_NO_ANSWER);
	default:            return T(S_MEDIA_SRC_NO_MATCH);
	}
}

static bool media_done_asking(const mf_state *st)
{
	int i;

	for (i = 0; i < AS_SOURCES; i++)
		if (st->q[i] == MFQ_WAIT || st->q[i] == MFQ_ASKING) return false;
	return true;
}

static void media_build(media_view *v, unsigned now)
{
	const mf_state *st = media_state(v);
	mpg_page *p = &v->pg;
	int nt = media_tiles(v), i, nr = 0;
	as_source asking = media_asking(v);
	bool done = media_done_asking(st);
	/* No network: begun so, or every source unreachable (mf_unreachable) */
	bool offline = mf_unreachable(st);

	memset(p, 0, sizeof *p);
	p->title = v->title;
	p->title_half = header_half();
	p->title_phase = now - v->opened;
	p->zyper = store_zyper;

	/* Now: the cover, credited with its source when that was kept */
	p->now_head = T(S_MEDIA_NOW);
	p->now_tex = v->now_tex;
	p->now_w = v->now_w;
	p->now_h = v->now_h;
	if (v->now_es) p->now_credit = "EmulationStation";
	else if (!v->have_now) p->now_credit = T(S_MEDIA_NO_COVER);
	else if (v->now_as.src != AS_NONE) p->now_credit = artsrc_name(v->now_as.src);

	/* Found */
	p->found_head = T(S_MEDIA_FOUND);
	if (v->focus >= nt) v->focus = nt > 0 ? nt - 1 : 0;
	if (asking != AS_NONE && !offline) {
		snprintf(p->found_right, sizeof p->found_right, T(S_MEDIA_ASKING), artsrc_name(asking));
		p->found_right_short = artsrc_name(asking);
		p->found_dots = true;
		p->dots_step = v->shot ? 1 : (int)(now / CORNER_DOT_MS);
	} else if (nt > 0) {
		snprintf(p->found_right, sizeof p->found_right, "%d / %d", v->focus + 1, nt);
	}

	if (offline && !v->have_before) {
		p->msg = MSG_NEEDS_WIFI;
		p->msg_wrap = T(S_MEDIA_OR_PC);
	} else if (done && nt == 0) {
		p->msg = T(S_MEDIA_NOTHING);
		for (i = 0; i < AS_SOURCES && i < 3; i++) {
			snprintf(v->lines[i], sizeof v->lines[i], "%s · %s", artsrc_name((as_source)i),
			         media_q_word(st->q[i]));
			p->msg_lines[p->nmsg_lines++] = v->lines[i];
		}
	} else {
		p->strip_focus = v->row < 0;
		p->focus = v->focus;
		for (i = 0; i < nt && i < MPG_TILES; i++) {
			mpg_tile *t = &p->tile[i];
			int c = media_cand(v, i);

			t->load = -1;
			if (c < 0) {
				t->tex = v->before_tex; t->w = v->before_w; t->h = v->before_h;
				t->chip = T(S_MEDIA_BEFORE);
				continue;
			}
			t->tex = v->tex[c]; t->w = v->tw[c]; t->h = v->th[c];
			t->failed = st->c[c].thumb_st == MFT_FAILED;
			if (v->have_now && artsrc_same(&st->c[c].as, &v->now_as)) t->chip = T(S_MEDIA_NOW);
		}
		p->ntiles = nt;
		/* While nothing has come yet, the places it will come to */
		if (nt == 0) {
			p->ntiles = 3;
			for (i = 0; i < 3; i++) p->tile[i].load = -1;
		}
		if (nt > 0) {
			int c = media_cand(v, v->focus);
			const art_source *as = c < 0 ? &v->before_as : &st->c[c].as;

			if (c < 0 && as->src == AS_NONE) {
				snprintf(v->cap1, sizeof v->cap1, "%s", T(S_MEDIA_BEFORE));
				v->cap2[0] = '\0';
			} else {
				media_kind_line(as, v->cap1, sizeof v->cap1);
				media_source_line(as, v->cap2, sizeof v->cap2);
			}
			p->cap1 = v->cap1;
			p->cap2 = v->cap2[0] ? v->cap2 : NULL;
			p->cap2_short = as->src != AS_NONE ? artsrc_name(as->src) : NULL;
			p->hint = T(c < 0 ? S_MEDIA_PUT_BACK : S_MEDIA_LOOK);
		}
	}

	/* The video: its picture, its rows */
	p->video_head = T(S_MEDIA_VIDEO);
	p->vid_load = -1;
	if (v->has_snap) {
		v->row_job[nr] = MR_WATCH;
		v->vals[nr][0] = '\0';
		if (v->snap_secs >= 0) snprintf(v->vals[nr], sizeof v->vals[nr], "%d s", v->snap_secs);
		p->rows[nr] = (mpg_row){ T(S_MEDIA_WATCH), v->vals[nr], true };
		nr++;
	}
	/* Download (Replace, with a video) only when there is one to fetch or
	 * one coming; otherwise the empty tile and a line where the rows would
	 * be say why (2026-11-09: "always have the Baixar button, even if no
	 * wifi or no videos found") */
	p->vid_note = NULL;
	if (st->video_coming || (!offline && st->video_offer)) {
		const char *val = NULL;
		bool live = false;

		v->row_job[nr] = MR_GET;
		if (st->video_coming) {
			char got[24], all[24];

			sys_menu_bytes((unsigned long long)st->video_got, got, sizeof got);
			if (st->video_size > 0) {
				sys_menu_bytes((unsigned long long)st->video_size, all, sizeof all);
				snprintf(v->vals[nr], sizeof v->vals[nr], "%s / %s", got, all);
				p->vid_load = (float)st->video_got / (float)st->video_size;
			} else {
				snprintf(v->vals[nr], sizeof v->vals[nr], "%s", got);
				p->vid_load = 0;
			}
			val = v->vals[nr];
		} else {
			live = true;
			if (st->video_size > 0) {
				sys_menu_bytes((unsigned long long)st->video_size, v->vals[nr], sizeof v->vals[nr]);
				val = v->vals[nr];
			}
		}
		p->rows[nr] = (mpg_row){ T(v->has_snap ? S_MEDIA_REPLACE : S_MEDIA_DOWNLOAD), val, live };
		nr++;
	}
	if (v->has_snap) {
		v->row_job[nr] = MR_DELETE;
		sys_menu_bytes((unsigned long long)v->snap_bytes, v->vals[nr], sizeof v->vals[nr]);
		p->rows[nr] = (mpg_row){ T(S_MEDIA_DELETE), v->vals[nr], true };
		nr++;
	}
	p->nrows = nr;
	if (v->row >= nr) v->row = nr - 1;
	p->row = v->row;
	if (v->has_snap) {
		p->vid_tex = snapv_texture(&p->vid_w, &p->vid_h, &p->vid_src);
		if (v->shot && !p->vid_tex) {
			p->vid_tex = v->tex[MF_MAX - 1];
			p->vid_w = v->tw[MF_MAX - 1];
			p->vid_h = v->th[MF_MAX - 1];
			p->vid_src = (SDL_Rect){ 0, 0, 0, 0 };
		}
		p->vid_disc = v->row < 0;
	} else if (st->video_coming) {
		p->vid_status = NULL;      /* the bar, and the row's bytes, say it */
	} else if (!offline && (st->q[AS_SS] == MFQ_WAIT || st->q[AS_SS] == MFQ_ASKING)) {
		p->vid_status = T(S_MEDIA_LOOKING);
	} else if (!offline && st->q[AS_SS] != MFQ_OFF && !st->video_offer) {
		p->vid_status = T(S_MEDIA_NONE_FOUND);
		p->vid_note = T(S_MEDIA_VNOTE_NONE);
	} else {
		p->vid_status = T(S_MEDIA_NO_VIDEO);
	}
	/* Why there is nothing to download, beside a tile with no video */
	if (!v->has_snap && !st->video_coming && !st->video_offer) {
		if (offline) {
			p->vid_note = T(S_MEDIA_VNOTE_OFFLINE);
		} else if (st->q[AS_SS] == MFQ_OFF) {
			snprintf(v->vnote, sizeof v->vnote, T(S_MEDIA_VNOTE_LINK), T(S_SCRAPER_HEADING));
			p->vid_note = v->vnote;
		}
	}

	/* Look Closer: the focused picture whole, or its small copy while that
	 * comes */
	if (v->big && nt > 0) {
		int c = media_cand(v, v->focus);

		p->big = true;
		p->back = T(S_MEDIA_BACK);
		p->use = T(c < 0 ? S_MEDIA_PUT_BACK : S_MEDIA_USE);
		p->big_load = -1;
		if (c < 0) {
			p->big_tex = v->before_tex; p->big_w = v->before_w; p->big_h = v->before_h;
		} else if (v->big_tex && v->big_key == MK_BIG + c) {
			p->big_tex = v->big_tex; p->big_w = v->big_w; p->big_h = v->big_h;
		} else {
			p->big_tex = v->tex[c]; p->big_w = v->tw[c]; p->big_h = v->th[c];
			if (st->c[c].full_st != MFT_FAILED) p->big_load = 0;
		}
		snprintf(v->cap2 + strlen(v->cap2), sizeof v->cap2 - strlen(v->cap2), "%s%d / %d",
		         v->cap2[0] ? " · " : "", v->focus + 1, nt);
		p->cap2 = v->cap2;
	}
}

/* The small copies of the tiles on screen, asked of the source and of the
 * worker; and the whole of the one looked at closely */
static void media_ask(media_view *v)
{
	const mf_state *st = media_state(v);
	int i, lo = -1, hi = -1;

	for (i = 0; i < media_tiles(v); i++) {
		int c = media_cand(v, i);

		if (c < 0 || !mpg_tile_shown(&v->pg, i)) continue;
		if (lo < 0) lo = c;
		hi = c;
		if (!v->asked[c] && st->c[c].thumb_st == MFT_HAVE) {
			mt_ask(c, st->c[c].thumb, MF_THUMB_PX);
			v->asked[c] = true;
		}
	}
	if (!v->shot) mf_want(lo, hi);
	if (v->big) {
		int c = media_cand(v, v->focus);

		if (c >= 0 && (!v->shot ? mf_want_full(c) : st->c[c].full_st == MFT_HAVE) &&
		    v->big_key != MK_BIG + c) {
			v->big_key = MK_BIG + c;
			media_tex_free(&v->big_tex);
			mt_ask(MK_BIG + c, st->c[c].full, 640);
		}
	}
}

static void media_open(media_view *v, app *a, int owner, const game_entry *g)
{
	const system_cfg *s = &a->sys.systems[owner];

	memset(v, 0, sizeof *v);
	v->a = a;
	snprintf(v->folder, sizeof v->folder, "%s", s->folder);
	snprintf(v->exts, sizeof v->exts, "%s", s->exts);
	snprintf(v->file, sizeof v->file, "%s", g->file);
	snprintf(v->stem, sizeof v->stem, "%s", g->name);
	snprintf(v->title, sizeof v->title, "%s", g->title);
	v->sys_rgb = s->accent;
	v->row = -1;
	v->big_key = -1;
	v->opened = plat_now_ms();
	v->now_as.src = v->before_as.src = AS_NONE;
	mt_start();
	mt_forget();
}

static void media_close(media_view *v)
{
	int i;

	mt_forget();
	media_tex_free(&v->now_tex);
	media_tex_free(&v->before_tex);
	media_tex_free(&v->big_tex);
	for (i = 0; i < MF_MAX; i++) media_tex_free(&v->tex[i]);
}

static void media_draw(app *a, media_view *v)
{
	unsigned now = anim_now();

	media_build(v, now);
	draw_background(a);
	draw_topbar(a->r, theme_index(), UI_TEXT_SOFT, frames_wanted());
	mpg_draw(a->r, &v->pg, now);
}

static void media_base(app *a, void *ctx)
{
	media_draw(a, ctx);
}

/* Watch: the video snap on the whole screen, with its sound, until B */
static void media_watch(media_view *v)
{
	app *a = v->a;

	snap_ask("", false);
	g_snap_force_vol = musec_heard() ? 0 : 100;
	snap_ask(v->snap, false);
	g_snap_force_vol = -1;
	while (!want_quit && a->running) {
		SDL_Texture *t;
		SDL_Rect src = { 0, 0, 0, 0 };
		int w = 0, h = 0;

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { snap_off(); power_off(a); break; }
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_ACCEPT]) { uisound_play(DS_SOUND_BACK); break; }
		if (a->in.pressed[IN_MENU]) { menu_leaving(a); break; }
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		snapv_update(a->r);
		tick_theme(a);
		draw_background(a);
		draw_topbar(a->r, theme_index(), UI_TEXT_SOFT, true);
		{
			TTF_Font *fh = page_font(PAGE_ACTION);
			char fit[LIB_NAME];

			ui_fit_text(fh, v->title, fit, sizeof fit, 2 * header_half());
			ui_text(a->r, fh, fit, ZYPEROS_SCREEN_W / 2, ui_cap_y(fh, TOPBAR_MID), 0, UI_TEXT);
		}
		if ((t = snapv_texture(&w, &h, &src)) && w > 0 && h > 0) {
			float k = fminf(896.0f / w, 576.0f / h);
			SDL_FRect d = { 512 - w * k / 2, 104 + (576 - h * k) / 2, w * k, h * k };

			SDL_RenderCopyF(a->r, t, src.w > 0 ? &src : NULL, &d);
		}
		page_hint(a->r, "B", T(S_MEDIA_BACK), 64, 724, false, false);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	/* Muted again on the page */
	snap_ask("", false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* The page's picture of the video: muted, its first picture while the
 * strip has the cursor, playing while a video row has it */
static void media_snap(media_view *v)
{
	if (v->shot) return;
	if (!v->has_snap) { snap_ask("", false); return; }
	g_snap_force_vol = 0;
	snap_ask(v->snap, v->row < 0);
	g_snap_force_vol = -1;
	snapv_update(v->a->r);
}

/* A on a tile: look closer; on Before, put it back; in the big look, use */
static void media_pick(media_view *v)
{
	const mf_state *st = media_state(v);
	int nt = media_tiles(v), c;
	char dest[LIB_PATH * 3];

	if (nt <= 0) { uisound_play(DS_SOUND_DENY); return; }
	c = media_cand(v, v->focus);
	if (c < 0) {
		/* Put Back: the cover there was, and the one there is to Before */
		if (!mf_put_back()) { uisound_play(DS_SOUND_DENY); return; }
		uisound_play(DS_SOUND_ACCEPT);
		v->changed = true;
		v->big = false;
		media_read_card(v);
		return;
	}
	if (!v->big) {
		uisound_play(DS_SOUND_ACCEPT);
		v->big = true;
		return;
	}
	if (st->c[c].full_st == MFT_FAILED) {
		uisound_play(DS_SOUND_DENY);
		result_message(v->a, T(S_MEDIA_ROW), T(S_MEDIA_FAILED));
		return;
	}
	if (st->c[c].full_st != MFT_HAVE || !mf_use(c, dest, sizeof dest)) {
		uisound_play(DS_SOUND_DENY);
		return;
	}
	uisound_play(DS_SOUND_ACCEPT);
	art_shrink(dest);
	v->changed = true;
	v->big = false;
	media_read_card(v);
	/* The cover that was is first now, as Before */
	v->focus = 0;
}

static void media_row_act(media_view *v)
{
	const mf_state *st = media_state(v);
	app *a = v->a;

	switch (v->row_job[v->row]) {
	case MR_WATCH:
		uisound_play(DS_SOUND_ACCEPT);
		media_watch(v);
		break;
	case MR_GET:
		if (mf_unreachable(st)) {
			uisound_play(DS_SOUND_DENY);
			result_message(a, T(S_MEDIA_ROW), MSG_NEEDS_WIFI);
			break;
		}
		if (!mf_get_video()) { uisound_play(DS_SOUND_DENY); break; }
		uisound_play(DS_SOUND_ACCEPT);
		v->video_seen = 0;
		break;
	case MR_DELETE:
		uisound_play(DS_SOUND_ACCEPT);
		if (!store_ask(a, T(S_MEDIA_VIDEO), T(S_MEDIA_DELETE_ASK), T(S_GAMEMENU_DELETE_YES))) break;
		snap_off();
		unlink(v->snap);
		v->changed = true;
		media_read_card(v);
		v->row = 0;
		break;
	}
}

void media_screen(app *a, int owner, const game_entry *g)
{
	media_view *v = calloc(1, sizeof *v);
	int base;

	if (!v) return;
	media_open(v, a, owner, g);
	/* A way out (net_internet: Wi-Fi, or a phone's tethering), not merely an
	 * interface up: on a Pixel 2 with no adapter the USB cable's network is
	 * up and leads nowhere, and a page that asked anyway said "Nothing
	 * found." (2026-11-09, from Tarik's Pixel 2). mf_unreachable catches
	 * what this misses. */
	v->net = net_online() && net_internet(NULL);
	/* The one async slot, which an account answer may still hold */
	if (v->net) sync_abandon();
	mf_begin(v->folder, v->file, v->stem, v->exts, P_ROMS, v->net);
	media_read_card(v);
	/* Nothing to pick with no network and no Before: the video's rows */
	if (!v->net && !v->have_before) v->row = 0;
	base = panel_base();
	g_base_page = media_base;
	g_base_ctx = v;
	while (!want_quit && a->running) {
		const mf_state *st;
		int nt;

		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { mf_end(); snap_off(); power_off(a); break; }
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* What the sources and the worker brought since the last frame */
		if (mf_step()) redraw_now();
		media_take(v);
		st = mf_get();
		if (st->video_done && st->video_done != v->video_seen) {
			v->video_seen = st->video_done;
			if (st->video_done > 0) {
				snap_ask("", false);
				v->changed = true;
				media_read_card(v);
			} else {
				result_message(a, T(S_MEDIA_ROW), T(S_MEDIA_FAILED));
			}
		}
		nt = media_tiles(v);

		if (a->in.pressed[IN_MENU]) { menu_leaving(a); break; }
		if (a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			if (!v->big) break;
			v->big = false;
		} else if (v->big) {
			in_button b = in_repeat(&a->in, IN_LEFT) ? IN_LEFT : in_repeat(&a->in, IN_RIGHT) ? IN_RIGHT : IN_NONE;

			if (b != IN_NONE && nt > 0) {
				int to = v->focus + (b == IN_LEFT ? -1 : 1);

				if (to < 0) to = 0;
				if (to >= nt) to = nt - 1;
				step_sound(a, b, v->focus, to);
				v->focus = to;
			}
			if (a->in.pressed[IN_ACCEPT]) media_pick(v);
		} else {
			in_button b = in_repeat(&a->in, IN_UP) ? IN_UP : in_repeat(&a->in, IN_DOWN) ? IN_DOWN
			            : in_repeat(&a->in, IN_LEFT) ? IN_LEFT : in_repeat(&a->in, IN_RIGHT) ? IN_RIGHT : IN_NONE;

			if (b == IN_UP || b == IN_DOWN) {
				/* The strip is one stop above the rows */
				/* the strip is stop 0 of the list, the rows after it */
				v->row = list_step_heard(a, b, v->pg.nrows + 1, v->row + 1) - 1;
			} else if ((b == IN_LEFT || b == IN_RIGHT) && v->row < 0 && nt > 0) {
				int to = v->focus + (b == IN_LEFT ? -1 : 1);

				if (to < 0) to = 0;
				if (to >= nt) to = nt - 1;
				step_sound(a, b, v->focus, to);
				v->focus = to;
			}
			if (a->in.pressed[IN_ACCEPT]) {
				if (v->row < 0) media_pick(v);
				else if (v->pg.rows[v->row].live) media_row_act(v);
				else {
					uisound_play(DS_SOUND_DENY);
					if (mf_unreachable(st)) result_message(a, T(S_MEDIA_ROW), MSG_NEEDS_WIFI);
				}
			}
		}
		if (g_menu_closing) break;

		tick_tint(a);
		tick_theme(a);
		media_snap(v);
		media_draw(a, v);
		media_ask(v);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
	mf_end();
	snap_ask("", false);
	mm_close(base, anim_now(), 0, g_menu_closing);
	g_base_page = NULL;
	g_base_ctx = NULL;
	media_close(v);
	/* The shelf's cards were made from the art there was, and its focused
	 * game's snap worked out before this page got or deleted one */
	if (v->changed) {
		free_all_textures(a);
		snapshelf_media_changed();
	}
	refresh_game_titles(a);
	free(v);
}

/* --media NAME [TILE]: the page as the lab drew it, from a fixture rather
 * than the sources: page (the offers, the cursor on the first), look (Look
 * Closer at it), video (the cursor on Watch), videoget (a download under
 * way, for a game with none), fetching (libretro has answered, ScreenScraper
 * is asked), offline, nothing (every source answered with none, a game with
 * no cover) and after (a cover was used, the one before is Before).
 * --media-art DIR names the pictures: now.png, before.png, c0.png...,
 * snap.png; without them each is the launcher's generated card. */
const char *shot_media;
const char *shot_media_art;
int shot_media_tile = -1;

static void media_fixture_tex(media_view *v, SDL_Texture **t, int *w, int *h, const char *file,
                              const char *title)
{
	char p[LIB_PATH * 2];
	SDL_Surface *s = NULL;

	if (shot_media_art) {
		snprintf(p, sizeof p, "%s/%s", shot_media_art, file);
		s = mt_load(p, 640);
	}
	if (s) {
		*t = SDL_CreateTextureFromSurface(v->a->r, s);
		*w = s->w;
		*h = s->h;
		SDL_FreeSurface(s);
	} else {
		*t = ui_make_card(v->a->r, title, v->sys_rgb, w, h);
	}
}

void media_shot(app *a)
{
	static media_view v;
	static bool made;
	const char *name = shot_media;

	/* live: the real sources over the network for the game under the
	 * cursor, stepped until they have answered (60 s at most), then the page
	 * as the screen draws it - for trying the fetch on a host, never in a
	 * check, which is offline */
	if (!made && !strcmp(name, "live")) {
		sysview *sv = &a->view[a->sys_cursor];
		unsigned t0 = plat_now_ms();

		made = true;
		if (sv->list.count == 0) return;
		media_open(&v, a, shelf_owner(a, a->sys_cursor, sv->cursor), &sv->list.items[sv->cursor]);
		mf_begin(v.folder, v.file, v.stem, v.exts, P_ROMS, net_online());
		media_read_card(&v);
		mt_stop();               /* decoded here, so the frame has them all */
		while (plat_now_ms() - t0 < 60000) {
			const mf_state *st = mf_get();
			bool busy = !media_done_asking(st);
			int i;

			mf_step();
			media_build(&v, plat_now_ms());
			media_ask(&v);
			media_take(&v);
			for (i = 0; i < st->n; i++)
				if (st->c[i].thumb_st == MFT_COMING) busy = true;
			if (!busy && plat_now_ms() - t0 > 500) break;
			SDL_Delay(5);
		}
		media_take(&v);
		fprintf(stderr, "media live: %d offers, libretro %d, ScreenScraper %d, SteamGridDB %d, video %d\n",
		        mf_get()->n, mf_get()->q[AS_LR], mf_get()->q[AS_SS], mf_get()->q[AS_SG], mf_get()->video_offer);
	}
	if (!made) {
		/* Every kind and every region a caption can name, so the language
		 * shots draw each (tools/i18n-shots.py's media-tile-N) */
		static const struct { as_source src; as_kind kind; const char *reg, *by; } FIX[] = {
			{ AS_SS, AK_BOX, "us", "" }, { AS_SS, AK_BOX, "jp", "" }, { AS_SS, AK_BOX, "eu", "" },
			{ AS_SS, AK_BOX, "kr", "" }, { AS_SS, AK_BOX3D, "au", "" }, { AS_SS, AK_TITLE, "wor", "" },
			{ AS_SS, AK_SHOT, "br", "" },
			{ AS_LR, AK_BOX, "", "Fixture (Japan)" }, { AS_LR, AK_TITLE, "", "" }, { AS_LR, AK_SHOT, "", "" },
			{ AS_SG, AK_FAN, "", "Midgarsormr" },
		};
		sysview *sv = &a->view[a->sys_cursor];
		int owner, i, n = (int)(sizeof FIX / sizeof FIX[0]);
		bool nothing = !strcmp(name, "nothing"), fetching = !strcmp(name, "fetching");
		bool after = !strcmp(name, "after"), offline = !strcmp(name, "offline");
		bool videoget = !strcmp(name, "videoget");
		/* No ScreenScraper account: no video to download, and why */
		bool novideo = !strcmp(name, "novideo");

		made = true;
		if (sv->list.count == 0) return;
		owner = shelf_owner(a, a->sys_cursor, sv->cursor);
		media_open(&v, a, owner, &sv->list.items[sv->cursor]);
		v.shot = true;
		v.fix_asking = AS_NONE;
		for (i = 0; i < AS_SOURCES; i++) v.fix.q[i] = MFQ_DONE;
		v.have_now = !nothing;
		v.now_as = (art_source){ AS_LR, AK_BOX, "", "Fixture (Japan)" };
		if (nothing) {
			v.now_tex = ui_make_card(a->r, v.title, v.sys_rgb, &v.now_w, &v.now_h);
			v.fix.q[AS_SS] = MFQ_UNKNOWN;
			v.fix.q[AS_LR] = MFQ_NONE;
			v.fix.q[AS_SG] = MFQ_NO_ANSWER;
			n = 0;
		} else {
			media_fixture_tex(&v, &v.now_tex, &v.now_w, &v.now_h, after ? "c0.png" : "now.png", v.title);
		}
		if (after) {
			v.have_before = true;
			v.before_as = (art_source){ AS_LR, AK_BOX, "", "Fixture (Japan)" };
			v.now_as = (art_source){ AS_SS, AK_BOX, "us", "" };
			media_fixture_tex(&v, &v.before_tex, &v.before_w, &v.before_h, "now.png", v.title);
		}
		if (fetching) {
			/* libretro's three have come; ScreenScraper is being asked */
			static const int LR[] = { 7, 8, 9 };

			v.fix_asking = AS_SS;
			v.fix.q[AS_SS] = MFQ_ASKING;
			v.fix.q[AS_SG] = MFQ_WAIT;
			for (i = 0; i < 3; i++) {
				char f[16];

				v.fix.c[i].as = (art_source){ FIX[LR[i]].src, FIX[LR[i]].kind, "", "" };
				snprintf(v.fix.c[i].as.by, sizeof v.fix.c[i].as.by, "%s", FIX[LR[i]].by);
				v.fix.c[i].thumb_st = MFT_HAVE;
				snprintf(f, sizeof f, "c%d.png", LR[i]);
				media_fixture_tex(&v, &v.tex[i], &v.tw[i], &v.th[i], f, T(MEDIA_KIND[FIX[LR[i]].kind]));
			}
			v.fix.c[3].thumb_st = MFT_COMING;
			v.fix.c[4].thumb_st = MFT_COMING;
			v.fix.n = 5;
		} else if (offline) {
			v.fix.offline = true;
		} else {
			for (i = 0; i < n; i++) {
				char f[16];

				v.fix.c[i].as = (art_source){ FIX[i].src, FIX[i].kind, "", "" };
				snprintf(v.fix.c[i].as.region, sizeof v.fix.c[i].as.region, "%s", FIX[i].reg);
				snprintf(v.fix.c[i].as.by, sizeof v.fix.c[i].as.by, "%s", FIX[i].by);
				v.fix.c[i].thumb_st = v.fix.c[i].full_st = MFT_HAVE;
				snprintf(f, sizeof f, "c%d.png", i);
				media_fixture_tex(&v, &v.tex[i], &v.tw[i], &v.th[i], f, T(MEDIA_KIND[FIX[i].kind]));
			}
			v.fix.n = n;
		}
		/* The video: a game with one, but where it is being fetched or is none */
		v.has_snap = !videoget && !fetching && !nothing && !novideo;
		if (novideo) v.fix.q[AS_SS] = MFQ_OFF;
		if (v.has_snap) {
			v.snap_secs = 12;
			v.snap_bytes = 3100000;
			media_fixture_tex(&v, &v.tex[MF_MAX - 1], &v.tw[MF_MAX - 1], &v.th[MF_MAX - 1], "snap.png",
			                  T(S_MEDIA_VIDEO));
		}
		v.fix.video_offer = !nothing && !fetching && !novideo;
		v.fix.video_size = 2400000;
		if (videoget) {
			v.fix.video_coming = true;
			v.fix.video_got = 1100000;
		}
		if (!strcmp(name, "video") || offline || videoget) v.row = 0;
		if (!strcmp(name, "look")) v.big = true;
		if (shot_media_tile >= 0) v.focus = shot_media_tile;
		if (v.big) {
			v.big_key = MK_BIG + v.focus;
			media_fixture_tex(&v, &v.big_tex, &v.big_w, &v.big_h, "c0.png", T(MEDIA_KIND[AK_BOX]));
		}
	}
	if (!v.a) return;
	media_draw(a, &v);
	if (!strcmp(name, "delete")) {
		/* Delete, the video row's question, over the page */
		char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
		menu_row rows[DEL_ASK_LINES + 2];
		int nq = store_ask_rows(a, T(S_MEDIA_DELETE_ASK), lines, rows);

		rows[nq] = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
		rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
		ui_scrim(a->r, 0);
		menu_draw(a, T(S_MEDIA_VIDEO), rows, nq + 2, nq + 1, menu_std_width(a), MENU_ZYPEROS);
	} else if (!strcmp(name, "failed")) {
		/* A picture or a video that did not come, as result_message says it */
		const char *text = T(S_MEDIA_FAILED);
		int w = menu_std_width(a);
		char lines[3][LB_LINE];
		menu_row rows[3];
		int n = 1, i;

		rows[0] = (menu_row){ text, NULL, false };
		if (ui_text_width(ui_font(UI_F_MENU), text) > w) {
			n = ui_balance_lines(ui_font(UI_F_MENU), text, w, 3, lines);
			for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
		}
		ui_scrim(a->r, 0);
		menu_draw(a, T(S_MEDIA_ROW), rows, n, -1, n > 1 ? w : 0, MENU_ZYPEROS);
	}
}
