/* SPDX-License-Identifier: GPL-3.0-only */
/* Every question the launcher asks with two answers (DESIGN.md 11.3, "A
 * question with two answers"), in one table: its heading (the action, "Delete
 * Game"), how its words are made, its two answers in their places - the safe
 * one on the left, where the cursor starts - and what each answer does.
 *
 * One table, so one pattern: a screen names its question (CQ_GAME_DELETE)
 * and the words it puts in it (the game's name), and this says the rest. The
 * panel itself is src/ds/confirm.h and the run of it src/main.c's
 * confirm_ask. Plain C over the string tables (src/i18n.h), no SDL, so
 * tools/confirm-check.c holds every question without a screen.
 *
 * My picks in the confirm panels lab (2026-11-22,
 * ~/Documents/ZyperOS/labs/confirm-panels-lab.html): the two answers side by
 * side, Cancel on the left with the cursor on it, the answer that loses
 * something in **warning**, a short heading with the item's name in the
 * question, B's glyph in the answer B gives, a press before the panel goes,
 * each answer a raised slab. */
#ifndef ZYPEROS_CONFIRMQ_H
#define ZYPEROS_CONFIRMQ_H

#include <stdbool.h>
#include <stddef.h>

#include "i18n.h"

typedef enum {
	CQ_WIFI_FORGET,          /* X on a saved network (MENU > Wi-Fi) */
	CQ_WIFI_FORGET_JOINED,   /* ...the network the handheld is on */
	CQ_P8_RESTART_DISPLAY,   /* your PICO-8's Display row, a mode not on */
	CQ_P8_RESTART_BUTTONS,   /* your PICO-8's menu closing after new buttons */
	CQ_P8_RESET,             /* your PICO-8's Reset row, its Reset Game hotkey */
	CQ_GAME_RESET,           /* the Reset Game hotkey over a game */
	CQ_SPLORE_WIFI,          /* Splore with no network and a radio */
	CQ_CHEATS_CRASH,         /* a game that stopped last time with cheats on */
	CQ_CHEATS_FIRST,         /* a game's first cheat while achievements count */
	CQ_HOTKEYS_SWAP,         /* keys another hotkey has */
	CQ_HOTKEYS_RESET_GAME,   /* Reset Hotkeys, Applies To This Game */
	CQ_HOTKEYS_RESET_SYSTEM, /* ...a system */
	CQ_HOTKEYS_RESET_EVERY,  /* ...Every Game, or MENU > System > Hotkeys */
	CQ_STORE_UNUSED_ART,     /* Storage's Unused Art */
	CQ_STORE_OLD_STATES,     /* Storage's Old Save States */
	CQ_STORE_SYS_SNAPS,      /* a system's page of Storage, Delete Video Snaps */
	CQ_STORE_MEDIA_FOLDER,   /* an album, a book, a folder of videos */
	CQ_STORE_MEDIA_FILE,     /* a song, a book, a video */
	CQ_STORE_ORPHAN_PORT,    /* a port folder whose script is not on the card */
	CQ_GAME_DELETE,          /* Delete Game */
	CQ_PORT_DELETE,          /* Delete Game on a PortMaster port */
	CQ_ROLLBACK,             /* System Update's Roll Back */
	CQ_COLLECT_DELETE,       /* Delete Collection */
	CQ_SHOT_DELETE,          /* X on a screenshot */
	CQ_MEDIA_VIDEO_DELETE,   /* a game's Video page, Delete */
	CQ_SGDB_UNLINK,          /* SteamGridDB's Unlink */
	CQ_SS_UNLINK,            /* ScreenScraper's Unlink */
	CQ_SNAPS_DELETE_ALL,     /* the Video Snaps page's Delete */
	CQ_BUTTONS_RESET_GAME,   /* Reset Buttons, Applies To This Game */
	CQ_BUTTONS_RESET_ALL,    /* ...All <System> */
	CQ_USB_OFFER,            /* Zyper's one-time offer of Automatic */
	CQ_RA_SIGN_OUT,          /* RetroAchievements' Sign Out */
	CQ_DL_DELETE_VIDEOS,     /* Downloads > On the Card's Videos */
	CQ_DL_DELETE_CHEATS,     /* ...Cheats */
	CQ_DL_DELETE_SETS,       /* ...Achievements */
	CQ_DL_SYS_VIDEOS,        /* Downloads' Systems, X on a Videos cell */
	CQ_DL_SYS_SETS,          /* ...an Achievements cell */
	CQ_DL_SYS_CHEATS,        /* ...a Cheats cell */
	CQ_BT_FORGET,            /* X on a paired headset (MENU > Bluetooth) */
	CQ_LINK_GAMBATTE,        /* the Link Cable row on a Game Boy game on mGBA */
	CQ_STORE_DS_COPIES,      /* Storage's Unpacked DS Games (src/dscache.h) */
	CQ_COUNT
} cq_id;

