/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_WEBTHEME_H
#define ZYPEROS_WEBTHEME_H

/* The handheld's theme as the play page's theme.css (res/web/play, served by
 * p2d's --theme): the theme the handheld shows now, light or dark by the
 * handheld and not by the phone's setting, with the player's accent, tones
 * and corners (src/look.h). One :root rule that sets exactly the custom
 * properties the comment at the top of res/web/play/play.css lists, so it
 * wins over play.css's own two blocks and nothing else in the page reads
 * anything it could set. With nothing customised the values are play.css's
 * own, so the page looks the same with the file and without it.
 *
 * Plain C and no SDL, like src/themefile.c: the launcher hands in the
 * theme_at() table it is drawing in, whether it is dark, and look_square(),
 * and calls webtheme_write when any of them changes. */

#include <stdbool.h>
#include <stddef.h>

#include "design.h"

/* Where the launcher writes it and p2d is told to read it: /tmp, a RAM
 * filesystem on the device, a fact about this boot like THEME_FILE */
#define WEBTHEME_FILE "/tmp/zyperos-webtheme.css"

/* The file's text for theme `t` (dark: DS_DARK's lifts and color-scheme,
 * the table itself being the caller's, which a look has changed) with
 * Square corners when `square`, into `out`, NUL ended. Returns its length,
 * and 0, with `out` not to be read, when it and the NUL do not fit in
 * `cap`. */
size_t webtheme_css(const ds_theme *t, bool dark, bool square, char *out, size_t cap);

/* The same, into `path` atomically: a temporary beside it and a rename, so
 * p2d, which opens the file at each request, never reads half of one. False
 * when it could not be written, and then `path` is as it was. */
bool webtheme_write(const char *path, const ds_theme *t, bool dark, bool square);

#endif
