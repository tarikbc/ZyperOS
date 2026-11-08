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
 * them. Links src/storage.c and src/gamedel.c (with what gamedel.c links),
 * not SDL.
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
	"", "Roms", "Roms/Game Boy", "Roms/Game Boy/.media", "Roms/Game Boy/.cheevos",
	"Roms/PlayStation", "Saves", ".userdata", ".userdata/shared", ".userdata/shared/.zyperos",
	".userdata/shared/.zyperos/Game Boy", "Music", "Music/Artist", "Music/Artist/Album",
	"Videos", "Audiobooks", "Audiobooks/Book", "Bios", "ZyperOS", "Emulators",
	"Emulators/DraStic", NULL,
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
	put("Emulators/DraStic/drastic", 250000);
}

static st_paths paths(void)
{
	return (st_paths){ root, roms, shared, zyp, false };
}

static const char *SYS_GB = "Game Boy", *SYS_PS = "PlayStation";

static st_systems systems(void)
{
	st_systems s = { { SYS_GB, SYS_PS }, 2 };
	return s;
}

static st_game_in GAMES[3];
static int ngames;

static void games(bool zelda)
{
	ngames = 0;
	GAMES[ngames++] = (st_game_in){ SYS_GB, "GB", "mgba", "gb,gbc,zip", "Tetris.gb", "Tetris", false, 0 };
	if (zelda)
		GAMES[ngames++] = (st_game_in){ SYS_GB, "GB", "mgba", "gb,gbc,zip", "Zelda.gb", "Zelda", false, 0 };
	GAMES[ngames++] = (st_game_in){ SYS_PS, "PS1", "pcsx", "cue,chd", "Game.cue", "Game", false, 0 };
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
	                     on("Roms/Game Boy/.media/._Tetris.png"));
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
	CHECK_EQ(c.emulators, on("Emulators/DraStic/drastic"));
	CHECK_EQ(c.saves, on("Saves/Tetris.srm") + on("Saves/Game.srm"));

	/* A game's bytes are what Delete Game gives back, by part */
	CHECK(st_game(SYS_GB, "Tetris.gb", &t));
	/* its cached achievement set goes with it, and is in its folder */
	CHECK_EQ(t.games, on("Roms/Game Boy/Tetris.gb") + on("Roms/Game Boy/.cheevos/Tetris.set"));
	CHECK_EQ(t.saves, on("Saves/Tetris.srm"));
	CHECK_EQ(t.states, on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.state") +
	                   on(".userdata/shared/.zyperos/Game Boy/Tetris.gb.1.bmp"));
	CHECK_EQ(t.art, on("Roms/Game Boy/.media/Tetris.png") + on("Roms/Game Boy/.media/Tetris.mp4"));
	CHECK_EQ(t.bytes, t.games + t.saves + t.states + t.art);
	CHECK(st_game(SYS_PS, "Game.cue", &g));
	CHECK_EQ(g.games, on("Roms/PlayStation/Game.cue") + on("Roms/PlayStation/Game (Track 1).bin") +
	                  on("Roms/PlayStation/Game (Track 2).bin"));
	CHECK_EQ(g.saves, on("Saves/Game.srm"));

	/* The recommendations: the gone game's cover and snap, never a Mac's
	 * twin, and the gone game's two-year-old states, never today's */
	CHECK_EQ(c.unused_art, on("Roms/Game Boy/.media/Old Game.png") + on("Roms/Game Boy/.media/Old Game.mp4"));
	CHECK_EQ(c.unused_covers, 1);
	CHECK_EQ(c.unused_snaps, 1);
	CHECK_EQ(c.old_states, on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.state") +
	                       on(".userdata/shared/.zyperos/Game Boy/Gone.gb.auto.bmp"));
	CHECK_EQ(c.old_state_games, 1);

	/* The bar: its parts are the sums, and with free space the whole card */
	CHECK_EQ(c.cat[STC_GAMES], gb->games + ps->games);
	CHECK_EQ(c.cat[STC_ART], gb->covers + gb->snaps + ps->covers + ps->snaps);
	CHECK_EQ(c.cat[STC_SAVES], c.saves + c.states);
	CHECK_EQ(c.cat[STC_MEDIA], c.music + c.videos + c.books + c.bgm);
	CHECK_EQ(c.cat[STC_ZYPEROS], c.zyperos + c.emulators + c.partitions);
	for (k = 0; k < STC_COUNT; k++) cats += c.cat[k];
	CHECK_EQ(cats + c.free, c.total);
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

		art = on("Roms/Game Boy/.media/Old Game.png") + on("Roms/Game Boy/.media/Old Game.mp4");
		given = st_delete_unused_art(&pp, &s, GAMES, ngames, &files);
		CHECK_EQ(given, art);
		CHECK_EQ(files, 2);
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
	       "and is never believed when it cannot be read; the recommendations delete exactly their files\n");
	return 0;
}
