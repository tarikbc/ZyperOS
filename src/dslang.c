/* SPDX-License-Identifier: GPL-3.0-only */
/* The DS's language for ZyperOS's. See src/dslang.h. */
#include "dslang.h"

#include <string.h>

static const char *const IDS[DSL_CHOICES] = {
	[DSL_AUTO]     = "auto",
	[DSL_ENGLISH]  = "english",
	[DSL_SPANISH]  = "spanish",
	[DSL_FRENCH]   = "french",
	[DSL_GERMAN]   = "german",
	[DSL_ITALIAN]  = "italian",
	[DSL_JAPANESE] = "japanese",
	[DSL_CHINESE]  = "chinese",
};

const char *dslang_id(dsl_choice c)
{
	return c >= 0 && c < DSL_CHOICES ? IDS[c] : IDS[DSL_AUTO];
}

dsl_choice dslang_parse(const char *word)
{
	int c;

	if (!word) return DSL_AUTO;
	for (c = 0; c < DSL_CHOICES; c++)
		if (!strcmp(word, IDS[c])) return (dsl_choice)c;
	return DSL_AUTO;
}

dsl_choice dslang_step(dsl_choice c, int d)
{
	if (c < 0 || c >= DSL_CHOICES) c = DSL_AUTO;
	return (dsl_choice)((c + (d < 0 ? DSL_CHOICES - 1 : 1)) % DSL_CHOICES);
}

lang dslang_label_lang(dsl_choice c)
{
	switch (c) {
	case DSL_ENGLISH:  return LANG_EN;
	case DSL_SPANISH:  return LANG_ES;
	case DSL_FRENCH:   return LANG_FR;
	case DSL_GERMAN:   return LANG_DE;
	case DSL_ITALIAN:  return LANG_IT;
	case DSL_JAPANESE: return LANG_JA;
	case DSL_CHINESE:  return LANG_ZH_HANS;
	default:           return LANG_COUNT;
	}
}

const char *dslang_value(lang l)
{
	switch (l) {
	case LANG_JA: return "japanese";
	case LANG_FR: return "french";
	case LANG_DE: return "german";
	case LANG_IT: return "italian";
	case LANG_ES: return "spanish";
	/* The DS has no Portuguese, and no Chinese a DS game here carries */
	case LANG_PT_BR:
	case LANG_ZH_HANS:
	case LANG_EN:
	default:
		return "english";
	}
}

const char *dslang_option(dsl_choice c, lang l)
{
	return c > DSL_AUTO && c < DSL_CHOICES ? IDS[c] : dslang_value(l);
}
