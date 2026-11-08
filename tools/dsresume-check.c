/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-dsresume: a Nintendo DS game's resume, by the language its auto
 * state was left in (src/dsresume.h). The firmware's language as DSperate's
 * direct boot takes it, from made-up 256 KB images; the language a game
 * starts in, for each option; the .lang beside a state, read and written; and
 * the decision, Tarik's Mario Kart DS among the cases: a state from before
 * .lang files on a card whose firmware.bin is Spanish, the game now in
 * English, starts fresh once and then resumes. Plain C, scratch files under
 * build-native/dsresume. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/dsresume.h"

static int failures;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); failures++; } } while (0)

static const char *D = "build-native/dsresume";

static int same(const char *a, const char *b)
{
	return a && b ? !strcmp(a, b) : a == b;
}

/* A firmware image of `size` bytes with its user settings at 0x3FE00: each
 * copy's language and update counter */
static void firmware(const char *path, long size, unsigned ptr, int l0, int c0, int l1, int c1)
{
	unsigned char *fw = calloc(1, (size_t)size);
	FILE *f = fopen(path, "wb");
	unsigned base = ptr << 3;

	if (!fw || !f) { printf("  FAIL cannot write %s\n", path); exit(1); }
	fw[0x20] = (unsigned char)ptr;
	fw[0x21] = (unsigned char)(ptr >> 8);
	if (base && base + 0x200 <= (unsigned long)size) {
		fw[base + 0x64] = (unsigned char)(0x30 | l0);   /* the backlight shares it */
		fw[base + 0x70] = (unsigned char)c0;
		fw[base + 0x100 + 0x64] = (unsigned char)(0x30 | l1);
		fw[base + 0x100 + 0x70] = (unsigned char)c1;
	}
	fwrite(fw, 1, (size_t)size, f);
	fclose(f);
	free(fw);
}

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");

	if (f) { fputs(text, f); fclose(f); }
}

