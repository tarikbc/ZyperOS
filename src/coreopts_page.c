/* SPDX-License-Identifier: GPL-3.0-only */
/* See src/coreopts_page.h. */
#include "coreopts_page.h"

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "i18n.h"
#include "ingame_screen.h"
#include "menu_panel.h"
#include "panel.h"
#include "platform.h"
#include "ui.h"

static void on_option_line(const char *line, void *ctx)
{
	codefs *out = ctx;
	codef o;

	if (!codef_parse_line(line, &o)) return;
	if (out->n >= CODEFS_MAX) {
		codef_clear(&o);
		return;
	}
	out->opt[out->n++] = o;
}

static void load_current(coreopts_ui *ui)
{
	int i, k, n;

	for (i = 0; i < ui->defs.n; i++) {
		codef *o = &ui->defs.opt[i];
		const char *pick = o->deflt[0] ? o->deflt : (o->nvals ? o->vals[0] : "");

		if (o->value[0]) pick = o->value;
		n = plat_coreopt_count(ui->tag);
		for (k = 0; k < n; k++) {
			const char *kv = plat_coreopt(ui->tag, k);
			size_t len = strlen(o->key);

			if (kv && !strncmp(kv, o->key, len) && kv[len] == '=') {
				pick = kv + len + 1;
				break;
			}
		}
		snprintf(ui->cur[i], sizeof ui->cur[i], "%s", pick);
		if (codef_value_index(o, ui->cur[i]) < 0 && o->nvals > 0)
			snprintf(ui->cur[i], sizeof ui->cur[i], "%s", o->vals[0]);
	}
}

/* Live OPTIONS from the resident only. No userdata cache: Core Settings is
 * in-game, and without a running core there is nothing to show. */
static void gather(coreopts_ui *ui)
{
	char path[512];
	char allow[CODEFS_ALLOW_MAX][CODEFS_KEY_MAX];
	int n_allow;

	codefs_clear(&ui->defs);
	ui->empty = false;

	if (plat_resident_ready())
		plat_resident_options(on_option_line, &ui->defs, 400);

	snprintf(path, sizeof path, "%s/coreopts-show.cfg", P_ROOT);
	n_allow = codefs_load_allow(path, ui->core, allow, CODEFS_ALLOW_MAX);
	codefs_filter(&ui->defs, allow, n_allow);
	if (ui->defs.n == 0) ui->empty = true;
	else load_current(ui);
}

static void keep(coreopts_ui *ui, int i)
{
	char key[160];
	codef *o;
	const char *deflt;

	if (i < 0 || i >= ui->defs.n) return;
	o = &ui->defs.opt[i];
	codef_db_key(ui->tag, o->key, key, sizeof key);
	/* Same default load_current uses: the option's default, else the
	 * first value. At the default, drop the library key so a later
	 * device default can still apply. */
	deflt = o->deflt[0] ? o->deflt : (o->nvals > 0 ? o->vals[0] : "");
	if (!strcmp(ui->cur[i], deflt))
		db_del(db_lib(), key);
	else
		db_set_str(db_lib(), key, ui->cur[i]);
	plat_coreopts_invalidate();
	plat_resident_line("SETOPT\tkey=%s\tvalue=%s", o->key, ui->cur[i]);
}

void coreopts_ui_init(coreopts_ui *ui, const char *tag, const char *core)
{
	memset(ui, 0, sizeof *ui);
	snprintf(ui->tag, sizeof ui->tag, "%s", tag ? tag : "");
	snprintf(ui->core, sizeof ui->core, "%s", core ? core : "");
	gather(ui);
}

void coreopts_ui_finish(coreopts_ui *ui)
{
	if (ui) codefs_clear(&ui->defs);
}

int coreopts_ui_count(const coreopts_ui *ui)
{
	if (!ui || ui->empty) return 0;
	return ui->defs.n;
}

int coreopts_ui_rows(coreopts_ui *ui, menu_row *rows, int max)
{
	int i, n = 0;

	memset(ui->sound, 0, sizeof ui->sound);
	memset(ui->choice, 0, sizeof ui->choice);

	if (ui->empty || ui->defs.n == 0) {
		if (max > 0) rows[n++] = MENU_NOTE(T(S_SYSMENU_CORE_SETTINGS_EMPTY));
		return n;
	}

	/* Options only: no rule/note footer. The row's label is already the
	 * option's words; a pinned caption that repeated them said nothing. */
	for (i = 0; i < ui->defs.n && n < max; i++) {
		codef *o = &ui->defs.opt[i];
		int v, k;

		rows[n] = (menu_row){ o->desc[0] ? o->desc : o->key, ui->cur[i], true };
		ui->sound[n] = MENU_SND_RING;
		for (k = 0; k < o->nvals && k < CODEFS_VALUES_MAX; k++)
			ui->labels[i][k] = o->vals[k];
		v = codef_value_index(o, ui->cur[i]);
		if (v < 0) v = 0;
		ui->choice[n] = (menu_choice){
			.n = (unsigned char)(o->nvals > 255 ? 255 : o->nvals),
			.cur = (unsigned char)v,
			.labels = ui->labels[i],
		};
		n++;
	}
	return n;
}

