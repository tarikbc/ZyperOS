/* Menu music's rules, held on a build machine: which files are tracks, the
 * settings and their labels, the Track row's round of positions, the deck
 * that plays every track once before any twice, the history L2 goes back
 * along, the gain and its ramp, the Menu Music screen's rows, a track's name
 * from its tags or its file (src/bgm_tags.h) and the life of Zyper's pop-up
 * that says it (src/songpop.h), and what it says of MP4's track when L2/R2
 * move MP4 from the shelf.
 *
 * Links src/bgm.c, src/bgm_menu.c, src/bgm_tags.c and src/songpop.c and NOT
 * SDL or ALSA: if it ever needs either, the split in src/bgm.h has failed.
 * Run from the repository's root.
 */
#include "../src/bgm.h"
#include "../src/bgm_dec.h"
#include "../src/bgm_menu.h"
#include "../src/bgm_tags.h"
#include "../src/design.h"
#include "../src/songpop.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static void tracks(void)
{
	printf("which files are tracks:\n");
	ck(bgm_is_track("FC001.mp3"), "an MP3");
	ck(bgm_is_track("Waterflame - Byzantine.ogg"), "an Ogg");
	ck(bgm_is_track("LOUD.MP3") && bgm_is_track("x.Ogg"), "any case");
	ck(!bgm_is_track("._FC001.mp3"), "not macOS's ._ copy");
	ck(!bgm_is_track(".mp3"), "not a bare extension");
	ck(!bgm_is_track("song.flac") && !bgm_is_track("cover.jpg") &&
	   !bgm_is_track("notes.txt") && !bgm_is_track("mp3"), "nothing else");
}

static void settings(void)
{
	char b[16];

	printf("the settings:\n");
	ck(bgm_on_parse(NULL) && bgm_on_parse("") && bgm_on_parse("on") &&
	   bgm_on_parse("junk"), "on unless it says off");
	ck(!bgm_on_parse("off"), "off");
	ck(!strcmp(bgm_on_id(true), "on") && !strcmp(bgm_on_id(false), "off"),
	   "stored as on and off, which parse back");
	ck(bgm_volume_parse(NULL) == BGM_VOLUME_DEFAULT &&
	   bgm_volume_parse("") == BGM_VOLUME_DEFAULT &&
	   bgm_volume_parse("7x") == BGM_VOLUME_DEFAULT, "junk is the default volume");
	ck(bgm_volume_parse("7") == 7, "a volume reads back");
	ck(bgm_volume_parse("0") == BGM_VOLUME_MIN &&
	   bgm_volume_parse("99") == BGM_VOLUME_MAX, "and is held to its range");
	ck(bgm_volume_step(BGM_VOLUME_MAX, +1) == BGM_VOLUME_MAX &&
	   bgm_volume_step(BGM_VOLUME_MIN, -1) == BGM_VOLUME_MIN, "steps stop at the ends");
	ck(bgm_volume_step(5, +1) == 6 && bgm_volume_step(5, -1) == 4, "and move between");
	bgm_volume_label(5, b, sizeof b);
	ck(!strcmp(b, "50%"), "5 reads 50%");
	bgm_volume_label(10, b, sizeof b);
	ck(!strcmp(b, "100%"), "10 reads 100%");
}

static void titles(void)
{
	char b[64];

	printf("the Track row's names:\n");
	bgm_title("Waterflame - Byzantine.ogg", b, sizeof b);
	ck(!strcmp(b, "Waterflame - Byzantine"), "the name without its extension");
	bgm_title("Mr. Blue Sky.mp3", b, sizeof b);
	ck(!strcmp(b, "Mr. Blue Sky"), "only the last dot");
	bgm_title("noext", b, sizeof b);
	ck(!strcmp(b, "noext"), "a name with none stays");
	bgm_title("abcdefgh.mp3", b, 5);
	ck(!strcmp(b, "abcd"), "cut to the buffer");
}

static void steps(void)
{
	printf("the Track row's round (Shuffle, then each track):\n");
	ck(bgm_track_step(-1, 3, +1) == 0, "right from Shuffle is the first track");
	ck(bgm_track_step(2, 3, +1) == -1, "right from the last is Shuffle");
	ck(bgm_track_step(-1, 3, -1) == 2, "left from Shuffle is the last track");
	ck(bgm_track_step(0, 3, -1) == -1, "left from the first is Shuffle");
	ck(bgm_track_step(7, 3, +1) == 0, "a track no longer there counts as Shuffle");
	ck(bgm_track_step(-1, 0, +1) == -1, "an empty folder stays on Shuffle");
}

static void deck(void)
{
	bgm_deck d;
	int seen[50], i, k, last = -1, ok = 1;

	printf("the shuffle:\n");
	bgm_deck_init(&d, 50, 12345);
	for (k = 0; k < 20; k++) {
		memset(seen, 0, sizeof seen);
		for (i = 0; i < 50; i++) {
			int t = bgm_deck_next(&d);

			if (t < 0 || t >= 50 || seen[t]++) ok = 0;
			if (i == 0 && t == last) ok = 0;
			if (i == 49) last = t;
		}
	}
	ck(ok, "every track once a deck, and no deck starts with the last one's end");
	bgm_deck_free(&d);

	bgm_deck_init(&d, 2, 1);
	last = bgm_deck_next(&d);
	for (k = 0, ok = 1; k < 100; k++) {
		int t = bgm_deck_next(&d);

		if (t == last) ok = 0;
		last = t;
	}
	ck(ok, "two tracks take turns, never one twice in a row");
	bgm_deck_free(&d);

	bgm_deck_init(&d, 1, 9);
	ck(bgm_deck_next(&d) == 0 && bgm_deck_next(&d) == 0, "one track plays over and over");
	bgm_deck_free(&d);
	bgm_deck_init(&d, 0, 9);
	ck(bgm_deck_next(&d) == -1, "an empty folder deals nothing");
	bgm_deck_free(&d);
}

