/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_P2_H
#define ZYPEROS_P2_H

/* Player 2 in a game, the launcher's side: a friend joins the handheld's own
 * Wi-Fi (a USB Wi-Fi adapter makes it, src/hotspot.h) or a computer on the
 * USB cable, opens a page and plays the game as player 2 in the browser.
 *
 *   Diatom      shares the game's frames and reads the guests' buttons
 *               (SETPLAYER2, src/p2share.h)
 *   p2d         serves the page and its WebSocket (src/p2d/main.c), and on
 *               the hotspot the DNS that sends a phone's internet check there
 *   this file   the in-game menu's Player 2 row and page start and stop the
 *               two, with the hotspot or the cable's link, and say when a
 *               player joins or leaves
 *
 * Started from the page over the paused game, on for the rest of the game:
 * Continue goes back to the game with it on. Every end of the game ends it
 * (p2_stop), and so does the page's A.
 *
 * No SDL and no system calls of its own: every process and protocol action
 * goes through p2_ops, which src/main.c fills with the real ones and
 * tools/p2-check.c with fakes that write down the order they were asked in. */

#include <stdbool.h>

/* ---- the systems ---------------------------------------------------------
 *
 * Which systems Player 2 is offered on, by the core that runs the game, with
 * the RetroPad buttons a guest gets (libretro's bit ids, within
 * P2S_PAD_CEILING) and how many play at once, the handheld's own player
 * included. Diatom and p2d take both from SETPLAYER2, and the page builds its
 * pad from the buttons, so a system is added here: one row, and its pad's
 * layout on the page (res/web/play/play.js) if its buttons are not the NES's.
 * The NES's: B SELECT START and the d-pad and A (0x01FD), two players. */
typedef struct {
	const char *core;
	unsigned    buttons;
	int         players;
} p2_system;

/* The row for `core`, NULL when Player 2 is not offered on it */
const p2_system *p2_system_of(const char *core);
/* Offered for a game on `core` on a device that has Player 2 (`device`:
 * plat_player2, the Pixel 2's hotspot and cable link being the ones it was
 * built and tested on) */
bool p2_offered(const char *core, bool device);

typedef enum { P2_OFF, P2_WAITING, P2_CONNECTED, P2_FAILED } p2_state;
typedef enum { P2_HOTSPOT, P2_CABLE, P2_NO_LINK } p2_link;
/* What a notice over the game says: a player joined or left, or Player 2
 * stopped by itself (p2d or the hotspot went away) */
typedef enum { P2_JOINED, P2_LEFT, P2_STOPPED } p2_news;

/* How a friend can reach the handheld now: the cable when a computer is on
 * it, else the hotspot when an adapter is in, else neither. The Pixel 2 has
 * one port, so the two are never both, and a computer that is there is the
 * one the player means. */
p2_link p2_link_now(bool adapter_in, bool computer_on_cable);

/* Diatom's answer to SETPLAYER2, "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"
 * or "PLAYER2\ton=0", read as its key=value pairs in any order: false for a
 * line that is no PLAYER2 answer. `buttons` and `players` may be NULL, and
 * are left alone when the line does not say them. */
bool p2_answer(const char *line, bool *on, unsigned *buttons, int *players);

/* p2d's address and port on each link: the hotspot's (src/captive.h) and the
 * cable's (plastron's S40usbgadget), port 80 so the address needs no colon */
#define P2_HOTSPOT_ADDR "10.42.1.1"
#define P2_CABLE_ADDR   "10.42.0.1"
#define P2_PORT         "80"

/* ---- what it is done with ----------------------------------------------- */

