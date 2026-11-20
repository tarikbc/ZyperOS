/* SPDX-License-Identifier: GPL-3.0-only */
#include "ingame_screen.h"
#include "hkstore.h"
#include "hudmenu_screen.h"
#include "dmodepreview.h"
#include "menu_pick.h"

#include "achtab_screen.h"
#include "cheats_screen.h"
#include "coreopts_page.h"
#include "aout_route.h"
#include "app.h"
#include "border_pic.h"
#include "handframe.h"
#include "controls.h"
#include "ctl_channel.h"
#include "ctltab.h"
#include "game_menu.h"
#include "look.h"
#include "mp4c.h"
#include "p2.h"
#include "panel.h"
#include "portmenu.h"
#include "rabadge.h"
#include "rafetch.h"
#include "remapstore.h"
#include "strength.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "shaderdemo.h"
#include "levels.h"
#include <unistd.h>

/* ---------- launching ----------------------------------------------------- */

/* The in-game menu - the launcher's, over Diatom's pause.
 *
 * MENU in a game makes Diatom write a preview of the frame, stop presenting,
 * and say PAUSED. From that word the display is ours: the menu is that frame
 * dimmed, with the rows a player expects - Continue, Save, Load, Reset, Quit
 * - drawn with the launcher's own font and glow. The menu is the launcher's,
 * not something patched into the emulator.
 *
 * Every row acts through one protocol line. Save and Load use the same autosave
 * paths the launch handed over, so the autosave funnel stays one thing. */
/* Display sits with the things you do to the game rather than the things you do
 * to a save, because it is the one row whose effect you judge by looking at the
 * game behind the menu. */
/* gm_row, gm_ui and gm_rows: src/game_menu.h */

/* The paused frame, drawn where the game actually is.
 *
 * Diatom reports its rect with every DISPLAY message (put there so a launcher
 * need not recompute it from geometry it does not have), and the preview it
 * writes is the CORE'S frame - 256x224 for an NES, no display mode applied.
 * Stretching that to the panel, which is what this used to do, showed a game
 * that looked nothing like the one paused underneath at any mode that does
 * not fill the screen, and made the picture jump size the moment MENU was
 * pressed.
 *
 * Falls back to filling the panel when Diatom has not said - the standalone
 * path, and the first moments of a launch. */
static void draw_paused_frame(app *a, SDL_Texture *bg)
{
	SDL_Rect r;

	if (!bg) return;
	if (plat_resident_rect(&r)) SDL_RenderCopy(a->r, bg, NULL, &r);
	else                        SDL_RenderCopy(a->r, bg, NULL, NULL);
}

/* What every screen over a paused game sits on - the in-game menu, the save
 * slots, the achievements: the frame where the game is, on **ink** where it
 * is not, under **scrim.game**, which is as heavy in Beard as in Salt &
 * Pepper because the game is content and not part of the theme (DESIGN.md
 * 3.4). The in-game menu had the shelf's lighter scrim, and the slots and
 * achievements beside it the heavy one, over the same paused game. The
 * scrim at `k` of its strength, which is 1 but while the in-game menu opens
 * or closes (panel_frame).
 *
 * And the status corner over it all, as on the shelf (DESIGN.md 11.12): this
 * process draws these screens, so it draws the corner (in the game, Diatom
 * draws its disc). In Salt & Pepper, as scrim.game is dark in both themes,
 * with the clock in **text**, since a white frame under the scrim leaves
 * text-soft at 4.4:1. Each of these screens' loops draws every frame, so the
 * blink needs nothing more. */
void paused_backdrop(app *a, SDL_Texture *bg, float k)
{
	const ds_rgb ink = theme()->ink;

	g_frame_no++;
	SDL_SetRenderDrawColor(a->r, ink.r, ink.g, ink.b, 255);
	SDL_RenderClear(a->r);
	drawn_forget();
	draw_paused_frame(a, bg);
	ui_scrim_k(a->r, 1, k);
	/* The top bar (src/topbar.h): the clock at the top left and the marks
	 * with the battery's text at the top right, as on the home, so they match
	 * and a long Time Left text never meets the clock. */
	draw_topbar(a->r, DS_DARK, ui_col(theme_at(DS_DARK)->text, 255), frames_wanted());
}

/* A save's moment as the strip shows it, "Aug 23 9:21:05 AM": the month's
 * short name and the order of the parts are the language's (slots.when) */
void slot_when(char *out, size_t n, const struct tm *tm)
{
	static const str_id MON[12] = {
		S_SLOTS_JAN, S_SLOTS_FEB, S_SLOTS_MAR, S_SLOTS_APR, S_SLOTS_MAY, S_SLOTS_JUN,
		S_SLOTS_JUL, S_SLOTS_AUG, S_SLOTS_SEP, S_SLOTS_OCT, S_SLOTS_NOV, S_SLOTS_DEC,
	};
	int h12 = tm->tm_hour % 12;
	const char *mon = T(MON[tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0]);

	/* In the clock MENU > Clock chose, as the status corner is */
	if (g_clock24) {
		snprintf(out, n, T(S_SLOTS_WHEN_24), mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		return;
	}
	if (!h12) h12 = 12;
	snprintf(out, n, T(S_SLOTS_WHEN), mon, tm->tm_mday, h12, tm->tm_min, tm->tm_sec,
	         T(tm->tm_hour < 12 ? S_SLOTS_AM : S_SLOTS_PM));
}

/* Drawing only, so one frame of it can be rendered by --shot without a game
 * running or a device in hand.
 *
 * IN SALT & PEPPER'S ROLES, WHATEVER THEME IS ON. There is no panel here: the
 * heading, the picture, its name and time and the dots sit straight on
 * **scrim.game**, which is ink at 73% over a paused game in both themes, so
 * the ground they are read against is dark in Beard too. Beard's charcoal on
 * it had nothing to stand out from. The in-game menu beside it is a panel,
 * which brings its own surface and needs none of this. */
void slot_draw(app *a, const slot_view *sv, int sel)
{
	const ds_theme *dk = theme_at(DS_DARK);
	/* `area` is the layout slot the picture is fitted into; nothing is ever
	 * drawn to it. The border is the picture's own edge - a 240x160 GBA frame
	 * and a 256x224 NES frame are different shapes, and neither should be
	 * padded out into the same rectangle. */
	const SDL_Rect area = { (ZYPEROS_SCREEN_W - 700) / 2, 129, 700, 451 };
	const int bw = UI_PANEL_BORDER;
	SDL_Rect img = area, frame;
	char slotname[64];
	int line_menu = ui_font_line(UI_F_MENU), line_meta = ui_font_line(UI_F_META);
	int i;

	/* The heading sits close to the top edge so the picture gets the middle of
	 * the screen. Everything below hangs off the IMAGE rather than off `area`,
	 * so a frame shorter than the layout slot pulls its own caption up with it
	 * instead of leaving a gap.
	 *
	 * The margins are mirrored: the heading's top sits as far from the top of
	 * the screen as the marker rail's bottom sits from the bottom of it, and
	 * the heading is centered in the gap above the frame. That fixes every
	 * number here to one another rather than to taste, so changing the image
	 * size moves the rest to match instead of drifting into something.
	 *
	 * **text-soft**, as every heading is (DESIGN.md 11.3); it was the one
	 * heading in **text-dim**. */
	ui_text(a->r, ui_font(UI_F_LABEL), sv->saving ? T(S_SLOTS_SAVE_TO) : T(S_SLOTS_LOAD_FROM),
	        ZYPEROS_SCREEN_W / 2, ui_line_y(UI_F_LABEL, 39), 0,
	        ui_col(dk->text_soft, 255));

	if ((float)area.w / sv->aspect <= (float)area.h) {
		img.w = area.w;
		img.h = (int)(area.w / sv->aspect + 0.5f);
	} else {
		img.h = area.h;
		img.w = (int)(area.h * sv->aspect + 0.5f);
	}
	img.x = area.x + (area.w - img.w) / 2;
	img.y = area.y + (area.h - img.h) / 2;
	frame = (SDL_Rect){ img.x - bw, img.y - bw, img.w + bw * 2, img.h + bw * 2 };

	/* A thumbnail's lift, DESIGN.md 7 (focus-small), in the system's color:
	 * Salt & Pepper's, a glow, on this ground in both themes - Beard's ink
	 * shadow on ink at 73% lifts nothing. */
	ui_lift_in(a->r, &frame, a->tint, UI_LIFT_FOCUS_SMALL, DS_DARK);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	/* Opaque, matching the menu panel's border: DESIGN.md's frame, 3 units of
	 * **edge**. It was 12 of the system's color when the panel's was, and a
	 * system's color for decoration is what DESIGN.md 3.3 says it never is;
	 * the dot under the picture says whose game it is. The thumbnail itself
	 * is drawn at full brightness below - it never was darkened, only the
	 * frame around it.
	 *
	 * Rounded outside, square inside: the corner radius belongs to the chrome,
	 * and rounding the image would mean clipping the game's own pixels. */
	ui_round_rect(a->r, &frame, bw, ui_col(dk->edge, 255));
	if (sv->thumb[sel]) {
		SDL_SetTextureColorMod(sv->thumb[sel], 255, 255, 255);
		SDL_RenderCopy(a->r, sv->thumb[sel], NULL, &img);
	} else {
		/* An empty slot is the same frame with nothing in it, so the strip
		 * does not change shape as you cycle past one. */
		SDL_SetRenderDrawColor(a->r, dk->ground.r, dk->ground.g, dk->ground.b,
		                       238);
		SDL_RenderFillRect(a->r, &img);
		ui_text(a->r, ui_font(UI_F_MENU), T(S_SLOTS_EMPTY), img.x + img.w / 2,
		        ui_cap_y(ui_font(UI_F_MENU), img.y + img.h / 2), 0,
		        ui_col(dk->text_dim, 255));
	}

	if (sel == 0) snprintf(slotname, sizeof slotname, "%s", T(S_SLOTS_AUTO));
	else          snprintf(slotname, sizeof slotname, T(S_SLOTS_SLOT), sel);
	ui_text(a->r, ui_font(UI_F_MENU), slotname, ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_MENU, img.y + img.h + 36), 0, ui_col(dk->text, 255));
	ui_text(a->r, ui_font(UI_F_META),
	        sv->have[sel] ? sv->when[sel] : (sv->saving ? "\xE2\x80\x94" : ""),
	        ZYPEROS_SCREEN_W / 2,
	        ui_line_y(UI_F_META, img.y + img.h + 42 + line_menu), 0,
	        ui_col(dk->text_dim, 255));

	/* The dot rail: where you are among the slots, without showing every
	 * picture. The one you are on in the system's color as a mark - Salt &
	 * Pepper's mark, on this ground - a slot you can land on in **text-dim**,
	 * held back so it never matches the mark of a system with no color, and
	 * one you cannot in faint **track**. Three steps that stay apart over a
	 * dark game and a bright one, which the scrim leaves at ink to mid grey. */
	{
		int dots = GM_SLOTS + 1, dw = 36;
		int x0 = (ZYPEROS_SCREEN_W - dots * dw) / 2 + dw / 2;
		unsigned m = ui_sys_mark_in(a->tint, DS_DARK);
		ds_rgb here = { (Uint8)(m >> 16), (Uint8)(m >> 8), (Uint8)m };
		int y  = img.y + img.h + 62 + line_menu + line_meta;

		/* One size for every marker. Sizing the selected one larger meant the
		 * rail changed shape as you cycled, and color already says which slot
		 * you are on - two signals for one fact, one of them moving. */
		for (i = 0; i < dots; i++) {
			int can = sv->saving ? i >= 1 : sv->have[i];
			int r2  = 12;
			SDL_Rect d = { x0 + i * dw - r2, y - r2, r2 * 2, r2 * 2 };
			SDL_Color c = i == sel ? ui_col(here, 255)
			            : can      ? ui_col(dk->text_dim, 150)
			                       : ui_col(dk->track, 90);
			ui_round_rect(a->r, &d, r2 / 2, c);
		}
	}
}

