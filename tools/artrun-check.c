/* SPDX-License-Identifier: GPL-3.0-only */
/* Whole scraper runs, against a stubbed network.
 *
 *     make check-artrun
 *
 * The real src/artscrape.c, driven step by step to the end of a run with no
 * device and no download, for the two things its passes can get silently
 * wrong.
 *
 * REPLACE. It used to delete the cover and then run the scrape to fill the gap.
 * For a game libretro has no art for - a translation, homebrew, a cover added
 * by hand - there was nothing to fill it with, and the cover was simply gone.
 * Now it runs over that one game without deleting anything, and a download is
 * renamed over the old cover only once it has arrived. Pinned: a cover libretro
 * has replaces the old one, one it has not leaves the old one exactly as it was,
 * and a one-game run neither touches another game nor turns an over-long name
 * into a whole-system scrape.
 *
 * THE CHECKSUM PASS. For a game no name matched, the CRC in its zip names it
 * through No-Intro's list, and that name is matched against the catalog. A
 * wrong CRC is not an error anyone sees, it is a different game's cover, so
 * pinned: the ROM inside a zip is named rather than a readme beside it, a CRC
 * the list does not have stays missing, and a list that will not download
 * leaves the run finishing as it did before the pass existed.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "../src/artscrape.h"
#include "../src/db.h"
#include "../src/net.h"
#include "../src/ssrun.h"

#include <time.h>

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* The real one needs SDL_image, and nothing here is about resizing. */
void art_shrink(const char *path);
void art_shrink(const char *path) { (void)path; }

/* ---- the network, stubbed ------------------------------------------------
 *
 * The same contract as src/net.c: a GET is started and polled, and the file
 * appears at `path` only when the poll says 1 - written beside it first and
 * renamed over it, which is what makes a replace atomic. A request for anything
 * not being served answers -1 and leaves nothing behind. */
static const char *g_serve_image;    /* "Hit%20Game" is served, or NULL */
static const char *g_serve_also;     /* and a second, or NULL */
static const char *g_serve_dat;      /* the No-Intro list, or NULL for a 404 */
static const char *INDEX_OTHER =
	"<a href=\"Unrelated%20Title%20%28USA%29.png\">x</a>\n";
static const char *g_index = NULL;   /* the catalog; INDEX_OTHER when NULL */
static char g_pend_url[2048], g_pend_path[2048];
static bool g_pending;
static char g_urls[4096];            /* every URL asked for, one per line */
static int  g_nreq;
/* Both sources' requests in the order they were made: "N <url>" for the
 * network, "S <stem>" for a ScreenScraper lookup */
static char g_seq[8192];

static void seq_add(const char *kind, const char *what)
{
	size_t n = strlen(g_seq);

	snprintf(g_seq + n, sizeof g_seq - n, "%s %s\n", kind, what);
}

bool net_get_async(const char *url, const char *path, int timeout_s)
{
	(void)timeout_s;
	snprintf(g_pend_url, sizeof g_pend_url, "%s", url);
	snprintf(g_pend_path, sizeof g_pend_path, "%s", path);
	g_pending = true;
	g_nreq++;
	seq_add("N", url);
	if (strlen(g_urls) + strlen(url) + 2 < sizeof g_urls) {
		strcat(g_urls, url);
		strcat(g_urls, "\n");
	}
	return true;
}

static bool write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");

	if (!f) return false;
	fputs(text, f);
	return fclose(f) == 0;
}

/* A stand-in cover: a word to tell covers apart, behind a PNG's signature,
 * because a run counts a cover only when the file starts as a picture (a
 * link's text from libretro's mirror once passed for one, 2026-10-05). The
 * readers below hand back the word. */
#define PNG_SIG "\x89PNG"

static bool write_cover(const char *path, const char *word)
{
	char buf[64];

	snprintf(buf, sizeof buf, "%s%s", PNG_SIG, word);
	return write_file(path, buf);
}

static const char *word_of(const char *buf)
{
	return strncmp(buf, PNG_SIG, 4) ? buf : buf + 4;
}

int net_async_poll(void)
{
	char part[2100];
	const char *body = NULL;
	bool image = false;
	size_t n = strlen(g_pend_url);

	if (!g_pending) return -1;
	g_pending = false;

	if (n && g_pend_url[n - 1] == '/') {
		/* The catalog: by default other games only, so a fuzzy pass finds
		 * nothing. */
		body = g_index ? g_index : INDEX_OTHER;
	} else if (n > 4 && !strcmp(g_pend_url + n - 4, ".dat")) {
		body = g_serve_dat;
	} else if ((g_serve_image && strstr(g_pend_url, g_serve_image)) ||
	           (g_serve_also && strstr(g_pend_url, g_serve_also))) {
		body = "NEW";
		image = true;
	}
	if (!body) return -1;

	snprintf(part, sizeof part, "%s.part", g_pend_path);
	if (!(image ? write_cover(part, body) : write_file(part, body))) return -1;
	return rename(part, g_pend_path) == 0 ? 1 : -1;
}

