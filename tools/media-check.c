/* A game's Cover and Video page, the parts a device cannot show you
 * (DESIGN.md 11.34):
 *
 *   make check-media
 *
 *   - where a cover came from (src/artsrc.h): the record's line, read back
 *     as written, a line that is not one refused, and two records the same
 *     picture only when source, kind, region and artist all are;
 *   - Use as Cover and Put Back on a card of small files (src/mediafile.c):
 *     the picture in as the cover, the cover there was to .media/.before,
 *     the records with them, and Put Back swapping both back, twice over;
 *     a game with no cover keeps no Before; a copy that fails changes
 *     nothing;
 *   - an MP4's length from its mvhd, both versions, and none from what is
 *     not one.
 *
 * Links src/mediafile.c, src/artsrc.c and src/db.c, and not SDL or the
 * network.
 */
#include "../src/artsrc.h"
#include "../src/db.h"
#include "../src/mediafetch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-media-check-XXXXXX";
static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); failures++; } } while (0)

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");

	if (f) { fputs(text, f); fclose(f); }
}

static bool holds(const char *path, const char *text)
{
	char buf[256] = "";
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return false;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return !strcmp(buf, text);
}

static void records(void)
{
	art_source a = { AS_SS, AK_BOX, "us", "" }, b, c;
	char line[256];

	printf("where a cover came from, as a line:\n");
	CHECK(artsrc_format(&a, line, sizeof line) && !strcmp(line, "ss|box|us|"), "wrote \"%s\"", line);
	CHECK(artsrc_parse(line, &b) && artsrc_same(&a, &b), "read back as written");
	a = (art_source){ AS_SG, AK_FAN, "", "Midgarsormr" };
	CHECK(artsrc_format(&a, line, sizeof line) && artsrc_parse(line, &b) && artsrc_same(&a, &b) &&
	      !strcmp(b.by, "Midgarsormr"), "an artist kept: \"%s\"", line);
	a = (art_source){ AS_LR, AK_BOX, "", "Pipe|Name" };
	CHECK(!artsrc_format(&a, line, sizeof line), "a name with | in it is not written");
	CHECK(!artsrc_parse("xx|box||", &b) && b.src == AS_NONE, "a source it does not know is none");
	CHECK(!artsrc_parse("ss|poster||", &b), "nor a kind");
	CHECK(!artsrc_parse("ss|box", &b), "nor half a line");
	a = (art_source){ AS_SS, AK_BOX, "us", "" };
	c = (art_source){ AS_SS, AK_BOX, "jp", "" };
	CHECK(!artsrc_same(&a, &c), "another region is another picture");
	c = (art_source){ AS_LR, AK_BOX, "us", "" };
	CHECK(!artsrc_same(&a, &c), "another source too");
	b.src = AS_NONE;
	CHECK(!artsrc_same(&b, &b), "and no record is the same as nothing");
	CHECK(!strcmp(artsrc_name(AS_SS), "ScreenScraper") && !strcmp(artsrc_name(AS_LR), "libretro") &&
	      !strcmp(artsrc_name(AS_SG), "SteamGridDB"), "the sources by their own names");
}