/* Returns the chosen slot (SLOT_AUTO, or 1..GM_SLOTS) or 0 for backed out. */
static int slot_strip(app *a, SDL_Texture *bg, int saving)
{
	slot_view sv = { .aspect = 4.0f / 3.0f, .saving = saving };
	sysview *v = &a->view[a->sys_cursor];
	game_entry *g = &v->list.items[v->cursor];
	/* The game's own system, not the shelf: from Favorites the shelf is
	 * Favorites, which has no folder, and the slots came out loose in .zyperos/
	 * - a second set the system's shelf never saw. Reported 2026-09-24. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	int i, sel = -1, chosen = 0, done = 0;
	bool   have_b[GM_SLOTS + 1] = { false };
	char pth[LIB_PATH * 2];

	for (i = 0; i <= GM_SLOTS; i++) {
		int slot = gm_slot_at(i);
		struct stat st;

		slot_state_path(a, o, g, slot, pth, sizeof pth);
		sv.have[i] = (stat(pth, &st) == 0 && st.st_size > 0);
		if (sv.have[i]) {
			have_b[i] = true;
			/* The state's own mtime: when this moment was captured. The
			 * device clock is only as good as the device clock, and showing
			 * what the filesystem says beats pretending to know better. */
			struct tm *tm = localtime(&st.st_mtime);
			if (tm) {
				/* "Aug 23 9:21:05 AM". Built from the fields rather than with
				 * strftime's "%-I", which drops the leading zero but is a GNU
				 * extension and does nothing on the BSD libc the host build
				 * links against - it would have read right on the device and
				 * wrong in every screenshot. The month is the language's, as
				 * the menu's words are, not the C library's locale. */
				slot_when(sv.when[i], sizeof sv.when[i], tm);
			}

			slot_preview_path(a, o, g, slot, pth, sizeof pth);
			SDL_Surface *sf = IMG_Load(pth);
			if (sf) {
				sv.thumb[i] = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
		}
		/* Loading starts on the newest thing there is to load, which is
		 * almost always Auto. Saving is decided after the loop, below,
		 * because it needs to know about every slot before it can choose. */
		if (sel < 0 && !saving && sv.have[i]) sel = i;
	}

	/* Save lands on the first empty slot, or the last one when there is none -
	 * the rule and its reasoning are in game_menu.h, where a check can reach
	 * them. */
	if (saving) sel = gm_save_slot(have_b);

	for (i = 0; i <= GM_SLOTS; i++) {
		int tw = 0, th = 0;
		if (!sv.thumb[i]) continue;
		SDL_QueryTexture(sv.thumb[i], NULL, NULL, &tw, &th);
		if (tw > 0 && th > 0) { sv.aspect = (float)tw / (float)th; break; }
	}
	if (sel < 0) sel = saving ? 1 : -1;
	if (sel < 0) done = -1;                     /* nothing to load at all */

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		/* Each button read once a frame (in_repeat_axis): a repeat is
		 * true once, so a second read of a held Right was false and the
		 * carousel stepped back left on every repeat */
		int dir = in_repeat_axis(&a->in, IN_LEFT, IN_RIGHT);

		if (dir) {
			int next = sel;
			do {
				next = (next + dir + GM_SLOTS + 1) % (GM_SLOTS + 1);
			} while ((saving ? next == 0 : !sv.have[next]) && next != sel);
			step_sound(a, dir > 0 ? IN_RIGHT : IN_LEFT, sel, next);
			sel = next;
		}
		if (menu_leaving(a)) done = -1;
		/* SLOT_AUTO, not 9. Commit 4208619 renamed the resume slot from
		 * MinUI's number to `auto` and replaced it everywhere the constant
		 * was used - but here the old number was written out rather than
		 * referenced, so it survived the rename. Loading Auto from the menu
		 * has asked for a `.9.state` that has not existed since 2026-08-27,
		 * and every one of those was a state_rejected. */
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			chosen = gm_slot_at(sel);
			done = 1;
		}
		/* This screen used to ignore the power button outright - the one
		 * screen in the launcher that did. Stop the game and close with
		 * nothing chosen; game_menu sees the flag and closes behind us. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = -1;
		}

		tick_theme(a);
		paused_backdrop(a, bg, 1.0f);
		slot_draw(a, &sv, sel);
		plat_present();
		ui_frame_rest(8);
	}

	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
	return done == 1 ? chosen : 0;
}

/* Change the mode, keep it, tell the running game, and take back the rect so
 * the backdrop behind the menu redraws where the game is about to be. */
static void gm_cycle_display(app *a, int d)
{
	/* The OWNER's, so a mode chosen while playing a favorite is set for the
	 * system that game belongs to - which is what a per-system mode means.
	 * Set on the Favorites shelf it would have been filed under FAV and worn
	 * by every game launched from there, whatever machine it came from. */
	sysview *v = owner_view(a);

	v->dmode = (v->dmode + d + DMODE_COUNT) % DMODE_COUNT;
	display_save(a);
	plat_resident_line("SETDISPLAY\tmode=%s", DMODES[v->dmode].name);
	plat_resident_sync_rect(150);
}

/* The shader the same way, the owner's too: kept for the system and sent to
 * the game, which draws with it from the frame after Continue. The paused
 * picture under the menu is the core's frame, drawn by this process, so it
 * does not change; Diatom's DISPLAY answer is read off as Display's is. */
static void gm_cycle_shader(app *a, int d)
{
	sysview *v = owner_view(a);

	v->shader = shader_step(v->shader, d, a->sys.systems[v - a->view].tag);
	shader_save(a, (int)(v - a->view));
	plat_resident_line("SETDISPLAY\tfilter=%s", SHADERS[v->shader].name);
	plat_resident_sync_rect(150);
}

/* Its strength the same way (src/strength.h), stopping at the ends */
static void gm_step_strength(app *a, int d)
{
	sysview *v = owner_view(a);
	int k = strength_step(v->strength, d);

	if (k == strength_clamp(v->strength)) return;
	v->strength = k;
	strength_write(db_dev(), a->sys.systems[v - a->view].tag, k);
	plat_resident_line("SETDISPLAY\tstrength=%d", k);
	plat_resident_sync_rect(150);
}

/* The discs of the game running (issue #102), as Diatom's DISC answers say:
 * asked when the in-game menu opens and after each SETDISC, and kept, because
 * gm_gather runs on every key and a paused Diatom is not asked that often.
 * count under 2 is no row: a game on one disc, one whose core has no tray,
 * and a Diatom too old to answer (it says nothing to DISC, so the wait below
 * runs out and the menu is what it was). A shot's (--gm-discs) stands in. */
gm_disc_state g_gm_disc;
int g_shot_discs;
const char *g_shot_disc_label;
/* --gm-undo: Undo Load's row in a shot, with no state behind it */
bool g_shot_undo;
/* --gm-rumble: the game's core asked for the motor, so the Controls tab has
 * its Rumble row in a shot */
bool g_shot_rumble;

/* Where the focused game's Undo Load state is (SLOT_UNDO): as its slots are,
 * under its own system on Favorites too (shelf_owner). False with no game. */
static bool undo_path(app *a, char *out, size_t n)
{
	sysview *sv = &a->view[a->sys_cursor];

	if (sv->list.count <= 0 || sv->cursor < 0 || sv->cursor >= sv->list.count) return false;
	slot_state_path(a, shelf_owner(a, a->sys_cursor, sv->cursor),
	                &sv->list.items[sv->cursor], SLOT_UNDO, out, n);
	return true;
}

/* "DISC\tindex=1\tcount=3\tejected=0\tlabel=..." into g_gm_disc. The label is
 * the last field and may hold spaces; Diatom made its tabs spaces. */
static void gm_disc_note(const char *l)
{
	const char *p;

	g_gm_disc.count = 0;
	g_gm_disc.index = 0;
	g_gm_disc.label[0] = '\0';
	if ((p = strstr(l, "\tcount="))) g_gm_disc.count = atoi(p + 7);
	if ((p = strstr(l, "\tindex="))) g_gm_disc.index = atoi(p + 7);
	if ((p = strstr(l, "\tlabel="))) snprintf(g_gm_disc.label, sizeof g_gm_disc.label, "%s", p + 7);
}

/* Ask for the tray as the menu opens: the same short wait as the other
 * answers it reads while paused (plat_resident_sync_screens, 300 ms) */
static void gm_disc_ask(void)
{
	char l[256];

	memset(&g_gm_disc, 0, sizeof g_gm_disc);
	if (g_shot_discs || g_port_menu) return;
	if (!plat_resident_line("DISC")) return;
	if (plat_resident_sync_line("DISC\t", l, sizeof l, 300)) gm_disc_note(l);
}

/* Disc `d` steps from the one in the tray, sent as one SETDISC (Diatom opens
 * the tray, changes the disc and closes it, as a console's lid did); its DISC
 * answer says which disc is in, so the row follows the answer, not the press.
 * A refused index answers ERROR and then DISC with the disc as it was. */
static void gm_disc_step_to(int d)
{
	char l[256];
	int to = gm_disc_step(g_gm_disc.index, g_gm_disc.count, d);

	if (g_shot_discs) {
		g_gm_disc.index = to;
		return;
	}
	if (!plat_resident_line("SETDISC\tindex=%d", to)) return;
	if (plat_resident_sync_line("DISC\t", l, sizeof l, 300)) gm_disc_note(l);
}

/* Whether the core's name for a disc fits the Disc row's value: the menu's
 * content (menu_most_w, gm_width's; the pads are outside it, and this took
 * them off it again until 1.0.6) less the widest label of the Game tab and
 * the gap between the columns, in the menu's own font. A device's fonts may measure
 * a little wider than the host's, so the same half-row of air as the menu's
 * own fit (fit_gap) is kept back. */
static bool gm_disc_fits(const char *label)
{
	TTF_Font *fm = ui_font(UI_F_MENU);
	gm_ui u;
	menu_row rows[GM_MAX_ROWS];
	gm_bufs b;
	int n, i, wl = 0, room;

	memset(&u, 0, sizeof u);
	u.discs = 2;
	u.quick = 1;
	n = gm_rows(&u, GT_GAME, rows, &b);
	for (i = 0; i < n; i++)
		if (rows[i].label && ui_text_width(fm, rows[i].label) > wl)
			wl = ui_text_width(fm, rows[i].label);
	room = menu_most_w() - wl - menu_row_h() - menu_row_h() / 2;
	return ui_text_width(fm, label) <= room;
}

/* The picture Diatom said it is drawing, `sw` x `sh` with the shape `aspect`
 * (a shot's own when it asks for one); 0 x 0 when Diatom said none */
static void gm_source(int *sw, int *sh, double *aspect)
{
	if (shot_path && shot_gm_src_w > 0) {
		*sw = shot_gm_src_w;
		*sh = shot_gm_src_h;
		*aspect = shot_gm_aspect;
	} else if (!plat_resident_source(sw, sh, aspect)) {
		*sw = *sh = 0;
		*aspect = 0.0;
	}
}

/* Whether the game's display mode leaves room around it, by Diatom's own
 * size for it (gm_source) */
static bool gm_border_room(app *a)
{
	int sw, sh, fw, fh;
	double aspect;

	gm_source(&sw, &sh, &aspect);
	dmode_surface(&fw, &fh);
	return border_room(DMODES[owner_view(a)->dmode].name, sw, sh, aspect, fw, fh);
}

/* Border's row (src/border.h): its value while the display mode leaves room
 * around the game, or in any mode where a handheld's frame is drawn for its
 * system (choosing one turns Display to Sharp Pixels), and no Super Game Boy
 * border fills that room (its 256x224 on a Game Boy shelf that turns it on);
 * NULL, no row, else */
static const char *gm_border_value(app *a)
{
	sysview *v = owner_view(a);
	int sys = (int)(v - a->view), sw, sh;
	const system_cfg *s = &a->sys.systems[sys];
	double aspect;
	bool room;

	if (g_port_menu || g_game_dual) return NULL;
	gm_source(&sw, &sh, &aspect);
	room = gm_border_room(a);
	if (!border_row_shows(border_offer_room(a, sys, room), room)) return NULL;
	if (sgbm_shelf(s->tag, s->core) && sgbm_load(s) == SGBM_GAMES && sw == 256 && sh == 224) return NULL;
	return T(border_label_id(border_load_room(a, sys, room)));
}

/* Border stepped on left, right and A among what the row offers, kept for
 * the owner's system and sent to the game at once: Diatom takes SETBORDER
 * while paused, and the picture is around the game when it comes back. A
 * frame chosen at another mode turns Display to Sharp Pixels first, told to
 * the game before its frame, as the Display row tells it. */
static void gm_step_border(app *a, int d)
{
	sysview *v = owner_view(a);
	int sys = (int)(v - a->view);

	if (border_choose(a, sys, d, gm_border_room(a))) {
		display_save(a);
		plat_resident_line("SETDISPLAY\tmode=%s", DMODES[v->dmode].name);
		plat_resident_sync_rect(150);
	}
	border_send(a, sys);
}

/* What the in-game menu needs from the app, handed to gm_rows to become rows.
 * Same split as everywhere else. */
/* What the in-game menu shows, gathered: for its rows, and for where each
 * row is (gm_row_at), which the quick slot's row moves */
