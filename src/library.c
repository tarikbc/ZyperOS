/* SPDX-License-Identifier: GPL-3.0-only */
#include "library.h"
#include "titletext.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Disc-image extensions, in launch preference. A folder holding one of these
 * is a disc-based game -- a PC Engine CD or PlayStation title, typically --
 * and becomes one entry launching the image inside it. .m3u wins so a
 * multi-disc set is a single entry with disc switching. .mdf and .cbn are
 * PlayStation images pcsx_rearmed loads directly, so they are discs too, and
 * need the shelf's disc BIOS like any other. .mds is the Saturn's, last so a
 * PlayStation folder that holds an .mds beside its .mdf still opens the .mdf.
 * .cso is the PSP's compressed .iso, so a PSP game in a folder of its own is
 * one entry whichever of the two it is. */
static const char *DISC_EXTS[] = {
	"m3u", "chd", "cue", "ccd", "toc", "iso", "cso", "img", "pbp", "mdf", "cbn", "bin", "mds", NULL
};

static const char *ext_of(const char *name)
{
	const char *dot = strrchr(name, '.');
	return (dot && dot != name) ? dot + 1 : NULL;
}

/* See library.h. One rule for systems.cfg's list wherever it is read: Box Art
 * kept one of its own, commas only, and a shelf whose list was "nes sfc"
 * showed its games and got no covers. */
bool lib_ext_allowed(const char *ext, const char *list)
{
	const char *p = list;
	size_t n;

	if (!list || !*list) return true;
	if (!ext) return false;
	n = strlen(ext);
	while (*p) {
		const char *start = p;
		while (*p && *p != ',' && *p != ' ') p++;
		if ((size_t)(p - start) == n && strncasecmp(start, ext, n) == 0) return true;
		while (*p == ',' || *p == ' ') p++;
	}
	return false;
}

/* Is this filename a disc image? The shelf needs to know because a disc may
 * need firmware a cartridge on the same shelf does not - see disc_bios in
 * config.h. Same table the disc-folder ranking uses, so the two cannot drift. */
bool lib_is_disc(const char *name)
{
	const char *ext = ext_of(name);
	int i;

	if (!ext) return false;
	for (i = 0; DISC_EXTS[i]; i++)
		if (strcasecmp(ext, DISC_EXTS[i]) == 0) return true;
	return false;
}

/* A folder's disc images, in launch preference: DISC_EXTS's order, with the
 * Dreamcast's .gdi and .cdi after .cue. A .gdi set is a sheet and tracks, and
 * launching its track01.bin, as DISC_EXTS alone picked, was never the game.
 * Only for which file of a set folder starts it: whether a file is a disc
 * (lib_is_disc, the disc BIOS) is DISC_EXTS's alone, as before. */
static const char *SET_EXTS[] = {
	"m3u", "chd", "cue", "gdi", "cdi", "ccd", "toc", "iso", "img", "pbp", "mdf", "cbn", "bin", "mds", NULL
};
/* The sheets: files naming the others of their set. Beside one, a .bin or an
 * .img is its track, not a disc of its own. */
static const char *SHEET_EXTS[] = { "m3u", "cue", "gdi", "ccd", "toc", "mds", NULL };

static int ext_index(const char *name, const char *const *list)
{
	const char *ext = ext_of(name);
	if (!ext) return -1;
	for (int i = 0; list[i]; i++)
		if (strcasecmp(ext, list[i]) == 0) return i;
	return -1;
}

/* What disc image `name` says it is a disc of, to tell one game's discs from
 * several games' (set_read): the name up to its first bracket after a space,
 * as lib_title cuts it, in lower case and letters and digits alone, less a
 * disc or track number at its end. "Final Fantasy VII (USA) (Disc 2).chd",
 * "Final Fantasy VII (USA) (Disc 1).cue" and "ff7 cd1.bin" give
 * "finalfantasyvii", "finalfantasyvii" and "ff7"; "Disc 1.chd" gives "". A
 * game's own number stays: "megaman2" is not "megaman3". */
