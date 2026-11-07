/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* make check-boottheme: the words the boot screen's theme travels in
 * (src/boottheme.h), as boot.scr and the splash read them. */
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
	boottheme_words("auto", "America/Sao_Paulo", line, sizeof line, file, sizeof file);
	CHECK(!strcmp(line, "zytheme=auto zyzone=America/Sao_Paulo"), "auto and a zone, as the kernel line has them");
	CHECK(!strcmp(file, "zytheme=auto\nzyzone=America/Sao_Paulo\n"), "  ...and as theme.txt, a line each");
	boottheme_words("dark", "Etc/GMT+3", line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=dark zyzone=Etc/GMT+3"), "dark, a zone with a +");
	boottheme_words("sepia", "UTC", line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=light zyzone=UTC"), "a word the launcher does not know is light, as it reads it");
	boottheme_words(NULL, "Bad Zone", line, sizeof line, NULL, 0);
	CHECK(!strcmp(line, "zytheme=light zyzone=-"), "a zone with a space is -: one word on the line");
	boottheme_words("light", "x\nzytheme=dark", line, sizeof line, file, sizeof file);
	CHECK(!strcmp(file, "zytheme=light\nzyzone=-\n"), "a zone cannot add a line to theme.txt");

	boottheme_from_cmdline("root=/dev/mmcblk0p2 ro zyperos.slot=a zyperos.trial=0 zytheme=auto zyzone=America/Sao_Paulo",
	                       said, sizeof said);
	CHECK(!strcmp(said, "zytheme=auto zyzone=America/Sao_Paulo"), "the words back from a kernel line");
	boottheme_words("auto", "America/Sao_Paulo", line, sizeof line, NULL, 0);
	CHECK(!strcmp(said, line), "  ...the same as the settings make: nothing to write");
	boottheme_from_cmdline("root=/dev/mmcblk0p2 zyperos.slot=a\n", said, sizeof said);
	CHECK(!said[0], "a line without them (an older boot.scr): \"\"");
	boottheme_from_cmdline("root=/dev/vda2 zyperos.slot=a zytheme=- zyzone=-", said, sizeof said);
	CHECK(!strcmp(said, "zytheme=- zyzone=-"), "a boot.scr that found no theme.txt says -: not the settings, so it is written");
	boottheme_from_cmdline("xzytheme=dark zytheme=light zyzone=-", said, sizeof said);
	CHECK(!strcmp(said, "zytheme=light zyzone=-"), "a word inside another is not it");

	if (fails) { printf("check-boottheme: %d failed\n", fails); return 1; }
	printf("check-boottheme: all passed\n");
	return 0;
}
