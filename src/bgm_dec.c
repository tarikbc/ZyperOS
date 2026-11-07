/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Menu music's decoders. See src/bgm_dec.h.
 *
 * The two decoders are other people's code, each built in one file and
 * nowhere else - minimp3 here, stb_vorbis in src/bgm_ogg.c, apart because
 * both name a function get_bits - with the warnings this project turns on
 * turned off for them alone: they are copied unchanged
 * (src/third_party/README.md), and a warning in them is not ours to fix. */
#include "bgm_dec.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"

/* MP3 only: layers I and II are not menu music */
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

#pragma GCC diagnostic pop

/* Bytes of an MP3 held at once, and the least left before more is read:
 * minimp3 looks for several frames in a row before it trusts a sync, and a
 * frame at 320 kb/s is about 1.4 KB. */
#define MP3_BUF  (32 * 1024)
#define MP3_KEEP (16 * 1024)

struct bgm_dec {
	bool          ogg;
	int           rate;
	/* MP3 */
	FILE         *f;
	mp3dec_t      mp3;
	uint8_t       buf[MP3_BUF];
	size_t        have, pos;
	bool          eof;
	int16_t       pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	int           frames, at, ch;   /* the decoded frame, how far in, its channels */
	/* Gapless, from the encoder's own note (mp3_gapless): samples still to
	 * drop at the start, and the samples the track really has, counted down
	 * (-1 for a file with no note: everything is played) */
	bool          looked;
	int           skip;
	long long     left;
	/* Ogg Vorbis */
	bgm_ogg      *v;
};

static void mp3_fill(bgm_dec *d)
{
	size_t got;

	if (d->eof || d->have - d->pos >= MP3_KEEP) return;
	memmove(d->buf, d->buf + d->pos, d->have - d->pos);
	d->have -= d->pos;
	d->pos = 0;
	got = fread(d->buf + d->have, 1, MP3_BUF - d->have, d->f);
	d->have += got;
	if (got == 0) d->eof = true;
}

/* An MP3 is not gapless by itself: the encoder starts with its own delay and
 * pads the last frame, and LAME's first frame is no audio at all but a note
 * (Xing/Info) of how many frames follow, with the delay and the padding in
 * its LAME extension. Played as frames, a track had about 47 ms of silence
 * before it and up to 24 ms after, and a loop (Bossa Nova, The Arplands)
 * stopped for that long each time round, where Ogg Vorbis plays gapless.
 *
 * `f` is one whole frame. True when it is that note, with the samples to
 * drop at the start (the encoder's delay and the decoder's own 529, as
 * minimp3_ex counts them) and the samples the track holds. */
static bool mp3_gapless(const uint8_t *f, int n, int *skip, long long *total)
{
	bool mpeg1 = f[1] & 0x08, mono = (f[3] >> 6) == 3, crc = !(f[1] & 0x01);
	int at = 4 + (crc ? 2 : 0) + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
	unsigned flags, frames = 0, delay, padding;
	const uint8_t *x = f + at, *lame;

	if (at + 8 > n || (memcmp(x, "Xing", 4) && memcmp(x, "Info", 4))) return false;
	flags = (unsigned)x[4] << 24 | (unsigned)x[5] << 16 | (unsigned)x[6] << 8 | x[7];
	x += 8;
	if (flags & 1) {
		if (x + 4 > f + n) return false;
		frames = (unsigned)x[0] << 24 | (unsigned)x[1] << 16 | (unsigned)x[2] << 8 | x[3];
		x += 4;
	}
	if (flags & 2) x += 4;          /* bytes */
	if (flags & 4) x += 100;        /* the seek table */
	if (flags & 8) x += 4;          /* quality */
	lame = x;
	/* The extension: 9 bytes of encoder name, 12 more, then 12 bits each */
	if (!frames || lame + 24 > f + n || memcmp(lame, "LAME", 4)) return false;
	delay = (unsigned)lame[21] << 4 | lame[22] >> 4;
	padding = (unsigned)(lame[22] & 0x0f) << 8 | lame[23];
	*skip = (int)delay + 529;
	*total = (long long)frames * (mpeg1 ? 1152 : 576) - delay - padding;
	return *total > 0;
}

