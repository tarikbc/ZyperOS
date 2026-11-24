/* The zipped DS games DSperate unpacked into .dsperate (src/dscache.h).
 *
 *   make check-dscache
 *
 * Three parts, on a scratch card of small (sparse) files:
 *
 *   - where DSperate unpacks a zip: <its folder>/.dsperate/<zip stem>.nds,
 *     a folder of games' zip into that folder's own .dsperate, and nothing
 *     for a loose .nds or another kind of archive;
 *   - the cap after a DS game: the least recently launched images go first
 *     (by their tag's time, else their own), each with its tag, until the
 *     rest fit; the game just played stays even when it alone is over the
 *     cap; other files in the folder are not touched; a folder under the
 *     cap, or not there, is left as it is;
 *   - Delete Game: a zipped DS game's image, its tag and a .part go with
 *     it, a neighbour's image stays, .dsperate goes once it is empty, and a
 *     loose .nds or a zip on another shelf takes nothing of .dsperate;
 *   - Rename: the copy and its tag take the zip's new name, the tag names the
 *     new zip and nothing else in it changes, and a copy already under the
 *     new name (a game's deleted elsewhere) does not refuse the rename;
 *   - MENU > Storage: a game's size holds its copy, and follows it when
 *     DSperate unpacks it again, though the shelf's folder did not move;
 *     Unpacked DS Games measures every .dsperate, at the top and in a
 *     folder of games, and deletes the copies, their tags and leftovers in
 *     one go, and nothing else: a stray file, the zips, saves and states
 *     stay.
 *
 * Before src/dscache.h, the wrapper set no cap and Delete Game left the
 * image: every zipped DS game ever played stayed at its unpacked size
 * (asked for on Discord, 2026-11-23). Links src/dscache.c, src/storage.c,
 * Rename's and Delete Game's own files, and not SDL. */
#include "../src/dscache.h"
#include "../src/gamedel.h"
#include "../src/gamern.h"
#include "../src/storage.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-dscache-check-XXXXXX";
static char roms[256], saves[256], states[256], nds[300];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define MB (1ull << 20)
/* A path, or a tag's text, into `buf`: whole, or the check stops and says
 * so. A cut path would point the check at another file than the one it
 * means; the card's own paths are short, so this never happens unless the
 * scratch folder's name is far longer than mkdtemp's. */
#define PATHF(buf, ...) do { \
	int w_ = snprintf((buf), sizeof (buf), __VA_ARGS__); \
	if (w_ < 0 || (size_t)w_ >= sizeof (buf)) { \
		printf("  FAIL %s:%d: a path longer than its %zu bytes\n", __FILE__, __LINE__, sizeof (buf)); \
		exit(1); \
	} \
} while (0)

