/* SPDX-License-Identifier: GPL-3.0-only */
/* For check-hover-sweep (--hover-sweep, tools/hover-sweep-check.py): a
 * page's option lists with a show, each choice shown and the kept one put
 * back as its list would (src/ds/hover.h), proving a hover writes nothing
 * and leaves the page as it found it. A line each:
 *
 *   hover: <page>\t<row>\t<choices shown>\t<db rows written>\t<same|changed>\t<ms>
 *
 * "same": the page built again after reads the same, every row's value;
 * <ms> the slowest show, here (a show's cost on the host, not the Pixel 2's). */
#ifndef ZYPEROS_HOVER_SWEEP_H
#define ZYPEROS_HOVER_SWEEP_H

#include "app.h"
#include "menu.h"

struct ds_hover;

/* A page's rows and choices, built as its screen builds them (MENU's tabs,
 * a system's menu, Theme, the game menu...): the rows into `rows` (up to
 * `max`), its choices by place into *choices; how many rows */
typedef int (*hover_sweep_build)(app *a, void *ctx, menu_row *rows, int max, const menu_choice **choices);

/* Every row of the page whose choices' hover shows */
void hover_sweep_page(app *a, const char *page, hover_sweep_build build, void *ctx);

/* A list of its own (Language, Time Zone, a Controls button), not a
 * menu_choice: its hover, the kept choice and how many there are; `build`
 * the page it opens from, read before and after (NULL: none) */
void hover_sweep_list(app *a, const char *page, const char *label, const struct ds_hover *h,
                      int kept, int n, hover_sweep_build build, void *ctx);

#endif