static void level(void)
{
	int16_t pcm[8] = { 10000, -10000, 10000, -10000, 32767, -32768, 10000, 10000 };

	printf("the level:\n");
	ck(bgm_gain(BGM_VOLUME_MAX, 1, 1) == 65536, "full volume, faded in, is the track");
	ck(bgm_gain(5, 1, 1) == 16384, "5 of 10 is a quarter of the power");
	ck(bgm_gain(BGM_VOLUME_MAX, 0, 100) == 0, "the start of a fade is silence");
	ck(bgm_gain(BGM_VOLUME_MAX, 50, 100) == 32768, "the fade is linear");
	ck(bgm_gain(BGM_VOLUME_MAX, 500, 100) == 65536, "past its end it is full");
	bgm_apply(pcm, 4, 65536, 0);
	ck(pcm[0] == 10000 && pcm[1] == -10000, "a ramp starts where it was");
	ck(pcm[6] == 0 && pcm[7] == 0, "and ends where it was going");
	ck(pcm[2] > 0 && pcm[2] < 10000, "through the middle");
	{
		int16_t s[4] = { 1000, 3000, -2000, 0 };

		bgm_one_side(s, 2, true);
		ck(s[0] == 2000 && s[1] == 0 && s[2] == -1000 && s[3] == 0,
		   "one side carries both channels, mixed");
	}
	{
		int16_t s[4] = { 32767, 32767, -32768, 32767 };

		bgm_mono(s, 2);
		ck(s[0] == 32767 && s[1] == 32767, "mono at full scale both sides does not clip");
		ck(s[2] == 0 && s[3] == 0, "and opposite sides mix to the average");
	}
}

static void rate(void)
{
	static int16_t in[2 * 44100], out[2 * 48100], out2[2 * 48100];
	bgm_rs r;
	int i, n, m, k, ok;

	printf("the rate:\n");
	for (i = 0; i < 44100; i++) { in[2 * i] = (int16_t)(i % 30000); in[2 * i + 1] = 1000; }

	bgm_rs_init(&r, 48000, 48000);
	n = bgm_rs_run(&r, in, 1000, out, 48100);
	ck(n == 1000 && !memcmp(in, out, 1000 * 4), "48 kHz passes as it is");

	bgm_rs_init(&r, 44100, 48000);
	n = bgm_rs_run(&r, in, 44100, out, 48100);
	ck(n >= 47999 && n <= 48001, "a second of 44.1 kHz is a second of 48 kHz");
	for (i = 0, ok = 1; i < n; i++) if (out[2 * i + 1] != 1000) ok = 0;
	ck(ok, "a steady channel stays steady");
	for (i = 1, ok = 1; i < 30000 && i < n; i++) if (out[2 * i] < out[2 * i - 2]) ok = 0;
	ck(ok, "a rising one keeps rising");

	/* In slices of 1024, as the player feeds it: the same as all at once */
	bgm_rs_init(&r, 44100, 48000);
	for (k = 0, m = 0; k < 44100; k += 1024) {
		int take = 44100 - k < 1024 ? 44100 - k : 1024;

		m += bgm_rs_run(&r, in + 2 * k, take, out2 + 2 * m, 48100 - m);
	}
	ck(m == n && !memcmp(out, out2, (size_t)n * 4), "slice by slice is the same as whole");

	bgm_rs_init(&r, 8000, 48000);
	n = bgm_rs_run(&r, in, 1024, out, 1024 * 6 + 8);
	ck(n >= 6142 && n <= 6146, "an 8 kHz slice fits the output it is given");
}

static void scan(void)
{
	char dir[] = "build-native/bgm-check-XXXXXX";
	const char *files[] = { "b.mp3", "A.ogg", "._b.mp3", "c.txt", "C.MP3" };
	bgm_list l = { 0 };
	size_t i;

	printf("the folder:\n");
	if (!mkdtemp(dir)) { ck(0, "a scratch folder"); return; }
	for (i = 0; i < sizeof files / sizeof files[0]; i++) {
		char p[128];
		FILE *f;

		snprintf(p, sizeof p, "%s/%s", dir, files[i]);
		if ((f = fopen(p, "w"))) fclose(f);
	}
	bgm_scan(dir, &l);
	ck(l.n == 3, "three tracks among five files");
	ck(l.n == 3 && !strcmp(l.names[0], "A.ogg") && !strcmp(l.names[1], "b.mp3") &&
	   !strcmp(l.names[2], "C.MP3"), "in name order, any case");
	ck(bgm_find(&l, "b.mp3") == 1 && bgm_find(&l, "c.txt") == -1 &&
	   bgm_find(&l, "") == -1, "found by name");
	bgm_scan("build-native/no-such-folder", &l);
	ck(l.n == 0 && !l.names, "a missing folder is an empty list");
	for (i = 0; i < sizeof files / sizeof files[0]; i++) {
		char p[128];

		snprintf(p, sizeof p, "%s/%s", dir, files[i]);
		unlink(p);
	}
	rmdir(dir);
}