static void set_key(const char *name, char *out, size_t n)
{
	static const char *const MARKS[] = { "disc", "disk", "cd", "track", "side", NULL };
	const char *dot = strrchr(name, '.'), *end = dot && dot != name ? dot : name + strlen(name);
	size_t o = 0, digits;

	for (const char *p = name; p < end && o + 1 < n; p++) {
		unsigned char c = (unsigned char)*p;

		if (p != name && (c == '(' || c == '[')) break;
		if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) out[o++] = (char)c;
	}
	out[o] = '\0';
	for (digits = o; digits > 0 && out[digits - 1] >= '0' && out[digits - 1] <= '9'; digits--)
		;
	if (digits == o) return;
	for (int i = 0; MARKS[i]; i++) {
		size_t ml = strlen(MARKS[i]);

		if (digits >= ml && !strncmp(out + digits - ml, MARKS[i], ml)) {
			out[digits - ml] = '\0';
			return;
		}
	}
}

/* A folder's names and kinds, read once (dir_read): the scan decides what a
 * folder is from them and, when it is a folder of games, goes on with them,
 * so no folder is read twice. On the Pixel 2's exFAT card a directory entry
 * costs about 40 us (2026-11-07), and a second read of a folder of folders
 * was its whole cost again. */
typedef struct {
	char  *pool;       /* the names, each ended */
	size_t used, cap;
	int   *at;         /* each entry's name in the pool */
	unsigned char *type;   /* DT_REG, DT_DIR, or 0 for anything else */
	int    n, ncap;
} dir_list;

static void dir_free(dir_list *l)
{
	free(l->pool); free(l->at); free(l->type);
	memset(l, 0, sizeof *l);
}

static const char *dir_name(const dir_list *l, int i)
{
	return l->pool + l->at[i];
}

/* What an entry is: DT_REG, DT_DIR or anything else (a link is not followed,
 * as before). readdir says so on ext4 and on the Pixel 2's exFAT card; a
 * filesystem that answers DT_UNKNOWN costs one stat per entry instead. */
static int entry_type(const char *full, const struct dirent *e)
{
	if (e->d_type != DT_UNKNOWN) return e->d_type;
	{
		struct stat st;

		if (stat(full, &st) != 0) return -1;
		return S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG : -1;
	}
}

/* Every entry of `path` but the dot ones, which nobody put there as a game:
 * false when it cannot be opened, or memory runs out partway */
static bool dir_read(const char *path, dir_list *l)
{
	DIR *d = opendir(path);
	struct dirent *e;
	bool ok = true;

	memset(l, 0, sizeof *l);
	if (!d) return false;
	while ((e = readdir(d))) {
		char full[LIB_PATH * 3];
		size_t len = strlen(e->d_name) + 1;
		int type;

		if (e->d_name[0] == '.') continue;
		snprintf(full, sizeof full, "%s/%s", path, e->d_name);
		type = entry_type(full, e);
		if (l->n == l->ncap) {
			int more = l->ncap ? l->ncap * 2 : 64;
			int *at = realloc(l->at, (size_t)more * sizeof *at);
			unsigned char *ty;

			if (!at) { ok = false; break; }
			l->at = at;
			if (!(ty = realloc(l->type, (size_t)more))) { ok = false; break; }
			l->type = ty;
			l->ncap = more;
		}
		if (l->used + len > l->cap) {
			size_t more = l->cap ? l->cap * 2 : 4096;
			char *p;

			while (more < l->used + len) more *= 2;
			if (!(p = realloc(l->pool, more))) { ok = false; break; }
			l->pool = p;
			l->cap = more;
		}
		memcpy(l->pool + l->used, e->d_name, len);
		l->at[l->n] = (int)l->used;
		l->type[l->n] = type == DT_REG || type == DT_DIR ? (unsigned char)type : 0;
		l->used += len;
		l->n++;
	}
	closedir(d);
	return ok;
}

/* A folder's disc images, as set_read finds them */
typedef struct {
	char pick[LIB_NAME];   /* the one to launch: the best by SET_EXTS, then by name */
	int  games;            /* how many games they are discs of: 0, 1, or 2 for more */
	bool m3u;              /* a playlist, which makes them one game whatever */
} set_info;

/* Add `key` to up to two distinct ones; past two, *n is 3 and nothing more
 * is kept or read (keys has two places) */
static void key_add(char keys[2][LIB_NAME], int *n, const char *key)
{
	if (*n > 2) return;
	for (int i = 0; i < *n; i++)
		if (!strcmp(keys[i], key)) return;
	if (*n < 2) snprintf(keys[(*n)++], LIB_NAME, "%s", key);
	else *n = 3;
}

