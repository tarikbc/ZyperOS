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
 *     not one: crafted files (a box of size 0, a 64-bit size past the file
 *     or under its header, a box past its parent or the file, an empty or
 *     short mvhd, 5000 boxes before the moov) all say -1, read in bounds
 *     (the check is built with AddressSanitizer and UBSan where the
 *     compiler has them).
 *
 * And whether there is a network to ask (mf_unreachable): begun offline,
 * or every source asked gave no answer, as on a Pixel 2 with no Wi-Fi
 * adapter whose USB cable's network is up.
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

/* ---- crafted files: a snap is whatever a server sent ----------------------- */

static unsigned char buf[64 * 1024];
static size_t blen;

static void b_box(uint32_t size, const char *type)
{
	be32(buf + blen, size);
	memcpy(buf + blen + 4, type, 4);
	blen += 8;
}

static void b_bytes(const void *p, size_t n)
{
	memcpy(buf + blen, p, n);
	blen += n;
}

static void b_zero(size_t n)
{
	memset(buf + blen, 0, n);
	blen += n;
}

static int crafted(const char *name)
{
	char p[700];
	FILE *f;

	snprintf(p, sizeof p, "%s/%s.mp4", root, name);
	if ((f = fopen(p, "wb"))) { fwrite(buf, 1, blen, f); fclose(f); }
	return mf_mp4_seconds(p);
}

static void crafted_files(void)
{
	static const unsigned char huge[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0 };
	static const unsigned char tiny[8] = { 0, 0, 0, 0, 0, 0, 0, 15 };
	int i, s;

	printf("crafted files, each no length and nothing read past the file:\n");

	/* A first box of size 0 runs to the file's end: no moov after it */
	blen = 0;
	b_box(0, "ftyp");
	b_zero(16);
	CHECK((s = crafted("size0")) == -1, "a box of size 0 at the top: %d", s);

	/* Size 1 and a 64-bit size far past the file */
	blen = 0;
	b_box(1, "moov");
	b_bytes(huge, 8);
	b_zero(32);
	CHECK((s = crafted("huge")) == -1, "a 64-bit size past LONG_MAX: %d", s);

	/* Size 1 and a 64-bit size shorter than its own header */
	blen = 0;
	b_box(1, "moov");
	b_bytes(tiny, 8);
	b_zero(32);
	CHECK((s = crafted("under")) == -1, "a 64-bit size under its header: %d", s);

	/* An mvhd whose 64-bit size is negative as a long: its length must not
	 * become a read of the whole address space */
	{
		static const unsigned char neg[8] = { 0x80, 0, 0, 0, 0, 0, 0, 0x30 };

		blen = 0;
		b_box(72, "moov");
		b_box(1, "mvhd");
		b_bytes(neg, 8);
		b_zero(48);
		CHECK((s = crafted("mvhd-negative")) == -1, "an mvhd of a negative size: %d", s);
	}

	/* A box that says it is longer than the file */
	blen = 0;
	b_box(16, "ftyp");
	b_zero(8);
	b_box(1000, "moov");
	b_box(28, "mvhd");
	b_zero(20);
	CHECK((s = crafted("past")) == -1, "a moov past the file's end: %d", s);

	/* An mvhd past its moov */
	blen = 0;
	b_box(16, "moov");
	b_box(28, "mvhd");
	b_zero(20);
	CHECK((s = crafted("past-parent")) == -1, "an mvhd past its moov: %d", s);

	/* An mvhd with no body, a few bytes of one, and version 1 cut short */
	blen = 0;
	b_box(16, "moov");
	b_box(8, "mvhd");
	CHECK((s = crafted("mvhd-empty")) == -1, "an empty mvhd: %d", s);
	blen = 0;
	b_box(20, "moov");
	b_box(12, "mvhd");
	b_zero(4);
	CHECK((s = crafted("mvhd-tiny")) == -1, "an mvhd of 4 bytes: %d", s);
	blen = 0;
	b_box(36, "moov");
	b_box(28, "mvhd");
	b_zero(20);
	buf[16] = 1;                       /* version 1 wants 32 */
	CHECK((s = crafted("mvhd-v1-short")) == -1, "version 1 in 20 bytes: %d", s);

	/* A box shorter than a header */
	blen = 0;
	b_box(4, "free");
	b_zero(4);
	b_box(36, "moov");
	CHECK((s = crafted("short-box")) == -1, "a box of 4 bytes: %d", s);

	/* More boxes than any file has, before the moov: the walk stops */
	blen = 0;
	for (i = 0; i < 5000; i++) b_box(8, "free");
	b_box(36, "moov");
	b_box(28, "mvhd");
	b_zero(20);
	be32(buf + blen - 8, 600);
	be32(buf + blen - 4, 6000);
	CHECK((s = crafted("many")) == -1, "5000 boxes before the moov: %d", s);

	/* A scale of 0, and a 64-bit duration that is no int of seconds */
	blen = 0;
	b_box(36, "moov");
	b_box(28, "mvhd");
	b_zero(20);
	CHECK((s = crafted("scale0")) == -1, "a timescale of 0: %d", s);
	blen = 0;
	b_box(48, "moov");
	b_box(40, "mvhd");
	b_zero(32);
	buf[16] = 1;
	be32(buf + 16 + 20, 1);
	be32(buf + 16 + 24, 0xffffffffu);
	be32(buf + 16 + 28, 0xffffffffu);
	CHECK((s = crafted("forever")) == -1, "a duration past an int: %d", s);
}

/* No network, whatever an interface says: the page's offline state */
static void unreachable(void)
{
	mf_state st;
	int i;

	printf("no network to ask:\n");
	memset(&st, 0, sizeof st);
	st.offline = true;
	CHECK(mf_unreachable(&st), "begun offline");

	memset(&st, 0, sizeof st);
	for (i = 0; i < AS_SOURCES; i++) st.q[i] = MFQ_WAIT;
	CHECK(!mf_unreachable(&st), "not while the sources wait to be asked");

	/* A Pixel 2 with no adapter: the cable's network is up, and every
	 * request ends with curl unable to resolve the host */
	st.q[AS_LR] = MFQ_NO_ANSWER;
	st.q[AS_SS] = MFQ_ASKING;
	st.q[AS_SG] = MFQ_WAIT;
	CHECK(!mf_unreachable(&st), "not while one is still asked");
	st.q[AS_SS] = MFQ_NO_ANSWER;
	st.q[AS_SG] = MFQ_OFF;
	CHECK(mf_unreachable(&st), "every source asked gave no answer, the one with no key not asked");
	st.q[AS_SG] = MFQ_NO_ANSWER;
	CHECK(mf_unreachable(&st), "all three gave no answer");

	/* An answer of any kind is a network */
	st.q[AS_SS] = MFQ_UNKNOWN;
	CHECK(!mf_unreachable(&st), "ScreenScraper answered: a network, and nothing found");
	st.q[AS_SS] = MFQ_NONE;
	CHECK(!mf_unreachable(&st), "an answer with no match is a network");
	st.q[AS_SS] = MFQ_NO_ANSWER;
	st.n = 1;
	CHECK(!mf_unreachable(&st), "a picture came: a network");
	st.n = 0;
	st.video_offer = true;
	CHECK(!mf_unreachable(&st), "a video on offer: a network");
	st.video_offer = false;
	for (i = 0; i < AS_SOURCES; i++) st.q[i] = MFQ_OFF;
	CHECK(!mf_unreachable(&st), "nothing asked at all (no collection, account or key) is not offline");
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
	crafted_files();
	unreachable();
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
