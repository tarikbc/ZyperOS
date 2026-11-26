/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_EQ_H
#define ZYPEROS_EQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audioout.h"
#include "eqdsp.h"

/* The equalizer's settings: MENU > Sound > Equalizer, and the same row in
 * MP4's MENU (DESIGN.md 11.60). My picks in the Sound lab, 2026-11-19: one
 * preset for the speaker and one for headphones (wired, USB-C or
 * Bluetooth), each Off, Bass, Voice, Treble or Custom, and five bands of
 * your own; Off on a new card, and Off is no filter at all. Everything that
 * makes sound follows it: MP4, the menu music, games (Diatom) and ports
 * (plastron's zyperos-eq plugin, where the system has it). The arithmetic is
 * src/eqdsp.h's.
 *
 * Kept in the device's database:
 *
 *     eq.speaker, eq.phones              the preset's word; absent is Off
 *     eq.speaker.bands, eq.phones.bands  Custom's five bands, "2,0,-1,0,3"
 *
 * Pure but for the database (src/db.h) and the shared file: no SDL. */

typedef enum { EQ_OFF, EQ_BASS, EQ_VOICE, EQ_TREBLE, EQ_CUSTOM, EQ_PRESETS } eq_preset;
typedef enum { EQ_OUT_SPEAKER, EQ_OUT_PHONES, EQ_OUTS } eq_out;

/* The ready-made curves, dB a band (the lab's): Off is all 0, Custom is the
 * player's own */
extern const int8_t EQ_CURVE[EQ_PRESETS][EQ_BANDS];

/* The output a sound going to `d` is equalized for: the speaker, or
 * headphones (on the jack, a USB-C adapter or a Bluetooth headset) */
eq_out eq_out_of(aout_dest d);

/* The words the database keeps: "off", "bass", "voice", "treble", "custom" */
const char *eq_preset_id(eq_preset p);
/* `id` as a preset; `fallback` for NULL or a word this release cannot read */
eq_preset eq_preset_parse(const char *id, eq_preset fallback);
/* In the language that is on: Off, Bass, Voice, Treble, Custom */
const char *eq_preset_name(eq_preset p);
/* What it does, for its list's note */
const char *eq_preset_note(eq_preset p);
/* The output's name for the page's first row: Speaker or Headphones */
const char *eq_out_name(eq_out o);
/* A band's name, "120 Hz", "1 kHz", into `out` */
void eq_band_name(int band, char *out, size_t n);

/* The output's preset, and its Custom bands */
eq_preset eq_preset_get(eq_out o);
void      eq_preset_set(eq_out o, eq_preset p);
void      eq_custom_get(eq_out o, int8_t db[EQ_BANDS]);
void      eq_custom_set(eq_out o, const int8_t db[EQ_BANDS]);
/* The bands in force for the output: its preset's curve, Custom's own, or
 * all 0 for Off */
void      eq_bands(eq_out o, int8_t db[EQ_BANDS]);
/* Whether `db` is all 0: nothing to filter */
bool      eq_flat(const int8_t db[EQ_BANDS]);
/* "5,2,0,0,1", as SETEQ and MP4's EQ carry them, into `out` */
void      eq_bands_wire(const int8_t db[EQ_BANDS], char *out, size_t n);
/* And back: false, `db` untouched, for anything but five whole numbers */
bool      eq_bands_parse(const char *s, int8_t db[EQ_BANDS]);

/* Band `band` of the output stepped by `dir` from what is in force, kept as
 * Custom (a preset's curve becomes Custom's, with the step): false when it
 * is at its end already */
bool      eq_band_step(eq_out o, int band, int dir);

/* The bands for the output a port's sound goes to, into the shared file a
 * port's plugin reads (EQ_SHARE_PATH, $ZYPEROS_EQ_FILE): written only on a
 * change, in place, with the sequence number a reader checks. False when
 * the file could not be written. */
bool      eq_share_write(const int8_t db[EQ_BANDS]);

/* The volume boost's gain, dB (src/boost.h), into the same file, for a port
 * started later: the plugin applies it as MP4 and Diatom do theirs. */
bool      eq_share_gain(int db);
/* And read back, as a port's plugin reads it: the gain portkeys writes while
 * a port runs and the launcher is gone. -1 while there is no whole file, or
 * while it is being written. MP4's follower beside your PICO-8 passes it on
 * to MP4, which hears the boost only from its socket. */
int       eq_share_gain_get(void);
/* Your PICO-8's quiet while MP4 plays (DESIGN.md 11.55): the shared file's
 * cut, which the port's plugin plays silence for (eqdsp.h). Written by MP4's
 * follower beside PICO-8 and by the in-game menu over it, and put back to 0
 * by the launcher as it starts and as it hands over to any other program.
 * False when the file could not be had. */
bool      eq_share_cut(bool cut);

#endif