int main(void)
{
	char es[256], en[256], small[256], none[256], noptr[256], st[256], lang[256], buf[16];
	const char *w;

	mkdir("build-native", 0755);
	mkdir(D, 0755);
	snprintf(es, sizeof es, "%s/spanish.bin", D);
	snprintf(en, sizeof en, "%s/second-copy-english.bin", D);
	snprintf(small, sizeof small, "%s/dsi.bin", D);
	snprintf(none, sizeof none, "%s/no-such-firmware.bin", D);
	snprintf(noptr, sizeof noptr, "%s/no-settings.bin", D);
	snprintf(st, sizeof st, "%s/Mario Kart DS (USA).nds.auto.state", D);
	snprintf(lang, sizeof lang, "%s/Mario Kart DS (USA).nds.auto.lang", D);
	unlink(none);

	printf("the firmware's language, as DSperate's direct boot takes it:\n");
	firmware(es, 0x40000, 0x7FC0, 5, 3, 5, 3);
	CHECK(same(dsr_firmware_language(es), "spanish"), "both copies Spanish: spanish");
	firmware(en, 0x40000, 0x7FC0, 5, 3, 1, 4);
	CHECK(same(dsr_firmware_language(en), "english"), "the second copy one count past the first: the second's");
	firmware(en, 0x40000, 0x7FC0, 5, 0x7F, 1, 0);
	CHECK(same(dsr_firmware_language(en), "spanish"), "past the wrap (0x7F then 0): the first's, as DSperate");
	firmware(en, 0x40000, 0x7FC0, 0, 3, 1, 9);
	CHECK(same(dsr_firmware_language(en), "japanese"), "a second copy further ahead: the first's, as DSperate");
	firmware(small, 0x20000, 0x3FC0, 1, 0, 1, 0);
	CHECK(dsr_firmware_language(small) == NULL, "smaller than 256 KB: DSperate copies no settings, so none");
	firmware(noptr, 0x40000, 0, 1, 0, 1, 0);
	CHECK(dsr_firmware_language(noptr) == NULL, "no settings pointer: none");
	firmware(en, 0x40000, 0x7FC0, 7, 0, 7, 0);
	CHECK(dsr_firmware_language(en) == NULL, "language 7, which no DS has: none");
	CHECK(dsr_firmware_language(none) == NULL && dsr_firmware_language(NULL) == NULL, "no file: none");

	printf("the language a game starts in:\n");
	CHECK(same(dsr_effective("english", es), "english"), "the option's language, whatever the firmware's");
	CHECK(same(dsr_effective("chinese", es), "chinese"), "chinese too (the iQue DS's)");
	CHECK(same(dsr_effective("firmware", es), "spanish"), "firmware: the dump's own");
	CHECK(same(dsr_effective(NULL, es), "spanish") && same(dsr_effective("klingon", es), "spanish"),
	      "no option, or a word the core does not know: the dump's own");
	CHECK(same(dsr_effective("firmware", none), "english"), "no firmware.bin: English, DSperate's made-up one");
	CHECK(dsr_effective("firmware", small) == NULL, "a firmware.bin with no settings in reach: not known");

	printf("the .lang beside a state:\n");
	{
		char p[256];

		dsr_lang_path(st, p, sizeof p);
		CHECK(!strcmp(p, lang), "<game>.auto.state's is <game>.auto.lang");
		dsr_lang_path("x/odd", p, sizeof p);
		CHECK(!strcmp(p, "x/odd.lang"), "a name without .state gets .lang added");
	}
	unlink(lang);
	CHECK(same(dsr_recorded(st, es, buf, sizeof buf), "spanish"),
	      "no .lang: the firmware's own, which every game started in before the row");
	CHECK(same(dsr_recorded(st, none, buf, sizeof buf), "english"), "no .lang and no firmware.bin: English");
	CHECK(dsr_recorded(st, small, buf, sizeof buf) == NULL, "no .lang and a firmware with no settings: not known");
	CHECK(dsr_record(st, "english"), "written");
	CHECK(same(dsr_recorded(st, es, buf, sizeof buf), "english"), "and read back, over the firmware's");
	put(lang, "german\r\n");
	CHECK(same(dsr_recorded(st, es, buf, sizeof buf), "german"), "a line end of any kind");
	put(lang, "");
	CHECK(dsr_recorded(st, es, buf, sizeof buf) == NULL, "an empty .lang: not known");
	put(lang, "klingon\n");
	CHECK(dsr_recorded(st, es, buf, sizeof buf) == NULL, "a .lang naming no language: not known");
	CHECK(!dsr_record(st, "klingon") && !dsr_record(st, NULL), "only a language is written");
	CHECK(dsr_recorded(st, es, buf, sizeof buf) == NULL, "and the file is left as it was");

	printf("the decision:\n");
	CHECK(dsr_start_fresh("spanish", "english"), "made in another language: start fresh");
	CHECK(!dsr_start_fresh("english", "english"), "made in this one: resume");
	CHECK(!dsr_start_fresh(NULL, "english") && !dsr_start_fresh("spanish", NULL) && !dsr_start_fresh(NULL, NULL),
	      "either not known: resume, as before");
	/* The Mario Kart DS case, on the Spanish ROGUE dump */
	unlink(lang);
	put(st, "a state");
	{
		const char *eff = dsr_effective("english", es);
		const char *rec = dsr_recorded(st, es, buf, sizeof buf);

		CHECK(dsr_start_fresh(rec, eff), "a state from before .lang, Spanish firmware, English now: start fresh");
		CHECK(dsr_record(st, eff), "the run's state written in English");
		rec = dsr_recorded(st, es, buf, sizeof buf);
		CHECK(!dsr_start_fresh(rec, dsr_effective("english", es)), "the next start resumes it");
		CHECK(dsr_start_fresh(rec, dsr_effective("spanish", es)), "and Game Language Spanish starts fresh again");
		CHECK(!dsr_start_fresh(rec, dsr_effective("firmware", none)),
		      "no firmware.bin and the dump's own: English, so it resumes");
	}
	w = dsr_effective("italian", es);
	CHECK(same(w, "italian"), "every language the core takes is one this knows");

	if (failures) {
		printf("check-dsresume: %d failure(s)\n", failures);
		return 1;
	}
	printf("check-dsresume: ok\n");
	return 0;
}
