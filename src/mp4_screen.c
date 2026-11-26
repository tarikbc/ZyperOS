/* SPDX-License-Identifier: GPL-3.0-only */
#include "mp4_screen.h"

#include "aout_route.h"
#include "app.h"
#include "hareart.h"
#include "home.h"
#include "i18n.h"
#include "covercolor.h"
#include "mp4art.h"
#include "mp4c.h"
#include "net.h"
#include "nowplaying_screen.h"
#include "panel.h"
#include "taskhold.h"
#include "usbdrive_screen.h"
#include "texload.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"
#include "levels.h"

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

/* ---- MP4: covers --------------------------------------------------------- */

bool file_nonempty(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && st.st_size > 0;
}

/* Album `al`'s cover, when there is one on the card: true, with where in
 * `out`.
 *
 * The first time an album is asked about, a cover already on the card - from
 * an earlier visit - is found by looking. Otherwise the daemon is asked for the
 * one the music carries, and this answers false until cover_answers hears
 * back, a few milliseconds later. */
bool cover_file(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	cover_state *c;
	unsigned now = plat_now_ms();

	if (!g_cov || al < 0 || al >= g_mp4.nalbums) return false;
	c = &g_cov[al];
	ml_cover_base(g_mp4_root, &g_mp4, al, base, sizeof base);
	if (!base[0]) return false;
	/* Singles' is its own, the one put there for it or one put in its place,
	 * never a song's: its songs have nothing to do with one another, and the
	 * first one's picture became the album's (2026-10-05). So it is looked
	 * for each time and MP4 is never asked. */
	if (g_mp4.albums[al].singles) {
		snprintf(out, n, "%s.jpg", base);
		if (file_nonempty(out)) return true;
		snprintf(out, n, "%s.png", base);
		return file_nonempty(out);
	}
	if (c->st == COV_JPG || c->st == COV_PNG) {
		snprintf(out, n, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		return true;
	}
	if (c->st == COV_FOLDER) return ml_folder_image(g_mp4_root, &g_mp4, al, out, n);
	if (c->st == COV_NONE) return false;
	if (c->st == COV_ASKED && now - c->asked_ms < 5000) return false;

	snprintf(out, n, "%s.jpg", base);
	if (file_nonempty(out)) { c->st = COV_JPG; return true; }
	snprintf(out, n, "%s.png", base);
	if (file_nonempty(out)) { c->st = COV_PNG; return true; }
	/* A video's picture beside it before one of its frames: the daemon
	 * answers every video with a frame when it carries no cover (src/mp4d/
	 * cover.c), so the folder would never be looked at after it. */
	if (g_mp4.albums[al].kind == ML_VIDEOS &&
	    ml_folder_image(g_mp4_root, &g_mp4, al, out, n)) {
		c->st = COV_FOLDER;
		return true;
	}
	if (mp4c_cover_ask(g_mp4.tracks[g_mp4.albums[al].first].path, base)) {
		c->st = COV_ASKED;
		c->asked_ms = now;
	}
	return false;
}

/* What is known about album `al`'s cover, forgotten when the file it names is
 * no longer on the card, so cover_file looks again and asks MP4 for the one
 * the music carries. The transfer screen's: files there come and go under the
 * launcher, and a cover moved off the card stayed "a JPG" until a restart, so
 * its embedded picture was never copied out again (2026-10-05). */
void cover_recheck(int al)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	cover_state *c;

	if (!g_cov || al < 0 || al >= g_mp4.nalbums) return;
	c = &g_cov[al];
	if (c->st == COV_JPG || c->st == COV_PNG) {
		ml_cover_base(g_mp4_root, &g_mp4, al, base, sizeof base);
		snprintf(p, sizeof p, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		if (!file_nonempty(p)) c->st = COV_UNKNOWN;
	} else if (c->st == COV_FOLDER &&
	           !ml_folder_image(g_mp4_root, &g_mp4, al, p, sizeof p)) {
		c->st = COV_UNKNOWN;
	}
}

/* What the daemon said about the covers it was asked for. Matched by path, not
 * by album number, so an answer that lands after a rescan finds the album it
 * was about or none at all. True when anything landed, so a shelf that only
 * draws when something changes knows it has. */
bool cover_answers(void)
{
	char base[LIB_PATH * 2], file[LIB_PATH * 2 + 8], b[LIB_PATH * 2];
	bool landed = false;
	size_t fl;
	int i;

	while (mp4c_cover_take(base, sizeof base, file, sizeof file)) {
		landed = true;
		fl = strlen(file);
		for (i = 0; g_cov && i < g_mp4.nalbums; i++) {
			char pic[LIB_PATH * 2 + 8];

			ml_cover_base(g_mp4_root, &g_mp4, i, b, sizeof b);
			if (strcmp(b, base)) continue;
			/* Nothing in the files: a picture in the album's folder, which
			 * is where a book's cover usually is, before the card. */
			g_cov[i].st = fl > 0 ? (fl > 4 && !strcmp(file + fl - 4, ".png") ? COV_PNG : COV_JPG)
			            : ml_folder_image(g_mp4_root, &g_mp4, i, pic, sizeof pic)
			            ? COV_FOLDER : COV_NONE;
			break;
		}
	}
	return landed;
}

/* A cover's shape, wherever it is drawn: square, and square-cornered - Eric's
 * call, 2026-09-18; a sleeve is a square. An ARGB8888 surface, replaced when
 * it had to be cropped.
 *
 * Square by taking the middle of it. Covers are square nearly always, and the
 * ones that are not are a scan with a few pixels of scanner either side - the
 * first card has one, 245 by 225 - which cropping loses and stretching would
 * put into the picture. */
void cover_shape(SDL_Surface **ps)
{
	SDL_Surface *s = *ps, *sq;
	int side = s->w < s->h ? s->w : s->h;

	if (s->w != s->h &&
	    (sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32,
	                                         SDL_PIXELFORMAT_ARGB8888))) {
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(s, &(SDL_Rect){ (s->w - side) / 2, (s->h - side) / 2,
		                                side, side }, sq, NULL);
		SDL_FreeSurface(s);
		*ps = sq;
	}
}

