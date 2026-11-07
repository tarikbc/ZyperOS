/* Search Games' matching, held on a build machine (src/search.h): what a
 * query finds, in what order, and what it never finds.
 *
 * Links src/search.c and nothing else. */
#include "../src/search.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static const sr_item LIB[] = {
	{ "The Legend of Zelda: A Link to the Past", "Legend of Zelda, The - A Link to the Past (USA)", 0, 0 },
	{ "Super Mario World", "Super Mario World (USA)", 0, 1 },
	{ "Mario Kart DS", "Mario Kart DS (USA) (En,Fr,De,Es,It)", 1, 0 },
	{ "Pokémon Emerald", "Pokemon - Emerald Version (USA, Europe)", 2, 0 },
	{ "Super Mario Bros. 3", "Super Mario Bros. 3 (USA) (Rev 1)", 3, 0 },
	{ "Dr. Mario", "Dr. Mario (Japan, USA)", 3, 1 },
	{ "Contra", "Contra (USA)", 3, 2 },
	{ "Final Fantasy VII", "Final Fantasy VII (USA) (Disc 1)", 4, 0 },
};
#define NLIB ((int)(sizeof LIB / sizeof LIB[0]))

static int find(const char *q, sr_item *out, int max)
{
	return sr_search(q, LIB, NLIB, out, max);
}

int main(void)
{
	char b[128];
	sr_item r[SR_SHOWN];
	int n;

	printf("folding:\n");
	sr_fold("  Pokémon: Emerald!!  ", b, sizeof b);
	ck(!strcmp(b, "pokemon emerald"), "case, accents and punctuation go");
	sr_fold("ÀÉÎÕÜÇÑ", b, sizeof b);
	ck(!strcmp(b, "aeiouc" "n"), "Latin capitals with accents too");
	sr_fold("Dr. Mario", b, sizeof b);
	ck(!strcmp(b, "dr mario"), "a dot is a space");
	sr_fold("abcdef", b, 4);
	ck(!strcmp(b, "abc"), "cut to the buffer");

	printf("what a query finds:\n");
	n = find("mario", r, SR_SHOWN);
	ck(n == 4, "every title with the word, from every system");
	ck(n >= 1 && !strcmp(r[0].title, "Mario Kart DS"), "a title it starts first");
	ck(n == 4 && !strcmp(r[3].title, "Super Mario World"), "then the rest by title");
	n = find("zelda link", r, SR_SHOWN);
	ck(n == 1 && r[0].shelf == 0 && r[0].game == 0, "every word, in any order and place");
	n = find("pokemon", r, SR_SHOWN);
	ck(n == 1 && r[0].shelf == 2, "without its accent");
	n = find("POKÉMON emer", r, SR_SHOWN);
	ck(n == 1, "with it, in capitals, and a word cut short");
	n = find("rev 1", r, SR_SHOWN);
	ck(n == 1 && !strcmp(r[0].title, "Super Mario Bros. 3"), "a dump's tags, in its file name");
	n = find("disc", r, SR_SHOWN);
	ck(n == 1 && r[0].shelf == 4, "file name only, still found");
	n = find("mario usa", r, SR_SHOWN);
	ck(n == 4, "a word in the title and one in the file name");
	n = find("contra", r, SR_SHOWN);
	ck(n == 1, "one word, one game");
	n = find("metroid", r, SR_SHOWN);
	ck(n == 0, "nothing for a game that is not there");
	n = find("mario zelda", r, SR_SHOWN);
	ck(n == 0, "every word, not any");
	ck(find("", r, SR_SHOWN) == 0 && find("  !!  ", r, SR_SHOWN) == 0,
	   "nothing for nothing typed");

	printf("how many are shown:\n");
	n = find("a", r, 2);
	ck(n > 2, "the count is all it found");
	ck(r[0].title && r[1].title, "and the first ones are filled");

	printf("the order:\n");
	n = find("final", r, SR_SHOWN);
	ck(n == 1, "Final Fantasy VII");
	n = find("dr", r, SR_SHOWN);
	ck(n >= 1 && !strcmp(r[0].title, "Dr. Mario"), "a title that starts with it first");
	if (fails) { printf("search-check: %d FAILED\n", fails); return 1; }
	printf("search-check: ok\n");
	return 0;
}
