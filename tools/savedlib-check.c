/* Does the saved library give a start what reading the card would?
 *
 * src/savedlib.h keeps each folder's listing (lib_scan_listing) so a start
 * can put the shelves up before it reads the card. What has to hold:
 *
 *   - a listing written and read back, then finished (lib_scan_finish), is
 *     what lib_scan makes of the same folder: the same games, titles from
 *     the database, order, and left-out names - with tabs, backslashes and
 *     newlines in the names, which a host's card can have;
 *   - a listing is taken only for its own folder and extensions, once;
 *   - the digest the check compares does not care about the order, and does
 *     see a game added, gone or renamed, and a left-out file;
 *   - anything that is not a whole file this version wrote is no saved
 *     library at all, and the start reads the card.
 *
 * Links src/savedlib.c, src/library.c and src/db.c, and not SDL.
 */
#include "../src/db.h"
#include "../src/library.h"
#include "../src/savedlib.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-savedlib-check-XXXXXX";
static char roms[sizeof root + 8], saved[sizeof root + 32];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void touch(const char *name)
{
	char path[1200];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", roms, name);
	f = fopen(path, "wb");
	CHECK(f != NULL);
	if (f) fclose(f);
}

static void directory(const char *name)
{
	char path[1200];

	snprintf(path, sizeof path, "%s/%s", roms, name);
	CHECK(mkdir(path, 0700) == 0);
}

static void gone(const char *name)
{
	char path[1200];

	snprintf(path, sizeof path, "%s/%s", roms, name);
	CHECK(unlink(path) == 0);
}

