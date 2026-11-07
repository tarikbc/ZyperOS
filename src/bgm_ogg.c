/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Ogg Vorbis for menu music: stb_vorbis, vendored in src/third_party/, behind
 * the three calls src/bgm_dec.h names. A file of its own because minimp3,
 * in src/bgm_dec.c, has a function of the same name as one of its own. */
#include "bgm_dec.h"
#include "fileio.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Decoding a file to 16-bit is all this asks of it */
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"

#pragma GCC diagnostic pop

/* The file, read whole, and the decoder over it.
 *
 * Not stb_vorbis_open_filename: that reads the stream a byte at a time with
 * fgetc, and in a program with threads every fgetc takes the stream's lock.
 * Measured on the Pixel 2 (2026-11-05): an Ogg track cost the player 12% of a
 * core against 5.6% for an MP3, where decoding alone, in a program with one
 * thread, was 2.0% and 0.9%. From memory there is no lock to take, and the
 * card is read once, in one go, rather than all through the track. ROGUE's
 * largest Ogg is 5.2 MB. */
#define OGG_MAX (32 * 1024 * 1024)

struct bgm_ogg {
	stb_vorbis    *v;
	unsigned char *data;
};

bgm_ogg *bgm_ogg_open(const char *path, int *rate, bgm_tags *tags)
{
	struct bgm_ogg *o;
	unsigned char *data;
	size_t n;
	int err = 0;

	*rate = 0;
	if (!(data = (unsigned char *)file_read(path, OGG_MAX + 1L, &n))) return NULL;
	if (!(o = calloc(1, sizeof *o))) {
		free(data);
		return NULL;
	}
	o->data = data;
	if (!(o->v = stb_vorbis_open_memory(o->data, (int)n, &err, NULL))) {
		free(o->data);
		free(o);
		return NULL;
	}
	*rate = (int)stb_vorbis_get_info(o->v).sample_rate;
	/* Its comments, which stb_vorbis read with the headers */
	{
		stb_vorbis_comment c = stb_vorbis_get_comment(o->v);

		bgm_vorbis_tags(c.comment_list, c.comment_list_length, tags);
	}
	return o;
}

/* Stereo whatever the file: stb_vorbis puts a mono track on both sides */
int bgm_ogg_read(bgm_ogg *o, int16_t *pcm, int frames)
{
	return stb_vorbis_get_samples_short_interleaved(o->v, 2, pcm, frames * 2);
}

void bgm_ogg_close(bgm_ogg *o)
{
	if (!o) return;
	stb_vorbis_close(o->v);
	free(o->data);
	free(o);
}
