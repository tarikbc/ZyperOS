/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHOTS_H
#define ZYPEROS_SHOTS_H

/* The --shot harness: one frame of the launcher drawn on a host and written
 * out, so every screen can be looked at, checked and compared without a
 * device. Its flags (each described where it is defined, src/shots.c) are
 * set by main()'s argument loop in src/main.c; shot_compose and the shot
 * drawers draw them, and take_shot writes the picture. */
#include "app.h"
#include "panel.h"

/* What main.c uses of it; each is described where it is defined */
extern int shot_screen;
extern const char *shot_view;
extern const char *shot_folder;
extern const char *shot_game;
extern const char *shot_shelf_press;
extern int shot_menu_sel;
extern int shot_menu;
extern int shot_menu_steps;
extern int shot_tab_from;
extern int shot_tab_ms;
extern const char *shot_look;
extern const char *shot_look_page;
extern int shot_homesys;        /* --homesys-page ROW: MENU > Home Systems' page, or -1 */
extern int shot_lights;         /* --lights-page ROW: MENU > Side Lights' page, or -1 */
extern int shot_look_row;
extern int shot_kb_layer;
extern int shot_kb;
extern int shot_kb_row;
extern int shot_kb_col;
extern const char *shot_notice;
extern const char *shot_notice_head;
extern const char *shot_notice_badge;
extern int shot_notice_target;
extern int shot_notice_value;
extern nq_kind shot_notice_kind;
extern int shot_notice_mark;
extern const char *shot_popup;
extern int shot_popup_half;
extern int shot_lowbatt;
extern int shot_lowbatt_kind;
extern int shot_heatpop;
extern int shot_ranews;
extern const char *shot_upnews;
extern int shot_charging;
extern int shot_charging_kind;
extern int shot_cheevos;
extern int shot_cheevos_sel;
extern int shot_cheevo_card;
extern int shot_ingame;
extern float shot_sysmove;
extern float shot_gamemove;
extern int shot_at;
extern int shot_launch;
extern const char *shot_cheevos_game;
extern const char *shot_badge_name;
extern const char *shot_badge_set;
extern int shot_syn;
extern int shot_info;
extern int shot_delete_ask;
/* Collections (src/collect_screen.h): Add to Collection's list for the game
 * under the cursor, the cursor on that row (-1 none), and Delete
 * Collection's question for the collection the shelf is on */
extern int shot_collect_page, shot_collect_delete;
/* --collect-said K: one of Collections' messages (0 to 2) */
extern int shot_collect_said;
/* --shots-delete: Delete Screenshot's question on the Screenshots shelf
 * (src/shots_screen.h) */
extern int shot_shots_delete;
/* --shots-said: the message when the card would not let a shot go */
extern int shot_shots_said;
/* --shots-delete-now: Delete Screenshot's yes on the shot under the cursor,
 * and the shot is the shelf after it (tools/homesys-groups-check.py) */
extern int shot_shots_delete_now;
/* --zip-said: the message for a zip Diatom cannot unpack (src/gamefile.h) */
extern int shot_zip_said;
extern int shot_gm_said;
extern int shot_delete_now;
extern int shot_controls;
/* --hotkeys ROW: the Hotkeys list (src/hotkeys_screen.h), the cursor on an
 * action's row (0 to 8) or HKR_* id; --hk-game, the in-game one for the
 * focused game (Applies To first); --hk-scope N its scope; --hk-capture 1
 * the capture screen waiting, 2 with SELECT and --hk-key K down; --hk-ask
 * 1 the swap question for K, 2 Reset Hotkeys', 3 Reset Game's */
