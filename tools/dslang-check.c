/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-dslang, the first half
 *
 * The language a Nintendo DS game sees (src/dslang.h): every language
 * MENU > Language has maps to the DS language the core is told, the DS's own
 * where it has one and English where it has not (Português (Brasil), the
 * mistake that started this: Spanish, from the firmware dump, on a handheld
 * set to Portuguese). And the core takes what the launcher sends: the key
 * and every value are the ones mk/patches/dsperate-lr-language.patch offers
 * and numbers as the firmware's user settings do, so a word the launcher
 * knows and the core does not (which the core would take for "firmware", the
 * dump's own language, silently) cannot be. No device, no SDL. */
#include "../src/dslang.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

/* The firmware's numbering: 0 Japanese, 1 English, 2 French, 3 German,
 * 4 Italian, 5 Spanish (GBATEK; DSperate's bios/freebios.h) */
static const char *const DS[6] = { "japanese", "english", "french", "german", "italian", "spanish" };

static const struct { lang l; const char *code, *want; } TABLE[] = {
	{ LANG_EN,      "en",      "english"  },
	{ LANG_ZH_HANS, "zh-Hans", "english"  },   /* no Chinese on a DS game here */
	{ LANG_ES,      "es",      "spanish"  },
	{ LANG_PT_BR,   "pt-BR",   "english"  },   /* no Portuguese on the DS at all */
	{ LANG_FR,      "fr",      "french"   },
	{ LANG_DE,      "de",      "german"   },
	{ LANG_JA,      "ja",      "japanese" },
	{ LANG_IT,      "it",      "italian"  },
};

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *b;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	b = malloc((size_t)n + 1);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
	if (b) b[n] = '\0';
	fclose(f);
	return b;
}

static bool is_ds(const char *v)
{
	int i;

	for (i = 0; i < 6; i++)
		if (!strcmp(v, DS[i])) return true;
	return false;
}

int main(void)
{
	char *patch;
	const char *p, *end;
	unsigned k;
	int i;

	/* Every language ZyperOS speaks has a row here: a ninth language added
	 * to the tables without one is told by this count */
	CHECK(sizeof TABLE / sizeof TABLE[0] == LANG_COUNT, "a row for every language MENU > Language has");
	for (k = 0; k < sizeof TABLE / sizeof TABLE[0]; k++) {
		const char *got = dslang_value(TABLE[k].l);

		CHECK((int)TABLE[k].l == (int)k, "the table is in the language enum's order");
		CHECK(!strcmp(got, TABLE[k].want), TABLE[k].code);
		if (strcmp(got, TABLE[k].want)) printf("    %s: %s, not %s\n", TABLE[k].code, got, TABLE[k].want);
		CHECK(is_ds(got), "the answer is one of the DS's six");
	}
	/* The pseudo-locale and anything out of range: English, never a word the
	 * core does not know */
	CHECK(!strcmp(dslang_value(LANG_COUNT), "english"), "the pseudo-locale gets English");
	CHECK(!strcmp(dslang_value((lang)99), "english"), "an unknown language gets English");
	CHECK(!strcmp(dslang_value((lang)-1), "english"), "a negative language gets English");

	/* The patch: the option's key, its list (firmware first, the default,
	 * then the six), and the array the numbers come from, in the DS's order */
	patch = slurp("mk/patches/dsperate-lr-language.patch");
	CHECK(patch != NULL, "mk/patches/dsperate-lr-language.patch is there (run from the repo's root)");
	if (!patch) return 1;

	p = strstr(patch, "{\"" DSLANG_KEY "\", \"");
	CHECK(p != NULL, "the patch offers the option the launcher sends");
	if (p) {
		p = strchr(p + strlen(DSLANG_KEY) + 4, ';');
		end = p ? strstr(p, "\"}") : NULL;
		CHECK(p && end, "the option has a description and a list");
		if (p && end) {
			char list[256];
			size_t n;
			const char *at;

			while (*++p == ' ') ;
			n = (size_t)(end - p);
			CHECK(n < sizeof list, "the option's list is readable");
			snprintf(list, sizeof list, "%.*s", (int)(n < sizeof list ? n : sizeof list - 1), p);
			CHECK(!strncmp(list, "firmware|", 9), "firmware, the old behaviour, is the default (first)");
			at = list + 9;
			for (i = 0; i < 6; i++) {
				size_t l = strlen(DS[i]);

				CHECK(!strncmp(at, DS[i], l) && (at[l] == '|' || at[l] == '\0'),
				      "the list names the DS's languages in the firmware's order");
				at += l + 1;
			}
			CHECK(at > list + strlen(list), "nothing else is offered");
		}
	}
	/* kLanguages[], the core's map from a word to a number */
	p = strstr(patch, "kLanguages[] = {");
	CHECK(p != NULL, "the patch maps the words to the firmware's numbers");
	if (p) {
		p += strlen("kLanguages[] = {");
		for (i = 0; i < 6; i++) {
			char want[32];

			snprintf(want, sizeof want, "\"%s\"", DS[i]);
			CHECK(!strncmp(p, want, strlen(want)), "kLanguages is in the firmware's order");
			p += strlen(want);
			while (*p == ',' || *p == ' ') p++;
		}
		CHECK(*p == '}', "kLanguages has six");
	}
	/* The one field it writes, and no other */
	CHECK(strstr(patch, "UserField::Language") != NULL, "the patch sets the language field");
	CHECK(strstr(patch, "UserField::Nickname") == NULL && strstr(patch, "UserField::Message") == NULL &&
	      strstr(patch, "UserField::Colour") == NULL && strstr(patch, "UserField::Birthday") == NULL,
	      "and no other of the settings");
	/* The firmware file is not written: DSperate saves a changed firmware only
	 * where a frontend calls save_firmware_override, and the patch adds no call */
	for (p = patch; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
		if (*p == '+' && strncmp(p, "+++", 3) && strstr(p, "save_firmware_override(") &&
		    strstr(p, "save_firmware_override(") < (strchr(p, '\n') ? strchr(p, '\n') : p + strlen(p))) {
			CHECK(0, "the patch must not save the firmware");
			break;
		}
	}
	free(patch);

	if (failures) {
		printf("check-dslang: %d failure(s)\n", failures);
		return 1;
	}
	printf("check-dslang: the mapping and the core's option agree\n");
	return 0;
}
