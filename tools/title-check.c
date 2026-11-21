/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline title integration: real parser, runner, SQLite and library scan.
 * Only transport, credentials, ROM CRC and image shrinking are stubbed. */
#include "../src/db.h"
#include "../src/library.h"
#include "../src/ssrun.h"
#include "../src/net.h"
#include "../src/ss.h"
#include "../src/artscrape.h"
#include "../src/titletext.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
static char root[] = "/tmp/zyperos-title-XXXXXX", libpath[600], devpath[600];
static const char *reply, *retry;
static char pending[1200], last_url[2048];
static uint32_t crc;
static bool image, cover_ok;
static int requests;

bool ss_signed_in(void) { return true; }
bool ss_auth_query(char *out, size_t n) { snprintf(out, n, "test=1"); return true; }
bool art_rom_crc(const char *dir, const char *stem, const char *exts, uint32_t *out)
{ (void)dir; (void)stem; (void)exts; *out = crc; return crc != 0; }
void art_shrink(const char *path) { (void)path; }
bool net_get_async(const char *url, const char *path, int wait)
{
	(void)wait;
	snprintf(pending, sizeof pending, "%s", path);
	snprintf(last_url, sizeof last_url, "%s", url);
	image = strstr(url, "jeuInfos.php") == NULL;
	requests++;
	return true;
}
int net_async_poll(void)
{
	const char *body = image ? (cover_ok ? "cover" : NULL) : (requests > 1 && retry ? retry : reply);
	FILE *f;
	if (!body) return -1;
	f = fopen(pending, "wb");
	if (!f) return -1;
	fputs(body, f);
	return fclose(f) == 0 ? 1 : -1;
}
void net_async_abort(void) { pending[0] = '\0'; }
int net_async_http(void) { return 200; }
int net_async_exit(void) { return 0; }
int net_async_ms(void) { return 0; }
long net_get_buf(const char *url, const net_field *headers, int nh,
                char *out, size_t n, int wait)
{ (void)url; (void)headers; (void)nh; (void)out; (void)n; (void)wait; return -1; }

static const char GOOD[] = "{\"response\":{\"jeu\":{\"noms\":["
	"{\"region\":\"jp\",\"text\":\"日本語\"},"
	"{\"region\":\"us\",\"text\":\"  Pok\\u00e9mon &amp; Friends (Special) 2 \\n\"}],"
	"\"dates\":[{\"region\":\"us\",\"text\":\"1999\"}]}}}";
static const char WRONG[] = "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"Other Game 3\"}]}}}";
static const char ART[] = "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"Alpha 2\"}],"
	"\"medias\":[{\"type\":\"box-2D\",\"url\":\"https://test/cover\"}]}}}";

static void parse_titles(void)
{
	ss_result r;
	char json[3000], name[1100];
	const char *bad[] = { "{}", "{\"response\":{\"jeu\":null}}", "{\"response\":{\"jeu\":{}", NULL };
	const char *empty[] = { "[]", "[{\"text\":\"\\n\\t \"}]", "[{\"text\":null}]",
		"[{\"text\":\"ZZZ(notgame):#NONGAME\"}]", NULL };
	const char *no_name[] = { "", NULL };
	CHECK(!ss_name_ok("short.zip", no_name, 2));
	CHECK(ss_parse(GOOD, strlen(GOOD), "Pokemon 2.zip", &r));
	CHECK(r.found && r.name_ok && !r.art[0]);
	CHECK(!strcmp(r.meta.title, "Pokémon & Friends (Special) 2"));
	CHECK(!strcmp(r.name, r.meta.title));
	CHECK(ss_parse(WRONG, strlen(WRONG), "Pokemon 2.zip", &r));
	CHECK(!r.name_ok && !r.meta.title[0]);
	for (int i = 0; bad[i]; i++) CHECK(!ss_parse(bad[i], strlen(bad[i]), "short.zip", &r));
	for (int i = 0; empty[i]; i++) {
		snprintf(json, sizeof json, "{\"response\":{\"jeu\":{\"noms\":%s}}}", empty[i]);
		CHECK(ss_parse(json, strlen(json), "short.zip", &r));
		CHECK(!r.name_ok && !r.meta.title[0]);
	}
	/* Unknown region falls back to a usable name; title punctuation survives. */
	snprintf(json, sizeof json, "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"日本語 (特別) \\ud83d\\ude80\"}]}}}");
	CHECK(ss_parse(json, strlen(json), "short.zip", &r));
	CHECK(!strcmp(r.meta.title, "日本語 (特別) 🚀"));
	/* A long raw UTF-8 title stops at a character boundary, not at byte 255. */
	memset(name, 'a', 254); strcpy(name + 254, "é日本語");
	snprintf(json, sizeof json, "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"%s\"}]}}}", name);
	CHECK(ss_parse(json, strlen(json), "short.zip", &r));
	CHECK(strlen(r.meta.title) == 254 && r.name_ok);
	/* Escaped and raw forms have the same cap. */
	strcpy(name + 254, "\\u00e9日本語");
	snprintf(json, sizeof json, "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"%s\"}]}}}", name);
	CHECK(ss_parse(json, strlen(json), "short.zip", &r));
	CHECK(strlen(r.meta.title) == 254);
}