static void use_and_put_back(void)
{
	char roms[600], dest[1600], before[1600], from[700], p[1600];
	art_source us = { AS_SS, AK_BOX, "us", "" }, jp = { AS_LR, AK_BOX, "", "Game (Japan)" }, got;
	struct stat st;

	printf("Use as Cover and Put Back on a card:\n");
	snprintf(roms, sizeof roms, "%s/Roms", root);
	snprintf(p, sizeof p, "%s/SNES", roms);
	mkdir(roms, 0777);
	mkdir(p, 0777);
	snprintf(from, sizeof from, "%s/offer.img", root);

	/* A game with no cover: the picture comes in, nothing goes to Before */
	put(from, "us-box");
	CHECK(mf_file_use(roms, "SNES", "Game", from, &us, dest, sizeof dest), "the first cover came in");
	CHECK(holds(dest, "us-box"), "as the cover");
	CHECK(mf_before_path(roms, "SNES", "Game", before, sizeof before) && stat(before, &st) != 0,
	      "and nothing is Before, as nothing was there");
	CHECK(artsrc_get("SNES", "Game", &got) && artsrc_same(&got, &us), "recorded as ScreenScraper's");
	CHECK(!artsrc_get_before("SNES", "Game", &got), "and no record of a Before");
	CHECK(!mf_file_put_back(roms, "SNES", "Game"), "Put Back has nothing to put back");

	/* Another: the one there was goes to Before, its record with it */
	put(from, "jp-box");
	CHECK(mf_file_use(roms, "SNES", "Game", from, &jp, dest, sizeof dest), "a second cover came in");
	CHECK(holds(dest, "jp-box") && holds(before, "us-box"), "the first is Before now");
	CHECK(artsrc_get("SNES", "Game", &got) && artsrc_same(&got, &jp), "the shelf's record is the second's");
	CHECK(artsrc_get_before("SNES", "Game", &got) && artsrc_same(&got, &us), "Before's the first's");
	snprintf(p, sizeof p, "%s/SNES/.media/Game.png.pick", roms);
	CHECK(stat(p, &st) != 0, "and the copy it came in as is gone");

	/* Put Back, and back again */
	CHECK(mf_file_put_back(roms, "SNES", "Game"), "Put Back");
	CHECK(holds(dest, "us-box") && holds(before, "jp-box"), "the two changed places");
	CHECK(artsrc_get("SNES", "Game", &got) && artsrc_same(&got, &us) &&
	      artsrc_get_before("SNES", "Game", &got) && artsrc_same(&got, &jp), "and their records");
	CHECK(mf_file_put_back(roms, "SNES", "Game") && holds(dest, "jp-box") && holds(before, "us-box"),
	      "and back again: one level, either way");

	/* A copy that cannot be made changes nothing */
	snprintf(from, sizeof from, "%s/not-there.img", root);
	CHECK(!mf_file_use(roms, "SNES", "Game", from, &us, dest, sizeof dest), "a picture that is not there");
	CHECK(holds(dest, "jp-box") && holds(before, "us-box"), "leaves the cover and Before as they were");
	CHECK(artsrc_get("SNES", "Game", &got) && artsrc_same(&got, &jp), "and the record");

	artsrc_forget("SNES", "Game");
	CHECK(!artsrc_get("SNES", "Game", &got) && !artsrc_get_before("SNES", "Game", &got),
	      "a deleted game's records go");
}

static void be32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* ftyp, then free, then moov holding a trak stand-in before its mvhd */
static void write_mp4(const char *path, int version, uint32_t scale, uint64_t dur)
{
	unsigned char b[256];
	int n = 0, moov, mvhd_len = version ? 8 + 4 + 16 + 4 + 8 : 8 + 4 + 8 + 4 + 4;
	FILE *f = fopen(path, "wb");

	memset(b, 0, sizeof b);
	be32(b + n, 16); memcpy(b + n + 4, "ftypisom", 8); n += 16;
	be32(b + n, 12); memcpy(b + n + 4, "free", 4); n += 12;
	moov = n;
	memcpy(b + n + 4, "moov", 4); n += 8;
	be32(b + n, 16); memcpy(b + n + 4, "trak", 4); n += 16;
	be32(b + n, (uint32_t)mvhd_len); memcpy(b + n + 4, "mvhd", 4);
	b[n + 8] = (unsigned char)version;
	if (version) {
		be32(b + n + 8 + 4 + 16, scale);
		be32(b + n + 8 + 4 + 16 + 4, (uint32_t)(dur >> 32));
		be32(b + n + 8 + 4 + 16 + 8, (uint32_t)dur);
	} else {
		be32(b + n + 8 + 4 + 8, scale);
		be32(b + n + 8 + 4 + 8 + 4, (uint32_t)dur);
	}
	n += mvhd_len;
	be32(b + moov, (uint32_t)(n - moov));
	if (f) { fwrite(b, 1, (size_t)n, f); fclose(f); }
}

static void lengths(void)
{
	char p[700];

	printf("an MP4's length:\n");
	snprintf(p, sizeof p, "%s/v0.mp4", root);
	write_mp4(p, 0, 600, 7300);
	CHECK(mf_mp4_seconds(p) == 12, "version 0: %d s, not 12", mf_mp4_seconds(p));
	snprintf(p, sizeof p, "%s/v1.mp4", root);
	write_mp4(p, 1, 1000, 30500);
	CHECK(mf_mp4_seconds(p) == 31, "version 1: %d s, not 31 (rounded)", mf_mp4_seconds(p));
	snprintf(p, sizeof p, "%s/not.mp4", root);
	put(p, "<html>a refusal's page</html>");
	CHECK(mf_mp4_seconds(p) == -1, "a page is no video");
	CHECK(mf_mp4_seconds("/nonexistent/x.mp4") == -1, "nor nothing");
}

int main(void)
{
	char dev[700], lib[700];

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(dev, sizeof dev, "%s/device.db", root);
	snprintf(lib, sizeof lib, "%s/library.db", root);
	if (!db_init(dev, lib, NULL)) { printf("FAIL: no database\n"); return 1; }
	records();
	use_and_put_back();
	lengths();
	db_shutdown();
	{
		char cmd[800];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) { printf("media: %d FAILED\n", failures); return 1; }
	printf("ok: cover records, Use as Cover and Put Back, and a video's length\n");
	return 0;
}