void gm_gather(app *a, gm_ui *out)
{
	gm_ui u;

	memset(&u, 0, sizeof u);
	/* The owner's, as the shader's: the game's own system's, which launch
	 * sent and the row sets (gm_cycle_display). The shelf's read Stretch
	 * over every favorite until 1.0.6: nothing sets Favorites' own mode. */
	u.dmode  = dmode_label(owner_view(a)->dmode);
	/* The owner's, which is the one its row sets (gm_cycle_shader) */
	u.shader = shader_label(owner_view(a)->shader);
	u.strength = strength_row_label(a, (int)(owner_view(a) - a->view));
	u.border = gm_border_value(a);
	u.border_color = u.border ? border_color_value(a, (int)(owner_view(a) - a->view)) : NULL;
	u.hud = db_get_int(db_dev(), HUD_KEY, 0) == 1;
	u.earned = chv_earned();
	u.total  = chv_count();
	/* g_game_set is the game running's, written at every launch, and
	 * g_game_no_hash beside it: a game with no hash here says so instead */
	u.no_hash = u.total > 0 || g_port_menu || g_shot_unchecked ? RA_HASH_OK
	            : g_shot_no_hash != RA_HASH_OK ? g_shot_no_hash : g_game_no_hash;
	u.unchecked = u.total == 0 && !g_port_menu && u.no_hash == RA_HASH_OK &&
	              (g_shot_unchecked ||
	               chv_state_of(g_game_set, ra_signed_in()) == CHV_UNCHECKED);
	u.screens = g_game_dual;
	u.port   = g_port_menu;
	u.waiting = g_port_waiting;
	/* The game's own system, on Favorites too (shelf_owner); none where its
	 * core keeps no states, and none over a program of its own (a "run:"
	 * one, src/portmenu.h), which has no states and no Pak */
	if (!u.port) {
		const system_cfg *q = &a->sys.systems[shelf_owner(a, a->sys_cursor,
		                                      a->view[a->sys_cursor].cursor)];

		u.quick = ctl_has_states(q->tag) ? quick_slot_get(q->tag) : 0;
		/* A state from before the last load: Undo Load. Asked of the card
		 * at each key, as cheap as the slot strip's look at its files */
		if (ctl_has_states(q->tag)) {
			char up[LIB_PATH * 2];

			u.undo = g_shot_undo || (undo_path(a, up, sizeof up) && access(up, R_OK) == 0);
		}
		/* An N64 game's Pak, on either N64 core */
		if (pak_system(q) && a->view[a->sys_cursor].list.count > 0)
			u.pak = gm_pak_name(pak_get(q, a->view[a->sys_cursor].list
			                                     .items[a->view[a->sys_cursor].cursor].file));
		/* NES Player 2, on the cores the system table offers it on and on
		 * a device that has it (a shot draws it on the host) */
		if (p2_offered(q->core, plat_player2() || g_shot_p2)) {
			u.player2 = true;
			u.player2_value = gm_player2_value(p2_row_state());
		}
	}
	if (!u.port) {
		if (g_shot_discs) {
			u.discs = g_shot_discs;
			u.disc  = g_gm_disc.index;
			u.disc_label = g_shot_disc_label;
		} else {
			u.discs = g_gm_disc.count;
			u.disc  = g_gm_disc.index;
			u.disc_label = g_gm_disc.label;
		}
		u.disc_fits = gm_disc_fits;
	}
	/* Cheats for every game Diatom runs (src/cheats_screen.h): how many are
	 * on, and whether one has paused the game's achievements */
	if (!u.port) {
		u.cheats = true;
		u.cheats_on = cheats_game_on();
		u.cheats_paused = cheats_game_paused();
	}
	if (u.screens) {
		const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor,
		                                      a->view[a->sys_cursor].cursor)];

		screens_get(s->tag, &u.layout, &u.swap);
		/* The swap and the stylus as Diatom last said: L2 and R2 in the
		 * game turn them, so this process only knows them from SCREENS
		 * swap= and STYLUS on=. */
		u.swap   = plat_resident_swap();
		u.stylus = plat_resident_stylus();
	}
	*out = u;
}

/* What the in-game menu remembers of the game running: the tab it was on,
 * the row each tab was left on (by identity; -1 is none, the tab's first)
 * and, for the Controls tab's Applies To, whether a change is for every
 * game of the system (Task 7 reads and writes it). Reset when a game
 * starts (launch) and kept across MENU presses until it ends, so MENU opens
 * where it was closed.
 *
 * -1 means none (first row of that tab). */
gm_mem_state g_gm_mem;

void gm_mem_reset(void)
{
	int t;

	g_gm_mem.tab = GT_GAME;
	for (t = 0; t < GT_TABS; t++) g_gm_mem.row[t] = -1;
	g_gm_mem.scope_all = false;
}

const char *gm_tab_name_i(int i) { return gm_tab_name(i); }

/* The panel's one width on every tab and every value, so it holds still as
 * the tabs go round; a port's menu has no tabs and sizes itself */

/* The list itself, over the paused frame. menu_draw already windows a list
 * longer than the screen, which a set of 166 certainly is.
 *
 * Earned rows are drawn live and unearned quiet - the same distinction
 * menu_draw makes for a placeholder, and it reads correctly here: what you
 * have is bright, what is still ahead of you is not. */

/* The code point that starts at p, and its bytes in *len */
static unsigned wrap_cp(const char *p, int *len)
{
	const unsigned char *u = (const unsigned char *)p;

	*len = 1;
	if (u[0] < 0xC0) return u[0];
	if (u[0] < 0xE0 && (u[1] & 0xC0) == 0x80) { *len = 2; return (u[0] & 0x1Fu) << 6 | (u[1] & 0x3F); }
	if (u[0] < 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
		*len = 3;
		return (u[0] & 0x0Fu) << 12 | (u[1] & 0x3Fu) << 6 | (u[2] & 0x3F);
	}
	return u[0];
}

/* Whether a line may end at byte `i` of `s` inside a run with no spaces:
 * src/linebreak.c's rule for Chinese, which has none - between two
 * characters when either is Chinese, but not before a mark that closes
 * (，。！…) nor after one that opens (（「“). `i` is where a character
 * starts. */
static bool wrap_cjk_break(const char *s, int i)
{
	static const unsigned closes[] = { 0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF01, 0xFF1F, 0xFF1A,
	                                   0xFF1B, 0xFF09, 0x300D, 0x300F, 0x300B, 0x3009, 0x3011,
	                                   0x201D, 0x2019, 0x2026, 0x30FC, 0x00B7 };
	static const unsigned opens[] = { 0xFF08, 0x300C, 0x300E, 0x300A, 0x3008, 0x3010,
	                                  0x201C, 0x2018 };
	unsigned prev, next;
	int j = i - 1, len;
	size_t k;

	if (i <= 0 || !s[i]) return false;
	while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;
	prev = wrap_cp(s + j, &len);
	next = wrap_cp(s + i, &len);
#define WRAP_CJK(c) (((c) >= 0x2E80 && (c) <= 0x9FFF) || ((c) >= 0xF900 && (c) <= 0xFAFF) || \
                     ((c) >= 0xFF00 && (c) <= 0xFFEF))
	if (!WRAP_CJK(prev) && !WRAP_CJK(next)) return false;
#undef WRAP_CJK
	for (k = 0; k < sizeof closes / sizeof closes[0]; k++) if (next == closes[k]) return false;
	for (k = 0; k < sizeof opens / sizeof opens[0]; k++) if (prev == opens[k]) return false;
	return true;
}

/* Break `src` into lines no wider than `w`, at whitespace, and in Chinese,
 * which has none, between its characters (wrap_cjk_break). Returns how many.
 *
 * menu_draw marquees a note that does not fit, which is right for a footer
 * carrying four facts and wrong for a sentence: the reader has to wait for the
 * text to come back around to re-read a clause. Three still lines beat one
 * moving one, so a description is wrapped here rather than handed over long.
 *
 * A word wider than the panel is cut instead of hunted for a space, because
 * the alternative is a line that overflows and there is nowhere else for it to
 * go. Measuring per prefix is quadratic in the line, which nobody will notice
 * at 191 characters, once, when the card opens.
 *
 * LINE BREAKS IN THE SOURCE ARE PARAGRAPHS, and this used to walk straight past
 * them: only ' ' broke a line and only ' ' was skipped, so a '\n' rode into the
 * middle of a line and SDL_ttf drew it as nothing - a hole two characters wide
 * after "Gunstar-9!!" with the next paragraph running on behind it. Of the 78
 * ScreenScraper synopses on this card 47 carry a break and 42 carry a blank
 * line; splitting at every break gives 191 pieces with a median of 276
 * characters, which is paragraphs rather than a source that hard-wraps. So any
 * run of whitespace containing a break ends the paragraph and gets one empty
 * row, however many blank lines the run actually held. Never at the top, where
 * it would only push the first line down off the rule. */
int wrap_text(TTF_Font *f, const char *src, int w,
              char out[][CHV_WRAP_COLS], int max)
{
	int n = 0;

	while (n < max) {
		char buf[CHV_WRAP_COLS];
		int take = 0, fit = 0, i, len;
		bool para = false;

		while (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t') {
			if (*src == '\n' || *src == '\r') para = true;
			src++;
		}
		if (!*src) break;
		if (para && n > 0) {
			out[n++][0] = '\0';
			if (n >= max) break;
		}
		for (i = 1; src[i - 1] && src[i - 1] != '\n' && src[i - 1] != '\r' &&
		            src[i - 1] != '\t'; i++) {
			if (i >= (int)sizeof buf) break;
			/* Inside a character's bytes: measured, and cut, only at its
			 * end, so no line ends in half a character */
			if (((unsigned char)src[i] & 0xC0) == 0x80) continue;
			memcpy(buf, src, (size_t)i);
			buf[i] = '\0';
			if (ui_text_width(f, buf) > w) break;
			fit = i;
			if (src[i] == ' ' || src[i] == '\n' || src[i] == '\r' ||
			    src[i] == '\t' || src[i] == '\0' || wrap_cjk_break(src, i)) take = i;
		}
		if (!take) {                    /* one word longer than the panel */
			wrap_cp(src, &len);
			take = fit > 0 ? fit : len;
			if (take >= (int)sizeof buf) take = sizeof buf - 1;
		}
		memcpy(out[n], src, (size_t)take);
		out[n][take] = '\0';
		n++;
		src += take;
	}
	return n;
}

/* One achievement, opened with A from the list.
 *
 * The list has room for a title and a number, and a title is a NAME RATHER
 * THAN AN INSTRUCTION - it says which achievement, not what to do for it. The
 * description that says has been in memory the whole time, parsed beside the
 * title by src/cheevos.c (parse_meta) and shown nowhere.
 *
 * Drawn with sel = -1, so nothing highlights. This is a card rather than a
 * list: there is nothing here to choose between, and a cursor would invite
 * pressing A on a row that does not answer.
 *
 * Returns true if the caller should close too, which is what MENU and power
 * mean anywhere else in the launcher. */
/* What the two achievement screens sit on.
 *
 * In a game that is the paused frame under a heavy dim, which is what makes a
 * menu read as being over something that has stopped. On the shelf it is the
 * shelf under the SAME lighter dim the game details screen uses, so opening
 * Cheevos from that screen does not darken the room on the way. The scrim at
 * `k` of its strength (panel_frame). */