/* What an answer does, beside its words */
enum {
	/* The right answer loses what no press brings back (files, a password,
	 * play since a save, your own buttons): its words in **warning** */
	CQ_LOSES  = 1u << 0,
	/* B gives no answer: it leaves with nothing done, and no answer carries
	 * its glyph. The two that both go on (Splore's, the cheats'), and the
	 * two where the left answer itself does something that stays (the USB
	 * offer's Keep Asking, saved for good; PICO-8's Later, the cart going on
	 * with the menu closed). Otherwise B gives the left answer, Cancel. */
	CQ_B_NONE = 1u << 1,
	/* The question's first conversion is a count (%d), its plural form
	 * picked by it (TN) */
	CQ_COUNTED = 1u << 2,
};

typedef struct {
	const char *name;   /* its shot's name (--ask NAME) and the check's */
	str_id heading;     /* the action, in Title Case: never a question */
	str_id text;        /* the question's words, a format */
	str_id text2;       /* what goes with it, joined after (panel.join), or S_COUNT */
	str_id left;        /* the safe answer, where the cursor starts */
	str_id right;       /* the action */
	unsigned flags;
} cq_def;

const cq_def *cq_of(cq_id q);
/* By its name; CQ_COUNT for none */
cq_id cq_named(const char *name);

/* The question's words with the screen's in it: `text` formatted, and
 * `text2`'s after it, joined as panel.join joins two sentences. A question
 * with a `text2` takes only strings (const char *), `text`'s first and then
 * `text2`'s, as many as their conversions say; one without, whatever its
 * format takes. */
void cq_text(cq_id q, char *out, size_t n, ...);
/* The two sentences a question of two was made of by cq_text, lately, for
 * the host's check of what is drawn (each is its own string there) */
bool cq_parts(const char *whole, const char **one, const char **two);

/* The answer a question was given, as confirm_ask says it */
enum { CQ_NONE = 0, CQ_LEFT = 1, CQ_RIGHT = 2 };

/* ---- the keys (DESIGN.md 11.3): the run's rule, without a screen ---------- */

/* What one pass's input is to the panel: a direction (fresh or held), A, B,
 * MENU or POWER. One at a time, in that order of strength. */
typedef enum {
	CQK_NONE, CQK_LEFT, CQK_RIGHT, CQK_UP, CQK_DOWN, CQK_A, CQK_B, CQK_MENU, CQK_POWER
} cq_key;

typedef struct {
	int sel;        /* the answer under the cursor: 0 left, 1 right */
	bool side;      /* side by side (Left and Right move) or stacked (Up and Down) */
} cq_state;

typedef struct {
	int sel;        /* where the cursor is after */
	bool moved;     /* it moved: a step sound */
	bool edge;      /* a press that goes nowhere: the edge sound */
	int answer;     /* CQ_NONE while it stays; else the answer, and the panel goes */
	int press;      /* the answer that presses before it goes, -1 for none */
	bool done;      /* the panel goes (an answer, or left with nothing) */
} cq_step;

/* The cursor starts on the left answer, always: the safe one */
cq_state cq_start(bool side);
/* One key. `fresh`: pressed this pass, not a held repeat. A press on the
 * last answer goes round to the first; a held one stops at the end (the
 * caller's step sound plays edge once for it, step_sound). B gives the left
 * answer, pressed, unless CQ_B_NONE; MENU and POWER leave at once. */
cq_step cq_press(const cq_def *d, cq_state s, cq_key k, bool fresh);

#endif
