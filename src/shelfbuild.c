/* SPDX-License-Identifier: GPL-3.0-only */
#include "shelfbuild.h"

#include "db.h"
#include "esmedia.h"
#include "favorites.h"
#include "gamedel.h"
#include "musec.h"
#include "pminstall.h"
#include "savedlib.h"
#include "texload.h"
#include "theme.h"
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Drop systems with nothing in them.
 *
 * A card is a promise that opening it leads somewhere, and nine cards where
 * three have games is eight swipes to find the one you wanted. systems.cfg
 * stays the full list of what ZyperOS knows how to run; this is only what is
 * worth showing today.
 *
 * Compacted in place rather than filtered at draw time because sys_cursor
 * indexes sys.systems[] directly in a dozen places - the tint, the card art,
 * the launch, the resume - and an index layer over all of them would be a lot
 * of surface for a cosmetic rule. Both arrays move together or the shelf
 * shows one system's card over another's games.
 *
 * Textures are not freed here: this runs before prime_home, so there
 * are none yet. */
static void hide_empty_systems(app *a)
{
	int i, n = 0;

	for (i = 0; i < a->sys.count; i++) {
		if (a->view[i].list.count <= 0) continue;
		if (n != i) {
			a->sys.systems[n] = a->sys.systems[i];
			a->view[n] = a->view[i];
			memset(&a->view[i], 0, sizeof a->view[i]);
		}
		n++;
	}
	if (n != a->sys.count)
		fprintf(stderr, "scan: %d of %d systems have games\n", n, a->sys.count);
	a->sys.count = n;
	if (a->sys_cursor >= n) a->sys_cursor = n ? n - 1 : 0;
}

/* Build the Favorites shelf: one shelf whose games come from every other.
 *
 * Resolved against the shelves that actually scanned, so a favorite whose ROM
 * is off the card simply does not appear - which is the same rule as hiding
 * an empty system, and better than a card that opens onto a game that is not
 * there. Sorted by name like every other shelf rather than by the order
 * someone pressed Y, so it reads as a shelf and not as a history.
 *
 * Inserted at the front, which shifts every system index up by one, so the
 * owners recorded during resolution are corrected afterwards. Runs before
 * prime_home, so there are no textures to move with them.
 *
 * It is NOT a system: no core, no folder, no extensions. Nothing may read
 * those from a->sys.systems[] for a game on this shelf - shelf_owner() is how
 * every one of them is found instead. */
static void build_favorites_shelf(app *a)
{
	game_entry *items;
	int *owner;
	int n = 0, i, k, si, real;

	if (fav_count() <= 0 || a->sys.count <= 0) return;
	if (a->sys.count >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for a Favorites shelf\n");
		return;
	}

	items = calloc(FAV_MAX, sizeof *items);
	owner = calloc(FAV_MAX, sizeof *owner);
	if (!items || !owner) { free(items); free(owner); return; }

	real = a->sys.count;
	for (i = 0; i < fav_count(); i++) {
		const char *tag, *file;
		bool found = false;
		if (!fav_at(i, &tag, &file)) continue;
		for (si = 0; si < real; si++) {
			if (strcmp(a->sys.systems[si].tag, tag)) continue;
			for (k = 0; k < a->view[si].list.count; k++) {
				if (strcmp(a->view[si].list.items[k].file, file)) continue;
				items[n] = a->view[si].list.items[k];
				owner[n] = si;
				n++;
				found = true;
				break;
			}
			break;
		}
		/* Said out loud. A favorite that does not resolve is either a ROM
		 * that has left the card or a key that never matched, and silently
		 * showing one fewer game than the file lists is the kind of thing
		 * that gets noticed months later. */
		if (!found) fprintf(stderr, "fav: unresolved %s\t%s\n", tag, file);
		if (n >= FAV_MAX) break;
	}
	if (n == 0) { free(items); free(owner); return; }

	/* Insertion sort on the shown title, carrying the owner with it. n is at
	 * most FAV_MAX and realistically a dozen. */
	for (i = 1; i < n; i++) {
		game_entry t = items[i];
		int to = owner[i], j = i - 1;
		while (j >= 0 && lib_name_cmp(&items[j], &t) > 0) {
			items[j + 1] = items[j];
			owner[j + 1] = owner[j];
			j--;
		}
		items[j + 1] = t;
		owner[j + 1] = to;
	}

	for (i = a->sys.count; i > 0; i--) {
		a->sys.systems[i] = a->sys.systems[i - 1];
		a->view[i] = a->view[i - 1];
	}
	for (i = 0; i < n; i++) owner[i]++;          /* everything moved up one */

	memset(&a->sys.systems[0], 0, sizeof a->sys.systems[0]);
	snprintf(a->sys.systems[0].name, CFG_STR, "%s", T(S_LIBRARY_FAVORITES));
	snprintf(a->sys.systems[0].tag, sizeof a->sys.systems[0].tag, "%s", "FAV");
	snprintf(a->sys.systems[0].card, CFG_STR, "%s", "FAVORITES.png");
	/* ZyperOS's, not a console's: lime, as Muse is (see ZYPEROS_ACCENT) */
	a->sys.systems[0].accent = ZYPEROS_ACCENT;

	memset(&a->view[0], 0, sizeof a->view[0]);
	a->view[0].list.items = items;
	a->view[0].list.count = n;
	a->view[0].list.scanned = true;
	a->view[0].owner = owner;
	a->view[0].tex = calloc((size_t)n, sizeof *a->view[0].tex);
	a->view[0].tw  = calloc((size_t)n, sizeof *a->view[0].tw);
	a->view[0].th  = calloc((size_t)n, sizeof *a->view[0].th);
	/* cb with the rest. The scan allocates these four together and this path
	 * allocated three, so Favorites had a NULL where every other shelf had an
	 * array - and game_get_tex writes to it on the first card it draws. */
	a->view[0].cb  = calloc((size_t)n, sizeof *a->view[0].cb);
	if (!a->view[0].tex || !a->view[0].tw || !a->view[0].th || !a->view[0].cb) {
		free(a->view[0].tex); free(a->view[0].tw); free(a->view[0].th);
		free(a->view[0].cb);
		free(items); free(owner);
		memset(&a->view[0], 0, sizeof a->view[0]);
		for (i = 0; i < a->sys.count; i++) {
			a->sys.systems[i] = a->sys.systems[i + 1];
			a->view[i] = a->view[i + 1];
		}
		return;
	}
	a->sys.count++;
	fprintf(stderr, "scan: %-16s %d game%s\n", "Favorites", n, n == 1 ? "" : "s");
}

static bool fav_shelf_present(app *a)
{
	return a->sys.count > 0 && strcmp(a->sys.systems[0].tag, "FAV") == 0;
}

/* Take the Favorites shelf back off. Its game list and owner map are this
 * file's own allocations rather than lib_scan's, and the game_entry values in
 * it are copies - the originals belong to the shelves they came from and must
 * not be touched. */
static void drop_favorites_shelf(app *a)
{
	int i;

	if (!fav_shelf_present(a)) return;

	for (i = 0; i < a->view[0].list.count; i++)
		if (a->view[0].tex[i]) SDL_DestroyTexture(a->view[0].tex[i]);
	free(a->view[0].tex);
	free(a->view[0].tw);
	free(a->view[0].th);
	free(a->view[0].list.items);
	free(a->view[0].owner);

	for (i = 0; i + 1 < a->sys.count; i++) {
		a->sys.systems[i] = a->sys.systems[i + 1];
		a->view[i] = a->view[i + 1];
	}
	a->sys.count--;
	memset(&a->view[a->sys.count], 0, sizeof a->view[0]);
}