static void upgrade(void)
{
	sqlite3 *raw = NULL;
	sqlite3_stmt *st = NULL;
	game_meta got, m = {0};
	db *d;
	char title[GAME_TITLE_MAX], small[5];
	CHECK(sqlite3_open(libpath, &raw) == SQLITE_OK);
	CHECK(sqlite3_exec(raw,
		"CREATE TABLE settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL);"
		"INSERT INTO settings VALUES('favorite.NES.old.zip','1');"
		"INSERT INTO settings VALUES('sess.1.1.NES'||char(9)||'old.zip','90'||char(9)||'quit');"
		"CREATE TABLE games(folder TEXT NOT NULL,file TEXT NOT NULL,year TEXT,publisher TEXT,"
		"developer TEXT,players TEXT,genres TEXT,esrb TEXT,note TEXT,synopsis TEXT,scraped INTEGER,"
		"PRIMARY KEY(folder,file));"
		"INSERT INTO games VALUES('NES','old.zip','1991','Pub','Dev','2','Action','E','18','Old prose',123);",
		NULL, NULL, NULL) == SQLITE_OK);
	sqlite3_close(raw);
	for (int pass = 0; pass < 2; pass++) {
		d = db_open(libpath, DB_LIBRARY);
		CHECK(d != NULL && db_game_get(d, "NES", "old.zip", &got));
		CHECK(!got.title[0] && !strcmp(got.synopsis, "Old prose") && !strcmp(got.year, "1991"));
		CHECK(db_has(d, "favorite.NES.old.zip") && db_has(d, "sess.1.1.NES\told.zip"));
		CHECK(!db_game_title(d, "NES", "old.zip", title, sizeof title) && !title[0]);
		db_close(d);
	}
	CHECK(sqlite3_open(libpath, &raw) == SQLITE_OK);
	CHECK(sqlite3_prepare_v2(raw, "SELECT scraped FROM games WHERE file='old.zip'", -1, &st, NULL) == SQLITE_OK);
	CHECK(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == 123);
	sqlite3_finalize(st); sqlite3_close(raw);
	d = db_open(libpath, DB_LIBRARY);
	strcpy(m.title, "日本語 é 🚀 (Special)"); strcpy(m.year, "2000");
	CHECK(db_game_set(d, "NES", "old.zip", &m)); db_close(d);
	d = db_open(libpath, DB_LIBRARY);
	CHECK(db_game_get(d, "NES", "old.zip", &got) && !strcmp(got.title, m.title));
	CHECK(db_game_title(d, "NES", "old.zip", small, sizeof small) && !strcmp(small, "日"));
	m.title[0] = '\0'; strcpy(m.synopsis, "Updated prose");
	CHECK(db_game_set(d, "NES", "old.zip", &m));
	CHECK(db_game_get(d, "NES", "old.zip", &got) && !strcmp(got.title, "日本語 é 🚀 (Special)"));
	CHECK(!strcmp(got.synopsis, "Updated prose"));
	CHECK(!db_game_title(d, "SNES", "old.zip", title, sizeof title));
	CHECK(!db_game_title(d, "NES", "missing.zip", title, sizeof title));
	CHECK(!db_game_get(d, "NES", "missing.zip", &got) && !got.title[0]);
	db_close(d);
	d = db_open(devpath, DB_DEVICE);
	CHECK(!db_game_set(d, "NES", "old.zip", &m));
	CHECK(!db_game_title(d, "NES", "old.zip", title, sizeof title));
	db_close(d);
}

static void begin(const char *file, const char *stem, const char *body, const char *next, uint32_t sum)
{
	ss_run_cancel(); reply = body; retry = next; crc = sum; requests = 0; cover_ok = false;
	CHECK(ss_run_begin("NES", file, stem, root, "zip,chd"));
}

static void runner(void)
{
	game_meta m = {0}, got;
	CHECK(db_init(devpath, libpath, NULL));
	begin("short2.zip", "short2", GOOD, NULL, 0);
	CHECK(ss_run_step() == -1); /* no art, so libretro still gets its turn */
	CHECK(db_game_get(db_lib(), "NES", "short2.zip", &got));
	CHECK(!strcmp(got.title, "Pokémon & Friends (Special) 2"));
	strcpy(m.title, "Previous 2"); strcpy(m.synopsis, "Keep me");
	CHECK(db_game_set(db_lib(), "NES", "Game 2.zip", &m));
	begin("Game 2.zip", "Game 2", WRONG, WRONG, 0x1234);
	CHECK(ss_run_step() == 1 && requests == 2 && !strstr(last_url, "&crc="));
	CHECK(db_game_get(db_lib(), "NES", "Game 2.zip", &got) && !strcmp(got.title, "Previous 2"));
	CHECK(ss_run_step() == -1);
	CHECK(db_game_get(db_lib(), "NES", "Game 2.zip", &got) && !strcmp(got.synopsis, "Keep me"));
	begin("Game 2.zip", "Game 2", WRONG, GOOD, 0x1234);
	CHECK(ss_run_step() == 1 && ss_run_step() == -1);
	CHECK(db_game_get(db_lib(), "NES", "Game 2.zip", &got) && !strcmp(got.title, "Pokémon & Friends (Special) 2"));
	begin("Missing 2.zip", "Missing 2", WRONG, NULL, 0);
	CHECK(ss_run_step() == -1 && !db_game_get(db_lib(), "NES", "Missing 2.zip", &got));
	begin("Broken 2.zip", "Broken 2", "{bad", NULL, 0);
	CHECK(ss_run_step() == -1 && !db_game_get(db_lib(), "NES", "Broken 2.zip", &got));
	begin("Cover 2.zip", "Cover 2", ART, NULL, 0);
	CHECK(ss_run_step() == 1);
	CHECK(db_game_get(db_lib(), "NES", "Cover 2.zip", &got) && !strcmp(got.title, "Alpha 2"));
	CHECK(ss_run_step() == -1); /* failed cover keeps the title */
	begin("Cancel 2.zip", "Cancel 2", ART, NULL, 0);
	CHECK(ss_run_step() == 1); ss_run_cancel();
	CHECK(db_game_get(db_lib(), "NES", "Cancel 2.zip", &got) && !strcmp(got.title, "Alpha 2"));
	begin("Game 2/Disc 1.chd", "Game 2", GOOD, NULL, 0);
	CHECK(strstr(last_url, "romnom=Game%202") && !strstr(last_url, "Disc"));
	CHECK(ss_run_step() == -1);
	CHECK(db_game_get(db_lib(), "NES", "Game 2/Disc 1.chd", &got) && got.title[0]);
	begin("Bros. 2/Disc 1.chd", "Bros. 2", WRONG, NULL, 0);
	CHECK(strstr(last_url, "romnom=Bros.%202.chd") != NULL);
	CHECK(ss_run_step() == -1);
	CHECK(!db_game_get(db_lib(), "NES", "Bros. 2/Disc 1.chd", &got));
	ss_run_cancel();
	/* A database failure cannot become a successful metadata-only result. */
	requests = 0; reply = ART; retry = NULL; crc = 0;
	CHECK(ss_run_begin_metadata("NES", "Unsaved 2.zip", "Unsaved 2", root, "zip"));
	db_shutdown();
	CHECK(ss_run_step() == -1 && requests == 1);
	ss_run_cancel();
	CHECK(db_init(devpath, libpath, NULL));
	CHECK(!db_game_get(db_lib(), "NES", "Unsaved 2.zip", &got));
}

