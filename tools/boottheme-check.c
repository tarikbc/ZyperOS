/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-boottheme: the words the boot screen's theme and language travel
 * in (src/boottheme.h), as boot.scr and the splash read them. */
#include "../src/boottheme.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, what) do { \
	if (cond) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

int main(void)
{
	char line[BOOTTHEME_MAX], file[BOOTTHEME_MAX], said[BOOTTHEME_MAX];

	printf("check-boottheme:\n");
	boottheme_words("auto", "America/Sao_Paulo", "pt-BR", line, sizeof line, file, sizeof file);
	CHECK(!strcmp(line, "zytheme=auto zyzone=America/Sao_Paulo zylang=pt-BR"), "auto, a zone and a language, as the kernel line has them");
	CHECK(!strcmp(file, "zytheme=auto\nzyzone=America/Sao_Paulo\nzylang=pt-BR\n"), "  ...and as theme.txt, a line each");
	boottheme_words("dark", "Etc/GMT+3", "zh-Hans", line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=dark zyzone=Etc/GMT+3 zylang=zh-Hans"), "dark, a zone with a +, Chinese");
	boottheme_words("sepia", "UTC", NULL, line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=light zyzone=UTC zylang=en"), "a word the launcher does not know is light, as it reads it; no language is en");
	boottheme_words(NULL, "Bad Zone", "pt BR", line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=light zyzone=- zylang=en"), "a zone or a language with a space is - or en: one word on the line");
	boottheme_words("light", "x\nzytheme=dark", "fr\nzytheme=dark", line, sizeof line, file, sizeof file);
	CHECK(!strcmp(file, "zytheme=light\nzyzone=-\nzylang=en\n"), "a zone or a language cannot add a line to theme.txt");

	boottheme_from_cmdline("root=/dev/mmcblk0p2 ro zyperos.slot=a zyperos.trial=0 zytheme=auto zyzone=America/Sao_Paulo zylang=fr",
	                       said, sizeof said);
	CHECK(!strcmp(said, "zytheme=auto zyzone=America/Sao_Paulo zylang=fr"), "the words back from a kernel line");
	boottheme_words("auto", "America/Sao_Paulo", "fr", line, sizeof line, NULL, 0);
	CHECK(boottheme_same(line, said), "  ...the same as the settings make: nothing to write");
	boottheme_words("auto", "America/Sao_Paulo", "es", line, sizeof line, NULL, 0);
	CHECK(!boottheme_same(line, said), "  ...another language is a write");
	boottheme_from_cmdline("root=/dev/mmcblk0p2 zytheme=auto zyzone=America/Sao_Paulo", said, sizeof said);
	CHECK(!strcmp(said, "zytheme=auto zyzone=America/Sao_Paulo"), "a boot.scr from before the language: its two words");
	CHECK(boottheme_same(line, said), "  ...and the language is not a difference it could carry: nothing to write");
	boottheme_words("dark", "America/Sao_Paulo", "es", line, sizeof line, NULL, 0);
	CHECK(!boottheme_same(line, said), "  ...but the theme still is");
	boottheme_from_cmdline("root=/dev/mmcblk0p2 zyperos.slot=a\n", said, sizeof said);
	CHECK(!said[0], "a line without them (an older boot.scr): \"\"");
	boottheme_from_cmdline("root=/dev/vda2 zyperos.slot=a zytheme=- zyzone=- zylang=-", said, sizeof said);
	CHECK(!strcmp(said, "zytheme=- zyzone=- zylang=-"), "a boot.scr that found no theme.txt says -: not the settings, so it is written");
	CHECK(!boottheme_same(line, said), "  ...a write");
	boottheme_from_cmdline("xzytheme=dark zytheme=light zyzone=- xzylang=fr", said, sizeof said);
	CHECK(!strcmp(said, "zytheme=light zyzone=-"), "a word inside another is not it");

	if (fails) { printf("check-boottheme: %d failed\n", fails); return 1; }
	printf("check-boottheme: all passed\n");
	return 0;
}
