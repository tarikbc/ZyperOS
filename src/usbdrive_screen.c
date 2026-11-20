/* SPDX-License-Identifier: GPL-3.0-only */
#include "usbdrive_screen.h"

#include "aout_route.h"
#include "app.h"
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/page.h"
#include "look.h"
#include "osdbar.h"
#include "panel.h"
#include "phonelink.h"
#include "place.h"
#include "uisound.h"
#include "usblink.h"
#include "usblog.h"
#include "zyperdraw.h"
#include "zypermark.h"

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

/* A computer on the cable, asked once per plug over the shelf (usblink.h):
 * Charge Only, File Transfer, or on a development image Developer (SSH), as
 * three tiles side by side (DESIGN.md 11.18; my pick in the USB lab,
 * 2026-11-05), starting on Charge Only, his pick too, so a quick A keeps it
 * charging. Left/Right move, A chooses; B, MENU or the cable coming out
 * keep charging only. */
/* The tiles of that question: two, and on a development image a third */
/* What a tile of Computer Connected does */
enum { UA_CHARGE, UA_XFER, UA_DEV, UA_DRIVE };

/* USB Drive is offered by an image that has usbdrive (plastron), whose
 * kernel exports the card (f_mass_storage); --usb-ask's shots say so */
bool shot_usb_drive;
bool usb_drive_offered(void)
{
	return shot_usb_drive || plat_system_has("usbdrive");
}

int usb_ask_tiles(menu_row tiles[4], int what[4], int marks[4], bool dev)
{
	int n = 0;

	what[n] = UA_CHARGE; marks[n] = UI_GLYPH_CHARGE;
	tiles[n++] = (menu_row){ T(S_XFER_TILE_CHARGE_ONLY), NULL, true };
	what[n] = UA_XFER; marks[n] = UI_GLYPH_TRANSFER;
	tiles[n++] = (menu_row){ T(S_XFER_TILE_FILE_TRANSFER), NULL, true };
	/* Two to a row (tiles_panel's grid): Charge Only and File Transfer,
	 * then Flash Drive and, on a development image, Developer (SSH) */
	if (usb_drive_offered()) {
		what[n] = UA_DRIVE; marks[n] = UI_GLYPH_USB;
		tiles[n++] = (menu_row){ T(S_XFER_TILE_USB_DRIVE), NULL, true };
	}
	if (dev) {
		what[n] = UA_DEV; marks[n] = UI_GLYPH_TERMINAL;
		tiles[n++] = (menu_row){ T(S_XFER_TILE_DEVELOPER), NULL, true };
	}
	return n;
}

static void usb_drive(app *a);

/* ---------- USB Drive (plastron's usbdrive, DESIGN.md 11.18) -----------
 *
 * Computer Connected > USB Drive: the card as a USB drive, which any
 * computer opens with no driver (a Windows 10 player met File Transfer's
 * "no driver" hint, 2026-11-11). The card is where ZyperOS itself lives, so
 * ZyperOS leaves: it draws the pictures the image's usbdrive shows (it has
 * no fonts, no words and no theme of its own), in /tmp, asks launch.sh for
 * the mode and exits; usbdrive frees and unmounts the card, exports it until
 * the computer ejects it or the cable comes out, mounts it again and starts
 * ZyperOS again (plastron's package/usbdrive). */
#define USB_DRIVE_DIR "/tmp/zyperos_usbdrive.d"
#define USB_DRIVE_ASK "/tmp/zyperos_usbdrive"
/* The pages' geometry, in the launcher's 1024 x 768. The saved pictures are
 * 640 x 480 (5/8 of it), so every edge a slot or a glyph's cell starts on is
 * a multiple of 8 and lands on a whole pixel there. */
#define UD_TILE_Y    DS_PAGE_BODY_Y                             /* 256: the page frame's tiles' top (src/ds/page.h) */
#define UD_TILE_H    DS_PAGE_TILE_H                             /* 280 */
#define UD_MARK      64          /* the mark's slot, square */
#define UD_MARK_CY   312
#define UD_SPEED_CY  416
#define UD_SPEED_H   80          /* the speed's slot: the title face's line */
#define UD_TOTAL_CY  488
#define UD_TOTAL_H   48          /* the total's: the meta face's */
/* The saved pictures: 0.625 of the canvas both ways, 640 x 480 on a 4:3 one
 * (the device's plat_save_screen_hidden makes them so; usbdrive takes a
 * picture's width from its file and the atlas's from its size line) */
#define UD_SAVE_W    (ZYPEROS_SCREEN_W * 5 / 8)
#define UD_SAVE_H    480

/* The tokens of a number, as plastron's usbdrive makes them (its
 * drivestat.h): each digit, the mark, the space, a unit, "/s" */
enum { UT_DEC = 10, UT_SP, UT_B, UT_KB, UT_MB, UT_GB, UT_PS, UT_TOKENS };

/* A token's words in the language that is on */
static const char *ud_token(int t, char *buf)
{
	if (t >= 0 && t <= 9) { buf[0] = (char)('0' + t); buf[1] = '\0'; return buf; }
	switch (t) {
	case UT_DEC: return T(S_USBDRIVE_DECIMAL);
	case UT_SP:  return " ";
	case UT_B:   return T(S_USBDRIVE_UNIT_B);
	case UT_KB:  return T(S_USBDRIVE_UNIT_KB);
	case UT_MB:  return T(S_USBDRIVE_UNIT_MB);
	case UT_GB:  return T(S_USBDRIVE_UNIT_GB);
	default:     return T(S_USBDRIVE_PER_SECOND);
	}
}

/* How wide a token's cell is: a digit as wide as the widest digit (the
 * numbers do not shift as they count), anything else its own width */
static int ud_cell(TTF_Font *f, int t)
{
	char buf[2];

	if (t >= 0 && t <= 9) {
		int w = 0, d;

		for (d = 0; d <= 9; d++) {
			int dw;

			buf[0] = (char)('0' + d);
			buf[1] = '\0';
			dw = ui_text_width(f, buf);
			if (dw > w) w = dw;
		}
		return w;
	}
	return ui_text_width(f, ud_token(t, buf));
}

/* A number written as usbdrive writes it, "23.4" "MB" per-second, into
 * tokens; how many */
