/* SPDX-License-Identifier: GPL-3.0-only */
/* The Cover and Video page's files on the card: where the cover, Before's
 * and the video are, Use as Cover and Put Back, and a video's length. Apart
 * from src/mediafetch.c, which talks to the sources, so tools/media-check.c
 * can hold them to a card of small files with no network, no SDL. The
 * declarations are in src/mediafetch.h. */
#include "mediafetch.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MF_FILE_PATH 1600

bool mf_media_path(const char *roms, const char *folder, const char *stem,
                   const char *suffix, char *out, int n)
{
	int len = snprintf(out, (size_t)n, "%s/%s/.media/%s%s", roms, folder, stem, suffix);

	return len > 0 && len < n;
}

bool mf_before_path(const char *roms, const char *folder, const char *stem, char *out, int n)
{
	int len = snprintf(out, (size_t)n, "%s/%s/.media/.before/%s.png", roms, folder, stem);

	return len > 0 && len < n;
}

static bool copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb"), *out;
	char buf[16384];
	size_t n;
	bool ok = true;

	if (!in) return false;
	if (!(out = fopen(to, "wb"))) { fclose(in); return false; }
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	if (ferror(in)) ok = false;
	fclose(in);
	if (fclose(out) != 0) ok = false;
	if (!ok) unlink(to);
	return ok;
}

/* .media and .media/.before, made where they are not */
static void media_dirs(const char *roms, const char *folder)
{
	char p[MF_FILE_PATH];

	if (snprintf(p, sizeof p, "%s/%s/.media", roms, folder) < (int)sizeof p) mkdir(p, 0777);
	if (snprintf(p, sizeof p, "%s/%s/.media/.before", roms, folder) < (int)sizeof p) mkdir(p, 0777);
}

/* THE PICTURE IS COPIED IN BESIDE THE COVER, THEN RENAMED OVER IT, as a
 * fetch is (net.c's .part): the files it comes from are in RAM, another
 * filesystem, and a power cut during the copy leaves the cover there was.
 * The cover there was goes to .before first, by a rename, which a power cut
 * cannot leave half done. */
bool mf_file_use(const char *roms, const char *folder, const char *stem, const char *from,
                 const art_source *as, char *dest, int destn)
{
	char pick[MF_FILE_PATH], before[MF_FILE_PATH];
	struct stat st;
	bool had;

	if (!from || !mf_media_path(roms, folder, stem, ".png", dest, destn) ||
	    !mf_media_path(roms, folder, stem, ".png.pick", pick, sizeof pick) ||
	    !mf_before_path(roms, folder, stem, before, sizeof before))
		return false;
	media_dirs(roms, folder);
	if (!copy_file(from, pick)) return false;
	had = stat(dest, &st) == 0 && S_ISREG(st.st_mode);
	if (had && rename(dest, before) != 0) { unlink(pick); return false; }
	if (rename(pick, dest) != 0) {
		if (had) rename(before, dest);
		unlink(pick);
		return false;
	}
	/* The records go as the files went: the shelf's to Before's */
	if (had) artsrc_swap(folder, stem);
	artsrc_set(folder, stem, as);
	return true;
}

bool mf_file_put_back(const char *roms, const char *folder, const char *stem)
{
	char dest[MF_FILE_PATH], before[MF_FILE_PATH], swap[MF_FILE_PATH];
	struct stat st;

	if (!mf_media_path(roms, folder, stem, ".png", dest, sizeof dest) ||
	    !mf_before_path(roms, folder, stem, before, sizeof before) ||
	    !mf_media_path(roms, folder, stem, ".png.swap", swap, sizeof swap))
		return false;
	if (stat(before, &st) != 0) return false;
	if (stat(dest, &st) == 0) {
		if (rename(dest, swap) != 0) return false;
		if (rename(before, dest) != 0) { rename(swap, dest); return false; }
		rename(swap, before);
	} else if (rename(before, dest) != 0) {
		return false;
	}
	artsrc_swap(folder, stem);
	return true;
}

/* ---- no way out ------------------------------------------------------------ */

bool mf_unreachable(const mf_state *st)
{
	int i, asked = 0;

	if (!st) return false;
	if (st->offline) return true;
	if (st->n > 0 || st->video_offer) return false;
	for (i = 0; i < AS_SOURCES; i++) {
		if (st->q[i] == MFQ_WAIT || st->q[i] == MFQ_ASKING) return false;
		if (st->q[i] == MFQ_OFF) continue;
		if (st->q[i] != MFQ_NO_ANSWER) return false;
		asked++;
	}
	return asked > 0;
}

/* ---- an MP4's length ------------------------------------------------------- */

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The most boxes walked at one level: a real file has a handful */
#define MF_MAX_BOXES 4096

/* The box of `type` among those from `at` to `end`: its body's offset and
 * length; false when there is none.
 *
 * THE FILE IS NOT TRUSTED. A video snap is whatever a server sent, so every
 * size it states is held to the room there is: a box past LONG_MAX, shorter
 * than its own header, or running past `end` (the parent's end, or the
 * file's) ends the walk, as does one that would not move it forward, and no
 * more than MF_MAX_BOXES are looked at. */
static bool find_box(FILE *f, long at, long end, const char *type, long *body, long *len)
{
	unsigned char h[16];
	int walked;

	for (walked = 0; walked < MF_MAX_BOXES && at >= 0 && end - at >= 8; walked++) {
		uint64_t size;
		long hl = 8, next;

		if (fseek(f, at, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) return false;
		size = be32(h);
		if (size == 1) {
			if (end - at < 16 || fread(h + 8, 1, 8, f) != 8) return false;
			size = (uint64_t)be32(h + 8) << 32 | be32(h + 12);
			hl = 16;
		} else if (size == 0) {
			size = (uint64_t)(end - at);       /* to the end of its parent */
		}
		if (size > (uint64_t)LONG_MAX || size < (uint64_t)hl) return false;
		if ((long)size > end - at) return false;
		next = at + (long)size;
		if (next <= at) return false;
		if (!memcmp(h + 4, type, 4)) {
			*body = at + hl;
			*len = (long)size - hl;
			return true;
		}
		at = next;
	}
	return false;
}

int mf_mp4_seconds(const char *path)
{
	FILE *f = path ? fopen(path, "rb") : NULL;
	unsigned char m[32];
	long end, moov, moov_len, mvhd, mvhd_len;
	uint64_t scale, dur, secs;
	size_t want, got;
	int out = -1;

	if (!f) return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0) { fclose(f); return -1; }
	if (!find_box(f, 0, end, "moov", &moov, &moov_len) ||
	    !find_box(f, moov, moov + moov_len, "mvhd", &mvhd, &mvhd_len) ||
	    mvhd_len <= 0 || fseek(f, mvhd, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	/* The header's version says how much of it there must be: 20 bytes to
	 * the duration in version 0, 32 in version 1 */
	want = (unsigned long)mvhd_len < sizeof m ? (size_t)mvhd_len : sizeof m;
	got = fread(m, 1, want, f);
	fclose(f);
	if (got < 1) return -1;
	if (m[0] == 1) {
		if (mvhd_len < 32 || got < 32) return -1;
		scale = be32(m + 20);
		dur = (uint64_t)be32(m + 24) << 32 | be32(m + 28);
	} else {
		if (mvhd_len < 20 || got < 20) return -1;
		scale = be32(m + 12);
		dur = be32(m + 16);
	}
	if (!scale) return -1;
	secs = dur / scale + (dur % scale >= (scale + 1) / 2 ? 1 : 0);
	if (secs <= (uint64_t)INT_MAX) out = (int)secs;
	return out;
}
