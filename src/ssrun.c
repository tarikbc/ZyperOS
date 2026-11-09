/* SPDX-License-Identifier: GPL-3.0-only */
/* See ssrun.h. Apart from ssfetch.c on purpose: this half reaches for the
 * card, the shrinker and the shelf's paths, while that half is the reading
 * and the judging and is linked by tools/ss-check.c with no SDL anywhere
 * near it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "ssrun.h"

#include "artscrape.h"
#include "artshrink.h"
#include "artsrc.h"
#include "db.h"
#include "fileio.h"
#include "i18n.h"
#include "library.h"
#include "monoclock.h"
#include "net.h"
#include "ss.h"


#ifndef SS_TMP
#define SS_TMP "/tmp/zyperos-ss-reply.json"
#endif

/* Sized from what actually goes in them, the way artscrape.c sizes its own.
 * A destination sized "like the directory" is how the cross compiler came to
 * point out that the middle of the path could not fit - clang says nothing
 * about any of this, so the device build is the only place it shows up. */
#define SS_DIR_MAX  (LIB_PATH * 2)
#define SS_DEST_MAX (SS_DIR_MAX + LIB_PATH + 32)

static struct {
	enum { R_OFF, R_ASK, R_RETRY, R_ART, R_VIDEO, R_DONE, R_FAIL } phase;
	char      folder[64], file[LIB_PATH], stem[LIB_PATH], query[LIB_PATH];
	char      dir[SS_DIR_MAX], dest[SS_DEST_MAX], snap[SS_DEST_MAX + 8];
	int       answer;              /* what the step says once the snap is done */
	uint32_t  crc;
	bool      metadata_only;
	bool      lookup_only;      /* the reply and its text, no file */
	ss_result got;
	char      where[64];
} g_run;

/* Outside g_run on purpose: the account's day is not the current game's
 * business and must survive a cancel. */
static int  g_left = -1;
static bool g_toomany;
static bool (*g_want_video)(const char *folder);
static bool g_got_video;

void ss_run_videos(bool (*want)(const char *folder)) { g_want_video = want; }
bool ss_run_video_wanted(const char *folder)
{
	return g_want_video && folder && g_want_video(folder);
}
bool ss_run_got_video(void) { return g_got_video; }

const char      *ss_run_where(void)  { return g_run.where; }
int              ss_run_left(void)   { return g_left; }
bool             ss_run_too_many(void) { return g_toomany; }

void ss_run_cancel(void)
{
	if (g_run.phase == R_ASK || g_run.phase == R_RETRY || g_run.phase == R_ART ||
	    g_run.phase == R_VIDEO)
		net_async_abort();
	if (g_run.phase == R_VIDEO && g_run.snap[0]) remove(g_run.snap);
	remove(SS_TMP);
	memset(&g_run, 0, sizeof g_run);
}

/* Start one lookup, by checksum when there is one. */
static bool start_ask(uint32_t crc)
{
	char url[1024];
	bool ok;

	if (!ss_lookup_url(url, sizeof url, g_run.folder, g_run.query, crc))
		return false;
	remove(SS_TMP);
	ok = net_get_async(url, SS_TMP, SS_WAIT_S);
	memset(url, 0, sizeof url);          /* it carried both credentials */
	return ok;
}

