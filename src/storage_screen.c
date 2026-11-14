/* SPDX-License-Identifier: GPL-3.0-only */
#include "storage_screen.h"

#include "aout_route.h"
#include "app.h"
#include "bgm.h"
#include "gamedel_screen.h"
#include "i18n.h"
#include "menumotion.h"
#include "panel.h"
#include "shelfbuild.h"
#include "snaps.h"
#include "sort.h"
#include "stats.h"
#include "storage.h"
#include "storage_page.h"
#include "sys_menu.h"
#include "taskhold.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"
#include "zypermark.h"

#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/* ---------- MENU > Storage (DESIGN.md 11.32) --------------------------------
 *
 * How the card is used, as iPhone Storage shows a phone: one stacked bar by
 * part with the free room, a recommendation or two, then everything sorted
 * by size, and a page per system with its largest games. The numbers are
 * src/storage.h's, measured on a thread at the lowest priority while this
 * page is open and cached on the card, so the page opens at once with the
 * last numbers and moves to the new ones as the walk reports; it is drawn
 * by src/storage_page.c. A on a game is Delete Game's own question
 * (game_delete, DESIGN.md 11.31). My picks in the Storage lab
 * (docs/design/mock/storage-lab.html), 2026-11-07. */

enum {
	SR_ART, SR_STATES, SR_SYSTEM, SR_VIDEOS, SR_MUSIC, SR_BOOKS, SR_BIOS, SR_ZYPEROS,
	SR_OTHER, SR_SNAPS, SR_GAME, SR_ORPHAN, SR_REST, SR_MEDIA, SR_BGM
};

typedef struct {
	char key[24];
	SDL_Texture *tex;
} store_icon;

typedef struct {
	app *a;
	systems_cfg cfg;              /* every system ZyperOS knows, shown or not */
	bool cfg_ok;
	st_card c;
	unsigned gen;
	bool fixture;                 /* a shot's stand-in card */
	int fix_count[ST_MAX_SYS];    /* ...and its systems' games */
	int page;                     /* -1 the card, else a system: its place in c.sys */
	int main_sel;                 /* the card's cursor while a system's page is up */
	sp_row *rows;
	int nrows, cap;
	sp_page pg;
	sp_motion m;
	int cap_for;                  /* the row whose caption is written */
	st_game_in *games;            /* the walk's request */
	int ngames;
	st_systems sys;
	st_orphan orph[ST_ORPHANS];   /* the Ports shelf's folders no script names */
	int norph;
	int media;                    /* a media folder's page (st_media_kind), else -1 */
	st_media *med;                /* its items */
	int nmed;
	st_sort sort;                 /* the list's order, read as its rows are made */
	st_paths paths;
	store_icon icons[ST_MAX_SYS + 8];
	int nicons;
	char used[SP_VALUE];
} store_view;

/* MENU's row: the room left, asked of the card at most every 2 s, as About
 * asks it */
static char     g_store_val[48];
static unsigned g_store_val_at;

const char *storage_menu_value(void)
{
	struct statvfs vf;
	unsigned now = plat_now_ms();

	if (g_store_val_at && now - g_store_val_at < 2000) return g_store_val;
	g_store_val_at = now | 1;
	if (statvfs(P_CARD, &vf) == 0) {
		char sz[24];

		sys_menu_bytes((unsigned long long)vf.f_bavail * vf.f_frsize, sz, sizeof sz);
		snprintf(g_store_val, sizeof g_store_val, T(S_STORAGE_FREE), sz);
	} else {
		g_store_val[0] = '\0';
	}
	return g_store_val[0] ? g_store_val : NULL;
}

static const system_cfg *store_cfg(store_view *v, const char *folder)
{
	int i;

	for (i = 0; v->cfg_ok && i < v->cfg.count; i++)
		if (!strcmp(v->cfg.systems[i].folder, folder)) return &v->cfg.systems[i];
	return NULL;
}

/* The shelf holding a folder's games, -1 for none (an empty system is off
 * the home) */
static int store_shelf(app *a, const char *folder)
{
	int i;

	for (i = 0; i < a->sys.count; i++)
		if (!a->view[i].owner && !is_muse(&a->sys.systems[i]) &&
		    !strcmp(a->sys.systems[i].folder, folder))
			return i;
	return -1;
}

static int store_count(store_view *v, int idx)
{
	int s;

	if (v->fixture) return v->fix_count[idx];
	s = store_shelf(v->a, v->c.sys[idx].folder);
	return s < 0 ? 0 : sys_games(&v->a->view[s])->count;
}

/* A design's still (res/system-icons, src/systemart_data.h), loaded once
 * while the page is up; nearest-neighbour, as the home draws them */
static SDL_Texture *store_tex(store_view *v, const char *key)
{
	char path[CFG_STR * 2];
	int i;

	if (!key || !key[0]) return NULL;
	for (i = 0; i < v->nicons; i++)
		if (!strcmp(v->icons[i].key, key)) return v->icons[i].tex;
	if (v->nicons >= (int)(sizeof v->icons / sizeof v->icons[0])) return NULL;
	snprintf(path, sizeof path, "%s/system-icons/%s-still.png", P_ROOT, key);
	snprintf(v->icons[v->nicons].key, sizeof v->icons[v->nicons].key, "%s", key);
	if ((v->icons[v->nicons].tex = IMG_LoadTexture(v->a->r, path))) {
		SDL_SetTextureScaleMode(v->icons[v->nicons].tex, SDL_ScaleModeNearest);
		SDL_SetTextureBlendMode(v->icons[v->nicons].tex, SDL_BLENDMODE_BLEND);
	}
	return v->icons[v->nicons++].tex;
}

static void store_free_icons(store_view *v)
{
	int i;

	for (i = 0; i < v->nicons; i++)
		if (v->icons[i].tex) SDL_DestroyTexture(v->icons[i].tex);
	v->nicons = 0;
}

static bool store_own_fs(void)
{
	char parent[CFG_STR * 2], *slash;
	struct stat a, b;

	snprintf(parent, sizeof parent, "%s", P_CARD);
	if (!(slash = strrchr(parent, '/'))) return false;
	if (slash == parent) slash[1] = '\0';
	else *slash = '\0';
	return stat(P_CARD, &a) == 0 && stat(parent, &b) == 0 && a.st_dev != b.st_dev;
}

/* What the walk is told: every system's folder, and every game on the
 * card's own shelves with when it was last played */
static void store_request(store_view *v)
{
	app *a = v->a;
	long now = (long)time(NULL);
	int i, k, n = 0;

	free(v->games);
	v->games = NULL;
	v->ngames = 0;
	for (i = 0; i < a->sys.count; i++)
		if (!a->view[i].owner && !is_muse(&a->sys.systems[i]) && a->sys.systems[i].folder[0])
			n += sys_games(&a->view[i])->count;
	if (n > 0 && !(v->games = malloc((size_t)n * sizeof *v->games))) n = 0;
	stats_summarize(STATS_ALL, false, now);
	for (i = 0; i < a->sys.count && v->games; i++) {
		const system_cfg *s = &a->sys.systems[i];
		bool hooked = lib_shelf_hooked(s->exts);

		if (a->view[i].owner || is_muse(s) || !s->folder[0]) continue;
		const game_list *gl = sys_games(&a->view[i]);

		for (k = 0; k < gl->count && v->ngames < n; k++) {
			const game_entry *g = &gl->items[k];
			long secs = 0, last = 0;

			stats_lookup(s->tag, g->file, &secs, &last);
			v->games[v->ngames++] = (st_game_in){ s->folder, s->tag, s->core, s->exts,
			                                      g->file, g->name, hooked, last, g->dir };
		}
	}
	v->sys.n = 0;
	for (i = 0; v->cfg_ok && i < v->cfg.count && v->sys.n < ST_MAX_SYS; i++)
		if (v->cfg.systems[i].folder[0] && !is_muse(&v->cfg.systems[i]))
			v->sys.folders[v->sys.n++] = v->cfg.systems[i].folder;
	v->sys.ports = NULL;
	for (i = 0; v->cfg_ok && i < v->cfg.count; i++)
		if (!strcmp(v->cfg.systems[i].core, "port") && v->cfg.systems[i].folder[0])
			v->sys.ports = v->cfg.systems[i].folder;
	v->paths = (st_paths){ P_CARD, P_ROMS, P_SHARED, P_ROOT, store_own_fs() };
}