static void touch(const char *name)
{
	char path[1200]; FILE *f;
	snprintf(path, sizeof path, "%s/%s", root, name);
	f = fopen(path, "wb"); CHECK(f != NULL); if (f) fclose(f);
}
static void directory(const char *name)
{ char path[1200]; snprintf(path, sizeof path, "%s/%s", root, name); CHECK(mkdir(path, 0700) == 0); }
static void text(const char *name, const char *body)
{
	char path[1200]; FILE *f;
	snprintf(path, sizeof path, "%s/%s", root, name);
	f = fopen(path, "wb"); CHECK(f != NULL); if (f) { fputs(body, f); fclose(f); }
}

static void library(void)
{
	game_list list = {0}; game_meta m = {0}; char text[LIB_NAME]; int i;
	directory("NES"); directory("NES/Set"); directory("NES/Shadow");
	touch("NES/Zeta (USA).zip"); touch("NES/abbrev.zip"); touch("NES/Shadow.zip");
	touch("NES/Shadow/Shadow.chd"); touch("NES/Set/Disc 1.chd"); touch("NES/Set/Disc 2.chd"); touch("NES/Set/Set.m3u");
	strcpy(m.title, "Alpha (Official Edition)");
	CHECK(db_game_set(db_lib(), "NES", "abbrev.zip", &m));
	strcpy(m.title, "Set Display");
	CHECK(db_game_set(db_lib(), "NES", "Set/Set.m3u", &m));
	{	/* Rows db_game_set would never write: a stored title that is not
		 * UTF-8, and another folder's title for a file this one has */
		sqlite3 *raw;
		CHECK(sqlite3_open(libpath, &raw) == SQLITE_OK);
		CHECK(sqlite3_exec(raw, "INSERT OR REPLACE INTO games(folder,file,title) VALUES"
		                   "('NES','Shadow.zip','bad\xc0\xaf'),('SNES','Zeta (USA).zip','Wrong Folder');",
		                   NULL, NULL, NULL) == SQLITE_OK);
		sqlite3_close(raw);
	}
	CHECK(lib_scan(root, "NES", "zip,chd,m3u", &list) && list.count == 4);
	/* The scan reads the folder's titles at once: every entry's title is the
	 * one a lookup of that game alone gives, stored or else the cleaned name */
	for (int k = 0; k < list.count; k++) {
		lib_display_title(db_lib(), "NES", list.items[k].file, list.items[k].name, text, sizeof text);
		CHECK(!strcmp(list.items[k].title, text));
	}
	i = lib_find_entry(&list, NULL, 0, "Shadow.zip");
	CHECK(i >= 0 && !strcmp(list.items[i].title, "Shadow"));
	CHECK(!strcmp(list.items[0].file, "abbrev.zip") && !strcmp(list.items[0].name, "abbrev"));
	CHECK(!strcmp(list.items[0].title, "Alpha (Official Edition)"));
	i = lib_find_entry(&list, NULL, 0, "Set/Set.m3u");
	CHECK(i >= 0 && !strcmp(list.items[i].title, "Set Display") && !strcmp(list.items[i].name, "Set"));
	CHECK(lib_find_entry(&list, NULL, 0, "Shadow/Shadow.chd") == -1);
	i = lib_find_entry(&list, NULL, 0, "Zeta (USA).zip");
	CHECK(i >= 0 && !strcmp(list.items[i].title, "Zeta"));
	strcpy(m.title, "New Name"); CHECK(db_game_set(db_lib(), "NES", "Zeta (USA).zip", &m));
	CHECK(lib_refresh_title(db_lib(), "NES", &list.items[i]));
	CHECK(!strcmp(list.items[i].file, "Zeta (USA).zip") && !strcmp(list.items[i].name, "Zeta (USA)"));
	CHECK(!lib_refresh_title(db_lib(), "NES", &list.items[i]));
	lib_display_title(NULL, "NES", "abbrev.zip", "Fallback (USA)", text, sizeof text);
	CHECK(!strcmp(text, "Fallback"));
	lib_display_title(db_lib(), "SNES", "abbrev.zip", "Fallback (USA)", text, sizeof text);
	CHECK(!strcmp(text, "Fallback"));
	lib_title("日本語 (Japan)", text, 5); CHECK(!strcmp(text, "日"));
	/* No-Intro's trailing article goes back in front, at the end of the
	 * name or before its subtitle; a "The" that is a name's own word stays */
	lib_title("Last Ninja, The (USA)", text, sizeof text);
	CHECK(!strcmp(text, "The Last Ninja"));
	lib_title("Legend of Zelda, The - Link's Awakening (USA, Europe)", text, sizeof text);
	CHECK(!strcmp(text, "The Legend of Zelda - Link's Awakening"));
	lib_title("Boy and His Blob, A - Trouble on Blobolonia (USA)", text, sizeof text);
	CHECK(!strcmp(text, "A Boy and His Blob - Trouble on Blobolonia"));
	lib_title("Ninja, The", text, sizeof text); CHECK(!strcmp(text, "The Ninja"));
	lib_title("Addams Family, An (Proto)", text, sizeof text);
	CHECK(!strcmp(text, "An Addams Family"));
	lib_title("Theme Park (Europe)", text, sizeof text); CHECK(!strcmp(text, "Theme Park"));
	lib_title("Batman - Return of the Joker (USA)", text, sizeof text);
	CHECK(!strcmp(text, "Batman - Return of the Joker"));
	lib_title("Toy Story, The Video Game", text, sizeof text);
	CHECK(!strcmp(text, "Toy Story, The Video Game"));
	title_text("invalid\xc0\xaf", text, sizeof text); CHECK(!text[0]);
	lib_free(&list);

	/* ScummVM's shelf: a game is a folder, launched by its .scummvm hook,
	 * else by its first file (the core detects the game from the folder);
	 * a game's own .bin is data, never a disc to launch; a folder with no
	 * file at all is a folder of games, and with none in it no game. Since
	 * 1.0.5 a folder of loose files elsewhere is a folder of games, to any
	 * depth: each file in it a game of its own, never the folder one game. */
	directory("ScummVM"); directory("ScummVM/Sky"); directory("ScummVM/Queen");
	directory("ScummVM/Empty"); directory("NES/Hacks");
	directory("ScummVM/LucasArts"); directory("ScummVM/LucasArts/Monkey");
	touch("ScummVM/Sky/sky.dnr"); touch("ScummVM/Sky/a.bin"); touch("ScummVM/Sky/sky.scummvm");
	touch("ScummVM/Queen/readme.txt"); touch("ScummVM/Queen/queen.1");
	touch("ScummVM/LucasArts/Monkey/monkey.000"); touch("ScummVM/LucasArts/Monkey/monkey.scummvm");
	touch("NES/Hacks/a.nes"); touch("NES/Hacks/b.nes");
	CHECK(lib_scan(root, "ScummVM", "scummvm", &list) && list.count == 3);
	CHECK(lib_find_entry(&list, NULL, 0, "Sky/sky.scummvm") >= 0);
	i = lib_find_entry(&list, NULL, 0, "Queen/queen.1");
	CHECK(i >= 0 && !strcmp(list.items[i].name, "Queen") && list.items[i].dir == 0);
	i = lib_find_entry(&list, NULL, 0, "LucasArts/Monkey/monkey.scummvm");
	CHECK(i >= 0 && !strcmp(list.items[i].name, "Monkey") && list.items[i].dir == 9);
	CHECK(list.skipped == 1 && !strcmp(list.skipped_eg[0], "Empty/"));
	lib_free(&list);
	CHECK(lib_scan(root, "NES", "zip,chd,m3u,nes", &list) && list.count == 6);
	i = lib_find_entry(&list, NULL, 0, "Hacks/a.nes");
	CHECK(i >= 0 && list.items[i].dir == 5 && !strcmp(list.items[i].name, "a"));
	CHECK(list.skipped == 0);
	lib_free(&list);
	/* A folder of files the shelf does not take is still one thing left out,
	 * said as a folder, not each file in it */
	CHECK(lib_scan(root, "NES", "zip,chd,m3u", &list) && list.count == 4);
	CHECK(list.skipped == 1 && !strcmp(list.skipped_eg[0], "Hacks/"));
	lib_free(&list);
	/* A folder whose name looks like a game file is a folder all the same:
	 * a disc set when it holds one (its image named as it is), never a file
	 * to launch */
	directory("NES/Odd.zip");
	touch("NES/Odd.zip/Odd.chd");
	touch("NES/readme.txt");
	CHECK(lib_scan(root, "NES", "zip,chd,m3u,nes", &list) && list.count == 7);
	i = lib_find_entry(&list, NULL, 0, "Odd.zip/Odd.chd");
	CHECK(i >= 0 && !strcmp(list.items[i].name, "Odd.zip"));
	CHECK(lib_find_entry(&list, NULL, 0, "Odd.zip") == -1);
	CHECK(list.skipped == 1 && !strcmp(list.skipped_eg[0], "readme.txt"));
	lib_free(&list);
}

