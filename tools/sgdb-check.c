/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* SteamGridDB on the device, against a fake API.
 *
 *     make check-sgdb
 *
 * The real src/sgdb.c and src/sgdbrun.c - and the real src/artscrape.c around
 * them, for whole runs - with the network stubbed: every request is answered
 * from a written reply, and every request is kept, so what it carried can be
 * read back. Four things, each one that a device would get wrong quietly:
 *
 *   THE KEY. Cleaned as it is pasted, kept and read back across a restart,
 *   shown as four characters and never more, sent in a header and never in a
 *   URL, never to their CDN, and checked once when it is entered: kept when
 *   it works or nothing answered, not kept when it was refused (a 401 or a
 *   403), and the key before it kept then.
 *
 *   THE MATCH. The computer tool's title and 0.85, on written replies: the
 *   first of the best names, nothing under the bar, and only grids this
 *   card can draw. tools/sgdb-check.py holds title and ratio to the tool's
 *   own Python over many more names.
 *
 *   ONE GAME. A cover found lands whole and only then over the old one; an
 *   answer that is not a picture, a refused key, a 429 and silence each end
 *   the game the way the run needs to tell them apart.
 *
 *   A WHOLE RUN. SteamGridDB after libretro, for a shelf libretro misses and
 *   for one it has no collection for, through the real step machine.
 *
 * Modes for tools/sgdb-check.py: --title reads stems, one a line, and prints
 * each title; --ratio reads "a<TAB>b" lines and prints each sgdb_close. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "../src/artscrape.h"
#include "../src/db.h"
#include "../src/i18n.h"
#include "../src/net.h"
#include "../src/sgdb.h"
#include "../src/sgdbrun.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

#define KEY  "0123456789abcdef0123456789abcd9f"
#define KEY2 "fedcba9876543210fedcba9876543210"

/* ---- what artscrape.c needs that is not about this ---------------------- */
void art_shrink(const char *path);
static int g_shrunk;
void art_shrink(const char *path) { (void)path; g_shrunk++; }

