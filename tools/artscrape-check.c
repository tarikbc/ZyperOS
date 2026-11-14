/* SPDX-License-Identifier: GPL-3.0-only */
/* Does the device's title matcher agree with the host tool's?
 *
 *     make check-artscrape
 *
 * The C in src/artscrape.c is a port of tools/scrape-art.py, and the only
 * thing the port has to get right is norm(). Everything the feature claims -
 * 97% of a 178-ROM library matched, against 83% for guessing filenames -
 * rests on the two agreeing about what a title reduces to.
 *
 * A drift here does not fail. It finds fewer games and looks exactly like
 * libretro carrying less art than it does, which is unfalsifiable from the
 * device. So this prints what the C makes of a list of names and the Python
 * half compares it against re.sub, over the real library when there is one -
 * the same shape as check-rahash, and for the same reason.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/artscrape.h"
#include "../src/library.h"

/* artscrape calls this when a fetch lands. The real one is src/artshrink.c and
 * needs SDL_image; this check is offline and builds without SDL, and it only
 * exercises the title matcher, which never reaches a fetch. */
void art_shrink(const char *path);
void art_shrink(const char *path) { (void)path; }

static int failures;

#define BEATS(want, a, b)                                                     \
	do {                                                                      \
		int sa = art_tag_score((want), (a)), sb = art_tag_score((want), (b)); \
		if (sa <= sb) {                                                       \
			fprintf(stderr, "  FAIL: for %s\n        %s (%d) should beat"    \
			        " %s (%d)\n", (want), (a), sa, (b), sb);                  \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* Which of several candidates a game gets.
 *
 * 1703 of NES's 13418 entries normalize to a title some other entry also
 * normalizes to - "contra" alone has eight - so the fuzzy pass is choosing,
 * not finding. It used to take whichever sorted first, which is alphabetical,
 * which is a Japanese release as often as a US one. Nothing about the result
 * says which happened: you get box art, it is just the wrong box.
 *
 * The candidates below are real, copied out of the NES listing.
 */
static void check_scoring(void)
{
	fprintf(stderr, "  picking between candidates that normalize alike:\n");

	/* The plain case, and the one that was going wrong. */
	BEATS("Contra (USA)", "Contra (USA)", "Contra (Japan)");
	BEATS("Contra (USA)", "Contra (USA)", "Contra (Japan) (Sample)");

	/* A TOSEC-style name spells the region differently. Without the alias
	 * table this scored the same as the Japanese one. */
	BEATS("Contra (USA)", "Contra (1988-02)(Konami)(US)",
	      "Contra (1988-02-09)(Konami)(JP)");

	/* Extra tags cost something, but never enough to lose to the wrong
	 * region: a revision of the right release beats the wrong release. */
	BEATS("Crystalis (USA)", "Crystalis (USA)", "Crystalis (USA) (Beta)");
	BEATS("Blaster Master (USA)", "Blaster Master (USA) (Beta) (1988-05-20)",
	      "Blaster Master (Japan) (Virtual Console)");

	/* Several regions in one group, and a language marker the card lacks. */
	BEATS("Sonic The Hedgehog (USA, Europe, Brazil)",
	      "Sonic The Hedgehog (USA, Europe, Brazil) (En)",
	      "Sonic The Hedgehog (Japan)");

	/* A World dump is what a US card wants when there is no US one. */
	BEATS("Alex Kidd in Miracle World (USA)",
	      "Alex Kidd in Miracle World (World)",
	      "Alex Kidd in Miracle World (Japan)");

	/* And an untagged entry must not beat the right region, or "Contra"
	 * would win over "Contra (USA)" for every card. */
	BEATS("Contra (USA)", "Contra (USA)", "Contra");

	/* GoodTools' letters, which most of a card named before No-Intro carries.
	 * Vigilante (U) was given the Japanese box, 2026-10-05: unread, (U) and
	 * (USA) shared nothing, and the first in the list won. */
	BEATS("Vigilante (U)", "Vigilante (USA)", "Vigilante (Japan)");
	BEATS("Gradius (J)", "Gradius (Japan)", "Gradius (USA)");
	BEATS("Xenon 2 Megablast (E) [c][!]", "Xenon 2 - Megablast (Europe)",
	      "Xenon 2 - Megablast (Japan)");
	/* Several run together, each a region. */
	BEATS("Awesome Possum (UJE) [!]", "Awesome Possum (USA)", "Awesome Possum (Korea)");
	BEATS("Space Turtleship (K)", "Space Turtleship (Korea)", "Space Turtleship (USA)");
	/* And the aliases still come first: TOSEC's EU is Europe, not
	 * Europe and the USA. */
	BEATS("Contra (EU)", "Contra (Europe)", "Contra (USA)");
}

/* The loose pass: what it reads past, and what it must not. Measured on the
 * GKD Pixel 2's card 2026-10-05, the 17 games missing covers against libretro's
 * listings: 10 by name, 5 more loosely (three [c] dumps, two Ys
 * translations), 2 still none (a different title, a Korean-only game). */
#define LOOSE(a, b, same)                                                     \
	do {                                                                      \
		char la[256], lb[256];                                                \
		art_norm_loose((a), la, sizeof la);                                   \
		art_norm_loose((b), lb, sizeof lb);                                   \
		if ((strcmp(la, lb) == 0) != (same)) {                                \
			fprintf(stderr, "  FAIL: %s and %s should%s match loosely\n"     \
			        "        (\"%s\" against \"%s\")\n", (a), (b),            \
			        (same) ? "" : " not", la, lb);                            \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static void check_loose(void)
{
	fprintf(stderr, "  the loose pass:\n");
	/* GoodTools' flags, the three this card had. */
	LOOSE("Brian Lara Cricket (E) (Jun 1995) [c][!]", "Brian Lara Cricket (Europe)", true);
	LOOSE("Xenon 2 Megablast (E) [c][!]", "Xenon 2 - Megablast (Europe)", true);
	LOOSE("Ys III - Wanderers From Ys (J) [T+Eng1.00]", "Ys III - Wanderers from Ys (Japan)", true);
	/* "The" where it opens or closes, never where it is part of a word. */
	LOOSE("The Legend of Zelda (U)", "Legend of Zelda, The (USA)", true);
	LOOSE("Theme Park (E)", "Park (Europe)", false);
	LOOSE("Legend of the Mystical Ninja (U)", "Legend of Mystical Ninja (USA)", false);
	/* And a subtitle's opening "The", which No-Intro writes and a card may
	 * not; "The" inside a subtitle still counts. */
	LOOSE("SNK Vs Capcom - Match of The Millennium (World)",
	      "SNK vs. Capcom - The Match of the Millennium (World) (En,Ja)", true);
	LOOSE("Zelda - Link to the Past (U)", "Zelda - A Link to the Past (USA)", false);
	LOOSE("Ys II - Ancient Ys Vanished - The Final Chapter (J)",
	      "Ys II - Ancient Ys Vanished - Final Chapter (Japan)", true);
	/* & as libretro files it, and spelled out. */
	LOOSE("Sonic & Knuckles (W)", "Sonic _ Knuckles (World)", true);
	LOOSE("Sonic and Knuckles (W)", "Sonic _ Knuckles (World)", true);
	/* Accents folded rather than dropped. */
	LOOSE("Pok\xc3\xa9mon Pinball (U)", "Pokemon Pinball (USA)", true);
	/* And different games stay different. */
	LOOSE("Xenon 2 Megablast (E)", "Xenon Megablast (Europe)", false);
	LOOSE("NCAA Final Four College Basketball (U)", "NCAA Final Four Basketball (USA)", false);
}

/* The subtitle rule, against the mono Neo Geo Pocket's real libretro names
 * (2026-10-05) and a Mega Man list where a title fits two games. */
#define SUBTITLE(base, names, want)                                           \
	do {                                                                      \
		char got[256] = "";                                                   \
		bool hit = art_match_subtitle((base), (names), got, sizeof got);      \
		if ((want) ? !hit || strcmp(got, (want)) : hit) {                     \
			fprintf(stderr, "  FAIL: %s by its title alone gave \"%s\","     \
			        " wanted \"%s\"\n", (base), hit ? got : "(none)",        \
			        (want) ? (want) : "(none)");                              \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static void check_subtitle(void)
{
	static const char *NGP =
		"Baseball Stars - Pocket Sports Series (Japan, Europe) (En,Ja)\n"
		"King of Fighters R-1 - Pocket Fighting Series (Japan, Europe) (En,Ja)\n"
		"King of Fighters R-1 - Pocket Kakutou Series _ Melon-chan no Seichou Nikki (Japan) (Demo)\n"
		"Melon-chan no Seichou Nikki (Japan)\n"
		"Pocket Tennis - Pocket Sports Series (Japan, Europe) (En,Ja)\n"
		"Samurai Shodown! - Pocket Fighting Series (Japan, Europe) (En,Ja)\n";
	static const char *MEGAMAN =
		"Mega Man - Dr. Wily's Revenge (USA)\n"
		"Mega Man - Dr. Wily's Revenge (Europe)\n"
		"Mega Man - Xtreme (USA)\n";

	fprintf(stderr, "  the subtitle rule:\n");
	SUBTITLE("Baseball Stars (World)", NGP,
	         "Baseball Stars - Pocket Sports Series (Japan, Europe) (En,Ja)");
	SUBTITLE("Pocket Tennis (World)", NGP,
	         "Pocket Tennis - Pocket Sports Series (Japan, Europe) (En,Ja)");
	SUBTITLE("Samurai Shodown! (World)", NGP,
	         "Samurai Shodown! - Pocket Fighting Series (Japan, Europe) (En,Ja)");
	/* Two games under one title: neither, rather than a guess. */
	SUBTITLE("King of Fighters R-1 (World)", NGP, NULL);
	SUBTITLE("Mega Man (U)", MEGAMAN, NULL);
	/* One game in two regions is still one game, and the region decides. */
	SUBTITLE("Mega Man (E)", "Mega Man - Dr. Wily's Revenge (USA)\n"
	         "Mega Man - Dr. Wily's Revenge (Europe)\n",
	         "Mega Man - Dr. Wily's Revenge (Europe)");
	/* A card title with a subtitle of its own is not this rule's. */
	SUBTITLE("Baseball Stars - Pro Edition (World)", NGP, NULL);
}

/* Which files are games, by systems.cfg's extension list. Box Art asks the
 * shelf's own rule (lib_ext_allowed), so a list the shelf reads it reads the
 * same: until 2026-11-06 it split on commas alone and took the file's
 * extension in lower case, so "nes sfc" showed a shelf of games and Box Art
 * found none to scrape. The shipped systems.cfg uses commas, which both read
 * alike. Through art_missing, in a scratch folder, so the folder walk a run
 * makes is the one checked. */
#define EXT(ext, list, want)                                                  \
	do {                                                                      \
		if (lib_ext_allowed((ext), (list)) != (want)) {                       \
			fprintf(stderr, "  FAIL: %s in \"%s\" should be%s allowed\n",    \
			        (ext) ? (ext) : "(none)", (list) ? (list) : "(null)",    \
			        (want) ? "" : " not");                                    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

#define GAMES(dir, list, want)                                                \
	do {                                                                      \
		int got = art_missing((dir), (list), NULL, NULL);                     \
		if (got != (want)) {                                                  \
			fprintf(stderr, "  FAIL: Box Art took %d games for \"%s\","       \
			        " wanted %d\n", got, (list), (want));                    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static void check_exts(void)
{
	static const char *const files[] = {
		"One.nes", "Two.SFC", "Three.sfc", "Notes.txt", "No extension"
	};
	char dir[] = "/tmp/artscrape-check-XXXXXX", p[256];
	size_t i;

	fprintf(stderr, "  which files are games:\n");
	EXT("sfc", "nes,sfc", true);
	EXT("sfc", "nes, sfc", true);
	EXT("sfc", "nes sfc", true);
	EXT("SFC", "nes,sfc", true);
	EXT("sfc", "NES SFC", true);
	EXT("sf", "nes,sfc", false);
	EXT("txt", "nes sfc", false);
	EXT(NULL, "nes,sfc", false);
	EXT("txt", "", true);
	EXT(NULL, NULL, true);

	if (!mkdtemp(dir)) { perror("mkdtemp"); failures++; return; }
	for (i = 0; i < sizeof files / sizeof files[0]; i++) {
		FILE *f;

		snprintf(p, sizeof p, "%s/%s", dir, files[i]);
		if ((f = fopen(p, "w"))) fclose(f);
	}
	GAMES(dir, "nes,sfc", 3);
	GAMES(dir, "nes, sfc", 3);
	GAMES(dir, "nes sfc", 3);
	GAMES(dir, "NES SFC", 3);
	GAMES(dir, "nes", 1);
	GAMES(dir, "", 5);
	/* A folder of games (src/shelfdir.h, 1.0.5): its games are the shelf's
	 * too, at every depth, and want art as theirs do; one with a cover in
	 * .media, by its name, does not */
	{
		FILE *f;

		snprintf(p, sizeof p, "%s/Hacks", dir); mkdir(p, 0700);
		snprintf(p, sizeof p, "%s/Hacks/Deep", dir); mkdir(p, 0700);
		snprintf(p, sizeof p, "%s/.media", dir); mkdir(p, 0700);
		snprintf(p, sizeof p, "%s/Hacks/Hack.nes", dir); if ((f = fopen(p, "w"))) fclose(f);
		snprintf(p, sizeof p, "%s/Hacks/Deep/Deeper.nes", dir); if ((f = fopen(p, "w"))) fclose(f);
		GAMES(dir, "nes", 3);
		snprintf(p, sizeof p, "%s/.media/Deeper.png", dir); if ((f = fopen(p, "w"))) { fputs("\x89PNG", f); fclose(f); }
		GAMES(dir, "nes", 2);
		snprintf(p, sizeof p, "%s/.media/Deeper.png", dir); unlink(p);
		snprintf(p, sizeof p, "%s/.media", dir); rmdir(p);
		snprintf(p, sizeof p, "%s/Hacks/Deep/Deeper.nes", dir); unlink(p);
		snprintf(p, sizeof p, "%s/Hacks/Hack.nes", dir); unlink(p);
		snprintf(p, sizeof p, "%s/Hacks/Deep", dir); rmdir(p);
		snprintf(p, sizeof p, "%s/Hacks", dir); rmdir(p);
	}
	for (i = 0; i < sizeof files / sizeof files[0]; i++) {
		snprintf(p, sizeof p, "%s/%s", dir, files[i]);
		unlink(p);
	}
	rmdir(dir);
}

/* Never called: this file exercises the matching rules, which run before any
 * source is asked anything. Present so artscrape.c links without dragging the
 * ScreenScraper client and its network in behind it. */
bool ss_signed_in(void) { return false; }
bool ss_run_begin(const char *a, const char *b, const char *c, const char *d,
                  const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
/* These legacy checks do not exercise metadata; title-bulk-check uses the
 * real ScreenScraper runner and database for that path. */
int ss_system_id(const char *folder) { (void)folder; return 0; }
bool ss_run_begin_metadata(const char *a, const char *b, const char *c,
                           const char *d, const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }

int  ss_run_step(void) { return -1; }
void ss_run_cancel(void) { }
int  ss_run_left(void) { return -1; }
bool ss_run_too_many(void) { return false; }
bool ss_run_video_wanted(const char *folder) { (void)folder; return false; }
bool ss_run_got_video(void) { return false; }

/* SteamGridDB is the last source and needs a key (src/sgdb.h); with none, as
 * here, artscrape.c never asks it, and these only let it link. */
bool sgdb_have_key(void);
bool sgdb_run_begin(const char *stem, const char *dest);
int  sgdb_run_step(void);
void sgdb_run_cancel(void);
bool sgdb_run_too_many(void);
bool sgdb_run_refused(void);
bool sgdb_run_no_answer(void);
bool sgdb_have_key(void) { return false; }
bool sgdb_run_begin(const char *stem, const char *dest) { (void)stem; (void)dest; return false; }
int  sgdb_run_step(void) { return -1; }
void sgdb_run_cancel(void) { }
bool sgdb_run_too_many(void) { return false; }
bool sgdb_run_refused(void) { return false; }
bool sgdb_run_no_answer(void) { return false; }

int main(int argc, char **argv)
{
	char line[512], out[512];
	size_t n;

	(void)argc; (void)argv;
	if (getenv("ART_SCORING")) {
		check_scoring();
		check_loose();
		check_subtitle();
		check_exts();
		if (failures) {
			fprintf(stderr, "\n  %d scoring check(s) failed\n", failures);
			return 1;
		}
		fprintf(stderr, "  ok: every candidate was chosen correctly\n");
		return 0;
	}
	/* One name per line in, one normalized form per line out. Deliberately
	 * dumb: the Python drives it, so this stays a pipe rather than growing
	 * its own idea of where a library is. */
	while (fgets(line, sizeof line, stdin)) {
		n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
		art_norm(line, out, sizeof out);
		printf("%s\n", out);
	}
	return 0;
}
