/* SPDX-License-Identifier: GPL-3.0-only */
/* The zipped Nintendo DS games DSperate unpacked, and how many of them stay.
 *
 * Diatom does not unzip a DS game: the wrapper says block_extract and takes
 * the zip itself, and DSperate maps its ROM from a file rather than holding
 * up to 512 MB of it in RAM. So a deflated one is inflated once to a file
 * beside the zip, <folder>/.dsperate/<zip stem>.nds, with a .nds.tag that
 * says which archive and entry it came from, and kept for the next launch,
 * which then maps it at once instead of inflating it again (DSperate's
 * src/core/cart/zip_cache.h). A zip that stores its ROM is mapped where it
 * lies, and a loose .nds needs nothing.
 *
 * DSperate's own frontend keeps that folder under 2 GB (cart.cache_mb =
 * 2048), the least recently launched image going first. The libretro wrapper
 * never set the cap (NDS::rom_cache_max_bytes, 0: no limit), so every zipped
 * DS game ever played stayed in .dsperate at its whole unpacked size, its
 * card's (8 to 512 MB), however small the zip *(2026-11-23, asked for
 * on Discord: "Decompressed ROMs are piling up inside ZYPEROS/Roms/Nintendo
 * DS/.dsperate")*. Delete Game left its image there too.
 *
 * Nothing else needs the image. A battery save is named after the zip
 * (Saves/<stem>.sav), and a save state knows its ROM by the header and size
 * of the unpacked bytes (DSperate's NDS::rom_identity), which an image
 * unpacked again has too: an image that is gone costs the next launch its
 * unpacking, seconds for a large game, and nothing of the player's.
 *
 * So the core holds its own frontend's 2 GB now as it unpacks a game
 * (mk/patches/dsperate-lr-zip-cache.patch), and the launcher holds the same
 * rule after each DS game: DSC_CAP per .dsperate folder, by DSperate's own
 * count (each image's size) and order (its tag's time, which DSperate
 * touches at every launch), never the game just played. The two agree, and
 * a card a core without the patch filled comes back under the cap after its
 * next DS game. src/gamedel.c takes a game's image with it, src/gamern.c
 * moves it and points its tag at the new zip, and MENU > Storage counts it
 * as its game's and deletes them all as Unpacked DS Games (src/storage.h).
 *
 * Plain C, no SDL: tools/dscache-check.c holds it. */
#ifndef ZYPEROS_DSCACHE_H
#define ZYPEROS_DSCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* DSperate's folder, beside the zips it unpacked */
#define DSC_DIR ".dsperate"
/* What one such folder keeps: DSperate's own cart.cache_mb */
#define DSC_CAP (2048ull << 20)

/* The folder DSperate unpacks `file` into, a launch path under `romdir`
 * ("Game.zip", or "Sub/Game.zip" in a folder of games): romdir/.dsperate or
 * romdir/Sub/.dsperate. False for a launch path that is not a .zip or a path
 * too long. As DSperate's choose_cache: the archive's own folder. */
static inline bool dsc_dir(const char *romdir, const char *file, char *out, size_t n)
{
	const char *slash = strrchr(file, '/');
	const char *base = slash ? slash + 1 : file;
	const char *dot = strrchr(base, '.');
	int w;

	if (!dot || dot == base || strcasecmp(dot, ".zip")) return false;
	w = slash ? snprintf(out, n, "%s/%.*s/" DSC_DIR, romdir, (int)(slash - file), file)
	          : snprintf(out, n, "%s/" DSC_DIR, romdir);
	return w > 0 && (size_t)w < n;
}

/* The image DSperate unpacks `file` to: <its folder>/.dsperate/<stem>.nds,
 * the stem being the zip's name without its extension (DSperate's stem_of).
 * Its tag is this path and ".tag", the file it writes first ".part". */
static inline bool dsc_image(const char *romdir, const char *file, char *out, size_t n)
{
	char dir[1024];
	const char *slash = strrchr(file, '/');
	const char *base = slash ? slash + 1 : file;
	const char *dot = strrchr(base, '.');
	int w;

	if (!dsc_dir(romdir, file, dir, sizeof dir)) return false;
	w = snprintf(out, n, "%s/%.*s.nds", dir, (int)(dot - base), base);
	return w > 0 && (size_t)w < n;
}

/* Keep the images in `dir` (a .dsperate folder) under `cap` bytes: the least
 * recently launched go first, each with its tag, as DSperate's enforce_cap
 * has it, and `keep` (an image path, may be NULL) never. An image is its
 * file's size; its time is its tag's, or its own when it has no tag. How
 * many bytes went; `removed`, if not NULL, how many images. A folder that is
 * not there is nothing to do. */
unsigned long long dsc_trim(const char *dir, unsigned long long cap, const char *keep,
                            int *removed);

#endif
