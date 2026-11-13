/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHEET_H
#define ZYPEROS_SHEET_H

/* A disc game's sheet - a .cue, .gdi or .toc naming its track files, an .m3u
 * its discs - read for the files it names, and the files kept beside a sheet
 * by its stem rather than named in it. Delete Game takes them with the game
 * (src/gamedel.h), and File Transfer moves them with it (src/hare.c), so the
 * two read a sheet alike. No SDL, nothing of the launcher's. */
#include <stdbool.h>
#include <stddef.h>

/* A path's room, as the library's (LIB_PATH) */
#define SHEET_PATH  544
#define SHEET_MAX   (64 * 1024)
#define SHEET_NAMES 128

typedef struct {
	char name[SHEET_NAMES][SHEET_PATH];
	int  n;
	bool outside;              /* a name that leaves the folder */
} sheet_names;

/* Whether a file is a sheet: .cue, .gdi, .toc or .m3u */
bool sheet_is(const char *name);

/* What the sheet at `rel` (relative to the folder `root`) names, added to
 * `s` as paths relative to `root`; a .m3u's sheets are read for their tracks
 * too, one level down (`depth` 0). A name that would leave `root` sets
 * `outside` and is not added. */
void sheet_read(const char *root, const char *rel, sheet_names *s, int depth);

/* The extensions of the files kept beside `file` by its stem: a .ccd's .img
 * and .sub, an .mds's .mdf and the other way round, a .cue's .sbi (PCSX
 * ReARMed's). How many, into `with`. */
int sheet_sides(const char *file, const char *with[2]);

#endif
