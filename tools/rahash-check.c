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
 */
#include <stdio.h>
#include <string.h>
#include "../src/rahash.h"

int main(int argc, char **argv)
{
	int i, bad = 0;

	if (argc >= 5 && !strcmp(argv[1], "--support")) {
		static const char *const word[] = { "ok", "file", "system" };

		for (i = 4; i < argc; i++)
			printf("%s\t%s\n", word[ra_hash_support(argv[2], argv[i], argv[3])], argv[i]);
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
