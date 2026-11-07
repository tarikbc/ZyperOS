/* SPDX-License-Identifier: GPL-3.0-only */
/* The interface sounds' rule and arithmetic. See src/uisound.h.
 *
 * No SDL, no ALSA, no threads: tools/uisound-check.c links this alone. */
#include "uisound.h"

#include <stdlib.h>
#include <string.h>

/* ---- the setting ---------------------------------------------------------- */

int uisound_setting_parse(const char *id)
{
	int v;

	if (id && !strcmp(id, "off")) return UIS_OFF;
	for (v = 0; v < DS_VOICES; v++)
		if (id && !strcmp(id, DS_VOICE_ID[v])) return v;
	return DS_VOICE_DEFAULT;
}

const char *uisound_setting_id(int s)
{
	return s >= 0 && s < DS_VOICES ? DS_VOICE_ID[s] : "off";
}

const char *uisound_setting_name(int s)
{
	return s >= 0 && s < DS_VOICES ? uisound_voice_name(s) : T(S_MENU_OFF_TITLE);
}

int uisound_setting_step(int s, int dir)
{
	if (s < 0 || s >= UIS_SETTINGS) s = DS_VOICE_DEFAULT;
	return ((s + dir) % UIS_SETTINGS + UIS_SETTINGS) % UIS_SETTINGS;
}

/* ---- which press plays what -------------------------------------------------- */

uis_cue uisound_step(uis_steps *st, bool moved, bool repeat)
{
	uis_cue c = { DS_SOUND_MOVE, 1.0f };

	if (!moved) {
		/* Once for a press. A press against the end is answered every time
		 * - that is a person asking - and a hold only the first time it
		 * arrives there. */
		if (repeat && st->edged) c.level = 0.0f;
		c.sound = DS_SOUND_EDGE;
		st->edged = true;
		return c;
	}
	st->edged = false;
	if (repeat) c.level = DS_SOUND_HELD_LEVEL;
	return c;
}

/* ---- where ------------------------------------------------------------------- */

const char *uisound_device(const aout_state *s)
{
	switch (aout_resolve(s)) {
	case AOUT_BT:  return NULL;
	case AOUT_USB: return AOUT_USB_DEVICE;
	default:       return "default";
	}
}

void uisound_sides(aout_channels chosen, aout_dest d, bool *left, bool *right)
{
	aout_channels c = aout_channels_for(chosen, d);

	*left  = c != AOUT_MONO_RIGHT;
	*right = c != AOUT_MONO_LEFT;
}

/* ---- the files ------------------------------------------------------------------ */

static unsigned le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int16_t *uisound_wav(const uint8_t *buf, size_t n, int *frames)
{
	size_t at = 12;
	bool fmt_ok = false;

	*frames = 0;
	if (!buf || n < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
		return NULL;
	/* Chunk by chunk, each padded to an even length: fmt must say what this
	 * plays, and data comes after it. */
	while (at + 8 <= n) {
		const uint8_t *c = buf + at;
		uint32_t size = le32(c + 4);

		if (size > n - at - 8) return NULL;               /* cut short */
		if (!memcmp(c, "fmt ", 4)) {
			if (size < 16) return NULL;
			fmt_ok = le16(c + 8) == 1 && le16(c + 10) == 1 &&
			         le32(c + 12) == UIS_RATE && le16(c + 22) == 16;
			if (!fmt_ok) return NULL;
		} else if (!memcmp(c, "data", 4)) {
			int16_t *pcm;
			uint32_t i, count = size / 2;

			if (!fmt_ok || !count) return NULL;
			pcm = malloc(count * sizeof *pcm);
			if (!pcm) return NULL;
			for (i = 0; i < count; i++)
				pcm[i] = (int16_t)le16(c + 8 + 2 * i);
			*frames = (int)count;
			return pcm;
		}
		at += 8 + (size_t)size + (size & 1);
	}
	return NULL;
}

/* ---- the mix --------------------------------------------------------------- */

int uisound_mix(uis_voice *v, int nv, int16_t *out, int frames,
                bool left, bool right)
{
	int f, k;

	for (f = 0; f < frames; f++) {
		int32_t sum = 0;

		for (k = 0; k < nv; k++) {
			uis_voice *x = &v[k];
			int32_t s;

			if (x->pos >= x->frames) continue;
			s = (int32_t)x->pcm[x->pos++] * x->gain >> 15;
			sum += s;
		}
		if (sum > 32767) sum = 32767;
		if (sum < -32768) sum = -32768;
		out[2 * f]     = left  ? (int16_t)sum : 0;
		out[2 * f + 1] = right ? (int16_t)sum : 0;
	}
	/* Keep the ones still sounding, in order. */
	for (f = k = 0; k < nv; k++)
		if (v[k].pos < v[k].frames) v[f++] = v[k];
	return f;
}
