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

static void library(void)
{
	game_list list = {0}; game_meta m = {0}; char text[LIB_NAME]; int i;
	directory("NES"); directory("NES/Set"); directory("NES/Shadow");
	touch("NES/Zeta (USA).zip"); touch("NES/abbrev.zip"); touch("NES/Shadow.zip");
	touch("NES/Shadow/Disc 1.chd"); touch("NES/Set/Disc 1.chd"); touch("NES/Set/Disc 2.chd"); touch("NES/Set/Set.m3u");
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
	CHECK(lib_find_entry(&list, NULL, 0, "Shadow/Disc 1.chd") == -1);
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
	 * a disc set when it holds one, never a file to launch */
	directory("NES/Odd.zip");
	touch("NES/Odd.zip/Disc 1.chd");
	touch("NES/readme.txt");
	CHECK(lib_scan(root, "NES", "zip,chd,m3u,nes", &list) && list.count == 7);
	i = lib_find_entry(&list, NULL, 0, "Odd.zip/Disc 1.chd");
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
	directory("PS/Imgs");
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
	touch("PS/Media/x.chd"); touch("PS/Imgs/y.chd");
	CHECK(lib_scan(root, "PS", "cue,chd,m3u", &list) && list.count == 10);
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
	parse_titles(); upgrade(); runner(); library(); disc_folders(); db_shutdown();
	/* A unique fixture tree (mkdtemp's), removed whole */
	nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (failures) return 1;
	puts("ok: parsed titles survive upgrades, cover failures and library scans");
	return 0;
}