/* A file, and the folders it needs, made empty */
static void touch(const char *root, const char *rel)
{
	char p[512];
	char *q;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	for (q = p + strlen(root) + 1; (q = strchr(q, '/')); q++) {
		*q = '\0';
		mkdir(p, 0755);
		*q = '/';
	}
	if ((f = fopen(p, "w"))) fclose(f);
}

static void rm_tree(const char *dir)
{
	char cmd[600];

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (system(cmd) != 0) printf("  (could not remove %s)\n", dir);
}

/* Everything that plays, from its two places (bgm_scan_all) */
static void all(void)
{
	char card[] = "build-native/bgm-check-XXXXXX";
	char dir[600], builtin[600], p[700];
	bgm_sources src;
	bgm_list l = { 0 };

	printf("everything that plays:\n");
	if (!mkdtemp(card)) { ck(0, "a scratch card"); return; }
	snprintf(dir, sizeof dir, "%s/Menu Music", card);
	snprintf(builtin, sizeof builtin, "%s/ZyperOS/res/bgm", card);
	touch(card, "Menu Music/b.mp3");
	touch(card, "Menu Music/FC001.mp3");
	touch(card, "Menu Music/Pixel Quest/02 Green Fields.mp3");
	touch(card, "Menu Music/Pixel Quest/01 Title Theme.ogg");
	touch(card, "Menu Music/Pixel Quest/cover.jpg");
	touch(card, "Menu Music/Pixel Quest/Disc 2/01 Ending.mp3");
	touch(card, "Menu Music/A/B/C/too deep.mp3");
	touch(card, "Menu Music/.media/hidden.mp3");
	touch(card, "Menu Music/Star Racer/Neon Circuit.flac");
	/* ROGUE OS's, an ordinary folder since 1.0.5 */
	touch(card, "Roms/BGM/WLF033.mp3");
	touch(card, "ZyperOS/res/bgm/Leap.mp3");
	touch(card, "ZyperOS/res/bgm/FC001.mp3");

	src = (bgm_sources){ dir, builtin, true };
	bgm_scan_all(&src, &l);
	ck(l.n == 6 && l.own == 5, "five of the card's own and one of ZyperOS's");
	ck(l.n == 6 && !strcmp(l.names[0], "b.mp3") && !strcmp(l.names[1], "FC001.mp3") &&
	   !strcmp(l.names[2], "Pixel Quest/01 Title Theme.ogg") &&
	   !strcmp(l.names[3], "Pixel Quest/02 Green Fields.mp3") &&
	   !strcmp(l.names[4], "Pixel Quest/Disc 2/01 Ending.mp3") && l.from[0] == BGM_FROM_FOLDER,
	   "Menu Music's first, its folders' too, in name order, each by its path in it");
	ck(bgm_find(&l, "A/B/C/too deep.mp3") < 0, "not deeper than BGM_DEPTH");
	ck(bgm_find(&l, ".media/hidden.mp3") < 0, "nor in a dot folder");
	ck(bgm_find(&l, "Star Racer/Neon Circuit.flac") < 0, "nor a FLAC");
	ck(bgm_find(&l, "WLF033.mp3") < 0, "and nothing from Roms/BGM, an ordinary folder now");
	ck(l.n == 6 && !strcmp(l.names[5], "Leap.mp3") && l.from[5] == BGM_FROM_BUILTIN,
	   "then ZyperOS's, less the one the card has of the same name");
	snprintf(p, sizeof p, "%s/Pixel Quest/02 Green Fields.mp3", dir);
	ck(l.n == 6 && !strcmp(l.paths[3], p), "a track's path is in its folder");
	snprintf(p, sizeof p, "%s/Leap.mp3", builtin);
	ck(l.n == 6 && !strcmp(l.paths[5], p), "and ZyperOS's in ZyperOS's");

	src.with_builtin = false;
	bgm_scan_all(&src, &l);
	ck(l.n == 5 && l.own == 5, "ZyperOS Tracks Off: the card's own alone");

	touch(card, "Menu Music/Pixel Quest/03 Later.mp3");
	bgm_scan_all(&src, &l);
	ck(l.n == 6 && bgm_find(&l, "Pixel Quest/03 Later.mp3") >= 0, "a track copied on later, read again");

	src = (bgm_sources){ "build-native/no-such-folder", builtin, false };
	bgm_scan_all(&src, &l);
	ck(l.n == 2 && l.own == 0 && l.from[0] == BGM_FROM_BUILTIN,
	   "none of the card's own: ZyperOS's play, whatever ZyperOS Tracks says");
	src.builtin = NULL;
	bgm_scan_all(&src, &l);
	ck(l.n == 0, "and with no folder of ZyperOS's either, nothing");
	bgm_free(&l);
	ck(!l.n && !l.names && !l.paths && !l.from, "freed is empty");

	{
		char t[64], h[64], body[64];

		bgm_title("Pixel Quest/02 Green Fields.mp3", t, sizeof t);
		ck(!strcmp(t, "02 Green Fields"), "a track in a folder is titled by its file");
		bgm_song_label(NULL, "Pixel Quest/02 Green Fields.mp3", h, sizeof h, body, sizeof body);
		ck(!strcmp(body, "Green Fields"), "and the pop-up says its file's name, tidied, with no folders");
	}
	rm_tree(card);
}