bool coreopts_ui_key(coreopts_ui *ui, in_button key, int sel)
{
	int d = 0;

	if (ui->empty) return false;
	if (sel < 0 || sel >= ui->defs.n) return false;
	/* A is the runner's (menu_choice): two values flip, more open a list,
	 * each applied as Left/Right here. */
	if (key == IN_LEFT) d = -1;
	else if (key == IN_RIGHT) d = 1;
	else return false;

	{
		codef *o = &ui->defs.opt[sel];
		int cur = codef_value_index(o, ui->cur[sel]);
		int to = codef_value_step(o, cur < 0 ? 0 : cur, d);

		if (o->nvals <= 0) return true;
		snprintf(ui->cur[sel], sizeof ui->cur[sel], "%s", o->vals[to]);
		keep(ui, sel);
	}
	return true;
}

typedef struct {
	coreopts_ui  ui;
	SDL_Texture *bg;
} cop_page;

static int cop_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	cop_page *pg = ctx;

	*heading = T(S_SYSMENU_CORE_SETTINGS);
	return coreopts_ui_rows(&pg->ui, rows, max);
}

static menu_result cop_key(app *a, void *ctx, in_button key, int sel)
{
	cop_page *pg = ctx;

	(void)a;
	coreopts_ui_key(&pg->ui, key, sel);
	return MENU_STAY;
}

static void cop_backdrop(app *a, void *ctx, float k)
{
	cop_page *pg = ctx;

	if (pg->bg) paused_backdrop(a, pg->bg, k);
}

static menu_result cop_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

void coreopts_page(app *a, const char *tag, const char *core, SDL_Texture *bg)
{
	cop_page pg;
	menu_style st;

	memset(&pg, 0, sizeof pg);
	coreopts_ui_init(&pg.ui, tag, core);
	pg.bg = bg;

	memset(&st, 0, sizeof st);
	st.fixed_w = gm_width(a);
	st.follow_tint = true;
	st.sound = pg.ui.sound;
	st.choices = pg.ui.choice;
	st.sel_out = &pg.ui.sel;
	st.backdrop = cop_backdrop;
	st.on_power = cop_power;
	menu_run(a, &st, cop_build, cop_key, &pg);
	coreopts_ui_finish(&pg.ui);
}

/* Host shots only: load the fixture cache so the page can be drawn without
 * a resident. The real page never reads that cache. */
static void gather_shot(coreopts_ui *ui)
{
	char path[512];
	char allow[CODEFS_ALLOW_MAX][CODEFS_KEY_MAX];
	int n_allow;

	codefs_clear(&ui->defs);
	ui->empty = false;
	codefs_cache_path(P_USERDATA, ui->core, path, sizeof path);
	if (!codefs_load_file(&ui->defs, path))
		ui->empty = true;
	snprintf(path, sizeof path, "%s/coreopts-show.cfg", P_ROOT);
	n_allow = codefs_load_allow(path, ui->core, allow, CODEFS_ALLOW_MAX);
	codefs_filter(&ui->defs, allow, n_allow);
	if (ui->defs.n == 0) ui->empty = true;
	else load_current(ui);
}

void coreopts_shot_draw(app *a, int row)
{
	coreopts_ui ui;
	menu_row rows[CODEFS_MAX + 4];
	const system_cfg *s;
	int n, sel;

	if (!a || a->sys.count <= 0) return;
	s = &a->sys.systems[a->sys_cursor];
	memset(&ui, 0, sizeof ui);
	snprintf(ui.tag, sizeof ui.tag, "%s", s->tag);
	snprintf(ui.core, sizeof ui.core, "%s", s->core);
	gather_shot(&ui);
	n = coreopts_ui_rows(&ui, rows, CODEFS_MAX + 4);
	if (n > 0) {
		sel = row >= 0 && row < n ? row : 0;
		ui_scrim(a->r, 0);
		menu_draw(a, T(S_SYSMENU_CORE_SETTINGS), rows, n, sel,
		          gm_width(a), MENU_ZYPEROS);
	}
	coreopts_ui_finish(&ui);
}
