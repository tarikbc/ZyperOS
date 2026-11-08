/* SPDX-License-Identifier: GPL-3.0-only */
/* Whether a Nintendo DS game resumes from its auto state, or starts fresh
 * because Game Language changed since the state was made.
 *
 * A DS game picks its language as it boots, from the user settings the
 * firmware copies into its memory (src/dslang.h), and a save state carries
 * that memory. So a game resumed from a state made in Spanish stays in
 * Spanish whatever Game Language says now: Mario Kart DS did, on the Pixel 2
 * on 2026-11-08, the state made before the row existed, when the game took
 * the language of the Spanish firmware.bin on the card. Reset brought the new
 * language in (mk/patches/dsperate-lr-language.patch sets it at every
 * reset), but the auto state is ZyperOS's own resume, not something the
 * player chose: the launcher starts the game fresh in its place, the battery
 * save (.srm) keeping the progress, and says so over the game.
 *
 * Which language a state was made in is kept beside it, <game>.auto.lang next
 * to <game>.auto.state: one word, the language the game ran in when its auto
 * state was written (src/main.c writes it as the game ends). A state from
 * before that file existed was made in the language of the card's
 * firmware.bin, which is what every DS game started in until the Game
 * Language row (2026-11-07), and English on a card with none, the language of
 * the firmware DSperate makes then. That guess is wrong only for a state made
 * in the day between the row and this file, from a fresh start in another
 * language: that game starts fresh once, and nothing is lost but where it
 * stood.
 *
 * The six manual slots are not held to it: a state the player loads is
 * loaded as asked, in its own language, and Reset brings the row's.
 *
 * Plain C, no SDL: tools/dsresume-check.c holds it. */
#ifndef ZYPEROS_DSRESUME_H
#define ZYPEROS_DSRESUME_H

#include <stdbool.h>
#include <stddef.h>

/* The language a DS firmware image gives a game, as DSperate's direct boot
 * copies its user settings (of the two copies, the second only when its
 * counter is the first's plus one): "japanese", "english", "french",
 * "german", "italian", "spanish" or "chinese". NULL for a file that is not
 * there, is smaller than a DS firmware's 256 KB, or has no user settings in
 * it. */
const char *dsr_firmware_language(const char *firmware_path);

/* The language a game started now runs in: `option`, the dsperate_language
 * sent with it, when that is a language; otherwise ("firmware", or a word
 * the core does not know) the firmware.bin's own, or English when there is
 * no firmware.bin. NULL when it cannot be known: a firmware.bin with no
 * user settings in it. */
const char *dsr_effective(const char *option, const char *firmware_path);

/* The file beside a state that says its language: <x>.state is <x>.lang */
void dsr_lang_path(const char *state_path, char *out, size_t n);

/* The language the state at `state_path` was made in: its .lang's word, or,
 * for a state from before .lang files, the firmware.bin's own (English with
 * none). Into `buf`, which is returned; NULL when it is not known. */
const char *dsr_recorded(const char *state_path, const char *firmware_path,
                         char *buf, size_t n);

/* Start fresh instead of resuming: both languages are known and they
 * differ. */
bool dsr_start_fresh(const char *recorded, const char *effective);

/* Write `lang` as the language of the state at `state_path`, whole or not
 * at all. False when it could not be written. */
bool dsr_record(const char *state_path, const char *lang);

#endif