/* L2's way back, and what R2 does after it */
static void history(void)
{
	bgm_hist h;
	int i, ok;

	printf("going back:\n");
	bgm_hist_clear(&h);
	ck(bgm_hist_now(&h) == -1 && bgm_hist_back(&h) == -1 && bgm_hist_fwd(&h) == -1,
	   "nothing heard, nothing to go to");
	bgm_hist_push(&h, 4);
	ck(bgm_hist_now(&h) == 4 && bgm_hist_back(&h) == -1, "the first track has none before it");
	bgm_hist_push(&h, 7);
	bgm_hist_push(&h, 2);
	ck(bgm_hist_back(&h) == 7 && bgm_hist_now(&h) == 7, "back is the track before");
	ck(bgm_hist_back(&h) == 4 && bgm_hist_back(&h) == -1, "and the one before that, and no further");
	ck(bgm_hist_fwd(&h) == 7 && bgm_hist_fwd(&h) == 2, "next walks forward again");
	ck(bgm_hist_fwd(&h) == -1 && bgm_hist_now(&h) == 2, "to the newest, where the deck deals");
	bgm_hist_back(&h);
	bgm_hist_push(&h, 9);
	ck(bgm_hist_now(&h) == 9 && bgm_hist_fwd(&h) == -1 && bgm_hist_back(&h) == 7,
	   "a track begun after a back drops what was ahead");
	bgm_hist_clear(&h);
	for (i = 0; i < BGM_HIST + 5; i++) bgm_hist_push(&h, i);
	ck(h.n == BGM_HIST && bgm_hist_now(&h) == BGM_HIST + 4, "kept to BGM_HIST, the newest last");
	for (ok = 1, i = BGM_HIST + 3; i >= 5; i--) ok = ok && bgm_hist_back(&h) == i;
	ck(ok && bgm_hist_back(&h) == -1, "the oldest let go");

	ck(bgm_file_step(0, 5, +1) == 1 && bgm_file_step(4, 5, +1) == 0 &&
	   bgm_file_step(0, 5, -1) == 4, "one track's next and before come round, no Shuffle between");
	ck(bgm_file_step(-1, 5, +1) == 0 && bgm_file_step(-1, 5, -1) == 4 &&
	   bgm_file_step(0, 0, 1) == -1 && bgm_file_step(0, 1, 1) == 0, "and from nowhere, and in a folder of one");
}

/* ---- tags ---------------------------------------------------------------------- */

typedef struct { unsigned char b[16384]; size_t n; } bytes;

static void add(bytes *x, const void *p, size_t n) { memcpy(x->b + x->n, p, n); x->n += n; }

static void put32(unsigned char *p, uint32_t v, int syncsafe)
{
	int sh = syncsafe ? 7 : 8, i;

	for (i = 3; i >= 0; i--) { p[i] = (unsigned char)(v & (syncsafe ? 0x7f : 0xff)); v >>= sh; }
}

/* An ID3v2 frame of version `ver` */
static void frame(bytes *x, int ver, const char *id, unsigned flags, const void *body, size_t n)
{
	unsigned char h[10];

	if (ver == 2) {
		memcpy(h, id, 3);
		h[3] = (unsigned char)(n >> 16); h[4] = (unsigned char)(n >> 8); h[5] = (unsigned char)n;
		add(x, h, 6);
	} else {
		memcpy(h, id, 4);
		put32(h + 4, (uint32_t)n, ver == 4);
		h[8] = (unsigned char)(flags >> 8); h[9] = (unsigned char)flags;
		add(x, h, 10);
	}
	add(x, body, n);
}

/* A text frame: its encoding byte, then the bytes */
static void text(bytes *x, int ver, const char *id, int enc, const void *s, size_t n)
{
	unsigned char b[512];

	b[0] = (unsigned char)enc;
	memcpy(b + 1, s, n);
	frame(x, ver, id, 0, b, n + 1);
}

/* The tag around `fr`, `pad` bytes of padding, then "AUDIO" where the audio
 * would start: a file */
static FILE *tagged(int ver, unsigned flags, const bytes *fr, size_t pad, long *audio)
{
	static bytes out;
	unsigned char h[10] = { 'I', 'D', '3', 0, 0, 0 };
	FILE *f = tmpfile();

	out.n = 0;
	h[3] = (unsigned char)ver;
	h[5] = (unsigned char)flags;
	put32(h + 6, (uint32_t)(fr->n + pad), 1);
	add(&out, h, 10);
	add(&out, fr->b, fr->n);
	memset(out.b + out.n, 0, pad);
	out.n += pad;
	*audio = (long)out.n;
	add(&out, "AUDIO", 5);
	fwrite(out.b, 1, out.n, f);
	rewind(f);
	return f;
}

