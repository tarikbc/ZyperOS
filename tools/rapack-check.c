/* SPDX-License-Identifier: GPL-3.0-only */
/* A set's badges in one file (src/rapack.h), on a card in /tmp:
 *
 *     make check-rapack
 *
 * The pack's place beside the set; a set's loose badges taken in, read back
 * the same byte for byte, and only then deleted, a loose file that is no
 * PNG left alone, a name the set does not have left alone; a second fetch's
 * badges added beside the first's, a loose one in place of a packed one of
 * the same name; nothing loose, the pack not written again; a cut pack read
 * as none and written again whole from what is loose; a pack that cannot be
 * written deleting nothing. No SDL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/rapack.h"

static int fails;
static char g_root[256], g_badges[320], g_set[400], g_pack[1100];

static void report(int ok, const char *name)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) fails++;
}

/* A badge's stand-in: a PNG's signature and a word */
static void put(const char *name, const char *word, bool png)
{
	char p[600];
	FILE *f;

	snprintf(p, sizeof p, "%s/%s.png", g_badges, name);
	if (!(f = fopen(p, "wb"))) return;
	if (png) fputs("\x89PNG\r\n\x1a\n", f);
	fputs(word, f);
	fclose(f);
}

static bool loose(const char *name)
{
	char p[600];

	snprintf(p, sizeof p, "%s/%s.png", g_badges, name);
	return access(p, F_OK) == 0;
}

/* The packed badge's word, or "" */
static const char *word(const char *name)
{
	static char w[64];
	size_t len;
	char *b = rp_read(g_pack, name, &len);

	w[0] = '\0';
	if (b && len > 8 && len - 8 < sizeof w && !memcmp(b, "\x89PNG", 4)) {
		memcpy(w, b + 8, len - 8);
		w[len - 8] = '\0';
	}
	free(b);
	return w;
}

static long mtime_ns(const char *p)
{
	struct stat st;

	if (stat(p, &st) != 0) return -1;
#ifdef __APPLE__
	return (long)st.st_mtimespec.tv_sec * 1000000000L + st.st_mtimespec.tv_nsec;
#else
	return (long)st.st_mtim.tv_sec * 1000000000L + st.st_mtim.tv_nsec;
#endif
}

int main(void)
{
	static const char names[4][RP_NAME] = { "1001", "1002", "1003", "9999" };
	static const char more[5][RP_NAME] = { "1001", "1002", "1003", "1004", "9999" };
	char p[600];
	int took = -1, r;
	long t;

	printf("rapack: a set's badges in one file\n");
	snprintf(g_root, sizeof g_root, "/tmp/rapack-check-%ld", (long)getpid());
	snprintf(p, sizeof p, "%s/.cheevos", g_root);
	mkdir(g_root, 0755);
	mkdir(p, 0755);
	snprintf(g_badges, sizeof g_badges, "%s/.cheevos/badges", g_root);
	mkdir(g_badges, 0755);
	snprintf(g_set, sizeof g_set, "%s/.cheevos/Game (USA).set", g_root);

	report(rp_pack_path(g_set, g_pack, sizeof g_pack) &&
	       !strcmp(strrchr(g_pack, '/'), "/Game (USA).pack") && strstr(g_pack, "/.cheevos/badges/"),
	       "the_pack_beside_the_set_in_badges");

	put("1001", "one", true);
	put("1002", "two", true);
	put("1003", "Game (Japan).png", false);   /* a link's text: no PNG */
	put("5555", "another set's", true);
	r = rp_pack(g_set, names, 4, &took);
	report(r == 2 && took == 2, "the_sets_loose_pngs_taken_in");
	report(!strcmp(word("1001"), "one") && !strcmp(word("1002"), "two"), "read_back_the_same");
	report(!loose("1001") && !loose("1002"), "and_the_loose_ones_deleted");
	report(loose("1003") && !rp_has(g_pack, "1003"), "a_loose_file_that_is_no_png_left_alone");
	report(loose("5555") && !rp_has(g_pack, "5555"), "a_badge_of_no_name_of_the_sets_left_alone");
	report(!rp_has(g_pack, "9999") && !rp_read(g_pack, "9999", NULL), "one_never_fetched_is_not_there");

	/* The next fetch: one new, one again */
	put("1004", "four", true);
	put("1002", "TWO", true);
	r = rp_pack(g_set, more, 5, &took);
	report(r == 3 && took == 2, "a_second_fetch_added_beside_the_first");
	report(!strcmp(word("1001"), "one") && !strcmp(word("1002"), "TWO") && !strcmp(word("1004"), "four"),
	       "a_loose_one_in_place_of_the_packed_one");

	/* Nothing loose: not written again */
	t = mtime_ns(g_pack);
	usleep(20000);
	r = rp_pack(g_set, more, 5, &took);
	report(r == 3 && took == 0 && mtime_ns(g_pack) == t, "nothing_loose_the_pack_untouched");

	/* A pack cut short: as none, and written again from what is loose */
	{
		FILE *f = fopen(g_pack, "r+b");

		if (f) {
			fseek(f, 0, SEEK_END);
			if (ftruncate(fileno(f), ftell(f) - 3) != 0) { }
			fclose(f);
		}
	}
	report(!rp_has(g_pack, "1001"), "a_cut_pack_reads_as_none");
	put("1001", "one again", true);
	r = rp_pack(g_set, more, 5, &took);
	report(r == 1 && !strcmp(word("1001"), "one again") && !loose("1001"), "and_is_written_again_whole");

	/* A pack that cannot be written deletes nothing */
	put("1004", "four again", true);
	chmod(g_badges, 0555);
	snprintf(p, sizeof p, "%s/probe", g_badges);
	{
		FILE *probe = fopen(p, "wb");

		if (probe) {   /* root writes anyway: nothing to see here */
			fclose(probe);
			remove(p);
			printf("  (skipped: the folder is writable to this user whatever its mode)\n");
		} else {
			r = rp_pack(g_set, more, 5, &took);
			report(r == -1 && took == 0 && loose("1004"), "a_pack_not_written_deletes_nothing");
		}
	}
	chmod(g_badges, 0755);

	snprintf(p, sizeof p, "rm -rf '%s'", g_root);
	if (system(p) != 0) { }
	if (fails) {
		printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