extern int shot_hotkeys, shot_hk_scope, shot_hk_capture, shot_hk_key;
extern float shot_menu_lock;
extern bool shot_menu_notice;
extern bool shot_hk_game;
extern int shot_hk_ask;
extern int shot_sgdb;
extern int shot_scraper;
extern int shot_about;
extern float shot_about_scroll;
extern int shot_language;
extern int shot_music_page;
extern int shot_music_steps;
extern int shot_update;
extern int shot_hare;
extern int shot_usb;
extern int shot_datetime, shot_datetime_mon;
extern int shot_dev;
extern bool shot_usb_dev;
extern int shot_power;
extern int shot_usbdrive;
extern int shot_xfer_ask;
extern bool shot_xfer_game;
extern int shot_hot_in;
extern int shot_hot;
extern const char *shot_hot_ssid;
extern const char *shot_hot_pass;
extern const char *shot_hot_phones;
extern const char *shot_hot_now;
extern const char *shot_hare_addr;
extern const char *shot_hare_who;
extern const char *shot_hare_head;
extern const char *shot_hare_move;
extern const char *shot_steps_way;
extern const char *shot_steps_addr;
extern const char *shot_steps_state;
extern bool shot_steps_noname;
extern const char *shot_wait;
extern const char *shot_wait_msg;   /* NULL: library.scanning's */;
extern int shot_osd_kind;
extern int shot_osd_val; /* --osd volume|brightness LEVEL */;
extern int shot_osd_boost; /* ... +CELLS, the boost open past the top */
extern int shot_tour;
extern int shot_tour_ms;
extern int shot_tour_done;
extern int shot_tour_wrong_ms;
extern int shot_tour_wrong;
extern int shot_tour_pick_ms;
extern int shot_tour_pick;
extern bool shot_tour_music;
extern int shot_fn_volume;
extern int shot_theme;
extern int shot_home;
extern const char *shot_lang;
extern int shot_cheer;
extern const char *shot_songpop;
extern int shot_songpop_ms;
extern const char *shot_song;
extern bool shot_song_paused;
extern int shot_flip;
extern int shot_failed;
extern const char *shot_portload;
extern int shot_poweroff;
extern const char *shot_kb_text;
extern const char *shot_kb_title;
extern int shot_kb_ss_user;      /* --kb-ss-user: the ScreenScraper sign-in's title */
extern int shot_slot_sel;
extern int shot_slots;
extern int shot_jump;
extern int shot_np_paused;
extern int shot_np;
extern int shot_np_track;
extern int shot_locked;
extern int shot_np_bench;
extern int shot_video, shot_video_w, shot_video_h, shot_video_trouble;
extern float shot_video_k, shot_video_grow;
extern float shot_np_at;
extern const char *shot_mode;
extern float shot_slot_aspect;
extern int shot_slot_mon, shot_slot_hour, shot_slot_save;
extern int shot_gamemenu_tab;
extern int shot_gamemenu_sel;
extern int shot_gamemenu;
extern const char *shot_backdrop;
extern bool shot_gm_all;
extern bool shot_gm_ask;
extern bool shot_gm_pick;
extern int shot_gm_keys;
extern int shot_gm_from;
extern const char *shot_gm_press;
extern int shot_gamecontrols;
extern const char *shot_gamecontrols_tag;
extern const char *shot_handheld_box;
extern const char *shot_controls_fit;
/* --ask NAME [ITEM]: a question with two answers (src/confirmq.h), its
 * words the shot's own with ITEM for the name in it; --confirm-fit: every
 * question in every language laid out, a line each on stderr */
extern const char *shot_ask;
extern const char *shot_ask_item;
extern bool shot_confirm_fit;
extern int shot_key;
extern int shot_gm_choose;     /* --gm-choose */
extern int shot_gm_restart;    /* --gm-restart */
extern int shot_gm_reset;      /* --gm-reset */
extern int shot_hud_page;      /* --hud-page ROW, -1 for none */
extern bool shot_hud_items;    /* --hud-items */
extern int  shot_hud_pick;     /* --hud-pick ROW */
extern const char *shot_hud_shows; /* --hud-shows WORDS */
extern bool shot_hud_alt;      /* --hud-alt */
extern bool shot_cheats;       /* --cheats: the cursor's game runs with its cheats */
extern int  shot_cheats_page;  /* --cheats-page ROW: its list, -1 for none */
extern bool shot_cheats_ask;   /* --cheats-ask: the first cheat's question over it */
extern const char *shot_cheats_query; /* --cheats-query WORD: a search's word */
extern bool shot_cheats_on_first;     /* --cheats-on-first: X pressed */
extern bool shot_cheats_note;  /* --cheats-note: the top line, as signed in */
extern bool shot_cheats_crash; /* --cheats-crash: the crash question over the shelf */
extern int shot_pick_at;       /* --pick-at K */
extern const char *shot_pick_en; /* --pick-en WORDS */
extern const char *shot_expect_en; /* --expect-en WORDS|WORDS?|... */
extern int shot_rows_report;   /* --rows-report */
extern int shot_hover_sweep;   /* --hover-sweep */
extern const char *shot_menu_label;
bool shot_status_parse(const char *spec);
void shot_motion(app *a);
void shot_portload_take(app *a);
void take_shot(app *a);

#endif
