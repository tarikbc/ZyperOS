/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
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
 *
 * Nothing here touches a file: main.c reads the kernel's line and the
 * settings, the device file writes theme.txt (plat_boot_theme), and
 * tools/boottheme-check.c holds the words. */
#ifndef ZYPEROS_BOOTTHEME_H
#define ZYPEROS_BOOTTHEME_H

#include <stddef.h>

#define BOOTTHEME_MAX 128

/* The two words for `appearance` ("light", "dark", "auto"; anything else is
 * light, as the launcher reads it) and `zone`: in `line` as a kernel line has
 * them, one space between, and in `file` as theme.txt holds them, a line
 * each, for U-Boot's env import. */
void boottheme_words(const char *appearance, const char *zone,
                     char *line, size_t n, char *file, size_t fn);

/* The two words this start's kernel line carried, as boottheme_words writes
 * `line`; "" when it carried neither (an older boot.scr, or no theme.txt). */
void boottheme_from_cmdline(const char *cmdline, char *line, size_t n);

#endif
