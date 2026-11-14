/* SPDX-License-Identifier: GPL-3.0-only */
#include "bgm_screen.h"

#include "app.h"
#include "panel.h"

#include <stdio.h>
#include <string.h>

/* The two folders menu music plays from (src/bgm.h): Menu Music at the top
 * of the card, and ZyperOS's own */
void bgm_dirs(char *dir, char *builtin, size_t n)
{
	snprintf(dir, n, "%s/%s", P_CARD, BGM_CARD_DIR);
	snprintf(builtin, n, "%s/%s", P_ROOT, BGM_BUILTIN_DIR);
}

/* What plays, for the Menu Music page: as the player reads it */
void bgm_scan_now(bgm_list *l)
{
	char dir[CFG_STR * 2], builtin[CFG_STR * 2];

	bgm_dirs(dir, builtin, sizeof dir);
	bgm_scan_all(&(bgm_sources){ dir, builtin, bgm_builtin() }, l);
}

int bgm_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	bgm_view *v = ctx;
	bgm_ui u;

	bgm_now(v->now, sizeof v->now);
	/* Playing names the song as the pop-up does, from its tags, when the
	 * player's news is the file playing */
	{
		bgm_song s;
		char head[BGM_TAG_MAX];

		bgm_song_now(&s);
		v->now_name[0] = '\0';
		if (v->now[0] && !strcmp(s.file, v->now))
			bgm_song_label(&s.tags, s.file, head, sizeof head, v->now_name, sizeof v->now_name);
	}
	/* A track set that is not in the list - one of ZyperOS's, with ZyperOS
	 * Tracks turned off, or one gone from the card - is not what plays: the
	 * player shuffles instead (src/bgm_out.c, wanted_file), so the row says so */
	u = (bgm_ui){ bgm_on(), bgm_volume(),
	              bgm_find(&v->list, bgm_track()) >= 0 ? bgm_track() : "",
	              v->now, v->list.n, v->list.own, bgm_builtin(), g_song_popup, v->now_name };
	return bgm_menu_build(&u, rows, max, &v->b, heading);
}

static menu_result bgm_key(app *a, void *ctx, in_button key, int sel)
{
	bgm_view *v = ctx;
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	/* Which row: ZyperOS Tracks is there only with tracks of the card's own */
	int row = sel >= 0 && sel < (int)sizeof v->b.id ? v->b.id[sel] : BGM_ROWS;
	char num[8];

	(void)a;
	if (row == BGM_ROW_ON && (d || key == IN_ACCEPT)) {
		bgm_set_on(!bgm_on());
		db_set_str(db_dev(), "bgm", bgm_on_id(bgm_on()));
	} else if (row == BGM_ROW_VOLUME && d) {
		bgm_set_volume(bgm_volume_step(bgm_volume(), d));
		snprintf(num, sizeof num, "%d", bgm_volume());
		db_set_str(db_dev(), "bgm.volume", num);
	} else if (row == BGM_ROW_BUILTIN && (d || key == IN_ACCEPT)) {
		bgm_set_builtin(!bgm_builtin());
		db_set_str(db_dev(), "bgm.builtin", bgm_on_id(bgm_builtin()));
		/* The Track row steps through what plays now */
		bgm_scan_now(&v->list);
	} else if (row == BGM_ROW_TRACK && d) {
		int at = bgm_track_step(bgm_find(&v->list, bgm_track()), v->list.n, d);
		const char *t = at < 0 ? "" : v->list.names[at];

		bgm_set_track(t);
		db_set_str(db_dev(), "bgm.track", t);
	} else if (row == BGM_ROW_POPUP && (d || key == IN_ACCEPT)) {
		g_song_popup = !g_song_popup;
		db_set_str(db_dev(), "bgm.popup", bgm_on_id(g_song_popup));
	} else if (row == BGM_ROW_NOW && key == IN_ACCEPT) {
		bgm_skip();
	}
	return MENU_STAY;
}

void bgm_screen(app *a)
{
	bgm_view v;
	menu_style st = {
		/* Fixed, so the panel does not change size as one track's name
		 * gives way to a longer one */
		.fixed_w = menu_std_width(a),
		.accent  = MENU_ZYPEROS,
	};

	memset(&v, 0, sizeof v);
	bgm_scan_now(&v.list);
	bgm_rescan();
	st.sound = v.b.sound;
	menu_run(a, &st, bgm_build, bgm_key, &v);
	bgm_free(&v.list);
}