/* A cover as Now Playing draws it, and its main color into `rgb` (the
 * Player Looks', src/covercolor.h). NULL when the file will not decode. */
static SDL_Texture *load_cover(SDL_Renderer *r, const char *path, unsigned *rgb)
{
	SDL_Surface *raw = IMG_Load(path), *s;
	SDL_Texture *t;

	if (!raw) return NULL;
	s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!s) return NULL;
	cover_shape(&s);
	if (rgb && SDL_LockSurface(s) == 0) {
		*rgb = cover_main_rgb(s->pixels, s->w, s->h, s->pitch);
		SDL_UnlockSurface(s);
	}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

void np_forget(void)
{
	if (g_np.tex) SDL_DestroyTexture(g_np.tex);
	g_np.tex = NULL;
	g_np.album = -1;
	g_np.done = false;
	g_np.rgb = CFG_NO_COLOR;
}

/* Bring the Now Playing cover up to date with album `al`, which may be -1 for
 * nothing. Called every frame; it costs a comparison once the cover is in. */
void np_cover(app *a, int al)
{
	char path[LIB_PATH * 2 + 8];
	int w, h;

	if (al != g_np.album) { np_forget(); g_np.album = al; }
	if (g_np.done || al < 0) return;
	if (cover_file(al, path, sizeof path)) {
		unsigned t0 = plat_now_ms();

		g_np.tex = load_cover(a->r, path, &g_np.rgb);
		fprintf(stderr, "mp4: cover for %s in %u ms\n", g_mp4.albums[al].name,
		        plat_now_ms() - t0);
		/* There, and not a picture: the card, as for an album with none. */
		if (!g_np.tex) g_cov[al].st = COV_NONE;
	}
	if (!g_np.tex && g_cov[al].st == COV_NONE) {
		g_np.tex = ui_make_cover(a->r, g_mp4.albums[al].name, ZYPEROS_ACCENT,
		                         &w, &h);
	}
	g_np.done = g_np.tex != NULL;
}

/* Card `i` on MP4's shelf - game_get_tex's half for MP4. Its album's cover
 * when the card holds one, decoded on the worker like any box art; the
 * generated card when the music carries none, drawn on the worker too;
 * nothing yet while the daemon is being asked, which cover_answers settles a
 * few milliseconds later. Asked for while the slot is empty or holds the card
 * of the theme that was, which it shows until its replacement lands, as a
 * game's slot does. */
/* An album's card where it has no cover, or one that will not decode: a
 * game's card (src/cardart.h), square - its ground, MP4's art faint in its
 * corner, and its name as every game's is - so a shelf of albums with no
 * art reads as a games shelf of them does (DESIGN.md 11.67) */
void mp4_album_card_order(app *a, int s, int i, unsigned rgb, ui_card_order *card)
{
	const char *tag = a->sys.systems[s].tag, *key = home_art_key(tag);
	char art[UI_CARD_PATH] = "";

	ui_card_order_set(card, a->view[s].list.items[i].title, rgb, true);
	if (key && key[0]) snprintf(art, sizeof art, "%s/system-icons/%s-still.png", P_ROOT, key);
	ui_card_order_game(card, tag, art, NULL);
}

void mp4_card_want(app *a, int s, int i)
{
	sysview *v = &a->view[s];
	char path[LIB_PATH * 2 + 8];
	ui_card_order card;
	int al;

	if (!v->album || i < 0 || i >= v->list.count) return;
	al = v->album[i];
	if (al < 0 || al >= g_mp4.nalbums) return;
	if (cover_file(al, path, sizeof path)) {
		/* A cover that will not decode is the card in the shelf's color,
		 * as shelf_put has always made it */
		mp4_album_card_order(a, s, i, a->sys.systems[shelf_owner(a, s, i)].accent, &card);
		if (texload_want_or_draw(s, i, path, NULL, &card)) return;
		/* No worker - a shot - so here, as game_get_tex does without one. */
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = load_cover(a->r, path, NULL);
		if (v->tex[i]) {
			SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
			v->cb[i] = 1.0f;
			return;
		}
		g_cov[al].st = COV_NONE;             /* there, and not a picture */
	}
	if (g_cov && g_cov[al].st == COV_NONE) {
		SDL_Surface *ground;

		mp4_album_card_order(a, s, i, ZYPEROS_ACCENT, &card);
		if (texload_want_or_draw(s, i, NULL, NULL, &card)) return;
		/* No worker (a shot): made here, its ground noted for the Grid to
		 * set the name on at the cell's size, as game_get_tex does */
		if (v->tex[i]) SDL_DestroyTexture(v->tex[i]);
		v->tex[i] = ui_make_card_for(a->r, &card, &v->tw[i], &v->th[i], &ground);
		grid_note(a, s, i, ground, true);
		if (ground) SDL_FreeSurface(ground);
		v->cb[i] = 1.0f;
	}
}

/* ---- MP4: the play mode --------------------------------------------------- */

