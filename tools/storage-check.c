/* MENU > Storage's numbers, on a card made of small files (src/storage.h).
 *
 *   make check-storage
 *
 * The card holds a Game Boy shelf (Tetris and Zelda, a cover and a video
 * snap each, the cover and the snap of a game no longer there, an
 * achievement set), a PlayStation shelf (a .cue with its two .bin tracks),
 * saves, states (Tetris's, written today; and those of a game long gone,
 * two years old), music, a film, two audiobooks, a BIOS, ZyperOS and an
 * emulator. Every number the page shows is held to the bytes the card's own
 * files hold, read here with lstat, so the sums are the clusters the files
 * take, as statvfs counts them:
 *
 *   - each part: a system's games without its .media, its covers and its
 *     snaps apart, its states, the media roots with their counts;
 *   - each game's bytes are what Delete Game would give back: the .cue's
 *     tracks, the save, the states, the cover and the snap;
 *   - the bar's parts are the sums of those, and with free space they come
 *     to the card's whole size;
 *   - the unused art is the gone game's cover and snap alone, and the old
 *     states the gone game's alone.
 *
 * Then the cache: a second start reads it and lists no folder whose time
 * did not move; a game added lists its own folder again and nothing else;
 * a cache it cannot read is measured again, never believed. And the two
 * recommendations' work deletes exactly those files and nothing beside
 * them.
 *
 * The media pages: Music's and Audiobooks' albums and books in an artist's
 * or an author's folder, a folder of tracks at the top and a track at the
 * top; Videos' films and a series whole; each sized from the walk, the
 * items and what none of them holds (a playlist, a cover beside them) coming
 * to the folder's whole, from the cache as from the card. Deleting one
 * gives back its bytes, takes it out of the numbers at once, and refuses a
 * hidden name, "..", a third level and a link. And X's orders: size, A to
 * Z, Last Played, each falling back to the others so a list never
 * reshuffles, Last Played left out of a list with no such time.
 *
 * Links src/storage.c and src/gamedel.c (with what gamedel.c links), not
 * SDL.
 */