static void tags(void)
{
	static const unsigned char utf16[] = { 0xff, 0xfe, 'B', 0, 'a', 0, 't', 0, 'm', 0,
	                                       'a', 0, 'n', 0, 0xe9, 0, 0, 0 };
	static const unsigned char utf16be[] = { 0x00, 'M', 0x00, 'e', 0xd8, 0x3d, 0xde, 0x00 };
	static unsigned char cover[3000];
	static const unsigned char unsync[] = { 3, 'A', 0xff, 0x00, 'B' };
	bytes fr;
	bgm_tags t;
	long audio;
	FILE *f;
	char *list[4];

	printf("a track's tags:\n");
	/* 2.3: Latin-1 title, a cover passed over, a UTF-16 album, the artist */
	memset(cover, 0xab, sizeof cover);
	fr.n = 0;
	text(&fr, 3, "TIT2", 0, "Stage 1 \xe9", 9);
	frame(&fr, 3, "APIC", 0, cover, sizeof cover);
	text(&fr, 3, "TALB", 1, utf16, sizeof utf16);
	text(&fr, 3, "TPE1", 0, "Sunsoft", 7);
	text(&fr, 3, "TIT2", 0, "Second", 6);
	f = tagged(3, 0, &fr, 100, &audio);
	memset(&t, 0, sizeof t);
	ck(bgm_id3v2(f, &t), "2.3: a tag");
	ck(!strcmp(t.title, "Stage 1 \xc3\xa9"), "its title, Latin-1 to UTF-8, and the first of two");
	ck(!strcmp(t.album, "Batman\xc3\xa9"), "its album, UTF-16 after a byte order mark");
	ck(!strcmp(t.artist, "Sunsoft"), "its artist, after a picture passed over");
	ck(ftell(f) == audio, "and the file left where the audio starts");
	fclose(f);

	/* 2.4: syncsafe sizes, UTF-8, UTF-16BE with a pair, a frame with its
	 * length given and unsynchronised, an album artist alone */
	fr.n = 0;
	text(&fr, 4, "TIT2", 3, "Caf\xc3\xa9\0Other", 11);
	text(&fr, 4, "TPE2", 2, utf16be, sizeof utf16be);
	{
		unsigned char b[64] = { 0, 0, 0, 0 };

		memcpy(b + 4, unsync, sizeof unsync);
		frame(&fr, 4, "TALB", 0x0003, b, 4 + sizeof unsync);
	}
	f = tagged(4, 0, &fr, 0, &audio);
	memset(&t, 0, sizeof t);
	ck(bgm_id3v2(f, &t) && !strcmp(t.title, "Caf\xc3\xa9"), "2.4: UTF-8, the first value");
	ck(!strcmp(t.artist, "Me\xf0\x9f\x98\x80"), "the album artist as the artist, a surrogate pair whole");
	ck(!strcmp(t.album, "A\xc3\xbf" "B"), "a frame unsynchronised, after its length");
	ck(ftell(f) == audio, "and the audio found");
	fclose(f);

	/* 2.2's three-letter frames */
	fr.n = 0;
	text(&fr, 2, "TT2", 0, "Old Tag", 7);
	text(&fr, 2, "TAL", 0, "Old Album", 9);
	f = tagged(2, 0, &fr, 10, &audio);
	memset(&t, 0, sizeof t);
	ck(bgm_id3v2(f, &t) && !strcmp(t.title, "Old Tag") && !strcmp(t.album, "Old Album") &&
	   ftell(f) == audio, "2.2: its own frame names");
	fclose(f);

	/* 2.3 unsynchronised whole: passed over, the audio still found */
	fr.n = 0;
	text(&fr, 3, "TIT2", 0, "Hidden", 6);
	f = tagged(3, 0x80, &fr, 0, &audio);
	memset(&t, 0, sizeof t);
	ck(bgm_id3v2(f, &t) && !t.title[0] && ftell(f) == audio,
	   "2.3 unsynchronised: not read, and passed over");
	fclose(f);

	/* No tag: the file as it was. ID3v1 at the end fills what is empty. */
	f = tmpfile();
	{
		unsigned char v1[128];

		fwrite("\xff\xfb\x90\x00 audio", 1, 10, f);
		memset(v1, 0, sizeof v1);
		memcpy(v1, "TAG", 3);
		memcpy(v1 + 3, "Overworld  ", 11);
		memcpy(v1 + 33, "Konami", 6);
		memcpy(v1 + 63, "Contra", 6);
		fwrite(v1, 1, sizeof v1, f);
	}
	rewind(f);
	memset(&t, 0, sizeof t);
	ck(!bgm_id3v2(f, &t) && ftell(f) == 0, "no ID3v2: none, and the file at its start");
	snprintf(t.artist, sizeof t.artist, "Kept");
	bgm_id3v1(f, &t);
	ck(!strcmp(t.title, "Overworld") && !strcmp(t.album, "Contra") && !strcmp(t.artist, "Kept"),
	   "ID3v1 fills the empty fields, its spaces off");
	ck(ftell(f) == 0, "and leaves the file where it was");
	fclose(f);

	/* Vorbis comments, as a card's own Ogg tracks carry them */
	list[0] = "title=Apple Cider";
	list[1] = "ALBUMARTIST=Nobody";
	list[2] = "Artist=Zane Little Music";
	list[3] = "COMMENT=ZyperOS menu music";
	memset(&t, 0, sizeof t);
	bgm_vorbis_tags(list, 4, &t);
	ck(!strcmp(t.title, "Apple Cider") && !strcmp(t.artist, "Zane Little Music") && !t.album[0],
	   "Vorbis comments, the key in any case");
	memset(&t, 0, sizeof t);
	bgm_vorbis_tags(list + 1, 1, &t);
	ck(!strcmp(t.artist, "Nobody"), "the album artist when there is no artist");
	bgm_vorbis_tags(NULL, 3, &t);
}