static void store_refresh(store_view *v)
{
	if (v->fixture) return;
	st_refresh(&v->paths, &v->sys, v->games, v->ngames, (long)time(NULL));
}

static void store_bytes(unsigned long long b, char *out, size_t n)
{
	sys_menu_bytes(b, out, n);
}

static unsigned long long store_sys_bytes(const st_sys *s)
{
	return s->games + s->covers + s->snaps + s->states + s->saves;
}

static unsigned long long store_zyperos(const st_card *c)
{
	return c->zyperos + c->emulators + c->partitions;
}

static unsigned long long store_other(const st_card *c)
{
	return c->cat[STC_OTHER] > c->bios ? c->cat[STC_OTHER] - c->bios : 0;
}

/* A row's own bytes, for the card's order */
static unsigned long long store_row_bytes(store_view *v, int kind, int index)
{
	const st_card *c = &v->c;

	switch (kind) {
	case SR_ART:     return c->unused_art;
	case SR_STATES:  return c->old_states;
	case SR_SYSTEM:  return store_sys_bytes(&c->sys[index]);
	case SR_VIDEOS:  return c->videos;
	case SR_MUSIC:   return c->music + c->bgm;
	case SR_BOOKS:   return c->books;
	case SR_BIOS:    return c->bios;
	case SR_ZYPEROS: return store_zyperos(c);
	case SR_OTHER:   return store_other(c);
	default:         return 0;
	}
}

/* The stand-in sizes a shot gives the demonstration card's games */
static unsigned long long store_fixture_game(const char *title)
{
	static const struct { const char *word; unsigned long long mb; } SZ[] = {
		{ "Metal Gear", 1240 }, { "Castlevania", 690 }, { "Tekken", 560 },
		{ "Crash", 470 }, { "Tony Hawk", 390 }, { "Spyro", 330 },
	};
	size_t i;

	for (i = 0; i < sizeof SZ / sizeof SZ[0]; i++)
		if (strstr(title, SZ[i].word)) return SZ[i].mb * 1000000ull;
	return 120000000ull;
}

static bool store_game_size(store_view *v, const char *folder, const game_entry *g, st_game_size *out)
{
	if (v->fixture) {
		memset(out, 0, sizeof *out);
		out->bytes = store_fixture_game(g->title);
		out->games = out->bytes;
		/* States on the largest game alone */
		if (strstr(g->title, "Metal Gear")) {
			out->games = out->bytes * 9 / 10;
			out->states = out->bytes - out->games;
		}
		return true;
	}
	return st_game(folder, g->file, out);
}

/* Each list's order, X's (src/storage.h's st_sort), kept as Sort By keeps
 * a shelf's (src/sort.h): the card's, each system's by its tag, each media
 * folder's. A shot's are --storage-sort's, and a shot writes none. */
int g_store_sort_shot = -1;

static void store_sort_key(const store_view *v, char *out, size_t n)
{
	if (v->media >= 0) {
		snprintf(out, n, "storage.sort.media.%s", st_media_root((st_media_kind)v->media));
	} else if (v->page >= 0) {
		const system_cfg *s = store_cfg((store_view *)v, v->c.sys[v->page].folder);

		snprintf(out, n, "storage.sort.%s", s && s->tag[0] ? s->tag : v->c.sys[v->page].folder);
	} else {
		snprintf(out, n, "%s", "storage.sort");
	}
}

/* A media folder has no Last Played: MP4 keeps no record of what was played */
static bool store_has_last(const store_view *v)
{
	return v->media < 0;
}

static st_sort store_sort_mode(const store_view *v)
{
	char key[CFG_STR + 24];
	int m;

	if (v->fixture) {
		m = g_store_sort_shot;
	} else {
		store_sort_key(v, key, sizeof key);
		m = db_get_int(db_dev(), key, ST_SORT_SIZE);
	}
	if (m < 0 || m >= ST_SORTS || (m == ST_SORT_LAST && !store_has_last(v))) m = ST_SORT_SIZE;
	return (st_sort)m;
}

static void store_sort_step(store_view *v)
{
	char key[CFG_STR + 24];

	v->sort = st_sort_next(v->sort, store_has_last(v));
	if (v->fixture) return;
	store_sort_key(v, key, sizeof key);
	db_set_int(db_dev(), key, (int)v->sort);
}

static const char *store_sort_label(st_sort mode)
{
	return mode == ST_SORT_AZ ? T(S_SYSMENU_ORDER_AZ) : mode == ST_SORT_LAST ? T(S_SORT_LAST_PLAYED)
	                                                                          : T(S_STORAGE_SORT_SIZE);
}

/* A system's games' last time played: its newest */
static long store_sys_last(const store_view *v, const char *folder)
{
	long last = 0;
	int i;

	for (i = 0; i < v->ngames; i++)
		if (!strcmp(v->games[i].folder, folder) && v->games[i].last_played > last)
			last = v->games[i].last_played;
	return last;
}

/* The stand-in times a shot gives systems and games, by a word of their
 * names: days ago, so Last Played has an order to show; 0 never played */
static long store_fixture_last(const char *name)
{
	static const struct { const char *word; int days; } AGO[] = {
		{ "Castlevania", 1 }, { "Crash", 6 }, { "Tekken", 30 },
		{ "PlayStation", 1 }, { "Nintendo DS", 3 }, { "Game Boy Advance", 12 }, { "SNES", 40 },
		{ "Super Nintendo", 40 },
	};
	size_t i;

	for (i = 0; name && i < sizeof AGO / sizeof AGO[0]; i++)
		if (strstr(name, AGO[i].word)) return (long)time(NULL) - AGO[i].days * 86400L;
	return 0;
}

/* What the stand-in card's media folders hold, for a shot: albums, films
 * and books sized as the card's Music, Videos and Audiobooks are, to the
 * covers and loose files beside them */
static int store_fixture_media(int kind, st_media **out)
{
	static const struct { int kind; const char *name, *parent; unsigned mb; int count; bool folder; } M[] = {
		{ STM_MUSIC, "Neon Harbor", "The Lowlights", 812, 40, true },
		{ STM_MUSIC, "Paper Satellites", "Mira Vale", 744, 36, true },
		{ STM_MUSIC, "Slow Static", "The Lowlights", 690, 32, true },
		{ STM_MUSIC, "Coastline Radio", "Juno Park", 655, 38, true },
		{ STM_MUSIC, "Afterglow Sessions", "Mira Vale", 610, 30, true },
		{ STM_MUSIC, "Quiet Machines", "Oslo Drive", 540, 34, true },
		{ STM_MUSIC, "Small Hours", "Juno Park", 480, 26, true },
		{ STM_MUSIC, "Singles", "", 371, 50, true },
		{ STM_MUSIC, "Live at the Pier", "Oslo Drive", 220, 32, true },
		{ STM_VIDEOS, "Sintel", "", 4200, 1, false },
		{ STM_VIDEOS, "Nature Walks", "", 3900, 2, true },
		{ STM_VIDEOS, "Tears of Steel", "", 3800, 1, false },
		{ STM_VIDEOS, "Big Buck Bunny", "", 3600, 1, false },
		{ STM_VIDEOS, "Elephants Dream", "", 2900, 1, false },
		{ STM_BOOKS, "Pride and Prejudice", "Jane Austen", 520, 61, true },
		{ STM_BOOKS, "The Time Machine", "H. G. Wells", 380, 12, true },
	};
	st_media *v;
	int i, n = 0;

	*out = NULL;
	if (!(v = calloc(sizeof M / sizeof M[0], sizeof *v))) return 0;
	for (i = 0; i < (int)(sizeof M / sizeof M[0]); i++) {
		st_media *m = &v[n];

		if (M[i].kind != kind) continue;
		snprintf(m->name, sizeof m->name, "%s", M[i].name);
		snprintf(m->parent, sizeof m->parent, "%s", M[i].parent);
		if (!M[i].folder) snprintf(m->rel, sizeof m->rel, "%s.mp4", M[i].name);
		else if (M[i].parent[0]) snprintf(m->rel, sizeof m->rel, "%s/%s", M[i].parent, M[i].name);
		else snprintf(m->rel, sizeof m->rel, "%s", M[i].name);
		m->bytes = M[i].mb * 1000000ull;
		m->count = M[i].count;
		m->folder = M[i].folder;
		n++;
	}
	*out = v;
	return n;
}