/* Rebuild the Favorites shelf in place, for use the moment a favorite
 * changes. Restarting the launcher to see a star take effect is not an
 * answer.
 *
 * Drop and rebuild rather than patch: the shelf is a sorted projection of a
 * set over every other shelf, and the four cases - it appears, it grows, it
 * shrinks, it goes away - are one line each this way and four separate
 * index-juggling routines the other.
 *
 * Everything is re-found by TAG afterwards, never by index. Inserting or
 * removing the shelf moves every system up or down by one, so the index the
 * caller was standing on means something different by the time this returns.
 * The tag does not move. */
void refresh_favorites_shelf(app *a)
{
	/* Favorites is rebuilt in place, so its indices now name other games. */
	texload_bump();
	char tag[sizeof a->sys.systems[0].tag];
	int cur, i, order = 0;

	if (a->sys.count <= 0) return;
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	cur = a->view[a->sys_cursor].cursor;
	for (i = 0; i < a->sys.count; i++)
		if (a->view[i].owner) { order = a->view[i].sort; break; }

	drop_favorites_shelf(a);
	build_favorites_shelf(a);
	/* Built in name order with its setting cleared, so the order chosen for
	 * it is put back: without this, favoriting a game turned the shelf back
	 * to Name, and the menu said Name, until the next boot read the setting
	 * again. */
	for (i = 0; i < a->sys.count; i++)
		if (a->view[i].owner) {
			a->view[i].sort = order;
			sort_shelf(a, i);
			break;
		}

	for (i = 0; i < a->sys.count; i++)
		if (strcmp(a->sys.systems[i].tag, tag) == 0) break;

	if (i < a->sys.count) {
		a->sys_cursor = i;
		/* The Favorites list can shrink under the cursor - un-favoriting the
		 * game you are looking at is the ordinary way to use this. */
		if (cur >= a->view[i].list.count)
			cur = a->view[i].list.count ? a->view[i].list.count - 1 : 0;
		a->view[i].cursor = cur;
		cf_reset(&a->view[i].cf, cur);
	} else {
		/* The shelf being stood on no longer exists, which happens exactly
		 * once: un-favoriting the last favorite while inside Favorites. There
		 * is no list to stay in, so go back out to the shelves. */
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Muse's shelf: a card per album, and which album each card is. Built in the
 * folder's own order, by artist; the order chosen for it is put on it by
 * sort_all once the setting has been read, the way every shelf's is. */
static void muse_fill_view(sysview *v)
{
	int n = g_muse.nalbums;

	if (n <= 0) return;
	v->list.items = calloc((size_t)n, sizeof *v->list.items);
	v->tex = calloc((size_t)n, sizeof *v->tex);
	v->tw = calloc((size_t)n, sizeof *v->tw);
	v->th = calloc((size_t)n, sizeof *v->th);
	v->cb = calloc((size_t)n, sizeof *v->cb);
	v->album = calloc((size_t)n, sizeof *v->album);
	if (!v->list.items || !v->tex || !v->tw || !v->th || !v->cb || !v->album) {
		free(v->list.items); free(v->tex); free(v->tw); free(v->th); free(v->cb);
		free(v->album);
		memset(v, 0, sizeof *v);
		return;
	}
	v->list.count = n;
	v->list.scanned = true;
	muse_order_view(v);
}

/* Muse's shelf in its order, v->sort: which album each card is, and each
 * card's entry from its album - the artist in `name`, the album in `title`,
 * which the card is called, and its folder in `file`, which is how a change
 * of order finds the album that was selected. Filled afresh rather than
 * sorted in place: an entry is only ever a copy of the library's names. The
 * letter jump goes by whatever the shelf is in order of. */
void muse_order_view(sysview *v)
{
	int k;

	/* The arrays hold every album; the shelf is the kind it shows. */
	if (!v->album) return;
	v->list.count = ml_shelf_order_kind(&g_muse, (ml_order)v->sort, muse_kind_shown(),
	                                    v->album);
	for (k = 0; k < v->list.count; k++) {
		game_entry *e = &v->list.items[k];
		int al = v->album[k];

		snprintf(e->name, sizeof e->name, "%s", g_muse.artists[muse_artist_of(al)].name);
		snprintf(e->title, sizeof e->title, "%s", g_muse.albums[al].name);
		muse_album_dir(al, e->file, sizeof e->file);
	}
	v->jump_by_name = v->sort == ML_BY_ARTIST;
}

/* A free name for `name` in `dir`: itself, or with " (2)", " (3)" ... before
 * its extension. False when none fits. */
static bool free_name(const char *dir, const char *name, char *out, size_t n)
{
	const char *dot = strrchr(name, '.');
	int stem = dot ? (int)(dot - name) : (int)strlen(name);
	int k;

	if (snprintf(out, n, "%s/%s", dir, name) >= (int)n) return false;
	for (k = 2; access(out, F_OK) == 0; k++) {
		if (k > 99) return false;
		if (snprintf(out, n, "%s/%.*s (%d)%s", dir, stem, name, k, dot ? dot : "")
		    >= (int)n) return false;
	}
	return true;
}

/* Songs loose at the top of Music/, in no folder, moved into Music/Singles,
 * where Muse reads them as an album (ml_album's `singles`): loose, they were
 * on no shelf. Eric's call, 2026-10-05. A name already taken there gets " (2)"
 * and on, so nothing is overwritten. And Singles gets its cover, shipped with
 * ZyperOS (tools/gensingles.py), at Music/.media/Singles.png, where every
 * album's is kept, unless it already has one. A rename on the same card, so
 * it costs a folder read when there is nothing to move. */
static void muse_tidy_singles(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16];
	char from[LIB_PATH * 2], to[LIB_PATH * 2 + 16];
	char (*loose)[256] = NULL;
	int nloose = 0, cap = 0, moved = 0, i;
	struct dirent *e;
	struct stat st;
	DIR *d;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (!(d = opendir(music))) return;
	/* Listed first, moved after: renaming out of a folder being read is
	 * allowed to show or skip entries. */
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !ml_is_audio(e->d_name)) continue;
		if (snprintf(from, sizeof from, "%s/%s", music, e->d_name) >= (int)sizeof from ||
		    stat(from, &st) != 0 || !S_ISREG(st.st_mode) ||
		    strlen(e->d_name) >= sizeof loose[0])
			continue;
		if (nloose == cap) {
			char (*more)[256] = realloc(loose, (size_t)(cap ? cap * 2 : 16) * sizeof *loose);

			if (!more) break;
			loose = more;
			cap = cap ? cap * 2 : 16;
		}
		snprintf(loose[nloose++], sizeof loose[0], "%s", e->d_name);
	}
	closedir(d);
	for (i = 0; i < nloose; i++) {
		mkdir(singles, 0755);
		snprintf(from, sizeof from, "%s/%s", music, loose[i]);
		if (free_name(singles, loose[i], to, sizeof to) && rename(from, to) == 0) moved++;
		else fprintf(stderr, "muse: could not move %s into %s\n", loose[i], ML_SINGLES);
	}
	free(loose);
	if (moved)
		fprintf(stderr, "muse: moved %d loose song%s into Music/%s\n",
		        moved, moved == 1 ? "" : "s", ML_SINGLES);
	singles_cover();
}

/* Singles' cover, the one shipped with ZyperOS, at Music/.media/Singles.png,
 * once there is a Singles and unless it has a cover already. At every scan,
 * and when the transfer screen opens, since a cover taken off the card there
 * was otherwise back only at the next restart (2026-10-05). */
void singles_cover(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16], cover[LIB_PATH * 2], src[LIB_PATH * 2];
	struct stat st;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (stat(singles, &st) != 0 || !S_ISDIR(st.st_mode)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.jpg", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.png", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(src, sizeof src, "%s/.media", music);
	mkdir(src, 0755);
	snprintf(src, sizeof src, "%s/singles.png", P_ROOT);
	copy_file(src, cover);
}

