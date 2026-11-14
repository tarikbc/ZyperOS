/* SPDX-License-Identifier: GPL-3.0-only */
/* Print the RetroAchievements hash of each ROM given, so it can be diffed
 * against the same rules written in Python.
 *
 *     tools/rahash-check.c  <tag> <rom>...   ->  <hash>\t<rom>
 *     tools/rahash-check.c  --support <tag> <exts> <name>...
 *                                            ->  ok|file|system\t<name>
 *
 * Driven by `make check-rahash`, which runs both over the whole test library
 * and requires every one of them to agree. Two implementations of someone
 * else's spec is a duplication; this is what keeps it from being a drift.
 * --support is ra_hash_support, which reads no file: a name is enough.
 *
 *     tools/rahash-check.c  --job <tag> <rom> <set>
 *                                            ->  <hash>|none\t<set>
 *
 * --job is the same hash made the way a first play makes it, on its own
 * thread (src/rahashjob.h), polled until it lands; and a second job asked
 * for while the first runs must be refused.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../src/rahash.h"
#include "../src/rahashjob.h"

int main(int argc, char **argv)
{
	int i, bad = 0;

	if (argc >= 5 && !strcmp(argv[1], "--support")) {
		static const char *const word[] = { "ok", "file", "system" };

		for (i = 4; i < argc; i++)
			printf("%s\t%s\n", word[ra_hash_support(argv[2], argv[i], argv[3])], argv[i]);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "--job")) {
		char h[33], set[2048];
		int r, waited = 0;

		if (!rhj_begin(argv[3], argv[2], argv[4])) return 3;
		if (rhj_begin(argv[3], argv[2], "second")) return 4;
		while ((r = rhj_poll(h, set, sizeof set)) == 0 && waited++ < 3000) usleep(10000);
		if (r != 1 && r != -1) return 5;
		if (rhj_poll(h, set, sizeof set) != -2) return 6;
		printf("%s\t%s\n", r == 1 ? h : "none", set);
		return 0;
	}
	if (argc < 3) { fprintf(stderr, "usage: rahash-check <tag> <rom>...\n"); return 2; }
	for (i = 2; i < argc; i++) {
		char h[33];
		if (ra_hash_rom(argv[i], argv[1], h)) printf("%s\t%s\n", h, argv[i]);
		else { printf("FAILED\t%s\n", argv[i]); bad++; }
	}
	return bad ? 1 : 0;
}