void net_async_abort(void) { g_pending = false; }
int  net_async_http(void)  { return 200; }
int  net_async_ms(void)    { return 0; }
int  net_async_exit(void)  { return 0; }

/* ---- ScreenScraper, stubbed the same way ---------------------------------
 *
 * The real client is four calls with a step machine behind them and a whole
 * second network path; what this file needs to hold is the RUN's behavior
 * around it - that a hit costs libretro nothing, that a miss falls through,
 * and that a refusal stops the pass instead of hammering the server. So the
 * client is the thing stubbed, and the rules under test stay real. */
static bool g_ss_on;          /* is there an account */
static const char *g_ss_has;  /* the one stem they have a cover for, or NULL */
static int  g_ss_busy;        /* how many more lookups answer 429 */
static int  g_ss_left = -1;   /* what the account says is left today */
static int  g_ss_asked;       /* lookups started */
static char g_ss_stem[256];
static char g_ss_dest[512];
static bool g_ss_toomany;
/* Lookups refused for a reason true of every game after them (ss_run_refusal):
 * that reason, for the next g_ss_no_n lookups, or for all of them at -1 */
static ss_refusal g_ss_no = SS_GO_ON;
static int  g_ss_no_n = -1;
static ss_refusal g_ss_refusal;
static bool g_ss_unknown;     /* a game they have not got is a 404, SS_UNKNOWN */

bool ss_signed_in(void) { return g_ss_on; }

bool ss_run_begin(const char *folder, const char *file, const char *stem,
                  const char *rom_dir, const char *exts)
{
	(void)folder; (void)file; (void)exts;
	if (!g_ss_on) return false;
	snprintf(g_ss_stem, sizeof g_ss_stem, "%s", stem);
	snprintf(g_ss_dest, sizeof g_ss_dest, "%s/.media/%s.png", rom_dir, stem);
	g_ss_asked++;
	seq_add("S", stem);
	return true;
}

/* These legacy checks do not exercise metadata; title-bulk-check uses the
 * real ScreenScraper runner and database for that path. */
int ss_system_id(const char *folder) { (void)folder; return 0; }
bool ss_run_begin_metadata(const char *a, const char *b, const char *c,
                           const char *d, const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }

int ss_run_step(void)
{
	g_ss_toomany = false;
	g_ss_refusal = SS_GO_ON;
	if (g_ss_busy > 0) { g_ss_busy--; g_ss_toomany = true; return -1; }
	if (g_ss_no != SS_GO_ON && g_ss_no_n != 0) {
		if (g_ss_no_n > 0) g_ss_no_n--;
		g_ss_refusal = g_ss_no;
		return -1;
	}
	if (!g_ss_has || strcmp(g_ss_has, g_ss_stem)) {
		if (g_ss_unknown) g_ss_refusal = SS_UNKNOWN;
		return -1;
	}
	return write_cover(g_ss_dest, "SS") ? 0 : -1;
}

void ss_run_cancel(void) { g_ss_stem[0] = '\0'; }
int  ss_run_left(void) { return g_ss_left; }
bool ss_run_too_many(void) { return g_ss_toomany; }
ss_refusal ss_run_refusal(void) { return g_ss_refusal; }
bool ss_run_video_wanted(const char *folder) { (void)folder; return false; }
bool ss_run_got_video(void) { return false; }

/* ---- SteamGridDB, stubbed the same way -----------------------------------
 *
 * The client's own rules - the name match, the formats, the key in a header -
 * are tools/sgdb-check.c's, against the real client. What is held here is
 * where the run puts it: last, only for what libretro left, and stopped for
 * the run by the answers that will be true of every game after this one. */
static bool g_sg_key;            /* is there a key */
static const char *g_sg_has;     /* the one stem they have a cover for */
static int  g_sg_busy;           /* how many more answers are 429 */
static bool g_sg_refuse;         /* every answer a 401 */
static bool g_sg_silent;         /* nothing answers */
static int  g_sg_asked;
static char g_sg_stems[2048];    /* every stem asked about, one a line */
static char g_sg_stem[256], g_sg_dest[512];
static bool g_sg_toomany, g_sg_refused, g_sg_noanswer;

bool sgdb_have_key(void) { return g_sg_key; }