/* 1.0.5: a disc set's folder against a folder of several games' discs */
static void disc_folders(void)
{
	game_list list = {0};
	int i;

	directory("PS"); directory("PS/FF7"); directory("PS/RPGs"); directory("PS/RPGs/Xeno");
	directory("PS/Pol"); directory("PS/Tracks"); directory("PS/Mixed"); directory("PS/Media");
	directory("PS/Imgs"); directory("PS/box3d"); directory("PS/Snaps"); directory("PS/downloaded_videos");
	/* One game's discs, with no playlist: one game, Disc 1 */
	touch("PS/FF7/Final Fantasy VII (USA) (Disc 2).cue"); touch("PS/FF7/Final Fantasy VII (USA) (Disc 2).bin");
	touch("PS/FF7/Final Fantasy VII (USA) (Disc 1).cue"); touch("PS/FF7/Final Fantasy VII (USA) (Disc 1).bin");
	/* Numbered discs with no game's name at all */
	touch("PS/Pol/Disc 1.chd"); touch("PS/Pol/DISC2.chd");
	/* A sheet and its tracks, each track named its own way */
	touch("PS/Tracks/Game.cue"); touch("PS/Tracks/Game (Track 01).bin"); touch("PS/Tracks/other.bin");
	/* Several games' discs: a folder of them, and a set folder inside */
	touch("PS/RPGs/Chrono Cross (USA) (Disc 1).chd"); touch("PS/RPGs/Chrono Cross (USA) (Disc 2).chd");
	touch("PS/RPGs/Vagrant Story (USA).chd"); touch("PS/RPGs/Parasite Eve (USA).chd");
	touch("PS/RPGs/Legend of Mana (USA).chd");
	touch("PS/RPGs/Xeno/Xenogears (USA) (Disc 1).cue"); touch("PS/RPGs/Xeno/Xenogears (USA) (Disc 1).bin");
	/* A playlist makes a folder one game whatever else is in it */
	touch("PS/Mixed/A.chd"); touch("PS/Mixed/B.chd"); touch("PS/Mixed/Mixed.m3u");
	/* Another frontend's pictures, never games, whatever they are */
	touch("PS/Media/x.chd"); touch("PS/Imgs/y.chd"); touch("PS/box3d/z.chd");
	touch("PS/Snaps/notes.txt"); touch("PS/downloaded_videos/v.mp4");
	CHECK(lib_scan(root, "PS", "cue,chd,m3u", &list) && list.count == 10);
	/* ...and never counted as left out: nobody put games there */
	CHECK(lib_es_media_dir("Box3D") && lib_es_media_dir("downloaded_media") &&
	      !lib_es_media_dir("Rom Hacks") && !lib_es_media_dir("Imgs"));
	i = lib_find_entry(&list, NULL, 0, "FF7/Final Fantasy VII (USA) (Disc 1).cue");
	CHECK(i >= 0 && list.items[i].dir == 0 && !strcmp(list.items[i].name, "FF7"));
	CHECK(lib_find_entry(&list, NULL, 0, "Pol/Disc 1.chd") >= 0);
	CHECK(lib_find_entry(&list, NULL, 0, "Tracks/Game.cue") >= 0);
	CHECK(lib_find_entry(&list, NULL, 0, "Mixed/Mixed.m3u") >= 0);
	i = lib_find_entry(&list, NULL, 0, "RPGs/Chrono Cross (USA) (Disc 1).chd");
	CHECK(i >= 0 && list.items[i].dir == 4 && !strcmp(list.items[i].name, "Chrono Cross (USA) (Disc 1)"));
	CHECK(lib_find_entry(&list, NULL, 0, "RPGs/Vagrant Story (USA).chd") >= 0);
	i = lib_find_entry(&list, NULL, 0, "RPGs/Xeno/Xenogears (USA) (Disc 1).cue");
	CHECK(i >= 0 && list.items[i].dir == 4 && !strcmp(list.items[i].name, "Xeno"));
	CHECK(list.skipped == 0);
	lib_free(&list);

	/* PSP (2026-11-13: a folder of one game opened straight into it): one
	 * image is the folder's own game only when it is named as the folder
	 * is; an EBOOT.PBP's folder is its game, as the PSP keeps one; a folder
	 * of one game's image named otherwise is a folder of games */
	directory("PSP"); directory("PSP/Favorites"); directory("PSP/God of War");
	directory("PSP/Cave Story"); directory("PSP/Homebrew"); directory("PSP/Homebrew/Tyrian");
	directory("PSP/Mixed");
	touch("PSP/Favorites/Ridge Racer (USA).iso");
	touch("PSP/God of War/God of War - Chains of Olympus (USA).iso");
	touch("PSP/Cave Story/EBOOT.PBP"); touch("PSP/Homebrew/Tyrian/EBOOT.PBP");
	touch("PSP/Mixed/Lumines (USA).cso");
	CHECK(lib_scan(root, "PSP", "iso,cso,chd,pbp", &list) && list.count == 5);
	i = lib_find_entry(&list, NULL, 0, "Favorites/Ridge Racer (USA).iso");
	CHECK(i >= 0 && list.items[i].dir == 9 && !strcmp(list.items[i].name, "Ridge Racer (USA)"));
	i = lib_find_entry(&list, NULL, 0, "God of War/God of War - Chains of Olympus (USA).iso");
	CHECK(i >= 0 && list.items[i].dir == 10);
	i = lib_find_entry(&list, NULL, 0, "Cave Story/EBOOT.PBP");
	CHECK(i >= 0 && list.items[i].dir == 0 && !strcmp(list.items[i].name, "Cave Story"));
	i = lib_find_entry(&list, NULL, 0, "Homebrew/Tyrian/EBOOT.PBP");
	CHECK(i >= 0 && list.items[i].dir == 8 && !strcmp(list.items[i].name, "Tyrian"));
	CHECK(lib_find_entry(&list, NULL, 0, "Mixed/Lumines (USA).cso") >= 0);
	lib_free(&list);
	/* ...and named as the folder is, it is the folder's game */
	directory("PSP/Ridge Racer");
	touch("PSP/Ridge Racer/Ridge Racer (USA).iso");
	CHECK(lib_scan(root, "PSP", "iso,cso,chd,pbp", &list) && list.count == 6);
	i = lib_find_entry(&list, NULL, 0, "Ridge Racer/Ridge Racer (USA).iso");
	CHECK(i >= 0 && list.items[i].dir == 0 && !strcmp(list.items[i].name, "Ridge Racer"));
	lib_free(&list);

	/* A Dreamcast .gdi set starts by its sheet, never its track01.bin */
	directory("DC"); directory("DC/Game");
	touch("DC/Game/Game.gdi"); touch("DC/Game/track01.bin"); touch("DC/Game/track03.bin");
	touch("DC/Game/track02.raw");
	CHECK(lib_scan(root, "DC", "chd,cdi,gdi,cue", &list) && list.count == 1);
	CHECK(lib_find_entry(&list, NULL, 0, "Game/Game.gdi") >= 0);
	lib_free(&list);

	/* A shelf that takes no disc: its folder of .bin dumps is a folder of
	 * games, not one game launching the first */
	directory("MD"); directory("MD/Hacks");
	touch("MD/Hacks/one.bin"); touch("MD/Hacks/two.bin");
	CHECK(lib_scan(root, "MD", "md,gen,bin,smd,zip", &list) && list.count == 2);
	i = lib_find_entry(&list, NULL, 0, "Hacks/two.bin");
	CHECK(i >= 0 && list.items[i].dir == 5);
	lib_free(&list);

	/* Ports: a port's folder is its data, never looked into, as before */
	directory("PORTS"); directory("PORTS/game"); directory("PORTS/More");
	touch("PORTS/Game.sh"); touch("PORTS/game/run.sh"); touch("PORTS/More/Other.sh");
	CHECK(lib_scan(root, "PORTS", "sh", &list) && list.count == 1);
	CHECK(lib_find_entry(&list, NULL, 0, "Game.sh") >= 0);
	CHECK(list.skipped == 2);
	lib_free(&list);

	/* The added time is the game's own: a loose game's file, a set's folder,
	 * never the folder of games it is kept in */
	{
		char p[1200];
		struct stat st;

		snprintf(p, sizeof p, "%s/PS/RPGs/Xeno", root);
		CHECK(stat(p, &st) == 0);
		CHECK(lib_added(root, "PS", "RPGs/Xeno/Xenogears (USA) (Disc 1).cue", 4) == (long)st.st_mtime);
		CHECK(lib_added(root, "PS", "FF7/Final Fantasy VII (USA) (Disc 1).cue", 0) > 0);
		CHECK(lib_added(root, "PS", "RPGs/Gone.chd", 4) == 0);
	}
}