static void chv_backdrop(app *a, SDL_Texture *bg, bool over_shelf, float k)
{
	if (!over_shelf) { paused_backdrop(a, bg, k); return; }
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

/* The achievement's badge on its card: the list's own picture of it
 * (src/rabadge.h), greyed and faded until earned, the plain trophy while the
 * card has no art, drawn near the art's own size - RetroAchievements' 64
 * pixels; CHV_CARD_BADGE, 96 on the grid, is 60 on the Pixel 2 - under the
 * heading's rule, centered over the description, which the card centers,
 * with the rule's gap under it. The panel's header picture (g_menu_header),
 * set around the card's draw. */
static SDL_Texture *g_card_badge;

static void cheevo_card_header(app *a, const SDL_Rect *r)
{
	if (g_card_badge)
		SDL_RenderCopy(a->r, g_card_badge, NULL,
		               &(SDL_Rect){ r->x + (r->w - CHV_CARD_BADGE) / 2, r->y,
		                            CHV_CARD_BADGE, CHV_CARD_BADGE });
}

/* The card's rows: the description wrapped whole, the rule, Points and
 * Status. Shared by the screen and --cheevo-card, so a shot is the card the
 * device draws. */
int cheevo_card_rows(const cheevo *c, bool pending, int fixed, cheevo_card *k)
{
	bool got = c->earned || c->earned_now;
	int n = 0, nl, i;

	memset(k->vcols, 0, sizeof k->vcols);
	/* WRAPPED WHOLE: as many lines as the description has. It was cut at
	 * twelve, which no real description reaches, and a cut sentence on a
	 * card is the one thing it must not do; every line takes at least one
	 * byte of the description, so CHV_DESC lines hold any. The body scrolls
	 * itself when the lines outrun the panel (menu_panel's card rule: no
	 * cursor, so only the body moves, and the footer stays). */
	nl = wrap_text(ui_font(UI_F_MENU),
	               c->desc[0] ? c->desc : T(S_CHEEVOLIST_NO_DESCRIPTION),
	               fixed, k->lines, CHV_CARD_LINES);
	if (!c->desc[0]) ui_fit_drawn(T(S_CHEEVOLIST_NO_DESCRIPTION));    /* drawn in lines */
	/* Live, unlike an ordinary note. A caption under a setting is a caption;
	 * here the description IS the content, and UI_TEXT_DIM is too quiet to be
	 * the only thing on the card worth reading. */
	for (i = 0; i < nl; i++)
		k->rows[n++] = (menu_row){ k->lines[i], MENU_NOTE_MARK, true };
	k->rows[n++] = MENU_RULE;
	snprintf(k->pts, sizeof k->pts, "%d", c->points);
	k->rows[n++] = (menu_row){ T(S_CHEEVOLIST_POINTS), k->pts, true };
	/* Earned and not on the account yet - earned offline - says so */
	k->rows[n] = (menu_row){ T(S_CHEEVOLIST_STATUS),
	                         !got ? T(S_CHEEVOLIST_NOT_EARNED)
	                         : pending ? T(S_CHEEVOLIST_EARNED_WAITING)
	                         : T(S_CHEEVOLIST_EARNED), true };
	k->vcols[n] = got ? VCOL_EARNED : 0u;
	n++;
	k->n = n;
	return n;
}

void cheevo_card_draw(app *a, const cheevo_card *k, const char *title,
                      SDL_Texture *badge, int fixed)
{
	g_card_badge = badge;
	g_menu_header = badge ? cheevo_card_header : NULL;
	g_menu_header_h = CHV_CARD_BADGE + menu_pad() / 2;
	menu_draw_ex(a, title, k->rows, k->n, -1, fixed, a->tint, k->vcols, false, 0);
	g_menu_header = NULL;
	g_card_badge = NULL;
}

static bool cheevo_detail_screen(app *a, SDL_Texture *bg, bool over_shelf,
                                 const cheevo *c)
{
	/* Kept off the stack: CHV_CARD_LINES lines are 24 KB */
	static cheevo_card k;
	bool got = c->earned || c->earned_now;
	int fixed = menu_std_width(a);
	int done = 0, lv;
	bool close_all = false;
	float k_scrim;

	cheevo_card_rows(c, got && chv_is_pending(c->id), fixed, &k);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_BACK);
			done = 1;
		}
		if (a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			done = 1; close_all = true; g_menu_closing = true;
		}
		/* Only over the shelf. Over a game this card is the in-game menu's,
		 * and MP4 would need that menu's power rule, which this screen does
		 * not have to give it. SELECT from the in-game menu itself works. */
		if (over_shelf && a->in.pressed[IN_SELECT]) mp4_open(a, NULL, NULL);
		/* Same reasoning as the list below: this screen is not inside
		 * plat_resident_wait, so nothing else is watching power for it. Over
		 * the shelf there is no game to stop, and stopping one is not what
		 * power means there. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
			close_all = true;
		}

		if (!panel_frame(a, lv, &k_scrim)) continue;
		tick_theme(a);
		chv_backdrop(a, bg, over_shelf, k_scrim);
		/* A badge that landed while the card is open is read on this frame */
		rb_landed();
		cheevo_card_draw(a, &k, c->title,
		                 rb_icon(a->r, chv_source(), c->badge, !got, theme()), fixed);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return close_all;
}

/* The synopsis, opened with A from the game info screen.
 *
 * The same shape as the achievement card above and for the same reason: there
 * is nothing here to choose between, so it is drawn with no cursor and scrolls
 * itself. A synopsis averages 670 characters and the longest on the test card
 * is 2,221, so most of them fit and the long ones walk.
 *
 * Over the shelf rather than a paused frame: this is reached from the shelf,
 * where the info screen already dims the same way. */
/* Room for the text twice over, plus the four blanks and the rule between the
 * copies. The rows are the wrapped lines POINTED AT twice, not wrapped twice,
 * so the line buffer stays SYN_WRAP_LINES - this is the only array that has to
 * grow, and 165 menu_rows is four kilobytes. */

/* The rows a synopsis is drawn as, and where they start repeating.
 *
 * Shared by the screen and by --synopsis, which used to build its own: the
 * harness then drew a card the device does not draw, which is the one thing a
 * harness must never do. Returns the row count; `rows` must hold SYN_ROWS. */
int syn_layout(const char *text, int fixed,
               char (*lines)[CHV_WRAP_COLS], menu_row *rows,
               int *loop_at)
{
	int n, i, k;

	*loop_at = 0;
	n = wrap_text(ui_font(UI_F_MENU), text, fixed, lines, SYN_WRAP_LINES);
	for (i = 0; i < n; i++)
		rows[i] = (menu_row){ lines[i], MENU_BODY_MARK, true };

	/* A SYNOPSIS TOO TALL FOR THE PANEL IS LAID OUT TWICE, with two blank
	 * lines, a rule and two blank lines between the copies, and menu_draw is
	 * told where the repeat starts.
	 *
	 * Both spacings were looked at on the device 2026-09-17. The rule carries
	 * air of its own - it is the bar the heading uses, with the heading's gap
	 * under it - and on its own it reads as a line ruled THROUGH the prose,
	 * with the last sentence and the first pressed against it. The blank lines
	 * are what make it the end of something.
	 *
	 * That is what turns the scroll from a ping-pong into a scroll-through.
	 * Running backwards is fine for a row of text too wide for its column,
	 * where the rewind is obviously a rewind; over fifteen lines of prose it
	 * means reading the end in reverse to get back to the beginning. Coming
	 * round instead needs somewhere to come round FROM, which is the rule: it
	 * says the text ended and is starting again, rather than leaving a reader
	 * to work out that the sentence they are on is one they have read.
	 *
	 * Only when it does not fit. A short synopsis sits still, and doubling it
	 * would make a page that fits scroll for no reason. */
	if (n <= menu_notes_fit()) return n;

	k = n;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_HR;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	*loop_at = k;
	for (i = 0; i < n && k < SYN_ROWS; i++)
		rows[k++] = (menu_row){ lines[i], MENU_BODY_MARK, true };
	return k;
}

void synopsis_screen(app *a, const char *title, const char *text,
                     unsigned accent)
{
	char (*lines)[CHV_WRAP_COLS];
	menu_row *rows;
	int fixed = menu_std_width(a);
	int n, done = 0, loop_at = 0, lv;
	float k;

	if (!text || !*text) return;
	lines = calloc(SYN_WRAP_LINES, sizeof *lines);
	rows  = calloc(SYN_ROWS, sizeof *rows);
	if (!lines || !rows) { free(lines); free(rows); return; }

	n = syn_layout(text, fixed, lines, rows, &loop_at);

	/* Opened, so its scroll starts at the top. Without this, closing a long
	 * synopsis and opening it again resumed halfway down: the clock's keys -
	 * the row count and the panel height - are identical on the way back in,
	 * so nothing told it a reader had arrived. */
	mq_reset(MQ_VSCROLL);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit && a->running) {
		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) break;
		if (a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_X]) uisound_play(DS_SOUND_BACK);
		if (menu_leaving(a) || a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_X]) done = 1;
		if (a->in.pressed[IN_SELECT]) mp4_open(a, NULL, NULL);
		/* Nothing else watches power for this screen, the same as every other
		 * loop the launcher runs outside plat_resident_wait. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw_ex(a, title, rows, n, -1, fixed, accent, NULL, false, loop_at);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	free(lines);
	free(rows);
}

/* The list's icon for row `row`: chv_at(row)'s badge, greyed until earned,
 * or the plain one where the card has none yet (src/rabadge.h). menu_panel's
 * g_row_icon while the list is drawn. Every row has one, so nothing moves
 * when a fetch lands the art: the next frame reads the file. */
SDL_Renderer *g_badge_r;

static SDL_Texture *cheevo_row_badge(int row)
{
	const cheevo *c = chv_at(row);

	return c ? rb_icon(g_badge_r, chv_source(), c->badge,
	                   !(c->earned || c->earned_now), theme()) : NULL;
}

void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf)
{
	int n = chv_count(), sel = 0, i, done = 0, lv;
	float k;
	menu_row *rows;
	unsigned *vcols;
	char (*vals)[32];
	char heading[192];

	if (n <= 0) return;
	rows  = calloc((size_t)n, sizeof *rows);
	vals  = calloc((size_t)n, sizeof *vals);
	vcols = calloc((size_t)n, sizeof *vcols);
	if (!rows || !vals || !vcols) { free(rows); free(vals); free(vcols); return; }

	/* Each row as the in-game menu's Achievements tab makes it
	 * (src/achtab.h): the title, the points or the count moving, earned in
	 * the earned color, so the two lists cannot disagree */
	for (i = 0; i < n; i++)
		at_row(chv_at(i), &rows[i], &vcols[i], vals[i], sizeof vals[i]);
	/* Two lines: the game on one, the counts on the other.
	 *
	 * On one line this was "Hagane: The Final Conflict   0/36   0/415 points",
	 * 1031 pixels against a 778 pixel panel - and menu_draw centers a heading,
	 * so it lost BOTH ends: the H and the word "points". Dropping the title
	 * fixed the clipping and threw away something worth keeping. Eric's
	 * suggestion, and it is better than either.
	 *
	 * The counts as DESIGN.md 12 writes them, "3 / 36 · 25 / 415 points".
	 * With "cheevos" after the first it was 818 wide in Lexend, more than
	 * the panel's content, and the line scrolled; at 585 it fits with room
	 * for a set of hundreds, and the list under it says what it counts. */
	/* And how many of this game's wait to be sent, earned offline, when any
	 * do: in the points' place, since the line with both was wider than the
	 * panel and scrolled its count off the left. Each row has its points. */
	if (chv_pending_for(chv_game()) > 0)
		snprintf(heading, sizeof heading,
		         TN(S_CHEEVOLIST_HEADING_WAITING, chv_pending_for(chv_game())),
		         chv_game_title(), chv_earned(), n, chv_pending_for(chv_game()));
	else
		snprintf(heading, sizeof heading, T(S_CHEEVOLIST_HEADING),
		         chv_game_title(), chv_earned(), n,
		         chv_points_earned(), chv_points_total());

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		plat_input_poll(&a->in);
		ui_sounds_follow();

		if (in_repeat(&a->in, IN_UP))   sel = list_step_heard(a, IN_UP, n, sel);
		if (in_repeat(&a->in, IN_DOWN)) sel = list_step_heard(a, IN_DOWN, n, sel);
		/* A set runs to well over a hundred entries, so the shoulder buttons
		 * page it the same way they page a shelf. */
		if (in_repeat(&a->in, IN_L1)) {
			step_sound(a, IN_L1, sel, sel > 8 ? sel - 8 : 0);
			sel = sel > 8 ? sel - 8 : 0;
		}
		if (in_repeat(&a->in, IN_R1)) {
			step_sound(a, IN_R1, sel, sel < n - 9 ? sel + 8 : n - 1);
			sel = sel < n - 9 ? sel + 8 : n - 1;
		}
		if (menu_leaving(a)) done = 1;
		if (over_shelf && a->in.pressed[IN_SELECT]) mp4_open(a, NULL, NULL);
		/* A used to close this screen. It opens the achievement instead,
		 * which is the only thing on it there was ever anything more to say
		 * about; BACK and MENU still close, so nothing lost a way out. */
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
		if (a->in.pressed[IN_ACCEPT] && cheevo_detail_screen(a, bg, over_shelf, chv_at(sel)))
			done = 1;
		/* Power still stops the game from in here. Not trapping it would
		 * make this screen the one place in the launcher that ignores it.
		 *
		 * The note is what was missing: stopping the game is not the same as
		 * powering off, and only the evdev watchdog inside plat_resident_wait
		 * sets that flag - a loop which is not running while this screen is
		 * up. So power here stopped the game and then went back to the shelf,
		 * with the menu still drawn over it until something was pressed.
		 *
		 * Opened from the game details screen there is no game to stop, and
		 * power means what it means everywhere else on the shelf. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		chv_backdrop(a, bg, over_shelf, k);
		/* visits_all: every achievement can be opened, earned or not, so
		 * `live` here is about color and not about reach. Each row with its
		 * badge, or the plain one until the card has it (g_row_icon); the
		 * count of badges that landed is read so it does not pile up, and
		 * the frame after a landing reads the files. */
		rb_landed();
		g_badge_r = a->r;
		g_row_icon = cheevo_row_badge;
		menu_draw_ex(a, heading, rows, n, sel, menu_std_width(a), a->tint,
		             vcols, true, 0);
		g_row_icon = NULL;
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	rb_textures_free();
	free(rows);
	free(vals);
	free(vcols);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* What the in-game Controls page needs to know about the game on the
 * shelf's cursor, which is the one running: its system (through Favorites
 * too, shelf_owner), that system's map and turbo, and the file's extension
 * (an .fds game turns its disk on L1 and R1). */
/* A Controls heading fits centered between two page counts' room, as
 * panel_show keeps it (DESIGN.md 11.3) */
bool ctl_head_fits(void *ctx, const char *head)
{
	app *a = ctx;
	int inset = ui_text_width(ui_font(UI_F_META), "4 / 4") + 2 * DS_GRID;

	return ui_text_width(ui_font(UI_F_LABEL), head) <= menu_std_width(a) - 2 * inset;
}

