/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See snaps.h. */
#include <dirent.h>
#include <stdio.h>
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
/* One key a console turned off, by its folder: on is the default, so a card
 * with new consoles has their snaps on without a key for each. */
#define K_OFF   "snaps.off."

bool snaps_fetch(void)        { return db_get_int(db_dev(), K_FETCH, 0) != 0; }
void snaps_set_fetch(bool on) { db_set_int(db_dev(), K_FETCH, on ? 1 : 0); }
bool snaps_play(void)         { return db_get_int(db_dev(), K_PLAY, 1) != 0; }
void snaps_set_play(bool on)  { db_set_int(db_dev(), K_PLAY, on ? 1 : 0); }

int snaps_sound(void)
{
	char v[16];
	int i;

	db_get_str(db_dev(), K_SOUND, v, sizeof v, SNAP_SOUND_NAME[SNAP_MUTED]);
	for (i = 0; i < 3; i++)
		if (!strcmp(v, SNAP_SOUND_NAME[i])) return i;
	return SNAP_MUTED;
}

void snaps_set_sound(int s)
{
	if (s < SNAP_MUTED || s > SNAP_FULL) s = SNAP_MUTED;
	db_set_str(db_dev(), K_SOUND, SNAP_SOUND_NAME[s]);
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
static unsigned long long each(const char *roms, const char *folder, bool del)
{
	char dir[1024], path[1400];
	unsigned long long sum = 0;
	struct dirent *e;
	DIR *d;

	if (snprintf(dir, sizeof dir, "%s/%s/.media", roms, folder) >= (int)sizeof dir)
		return 0;
	if (!(d = opendir(dir))) return 0;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		struct stat st;

		if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ".mp4"))
			continue;
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (del && unlink(path) != 0) continue;
		sum += (unsigned long long)st.st_size;
	}
	closedir(d);
	return sum;
}

unsigned long long snaps_bytes(const char *roms, const char *folder)
{
	return each(roms, folder, false);
}

unsigned long long snaps_delete(const char *roms, const char *folder)
{
	return each(roms, folder, true);
}