/* 2026-11-19 (metior, Davistoa): a game with an .m3u is one entry, its .m3u,
 * and the discs it names are off the list (hide_m3u_discs) */
static void m3u_discs(void)
{
	static const char *const GONE[] = {
		"Final Fantasy VII (USA) (Disc 1).chd", "Final Fantasy VII (USA) (Disc 2).chd",
		"Final Fantasy VII (USA) (Disc 3).chd",
		"Discs/Chrono Cross (USA) (Disc 1).chd",          /* a set folder: the entry Discs */
		"Mixed/Legend of Legaia (USA) (Disc 1).chd",
		"Vagrant Story (USA) (Disc 1).chd", "Vagrant Story (USA) (Disc 2).chd",
		"Parasite Eve (USA) (Disc 1).chd", "Shared (USA).chd", "Legend of Mana (USA).chd", NULL
	};
	static const char *const KEPT[] = {
		"Final Fantasy VII (USA).m3u", "Chrono Cross (USA).m3u", "Legaia.m3u", "Xenogears (USA).m3u",
		"Vagrant Story (USA).m3u", "Parasite Eve (USA).m3u", "Parasite Eve (USA) (Disc 2).chd",
		"Alpha.m3u", "Beta.m3u", "Mana.m3u", "Lone (USA).chd", "Mixed/Tomba (USA).chd",
		"Outside.m3u", "Gran Turismo (USA).chd", NULL
	};
	game_list list = {0};
	int i, n;

	directory("PS1"); directory("PS1/Discs"); directory("PS1/Mixed"); directory("PS1/.hidden");
	/* Relative names, the .m3u beside its discs */
	text("PS1/Final Fantasy VII (USA).m3u", "Final Fantasy VII (USA) (Disc 1).chd\n"
	     "Final Fantasy VII (USA) (Disc 2).chd\nFinal Fantasy VII (USA) (Disc 3).chd\n");
	touch("PS1/Final Fantasy VII (USA) (Disc 1).chd"); touch("PS1/Final Fantasy VII (USA) (Disc 2).chd");
	touch("PS1/Final Fantasy VII (USA) (Disc 3).chd");
	/* Into a folder under it: a set folder (one game's discs) goes whole, a
	 * folder of games keeps what the .m3u does not name; Davistoa's hidden
	 * folder is never read, and its .m3u stays */
	text("PS1/Chrono Cross (USA).m3u", "Discs/Chrono Cross (USA) (Disc 1).chd\n"
	     "Discs/Chrono Cross (USA) (Disc 2).chd\n");
	touch("PS1/Discs/Chrono Cross (USA) (Disc 1).chd"); touch("PS1/Discs/Chrono Cross (USA) (Disc 2).chd");
	text("PS1/Legaia.m3u", "./Mixed/Legend of Legaia (USA) (Disc 1).chd\nMixed\\Legend of Legaia (USA) (Disc 2).chd\n");
	touch("PS1/Mixed/Legend of Legaia (USA) (Disc 1).chd"); touch("PS1/Mixed/Tomba (USA).chd");
	text("PS1/Xenogears (USA).m3u", ".hidden/Xenogears (USA) (Disc 1).chd\n.hidden/Xenogears (USA) (Disc 2).chd\n");
	touch("PS1/.hidden/Xenogears (USA) (Disc 1).chd"); touch("PS1/.hidden/Xenogears (USA) (Disc 2).chd");
	/* CRLF, a byte order mark, comments, blank lines, spaces round a name */
	text("PS1/Vagrant Story (USA).m3u", "\xEF\xBB\xBF#EXTM3U\r\n# two discs\r\n"
	     "Vagrant Story (USA) (Disc 1).chd\r\n\r\n  Vagrant Story (USA) (Disc 2).chd  \r\n");
	touch("PS1/Vagrant Story (USA) (Disc 1).chd"); touch("PS1/Vagrant Story (USA) (Disc 2).chd");
	/* A disc not there: it hides nothing, and the disc it meant stays */
	text("PS1/Parasite Eve (USA).m3u", "Parasite Eve (USA) (Disc 1).chd\nParasite Eve (USA) (Disc 2).cue\n");
	touch("PS1/Parasite Eve (USA) (Disc 1).chd"); touch("PS1/Parasite Eve (USA) (Disc 2).chd");
	/* Two .m3u naming one disc, both kept; a name in another case */
	text("PS1/Alpha.m3u", "Shared (USA).chd\n"); text("PS1/Beta.m3u", "Shared (USA).chd\n");
	touch("PS1/Shared (USA).chd");
	text("PS1/Mana.m3u", "legend of mana (usa).CHD\n"); touch("PS1/Legend of Mana (USA).chd");
	/* Named by no .m3u; and a name leaving the folder hides nothing */
	touch("PS1/Lone (USA).chd");
	text("PS1/Outside.m3u", "../PS1/Gran Turismo (USA).chd\n/abs/Gran Turismo (USA).chd\n");
	touch("PS1/Gran Turismo (USA).chd");

	CHECK(lib_scan(root, "PS1", "cue,chd,pbp,m3u", &list));
	for (i = 0; GONE[i]; i++)
		if (lib_find_entry(&list, NULL, 0, GONE[i]) >= 0) {
			fprintf(stderr, "FAIL: %s is still listed\n", GONE[i]);
			failures++;
		}
	for (i = n = 0; KEPT[i]; i++, n++)
		if (lib_find_entry(&list, NULL, 0, KEPT[i]) < 0) {
			fprintf(stderr, "FAIL: %s is not listed\n", KEPT[i]);
			failures++;
		}
	CHECK(list.count == n);
	CHECK(list.skipped == 0);
	lib_free(&list);

	/* A shelf that takes no .m3u: its .m3u is no game and hides nothing */
	CHECK(lib_scan(root, "PS1", "chd", &list));
	CHECK(lib_find_entry(&list, NULL, 0, "Final Fantasy VII (USA) (Disc 1).chd") >= 0);
	CHECK(lib_find_entry(&list, NULL, 0, "Final Fantasy VII (USA).m3u") < 0);
	lib_free(&list);

	/* Doom's .m3u names WADs, each a game too: none goes */
	directory("DOOM");
	text("DOOM/Sigil.m3u", "doom.wad\nsigil.wad\n");
	touch("DOOM/doom.wad"); touch("DOOM/sigil.wad");
	CHECK(lib_scan(root, "DOOM", "wad,iwad,pwad,m3u", &list) && list.count == 3);
	lib_free(&list);
}

