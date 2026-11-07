/* SPDX-License-Identifier: GPL-3.0-only */
/* See sgdbrun.h. Apart from sgdb.c the way ssrun.c is apart from ssfetch.c:
 * this half reaches for the card, the shrinker and the one async slot, and
 * that half is the reading and the judging. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "sgdbrun.h"

#include "artscrape.h"
#include "artshrink.h"
#include "fileio.h"
#include "monoclock.h"
#include "net.h"
#include "sgdb.h"

#ifndef SGDB_TMP
#define SGDB_TMP "/tmp/zyperos-sgdb-reply.json"
#endif
/* Between two requests to the API, at least. Overridable so the check does
 * not spend it. */
#ifndef SGDB_GAP_MS
#define SGDB_GAP_MS 250
#endif
#define SGDB_API_WAIT_S   20
#define SGDB_IMAGE_WAIT_S 60
/* A search lists a few dozen games and a game's grids a few dozen images:
 * tens of kilobytes. Anything near this is not a reply worth reading. */
#define SGDB_REPLY_MAX (1024 * 1024)
#define SGDB_PATH_MAX  2048

static struct {
	enum { G_OFF, G_ASK, G_ASK_WAIT, G_GRIDS, G_GRIDS_WAIT, G_IMAGE_WAIT,
	       G_DONE, G_FAIL } phase;
	char   stem[512], want[512], name[256];
	char   dest[SGDB_PATH_MAX], got[SGDB_PATH_MAX + 8];
	long   id;
	double score;
} g;

/* Outside `g`: the pacing is the API's business, not the game's, and the
 * reasons are read after a step has ended the game. */
static long g_not_before;
static bool g_toomany, g_refused, g_noanswer;

bool        sgdb_run_too_many(void)  { return g_toomany; }
bool        sgdb_run_refused(void)   { return g_refused; }
bool        sgdb_run_no_answer(void) { return g_noanswer; }

/* One line per request, for the log: the game, what was asked, what came
 * back. Never a URL and never the header - the key is in the header. */
static void say(const char *fmt, ...)
{
#ifdef SGDB_QUIET
	(void)fmt;
#else
	va_list ap;

	fprintf(stderr, "sgdb: %s: ", g.stem);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
#endif
}

void sgdb_run_cancel(void)
{
	if (g.phase == G_ASK_WAIT || g.phase == G_GRIDS_WAIT || g.phase == G_IMAGE_WAIT)
		net_async_abort();
	remove(SGDB_TMP);
	if (g.got[0]) unlink(g.got);
	memset(&g, 0, sizeof g);
}

bool sgdb_run_begin(const char *stem, const char *dest)
{
	sgdb_run_cancel();
	g_toomany = g_refused = g_noanswer = false;
	if (!stem || !dest || !sgdb_have_key()) return false;
	snprintf(g.stem, sizeof g.stem, "%s", stem);
	sgdb_title(stem, g.want, sizeof g.want);
	if (!g.want[0]) return false;
	if (snprintf(g.dest, sizeof g.dest, "%s", dest) >= (int)sizeof g.dest ||
	    snprintf(g.got, sizeof g.got, "%s.sgdb", dest) >= (int)sizeof g.got) {
		memset(&g, 0, sizeof g);
		return false;
	}
	g.phase = G_ASK;
	return true;
}

/* Start one request to the API, key in the header. */
static bool ask(const char *url)
{
	char hdr[SGDB_KEY_MAX + 32];
	bool ok;

	if (!sgdb_auth_header(hdr, sizeof hdr)) return false;
	remove(SGDB_TMP);
	ok = net_get_async_hdr(url, SGDB_TMP, SGDB_API_WAIT_S, hdr);
	memset(hdr, 0, sizeof hdr);
	g_not_before = now_ms() + SGDB_GAP_MS;
	return ok;
}