/* A media page's whole: its folder, and Music's the menu's own tracks too
 * (Menu Music), as the card's Music row counts them */
static unsigned long long store_media_bytes(const st_card *c, int kind)
{
	return kind == STM_MUSIC ? c->music + c->bgm : kind == STM_VIDEOS ? c->videos : c->books;
}

static sp_row *store_add(store_view *v)
{
	if (v->nrows == v->cap) {
		int cap = v->cap ? v->cap * 2 : 32;
		sp_row *n = realloc(v->rows, (size_t)cap * sizeof *n);

		if (!n) return NULL;
		v->rows = n;
		v->cap = cap;
	}
	memset(&v->rows[v->nrows], 0, sizeof v->rows[v->nrows]);
	return &v->rows[v->nrows++];
}

/* The card's folders that are no system's, by the design they wear
 * (res/system-art/original): Music's vinyl album and the others' own art,
 * as on the File Transfer page */
#define STORE_ART_MUSIC  "MUSIC"
#define STORE_ART_VIDEOS "VIDEOS"
#define STORE_ART_BOOKS  "BOOKS"
#define STORE_ART_BIOS   "BIOS"

static void store_row_look(store_view *v, sp_row *r)
{
	const st_card *c = &v->c;

	switch (r->kind) {
	case SR_ART:     r->name = T(S_STORAGE_UNUSED_ART); r->icon = SP_ICON_SPARK; break;
	case SR_STATES:  r->name = T(S_STORAGE_OLD_STATES); r->icon = SP_ICON_SPARK; break;
	case SR_SYSTEM: {
		const system_cfg *s = store_cfg(v, c->sys[r->index].folder);

		r->name = s ? s->name : c->sys[r->index].folder;
		r->icon = SP_ICON_TEX;
		r->tex = s ? store_tex(v, home_art_key(s->tag)) : NULL;
		break;
	}
	case SR_VIDEOS:  r->name = T(S_STORAGE_VIDEOS); r->icon = SP_ICON_TEX; r->tex = store_tex(v, STORE_ART_VIDEOS); break;
	case SR_MUSIC:   r->name = T(S_STORAGE_MUSIC);  r->icon = SP_ICON_TEX; r->tex = store_tex(v, STORE_ART_MUSIC); break;
	case SR_BOOKS:   r->name = T(S_STORAGE_BOOKS);  r->icon = SP_ICON_TEX; r->tex = store_tex(v, STORE_ART_BOOKS); break;
	case SR_BIOS:    r->name = T(S_STORAGE_BIOS);   r->icon = SP_ICON_TEX; r->tex = store_tex(v, STORE_ART_BIOS); break;
	case SR_ZYPEROS: r->name = T(S_STORAGE_CAT_ZYPEROS); r->icon = SP_ICON_ZYPER; break;
	case SR_OTHER:   r->name = T(S_STORAGE_CAT_OTHER); r->icon = SP_ICON_TEX; r->tex = store_tex(v, "FOLDER"); break;
	case SR_SNAPS:   r->name = T(S_STORAGE_DELETE_SNAPS); r->icon = SP_ICON_NONE; break;
	default: break;
	}
}

/* The focused row's facts, written when the cursor comes to it */
static void store_caption(store_view *v, sp_row *r)
{
	const st_card *c = &v->c;
	char a[24], b[24], part[96];

	r->caption[0] = '\0';
	switch (r->kind) {
	case SR_ART:
		snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_UNUSED_CAPTION));
		break;
	case SR_STATES:
		snprintf(r->caption, sizeof r->caption, TN(S_STORAGE_OLD_CAPTION, c->old_state_games),
		         c->old_state_games);
		break;
	case SR_SYSTEM: {
		const st_sys *s = &c->sys[r->index];
		int n = store_count(v, r->index);

		snprintf(r->caption, sizeof r->caption, TN(S_SHELF_GAMES, n), n);
		if (s->saves + s->states) {
			store_bytes(s->saves + s->states, a, sizeof a);
			snprintf(part, sizeof part, T(S_STORAGE_SAVES_PART), a);
			snprintf(r->caption + strlen(r->caption), sizeof r->caption - strlen(r->caption), " \xc2\xb7 %s", part);
		}
		if (s->covers + s->snaps) {
			store_bytes(s->covers + s->snaps, b, sizeof b);
			snprintf(part, sizeof part, T(S_STORAGE_ART_PART), b);
			snprintf(r->caption + strlen(r->caption), sizeof r->caption - strlen(r->caption), " \xc2\xb7 %s", part);
		}
		break;
	}
	case SR_VIDEOS: snprintf(r->caption, sizeof r->caption, TN(S_SHELF_VIDEOS, c->nvideos), c->nvideos); break;
	case SR_MUSIC:
		snprintf(r->caption, sizeof r->caption, TN(S_STORAGE_SONGS, c->nmusic + c->nbgm), c->nmusic + c->nbgm);
		break;
	case SR_BOOKS:  snprintf(r->caption, sizeof r->caption, TN(S_SHELF_BOOKS, c->nbooks), c->nbooks); break;
	case SR_BIOS:   snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_BIOS_CAPTION)); break;
	case SR_ZYPEROS: snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_ZYPEROS_CAPTION)); break;
	case SR_OTHER:  snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_OTHER_CAPTION)); break;
	case SR_SNAPS: {
		const system_cfg *s = store_cfg(v, c->sys[v->page].folder);

		snprintf(r->caption, sizeof r->caption, T(S_STORAGE_SNAPS_CAPTION), s ? s->name : c->sys[v->page].folder);
		break;
	}
	case SR_ORPHAN:
		snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_ORPHAN_CAPTION));
		break;
	case SR_MEDIA: {
		const st_media *m = r->index < v->nmed ? &v->med[r->index] : NULL;
		char count[64];

		if (!m) break;
		/* A folder: its songs, films or files; a file: its own name */
		if (!m->folder) {
			snprintf(count, sizeof count, "%s", m->rel);
		} else if (v->media == STM_MUSIC) {
			snprintf(count, sizeof count, TN(S_STORAGE_SONGS, m->count), m->count);
		} else if (v->media == STM_VIDEOS) {
			snprintf(count, sizeof count, TN(S_SHELF_VIDEOS, m->count), m->count);
		} else {
			snprintf(count, sizeof count, TN(S_STORAGE_FILES, m->count), m->count);
		}
		if (m->parent[0]) snprintf(r->caption, sizeof r->caption, "%s \xc2\xb7 %s", m->parent, count);
		else snprintf(r->caption, sizeof r->caption, "%s", count);
		break;
	}
	case SR_BGM:
		/* The menu's own tracks, where they are and how many */
		snprintf(part, sizeof part, TN(S_STORAGE_SONGS, c->nbgm), c->nbgm);
		snprintf(r->caption, sizeof r->caption, "%s \xc2\xb7 %s", BGM_CARD_DIR, part);
		break;
	case SR_REST:
		snprintf(r->caption, sizeof r->caption, "%s",
		         T(v->media >= 0 ? S_STORAGE_MEDIA_REST_CAPTION : S_STORAGE_REST_CAPTION));
		break;
	case SR_GAME: {
		int sh = store_shelf(v->a, c->sys[v->page].folder);
		const game_entry *g = sh >= 0 && r->index < sys_games(&v->a->view[sh])->count
		                    ? &sys_games(&v->a->view[sh])->items[r->index] : NULL;
		const char *tag = sh >= 0 ? v->a->sys.systems[sh].tag : "";
		long secs = 0, last = 0;
		st_game_size sz;

		if (!g) break;
		/* A shot's games have its stand-in times, as its orders do */
		if (v->fixture && (last = store_fixture_last(g->title))) secs = 12L * 3600 + 30 * 60;
		if ((v->fixture ? secs > 0 : stats_lookup(tag, g->file, &secs, &last)) && secs > 0) {
			char t[32], ago[48], la[64];

			stats_format(secs, t, sizeof t);
			stats_ago(last, (long)time(NULL), ago, sizeof ago);
			snprintf(r->caption, sizeof r->caption, T(S_GAMES_PLAYED), t);
			snprintf(la, sizeof la, T(S_STORAGE_LAST_PLAYED), ago);
			snprintf(r->caption + strlen(r->caption), sizeof r->caption - strlen(r->caption), " \xc2\xb7 %s", la);
		} else {
			snprintf(r->caption, sizeof r->caption, "%s", T(S_STORAGE_NEVER_PLAYED));
		}
		if (store_game_size(v, c->sys[v->page].folder, g, &sz) && sz.saves + sz.states) {
			store_bytes(sz.saves + sz.states, a, sizeof a);
			snprintf(part, sizeof part, T(S_STORAGE_SAVES_PART), a);
			snprintf(r->caption + strlen(r->caption), sizeof r->caption - strlen(r->caption), " \xc2\xb7 %s", part);
		}
		break;
	}
	default: break;
	}
}