/* ---- Muse: albums filed under their artists -----------------------------
 *
 * An album folder straight in Music/ is its own artist on the shelf, so
 * Trompe Le Monde sorted under T rather than with the Pixies. Its tags say who
 * it is by, and Muse's player reads them (src/muse/tags.h): with a clear
 * answer it moves to Music/<artist>/<album>, its cover and Album Art's memory
 * of it with it, and the shelf is read again once. Eric's call, 2026-10-05.
 *
 * Left where it is when the tags give no clear answer (a podcast, a mix), when
 * the answer is its own name, when Music/<artist>/<album> is already there,
 * and for now when Muse is playing from it. Never Singles, never a book. The
 * player is not up at the first scan of a boot, so this happens a moment
 * after, one album at a time, from the main loop. */
typedef struct { char dir[LIB_PATH]; unsigned asked; bool done; } flat_album;
static flat_album *g_flat;
static int         g_nflat;
bool        g_muse_moved;            /* something moved: read it again */

/* The albums straight in Music/, after a scan. */
static void flat_albums_find(void)
{
	int i;

	free(g_flat);
	g_nflat = 0;
	g_flat = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1), sizeof *g_flat);
	if (!g_flat) return;
	for (i = 0; i < g_muse.nalbums; i++) {
		const ml_album *al = &g_muse.albums[i];
		const char *p, *slash;

		if (al->book || al->singles || al->n <= 0) continue;
		p = g_muse.tracks[al->first].path;            /* Music/<album>/<track> */
		if (strncmp(p, "Music/", 6) || !(slash = strchr(p + 6, '/')) ||
		    strchr(slash + 1, '/'))
			continue;
		snprintf(g_flat[g_nflat++].dir, sizeof g_flat[0].dir, "%.*s", (int)(slash - p), p);
	}
}

/* An artist's name as a folder: what the card cannot store in one becomes -,
 * and no leading or trailing space or trailing dot. */
static void folder_name(const char *in, char *out, size_t n)
{
	size_t o = 0, len;

	while (*in == ' ') in++;
	for (; *in && o + 1 < n; in++)
		out[o++] = strchr("/\\:*?\"<>|", *in) || (unsigned char)*in < 0x20 ? '-' : *in;
	out[o] = '\0';
	for (len = o; len && (out[len - 1] == ' ' || out[len - 1] == '.'); len--) out[len - 1] = '\0';
}

/* Move Music/<album> under its artist. 1 moved, 0 left where it is, -1 not
 * now (Muse is playing from it). */
static int file_album(const char *rel, const char *artist)
{
	const char *album = strrchr(rel, '/') + 1;
	char who[256], from[LIB_PATH * 2], dir[LIB_PATH * 2], to[LIB_PATH * 2 + 256];
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 600], key[LIB_PATH + 300], val[96];
	static const char *const EXT[] = { "jpg", "png" };
	size_t rl = strlen(rel);
	const char *t;
	int k;

	folder_name(artist, who, sizeof who);
	if (!who[0] || !strcmp(who, album) || !strcmp(who, ML_SINGLES)) return 0;
	for (k = 0; (t = musec_track(k)); k++)
		if (!strncmp(t, rel, rl) && t[rl] == '/') return -1;

	snprintf(from, sizeof from, "%s/%s", P_CARD, rel);
	snprintf(dir, sizeof dir, "%s/Music/%s", P_CARD, who);
	snprintf(to, sizeof to, "%s/%s", dir, album);
	if (access(to, F_OK) == 0) {
		fprintf(stderr, "muse: Music/%s stays: Music/%s/%s is there already\n",
		        album, who, album);
		return 0;
	}
	mkdir(dir, 0755);
	if (rename(from, to) != 0) {
		fprintf(stderr, "muse: could not move Music/%s under Music/%s\n", album, who);
		return 0;
	}
	/* Its cover, from Music/.media to the artist's. */
	for (k = 0; k < 2; k++) {
		snprintf(cfrom, sizeof cfrom, "%s/Music/.media/%s.%s", P_CARD, album, EXT[k]);
		if (access(cfrom, F_OK) != 0) continue;
		snprintf(cto, sizeof cto, "%s/.media", dir);
		mkdir(cto, 0755);
		snprintf(cto, sizeof cto, "%s/.media/%s.%s", dir, album, EXT[k]);
		rename(cfrom, cto);
	}
	/* And what Album Art remembers of it, so it is not fetched again. */
	snprintf(key, sizeof key, "museart.%s", rel);
	if (db_get_str(db_lib(), key, val, sizeof val, "") && val[0]) {
		db_del(db_lib(), key);
		snprintf(key, sizeof key, "museart.Music/%s/%s", who, album);
		db_set_str(db_lib(), key, val);
	}
	fprintf(stderr, "muse: filed Music/%s under Music/%s\n", album, who);
	return 1;
}

/* Whether any album is still waiting for its answer: the shelf is read again
 * once they all have one, not once per album moved. */
bool flat_albums_waiting(void)
{
	int i;

	for (i = 0; i < g_nflat; i++)
		if (!g_flat[i].done) return true;
	return false;
}

/* One step: take what the player has answered, and ask about the next album.
 * From the main loop and the transfer screen's. */
void muse_file_albums(void)
{
	char dir[LIB_PATH * 2], name[256];
	unsigned now = plat_now_ms();
	int i;

	while (musec_artist_take(dir, sizeof dir, name, sizeof name))
		for (i = 0; i < g_nflat; i++) {
			int r;

			if (g_flat[i].done || strcmp(g_flat[i].dir, dir)) continue;
			r = name[0] ? file_album(dir, name) : 0;
			if (r < 0) { g_flat[i].asked = 0; break; }   /* playing: ask later */
			g_flat[i].done = true;
			if (r > 0) g_muse_moved = true;
			break;
		}
	/* One at a time: the next once this one is answered, or after five
	 * seconds for an answer lost with the player. */
	for (i = 0; i < g_nflat; i++) {
		if (g_flat[i].done) continue;
		if (g_flat[i].asked && now - g_flat[i].asked < 5000) return;
		if (musec_artist_ask(g_flat[i].dir)) g_flat[i].asked = now ? now : 1;
		return;
	}
}

/* Muse's card, at the end of the shelf, when there is music to play.
 *
 * After Favorites and after the empty systems are hidden, so it is neither
 * compacted away for having no games nor shifted by Favorites being inserted
 * in front: it is appended, and every later insertion moves it along with
 * everything else. Its view holds the albums - see muse_fill_view - so A on
 * the card opens a shelf of their covers, the way a console's opens its
 * games.
 *
 * Hidden when the folder is empty or missing, the same rule an empty system
 * and an empty Favorites shelf follow: a card should lead somewhere. */