static void mkdirs(const char *path)
{
	char p[1200], *s;

	PATHF(p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

/* A file of `bytes` (sparse: nothing is written), its folders made, its time
 * `when` seconds after the epoch's 1e9 (0: now) */
static void put(const char *path, unsigned long long bytes, long when)
{
	char dir[1200], *slash;
	FILE *f;

	PATHF(dir, "%s", path);
	if ((slash = strrchr(dir, '/'))) { *slash = '\0'; mkdirs(dir); }
	f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	if (bytes && ftruncate(fileno(f), (off_t)bytes) != 0) { perror(path); exit(1); }
	fclose(f);
	if (when) {
		struct timeval tv[2] = { { 1000000000L + when, 0 }, { 1000000000L + when, 0 } };

		utimes(path, tv);
	}
}

static bool there(const char *path)
{
	struct stat st;

	return lstat(path, &st) == 0;
}

/* An image in `dir`, `mb` MB, launched at `when`: its tag carries the time
 * (DSperate touches it at every launch), the image an older one */
static void image(const char *dir, const char *stem, unsigned mb, long when, bool tagged)
{
	char p[1200];

	PATHF(p, "%s/%s.nds", dir, stem);
	put(p, mb * MB, tagged ? 1 : when);
	if (tagged) {
		PATHF(p, "%s/%s.nds.tag", dir, stem);
		put(p, 100, when);
	}
}

static bool image_there(const char *dir, const char *stem, const char *suffix)
{
	char p[1200];

	PATHF(p, "%s/%s.nds%s", dir, stem, suffix);
	return there(p);
}

static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
	(void)st; (void)flag; (void)f;
	return remove(p);
}

/* ---- where ----------------------------------------------------------------- */

static void the_paths(void)
{
	char out[1200], want[1200];

	printf("where DSperate unpacks a zip:\n");
	CHECK(dsc_dir("/r/Nintendo DS", "Game (USA).zip", out, sizeof out));
	CHECK(!strcmp(out, "/r/Nintendo DS/.dsperate"));
	CHECK(dsc_image("/r/Nintendo DS", "Game (USA).zip", out, sizeof out));
	CHECK(!strcmp(out, "/r/Nintendo DS/.dsperate/Game (USA).nds"));
	/* A folder of games keeps its own */
	CHECK(dsc_image("/r/Nintendo DS", "RPG/Some.Game.v1.1.ZIP", out, sizeof out));
	CHECK(!strcmp(out, "/r/Nintendo DS/RPG/.dsperate/Some.Game.v1.1.nds"));
	CHECK(dsc_dir("/r/Nintendo DS", "RPG/Some.Game.v1.1.ZIP", out, sizeof out));
	CHECK(!strcmp(out, "/r/Nintendo DS/RPG/.dsperate"));
	/* Mapped where it lies, or not DSperate's to unpack */
	CHECK(!dsc_image("/r/Nintendo DS", "Game (USA).nds", out, sizeof out));
	CHECK(!dsc_dir("/r/Nintendo DS", "Game (USA).7z", out, sizeof out));
	CHECK(!dsc_dir("/r/Nintendo DS", ".zip", out, sizeof out));
	/* A path that does not fit is no path */
	CHECK(!dsc_image("/r/Nintendo DS", "Game (USA).zip", want, 20));
}

/* ---- the cap --------------------------------------------------------------- */

static void the_cap(void)
{
	char dir[1200], keep[1200], other[1200];
	unsigned long long freed;
	int went = -1;

	printf("the cap after a DS game:\n");
	PATHF(dir, "%s/cap/.dsperate", root);
	/* 2.5 GB, oldest first: Old (no tag, its own time), Mid, Recent, and
	 * Just, the game just played, whose tag is the oldest of all by its
	 * time but which is never the one to go */
	image(dir, "Old", 512, 100, false);
	image(dir, "Mid", 1024, 200, true);
	image(dir, "Recent", 512, 300, true);
	image(dir, "Just", 512, 50, true);
	PATHF(other, "%s/Half.nds.part", dir);
	put(other, 64 * MB, 10);
	PATHF(other, "%s/notes.txt", dir);
	put(other, 1, 10);
	PATHF(keep, "%s/Just.nds", dir);

	freed = dsc_trim(dir, DSC_CAP, keep, &went);
	/* 2560 MB less Old's 512 is 2048: Old alone goes */
	CHECK(freed == 512 * MB);
	CHECK(went == 1);
	CHECK(!image_there(dir, "Old", ""));
	CHECK(image_there(dir, "Mid", "") && image_there(dir, "Mid", ".tag"));
	CHECK(image_there(dir, "Recent", "") && image_there(dir, "Just", ""));

	/* Under the cap: nothing more */
	CHECK(dsc_trim(dir, DSC_CAP, keep, &went) == 0 && went == 0);

	/* A cap of 1 GB: Mid goes, with its tag; Recent and Just fit */
	freed = dsc_trim(dir, 1024 * MB, keep, &went);
	CHECK(freed == 1024 * MB && went == 1);
	CHECK(!image_there(dir, "Mid", "") && !image_there(dir, "Mid", ".tag"));
	CHECK(image_there(dir, "Recent", "") && image_there(dir, "Recent", ".tag"));

	/* The game just played alone over the cap: it stays, the rest go */
	freed = dsc_trim(dir, 100 * MB, keep, &went);
	CHECK(freed == 512 * MB && went == 1);
	CHECK(!image_there(dir, "Recent", ""));
	CHECK(image_there(dir, "Just", "") && image_there(dir, "Just", ".tag"));
	/* Not an image: a .part (DSperate's own to sweep) and anything else */
	PATHF(other, "%s/Half.nds.part", dir);
	CHECK(there(other));
	PATHF(other, "%s/notes.txt", dir);
	CHECK(there(other));

	/* No folder, no game: nothing */
	PATHF(other, "%s/cap/none/.dsperate", root);
	CHECK(dsc_trim(other, 0, NULL, &went) == 0 && went == 0);
	CHECK(dsc_trim(NULL, 0, NULL, NULL) == 0);
}

/* ---- Delete Game ----------------------------------------------------------- */

static void delete_one(const char *tag, const char *folder, const char *file, const char *name,
                       const gd_other *others, int n, gd_done *done)
{
	gd_card card = { roms, saves, states, NULL };
	gd_game g = { folder, tag, "dsperate", "nds|zip", file, name, 0 };
	gd_plan *p = calloc(1, sizeof *p);

	CHECK(p && gd_plan_make(&card, &g, others, n, p));
	if (p && p->why == GD_OK) gd_plan_run(p, done);
	free(p);
}

static void the_delete(void)
{
	char d[1200], f[1200], gba[1200];
	gd_done done;
	const gd_other with_loose[] = {
		{ "Nintendo DS", "Mario Kart DS (USA).zip", "Mario Kart DS (USA)", false, 0 },
		{ "Nintendo DS", "Zelda.nds", "Zelda", false, 0 },
	};
	const gd_other alone[] = {
		{ "Nintendo DS", "Zelda.nds", "Zelda", false, 0 },
	};

	printf("Delete Game takes a zipped DS game's unpacked copy:\n");
	PATHF(d, "%s/.dsperate", nds);
	PATHF(f, "%s/Pokemon HeartGold (USA).zip", nds);
	put(f, 1000, 0);
	PATHF(f, "%s/Mario Kart DS (USA).zip", nds);
	put(f, 1000, 0);
	PATHF(f, "%s/Zelda.nds", nds);
	put(f, 1000, 0);
	image(d, "Pokemon HeartGold (USA)", 128, 100, true);
	PATHF(f, "%s/Pokemon HeartGold (USA).nds.part", d);
	put(f, 1000, 0);
	image(d, "Mario Kart DS (USA)", 32, 100, true);
	/* A zip of the same name on another shelf: not DSperate's */
	PATHF(gba, "%s/Game Boy Advance/Pokemon HeartGold (USA).zip", roms);
	put(gba, 1000, 0);
	PATHF(f, "%s/Game Boy Advance/.dsperate/Pokemon HeartGold (USA).nds", roms);
	put(f, 1000, 0);

	delete_one("NDS", "Nintendo DS", "Pokemon HeartGold (USA).zip", "Pokemon HeartGold (USA)",
	           with_loose, 2, &done);
	CHECK(done.game_gone && done.failed == 0);
	CHECK(!image_there(d, "Pokemon HeartGold (USA)", ""));
	CHECK(!image_there(d, "Pokemon HeartGold (USA)", ".tag"));
	CHECK(!image_there(d, "Pokemon HeartGold (USA)", ".part"));
	/* A 128 MB image is what the delete gives back, beside a 1 KB zip */
	CHECK(done.bytes >= 128 * MB);
	CHECK(image_there(d, "Mario Kart DS (USA)", "") && image_there(d, "Mario Kart DS (USA)", ".tag"));
	CHECK(there(d));

	/* A loose .nds has nothing there to take */
	delete_one("NDS", "Nintendo DS", "Zelda.nds", "Zelda", with_loose, 1, &done);
	CHECK(done.game_gone);
	CHECK(image_there(d, "Mario Kart DS (USA)", ""));

	/* The last image: .dsperate goes with it */
	delete_one("NDS", "Nintendo DS", "Mario Kart DS (USA).zip", "Mario Kart DS (USA)",
	           alone, 0, &done);
	CHECK(done.game_gone && done.failed == 0);
	CHECK(!there(d));

	/* The other shelf's zip takes nothing from its .dsperate */
	{
		gd_card card = { roms, saves, states, NULL };
		gd_game g = { "Game Boy Advance", "GBA", "mgba", "gba|zip", "Pokemon HeartGold (USA).zip",
		              "Pokemon HeartGold (USA)", 0 };
		gd_plan *p = calloc(1, sizeof *p);

		CHECK(p && gd_plan_make(&card, &g, NULL, 0, p));
		for (int i = 0; p && i < p->n; i++) CHECK(!strstr(p->path[i], DSC_DIR));
		free(p);
	}
}

/* ---- Rename ---------------------------------------------------------------- */

static char *slurp(const char *path)
{
	static char buf[4096];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return NULL;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

static void the_rename(void)
{
	gd_card card = { roms, saves, states, NULL };
	char d[1200], f[1200], real[1200], tag[2400], *t;
	gr_plan *p = calloc(1, sizeof *p);
	gr_done done;
	FILE *o;

	printf("Rename takes a zipped DS game's copy and its tag with it:\n");
	PATHF(d, "%s/.dsperate", nds);
	PATHF(f, "%s/Kirby (USA).zip", nds);
	put(f, 1000, 0);
	CHECK(realpath(f, real) != NULL);
	PATHF(f, "%s/Kirby (USA).nds", d);
	put(f, 64 * MB, 0);
	PATHF(tag, "dsperate zip cache 2\narchive %s\nzip 1000 1700000000\n"
	         "entry Kirby Super Star Ultra (USA).nds\ncrc 305419896 size 67108864\n", real);
	PATHF(f, "%s/Kirby (USA).nds.tag", d);
	o = fopen(f, "wb");
	fputs(tag, o);
	fclose(o);

	{
		gd_game g = { "Nintendo DS", "NDS", "dsperate", "nds|zip", "Kirby (USA).zip", "Kirby (USA)", 0 };

		CHECK(p && gr_plan_make(&card, &g, NULL, 0, "Kirby Super Star", p));
		if (p && p->why == GR_OK) gr_plan_run(p, &done);
	}
	CHECK(done.game_moved && done.failed == 0);
	CHECK(!image_there(d, "Kirby (USA)", "") && !image_there(d, "Kirby (USA)", ".tag"));
	CHECK(image_there(d, "Kirby Super Star", "") && image_there(d, "Kirby Super Star", ".tag"));
	{
		struct stat st;

		PATHF(f, "%s/Kirby Super Star.nds", d);
		CHECK(stat(f, &st) == 0 && (unsigned long long)st.st_size == 64 * MB);
	}
	/* The tag: its archive is the new zip, by its realpath; the rest as it was */
	PATHF(f, "%s/Kirby Super Star.zip", nds);
	CHECK(realpath(f, real) != NULL);
	PATHF(tag, "dsperate zip cache 2\narchive %s\nzip 1000 1700000000\n"
	         "entry Kirby Super Star Ultra (USA).nds\ncrc 305419896 size 67108864\n", real);
	PATHF(f, "%s/Kirby Super Star.nds.tag", d);
	t = slurp(f);
	CHECK(t && !strcmp(t, tag));

	/* A copy already under the new name, left by a game deleted elsewhere:
	 * the rename goes on, this copy stays for DSperate to sweep */
	PATHF(f, "%s/Luigi.zip", nds);
	put(f, 1000, 0);
	image(d, "Luigi", 16, 100, true);
	image(d, "Mario", 8, 100, true);
	{
		gd_game g = { "Nintendo DS", "NDS", "dsperate", "nds|zip", "Luigi.zip", "Luigi", 0 };

		memset(p, 0, sizeof *p);
		CHECK(gr_plan_make(&card, &g, NULL, 0, "Mario", p));
		CHECK(p->why == GR_OK);
		if (p->why == GR_OK) gr_plan_run(p, &done);
	}
	CHECK(done.game_moved && done.failed == 0);
	PATHF(f, "%s/Mario.zip", nds);
	CHECK(there(f));
	CHECK(image_there(d, "Luigi", "") && image_there(d, "Mario", ""));
	{
		struct stat st;

		PATHF(f, "%s/Mario.nds", d);
		CHECK(stat(f, &st) == 0 && (unsigned long long)st.st_size == 8 * MB);
	}

	/* A loose .nds has no copy to take */
	PATHF(f, "%s/Loose.nds", nds);
	put(f, 1000, 0);
	{
		gd_game g = { "Nintendo DS", "NDS", "dsperate", "nds|zip", "Loose.nds", "Loose", 0 };

		memset(p, 0, sizeof *p);
		CHECK(gr_plan_make(&card, &g, NULL, 0, "Tight", p));
		for (int i = 0; i < p->n; i++) CHECK(!strstr(p->from[i], DSC_DIR));
		CHECK(p->ds_tag == 0);
	}
	free(p);
}

/* ---- Storage --------------------------------------------------------------- */

/* A file of `bytes` real bytes: Storage counts the clusters a file holds */
static void put_real(const char *path, size_t bytes)
{
	static char buf[64 * 1024];
	FILE *f;

	put(path, 0, 0);
	if (!(f = fopen(path, "wb"))) { perror(path); exit(1); }
	for (size_t done = 0; done < bytes; done += sizeof buf)
		fwrite(buf, 1, bytes - done < sizeof buf ? bytes - done : sizeof buf, f);
	fclose(f);
}

static void the_storage(void)
{
	char shared[300], zroot[300], f[1200], d[1200];
	st_paths sp;
	st_systems ss = { { "Nintendo DS" }, 1, NULL };
	st_game_in g = { "Nintendo DS", "NDS", "dsperate", "nds|zip", "Chrono Trigger (USA).zip",
	                 "Chrono Trigger (USA)", false, 0, 0 };
	st_game_size gz;

	printf("MENU > Storage counts a zipped DS game's copy as the game's:\n");
	PATHF(shared, "%s/shared", root);
	PATHF(zroot, "%s/ZyperOS", root);
	mkdirs(zroot);
	sp = (st_paths){ root, roms, shared, zroot, false };
	PATHF(f, "%s/Chrono Trigger (USA).zip", nds);
	put_real(f, 256 * 1024);
	PATHF(d, "%s/.dsperate", nds);
	/* What the parts before left there, gone: this part counts its own */
	nftw(d, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	PATHF(f, "%s/Chrono Trigger (USA).nds", d);
	put_real(f, 2 * MB);
	PATHF(f, "%s/Chrono Trigger (USA).nds.tag", d);
	put_real(f, 100);

	st_reset();
	st_measure_now(&sp, &ss, &g, 1, 2000000000L, false);
	CHECK(st_game("Nintendo DS", "Chrono Trigger (USA).zip", &gz));
	CHECK(gz.games >= 2 * MB + 256 * 1024);
	CHECK(gz.games < 3 * MB);

	/* Unpacked again, larger: only .dsperate's time moves */
	sleep(1);
	PATHF(f, "%s/Chrono Trigger (USA).nds", d);
	unlink(f);
	PATHF(f, "%s/Chrono Trigger (USA).nds.part", d);
	put_real(f, 4 * MB);
	{
		char to[1200];

		PATHF(to, "%s/Chrono Trigger (USA).nds", d);
		CHECK(rename(f, to) == 0);
	}
	st_measure_now(&sp, &ss, &g, 1, 2000000000L, true);
	CHECK(st_game("Nintendo DS", "Chrono Trigger (USA).zip", &gz));
	CHECK(gz.games >= 4 * MB + 256 * 1024);

	/* Unpacked DS Games: what every .dsperate holds, at the top and in a
	 * folder of games, measured; then deleted in one go, and only that */
	printf("MENU > Storage's Unpacked DS Games measures them and deletes them, and only them:\n");
	{
		char sub[1200], save[1200], state[1200], note[1200];
		st_card card;
		int files = -1;
		unsigned long long b;

		PATHF(sub, "%s/RPG/" DSC_DIR, nds);
		PATHF(f, "%s/RPG/Golden Sun DS.zip", nds);
		put_real(f, 64 * 1024);
		PATHF(f, "%s/Golden Sun DS.nds", sub);
		put_real(f, 1 * MB);
		PATHF(f, "%s/Golden Sun DS.nds.tag", sub);
		put_real(f, 100);
		PATHF(f, "%s/Half.nds.part", d);
		put_real(f, 512 * 1024);
		/* What is not a copy, and what must never go */
		PATHF(note, "%s/readme.txt", d);
		put_real(note, 100);
		PATHF(save, "%s/Chrono Trigger (USA).sav", saves);
		put_real(save, 512 * 1024);
		PATHF(state, "%s/Nintendo DS/Chrono Trigger (USA).zip.auto.state", states);
		put_real(state, 1 * MB);

		st_measure_now(&sp, &ss, &g, 1, 2000000000L, true);
		st_snapshot(&card);
		/* 4 MB + 1 MB of copies, the .part and the tags: 5.5 MB and a little */
		CHECK(card.ds_copies >= 5 * MB + 512 * 1024);
		CHECK(card.ds_copies < 6 * MB + 512 * 1024);
		CHECK(card.nsys == 1 && card.sys[0].ds_copies == card.ds_copies);

		b = st_delete_ds_copies(&sp, &ss, &files);
		CHECK(files == 2);
		CHECK(b >= 5 * MB + 512 * 1024);
		CHECK(!image_there(d, "Chrono Trigger (USA)", "") && !image_there(d, "Chrono Trigger (USA)", ".tag"));
		CHECK(!image_there(d, "Half", ".part"));
		CHECK(!there(sub));
		CHECK(there(note) && there(d));
		CHECK(there(save) && there(state));
		PATHF(f, "%s/Chrono Trigger (USA).zip", nds);
		CHECK(there(f));
		PATHF(f, "%s/RPG/Golden Sun DS.zip", nds);
		CHECK(there(f));

		st_measure_now(&sp, &ss, &g, 1, 2000000000L, true);
		st_snapshot(&card);
		CHECK(card.ds_copies < 64 * 1024);
	}
	st_reset();
}

int main(void)
{
	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	PATHF(roms, "%s/Roms", root);
	PATHF(saves, "%s/Saves", root);
	PATHF(states, "%s/shared/.zyperos", root);
	PATHF(nds, "%s/Nintendo DS", roms);
	mkdirs(saves);
	mkdirs(states);
	mkdirs(nds);

	the_paths();
	the_cap();
	the_delete();
	the_rename();
	the_storage();

	nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (failures) {
		printf("dscache: %d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("dscache: OK\n");
	return 0;
}
