/* SPDX-License-Identifier: GPL-3.0-only */
/* The interface sounds, DESIGN.md 9: six short sounds in three voices, played
 * by the launcher on a press and never over a game.
 *
 * Two halves. src/uisound.c is the rule and the arithmetic - which press plays
 * what, where it may be heard, the WAV files, the mix - with no SDL, no ALSA
 * and no threads, so tools/uisound-check.c holds it to DESIGN.md on a build
 * machine. src/uisound_out.c is the player: it reads the files, keeps the
 * output, and mixes into it from a thread of its own, so a press never waits
 * on the sound it starts. The same split as src/audioout.c and main.c.
 *
 * The sounds are res/sounds/<voice>/<sound>.wav, which tools/gensounds.py
 * synthesizes from tools/designdef.py's tones: 48 kHz, the rate the system's
 * mixed output runs at, 16-bit, one channel. */
#ifndef ZYPEROS_UISOUND_H
#define ZYPEROS_UISOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audioout.h"
#include "design.h"
#include "i18n.h"

#define UIS_RATE 48000

/* ---- the setting ---------------------------------------------------------
 *
 * MENU > UI Sounds: a voice, or off. Four positions, the voices in
 * DESIGN.md's order and then Off, stored as the voice's id or "off". */
#define UIS_OFF ((int)DS_VOICES)
#define UIS_SETTINGS (UIS_OFF + 1)

/* Anything that is not a voice's id or "off" is the default voice: a setting
 * that outlives a voice falls back to sound rather than to silence. */
int         uisound_setting_parse(const char *id);
const char *uisound_setting_id(int s);     /* "wood" .. "off", for the database */
const char *uisound_setting_name(int s);   /* "Wood" .. "Off", for the row */

/* A voice's name for the row, in the language that is on. design.h's
 * DS_VOICE_NAME is the English one; past the voices, the default's. */
_Static_assert(DS_VOICES == 3, "a voice's name in res/strings/sounds.txt for each");
static inline const char *uisound_voice_name(int v)
{
	static const str_id name[DS_VOICES] = {
		[DS_VOICE_WOOD] = S_SOUNDS_WOOD, [DS_VOICE_GLASS] = S_SOUNDS_GLASS,
		[DS_VOICE_BUBBLE] = S_SOUNDS_BUBBLE,
	};

	return T(name[v >= 0 && v < DS_VOICES ? v : DS_VOICE_DEFAULT]);
}
int         uisound_setting_step(int s, int dir);   /* round the four */

/* ---- which press plays what -----------------------------------------------
 *
 * A sound and the level to play it at, of its own: 1 is the sound as it was
 * made, 0 is nothing at all. */
typedef struct { ds_sound sound; float level; } uis_cue;

/* What one list or shelf remembers between steps: whether the last one was
 * held against the end. One per screen, put down when the screen opens. */
typedef struct { bool edged; } uis_steps;

/* Every move, including a wrap, plays move. A blocked step plays edge once
 * per press. Repeats held against the same blocked edge are silent.
 * A moving repeat plays at DS_SOUND_HELD_LEVEL. */
uis_cue uisound_step(uis_steps *st, bool moved, bool repeat);

/* ---- where ------------------------------------------------------------------
 *
 * The device the sounds go to, given where the system's sound goes: the
 * codec's "default" for the speaker and the jack, the DAC's mixed device, and
 * NULL - nowhere - on a Bluetooth headset, whose A2DP stream is one player's
 * at a time and whose delay would put every tick after its press. */
const char *uisound_device(const aout_state *s);

/* Which sides carry them, for Audio Channels: mono on one side plays them on
 * that side only, where a side means anything (aout_channels_for). */
void uisound_sides(aout_channels chosen, aout_dest d, bool *left, bool *right);

/* ---- the files ----------------------------------------------------------------
 *
 * The samples of a WAV in memory: RIFF, PCM, one channel, 16-bit, UIS_RATE,
 * which is all tools/gensounds.py writes. Anything else, or a file cut short,
 * is NULL. The samples are malloc'd; *frames says how many. */
int16_t *uisound_wav(const uint8_t *buf, size_t n, int *frames);

/* ---- the mix --------------------------------------------------------------- */

/* A sound playing: its samples, how far in, and its level in 1/32768ths. */
typedef struct {
	const int16_t *pcm;
	int frames, pos;
	int gain;
} uis_voice;

/* Mix the voices into `frames` stereo frames of `out`, summing and holding to
 * the 16-bit range, on the sides asked for and silence on the other. Voices
 * that end are taken out of `v`; returns how many are left. */
int uisound_mix(uis_voice *v, int nv, int16_t *out, int frames,
                bool left, bool right);

/* ---- the player (src/uisound_out.c) -------------------------------------------
 *
 * Everything below may be called from the main thread at any time and returns
 * at once: the reading, the opening and the writing are the player's own
 * thread's. Before uisound_init, or with no files, they do nothing. */

/* Start the player, reading the voices from `dir` (res/sounds) on its own
 * thread. Nothing is opened until the first sound. */
void uisound_init(const char *dir);
void uisound_quit(void);

/* The setting: which voice plays, or UIS_OFF. Off lets what is playing end. */
void uisound_set(int setting);
int  uisound_setting(void);

void uisound_play(ds_sound s);
void uisound_cue(uis_cue c);

/* Where to play: a device from uisound_device, or NULL for nowhere, and the
 * sides. Nowhere until the first call, so nothing plays before it. Cheap when
 * nothing changed, so it can be said every frame. */
void uisound_route(const char *device, bool left, bool right);

/* A game is about to have the device: let the output go as soon as what is
 * playing ends, rather than after the usual quiet spell. Returns at once - a
 * launch waits for nothing here - and the next sound opens it again. */
void uisound_release(void);

#endif
