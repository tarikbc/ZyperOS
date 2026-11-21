/* SPDX-License-Identifier: GPL-3.0-only */
#include "eq.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "db.h"
#include "i18n.h"

/* The lab's curves (the Sound lab, 2026-11-19): Bass lifts the bottom with a
 * touch of air on top, Voice the middle where words are, Treble the top */
const int8_t EQ_CURVE[EQ_PRESETS][EQ_BANDS] = {
	[EQ_OFF]    = {  0, 0, 0, 0, 0 },
	[EQ_BASS]   = {  5, 2, 0, 0, 1 },
	[EQ_VOICE]  = { -2, 0, 3, 3, 0 },
	[EQ_TREBLE] = {  0, 0, 0, 3, 5 },
	[EQ_CUSTOM] = {  0, 0, 0, 0, 0 },
};

static const char *const IDS[EQ_PRESETS] = { "off", "bass", "voice", "treble", "custom" };
static const char *const OUT_KEY[EQ_OUTS] = { "eq.speaker", "eq.phones" };

static eq_preset valid(eq_preset p)
{
	return p >= EQ_OFF && p < EQ_PRESETS ? p : EQ_OFF;
}

static eq_out out_ok(eq_out o)
{
	return o == EQ_OUT_PHONES ? EQ_OUT_PHONES : EQ_OUT_SPEAKER;
}

eq_out eq_out_of(aout_dest d)
{
	return d == AOUT_SPK ? EQ_OUT_SPEAKER : EQ_OUT_PHONES;
}

const char *eq_preset_id(eq_preset p) { return IDS[valid(p)]; }

eq_preset eq_preset_parse(const char *id, eq_preset fallback)
{
	int i;

	if (id)
		for (i = 0; i < EQ_PRESETS; i++)
			if (!strcmp(id, IDS[i])) return (eq_preset)i;
	return valid(fallback);
}

const char *eq_preset_name(eq_preset p)
{
	switch (valid(p)) {
	case EQ_BASS:   return T(S_EQ_BASS);
	case EQ_VOICE:  return T(S_EQ_VOICE);
	case EQ_TREBLE: return T(S_EQ_TREBLE);
	case EQ_CUSTOM: return T(S_EQ_CUSTOM);
	default:        return T(S_MENU_OFF_TITLE);
	}
}

const char *eq_preset_note(eq_preset p)
{
	switch (valid(p)) {
	case EQ_BASS:   return T(S_EQ_NOTE_BASS);
	case EQ_VOICE:  return T(S_EQ_NOTE_VOICE);
	case EQ_TREBLE: return T(S_EQ_NOTE_TREBLE);
	case EQ_CUSTOM: return T(S_EQ_NOTE_CUSTOM);
	default:        return T(S_EQ_NOTE_OFF);
	}
}

const char *eq_out_name(eq_out o)
{
	return out_ok(o) == EQ_OUT_PHONES ? T(S_EQ_HEADPHONES) : T(S_AUDIO_SPEAKER);
}

void eq_band_name(int band, char *out, size_t n)
{
	const int hz = band >= 0 && band < EQ_BANDS ? (int)EQ_FREQ[band] : 0;

	if (!n) return;
	if (hz >= 1000) snprintf(out, n, "%d kHz", hz / 1000);
	else            snprintf(out, n, "%d Hz", hz);
}

eq_preset eq_preset_get(eq_out o)
{
	char v[16];

	if (!db_dev() || !db_get_str(db_dev(), OUT_KEY[out_ok(o)], v, sizeof v, NULL)) return EQ_OFF;
	return eq_preset_parse(v, EQ_OFF);
}

void eq_preset_set(eq_out o, eq_preset p)
{
	if (!db_dev()) return;
	/* Off is a new card's: no key */
	if (valid(p) == EQ_OFF) db_del(db_dev(), OUT_KEY[out_ok(o)]);
	else                    db_set_str(db_dev(), OUT_KEY[out_ok(o)], IDS[valid(p)]);
}

void eq_bands_wire(const int8_t db[EQ_BANDS], char *out, size_t n)
{
	snprintf(out, n, "%d,%d,%d,%d,%d", db[0], db[1], db[2], db[3], db[4]);
}

bool eq_bands_parse(const char *s, int8_t db[EQ_BANDS])
{
	int8_t got[EQ_BANDS];
	int b;

	if (!s) return false;
	for (b = 0; b < EQ_BANDS; b++) {
		char *end;
		long v = strtol(s, &end, 10);

		if (end == s || v < EQ_DB_MIN || v > EQ_DB_MAX) return false;
		got[b] = (int8_t)v;
		s = end;
		if (b < EQ_BANDS - 1) {
			if (*s != ',') return false;
			s++;
		}
	}
	if (*s) return false;
	memcpy(db, got, sizeof got);
	return true;
}