/* 2026-11-21 (DoctorIssa): a game's details show its file whole, the brackets
 * lib_title leaves out of its title, and a set's discs (DESIGN.md 11.69). On
 * the folders disc_folders and m3u_discs made. */
static void file_shown(void)
{
	char out[LIB_PATH + 1];
	game_list list = {0};
	int i;

	/* The name, whole: every bracket the title cuts, less the extension, as
	 * Rename has it */
	lib_file_shown("Final Fantasy VII (USA) (Disc 1).chd", 0, false, out, sizeof out);
	CHECK(!strcmp(out, "Final Fantasy VII (USA) (Disc 1)"));
	lib_title("Final Fantasy VII (USA) (Disc 1)", out, sizeof out);
	CHECK(!strcmp(out, "Final Fantasy VII"));
	/* ...less the folders the shelf only keeps it in */
	lib_file_shown("Rom Hacks/Translations/Mother 3 (English) (v1.3).gba", 22, false, out, sizeof out);
	CHECK(!strcmp(out, "Mother 3 (English) (v1.3)"));
	/* A set in a folder of its own: the folder, which is the game, and the
	 * file it starts by, whichever of the two has the brackets */
	lib_file_shown("FF7/Final Fantasy VII (USA) (Disc 1).cue", 0, false, out, sizeof out);
	CHECK(!strcmp(out, "FF7/Final Fantasy VII (USA) (Disc 1)"));
	lib_file_shown("RPGs/Xeno/Xenogears (USA) (Disc 1).cue", 4, false, out, sizeof out);
	CHECK(!strcmp(out, "Xeno/Xenogears (USA) (Disc 1)"));
	lib_file_shown("God of War/God of War - Chains of Olympus (USA).iso", 0, false, out, sizeof out);
	CHECK(!strcmp(out, "God of War/God of War - Chains of Olympus (USA)"));
	/* A dot in a folder's name is no extension; one leading a name keeps it */
	lib_file_shown("Vol. 2 (Japan)/Disc", 0, false, out, sizeof out);
	CHECK(!strcmp(out, "Vol. 2 (Japan)/Disc"));
	lib_file_shown(".hack", 0, false, out, sizeof out);
	CHECK(!strcmp(out, ".hack"));
	lib_file_shown("Super Mario Bros. 3 (USA) (Rev 1).nes", 0, false, out, sizeof out);
	CHECK(!strcmp(out, "Super Mario Bros. 3 (USA) (Rev 1)"));
	/* A ScummVM game: its folder, never the hook or the file the core is handed */
	lib_file_shown("Flight of the Amazon Queen (CD DOS)/queen.scummvm", 0, true, out, sizeof out);
	CHECK(!strcmp(out, "Flight of the Amazon Queen (CD DOS)"));
	lib_file_shown("Adventures/Soltys/readme.txt", 10, true, out, sizeof out);
	CHECK(!strcmp(out, "Soltys"));
	/* A `dir` that is no run of folders leaves the path whole */
	lib_file_shown("Game (USA).zip", 9, false, out, sizeof out);
	CHECK(!strcmp(out, "Game (USA)"));
	lib_file_shown("Tetris (World) (Rev 1).gb", 0, false, out, 8);
	CHECK(!strcmp(out, "Tetris "));

	/* Discs: a loose game is one, read nothing; a set's folder its images,
	 * a sheet's tracks not counted; an .m3u what it names */
	CHECK(lib_discs(root, "PS", "RPGs/Vagrant Story (USA).chd", 4, false) == 1);
	CHECK(lib_discs(root, "PS", "FF7/Final Fantasy VII (USA) (Disc 1).cue", 0, false) == 2);
	CHECK(lib_discs(root, "PS", "Pol/Disc 1.chd", 0, false) == 2);
	CHECK(lib_discs(root, "PS", "Tracks/Game.cue", 0, false) == 1);
	CHECK(lib_discs(root, "PS", "RPGs/Xeno/Xenogears (USA) (Disc 1).cue", 4, false) == 1);
	CHECK(lib_discs(root, "PS", "Mixed/Mixed.m3u", 0, false) == 1);   /* an empty playlist */
	CHECK(lib_discs(root, "PS1", "Final Fantasy VII (USA).m3u", 0, false) == 3);
	CHECK(lib_discs(root, "PS1", "Chrono Cross (USA).m3u", 0, false) == 2);
	CHECK(lib_discs(root, "PS1", "Vagrant Story (USA).m3u", 0, false) == 2);
	CHECK(lib_discs(root, "PS1", "Mana.m3u", 0, false) == 1);
	CHECK(lib_discs(root, "DC", "Game/Game.gdi", 0, false) == 1);
	CHECK(lib_discs(root, "DOOM", "Sigil.m3u", 0, false) == 1);      /* WADs, no discs */
	CHECK(lib_discs(root, "PSP", "Cave Story/EBOOT.PBP", 0, false) == 1);
	CHECK(lib_discs(root, "PS", "Gone/Disc 1.chd", 0, false) == 1);
	/* A folder with a playlist inside: what the playlist names */
	text("PS/Mixed/Mixed.m3u", "A.chd\nB.chd\n");
	CHECK(lib_discs(root, "PS", "Mixed/Mixed.m3u", 0, false) == 2);
	/* A ScummVM game's folder holds no discs, whatever its data is called */
	directory("SCUMM"); directory("SCUMM/Loom");
	touch("SCUMM/Loom/DISK01.LEC"); touch("SCUMM/Loom/a.img"); touch("SCUMM/Loom/b.img");
	CHECK(lib_discs(root, "SCUMM", "Loom/a.img", 0, true) == 1);

	/* As a scan makes them: a set's entry and a loose disc's */
	CHECK(lib_scan(root, "PS", "cue,chd,m3u", &list));
	i = lib_find_entry(&list, NULL, 0, "FF7/Final Fantasy VII (USA) (Disc 1).cue");
	CHECK(i >= 0);
	if (i >= 0) {
		lib_file_shown(list.items[i].file, list.items[i].dir, false, out, sizeof out);
		CHECK(!strcmp(out, "FF7/Final Fantasy VII (USA) (Disc 1)"));
		CHECK(lib_discs(root, "PS", list.items[i].file, list.items[i].dir, false) == 2);
	}
	i = lib_find_entry(&list, NULL, 0, "RPGs/Chrono Cross (USA) (Disc 1).chd");
	CHECK(i >= 0);
	if (i >= 0) {
		lib_file_shown(list.items[i].file, list.items[i].dir, false, out, sizeof out);
		CHECK(!strcmp(out, "Chrono Cross (USA) (Disc 1)"));
		CHECK(lib_discs(root, "PS", list.items[i].file, list.items[i].dir, false) == 1);
	}
	lib_free(&list);
}

static int rm_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st; (void)flag; (void)ftw;
	return remove(path);
}

int main(void)
{
	CHECK(mkdtemp(root) != NULL);
	snprintf(libpath, sizeof libpath, "%s/library.db", root);
	snprintf(devpath, sizeof devpath, "%s/device.db", root);
	parse_titles(); upgrade(); runner(); library(); disc_folders(); m3u_discs(); file_shown(); db_shutdown();
	/* A unique fixture tree (mkdtemp's), removed whole */
	nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (failures) return 1;
	puts("ok: parsed titles survive upgrades, cover failures and library scans; a game's file is shown whole");
	return 0;
}