/* The next frame into d->pcm: how many samples per channel, 0 at the end */
static int mp3_frame(bgm_dec *d)
{
	for (;;) {
		mp3dec_frame_info_t info;
		int s;

		mp3_fill(d);
		if (d->pos >= d->have) return 0;
		s = mp3dec_decode_frame(&d->mp3, d->buf + d->pos, (int)(d->have - d->pos),
		                        d->pcm, &info);
		if (info.frame_bytes == 0) {
			/* Nothing that syncs in all that is held: past it, or done */
			if (d->eof) return 0;
			d->pos = d->have;
			continue;
		}
		if (!d->looked && s > 0) {
			/* The first frame: LAME's note, which is no audio */
			int skip;
			long long total;

			d->looked = true;
			if (mp3_gapless(d->buf + d->pos + info.frame_offset,
			                info.frame_bytes - info.frame_offset, &skip, &total)) {
				d->pos += (size_t)info.frame_bytes;
				d->rate = info.hz;
				d->skip = skip;
				d->left = total;
				continue;
			}
		}
		d->pos += (size_t)info.frame_bytes;
		if (s > 0) {
			if (!d->rate) d->rate = info.hz;
			d->frames = s;
			d->ch = info.channels;
			d->at = 0;
			/* The encoder's delay, dropped as it comes */
			if (d->skip > 0) {
				d->at = d->skip < s ? d->skip : s;
				d->skip -= d->at;
				if (d->at >= s) continue;
			}
			return s;
		}
	}
}

bgm_dec *bgm_dec_open(const char *path, bgm_tags *tags)
{
	const char *dot = strrchr(path, '.');
	bgm_dec *d = calloc(1, sizeof *d);
	bgm_tags none;

	if (!tags) tags = &none;
	memset(tags, 0, sizeof *tags);
	if (!d) return NULL;
	if (dot && !strcasecmp(dot, ".ogg")) {
		d->ogg = true;
		d->v = bgm_ogg_open(path, &d->rate, tags);
		if (!d->v) { free(d); return NULL; }
		if (d->rate <= 0) { bgm_dec_close(d); return NULL; }
		return d;
	}
	if (!(d->f = fopen(path, "rb"))) { free(d); return NULL; }
	/* Past an ID3v2 tag, its words read on the way: its size is in its
	 * header, and a cover picture in it is hundreds of kilobytes for minimp3
	 * to search for a sync in. A file with no title there may have the
	 * older tag at its end. */
	bgm_id3v2(d->f, tags);
	if (!tags->title[0]) bgm_id3v1(d->f, tags);
	mp3dec_init(&d->mp3);
	d->left = -1;
	if (mp3_frame(d) <= 0 || d->rate <= 0) { bgm_dec_close(d); return NULL; }
	return d;
}

int bgm_dec_rate(const bgm_dec *d)
{
	return d->rate;
}

int bgm_dec_read(bgm_dec *d, int16_t *pcm, int frames)
{
	int done = 0;

	if (d->ogg)
		return bgm_ogg_read(d->v, pcm, frames);
	while (done < frames) {
		int k, take;

		if (!d->left) break;             /* the padding: not the track's */
		if (d->at >= d->frames && mp3_frame(d) <= 0) break;
		take = d->frames - d->at;
		if (take > frames - done) take = frames - done;
		if (d->left > 0 && take > d->left) take = (int)d->left;
		for (k = 0; k < take; k++) {
			int i = d->at + k;

			if (d->ch == 2) {
				pcm[2 * (done + k)]     = d->pcm[2 * i];
				pcm[2 * (done + k) + 1] = d->pcm[2 * i + 1];
			} else {
				pcm[2 * (done + k)] = pcm[2 * (done + k) + 1] = d->pcm[i];
			}
		}
		d->at += take;
		done += take;
		if (d->left > 0) d->left -= take;
	}
	return done;
}

void bgm_dec_close(bgm_dec *d)
{
	if (!d) return;
	if (d->v) bgm_ogg_close(d->v);
	if (d->f) fclose(d->f);
	free(d);
}
