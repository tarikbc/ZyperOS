/* SPDX-License-Identifier: GPL-3.0-only */
/* The boot screen in the theme the player chose (DESIGN.md 11.21).
 *
 * The boot animation plays from the moment the panel lights, before the card
 * with the settings is mounted, so the choice reaches it another way: the
 * launcher keeps it in theme.txt on the boot partition, U-Boot's boot.scr
 * (plastron's board/zyperos/boot-slots.cmd) reads that file and puts its two
 * words on the kernel's line, and the kernel hands a word without a dot to
 * init as a variable of its environment, which plastron's splash reads:
 *
 *   zytheme=light|dark|auto   the Appearance setting (src/theme.h's words)
 *   zyzone=America/New_York   the clock's zone, for Auto at 7:00 and 19:00;
 *                             "-" when the zone has a character a kernel
 *                             line or U-Boot could not carry
 *   zylang=pt-BR              MENU > Language's code (src/i18n.h), for the
 *                             first start's line under the animation
 *                             (DESIGN.md 11.20); "en" for any other word
 *
 * A boot.scr from before the language carries the first two only: System
 * Update does not write boot.scr, so a card that had 1.0.0 keeps one. The
 * language reaches the splash on such a card from the card itself, where
 * plastron's init reads it when the copy starts (src/i18n.h, LANG_FILE).
 *
 * Nothing here touches a file: main.c reads the kernel's line and the
 * settings, the device file writes theme.txt (plat_boot_theme), and
 * tools/boottheme-check.c holds the words. */
#ifndef ZYPEROS_BOOTTHEME_H
#define ZYPEROS_BOOTTHEME_H

#include <stddef.h>

#define BOOTTHEME_MAX 128

/* The three words for `appearance` ("light", "dark", "auto"; anything else
 * is light, as the launcher reads it), `zone` and `lang` (a language's code,
 * "en" for one that is none): in `line` as a kernel line has them, one space
 * between, and in `file` as theme.txt holds them, a line each, for U-Boot's
 * env import. */
void boottheme_words(const char *appearance, const char *zone, const char *lang,
                     char *line, size_t n, char *file, size_t fn);

/* The words this start's kernel line carried, as boottheme_words writes
 * `line`; "" when it carried none (an older boot.scr, or no theme.txt). A
 * line from a boot.scr that carries no zylang has the words it does carry:
 * boottheme_same then leaves the language out of the comparison. */
void boottheme_from_cmdline(const char *cmdline, char *line, size_t n);

/* Whether `want` (boottheme_words' line) is what `said` (the kernel line's)
 * already says: all three words, or the first two where `said` has no
 * zylang, as a boot.scr from before the language could not carry it. */
int boottheme_same(const char *want, const char *said);

#endif