/* The port's system and game, for its Controls page (port_menu_main) */
ctl_game g_port_ctl;
ctl_game game_controls_of(app *a)
{
	if (g_port_menu) return g_port_ctl;
	sysview *sv = &a->view[a->sys_cursor];
	int o = shelf_owner(a, a->sys_cursor, sv->cursor);
	const system_cfg *s = &a->sys.systems[o];
	const char *file = sv->list.count > 0 ? sv->list.items[sv->cursor].file : "";
	const char *dot = strrchr(file, '.');

	/* The map in force for this game, whole: ZyperOS's, the system's and the
	 * game's pairs over it (rms_game) */
	static char map[CFG_STR + 200];
	rm_table mt;
	ctl_game g;

	if (sv->list.count > 0) rms_game(s, file, &mt);
	else rms_system(s, &mt);
	rm_format(&mt, map, sizeof map);
	g = (ctl_game){ s->tag, s->name, home_label(s->tag, s->name), map,
	                NULL, dot ? dot + 1 : "", ctl_head_fits, a, is_port(s), NULL, NULL, false,
	                CTL_STICK_TABLE };

	/* What the N64's C buttons page says is in the controller */
	if (pak_system(s) && sv->list.count > 0) g.accessory = gm_pak_name(pak_get(s, file));
	/* ...and what the d-pad is, as this game's D-pad as Stick has it; the
	 * PSP's R2 turns it in the game, and its page says so */
	if (rms_stick_offered(s) && strcmp(s->stick, "toggle") && sv->list.count > 0)
		g.stick = rms_stick_get(s, file) ? CTL_STICK_ON : CTL_STICK_OFF;
	/* A port's pages are what its own files say (src/portdoc.h), read for
	 * the script on the cursor as port_menu_main reads them over it: for a
	 * --gamecontrols shot, as a port never runs under this menu */
	if (!strcmp(s->core, "port") && sv->list.count > 0) {
		static port_doc doc;
		char path[LIB_PATH * 2];

		snprintf(path, sizeof path, "%s/%s/%s", P_ROMS, s->folder, file);
		port_doc_read(path, &doc);
		g.doc = &doc;
	}
	return g;
}

/* A Controls page as it is drawn: ctl_game_rows', or for a port's About
 * (ctl_game_prose) its paragraphs wrapped to the panel, `width`, and laid
 * out as a synopsis is (syn_layout), to scroll through when they outrun it.
 * The rows point into `b` and the port's doc, the caller's, and into this
 * function's own lines, which the next call replaces. */
int controls_page_rows(const ctl_game *g, int page, int width, ctl_buf *b,
                       const menu_row **out, int *loop_at)
{
	static menu_row rows[SYN_ROWS];
	static char lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
	static char text[2 * PD_TEXT + 4];
	static char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
	menu_row made[CTL_MAX_ROWS];
	int n = ctl_game_rows(g, page, made, b), i;

	*loop_at = 0;
	*out = rows;
	if (!ctl_game_prose(g, page)) {
		memcpy(rows, made, (size_t)n * sizeof *rows);
		/* the button column in the handheld's own glyphs (DESIGN.md 11.9) */
		ctl_glyph_rows(rows, n, keys);
		return n;
	}
	text[0] = '\0';
	for (i = 0; i < n; i++) {
		size_t at = strlen(text);

		snprintf(text + at, sizeof text - at, "%s%s", i ? "\n\n" : "", made[i].label);
	}
	return syn_layout(text, width, lines, rows, loop_at);
}

/* The panel of a game's Controls: the standard one, or wider where a page's
 * button and what it does do not fit side by side in it. Chinese has the
 * shelf menus' narrowest panel, and Atari 7800's START/SELECT beside
 * Pause/Select (in Latin letters in every language) slid in it, as the
 * host's fit check found once every system was a shelf in it (2026-11-08).
 * Measured across every page of the game, so turning a page does not resize
 * the panel, and never past the width menu_draw can make one. A note under
 * the rows is no part of it: a long one scrolls. */
int game_controls_width(app *a, const ctl_game *g)
{
	int w = menu_std_width(a), p, pages = ctl_game_pages(g);
	int most = menu_most_w();

	for (p = 0; p < pages; p++) {
		menu_row rows[CTL_MAX_ROWS], kept[CTL_MAX_ROWS];
		char keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
		ctl_buf b;
		int i, n = ctl_game_rows(g, p, rows, &b), k = 0, m;

		/* measured as drawn, the buttons as glyphs */
		if (!ctl_game_prose(g, p)) ctl_glyph_rows(rows, n, keys);
		for (i = 0; i < n; i++)
			if (!ROW_IS_NOTE(rows[i])) kept[k++] = rows[i];
		m = menu_measure(kept, k, NULL);
		if (m > w) w = m;
	}
	return w < most ? w : most;
}

/* The in-game menu's Controls (DESIGN.md 11.28): every control this game
 * has, a page at a time over the paused game, Left/Right to turn them, as
 * MENU > Controls is on the home. Read-only. The rows are made when a page
 * turns, never per frame and never while the game runs.
 *
 * Over `bg`, the port's menu's Controls row; or opened by the Controls tab's
 * Hotkeys row (game_controls_pages), over `over`'s backdrop with its power
 * rule, less the system's buttons, which the tab shows (ctl_game.no_buttons).
 * At their own width either way, with no handheld beside them. */
static void controls_pages(app *a, SDL_Texture *bg, const menu_style *over, void *ctx)
{
	ctl_game g = game_controls_of(a);
	const menu_row *rows;
	ctl_buf b;
	char count[16] = "";
	int page = 0, pages, n, lv, done = 0, width, loop_at;
	float k;

	g.no_buttons = over != NULL;
	ctl_set_chords(&plat_profile()->chords);
	{
		/* The hotkeys this game plays with (src/hkstore.h) */
		sysview *sv = &a->view[a->sys_cursor];
		hk_table hk;

		if (sv->list.count > 0 && sv->cursor >= 0 && sv->cursor < sv->list.count)
			hks_get(HKS_GAME, a->sys.systems[shelf_owner(a, a->sys_cursor, sv->cursor)].tag,
			        sv->list.items[sv->cursor].file, &hk);
		else
			hks_get(HKS_EVERY, NULL, NULL, &hk);
		ctl_set_hotkeys(&hk);
	}
	pages = ctl_game_pages(&g);
	width = game_controls_width(a, &g);
	n = controls_page_rows(&g, page, width, &b, &rows, &loop_at);
	mq_reset(MQ_VSCROLL);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();

	while (!done && !want_quit) {
		int to = page;

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (menu_leaving(a)) done = 1;
		if (in_repeat(&a->in, IN_RIGHT)) {
			to = (page + 1) % pages;
			step_sound(a, IN_RIGHT, page, to);
		}
		if (in_repeat(&a->in, IN_LEFT)) {
			to = (page + pages - 1) % pages;
			step_sound(a, IN_LEFT, page, to);
		}
		if (to != page) {
			page = to;
			n = controls_page_rows(&g, page, width, &b, &rows, &loop_at);
			/* a port's About scrolls from its top */
			mq_reset(MQ_VSCROLL);
		}
		/* which page of how many, at the heading's right (DESIGN.md 11.3) */
		if (pages > 1) snprintf(count, sizeof count, "%d / %d", page + 1, pages);
		levels_keys(&a->in, BOOST_HIDE);
		/* Power stops the game from here, as from the achievements */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over && over->on_power) {
				over->on_power(a, ctx);
			} else {
				plat_note_power_pressed();
				plat_resident_line("STOP");
			}
			done = 1;
		}

		if (!panel_frame(a, lv, &k)) continue;
		tick_theme(a);
		if (over && over->backdrop) over->backdrop(a, ctx, k);
		else paused_backdrop(a, bg, k);
		menu_draw_caption(a, b.head, rows, n, -1, width, a->tint, NULL,
		                  false, loop_at, count[0] ? count : NULL);
		plat_present();
		ui_frame_rest(8);
	}

	panel_close(a, lv, false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void game_controls_screen(app *a, SDL_Texture *bg)
{
	controls_pages(a, bg, NULL, NULL);
}

void game_controls_pages(app *a, const menu_style *over, void *ctx)
{
	controls_pages(a, NULL, over, ctx);
}

/* One width on every tab, the widest a panel is (menu_most_w: 864, the
 * panel MENU_MARGIN from the sides, 860 until 1.0.6), to make room for the
 * Controls tab's handheld, and so cycling a value or turning a tab
 * does not resize the panel under the cursor. A port's menu, which has no
 * tabs and no values to cycle, is as wide as its rows. */
int gm_width(app *a)
{
	(void)a;
	return g_port_menu ? 0 : menu_most_w();
}

/* The DS rows: the layout steps through the three, the swap and the stylus
 * turn over, each sent to the game at once. The layout is kept for the
 * system; the swap and the stylus belong to this game. Diatom answers with the
 * new rect and a new paused picture for it (plat_resident_sync_screens), so
 * the backdrop behind the menu is the layout just chosen. Returns whether it
 * heard back, which is when the picture is worth loading again. */
static bool gm_screens(app *a, gm_row id, int d)
{
	const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor,
	                                      a->view[a->sys_cursor].cursor)];
	int lay;
	bool sw;

	screens_get(s->tag, &lay, &sw);
	sw = plat_resident_swap();
	if (id == GM_SCREENS)   screens_put(s->tag, (lay + d + GM_LAYOUTS) % GM_LAYOUTS, sw);
	else if (id == GM_SWAP) screens_put(s->tag, lay, !sw);
	else if (id == GM_STYLUS) {
		bool on = !plat_resident_stylus();

		plat_resident_line("SETSTYLUS\ton=%d", on ? 1 : 0);
		plat_resident_note_stylus(on);
	}
	return plat_resident_sync_screens(300);
}

/* A row of any tab by identity: gm_row, ct_row, or an achievement
 * (AT_ID0 + i) - apart from -1, the kept row's "none" (g_gm_mem) */
#define AT_ID0 1000
#define GM_IDS_MAX (CHV_MAX > CT_MAX_ROWS ? CHV_MAX : CT_MAX_ROWS)

int shot_achtab = -1;      /* --achtab STATE: the tab's state in a shot */

/* What the Achievements tab needs to know about the game running
 * (src/achtab.h): its set, the account, the fetch behind it, its hash. A
 * shot says it with --achtab. */
void gm_at_facts(at_facts *f)
{
	memset(f, 0, sizeof *f);
	if (shot_path && shot_achtab >= 0) {
		f->signed_in = shot_achtab != AT_SIGNED_OUT;
		f->set = shot_achtab == AT_UNCHECKED ? CHV_UNCHECKED : CHV_NONE;
		f->fetching = shot_achtab == AT_WAITING;
		f->no_hash = shot_achtab == AT_NOT_SYSTEM ? RA_HASH_NO_SYSTEM
		             : shot_achtab == AT_NOT_FILE ? RA_HASH_NO_FILE : RA_HASH_OK;
		f->cheats_paused = shot_achtab == AT_PAUSED;
		if (shot_achtab == AT_LIST || shot_achtab == AT_STATES) {
			int i;

			/* AT_STATES stands for every one earned (--achtab all) */
			achtab_shot_earned(shot_achtab == AT_STATES ? 8 : 3);
			f->count = achtab_shot_count();
			for (i = 0; i < f->count; i++) {
				const cheevo *c = achtab_shot_at(i);

				f->points_total += c->points;
				if (c->earned) { f->earned++; f->points += c->points; }
			}
			f->pending = shot_cheevos_waiting;
		}
		return;
	}
	f->count = chv_count();
	f->earned = chv_earned();
	f->points = chv_points_earned();
	f->points_total = chv_points_total();
	f->pending = f->count > 0 ? chv_pending_for(chv_game()) : 0;
	f->signed_in = ra_signed_in();
	f->fetching = g_pending_set[0] && !strcmp(g_pending_set, g_game_set);
	f->set = chv_state_of(g_game_set, f->signed_in);
	f->no_hash = g_port_menu ? RA_HASH_OK : g_game_no_hash;
	f->cheats_paused = !g_port_menu && cheats_game_paused();
}

at_source gm_at_source(void)
{
	return shot_path && shot_achtab >= 0 ? achtab_shot_at : chv_at;
}

/* The game the Controls tab is about (src/ctltab_screen.h): the one on the
 * shelf's cursor, which is the one running, its own system (Favorites too),
 * its Pak, and Applies To kept with the menu's memory. `c` may be NULL (a
 * shot), with nothing to draw its pages over. */
void gm_ctl_game(app *a, gm_ctx *c, ctlscr_game *g)
{
	sysview *sv = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor, sv->cursor)];
	const game_entry *e = sv->list.count > 0 ? &sv->list.items[sv->cursor] : NULL;

	memset(g, 0, sizeof *g);
	g->s = s;
	g->file = e ? e->file : "";
	g->title = e ? e->title : "";
	g->pak = pak_system(s) && e ? gm_pak_name(pak_get(s, e->file)) : NULL;
	/* Rumble where the handheld has a motor and the game's core asked for
	 * it (a shot's --gm-rumble stands in for the asking) */
	g->rumble = plat_has_rumble() && e && (shot_path ? g_shot_rumble : plat_resident_rumble_asked());
	g->scope_all = &g_gm_mem.scope_all;
	/* A shot sends nothing: there is no game */
	g->running = !shot_path;
	g->over = c ? &c->st : NULL;
	g->ctx = c;
}

