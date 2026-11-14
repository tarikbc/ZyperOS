/* SPDX-License-Identifier: GPL-3.0-only */
/* See snaps.h. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "snaps.h"

#include "db.h"
#include "sys_menu.h"

#define K_FETCH "snaps.fetch"
#define K_PLAY  "snaps.play"
#define K_SOUND "snaps.sound"
/* A system's own videos switch, by its folder (snaps_get), and which games */
#define K_GET   "snaps.get."
#define K_FOR   "snaps.for"
#define K_COV   "scraper.covers."
/* One key a console turned off, by its folder: on is the default, so a card
 * with new consoles has their snaps on without a key for each. */
#define K_OFF   "snaps.off."
/* The sound setting as the database keeps it, by SNAP_MUTED, SNAP_QUIET and
 * SNAP_FULL: its own words, apart from SNAP_SOUND_NAME, which is what MENU
 * shows and goes into the language that is on */
static const char *const SOUND_ID[3] = { "muted", "quiet", "full" };

bool snaps_fetch(void)        { return db_get_int(db_dev(), K_FETCH, 0) != 0; }
void snaps_set_fetch(bool on) { db_set_int(db_dev(), K_FETCH, on ? 1 : 0); }
bool snaps_play(void)         { return db_get_int(db_dev(), K_PLAY, 1) != 0; }
void snaps_set_play(bool on)  { db_set_int(db_dev(), K_PLAY, on ? 1 : 0); }

int snaps_sound(void)
{
	char v[16];
	int i;

	db_get_str(db_dev(), K_SOUND, v, sizeof v, SOUND_ID[SNAP_MUTED]);
	for (i = 0; i < 3; i++)
		if (!strcmp(v, SOUND_ID[i])) return i;
	return SNAP_MUTED;
}

void snaps_set_sound(int s)
{
	if (s < SNAP_MUTED || s > SNAP_FULL) s = SNAP_MUTED;
	db_set_str(db_dev(), K_SOUND, SOUND_ID[s]);
}

static bool off_key(const char *folder, char *k, size_t n)
{
	return folder && *folder &&
	       snprintf(k, n, K_OFF "%s", folder) < (int)n;
}

bool snaps_console_on(const char *folder)
{
	char k[160];

	return !off_key(folder, k, sizeof k) || !db_has(db_dev(), k);
}

void snaps_set_console(const char *folder, bool on)
{
	char k[160];

	if (!off_key(folder, k, sizeof k)) return;
	if (on) db_del(db_dev(), k);
	else    db_set_int(db_dev(), k, 1);
}

bool snaps_path(const char *roms, const char *folder, const char *stem,
                char *out, size_t n)
{
	return roms && folder && stem &&
	       snprintf(out, n, "%s/%s/.media/%s.mp4", roms, folder, stem) < (int)n;
}

/* Every snap in a console's .media folder, handed to `fn` with its size. */
/* Each file of `ext` (".mp4", ".png") in a system's .media: its bytes, and
 * how many, deleted on the way with `del`; the first `limit` alone when it
 * is above 0 */
static unsigned long long each(const char *roms, const char *folder, const char *ext, bool del, int *count,
                               int limit)
{
	char dir[1024], path[1400];
	unsigned long long sum = 0;
	struct dirent *e;
	DIR *d;

	if (count) *count = 0;
	if (snprintf(dir, sizeof dir, "%s/%s/.media", roms, folder) >= (int)sizeof dir)
		return 0;
	if (!(d = opendir(dir))) return 0;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		struct stat st;

		if (limit > 0 && count && *count >= limit) break;
		if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ext))
			continue;
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (del && unlink(path) != 0) continue;
		sum += (unsigned long long)st.st_size;
		if (count) (*count)++;
	}
	closedir(d);
	return sum;
}

unsigned long long snaps_bytes(const char *roms, const char *folder)
{
	return each(roms, folder, ".mp4", false, NULL, 0);
}

unsigned long long snaps_delete(const char *roms, const char *folder)
{
	return each(roms, folder, ".mp4", true, NULL, 0);
}

unsigned long long snaps_cover_bytes(const char *roms, const char *folder, int *n, int limit)
{
	return each(roms, folder, ".png", false, n, limit);
}

bool snaps_get(const char *folder)
{
	char k[160];

	if (!folder || !*folder || snprintf(k, sizeof k, K_GET "%s", folder) >= (int)sizeof k)
		return false;
	if (db_has(db_dev(), k)) return db_get_int(db_dev(), k, 0) != 0;
	return snaps_fetch() && snaps_console_on(folder);
}

void snaps_set_get(const char *folder, bool on)
{
	char k[160];

	if (!folder || !*folder || snprintf(k, sizeof k, K_GET "%s", folder) >= (int)sizeof k)
		return;
	db_set_int(db_dev(), k, on ? 1 : 0);
}

bool snaps_covers(const char *folder)
{
	char k[160];

	if (!folder || !*folder || snprintf(k, sizeof k, K_COV "%s", folder) >= (int)sizeof k)
		return false;
	return db_get_int(db_dev(), k, 1) != 0;
}

void snaps_set_covers(const char *folder, bool on)
{
	char k[160];

	if (!folder || !*folder || snprintf(k, sizeof k, K_COV "%s", folder) >= (int)sizeof k)
		return;
	if (on) db_del(db_dev(), k);
	else    db_set_int(db_dev(), k, 0);
}

static const char *const FOR_ID[SNAPS_FOR_COUNT] = { "all", "favorites", "played" };

int snaps_for(void)
{
	char v[16];
	int i;

	db_get_str(db_dev(), K_FOR, v, sizeof v, FOR_ID[SNAPS_FOR_ALL]);
	for (i = 0; i < SNAPS_FOR_COUNT; i++)
		if (!strcmp(v, FOR_ID[i])) return i;
	return SNAPS_FOR_ALL;
}

void snaps_set_for(int which)
{
	if (which < 0 || which >= SNAPS_FOR_COUNT) which = SNAPS_FOR_ALL;
	db_set_str(db_dev(), K_FOR, FOR_ID[which]);
}

bool snaps_for_game(int which, bool favorite, bool played)
{
	return which == SNAPS_FOR_FAVORITES ? favorite : which == SNAPS_FOR_PLAYED ? played : true;
}

static int stem_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static char **stems(const char *roms, const char *folder, const char *ext, int *n)
{
	char dir[1024], **v = NULL;
	struct dirent *e;
	int cap = 0;
	DIR *d;

	*n = 0;
	if (!roms || !folder || snprintf(dir, sizeof dir, "%s/%s/.media", roms, folder) >= (int)sizeof dir ||
	    !(d = opendir(dir))) return NULL;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char *stem;

		if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ext)) continue;
		if (*n == cap) {
			char **w = realloc(v, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *v);

			if (!w) break;
			v = w;
		}
		if (!(stem = strndup(e->d_name, len - 4))) break;
		v[(*n)++] = stem;
	}
	closedir(d);
	if (*n > 1) qsort(v, (size_t)*n, sizeof *v, stem_cmp);
	return v;
}

char **snaps_stems(const char *roms, const char *folder, int *n)
{
	return stems(roms, folder, ".mp4", n);
}

char **snaps_cover_stems(const char *roms, const char *folder, int *n)
{
	return stems(roms, folder, ".png", n);
}

void snaps_stems_free(char **v, int n)
{
	int i;

	for (i = 0; i < n; i++) free(v[i]);
	free(v);
}