static void build_muse_shelf(app *a)
{
	system_cfg *s;
	int i;

	/* Everything indexed by album goes with the albums: a rescan can put a
	 * different album at every index. */
	g_muse_gen++;
	g_nflat = 0;
	ml_free(&g_muse);
	free(g_cov);
	g_cov = NULL;
	free(g_book_done);
	g_book_done = NULL;
	np_forget();
	snprintf(g_muse_root, sizeof g_muse_root, "%s", P_CARD);
	muse_tidy_singles();
	if (!ml_scan_card(g_muse_root, &g_muse) || g_muse.ntracks == 0) {
		fprintf(stderr, "scan: %-16s no music, books or videos in %s\n", "MP4", g_muse_root);
		return;
	}
	g_cov = calloc((size_t)g_muse.nalbums, sizeof *g_cov);
	g_book_done = calloc((size_t)g_muse.nalbums, sizeof *g_book_done);
	if (!g_book_done) { ml_free(&g_muse); return; }
	flat_albums_find();
	for (i = 0; i < g_muse.nalbums; i++) {
		char key[LIB_PATH + 8], val[16];

		if (!g_muse.albums[i].book) continue;
		book_key(i, key, sizeof key);
		db_get_str(db_dev(), key, val, sizeof val, "");
		g_book_done[i] = !strcmp(val, "finished");
	}
	{
		char show[16];

		/* All is where the shelf starts since 2026-11-09. A handheld from
		 * before kept "music" or "videos" - often not chosen at all but
		 * left by the shelf following what played - so it starts on All
		 * once, and a kind chosen after that is kept. */
		if (!db_has(db_dev(), "muse.show_all_once")) {
			db_set_str(db_dev(), "muse.show", "all");
			db_set_str(db_dev(), "muse.show_all_once", "1");
		}
		db_get_str(db_dev(), "muse.show", show, sizeof show, "all");
		g_muse_kind = !strcmp(show, "books") ? ML_BOOKS : !strcmp(show, "videos") ? ML_VIDEOS
		            : !strcmp(show, "music") ? ML_MUSIC : ML_ALL;
	}
	if (a->sys.count >= CFG_MAX_SYSTEMS) return;
	i = a->sys.count;
	s = &a->sys.systems[i];
	memset(s, 0, sizeof *s);
	/* MP4, the media player, which was Muse when it played sound alone
	 * (2026-11-05). The tag and the card's file keep the old name:
	 * settings and themes are keyed by them. */
	snprintf(s->name, CFG_STR, "%s", "MP4");
	snprintf(s->tag, sizeof s->tag, "%s", "MUSE");
	snprintf(s->card, CFG_STR, "%s", "MUSE.png");
	s->accent = ZYPEROS_ACCENT;
	memset(&a->view[i], 0, sizeof a->view[i]);
	muse_fill_view(&a->view[i]);
	a->sys.count++;
	fprintf(stderr, "scan: %-16s %d artists, %d albums, %d books, %d videos, %d tracks\n", "MP4",
	        g_muse.nartists, ml_count(&g_muse, false), ml_count(&g_muse, true),
	        ml_count_kind(&g_muse, ML_VIDEOS), g_muse.ntracks);
}

/* ---------- the saved library (src/savedlib.h) ---------------------------
 *
 * A start's shelves from the saved library when it has them, and the card
 * read again once the shelf is up (lib_check, from main.c): on my card,
 * 8,844 games on exFAT, the scan was the longest thing before the first
 * frame, 600-700 ms after a cold boot (2026-11-07). What each shelf showed is
 * kept here, one per system in systems.cfg, so the check can tell whether
 * the card is still that. */

#ifndef ZYPEROS_VERSION
#define ZYPEROS_VERSION "dev"
#endif

static struct {
	char     folder[LIB_NAME], exts[LIB_NAME];
	/* The folder systems.cfg names and its EmulationStation names, for the
	 * check to choose the folder again (shelf_folder_choose) */
	char     home[LIB_NAME], es[160];
	uint64_t shown;           /* lib_listing_digest of the listing the shelf showed */
	uint64_t saved;           /* and of the saved file's, when it had one */
	bool     in_file;
	bool     pm;              /* the Ports shelf, listed with PortMaster's entry */
} g_shown[CFG_MAX_SYSTEMS];
static int g_nshown;
static int g_saved_extra;     /* the file's shelves for folders no system has */
static SDL_atomic_t g_lib_gen;  /* one more at every scan: a check older is moot */
static SDL_mutex *g_save_lock;

static void saved_path(char *out, size_t n)
{
	snprintf(out, n, "%s/saved-library", P_USERDATA);
}

/* ---------- PortMaster's entry on the Ports shelf (src/pminstall.h) -------
 *
 * The Ports shelf lists Roms/Ports/PortMaster.sh whether it is on the card or
 * not, wherever the card's ZyperOS has the installer (the Pixel 2's, whose
 * launch.sh runs ports): the same entry a scan makes of the real one, so the
 * saved library, the card read again after a start, a favorite and the play
 * time are all the same before and after it is installed. Only the A on it
 * differs: the page that installs it, while it is not on the card. */

bool g_pm_offer;

static bool pm_installer_here(void)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s", P_ROOT, PM_INSTALLER);
	return access(p, R_OK) == 0;
}

static bool is_ports(const system_cfg *s)
{
	return !strcmp(s->core, "port");
}

bool pm_on_card(const char *folder)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s/%s/%s", P_ROMS, folder, PM_FOLDER, PM_PROGRAM);
	if (access(p, R_OK) != 0) return false;
	snprintf(p, sizeof p, "%s/%s/%s", P_ROMS, folder, PM_SCRIPT);
	return access(p, R_OK) == 0;
}

bool pm_offer_refresh(const app *a)
{
	g_pm_offer = false;
	if (!pm_installer_here()) return false;
	for (int i = 0; i < a->sys.count; i++)
		if (is_ports(&a->sys.systems[i])) {
			g_pm_offer = !pm_on_card(a->sys.systems[i].folder);
			break;
		}
	return g_pm_offer;
}

bool pm_is_offer(const app *a, int sys, const game_entry *g)
{
	return g_pm_offer && g && sys >= 0 && sys < a->sys.count &&
	       is_ports(&a->sys.systems[sys]) && !strcasecmp(g->file, PM_SCRIPT);
}

bool pm_is_entry(const app *a, int sys, const game_entry *g)
{
	return g && sys >= 0 && sys < a->sys.count && is_ports(&a->sys.systems[sys]) &&
	       !strcasecmp(g->file, PM_SCRIPT);
}

bool pm_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                   char *out, size_t n)
{
	if (!pm_is_entry(a, sys, g)) return false;
	snprintf(out, n, "%s/%s", P_ROOT,
	         theme_ix == DS_DARK ? PM_COVER_DARK : PM_COVER_LIGHT);
	return access(out, R_OK) == 0;
}

bool pm_icon_path(char *out, size_t n)
{
	snprintf(out, n, "%s/%s", P_ROOT, PM_ICON);
	return access(out, R_OK) == 0;
}

int pm_shelf_offers(const app *a, int sys)
{
	const sysview *v = &a->view[sys];

	if (!g_pm_offer || a->view[sys].owner || !is_ports(&a->sys.systems[sys])) return 0;
	for (int i = 0; i < v->list.count; i++)
		if (!strcasecmp(v->list.items[i].file, PM_SCRIPT)) return 1;
	return 0;
}

bool pm_only_offer(const app *a, int sys)
{
	return a->view[sys].list.count == 1 && pm_shelf_offers(a, sys) == 1;
}

/* ---------- EmulationStation's folders (config/systems.cfg, es lines) ----
 *
 * A system's shelf reads ONE folder: its own while that has a game, and
 * otherwise the one of its EmulationStation names on the card with the most
 * games, the first listed on a tie. Chosen whenever the card is read - a
 * rescan, and the check after a start that came from the saved library -
 * and kept by home folder (lib_folder_set) for every load of systems.cfg
 * after it (shelf_folders_apply). A start from the saved library takes the
 * folder the file has, so it reads nothing more off the card. */

void shelf_folders_apply(systems_cfg *s)
{
	for (int i = 0; s && i < s->count; i++)
		lib_folder_get(s->systems[i].home, s->systems[i].folder, sizeof s->systems[i].folder);
}

/* Roms/'s folders, read once for a scan that reads the card and only when a
 * system there has EmulationStation names: one listing of about forty
 * entries */
typedef struct {
	char (*name)[LIB_NAME];
	int  n;
	bool read;
} roms_dirs;