/* A tab's rows by identity, each tab's own kind: gm_row for Game and
 * Display, ct_row for Controls, option index for Core, AT_ID0 and a place
 * for Achievements; at most `max` (GM_IDS_MAX holds any tab's). The count. */
static int gm_ids(app *a, gm_ctx *c, const gm_ui *u, int tab, int *ids, int max)
{
	gm_row r[GM_MAX_ROWS];
	int n, i;

	if (!u->port && tab == GT_CONTROLS) {
		ctlscr_game g;
		menu_row rows[CT_MAX_ROWS];
		unsigned vcol[CT_MAX_ROWS];
		int ct[CT_MAX_ROWS];

		gm_ctl_game(a, c, &g);
		n = ctlscr_rows(a, &g, rows, vcol, NULL, ct);
		if (n > max) n = max;
		for (i = 0; i < n; i++) ids[i] = ct[i];
		return n;
	}
	/* The Achievements tab: an achievement's place, or the one row of a
	 * sentence, which the cursor cannot leave. A set can be CHV_MAX long,
	 * far more than any other tab's rows */
	if (!u->port && tab == GT_ACHIEVEMENTS) {
		at_facts f;

		gm_at_facts(&f);
		n = at_state_of(&f) == AT_LIST ? (f.count < CHV_MAX ? f.count : CHV_MAX) : 1;
		if (n > max) n = max;
		for (i = 0; i < n; i++) ids[i] = AT_ID0 + i;
		return n;
	}
	n = gm_tab_rows(u, (gm_tab)tab, r);
	if (n > max) n = max;
	for (i = 0; i < n; i++) ids[i] = (int)r[i];
	return n;
}


/* The in-game menu's option rows of more than two choices (menu.h's
 * menu_choice, DESIGN.md 9), as gm_key steps them, so A opens their list:
 * Display, Shader, Strength, the quick slot, the disc and a DS game's
 * Screens. The words live here until the next build. */
static menu_choice g_gm_choice[GM_MAX_ROWS];
#define GM_CH_MAX 32   /* an in-game row's most: Display's modes, the discs */

/* The paused frame the in-game menu is over, for the Display list's picture
 * (game_menu sets it; a shot too, gm_preview_frame) */
static SDL_Texture *g_gm_frame;

void gm_preview_frame(SDL_Texture *bg)
{
	g_gm_frame = bg;
}

/* --gm-source WxH[@ASPECT]: the core's picture a shot's frame stands for */
int    shot_gm_src_w, shot_gm_src_h;
double shot_gm_aspect;

/* The Display list's picture (menu_choice's preview): the paused game as
 * mode `k` would draw it, at the size and shape Diatom said */
/* The Shader and Strength lists' picture (src/shaderdemo.h), with the game's
 * own system's shader and strength (owner_view) as the lists were made */
static int g_gm_demo_shader, g_gm_demo_strength;
static const char *g_gm_demo_tag;

static void gm_shader_preview(void *app_, int k, int x, int y, int w, int h)
{
	shader_demo_draw(app_, shader_at(k, g_gm_demo_tag), g_gm_demo_strength, x, y, w, h);
}

static void gm_strength_preview(void *app_, int k, int x, int y, int w, int h)
{
	shader_demo_draw(app_, g_gm_demo_shader, STRENGTH_MIN + k * STRENGTH_STEP, x, y, w, h);
}

/* Border's list's picture: the paused frame where the mode puts it, each
 * kind the row offers around it (dmode_border_preview) */
static border_kind g_gm_border_kinds[BORDER_KINDS];

static void gm_border_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	sysview *v = owner_view(a);
	int sw, sh;
	double aspect;
	char path[CFG_STR * 3] = "";
	SDL_Surface *own = NULL;
	border_kind kind = k >= 0 && k < BORDER_KINDS ? g_gm_border_kinds[k] : BORDER_OFF;

	gm_source(&sw, &sh, &aspect);
	if (kind == BORDER_OWN) own = border_own_cached(&a->sys.systems[v - a->view], path, sizeof path);
	if (kind == BORDER_HANDHELD || kind == BORDER_WHOLE) {
		SDL_Rect at;

		own = border_frame_cached(a, (int)(v - a->view), kind, -1, path, sizeof path, &at);
		dmode_border_preview(a, g_gm_frame, sw, sh, aspect, v->dmode, (int)kind, own, path, own ? &at : NULL,
		                     x, y, w, h);
		return;
	}
	dmode_border_preview(a, g_gm_frame, sw, sh, aspect, v->dmode, (int)kind, own, path, NULL, x, y, w, h);
}

/* Handheld Color's list's picture: the frame in color k, whole where it is
 * offered (border_color_kind) */
static void gm_color_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	sysview *v = owner_view(a);
	int sys = (int)(v - a->view), sw, sh;
	double aspect;
	border_kind kind = border_color_kind(a, sys);
	char key[96];
	SDL_Surface *f;
	SDL_Rect at;

	gm_source(&sw, &sh, &aspect);
	f = border_frame_cached(a, sys, kind, k, key, sizeof key, &at);
	dmode_border_preview(a, g_gm_frame, sw, sh, aspect, v->dmode, (int)kind, f, key, f ? &at : NULL, x, y, w, h);
}

static void gm_dmode_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	int sw = 0, sh = 0, fw, fh;
	double aspect = 0.0;

	if (shot_path && shot_gm_src_w > 0) {
		sw = shot_gm_src_w;
		sh = shot_gm_src_h;
		aspect = shot_gm_aspect;
	} else if (!plat_resident_source(&sw, &sh, &aspect)) {
		/* A Diatom that does not say: the frame's own size, a console's
		 * shape */
		sw = sh = 0;
		aspect = 4.0 / 3.0;
	}
	dmode_surface(&fw, &fh);
	dmode_preview_draw(a, g_gm_frame, sw, sh, aspect, fw, fh, k, x, y, w, h);
}

const menu_choice *gm_choices(app *a, const gm_ui *u)
{
	static const char *lab[6][GM_CH_MAX];
	static char buf[3][GM_CH_MAX][160];
	sysview *v = owner_view(a);
	int i, n, cur;

	memset(g_gm_choice, 0, sizeof g_gm_choice);
	{
		static const char *notes[GM_CH_MAX];

		for (i = 0; i < DMODE_COUNT && i < GM_CH_MAX; i++) {
			lab[0][i] = dmode_label(i);
			notes[i] = dmode_note(i);
		}
		g_gm_choice[GM_DISPLAY] = (menu_choice){ .n = (unsigned char)DMODE_COUNT, .cur = (unsigned char)v->dmode,
		                                         .labels = lab[0], .notes = notes,
		                                         .preview = gm_dmode_preview };
	}
	{
		/* Border's offered kinds, each with its note and picture */
		static const char *labels[BORDER_KINDS], *notes[BORDER_KINDS];
		const bool room = gm_border_room(a);
		border_kind kept = border_load_room(a, (int)(v - a->view), room);

		n = border_offered(border_offer_room(a, (int)(v - a->view), room), g_gm_border_kinds);
		for (i = 0, cur = 0; i < n; i++) {
			labels[i] = T(border_label_id(g_gm_border_kinds[i]));
			notes[i] = T(border_note_id(g_gm_border_kinds[i]));
			if (g_gm_border_kinds[i] == kept) cur = i;
		}
		g_gm_choice[GM_BORDER] = (menu_choice){ .n = (unsigned char)n, .cur = (unsigned char)cur,
		                                        .labels = labels, .notes = notes,
		                                        .preview = gm_border_preview, .list = true };
	}
	{
		/* Handheld Color's colors, each with the frame in it */
		static const char *labels[HF_COLORS];
		const hf_model *m = hf_model_for(a->sys.systems[v - a->view].tag);

		if (m && m->ncolors > 1) {
			for (i = 0; i < m->ncolors && i < HF_COLORS; i++) labels[i] = border_color_label(m->color_name[i]);
			g_gm_choice[GM_BORDER_COLOR] = (menu_choice){
				.n = (unsigned char)m->ncolors, .cur = (unsigned char)border_color_load(a, (int)(v - a->view)),
				.labels = labels, .preview = gm_color_preview };
		}
	}
	g_gm_demo_tag = a->sys.systems[v - a->view].tag;
	for (i = 0, n = 0, cur = 0; i < SHADER_COUNT && n < GM_CH_MAX; i++) {
		if (SHADERS[i].gpu && !plat_gpu_shaders()) continue;
		if (!shader_for(i, g_gm_demo_tag)) continue;
		if (i == v->shader) cur = n;
		lab[1][n++] = shader_label(i);
	}
	{
		static const char *notes[GM_CH_MAX];

		shader_demo_notes(notes, n, g_gm_demo_tag);
		g_gm_demo_shader = v->shader;
		g_gm_demo_strength = strength_clamp(v->strength);
		g_gm_choice[GM_SHADER] = (menu_choice){ .n = (unsigned char)n, .cur = (unsigned char)cur, .labels = lab[1],
		                                        .notes = notes, .preview = gm_shader_preview };
	}
	for (i = STRENGTH_MIN, n = 0, cur = 0; i <= STRENGTH_MAX && n < GM_CH_MAX; i += STRENGTH_STEP, n++) {
		if (i == strength_clamp(v->strength)) cur = n;
		snprintf(buf[0][n], sizeof buf[0][n], "%d%%", i);
		lab[2][n] = buf[0][n];
	}
	{
		static const char *notes[GM_CH_MAX];

		shader_demo_strength_notes(notes, n);
		g_gm_choice[GM_STRENGTH] = (menu_choice){ .n = (unsigned char)n, .cur = (unsigned char)cur, .labels = lab[2],
		                                          .notes = notes, .preview = gm_strength_preview };
	}
	for (i = 0; i < GM_SLOTS; i++) {
		snprintf(buf[1][i], sizeof buf[1][i], "%d", i + 1);
		lab[3][i] = buf[1][i];
	}
	if (u->quick >= 1 && u->quick <= GM_SLOTS)
		g_gm_choice[GM_QUICK] = (menu_choice){ .n = GM_SLOTS, .cur = (unsigned char)(u->quick - 1), .labels = lab[3] };
	/* The discs Diatom says there are, as many as the list holds: a count
	 * or an index past what was filled gives no table, and the row keeps
	 * Left and Right */
	n = u->discs < GM_CH_MAX ? u->discs : GM_CH_MAX;
	for (i = 0; i < n; i++) {
		if (i == u->disc) gm_disc_value(u, buf[2][i], sizeof buf[2][i], u->disc_fits);
		else snprintf(buf[2][i], sizeof buf[2][i], T(S_GAMEMENU_DISC_VALUE), i + 1, u->discs);
		lab[4][i] = buf[2][i];
	}
	if (n > 1 && u->discs == n && u->disc >= 0 && u->disc < n)
		g_gm_choice[GM_DISC] = (menu_choice){ .n = (unsigned char)n, .cur = (unsigned char)u->disc,
		                                      .labels = lab[4] };
	for (i = 0; i < GM_LAYOUTS; i++) lab[5][i] = gm_layout_label(i);
	if (u->screens && u->layout >= 0 && u->layout < GM_LAYOUTS)
		g_gm_choice[GM_SCREENS] = (menu_choice){ .n = GM_LAYOUTS, .cur = (unsigned char)u->layout, .labels = lab[5] };
	return g_gm_choice;
}

static int gm_menu_build(void *ctx, menu_row *rows, int max,
                         const char **heading)
{
	gm_ctx *c = ctx;
	gm_ui u;
	int n;

	gm_gather(c->a, &u);
	/* A game's menu names its tab in the heading's place (menu_draw draws the
	 * tab row there, which MENU's does the same); a port's has no title: the
	 * game behind it is the title */
	*heading = u.port ? NULL : gm_tab_name(c->tabs.at);
	/* The Controls tab's rows are src/ctltab.c's, beside the handheld, a
	 * changed value in accent-ink; the other tabs' gm_rows', in the whole
	 * width */
	if (!u.port && c->tabs.at == GT_CONTROLS) {
		ctlscr_game g;

		gm_ctl_game(c->a, c, &g);
		n = ctlscr_rows(c->a, &g, rows, c->ct_vcol, c->ct_sound, NULL);
		c->st.sound = c->ct_sound;
		c->st.choices = NULL;   /* a button opens its own list (ctlscr_key) */
		c->st.vcolors = c->ct_vcol;
		c->st.side_w = CTLSCR_SIDE_W;
		c->st.header = NULL;
		c->st.header_h = 0;
		c->st.row_icon = NULL;
		c->st.visits_all = false;
		c->st.sel_out = &c->seen;
		return n;
	}
	/* The Achievements tab's: the game's list, or why it has none
	 * (src/achtab_screen.h), which sets the style its rows want */
	if (!u.port && c->tabs.at == GT_ACHIEVEMENTS) {
		at_facts f;

		gm_at_facts(&f);
		c->st.choices = NULL;
		c->st.sel_out = &c->seen;
		return achtab_rows(c->a, &f, gm_at_source(), &c->ach, rows, max,
		                   c->st.fixed_w, &c->st);
	}
	c->st.header = NULL;
	c->st.header_h = 0;
	c->st.row_icon = NULL;
	c->st.visits_all = false;
	c->st.sound = c->bufs.sound;
	c->st.choices = c->bufs.choice;
	c->st.vcolors = NULL;
	c->st.side_w = 0;
	c->st.sel_out = &c->seen;
	gm_choices(c->a, &u);
	u.choice = g_gm_choice;
	n = gm_rows(&u, (gm_tab)c->tabs.at, rows, &c->bufs);
	/* A tab with no rows: one empty note, with no cursor on it, because the
	 * runner leaves a menu that has none */
	if (n == 0) {
		rows[0] = MENU_NOTE("");
		c->bufs.sound[0] = 0;
		n = 1;
	}
	return n;
}