static bool run_begin(const char *folder, const char *file, const char *stem,
                      const char *rom_dir, const char *exts, bool metadata_only,
                      bool lookup_only)
{
	memset(&g_run, 0, sizeof g_run);
	g_toomany = false;
	g_got_video = false;
	g_run.metadata_only = metadata_only;
	g_run.lookup_only = lookup_only;
	if (metadata_only && !db_lib()) return false;
	if (!ss_signed_in() || !folder || !file || !stem) return false;
	if (ss_system_id(folder) == 0) return false;

	snprintf(g_run.folder, sizeof g_run.folder, "%s", folder);
	snprintf(g_run.file, sizeof g_run.file, "%s", file);
	snprintf(g_run.stem, sizeof g_run.stem, "%s", stem);
	/* A disc folder may launch "Disc 1.chd". Ask about the folder's game,
	 * not that generic disc label, but keep the full launch path as DB key. */
	if (strchr(file, '/')) {
		const char *ext = strrchr(strrchr(file, '/') + 1, '.');
		/* Keep an extension so the name checker does not treat a dot in
		 * the title itself ("Bros. 3") as the file extension. */
		snprintf(g_run.query, sizeof g_run.query, "%s%s", stem, ext ? ext : "");
	} else {
		snprintf(g_run.query, sizeof g_run.query, "%s", file);
	}
	/* The system's directory, handed in rather than built from platform.h:
	 * this file has no business knowing where the card keeps its ROMs, and
	 * that is also what keeps it linkable without SDL. */
	snprintf(g_run.dir, sizeof g_run.dir, "%s", rom_dir ? rom_dir : ".");
	/* A loose file has no central directory to read, so it is asked about by
	 * name alone - the same path a disc image takes. */
	if (!art_rom_crc(rom_dir, stem, exts, &g_run.crc)) g_run.crc = 0;

	snprintf(g_run.where, sizeof g_run.where, "%s", T(S_SS_WHERE_LOOKING));
	if (!start_ask(g_run.crc)) return false;
	g_run.phase = R_ASK;
	return true;
}

bool ss_run_begin(const char *folder, const char *file, const char *stem,
                  const char *rom_dir, const char *exts)
{
	return run_begin(folder, file, stem, rom_dir, exts, false, false);
}

bool ss_run_begin_metadata(const char *folder, const char *file, const char *stem,
                           const char *rom_dir, const char *exts)
{
	return run_begin(folder, file, stem, rom_dir, exts, true, false);
}

bool ss_run_begin_lookup(const char *folder, const char *file, const char *stem,
                         const char *rom_dir, const char *exts)
{
	return run_begin(folder, file, stem, rom_dir, exts, false, true);
}

const ss_result *ss_run_result(void)
{
	return g_run.phase == R_DONE ? &g_run.got : NULL;
}

/* One line per request, for the log: the game, how it was asked, what came
 * back, how big and how long, and what was made of it. Never the URL, which
 * carries both credentials.
 *
 * The scraper used to say nothing at all, which is how twelve of the most
 * famous games on a fresh card came back with covers and no text and nothing
 * in the log to say why. */
static void say_reply(const char *how, long bytes, int parse_ms, const char *what)
{
	char curl[24] = "";

	if (net_async_http() == 0)
		snprintf(curl, sizeof curl, ", curl %d", net_async_exit());
	fprintf(stderr, "ss: %s by %s: HTTP %d%s, %ld bytes, %d ms, read in %d ms - %s\n",
	        g_run.file, how, net_async_http(), curl, bytes, net_async_ms(),
	        parse_ms, what);
}

/* The reply file, into g_run.got. `bytes` takes its size and `parse_ms` the
 * time spent reading it, both for the log: a reply over the limit is refused
 * here, and reading one runs inside a frame. */
static bool read_reply(long *bytes, int *parse_ms)
{
	long t0 = now_ms();
	size_t n;
	char *body = file_read(SS_TMP, SS_REPLY_MAX, &n);
	bool ok = false;

	*bytes = (long)n;
	if (body) {
		ok = ss_parse(body, n, g_run.query, &g_run.got);
		/* Even a reply about the wrong game states the account's day. */
		if (g_run.got.max_today > 0)
			g_left = g_run.got.max_today - g_run.got.used_today;
		free(body);
	}
	remove(SS_TMP);
	*parse_ms = (int)(now_ms() - t0);
	return ok;
}

/* A snap is a megabyte or three: two minutes covers a slow Wi-Fi */
#define SS_SNAP_WAIT_S 120

static void mkdir_media(const char *dir)
{
	char p[SS_DIR_MAX + 16];

	if (snprintf(p, sizeof p, "%s/.media", dir) < (int)sizeof p) mkdir(p, 0777);
}

