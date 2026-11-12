/* EmulationStation's folders and media, on a card made of small files
 * (src/esmedia.h, src/library.h).
 *
 *   make check-esmedia
 *
 * The card holds three layouts another system leaves:
 *
 *   - Batocera's and Knulli's, Roms/nds: a gamelist.xml with relative and
 *     absolute paths, escapes, a disc set listed by its .m3u, a <folder>
 *     and an empty <game/>; images/<name>-thumb, -image and -marquee, and
 *     videos/<name>-video.mp4;
 *   - Skraper's media/ layout with no list, Roms/psx: media/images/ and
 *     media/videos/;
 *   - ROGUE OS's, Roms/sfc: images/<name>.png and nothing else.
 *
 * Each game's cover and video are found in es_media's order, a missing file
 * falls through to the next, a folder with no ES media answers nothing, and
 * the frame's ask (no `wait`) answers only once the index is built. The
 * details read as ZyperOS keeps them, and es_import writes a row only for a
 * game with none and something to say. Rename's half: es_files pairs each
 * file named after the game with its new name, and es_list_rename moves the
 * entry, the rest of the list byte for byte as it was. And the scan: images/
 * and gamelist.xml are not games it left out, and lib_folder_pick and the
 * folder table answer as the shelf needs.
 *
 * Links src/esmedia.c, src/library.c and src/db.c, and not SDL.
 */
#include "../src/db.h"
#include "../src/esmedia.h"
#include "../src/library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/zyperos-esmedia-check-XXXXXX";
static char roms[300];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *rel, const char *text)
{
	char p[1200], *s;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	for (s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) {
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	f = fopen(p, "wb");
	if (!f) { perror(p); exit(1); }
	fputs(text, f);
	fclose(f);
}

static bool there_rom(const char *name)
{
	char p[1200];

	snprintf(p, sizeof p, "%s/nds/%s", roms, name);
	return access(p, F_OK) == 0;
}

static bool ends(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);

	return a >= b && !strcmp(s + a - b, tail);
}

