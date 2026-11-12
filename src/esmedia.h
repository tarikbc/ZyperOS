/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ESMEDIA_H
#define ZYPEROS_ESMEDIA_H

/* EmulationStation's art, videos and game list, where another system left
 * them on the card (DESIGN.md 11.37; docs/guide.md, Coming from
 * EmulationStation). Knulli and Batocera, ROCKNIX, ArkOS, AmberELEC, ROGUE
 * OS, RetroPie and Recalbox scrape into each console's folder: a
 * gamelist.xml naming each game's name, details, picture and video, and the
 * files themselves in images/, videos/ or media/ beside the games.
 *
 * ZYPEROS'S OWN COME FIRST. A cover is Roms/<folder>/.media/<name>.png, and
 * a video snap .media/<name>.mp4, as the scraper and Cover and Video keep
 * them; these are read only for a game with none of its own. A game's
 * details and title are its row in the card's library database, and
 * gamelist.xml's go in only for a game with no row (es_import): a scrape
 * after that writes over them, an import never writes over a scrape.
 *
 * The order for one game, where the folder has the file:
 *   cover  gamelist.xml's <thumbnail> (the box, on Batocera), <image>, then
 *          images/<name>-thumb, -image and plain, media/box2dfront/,
 *          media/images/, boxart/, downloaded_images/ (.png, then .jpg),
 *          and last its logo: <marquee>, images/<name>-marquee.png,
 *          media/marquees/, media/wheel/
 *   video  gamelist.xml's <video>, then videos/<name>-video.mp4, videos/,
 *          media/videos/, downloaded_videos/
 * gamelist.xml's paths are taken as ES writes them: "./images/x.png",
 * relative to the folder, or a path on the other system ("/userdata/roms/
 * nds/images/x.png", "/storage/roms/nds/...") that finds the folder by its
 * name, or by "roms/". A game is found in it by its file ("./x.nds", any
 * case), else by its name, which is how a disc set listed by its .m3u
 * finds the folder ZyperOS lists.
 *
 * One index a folder, built the first time a game of it asks (es_media with
 * `wait`, from Box Art's thread or the cover worker), and kept until the card
 * changes (es_forget): which media folders it has, a stat each, and
 * gamelist.xml read once into a table of its games. A folder with neither
 * costs nothing after its first look. No SDL: tools/esmedia-check.c links
 * it with src/db.c and src/library.c. */

#include <stdbool.h>
#include <stddef.h>

#include "db.h"
#include "library.h"

#define ES_PATH 1200

typedef enum { ES_COVER, ES_VIDEO } es_kind;

/* The game's picture or video in Roms/<folder>, its full path into `out`:
 * the game is `file`, the shelf's launch path, and `name`, its stem. False
 * when the folder has none for it. With `wait` false it answers false while
 * the folder's index is not built, rather than build it there: the frame's
 * callers (es_ready says when to ask again). Every path answered is a file
 * that is there. */
bool es_media(const char *roms, const char *folder, const char *file, const char *name,
              es_kind kind, bool wait, char *out, size_t n);

/* Whether the game has an EmulationStation video, from the folder's index
 * alone (no file is looked at): for the play mark on a cover, asked every
 * frame. False while the index is not built. */
bool es_has_video(const char *folder, const char *file, const char *name);

/* Whether `folder`'s index is built, so es_media without `wait` answers */
bool es_ready(const char *folder);

/* The folder's index, built now when it is not (es_media's `wait`) */
void es_build(const char *roms, const char *folder);

/* What gamelist.xml says of the game, as ZyperOS's details: its name as the
 * title, <desc> the synopsis, <releasedate>'s year, <genre>, <players>,
 * <developer>, <publisher> and <rating> (0 to 1) out of 20. `favorite` its
 * <favorite>, when not NULL. False with no entry for it. The title is left
 * empty where ES only wrote the file's own name, which ZyperOS tidies
 * better (lib_title). Builds the index if need be. */
bool es_details(const char *roms, const char *folder, const char *file, const char *name,
                game_meta *out, bool *favorite);

/* Every index dropped: the card changed under them (a rescan) */
void es_forget(void);

/* ---- into the library database ------------------------------------------
 *
 * gamelist.xml's details for each game of `list` (a listing of
 * Roms/<folder>) that has no row in the card's library database yet: a row
 * written for it (db_game_set, one transaction), and the game named in
 * `out` with the title and favorite it came with, for the shelf to show at
 * once. Games with a row are left: a scrape, a rename or an earlier import
 * wrote it. An entry with nothing to say beyond the file's name writes no
 * row. For the thread after a start's first frame. How many rows. */
typedef struct {
	char file[LIB_PATH];
	char title[LIB_NAME];
	bool favorite;
} es_got;

typedef struct {
	es_got *got;
	int     n, cap;
} es_import_list;

int  es_import(const char *roms, const char *folder, const game_list *list,
               es_import_list *out);
void es_import_free(es_import_list *l);

/* ---- for Rename (src/gamern.h) ------------------------------------------
 *
 * The game's own ES files, relative to the folder: each that gamelist.xml's
 * entry names inside ES's media folders (images/, videos/, media/...,
 * lib_es_media_dir: a list never moves a game of the folder), and each
 * es_media's names find (images/<name>-image.png and the rest), into `rel`,
 * up to `max`, with what each becomes when the game is named `to` into
 * `rel_to`: the game's name in it changed, in the same folder. A file whose
 * name does not hold the game's keeps its name. How many.
 *
 * A path gamelist.xml writes longer than ES_PATH is no path (cut short, it
 * would name another file), and a list whose values hold one another is
 * not written back by es_list_rename. */
int es_files(const char *roms, const char *folder, const char *file, const char *name,
             const char *to, char (*rel)[ES_PATH], char (*rel_to)[ES_PATH], int max);

/* gamelist.xml with the game's entry following it to `new_file`: its <path>,
 * and each media path in it that is in `rel` (es_files) changed to its
 * `rel_to`, the file written whole through a temporary one. True when the
 * list had an entry for the game and was written. */
bool es_list_rename(const char *roms, const char *folder, const char *file, const char *name,
                    const char *new_file, char (*rel)[ES_PATH], char (*rel_to)[ES_PATH],
                    int nrel);

#endif