/* A refusal, sorted into the three that mean something past this game. */
static int refused(const char *what)
{
	int http = net_async_http();

	g_refused  = http == 401 || http == 403;
	g_toomany  = http == 429;
	g_noanswer = http == 0 || http >= 500;
	if (g_refused) sgdb_key_mark(SGDB_REFUSED);
	say("%s: %s%s", what, art_answer(),
	    g_refused ? " - the key was refused" : g_toomany ? " - too many requests"
	    : g_noanswer ? "" : " - not there");
	g.phase = G_FAIL;
	return -1;
}

/* The reply in SGDB_TMP, read whole, or NULL. */
static char *reply(size_t *len)
{
	char *body = file_read(SGDB_TMP, SGDB_REPLY_MAX, len);

	remove(SGDB_TMP);
	return body;
}

int sgdb_run_step(void)
{
	char url[1024];
	char *body;
	size_t len;
	int r;

	switch (g.phase) {
	case G_OFF:  return -1;
	case G_DONE: return 0;
	case G_FAIL: return -1;

	case G_ASK:
		if (now_ms() < g_not_before) return 1;
		if (!sgdb_search_url(g.want, url, sizeof url) || !ask(url)) {
			say("could not ask");
			g_noanswer = true;
			g.phase = G_FAIL;
			return -1;
		}
		g.phase = G_ASK_WAIT;
		return 1;

	case G_ASK_WAIT:
		if ((r = net_async_poll()) == 0) return 1;
		if (r < 0) return refused("search");
		sgdb_key_mark(SGDB_WORKS);
		body = reply(&len);
		if (!body || !sgdb_pick_game(body, len, g.want, &g.id, g.name,
		                             sizeof g.name, &g.score)) {
			if (g.name[0])
				say("search \"%s\": %s - closest \"%s\" at %.2f, under %.2f",
				    g.want, art_answer(), g.name, g.score, SGDB_MATCH);
			else
				say("search \"%s\": %s - no game", g.want, art_answer());
			free(body);
			g.phase = G_FAIL;
			return -1;
		}
		free(body);
		say("search \"%s\": %s - \"%s\" at %.2f", g.want, art_answer(), g.name, g.score);
		g.phase = G_GRIDS;
		return 1;

	case G_GRIDS:
		if (now_ms() < g_not_before) return 1;
		if (!sgdb_grids_url(g.id, url, sizeof url) || !ask(url)) {
			g_noanswer = true;
			g.phase = G_FAIL;
			return -1;
		}
		g.phase = G_GRIDS_WAIT;
		return 1;

	case G_GRIDS_WAIT:
		if ((r = net_async_poll()) == 0) return 1;
		if (r < 0) return refused("covers");
		body = reply(&len);
		if (!body || !sgdb_pick_grid(body, len, url, sizeof url)) {
			say("covers for \"%s\": %s - none portrait in PNG or JPEG", g.name,
			    art_answer());
			free(body);
			g.phase = G_FAIL;
			return -1;
		}
		free(body);
		/* Their CDN, with no header: the key is for the API alone. */
		unlink(g.got);
		if (!net_get_async(url, g.got, SGDB_IMAGE_WAIT_S)) {
			g_noanswer = true;
			g.phase = G_FAIL;
			return -1;
		}
		g.phase = G_IMAGE_WAIT;
		return 1;

	case G_IMAGE_WAIT: {
		struct stat st;

		if ((r = net_async_poll()) == 0) return 1;
		if (r < 0) {
			g_noanswer = net_async_http() == 0 || net_async_http() >= 500;
			say("cover: %s - failed", art_answer());
			g.phase = G_FAIL;
			return -1;
		}
		/* A 200 with a page in it is not a cover. */
		if (!art_is_cover(g.got) || rename(g.got, g.dest) != 0) {
			say("cover: %s - not a PNG or JPEG, not kept", art_answer());
			unlink(g.got);
			g.phase = G_FAIL;
			return -1;
		}
		say("cover: %s, %ld bytes", art_answer(),
		    stat(g.dest, &st) == 0 ? (long)st.st_size : 0L);
		g.got[0] = '\0';
		/* To the size the shelf draws, and to PNG */
		art_shrink(g.dest);
		g.phase = G_DONE;
		return 0;
	}
	}
	return -1;
}