static void roms_dirs_read(roms_dirs *d)
{
	DIR *dh;
	struct dirent *e;
	int cap = 0;

	d->read = true;
	if (!(dh = opendir(P_ROMS))) return;
	while ((e = readdir(dh))) {
		if (e->d_name[0] == '.') continue;
		if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) continue;
		if (e->d_type == DT_UNKNOWN) {
			char p[LIB_PATH * 2];
			struct stat st;

			snprintf(p, sizeof p, "%s/%s", P_ROMS, e->d_name);
			if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		}
		if (d->n == cap) {
			int more = cap ? cap * 2 : 64;
			char (*bigger)[LIB_NAME] = realloc(d->name, (size_t)more * sizeof *bigger);

			if (!bigger) break;
			d->name = bigger;
			cap = more;
		}
		snprintf(d->name[d->n++], LIB_NAME, "%s", e->d_name);
	}
	closedir(dh);
}

static void roms_dirs_free(roms_dirs *d)
{
	free(d->name);
	memset(d, 0, sizeof *d);
}

/* The folder of that name on the card, as the card spells it: exFAT takes
 * any case, a host's file system may not */
static const char *roms_dirs_find(const roms_dirs *d, const char *name)
{
	for (int i = 0; i < d->n; i++)
		if (!strcasecmp(d->name[i], name)) return d->name[i];
	return NULL;
}

#define ES_PICK_MAX 12

/* Which folder a system with EmulationStation names reads, into `folder`,
 * with its listing in `out`. The losers are named in the log, with what
 * they hold. `tag` is for the log only. */
static void shelf_folder_choose(const char *tag, const char *home, const char *es,
                                const char *exts, const roms_dirs *d,
                                char *folder, size_t fn, game_list *out)
{
	system_cfg one;
	const char *there[ES_PICK_MAX];
	game_list lists[ES_PICK_MAX];
	int games[ES_PICK_MAX], n = 0, k, pick, home_games;
	char name[LIB_NAME];

	memset(&one, 0, sizeof one);
	snprintf(one.es, sizeof one.es, "%s", es);
	for (k = 0; n < ES_PICK_MAX && cfg_es_at(&one, k, name, sizeof name); k++) {
		const char *on_card = roms_dirs_find(d, name);

		if (on_card && strcasecmp(on_card, home)) there[n++] = on_card;
	}
	lib_scan_listing(P_ROMS, home, exts, out);
	snprintf(folder, fn, "%s", home);
	if (n == 0) return;
	home_games = out->count;
	memset(lists, 0, sizeof lists);
	for (k = 0; k < n; k++) {
		lib_scan_listing(P_ROMS, there[k], exts, &lists[k]);
		games[k] = lists[k].count;
	}
	pick = lib_folder_pick(home_games, games, n);
	if (pick >= 0) {
		fprintf(stderr, "scan: %-16s reads Roms/%s (%d game%s): Roms/%s has none\n", tag,
		        there[pick], games[pick], games[pick] == 1 ? "" : "s", home);
		lib_free(out);
		*out = lists[pick];
		memset(&lists[pick], 0, sizeof lists[pick]);
		snprintf(folder, fn, "%s", there[pick]);
	}
	for (k = 0; k < n; k++) {
		if (k != pick && games[k] > 0)
			fprintf(stderr, "scan: %-16s does not read Roms/%s (%d game%s): one folder a shelf, "
			        "Roms/%s's\n", tag, there[k], games[k], games[k] == 1 ? "" : "s", folder);
		lib_free(&lists[k]);
	}
}

void shelf_folders_from_card(systems_cfg *s)
{
	roms_dirs dirs = { 0 };
	game_list l = { 0 };

	for (int i = 0; s && i < s->count; i++) {
		system_cfg *sc = &s->systems[i];

		if (!sc->es[0]) continue;
		if (!dirs.read) roms_dirs_read(&dirs);
		shelf_folder_choose(sc->tag, sc->home, sc->es, sc->exts, &dirs, sc->folder,
		                    sizeof sc->folder, &l);
		lib_free(&l);
		lib_folder_set(sc->home, sc->folder);
	}
	roms_dirs_free(&dirs);
}

/* An untaken saved shelf for one of a system's EmulationStation names,
 * taken: the folder a start before this one chose (shelf_folder_choose).
 * `folder` gets the saved spelling. */
static bool saved_take_es(saved_lib *saved, const system_cfg *sc, char *folder, size_t fn,
                          game_list *out)
{
	char name[LIB_NAME];

	for (int k = 0; cfg_es_at(sc, k, name, sizeof name); k++)
		for (int i = 0; i < saved->count; i++) {
			saved_shelf *sh = &saved->shelves[i];

			if (sh->taken || strcasecmp(sh->folder, name) || strcmp(sh->exts, sc->exts)) continue;
			snprintf(folder, fn, "%s", sh->folder);
			return savedlib_take(saved, sh->folder, sh->exts, out);
		}
	return false;
}

int scan_all_from(app *a, saved_lib *saved, bool save)
{
	int from_saved = 0;
	bool pm_installer = pm_installer_here();
	roms_dirs dirs = { 0 };

	SDL_AtomicIncRef(&g_lib_gen);
	g_nshown = 0;
	for (int i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];
		system_cfg *sc = &a->sys.systems[i];
		bool kept = saved && savedlib_take(saved, sc->folder, sc->exts, &v->list);
		uint64_t digest;

		bool pm = is_ports(sc) && pm_installer;

		/* The saved library under one of its EmulationStation names: the
		 * folder the last read of the card chose */
		if (!kept && saved && sc->es[0] &&
		    saved_take_es(saved, sc, sc->folder, sizeof sc->folder, &v->list)) {
			kept = true;
			lib_folder_set(sc->home, sc->folder);
		}
		if (!kept && sc->es[0]) {
			if (!dirs.read) roms_dirs_read(&dirs);
			shelf_folder_choose(sc->tag, sc->home, sc->es, sc->exts, &dirs,
			                    sc->folder, sizeof sc->folder, &v->list);
			lib_folder_set(sc->home, sc->folder);
		} else if (!kept) {
			lib_scan_listing(P_ROMS, sc->folder, sc->exts, &v->list);
		}
		if (pm) pm_listing_add(&v->list);
		digest = lib_listing_digest(&v->list);
		if (g_nshown < CFG_MAX_SYSTEMS) {
			snprintf(g_shown[g_nshown].folder, sizeof g_shown[0].folder, "%s", sc->folder);
			snprintf(g_shown[g_nshown].exts, sizeof g_shown[0].exts, "%s", sc->exts);
			snprintf(g_shown[g_nshown].home, sizeof g_shown[0].home, "%s", sc->home);
			snprintf(g_shown[g_nshown].es, sizeof g_shown[0].es, "%s", sc->es);
			g_shown[g_nshown].shown = digest;
			g_shown[g_nshown].saved = kept ? digest : 0;
			g_shown[g_nshown].in_file = kept;
			g_shown[g_nshown].pm = pm;
			g_nshown++;
		}
		from_saved += kept;
		lib_scan_finish(sc->folder, &v->list);
	}
	roms_dirs_free(&dirs);
	g_saved_extra = saved ? savedlib_untaken(saved) : 0;
	/* Read off the card (a rescan): the file is this now */
	if (save) {
		savedlib_row rows[CFG_MAX_SYSTEMS];
		char path[LIB_PATH];
		int n = 0;

		for (int i = 0; i < a->sys.count && n < CFG_MAX_SYSTEMS; i++, n++)
			rows[n] = (savedlib_row){ a->sys.systems[i].folder, a->sys.systems[i].exts,
			                          &a->view[i].list };
		saved_path(path, sizeof path);
		if (g_save_lock) SDL_LockMutex(g_save_lock);
		if (!savedlib_write(path, ZYPEROS_VERSION, rows, n))
			fprintf(stderr, "saved library: could not write %s\n", path);
		if (g_save_lock) SDL_UnlockMutex(g_save_lock);
	}
	for (int i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];
		if (v->list.count > 0) {
			v->tex = calloc((size_t)v->list.count, sizeof *v->tex);
			v->tw = calloc((size_t)v->list.count, sizeof *v->tw);
			v->th = calloc((size_t)v->list.count, sizeof *v->th);
			v->cb = calloc((size_t)v->list.count, sizeof *v->cb);
			/* A count with no array behind it would be dereferenced on the
			 * next frame. An empty shelf is the honest answer. */
			if (!v->tex || !v->tw || !v->th || !v->cb) {
				free(v->tex); free(v->tw); free(v->th); free(v->cb);
				v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
				lib_free(&v->list);
			}
		}
		fprintf(stderr, "scan: %-16s %d game%s\n", a->sys.systems[i].folder,
		        v->list.count, v->list.count == 1 ? "" : "s");
		/* What was left out, and what the folder takes, so a game that is
		 * on the card and not on the shelf has its reason in the log. */
		if (v->list.skipped) {
			char names[LIB_SKIPS_SHOWN * (LIB_NAME + 4)] = "";
			int k, shown = v->list.skipped < LIB_SKIPS_SHOWN ? v->list.skipped
			                                                 : LIB_SKIPS_SHOWN;

			for (k = 0; k < shown; k++) {
				size_t at = strlen(names);

				snprintf(names + at, sizeof names - at, "%s\"%s\"", k ? ", " : "",
				         v->list.skipped_eg[k]);
			}
			fprintf(stderr, "scan: %-16s left out %d (it takes %s): %s%s\n",
			        a->sys.systems[i].folder, v->list.skipped,
			        a->sys.systems[i].exts[0] ? a->sys.systems[i].exts : "anything",
			        names, v->list.skipped > shown ? ", and more" : "");
		}
	}
	hide_empty_systems(a);
	build_favorites_shelf(a);
	build_muse_shelf(a);
	pm_offer_refresh(a);
	return from_saved;
}