bool ss_signed_in(void) { return false; }
bool ss_run_begin(const char *a, const char *b, const char *c, const char *d,
                  const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
int  ss_system_id(const char *folder) { (void)folder; return 0; }
bool ss_run_begin_metadata(const char *a, const char *b, const char *c,
                           const char *d, const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
int  ss_run_step(void) { return -1; }
void ss_run_cancel(void) { }
int  ss_run_left(void) { return -1; }
bool ss_run_too_many(void) { return false; }
bool ss_run_video_wanted(const char *folder) { (void)folder; return false; }
bool ss_run_got_video(void) { return false; }

/* ---- the fake API --------------------------------------------------------
 *
 * Search, grids and image answers, chosen per test. Everything else - the
 * libretro passes a whole run makes first - is a 404, so a run reaches
 * SteamGridDB with every game still missing. */
static const char *SEARCH_HIT =
	"{\"success\":true,\"data\":["
	"{\"id\":11,\"name\":\"Celeste\",\"types\":[\"steam\"],\"verified\":true},"
	"{\"id\":22,\"name\":\"Celeste Classic\",\"types\":[],\"verified\":true},"
	"{\"id\":33,\"name\":\"Celeste Classic\",\"types\":[],\"verified\":false}]}";
static const char *GRIDS_HIT =
	"{\"success\":true,\"data\":["
	"{\"id\":1,\"width\":600,\"height\":900,\"mime\":\"image/webp\","
	"\"url\":\"https://cdn2.steamgriddb.com/grid/webp-one.webp\"},"
	"{\"id\":2,\"width\":600,\"height\":900,\"mime\":\"image/png\","
	"\"url\":\"http://cdn2.steamgriddb.com/grid/plain-http.png\"},"
	"{\"id\":3,\"width\":600,\"height\":900,\"mime\":\"image/jpeg\","
	"\"url\":\"https://cdn2.steamgriddb.com/grid/the-cover.jpg\"}]}";
static const char *GRIDS_WEBP_ONLY =
	"{\"success\":true,\"data\":[{\"id\":1,\"mime\":\"image/webp\","
	"\"url\":\"https://cdn2.steamgriddb.com/grid/webp-one.webp\"}]}";

static const char *g_search = NULL;    /* NULL: SEARCH_HIT */
static const char *g_grids = NULL;     /* NULL: GRIDS_HIT */
static const char *g_image = "\xFF\xD8\xFF\xE0jpeg-cover";
static int  g_api_status = 200;        /* every API answer's status */
static int  g_check_status = 200;      /* the key check's */
static int  g_http, g_nreq;
static char g_url[2048], g_path[2048], g_hdr[256];
static bool g_pending;
static char g_log[8192];               /* "URL | header" for every request */

static void note(const char *url, const char *header)
{
	char line[2400];

	snprintf(line, sizeof line, "%s | %s\n", url, header ? header : "");
	if (strlen(g_log) + strlen(line) < sizeof g_log) strcat(g_log, line);
	g_nreq++;
}

bool net_get_async_hdr(const char *url, const char *path, int timeout_s,
                       const char *header)
{
	(void)timeout_s;
	snprintf(g_url, sizeof g_url, "%s", url);
	snprintf(g_path, sizeof g_path, "%s", path);
	snprintf(g_hdr, sizeof g_hdr, "%s", header ? header : "");
	note(url, header);
	g_pending = true;
	return true;
}

bool net_get_async(const char *url, const char *path, int timeout_s)
{
	return net_get_async_hdr(url, path, timeout_s, NULL);
}

static bool put(const char *path, const char *body)
{
	char part[2100];
	FILE *f;

	snprintf(part, sizeof part, "%s.part", path);
	if (!(f = fopen(part, "wb"))) return false;
	fputs(body, f);
	if (fclose(f) != 0) return false;
	return rename(part, path) == 0;
}

int net_async_poll(void)
{
	const char *body = NULL;

	if (!g_pending) return -1;
	g_pending = false;
	g_http = 404;
	if (strstr(g_url, "/search/autocomplete/")) {
		g_http = g_api_status;
		body = g_search ? g_search : SEARCH_HIT;
	} else if (strstr(g_url, "/grids/game/")) {
		g_http = g_api_status;
		body = g_grids ? g_grids : GRIDS_HIT;
	} else if (g_url[0] && g_url[strlen(g_url) - 1] == '/') {
		/* libretro's catalog, of other games only */
		g_http = 200;
		body = "<a href=\"Unrelated%20Title%20%28USA%29.png\">x</a>\n";
	} else if (strstr(g_url, "cdn2.steamgriddb.com")) {
		g_http = 200;
		body = g_image;
	}
	if (g_http != 200 || !body) return -1;
	return put(g_path, body) ? 1 : -1;
}

void net_async_abort(void) { g_pending = false; }
int  net_async_http(void)  { return g_http; }
int  net_async_ms(void)    { return 0; }
int  net_async_exit(void)  { return g_http ? 0 : 6; }

/* The check on entry, which waits: a thread calls this. It answers with
 * g_check_status as net.c would: the status a server gave, 0 for nothing
 * answering, and the body only for a 200. */
static char g_check_hdr[256];
long net_get_hdr_answer(const char *url, const char *header, char *out, size_t outn,
                        int timeout_s, int *http)
{
	(void)timeout_s;
	snprintf(g_check_hdr, sizeof g_check_hdr, "%s", header ? header : "");
	*http = 0;
	if (strstr(url, KEY) || strstr(url, KEY2)) return -1;   /* caught below */
	*http = g_check_status;
	if (g_check_status != 200) return -1;
	snprintf(out, outn, "{\"success\":true,\"data\":[]}");
	return (long)strlen(out);
}

static void fresh(void)
{
	g_search = g_grids = NULL;
	g_image = "\xFF\xD8\xFF\xE0jpeg-cover";
	g_api_status = g_check_status = 200;
	g_log[0] = '\0';
	g_nreq = 0;
	g_shrunk = 0;
}

/* ---- the card, in /tmp --------------------------------------------------- */
static char g_root[256];

static bool write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");

	if (!f) return false;
	fputs(text, f);
	return fclose(f) == 0;
}

static const char *read_back(const char *path)
{
	static char buf[128];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return "(none)";
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

static bool exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* One game through sgdbrun, to its end. Bounded: a machine that never ends is
 * a failing check, not a hung one. */
static int one_game(const char *stem, const char *dest)
{
	int r = -1, steps = 0;

	if (!sgdb_run_begin(stem, dest)) return -2;
	while ((r = sgdb_run_step()) == 1 && steps++ < 1000) {}
	return r;
}

static sgdb_try settle(void)
{
	sgdb_try t;
	int spins = 0;

	while ((t = sgdb_try_poll()) == SGDB_TRY_RUNNING && spins++ < 2000) usleep(1000);
	/* One line of the message panel at 640 wide: no message may scroll */
	CHECK(strlen(sgdb_try_said()) <= 34, "\"%s\" is longer than a line", sgdb_try_said());
	return t;
}

/* ---- the modes sgdb-check.py drives ------------------------------------- */
static int mode_title(void)
{
	char line[1024], out[1024];

	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\n")] = '\0';
		sgdb_title(line, out, sizeof out);
		printf("%s\n", out);
	}
	return 0;
}

static int mode_ratio(void)
{
	char line[2048];

	while (fgets(line, sizeof line, stdin)) {
		char *tab;

		line[strcspn(line, "\n")] = '\0';
		if (!(tab = strchr(line, '\t'))) { printf("?\n"); continue; }
		*tab = '\0';
		printf("%.17g\n", sgdb_close(line, tab + 1));
	}
	return 0;
}

int main(int argc, char **argv)
{
	char dev[300], lib[300], shelf[400], media[500], dest[600], buf[256];
	char url[1024];
	long id;
	double score;

	if (argc > 1 && !strcmp(argv[1], "--title")) return mode_title();
	if (argc > 1 && !strcmp(argv[1], "--ratio")) return mode_ratio();

	printf("sgdb: SteamGridDB on the device, against a fake API\n");
	snprintf(g_root, sizeof g_root, "/tmp/zyperos-sgdb-%ld", (long)getpid());
	mkdir(g_root, 0755);
	snprintf(dev, sizeof dev, "%s/device.db", g_root);
	snprintf(lib, sizeof lib, "%s/library.db", g_root);
	if (!db_init(dev, lib, NULL)) { printf("  FAIL: no database\n"); return 1; }

	/* ---- the key ------------------------------------------------------- */
	printf("  a key is cleaned as it is pasted:\n");
	CHECK(sgdb_key_clean("  " KEY " \r\n", buf, sizeof buf) && !strcmp(buf, KEY),
	      "white space around a pasted key was not dropped: \"%s\"", buf);
	CHECK(sgdb_key_clean("Bearer " KEY, buf, sizeof buf) && !strcmp(buf, KEY),
	      "a pasted \"Bearer \" was not dropped: \"%s\"", buf);
	CHECK(!sgdb_key_clean("abc123", buf, sizeof buf), "a few characters passed for a key");
	CHECK(!sgdb_key_clean("0123456789abcdef 0123456789abcdef", buf, sizeof buf),
	      "a key with a space inside passed");
	CHECK(!sgdb_key_clean("0123456789abcdef\"0123456789abcdef", buf, sizeof buf),
	      "a key with a quote passed");
	CHECK(!sgdb_key_clean(KEY KEY, buf, sizeof buf), "a 64-character key passed");
	CHECK(!buf[0], "a refused key left text behind");

	printf("  kept, it survives a restart and shows four characters:\n");
	CHECK(!sgdb_key_load() && !sgdb_have_key(), "a key with nothing stored");
	CHECK(sgdb_key_keep(KEY, SGDB_WORKS), "the key was not kept");
	db_shutdown();
	CHECK(db_init(dev, lib, NULL), "the database did not reopen");
	CHECK(sgdb_key_load() && sgdb_have_key(), "the key did not survive a restart");
	CHECK(sgdb_key_state() == SGDB_WORKS, "its state did not survive: %d", sgdb_key_state());
	sgdb_key_masked(buf, sizeof buf);
	CHECK(!strcmp(buf, "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "cd9f"),
	      "masked, it reads \"%s\"", buf);
	CHECK(!strstr(buf, "abcd9f"), "more than four characters of the key were shown");
	{
		char got[128];

		db_get_str(db_dev(), "sgdb.key", got, sizeof got, "");
		CHECK(!strcmp(got, KEY), "the database holds \"%s\"", got);
		db_set_str(db_dev(), "sgdb.key", "short");
		CHECK(!sgdb_key_load(), "a stored key that is not one was used");
		CHECK(sgdb_key_keep(KEY, SGDB_UNCHECKED), "the key was not kept again");
	}

	printf("  addresses never carry it:\n");
	CHECK(sgdb_search_url("Celeste Classic", url, sizeof url) && !strstr(url, KEY),
	      "the search address: %s", url);
	CHECK(strstr(url, "/search/autocomplete/Celeste%20Classic") != NULL,
	      "the title was not encoded into the path: %s", url);
	CHECK(sgdb_grids_url(22, url, sizeof url) && !strstr(url, KEY), "the grids address: %s", url);
	CHECK(strstr(url, "dimensions=600x900") && strstr(url, "types=static") &&
	      strstr(url, "nsfw=false") && strstr(url, "mimes=image/png,image/jpeg"),
	      "the grids address lost a filter: %s", url);

	/* ---- the check on entry --------------------------------------------- */
	printf("  a key that works is kept and said to work:\n");
	fresh();
	sgdb_key_forget();
	CHECK(sgdb_try_begin(KEY2) == SGDB_TRY_RUNNING, "the check did not start");
	CHECK(settle() == SGDB_TRY_WORKS, "a working key was not taken");
	CHECK(sgdb_have_key() && sgdb_key_state() == SGDB_WORKS, "and was not kept as working");
	CHECK(!strcmp(g_check_hdr, "Authorization: Bearer " KEY2),
	      "the check's header was \"%s\"", g_check_hdr);
	CHECK(sgdb_try_poll() == SGDB_TRY_WORKS, "the outcome did not stay for a page to read");

	printf("  no answer keeps it, to be checked on the next run:\n");
	g_check_status = 0;
	CHECK(sgdb_try_begin(KEY) == SGDB_TRY_RUNNING, "the check did not start");
	CHECK(settle() == SGDB_TRY_KEPT, "a key nobody could check was thrown away");
	CHECK(sgdb_key_state() == SGDB_UNCHECKED, "and was not kept as unchecked");
	g_check_status = 429;
	CHECK(sgdb_try_begin(KEY) == SGDB_TRY_RUNNING && settle() == SGDB_TRY_KEPT,
	      "a 429 at the check threw the key away");

	/* SteamGridDB's 401 for a wrong key (measured 2026-11-06), and a 403:
	 * refused, said so, not kept, and the key before it kept as it was. It
	 * was no answer once - curl's `fail` made every refusal look like the
	 * network - and a wrong key was kept as unchecked. Their site down is
	 * still no answer about the key. */
	printf("  a key SteamGridDB refuses is said refused, and the key before it stays:\n");
	{
		static const int REFUSALS[] = { 401, 403 };
		char kept[128];
		size_t i;

		for (i = 0; i < sizeof REFUSALS / sizeof REFUSALS[0]; i++) {
			g_check_status = REFUSALS[i];
			CHECK(sgdb_try_begin(KEY2) == SGDB_TRY_RUNNING, "the check did not start");
			CHECK(settle() == SGDB_TRY_REFUSED, "a %d at the check was not a refusal", REFUSALS[i]);
			CHECK(!strcmp(sgdb_try_said(), T(S_SGDB_TRY_REFUSED)),
			      "after a %d it says \"%s\"", REFUSALS[i], sgdb_try_said());
			CHECK(sgdb_secret() && !strcmp(sgdb_secret(), KEY),
			      "a key refused with %d took the place of the one before", REFUSALS[i]);
			CHECK(sgdb_key_state() == SGDB_UNCHECKED, "and the one before is %d", sgdb_key_state());
			db_get_str(db_dev(), "sgdb.key", kept, sizeof kept, "");
			CHECK(!strcmp(kept, KEY), "the database holds \"%s\" after a %d", kept, REFUSALS[i]);
		}
		g_check_status = 503;
		CHECK(sgdb_try_begin(KEY) == SGDB_TRY_RUNNING && settle() == SGDB_TRY_KEPT,
		      "their site down threw the key away");
		CHECK(sgdb_key_state() == SGDB_UNCHECKED, "and was not kept as unchecked");
	}

	printf("  something that is no key is refused before any request:\n");
	CHECK(sgdb_try_begin("not a key") == SGDB_TRY_BAD, "\"not a key\" was checked");
	CHECK(strlen(sgdb_try_said()) <= 34, "\"%s\" is longer than a line", sgdb_try_said());
	CHECK(sgdb_have_key(), "a bad paste cost the key that was kept");

	/* ---- the match ------------------------------------------------------- */
	printf("  the best name, the first of equals, and nothing under 0.85:\n");
	CHECK(sgdb_pick_game(SEARCH_HIT, strlen(SEARCH_HIT), "Celeste Classic", &id,
	                     buf, sizeof buf, &score) && id == 22,
	      "picked id %ld \"%s\" at %.2f, wanted the first Celeste Classic", id, buf, score);
	CHECK(!sgdb_pick_game(SEARCH_HIT, strlen(SEARCH_HIT), "Celestial Journey Deluxe",
	                      &id, buf, sizeof buf, &score),
	      "a different game passed at %.2f as \"%s\"", score, buf);
	/* Either side of the line, as difflib puts them: 0.857 and 0.789 */
	{
		static const char *OVER = "{\"success\":true,\"data\":[{\"id\":7,\"name\":\"Celeste Classic Plus\"}]}";
		static const char *UNDER = "{\"success\":true,\"data\":[{\"id\":8,\"name\":\"Celeste: Classic Edition\"}]}";

		CHECK(sgdb_pick_game(OVER, strlen(OVER), "Celeste Classic", &id, buf, sizeof buf,
		                     &score) && id == 7,
		      "a name at %.4f was refused - 0.85 is the bar", score);
		CHECK(!sgdb_pick_game(UNDER, strlen(UNDER), "Celeste Classic", &id, buf, sizeof buf,
		                      &score),
		      "a name at %.4f was taken - 0.85 is the bar", score);
		CHECK(score > 0.789 && score < 0.790, "scored %.4f, difflib says 0.7895", score);
	}
	CHECK(!sgdb_pick_game("{\"success\":true,\"data\":[]}", 26, "Celeste", &id, buf,
	                      sizeof buf, &score), "an empty reply named a game");
	printf("  only covers this card can draw:\n");
	CHECK(sgdb_pick_grid(GRIDS_HIT, strlen(GRIDS_HIT), url, sizeof url) &&
	      !strcmp(url, "https://cdn2.steamgriddb.com/grid/the-cover.jpg"),
	      "took \"%s\" - not the JPEG past the WebP and the plain http", url);
	CHECK(!sgdb_pick_grid(GRIDS_WEBP_ONLY, strlen(GRIDS_WEBP_ONLY), url, sizeof url),
	      "a WebP was taken: %s", url);

	/* ---- one game ------------------------------------------------------- */
	snprintf(shelf, sizeof shelf, "%s/Game Boy", g_root);
	snprintf(media, sizeof media, "%s/.media", shelf);
	snprintf(dest, sizeof dest, "%s/Celeste Classic (homebrew).png", media);
	mkdir(shelf, 0755);
	mkdir(media, 0755);

	printf("  a cover found lands at the shelf's path:\n");
	fresh();
	CHECK(one_game("Celeste Classic (homebrew)", dest) == 0, "the game was not found");
	CHECK(!strcmp(read_back(dest), g_image), "the cover reads \"%s\"", read_back(dest));
	CHECK(g_shrunk == 1, "the cover went to the shrinker %d times", g_shrunk);
	{
		char sg[700];

		snprintf(sg, sizeof sg, "%s.sgdb", dest);
		CHECK(!exists(sg), "the download was left beside the cover");
	}
	CHECK(strstr(g_log, "/search/autocomplete/Celeste%20Classic | Authorization: Bearer " KEY "\n") != NULL,
	      "the search did not carry the key in its header:\n%s", g_log);
	CHECK(strstr(g_log, "/grids/game/22?") != NULL, "the grids asked were not game 22's:\n%s", g_log);
	CHECK(strstr(g_log, "the-cover.jpg | \n") != NULL,
	      "the CDN was sent a header, or not asked:\n%s", g_log);
	{
		/* The key in no URL: everything before each " | " */
		char *p, copy[sizeof g_log];
		bool in_url = false;

		snprintf(copy, sizeof copy, "%s", g_log);
		for (p = strtok(copy, "\n"); p; p = strtok(NULL, "\n")) {
			char *bar = strstr(p, " | ");

			if (bar) *bar = '\0';
			if (strstr(p, KEY)) in_url = true;
		}
		CHECK(!in_url, "the key went into a URL:\n%s", g_log);
	}

	printf("  an answer that is no picture leaves the old cover:\n");
	fresh();
	write_file(dest, "\x89PNGold");
	g_image = "<html>not found</html>";
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1, "a page passed for a cover");
	CHECK(!strcmp(read_back(dest), "\x89PNGold"), "the old cover is now \"%s\"", read_back(dest));
	CHECK(g_shrunk == 0, "a page went to the shrinker");

	printf("  a name under the bar asks for no covers:\n");
	fresh();
	g_search = "{\"success\":true,\"data\":[{\"id\":5,\"name\":\"Something Else Entirely\"}]}";
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1, "a poor match was taken");
	CHECK(!strstr(g_log, "/grids/"), "covers were asked for a game that did not match:\n%s", g_log);
	CHECK(!sgdb_run_refused() && !sgdb_run_too_many() && !sgdb_run_no_answer(),
	      "an ordinary miss was reported as more");

	printf("  WebP alone is a miss, with no download:\n");
	fresh();
	g_grids = GRIDS_WEBP_ONLY;
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1, "a WebP was taken");
	CHECK(!strstr(g_log, "cdn2"), "a WebP was downloaded:\n%s", g_log);

	printf("  a refused key, a 429 and silence are told apart:\n");
	fresh();
	g_api_status = 429;
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1 && sgdb_run_too_many(),
	      "a 429 was not reported as one");
	fresh();
	g_api_status = 0;
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1 && sgdb_run_no_answer(),
	      "silence was not reported as no answer");
	fresh();
	g_api_status = 401;
	CHECK(one_game("Celeste Classic (homebrew)", dest) == -1 && sgdb_run_refused(),
	      "a 401 was not reported as a refused key");
	CHECK(sgdb_key_state() == SGDB_REFUSED, "and the key was not marked refused");
	fresh();
	CHECK(one_game("Celeste Classic (homebrew)", dest) == 0, "a working key again");
	CHECK(sgdb_key_state() == SGDB_WORKS, "and the key was not marked working again");

	printf("  a stem that is all tags is not asked about:\n");
	fresh();
	CHECK(one_game("(Proto) [b]", dest) == -2 && g_nreq == 0, "a search for nothing");

	/* ---- a whole run ---------------------------------------------------- */
	printf("  a whole run: libretro misses, then SteamGridDB has it:\n");
	{
		systems_cfg sys;
		art_progress st;
		char rom[700], cover[700];
		int steps = 0;

		fresh();
		remove(dest);
		snprintf(rom, sizeof rom, "%s/Celeste Classic (homebrew).gb", shelf);
		write_file(rom, "rom");
		memset(&sys, 0, sizeof sys);
		sys.count = 2;
		snprintf(sys.systems[0].folder, sizeof sys.systems[0].folder, "Game Boy");
		snprintf(sys.systems[0].exts, sizeof sys.systems[0].exts, "gb");
		/* A shelf libretro files nothing under */
		snprintf(sys.systems[1].folder, sizeof sys.systems[1].folder, "Homebrew Shelf");
		snprintf(sys.systems[1].exts, sizeof sys.systems[1].exts, "gb");
		snprintf(rom, sizeof rom, "%s/Homebrew Shelf", g_root);
		mkdir(rom, 0755);
		snprintf(rom, sizeof rom, "%s/Homebrew Shelf/Celeste-Classic.gb", g_root);
		write_file(rom, "rom");

		art_begin(&sys, g_root, NULL);
		while (art_step() == 1 && steps++ < 5000) {}
		art_status(&st);
		CHECK(st.found == 2, "found %d of 2", st.found);
		CHECK(!strcmp(read_back(dest), g_image), "the Game Boy cover reads \"%s\"",
		      read_back(dest));
		snprintf(cover, sizeof cover, "%s/Homebrew Shelf/.media/Celeste-Classic.png", g_root);
		CHECK(!strcmp(read_back(cover), g_image), "the other shelf's cover reads \"%s\"",
		      read_back(cover));
		CHECK(strstr(g_log, "thumbnails.libretro.com") != NULL,
		      "libretro was not asked first:\n%s", g_log);
		CHECK(!st.problem[0], "the run reports \"%s\"", st.problem);
	}

	{
		char cmd[400];

		db_shutdown();
		snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", g_root);
	}
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