bool sgdb_run_begin(const char *stem, const char *dest)
{
	if (!g_sg_key) return false;
	snprintf(g_sg_stem, sizeof g_sg_stem, "%s", stem);
	snprintf(g_sg_dest, sizeof g_sg_dest, "%s", dest);
	if (strlen(g_sg_stems) + strlen(stem) + 2 < sizeof g_sg_stems) {
		strcat(g_sg_stems, stem);
		strcat(g_sg_stems, "\n");
	}
	g_sg_asked++;
	return true;
}

int sgdb_run_step(void)
{
	g_sg_toomany = g_sg_refused = g_sg_noanswer = false;
	if (g_sg_refuse) { g_sg_refused = true; return -1; }
	if (g_sg_busy > 0) { g_sg_busy--; g_sg_toomany = true; return -1; }
	if (g_sg_silent) { g_sg_noanswer = true; return -1; }
	if (!g_sg_has || strcmp(g_sg_has, g_sg_stem)) return -1;
	return write_cover(g_sg_dest, "SG") ? 0 : -1;
}

void sgdb_run_cancel(void) { g_sg_stem[0] = '\0'; }
bool sgdb_run_too_many(void) { return g_sg_toomany; }
bool sgdb_run_refused(void) { return g_sg_refused; }
bool sgdb_run_no_answer(void) { return g_sg_noanswer; }

static void sg_reset(void)
{
	g_sg_has = NULL;
	g_sg_busy = 0;
	g_sg_refuse = g_sg_silent = false;
	g_sg_asked = 0;
	g_sg_stems[0] = '\0';
}

/* ---- the card, in /tmp --------------------------------------------------- */

static char g_root[256], g_shelf[320], g_media[400];

static void make_card(void)
{
	char p[512];

	snprintf(g_root, sizeof g_root, "/tmp/zyperos-artrun-%ld", (long)getpid());
	snprintf(g_shelf, sizeof g_shelf, "%s/Game Boy", g_root);
	snprintf(g_media, sizeof g_media, "%s/.media", g_shelf);
	mkdir(g_root, 0755);
	mkdir(g_shelf, 0755);
	mkdir(g_media, 0755);
	snprintf(p, sizeof p, "%s/Hit Game.gb", g_shelf);    write_file(p, "rom");
	snprintf(p, sizeof p, "%s/Other Game.gb", g_shelf);  write_file(p, "rom");
}

static void set_cover(const char *text)
{
	char p[512];

	snprintf(p, sizeof p, "%s/Hit Game.png", g_media);
	write_cover(p, text);
}

static const char *cover(void)
{
	static char buf[64];
	char p[512];
	FILE *f;
	size_t n;

	snprintf(p, sizeof p, "%s/Hit Game.png", g_media);
	if (!(f = fopen(p, "rb"))) return "(gone)";
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return word_of(buf);
}

/* Any game's cover, by stem. */
static const char *cover_of(const char *stem)
{
	static char buf[64];
	char p[512];
	FILE *f;
	size_t n;

	snprintf(p, sizeof p, "%s/%s.png", g_media, stem);
	if (!(f = fopen(p, "rb"))) return "(none)";
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return word_of(buf);
}

/* A game's cover on another shelf, by stem */
static const char *cover_of_in(const char *shelf, const char *stem)
{
	static char buf[64];
	char p[600];
	FILE *f;
	size_t n;

	snprintf(p, sizeof p, "%s/.media/%s.png", shelf, stem);
	if (!(f = fopen(p, "rb"))) return "(none)";
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return word_of(buf);
}

static void put16(FILE *f, unsigned v)
{
	fputc((int)(v & 255), f);
	fputc((int)((v >> 8) & 255), f);
}

static void put32(FILE *f, unsigned long v)
{
	put16(f, (unsigned)(v & 0xFFFF));
	put16(f, (unsigned)((v >> 16) & 0xFFFF));
}

/* A stored zip holding `n` four-byte files. The CRCs are written as given, not
 * computed: the pass reads what the zip records and never hashes, so the test
 * can choose them. Local headers, central directory and end record, laid out
 * as the format says. */