static void label(const bgm_tags *t, const char *file, const char *head, const char *body,
                  const char *what)
{
	char h[BGM_TAG_MAX], b[BGM_TAG_MAX];

	bgm_song_label(t, file, h, sizeof h, b, sizeof b);
	ck(!strcmp(h, head) && !strcmp(b, body), what);
	if (strcmp(h, head) || strcmp(b, body)) printf("    (said \"%s\" / \"%s\")\n", h, b);
}

static void names(void)
{
	bgm_tags t;
	char out[16];

	printf("what the pop-up says:\n");
	memset(&t, 0, sizeof t);
	snprintf(t.title, sizeof t.title, "Stage 1");
	snprintf(t.album, sizeof t.album, "Batman");
	snprintf(t.artist, sizeof t.artist, "Sunsoft");
	label(&t, "FC001.mp3", "Batman", "Stage 1", "a rip: the game over the track");
	t.album[0] = '\0';
	label(&t, "FC001.mp3", "Sunsoft", "Stage 1", "no album: the artist");
	t.artist[0] = '\0';
	label(&t, "FC001.mp3", "Now playing", "Stage 1", "neither: Now playing");
	t.title[0] = '\0';
	label(&t, "FC001.mp3", "Now playing", "FC001", "no title: the file's name");
	label(NULL, "01 - Overworld_Theme.ogg", "Now playing", "Overworld Theme", "no tags at all");

	label(NULL, "07. Boss.mp3", "Now playing", "Boss", "a number and a dot");
	label(NULL, "1-01 Title Screen.mp3", "Now playing", "Title Screen", "a disc and a track");
	label(NULL, "3_castle.ogg", "Now playing", "castle", "a number and an underscore");
	label(NULL, "01 Intro.mp3", "Now playing", "Intro", "a padded number and a space");
	label(NULL, "7 Seas.mp3", "Now playing", "7 Seas", "a name's own number stays");
	label(NULL, "99 Luftballons.mp3", "Now playing", "99 Luftballons", "even two digits");
	label(NULL, "1999.ogg", "Now playing", "1999", "a number alone is the name");
	label(NULL, "01.mp3", "Now playing", "01", "even a track number alone");
	label(NULL, "Waterflame - Byzantine.ogg", "Now playing", "Waterflame - Byzantine",
	      "a name with a dash as it is");
	label(NULL, "  a__b  .mp3", "Now playing", "a b", "runs of spaces as one, none at the ends");
	bgm_tidy_name("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9.mp3", out, 6);
	ck(!strcmp(out, "\xc3\xa9\xc3\xa9"), "cut short on a character's edge");
}

/* The pop-up's life on the design's clocks */
static void popup(void)
{
	songpop p;
	float a, r;
	const unsigned T = 10000, H = DS_MS_HOLD_LONG;
	unsigned s;

	printf("Zyper's pop-up:\n");
	memset(&p, 0, sizeof p);
	ck(!songpop_at(&p, T, &a, &r), "nothing until a song");
	songpop_show(&p, "Batman", "Stage 1", H, T);
	s = p.serial;
	ck(songpop_at(&p, T, &a, &r) && a == 0.0f && r == SONGPOP_RISE, "comes in from nothing, above");
	ck(songpop_at(&p, T + DS_MS_QUICK / 2, &a, &r) && a > 0.5f && r < SONGPOP_RISE / 2.0f,
	   "on the out curve, quick");
	ck(songpop_at(&p, T + DS_MS_QUICK, &a, &r) && a == 1.0f && r == 0.0f, "and there in quick");
	ck(songpop_wait(&p, T + 500) == H - 500, "standing still, it asks for no frames till its hold ends");
	ck(songpop_at(&p, T + H, &a, &r) && a == 1.0f, "held hold-long");
	ck(songpop_at(&p, T + H + DS_MS_FADE_OUT / 2, &a, &r) && a > 0.4f && a < 0.6f,
	   "fading out, linearly");
	songpop_show(&p, "Batman", "Stage 2", H, T + H + DS_MS_FADE_OUT / 2);
	ck(p.serial != s && songpop_at(&p, T + H + DS_MS_FADE_OUT / 2, &a, &r) && a == 1.0f && r == 0.0f,
	   "a new song while it fades: back to full where it is, no second slide");
	ck(!strcmp(p.body, "Stage 2") && p.shown_at == T, "its words changed in place");
	ck(!songpop_at(&p, T + H + DS_MS_FADE_OUT / 2 + H + DS_MS_FADE_OUT, &a, &r) && !p.on,
	   "gone after its hold and fade-out from the last change");
	songpop_show(&p, "A", "B", H, 50000);
	songpop_hide(&p);
	ck(!songpop_at(&p, 50001, &a, &r), "put away at once");
	songpop_show(&p, "A", "B", H, 60000);
	ck(p.shown_at == 60000, "and comes in fresh after");
}

static void mp4_words(const char *title, const char *artist, const char *album,
                      const char *chapter, bool book, const char *head, const char *body,
                      const char *what)
{
	char h[BGM_TAG_MAX], b[BGM_TAG_MAX];

	songpop_mp4_words(title, artist, album, chapter, book, h, sizeof h, b, sizeof b);
	ck(!strcmp(h, head) && !strcmp(b, body), what);
	if (strcmp(h, head) || strcmp(b, body))
		printf("      got \"%s\" / \"%s\"\n", h, b);
}