/* And back out again when nothing landed in it: rmdir takes only an empty
 * folder, so a console's covers are never at risk. An empty .media in every
 * console the Scraper tried was half of "folders basically everywhere"
 * (2026-11-06). */
static void rmdir_media(const char *dir)
{
	char p[SS_DIR_MAX + 16];

	if (snprintf(p, sizeof p, "%s/.media", dir) < (int)sizeof p) rmdir(p);
}

/* The snap, if this console wants snaps, the reply offered one and there is
 * none on the card yet: started, and the step's answer kept for when it is
 * over. False when there is nothing to fetch. */
static bool start_video(int answer)
{
	char want[SS_DEST_MAX];
	struct stat st;

	if (!g_run.got.video[0] || !ss_run_video_wanted(g_run.folder)) return false;
	if (snprintf(want, sizeof want, "%s/.media/%s.mp4", g_run.dir, g_run.stem)
	    >= (int)sizeof want) return false;
	if (stat(want, &st) == 0 && st.st_size > 0) return false;
	snprintf(g_run.snap, sizeof g_run.snap, "%s.snap", want);
	mkdir_media(g_run.dir);
	snprintf(g_run.where, sizeof g_run.where, "%s", T(S_SS_WHERE_SNAP));
	if (!net_get_async(g_run.got.video, g_run.snap, SS_SNAP_WAIT_S)) return false;
	g_run.answer = answer;
	g_run.phase = R_VIDEO;
	return true;
}

/* An MP4 says "ftyp" at its fifth byte */
static bool is_mp4(const char *path)
{
	unsigned char h[8];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return false;
	n = fread(h, 1, sizeof h, f);
	fclose(f);
	return n == 8 && !memcmp(h + 4, "ftyp", 4);
}

static int run_step(void);

/* A step, and the folder made for its files taken back when the game ends
 * with none in it */
int ss_run_step(void)
{
	int r = run_step();

	if (r != 1 && g_run.dir[0]) rmdir_media(g_run.dir);
	return r;
}

