/* SPDX-License-Identifier: GPL-3.0-only */
/* A menu music track's name, for Zyper's pop-up that says it on the shelf
 * (DESIGN.md 11.22; 2026-11-06: "whenever the bg song changes, can we
 * have zyper popup and say the new song name?").
 *
 * From the file's own tags where it has them - an MP3's ID3v2 frames, or the
 * ID3v1 block at its end, and an Ogg's Vorbis comments - and from its file
 * name where it has none. ROGUE's 330 tracks are game soundtrack rips named
 * by a code ("FC001.mp3", which said nothing in MENU > Menu Music's Playing
 * row), with the game and the track in their tags; ZyperOS's own ten in
 * res/bgm carry their title and artist in their ID3v2 tags.
 *
 * Plain C and stdio, no SDL: src/bgm_dec.c reads the tags as it opens a
 * track, on the player's thread, and tools/bgm-check.c holds all of this on
 * a build machine. */
#ifndef ZYPEROS_BGM_TAGS_H
#define ZYPEROS_BGM_TAGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "i18n.h"

/* Bytes of a field kept, its terminator included. A longer one is cut on a
 * character's edge, never through one. */
#define BGM_TAG_MAX 128

typedef struct {
	char title[BGM_TAG_MAX];
	char artist[BGM_TAG_MAX];
	char album[BGM_TAG_MAX];
} bgm_tags;

/* An ID3v2 tag (2.2, 2.3 or 2.4) at the start of `f`: the title, artist and
 * album read into `t` as UTF-8, whatever the frames' own encoding (Latin-1,
 * UTF-16 or UTF-8). Every other frame - a cover picture is hundreds of
 * kilobytes - is passed over by its size and never read. Leaves `f` just past
 * the tag, where the audio starts, or at the start when there is none; that
 * is how the MP3 decoder is spared searching a picture for a sync. True when
 * there was a tag. A tag whose whole body is unsynchronised (2.3's flag) is
 * passed over without reading: its frame sizes do not say where they end. */
bool bgm_id3v2(FILE *f, bgm_tags *t);

/* The ID3v1 block in the file's last 128 bytes, into whichever of `t`'s
 * fields are still empty. Leaves the position where it was. */
void bgm_id3v1(FILE *f, bgm_tags *t);

/* Vorbis comments, "KEY=value" each with the key in any case, as stb_vorbis
 * hands them over: TITLE, ARTIST (ALBUMARTIST when there is no ARTIST) and
 * ALBUM, the first of each. */
void bgm_vorbis_tags(char *const *list, int n, bgm_tags *t);

/* What the pop-up says. `head`, the small line, is where the song is from:
 * the album, which for a soundtrack rip is the game, else the artist, else
 * "Now playing". `body`, the large line, is the title, else the file's name
 * tidied (bgm_tidy_name). `t` may be NULL, for a track with no tags. */
#define BGM_SONG_HEAD_NONE T(S_BGM_NOW_PLAYING)
void bgm_song_label(const bgm_tags *t, const char *file,
                    char *head, size_t hn, char *body, size_t bn);

/* A file's name as a song's: without its extension, underscores as spaces, a
 * leading track number off ("01 - ", "07. ", "1-01 ", "3_"), and runs of
 * spaces as one. A single digit followed by a space stays, as in "7 Seas".
 * The name without its extension, as it is, when that would leave nothing. */
void bgm_tidy_name(const char *file, char *out, size_t n);

#endif