static int ud_tokens(const char *num, int unit, bool per_s, int *tok, int max)
{
	int n = 0;

	for (; *num && n < max - 3; num++)
		tok[n++] = *num == '.' ? UT_DEC : *num - '0';
	tok[n++] = UT_SP;
	tok[n++] = unit;
	if (per_s) tok[n++] = UT_PS;
	return n;
}

/* Their width, cell by cell as usbdrive puts them side by side */
static int ud_tokens_width(TTF_Font *f, const int *tok, int n)
{
	int w = 0, i;

	for (i = 0; i < n; i++) w += ud_cell(f, tok[i]);
	return w;
}

/* A number as plastron's usbdrive writes one on its page, by the rule of
 * its drivestat.c (dst_fmt_total, dst_fmt_rate): File Transfer's tiles
 * count their own bytes and say them as Flash Drive's do. `v` over `unit`
 * with `dec` decimals, rounded to the nearest. */
static int ud_scaled(unsigned long long v, unsigned long long unit, int dec, int u, bool per_s, int *tok,
                     int max)
{
	unsigned long long p10 = dec == 2 ? 100 : dec == 1 ? 10 : 1, x = (v * p10 + unit / 2) / unit;
	char num[32];

	if (dec) snprintf(num, sizeof num, "%llu.%0*llu", x / p10, dec, x % p10);
	else snprintf(num, sizeof num, "%llu", x);
	return ud_tokens(num, u, per_s, tok, max);
}

/* A total: "0 B", "312 MB", "1.24 GB" */
int ud_total(unsigned long long b, int *tok, int max)
{
	if (b < 1000) return ud_scaled(b, 1, 0, UT_B, false, tok, max);
	if (b < 999500ull) return ud_scaled(b, 1000ull, 0, UT_KB, false, tok, max);
	if (b < 999500000ull) return ud_scaled(b, 1000000ull, 0, UT_MB, false, tok, max);
	if (b < 999995000000ull) return ud_scaled(b, 1000000000ull, 2, UT_GB, false, tok, max);
	return ud_scaled(b, 1000000000ull, 0, UT_GB, false, tok, max);
}

/* A speed: "0 MB/s" when nothing moves, "512 KB/s", "23.4 MB/s" */
int ud_rate(unsigned long long b, int *tok, int max)
{
	if (b == 0) return ud_scaled(0, 1000000ull, 0, UT_MB, true, tok, max);
	if (b < 999500ull) return ud_scaled(b, 1000ull, 0, UT_KB, true, tok, max);
	if (b < 999950000ull) return ud_scaled(b, 1000000ull, 1, UT_MB, true, tok, max);
	return ud_scaled(b, 1000000000ull, 2, UT_GB, true, tok, max);
}

/* The tokens drawn from x, as usbdrive composes them from the atlas */
static void ud_draw_tokens(SDL_Renderer *r, TTF_Font *f, const int *tok, int n, int x, int cy, SDL_Color c)
{
	char buf[2];
	int i;

	for (i = 0; i < n; i++) {
		int cw = ud_cell(f, tok[i]);

		if (tok[i] >= 0 && tok[i] <= 9) {
			/* A digit in the middle of its cell */
			const char *s = ud_token(tok[i], buf);

			ui_text(r, f, s, x + (cw - ui_text_width(f, s)) / 2, ui_cap_y(f, cy), -1, c);
		} else if (tok[i] != UT_SP) {
			ui_text(r, f, ud_token(tok[i], buf), x, ui_cap_y(f, cy), -1, c);
		}
		x += cw;
	}
}

/* The widest number a slot can show (999.9 MB/s; 999.99 GB): said to the
 * host's fit check when it is wider than the tile's room */
static void ud_fit_widest(TTF_Font *f, bool speed, int room)
{
	int tok[16], n = speed ? ud_tokens("999.9", UT_MB, true, tok, 16) : ud_tokens("999.99", UT_GB, false, tok, 16);

	if (ud_tokens_width(f, tok, n) > room)
		printf("i18n: cut data: %s (the widest number, %d of %d)\n", speed ? "999.9 MB/s" : "999.99 GB",
		       ud_tokens_width(f, tok, n), room);
}

const ud_rows UD_PAGE = { UD_PAD, UD_MARK_CY - UD_TILE_Y, UD_SPEED_CY - UD_TILE_Y,
                          UD_TOTAL_CY - UD_TILE_Y, UD_TILE_H - 22 };

void ud_tile(SDL_Renderer *r, const SDL_Rect *q, const ud_rows *at, bool in, bool active,
             const int *speed, int ns, const int *total, int nt, float bar, ud_rect slot[UD_SLOTS])
{
	const ds_theme *t = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int left = q->x + at->pad, room = q->w - 2 * at->pad;
	int mark_cy = q->y + at->mark, speed_cy = q->y + at->speed, total_cy = q->y + at->total;
	char fit[160];

	ui_round_rect(r, q, DS_RADIUS_CARD, ui_col(t->raised, 255));
	ui_glyph_draw(r, in ? UI_GLYPH_TO_CARD : UI_GLYPH_FROM_CARD, left + UD_MARK / 2, mark_cy, 56,
	              active ? ui_col(t->accent_ink, 255) : UI_TEXT_DIM);
	ui_fit_text(fm, T(in ? S_USBDRIVE_TO_CARD : S_USBDRIVE_FROM_CARD), fit, sizeof fit,
	            room - UD_MARK - 16);
	ui_text(r, fm, fit, left + UD_MARK + 16, ui_cap_y(fm, mark_cy), -1, UI_TEXT);
	if (speed) ud_draw_tokens(r, ft, speed, ns, left, speed_cy, UI_TEXT);
	if (total) ud_draw_tokens(r, fs, total, nt, left, total_cy, UI_TEXT_SOFT);
	if (bar >= 0.0f)
		ds_bar(r, left, q->y + at->bar, room, (int)(room * (bar > 1.0f ? 1.0f : bar) + 0.5f), 255);
	ud_fit_widest(ft, true, room);
	ud_fit_widest(fs, false, room);
	if (slot) {
		int k = in ? 0 : 1;

		slot[UD_S_MARK_IN + k] = (ud_rect){ left, mark_cy - UD_MARK / 2, left + UD_MARK, mark_cy + UD_MARK / 2 };
		slot[UD_S_SPEED_IN + k] = (ud_rect){ left, speed_cy - UD_SPEED_H / 2, left + room,
		                                     speed_cy + UD_SPEED_H / 2 };
		slot[UD_S_TOTAL_IN + k] = (ud_rect){ left, total_cy - UD_TOTAL_H / 2, left + room,
		                                     total_cy + UD_TOTAL_H / 2 };
	}
}