static void write_zip(const char *path, int n, const char **names,
                      const unsigned long *crcs)
{
	FILE *f = fopen(path, "wb");
	long offs[8], cd, end;
	int i;

	if (!f) return;
	for (i = 0; i < n; i++) {
		offs[i] = ftell(f);
		put32(f, 0x04034b50UL);
		put16(f, 20); put16(f, 0); put16(f, 0); put16(f, 0); put16(f, 0);
		put32(f, crcs[i]); put32(f, 4); put32(f, 4);
		put16(f, (unsigned)strlen(names[i])); put16(f, 0);
		fputs(names[i], f);
		fputs("data", f);
	}
	cd = ftell(f);
	for (i = 0; i < n; i++) {
		put32(f, 0x02014b50UL);
		put16(f, 20); put16(f, 20); put16(f, 0); put16(f, 0); put16(f, 0); put16(f, 0);
		put32(f, crcs[i]); put32(f, 4); put32(f, 4);
		put16(f, (unsigned)strlen(names[i]));
		put16(f, 0); put16(f, 0); put16(f, 0); put16(f, 0);
		put32(f, 0); put32(f, (unsigned long)offs[i]);
		fputs(names[i], f);
	}
	end = ftell(f);
	put32(f, 0x06054b50UL);
	put16(f, 0); put16(f, 0); put16(f, (unsigned)n); put16(f, (unsigned)n);
	put32(f, (unsigned long)(end - cd)); put32(f, (unsigned long)cd);
	put16(f, 0);
	fclose(f);
}

static bool part_left(void)
{
	char p[512];
	struct stat st;

	snprintf(p, sizeof p, "%s/Hit Game.png.part", g_media);
	return stat(p, &st) == 0;
}

/* A whole run, to the end. Bounded, so a step machine that never finishes is a
 * failing check rather than a hung one. */
static int run_on(const char *folder, const char *only, art_progress *out)
{
	systems_cfg sys;
	int steps = 0, r;

	memset(&sys, 0, sizeof sys);
	sys.count = 1;
	snprintf(sys.systems[0].folder, sizeof sys.systems[0].folder, "%s", folder);
	snprintf(sys.systems[0].exts, sizeof sys.systems[0].exts, "gb,zip");

	g_urls[0] = '\0';
	g_nreq = 0;
	art_begin(&sys, g_root, only);
	while ((r = art_step()) == 1 && steps < 1000) steps++;
	art_status(out);
	return r;
}

static int run(const char *only, art_progress *out)
{
	return run_on("Game Boy", only, out);
}

