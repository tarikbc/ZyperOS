/* SPDX-License-Identifier: GPL-3.0-only */
/* Real bulk driver, ScreenScraper runner/parser, library and database. Only
 * network, credentials and the image shrinker are stubbed. No live account. */
#include "../src/artscrape.h"
#include "../src/db.h"
#include "../src/library.h"
#include "../src/ss.h"
#include "../src/ssrun.h"
#include "../src/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static char root[] = "/tmp/zyperos-title-bulk-XXXXXX";
static char pending[1600], url[2048];
static int requests, images, shrink_calls, refusals, http;
static bool signed_in = true, no_cover_reply, wrong_reply, quota_reply, video_reply;
/* What the snap's URL answers: an MP4's first bytes, or a page */
static const char *snap_body = "XXXXftypisom-snap";
static const char VIDEO[] = "{\"response\":{\"jeu\":{\"noms\":[{\"region\":\"us\",\"text\":\"Catalog Game 2\"}],"
	"\"medias\":[{\"type\":\"video-normalized\",\"url\":\"https://test/snap.mp4\"}]}}}";
static bool nes_only(const char *folder) { return !strcmp(folder, "NES"); }
static systems_cfg systems;
static const char GOOD[] = "{\"response\":{\"jeu\":{\"noms\":[{\"region\":\"us\",\"text\":\"Catalog Game 2\"}],"
	"\"medias\":[{\"type\":\"box-2D\",\"url\":\"https://test/cover.png\"}]}}}";
static const char NO_COVER[] = "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"Catalog Game 2\"}]}}}";
static const char WRONG[] = "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"Other Game 3\"}]}}}";
static const char QUOTA[] = "{\"response\":{\"ssuser\":{\"requeststoday\":\"19999\",\"maxrequestsperday\":\"20000\"},"
	"\"jeu\":{\"noms\":[{\"text\":\"Catalog Game 2\"}]}}}";

bool ss_signed_in(void) { return signed_in; }
bool ss_auth_query(char *out, size_t n) { snprintf(out, n, "test=1"); return true; }
void art_shrink(const char *path) { (void)path; shrink_calls++; }
long net_get_buf(const char *u, const net_field *fields, int n, char *out, size_t cap, int timeout)
{ (void)u; (void)fields; (void)n; (void)out; (void)cap; (void)timeout; return -1; }
bool net_get_async(const char *u, const char *path, int timeout)
{
	(void)timeout;
	strcpy(url, u); strcpy(pending, path);
	requests++;
	if (!strstr(url, "jeuInfos.php")) images++;
	return true;
}
int net_async_poll(void)
{
	FILE *f;
	const char *body;
	http = 200;
	if (strstr(url, "jeuInfos.php") && refusals > 0) { refusals--; http = 429; return -1; }
	body = strstr(url, "jeuInfos.php")
	     ? (wrong_reply ? WRONG : quota_reply ? QUOTA : no_cover_reply ? NO_COVER :
	        video_reply ? VIDEO : GOOD)
	     : strstr(url, "snap.mp4") ? snap_body : "\x89PNGnew-cover";
	f = fopen(pending, "wb");
	if (!f) return -1;
	fputs(body, f);
	return fclose(f) == 0 ? 1 : -1;
}
void net_async_abort(void) { pending[0] = '\0'; }
int net_async_http(void) { return http; }
int net_async_exit(void) { return 0; }
int net_async_ms(void) { return 0; }

static void write_at(const char *name, const char *body)
{
	char path[1200]; FILE *f;
	snprintf(path, sizeof path, "%s/%s", root, name);
	f = fopen(path, "wb"); CHECK(f != NULL);
	if (f) { fputs(body, f); fclose(f); }
}
static bool matches(const char *name, const char *body)
{
	char path[1200], buf[256]; FILE *f; size_t n;
	snprintf(path, sizeof path, "%s/%s", root, name);
	f = fopen(path, "rb"); if (!f) return false;
	n = fread(buf, 1, sizeof buf - 1, f); buf[n] = '\0'; fclose(f);
	return !strcmp(buf, body);
}
static void mkdir_at(const char *name)
{
	char path[1200]; snprintf(path, sizeof path, "%s/%s", root, name); CHECK(mkdir(path, 0700) == 0);
}
static void fresh_db(void)
{
	db_shutdown(); CHECK(db_init(":memory:", ":memory:", NULL));
	game_meta m = {0}; strcpy(m.title, "Known title");
	CHECK(db_game_set(db_lib(), "NES", "Known.zip", &m));
}
static art_progress run(void)
{
	int result = 1, steps = 0;
	art_progress p;
	requests = images = shrink_calls = 0;
	art_begin(&systems, root, NULL);
	while (result == 1 && steps++ < 1000) result = art_step();
	CHECK(result == 0);
	art_status(&p);
	return p;
}
static void check_covers(void)
{
	CHECK(matches("NES/.media/Covered 2.png", "\x89PNGhand-cover"));
	CHECK(matches("NES/.media/Known.png", "\x89PNGknown-cover"));
	CHECK(matches("NES/.media/Set 2.png", "\x89PNGdisc-cover"));
	CHECK(matches("PICO-8/.media/Tiny 2.png", "\x89PNGtiny-cover"));
}
/* SteamGridDB is the last source and needs a key (src/sgdb.h); with none, as
 * here, artscrape.c never asks it, and these only let it link. */