/* Flash Drive's page's tile for a way */
SDL_Rect ud_page_tile(bool in)
{
	return ds_page_frame_of(true).tile[in ? 0 : 1];
}

/* Two button hints at the foot (ds_page_foot), the shelves' size, centered as
 * a pair; or one, centered, with `k2` NULL */
void ud_page_foot(SDL_Renderer *r, const char *k1, const char *l1, const char *k2, const char *l2)
{
	const char *keys[2] = { k1, k2 }, *words[2] = { l1, l2 };
	const ds_page_frame f = ds_page_frame_of(true);

	ds_page_foot(r, &f, keys, words, k2 ? 2 : 1, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}
#define ud_foot ud_page_foot

/* A page's top (src/ds/page.h): the ground, and Zyper beside the heading,
 * unless the look has him off; and the page's frame, its heading beside him,
 * or with him off where the tiles start */
ds_page_frame page_top(app *a, float smile)
{
	draw_background(a);
	if (!look_zyper_off())
		draw_zyper(a->r, DS_PAGE_ZYPER_X, DS_PAGE_ZYPER_Y, DS_PAGE_ZYPER_H, ZYPER_TILT_DEG, smile, 0.0f, 255);
	return ds_page_frame_of(!look_zyper_off());
}

/* What a shot shows in the tiles (--usb-drive 0 [in|out|both|wide]): 0 the
 * page as usbdrive starts it, both idle; 1 a copy to the card; 2 one from
 * it; 3 both ways; 4 the widest numbers */
int shot_usbdrive_vals;

/* One of the pages: Zyper beside the heading, as on the empty home, unless
 * the look has him off.
 * Connected: two **raised** tiles (**radius-card**) side by side, to the
 * card and from it, the eject line under them in **meta** **text-soft**, and
 * POWER's hint at the foot as the shelves' are. One moment and The card is
 * busy: their words in one tile the width of both. */
void usb_drive_draw(app *a, int which)
{
	static const str_id HEAD[UD_FRAMES] = { S_USBDRIVE_CONNECTED, S_USBDRIVE_FINISHING, S_USBDRIVE_FAILED,
	                                        S_USBDRIVE_CONFIRM_HEAD };
	static const str_id BODY[UD_FRAMES] = { S_USBDRIVE_EJECT, S_USBDRIVE_FINISHING_BODY, S_USBDRIVE_FAILED_BODY,
	                                        S_USBDRIVE_CONFIRM_BODY };
	const ds_theme *t = theme();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	char lines[3][LB_LINE];
	/* With Zyper off (Theme), the heading starts where the tiles do */
	ds_page_frame pf = page_top(a, which == UD_FAILED ? 0.0f : 1.0f);
	int n, k;

	/* The heading on one line, or two balanced beside him */
	ds_page_head(a->r, &pf, DS_HEAD_BALANCED, T(HEAD[which]), NULL, UI_GLYPH_COUNT, 0);
	if (which != UD_CONNECTED) {
		/* The words in one tile, balanced, in the middle of it */
		SDL_Rect q = { pf.tile[0].x, pf.tile[0].y, pf.tile[1].x + pf.tile[1].w - pf.tile[0].x, pf.tile[0].h };

		ui_round_rect(a->r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
		n = ui_balance_lines(fm, T(BODY[which]), q.w - 2 * UD_PAD, 3, lines);
		if (n > 1) ui_fit_drawn(T(BODY[which]));
		for (k = 0; k < n; k++)
			ui_text(a->r, fm, lines[k], ZYPEROS_SCREEN_W / 2,
			        ui_cap_y(fm, q.y + q.h / 2 + (2 * k - (n - 1)) * 26), 0, UI_TEXT_SOFT);
		if (which == UD_CONFIRM)
			ud_foot(a->r, "A", T(S_USBDRIVE_EJECT_KEY), "B", T(S_USBDRIVE_KEEP));
		return;
	}
	{
		/* The tiles: as usbdrive starts them (nothing moved yet), or a
		 * shot's numbers */
		static const struct { const char *s; int su; const char *t; int tu; } V[5][2] = {
			{ { "0", UT_MB, "0", UT_B },          { "0", UT_MB, "0", UT_B } },
			{ { "23.4", UT_MB, "1.24", UT_GB },   { "0", UT_MB, "312", UT_MB } },
			{ { "0", UT_MB, "1.24", UT_GB },      { "41.7", UT_MB, "312", UT_MB } },
			{ { "18.2", UT_MB, "1.24", UT_GB },   { "6.5", UT_MB, "312", UT_MB } },
			{ { "999.9", UT_MB, "999.99", UT_GB }, { "999.9", UT_MB, "999.99", UT_GB } },
		};
		static const bool ACTIVE[5][2] = { { false, false }, { true, false }, { false, true }, { true, true },
		                                   { true, true } };
		int v = shot_usbdrive_vals >= 0 && shot_usbdrive_vals < 5 ? shot_usbdrive_vals : 3, w;

		for (w = 0; w < 2; w++) {
			int sp[16], to[16];
			int ns = ud_tokens(V[v][w].s, V[v][w].su, true, sp, 16);
			int nt = ud_tokens(V[v][w].t, V[v][w].tu, false, to, 16);
			SDL_Rect q = ud_page_tile(w == 0);

			ud_tile(a->r, &q, &UD_PAGE, w == 0, ACTIVE[v][w], sp, ns, to, nt, -1.0f, NULL);
		}
	}
	n = ui_balance_lines(fs, T(S_USBDRIVE_EJECT), ZYPEROS_SCREEN_W - 2 * DS_INSET, 2, lines);
	if (n > 1) ui_fit_drawn(T(S_USBDRIVE_EJECT));
	for (k = 0; k < n; k++)
		ui_text(a->r, fs, lines[k], ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, pf.news_y + k * DS_LINE_META), 0, UI_TEXT_SOFT);
	ud_foot(a->r, "B", T(S_USBDRIVE_EJECT_KEY), "POWER", T(S_USBDRIVE_STOP));
	/* A shot of FUNCTION and volume on the page (--usb-drive 0 bright): the
	 * bar usbdrive shows, at a rung, where it shows it */
	if (shot_usbdrive_vals == 5) {
		static uint32_t px[OSDBAR_W * OSDBAR_H];
		SDL_Texture *tx;

		osdbar_render(px, OSDBAR_W, OSDBAR_H, OSDBAR_W, 1.0f, theme_index(), OSDBAR_BRIGHT, 8,
		              PLAT_BRIGHT_MAX, 1.0f, false);
		if ((tx = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, OSDBAR_W,
		                            OSDBAR_H))) {
			SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
			SDL_UpdateTexture(tx, NULL, px, OSDBAR_W * 4);
			SDL_RenderCopy(a->r, tx, NULL, &(SDL_Rect){ (ZYPEROS_SCREEN_W - OSDBAR_W) / 2, OSDBAR_Y, OSDBAR_W,
			                                           OSDBAR_H });
			SDL_DestroyTexture(tx);
		}
	}
}