/* The rest of a list's whole, what none of its rows holds, as a last row
 * when it is a megabyte or more */
static void store_rest(store_view *v, unsigned long long whole, unsigned long long owned)
{
	sp_row *r;

	if (whole <= owned || whole - owned < 1000000ull || !(r = store_add(v))) return;
	r->kind = SR_REST;
	r->name = T(S_STORAGE_REST);
	store_bytes(whole - owned, r->value, sizeof r->value);
	r->icon = SP_ICON_NONE;
}

/* The page's rows from the numbers as they are, in the list's order (X's),
 * the cursor kept on the row it was on */
static void store_rows(store_view *v)
{
	const st_card *c = &v->c;
	int was_kind = -1, was_index = -1, i, n = 0;
	st_item *it;

	if (v->nrows > 0 && v->pg.sel >= 0 && v->pg.sel < v->nrows) {
		was_kind = v->rows[v->pg.sel].kind;
		was_index = v->rows[v->pg.sel].index;
	}
	v->nrows = 0;
	v->cap_for = -1;
	v->sort = store_sort_mode(v);
	if (v->media >= 0) {
		/* A media folder's page: its albums, films or books, Music's menu
		 * tracks, and what none of them holds, so the rows come to the
		 * folder's whole */
		unsigned long long owned = 0, whole = store_media_bytes(c, v->media);
		bool bgm = v->media == STM_MUSIC && c->bgm > 0;

		free(v->med);
		v->nmed = v->fixture ? store_fixture_media(v->media, &v->med)
		                     : st_media_items((st_media_kind)v->media, &v->med);
		if ((v->nmed > 0 || bgm) && (it = malloc((size_t)(v->nmed + 1) * sizeof *it))) {
			for (i = 0; i < v->nmed; i++) {
				it[n++] = (st_item){ SR_MEDIA, i, v->med[i].bytes, true, v->med[i].name, 0 };
				owned += v->med[i].bytes;
			}
			if (bgm) {
				it[n++] = (st_item){ SR_BGM, 0, c->bgm, true, T(S_SYSMENU_MENU_MUSIC), 0 };
				owned += c->bgm;
			}
			st_sort_items(it, n, v->sort);
			for (i = 0; i < n; i++) {
				sp_row *r = store_add(v);

				if (!r) break;
				r->kind = it[i].kind;
				r->index = it[i].index;
				r->name = it[i].title;
				store_bytes(it[i].bytes, r->value, sizeof r->value);
				r->icon = SP_ICON_NONE;
			}
			free(it);
		}
		store_rest(v, whole, owned);
	} else if (v->page < 0) {
		int nr = 0;
		st_item recs[2];

		if (!c->measured) { v->pg.sel = 0; return; }
		/* The recommendations stay first, whatever the order */
		if (c->unused_art >= ST_REC_MIN) recs[nr++] = (st_item){ SR_ART, 0, c->unused_art, true, NULL, 0 };
		if (c->old_states >= ST_REC_MIN) recs[nr++] = (st_item){ SR_STATES, 0, c->old_states, true, NULL, 0 };
		if (nr == 2 && recs[1].bytes > recs[0].bytes) { st_item t = recs[0]; recs[0] = recs[1]; recs[1] = t; }
		for (i = 0; i < nr; i++) {
			sp_row *r = store_add(v);

			if (!r) break;
			r->kind = recs[i].kind;
			store_bytes(recs[i].bytes, r->value, sizeof r->value);
			r->rule_after = i == nr - 1;
			store_row_look(v, r);
		}
		if (!(it = malloc((size_t)(c->nsys + 8) * sizeof *it))) return;
		for (i = 0; i < c->nsys; i++)
			if (store_sys_bytes(&c->sys[i])) {
				const system_cfg *sc = store_cfg(v, c->sys[i].folder);
				const char *name = sc ? sc->name : c->sys[i].folder;

				it[n++] = (st_item){ SR_SYSTEM, i, store_sys_bytes(&c->sys[i]), true, name,
				                     v->fixture ? store_fixture_last(name) : store_sys_last(v, c->sys[i].folder) };
			}
		{
			static const int MORE[] = { SR_VIDEOS, SR_MUSIC, SR_BOOKS, SR_BIOS, SR_ZYPEROS, SR_OTHER };

			for (i = 0; i < (int)(sizeof MORE / sizeof MORE[0]); i++) {
				unsigned long long b = store_row_bytes(v, MORE[i], 0);
				sp_row look;

				if (!b) continue;
				memset(&look, 0, sizeof look);
				look.kind = MORE[i];
				store_row_look(v, &look);
				it[n++] = (st_item){ MORE[i], 0, b, true, look.name, 0 };
			}
		}
		st_sort_items(it, n, v->sort);
		for (i = 0; i < n; i++) {
			sp_row *r = store_add(v);

			if (!r) break;
			r->kind = it[i].kind;
			r->index = it[i].index;
			store_bytes(it[i].bytes, r->value, sizeof r->value);
			store_row_look(v, r);
		}
		free(it);
	} else {
		const st_sys *s = &c->sys[v->page];
		int sh = store_shelf(v->a, s->folder), cnt = sh >= 0 ? sys_games(&v->a->view[sh])->count : 0;

		if (s->snaps) {
			sp_row *r = store_add(v);

			if (r) {
				r->kind = SR_SNAPS;
				store_bytes(s->snaps, r->value, sizeof r->value);
				r->rule_after = cnt > 0;
				store_row_look(v, r);
			}
		}
		/* On the Ports shelf, its folders no script names too: a port whose
		 * script is gone, by its port.json's name, or a folder of nothing's */
		bool ports = v->sys.ports && !strcmp(s->folder, v->sys.ports);
		unsigned long long owned = 0, whole = store_sys_bytes(s);
		bool all_sized = true;

		v->norph = ports && !v->fixture ? st_orphans(v->orph, ST_ORPHANS) : 0;
		if ((cnt > 0 || v->norph > 0) && (it = malloc((size_t)(cnt + v->norph) * sizeof *it))) {
			for (i = 0; i < cnt; i++) {
				const game_entry *g = &sys_games(&v->a->view[sh])->items[i];
				st_game_size sz;
				bool ok = store_game_size(v, s->folder, g, &sz);
				long secs = 0, last = 0;

				/* Read only for the order that needs it: a lookup walks the
				 * play-time rows (src/sort.h) */
				if (v->sort == ST_SORT_LAST) {
					if (v->fixture) last = store_fixture_last(g->title);
					else stats_lookup(v->a->sys.systems[sh].tag, g->file, &secs, &last);
				}
				it[n++] = (st_item){ SR_GAME, i, ok ? sz.bytes : 0, ok, g->title, last };
				if (ok) owned += sz.bytes;
				else all_sized = false;
			}
			for (i = 0; i < v->norph; i++) {
				it[n++] = (st_item){ SR_ORPHAN, i, v->orph[i].bytes, true,
				                     v->orph[i].title[0] ? v->orph[i].title : v->orph[i].folder, 0 };
				owned += v->orph[i].bytes;
			}
			st_sort_items(it, n, v->sort);
			for (i = 0; i < n; i++) {
				sp_row *r = store_add(v);

				if (!r) break;
				r->kind = it[i].kind;
				r->index = it[i].index;
				r->name = it[i].kind == SR_GAME ? sys_games(&v->a->view[sh])->items[it[i].index].title : it[i].title;
				if (it[i].sized) store_bytes(it[i].bytes, r->value, sizeof r->value);
				r->icon = SP_ICON_NONE;
			}
			free(it);
		}
		/* What none of those owns - art of games gone, a loose file, a set
		 * two games share - so the rows come to the system's whole */
		if (all_sized) store_rest(v, whole, owned);
	}
	/* Back on the row it was on, or the nearest */
	if (v->pg.sel >= v->nrows) v->pg.sel = v->nrows ? v->nrows - 1 : 0;
	if (was_kind >= 0)
		for (i = 0; i < v->nrows; i++)
			if (v->rows[i].kind == was_kind && v->rows[i].index == was_index) { v->pg.sel = i; break; }
	if (v->pg.sel < 0) v->pg.sel = 0;
}