void scan_all_start(app *a, bool use_saved)
{
	saved_lib saved;
	char path[LIB_PATH];
	bool have = false;

	if (use_saved) {
		saved_path(path, sizeof path);
		have = savedlib_load(path, ZYPEROS_VERSION, &saved);
		if (!have) fprintf(stderr, "saved library: none to use, the card read\n");
	}
	if (have) {
		int systems = a->sys.count, n = scan_all_from(a, &saved, false);

		fprintf(stderr, "saved library: %d of %d shelves from it, the rest read off the card\n",
		        n, systems);
		savedlib_free(&saved);
	} else {
		scan_all_from(a, NULL, false);
	}
}

struct lib_check {
	int         gen;
	int         n;
	saved_shelf fresh[CFG_MAX_SYSTEMS];
	uint64_t    shown[CFG_MAX_SYSTEMS], saved[CFG_MAX_SYSTEMS];
	bool        in_file[CFG_MAX_SYSTEMS];
	bool        pm[CFG_MAX_SYSTEMS];
	/* Each shelf's folder systems.cfg names and its EmulationStation names,
	 * and whether the check chose another folder than the one shown */
	char        home[CFG_MAX_SYSTEMS][LIB_NAME], es[CFG_MAX_SYSTEMS][160];
	bool        moved[CFG_MAX_SYSTEMS];
	/* What lib_check_import wrote of each folder's gamelist.xml */
	es_import_list got[CFG_MAX_SYSTEMS];
	bool        rewrite;          /* the file is not the card, or there is none */
	char        changed[96];      /* the folders the shelves are behind on, for the log */
	bool        behind;           /* and whether there are any */
	unsigned    ms;
};

lib_check *lib_check_begin(void)
{
	lib_check *c;

	if (g_nshown <= 0 || !(c = calloc(1, sizeof *c))) return NULL;
	if (!g_save_lock) g_save_lock = SDL_CreateMutex();
	c->gen = SDL_AtomicGet(&g_lib_gen);
	c->n = g_nshown;
	c->rewrite = g_saved_extra > 0;
	_Static_assert(sizeof c->fresh[0].folder == sizeof g_shown[0].folder &&
	               sizeof c->fresh[0].exts == sizeof g_shown[0].exts, "the same fields");
	for (int k = 0; k < c->n; k++) {
		memcpy(c->fresh[k].folder, g_shown[k].folder, sizeof c->fresh[k].folder);
		memcpy(c->fresh[k].exts, g_shown[k].exts, sizeof c->fresh[k].exts);
		c->shown[k] = g_shown[k].shown;
		c->saved[k] = g_shown[k].saved;
		c->in_file[k] = g_shown[k].in_file;
		c->pm[k] = g_shown[k].pm;
		memcpy(c->home[k], g_shown[k].home, sizeof c->home[k]);
		memcpy(c->es[k], g_shown[k].es, sizeof c->es[k]);
	}
	return c;
}

void lib_check_run(lib_check *c)
{
	unsigned t0 = plat_now_ms();
	char path[LIB_PATH];
	roms_dirs dirs = { 0 };

	for (int k = 0; k < c->n; k++) {
		uint64_t d;

		if (c->es[k][0]) {
			char was[LIB_NAME];

			/* The folder chosen again, from the card as it is now: an
			 * EmulationStation folder copied on since the saved library
			 * was written, or ZyperOS's own given games */
			if (!dirs.read) roms_dirs_read(&dirs);
			snprintf(was, sizeof was, "%s", c->fresh[k].folder);
			shelf_folder_choose(c->home[k], c->home[k], c->es[k], c->fresh[k].exts, &dirs,
			                    c->fresh[k].folder, sizeof c->fresh[k].folder,
			                    &c->fresh[k].list);
			c->moved[k] = strcmp(was, c->fresh[k].folder) != 0;
			if (c->moved[k]) c->rewrite = true;
		} else {
			lib_scan_listing(P_ROMS, c->fresh[k].folder, c->fresh[k].exts, &c->fresh[k].list);
		}
		if (c->pm[k]) pm_listing_add(&c->fresh[k].list);
		d = lib_listing_digest(&c->fresh[k].list);
		if (!c->in_file[k] || d != c->saved[k]) c->rewrite = true;
		if (d != c->shown[k] || c->moved[k]) {
			size_t at = strlen(c->changed);

			if (at + strlen(c->fresh[k].folder) + 3 < sizeof c->changed)
				snprintf(c->changed + at, sizeof c->changed - at, "%s%s", at ? ", " : "",
				         c->fresh[k].folder);
			c->behind = true;
		}
	}
	if (c->rewrite) {
		savedlib_row rows[CFG_MAX_SYSTEMS];

		for (int k = 0; k < c->n; k++)
			rows[k] = (savedlib_row){ c->fresh[k].folder, c->fresh[k].exts, &c->fresh[k].list };
		saved_path(path, sizeof path);
		SDL_LockMutex(g_save_lock);
		/* Not over a rescan's: the card may have changed since this read it */
		if (SDL_AtomicGet(&g_lib_gen) == c->gen &&
		    !savedlib_write(path, ZYPEROS_VERSION, rows, c->n))
			fprintf(stderr, "saved library: could not write %s\n", path);
		SDL_UnlockMutex(g_save_lock);
	}
	roms_dirs_free(&dirs);
	c->ms = plat_now_ms() - t0;
}


/* Read the card again and rebuild every shelf, for when something outside the
 * launcher has changed what is on it - which today means Wi-Fi File Transfer.
 *
 * Drop and rebuild rather than patch, on the same reasoning
 * drop_favorites_shelf gives: the cases are "a game appeared", "a game went",
 * "a system that was empty now has games", "a system emptied", and "the shelf
 * you are standing on is one of those". Patching five cases correctly is
 * harder than doing the whole thing again, and the whole thing is three
 * directory reads.
 *
 * The config is re-read because hide_empty_systems does not hide, it COMPACTS:
 * a system with no games is removed from systems[] and its entry overwritten.
 * Uploading the first ROM for a system is precisely what this feature is for,
 * so without the reload that system could never come back. */