static void write_text(const char *text)
{
	FILE *f = fopen(saved, "wb");

	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

/* The same games, in the same order, field by field */
static bool same_list(const game_list *a, const game_list *b)
{
	int i;

	if (a->count != b->count || a->skipped != b->skipped || a->scanned != b->scanned) return false;
	for (i = 0; i < a->skipped && i < LIB_SKIPS_SHOWN; i++)
		if (strcmp(a->skipped_eg[i], b->skipped_eg[i])) return false;
	for (i = 0; i < a->count; i++) {
		const game_entry *x = &a->items[i], *y = &b->items[i];

		if (strcmp(x->file, y->file) || strcmp(x->name, y->name) || strcmp(x->title, y->title) ||
		    x->added != y->added)
			return false;
	}
	return true;
}

static const char *FOLDERS[] = { "NES", "PS1", "Empty", "Missing" };
static const char *EXTS[]    = { "nes", "chd,cue,m3u", "nes", "nes" };
#define NFOLDERS 4

static void round_trip(void)
{
	game_list listing[NFOLDERS] = { 0 }, full[NFOLDERS] = { 0 };
	savedlib_row rows[NFOLDERS];
	saved_lib s;
	game_meta m = { 0 };
	int i;

	printf("a saved listing, finished, is the scan:\n");
	directory("NES");
	directory("PS1");
	directory("PS1/Final Disc");
	directory("PS1/Nothing Here");
	directory("Empty");
	touch("NES/Zelda.nes");
	touch("NES/Contra (USA).nes");
	touch("NES/readme.txt");
	touch("NES/Tab\there.nes");
	touch("NES/Back\\slash.nes");
	touch("NES/New\nline.nes");
	touch("PS1/Final Disc/Final Disc.cue");
	touch("PS1/Final Disc/Final Disc.bin");
	touch("PS1/Nothing Here/notes.txt");
	touch("PS1/Solo.chd");
	strcpy(m.title, "The Legend of Zelda");
	CHECK(db_game_set(db_lib(), "NES", "Zelda.nes", &m));

	for (i = 0; i < NFOLDERS; i++) {
		lib_scan_listing(roms, FOLDERS[i], EXTS[i], &listing[i]);
		lib_scan(roms, FOLDERS[i], EXTS[i], &full[i]);
		rows[i] = (savedlib_row){ FOLDERS[i], EXTS[i], &listing[i] };
	}
	CHECK(full[0].count == 5 && full[0].skipped == 1 && !strcmp(full[0].skipped_eg[0], "readme.txt"));
	CHECK(full[1].count == 2 && full[1].skipped == 1 && !strcmp(full[1].skipped_eg[0], "Nothing Here/"));
	CHECK(full[2].count == 0 && full[3].count == 0);
	CHECK(savedlib_write(saved, "v1", rows, NFOLDERS));
	CHECK(savedlib_load(saved, "v1", &s) && s.count == NFOLDERS);
	for (i = 0; i < NFOLDERS; i++) {
		game_list got = { 0 };

		CHECK(savedlib_take(&s, FOLDERS[i], EXTS[i], &got));
		CHECK(lib_listing_digest(&got) == lib_listing_digest(&listing[i]));
		lib_scan_finish(FOLDERS[i], &got);
		CHECK(same_list(&got, &full[i]));
		lib_free(&got);
	}
	{
		game_list got = { 0 };
		int k, titled = 0;

		CHECK(!savedlib_take(&s, "NES", "nes", &got));            /* taken already */
		CHECK(savedlib_untaken(&s) == 0);
		for (k = 0; k < full[0].count; k++)
			titled += !strcmp(full[0].items[k].file, "Zelda.nes") &&
			          !strcmp(full[0].items[k].title, "The Legend of Zelda");
		CHECK(titled == 1);                                      /* from the database */
	}
	savedlib_free(&s);
	CHECK(savedlib_load(saved, "v1", &s));
	{
		game_list got = { 0 };

		CHECK(!savedlib_take(&s, "NES", "nes,zip", &got));       /* other extensions */
		CHECK(!savedlib_take(&s, "nes", "nes", &got));           /* another folder */
		CHECK(savedlib_untaken(&s) == NFOLDERS);
	}
	savedlib_free(&s);
	for (i = 0; i < NFOLDERS; i++) {
		lib_free(&listing[i]);
		lib_free(&full[i]);
	}
}

static void digests(void)
{
	game_list l = { 0 }, back = { 0 };
	uint64_t d0;
	int i;

	printf("the check's digest sees changes, not order:\n");
	lib_scan_listing(roms, "NES", "nes", &l);
	d0 = lib_listing_digest(&l);
	back = l;
	back.items = malloc((size_t)l.count * sizeof *l.items);
	CHECK(back.items != NULL);
	if (!back.items) return;
	for (i = 0; i < l.count; i++) back.items[i] = l.items[l.count - 1 - i];
	CHECK(lib_listing_digest(&back) == d0);
	free(back.items);

	touch("NES/Metroid.nes");
	lib_scan_listing(roms, "NES", "nes", &l);
	CHECK(lib_listing_digest(&l) != d0);
	gone("NES/Metroid.nes");
	lib_scan_listing(roms, "NES", "nes", &l);
	CHECK(lib_listing_digest(&l) == d0);
	{
		char a[1200], b[1200];

		snprintf(a, sizeof a, "%s/NES/Zelda.nes", roms);
		snprintf(b, sizeof b, "%s/NES/Zelda II.nes", roms);
		CHECK(rename(a, b) == 0);
		lib_scan_listing(roms, "NES", "nes", &l);
		CHECK(lib_listing_digest(&l) != d0);
		CHECK(rename(b, a) == 0);
	}
	touch("NES/notes.txt");
	lib_scan_listing(roms, "NES", "nes", &l);
	CHECK(lib_listing_digest(&l) != d0);
	gone("NES/notes.txt");
	lib_free(&l);
}

static void refusals(void)
{
	game_list one = { 0 };
	savedlib_row row = { "NES", "nes", &one };
	saved_lib s;
	char *text = NULL;
	long n = 0;
	FILE *f;

	printf("anything but a whole file this version wrote is none:\n");
	lib_scan_listing(roms, "NES", "nes", &one);
	CHECK(savedlib_write(saved, "v1", &row, 1));
	CHECK(!savedlib_load(saved, "v2", &s) && s.count == 0 && !s.shelves);
	f = fopen(saved, "rb");
	CHECK(f != NULL);
	if (f) {
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		text = calloc(1, (size_t)n + 1);
		if (text && fread(text, 1, (size_t)n, f) != (size_t)n) n = 0;
		fclose(f);
	}
	CHECK(text && n > 10);
	if (text && n > 10) {
		text[n - 5] = '\0';                     /* cut short */
		write_text(text);
		CHECK(!savedlib_load(saved, "v1", &s));
	}
	free(text);
	write_text("hello\n");
	CHECK(!savedlib_load(saved, "v1", &s));
	write_text("ZyperOS saved library 1\tv1\nshelf\t2\t0\tnes\tNES\ngame\ta.nes\ta\n");
	CHECK(!savedlib_load(saved, "v1", &s));     /* a game short */
	write_text("ZyperOS saved library 1\tv1\nshelf\t1\t0\tnes\tNES\ngame\ta\\q.nes\ta\n");
	CHECK(!savedlib_load(saved, "v1", &s));     /* no such escape */
	write_text("ZyperOS saved library 1\tv1\nshelf\t1\t0\tnes\tNES\ngame\ta.nes\ta\textra\n");
	CHECK(!savedlib_load(saved, "v1", &s));     /* a field too many */
	write_text("ZyperOS saved library 1\tv1\nshelf\t-1\t0\tnes\tNES\n");
	CHECK(!savedlib_load(saved, "v1", &s));
	write_text("ZyperOS saved library 1\tv1\nshelf\t1\t0\tnes\tNES\ngame\ta.nes\ta\n");
	CHECK(savedlib_load(saved, "v1", &s) && s.count == 1 && s.shelves[0].list.count == 1);
	savedlib_free(&s);
	unlink(saved);
	CHECK(!savedlib_load(saved, "v1", &s));     /* none */
	lib_free(&one);
}

static int rm_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st; (void)flag; (void)ftw;
	return remove(path);
}

int main(void)
{
	char devpath[sizeof root + 16], libpath[sizeof root + 16];

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(roms, sizeof roms, "%s/Roms", root);
	snprintf(saved, sizeof saved, "%s/saved-library", root);
	snprintf(devpath, sizeof devpath, "%s/device.db", root);
	snprintf(libpath, sizeof libpath, "%s/library.db", root);
	CHECK(mkdir(roms, 0700) == 0);
	CHECK(db_init(devpath, libpath, NULL));

	round_trip();
	digests();
	refusals();

	db_shutdown();
	nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);   /* this test's own mkdtemp */
	if (failures) { printf("\n%d FAILED\n", failures); return 1; }
	puts("ok: a saved listing is the scan, and nothing else is read as one");
	return 0;
}
