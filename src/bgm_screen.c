/* SPDX-License-Identifier: GPL-3.0-only */
#include "bgm_screen.h"
#include "menu_pick.h"
#include "ds/hover.h"
#include "hover_sweep.h"

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

/* Menu Music's rows' choices by place (bgm_build) */
static menu_choice g_bgm_choice[BGM_ROWS + 3];
static const ds_hover BGM_VOLUME_HOVER, BGM_TRACK_HOVER;

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
	{
		int n = bgm_menu_build(&u, rows, max, &v->b, heading), i, k;
		static const char *vol[BGM_VOLUME_MAX], *trk[MENU_CHOICES_MAX];
		static char vb[BGM_VOLUME_MAX][8], tb[MENU_CHOICES_MAX][96];

		/* Volume's ten and the tracks (Shuffle, then each), as bgm_key
		 * steps them, so A opens their list (menu.h's menu_choice) */
		memset(g_bgm_choice, 0, sizeof g_bgm_choice);
		for (k = BGM_VOLUME_MIN; k <= BGM_VOLUME_MAX; k++) {
			bgm_volume_label(k, vb[k - BGM_VOLUME_MIN], sizeof vb[0]);
			vol[k - BGM_VOLUME_MIN] = vb[k - BGM_VOLUME_MIN];
		}
		trk[0] = T(S_MUSIC_SHUFFLE);
		for (k = 0; k < v->list.n && k + 1 < MENU_CHOICES_MAX; k++) {
			bgm_title(v->list.names[k], tb[k + 1], sizeof tb[0]);
			trk[k + 1] = tb[k + 1];
		}
		for (i = 0; i < n && i < (int)sizeof v->b.id; i++) {
			if (v->b.id[i] == BGM_ROW_VOLUME)
				g_bgm_choice[i] = (menu_choice){ .n = BGM_VOLUME_MAX - BGM_VOLUME_MIN + 1,
				                                 .cur = (unsigned char)(bgm_volume() - BGM_VOLUME_MIN), .labels = vol,
				                                 .hover = &BGM_VOLUME_HOVER };
			else if (v->b.id[i] == BGM_ROW_TRACK && v->list.n > 0 && v->list.n + 1 <= MENU_CHOICES_MAX)
				g_bgm_choice[i] = (menu_choice){ .n = (unsigned char)(v->list.n + 1),
				                                 .cur = (unsigned char)(bgm_find(&v->list, bgm_track()) + 1),
				                                 .labels = trk, .hover = &BGM_TRACK_HOVER };
		}
		return n;
	}
}

/* Volume's and Track's lists (src/ds/hover.h): the music at the volume
 * under the cursor, and the track under it once the cursor rests on it,
 * from its start (bgm_hear); the setting untouched until A takes one. B
 * goes back to the track that played as the list opened, from its start. */
static const bgm_view *g_bgm_view;
static char g_bgm_was[512];
static bool g_bgm_heard;

static void volume_show(void *app_, int k)
{
	(void)app_;
	bgm_set_volume(BGM_VOLUME_MIN + k);
}

static void volume_undo(void *app_, int kept)
{
	volume_show(app_, kept);
}

static void track_show(void *app_, int k)
{
	(void)app_;
	if (!g_bgm_heard) {
		bgm_now(g_bgm_was, sizeof g_bgm_was);
		g_bgm_heard = true;
	}
	/* Shuffle: the one that played */
	bgm_hear(k > 0 && g_bgm_view && k - 1 < g_bgm_view->list.n ? g_bgm_view->list.names[k - 1] : g_bgm_was);
}

static void track_undo(void *app_, int kept)
{
	char now[512];

	(void)app_;
	if (!g_bgm_heard) return;
	g_bgm_heard = false;
	bgm_now(now, sizeof now);
	/* A track kept: the setting plays it again, from its start. Shuffle
	 * kept: the one that played, once, if another plays now */
	if (kept > 0 || !strcmp(now, g_bgm_was)) bgm_hear("");
	else bgm_hear_back(g_bgm_was);
}

static const ds_hover BGM_VOLUME_HOVER = { .show = volume_show, .undo = volume_undo };
static const ds_hover BGM_TRACK_HOVER = { .show = track_show, .undo = track_undo, .rest_ms = 500 };

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
	g_bgm_view = &v;
	bgm_scan_now(&v.list);
	bgm_rescan();
	st.sound = v.b.sound;
	st.choices = g_bgm_choice;
	menu_run(a, &st, bgm_build, bgm_key, &v);
	g_bgm_view = NULL;
	bgm_free(&v.list);
}

void bgm_rows_report(app *a)
{
	menu_row rows[BGM_ROWS + 3];
	bgm_view v;
	const char *h;
	int n;

	(void)a;
	memset(&v, 0, sizeof v);
	bgm_scan_now(&v.list);
	n = bgm_build(&v, rows, BGM_ROWS + 3, &h);
	menu_rows_print("music", rows, n, v.b.sound, g_bgm_choice);
	bgm_free(&v.list);
}

/* Menu Music's lists through what their cursors show (src/hover_sweep.h) */
static int sweep_music(app *a, void *ctx, menu_row *rows, int max, const menu_choice **choices)
{
	const char *h;

	(void)a;
	*choices = g_bgm_choice;
	return bgm_build(ctx, rows, max, &h);
}

void bgm_hover_sweep(app *a)
{
	static bgm_view v;

	memset(&v, 0, sizeof v);
	bgm_scan_now(&v.list);
	g_bgm_view = &v;
	hover_sweep_page(a, "music", sweep_music, &v);
	g_bgm_view = NULL;
	bgm_free(&v.list);
}
