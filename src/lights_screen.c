/* SPDX-License-Identifier: GPL-3.0-only */
#include "lights_screen.h"
#include "shelfbuild.h"

#include "app.h"
#include "coreopt_defs.h"
#include "panel.h"
#include "platform.h"

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* MENU > Side Lights: a switch for each part of what the lights show
 * (src/sidelights.h, sys_menu_lights_rows). Each change goes to the card at
 * once and the gauge reads it again, so the lights follow the row. The row
 * under the cursor plays its part on the lights, On or Off, while the
 * cursor stays on it (side_lights_preview_step, each pass of the menu). */
typedef struct {
	unsigned char sound[LP_COUNT];
	int sel;                      /* the menu's cursor (menu_style.sel_out) */
	side_lights_preview pv;
} lights_page;

static int lights_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	lights_page *pg = ctx;
	side_light_part ids[LP_COUNT];
	char line[48];
	int n, i;

	(void)max;
	*heading = T(S_PANEL_SIDE_LIGHTS);
	n = sys_menu_lights_rows(plat_side_lights(), rows, ids);
	for (i = 0; i < n; i++) pg->sound[i] = MENU_SND_RING;
	if (n > 0 && side_lights_preview_step(&pg->pv, ids[pg->sel < 0 ? 0 : pg->sel >= n ? n - 1 : pg->sel],
	                                      plat_now_ms(), line, sizeof line))
		plat_side_lights_say(line);
	return n;
}

static menu_result lights_key(app *a, void *ctx, in_button key, int sel)
{
	menu_row rows[LP_COUNT];
	side_light_part ids[LP_COUNT];
	side_lights on = plat_side_lights();
	int n = sys_menu_lights_rows(on, rows, ids);

	(void)a; (void)ctx;
	if (key != IN_LEFT && key != IN_RIGHT && key != IN_ACCEPT) return MENU_STAY;
	if (sel < 0 || sel >= n) return MENU_STAY;
	plat_side_lights_set(side_lights_flip(on, ids[sel]));
	return MENU_STAY;
}

/* A language on from the next frame: its words (src/i18n.h) and its faces
 * (src/ui.h), every panel measured again for them, and kept: the setting,
 * and the boot screen's words (tick_boot_theme) */
void language_apply(app *a, lang l, bool keep)
{
	i18n_set(l);
	ui_fonts_for(l);
	ui_text_memo_clear();
	a->menu_w = 0;
	/* The shelves whose names are words and not a system's: Favorites and
	 * Recents, named when the shelves were built (src/shelfbuild.c) */
	shelf_groups_rename(a);
	if (!keep) return;
	db_set_str(db_dev(), "language", lang_code(l));
	boot_theme_changed();
	/* Core Settings' cached OPTION descs and Diatom's GET_LANGUAGE were for
	 * the old tongue; drop the cache and restart the resident with --language. */
	codefs_cache_clear(P_USERDATA);
	plat_resident_restart();
	fprintf(stderr, "language: %s\n", lang_code(l));
}

/* MENU > Language (DESIGN.md 11.30; my pick in the languages lab,
 * 2026-11-07): the five, each in its own words and its own face, a tick on
 * the one that is on and the cursor on it. A chooses it and goes back to the
 * menu, which is drawn in it from the next frame; B leaves it as it was. */
int language_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	unsigned char *sound = ctx;
	int i, on = (int)i18n_lang();

	*heading = T(S_MENU_LANGUAGE);
	for (i = 0; i < LANG_COUNT && i < max; i++) {
		/* Each name in its own face: ui_text draws Chinese in the Chinese
		 * face whatever is on (src/ui.h) */
		rows[i] = (menu_row){ lang_name((lang)i), i == on ? MENU_TICK : NULL, true };
		sound[i] = 0;
	}
	return i;
}

static menu_result language_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	if (key != IN_ACCEPT || sel < 0 || sel >= LANG_COUNT) return MENU_STAY;
	if ((lang)sel != i18n_lang()) language_apply(a, (lang)sel, true);
	return MENU_DONE;
}

void language_screen(app *a)
{
	static unsigned char sound[LANG_COUNT];

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .sound = sound, .start_sel = (int)i18n_lang() },
	         language_build, language_key, sound);
}

/* --lights-page ROW: the page over the shelf, as menu_run draws it, the
 * cursor on ROW and the lights left alone (a host shot). Nothing where the
 * handheld has no lights, as MENU has no row there. */
void lights_shot_draw(app *a, int row)
{
	menu_row rows[LP_COUNT];
	side_light_part ids[LP_COUNT];
	int n;

	if (!plat_battery_lights()) return;
	n = sys_menu_lights_rows(plat_side_lights(), rows, ids);
	ui_scrim(a->r, 0);
	menu_draw(a, T(S_PANEL_SIDE_LIGHTS), rows, n, row >= 0 && row < n ? row : 0, menu_std_width(a), MENU_ZYPEROS);
}

void lights_screen(app *a)
{
	lights_page pg = { .sel = 0 };
	char line[48];

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a), .sound = pg.sound,
	                           .sel_out = &pg.sel },
	         lights_build, lights_key, &pg);
	/* The lights back to what they show on their own */
	if (side_lights_preview_end(&pg.pv, line, sizeof line)) plat_side_lights_say(line);
}