/* What the pop-up says of MP4, when L2/R2 on the shelf moves it */
static void popup_mp4(void)
{
	printf("Zyper's pop-up for MP4:\n");
	mp4_words("Hey Jude", "The Beatles", "1", NULL, false, "The Beatles", "Hey Jude",
	          "music: the artist over the title");
	mp4_words("Hey Jude", "", "1", NULL, false, "1", "Hey Jude",
	          "music with no artist: the album");
	mp4_words("Hey Jude", "", "", "", false, "Now playing", "Hey Jude",
	          "nothing known: Now playing, as the menu music's");
	mp4_words("Track 3", "Track 3", "", NULL, false, "Now playing", "Track 3",
	          "a head that only repeats the title says Now playing");
	mp4_words("Part 04", "Andy Weir", "The Martian", NULL, true, "The Martian", "Part 04",
	          "a book in files: the book over the file");
	mp4_words("Part 04", "Andy Weir", "", NULL, true, "Andy Weir", "Part 04",
	          "a book with no album: its author");
	mp4_words("Dungeon Crawler Carl", "Matt Dinniman", "Dungeon Crawler Carl", "Chapter 3",
	          true, "Dungeon Crawler Carl", "Chapter 3",
	          "a chaptered book: the book over the chapter, by number");
	mp4_words("The Hobbit", "Tolkien", "", "An Unexpected Party", true, "The Hobbit",
	          "An Unexpected Party", "and by its own title");
	mp4_words("", "Tolkien", "The Hobbit", "Chapter 1", true, "The Hobbit", "Chapter 1",
	          "a chaptered file with no title: its album");
	mp4_words("Big Buck Bunny", "", "Videos", NULL, false, "Videos", "Big Buck Bunny",
	          "a video, as music");
}

/* The row that is `id` in the last build, or -1 when it is not there */
static int row_of(const bgm_bufs *b, int n, int id)
{
	int i;

	for (i = 0; i < n; i++)
		if (b->id[i] == id) return i;
	return -1;
}

static void menu(void)
{
	menu_row rows[BGM_ROWS + 3];
	bgm_bufs b;
	const char *heading = NULL;
	/* 330 of the card's own and ZyperOS's 10 with them */
	bgm_ui u = { true, 5, "", "FC001.mp3", 340, 330, true, true, NULL };
	int n, k;

	printf("the Menu Music screen:\n");
	n = bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(heading && !strcmp(heading, "Menu Music"), "is headed Menu Music");
	ck(n == BGM_ROWS + 2, "six rows, a rule and a note");
	for (k = 0; k < BGM_ROWS; k++) ck(row_of(&b, n, k) == k, "each row in its place");
	ck(!strcmp(rows[BGM_ROW_BUILTIN].label, "ZyperOS Tracks") &&
	   !strcmp(rows[BGM_ROW_BUILTIN].value, "On") && rows[BGM_ROW_BUILTIN].live &&
	   b.sound[BGM_ROW_BUILTIN] == MENU_SND_RING, "ZyperOS Tracks reads On, and comes round");
	ck(BGM_ROW_VOLUME < BGM_ROW_BUILTIN && BGM_ROW_BUILTIN < BGM_ROW_TRACK,
	   "ZyperOS Tracks before Track, which steps through what it lets in");
	ck(!strcmp(rows[BGM_ROW_POPUP].label, "Song Pop-up") &&
	   !strcmp(rows[BGM_ROW_POPUP].value, "On") && rows[BGM_ROW_POPUP].live &&
	   b.sound[BGM_ROW_POPUP] == MENU_SND_RING, "Song Pop-up reads On, and comes round");
	ck(BGM_ROW_POPUP < BGM_ROW_NOW && BGM_ROW_NOW == BGM_ROWS - 1, "Playing stays last");
	ck(!strcmp(rows[BGM_ROW_ON].label, "Music") && !strcmp(rows[BGM_ROW_ON].value, "On"),
	   "Music reads On");
	ck(!strcmp(rows[BGM_ROW_VOLUME].value, "50%"), "Volume reads 50%");
	ck(!strcmp(rows[BGM_ROW_TRACK].value, "Shuffle"), "no track is Shuffle");
	ck(!strcmp(rows[BGM_ROW_NOW].value, "FC001"), "Playing names the track");
	ck(rows[BGM_ROW_NOW].live, "and A on it is live, for the next");
	ck(!strcmp(rows[n - 1].label, "330 of yours, 10 ZyperOS"), "the note counts whose they are");
	ck(b.sound[BGM_ROW_ON] == MENU_SND_RING && b.sound[BGM_ROW_VOLUME] == MENU_SND_NO_A,
	   "On comes round, Volume stops at its ends");
	ck(b.id[n - 1] == BGM_ROWS && b.id[n - 2] == BGM_ROWS, "the rule and the note are no row");

	u.with_builtin = false;
	u.ntracks = 330;
	n = bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_BUILTIN].value, "Off") && rows[BGM_ROW_BUILTIN].live,
	   "ZyperOS Tracks Off, still live to turn on");
	ck(!strcmp(rows[n - 1].label, "330 tracks of yours"), "and the note counts only yours");
	u.own = u.ntracks = 1;
	n = bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[n - 1].label, "1 track of yours"), "one track, said as one");
	u.own = 330;
	u.ntracks = 340;
	u.with_builtin = true;

	u.now_name = "Stage 1 - Gotham City";
	bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_NOW].value, "Stage 1 - Gotham City"),
	   "Playing names the song from its tags, as the pop-up does");
	u.now_name = NULL;
	u.track = "Waterflame - Byzantine.ogg";
	bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_TRACK].value, "Waterflame - Byzantine"), "a track names itself");
	u.track = "Pixel Quest/02 Green Fields.mp3";
	u.now = "Pixel Quest/02 Green Fields.mp3";
	bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_TRACK].value, "02 Green Fields") &&
	   !strcmp(rows[BGM_ROW_NOW].value, "02 Green Fields"),
	   "one in a folder of Menu Music by its file's name, not its path");
	u.track = "";
	u.now = "FC001.mp3";

	u.on = false;
	bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_ON].value, "Off") && rows[BGM_ROW_ON].live,
	   "Off, and still live to turn on");
	ck(!rows[BGM_ROW_VOLUME].live && !rows[BGM_ROW_TRACK].live && !rows[BGM_ROW_NOW].live &&
	   !rows[BGM_ROW_POPUP].live && !rows[BGM_ROW_BUILTIN].live, "the rest quiet while off");
	ck(!strcmp(rows[BGM_ROW_NOW].value, "nothing"), "and nothing playing");

	u.on = true;
	u.popup = false;
	bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(!strcmp(rows[BGM_ROW_POPUP].value, "Off"), "Song Pop-up reads Off");

	/* ZyperOS's alone: none of the card's own, so ZyperOS Tracks is not
	 * there (they play whatever it says), and the note says where yours
	 * would come from */
	u.ntracks = 10;
	u.own = 0;
	u.with_builtin = false;
	u.now = "Apple Cider.ogg";
	n = bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	ck(n == BGM_ROWS + 1, "ZyperOS's own tracks: five rows, a rule and a note");
	ck(row_of(&b, n, BGM_ROW_BUILTIN) < 0, "no ZyperOS Tracks with none of yours");
	k = row_of(&b, n, BGM_ROW_TRACK);
	ck(k == 2 && rows[k].live && !strcmp(rows[row_of(&b, n, BGM_ROW_NOW)].value, "Apple Cider"),
	   "chosen and named like a card's own, the rows moved up one");
	ck(b.sound[k] == (MENU_SND_RING | MENU_SND_NO_A), "each row's sound with it");
	ck(!strcmp(rows[n - 1].label, "Add MP3 or OGG to Menu Music"),
	   "the note says where yours go");

	u.ntracks = 0;
	u.now = "";
	n = bgm_menu_build(&u, rows, BGM_ROWS + 3, &b, &heading);
	k = row_of(&b, n, BGM_ROW_TRACK);
	ck(k >= 0 && !rows[k].live && !rows[row_of(&b, n, BGM_ROW_NOW)].live,
	   "no tracks, nothing to choose");
	ck(!strcmp(rows[n - 1].label, "Add MP3 or OGG to Menu Music"),
	   "and the note says where they go");
	ck(bgm_menu_build(&u, rows, 3, &b, &heading) == 0, "a short array gets nothing");

	ck(!strcmp(bgm_menu_value(true, 3), "On") && !strcmp(bgm_menu_value(false, 3), "Off"),
	   "the ZyperOS menu's row says On or Off");
	ck(!strcmp(bgm_menu_value(true, 0), "no tracks"), "or why it is silent");
}

