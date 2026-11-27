/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_USBDRIVE_SCREEN_H
#define ZYPEROS_USBDRIVE_SCREEN_H

/* The cable's screens: MENU > USB Drive's page (plastron's usbdrive,
 * DESIGN.md 11.18) and the drawing it shares with File Transfer's moving
 * page, the questions a plug asks (Computer Connected, What did you plug
 * in?, USB Accessories' offer), its log, and the replug notice. The code is
 * src/usbdrive_screen.c. */
#include "app.h"
#include "ds/page.h"
#include "panel.h"

enum { UD_CONNECTED, UD_FINISHING, UD_FAILED, UD_CONFIRM, UD_FRAMES };

#define UD_TILE_W    DS_PAGE_TILE_W                             /* 472 */

#define UD_PAD       DS_PAGE_TILE_PAD                           /* 32 */

/* One way's tile, on Flash Drive's page and in File Transfer's panel
 * (xs_move_*): its mark, moving in **accent-ink** or idle in **text-dim**,
 * and its name in **menu** **text**; its speed big in **title** **text**;
 * its total in **meta** **text-soft**. `q` is the tile and `at` where its
 * rows are in it. `speed` and `total` as tokens, NULL for none (the atlas's
 * page). `bar`, 0..1, the file on its way (File Transfer knows it, Flash
 * Drive never does): a **bar** of **accent-ink** on **track** at the
 * tile's foot; below 0, none. */
typedef struct { int x0, y0, x1, y1; } ud_rect;

enum { UD_S_SPEED_IN, UD_S_SPEED_OUT, UD_S_TOTAL_IN, UD_S_TOTAL_OUT, UD_S_MARK_IN, UD_S_MARK_OUT, UD_SLOTS };

/* A tile's padding, and its rows' middles and the bar's top down from its top */
typedef struct { int pad, mark, speed, total, bar; } ud_rows;

/* Something on the USB port wants power and gives none (usbpower.h): a
 * DAC, a Wi-Fi adapter, a keyboard, a computer whose port took the sink
 * side, or a phone, which the handheld cannot tell apart without feeding it.
 * Asked once per plug over the shelf, as Computer Connected is (DESIGN.md
 * 11.36): three tiles, USB Accessory, Computer and Phone, starting on
 * Computer, so a quick A or B never feeds a computer. USB Accessory asks
 * usbrole for host for this plug (it says yes only while no power comes
 * in); Computer, B or MENU leave the port as it is and say to plug the cable
 * in again, so the computer can take the side that gives power. Phone powers
 * it as USB Accessory does: a phone takes the side that waits for power at
 * every plug, so a replug never helps, and powered it is the device on the
 * handheld's bus whatever its own USB settings say (2026-11-13); its USB
 * tethering then gives the handheld a network to the phone, and File
 * Transfer opens there (phonelink.h, usbpower.h). The question goes by itself
 * when the plug stops wanting power (unplugged, or power came). A list since
 * 2026-11-13 (choice_list_panel). */
enum { UP_ACCESSORY, UP_COMPUTER, UP_PHONE, UP_TILES, UP_GONE = UP_TILES };

/* Zyper's offer of Automatic (usbpower_offer_after, DESIGN.md 11.36), on
 * the shelf after the USB Accessory answer that made it due, the accessory
 * powered by then: the two-answer panel (CQ_USB_OFFER), Keep Asking on the
 * left with the cursor on it and Automatic on the right. It comes by
 * itself, so it takes a press as the question does (usbpower_guard).
 * Automatic is MENU > USB Accessories' own, told to usbrole; either answer
 * is kept, and then it is never offered again. B, MENU, POWER, Auto Off or a
 * quit answer nothing and keep nothing. */

typedef struct {
	bool     on;                        /* the message is up */
	unsigned plug;                      /* for this plug */
	int      say;                       /* USBPOWER_SAY_COMPUTER or _PHONE */
} replug_state;

/* What main.c uses of it; each is described where it is defined */
extern bool shot_usb_drive;
extern const ud_rows UD_PAGE;
extern int shot_usbdrive_vals;
extern const int UP_MARK[UP_TILES];
extern unsigned g_power_ask;
extern replug_state g_replug;
int usb_ask_tiles(menu_row tiles[4], int what[4], int marks[4], bool dev);
int ud_total(unsigned long long b, int *tok, int max);
int ud_rate(unsigned long long b, int *tok, int max);
void ud_tile(SDL_Renderer *r, const SDL_Rect *q, const ud_rows *at, bool in, bool active,
             const int *speed, int ns, const int *total, int nt, float bar, ud_rect slot[UD_SLOTS]);
SDL_Rect ud_page_tile(bool in);
void ud_page_foot(SDL_Renderer *r, const char *k1, const char *l1, const char *k2, const char *l2);
ds_page_frame page_top(app *a, float smile);
void usb_drive_draw(app *a, int which);
bool usb_drive_atlas(app *a, const char *raw, const char *txt, FILE *shot);
void usb_ask(app *a);
int usb_power_rows(menu_row rows[UP_TILES + 1]);
void usb_offer_show(app *a);
bool phone_link_due(bool xfer_on);
void power_follow(void);
/* What a plug on the USB port asks, from any screen that waits on the
 * player (the shelf, MP4's): a computer's question, once per plug, and
 * "What did you plug in?" for something that wants power, a USB DAC
 * among them (2026-11-24: a DAC plugged in while MP4 played was never
 * asked about, so it stayed unpowered). True when one was asked: the
 * caller's screen is to be drawn again and its input is stale. */
bool usb_questions(app *a);
void replug_draw_at(SDL_Renderer *r);
void replug_overlay(SDL_Renderer *r);
void replug_game_tick(void);

#endif
