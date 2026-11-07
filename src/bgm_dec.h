/* SPDX-License-Identifier: GPL-3.0-only */
/* A menu music track, decoded a slice at a time: MP3 through minimp3, Ogg
 * Vorbis through stb_vorbis, both vendored in src/third_party/. Always
 * 16-bit stereo out, a mono track on both sides. See src/bgm.h. */
#ifndef ZYPEROS_BGM_DEC_H
#define ZYPEROS_BGM_DEC_H

#include <stdint.h>

#include "bgm_tags.h"

typedef struct bgm_dec bgm_dec;

/* NULL for a file that will not open or holds no audio either can read. The
 * first frame is decoded here, so the rate is known before anything plays.
 * `tags`, when not NULL, gets the track's title, artist and album
 * (src/bgm_tags.h), read on the way in: an MP3's ID3v2 tag is where its
 * audio starts anyway, and an Ogg's comments are in its first pages. */
bgm_dec *bgm_dec_open(const char *path, bgm_tags *tags);
int      bgm_dec_rate(const bgm_dec *d);
/* Up to `frames` stereo frames into pcm. 0 at the end of the track. */
int      bgm_dec_read(bgm_dec *d, int16_t *pcm, int frames);
void     bgm_dec_close(bgm_dec *d);

/* stb_vorbis behind three calls, in src/bgm_ogg.c: minimp3 and it cannot share
 * a file. For bgm_dec_open, not for anyone else. */
typedef struct bgm_ogg bgm_ogg;
bgm_ogg *bgm_ogg_open(const char *path, int *rate, bgm_tags *tags);
int      bgm_ogg_read(bgm_ogg *o, int16_t *pcm, int frames);
void     bgm_ogg_close(bgm_ogg *o);

#endif