void eq_custom_get(eq_out o, int8_t db[EQ_BANDS])
{
	char key[32], v[64];

	memset(db, 0, EQ_BANDS);
	snprintf(key, sizeof key, "%s.bands", OUT_KEY[out_ok(o)]);
	if (db_dev() && db_get_str(db_dev(), key, v, sizeof v, NULL)) eq_bands_parse(v, db);
}

void eq_custom_set(eq_out o, const int8_t db[EQ_BANDS])
{
	char key[32], v[64];

	if (!db_dev()) return;
	snprintf(key, sizeof key, "%s.bands", OUT_KEY[out_ok(o)]);
	if (eq_flat(db)) {
		db_del(db_dev(), key);
		return;
	}
	eq_bands_wire(db, v, sizeof v);
	db_set_str(db_dev(), key, v);
}

bool eq_flat(const int8_t db[EQ_BANDS])
{
	int b;

	for (b = 0; b < EQ_BANDS; b++)
		if (db[b]) return false;
	return true;
}

void eq_bands(eq_out o, int8_t db[EQ_BANDS])
{
	const eq_preset p = eq_preset_get(o);

	if (p == EQ_CUSTOM) eq_custom_get(o, db);
	else                memcpy(db, EQ_CURVE[p], EQ_BANDS);
}

bool eq_band_step(eq_out o, int band, int dir)
{
	int8_t db[EQ_BANDS];
	int v;

	if (band < 0 || band >= EQ_BANDS || !dir) return false;
	eq_bands(o, db);
	v = db[band] + (dir < 0 ? -1 : 1);
	if (v < EQ_DB_MIN || v > EQ_DB_MAX) return false;
	db[band] = (int8_t)v;
	/* A step off a preset makes its curve the player's own, with the step */
	eq_custom_set(o, db);
	eq_preset_set(o, EQ_CUSTOM);
	return true;
}

/* ---- the file a port's plugin reads ----------------------------------- */

static const char *share_path(void)
{
	const char *e = getenv("ZYPEROS_EQ_FILE");

	return e && *e ? e : EQ_SHARE_PATH;
}

static eq_share *share_map(void);

bool eq_share_write(const int8_t db[EQ_BANDS])
{
	eq_share *map = share_map();

	if (!map) return false;
	if (!memcmp(map->db, db, EQ_BANDS)) return true;
	/* Odd while the bands change, even after: a reader that sees either
	 * odd or a change reads again (eq_share_read) */
	eq_share_put(map, db, -1);
	return true;
}

bool eq_share_gain(int db)
{
	eq_share *map = share_map();

	if (!map) return false;
	if (map->gain != db) eq_share_put(map, NULL, db < 0 ? 0 : db);
	return true;
}

int eq_share_gain_get(void)
{
	eq_share *map = share_map();
	int8_t db[EQ_BANDS], gain = 0;

	if (!map || !eq_share_read(map, db, &gain)) return -1;
	return gain < 0 ? 0 : gain > EQ_GAIN_MAX ? EQ_GAIN_MAX : gain;
}

bool eq_share_cut(bool cut)
{
	eq_share *map = share_map();

	if (!map) return false;
	eq_share_cut_put(map, cut);
	return true;
}

/* The shared file, mapped once (and again for another path) */
static eq_share *share_map(void)
{
	static eq_share *map;
	static char mapped[256];
	/* A file that would not open is tried again five seconds on, not every
	 * frame the menus route (a computer has no /run/zyperos) */
	static time_t failed_at;
	static char failed_for[256];
	const char *path = share_path();

	if (map && strcmp(mapped, path)) {
		munmap(map, sizeof *map);
		map = NULL;
	}
	if (!map && !strcmp(failed_for, path) && time(NULL) - failed_at < 5) return NULL;
	if (!map) {
		char dir[256], *slash;
		int fd;

		snprintf(failed_for, sizeof failed_for, "%s", path);
		failed_at = time(NULL);

		/* Its folder first: /run/zyperos is the launcher's, made at need */
		snprintf(dir, sizeof dir, "%s", path);
		if ((slash = strrchr(dir, '/')) && slash != dir) {
			*slash = '\0';
			if (mkdir(dir, 0755) < 0 && errno != EEXIST) return NULL;
		}
		fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		void *m;

		if (fd < 0) return NULL;
		if (ftruncate(fd, (off_t)sizeof *map) < 0) {
			close(fd);
			return NULL;
		}
		m = mmap(NULL, sizeof *map, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (m == MAP_FAILED) return NULL;
		map = m;
		failed_for[0] = '\0';
		snprintf(mapped, sizeof mapped, "%s", path);
		if (map->magic != EQ_SHARE_MAGIC || (map->seq & 1)) {
			/* A new file, or one a writer left half done: whole, flat */
			map->seq = 0;
			memset(map->db, 0, sizeof map->db);
			map->gain = 0;
			map->cut = 0;
			map->pad = 0;
			map->magic = EQ_SHARE_MAGIC;
		}
	}
	return map;
}
