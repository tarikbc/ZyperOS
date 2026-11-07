/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* DraStic's Nintendo DS battery saves, taken over by DSperate.
 *
 * DraStic writes Saves/<rom name>.dsv: the save chip's bytes, then the
 * 122-byte footer DeSmuME introduced - "|<--Snip above here to create a raw
 * sav by excluding this DeSmuME savedata footer:", six u32s (sizes and the
 * address width) and "|-DESMUME SAVE-|". ROGUE OS's are the same: read from
 * its card on 2026-11-03, ten of them, 512 B to 256 KB of save each. Diatom
 * keeps a libretro core's battery save as Saves/<rom name>.srm, the chip's
 * bytes alone.
 *
 * So the first time a DS game runs on DSperate with a .dsv and no .srm, the
 * .srm is written from the .dsv with the footer off, and the game goes on from
 * where DraStic left it. Once: after that each emulator keeps its own file,
 * and tools/dsv2srm.py takes a save back the other way.
 *
 * No SDL: tools/dsv-check.c links this and src/atomic.c alone. */
#ifndef ZYPEROS_DSV_H
#define ZYPEROS_DSV_H

#include <stdbool.h>

/* `romfile` is the game's file name (any extension); `save_dir` is Saves/.
 * True when an .srm was written. Never writes over an .srm that is there, and
 * leaves a file without the footer alone. */
bool dsv_import(const char *save_dir, const char *romfile);

#endif