bool g_order_release;

void system_order_load(void)
{
	char v[16];

	db_get_str(db_dev(), "systems.order", v, sizeof v, "az");
	g_order_release = !strcmp(v, "release");
}

void system_order_apply(systems_cfg *s)
{
	if (!g_order_release) cfg_sort_systems(s);
}

/* Every shelf built again: rescan_all's, from the card, and lib_check's,
 * from the listings it read (`pre`). `keep_place` keeps the cursor on the
 * game it was on, which a check asks for: it comes by itself, a moment
 * after the shelf is up, and nobody asked for the list to start over. */
static void rebuild(app *a, saved_lib *pre, bool keep_place)
{
	char tag[sizeof a->sys.systems[0].tag];
	char path[CFG_STR * 2], file[LIB_PATH] = "";
	int i;

	/* Indices move when a system appears or empties, so the tag is the only
	 * handle on "where the player was" that survives the rebuild. */
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	if (keep_place && a->sys.count > 0) {
		const sysview *v = &a->view[a->sys_cursor];

		if (v->cursor >= 0 && v->cursor < v->list.count)
			snprintf(file, sizeof file, "%s", v->list.items[v->cursor].file);
	}

	/* First, and on its own: its list and owner map are this file's
	 * allocations rather than lib_scan's, and its textures are its own. */
	drop_favorites_shelf(a);

	free_all_textures(a);
	for (i = 0; i < a->sys.count; i++) {
		free(a->view[i].tex); free(a->view[i].tw); free(a->view[i].th);
		free(a->view[i].cb);
		free(a->view[i].album);                         /* Muse's, else NULL */
		lib_free(&a->view[i].list);
		memset(&a->view[i], 0, sizeof a->view[i]);
	}

	/* menu_shelf_width measures every row of every system's menu once and
	 * caches it. The set of systems is exactly what it measured across, so a
	 * rescan invalidates it - and the case that shows is a card that booted
	 * empty: with no systems there are no game-menu rows to measure, the
	 * panel is sized for the ZyperOS menu alone, and the first system to
	 * arrive gets "Integer tall" cut off inside a frame measured before it
	 * existed. */
	a->menu_w = 0;

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (cfg_load_systems(path, &a->sys)) shelf_folders_apply(&a->sys);
	else {
		/* The card was readable a moment ago, so this is a card that has gone
		 * away underneath us. An empty shelf is the honest picture. */
		fprintf(stderr, "rescan: no usable %s\n", path);
		a->sys.count = 0;
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
		return;
	}
	system_order_apply(&a->sys);

	/* A rescan read the card, so the saved library is that now; a check's
	 * listings are the file already (lib_check_run) */
	scan_all_from(a, pre, !pre);
	display_load(a);     /* indexes by tag, so it is safe to run again */
	core_load(a);
	sort_load(a);
	sort_all(a);

	a->sys_cursor = 0;
	for (i = 0; i < a->sys.count; i++)
		if (strcmp(a->sys.systems[i].tag, tag) == 0) { a->sys_cursor = i; break; }

	/* Whether the caller can stay where it was.
	 *
	 * Rescanning from inside a system's game list should leave you in that
	 * list looking at the new games, so the screen is not forced here. But the
	 * shelf being stood on can have emptied and gone - deleting the last ROM
	 * for a system over the network is the ordinary way - and then there is no
	 * list to stay in. Same rule refresh_favorites_shelf follows when the last
	 * favorite goes. */
	if (i >= a->sys.count || !a->sys.count) a->screen = SCREEN_SYSTEMS;

	if (a->sys.count) {
		sysview *v = &a->view[a->sys_cursor];
		int at = 0;

		for (int k = 0; file[0] && k < v->list.count; k++)
			if (!strcmp(v->list.items[k].file, file)) { at = k; break; }
		a->tint = a->sys.systems[a->sys_cursor].accent;
		/* The rebuilt view starts at zero, or on the kept game, and its
		 * coverflow has to agree, or the shelf animates from wherever the
		 * old one happened to be. */
		v->cursor = at;
		cf_reset(&v->cf, at);
		if (keep_place) games_reset(&v->row, at, v->list.count, plat_now_ms());
	}
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

void rescan_all(app *a)
{
	/* The card read again: EmulationStation's indexes with it */
	es_forget();
	rebuild(a, NULL, false);
}

/* The saved library as the shelves are now, after a change the launcher
 * made itself and so knows without reading the card: the folder's digest
 * for the check, and the file for the next start (src/savedlib.h) */
static void saved_follow(app *a, const char *folder)
{
	savedlib_row rows[CFG_MAX_SYSTEMS];
	char path[LIB_PATH];
	int i, n = 0;

	for (i = 0; i < g_nshown; i++) {
		int k;

		if (strcmp(g_shown[i].folder, folder)) continue;
		g_shown[i].shown = 0;
		for (k = 0; k < a->sys.count; k++)
			if (!a->view[k].owner && !strcmp(a->sys.systems[k].folder, folder))
				g_shown[i].shown = lib_listing_digest(&a->view[k].list);
		g_shown[i].saved = g_shown[i].shown;
	}
	for (i = 0; i < a->sys.count && n < CFG_MAX_SYSTEMS; i++) {
		const system_cfg *sc = &a->sys.systems[i];

		if (a->view[i].owner || !strcmp(sc->tag, "MUSE") || !sc->folder[0]) continue;
		rows[n++] = (savedlib_row){ sc->folder, sc->exts, &a->view[i].list };
	}
	saved_path(path, sizeof path);
	if (g_save_lock) SDL_LockMutex(g_save_lock);
	if (!savedlib_write(path, ZYPEROS_VERSION, rows, n))
		fprintf(stderr, "saved library: could not write %s\n", path);
	if (g_save_lock) SDL_UnlockMutex(g_save_lock);
}

/* One shelf's system taken off the home: its arrays freed, the rest moved
 * down, as hide_empty_systems compacts them */
static void drop_system(app *a, int si)
{
	sysview *v = &a->view[si];
	int i;

	free(v->tex); free(v->tw); free(v->th); free(v->cb);
	free(v->album);
	lib_free(&v->list);
	for (i = si; i + 1 < a->sys.count; i++) {
		a->sys.systems[i] = a->sys.systems[i + 1];
		a->view[i] = a->view[i + 1];
	}
	a->sys.count--;
	memset(&a->sys.systems[a->sys.count], 0, sizeof a->sys.systems[0]);
	memset(&a->view[a->sys.count], 0, sizeof a->view[0]);
}

/* A game deleted from the card (src/gamedel.h, DESIGN.md 11.31): off its own
 * shelf and off Favorites, in place, with no read of the card. The shelf
 * stood on keeps its place and its cursor goes to the next game, or the one
 * before at the end (gd_cursor_after); a shelf left empty goes, as an empty
 * system always has (hide_empty_systems), and the home is shown on the
 * system that took its place - or, with no games left on the card, the empty
 * home. Favorites keeps the game its cursor was on.
 *
 * In place rather than rescan_all, which is a read of every console's folder
 * for one file the launcher itself just deleted: on my card, 8,844 games on
 * exFAT, that is most of a second (lib_scan_listing). */
void shelf_drop_game(app *a, const char *tag, const char *file)
{
	char stand[sizeof a->sys.systems[0].tag], fav_tag[sizeof a->sys.systems[0].tag] = "";
	char fav_file[LIB_PATH] = "", folder[CFG_STR] = "";
	int was_sys, si, k, i, order = 0, fav_at_i = 0;

	if (a->sys.count <= 0) return;
	snprintf(stand, sizeof stand, "%s", a->sys.systems[a->sys_cursor].tag);
	was_sys = a->sys_cursor;
	/* Every index from here on can name another game */
	free_all_textures(a);
	SDL_AtomicIncRef(&g_lib_gen);       /* a check that read the card before is moot */

	for (i = 0; i < a->sys.count; i++)
		if (a->view[i].owner) {
			const sysview *f = &a->view[i];

			order = f->sort;
			fav_at_i = f->cursor;
			if (f->cursor >= 0 && f->cursor < f->list.count) {
				snprintf(fav_tag, sizeof fav_tag, "%s",
				         a->sys.systems[f->owner[f->cursor]].tag);
				snprintf(fav_file, sizeof fav_file, "%s", f->list.items[f->cursor].file);
			}
			break;
		}
	drop_favorites_shelf(a);

	for (si = 0; si < a->sys.count; si++)
		if (!strcmp(a->sys.systems[si].tag, tag)) break;
	if (si < a->sys.count && (k = lib_find_entry(&a->view[si].list, NULL, 0, file)) >= 0) {
		sysview *v = &a->view[si];
		size_t rest = (size_t)(v->list.count - k - 1);

		snprintf(folder, sizeof folder, "%s", a->sys.systems[si].folder);
		memmove(&v->list.items[k], &v->list.items[k + 1], rest * sizeof *v->list.items);
		if (v->tex) memmove(&v->tex[k], &v->tex[k + 1], rest * sizeof *v->tex);
		if (v->tw)  memmove(&v->tw[k],  &v->tw[k + 1],  rest * sizeof *v->tw);
		if (v->th)  memmove(&v->th[k],  &v->th[k + 1],  rest * sizeof *v->th);
		if (v->cb)  memmove(&v->cb[k],  &v->cb[k + 1],  rest * sizeof *v->cb);
		v->list.count--;
		/* Its cursor: on the game, it is now on the next one; past the
		 * end, on the one before */
		if (v->cursor > k) v->cursor--;
		if (v->cursor >= v->list.count) v->cursor = v->list.count > 0 ? v->list.count - 1 : 0;
		fprintf(stderr, "delete: %s %s off its shelf, %d game%s left\n", tag, file,
		        v->list.count, v->list.count == 1 ? "" : "s");
		saved_follow(a, folder);
		if (v->list.count == 0) {
			fprintf(stderr, "delete: %s has no games left, off the home\n", tag);
			drop_system(a, si);
		}
	}
	build_favorites_shelf(a);
	for (i = 0; i < a->sys.count; i++)
		if (a->view[i].owner) {
			sysview *f = &a->view[i];
			int c = -1;

			f->sort = order;
			sort_shelf(a, i);
			/* The game Favorites was on, or where it was when that was the
			 * game deleted */
			for (k = 0; fav_file[0] && k < f->list.count; k++)
				if (!strcmp(f->list.items[k].file, fav_file) &&
				    !strcmp(a->sys.systems[f->owner[k]].tag, fav_tag))
					c = k;
			if (c < 0) c = fav_at_i < f->list.count ? fav_at_i : f->list.count - 1;
			f->cursor = c > 0 ? c : 0;
			cf_reset(&f->cf, f->cursor);
			break;
		}

	/* Back where the player stood. On the game's own shelf its cursor was
	 * the game, and moved above to the next one or the one before at the
	 * end (gd_cursor_after's rule); on Favorites the same, above */
	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, stand)) break;
	if (i < a->sys.count) {
		sysview *v = &a->view[i];

		a->sys_cursor = i;
		cf_reset(&v->cf, v->cursor);
		games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	} else {
		/* The shelf stood on emptied and went: the home, on the system now
		 * in its place */
		a->sys_cursor = was_sys < a->sys.count ? was_sys : a->sys.count - 1;
		if (a->sys_cursor < 0) a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	if (a->sys.count > 0) a->tint = a->sys.systems[a->sys_cursor].accent;
	home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
}

