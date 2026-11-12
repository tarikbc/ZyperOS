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
 * PlayStation folder that holds an .mds beside its .mdf still opens the .mdf. */
static const char *DISC_EXTS[] = {
	"m3u", "chd", "cue", "ccd", "toc", "iso", "img", "pbp", "mdf", "cbn", "bin", "mds", NULL
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

static int disc_rank(const char *name)
{
	const char *ext = ext_of(name);
	if (!ext) return -1;
	for (int i = 0; DISC_EXTS[i]; i++)
		if (strcasecmp(ext, DISC_EXTS[i]) == 0) return i;
	return -1;
}

/* Pick the file to launch inside a game folder: the best-ranked disc image,
 * ties broken alphabetically so a discs-only folder lands on Disc 1. */
static bool folder_launch_file(const char *dirpath, char *out, size_t outsz)
{
	DIR *d = opendir(dirpath);
	struct dirent *e;
	int best = 9999;
	char pick[LIB_NAME] = "";

	if (!d) return false;
	while ((e = readdir(d))) {
		int r;
		if (e->d_name[0] == '.') continue;
		r = disc_rank(e->d_name);
		if (r < 0) continue;
		if (r < best || (r == best && strcasecmp(e->d_name, pick) < 0)) {
			best = r;
			snprintf(pick, sizeof pick, "%s", e->d_name);
		}
	}
	closedir(d);
	if (!pick[0]) return false;
	snprintf(out, outsz, "%s", pick);
	return true;
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

long lib_added(const char *roms_root, const char *folder, const char *file)
{
	char path[LIB_PATH * 3];
	const char *slash = strchr(file, '/');
	struct stat st;

	snprintf(path, sizeof path, "%s/%s/%.*s", roms_root, folder,
	         slash ? (int)(slash - file) : (int)strlen(file), file);
	return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

void lib_free(game_list *l)
{
	free(l->items);
	l->items = NULL;
	l->count = 0;
	l->scanned = false;
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

static bool is_file(const char *full, const struct dirent *e)
{
#ifdef DT_REG
	if (e->d_type == DT_REG) return true;
	if (e->d_type != DT_UNKNOWN) return false;
#else
	(void)e;
#endif
	{
		struct stat st;
		return stat(full, &st) == 0 && S_ISREG(st.st_mode);
	}
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

/* The file to launch in a game folder on such a shelf: its hook, else its
 * first file by name, from which the core detects the game. Only on a hooked
 * shelf: anywhere else a folder of loose files is not one game, and staying
 * skipped is what keeps a folder of NES hacks from becoming a single entry. */
static bool folder_hook_file(const char *dirpath, const char *exts, char *out, size_t outsz)
{
	DIR *d;
	struct dirent *e;
	char hook[LIB_NAME] = "", first[LIB_NAME] = "";

	if (!lib_shelf_hooked(exts) || !(d = opendir(dirpath))) return false;
	while ((e = readdir(d))) {
		char full[LIB_PATH * 3];
		const char *ext = ext_of(e->d_name);
		char *pick = NULL;
		int i;

		if (e->d_name[0] == '.') continue;
		snprintf(full, sizeof full, "%s/%s", dirpath, e->d_name);
		if (!is_file(full, e)) continue;
		for (i = 0; ext && HOOK_EXTS[i]; i++)
			if (strcasecmp(ext, HOOK_EXTS[i]) == 0) pick = hook;
		if (!pick) pick = first;
		if (!pick[0] || strcasecmp(e->d_name, pick) < 0)
			snprintf(pick, LIB_NAME, "%s", e->d_name);
	}
	closedir(d);
	if (!hook[0] && !first[0]) return false;
	snprintf(out, outsz, "%s", hook[0] ? hook : first);
	return true;
}

/* What EmulationStation's scrapers and its systems put in a console's folder
 * (Skraper's media/ layout, Batocera's images/ and videos/, ArkOS's and
 * ROGUE OS's downloaded_images/, RetroPie's and Recalbox's media/ folders) */
bool lib_es_media_dir(const char *name)
{
	static const char *const DIRS[] = {
		"images", "videos", "media", "manuals", "downloaded_images", "downloaded_videos",
		"boxart", "box2dfront", "box3d", "covers", "marquees", "wheel", "snap", "snaps",
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

bool lib_scan_listing(const char *roms_root, const char *folder, const char *exts,
                      game_list *out)
{
	char dirpath[LIB_PATH * 2];
	DIR *d;
	struct dirent *e;
	int cap = 64, n = 0, files_n, ndirs = 0, dcap = 0, i;
	char (*dirs)[LIB_NAME] = NULL;
	game_entry *list;

	lib_free(out);
	out->scanned = true;
	out->skipped = 0;
	snprintf(dirpath, sizeof dirpath, "%s/%s", roms_root, folder);
	d = opendir(dirpath);
	if (!d) return false;
	list = malloc((size_t)cap * sizeof *list);
	if (!list) { closedir(d); return false; }

	/* One read of the folder. A file at the top level is a game, launched
	 * directly; a subfolder is kept for the second pass below. It was read
	 * twice, the second time (rewinddir) for its few subfolders alone, and
	 * on the Pixel 2's exFAT card a directory entry costs about 40 us to
	 * read: on my card, 8,844 games, that second read was about 380 ms
	 * of every start (2026-11-07). */
	while ((e = readdir(d))) {
		char full[LIB_PATH * 3];
		char *dot;
		int type;

		if (e->d_name[0] == '.') continue;
		snprintf(full, sizeof full, "%s/%s", dirpath, e->d_name);
		type = entry_type(full, e);
		if (type == DT_DIR) {
			if (ndirs == dcap) {
				int more = dcap ? dcap * 2 : 16;
				char (*bigger)[LIB_NAME] = realloc(dirs, (size_t)more * sizeof *dirs);

				if (!bigger) continue;
				dirs = bigger;
				dcap = more;
			}
			snprintf(dirs[ndirs++], sizeof dirs[0], "%s", e->d_name);
			continue;
		}
		if (!lib_ext_allowed(ext_of(e->d_name), exts)) {
			/* EmulationStation's list of the folder's games is no game
			 * the shelf left out (src/esmedia.h reads it) */
			if (type == DT_REG && strcasecmp(e->d_name, "gamelist.xml")) note_skip(out, e->d_name);
			continue;
		}
		if (type != DT_REG) continue;
		if (n == cap) {
			/* Grow cap only once the memory is actually there. Doubling it
			 * first and then bailing would leave the second pass believing
			 * there is room in a buffer that never grew. */
			game_entry *bigger = realloc(list, (size_t)cap * 2 * sizeof *list);
			if (!bigger) break;
			list = bigger;
			cap *= 2;
		}
		memset(&list[n], 0, sizeof list[n]);
		snprintf(list[n].file, sizeof list[n].file, "%s", e->d_name);
		snprintf(list[n].name, sizeof list[n].name, "%s", e->d_name);
		list[n].added = LIB_ADDED_UNKNOWN;   /* lib_added, when it is asked for */
		dot = strrchr(list[n].name, '.');
		if (dot && dot != list[n].name) *dot = '\0';
		n++;
	}

	/* Pass 2, over the subfolders the read found, in its order: a subfolder
	 * holding a disc image is a game too -- one entry named after the
	 * folder, launching the image inside it -- and so is any subfolder on a
	 * hooked shelf (ScummVM's, folder_hook_file). Skip a folder shadowed by
	 * a same-named top-level file so the two layouts cannot double up. */
	files_n = n;
	for (i = 0; i < ndirs; i++) {
		char full[LIB_PATH * 3], inside[LIB_NAME];
		const char *name = dirs[i];
		bool shadowed = false;

		snprintf(full, sizeof full, "%s/%s", dirpath, name);
		for (int k = 0; k < files_n; k++)
			if (strcasecmp(list[k].name, name) == 0) { shadowed = true; break; }
		if (shadowed) continue;
		/* EmulationStation's art and video, read by src/esmedia.c, and
		 * nothing anyone copied in as a game: not left out either */
		if (lib_es_media_dir(name)) continue;
		/* A hooked shelf's folder by its hook alone: a ScummVM game's own
		 * .bin or .img is game data, not a disc to launch. */
		if (lib_shelf_hooked(exts) ? !folder_hook_file(full, exts, inside, sizeof inside)
		                       : !folder_launch_file(full, inside, sizeof inside)) {
			char shown[LIB_NAME];

			snprintf(shown, sizeof shown, "%.*s/", LIB_NAME - 2, name);
			note_skip(out, shown);
			continue;
		}
		if (n == cap) {
			game_entry *bigger = realloc(list, (size_t)cap * 2 * sizeof *list);
			if (!bigger) break;
			list = bigger;
			cap *= 2;
		}
		memset(&list[n], 0, sizeof list[n]);
		snprintf(list[n].file, sizeof list[n].file, "%s/%s", name, inside);
		snprintf(list[n].name, sizeof list[n].name, "%s", name);
		list[n].added = LIB_ADDED_UNKNOWN;   /* the folder's, by lib_added */
		n++;
	}
	free(dirs);
	closedir(d);

	if (n == 0) { free(list); return false; }
	out->items = list;
	out->count = n;
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
	 * then the counts beside it */
	for (i = 0; i < l->count; i++)
		sum += fnv(fnv(14695981039346656037ULL, l->items[i].file), l->items[i].name);
	return sum * 1099511628211ULL ^
	       ((uint64_t)(unsigned)l->count << 32 | (uint64_t)(unsigned)l->skipped);
}
