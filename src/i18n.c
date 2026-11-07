/* SPDX-License-Identifier: GPL-3.0-only */
/* See i18n.h. */
#include "i18n.h"

#include <string.h>
#include <strings.h>

#ifdef I18N_PSEUDO
#define I18N_N (LANG_COUNT + 1)
#else
#define I18N_N LANG_COUNT
#endif

extern const char *const i18n_blobs[I18N_N];
extern const uint32_t *const i18n_offs[I18N_N];
extern const char *const i18n_codes[LANG_COUNT];
extern const char *const i18n_names[LANG_COUNT];

/* English until the setting is read: anything drawn before it is English */
const char *i18n_blob;
const uint32_t *i18n_off;
static lang cur = LANG_EN;
static bool ready;

/* Which languages' "one" is 0 and 1 rather than 1 alone (French) */
static const unsigned char zero_is_one[LANG_COUNT] = I18N_PLURAL_RULES;

static void init(void)
{
	if (ready) return;
	i18n_blob = i18n_blobs[LANG_EN];
	i18n_off = i18n_offs[LANG_EN];
	ready = true;
}

/* A constructor, so T() is English from the first line of main without a
 * call first; i18n_set is the only other writer */
__attribute__((constructor)) static void i18n_boot(void) { init(); }

void i18n_set(lang l)
{
	if ((int)l < 0 || (int)l >= I18N_N) l = LANG_EN;
	cur = l;
	i18n_blob = i18n_blobs[l];
	i18n_off = i18n_offs[l];
	ready = true;
}

lang i18n_lang(void) { return cur; }

const char *TN(str_id id, long n)
{
	bool one = n == 1 || (n == 0 && cur < LANG_COUNT && zero_is_one[cur]);

	return T(one ? id : id + 1);
}

const char *T_en(str_id id)
{
	return i18n_blobs[LANG_EN] + i18n_offs[LANG_EN][id];
}

lang lang_parse(const char *code)
{
	size_t n;

	if (!code || !*code) return LANG_EN;
#ifdef I18N_PSEUDO
	if (!strcasecmp(code, "xx")) return LANG_PSEUDO;
#endif
	for (int i = 0; i < LANG_COUNT; i++)
		if (!strcasecmp(code, i18n_codes[i])) return (lang)i;
	/* "pt" for "pt-BR": the language alone, where one region is all there is */
	for (int i = 0; i < LANG_COUNT; i++) {
		n = strcspn(i18n_codes[i], "-");
		if (strlen(code) == n && !strncasecmp(code, i18n_codes[i], n)) return (lang)i;
	}
	return LANG_EN;
}

const char *lang_code(lang l)
{
#ifdef I18N_PSEUDO
	if (l == LANG_PSEUDO) return "xx";
#endif
	return (int)l >= 0 && (int)l < LANG_COUNT ? i18n_codes[l] : i18n_codes[LANG_EN];
}

const char *lang_name(lang l)
{
#ifdef I18N_PSEUDO
	if (l == LANG_PSEUDO) return "[Pšéûðö·····]";
#endif
	return (int)l >= 0 && (int)l < LANG_COUNT ? i18n_names[l] : i18n_names[LANG_EN];
}

bool i18n_owns(const char *s)
{
	const char *end = i18n_blob + i18n_off[S_COUNT - 1];

	return s >= i18n_blob && s <= end + strlen(end);
}