/* What a folder's disc images are: the one to launch it by, ties broken by
 * name so a folder of discs lands on Disc 1, and whether they are one game's
 * discs or several games'. A .bin or .img beside a sheet is the sheet's track
 * and says nothing; without one it is a disc. */
static void set_read(const dir_list *l, set_info *s)
{
	char strong[2][LIB_NAME], weak[2][LIB_NAME], key[LIB_NAME];
	int ns = 0, nw = 0, best = 9999;
	bool sheet = false;

	memset(s, 0, sizeof *s);
	for (int i = 0; i < l->n; i++) {
		const char *name = dir_name(l, i);
		int r = ext_index(name, SET_EXTS);

		if (r < 0 || l->type[i] != DT_REG) continue;
		if (r < best || (r == best && strcasecmp(name, s->pick) < 0)) {
			best = r;
			snprintf(s->pick, sizeof s->pick, "%s", name);
		}
		if (ext_index(name, SHEET_EXTS) >= 0) sheet = true;
		if (!strcasecmp(SET_EXTS[r], "m3u")) s->m3u = true;
		set_key(name, key, sizeof key);
		if (!strcasecmp(SET_EXTS[r], "bin") || !strcasecmp(SET_EXTS[r], "img"))
			key_add(weak, &nw, key);
		else
			key_add(strong, &ns, key);
	}
	if (!sheet)
		for (int i = 0; i < nw && i < 2; i++) key_add(strong, &ns, weak[i]);
	if (!sheet && nw > 2) ns = 3;
	s->games = ns > 2 ? 2 : ns;
}

static int game_cmp(const void *pa, const void *pb)
{
	return lib_name_cmp(pa, pb);
}

/* The display title: `name` up to the first bracketed group that follows a
 * space. ROM sets carry their cataloging in the filename - "(USA)", "(Rev 1)",
 * "(En,Fr,De)", "[!]", "[T+Eng]" - and a shelf is not a catalog, so the card
 * shows the game and the file keeps the provenance.
 *
 * The space before the bracket is what makes this safe to do blindly: a name
 * that OPENS with a bracket, which is how BIOS and disc-set folders are often
 * marked, has nothing before it to cut and survives whole. Cutting to nothing
 * falls back to the full name for the same reason - an empty card is worse
 * than a noisy one.
 *
 * This is only the filename fallback. Never apply this cleanup to a scraped
 * title: parentheses may be part of its official name. Identity and box-art
 * lookup continue to use the unmodified filename. */
/* No-Intro moves a leading article behind the name, so lists sort the game
 * under its first real word: "Legend of Zelda, The - Link's Awakening". A
 * shelf reads the name out, so the article goes back in front ("The Legend of
 * Zelda - Link's Awakening"); the shelf still sorts by the file's name, so it
 * stays under L. The, A and An, at the end of the name or before its " - "
 * subtitle. 2026-11-06: games showed as "Last Ninja, The". */
static void article_front(char *s, size_t n)
{
	static const char *const art[] = { "The", "An", "A" };
	const char *sub = strstr(s, " - ");
	size_t base = sub ? (size_t)(sub - s) : strlen(s), i;
	char out[LIB_NAME];

	for (i = 0; i < sizeof art / sizeof art[0]; i++) {
		size_t al = strlen(art[i]);

		if (base <= al + 2 || strncmp(s + base - al - 2, ", ", 2) ||
		    strncmp(s + base - al, art[i], al))
			continue;
		snprintf(out, sizeof out, "%s %.*s%s", art[i], (int)(base - al - 2), s, s + base);
		snprintf(s, n, "%s", out);
		return;
	}
}

void lib_title(const char *name, char *out, size_t n)
{
	const char *cut = NULL, *p;
	char bare[LIB_NAME];

	/* Searching from the second character, never the first: a name that OPENS
	 * with a bracket is how BIOS images and disc sets are usually marked, and
	 * it has nothing in front of the bracket to keep, so it survives whole
	 * instead of becoming nothing. */
	for (p = name; *p; p++) {
		if (p != name && (*p == '(' || *p == '[')) { cut = p; break; }
	}
	if (cut) {
		/* Take any run of spaces before the bracket with it. */
		while (cut > name && (cut[-1] == ' ' || cut[-1] == '\t')) cut--;
		if (cut == name) cut = NULL;
	}
	snprintf(bare, sizeof bare, "%.*s", cut ? (int)(cut - name) : (int)strlen(name), name);
	article_front(bare, sizeof bare);
	title_text(bare, out, n);
}