/* usbdrive's atlas (plastron's package/usbdrive/src/atlas.h): every token in
 * the speeds' face and in the totals', and each way's mark moving and idle,
 * on the tiles' color, each from an edge that is a multiple of 8, saved as
 * atlas.raw beside the pages, and atlas.txt saying where they and the
 * page's slots are in the saved 640 x 480. A shot (`shot`, --usb-drive 3)
 * prints atlas.txt there and saves nothing. */
bool usb_drive_atlas(app *a, const char *raw, const char *txt, FILE *shot)
{
	static const char *const TOKEN[UT_TOKENS] = {
		"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "dec", "sp", "B", "KB", "MB", "GB", "ps",
	};
	static const char *const MARK[4] = { "in_on", "in_off", "out_on", "out_off" };
	static const char *const SLOT[UD_SLOTS] = { "speed_in", "speed_out", "total_in", "total_out",
	                                            "mark_in", "mark_out" };
	TTF_Font *ft = ui_font(UI_F_TITLE), *fs = ui_font(UI_F_META);
	const ds_theme *th = theme();
	ud_rect slot[UD_SLOTS];
	int bx[UT_TOKENS], bcy[UT_TOKENS], i;
	FILE *f;

	/* Where the slots are: the connected page's own tiles */
	SDL_SetRenderDrawColor(a->r, th->raised.r, th->raised.g, th->raised.b, 255);
	SDL_RenderClear(a->r);
	for (i = 0; i < 2; i++) {
		SDL_Rect q = ud_page_tile(i == 0);

		ud_tile(a->r, &q, &UD_PAGE, i == 0, false, NULL, 0, NULL, 0, -1.0f, slot);
	}
	SDL_SetRenderDrawColor(a->r, th->raised.r, th->raised.g, th->raised.b, 255);
	SDL_RenderClear(a->r);
	/* The speeds' tokens: the digits, the mark and the space on a row, the
	 * units and "/s" on the next; the totals' all on a third; the marks on a
	 * fourth. Every cell from a multiple of 8, a row's middle on one too. */
	for (i = 0; i < UT_TOKENS; i++) {
		int tok = i;

		bx[i] = i <= UT_SP ? 32 + i * 64 : 32 + (i - UT_B) * 128;
		bcy[i] = i <= UT_SP ? 64 : 160;
		ud_draw_tokens(a->r, ft, &tok, 1, bx[i], bcy[i], UI_TEXT);
		ud_draw_tokens(a->r, fs, &tok, 1, 32 + i * 56, 248, UI_TEXT_SOFT);
	}
	for (i = 0; i < 4; i++)
		ui_glyph_draw(a->r, i < 2 ? UI_GLYPH_TO_CARD : UI_GLYPH_FROM_CARD, 32 + i * 96 + UD_MARK / 2, 336, 56,
		              i % 2 ? UI_TEXT_DIM : ui_col(th->accent_ink, 255));
	if (shot) f = shot;
	else if (!plat_save_screen_hidden(raw) || !(f = fopen(txt, "w"))) return false;
	fprintf(f, "zyperos-drive-atlas 2\nsize %d %d\n", UD_SAVE_W, UD_SAVE_H);
	for (i = 0; i < UD_SLOTS; i++)
		fprintf(f, "slot %s %d %d %d %d left\n", SLOT[i], slot[i].x0 * 5 / 8, slot[i].y0 * 5 / 8,
		        slot[i].x1 * 5 / 8, slot[i].y1 * 5 / 8);
	/* A cell up to the pixel its width ends in */
	for (i = 0; i < UT_TOKENS; i++) {
		fprintf(f, "glyph b%s %d %d %d %d\n", TOKEN[i], bx[i] * 5 / 8, (bcy[i] - UD_SPEED_H / 2) * 5 / 8,
		        (ud_cell(ft, i) * 5 + 7) / 8, UD_SPEED_H * 5 / 8);
		fprintf(f, "glyph s%s %d %d %d %d\n", TOKEN[i], (32 + i * 56) * 5 / 8, (248 - UD_TOTAL_H / 2) * 5 / 8,
		        (ud_cell(fs, i) * 5 + 7) / 8, UD_TOTAL_H * 5 / 8);
	}
	for (i = 0; i < 4; i++)
		fprintf(f, "glyph %s %d %d %d %d\n", MARK[i], (32 + i * 96) * 5 / 8, (336 - UD_MARK / 2) * 5 / 8,
		        UD_MARK * 5 / 8, UD_MARK * 5 / 8);
	return shot ? true : fclose(f) == 0;
}

/* usbdrive's brightness (its atlas.h's at_bright): bright.txt, the rung now
 * and the backlight's ladder, the launcher's own so FUNCTION and volume step
 * the same rungs there, and where the bar goes; and bright.raw, the bar
 * (src/osdbar.h, the one every other place shows) at each rung, already laid
 * on the page's ground, where it sits at the top. False where the backlight
 * has no ladder to hand on (the Brick). */
#define UD_BAR_X ((ZYPEROS_SCREEN_W - OSDBAR_W) / 2 * 5 / 8)   /* OSDBAR_X on 4:3: in the middle */
#define UD_BAR_Y ((OSDBAR_Y * 5) / 8)
#define UD_BAR_W ((OSDBAR_W * 5 + 7) / 8)
#define UD_BAR_H ((OSDBAR_H * 5 + 7) / 8)