static void store_part(sp_page *p, unsigned long long bytes, SDL_Color c, const char *name,
                       const char *value, bool stripes)
{
	sp_part *pt;

	if (p->nparts >= SP_PARTS) return;
	pt = &p->part[p->nparts++];
	pt->bytes = bytes;
	pt->col = c;
	pt->stripes = stripes;
	pt->name = name;
	snprintf(pt->value, sizeof pt->value, "%s", value ? value : "");
}

/* A media page's name, its heading and its questions' */
static const char *store_media_name(int kind)
{
	return T(kind == STM_MUSIC ? S_STORAGE_MUSIC : kind == STM_VIDEOS ? S_STORAGE_VIDEOS : S_STORAGE_BOOKS);
}

/* The page's words and bar from the numbers */
static void store_page(store_view *v)
{
	const ds_theme *th = theme();
	const st_card *c = &v->c;
	sp_page *p = &v->pg;
	char sz[24];
	int sel = p->sel;
	sp_row *rows = v->rows;

	memset(p, 0, sizeof *p);
	p->sel = sel;
	p->rows = rows;
	p->nrows = v->nrows;
	if (v->media >= 0) {
		static const char *const KEY[STM_KINDS] = { STORE_ART_MUSIC, STORE_ART_VIDEOS, STORE_ART_BOOKS };
		sp_row look;
		unsigned long long whole = store_media_bytes(c, v->media);
		char val[24];

		memset(&look, 0, sizeof look);
		look.kind = v->media == STM_MUSIC ? SR_MUSIC : v->media == STM_VIDEOS ? SR_VIDEOS : SR_BOOKS;
		store_row_look(v, &look);
		p->heading = look.name;
		p->head_tex = store_tex(v, KEY[v->media]);
		p->used_fmt = "%s";
		store_bytes(whole, p->used, sizeof p->used);
		look.caption[0] = '\0';
		store_caption(v, &look);
		snprintf(p->right, sizeof p->right, "%s", look.caption);
		/* Its share of the whole card */
		p->whole = c->total;
		store_bytes(whole, val, sizeof val);
		store_part(p, whole, ui_col(th->text_soft, 255), look.name, val, false);
		if (p->nrows > 0) p->sort_label = store_sort_label(v->sort);
		if (p->nrows > 0 && v->cap_for != p->sel) {
			store_caption(v, &v->rows[p->sel]);
			v->cap_for = p->sel;
		}
		return;
	}
	if (v->page < 0 && !c->measured && !c->walking) {
		/* The few frames before the walk's first word: the heading alone,
		 * never a flash of the first walk's page on a card with a cache */
		p->heading = T(S_STORAGE_TITLE);
		p->nrows = 0;
		p->used_fmt = NULL;
		return;
	}
	if (v->page < 0) {
		unsigned long long used = c->total > c->free ? c->total - c->free : 0;

		p->heading = T(S_STORAGE_TITLE);
		if (c->total) {
			snprintf(sz, sizeof sz, "%llu", st_nominal(c->total) / 1000000000ull);
			snprintf(p->heading_right, sizeof p->heading_right, T(S_SYSMENU_SIZE_GB), sz);
		}
		p->used_fmt = T(S_STORAGE_USED);
		store_bytes(used, p->used, sizeof p->used);
		store_bytes(c->free, sz, sizeof sz);
		snprintf(p->right, sizeof p->right, T(S_STORAGE_FREE), sz);
		p->whole = c->total;
		store_part(p, c->cat[STC_GAMES], ui_col(th->store_games, 255), T(S_STORAGE_CAT_GAMES), NULL, false);
		store_part(p, c->cat[STC_SAVES], ui_col(th->store_saves, 255), T(S_STORAGE_CAT_SAVES), NULL, false);
		store_part(p, c->cat[STC_ART], ui_col(th->store_art, 255), T(S_STORAGE_CAT_ART), NULL, false);
		store_part(p, c->cat[STC_MEDIA], ui_col(th->text_soft, 255), T(S_STORAGE_CAT_MEDIA), NULL, false);
		store_part(p, c->cat[STC_ZYPEROS], ui_col(th->accent_ink, 255), T(S_STORAGE_CAT_ZYPEROS), NULL, false);
		if (c->measured) {
			store_part(p, c->cat[STC_OTHER], ui_col(th->text_dim, 255), T(S_STORAGE_CAT_OTHER), NULL, false);
		} else {
			int k;

			/* The first walk: what it has not reached yet is striped, and a
			 * part with nothing yet is not in the legend */
			for (k = 0; k < p->nparts; k++) if (!p->part[k].bytes) p->part[k].name = NULL;
			store_part(p, c->cat[STC_OTHER], ui_col(th->text_dim, 255), T(S_STORAGE_MEASURING_PART), NULL, true);
			p->measuring = true;
			p->measure_line = T(S_STORAGE_MEASURING);
			if (c->at[0]) {
				const system_cfg *s = store_cfg(v, c->at);

				snprintf(p->measure_at, sizeof p->measure_at, T(S_STORAGE_PROGRESS),
				         s ? s->name : c->at, c->at_done, c->at_total);
			}
			p->note = T(S_STORAGE_FIRST_NOTE);
		}
	} else {
		const st_sys *s = &c->sys[v->page];
		const system_cfg *cf = store_cfg(v, s->folder);
		int n = store_count(v, v->page);
		char g[24], sv[24], ar[24];

		p->heading = cf ? cf->name : s->folder;
		p->head_tex = cf ? store_tex(v, home_art_key(cf->tag)) : NULL;
		p->used_fmt = "%s";
		store_bytes(store_sys_bytes(s), p->used, sizeof p->used);
		snprintf(p->right, sizeof p->right, TN(S_SHELF_GAMES, n), n);
		p->whole = store_sys_bytes(s);
		store_bytes(s->games, g, sizeof g);
		store_bytes(s->saves + s->states, sv, sizeof sv);
		store_bytes(s->covers + s->snaps, ar, sizeof ar);
		store_part(p, s->games, ui_col(th->store_games, 255), T(S_STORAGE_CAT_GAMES), g, false);
		store_part(p, s->saves + s->states, ui_col(th->store_saves, 255), T(S_STORAGE_CAT_SAVES), sv, false);
		store_part(p, s->covers + s->snaps, ui_col(th->store_art, 255), T(S_STORAGE_CAT_ART), ar, false);
	}
	if (p->nrows > 0 && !p->measuring) p->sort_label = store_sort_label(v->sort);
	if (p->nrows > 0 && v->cap_for != p->sel) {
		store_caption(v, &v->rows[p->sel]);
		v->cap_for = p->sel;
	}
}

/* Zyper for the page: his face on ZyperOS's row (tongue out), and over the
 * first walk with his head cocked and his shadow on Beard (tongue in) */