static char *slurp(const char *rel)
{
	char p[1200], *buf;
	FILE *f;
	long n;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	if (!(f = fopen(p, "rb"))) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = calloc(1, (size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
	fclose(f);
	return buf;
}

static const char LIST[] =
	"<?xml version=\"1.0\"?>\n"
	"<gameList>\n"
	"\t<folder><path>./Old</path><name>Old</name></folder>\n"
	"\t<game/>\n"
	"\t<game id=\"1\" source=\"ScreenScraper.fr\">\n"
	"\t\t<path>./Mario Kart DS (USA).nds</path>\n"
	"\t\t<name>Mario Kart DS</name>\n"
	"\t\t<desc>Race &amp; battle, caf&#233; &lt;8&gt; players</desc>\n"
	"\t\t<image>./images/Mario Kart DS (USA)-image.png</image>\n"
	"\t\t<thumbnail>./images/Mario Kart DS (USA)-thumb.png</thumbnail>\n"
	"\t\t<marquee>./images/Mario Kart DS (USA)-marquee.png</marquee>\n"
	"\t\t<video>./videos/Mario Kart DS (USA)-video.mp4</video>\n"
	"\t\t<rating>0.85</rating>\n"
	"\t\t<releasedate>20051114T000000</releasedate>\n"
	"\t\t<developer>Nintendo</developer>\n"
	"\t\t<publisher>Nintendo</publisher>\n"
	"\t\t<genre>Racing</genre>\n"
	"\t\t<players>1-8</players>\n"
	"\t\t<favorite>true</favorite>\n"
	"\t\t<manual>./Mario Kart DS (USA) Bonus.nds</manual>\n"
	"\t</game>\n"
	"\t<game>\n"
	"\t\t<path>/userdata/roms/nds/Tetris DS (Europe).nds</path>\n"
	"\t\t<name>Tetris DS (Europe)</name>\n"
	"\t\t<image>/userdata/roms/nds/images/Tetris DS (Europe)-image.png</image>\n"
	"\t</game>\n"
	"\t<game>\n"
	"\t\t<path>./Disc Set/Disc Set.m3u</path>\n"
	"\t\t<name><![CDATA[Disc <Set>]]></name>\n"
	"\t\t<desc/>\n"
	"\t\t<image>./images/gone.png</image>\n"
	"\t</game>\n"
	"\t<game><path>../../etc/passwd</path><name>Out</name><image>../../etc/x.png</image></game>\n"
	"</gameList>\n";

int main(void)
{
	char out[ES_PATH], dev[400], lib[400];
	game_meta m;
	bool fav;

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(roms, sizeof roms, "%s/Roms", root);
	put("Roms/nds/Mario Kart DS (USA).nds", "rom");
	put("Roms/nds/Tetris DS (Europe).nds", "rom");
	put("Roms/nds/Picross DS (Europe).nds", "rom");
	put("Roms/nds/Disc Set/Disc Set.cue", "cue");
	put("Roms/nds/gamelist.xml", LIST);
	/* A game of the folder that the list names as Mario Kart's manual: never
	 * moved by its rename, which takes only what is in ES's media folders */
	put("Roms/nds/Mario Kart DS (USA) Bonus.nds", "rom");
	put("Roms/nds/images/Mario Kart DS (USA)-image.png", "png");
	put("Roms/nds/images/Mario Kart DS (USA)-thumb.png", "png");
	put("Roms/nds/images/Mario Kart DS (USA)-marquee.png", "png");
	put("Roms/nds/videos/Mario Kart DS (USA)-video.mp4", "mp4");
	put("Roms/nds/images/Tetris DS (Europe)-image.png", "png");
	put("Roms/nds/images/Picross DS (Europe)-image.jpg", "jpg");
	put("Roms/nds/images/Disc Set-image.png", "png");
	put("Roms/psx/Crash (USA).chd", "chd");
	put("Roms/psx/media/images/Crash (USA).png", "png");
	put("Roms/psx/media/videos/Crash (USA).mp4", "mp4");
	put("Roms/sfc/Zelda (USA).sfc", "rom");
	put("Roms/sfc/images/Zelda (USA).png", "png");
	put("Roms/GB/Tetris (World).gb", "rom");
	put("shared/.zyperos/.keep", "");
	put("userdata/.keep", "");

	printf("the frame's ask:\n");
	CHECK(!es_ready("nds"));
	CHECK(!es_media(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", ES_COVER, false,
	                out, sizeof out));
	es_build(roms, "nds");
	CHECK(es_ready("nds"));
	CHECK(es_media(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", ES_COVER, false,
	               out, sizeof out));

	printf("covers, in order:\n");
	CHECK(es_media(roms, "nds", "mario kart ds (usa).NDS", "x", ES_COVER, true, out, sizeof out) &&
	      ends(out, "/nds/images/Mario Kart DS (USA)-thumb.png"));
	{
		char p[1200];

		snprintf(p, sizeof p, "%s/nds/images/Mario Kart DS (USA)-thumb.png", roms);
		unlink(p);
		CHECK(es_media(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", ES_COVER, true,
		               out, sizeof out) && ends(out, "/nds/images/Mario Kart DS (USA)-image.png"));
		snprintf(p, sizeof p, "%s/nds/images/Mario Kart DS (USA)-image.png", roms);
		unlink(p);
		CHECK(es_media(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", ES_COVER, true,
		               out, sizeof out) && ends(out, "/nds/images/Mario Kart DS (USA)-marquee.png"));
		put("Roms/nds/images/Mario Kart DS (USA)-image.png", "png");
		put("Roms/nds/images/Mario Kart DS (USA)-thumb.png", "png");
	}
	CHECK(es_media(roms, "nds", "Tetris DS (Europe).nds", "Tetris DS (Europe)", ES_COVER, true,
	               out, sizeof out) && ends(out, "/nds/images/Tetris DS (Europe)-image.png"));
	CHECK(es_media(roms, "nds", "Picross DS (Europe).nds", "Picross DS (Europe)", ES_COVER, true,
	               out, sizeof out) && ends(out, "/nds/images/Picross DS (Europe)-image.jpg"));
	CHECK(es_media(roms, "nds", "Disc Set/Disc Set.cue", "Disc Set", ES_COVER, true, out, sizeof out) &&
	      ends(out, "/nds/images/Disc Set-image.png"));
	CHECK(es_media(roms, "psx", "Crash (USA).chd", "Crash (USA)", ES_COVER, true, out, sizeof out) &&
	      ends(out, "/psx/media/images/Crash (USA).png"));
	CHECK(es_media(roms, "sfc", "Zelda (USA).sfc", "Zelda (USA)", ES_COVER, true, out, sizeof out) &&
	      ends(out, "/sfc/images/Zelda (USA).png"));
	CHECK(!es_media(roms, "GB", "Tetris (World).gb", "Tetris (World)", ES_COVER, true, out, sizeof out));
	CHECK(!es_media(roms, "nds", "../../etc/passwd", "passwd", ES_COVER, true, out, sizeof out));

	printf("videos:\n");
	CHECK(es_media(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", ES_VIDEO, true,
	               out, sizeof out) && ends(out, "/nds/videos/Mario Kart DS (USA)-video.mp4"));
	CHECK(es_media(roms, "psx", "Crash (USA).chd", "Crash (USA)", ES_VIDEO, true, out, sizeof out) &&
	      ends(out, "/psx/media/videos/Crash (USA).mp4"));
	CHECK(!es_media(roms, "nds", "Tetris DS (Europe).nds", "Tetris DS (Europe)", ES_VIDEO, true,
	                out, sizeof out));
	CHECK(es_has_video("nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)"));
	CHECK(es_has_video("psx", "Crash (USA).chd", "crash (usa)"));
	CHECK(!es_has_video("nds", "Tetris DS (Europe).nds", "Tetris DS (Europe)"));
	CHECK(!es_has_video("GB", "Tetris (World).gb", "Tetris (World)"));

	printf("details:\n");
	CHECK(es_details(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", &m, &fav));
	CHECK(!strcmp(m.title, "Mario Kart DS"));
	CHECK(!strcmp(m.synopsis, "Race & battle, caf\xc3\xa9 <8> players"));
	CHECK(!strcmp(m.year, "2005") && !strcmp(m.genres, "Racing") && !strcmp(m.players, "1-8"));
	CHECK(!strcmp(m.note, "17") && !strcmp(m.developer, "Nintendo") && fav);
	CHECK(es_details(roms, "nds", "Tetris DS (Europe).nds", "Tetris DS (Europe)", &m, &fav));
	CHECK(!m.title[0] && !fav);                   /* ES's own name for an unscraped file */
	CHECK(es_details(roms, "nds", "Disc Set/Disc Set.cue", "Disc Set", &m, NULL) &&
	      !strcmp(m.title, "Disc <Set>"));
	CHECK(!es_details(roms, "nds", "Picross DS (Europe).nds", "Picross DS (Europe)", &m, NULL));

	printf("into the library:\n");
	snprintf(dev, sizeof dev, "%s/userdata/zyperos.db", root);
	snprintf(lib, sizeof lib, "%s/shared/.zyperos/library.db", root);
	CHECK(db_init(dev, lib, NULL));
	{
		game_list l = { 0 };
		es_import_list got = { 0 };
		game_meta have = { 0 };
		char t[256];

		/* "cue" too: Disc Set is a disc folder, and since the scan reads
		 * folders of games a folder is one game only on a shelf of discs */
		CHECK(lib_scan_listing(roms, "nds", "nds,zip,cue", &l));
		CHECK(l.count == 5);
		CHECK(l.skipped == 0);                    /* not images/, videos/ or gamelist.xml */
		snprintf(have.title, sizeof have.title, "%s", "Scraped Name");
		CHECK(db_game_set(db_lib(), "nds", "Disc Set/Disc Set.cue", &have));
		CHECK(es_import(roms, "nds", &l, &got) == 1);
		CHECK(got.n == 1 && !strcmp(got.got[0].file, "Mario Kart DS (USA).nds") &&
		      !strcmp(got.got[0].title, "Mario Kart DS") && got.got[0].favorite);
		CHECK(db_game_title(db_lib(), "nds", "Mario Kart DS (USA).nds", t, sizeof t) &&
		      !strcmp(t, "Mario Kart DS"));
		CHECK(db_game_get(db_lib(), "nds", "Mario Kart DS (USA).nds", &have) && !strcmp(have.year, "2005"));
		CHECK(db_game_title(db_lib(), "nds", "Disc Set/Disc Set.cue", t, sizeof t) &&
		      !strcmp(t, "Scraped Name"));         /* a scrape's row stays */
		CHECK(!db_game_get(db_lib(), "nds", "Tetris DS (Europe).nds", &have));
		es_import_free(&got);
		CHECK(es_import(roms, "nds", &l, &got) == 0);   /* once */
		es_import_free(&got);
		CHECK(db_game_move(db_lib(), "nds", "Mario Kart DS (USA).nds", "MKDS.nds"));
		CHECK(!db_game_title(db_lib(), "nds", "MKDS.nds", t, sizeof t));
		CHECK(db_game_get(db_lib(), "nds", "MKDS.nds", &have) && !strcmp(have.year, "2005"));
		CHECK(db_game_move(db_lib(), "nds", "MKDS.nds", "Mario Kart DS (USA).nds"));
		lib_free(&l);
	}

	printf("rename's half:\n");
	{
		char rel[16][ES_PATH], to[16][ES_PATH], *before, *after;
		int n = es_files(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", "MKDS",
		                 rel, to, 16), i;
		bool image = false, video = false, marquee = false;

		CHECK(n == 4);
		for (i = 0; i < n; i++) {
			if (!strcmp(rel[i], "images/Mario Kart DS (USA)-image.png") &&
			    !strcmp(to[i], "images/MKDS-image.png")) image = true;
			if (!strcmp(rel[i], "videos/Mario Kart DS (USA)-video.mp4") &&
			    !strcmp(to[i], "videos/MKDS-video.mp4")) video = true;
			if (!strcmp(rel[i], "images/Mario Kart DS (USA)-marquee.png")) marquee = true;
		}
		CHECK(image && video && marquee);
		for (i = 0; i < n; i++) CHECK(strstr(rel[i], "Bonus") == NULL);
		before = slurp("Roms/nds/gamelist.xml");
		CHECK(es_list_rename(roms, "nds", "Mario Kart DS (USA).nds", "Mario Kart DS (USA)", "MKDS.nds",
		                     rel, to, n));
		after = slurp("Roms/nds/gamelist.xml");
		CHECK(after && strstr(after, "<path>./MKDS.nds</path>") &&
		      strstr(after, "<image>./images/MKDS-image.png</image>") &&
		      strstr(after, "<video>./videos/MKDS-video.mp4</video>") &&
		      strstr(after, "<marquee>./images/MKDS-marquee.png</marquee>") &&
		      !strstr(after, "Mario Kart DS (USA).nds") && !strstr(after, "Mario Kart DS (USA)-"));
		/* the rest as it was: the Tetris entry, byte for byte */
		CHECK(before && after && strstr(before, "<path>/userdata/roms/nds/Tetris DS (Europe).nds</path>") &&
		      strstr(after, "<path>/userdata/roms/nds/Tetris DS (Europe).nds</path>") &&
		      strlen(after) == strlen(before) - 5 * (strlen("Mario Kart DS (USA)") - strlen("MKDS")));
		for (i = 0; i < n; i++) {
			char a[1200], b[1200];

			snprintf(a, sizeof a, "%s/nds/%s", roms, rel[i]);
			snprintf(b, sizeof b, "%s/nds/%s", roms, to[i]);
			CHECK(rename(a, b) == 0);
		}
		CHECK(es_media(roms, "nds", "MKDS.nds", "MKDS", ES_VIDEO, true, out, sizeof out) &&
		      ends(out, "/nds/videos/MKDS-video.mp4"));
		CHECK(es_details(roms, "nds", "MKDS.nds", "MKDS", &m, NULL) && !strcmp(m.title, "Mario Kart DS"));
		CHECK(after && strstr(after, "<manual>./Mario Kart DS (USA) Bonus.nds</manual>"));
		CHECK(there_rom("Mario Kart DS (USA) Bonus.nds"));
		free(before);
		free(after);
	}

	printf("a list made to mislead:\n");
	{
		/* A path through ".", an empty part or a link out of the media
		 * folder: no file of the game's for Rename */
		char rel[8][ES_PATH], to[8][ES_PATH], p[1200], q[1200];

		put("Roms/lnk/Tetris.nds", "rom");
		put("Roms/lnk/Tetris Bonus.nds", "rom");
		snprintf(p, sizeof p, "%s/lnk", roms);
		snprintf(q, sizeof q, "%s/lnk/images", roms);
		CHECK(symlink(p, q) == 0);             /* images/ is the folder itself */
		put("Roms/lnk/gamelist.xml",
		    "<gameList><game><path>./Tetris.nds</path><name>Tetris DS</name>"
		    "<image>./images/Tetris Bonus.nds</image><thumbnail>./images/./Tetris Bonus.nds</thumbnail>"
		    "<marquee>./images//Tetris Bonus.nds</marquee></game></gameList>\n");
		CHECK(es_files(roms, "lnk", "Tetris.nds", "Tetris", "Tet", rel, to, 8) == 0);
	}
	{
		/* A path too long for ES_PATH, cut, would name another file: no game */
		char *big = malloc(ES_PATH + 400), *list = malloc(ES_PATH + 800);

		memset(big, 'a', ES_PATH + 100);
		big[ES_PATH + 100] = '\0';
		memcpy(big, "./Zelda", 7);
		snprintf(list, ES_PATH + 800, "<gameList><game><path>%s.nds</path><name>Cut</name>"
		         "<image>./images/Zelda-image.png</image></game></gameList>\n", big);
		put("Roms/cut/Zelda.nds", "rom");
		put("Roms/cut/images/Zelda-image.png", "png");
		put("Roms/cut/gamelist.xml", list);
		CHECK(!es_details(roms, "cut", "Zelda.nds", "Zelda", &m, NULL));
		CHECK(es_media(roms, "cut", "Zelda.nds", "Zelda", ES_COVER, true, out, sizeof out) &&
		      ends(out, "/cut/images/Zelda-image.png"));   /* by ES's names still */
		free(big);
		free(list);
	}
	{
		/* A <path> in a comment or a CDATA section ahead of the real one is
		 * no path */
		put("Roms/cdata/Real.nds", "rom");
		put("Roms/cdata/gamelist.xml",
		    "<gameList><game><!-- <path>./Fake.nds</path> --><desc><![CDATA[<path>./Fake.nds</path>]]></desc>"
		    "<path>./Real.nds</path><name>Real Name</name></game></gameList>\n");
		CHECK(es_details(roms, "cdata", "Real.nds", "Real", &m, NULL) && !strcmp(m.title, "Real Name"));
		CHECK(!strcmp(m.synopsis, "<path>./Fake.nds</path>"));
		CHECK(!es_details(roms, "cdata", "Fake.nds", "Fake", &m, NULL));
	}

	printf("the shelf's folder:\n");
	{
		int none[2] = { 0, 0 }, some[3] = { 3, 9, 9 }, one[1] = { 5 };
		char f[256];

		CHECK(lib_folder_pick(2, one, 1) == -1);  /* its own has games */
		CHECK(lib_folder_pick(0, one, 1) == 0);
		CHECK(lib_folder_pick(0, some, 3) == 1);  /* the most, the first on a tie */
		CHECK(lib_folder_pick(0, none, 2) == -1);
		CHECK(lib_es_media_dir("Images") && lib_es_media_dir("downloaded_images") &&
		      !lib_es_media_dir("Disc Set"));
		lib_folder_set("Nintendo DS", "nds");
		CHECK(!strcmp(lib_folder_get("Nintendo DS", f, sizeof f), "nds"));
		CHECK(!strcmp(lib_folder_home("nds", f, sizeof f), "Nintendo DS"));
		CHECK(!strcmp(lib_folder_home("SNES", f, sizeof f), "SNES"));
		CHECK(!strcmp(lib_folder_get("SNES", f, sizeof f), "SNES"));
		lib_folder_set("Nintendo DS", "Nintendo DS");
		CHECK(!strcmp(lib_folder_get("Nintendo DS", f, sizeof f), "Nintendo DS"));
		CHECK(!strcmp(lib_folder_home("nds", f, sizeof f), "nds"));
	}

	es_forget();
	CHECK(!es_ready("nds"));
	{
		char cmd[400];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) { printf("esmedia: %d FAILED\n", failures); return 1; }
	printf("esmedia: ok\n");
	return 0;
}
