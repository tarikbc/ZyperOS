/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LIGHTS_SCREEN_H
#define ZYPEROS_LIGHTS_SCREEN_H

/* MENU > Side Lights (src/sidelights.h) and MENU > Language (src/i18n.h):
 * two pages of one list each, and the language put in place. The screens
 * are src/lights_screen.c. */
#include "app.h"
#include "panel.h"

#include "ds/hover.h"

/* MENU > Language's page: its rows' sounds, the language kept as it opened
 * (ticked while the cursor shows the others) and what its cursor shows */
typedef struct {
	unsigned char sound[LANG_COUNT];
	int kept;
	ds_hover_run run;
} language_page;

/* The whole screen in the language under the cursor, as the first-start
 * tour's language step does (src/ds/hover.h): its words and faces in
 * memory, nothing saved */
extern const ds_hover LANGUAGE_HOVER;

/* What main.c uses of it; each is described where it is defined. `ctx` is
 * a language_page. */
int language_build(void *ctx, menu_row *rows, int max, const char **heading);
void lights_shot_draw(app *a, int row);

#endif