static bool usb_drive_bright(const char *txt, const char *raw)
{
	static uint32_t px[UD_BAR_W * UD_BAR_H];
	const ds_theme *t = theme();
	FILE *f;
	int k, i;

	if (plat_brightness_raw(0) < 0 || !(f = fopen(txt, "w"))) return false;
	fprintf(f, "zyperos-drive-bright 1\nlevel %d\nladder", plat_brightness_get());
	for (k = 0; k <= PLAT_BRIGHT_MAX; k++) fprintf(f, " %d", plat_brightness_raw(k));
	fprintf(f, "\nbar %d %d %d %d\n", UD_BAR_X, UD_BAR_Y, UD_BAR_W, UD_BAR_H);
	if (fclose(f) != 0 || !(f = fopen(raw, "wb"))) return false;
	for (k = 0; k <= PLAT_BRIGHT_MAX; k++) {
		osdbar_render(px, UD_BAR_W, UD_BAR_H, UD_BAR_W, 0.625f, theme_index(), OSDBAR_BRIGHT, k,
		              PLAT_BRIGHT_MAX, 1.0f, false);
		/* Over the ground, as the page has it there */
		for (i = 0; i < UD_BAR_W * UD_BAR_H; i++) {
			uint32_t c = px[i], al = c >> 24;
			uint32_t r = ((c >> 16 & 255) * al + t->ground.r * (255 - al) + 127) / 255;
			uint32_t g = ((c >> 8 & 255) * al + t->ground.g * (255 - al) + 127) / 255;
			uint32_t b = ((c & 255) * al + t->ground.b * (255 - al) + 127) / 255;

			px[i] = 0xFF000000u | r << 16 | g << 8 | b;
		}
		if (fwrite(px, 4, (size_t)UD_BAR_W * UD_BAR_H, f) != (size_t)UD_BAR_W * UD_BAR_H) {
			fclose(f);
			return false;
		}
	}
	return fclose(f) == 0;
}

static void usb_drive(app *a)
{
	static const char *const NAME[UD_FRAMES] = { "connected.raw", "finishing.raw", "failed.raw", "confirm.raw" };
	char path[256], txt[256];
	int i;
	FILE *f;

	mkdir(USB_DRIVE_DIR, 0755);
	shot_usbdrive_vals = 0;
	/* "One moment" up at once and held: the pages below are made and saved
	 * out of sight (plat_save_screen_hidden), so nothing else flashes past
	 * before usbdrive shows the same "One moment" */
	usb_drive_draw(a, UD_FINISHING);
	plat_present();
	for (i = 0; i < UD_FRAMES; i++) {
		usb_drive_draw(a, i);
		snprintf(path, sizeof path, "%s/%s", USB_DRIVE_DIR, NAME[i]);
		unlink(path);
		if (!plat_save_screen_hidden(path)) {
			/* No picture to leave (a host): nothing is asked */
			fprintf(stderr, "usb: USB Drive: could not save %s\n", path);
			plat_present();
			return;
		}
	}
	/* The atlas for the live rows; without it usbdrive shows the page as
	 * it is, so a failure here is no reason to stop */
	snprintf(path, sizeof path, "%s/atlas.raw", USB_DRIVE_DIR);
	snprintf(txt, sizeof txt, "%s/atlas.txt", USB_DRIVE_DIR);
	unlink(path);
	unlink(txt);
	if (!usb_drive_atlas(a, path, txt, NULL)) {
		fprintf(stderr, "usb: USB Drive: no atlas; the page without live rows\n");
		unlink(txt);
	}
	/* And the brightness for its page; without it the chord does nothing */
	snprintf(path, sizeof path, "%s/bright.raw", USB_DRIVE_DIR);
	snprintf(txt, sizeof txt, "%s/bright.txt", USB_DRIVE_DIR);
	unlink(path);
	unlink(txt);
	if (!usb_drive_bright(txt, path)) {
		fprintf(stderr, "usb: USB Drive: no brightness for its page\n");
		unlink(txt);
	}
	/* What it was doing comes back after, as from a power-off */
	xs_end();
	snap_off();
	remember_place(a);
	book_keep(true);
	if (!(f = fopen(USB_DRIVE_ASK, "w"))) return;
	fclose(f);
	/* usbdrive steps the backlight on these rungs but sets no gamma table:
	 * the light at the two under the floor is the floor's there, and the
	 * others are right only with no table on */
	plat_display_plain();
	fprintf(stderr, "usb: USB Drive: ZyperOS leaves the card to the computer\n");
	usb_drive_draw(a, UD_FINISHING);
	plat_present();
	a->running = false;
}
static void ulog_note(const char *what);