#include "../src/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-storage-check-XXXXXX";
static char roms[512], shared[512], zyp[512];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_EQ(a, b) do { unsigned long long a_ = (a), b_ = (b); if (a_ != b_) { \
	printf("  FAIL %s:%d: %s is %llu, not %llu\n", __FILE__, __LINE__, #a, a_, b_); failures++; } } while (0)

/* A file of `bytes` at `rel` under the card, its folders made */
static void put(const char *rel, size_t bytes)
{
	char p[1200], *s;
	FILE *f;
	size_t i;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	for (s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) {
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	if (!(f = fopen(p, "wb"))) { CHECK(!"a file could be made"); return; }
	for (i = 0; i < bytes; i++) fputc('a' + (int)(i % 26), f);
	fclose(f);
}

static void put_text(const char *rel, const char *text)
{
	char p[1200];
	FILE *f;

	put(rel, 0);
	snprintf(p, sizeof p, "%s/%s", root, rel);
	if ((f = fopen(p, "w"))) { fputs(text, f); fclose(f); }
}

/* What a file takes on the card, as storage.c counts it */
static unsigned long long on(const char *rel)
{
	char p[1200];
	struct stat st;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	if (lstat(p, &st) != 0) return 0;
	return st.st_blocks > 0 ? (unsigned long long)st.st_blocks * 512ull : (unsigned long long)st.st_size;
}

static bool there(const char *rel)
{
	char p[1200];
	struct stat st;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	return lstat(p, &st) == 0;
}

/* One clock for the whole check, so a folder set back twice has one time */
static time_t g_t0;

/* A file's or a folder's time, `ago` seconds before the check began */
static void age(const char *rel, long ago)
{
	char p[1200];
	struct timeval tv[2];

	snprintf(p, sizeof p, "%s/%s", root, rel);
	tv[0].tv_sec = g_t0 - ago;
	tv[0].tv_usec = 0;
	tv[1] = tv[0];
	utimes(p, tv);
}

/* Every folder the walk reads, an hour old: the cache's racy rule lists a
 * folder again while its time is within seconds of its listing */
static const char *const DIRS[] = {
	"", "Roms", "Roms/Game Boy", "Roms/Game Boy/.media", "Roms/Game Boy/.media/.before", "Roms/Game Boy/.cheevos",
	"Roms/PlayStation", "Saves", ".userdata", ".userdata/shared", ".userdata/shared/.zyperos",
	".userdata/shared/.zyperos/Game Boy", "Music", "Music/Artist", "Music/Artist/Album",
	"Videos", "Audiobooks", "Audiobooks/Book", "Bios", "ZyperOS", "Emulators",
	"Emulators/Flycast", "Roms/Ports", "Roms/Ports/stardewvalley",
	"Roms/Ports/stardewvalley/gamedata", "Roms/Ports/.media", "Roms/Ports/celeste",
	"Roms/Ports/PortMaster", "Roms/Ports/PortMaster/libs", "Roms/Ports/gtavc",
	"Roms/Ports/apotris", NULL,
};

static void settle(long ago)
{
	int i;

	for (i = 0; DIRS[i]; i++) age(DIRS[i], ago);
}

static void card(void)
{
	put("Roms/Game Boy/Tetris.gb", 32768);
	put("Roms/Game Boy/.media/Tetris.png", 41000);
	put("Roms/Game Boy/.media/Tetris.mp4", 300000);
	put("Roms/Game Boy/.media/Old Game.png", 52000);
	put("Roms/Game Boy/.media/Old Game.mp4", 260000);
	/* the covers Cover and Video put aside: one a game's, one a gone game's */
	put("Roms/Game Boy/.media/.before/Tetris.png", 41000);
	put("Roms/Game Boy/.media/.before/Old Game.png", 37000);
	put("Roms/Game Boy/.media/._Tetris.png", 4096);          /* a Mac's twin: no cover */
	put("Roms/Game Boy/.cheevos/Tetris.set", 9000);
	put_text("Roms/PlayStation/Game.cue",
	         "FILE \"Game (Track 1).bin\" BINARY\n  TRACK 01 MODE2/2352\n"
	         "FILE \"Game (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n");
	put("Roms/PlayStation/Game (Track 1).bin", 700000);
	put("Roms/PlayStation/Game (Track 2).bin", 120000);
	put("Saves/Tetris.srm", 8192);
	put("Saves/Game.srm", 131072);
	put(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state", 70000);
	put(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.bmp", 30000);
	put(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state", 90000);
	put(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp", 20000);
	age(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state", 2L * 365 * 24 * 3600);
	age(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp", 2L * 365 * 24 * 3600);
	put("Music/Artist/Album/01 One.mp3", 400000);
	put("Music/Artist/Album/02 Two.flac", 900000);
	put("Music/Artist/Album/cover.jpg", 60000);
	put("Videos/Film.mp4", 2000000);
	put("Audiobooks/Book/Chapter 1.mp3", 500000);
	put("Audiobooks/Another.m4b", 300000);
	put("Bios/scph5501.bin", 524288);
	put("ZyperOS/zyperos.elf", 1500000);
	put("Emulators/Flycast/flycast", 250000);
	/* The Ports shelf, as PortMaster lays it out (2026-11-08, Tarik's Pixel
	 * 2): a port by its port.json, one by its GAMEDIR, PortMaster, a port
	 * whose script is gone, a folder of nothing's, and a loose file */
	put_text("Roms/Ports/StardewValley.sh", "#!/bin/bash\nGAMEDIR=/$directory/ports/stardewvalley\n");
	put_text("Roms/Ports/stardewvalley/port.json",
	         "{\"items\": [\"stardewvalley/\", \"StardewValley.sh\"], \"attr\": {\"title\": \"Stardew Valley\"}}");
	put("Roms/Ports/stardewvalley/gamedata/Stardew Valley.dll", 900000);
	put("Roms/Ports/.media/StardewValley.png", 40000);
	put_text("Roms/Ports/Celeste.sh", "#!/bin/bash\nGAMEDIR=\"/$directory/ports/celeste/\"\n");
	put("Roms/Ports/celeste/Celeste.exe", 1200000);
	put("Roms/Ports/PortMaster.sh", 0);
	put("Roms/Ports/PortMaster/libs/mono.squashfs", 1500000);
	put_text("Roms/Ports/gtavc/port.json",
	         "{\"items\": [\"gtavc/\", \"Grand Theft Auto Vice City.sh\"], "
	         "\"attr\": {\"title\": \"Grand Theft Auto: Vice City\"}}");
	put("Roms/Ports/gtavc/data.img", 1600000);
	put("Roms/Ports/apotris/apotris.bin", 140000);
	put("Roms/Ports/gamelist.xml", 3000);
}

static st_paths paths(void)
{
	return (st_paths){ root, roms, shared, zyp, false };
}

static const char *SYS_GB = "Game Boy", *SYS_PS = "PlayStation", *SYS_PRT = "Ports";

static st_systems systems(void)
{
	st_systems s = { { SYS_GB, SYS_PS, SYS_PRT }, 3, SYS_PRT };
	return s;
}

static st_game_in GAMES[8];
static int ngames;

static void games(bool zelda)
{
	ngames = 0;
	GAMES[ngames++] = (st_game_in){ SYS_GB, "GB", "mgba", "gb,gbc,zip", "Tetris.gb", "Tetris", false, 0 };
	if (zelda)
		GAMES[ngames++] = (st_game_in){ SYS_GB, "GB", "mgba", "gb,gbc,zip", "Zelda.gb", "Zelda", false, 0 };
	GAMES[ngames++] = (st_game_in){ SYS_PS, "PS1", "pcsx", "cue,chd", "Game.cue", "Game", false, 0 };
	GAMES[ngames++] = (st_game_in){ SYS_PRT, "PRT", "port", "sh", "Celeste.sh", "Celeste", false, 0 };
	GAMES[ngames++] = (st_game_in){ SYS_PRT, "PRT", "port", "sh", "PortMaster.sh", "PortMaster", false, 0 };
	GAMES[ngames++] = (st_game_in){ SYS_PRT, "PRT", "port", "sh", "StardewValley.sh", "StardewValley", false, 0 };
}

static const st_sys *sys_of(const st_card *c, const char *folder)
{
	int i;

	for (i = 0; i < c->nsys; i++)
		if (!strcmp(c->sys[i].folder, folder)) return &c->sys[i];
	return NULL;
}

static void measure_now(bool cache)
{
	st_paths p = paths();
	st_systems s = systems();

	st_measure_now(&p, &s, GAMES, ngames, (long)time(NULL), cache);
}

/* Everything the first walk must say, in one place, for each start */
static void numbers(const char *when)
{
	st_card c;
	const st_sys *gb, *ps;
	st_game_size t, g;
	unsigned long long cats = 0;
	int k;

	printf("%s\n", when);
	st_snapshot(&c);
	CHECK(c.measured);
	CHECK(!c.walking);
	gb = sys_of(&c, SYS_GB);
	ps = sys_of(&c, SYS_PS);
	CHECK(gb && ps);
	if (!gb || !ps) return;

	/* A system's parts: its games without .media (the set is with them),
	 * its covers and snaps apart, its states */
	CHECK_EQ(gb->games, on("Roms/Game Boy/Tetris.gb") + on("Roms/Game Boy/.cheevos/Tetris.set"));
	CHECK_EQ(gb->covers, on("Roms/Game Boy/.media/Tetris.png") + on("Roms/Game Boy/.media/Old Game.png") +
	                     on("Roms/Game Boy/.media/._Tetris.png") + on("Roms/Game Boy/.media/.before/Tetris.png") +
	                     on("Roms/Game Boy/.media/.before/Old Game.png"));
	CHECK_EQ(gb->snaps, on("Roms/Game Boy/.media/Tetris.mp4") + on("Roms/Game Boy/.media/Old Game.mp4"));
	CHECK_EQ(gb->states, on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state") +
	                     on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.bmp") +
	                     on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state") +
	                     on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp"));
	CHECK_EQ(gb->saves, on("Saves/Tetris.srm"));
	CHECK_EQ(ps->games, on("Roms/PlayStation/Game.cue") + on("Roms/PlayStation/Game (Track 1).bin") +
	                    on("Roms/PlayStation/Game (Track 2).bin"));
	CHECK_EQ(ps->saves, on("Saves/Game.srm"));

	/* The roots, and the counts their captions say */
	CHECK_EQ(c.music, on("Music/Artist/Album/01 One.mp3") + on("Music/Artist/Album/02 Two.flac") +
	                  on("Music/Artist/Album/cover.jpg"));
	CHECK_EQ(c.nmusic, 2);
	CHECK_EQ(c.videos, on("Videos/Film.mp4"));
	CHECK_EQ(c.nvideos, 1);
	CHECK_EQ(c.books, on("Audiobooks/Book/Chapter 1.mp3") + on("Audiobooks/Another.m4b"));
	CHECK_EQ(c.nbooks, 2);
	CHECK_EQ(c.bios, on("Bios/scph5501.bin"));
	CHECK_EQ(c.zyperos, on("ZyperOS/zyperos.elf"));
	CHECK_EQ(c.emulators, on("Emulators/Flycast/flycast"));
	CHECK_EQ(c.saves, on("Saves/Tetris.srm") + on("Saves/Game.srm"));

	/* A game's bytes are what Delete Game gives back, by part */
	CHECK(st_game(SYS_GB, "Tetris.gb", &t));
	/* its cached achievement set goes with it, and is in its folder */
	CHECK_EQ(t.games, on("Roms/Game Boy/Tetris.gb") + on("Roms/Game Boy/.cheevos/Tetris.set"));
	CHECK_EQ(t.saves, on("Saves/Tetris.srm"));
	CHECK_EQ(t.states, on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state") +
	                   on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.bmp"));
	/* its cover, its video, and the cover Cover and Video put aside */
	CHECK_EQ(t.art, on("Roms/Game Boy/.media/Tetris.png") + on("Roms/Game Boy/.media/Tetris.mp4") +
	                on("Roms/Game Boy/.media/.before/Tetris.png"));
	CHECK_EQ(t.bytes, t.games + t.saves + t.states + t.art);
	CHECK(st_game(SYS_PS, "Game.cue", &g));
	CHECK_EQ(g.games, on("Roms/PlayStation/Game.cue") + on("Roms/PlayStation/Game (Track 1).bin") +
	                  on("Roms/PlayStation/Game (Track 2).bin"));
	CHECK_EQ(g.saves, on("Saves/Game.srm"));

	/* The Ports shelf: each port its script and its own folders and cover,
	 * PortMaster its script and its folder, the folders no script names
	 * rows of their own, and the rows with the loose file come to the
	 * shelf's whole */
	{
		const st_sys *pr = sys_of(&c, SYS_PRT);
		st_game_size sv, ce, pm;
		st_orphan o[8];
		int no = st_orphans(o, 8), i;
		unsigned long long rows = 0;

		CHECK(pr != NULL);
		CHECK(st_game(SYS_PRT, "StardewValley.sh", &sv));
		CHECK_EQ(sv.bytes, on("Roms/Ports/StardewValley.sh") + on("Roms/Ports/stardewvalley/port.json") +
		                   on("Roms/Ports/stardewvalley/gamedata/Stardew Valley.dll") +
		                   on("Roms/Ports/.media/StardewValley.png"));
		CHECK(st_game(SYS_PRT, "Celeste.sh", &ce));
		CHECK_EQ(ce.bytes, on("Roms/Ports/Celeste.sh") + on("Roms/Ports/celeste/Celeste.exe"));
		CHECK(st_game(SYS_PRT, "PortMaster.sh", &pm));
		CHECK_EQ(pm.bytes, on("Roms/Ports/PortMaster.sh") + on("Roms/Ports/PortMaster/libs/mono.squashfs"));
		CHECK_EQ(no, 2);
		for (i = 0; i < no; i++) {
			if (!strcmp(o[i].folder, "gtavc")) {
				CHECK(!strcmp(o[i].title, "Grand Theft Auto: Vice City"));
				CHECK_EQ(o[i].bytes, on("Roms/Ports/gtavc/port.json") + on("Roms/Ports/gtavc/data.img"));
			} else {
				CHECK(!strcmp(o[i].folder, "apotris"));
				CHECK(!strcmp(o[i].title, ""));
				CHECK_EQ(o[i].bytes, on("Roms/Ports/apotris/apotris.bin"));
			}
			rows += o[i].bytes;
		}
		rows += sv.bytes + ce.bytes + pm.bytes + on("Roms/Ports/gamelist.xml");
		if (pr) CHECK_EQ(rows, pr->games + pr->covers + pr->snaps + pr->states + pr->saves);
	}

	/* The recommendations: the gone game's cover and snap, never a Mac's
	 * twin, and the gone game's two-year-old states, never today's */
	CHECK_EQ(c.unused_art, on("Roms/Game Boy/.media/Old Game.png") + on("Roms/Game Boy/.media/Old Game.mp4") +
	                       on("Roms/Game Boy/.media/.before/Old Game.png"));
	CHECK_EQ(c.unused_covers, 2);
	CHECK_EQ(c.unused_snaps, 1);
	CHECK_EQ(c.old_states, on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state") +
	                       on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp"));
	CHECK_EQ(c.old_state_games, 1);

	/* The bar: its parts are the sums, and with free space the whole card */
	{
		const st_sys *pr = sys_of(&c, SYS_PRT);

		CHECK_EQ(c.cat[STC_GAMES], gb->games + ps->games + (pr ? pr->games : 0));
		CHECK_EQ(c.cat[STC_ART], gb->covers + gb->snaps + ps->covers + ps->snaps +
		                         (pr ? pr->covers + pr->snaps : 0));
	}
	CHECK_EQ(c.cat[STC_SAVES], c.saves + c.states);
	CHECK_EQ(c.cat[STC_MEDIA], c.music + c.videos + c.books + c.bgm);
	CHECK_EQ(c.cat[STC_ZYPEROS], c.zyperos + c.emulators + c.partitions);
	for (k = 0; k < STC_COUNT; k++) cats += c.cat[k];
	CHECK_EQ(cats + c.free, c.total);
}

/* A media page's item by its path under the root, NULL for none */
static const st_media *item(const st_media *v, int n, const char *rel)
{
	int i;

	for (i = 0; i < n; i++)
		if (!strcmp(v[i].rel, rel)) return &v[i];
	return NULL;
}

static unsigned long long items_bytes(const st_media *v, int n)
{
	unsigned long long b = 0;
	int i;

	for (i = 0; i < n; i++) b += v[i].bytes;
	return b;
}

/* Music's, Videos' and Audiobooks' items as their pages list them */
static void media_items_say(const char *when)
{
	st_card c;
	st_media *m;
	const st_media *it;
	int n;

	printf("%s\n", when);
	st_snapshot(&c);

	/* Music: the artist's album, a folder of tracks at the top, a track at
	 * the top; the playlist, the artist's picture and the covers' .media
	 * are the rest */
	n = st_media_items(STM_MUSIC, &m);
	CHECK_EQ(n, 3);
	it = item(m, n, "Artist/Album");
	CHECK(it && it->folder && !strcmp(it->name, "Album") && !strcmp(it->parent, "Artist"));
	if (it) {
		CHECK_EQ(it->bytes, on("Music/Artist/Album/01 One.mp3") + on("Music/Artist/Album/02 Two.flac") +
		                    on("Music/Artist/Album/cover.jpg"));
		CHECK_EQ(it->count, 2);
	}
	it = item(m, n, "Compilation");
	CHECK(it && it->folder && !it->parent[0] && it->count == 1);
	if (it) CHECK_EQ(it->bytes, on("Music/Compilation/01 A.mp3"));
	it = item(m, n, "Loose Song.mp3");
	CHECK(it && !it->folder && !strcmp(it->name, "Loose Song"));
	if (it) CHECK_EQ(it->bytes, on("Music/Loose Song.mp3"));
	CHECK_EQ(items_bytes(m, n) + on("Music/playlist.m3u") + on("Music/Artist/artist.jpg") +
	         on("Music/.media/Compilation.jpg"), c.music);
	free(m);

	/* Videos: a film, and a series whole */
	n = st_media_items(STM_VIDEOS, &m);
	CHECK_EQ(n, 2);
	it = item(m, n, "Film.mp4");
	CHECK(it && !it->folder && !strcmp(it->name, "Film"));
	it = item(m, n, "Series");
	CHECK(it && it->folder && it->count == 2);
	if (it) CHECK_EQ(it->bytes, on("Videos/Series/Ep 1.mp4") + on("Videos/Series/Ep 2.mkv"));
	CHECK_EQ(items_bytes(m, n) + on("Videos/notes.txt"), c.videos);
	free(m);

	/* Audiobooks: a book at the top, a file at the top, and an author's */
	n = st_media_items(STM_BOOKS, &m);
	CHECK_EQ(n, 3);
	CHECK(item(m, n, "Book") && item(m, n, "Another.m4b"));
	it = item(m, n, "Author/Book Two");
	CHECK(it && !strcmp(it->parent, "Author"));
	CHECK_EQ(items_bytes(m, n), c.books);
	free(m);

	/* The menu's own tracks, counted beside Music */
	CHECK_EQ(c.bgm, on("Roms/BGM/Theme.ogg"));
	CHECK_EQ(c.nbgm, 1);
}

static void media_pages(void)
{
	st_paths pp = paths();
	st_card was, c;
	st_media *m;
	unsigned long long want;
	int n;
	char lnk[1200];

	put("Music/Compilation/01 A.mp3", 200000);
	put("Music/Loose Song.mp3", 150000);
	put("Music/playlist.m3u", 5000);
	put("Music/Artist/artist.jpg", 30000);
	put("Music/.media/Compilation.jpg", 20000);
	put("Videos/Series/Ep 1.mp4", 800000);
	put("Videos/Series/Ep 2.mkv", 700000);
	put("Videos/notes.txt", 1000);
	put("Audiobooks/Author/Book Two/Part 1.mp3", 250000);
	put("Roms/BGM/Theme.ogg", 100000);
	snprintf(lnk, sizeof lnk, "%s/Music/Link", root);
	if (symlink("../Roms", lnk) != 0) CHECK(!"a link could be made");
	games(false);
	st_reset();
	measure_now(true);
	media_items_say("the media pages: albums, books and films by folder, and the rest beside them");
	st_reset();
	measure_now(true);
	media_items_say("and the same from a second start");

	printf("deleting a media item gives back its bytes and takes it out of the numbers at once\n");
	st_snapshot(&was);
	want = on("Music/Compilation/01 A.mp3");
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Compilation"), want);
	CHECK(!there("Music/Compilation"));
	st_snapshot(&c);
	CHECK_EQ(c.music, was.music - want);
	CHECK_EQ(c.free, was.free + want);
	CHECK(c.gen != was.gen);
	n = st_media_items(STM_MUSIC, &m);
	CHECK_EQ(n, 2);
	CHECK(!item(m, n, "Compilation"));
	free(m);
	want = on("Music/Loose Song.mp3");
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Loose Song.mp3"), want);
	CHECK(!there("Music/Loose Song.mp3"));
	want = on("Audiobooks/Author/Book Two/Part 1.mp3");
	CHECK_EQ(st_delete_media(&pp, STM_BOOKS, "Author/Book Two"), want);
	CHECK(!there("Audiobooks/Author/Book Two") && there("Audiobooks/Author"));

	printf("and refuses a hidden name, \"..\", a third level, a link and a path out of its root\n");
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, ".media"), 0);
	CHECK(there("Music/.media/Compilation.jpg"));
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, ".."), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "../Roms"), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Artist/.."), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Artist/"), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "/Artist"), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Artist/Album/01 One.mp3"), 0);
	CHECK(there("Music/Artist/Album/01 One.mp3"));
	/* A real folder that is no item of the walk (the artist above its
	 * albums): what a path cut short would name. Only listed items go */
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Artist"), 0);
	CHECK(there("Music/Artist/Album/01 One.mp3"));
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Link"), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Link/Game Boy"), 0);
	CHECK(there("Roms/Game Boy/Tetris.gb"));
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, ""), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, NULL), 0);
	CHECK_EQ(st_delete_media(&pp, STM_MUSIC, "Not There"), 0);
}

/* X's orders */
static bool order_is(const st_item *v, int n, const char *want)
{
	char got[64] = "";
	int i;

	for (i = 0; i < n; i++) strncat(got, v[i].title, 1);
	if (strcmp(got, want)) printf("  (the order is %s, not %s)\n", got, want);
	return !strcmp(got, want);
}

static void orders(void)
{
	static const st_item ROWS[] = {
		{ 1, 0, 300, true, "beta", 0 },
		{ 1, 1, 100, true, "Alpha", 200 },
		{ 1, 2, 300, true, "gamma", 100 },
		{ 1, 3, 0, false, "delta", 0 },
		{ 1, 4, 50, true, "Epsilon", 0 },
	};
	st_item v[5], *big;
	int i, n = 3000;
	bool ok = true;

	printf("X's orders: by size, A to Z and Last Played, each with the others to break a tie\n");
	memcpy(v, ROWS, sizeof v);
	CHECK(st_sort_items(v, 5, ST_SORT_SIZE));
	CHECK(order_is(v, 5, "bgAEd"));
	memcpy(v, ROWS, sizeof v);
	CHECK(st_sort_items(v, 5, ST_SORT_AZ));
	CHECK(order_is(v, 5, "AbdEg"));
	memcpy(v, ROWS, sizeof v);
	CHECK(st_sort_items(v, 5, ST_SORT_LAST));
	CHECK(order_is(v, 5, "AgbEd"));
	/* Sorting a sorted list again moves nothing */
	CHECK(st_sort_items(v, 5, ST_SORT_LAST));
	CHECK(order_is(v, 5, "AgbEd"));
	/* Rows alike in all but their place keep it */
	for (i = 0; i < 5; i++) v[i] = (st_item){ 1, 4 - i, 10, true, "same", 0 };
	CHECK(st_sort_items(v, 5, ST_SORT_AZ));
	for (i = 0; i < 5; i++) CHECK_EQ(v[i].index, i);
	CHECK(st_sort_items(NULL, 0, ST_SORT_SIZE));

	printf("three thousand rows come out in order\n");
	if (!(big = malloc((size_t)n * sizeof *big))) { CHECK(!"memory"); return; }
	for (i = 0; i < n; i++) big[i] = (st_item){ 1, i, (unsigned long long)((i * 7919) % 2999), true, "x", 0 };
	CHECK(st_sort_items(big, n, ST_SORT_SIZE));
	for (i = 1; i < n; i++)
		if (big[i - 1].bytes < big[i].bytes) ok = false;
	CHECK(ok);
	free(big);

	printf("X steps Size, A to Z, Last Played and round, without Last Played where there is none\n");
	CHECK_EQ(st_sort_next(ST_SORT_SIZE, true), ST_SORT_AZ);
	CHECK_EQ(st_sort_next(ST_SORT_AZ, true), ST_SORT_LAST);
	CHECK_EQ(st_sort_next(ST_SORT_LAST, true), ST_SORT_SIZE);
	CHECK_EQ(st_sort_next(ST_SORT_SIZE, false), ST_SORT_AZ);
	CHECK_EQ(st_sort_next(ST_SORT_AZ, false), ST_SORT_SIZE);
	CHECK_EQ(st_sort_next((st_sort)7, true), ST_SORT_SIZE);
}

int main(void)
{
	st_card c;
	FILE *f;
	char p[1200];
	unsigned long long art, old, given;
	int files, gone_games;
	bool video = false;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	g_t0 = time(NULL);
	snprintf(roms, sizeof roms, "%s/Roms", root);
	snprintf(shared, sizeof shared, "%s/.userdata/shared", root);
	snprintf(zyp, sizeof zyp, "%s/ZyperOS", root);
	card();
	settle(3600);
	games(false);

	printf("names a size as cards are sold, and knows a cover's and a snap's suffixes\n");
	CHECK_EQ(st_nominal(127900000000ull), 128000000000ull);
	CHECK_EQ(st_nominal(124000000000ull), 128000000000ull);
	CHECK_EQ(st_nominal(100000000000ull), 100000000000ull);
	CHECK_EQ(st_nominal(31900000000ull), 32000000000ull);
	CHECK_EQ(st_nominal(998000000000ull), 1000000000000ull);
	CHECK_EQ(st_media_suffix("Tetris.png", &video), 4);
	CHECK(!video);
	CHECK_EQ(st_media_suffix("Tetris.mp4.part", &video), 9);
	CHECK(video);
	CHECK_EQ(st_media_suffix("Tetris.png.sgdb", NULL), 9);
	CHECK_EQ(st_media_suffix("notes.txt", NULL), 0);
	CHECK_EQ(st_media_suffix(".png", NULL), 0);

	measure_now(true);
	numbers("the first walk counts every part from the card's own files");
	CHECK(st_listed() > 0);
	snprintf(p, sizeof p, "%s/storage.cache", shared);
	CHECK(access(p, R_OK) == 0);

	/* A second start: the cache, and no folder listed again */
	st_reset();
	measure_now(true);
	numbers("a second start reads the cache and says the same");
	if (st_listed() != 0) printf("  (it listed %d folders)\n", st_listed());
	CHECK_EQ(st_listed(), 0);

	/* A game added: its folder alone listed again, and the game sized */
	printf("a game added lists its own folder again and nothing else\n");
	put("Roms/Game Boy/Zelda.gb", 65536);
	settle(3600);
	age("Roms/Game Boy", 1800);
	games(true);
	measure_now(true);
	CHECK_EQ(st_listed(), 1);
	st_snapshot(&c);
	CHECK(sys_of(&c, SYS_GB) && sys_of(&c, SYS_GB)->games ==
	      on("Roms/Game Boy/Tetris.gb") + on("Roms/Game Boy/Zelda.gb") + on("Roms/Game Boy/.cheevos/Tetris.set"));
	{
		st_game_size z;

		CHECK(st_game(SYS_GB, "Zelda.gb", &z) && z.games == on("Roms/Game Boy/Zelda.gb"));
	}

	/* The same walk on its thread, as the page starts it: it ends on its own,
	 * says the same, and a stop asked of it mid-walk returns at once and
	 * leaves a cache the next start still reads */
	printf("the walk on its thread says the same, and stops when asked\n");
	{
		st_paths pp = paths();
		st_systems s = systems();
		int waited;

		st_reset();
		st_refresh(&pp, &s, GAMES, ngames, (long)time(NULL));
		for (waited = 0; waited < 1000; waited++) {
			st_snapshot(&c);
			if (c.measured && !c.walking) break;
			usleep(10000);
		}
		CHECK(c.measured && !c.walking);
		st_stop();
		st_snapshot(&c);
		CHECK(sys_of(&c, SYS_GB) && sys_of(&c, SYS_GB)->games ==
		      on("Roms/Game Boy/Tetris.gb") + on("Roms/Game Boy/Zelda.gb") + on("Roms/Game Boy/.cheevos/Tetris.set"));
		st_reset();
		st_refresh(&pp, &s, GAMES, ngames, (long)time(NULL));
		st_stop();
		st_stop();      /* and a second stop is nothing */
		st_reset();
		measure_now(true);
		st_snapshot(&c);
		CHECK(c.measured);
	}

	/* A cache it cannot read is measured again, not believed */
	printf("a cache it cannot read is measured again, never believed\n");
	if ((f = fopen(p, "w"))) { fputs("zyperos-storage 1\nD nonsense\n", f); fclose(f); }
	st_reset();
	games(false);
	{
		char z[1200];

		snprintf(z, sizeof z, "%s/Roms/Game Boy/Zelda.gb", root);
		unlink(z);
	}
	settle(3600);
	measure_now(true);
	CHECK(st_listed() > 0);
	numbers("and the numbers are the card's again");

	/* The recommendations' work: exactly their files */
	printf("Unused Art deletes the gone game's cover and snap, and nothing beside them\n");
	{
		st_paths pp = paths();
		st_systems s = systems();

		art = on("Roms/Game Boy/.media/Old Game.png") + on("Roms/Game Boy/.media/Old Game.mp4") +
		      on("Roms/Game Boy/.media/.before/Old Game.png");
		given = st_delete_unused_art(&pp, &s, GAMES, ngames, &files);
		CHECK_EQ(given, art);
		CHECK_EQ(files, 3);
		CHECK(!there("Roms/Game Boy/.media/.before/Old Game.png"));
		CHECK(there("Roms/Game Boy/.media/.before/Tetris.png"));
		CHECK(!there("Roms/Game Boy/.media/Old Game.png"));
		CHECK(!there("Roms/Game Boy/.media/Old Game.mp4"));
		CHECK(there("Roms/Game Boy/.media/Tetris.png"));
		CHECK(there("Roms/Game Boy/.media/Tetris.mp4"));
		CHECK(there("Roms/Game Boy/.media/._Tetris.png"));

		printf("Old Save States deletes the gone game's states, never today's, never a save\n");
		old = on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state") +
		      on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp");
		given = st_delete_old_states(&pp, &s, GAMES, ngames, (long)time(NULL), &gone_games);
		CHECK_EQ(given, old);
		CHECK_EQ(gone_games, 1);
		CHECK(!there(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state"));
		CHECK(!there(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp"));
		CHECK(there(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state"));
		CHECK(there("Saves/Tetris.srm"));

		/* A game played this year keeps its old states */
		printf("states older than a year stay when their game was played this year\n");
		put(".userdata/shared/.zyperos/Game Boy/Tetris.gb.2.state", 50000);
		age(".userdata/shared/.zyperos/Game Boy/Tetris.gb.2.state", 2L * 365 * 24 * 3600);
		age(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state", 2L * 365 * 24 * 3600);
		age(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.bmp", 2L * 365 * 24 * 3600);
		GAMES[0].last_played = (long)time(NULL) - 30L * 24 * 3600;
		given = st_delete_old_states(&pp, &s, GAMES, ngames, (long)time(NULL), &gone_games);
		CHECK_EQ(given, 0);
		CHECK(there(".userdata/shared/.zyperos/Game Boy/Tetris.gb.2.state"));
		/* ...and go once it has not been */
		GAMES[0].last_played = (long)time(NULL) - 400L * 24 * 3600;
		given = st_delete_old_states(&pp, &s, GAMES, ngames, (long)time(NULL), &gone_games);
		CHECK(given > 0);
		CHECK(!there(".userdata/shared/.zyperos/Game Boy/Tetris.gb.2.state"));
		CHECK(there("Saves/Tetris.srm"));
	}

	printf("a port folder whose script is gone is deleted whole; a port's own and PortMaster's never\n");
	{
		st_paths pp = paths();
		st_systems s = systems();
		unsigned long long want = on("Roms/Ports/gtavc/port.json") + on("Roms/Ports/gtavc/data.img");

		games(false);
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "gtavc"), want);
		CHECK(!there("Roms/Ports/gtavc"));
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "PortMaster"), 0);
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "PortMaster."), 0);
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "portmaster"), 0);
		CHECK(there("Roms/Ports/PortMaster/libs/mono.squashfs"));
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "stardewvalley"), 0);
		CHECK(there("Roms/Ports/stardewvalley/gamedata/Stardew Valley.dll"));
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "celeste"), 0);
		CHECK_EQ(st_delete_port_folder(&pp, &s, GAMES, ngames, "../Game Boy"), 0);
		CHECK(there("Roms/Game Boy/Tetris.gb"));
	}

	media_pages();
	orders();

	{
		char cmd[1200];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) {
		printf("storage: %d FAILED\n", failures);
		return 1;
	}
	printf("ok: every part, every game and the bar are the card's own bytes; the cache lists only what moved\n"
	       "and is never believed when it cannot be read; the recommendations delete exactly their files;\n"
	       "the media pages' items come to their folders and delete only themselves; X's orders hold\n");
	return 0;
}