void lib_display_title(db *d, const char *folder, const char *file,
                       const char *name, char *out, size_t n)
{
	if (!db_game_title(d, folder, file, out, n)) lib_title(name, out, n);
}

bool lib_refresh_title(db *d, const char *folder, game_entry *entry)
{
	char title[LIB_NAME] = { 0 };

	lib_display_title(d, folder, entry->file, entry->name, title, sizeof title);
	if (!strcmp(entry->title, title)) return false;
	memcpy(entry->title, title, sizeof entry->title);
	return true;
}

int lib_find_entry(const game_list *list, const int *owners, int owner, const char *file)
{
	int i;

	for (i = 0; i < list->count; i++)
		if ((!owners || owners[i] == owner) && !strcmp(list->items[i].file, file))
			return i;
	return -1;
}

long lib_added(const char *roms_root, const char *folder, const char *file,
               unsigned dir)
{
	char path[LIB_PATH * 3];
	const char *part = file + (dir && dir < strlen(file) ? dir + 1 : 0);
	const char *slash = strchr(part, '/');
	struct stat st;

	snprintf(path, sizeof path, "%s/%s/%.*s", roms_root, folder,
	         (int)(slash ? (size_t)(slash - file) : strlen(file)), file);
	return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

void lib_free(game_list *l)
{
	free(l->items);
	l->items = NULL;
	l->count = 0;
	l->scanned = false;
}

/* lib_refresh_title's answer for a fresh entry, from the folder's titles
 * read at once: the stored title as db_game_title gives it, else the cleaned
 * filename. */
static void scan_title(const db_titles *titles, game_entry *e)
{
	const char *stored = db_titles_find(titles, e->file);
	char title[LIB_NAME] = { 0 };

	if (stored) title_text(stored, title, sizeof title);
	if (!title[0]) lib_title(e->name, title, sizeof title);
	memcpy(e->title, title, sizeof e->title);
}

/* Hook extensions: a file that names its folder's game for the core rather
 * than being the game. A ScummVM game is a folder of the original's files,
 * and ScummVM's libretro core starts it from a .scummvm file inside that
 * folder, which holds the game's ScummVM ID ("sky", "queen") - or, handed any
 * other file of the folder, detects the game from the folder itself
 * (backends/platform/libretro/README.md in ScummVM's tree). */
static const char *HOOK_EXTS[] = { "scummvm", NULL };

/* A shelf whose games are hooked folders: its extension list names a hook. */
bool lib_shelf_hooked(const char *exts)
{
	int i;

	if (!exts || !*exts) return false;
	for (i = 0; HOOK_EXTS[i]; i++)
		if (lib_ext_allowed(HOOK_EXTS[i], exts)) return true;
	return false;
}

/* A shelf of scripts: Ports', whose folders are what its scripts run from
 * (PortMaster's layout: Roms/PORTS/Game.sh beside Roms/PORTS/game/). A port's
 * folder holds scripts of its own, which would read as ports. */
bool lib_shelf_scripts(const char *exts)
{
	return exts && *exts && lib_ext_allowed("sh", exts);
}

/* The file to launch in a game folder on a hooked shelf, from its names: its
 * hook, else its first file by name, from which the core detects the game.
 * None when it holds no file at all: a folder of game folders, then. */
static bool hook_pick(const dir_list *l, char *out, size_t outsz)
{
	char hook[LIB_NAME] = "", first[LIB_NAME] = "";

	for (int i = 0; i < l->n; i++) {
		const char *name = dir_name(l, i);
		char *pick;

		if (l->type[i] != DT_REG) continue;
		pick = ext_index(name, HOOK_EXTS) >= 0 ? hook : first;
		if (!pick[0] || strcasecmp(name, pick) < 0)
			snprintf(pick, LIB_NAME, "%s", name);
	}
	if (!hook[0] && !first[0]) return false;
	snprintf(out, outsz, "%s", hook[0] ? hook : first);
	return true;
}

/* What EmulationStation's scrapers and its systems put in a console's folder
 * (Skraper's media/ layout, Batocera's images/ and videos/, ArkOS's and
 * ROGUE OS's downloaded_images/, RetroPie's and Recalbox's media/ folders,
 * ES-DE's downloaded_media) */
bool lib_es_media_dir(const char *name)
{
	static const char *const DIRS[] = {
		"images", "videos", "media", "manuals", "downloaded_images", "downloaded_videos",
		"downloaded_media", "boxart", "box2dfront", "box3d", "covers", "marquees", "wheel", "snap", "snaps",
		"screenshots", "titles", "titleshots", "fanart", "thumbnails", "mixart", NULL
	};
	int i;

	for (i = 0; name && DIRS[i]; i++)
		if (!strcasecmp(name, DIRS[i])) return true;
	return false;
}

/* The folders in use that systems.cfg does not name, by home folder */
#define LIB_FOLDERS 64
static struct { char home[LIB_NAME], folder[LIB_NAME]; } g_folders[LIB_FOLDERS];
static int g_nfolders;
static pthread_mutex_t g_folders_lock = PTHREAD_MUTEX_INITIALIZER;

void lib_folder_set(const char *home, const char *folder)
{
	int i;

	if (!home || !home[0]) return;
	pthread_mutex_lock(&g_folders_lock);
	for (i = 0; i < g_nfolders; i++)
		if (!strcmp(g_folders[i].home, home)) break;
	if (!folder || !folder[0] || !strcmp(folder, home)) {
		if (i < g_nfolders) g_folders[i] = g_folders[--g_nfolders];
	} else if (i < g_nfolders || g_nfolders < LIB_FOLDERS) {
		if (i == g_nfolders) g_nfolders++;
		snprintf(g_folders[i].home, sizeof g_folders[i].home, "%s", home);
		snprintf(g_folders[i].folder, sizeof g_folders[i].folder, "%s", folder);
	}
	pthread_mutex_unlock(&g_folders_lock);
}

const char *lib_folder_get(const char *home, char *out, size_t n)
{
	int i;

	snprintf(out, n, "%s", home ? home : "");
	pthread_mutex_lock(&g_folders_lock);
	for (i = 0; home && i < g_nfolders; i++)
		if (!strcmp(g_folders[i].home, home)) { snprintf(out, n, "%s", g_folders[i].folder); break; }
	pthread_mutex_unlock(&g_folders_lock);
	return out;
}

const char *lib_folder_home(const char *folder, char *out, size_t n)
{
	int i;

	snprintf(out, n, "%s", folder ? folder : "");
	pthread_mutex_lock(&g_folders_lock);
	for (i = 0; folder && i < g_nfolders; i++)
		if (!strcmp(g_folders[i].folder, folder)) { snprintf(out, n, "%s", g_folders[i].home); break; }
	pthread_mutex_unlock(&g_folders_lock);
	return out;
}

void lib_folder_reset(void)
{
	pthread_mutex_lock(&g_folders_lock);
	g_nfolders = 0;
	pthread_mutex_unlock(&g_folders_lock);
}

int lib_folder_pick(int home_games, const int *games, int n)
{
	int best = -1, i;

	if (home_games > 0) return -1;
	for (i = 0; i < n; i++)
		if (games[i] > 0 && (best < 0 || games[i] > games[best])) best = i;
	return best;
}

static void note_skip(game_list *out, const char *name)
{
	if (out->skipped < LIB_SKIPS_SHOWN)
		snprintf(out->skipped_eg[out->skipped], LIB_NAME, "%s", name);
	out->skipped++;
}

bool lib_scan(const char *roms_root, const char *folder, const char *exts,
              game_list *out)
{
	bool any = lib_scan_listing(roms_root, folder, exts, out);

	lib_scan_finish(folder, out);
	return any;
}

/* The titles from the database, and the shelf's order (lib_scan's second half) */
void lib_scan_finish(const char *folder, game_list *l)
{
	db_titles titles;
	int i;

	if (!l->items || l->count <= 0) return;
	db_titles_load(db_lib(), folder, &titles);
	for (i = 0; i < l->count; i++) scan_title(&titles, &l->items[i]);
	db_titles_free(&titles);
	qsort(l->items, (size_t)l->count, sizeof *l->items, game_cmp);
}

/* Folders that hold a shelf's pictures by another frontend's habit, never its
 * games: EmulationStation's (lib_es_media_dir) and MinUI's Imgs. Never a
 * folder of games, and never left out either: nobody put games there. Most
 * shelves would find no game in them anyway, but PICO-8's, whose games are
 * .png files, would take every picture in them. */
static bool art_folder(const char *name)
{
	return lib_es_media_dir(name) || !strcasecmp(name, "Imgs");
}

/* A walk over a shelf's folder and the folders in it */
typedef struct {
	const char *exts;
	bool        hooked;    /* its games are folders with a hook (ScummVM's) */
	bool        scripts;   /* its games are scripts and its folders their data */
	bool        discs;     /* it takes disc images: a folder of one game's is a game */
	game_list  *out;
	int         cap;
	bool        full;      /* no memory for more */
} scan_walk;

/* The next entry, cleared, or NULL. Grows the list only once the memory is
 * there, so a failed grow never leaves the count beyond the buffer. */
static game_entry *walk_add(scan_walk *w)
{
	game_list *l = w->out;

	if (w->full) return NULL;
	if (l->count == w->cap) {
		int more = w->cap ? w->cap * 2 : 64;
		game_entry *bigger = realloc(l->items, (size_t)more * sizeof *bigger);

		if (!bigger) { w->full = true; return NULL; }
		l->items = bigger;
		w->cap = more;
	}
	memset(&l->items[l->count], 0, sizeof l->items[0]);
	l->items[l->count].added = LIB_ADDED_UNKNOWN;   /* lib_added, when it is asked for */
	return &l->items[l->count];
}

/* A game: `rel`'s folder (the top when empty), the launch path in it, and the
 * name. False when the path is too long to keep, which skips it whole rather
 * than keep a launch path cut short. */
static bool walk_game(scan_walk *w, const char *rel, const char *path, const char *name)
{
	game_entry *g = walk_add(w);
	size_t rl = strlen(rel);
	int k;

	if (!g) return false;
	k = snprintf(g->file, sizeof g->file, "%s%s%s", rel, rl ? "/" : "", path);
	if (k < 0 || k >= (int)sizeof g->file || rl > 0xffff) return false;
	snprintf(g->name, sizeof g->name, "%s", name);
	g->dir = (unsigned short)rl;
	w->out->count++;
	return true;
}

/* One folder of a shelf, read already (`l`): `abs` on the card, `rel` its
 * path in the shelf's folder ("" for the shelf's own). A file is a game,
 * launched directly; a folder is a game of its own when it is one (a disc
 * set, a ScummVM game), else a folder of games, read the same way. How many
 * games it has at every depth. */
static int walk_folder(scan_walk *w, const char *abs, const char *rel,
                       const dir_list *l, int depth)
{
	int first = w->out->count, files_n, added = 0;

	for (int i = 0; i < l->n; i++) {
		const char *nm = dir_name(l, i);
		char stem[LIB_NAME], *dot;

		if (l->type[i] == DT_DIR) continue;
		if (!lib_ext_allowed(ext_of(nm), w->exts)) {
			/* EmulationStation's list of the folder's games is no game
			 * the shelf left out (src/esmedia.h reads it) */
			if (l->type[i] == DT_REG && strcasecmp(nm, "gamelist.xml")) {
				char shown[LIB_PATH];

				snprintf(shown, sizeof shown, "%s%s%s", rel, *rel ? "/" : "", nm);
				note_skip(w->out, shown);
			}
			continue;
		}
		if (l->type[i] != DT_REG) continue;
		snprintf(stem, sizeof stem, "%s", nm);
		dot = strrchr(stem, '.');
		if (dot && dot != stem) *dot = '\0';
		added += walk_game(w, rel, nm, stem);
	}
	files_n = w->out->count;

	/* The folders, in the read's order. A folder shadowed by a game file of
	 * its own name beside it is left alone, so the two layouts of one game
	 * cannot double up. */
	for (int i = 0; i < l->n && !w->full; i++) {
		const char *nm = dir_name(l, i);
		char full[LIB_PATH * 3], sub[LIB_PATH], inside[LIB_NAME], shown[LIB_PATH + 2];
		dir_list in;
		bool game = false, shadowed = false;
		int got = 0;

		if (l->type[i] != DT_DIR) continue;
		if (art_folder(nm)) continue;
		snprintf(full, sizeof full, "%s/%s", abs, nm);
		snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", nm);
		snprintf(shown, sizeof shown, "%s/", sub);
		if (!dir_read(full, &in)) { dir_free(&in); note_skip(w->out, shown); continue; }
		for (int k = first; k < files_n; k++)
			if (!strcasecmp(w->out->items[k].name, nm)) { shadowed = true; break; }

		if (w->hooked) {
			/* A ScummVM game by its hook, else its first file: its own
			 * .bin or .img is game data, not a disc to launch. A folder
			 * with no file in it is a folder of games. */
			game = hook_pick(&in, inside, sizeof inside);
		} else {
			set_info s;

			set_read(&in, &s);
			/* A disc set: the discs of one game, or a playlist. On Ports'
			 * shelf any disc image, as it always was: a port's folder is
			 * never looked into. Discs of several games are a folder of
			 * them, on a shelf that takes discs. */
			game = s.pick[0] && (w->scripts || !w->discs ? w->scripts
			                                             : s.m3u || s.games <= 1);
			if (game) snprintf(inside, sizeof inside, "%s", s.pick);
		}
		if (game) {
			char launch[LIB_PATH];

			snprintf(launch, sizeof launch, "%s/%s", nm, inside);
			if (!shadowed) added += walk_game(w, rel, launch, nm);
		} else if (!w->scripts && depth < LIB_DEPTH) {
			int skipped = w->out->skipped;

			got = walk_folder(w, full, sub, &in, depth + 1);
			added += got;
			/* A folder with no game in it is one thing left out, as it
			 * always was, not each file in it */
			if (!got) {
				w->out->skipped = skipped;
				note_skip(w->out, shown);
			}
		} else {
			note_skip(w->out, shown);
		}
		dir_free(&in);
	}
	return added;
}

/* A shelf that takes disc images: one of the formats only a disc is (a .bin
 * or an .img can be a cartridge's dump: the Atari 2600's, the Genesis's) */
static bool shelf_discs(const char *exts)
{
	static const char *const ONLY[] = {
		"m3u", "chd", "cue", "gdi", "cdi", "ccd", "toc", "iso", "pbp", "mdf", "cbn", "mds", NULL
	};

	if (!exts || !*exts) return true;
	for (int i = 0; ONLY[i]; i++)
		if (lib_ext_allowed(ONLY[i], exts)) return true;
	return false;
}

bool lib_scan_listing(const char *roms_root, const char *folder, const char *exts,
                      game_list *out)
{
	char dirpath[LIB_PATH * 2];
	dir_list top;
	scan_walk w;

	lib_free(out);
	out->scanned = true;
	out->skipped = 0;
	snprintf(dirpath, sizeof dirpath, "%s/%s", roms_root, folder);
	if (!dir_read(dirpath, &top)) {
		dir_free(&top);
		/* What was read before memory ran out is no listing: none */
		return false;
	}
	memset(&w, 0, sizeof w);
	w.exts = exts;
	w.hooked = lib_shelf_hooked(exts);
	w.scripts = lib_shelf_scripts(exts);
	w.discs = shelf_discs(exts);
	w.out = out;
	walk_folder(&w, dirpath, "", &top, 0);
	dir_free(&top);
	if (out->count == 0) { free(out->items); out->items = NULL; return false; }
	return true;
}

/* FNV-1a, 64 bits */
static uint64_t fnv(uint64_t h, const char *s)
{
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
	return (h ^ 0xff) * 1099511628211ULL;      /* the end, so "ab"+"c" is not "a"+"bc" */
}

uint64_t lib_listing_digest(const game_list *l)
{
	uint64_t sum = 0;
	int i;

	/* A sum of the entries' hashes, so the order they came in does not count;
	 * then the counts beside it. The folder a game is in counts: a disc set
	 * at the top and a disc loose in a folder of the same name are two
	 * listings. */
	for (i = 0; i < l->count; i++)
		sum += fnv(fnv(14695981039346656037ULL ^ l->items[i].dir, l->items[i].file),
		           l->items[i].name);
	return sum * 1099511628211ULL ^
	       ((uint64_t)(unsigned)l->count << 32 | (uint64_t)(unsigned)l->skipped);
}