/* The paused game, not the shelf. Diatom reports its rect with every DISPLAY
 * message, so the preview lands where the game actually is. */
void gm_backdrop(app *a, void *ctx, float k)
{
	gm_ctx *c = ctx;

	/* the corner with it (paused_backdrop) */
	paused_backdrop(a, c->bg, k);
}

/* Power here stops the GAME. The device is not going anywhere, and the wait
 * loop under this menu has to be told or it goes straight back to waiting on
 * a game nobody is running. */
static menu_result gm_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

/* Quit, the in-game menu's row and px2-open's quit (ctl_poll): the wait loop
 * carries on until EXIT arrives; quitting is asking, not tearing down. */
void gm_quit(void)
{
	plat_resident_line("STOP");
}

static menu_result gm_key(app *a, void *ctx, in_button key, int sel)
{
	gm_ctx *c = ctx;

	/* A row's place to its id: the tab on says which rows there are, and the
	 * same list the build used says which is which. */
	{
		gm_ui u;
		int ids[GM_IDS_MAX];
		int n;

		gm_gather(a, &u);
		n = gm_ids(a, c, &u, c->tabs.at, ids, GM_IDS_MAX);
		/* The row this tab is on is kept, for MENU's next press */
		if (!u.port && sel >= 0 && sel < n) g_gm_mem.row[c->tabs.at] = ids[sel];
		/* L1 and R1 go round the tabs, as MENU's do: the row each tab was
		 * on is kept and the cursor lands on the next tab's own */
		if (!u.port && (key == IN_L1 || key == IN_R1)) {
			int k;

			tabs_turn(&c->tabs, key == IN_R1 ? +1 : -1);
			g_gm_mem.tab = c->tabs.at;
			if (c->tabs.at == GT_CONTROLS) ctlscr_enter();
			n = gm_ids(a, c, &u, c->tabs.at, ids, GM_IDS_MAX);
			for (k = 0; k < n && ids[k] != g_gm_mem.row[c->tabs.at]; k++) ;
			g_menu_sel_to = k < n ? k : 0;
			moved_sound(a, key);
			redraw_now();
			return MENU_STAY;
		}
		/* The Achievements tab: A on an achievement opens it, as the list
		 * over the shelf does; nothing else but X, which quits from any tab */
		if (!u.port && c->tabs.at == GT_ACHIEVEMENTS && key != IN_X) {
			const cheevo *ch = key == IN_ACCEPT ? achtab_pick(&c->ach, gm_at_source(), sel)
			                                    : NULL;

			if (ch) cheevo_detail_screen(a, c->bg, false, ch);
			return plat_run_power_pressed() ? MENU_DONE : MENU_STAY;
		}
		/* The Controls tab's rows, but X, which quits from any tab, and the
		 * Pak, below as before */
		if (!u.port && c->tabs.at == GT_CONTROLS && key != IN_X) {
			ctlscr_game g;
			int id = sel >= 0 && sel < n ? ids[sel] : -1;

			if (id == CT_PAK) {
				sel = GM_PAK;
			} else {
				gm_ctl_game(a, c, &g);
				ctlscr_key(a, &g, key, id);
				/* A page it opened took the power press: the game stops */
				return plat_run_power_pressed() ? MENU_DONE : MENU_STAY;
			}
		} else {
			sel = sel >= 0 && sel < n ? ids[sel] : GM_MAX_ROWS;
		}
	}
	/* X, wherever the cursor is: what A on its row does (gm_x_row), heard
	 * as A there is, since the runner plays accept for A alone */
	if (key == IN_X) {
		uisound_play(DS_SOUND_ACCEPT);
		sel = gm_x_row();
		key = IN_ACCEPT;
	}
	if ((sel == GM_SCREENS || sel == GM_SWAP || sel == GM_STYLUS) &&
	    (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		if (gm_screens(a, (gm_row)sel, key == IN_LEFT ? -1 : 1)) {
			const char *pv = plat_resident_last_preview();
			SDL_Surface *sf = pv && *pv ? IMG_Load(pv) : NULL;

			if (sf) {
				if (c->bg) SDL_DestroyTexture(c->bg);
				c->bg = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
		}
		return MENU_STAY;
	}

	/* Applied to the running game at once, not on resume: the whole point of
	 * this row being here rather than on the shelf is judging the mode against
	 * the game it is being applied to. Diatom takes SETDISPLAY while paused,
	 * answers with the new rect, and the backdrop behind this menu redraws
	 * into it. */
	if (sel == GM_DISPLAY && (key == IN_LEFT || key == IN_RIGHT)) {
		gm_cycle_display(a, key == IN_RIGHT ? 1 : -1);
		return MENU_STAY;
	}
	/* Handheld Color the same way, the frame sent again in its color */
	if (sel == GM_BORDER_COLOR && (key == IN_LEFT || key == IN_RIGHT)) {
		int sys = (int)(owner_view(a) - a->view);

		border_color_step(a, sys, key == IN_LEFT ? -1 : 1);
		border_send(a, sys);
		return MENU_STAY;
	}
	/* Border, on left and right, sent to the game the same way; A opens its
	 * list (its picture beside each), which comes back as steps */
	if (sel == GM_BORDER && (key == IN_LEFT || key == IN_RIGHT)) {
		gm_step_border(a, key == IN_LEFT ? -1 : 1);
		return MENU_STAY;
	}
	/* The shader, on left, right and A, sent to the game the same way */
	if (sel == GM_SHADER && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		gm_cycle_shader(a, key == IN_LEFT ? -1 : 1);
		return MENU_STAY;
	}
	/* On-Screen Stats: left and right turn it over, kept for the player and
	 * sent to the game at once (Diatom takes SETHUD while paused, and the
	 * line is over the game when it comes back); A opens its page, what it
	 * shows and how (src/hudmenu_screen.h) */
	if (sel == GM_HUD && (key == IN_LEFT || key == IN_RIGHT)) {
		hm_state s;

		hm_load(&s);
		s.on = !s.on;
		db_set_int(db_dev(), HUD_KEY, s.on ? 1 : 0);
		hm_send(&s);
		return MENU_STAY;
	}
	if (sel == GM_HUD && key == IN_ACCEPT) {
		hud_page(a, c->bg);
		return MENU_STAY;
	}
	/* Its strength, on left and right alone */
	if (sel == GM_STRENGTH && (key == IN_LEFT || key == IN_RIGHT)) {
		gm_step_strength(a, key == IN_LEFT ? -1 : 1);
		return MENU_STAY;
	}
	/* The quick slot steps round 1 to 6 on left, right and A, kept for the
	 * game's system at once: SELECT+R1 and SELECT+L1 use it from the next
	 * press, in this game and every other on the system */
	/* The Pak turns between the two on left, right and A, kept for this game
	 * and put in the controller at once: Diatom takes SETOPT while paused,
	 * and the core swaps the pak on its next frame */
	if (sel == GM_PAK && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		sysview *sv = &a->view[a->sys_cursor];
		const system_cfg *q = &a->sys.systems[shelf_owner(a, a->sys_cursor, sv->cursor)];

		if (sv->list.count > 0) {
			const char *file = sv->list.items[sv->cursor].file;
			const char *to = gm_pak_other(pak_get(q, file));

			pak_put(q->tag, file, to);
			if (gm_pak_option(q->core))
				plat_resident_line("SETOPT\tkey=%s\tvalue=%s", gm_pak_option(q->core), to);
			ctlscr_changed();
		}
		return MENU_STAY;
	}
	/* The disc steps round on left, right and A, at once: the game sees the
	 * tray open, a new disc and the tray close (gm_disc_step_to) */
	if (sel == GM_DISC && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		gm_disc_step_to(key == IN_LEFT ? -1 : 1);
		return MENU_STAY;
	}
	if (sel == GM_QUICK && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		const system_cfg *q = &a->sys.systems[shelf_owner(a, a->sys_cursor,
		                                      a->view[a->sys_cursor].cursor)];
		int d = key == IN_LEFT ? -1 : 1;

		quick_slot_put(q->tag, gm_quick_step(quick_slot_get(q->tag), d));
		return MENU_STAY;
	}
	if (key != IN_ACCEPT) return MENU_STAY;

	switch ((gm_row)sel) {
	case GM_CONTINUE:
		c->resume = true;
		return MENU_DONE;
	case GM_SAVE:
	case GM_LOAD: {
		int slot = slot_strip(a, c->bg, sel == GM_SAVE);

		if (slot) {
			sysview *sv = &a->view[a->sys_cursor];
			int o = shelf_owner(a, a->sys_cursor, sv->cursor);   /* see slot_strip */
			char sp[LIB_PATH * 2], pp[LIB_PATH * 2];

			slot_state_path(a, o, &sv->list.items[sv->cursor],
			                slot, sp, sizeof sp);
			if (sel == GM_SAVE) {
				plat_resident_line("SAVE\tpath=%s", sp);
				/* The paused frame is what the save holds, and Diatom already
				 * wrote it as the pause preview: copy it beside the state so
				 * the strip can show what is inside the slot. */
				slot_preview_path(a, o, &sv->list.items[sv->cursor],
				                  slot, pp, sizeof pp);
				copy_file(plat_resident_last_preview(), pp);
			} else {
				char up[LIB_PATH * 2];

				/* The game as it is, for Undo Load, first: Diatom writes
				 * it, then loads the slot over it */
				if (undo_path(a, up, sizeof up))
					plat_resident_line("SAVE\tpath=%s", up);
				plat_resident_line("LOAD\tpath=%s", sp);
			}
			c->resume = true;
			return MENU_DONE;
		}
		/* Backed out: fall through to the menu, still paused. */
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		break;
	}
	/* A on Display opens its list (the runner's menu_choose, DESIGN.md
	 * 11.3) and never reaches here from the menu; a forward step stays for
	 * any caller that sends A itself. */
	case GM_DISPLAY:
		gm_cycle_display(a, +1);
		break;
	case GM_CONTROLS:
		game_controls_screen(a, c->bg);
		break;
	case GM_COREOPTS: {
		sysview *sv = &a->view[a->sys_cursor];
		const system_cfg *q = &a->sys.systems[shelf_owner(a, a->sys_cursor, sv->cursor)];

		/* Same page as the shelf's Core Settings, live over the paused game */
		coreopts_page(a, q->tag, q->core, c->bg, true);
		break;
	}
	case GM_PLAYER2:
		player2_page(a, c->bg);
		break;
	/* The game's cheats, each on or off at once (DESIGN.md 11.66) */
	case GM_CHEATS:
		cheats_page(a, c->bg);
		break;
	/* The Achievements tab, one L1 away: the row turns to it, the list
		 * or the sentence that says why there is none (1.0.5). It opened the
		 * list as a screen of its own before the tab. */
	case GM_CHEEVOS: {
		gm_ui u;
		int ids[GM_IDS_MAX], n, k;

		gm_gather(a, &u);
		tabs_turn(&c->tabs, -1);
		g_gm_mem.tab = c->tabs.at;
		n = gm_ids(a, c, &u, c->tabs.at, ids, GM_IDS_MAX);
		for (k = 0; k < n && ids[k] != g_gm_mem.row[c->tabs.at]; k++) ;
		g_menu_sel_to = k < n ? k : 0;
		redraw_now();
		return MENU_STAY;
	}
	/* Back to the game as it was before the last load. Kept: the row stays,
	 * and goes back to the same moment until the next load writes over it */
	case GM_UNDO: {

		char up[LIB_PATH * 2];

		if (!undo_path(a, up, sizeof up) || access(up, R_OK) != 0) break;
		plat_resident_line("LOAD\tpath=%s", up);
		c->resume = true;
		return MENU_DONE;
	}
	case GM_RESET:
		plat_resident_line("RESET");
		c->resume = true;
		return MENU_DONE;
	case GM_QUIT:
		gm_quit();
		return MENU_DONE;
	default: break;
	}

	/* A nested screen may have been the one that took the power press or ran
	 * the clock out - the slot strip and the achievements list both stop the
	 * game and close themselves, which would leave this menu drawn over a game
	 * that had already been told to stop. Only those two set the flag, and
	 * both are reached from right here, so this is where it is asked. */
	if (plat_run_power_pressed()) return MENU_DONE;
	return MENU_STAY;
}

static void gm_resume(app *a);

/* The next in-game menu was asked for by the launcher, not by a press: the
 * PAUSE a game resumed from a shutdown is sent with (launch). */
bool g_menu_unasked;

void game_menu(app *a)
{
	gm_ctx c = { 0 };
	const char *pv = plat_resident_last_preview();

	/* The menu opening over the paused game: MENU was pressed in it - unless
	 * the launcher asked for it, at a resume from a shutdown. */
	if (!g_menu_unasked) uisound_play(DS_SOUND_ACCEPT);
	g_menu_unasked = false;
	c.a = a;
	if (pv && *pv) {
		SDL_Surface *sf = IMG_Load(pv);

		if (sf) {
			c.bg = SDL_CreateTextureFromSurface(a->r, sf);
			SDL_FreeSurface(sf);
		}
	}
	gm_preview_frame(c.bg);
	/* The shape this game's picture has, kept for its system: the shelf's
	 * Display list draws its games with it */
	dmode_note_shape(a->sys.systems[shelf_owner(a, a->sys_cursor, a->view[a->sys_cursor].cursor)].tag);
	/* Measured across every mode label, so cycling Display does not resize
	 * the panel underneath the cursor. */
	c.st.fixed_w     = gm_width(a);
	c.st.sound       = c.bufs.sound;
	c.st.follow_tint = true;
	c.st.backdrop    = gm_backdrop;
	c.st.on_power    = gm_power;
	gm_disc_ask();
	/* MENU's tab row, opened where this game's menu was last closed */
	c.tabs = (menu_tabs){ .count = GT_TABS, .name = gm_tab_name_i };
	if (g_gm_mem.tab < 0 || g_gm_mem.tab >= GT_TABS) g_gm_mem.tab = GT_GAME;
	c.tabs.at = c.tabs.from = g_gm_mem.tab;
	if (!g_port_menu) {
		gm_ui u;
		int ids[GM_IDS_MAX];
		int n, k;

		c.st.tabs = &c.tabs;
		c.st.sel_out = &c.seen;
		/* The handheld beside the Controls tab's rows, and as it leaves */
		c.st.side = ctlscr_side;
		ctlscr_open();
		gm_gather(a, &u);
		n = gm_ids(a, &c, &u, c.tabs.at, ids, GM_IDS_MAX);
		for (k = 0; k < n && ids[k] != g_gm_mem.row[c.tabs.at]; k++) ;
		c.st.start_sel = k < n ? k : 0;
		g_menu_epoch++;
	}

	/* B and MENU are the runner's, and both mean Continue here. The runner has
	 * to be the one to say so: it flushes the input on its way out, so asking
	 * a->in afterwards would always say no button was pressed. MENU from a
	 * screen this menu opened - the save slots, Achievements - comes back as
	 * MENU_LEFT_BACK too, so it is Continue from there as well. */
	g_menu_closing = false;
	g_in_game_menu = true;
	if (menu_run(a, &c.st, gm_menu_build, gm_key, &c) == MENU_LEFT_BACK)
		c.resume = true;
	g_in_game_menu = false;
	g_menu_closing = false;
	/* Where it was closed, for the next MENU press in this game: the cursor
	 * moves without asking gm_key, so it is read from the runner here */
	if (c.st.tabs) {
		gm_ui u;
		int ids[GM_IDS_MAX];
		int n;

		gm_gather(a, &u);
		n = gm_ids(a, &c, &u, c.tabs.at, ids, GM_IDS_MAX);
		g_gm_mem.tab = c.tabs.at;
		if (c.seen >= 0 && c.seen < n) g_gm_mem.row[c.tabs.at] = ids[c.seen];
	}
	/* Closed by px2-open's quit, which said STOP as Quit does: no Continue */
	if (g_ctl_close_menu) c.resume = false;
	g_ctl_close_menu = false;

	/* Its close, over the paused game, while the display is still this
	 * process's: before the pages are handed back below, and never after
	 * RESUME, when Diatom is drawing. Ending the game instead leaves nothing
	 * to see go, and nothing for the shelf to draw later. */
	if (c.resume) {
		leave_end how = panel_leave_run(a, gm_backdrop, &c);

		/* Power or Auto Off as it closed: what they mean in the menu */
		if (how == LEAVE_POWER) gm_power(a, &c);
		if (how != LEAVE_GONE) c.resume = false;
	}
	mm_leaving_cut();
	/* The handheld's kept texture, which the game has no use for, and the
	 * Achievements tab's badges */
	ctlscr_close();
	achtab_close();

	gm_preview_frame(NULL);
	if (c.bg) SDL_DestroyTexture(c.bg);

	/* Asked to quit with the menu open. Leaving here without saying anything
	 * strands the game: it is paused, waiting on this socket, and the wait
	 * loop below would go straight back to waiting on IT. Each on the other,
	 * and neither reachable by a signal. Seen on the device 2026-08-29.
	 *
	 * STOP rather than RESUME - the process is going away, and a game that
	 * ends writes its state on the way out. */
	if ((want_quit || !a->running) && !c.resume) plat_resident_line("STOP");

	if (c.resume) gm_resume(a);
	/* Nothing presents from here: the next frame on screen is the game's. */
}

/* Back to the game from a panel over it (the menu, Reset Game's question):
 * the pages handed back, the music, the mute and the level said, RESUME */
static void gm_resume(app *a)
{
	{
		/* Hand the pages back the way Diatom expects to find them.
		 *
		 * Diatom does not repaint the area outside its picture every frame -
		 * its pages start opaque black and it writes only the rect - which is
		 * sound while it owns the framebuffer and false the moment this
		 * process has drawn a full-screen menu into the same pages. At any
		 * display mode that does not fill the panel, resuming showed the game
		 * correctly sized with this menu still surrounding it, and flickering:
		 * Diatom cycles three pages and only the two this process presents
		 * into had been dirtied, so the border alternated menu, menu, black at
		 * the refresh rate.
		 *
		 * Twice because this process alternates two pages and one present only
		 * clears the one it lands on. Before RESUME and never after:
		 * afterwards Diatom is drawing, and this would be a second presenter. */
		present_clear(a, DIATOM_PAGE);
		present_clear(a, DIATOM_PAGE);
		/* SELECT in this menu opens MP4, so the music may have started or
		 * stopped while the game was paused. Said before the game moves, not
		 * a tick after it. */
		plat_resident_quiet(mp4c_playing());
		/* And the volume and the mute switch, both of which this menu had.
		 *
		 * On RESUME Diatom forgets its volume and reads it back from the
		 * codec. With the switch down in this menu that read finds the gain
		 * this side wrote for the cut and takes it for level 0, so putting
		 * the switch back up in the game brought the sound back at 0: silent
		 * until a volume key. Sending the level after RESUME, as every RUN
		 * does, leaves Diatom nothing to guess. The mute goes first, or the
		 * level would turn the sound back on with the switch still down. */
		plat_resident_line("SETMUTE\ton=%d", plat_muted() ? 1 : 0);
		/* Continue's back is the last interface sound until the menu
		 * opens again: the output is let go the moment it ends. */
		uisound_release();
		plat_resident_line("RESUME");
		/* Each with its boost past the top (src/levels.h), which this menu's
		 * keys may have filled or folded: the brightness's opens here too,
		 * and Diatom would go on with the one it had before the menu. */
		{
			int vol = plat_volume_get(), bri = plat_brightness_get();

			if (vol >= 0)
				plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d\tboost=%d",
				                   vol, PLAT_VOL_MAX + 1, levels_boost(OSDBAR_VOLUME));
			if (bri >= 0)
				plat_resident_line("SETLEVEL\tkind=brightness\tindex=%d\tcount=%d\tboost=%d",
				                   bri, PLAT_BRIGHT_MAX + 1, levels_boost(OSDBAR_BRIGHT));
		}
	}
}

/* Reset Game's hotkey (src/hotkeys.h): Diatom paused the game for it
 * (PAUSED for=reset), and this asks, as Delete Game does, over the paused
 * frame, the cursor on Cancel *(my pick in the Controls lab, 2026-11-19: a
 * reset loses play, so one press is not enough)*. Reset resets and goes on;
 * Cancel, or B, goes back to the game where it was. */
void game_reset_ask(app *a)
{
	gm_ctx c = { 0 };
	const char *pv = plat_resident_last_preview();
	bool yes;

	c.a = a;
	if (pv && *pv) {
		SDL_Surface *sf = IMG_Load(pv);

		if (sf) {
			c.bg = SDL_CreateTextureFromSurface(a->r, sf);
			SDL_FreeSurface(sf);
		}
	}
	gm_preview_frame(c.bg);
	c.st.follow_tint = true;
	c.st.backdrop = gm_backdrop;
	c.st.on_power = gm_power;
	uisound_play(DS_SOUND_ACCEPT);
	yes = ask_panel(a, &c.st, &c, T(S_HOTKEYS_RESET_ASK), T(S_HOTKEYS_RESET_LOST), T(S_GAMEMENU_RESET));
	gm_preview_frame(NULL);
	if (c.bg) SDL_DestroyTexture(c.bg);
	/* POWER while it asked stopped the game (gm_power); quitting the
	 * launcher stops it as the menu does */
	if (plat_run_power_pressed()) return;
	if (want_quit || !a->running) {
		plat_resident_line("STOP");
		return;
	}
	if (yes) plat_resident_line("RESET");
	gm_resume(a);
}

/* Next Shader and Next Display, the hotkeys (src/hotkeys.h), while the game
 * runs: the system's own row stepped on, kept and sent as the menu's rows
 * do; the name now on into `out`, for the pop-up */
void gm_hotkey_step(app *a, bool shader, char *out, size_t n)
{
	sysview *v = owner_view(a);

	if (shader) {
		v->shader = shader_step(v->shader, +1, a->sys.systems[v - a->view].tag);
		shader_save(a, (int)(v - a->view));
		plat_resident_line("SETDISPLAY\tfilter=%s", SHADERS[v->shader].name);
		snprintf(out, n, "%s", T(SHADERS[v->shader].text));
	} else {
		v->dmode = (v->dmode + 1) % DMODE_COUNT;
		display_save(a);
		plat_resident_line("SETDISPLAY\tmode=%s", DMODES[v->dmode].name);
		snprintf(out, n, "%s", T(DMODES[v->dmode].text));
	}
}

/* The in-game menu over a port (src/portmenu.h): this process started by
 * portkeys for it alone, with the port stopped and its last frame saved. The
 * same menu as over a Diatom game, over that frame, with the rows a stopped
 * program can answer to (gm_tab_rows): Continue, Controls - the port's system's
 * page and In a game's - and Quit. Its exit status says which: Continue, and
 * B or MENU, go back to the game; Quit ends it and the shelf comes back;
 * POWER, here or on the Controls page, or the battery run flat, end it and
 * turn the handheld off. The opening and the closing are the in-game menu's
 * own, so the close lands on the frame the game goes on from. */
int port_menu(app *a)
{
	gm_ctx c = { 0 };

	uisound_play(DS_SOUND_ACCEPT);
	c.a = a;
	c.bg = plat_load_screen(PORT_MENU_STILL);
	c.st.fixed_w     = gm_width(a);
	c.st.sound       = c.bufs.sound;
	c.st.follow_tint = true;
	c.st.backdrop    = gm_backdrop;
	c.st.on_power    = gm_power;

	g_menu_closing = false;
	g_in_game_menu = true;
	if (menu_run(a, &c.st, gm_menu_build, gm_key, &c) == MENU_LEFT_BACK)
		c.resume = true;
	g_in_game_menu = false;
	g_menu_closing = false;
	if (c.resume) {
		leave_end how = panel_leave_run(a, gm_backdrop, &c);

		if (how == LEAVE_POWER) gm_power(a, &c);
		if (how == LEAVE_QUIT) c.resume = true;      /* asked to stop: the game goes on */
	}
	mm_leaving_cut();
	if (c.bg) SDL_DestroyTexture(c.bg);
	if (plat_run_power_pressed()) return PORT_MENU_POWER;
	/* A TERM to this process is not the player's Quit: the game goes on, or
	 * ends with the system that sent it */
	return c.resume || want_quit ? PORT_MENU_CONTINUE : PORT_MENU_QUIT;
}

void gm_rows_report(app *a)
{
	menu_row rows[GM_MAX_ROWS];
	gm_bufs b;
	gm_ui u;
	int n;

	gm_gather(a, &u);
	u.choice = gm_choices(a, &u);
	n = gm_rows(&u, GT_GAME, rows, &b);
	menu_rows_print("ingame-game", rows, n, b.sound, b.choice);
	n = gm_rows(&u, GT_DISPLAY, rows, &b);
	menu_rows_print("ingame-display", rows, n, b.sound, b.choice);
}