/* Y ON NOW PLAYING, AND NOWHERE ELSE IN MP4. Eric's call, 2026-09-19.
 *
 * Y used to cycle the mode on every MP4 screen, and because a mark on its own
 * is a new shape to learn, each press named the mode on a pill in the middle
 * of the screen for 1400 ms. On Now Playing that pill said what the mark
 * beside the track count was already saying, a moment later and larger.
 *
 * The mode is now set where it is shown. That leaves the shelf and the lists
 * with nothing to announce, so the pill is gone with them, and Y is free there
 * the way X is. The cost is that the mode cannot be changed before something
 * plays, since Now Playing is a screen about a track - and it is kept across
 * restarts, so it is not a thing anyone sets often. */
void mp4_cycle_mode(void)
{
	muq_mode m = (muq_mode)((mp4c_mode() + 1) % MUQ_MODES);

	mp4c_set_mode(m);
	db_set_str(db_dev(), "mp4.mode", MP4_MODES[m].key);
}

/* Videos' Repeat, Y on a video (MP4's MENU > Repeat): Off, One, All and
 * round, the row's own order, applied to the video playing and kept */
void video_cycle_repeat(void)
{
	muq_mode m = muq_video_repeat_next(mp4c_video_mode());

	mp4c_set_video_mode(m);
	db_set_str(db_dev(), "video.mode", MP4_MODES[m].key);
}

/* ---- MP4: what it is open over ------------------------------------------ */

/* NULL is the shelf. The in-game menu hands over its style for as long as MP4
 * is up, because a game is paused underneath: the list is drawn over that
 * game's frame rather than a shelf it is not on, and power stops the game
 * rather than the device - which is what that menu's power rule does, and why
 * it has one. MP4 is modal and never opens itself, so one of each is enough. */
const menu_style *g_mp4_over;
void             *g_mp4_over_ctx;
bool              g_mp4_gone;    /* that rule ended what was underneath */

