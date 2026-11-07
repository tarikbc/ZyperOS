/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The words a player reads, in the language MENU > Language chose.
 *
 * Every string the launcher shows has an ID (src/i18n_ids.h, S_...), and
 * res/strings/ holds it in every language; tools/i18n.py makes the
 * tables (src/i18n_tables.c). T(id) is the string in the language that is on:
 * a load and an add, no parsing, no lookup by name and nothing allocated, so
 * a frame drawn in Deutsch costs what it cost in English. A change of
 * language is i18n_set, two pointers; ui_text caches rendered lines by their
 * text, so the next frame draws the new words and the old ones age out.
 *
 *     ui_text(r, f, T(S_MENU_LANGUAGE), x, y, ...);
 *     snprintf(b, n, TN(S_HOME_GAMES, count), count);   a count: "1 game", "3 games"
 *
 * A T() string is static: it may be kept, compared by pointer only within one
 * language, and never freed. A format string from T() is checked by
 * tools/i18n.py rather than the compiler: every language has English's
 * conversions in English's order (or %1$s for another order).
 *
 * The setting is kept in the device database as its code ("pt-BR"), read
 * before the first frame, and written to theme.txt on the boot partition with
 * the theme (src/boottheme.h), where the boot screen's line finds it. */
#ifndef ZYPEROS_I18N_H
#define ZYPEROS_I18N_H

#include <stdbool.h>
#include <stdint.h>

#include "i18n_ids.h"

/* The card's copy of the setting, in P_USERDATA: the code and a line end,
 * which plastron's init reads for the splash when a system update's copy
 * starts (src/boottheme.h) */
#define LANG_FILE "language"

extern const char *i18n_blob;
extern const uint32_t *i18n_off;

#define T(id) (i18n_blob + i18n_off[(id)])

/* A count's form: id is the string's one form, id + 1 its other */
const char *TN(str_id id, long n);

/* id in English, whatever is on: for matching a name typed on the command
 * line (--menu Appearance) and for the log, which stays English */
const char *T_en(str_id id);

void i18n_set(lang l);
lang i18n_lang(void);
/* "pt-BR" (or "pt-br", "pt") is LANG_PT_BR; anything else, NULL included,
 * is English. "xx" is the pseudo-locale on a host build. */
lang lang_parse(const char *code);
/* The code the setting keeps, and the name in its own words ("Deutsch") */
const char *lang_code(lang l);
const char *lang_name(lang l);
/* Whether s is one of the table's strings in the language that is on (and
 * not text formatted into a buffer): what the host's overflow log asks */
bool i18n_owns(const char *s);

#endif