void store_zyper(SDL_Renderer *r, float cx, float cy, float h, float tilt, bool shadow)
{
	if (shadow) zyper_shadow_draw(r, cx, cy, h, tilt, 1.0f);
	draw_zyper(r, cx, cy, h, tilt, shadow ? 0.0f : 1.0f, 0.0f, 255);
}

static void store_draw(app *a, store_view *v)
{
	draw_background(a);
	store_page(v);
	v->pg.zyper = store_zyper;
	sp_draw(a->r, &v->pg, &v->m, anim_now());
}

static void store_base(app *a, void *ctx)
{
	store_draw(a, ctx);
}

/* A question as Delete Game asks it (delete_ask_rows): its lines wrapped
 * to the panel, a rule, then the answer and Cancel. `rows` holds
 * DEL_ASK_LINES + 2. */
int store_ask_rows(app *a, const char *q, char (*lines)[CHV_WRAP_COLS], menu_row *rows)
{
	bool rule;
	int fit = delete_ask_fit(&rule), n, i;

	n = wrap_text(ui_font(UI_F_MENU), q, menu_std_width(a), lines, DEL_ASK_LINES);
	if (n > fit) n = fit;
	for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], MENU_NOTE_MARK, false };
	if (n < fit || rule) rows[n++] = MENU_HR;
	return n;
}

bool store_ask(app *a, const char *heading, const char *q, const char *yes_label)
{
	return ask_panel(a, NULL, NULL, heading, q, yes_label);
}

bool ask_panel(app *a, const menu_style *over, void *ctx, const char *heading,
               const char *q, const char *yes_label)
{
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
	menu_row rows[DEL_ASK_LINES + 2];
	int nq = store_ask_rows(a, q, lines, rows), lv = panel_open();
	bool yes = confirm_rows_over(a, heading, rows, nq, yes_label, menu_std_width(a), lv,
	                             over, ctx);

	panel_close(a, lv, !a->running || (over && plat_run_power_pressed()));
	return yes && a->running;
}

/* After a delete: the shelves may have changed, so the walk is told again */
static void store_after(store_view *v)
{
	store_request(v);
	store_refresh(v);
}

static void store_said(app *a, const char *heading, unsigned long long bytes, str_id fmt)
{
	char sz[24], said[96];

	sys_menu_bytes(bytes, sz, sizeof sz);
	snprintf(said, sizeof said, T(fmt), sz);
	result_message(a, heading, said);
}

/* A on the focused row */
static void store_act(store_view *v)
{
	app *a = v->a;
	sp_row *r;
	char q[640], sz[24];

	if (v->nrows <= 0) { uisound_play(DS_SOUND_DENY); return; }
	r = &v->rows[v->pg.sel];
	switch (r->kind) {
	case SR_SYSTEM:
		uisound_play(DS_SOUND_ACCEPT);
		v->main_sel = v->pg.sel;
		v->page = r->index;
		v->pg.sel = 0;
		memset(&v->m, 0, sizeof v->m);
		v->nrows = 0;
		store_rows(v);
		break;
	case SR_ART: {
		unsigned long long b;
		int files;

		uisound_play(DS_SOUND_ACCEPT);
		store_bytes(v->c.unused_art, sz, sizeof sz);
		snprintf(q, sizeof q, T(S_STORAGE_UNUSED_ASK), sz);
		if (!store_ask(a, T(S_STORAGE_UNUSED_ART), q, T(S_GAMEMENU_DELETE_YES))) break;
		st_stop();
		wait_panel(a, T(S_STORAGE_UNUSED_ART), T(S_GAMEMENU_DELETING));
		b = st_delete_unused_art(&v->paths, &v->sys, v->games, v->ngames, &files);
		fprintf(stderr, "storage: unused art: %d files, %llu bytes\n", files, b);
		store_said(a, T(S_STORAGE_UNUSED_ART), b, S_GAMEMENU_DELETED);
		store_after(v);
		break;
	}
	case SR_STATES: {
		unsigned long long b;
		int games;

		uisound_play(DS_SOUND_ACCEPT);
		store_bytes(v->c.old_states, sz, sizeof sz);
		snprintf(q, sizeof q, TN(S_STORAGE_OLD_ASK, v->c.old_state_games), v->c.old_state_games, sz);
		if (!store_ask(a, T(S_STORAGE_OLD_STATES), q, T(S_GAMEMENU_DELETE_YES))) break;
		st_stop();
		wait_panel(a, T(S_STORAGE_OLD_STATES), T(S_GAMEMENU_DELETING));
		b = st_delete_old_states(&v->paths, &v->sys, v->games, v->ngames, (long)time(NULL), &games);
		fprintf(stderr, "storage: old states: %d games, %llu bytes\n", games, b);
		store_said(a, T(S_STORAGE_OLD_STATES), b, S_GAMEMENU_DELETED);
		store_after(v);
		break;
	}
	case SR_SNAPS: {
		const char *folder = v->c.sys[v->page].folder;
		unsigned long long b;

		uisound_play(DS_SOUND_ACCEPT);
		store_bytes(v->c.sys[v->page].snaps, sz, sizeof sz);
		snprintf(q, sizeof q, T(S_SNAPS_DELETE_ASK), sz);
		if (!store_ask(a, T(S_STORAGE_DELETE_SNAPS), q, T(S_SNAPS_DELETE))) break;
		st_stop();
		snap_off();
		b = snaps_delete(P_ROMS, folder);
		store_said(a, T(S_STORAGE_DELETE_SNAPS), b, S_SNAPS_GIVEN_BACK);
		store_after(v);
		break;
	}
	case SR_MUSIC:
	case SR_VIDEOS:
	case SR_BOOKS:
		/* Its page: what it holds, by size (2026-11-08: "why can't I
		 * explore videos/songs in storage?") */
		uisound_play(DS_SOUND_ACCEPT);
		v->main_sel = v->pg.sel;
		v->media = r->kind == SR_MUSIC ? STM_MUSIC : r->kind == SR_VIDEOS ? STM_VIDEOS : STM_BOOKS;
		v->pg.sel = 0;
		memset(&v->m, 0, sizeof v->m);
		v->nrows = 0;
		store_rows(v);
		break;
	case SR_MEDIA: {
		const st_media *m = r->index < v->nmed ? &v->med[r->index] : NULL;
		char rel[2 * ST_FOLDER + 2], abs[CFG_STR * 4];
		int kind = v->media;
		const char *head = store_media_name(kind);
		unsigned long long b;

		/* As Delete Game asks, under the page's name: an album is no game */
		if (!m) { uisound_play(DS_SOUND_DENY); break; }
		uisound_play(DS_SOUND_ACCEPT);
		snprintf(rel, sizeof rel, "%s", m->rel);
		store_bytes(m->bytes, sz, sizeof sz);
		snprintf(q, sizeof q, T(m->folder ? S_STORAGE_MEDIA_ASK_FOLDER : S_STORAGE_MEDIA_ASK_FILE), m->name, sz);
		if (!store_ask(a, head, q, T(S_GAMEMENU_DELETE_YES))) break;
		st_stop();
		wait_panel(a, head, T(S_GAMEMENU_DELETING));
		/* MP4 lets go of it first, if it plays or holds it (File Transfer's
		 * rule), and its shelf is read again after */
		snprintf(abs, sizeof abs, "%s/%s/%s", P_CARD, st_media_root((st_media_kind)kind), rel);
		muse_before_delete(abs);
		b = st_delete_media(&v->paths, (st_media_kind)kind, rel);
		fprintf(stderr, "storage: media %s: %llu bytes\n", rel, b);
		if (b) {
			rescan_all(a);
			store_said(a, head, b, S_GAMEMENU_DELETED);
		} else {
			result_message(a, head, T(S_GAMEMENU_DELETE_FAILED));
		}
		store_after(v);
		break;
	}
	case SR_ORPHAN: {
		const st_orphan *o = r->index < v->norph ? &v->orph[r->index] : NULL;
		char folder[ST_FOLDER];
		unsigned long long b;

		if (!o) { uisound_play(DS_SOUND_DENY); break; }
		uisound_play(DS_SOUND_ACCEPT);
		snprintf(folder, sizeof folder, "%s", o->folder);
		store_bytes(o->bytes, sz, sizeof sz);
		snprintf(q, sizeof q, T(S_STORAGE_ORPHAN_ASK), o->title[0] ? o->title : o->folder, sz);
		if (!store_ask(a, T(S_GAMEMENU_DELETE), q, T(S_GAMEMENU_DELETE_YES))) break;
		st_stop();
		wait_panel(a, T(S_GAMEMENU_DELETE), T(S_GAMEMENU_DELETING));
		b = st_delete_port_folder(&v->paths, &v->sys, v->games, v->ngames, folder);
		fprintf(stderr, "storage: port folder %s: %llu bytes\n", folder, b);
		if (b) store_said(a, T(S_GAMEMENU_DELETE), b, S_GAMEMENU_DELETED);
		else result_message(a, T(S_GAMEMENU_DELETE), T(S_GAMEMENU_DELETE_FAILED));
		store_after(v);
		break;
	}
	case SR_GAME: {
		int sh = store_shelf(a, v->c.sys[v->page].folder);
		char said[256];

		if (sh < 0 || r->index >= sys_games(&a->view[sh])->count) { uisound_play(DS_SOUND_DENY); break; }
		uisound_play(DS_SOUND_ACCEPT);
		/* Nothing may make a plan while the walk makes its own (gamedel.c's
		 * buffers are its own) */
		st_stop();
		game_delete(a, sh, &sys_games(&a->view[sh])->items[r->index], said, sizeof said);
		if (said[0] && a->running) result_message(a, T(S_GAMEMENU_DELETE), said);
		store_after(v);
		break;
	}
	default:
		/* A folder that is only read here: Videos, Music, BIOS... */
		uisound_play(DS_SOUND_DENY);
		break;
	}
}