void usb_ask(app *a)
{
	menu_row tiles[4];
	int what[4], marks[4];
	int n, sel = 0, choice = -1, lv;
	usbpower_guard guard;
	float k;

	n = usb_ask_tiles(tiles, what, marks, usblink_dev_image());
	lv = panel_open();
	ask_guard_start(a, &guard);
	while (choice < 0) {
		plat_input_poll(&a->in);
		ask_settle(a, &guard);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { panel_close(a, lv, true); power_off(a); return; }
		if (menu_leaving(a)) choice = 0;
		if (plat_usb_peer() != USB_PEER_COMPUTER) choice = 0;   /* unplugged */
		/* each button read once a frame: a second read of a held Left
		 * was false and turned it Right (in_repeat_axis) */
		int lr = in_repeat_axis(&a->in, IN_LEFT, IN_RIGHT);

		if (lr) {
			in_button b = lr < 0 ? IN_LEFT : IN_RIGHT;

			moved_sound(a, b);
			sel = (sel + lr + n) % n;
		}
		/* Up and down between the grid's two rows */
		int ud = in_repeat_axis(&a->in, IN_UP, IN_DOWN);

		if (ud && n > 2) {
			in_button b = ud < 0 ? IN_UP : IN_DOWN;
			int to = sel >= 2 ? sel - 2 : sel + 2;

			if (to >= n) to = n - 1;
			if (to != sel) { moved_sound(a, b); sel = to; }
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			choice = sel;
		}
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		tiles_show(a, PANEL_TILES_GRID, T(S_XFER_COMPUTER_CONNECTED), tiles, n, marks, n, sel);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	g_tiles_hints = true;
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	choice = choice >= 0 && choice < n ? what[choice] : UA_CHARGE;
	ulog_note(choice == UA_XFER ? "Computer Connected: File Transfer"
	          : choice == UA_DEV ? "Computer Connected: Developer"
	          : choice == UA_DRIVE ? "Computer Connected: Flash Drive" : "Computer Connected: Charge Only");
	if (choice == UA_DRIVE) {
		usb_drive(a);
	} else if (choice == UA_XFER) {
		xfer_screen(a);
	} else if (choice == UA_DEV) {
		menu_row row = { T(S_XFER_DEV_LINK_ON), NULL, false };

		usblink_hold(true);
		timed_message(a, T(S_XFER_DEV_HEADING), &row, 1, 0, MENU_ZYPEROS);
	} else {
		usblink_hold(false);
	}
}

const int UP_MARK[UP_TILES] = { UI_GLYPH_HEADPHONES, UI_GLYPH_LAPTOP, UI_GLYPH_PHONE };

int usb_power_rows(menu_row rows[UP_TILES + 1])
{
	/* A tile's value is the small line under its name (tiles_panel) */
	rows[UP_ACCESSORY] = (menu_row){ T(S_XFER_TILE_ACCESSORY), T(S_XFER_TILE_ACCESSORY_SUB), true };
	rows[UP_COMPUTER] = (menu_row){ T(S_XFER_TILE_COMPUTER), T(S_XFER_TILE_COMPUTER_SUB), true };
	rows[UP_PHONE] = (menu_row){ T(S_XFER_TILE_PHONE), T(S_XFER_TILE_PHONE_SUB), true };
	rows[UP_TILES] = MENU_NOTE(list_hints(T(S_XFER_TILE_COMPUTER)));
	return UP_TILES + 1;
}

static void replug_on(unsigned plug);

static void phone_said_on(unsigned plug);
/* The plug answered Phone, 0 none: only its tethering is acted on by itself
 * (phone_link_due) */
static unsigned g_phone_plug;

static void replug_off(void);

/* The port's log for support (src/usblog.h): read from the card at the
 * first look, kept as the port is followed, written back at most every 2 s
 * when it changed, whole under another name and renamed over */
static usblog g_ulog;

static void ulog_path(char *out, size_t n)
{
	snprintf(out, n, "%s/logs/usb.log", P_USERDATA);
}

static void ulog_save(bool now)
{
	static unsigned saved_at;
	static char text[USBLOG_MAX * USBLOG_LINE + 64];
	char path[512], tmp[520];
	unsigned t = plat_now_ms();
	FILE *f;

	if (!g_ulog.dirty || (!now && saved_at && t - saved_at < 2000)) return;
	saved_at = t ? t : 1;
	g_ulog.dirty = false;
	ulog_path(path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s.new", path);
	usblog_text(&g_ulog, text, sizeof text);
	if (!(f = fopen(tmp, "w"))) return;
	fputs(text, f);
	if (fclose(f) == 0) rename(tmp, path);
	else remove(tmp);
}

static void ulog_follow(const usb_power *p)
{
	static bool loaded;

	if (!loaded) {
		static char text[USBLOG_MAX * USBLOG_LINE + 64];
		char path[512];
		FILE *f;
		size_t got = 0;

		loaded = true;
		ulog_path(path, sizeof path);
		if ((f = fopen(path, "r"))) {
			got = fread(text, 1, sizeof text - 1, f);
			fclose(f);
		}
		text[got] = '\0';
		usblog_load(&g_ulog, text);
	}
	usblog_follow(&g_ulog, p, (long long)time(NULL));
	ulog_save(false);
}

static void ulog_note(const char *what)
{
	usblog_note(&g_ulog, what);
	ulog_save(true);
}

void usb_power_ask(app *a, unsigned plug)
{
	menu_row rows[UP_TILES + 1];
	int n = usb_power_rows(rows), sel = UP_COMPUTER, choice = -1, lv;
	unsigned looked = 0;
	usbpower_guard guard;
	float f;

	/* The question takes the replug message's place: Computer brings it
	 * back */
	replug_off();
	lv = panel_open();
	ask_guard_start(a, &guard);
	while (choice < 0) {
		unsigned now = plat_now_ms();

		plat_input_poll(&a->in);
		ask_settle(a, &guard);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { panel_close(a, lv, true); power_off(a); return; }
		if (menu_leaving(a)) choice = UP_COMPUTER;
		/* Unplugged, or power came in: nothing to ask any more */
		if (!looked || now - looked >= 200u) {
			usb_power p;

			looked = now ? now : 1;
			if (!plat_usb_power(&p) || p.plug != plug || !p.ask) choice = UP_GONE;
		}
		/* A list: Up and Down, and Left and Right as the tiles were */
		/* each button read once a frame (in_repeat_axis): the second read
		 * of a held one was false, and the step went the other way */
		int v = in_repeat_axis(&a->in, IN_UP, IN_DOWN), h = in_repeat_axis(&a->in, IN_LEFT, IN_RIGHT);

		if (v || h) {
			in_button b = v < 0 ? IN_UP : v > 0 ? IN_DOWN : h < 0 ? IN_LEFT : IN_RIGHT;

			moved_sound(a, b);
			sel = (sel + (b == IN_UP || b == IN_LEFT ? -1 : 1) + UP_TILES) % UP_TILES;
		}
		if (a->in.pressed[IN_ACCEPT]) {
			uisound_play(DS_SOUND_ACCEPT);
			choice = sel;
		}
		if (!panel_frame(a, lv, &f)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, f);
		tiles_show(a, PANEL_LIST, T(S_XFER_USB_DEVICE_CONNECTED), rows, n, UP_MARK, UP_TILES, sel);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	g_tiles_hints = true;
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	if (a->running) ulog_note(choice == UP_ACCESSORY ? "asked: USB Accessory"
	                          : choice == UP_COMPUTER ? "asked: Computer"
	                          : choice == UP_PHONE ? "asked: Phone" : "asked: gone before an answer");
	if (!a->running || choice == UP_GONE) return;
	if (choice == UP_ACCESSORY) {
		plat_usb_power_grant(plug);
		/* Counted after the power, which waits for nothing: the second
		 * may bring Zyper's offer of Automatic, on the shelf */
		usb_offer_count();
		return;
	}
	/* A phone: powered, so it is on the handheld's bus as a device, and
	 * asked for its USB tethering, until the cable comes out or File
	 * Transfer opens on its network (phone_link_follow); an iPhone's own
	 * message once it is seen (power_follow) */
	if (choice == UP_PHONE) {
		plat_usb_power_grant(plug);
		g_phone_plug = plug;
		phone_said_on(plug);
		return;
	}
	/* A computer: the message, until the cable comes out */
	replug_on(plug);
}

int usb_offer_rows(app *a, char lines[USB_OFFER_LINES][LB_LINE], menu_row *rows)
{
	int n = ui_balance_lines(ui_font(UI_F_MENU), T(S_XFER_USB_OFFER_SAID), menu_std_width(a),
	                         USB_OFFER_LINES, lines), i;

	ui_fit_drawn(T(S_XFER_USB_OFFER_SAID));
	for (i = 0; i < n; i++) rows[i] = MENU_NOTE(lines[i]);
	rows[n++] = MENU_HR;
	return n;
}

void usb_offer_show(app *a)
{
	char lines[USB_OFFER_LINES][LB_LINE];
	menu_row rows[USB_OFFER_LINES + 3];
	usbpower_guard guard;
	int nq, lv;
	bool yes;

	g_usb_offer_due = false;
	if (g_usb_auto || db_has(db_dev(), "usb_auto_offer")) return;
	nq = usb_offer_rows(a, lines, rows);
	lv = panel_open();
	ask_guard_start(a, &guard);
	yes = confirm_rows_as(a, T(S_XFER_USB_OFFER), rows, nq, T(S_SYSMENU_USB_AUTO),
	                      T(S_XFER_USB_OFFER_KEEP), menu_std_width(a), lv, &guard);
	g_tiles_hints = true;
	panel_close(a, lv, !a->running);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	if (!a->running) return;
	ulog_note(yes ? "offer: Automatic" : "offer: Keep Asking");
	db_set_str(db_dev(), "usb_auto_offer", yes ? "auto" : "ask");
	if (yes) usb_auto_save(true);
}

/* usbrole's state, five times a second at most, from wherever the launcher
 * is: the shelf's loop, every screen's frame (the overlay below) and a
 * game's tick. A plug to ask about waits in g_power_ask for the shelf, the
 * only place that asks; a computer that has not connected brings up the
 * replug message (below). */
static usbpower_follow_state g_power_f;
unsigned g_power_ask;            /* a plug to ask about, 0 none */
replug_state g_replug;

static void said_on(unsigned plug, int say)
{
	if (g_replug.on && g_replug.plug == plug && g_replug.say == say) return;
	g_replug.on = true;
	g_replug.plug = plug;
	g_replug.say = say;
	if (say == USBPOWER_SAY_PHONE) {
		fprintf(stderr, "usb: a phone: what it needs to connect (plug %u)\n", plug);
		ulog_note("phone message");
	} else {
		fprintf(stderr, "usb: the computer has not connected: replug message up (plug %u)\n", plug);
		ulog_note("replug message");
	}
	redraw_now();
}

static void replug_on(unsigned plug) { said_on(plug, USBPOWER_SAY_COMPUTER); }

static void phone_said_on(unsigned plug) { said_on(plug, USBPOWER_SAY_PHONE); }

static void replug_off(void)
{
	if (!g_replug.on) return;
	g_replug.on = false;

	fprintf(stderr, "usb: replug message down\n");
	redraw_now();
}

/* A phone's USB tethering (phonelink.h), looked at twice a second while
 * the port powers the plug the player answered Phone for: true once a link,
 * when it has its address and File Transfer is not on (`xfer_on`), so the
 * shelf opens it there. The phone message goes then: its words are done.
 * Only for that plug: a network device on a plug answered USB Accessory, or
 * powered by Automatic, is never given a lease or a File Transfer by itself
 * (anything can say it is a USB network). MENU's USB File Transfer opens
 * on the phone's link again once it has its address. */
bool phone_link_due(bool xfer_on)
{
	static unsigned looked;
	static char last[64];
	static bool acted;                  /* plat_phone_link set it up */
	unsigned now = plat_now_ms();
	char ifname[32], ip[64];
	usb_power p = { 0 };

	if (looked && now - looked < 500u) return false;
	looked = now ? now : 1;
	if (!g_phone_plug || !plat_usb_power(&p) || !p.host || p.plug != g_phone_plug) {
		/* not the phone's plug, or the port no host: none is looked for
		 * (the scan is for the host's devices); the phone just gone, its
		 * lease's client goes too (plat_phone_link finds no interface) */
		if (acted && !p.host) {
			plat_phone_link(true, NULL, 0, NULL, 0);
			acted = false;
		}
		if (last[0]) phonelink_new(last, NULL, NULL);
		if (g_phone_plug && p.known && p.plug != g_phone_plug) g_phone_plug = 0;
		return false;
	}
	acted = true;
	if (!plat_phone_link(true, ifname, sizeof ifname, ip, sizeof ip)) ifname[0] = ip[0] = '\0';
	if (!phonelink_new(last, ifname, ip) || xfer_on) return false;
	fprintf(stderr, "usb: the phone shared its network: %s on %s\n", ip, ifname);
	ulog_note("phone network");
	if (g_replug.on && (g_replug.say == USBPOWER_SAY_PHONE || g_replug.say == USBPOWER_SAY_IPHONE))
		replug_off();
	return true;
}

void power_follow(void)
{
	static unsigned looked;
	unsigned now = plat_now_ms(), plug;
	usb_power p;

	if (looked && now - looked < 200u) return;
	looked = now ? now : 1;
	if (!plat_usb_power(&p)) { replug_off(); g_usb_wait = 0; return; }
	ulog_follow(&p);
	{
		unsigned was = g_usb_wait;

		g_usb_wait = p.known && p.ask && (!p.automatic || p.paused) ? p.plug : 0;
		if (g_usb_wait != was) redraw_now();
	}
	/* usbrole started after the launcher told it, or again: told again */
	if (p.known && p.automatic != g_usb_auto) plat_usb_power_mode(g_usb_auto);
	/* The message stays until the plug ends: unplugged (the pin idle),
	 * another plug, the computer or the phone connected (power in), or the
	 * port a host (usbpower_said_ends) */
	if (g_replug.on && usbpower_said_ends(g_replug.say, g_replug.plug, &p))
		replug_off();
	/* Phone's message, the phone powered and on the bus: an iPhone, which
	 * shares its network only with a computer it trusts, gets its own */
	if (g_replug.on && g_replug.say == USBPOWER_SAY_PHONE && p.host && plat_usb_iphone()) {
		fprintf(stderr, "usb: the phone is an iPhone: its message\n");
		ulog_note("an iPhone");
		g_replug.say = USBPOWER_SAY_IPHONE;
		redraw_now();
	}
	if (g_power_ask && (p.plug != g_power_ask || !p.ask)) g_power_ask = 0;
	/* The accessory powered when the handheld turned off, there again from
	 * the start: powered, and never asked about (usbpower_resume) */
	if (g_usb_kept) {
		if (usbpower_resume(&p, true, now - t_boot0)) {
			fprintf(stderr, "usb: plug %u is the accessory powered before: powered again\n", p.plug);
			plat_usb_power_grant(p.plug);
			g_power_f.asked = p.plug;
			g_usb_kept = false;
		} else if (now - t_boot0 >= USBPOWER_RESUME_MS || (p.known && p.plug > 1)) {
			g_usb_kept = false;
		}
	}
	switch (usbpower_follow(&g_power_f, &p, now, &plug)) {
	case USBPOWER_ASK:    g_power_ask = plug; break;
	case USBPOWER_REPLUG: replug_on(plug);    break;
	default:              break;
	}
}

/* The message's rows: its words balanced onto up to four lines in `w`;
 * Phone's, longer, at the smaller size (**meta**) when the menu's takes
 * more, onto up to six. *small says which. */
#define REPLUG_LINES 6
static int replug_lines(int w, char lines[LB_MAX_LINES][LB_LINE], bool *small)
{
	bool phone = g_replug.say == USBPOWER_SAY_PHONE || g_replug.say == USBPOWER_SAY_IPHONE;
	const char *text = T(g_replug.say == USBPOWER_SAY_IPHONE ? S_XFER_IPHONE_SAID
	                     : phone ? S_XFER_PHONE_SAID : S_XFER_REPLUG_COMPUTER);
	int n;

	*small = false;
	ui_fit_drawn(text);    /* drawn in lines, said whole to the host's check */
	if (!phone) {
		n = ui_balance_lines(ui_font(UI_F_MENU), text, w, 4, lines);
	} else {
		/* Room for all of it first, so the fewest lines it takes are
		 * known without a cut: four or under stay at the menu's size */
		n = ui_balance_lines(ui_font(UI_F_MENU), text, w, LB_MAX_LINES, lines);
		if (n > 4) {
			*small = true;
			n = ui_balance_lines(ui_font(UI_F_META), text, w, REPLUG_LINES, lines);
		}
	}
	ui_fit_drawn(text);
	return n;
}

/* The replug message over whatever screen is up (plat_set_overlay): a slab
 * at the top, under the header, as the song pop-up stands (DESIGN.md 11.22
 * and 11.36): **surface** with the **panel** lift, the laptop mark at its
 * left in **text-soft**, "Computer" in **meta** **text-dim** over the words
 * in **menu** **text** (up to four balanced lines), and under them the cable
 * that always connects in **meta** **text-dim** (up to two), until the plug
 * ends. Phone's message the same, with the phone mark and "Phone", in **menu**
 * up to four lines, else in **meta** up to six, and under it, in place of the
 * cable line, what a phone with no USB tethering does (up to three). */
#define REPLUG_W    960
#define REPLUG_TOP  104
#define REPLUG_TIP_GAP 8
void replug_draw_at(SDL_Renderer *r)
{
	bool phone = g_replug.say == USBPOWER_SAY_PHONE || g_replug.say == USBPOWER_SAY_IPHONE;
	/* the smaller line under the words: the cable that always connects a
	 * computer (up to two lines); for a phone asked to share its network,
	 * what one with no USB tethering does and that Flash Drive needs an
	 * adapter, the phone being the device here (up to three); none for an
	 * iPhone, whose message names the adapter itself */
	int tip_id = g_replug.say == USBPOWER_SAY_COMPUTER ? S_XFER_REPLUG_TIP
	           : g_replug.say == USBPOWER_SAY_PHONE ? S_XFER_PHONE_TIP : -1;
	char lines[LB_MAX_LINES][LB_LINE], tip[3][LB_LINE];
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META), *fb;
	const int lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	const int mark = 64, padx = 32, pady = 24, tx_off = padx + mark + 28;
	SDL_Rect slab;
	bool small;
	int n, k, y, lb, m = 0;

	n = replug_lines(REPLUG_W - tx_off - padx, lines, &small);
	fb = small ? fs : fm;
	lb = small ? ls : lm;
	/* A computer whose USB-C port takes the waiting side at every plug, as a
	 * phone's does, never connects by plugging in again (Discord,
	 * 2026-11-13): the cable that always does, under the words */
	if (tip_id >= 0) {
		m = ui_balance_lines(fs, T(tip_id), REPLUG_W - tx_off - padx, phone ? 3 : 2, tip);
		ui_fit_drawn(T(tip_id));
	}
	slab = (SDL_Rect){ (ZYPEROS_SCREEN_W - REPLUG_W) / 2, REPLUG_TOP, REPLUG_W,
	                   pady + ls + 4 + n * lb + (m ? REPLUG_TIP_GAP + m * ls : 0) + pady };
	ui_lift(r, &slab, CFG_NO_COLOR, UI_LIFT_PANEL);
	ui_panel(r, &slab, DS_RADIUS_PANEL);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	ui_glyph_draw(r, phone ? UI_GLYPH_PHONE : UI_GLYPH_LAPTOP, slab.x + padx + mark / 2, slab.y + slab.h / 2,
	              mark, UI_TEXT_SOFT);
	y = slab.y + pady;
	ui_text(r, fs, T(phone ? S_XFER_TILE_PHONE : S_XFER_TILE_COMPUTER), slab.x + tx_off,
	        ui_line_y(UI_F_META, y), -1, UI_TEXT_DIM);
	y += ls + 4;
	for (k = 0; k < n; k++)
		ui_text(r, fb, lines[k], slab.x + tx_off, ui_line_y(small ? UI_F_META : UI_F_MENU, y + k * lb), -1,
		        UI_TEXT);
	y += n * lb + REPLUG_TIP_GAP;
	for (k = 0; k < m; k++)
		ui_text(r, fs, tip[k], slab.x + tx_off, ui_line_y(UI_F_META, y + k * ls), -1, UI_TEXT_DIM);
}

bool replug_shown(void) { return g_replug.on; }

void replug_overlay(SDL_Renderer *r)
{
	power_follow();
	if (g_replug.on) replug_draw_at(r);
}

/* In a game nothing is shown (2026-11-10): the plug is only followed, so a
 * plug that ends is forgotten and one still waiting is asked about once the
 * player is back on the shelf */
void replug_game_tick(void)
{
	power_follow();
}