/* MP3 played gapless: a 1 s tone at 48 kHz (48,000 frames from its first
 * sample to its last), encoded by LAME, comes out as exactly those frames, in
 * place - its first sample where the tone's first is, not 47 ms of the
 * encoder's delay later - and a file with no LAME note plays every frame it
 * has. tools/fixtures/bgm-gapless.mp3 and bgm-nonote.mp3 were made from
 * ffmpeg's sine=440 with lame -V 2 (and -t for the second). */
static long decode_all(const char *path, int16_t *out, long cap)
{
	bgm_dec *d = bgm_dec_open(path, NULL);
	long n = 0;
	int got;

	if (!d) return -1;
	while ((got = bgm_dec_read(d, out + 2 * n, cap - n > 1024 ? 1024 : (int)(cap - n))) > 0)
		n += got;
	bgm_dec_close(d);
	return n;
}

static void gapless(void)
{
	static int16_t pcm[2 * 60000];
	long n = decode_all("tools/fixtures/bgm-gapless.mp3", pcm, 60000), lag, best = 0;
	double top = -1;

	printf("mp3, gapless:\n");
	ck(n == 48000, "LAME's note: the tone's 48,000 frames and no more");
	/* Where the tone lines up: against the sine itself, the lag with the
	 * most in common over the first 100 ms */
	for (lag = -64; lag <= 64 && n > 0; lag++) {
		double sum = 0;
		long i;

		for (i = 0; i < 4800; i++) {
			long k = i + lag;

			if (k >= 0 && k < n) sum += pcm[2 * k] * sin(2 * M_PI * 440 * i / 48000.0);
		}
		if (sum > top) { top = sum; best = lag; }
	}
	ck(n > 0 && best == 0, "and in place: the first sample is the tone's first");
	n = decode_all("tools/fixtures/bgm-nonote.mp3", pcm, 60000);
	ck(n > 48000 && n % 1152 == 0, "a file with no note plays all its frames");
}

int main(void)
{
	gapless();
	tracks();
	settings();
	titles();
	steps();
	deck();
	history();
	level();
	rate();
	scan();
	all();
	menu();
	tags();
	names();
	popup();
	popup_mp4();
	if (fails) { printf("bgm-check: %d FAILED\n", fails); return 1; }
	printf("bgm-check: ok\n");
	return 0;
}