static void store_open(store_view *v, app *a)
{
	char path[CFG_STR * 2];

	memset(v, 0, sizeof *v);
	v->a = a;
	v->page = -1;
	v->media = -1;
	v->cap_for = -1;
	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	v->cfg_ok = cfg_load_systems(path, &v->cfg);
	if (v->cfg_ok) shelf_folders_apply(&v->cfg);
}

static void store_close(store_view *v)
{
	store_free_icons(v);
	free(v->rows);
	free(v->games);
	free(v->med);
	v->rows = NULL;
	v->games = NULL;
	v->med = NULL;
}

/* The page as a base, as Muse's shelf is: the screen's, and --storage
 * delete-open's, which holds it to the panel that opens on it */
int store_base_open(void)
{
	return panel_base();
}

void storage_screen(app *a)
{
	store_view *v = calloc(1, sizeof *v);
	int base;

	if (!v) return;
	store_open(v, a);
	store_request(v);
	store_refresh(v);
	/* The page is a base, as Muse's shelf is (panel_base): MENU's panel
	 * leaves over it, and a panel opened on it - Delete Game's question,
	 * a recommendation's, "Deleting...", a result - comes from nothing.
	 * Without it the question opened as a swap from the last panel drawn,
	 * which left over the page first: a panel there, gone, and back (seen
	 * on the Pixel 2, 2026-11-08). */
	base = store_base_open();
	g_base_page = store_base;
	g_base_ctx = v;
	while (!want_quit && a->running) {
		st_card c;

		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { st_stop(); power_off(a); break; }
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* The walk's latest numbers, and the rows from them when they moved */
		st_snapshot(&c);
		/* The walk of the card is a long task: Auto Off waits for it
		 * (src/taskhold.h) */
		if (c.walking) task_note(TASK_STORAGE, plat_now_ms());
		if (c.gen != v->gen || v->nrows == 0) {
			v->c = c;
			v->gen = c.gen;
			store_rows(v);
		}

		if (a->in.pressed[IN_MENU]) {
			menu_leaving(a);
			break;
		}
		if (a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			if (v->page < 0 && v->media < 0) break;
			/* From a system or a media folder back to the card, on its row */
			v->page = -1;
			v->media = -1;
			v->pg.sel = v->main_sel;
			memset(&v->m, 0, sizeof v->m);
			v->nrows = 0;
			store_rows(v);
			v->pg.sel = v->main_sel < v->nrows ? v->main_sel : 0;
		} else if (v->nrows > 0) {
			in_button b = in_repeat(&a->in, IN_UP) ? IN_UP : in_repeat(&a->in, IN_DOWN) ? IN_DOWN : IN_NONE;

			if (b != IN_NONE) {
				v->pg.sel = list_step_heard(a, b, v->nrows, v->pg.sel);
			}
			if (a->in.pressed[IN_ACCEPT]) store_act(v);
			/* X: the list's next order, the cursor kept on its row; a value
			 * stepping round, heard as move (DESIGN.md 9) */
			else if (a->in.pressed[IN_X]) {
				step_sound(a, IN_X, 0, 1);
				store_sort_step(v);
				store_rows(v);
			}
		} else if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_DENY);
		}
		if (g_menu_closing) break;

		tick_tint(a);
		tick_theme(a);
		store_draw(a, v);
		/* A panel closed onto the page leaving it, with its scrim */
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
	/* What the walk finished is kept and cached; nothing walks once the
	 * page is gone, least of all under a game */
	st_stop();
	/* Back to MENU, whose panel comes in again as it opened; or with MENU
	 * pressed, every screen closing, nothing comes in */
	mm_close(base, anim_now(), 0, g_menu_closing);
	g_base_page = NULL;
	g_base_ctx = NULL;
	store_close(v);
	free(v);
}

/* --storage NAME [ROW]: the page on a stand-in card of 128 GB nearly full,
 * as the Storage lab drew it. NAME is main, system (the PlayStation shelf
 * of the card the shot runs on), art and states (a recommendation's
 * question), done (the card after Unused Art), delete (Delete Game's
 * question from the system's page), delete-open (that question opening,
 * with --shot-at, as the page opens it: shot_motion), first (the first
 * walk), music, videos or books (a media folder's page), media-delete (the
 * question for Music's first album) or videos-delete (for the first film). ROW is the focused row, by number
 * or by what it is (videos, music, books, bios, zyperos, other, rest, bgm).
 * --storage-sort size|az|last puts every list in that order. */
const char *shot_storage;
int shot_storage_sel = -1;
const char *shot_storage_row;

static void store_fixture(store_view *v, bool measured)
{
	static const struct {
		const char *folder;
		int count;
		unsigned long long games, covers, snaps, states, saves;   /* MB */
	} SYS[] = {
		{ "PlayStation", 116, 42600, 440, 1660, 1290, 12 },
		{ "Sega CD", 46, 15900, 18, 70, 180, 4 },
		{ "TurboGrafx-16", 61, 11200, 22, 60, 90, 3 },
		{ "Nintendo DS", 105, 8600, 36, 260, 520, 60 },
		{ "Ports", 12, 4100, 4, 0, 60, 0 },
		{ "Nintendo 64", 38, 1200, 15, 70, 260, 6 },
		{ "Game Boy Advance", 92, 800, 30, 60, 90, 9 },
		{ "Arcade", 140, 900, 50, 40, 20, 2 },
		{ "SNES", 60, 150, 20, 40, 20, 2 },
	};
	st_card *c = &v->c;
	int i, k;

	memset(c, 0, sizeof *c);
	c->total = 127900000000ull;
	c->free = 7400000000ull;
	c->partitions = 1100000000ull;
	c->zyperos = 1500000000ull;
	c->videos = 18400000000ull;
	c->nvideos = 6;
	c->music = 5160000000ull;
	c->nmusic = 308;
	c->bgm = 40000000ull;
	c->nbgm = 10;
	c->books = 900000000ull;
	c->nbooks = 2;
	c->bios = 300000000ull;
	for (i = 0; i < (int)(sizeof SYS / sizeof SYS[0]) && i < ST_MAX_SYS; i++) {
		st_sys *s = &c->sys[c->nsys];

		if (!store_cfg(v, SYS[i].folder)) continue;
		snprintf(s->folder, sizeof s->folder, "%s", SYS[i].folder);
		s->games = SYS[i].games * 1000000ull;
		s->covers = SYS[i].covers * 1000000ull;
		s->snaps = SYS[i].snaps * 1000000ull;
		s->states = SYS[i].states * 1000000ull;
		s->saves = SYS[i].saves * 1000000ull;
		c->saves += s->saves;
		c->states += s->states;
		v->fix_count[c->nsys] = SYS[i].count;
		c->nsys++;
	}
	c->sys[0].unused_art = 1200000000ull;
	c->sys[0].unused_covers = 212;
	c->sys[0].unused_snaps = 37;
	c->sys[3].old_states = 940000000ull;
	c->sys[3].old_state_games = 41;
	st_totals(c);
	c->measured = measured;
	if (!measured) {
		/* Partway through the first walk: the PlayStation's games */
		for (k = 1; k < c->nsys; k++) memset(&c->sys[k], 0, sizeof c->sys[k]);
		c->music = c->videos = c->books = c->bgm = c->bios = c->zyperos = c->partitions = 0;
		c->saves = c->states = 0;
		c->sys[0].states = c->sys[0].saves = 0;
		c->sys[0].games = 28300000000ull;
		st_totals(c);
		c->walking = true;
		snprintf(c->at, sizeof c->at, "%s", "PlayStation");
		c->at_done = 41;
		c->at_total = 116;
	}
}

