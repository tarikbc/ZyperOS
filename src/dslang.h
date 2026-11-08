/* SPDX-License-Identifier: GPL-3.0-only */
/* The language a Nintendo DS game sees, on DSperate (mk/build-dsperate.sh).
 *
 * A DS game reads its language from the firmware's user settings, which the
 * console's own menu wrote, and so it does here: a firmware.bin in Bios/
 * (an encrypted dump needs it) brings the language of the console it was
 * dumped from, whatever the player or the handheld speaks. The firmware.bin
 * ROGUE kept is Spanish, so Mario Kart DS came up in Spanish on a handheld
 * set to Português (Brasil) - and without a firmware.bin DSperate makes one,
 * in English. Neither followed MENU > Language; Diatom answers a core that
 * asks for the frontend's language with English (diatom/src/env.c), and
 * DSperate does not ask.
 *
 * So the launcher says it. DSperate's wrapper has a core option,
 * dsperate_language (mk/patches/dsperate-lr-language.patch), and the launcher
 * sends it with every DS game, as the other opinions it holds for a core
 * (src/platform.c, plat_coreopt): the language MENU > Language chose where
 * the DS has it, and English where it does not - the DS knows Japanese,
 * English, French, German, Italian and Spanish, so Português (Brasil) and
 * 简体中文 (Chinese is the iQue DS's, and no DS game here has it) get English,
 * the language every DS game has. The wrapper changes that one field of its
 * own copy of the firmware and nothing else: the dump on the card and the
 * game's save are not touched.
 *
 * A game resumed from its saved state keeps the language its memory was
 * saved with: the language is copied into the game's memory as it starts, so
 * the in-game menu's Reset (which starts it again, its battery save kept)
 * brings the new one in.
 *
 * An entry for dsperate_language in the library database
 * (coreopt.NDS.dsperate_language) is sent after this one and wins, for
 * whoever wants a different language from the handheld's, or "firmware" for
 * what the dump says.
 *
 * Plain C: tools/dslang-check.c holds the table, and that every value is one
 * the core offers. */
#ifndef ZYPEROS_DSLANG_H
#define ZYPEROS_DSLANG_H

#include "i18n_ids.h"

/* The core option's key, and the system tag it is sent for (systems.cfg) */
#define DSLANG_KEY "dsperate_language"
#define DSLANG_TAG "NDS"

/* The option's value for ZyperOS's language `l`: "japanese", "english",
 * "french", "german", "italian" or "spanish", always one of them (English
 * for a language the DS lacks, and for one that is not a language) */
const char *dslang_value(lang l);

#endif