static void mp4_backdrop(app *a, float k)
{
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

/* The power button or Auto Off, inside MP4. It closes MP4 either way; what
 * else it does is up to whatever MP4 is open over. */
void mp4_power(app *a)
{
	if (g_mp4_over && g_mp4_over->on_power) {
		if (g_mp4_over->on_power(a, g_mp4_over_ctx) == MENU_DONE)
			g_mp4_gone = true;
		return;
	}
	power_off(a);
}

/* ---- MP4: an album's tracks ---------------------------------------------- */

/* The album the queue is on, and the row its track sits at. -1 when nothing
 * is loaded, or when what is loaded is not on the card any more. */
static int mp4_playing_album(int *row)
{
	const char *t = mp4c_track(mp4c_now()->index);
	int al = ml_album_of(&g_mp4, t), i;

	if (row) *row = 0;
	if (al < 0) return -1;
	if (row)
		for (i = 0; i < g_mp4.albums[al].n; i++)
			if (!strcmp(g_mp4.tracks[g_mp4.albums[al].first + i].path, t))
				*row = i;
	return al;
}

/* One album's tracks, over the shelf its cover sits on.
 *
 * THIS USED TO BE THE WHOLE OF MP4: artists, then their albums, then tracks,
 * with Now Playing as a row at the top, because the covers it browsed by had
 * no shelf of their own yet. They have one now - see mp4_shelf_screen - and
 * SELECT lands there, so the two levels above this one had nothing left to
 * do. Eric's, 2026-09-20: the rest of the launcher is pictures, and a list
 * was the wrong front door for the one screen that is all covers.
 *
 * A plays a track and opens Now Playing; on the track already playing it just
 * opens Now Playing, rather than starting it over. B goes back to the shelf.
 * SELECT closes MP4 from here, and MENU is MP4's menu, as everywhere in MP4.
 * L1 and R1 are the previous and next track, left and right seek ten seconds.
 * X and Y are free. Leaving does not stop the music - that is what the daemon
 * is for.
 *
 * `now` opens straight onto Now Playing, with these tracks underneath, so B
 * from there lands on the album it is playing. Returns how it was left, so
 * the shelf can close too when SELECT closed MP4 - it used to be told
 * nothing, and SELECT from Now Playing landed on the shelf instead. */
static mp4_exit mp4_tracks(app *a, int album, bool now)
{
	int sel = 0, done = 0, art, i, lv;
	float k;
	mp4_exit how = MP4_BACK;
	menu_row *rows;
	char (*vals)[24];
	char heading[300], note[300], t0[16], t1[16];
	const ml_album *al;

	if (album < 0 || album >= g_mp4.nalbums || g_mp4.nartists == 0) return MP4_BACK;
	al = &g_mp4.albums[album];
	art = mp4_artist_of(album);
	rows = calloc((size_t)al->n + 4, sizeof *rows);
	vals = calloc((size_t)al->n + 4, sizeof *vals);
	if (!rows || !vals) { free(rows); free(vals); return MP4_BACK; }

	/* On the track that is playing, when it is one of these. */
	if (mp4_playing_album(&i) == album) sel = i;

	if (now) {
		how = mp4_now_screen(a);
		if (how != MP4_BACK) done = 1;
		else if (mp4_playing_album(&i) == album) sel = i;
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	mp4_input_reset();
	/* Opened from here, after a Now Playing it may have opened onto */
	lv = done ? 0 : panel_open();

	while (!done && !want_quit && a->running) {
		const mu_now *mn;
		bool loaded, open_now = false;
		int n = 0, items;
		const char *playing;

		mp4_screen_poll();
		cover_answers();
		mn = mp4c_now();
		loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;
		playing = mp4c_path();
		mp4_input(a);
		/* A plug on the USB port asks here too, as at the shelf: a DAC
		 * plugged in to listen gets its question (usb_questions) */
		if (usb_questions(a)) {
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
			mp4_input_reset();
			redraw_now();
			continue;
		}

		/* The album over its artist - and the album alone when there is no
		 * artist over it, a folder of tracks or of episodes straight in
		 * Music or Videos, which would say its name twice. */
		if (!g_mp4.artists[art].name[0] || !strcmp(g_mp4.artists[art].name, al->name))
			snprintf(heading, sizeof heading, "%s", al->name);
		else
			snprintf(heading, sizeof heading, "%s\n%s", al->name,
			         g_mp4.artists[art].name);
		for (i = 0; i < al->n; i++, n++) {
			const ml_track *t = &g_mp4.tracks[al->first + i];
			/* Every row a value, if only an empty one: a list with a value
			 * anywhere is laid out in two columns, left-aligned, and one
			 * without is centered. Without this the list sat centered until
			 * one of its tracks played and then jumped left, every name
			 * moving the moment A was pressed. Eric, 2026-09-19. */
			const char *v = "";

			if (playing[0] && !strcmp(playing, t->path))
				v = mn->state == MU_PAUSED ? T(S_MP4_ROW_PAUSED) : T(S_MP4_ROW_PLAYING);
			rows[n] = (menu_row){ t->name, v, true };
		}
		items = n;
		if (sel >= items) sel = items ? items - 1 : 0;

		/* What is playing, under a rule, even when it is another album's -
		 * the list is a way to reach music, and the music is the point. */
		if (loaded) {
			char facts[sizeof note];

			mmss(t0, sizeof t0, mn->at);
			mmss(t1, sizeof t1, mn->len);
			snprintf(facts, sizeof facts, "%s · %s · %s / %s", mn->title,
			         mn->artist[0] ? mn->artist : mn->album, t0, t1);
			snprintf(note, sizeof note, mn->state == MU_PAUSED ? T(S_MP4_PAUSED_NOTE) : "%s",
			         facts);
			rows[n++] = MENU_RULE;
			rows[n++] = MENU_NOTE(note);
		}

		if (items > 0) {
			if (in_repeat(&a->in, IN_UP))   sel = list_step_heard(a, IN_UP, items, sel);
			if (in_repeat(&a->in, IN_DOWN)) sel = list_step_heard(a, IN_DOWN, items, sel);
		}
		if (in_repeat(&a->in, IN_LEFT))     { moved_sound(a, IN_LEFT); mp4c_seek_by(-seek_step(&a->in, IN_LEFT)); }
		if (in_repeat(&a->in, IN_RIGHT))    { moved_sound(a, IN_RIGHT); mp4c_seek_by(+seek_step(&a->in, IN_RIGHT)); }
		if (in_repeat(&a->in, IN_L1))       { moved_sound(a, IN_L1); mp4c_prev(); }
		if (in_repeat(&a->in, IN_R1))       { moved_sound(a, IN_R1); mp4c_next(); }
		if (mp4_keys(a)) {
			mp4_locked(a);
			if (!a->running || g_mp4_gone) { how = MP4_CLOSE; break; }
			continue;
		}

		if (a->in.pressed[IN_ACCEPT])
			uisound_play(items > 0 ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
		if (a->in.pressed[IN_ACCEPT] && items > 0) {
			/* The track already playing is not started over: A on it is the
			 * way to its Now Playing, the same as the row. */
			if (!loaded || strcmp(playing, g_mp4.tracks[al->first + sel].path)) {
				const char **paths = calloc((size_t)al->n, sizeof *paths);

				if (paths) {
					for (i = 0; i < al->n; i++)
						paths[i] = g_mp4.tracks[al->first + i].path;
					/* A book's file from its start: choosing one is choosing
					 * where to listen from, and the place moves with it. An
					 * episode from where it was left. */
					if (al->kind == ML_VIDEOS)
						mp4_play_videos(paths, al->n, sel, g_mp4.artists[art].name,
						                 al->name);
					else
						mp4_play(paths, al->n, sel, 0, al->book,
						          al->book ? book_speed(album) : 1.0,
						          g_mp4.artists[art].name, al->name);
					free(paths);
				}
			}
			open_now = true;
		}
		if (a->in.pressed[IN_BACK] || mp4_select()) uisound_play(DS_SOUND_BACK);
		if (a->in.pressed[IN_BACK]) done = 1;
		if (mp4_select()) { how = MP4_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { mp4_power(a); how = MP4_CLOSE; break; }
		/* After a rescan `al` is gone with the old library: not one more
		 * frame from it. */
		if (a->in.pressed[IN_MENU]) {
			how = mp4_menu(a);
			if (how != MP4_BACK) break;
			continue;
		}

		/* Back from Now Playing onto the track it had reached, which may be
		 * several past the one chosen. */
		if (open_now) {
			how = mp4_now_screen(a);
			if (how != MP4_BACK) break;
			if (mp4_playing_album(&i) == album) sel = i;
			continue;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		mp4_backdrop(a, k);
		menu_draw(a, heading, rows, n, sel, menu_std_width(a), ZYPEROS_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}

	if (lv) panel_close(a, lv, !a->running);
	free(rows);
	free(vals);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* SELECT, from anywhere but a running game - the shelf, every menu, and the
 * in-game menu, where the game waits paused underneath: MP4, and straight to
 * Now Playing when something is playing or paused. Nothing on a card with no
 * music, which is also a card with no MP4 on its shelf.
 *
 * `over` is the menu it opened over, or NULL for the shelf and the screens
 * that draw it. True when that menu's power rule ended it too. */
bool mp4_open(app *a, const menu_style *over, void *ctx)
{
	snap_off();
	const mu_now *mn = mp4c_now();

	/* SELECT with nothing on the card to play: a press nothing can act on. */
	if (g_mp4.ntracks == 0) { uisound_play(DS_SOUND_DENY); return false; }
	uisound_play(DS_SOUND_ACCEPT);
	g_mp4_over = over;
	g_mp4_over_ctx = ctx;
	g_mp4_gone = false;
	mp4_shelf_screen(a, mn->state == MU_PLAYING || mn->state == MU_PAUSED);
	g_mp4_over = NULL;
	g_mp4_over_ctx = NULL;
	/* Nothing MP4 did closes the menu it may have been opened over. */
	g_menu_closing = false;
	return g_mp4_gone || g_message_stopped;
}

/* MP4 opening by itself at a start, on what it resumed (src/mp4resume.h):
 * straight to Now Playing, paused, as SELECT opens it on something paused,
 * but with no sound of its own - nothing was pressed */
void mp4_resume_open(app *a)
{
	snap_off();
	if (g_mp4.ntracks == 0) return;
	g_mp4_over = NULL;
	g_mp4_over_ctx = NULL;
	g_mp4_gone = false;
	mp4_shelf_screen(a, true);
	g_menu_closing = false;
}

/* A on a book: where it was left, straight onto Now Playing with its files
 * underneath. The beginning for a book never started or finished; nothing new
 * for the book already playing or paused, which A only opens. */
static mp4_exit mp4_book(app *a, int al)
{
	const mu_now *mn = mp4c_now();
	bool loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;

	if (!loaded || mp4_playing_album(NULL) != al) {
		const ml_album *b = &g_mp4.albums[al];
		const char **paths = calloc((size_t)b->n, sizeof *paths);
		double at;
		int i, start = book_place(al, &at);

		if (!paths) return MP4_BACK;
		for (i = 0; i < b->n; i++) paths[i] = g_mp4.tracks[b->first + i].path;
		fprintf(stderr, "mp4: %s from file %d at %.1f\n", b->name, start + 1, at);
		mp4_play(paths, b->n, start, at, true, book_speed(al),
		          g_mp4.artists[mp4_artist_of(al)].name, b->name);
		free(paths);
	}
	return mp4_tracks(a, al, true);
}

/* A on a film: where it was left, straight onto the player, and B from there
 * back to the shelf - a list of one file would be a screen with nothing to
 * choose. Nothing new for the film already playing or paused, which A only
 * opens. */
static mp4_exit mp4_film(app *a, int al)
{
	const mu_now *mn = mp4c_now();
	bool loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;

	if (!loaded || mp4_playing_album(NULL) != al) {
		const char *p = g_mp4.tracks[g_mp4.albums[al].first].path;

		mp4_play_videos(&p, 1, 0, "", g_mp4.albums[al].name);
	}
	return mp4_now_screen(a);
}

/* A on card `k` of MP4's shelf: its album's tracks, over the shelf, or a
 * book where it was left - and a film, which is one file, straight onto the
 * player; a folder of videos lists its episodes like an album. */
static mp4_exit mp4_album(app *a, const sysview *v, int k)
{
	if (!v->album || k < 0 || k >= v->list.count) return MP4_BACK;
	if (g_mp4.albums[v->album[k]].book) return mp4_book(a, v->album[k]);
	if (g_mp4.albums[v->album[k]].solo) return mp4_film(a, v->album[k]);
	return mp4_tracks(a, v->album[k], false);
}

/* MP4's shelf again after its menu, whose Rescan Folder rebuilds every view
 * on the card, so the view being held is found again rather than trusted.
 * NULL when there is no MP4 left to show. */
static sysview *mp4_shelf_refind(app *a)
{
	sysview *v;
	int i, mp4 = -1;

	for (i = 0; i < a->sys.count; i++)
		if (is_mp4(&a->sys.systems[i])) { mp4 = i; break; }
	if (mp4 < 0 || a->view[mp4].list.count <= 0) return NULL;
	a->sys_cursor = mp4;
	a->screen = SCREEN_GAMES;
	v = &a->view[mp4];
	if (v->cursor >= v->list.count) v->cursor = v->list.count - 1;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
	return v;
}

/* Which card on MP4's shelf is that album, or -1. */
static int mp4_card_of(const sysview *v, int album)
{
	int k;

	for (k = 0; v->album && k < v->list.count; k++)
		if (v->album[k] == album) return k;
	return -1;
}

/* MP4'S SHELF, OPENED ON TOP OF WHATEVER WAS ON SCREEN. Eric's, 2026-09-20.
 *
 * SELECT used to open a list of artists, because this shelf belongs to the
 * main loop - it is the games shelf, drawn for MP4's own view - and SELECT
 * can be pressed inside a menu, on Play Time, or in the in-game menu over a
 * paused game, none of which the main loop is driving. A list could draw over
 * any of those and the shelf could not, so the front door to a screen made
 * entirely of covers was text. The rest of the launcher is pictures.
 *
 * So the shelf is driven from here as well: the same view, the same cards, the
 * same movement, and draw_shelf doing the drawing - the one loop in this file
 * that borrows another screen's. What it does NOT borrow is update_games'
 * input, because B has to come back here rather than walk out to the systems
 * row, and MENU has to survive a Rescan rebuilding every view underneath it.
 *
 * The screen and the cursor are put back on the way out, so whatever opened
 * MP4 is still there when it closes. */
void mp4_shelf_screen(app *a, bool now)
{
	int prev_screen = a->screen, prev_sys = a->sys_cursor;
	sysview *v;
	int mp4 = -1, i, playing, base;
	bool done = false;

	for (i = 0; i < a->sys.count; i++)
		if (is_mp4(&a->sys.systems[i])) { mp4 = i; break; }
	if (mp4 < 0 || a->view[mp4].list.count <= 0) return;

	a->sys_cursor = mp4;
	mp4_input_reset();
	enter_system(a);                      /* the screen, the coverflow, the window */
	v = &a->view[mp4];
	base = panel_base();

	/* On the album that is playing, so SELECT lands where the music is - on
	 * the books when it is a book. */
	playing = mp4_playing_album(NULL);
	if (playing >= 0 && mp4_kind_shown() != ML_ALL &&
	    g_mp4.albums[playing].kind != mp4_kind_shown())
		mp4_show(v, g_mp4.albums[playing].kind);
	if (playing >= 0) {
		int k = mp4_card_of(v, playing);

		/* There at once in every View: the grid and the list placed
		 * again on it rather than gliding from where they were */
		if (k >= 0) {
			v->cursor = k;
			cf_reset(&v->cf, k);
			games_reset(&v->row, k, v->list.count, plat_now_ms());
			v->lm.set = v->gm.set = false;
		}
	}
	if (now && playing >= 0) {
		/* A film is its player, with no list of one under it. */
		mp4_exit how = g_mp4.albums[playing].solo ? mp4_now_screen(a)
		                                            : mp4_tracks(a, playing, true);

		if (how == MP4_CLOSE || g_mp4_gone) done = true;
		else if (how == MP4_REBUILT) {
			sysview *nv = mp4_shelf_refind(a);

			if (nv) v = nv; else done = true;
		}
	}

	while (!done && !want_quit && a->running) {
		int n = v->list.count, dir = 0;

		mp4_screen_poll();
		cover_answers();
		mp4_input(a);
		if (a->in.quit_requested) { a->running = false; break; }
		/* A plug on the USB port asks here too, as at the shelf: a DAC
		 * plugged in to listen gets its question (usb_questions) */
		if (usb_questions(a)) {
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
			mp4_input_reset();
			redraw_now();
			continue;
		}
		if (a->in.pressed[IN_POWER] || idle_due(a)) { mp4_power(a); break; }
		/* B and SELECT both leave, which is the rule everywhere in MP4: one
		 * button in, the same button out, and B for the level below. SELECT
		 * as it is let go, and not with START, which locks (mp4_input). */
		if (a->in.pressed[IN_BACK] || mp4_select()) {
			uisound_play(DS_SOUND_BACK);
			break;
		}

		/* Moved as a games shelf moves in the same View: the row of
		 * covers, the grid or the list (DESIGN.md 11.67) */
		if (n > 0) {
			int was = v->cursor;

			dir = shelf_move(a, v);
			games_select(&v->row, v->cursor, n, dir, plat_now_ms());
			/* where it lands asked for at once, as the main loop's
			 * window is (asking only queues) */
			if (v->cursor != was) prime_toward(a, mp4, v->cursor);
		}
		/* START turns the View, Covers to Grid to List and round, as it
		 * does on every shelf; as it is let go, since SELECT with START
		 * locks (mp4_input) */
		if (mp4_start()) {
			uisound_play(DS_SOUND_MOVE);
			view_turn(a, mp4, 1);
		}
		if (mp4_keys(a)) {
			mp4_locked(a);
			if (!a->running || g_mp4_gone) break;
			continue;
		}

		if (a->in.pressed[IN_ACCEPT]) {
			/* SELECT from the tracks or Now Playing closes all of MP4, and
			 * a rescan from their MENU leaves this shelf to be found again. */
			mp4_exit how;

			uisound_play(DS_SOUND_ACCEPT);
			how = mp4_album(a, v, v->cursor);

			if (how == MP4_CLOSE || g_mp4_gone) break;
			if (how == MP4_REBUILT) {
				sysview *nv = mp4_shelf_refind(a);

				if (!nv) break;
				v = nv;
			}
		} else if (a->in.pressed[IN_MENU]) {
			/* MP4's own menu. Sort By reorders this shelf and Rescan
			 * Folder rebuilds it, so it is found again either way. */
			sysview *nv;

			if (mp4_menu(a) == MP4_CLOSE) break;
			if (!(nv = mp4_shelf_refind(a))) break;
			v = nv;
		}

		tick_tint(a);
		tick_theme(a);
		/* As the main loop draws a shelf: the cover row's no-art sweep
		 * plays here as on any shelf (draw_shelf_live) */
		draw_shelf_live(a);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}

	panel_base_close(base);
	evict_far(v, TEX_KEEP_FAR);
	a->screen = prev_screen;
	a->sys_cursor = prev_sys;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- MP4: Album Art -------------------------------------------------------- */

/* The artist an album is under, and its folder relative to the Music folder. */
int mp4_artist_of(int al)
{
	int ar;

	for (ar = 0; ar < g_mp4.nartists - 1; ar++)
		if (al < g_mp4.artists[ar].first + g_mp4.artists[ar].n) break;
	return ar;
}

void mp4_album_dir(int al, char *out, size_t n)
{
	const char *p = g_mp4.albums[al].n > 0 ? g_mp4.tracks[g_mp4.albums[al].first].path : "";
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "%.*s", slash ? (int)(slash - p) : 0, p);
}

/* Which albums Album Art asks about: every one with no cover on the card, or
 * with one smaller on its short side than the shelf draws it - the album
 * frame's height, 461px - which the service has not already supplied. Eric's
 * rule, 2026-09-19. A cover the service gave is remembered, because its own
 * can be small too - La Strada's is 360px - and asking again would only fetch
 * the same one; an album it could not find is asked about on every run. */
/* The short side a cover must have not to be asked about again: the height
 * MP4's album row drew one, 461px, before its shelf was a games shelf's
 * (DESIGN.md 11.67), kept as Album Art's rule; larger than any cover is drawn
 * now (Now Playing's, NP_SIDE 432) */
#define ALBUM_CRISP 461

static int mp4art_jobs(mp4art_job *jobs, int max)
{
	int crisp = ALBUM_CRISP;
	int i, n = 0;

	for (i = 0; i < g_mp4.nalbums && n < max; i++) {
		char dir[LIB_PATH], key[LIB_PATH + 16], p[LIB_PATH * 2 + 8];
		mp4art_job *j = &jobs[n];
		int w = 0, h = 0;
		bool have;

		/* Not a book: MusicBrainz knows records, and a book's cover is its
		 * own file or the picture in its folder. Nor Singles, which is no
		 * record at all and has its own cover. */
		if (g_mp4.albums[i].n <= 0 || g_mp4.albums[i].kind != ML_MUSIC ||
		    g_mp4.albums[i].singles) continue;
		mp4_album_dir(i, dir, sizeof dir);
		snprintf(key, sizeof key, "mp4art.%s", dir);
		ml_cover_base(g_mp4_root, &g_mp4, i, j->base, sizeof j->base);
		snprintf(p, sizeof p, "%s.jpg", j->base);
		have = file_nonempty(p);
		if (!have) { snprintf(p, sizeof p, "%s.png", j->base); have = file_nonempty(p); }
		/* Remembered only while the cover it gave is on the card: one deleted
		 * since is asked about again, where the memory alone kept every such
		 * album off the list for good (2026-10-05). */
		if (have && db_has(db_lib(), key)) continue;
		if (have && mp4art_image_size(p, &w, &h) && (w < h ? w : h) >= crisp) continue;
		snprintf(j->artist, sizeof j->artist, "%s", g_mp4.artists[mp4_artist_of(i)].name);
		snprintf(j->album, sizeof j->album, "%s", g_mp4.albums[i].name);
		j->tracks = g_mp4.albums[i].n;
		j->id = i;
		n++;
	}
	return n;
}

/* A cover arrived for album `al`: remember where it came from, and drop the old
 * one from the shelf and from Now Playing so both draw the new one. The whole
 * worker queue is disowned, not just this card, because a decode of the OLD
 * file may already be in flight and would otherwise land on top. */
static void album_art_landed(app *a, int al, const char *rg)
{
	char dir[LIB_PATH], key[LIB_PATH + 16];
	int s;

	if (al < 0 || al >= g_mp4.nalbums) return;
	mp4_album_dir(al, dir, sizeof dir);
	snprintf(key, sizeof key, "mp4art.%s", dir);
	db_set_str(db_lib(), key, rg[0] ? rg : "fetched");
	if (g_cov) g_cov[al].st = COV_JPG;
	for (s = 0; s < a->sys.count; s++) {
		sysview *v = &a->view[s];
		int k;

		if (!is_mp4(&a->sys.systems[s]) || !v->tex || !v->album) continue;
		for (k = 0; k < v->list.count; k++)
			if (v->album[k] == al && v->tex[k]) {
				SDL_DestroyTexture(v->tex[k]);
				v->tex[k] = NULL;
			}
	}
	texload_bump();
	if (g_np.album == al) np_forget();
}

/* ---- MP4: album covers from the web page (USB File Transfer) ------------
 *
 * Album Art's rules, answered for the page at the other end of the cable,
 * which asks MusicBrainz and the Cover Art Archive itself: the Pixel has no
 * network. Which albums want a cover is mp4art_jobs, which release a reply
 * names is mp4art_pick, and a cover that lands is remembered as Album Art
 * remembers its own. See hare_set_albums. */

/* Where an album's cover goes, as a path the page uploads to: the card's
 * Music/<artist>/.media/<album>.jpg is music/<artist>/.media/<album>.jpg. */
static bool album_cover_path(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	size_t root = strlen(g_mp4_root);

	ml_cover_base(g_mp4_root, &g_mp4, al, base, sizeof base);
	if (strncmp(base, g_mp4_root, root) || strncmp(base + root, "/Music/", 7))
		return false;
	return snprintf(out, n, "music/%s.jpg", base + root + 7) < (int)n;
}

/* The album a pick answered and its release group, until its cover lands.
 * One at a time: the page uploads each cover straight after its pick. */
static int  g_pick_al = -1;
static char g_pick_rg[64];

char *albums_wanted(size_t *len, char *why, size_t wn)
{
	mp4art_job *jobs = calloc((size_t)(g_mp4.nalbums > 0 ? g_mp4.nalbums : 1),
	                           sizeof *jobs);
	hj o = { 0 };
	bool first = true;
	int n, i, k;

	if (!jobs) { snprintf(why, wn, "out of memory"); return NULL; }
	n = mp4art_jobs(jobs, g_mp4.nalbums);
	hj_lit(&o, "{\"albums\":[");
	for (i = 0; i < n; i++) {
		char artists[MP4ART_TRIES][128], albums[MP4ART_TRIES][128];
		char url[1024], cover[LIB_PATH * 2];
		int tries = mp4art_tries(jobs[i].artist, jobs[i].album, artists, albums);

		if (!album_cover_path(jobs[i].id, cover, sizeof cover)) continue;
		if (!first) hj_lit(&o, ",");
		first = false;
		hj_lit(&o, "{\"id\":");
		hj_int(&o, jobs[i].id);
		hj_lit(&o, ",\"artist\":");
		hj_str(&o, jobs[i].artist);
		hj_lit(&o, ",\"album\":");
		hj_str(&o, jobs[i].album);
		/* The searches in Album Art's order, each asked only if the one
		 * before named nothing. */
		hj_lit(&o, ",\"searches\":[");
		for (k = 0; k < tries; k++) {
			if (!mp4art_search_url(artists[k], albums[k], url, sizeof url)) continue;
			if (k) hj_lit(&o, ",");
			hj_str(&o, url);
		}
		hj_lit(&o, "]}");
	}
	hj_lit(&o, "]}");
	free(jobs);
	return hj_done(&o, len, why, wn);
}

char *album_pick(int id, const char *json, size_t n, size_t *len,
                 char *why, size_t wn)
{
	char rg[64] = "", cover[LIB_PATH * 2] = "", base[LIB_PATH * 2], *slash;
	hj o = { 0 };

	if (id < 0 || id >= g_mp4.nalbums || g_mp4.albums[id].kind != ML_MUSIC) {
		snprintf(why, wn, "no such album");
		return NULL;
	}
	if (mp4art_pick(json, n, g_mp4.albums[id].n, rg, sizeof rg) &&
	    album_cover_path(id, cover, sizeof cover)) {
		/* The .media folder, which an album whose files never carried a
		 * cover does not have: the upload needs it to exist. */
		ml_cover_base(g_mp4_root, &g_mp4, id, base, sizeof base);
		if ((slash = strrchr(base, '/'))) { *slash = '\0'; mkdir(base, 0755); }
		g_pick_al = id;
		snprintf(g_pick_rg, sizeof g_pick_rg, "%s", rg);
	} else {
		rg[0] = cover[0] = '\0';
	}
	hj_lit(&o, "{\"rg\":");
	hj_str(&o, rg);
	hj_lit(&o, ",\"cover\":");
	hj_str(&o, cover);
	hj_lit(&o, "}");
	return hj_done(&o, len, why, wn);
}

/* Every upload, from the transfer screen: a game's cover is shrunk, and the
 * cover of the album last picked is taken as Album Art takes one, the PNG the
 * files carried removed and the release group remembered. */
void xfer_after_write(const char *abs)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];

	hareart_after_write(abs);
	if (g_pick_al < 0 || g_pick_al >= g_mp4.nalbums) return;
	ml_cover_base(g_mp4_root, &g_mp4, g_pick_al, base, sizeof base);
	snprintf(p, sizeof p, "%s.jpg", base);
	if (strcmp(p, abs)) return;
	snprintf(p, sizeof p, "%s.png", base);
	remove(p);
	if (g_logs_app) album_art_landed(g_logs_app, g_pick_al, g_pick_rg);
	g_pick_al = -1;
}

/* Album Art, from MP4's menu: MusicBrainz and the Cover Art Archive, for the
 * albums mp4art_jobs chooses. The covers change on the shelf behind the panel
 * as they arrive. B stops. */
void album_art_screen(app *a)
{
	mp4art_job *jobs;
	mp4art_status st;
	menu_row rows[3];
	char head[300], fit[200], where[64], counts[160];
	bool working, done = false;
	int n, lv;
	float k;

	if (!net_online()) {
		menu_row row = { MSG_NEEDS_WIFI, NULL, false };

		timed_message(a, T(S_MP4_ART_TITLE), &row, 1, 0, ZYPEROS_ACCENT);
		return;
	}
	jobs = calloc((size_t)(g_mp4.nalbums > 0 ? g_mp4.nalbums : 1), sizeof *jobs);
	if (!jobs) return;
	n = mp4art_jobs(jobs, g_mp4.nalbums);
	/* The one async slot, which an account answer from a game just quit may
	 * still be holding - the same reason Box Art gives it up first. */
	sync_abandon();
	working = mp4art_begin(jobs, n, "/tmp/zyperos-musicbrainz.json");
	free(jobs);
	fprintf(stderr, "album art: %d album%s to ask about\n", n, n == 1 ? "" : "s");

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();
	while (!done && !want_quit && a->running) {
		if (working && mp4art_step(plat_now_ms()) == 0) {
			working = false;
			mp4art_status_get(&st);
			fprintf(stderr, "album art: %d found, %d not found, %d failed%s%s\n",
			        st.found, st.missing, st.failed, st.problem[0] ? " - " : "",
			        st.problem);
		}
		mp4art_status_get(&st);
		if (st.landed >= 0) album_art_landed(a, st.landed, st.rg);

		ui_fit_text(ui_font(UI_F_LABEL),
		            working ? (st.now[0] ? st.now : T(S_MP4_ART_STARTING))
		            : st.problem[0] ? st.problem
		            : n == 0 ? T(S_MP4_ART_ALL_CRISP) : T(S_MP4_ART_DONE),
		            fit, sizeof fit, ZYPEROS_SCREEN_W * 3 / 4);
		snprintf(head, sizeof head, "%s\n%s", T(S_MP4_ART_TITLE), fit);
		snprintf(where, sizeof where, "%d / %d", st.done, st.n);
		if (st.failed)
			snprintf(counts, sizeof counts, T(S_MP4_ART_COUNTS_FAILED),
			         st.found, st.missing, st.failed);
		else
			snprintf(counts, sizeof counts, T(S_MP4_ART_COUNTS), st.found, st.missing);
		rows[0] = (menu_row){ T(S_MP4_ART_ALBUMS), where,  false };
		rows[1] = (menu_row){ T(S_MP4_ART_COVERS), counts, false };
		rows[2] = (menu_row){ working ? b_legend(S_KEYHINT_STOP) : b_legend(S_KEYHINT_CLOSE), NULL, false };

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { mp4art_cancel(); a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			mp4art_cancel();
			power_off(a);
			return;
		}
		if (menu_leaving(a)) done = true;
		levels_keys(&a->in, BOOST_OPEN);

		/* Idle does not count while covers are arriving: it is the device
		 * doing what it was asked, and powering off halfway loses the rest.
		 * Said as every long task says it (src/taskhold.h). */
		if (working) task_note(TASK_COVERS, plat_now_ms());

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw(a, head, rows, 3, -1, menu_std_width(a), ZYPEROS_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
	mp4art_cancel();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}