void storage_shot(app *a)
{
	static store_view v;
	static bool made;
	const char *name = shot_storage;
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS], q[640], sz[24];
	menu_row rows[DEL_ASK_LINES + 2];
	int nq, i;

	if (!made) {
		made = true;
		store_open(&v, a);
		v.fixture = true;
		store_fixture(&v, strcmp(name, "first") != 0);
		if (!strcmp(name, "done")) {
			v.c.sys[0].unused_art = 0;
			v.c.sys[0].covers -= 400000000ull;
			v.c.sys[0].snaps -= 800000000ull;
			v.c.free += 1200000000ull;
			st_totals(&v.c);
		}
		if (!strcmp(name, "music") || !strcmp(name, "media-delete")) v.media = STM_MUSIC;
		if (!strcmp(name, "videos") || !strcmp(name, "videos-delete")) v.media = STM_VIDEOS;
		if (!strcmp(name, "books")) v.media = STM_BOOKS;
		if (!strcmp(name, "system") || !strcmp(name, "delete") || !strcmp(name, "delete-open")) {
			for (i = 0; i < v.c.nsys; i++)
				if (!strcmp(v.c.sys[i].folder, "PlayStation")) v.page = i;
			/* The shelf the shot's card has, sized as a nearly full card's */
			if (v.page >= 0) {
				int sh = store_shelf(a, "PlayStation");
				st_sys *s = &v.c.sys[v.page];

				v.fix_count[v.page] = sh >= 0 ? sys_games(&a->view[sh])->count : 0;
				s->games = s->states = 0;
				for (i = 0; sh >= 0 && i < sys_games(&a->view[sh])->count; i++) {
					st_game_size gz;

					store_game_size(&v, "PlayStation", &sys_games(&a->view[sh])->items[i], &gz);
					s->games += gz.games;
					s->states += gz.states;
				}
				s->saves = 4000000ull;
				s->covers = 9000000ull;
				s->snaps = 140000000ull;
			}
		}
		store_rows(&v);
		/* The focus: the asked row, else the first system's (the card) or
		 * the third (a system's page) */
		v.pg.sel = 0;
		for (i = 0; i < v.nrows; i++)
			if (v.page < 0 && v.rows[i].kind == SR_SYSTEM) { v.pg.sel = i; break; }
		if (v.page >= 0) v.pg.sel = v.nrows > 2 ? 2 : 0;
		if (v.media >= 0) v.pg.sel = 0;
		if (!strcmp(name, "art") || !strcmp(name, "states"))
			for (i = 0; i < v.nrows; i++)
				if (v.rows[i].kind == (!strcmp(name, "art") ? SR_ART : SR_STATES)) v.pg.sel = i;
		if (!strcmp(name, "done")) v.pg.sel = 0;
		/* The list as it is, for tools/storage-pages-check.py: every row,
		 * not the five the window shows */
		fprintf(stderr, "storage: order %s:", store_sort_label(v.sort));
		for (i = 0; i < v.nrows; i++) fprintf(stderr, "%s%s", i ? "|" : " ", v.rows[i].name ? v.rows[i].name : "");
		fprintf(stderr, "\n");
		if (shot_storage_sel >= 0 && shot_storage_sel < v.nrows) v.pg.sel = shot_storage_sel;
		if (shot_storage_row) {
			static const struct { const char *name; int kind; } ROWS[] = {
				{ "videos", SR_VIDEOS }, { "music", SR_MUSIC }, { "books", SR_BOOKS },
				{ "bios", SR_BIOS }, { "zyperos", SR_ZYPEROS }, { "other", SR_OTHER },
				{ "rest", SR_REST }, { "bgm", SR_BGM },
			};
			size_t k;

			for (k = 0; k < sizeof ROWS / sizeof ROWS[0]; k++)
				if (!strcmp(shot_storage_row, ROWS[k].name))
					for (i = 0; i < v.nrows; i++)
						if (v.rows[i].kind == ROWS[k].kind) v.pg.sel = i;
		}
	}
	store_draw(a, &v);
	if (!strcmp(name, "art") || !strcmp(name, "states")) {
		bool art = !strcmp(name, "art");

		store_bytes(art ? v.c.unused_art : v.c.old_states, sz, sizeof sz);
		if (art) snprintf(q, sizeof q, T(S_STORAGE_UNUSED_ASK), sz);
		else snprintf(q, sizeof q, TN(S_STORAGE_OLD_ASK, v.c.old_state_games), v.c.old_state_games, sz);
		nq = store_ask_rows(a, q, lines, rows);
		rows[nq] = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
		rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
		ui_scrim(a->r, 0);
		menu_draw(a, T(art ? S_STORAGE_UNUSED_ART : S_STORAGE_OLD_STATES), rows, nq + 2, nq + 1,
		          menu_std_width(a), MENU_ZYPEROS);
	} else if ((!strcmp(name, "delete") || !strcmp(name, "delete-open")) &&
	           v.nrows > 0 && v.rows[v.pg.sel].kind == SR_GAME) {
		int sh = store_shelf(a, "PlayStation");
		const char *heading;

		if (sh >= 0) {
			float k = 1.0f;

			nq = delete_ask_rows(a, sys_games(&a->view[sh])->items[v.rows[v.pg.sel].index].title, lines, rows, &heading);
			rows[nq] = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
			rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
			/* delete-open: where the question is in its open (shot_motion) */
			if (shot_lv) panel_frame(a, shot_lv, &k);
			ui_scrim_k(a->r, 0, k);
			menu_draw(a, heading, rows, nq + 2, nq + 1, menu_std_width(a), MENU_ZYPEROS);
		}
	} else if ((!strcmp(name, "media-delete") || !strcmp(name, "videos-delete")) &&
	           v.nrows > 0 && v.rows[v.pg.sel].kind == SR_MEDIA) {
		const st_media *m = &v.med[v.rows[v.pg.sel].index];

		store_bytes(m->bytes, sz, sizeof sz);
		snprintf(q, sizeof q, T(m->folder ? S_STORAGE_MEDIA_ASK_FOLDER : S_STORAGE_MEDIA_ASK_FILE), m->name, sz);
		nq = store_ask_rows(a, q, lines, rows);
		rows[nq] = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
		rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
		ui_scrim(a->r, 0);
		menu_draw(a, store_media_name(v.media), rows, nq + 2, nq + 1, menu_std_width(a), MENU_ZYPEROS);
	} else if (!strcmp(name, "done")) {
		char said[96];
		menu_row row;

		sys_menu_bytes(1200000000ull, sz, sizeof sz);
		snprintf(said, sizeof said, T(S_GAMEMENU_DELETED), sz);
		static const unsigned colors[1] = { VCOL_MESSAGE };

		row = (menu_row){ said, NULL, false };
		ui_scrim(a->r, 0);
		/* As result_message draws it (timed_message): measured to its line */
		menu_draw_ex(a, T(S_STORAGE_UNUSED_ART), &row, 1, -1, 0, MENU_ZYPEROS, colors, false, 0);
	}
}