/* Rename (src/gamern.h, DESIGN.md 11.38): the game's entry under its new
 * file and name, in place, as shelf_drop_game takes one off - no read of the
 * card. Its title is the library's for the new file, which a rename empties
 * (db_game_move), so the new name tidied (lib_title). Its shelf put in
 * order again, its cursor still on it where it was on it; Favorites made
 * again from the shelves; the saved library told. */
bool shelf_rename_game(app *a, const char *tag, const char *file, const char *new_file,
                       const char *new_name)
{
	char folder[CFG_STR], was[LIB_PATH] = "";
	int si, k, at;
	sysview *v;
	game_entry *e;

	for (si = 0; si < a->sys.count; si++)
		if (!a->view[si].owner && !strcmp(a->sys.systems[si].tag, tag)) break;
	if (si >= a->sys.count || (k = lib_find_entry(&a->view[si].list, NULL, 0, file)) < 0) return false;
	v = &a->view[si];
	e = &v->list.items[k];
	snprintf(folder, sizeof folder, "%s", a->sys.systems[si].folder);
	if (v->cursor >= 0 && v->cursor < v->list.count)
		snprintf(was, sizeof was, "%s", v->cursor == k ? new_file : v->list.items[v->cursor].file);
	snprintf(e->file, sizeof e->file, "%s", new_file);
	snprintf(e->name, sizeof e->name, "%s", new_name);
	lib_display_title(db_lib(), folder, e->file, e->name, e->title, sizeof e->title);
	/* Every index from here on can name another game */
	free_all_textures(a);
	SDL_AtomicIncRef(&g_lib_gen);       /* a check that read the card before is moot */
	sort_shelf(a, si);
	if (was[0] && (at = lib_find_entry(&v->list, NULL, 0, was)) >= 0) v->cursor = at;
	cf_reset(&v->cf, v->cursor);
	fprintf(stderr, "rename: %s %s is %s on its shelf\n", tag, file, new_file);
	saved_follow(a, folder);
	refresh_favorites_shelf(a);
	return true;
}

bool lib_check_apply(app *a, lib_check *c)
{
	saved_lib pre;

	if (!c->behind) {
		fprintf(stderr, "saved library: the card is as the shelves show it (%d shelves read in %u ms)\n",
		        c->n, c->ms);
		return false;
	}
	if (c->gen != SDL_AtomicGet(&g_lib_gen)) return false;      /* read again since */
	fprintf(stderr, "saved library: the card changed (%s), the shelves read again\n", c->changed);
	/* A folder the check chose again, for the systems.cfg rebuild reads */
	for (int k = 0; k < c->n; k++)
		if (c->moved[k]) lib_folder_set(c->home[k], c->fresh[k].folder);
	pre.shelves = c->fresh;
	pre.count = c->n;
	rebuild(a, &pre, true);
	if (a->sys.count > 0) {
		const sysview *v = &a->view[a->sys_cursor];

		fprintf(stderr, "saved library: still on %s, %s\n", a->sys.systems[a->sys_cursor].tag,
		        v->list.count > 0 ? v->list.items[v->cursor].file : "no game");
	}
	return true;
}

void lib_check_free(lib_check *c)
{
	if (!c) return;
	for (int k = 0; k < c->n; k++) {
		lib_free(&c->fresh[k].list);
		es_import_free(&c->got[k]);
	}
	free(c);
}

void lib_check_import(lib_check *c)
{
	unsigned t0 = plat_now_ms();
	int rows = 0;

	for (int k = 0; c && k < c->n; k++)
		if (c->fresh[k].list.count > 0)
			rows += es_import(P_ROMS, c->fresh[k].folder, &c->fresh[k].list, &c->got[k]);
	if (rows)
		fprintf(stderr, "gamelist.xml: details of %d game%s into the library (%u ms)\n", rows,
		        rows == 1 ? "" : "s", plat_now_ms() - t0);
}

void lib_check_imported(lib_check *c,
                        void (*fn)(const char *folder, const es_import_list *l, void *ctx), void *ctx)
{
	for (int k = 0; c && k < c->n; k++)
		if (c->got[k].n) fn(c->fresh[k].folder, &c->got[k], ctx);
}