static int run_step(void)
{
	int poll;

	switch (g_run.phase) {
	case R_OFF:  return -1;
	case R_DONE: return 0;
	case R_FAIL: return -1;

	case R_ASK:
	case R_RETRY: {
		const char *how = (g_run.phase == R_RETRY || !g_run.crc) ? "name" : "checksum";
		bool again = g_run.phase == R_ASK && g_run.crc;
		char what[GAME_TITLE_MAX + 80];
		long bytes;
		int  parse_ms, http;
		bool saved;

		poll = net_async_poll();
		if (poll == 0) return 1;                     /* still in flight */
		if (poll < 0) {
			http = net_async_http();
			g_toomany = (http == 429);
			say_reply(how, 0, 0, http == 429 ? "too many requests"
			                   : http == 404 ? "not a game they know"
			                   : http == 0   ? "no answer" : "refused");
			g_run.phase = R_FAIL;
			return -1;
		}
		if (!read_reply(&bytes, &parse_ms) || !g_run.got.found) {
			say_reply(how, bytes, parse_ms,
			          bytes >= SS_REPLY_MAX ? "too big, not read"
			          : bytes == 0 ? "empty" : "no game in it");
			g_run.phase = R_FAIL;
			return -1;
		}
		/* A checksum answer that fails the check is asked again by name; see
		 * ss_lookup for the measurement that says why. Once only - the retry
		 * has already been asked the only other way there is. */
		if (!g_run.got.name_ok) {
			snprintf(what, sizeof what, "\"%s\" fails the name check%s",
			         g_run.got.name, again ? ", asking by name" : "");
			say_reply(how, bytes, parse_ms, what);
			if (again) {
				snprintf(g_run.where, sizeof g_run.where, "%s", T(S_SS_WHERE_BY_NAME));
				if (start_ask(0)) { g_run.phase = R_RETRY; return 1; }
			}
			/* The wrong game. Never persist its title or prose. */
			g_run.phase = R_FAIL;
			return -1;
		}
		snprintf(what, sizeof what, "\"%s\"%s", g_run.got.name,
		         g_run.got.art[0] ? "" : ", no cover");
		say_reply(how, bytes, parse_ms, what);
		/* Metadata is useful without a cover, and survives a failed or
		 * canceled image fetch. Only a validated answer reaches this write. */
		saved = db_game_set(db_lib(), g_run.folder, g_run.file, &g_run.got.meta);
		if (!saved) fprintf(stderr, "ss: %s metadata: not saved\n", g_run.file);
		/* The page asked what is on offer: the reply is the answer, kept
		 * for ss_run_result, and nothing is fetched */
		if (g_run.lookup_only) {
			g_run.phase = R_DONE;
			return 0;
		}
		/* A metadata-only run never asks for, shrinks, or replaces an image.
		 * Its success is the persisted row, regardless of available media. */
		if (g_run.metadata_only) {
			if (saved && start_video(0)) return 1;
			g_run.phase = saved ? R_DONE : R_FAIL;
			return saved ? 0 : -1;
		}
		if (!g_run.got.art[0]) {
			/* No cover, but a snap is still worth having for later */
			if (start_video(-1)) return 1;
			g_run.phase = R_FAIL;
			return -1;
		}
		snprintf(g_run.dest, sizeof g_run.dest, "%s/.media/%s.png",
		         g_run.dir, g_run.stem);
		/* The folder is made by what puts the first cover in it: the card
		 * no longer comes with one per console (mk/payload.sh) */
		mkdir_media(g_run.dir);
		snprintf(g_run.where, sizeof g_run.where, "%s", T(S_SS_WHERE_COVER));
		if (!net_get_async(g_run.got.art, g_run.dest, 60)) {
			g_run.phase = R_FAIL;
			return -1;
		}
		g_run.phase = R_ART;
		return 1;
	}

	case R_ART: {
		struct stat st;

		poll = net_async_poll();
		if (poll == 0) return 1;
		fprintf(stderr, "ss: %s cover: HTTP %d, %ld bytes, %d ms%s\n",
		        g_run.file, net_async_http(),
		        poll > 0 && stat(g_run.dest, &st) == 0 ? (long)st.st_size : 0L,
		        net_async_ms(), poll > 0 ? "" : " - failed");
		if (poll < 0) { g_run.phase = R_FAIL; return -1; }
		/* Down to the size a card is ever drawn at, the same as a libretro
		 * cover: art_shrink queues it and returns. */
		art_shrink(g_run.dest);
		/* Where it came from, for the game's Cover and Video page */
		{
			art_source as = { AS_SS, AK_BOX, "", "" };

			snprintf(as.region, sizeof as.region, "%s", g_run.got.art_region);
			artsrc_set(g_run.folder, g_run.stem, &as);
		}

		if (start_video(0)) return 1;
		g_run.phase = R_DONE;
		return 0;
	}

	case R_VIDEO: {
		char want[SS_DEST_MAX + 8];
		struct stat st;
		size_t k;

		poll = net_async_poll();
		if (poll == 0) return 1;
		snprintf(want, sizeof want, "%s", g_run.snap);
		k = strlen(want);
		if (k > 5) want[k - 5] = '\0';              /* the ".snap" off */
		if (poll > 0 && is_mp4(g_run.snap) && rename(g_run.snap, want) == 0) {
			g_got_video = true;
			fprintf(stderr, "ss: %s snap: HTTP %d, %ld bytes, %d ms\n", g_run.file,
			        net_async_http(), stat(want, &st) == 0 ? (long)st.st_size : 0L,
			        net_async_ms());
		} else {
			remove(g_run.snap);
			fprintf(stderr, "ss: %s snap: HTTP %d, %d ms - not kept\n", g_run.file,
			        net_async_http(), net_async_ms());
		}
		g_run.snap[0] = '\0';
		g_run.phase = g_run.answer == 0 ? R_DONE : R_FAIL;
		return g_run.answer;
	}
	}
	return -1;
}