typedef struct {
	/* One protocol line to Diatom, as plat_resident_line sends it */
	bool (*resident)(const char *line);
	/* The next line from Diatom that starts with `prefix`, within `ms`, into
	 * `out`: false when none came. With `ms` 0, only among the lines waiting
	 * already, never waiting for one. Only asked while the game is paused. */
	bool (*answer)(const char *prefix, char *out, unsigned n, unsigned ms);
	/* The handheld's theme now, into WEBTHEME_FILE (src/webtheme.h) */
	bool (*theme)(void);
	/* The hotspot without its own DNS (hotspot_start_dns(false)), its poll
	 * (a hotspot_state), and its stop, which brings the Wi-Fi back */
	bool (*hotspot_start)(void);
	int  (*hotspot_poll)(void);
	void (*hotspot_stop)(void);
	/* The cable's link for this plug (usblink_for_transfer), and as it was
	 * after (usblink_after_transfer) */
	void (*cable_up)(void);
	void (*cable_down)(void);
	/* zyperos.local on the cable (src/mdns.h): false when it could not start */
	bool (*mdns_start)(void);
	void (*mdns_poll)(unsigned now_ms);
	void (*mdns_stop)(void);
	/* p2d: started with `argv` and left running, its pid or -1; a signal to
	 * it; and whether it has ended, reaped then, never waiting */
	long (*spawn)(char *const argv[]);
	void (*signal)(long pid, int sig);
	bool (*ended)(long pid, int *status);
	/* The clock, and a short sleep while p2d goes */
	unsigned (*now)(void);
	void (*sleep)(unsigned ms);
	/* The share's guests (p2share.h): bit k while a browser plays port k+1;
	 * -1 when there is no share, which Diatom takes away only as the game
	 * ends; and the share's file removed, for a Diatom killed before it could
	 * (SIGKILL: the STOP's last step, or the kernel short of memory) */
	int  (*guests)(void);
	void (*unshare)(void);
	/* A notice over the game: `player` (2 is the first guest) joined or
	 * left, or Player 2 stopped (`player` 0) */
	void (*notice)(p2_news what, int player);
	/* The launcher's language now, as p2d's --lang takes it ("pt-BR") */
	const char *(*lang)(void);
	const char *p2d;      /* the binary */
	const char *root;     /* the folder p2d serves play/ and i18n/ from */
} p2_ops;

void p2_use(const p2_ops *ops);

/* ---- on and off --------------------------------------------------------- */

/* Player 2 on for the game running `core`, over `l`: SETPLAYER2 on=1 with the
 * system's buttons and players, taken or refused by Diatom's answer; the
 * theme file; then on the hotspot its start (p2d follows in p2_tick once it
 * is up: p2d's DNS binds the hotspot's address once, at its start), on the
 * cable the link, zyperos.local and p2d. False when it did not start, and the
 * state says P2_FAILED (or P2_OFF for a core or link it cannot be on). A
 * start while on does nothing; one after a failure stops what was left
 * first. Diatom's answers that are still waiting (a stop's or a failure's
 * on=0, which nothing read) are read away before its on=1, so the answer it
 * takes is its own. Takes as long as the hotspot's start takes, up to two
 * seconds. */
bool p2_start(const char *core, p2_link l);

/* From every tick while it is on, in a game and in the screens over its
 * pause: the hotspot coming up and p2d after it, the guests coming and going
 * (a notice for each), zyperos.local, and p2d or the hotspot going away,
 * which is P2_FAILED and a notice that it stopped. The share gone, or p2d
 * ending by itself with 0 (which it does when the share goes), is the game
 * ending, which says nothing. Bounded: never waits, and a failure stops only
 * what stops at once; p2_stop does the rest. */
void p2_tick(unsigned now_ms);

/* The game is over, however it ended: from here the tick does nothing and
 * no notice is said (the share is gone, and a guest's "left" over no game
 * is noise), and p2_stop removes the share's file too. The launcher says it
 * the moment its wait for the game ends, before anything that ticks. */
void p2_game_over(void);

/* Everything off, in the reverse order: the share's file first once the game
 * is over (so p2d tells its guests the game ended, not that Player 2 was
 * turned off), p2d (SIGTERM, SIGKILL after a second), SETPLAYER2 on=0, then
 * the hotspot (which brings the Wi-Fi back, a second or two) or
 * zyperos.local and the cable's link. At every end of a game and from the
 * page; nothing at all when nothing is on. */
void p2_stop(void);

/* The theme changed while it is on: the file again, once, for the page's
 * next load (p2d reads it at each request) */
void p2_theme_changed(void);

/* ---- what the row and the page say -------------------------------------- */

p2_state p2_state_now(void);
/* The link it started (the hotspot asked for, or the cable bound),
 * P2_NO_LINK while off and after a start Diatom refused */
p2_link  p2_link_on(void);
/* On the hotspot, which is not up yet (p2d not started) */
bool     p2_starting(void);
/* The guests as last read, bit k for player k+2 */
unsigned p2_guests(void);
/* How many play at once, the handheld's player too: 2 on the NES; 0 off */
int      p2_players(void);
/* The page's address on the link it is on, the same for every player:
 * http://10.42.1.1/play/ on the hotspot, http://zyperos.local/play/ on the
 * cable; "" while off */
const char *p2_join_url(void);

#endif