bool sgdb_have_key(void);
bool sgdb_run_begin(const char *stem, const char *dest);
int  sgdb_run_step(void);
void sgdb_run_cancel(void);
bool sgdb_run_too_many(void);
bool sgdb_run_refused(void);
bool sgdb_run_no_answer(void);
bool sgdb_have_key(void) { return false; }
bool sgdb_run_begin(const char *stem, const char *dest) { (void)stem; (void)dest; return false; }
int  sgdb_run_step(void) { return -1; }
void sgdb_run_cancel(void) { }
bool sgdb_run_too_many(void) { return false; }
bool sgdb_run_refused(void) { return false; }
bool sgdb_run_no_answer(void) { return false; }

int main(void)
{
	art_progress p;
	char title[GAME_TITLE_MAX], path[1200];
	CHECK(mkdtemp(root) != NULL);
	mkdir_at("NES"); mkdir_at("NES/.media"); mkdir_at("NES/Set 2");
	mkdir_at("PICO-8"); mkdir_at("PICO-8/.media");
	write_at("NES/Covered 2.zip", "rom"); write_at("NES/Known.zip", "rom");
	write_at("NES/Set 2/Set.m3u", "Disc 1.chd\n"); write_at("NES/Set 2/Disc 1.chd", "disc");
	write_at("PICO-8/Tiny 2.p8", "rom");
	write_at("NES/.media/Covered 2.png", "\x89PNGhand-cover");
	write_at("NES/.media/Known.png", "\x89PNGknown-cover");
	write_at("NES/.media/Set 2.png", "\x89PNGdisc-cover");
	write_at("PICO-8/.media/Tiny 2.png", "\x89PNGtiny-cover");
	systems.count = 2;
	strcpy(systems.systems[0].folder, "NES"); strcpy(systems.systems[0].exts, "zip,m3u,chd");
	strcpy(systems.systems[1].folder, "PICO-8"); strcpy(systems.systems[1].exts, "p8");

	fresh_db(); p = run();
	CHECK(requests == 3 && images == 0 && shrink_calls == 0);
	CHECK(p.named == 3 && p.found == 0 && p.missing == 0 && !p.problem[0]);
	CHECK(db_game_title(db_lib(), "NES", "Covered 2.zip", title, sizeof title) && !strcmp(title, "Catalog Game 2"));
	CHECK(db_game_title(db_lib(), "NES", "Set 2/Set.m3u", title, sizeof title));
	CHECK(!db_game_title(db_lib(), "NES", "Set 2", title, sizeof title));
	CHECK(db_game_title(db_lib(), "NES", "Known.zip", title, sizeof title) && !strcmp(title, "Known title"));
	check_covers();
	p = run(); CHECK(requests == 0 && p.named == 0 && !p.problem[0]);

	/* A reply without art is still successful metadata, not a cover miss. */
	fresh_db(); no_cover_reply = true; p = run();
	CHECK(requests == 3 && images == 0 && p.named == 3 && p.missing == 0 && !p.problem[0]);
	check_covers(); no_cover_reply = false;

	fresh_db(); wrong_reply = true; p = run();
	CHECK(p.named == 0 && images == 0 && p.missing == 0);
	CHECK(!db_game_title(db_lib(), "NES", "Covered 2.zip", title, sizeof title));
	check_covers(); wrong_reply = false;

	fresh_db(); signed_in = false; p = run();
	CHECK(requests == 0 && p.named == 0); check_covers(); signed_in = true;

	fresh_db(); refusals = 1; p = run();
	CHECK(requests == 4 && p.named == 3 && images == 0 && !p.problem[0]); check_covers();
	fresh_db(); refusals = 9; p = run();
	CHECK(requests == 2 && p.named == 0 && images == 0 && strstr(p.problem, "too many"));
	check_covers(); refusals = 0;

	/* Stop while metadata is in flight. No cover and no title may change. */
	fresh_db(); requests = 0; art_begin(&systems, root, NULL);
	for (int i = 0; i < 100 && !requests; i++) CHECK(art_step() == 1);
	CHECK(requests == 1); art_cancel();
	CHECK(!db_game_title(db_lib(), "NES", "Covered 2.zip", title, sizeof title)); check_covers();

	/* Mixed work still fetches one missing cover, without replacing old art. */
	fresh_db(); write_at("NES/Missing 2.zip", "rom"); p = run();
	CHECK(p.named == 3 && p.found == 1 && p.missing == 0 && images == 1 && shrink_calls == 1);
	CHECK(matches("NES/.media/Missing 2.png", "\x89PNGnew-cover")); check_covers();

	/* Video snaps (src/snaps.h): with the hook on for NES, a game that has
	 * its cover and its title gets the lookup that brings its snap; only an
	 * MP4 is kept; and with the hook off nothing is asked for. */
	{
		const char *snaps[] = { "NES/.media/Covered 2.mp4", "NES/.media/Known.mp4",
		                        "NES/.media/Set 2.mp4", "NES/.media/Missing 2.mp4" };
		game_meta t = {0};

		fresh_db(); strcpy(t.title, "Stored title");
		CHECK(db_game_set(db_lib(), "NES", "Covered 2.zip", &t));
		CHECK(db_game_set(db_lib(), "NES", "Set 2/Set.m3u", &t));
		CHECK(db_game_set(db_lib(), "NES", "Missing 2.zip", &t));
		CHECK(db_game_set(db_lib(), "PICO-8", "Tiny 2.p8", &t));
		ss_run_videos(nes_only); video_reply = true;
		p = run();
		CHECK(p.snaps == 4 && p.named == 0 && requests == 8 && !p.problem[0]);
		for (unsigned i = 0; i < 4; i++) CHECK(matches(snaps[i], "XXXXftypisom-snap"));
		CHECK(!matches("PICO-8/.media/Tiny 2.mp4", "XXXXftypisom-snap"));
		check_covers();
		p = run(); CHECK(requests == 0 && p.snaps == 0);
		snprintf(path, sizeof path, "%s/%s", root, snaps[1]); unlink(path);
		snap_body = "<html>not found</html>";
		p = run(); CHECK(requests == 2 && p.snaps == 0);
		CHECK(!matches(snaps[1], "<html>not found</html>"));
		snprintf(path, sizeof path, "%s/%s.snap", root, snaps[1]);
		CHECK(access(path, F_OK) != 0);
		ss_run_videos(NULL); snap_body = "XXXXftypisom-snap";
		p = run(); CHECK(requests == 0 && p.snaps == 0);
		video_reply = false;
		for (unsigned i = 0; i < 4; i++) { snprintf(path, sizeof path, "%s/%s", root, snaps[i]); unlink(path); }
	}

	/* The quota result survives cancel; run this last. */
	fresh_db(); quota_reply = true; p = run();
	CHECK(requests == 1 && p.named == 1 && images == 0 && strstr(p.problem, "quota")); check_covers();
	/* A complete library makes no request and reports no quota failure. */
	game_meta known = {0}; strcpy(known.title, "Stored title");
	CHECK(db_game_set(db_lib(), "NES", "Covered 2.zip", &known));
	CHECK(db_game_set(db_lib(), "NES", "Set 2/Set.m3u", &known));
	CHECK(db_game_set(db_lib(), "NES", "Missing 2.zip", &known));
	CHECK(db_game_set(db_lib(), "PICO-8", "Tiny 2.p8", &known));
	p = run(); CHECK(requests == 0 && p.named == 0 && !p.problem[0]);

	db_shutdown();
	const char *files[] = { "NES/Covered 2.zip", "NES/Known.zip", "NES/Missing 2.zip", "NES/Set 2/Set.m3u", "NES/Set 2/Disc 1.chd", "PICO-8/Tiny 2.p8", "NES/.media/Covered 2.png", "NES/.media/Known.png", "NES/.media/Set 2.png", "NES/.media/Missing 2.png", "PICO-8/.media/Tiny 2.png" };
	for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) { snprintf(path, sizeof path, "%s/%s", root, files[i]); unlink(path); }
	const char *dirs[] = { "NES/Set 2", "NES/.media", "NES", "PICO-8/.media", "PICO-8" };
	for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) { snprintf(path, sizeof path, "%s/%s", root, dirs[i]); rmdir(path); }
	rmdir(root);
	if (fails) return 1;
	puts("ok: bulk scraping fills missing names without replacing covers or reporting art failures");
	return 0;
}