int main(void)
{
	art_progress st;
	char big[1024];

	printf("artrun: whole scraper runs, against a stubbed network\n");
	make_card();

	printf("  a cover libretro has replaces the old one:\n");
	set_cover("OLD");
	g_serve_image = "Hit%20Game";
	CHECK(run("Hit Game", &st) == 0, "the run did not finish");
	CHECK(!strcmp(cover(), "NEW"), "the cover reads \"%s\", wanted NEW", cover());
	CHECK(st.found == 1, "found %d, wanted 1", st.found);
	CHECK(!strstr(g_urls, "Other%20Game"),
	      "a one-game replace asked about another game:\n%s", g_urls);
	CHECK(!part_left(), "a part-file was left beside the cover");

	printf("  a cover libretro has not got leaves the old one alone:\n");
	set_cover("OLD");
	g_serve_image = NULL;
	CHECK(run("Hit Game", &st) == 0, "the run did not finish");
	CHECK(!strcmp(cover(), "OLD"),
	      "the cover reads \"%s\" - a miss must keep the one it had", cover());
	CHECK(st.found == 0, "found %d on a miss", st.found);
	CHECK(strstr(g_urls, "Hit%20Game.png") != NULL,
	      "the game was never asked for, so the miss proves nothing:\n%s", g_urls);
	CHECK(!part_left(), "a part-file was left beside the cover");

	printf("  the ordinary run still skips a game that has art:\n");
	set_cover("OLD");
	g_serve_image = NULL;
	CHECK(run(NULL, &st) == 0, "the run did not finish");
	CHECK(!strstr(g_urls, "Hit%20Game"),
	      "the ordinary run asked for a game that already has art:\n%s", g_urls);
	CHECK(strstr(g_urls, "Other%20Game") != NULL,
	      "the ordinary run did not ask for the game with no art:\n%s", g_urls);
	CHECK(!strcmp(cover(), "OLD"), "the ordinary run changed a cover it skipped");

	/* What libretro's GitHub mirror answers for a duplicate cover: the text
	 * of a link, the name of the file it points at. Four landed as covers on
	 * 2026-10-05 and, counted as art, were never fetched again. */
	printf("  a cover that is no picture is fetched again:\n");
	{
		char p[512];

		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);
		write_file(p, "Hit Game (USA).png");
	}
	g_serve_image = "Hit%20Game";
	CHECK(run(NULL, &st) == 0, "the run did not finish");
	CHECK(strstr(g_urls, "Hit%20Game") != NULL,
	      "a link's text passed for a cover and was skipped:\n%s", g_urls);
	CHECK(!strcmp(cover(), "NEW"), "the cover reads \"%s\", wanted NEW", cover());

	printf("  a name too long to hold is refused, not widened to the shelf:\n");
	memset(big, 'x', sizeof big - 1);
	big[sizeof big - 1] = '\0';
	CHECK(run(big, &st) == 0, "the run did not finish");
	CHECK(g_nreq == 0, "%d request(s) for a refused name:\n%s", g_nreq, g_urls);

	/* ---- the checksum pass ---------------------------------------------- */
	{
		const char *names[] = { "readme.txt", "Old Name (USA).gb" };
		const unsigned long crcs[] = { 0xDEADBEEFUL, 0x1234ABCDUL };
		char p[512];
		/* The readme's CRC names a game the catalog also has. Taking the
		 * first entry in the zip would fetch that one - a wrong cover, which
		 * is exactly the failure nothing on screen would report. */
		const char *LIST =
			"clrmamepro (\n\tname \"Nintendo - Game Boy\"\n)\n\n"
			"game (\n\tname \"Readme Decoy (USA)\"\n"
			"\trom ( name \"Readme Decoy (USA).gb\" size 4 crc DEADBEEF md5 00 )\n)\n"
			"game (\n\tname \"New Name - Subtitle (USA)\"\n"
			"\trom ( name \"New Name - Subtitle (USA).gb\" size 4 crc 1234ABCD md5 00 )\n)\n";
		const char *CATALOG =
			"<a href=\"Readme%20Decoy%20%28USA%29.png\">x</a>\n"
			"<a href=\"New%20Name%20-%20Subtitle%20%28USA%29.png\">x</a>\n";

		snprintf(p, sizeof p, "%s/Old Name (USA).zip", g_shelf);
		write_zip(p, 2, names, crcs);
		g_index = CATALOG;

		printf("  a zip no name reaches is named by the ROM's checksum:\n");
		g_serve_dat = LIST;
		/* Both candidates are served, so only the name chosen decides which
		 * cover lands - and the file's own name is not, so it cannot be found
		 * by name first. */
		g_serve_image = "New%20Name%20-%20Subtitle";
		g_serve_also = "Readme%20Decoy";
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(strstr(g_urls, "metadat/no-intro/Nintendo%20-%20Game%20Boy.dat") != NULL,
		      "the No-Intro list was never asked for:\n%s", g_urls);
		CHECK(strstr(g_urls, "New%20Name%20-%20Subtitle%20%28USA%29.png") != NULL,
		      "the cover No-Intro names for the ROM was not fetched:\n%s", g_urls);
		CHECK(!strstr(g_urls, "Readme%20Decoy"),
		      "the readme's checksum named the game:\n%s", g_urls);
		CHECK(!strcmp(cover_of("Old Name (USA)"), "NEW"),
		      "the cover reads \"%s\", wanted NEW", cover_of("Old Name (USA)"));

		printf("  a checksum the list does not have stays missing:\n");
		snprintf(p, sizeof p, "%s/Old Name (USA).png", g_media);
		remove(p);
		g_serve_dat = "clrmamepro (\n\tname \"Nintendo - Game Boy\"\n)\n";
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Old Name (USA)"), "(none)"),
		      "a cover appeared for a checksum nobody lists");
		CHECK(!strstr(g_urls, "New%20Name"), "a cover was fetched with no name to go on");

		printf("  a list that will not download leaves the run as it was:\n");
		g_serve_dat = NULL;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Old Name (USA)"), "(none)"),
		      "a cover appeared with no list to name it");
		CHECK(st.missing >= 2, "missing %d - the unnamed games were not counted",
		      st.missing);
		g_index = NULL;
		g_serve_also = NULL;
	}

	/* ---- the second source, which goes first ---------------------------
	 *
	 * Four rules, and the last two are the ones that had to exist before a run
	 * over 1,708 games could: a refusal that means "not now" is worth one more
	 * ask, and a day that is spent is worth none. */
	{
		char p[512];

		printf("  ScreenScraper goes first, and libretro is never asked:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		g_ss_on = true;
		g_ss_has = "Hit Game";
		g_ss_busy = 0;
		g_ss_left = -1;
		g_ss_asked = 0;
		g_serve_image = "Hit%20Game";
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Hit Game"), "SS"),
		      "the cover reads \"%s\", wanted the ScreenScraper one",
		      cover_of("Hit Game"));
		CHECK(!strstr(g_urls, "Hit%20Game"),
		      "libretro was asked for a cover ScreenScraper had already:\n%s",
		      g_urls);

		printf("  a game they have not got falls through to libretro:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_has = NULL;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "the cover reads \"%s\", wanted libretro's", cover_of("Hit Game"));

		printf("  \"too many at once\" is asked again, once:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_has = "Hit Game";
		g_ss_busy = 1;                  /* refuse the first ask only */
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Hit Game"), "SS"),
		      "a 429 was not retried: the cover reads \"%s\"",
		      cover_of("Hit Game"));
		CHECK(g_ss_asked >= 2, "asked %d times, wanted the game asked again",
		      g_ss_asked);

		printf("  refused twice, it stops and leaves the rest to libretro:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		g_ss_busy = 99;                 /* refuse everything */
		g_ss_asked = 0;
		g_serve_image = "Hit%20Game";
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 2, "asked %d times, wanted two and then a stop",
		      g_ss_asked);
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro did not finish the job after the pass stopped");
		CHECK(strstr(st.problem, "too many") != NULL,
		      "the run did not say why it stopped: \"%s\"", st.problem);

		printf("  a spent day is not asked at all:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_busy = 0;
		g_ss_left = 10;                 /* under SS_DAY_FLOOR */
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 0, "asked %d times with the day spent", g_ss_asked);
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro did not cover for the spent quota");
		CHECK(strstr(st.problem, "quota") != NULL,
		      "the run did not say the quota was spent: \"%s\"", st.problem);

		/* The server's own "not today" and "closed", and no answer: each
		 * true of every game after it. They were "a game they do not
		 * have" once, and a run asked every game of the shelf, refused,
		 * before libretro had its turn (Discord, 1.0.4). */
		printf("  a day the server says is spent (430, 431) stops it at once:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		g_ss_left = -1;
		g_ss_no = SS_SPENT;
		g_ss_no_n = -1;
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 1, "asked %d times, wanted one and then a stop",
		      g_ss_asked);
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro did not finish the job after the pass stopped");
		CHECK(strstr(st.problem, "quota") != NULL,
		      "the run did not say the quota was spent: \"%s\"", st.problem);

		printf("  a server closed to the account (401, 423) stops it at once:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_no = SS_CLOSED;
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 1, "asked %d times, wanted one and then a stop",
		      g_ss_asked);
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro did not finish the job after the pass stopped");
		CHECK(strstr(st.problem, "busy") != NULL,
		      "the run did not say the server is busy: \"%s\"", st.problem);

		/* A third game, so that "stopped after two" and "asked them all"
		 * are different counts whatever came before. With the checksum
		 * pass's Old Name (USA), the shelf now has four with no cover. */
		snprintf(p, sizeof p, "%s/Third Game.gb", g_shelf); write_file(p, "rom");

		printf("  no answer twice in a row stops it:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_no = SS_NO_ANSWER;
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 2, "asked %d times, wanted two and then a stop",
		      g_ss_asked);
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro did not finish the job after the pass stopped");
		CHECK(strstr(st.problem, "no answer") != NULL,
		      "the run did not say there was no answer: \"%s\"", st.problem);

		printf("  one with no answer is a blip, and the rest are asked:\n");
		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);  remove(p);
		g_ss_no_n = 1;
		g_ss_asked = 0;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 4, "asked %d times, wanted all four", g_ss_asked);
		CHECK(!st.problem[0], "a single blip stopped the run: \"%s\"", st.problem);

		snprintf(p, sizeof p, "%s/Third Game.gb", g_shelf); remove(p);
		snprintf(p, sizeof p, "%s/Third Game.png", g_media); remove(p);
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		g_ss_no = SS_GO_ON;
		g_ss_no_n = -1;
		g_ss_on = false;
		g_ss_left = -1;
	}

	/* ---- a console ScreenScraper does not know (2026-11-13) -------------
	 *
	 * Twelve games it has never heard of, all of them on libretro. Libretro
	 * is asked about each game right after ScreenScraper's miss, not after
	 * the whole console; ten misses in a row leave the rest of the console
	 * to libretro; the misses are kept, so the next run does not ask again,
	 * until they are a month old. And the count moves with every game.
	 * Whatever order the folder lists them in. */
	{
		char shelf[400], p[512];
		const char *u;
		systems_cfg sys;
		int i, steps = 0, direct = 0;
		long long now = (long long)time(NULL);

		CHECK(db_init(":memory:", ":memory:", NULL), "no database to keep misses in");
		snprintf(shelf, sizeof shelf, "%s/Game Boy Color", g_root);
		mkdir(shelf, 0755);
		for (i = 1; i <= 12; i++) {
			snprintf(p, sizeof p, "%s/Game %02d.gb", shelf, i);
			write_file(p, "rom");
		}
		memset(&sys, 0, sizeof sys);
		sys.count = 1;
		snprintf(sys.systems[0].folder, sizeof sys.systems[0].folder, "Game Boy Color");
		snprintf(sys.systems[0].exts, sizeof sys.systems[0].exts, "gb");
		g_ss_on = true;
		g_ss_has = NULL;
		g_ss_unknown = true;
		g_ss_busy = 0;
		g_ss_left = -1;
		g_serve_image = "Game%20";
		g_serve_also = NULL;
		g_index = NULL;

		printf("  a miss is libretro's at once, and the count moves with every game:\n");
		g_urls[0] = g_seq[0] = '\0';
		g_ss_asked = 0;
		art_begin(&sys, g_root, NULL);
		while (g_ss_asked < 2 && art_step() == 1 && steps < 1000) steps++;
		art_status(&st);
		CHECK(st.found == 1 && st.lr_found == 1,
		      "libretro's cover for the first game waited: %d found", st.found);
		CHECK(st.looked == 1, "looked %d, wanted the first game counted", st.looked);
		CHECK(st.src == ART_SRC_SS, "the dot is not on ScreenScraper (%d)", st.src);
		{
			/* "S Game NN", then libretro for Game NN, then the next lookup */
			char first[16], want[64];

			CHECK(sscanf(g_seq, "S Game %15[0-9]", first) == 1, "no first lookup:\n%s", g_seq);
			snprintf(want, sizeof want, "Game%%20%s.png\nS Game ", first);
			CHECK(strstr(g_seq, want) != NULL,
			      "libretro was not asked before the next game:\n%s", g_seq);
		}
		while (art_step() == 1 && steps < 2000) steps++;
		art_status(&st);

		printf("  ten misses in a row leave the rest of the console to libretro:\n");
		CHECK(g_ss_asked == 10, "ScreenScraper asked %d times, wanted ten", g_ss_asked);
		for (u = g_urls; (u = strstr(u, "Named_Boxarts/Game%20")); u++) direct++;
		CHECK(direct == 12, "libretro asked %d times by name, wanted each game once", direct);
		CHECK(st.looked == 12, "looked %d, wanted all twelve", st.looked);
		CHECK(st.found == 12 && !strcmp(cover_of_in(shelf, "Game 01"), "NEW"),
		      "libretro's covers are not there: %d found", st.found);

		printf("  the misses are kept, and the next run does not ask again:\n");
		for (i = 1; i <= 12; i++) {
			snprintf(p, sizeof p, "%s/.media/Game %02d.png", shelf, i);
			remove(p);
		}
		g_serve_image = NULL;
		g_ss_asked = 0;
		CHECK(run_on("Game Boy Color", NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 2, "asked %d times, wanted only the two never asked", g_ss_asked);

		printf("  a miss a month old is asked about again:\n");
		CHECK(db_ss_miss_set(db_lib(), "Game Boy Color", "Game 05.gb", now - 31 * 86400LL),
		      "could not age a miss");
		g_ss_asked = 0;
		g_seq[0] = '\0';
		CHECK(run_on("Game Boy Color", NULL, &st) == 0, "the run did not finish");
		CHECK(g_ss_asked == 1 && strstr(g_seq, "S Game 05"),
		      "asked %d times, wanted Game 05 alone:\n%s", g_ss_asked, g_seq);

		for (i = 1; i <= 12; i++) {
			snprintf(p, sizeof p, "%s/Game %02d.gb", shelf, i);         remove(p);
			snprintf(p, sizeof p, "%s/.media/Game %02d.png", shelf, i); remove(p);
		}
		snprintf(p, sizeof p, "%s/.media", shelf); rmdir(p);
		rmdir(shelf);
		db_shutdown();
		g_ss_unknown = false;
		g_ss_on = false;
		g_serve_image = NULL;
	}

	/* ---- the last source: SteamGridDB, with a key ----------------------- */
	{
		char p[512];

		snprintf(p, sizeof p, "%s/Hit Game.png", g_media);   remove(p);
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);

		printf("  with no key, SteamGridDB is never asked:\n");
		sg_reset();
		g_sg_key = false;
		g_serve_image = NULL;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_sg_asked == 0, "asked %d times with no key", g_sg_asked);

		printf("  it is asked only for what libretro left:\n");
		sg_reset();
		g_sg_key = true;
		g_sg_has = "Other Game";
		g_serve_image = "Hit%20Game";
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Hit Game"), "NEW"),
		      "libretro's cover reads \"%s\"", cover_of("Hit Game"));
		CHECK(!strcmp(cover_of("Other Game"), "SG"),
		      "the cover libretro has not got reads \"%s\", wanted SteamGridDB's",
		      cover_of("Other Game"));
		CHECK(!strstr(g_sg_stems, "Hit Game"),
		      "SteamGridDB was asked for a game libretro had:\n%s", g_sg_stems);
		CHECK(st.found >= 2, "found %d, wanted both", st.found);

		printf("  \"too many at once\" is asked again, once:\n");
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		sg_reset();
		g_sg_has = "Other Game";
		g_sg_busy = 1;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover_of("Other Game"), "SG"),
		      "a 429 was not retried: the cover reads \"%s\"", cover_of("Other Game"));
		{
			/* Whichever game the 429 fell on is the one asked again */
			char first[300];
			const char *nl = strchr(g_sg_stems, '\n');
			size_t k = nl ? (size_t)(nl - g_sg_stems) + 1 : 0;

			snprintf(first, sizeof first, "%.*s%.*s", (int)k, g_sg_stems,
			         (int)k, g_sg_stems);
			CHECK(k && !strncmp(g_sg_stems, first, 2 * k),
			      "the refused game was not asked again:\n%s", g_sg_stems);
		}

		printf("  refused twice, the pass stops:\n");
		snprintf(p, sizeof p, "%s/Other Game.png", g_media); remove(p);
		sg_reset();
		g_sg_has = "Other Game";
		g_sg_busy = 99;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_sg_asked == 2, "asked %d times, wanted two and then a stop", g_sg_asked);
		CHECK(strstr(st.problem, "SteamGridDB: too busy") != NULL,
		      "the run did not say why it stopped: \"%s\"", st.problem);

		printf("  a refused key stops the pass at once and says so:\n");
		sg_reset();
		g_sg_refuse = true;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_sg_asked == 1, "asked %d times with a key they refuse", g_sg_asked);
		CHECK(strstr(st.problem, "key refused") != NULL,
		      "the run did not say the key was refused: \"%s\"", st.problem);
		/* The run's closing heading: about 25 characters fit at 640 wide */
		CHECK(strlen(st.problem) <= 25, "\"%s\" is too long for the heading", st.problem);

		printf("  no answer twice in a row stops it too:\n");
		sg_reset();
		g_sg_silent = true;
		CHECK(run(NULL, &st) == 0, "the run did not finish");
		CHECK(g_sg_asked == 2, "asked %d times with nothing answering", g_sg_asked);
		CHECK(strstr(st.problem, "SteamGridDB: no answer") != NULL,
		      "the run did not say nothing answered: \"%s\"", st.problem);

		printf("  a replace takes SteamGridDB's cover when libretro has none:\n");
		sg_reset();
		set_cover("OLD");
		g_serve_image = NULL;
		g_sg_has = "Hit Game";
		CHECK(run("Hit Game", &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover(), "SG"), "the cover reads \"%s\", wanted SG", cover());
		CHECK(!strstr(g_sg_stems, "Other Game"),
		      "a one-game replace asked about another game:\n%s", g_sg_stems);

		printf("  and a miss there keeps the cover it had:\n");
		sg_reset();
		set_cover("OLD");
		CHECK(run("Hit Game", &st) == 0, "the run did not finish");
		CHECK(!strcmp(cover(), "OLD"),
		      "the cover reads \"%s\" - a miss must keep the one it had", cover());
		CHECK(g_sg_asked == 1, "the game was not asked about, so the miss proves nothing");

		/* A shelf libretro files nothing under: it used to end at "not in
		 * the table", and with a key it is SteamGridDB's to answer. */
		printf("  a shelf libretro has no collection for is asked about:\n");
		snprintf(p, sizeof p, "%s/Homebrew Shelf", g_root);  mkdir(p, 0755);
		snprintf(p, sizeof p, "%s/Homebrew Shelf/Indie Game.gb", g_root);
		write_file(p, "rom");
		sg_reset();
		g_sg_has = "Indie Game";
		CHECK(run_on("Homebrew Shelf", NULL, &st) == 0, "the run did not finish");
		CHECK(st.found == 1, "found %d on the shelf, wanted 1", st.found);
		CHECK(!st.problem[0], "a shelf SteamGridDB served reports \"%s\"", st.problem);
		snprintf(p, sizeof p, "%s/Homebrew Shelf/.media/Indie Game.png", g_root);
		remove(p);
		g_sg_key = false;
		CHECK(run_on("Homebrew Shelf", NULL, &st) == 0, "the run did not finish");
		CHECK(strstr(st.problem, "not in the table") != NULL,
		      "with no key the shelf should say it is not in the table: \"%s\"",
		      st.problem);
		sg_reset();
	}

	{
		char cmd[400];
		snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", g_root);
	}

	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
