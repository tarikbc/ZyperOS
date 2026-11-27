/* SPDX-License-Identifier: GPL-3.0-only */
#include "link_screen.h"

#include "aout_route.h"
#include "app.h"
#include "ctl_channel.h"
#include "platform.h"
#include "fileio.h"
#include "hotspot.h"
#include "ingame_screen.h"
#include "keyhints.h"
#include "levels.h"
#include "linknet.h"
#include "linkpanel.h"
#include "panel.h"
#include "uisound.h"
#include "wifi.h"
#include "db.h"
#include "usbpeer.h"
#include "usblink.h"
#include "zyperdraw.h"

#include <SDL.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef ZYPEROS_DESK
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#endif

/* ---------- the Game Boy's Link Cable --------------------------------------
 *
 * The in-game menu's Link Cable row on a Game Boy or Game Boy Color game on
 * Gambatte opens this page over the paused game (src/link.h, DESIGN.md
 * 11.72). Off, it asks Host or Join in tiles, as the USB question does. Host
 * makes this handheld's own network and waits for the other one; Join looks
 * for a host and lists what it finds. Linked, the page closes and the game
 * goes on, and a notice says so on both. B stops what it started and goes
 * back to the tiles; B on the tiles closes the page. Every end of the game
 * ends the link (link_game_end). */

#define WLAN_ADDRESS "/sys/class/net/wlan0/address"
/* With no adapter in, the handheld's own serial names it on the cable: the
 * SoC's, as the boot loader puts it in the device tree, else the card's */
static const char *const SERIALS[] = { "/sys/firmware/devicetree/base/serial-number",
                                       "/sys/block/mmcblk0/device/serial" };

/* --link KIND: the page as a shot draws it, and the row's value with
 * --gamemenu */
bool      g_shot_link;
link_shot g_shot_link_kind;

static const char *const SHOT_NAMES[LINK_SHOT_KINDS] = {
	"tiles", "no-adapter", "starting", "waiting", "looking", "found", "joining",
	"linked", "lost", "failed", "stopping",
	"failed-network", "failed-join", "failed-busy", "failed-game", "failed-code",
	"cable-tiles", "cable-waiting", "cable-joining", "failed-cable",
};

bool link_shot_parse(const char *kind)
{
	int i;

	for (i = 0; kind && i < LINK_SHOT_KINDS; i++)
		if (!strcmp(kind, SHOT_NAMES[i])) {
			g_shot_link = true;
			g_shot_link_kind = (link_shot)i;
			return true;
		}
	return false;
}

/* What the page shows, gathered from src/link.c or made up by a shot */
typedef struct {
	link_state   st;
	link_failure why;
	bool         stopping;     /* the Wi-Fi coming back */
	const char  *self, *peer, *joining;
	const char  *code;         /* the host's, while it hosts */
	const char  *hosts[LINKPANEL_HOSTS];
	int          nhosts;
	bool         scanned;
	link_medium  medium;       /* what it is on, or what a start would take */
} link_view;

#ifdef ZYPEROS_DESK
/* Desk mode as the cable's joiner, for a test with one Pixel 2: with
 * ZYPEROS_DESK_LINK_CABLE=1 this computer, on the Pixel 2's cable network
 * (USB File Transfer's 10.42.0.1, this end 10.42.0.2), is the other end of
 * the cable. Its port is no USB host and has no usbrole: the join takes
 * the address this computer has toward 10.42.0.1, and the rest is
 * src/link.c's own, the hello, the beat, the options to Diatom. It cannot
 * host (it has no gadget). */
static bool desk_cable(void)
{
	const char *e = getenv("ZYPEROS_DESK_LINK_CABLE");

	return e && *e == '1';
}

/* The host it meets: 10.42.0.1, or ZYPEROS_DESK_LINK_HOST's stand-in */
static const char *desk_cable_host(void)
{
	const char *e = getenv("ZYPEROS_DESK_LINK_HOST");

	return e && *e ? e : LINK_CABLE_ADDR;
}

/* The joiner's beat socket, on ZYPEROS_DESK_LINK_PORT instead of 56399 for
 * a stand-in host on this same computer (two sockets cannot both have one
 * address's 56399); the stand-in answers there. Never on a handheld. */
static int lop_udp_open(const char *addr, int port)
{
	const char *e = getenv("ZYPEROS_DESK_LINK_PORT");

	/* Desk mode never hosts (lop_cable_host_up): a 56399 here is the joiner's */
	if (desk_cable() && e && *e && port == LINK_BEAT_PORT)
		port = atoi(e);
	return linknet_open(addr, port);
}

static int desk_cable_addr(char *addr, unsigned n)
{
	struct sockaddr_in a, me;
	socklen_t ml = sizeof me;
	int s = socket(AF_INET, SOCK_DGRAM, 0), ok;

	if (s < 0) return -1;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(LINK_BEAT_PORT);
	inet_pton(AF_INET, desk_cable_host(), &a.sin_addr);
	/* No datagram goes: a connect only picks the route and its address */
	ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0 &&
	     getsockname(s, (struct sockaddr *)&me, &ml) == 0 &&
	     inet_ntop(AF_INET, &me.sin_addr, addr, n) != NULL;
	close(s);
	return ok ? 1 : 0;
}
#else
#define lop_udp_open linknet_open
static bool desk_cable(void) { return false; }
static int desk_cable_addr(char *addr, unsigned n) { (void)addr; (void)n; return -1; }
#endif

bool link_desk_holds_idle(void) { return desk_cable(); }

/* Another device on the USB-C port, for the cable link: a plug that wants
 * power and gives none (usbrole may make the port its host), the port a
 * host already, or a port that powers this one with data. Looked at five
 * times a second at most: two small files. */
static bool cable_peer(void)
{
	static unsigned at;
	static bool was;
	unsigned now = plat_now_ms();
	usb_power p;

	if (desk_cable()) return true;
	if (at && now - at < 200u) return was;
	at = now ? now : 1;
	was = (plat_usb_power(&p) && p.known && ((p.plug && p.ask) || p.host)) ||
	      plat_usb_peer() == USB_PEER_COMPUTER;
	return was;
}

/* What a start takes now: the adapter's Wi-Fi, else the cable */
static link_medium medium_now(void)
{
	link_medium m = link_medium_now();

	return m != LINK_NONE ? m : link_medium_of(hotspot_available(), cable_peer());
}

static void view_now(link_view *v, bool stopping)
{
	int i;

	memset(v, 0, sizeof *v);
	v->st = link_state_now();
	v->why = link_failure_now();
	v->stopping = stopping;
	v->self = link_self_name();
	v->code = link_code();
	v->peer = link_peer_name();
	v->joining = link_joining_name();
	v->nhosts = link_host_count();
	if (v->nhosts > LINKPANEL_HOSTS) v->nhosts = LINKPANEL_HOSTS;
	for (i = 0; i < v->nhosts; i++) v->hosts[i] = link_host_name(i);
	v->scanned = link_scanned();
	v->medium = medium_now();
}

static void view_shot(link_view *v)
{
	static const char *const HOSTS[] = { "Pixel 2 0C11", "Pixel 2 B3E4" };

	memset(v, 0, sizeof *v);
	v->medium = LINK_WIFI;
	v->self = "Pixel 2 7F2A";
	v->code = "4821";
	v->peer = v->joining = "Pixel 2 0C11";
	switch (g_shot_link_kind) {
	case LINK_SHOT_STARTING: v->st = LINK_HOST_STARTING; break;
	case LINK_SHOT_WAITING:  v->st = LINK_HOST_WAITING; break;
	case LINK_SHOT_LOOKING:  v->st = LINK_JOIN_LOOKING; break;
	case LINK_SHOT_FOUND:
		v->st = LINK_JOIN_LOOKING;
		v->hosts[0] = HOSTS[0];
		v->hosts[1] = HOSTS[1];
		v->nhosts = 2;
		v->scanned = true;
		break;
	case LINK_SHOT_JOINING:  v->st = LINK_JOIN_ASKING; break;
	case LINK_SHOT_LINKED:   v->st = LINK_LINKED; break;
	case LINK_SHOT_LOST:     v->st = LINK_LOST; break;
	case LINK_SHOT_FAILED:   v->st = LINK_FAILED; v->why = LINK_FAIL_ANSWER; break;
	case LINK_SHOT_STOPPING: v->st = LINK_HOST_WAITING; v->stopping = true; break;
	case LINK_SHOT_FAILED_NETWORK: v->st = LINK_FAILED; v->why = LINK_FAIL_NETWORK; break;
	case LINK_SHOT_FAILED_JOIN:    v->st = LINK_FAILED; v->why = LINK_FAIL_JOIN; break;
	case LINK_SHOT_FAILED_CODE:    v->st = LINK_FAILED; v->why = LINK_FAIL_CODE; break;
	case LINK_SHOT_FAILED_BUSY:    v->st = LINK_FAILED; v->why = LINK_FAIL_BUSY; break;
	case LINK_SHOT_FAILED_GAME:    v->st = LINK_FAILED; v->why = LINK_FAIL_GAME; break;
	case LINK_SHOT_CABLE_TILES:    v->medium = LINK_CABLE; v->st = LINK_OFF; break;
	case LINK_SHOT_CABLE_WAITING:  v->medium = LINK_CABLE; v->st = LINK_HOST_WAITING; v->code = ""; break;
	case LINK_SHOT_CABLE_JOINING:  v->medium = LINK_CABLE; v->st = LINK_JOIN_JOINING; break;
	case LINK_SHOT_FAILED_CABLE:   v->medium = LINK_CABLE; v->st = LINK_FAILED; v->why = LINK_FAIL_CABLE; break;
	default:                 v->st = LINK_OFF; break;
	}
}

link_state link_row_state(void)
{
	link_view v;

	if (!g_shot_link) return link_state_now();
	view_shot(&v);
	return v.st;
}

bool link_row_adapter(void)
{
	if (g_shot_link) return g_shot_link_kind != LINK_SHOT_NO_ADAPTER;
	return medium_now() != LINK_NONE;
}

static bool view_tiles(const link_view *v)
{
	return !v->stopping && (v->st == LINK_OFF || v->st == LINK_FAILED || v->st == LINK_LOST);
}

/* ---- the page's rows ------------------------------------------------------- */

typedef struct {
	char big[128], line[2][160];
	char legend[256];
} link_bufs;

/* The line under the tiles: what went wrong, or what both need */
static const char *tiles_line(const link_view *v)
{
	if (v->st == LINK_LOST) return T(S_LINK_LOST_LINE);
	if (v->st == LINK_FAILED) {
		switch (v->why) {
		case LINK_FAIL_JOIN:   return T(S_LINK_FAIL_JOIN);
		case LINK_FAIL_CODE:   return T(S_LINK_FAIL_CODE);
		case LINK_FAIL_ANSWER: return T(S_LINK_FAIL_ANSWER);
		case LINK_FAIL_BUSY:   return T(S_LINK_FAIL_BUSY);
		case LINK_FAIL_GAME:   return T(S_LINK_FAIL_GAME);
		case LINK_FAIL_CABLE:  return T(S_LINK_FAIL_CABLE);
		default:               return T(S_LINK_FAIL_NETWORK);
		}
	}
	/* On the cable: no adapter and no code, and Join gives power */
	return T(v->medium == LINK_CABLE ? S_LINK_CABLE_LINE : S_LINK_ADAPTERS);
}

/* Host and Join, as the tiles take them: the two, a line under the heading
 * (none), the line under the tiles, the legend */
static int tiles_rows(const link_view *v, menu_row *rows, link_bufs *b)
{
	key_hint hh[3] = { { KEYHINT_LEFT_RIGHT, T(S_KEYHINT_MOVE) }, { "A", T(S_KEYHINT_CHOOSE) },
	                   { "B", T(S_KEYHINT_CLOSE_TILES) } };

	keyhints_pack(hh, 3, b->legend, sizeof b->legend);
	rows[0] = (menu_row){ T(S_LINK_HOST), T(S_LINK_HOST_LINE), true };
	rows[1] = (menu_row){ T(S_LINK_JOIN), T(S_LINK_JOIN_LINE), true };
	rows[2] = MENU_NOTE(NULL);
	rows[3] = MENU_NOTE(tiles_line(v));
	rows[4] = MENU_NOTE(b->legend);
	return 5;
}

/* The panel's rows (PANEL_LINK, link_panel reads them back): the large
 * line, three lines under it, the dots (any label for on), the host's code
 * (NULL for none), the hosts, and the legend last */
static int panel_rows(const link_view *v, menu_row *rows, link_bufs *b)
{
	const char *lines[3] = { NULL, NULL, NULL }, *code = NULL;
	bool dots = false;
	int n = 0, i;

	b->big[0] = b->legend[0] = '\0';
	if (v->stopping) {
		snprintf(b->big, sizeof b->big, "%s", T(S_LINK_STOPPING));
		dots = true;
	} else if (v->st == LINK_HOST_STARTING || v->st == LINK_HOST_WAITING) {
		snprintf(b->big, sizeof b->big, "%s",
		         T(v->st == LINK_HOST_STARTING ? S_LINK_STARTING : S_LINK_WAITING));
		snprintf(b->line[0], sizeof b->line[0], T(S_LINK_THIS_ONE), v->self);
		lines[0] = b->line[0];
		lines[1] = T(v->medium == LINK_CABLE ? S_LINK_THEN_JOIN_CABLE : S_LINK_THEN_JOIN);
		/* The code the other one types, once there is one */
		code = v->code && *v->code ? v->code : NULL;
		dots = true;
		snprintf(b->legend, sizeof b->legend, "%s", b_legend(S_KEYHINT_STOP));
	} else if (v->st == LINK_JOIN_LOOKING && v->nhosts) {
		key_hint hh[3] = { { "^ v", T(S_KEYHINT_MOVE) }, { "A", T(S_KEYHINT_CHOOSE) },
		                   { "B", T(S_KEYHINT_STOP) } };

		snprintf(b->big, sizeof b->big, "%s", T(S_LINK_PICK));
		keyhints_pack(hh, 3, b->legend, sizeof b->legend);
	} else if (v->st == LINK_JOIN_LOOKING) {
		snprintf(b->big, sizeof b->big, "%s", T(S_LINK_LOOKING));
		lines[0] = T(S_LINK_THEN_HOST);
		dots = true;
		snprintf(b->legend, sizeof b->legend, "%s", b_legend(S_KEYHINT_STOP));
	} else if ((v->st == LINK_JOIN_JOINING || v->st == LINK_JOIN_ASKING) && v->medium == LINK_CABLE) {
		/* The cable: no list and no name yet, and this one gives power */
		snprintf(b->big, sizeof b->big, "%s", T(S_LINK_JOINING_CABLE));
		lines[0] = T(S_LINK_POWER_LINE);
		dots = true;
		snprintf(b->legend, sizeof b->legend, "%s", b_legend(S_KEYHINT_STOP));
	} else if (v->st == LINK_JOIN_JOINING || v->st == LINK_JOIN_ASKING) {
		snprintf(b->big, sizeof b->big, T(S_LINK_JOINING), v->joining);
		dots = true;
		snprintf(b->legend, sizeof b->legend, "%s", b_legend(S_KEYHINT_STOP));
	} else if (v->st == LINK_LINKED) {
		key_hint hh[2] = { { "A", T(S_KEYHINT_STOP) }, { "B", T(S_KEYHINT_CLOSE) } };

		snprintf(b->big, sizeof b->big, T(S_LINK_LINKED_WITH), v->peer);
		lines[0] = T(S_LINK_PAUSE_LINE);
		keyhints_pack(hh, 2, b->legend, sizeof b->legend);
	}
	rows[n++] = (menu_row){ b->big, NULL, false };
	for (i = 0; i < 3; i++) rows[n++] = (menu_row){ lines[i], NULL, false };
	rows[n++] = (menu_row){ dots ? "..." : NULL, NULL, false };
	rows[n++] = (menu_row){ code, NULL, false };
	for (i = 0; i < v->nhosts && v->st == LINK_JOIN_LOOKING && !v->stopping; i++)
		rows[n++] = (menu_row){ v->hosts[i], NULL, true };
	rows[n++] = MENU_NOTE(b->legend);
	return n;
}

void link_panel(app *a, const char *heading, const menu_row *rows, int n, int sel,
                unsigned accent, mm_pose pose)
{
	bool posed = pose.scale != 1.0f || pose.alpha != 1.0f, lifted = false;
	linkpanel p;
	SDL_Rect q;
	int i;

	if (n < 7) return;
	memset(&p, 0, sizeof p);
	p.heading = heading;
	p.big = rows[0].label;
	for (i = 0; i < LINKPANEL_LINES; i++) p.lines[i] = rows[1 + i].label;
	p.dots = rows[4].label != NULL;
	p.code = rows[5].label;
	for (i = 6; i < n - 1 && p.nhosts < LINKPANEL_HOSTS; i++) p.hosts[p.nhosts++] = rows[i].label;
	p.sel = sel;
	p.legend = rows[n - 1].label ? rows[n - 1].label : "";
	q = linkpanel_rect(&p);
	if (posed) {
		SDL_FRect f = panel_scaled(&q, pose.scale);
		SDL_Rect fq = { (int)(f.x + 0.5f), (int)(f.y + 0.5f), (int)(f.w + 0.5f), (int)(f.h + 0.5f) };

		ui_lift_k(a->r, &fq, accent, UI_LIFT_PANEL, pose.alpha);
		lifted = true;
		posed = panel_pass_begin(a);
	}
	linkpanel_draw(a->r, &p, !lifted, anim_now());
	if (posed) panel_pass_end(a, &q, pose);
}

static const int MARKS[2] = { UI_GLYPH_BEACON, UI_GLYPH_SEARCH };

/* The page as the view says, through panel_show: the tiles or the panel */
static void show(app *a, const link_view *v, int sel)
{
	menu_row rows[6 + LINKPANEL_HOSTS + 1];
	link_bufs b;
	int n;

	if (view_tiles(v)) {
		n = tiles_rows(v, rows, &b);
		tiles_show(a, PANEL_TILES, T(S_LINK_ROW), rows, n, MARKS, 2, sel);
	} else {
		n = panel_rows(v, rows, &b);
		panel_show(a, PANEL_LINK, T(S_LINK_ROW), rows, n, sel, 0, a->tint, NULL, false, 0, NULL);
	}
}

void link_shot_draw(app *a, SDL_Texture *bg)
{
	link_view v;

	view_shot(&v);
	paused_backdrop(a, bg, 1.0f);
	show(a, &v, 0);
}

/* One frame of the page, shown before something that blocks: the
 * hotspot's start (the supplicant stops), and a stop that brings the Wi-Fi
 * back */
static void page_frame(app *a, SDL_Texture *bg, const link_view *v, int sel)
{
	paused_backdrop(a, bg, 1.0f);
	show(a, v, sel);
	plat_present();
}

/* Everything the link has up, off; the Wi-Fi back said on the page while it
 * comes */
static void stop_shown(app *a, SDL_Texture *bg)
{
	if (link_owes_wifi()) {
		link_view v;

		view_now(&v, true);
		page_frame(a, bg, &v, 0);
	}
	link_stop();
}

/* The title of the game playing, for the other one's log */
static const char *game_title(app *a)
{
	sysview *sv = &a->view[a->sys_cursor];

	if (sv->list.count <= 0 || sv->cursor < 0 || sv->cursor >= sv->list.count) return "";
	return sv->list.items[sv->cursor].title;
}

/* This handheld's address for its names: the adapter's, else one made from
 * its serial (link_mac_or_serial), so the cable's host is no "Pixel 2 0000" */
static void self_address(char *mac, unsigned n)
{
	char got[32] = "", serial[64];
	unsigned i;

	file_head(WLAN_ADDRESS, got, sizeof got);
	if (link_mac_or_serial(got, NULL, mac, n)) return;
	for (i = 0; i < sizeof SERIALS / sizeof SERIALS[0]; i++) {
		serial[0] = '\0';
		file_head(SERIALS[i], serial, sizeof serial);
		if (link_mac_or_serial(NULL, serial, mac, n)) return;
	}
	mac[0] = '\0';
	/* Desk mode as the cable's joiner: "Pixel 2 DE5C" on the other one */
	if (desk_cable()) link_mac_or_serial(NULL, "de5c", mac, n);
}

/* This handheld's adapter names it and its network: read as the page
 * opens, since an adapter can be plugged in at any time */
static void name_from_adapter(void)
{
	char mac[32] = "";

	if (link_state_now() != LINK_OFF) return;
	self_address(mac, sizeof mac);
	link_use(NULL, mac);
}

bool link_page(app *a, SDL_Texture *bg)
{
	int lv, sel = 0, host_sel = 0;
	bool done = false, linked = false;
	link_state was = link_state_now();
	float k;

	name_from_adapter();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	lv = panel_open();
	while (!done && !want_quit) {
		link_view v;
		link_state st;

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (menu_leaving(a)) done = true;
		levels_keys(&a->in, BOOST_HIDE);
		/* Power stops the game from here, as from the in-game menu's
		 * other pages; the game's end stops the link */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = true;
		}
		view_now(&v, false);
		st = v.st;
		/* Linked while the page waited: it closes, and the game goes on */
		if (st == LINK_LINKED && was != LINK_LINKED) {
			uisound_play(DS_SOUND_ACCEPT);
			linked = done = true;
			break;
		}
		was = st;
		if (!done && view_tiles(&v)) {
			bool left = in_repeat(&a->in, IN_LEFT), right = in_repeat(&a->in, IN_RIGHT);

			if (left || right) {
				moved_sound(a, left ? IN_LEFT : IN_RIGHT);
				sel = !sel;
			}
			if (a->in.pressed[IN_ACCEPT]) {
				uisound_play(DS_SOUND_ACCEPT);
				/* What a lost link or a failure left goes first */
				if (st != LINK_OFF) stop_shown(a, bg);
				if (v.medium == LINK_CABLE) {
					/* The cable: Host binds this end's network, Join makes
					 * this port the host and gives the other power */
					if (!(sel == 0 ? link_host_cable(game_title(a)) : link_join_cable(game_title(a))))
						uisound_play(DS_SOUND_DENY);
				} else if (v.medium == LINK_NONE) {
					uisound_play(DS_SOUND_DENY);
				} else if (sel == 0) {
					link_view s;

					view_now(&s, false);
					s.st = LINK_HOST_STARTING;
					page_frame(a, bg, &s, 0);
					if (!link_host(game_title(a))) uisound_play(DS_SOUND_DENY);
				} else if (!link_join_look(game_title(a))) {
					uisound_play(DS_SOUND_DENY);
				}
				host_sel = 0;
			} else if (a->in.pressed[IN_BACK]) {
				done = true;
			}
		} else if (!done) {
			if (st == LINK_JOIN_LOOKING && v.nhosts) {
				bool up = in_repeat(&a->in, IN_UP), down = in_repeat(&a->in, IN_DOWN);

				if (host_sel >= v.nhosts) host_sel = v.nhosts - 1;
				if (up || down) {
					moved_sound(a, up ? IN_UP : IN_DOWN);
					host_sel = (host_sel + (up ? -1 : 1) + v.nhosts) % v.nhosts;
				}
				if (a->in.pressed[IN_ACCEPT]) {
					char title[128], typed[24] = "";

					uisound_play(DS_SOUND_ACCEPT);
					/* The code the host's page shows, typed here: the
					 * network's password, new each time it hosts */
					snprintf(title, sizeof title, T(S_LINK_CODE_ASK), v.hosts[host_sel]);
					if (keyboard_prompt(a, title, typed, (int)sizeof typed) == KB_ACCEPT && a->running &&
					    !link_join_pick(host_sel, typed))
						uisound_play(DS_SOUND_DENY);
					memset(typed, 0, sizeof typed);
					plat_input_flush();
					memset(&a->in, 0, sizeof a->in);
					continue;
				}
			} else if (st == LINK_LINKED && a->in.pressed[IN_ACCEPT]) {
				uisound_play(DS_SOUND_BACK);
				stop_shown(a, bg);
			} else if (a->in.pressed[IN_ACCEPT]) {
				/* Nothing to choose while it waits: heard, and said why by
				 * the page */
				uisound_play(DS_SOUND_DENY);
			}
			if (a->in.pressed[IN_BACK]) {
				/* Linked, B closes the page and the link stays; otherwise it
				 * stops what it started, back to the tiles */
				if (st == LINK_LINKED) {
					done = true;
				} else {
					uisound_play(DS_SOUND_BACK);
					stop_shown(a, bg);
				}
			}
		}
		if (a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_BACK]) {
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
		}
		if (!panel_frame(a, lv, &k)) continue;
		/* The link's tick (link_tick) is in it, as in every screen's loop */
		tick_theme(a);
		paused_backdrop(a, bg, k);
		view_now(&v, false);
		show(a, &v, view_tiles(&v) ? sel : host_sel);
		plat_present();
		ui_frame_rest(8);
	}
	/* Closed after a failure or a lost link: what it left goes too, so the
	 * row says off and the Wi-Fi the player had is back */
	if (!linked && (link_state_now() == LINK_FAILED || link_state_now() == LINK_LOST))
		stop_shown(a, bg);
	panel_close(a, lv, false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return linked;
}

/* ---- on mGBA: the game again on Gambatte ----------------------------------
 *
 * Tarik's decision (2026-11-27): the row shows on a Game Boy or Game Boy
 * Color game whichever core it runs on; on mGBA it asks first
 * (CQ_LINK_GAMBATTE), and its yes gives this game Gambatte as its own core
 * (core_game_set: the shelf and its other games stay as they are, and the
 * Core row on its details page changes it back), stops the game and opens
 * it again on Gambatte,
 * from the shelf's turn as px2-open's open after a quit is (ctl_open_after),
 * and the Link Cable page opens over it at once (link_page_next_take). */
static bool g_page_next;
static char g_page_file[LIB_PATH];

bool link_page_next_take(const char *file)
{
	bool v = g_page_next && file && !strcmp(file, g_page_file);

	g_page_next = false;
	return v;
}

bool link_restart_on_core(app *a)
{
	sysview *sv = &a->view[a->sys_cursor];
	system_cfg *s;
	char rom[LIB_PATH * 2];
	int o;

	if (sv->list.count <= 0 || sv->cursor < 0 || sv->cursor >= sv->list.count) return false;
	o = shelf_owner(a, a->sys_cursor, sv->cursor);
	if (o < 0) return false;
	s = &a->sys.systems[o];
	if (!core_ready(s, LINK_CORE)) return false;
	snprintf(rom, sizeof rom, "%s/%s/%s", P_ROMS, s->folder, sv->list.items[sv->cursor].file);
	/* This game's own core from now on (Tarik's pick, 2026-11-27): the
	 * shelf and its other games stay as they are; the game's details page
	 * steps it back (its Core row) */
	core_game_set(s, sv->list.items[sv->cursor].file, LINK_CORE);
	fprintf(stderr, "link: %s now runs on %s, starting again on it\n", sv->list.items[sv->cursor].file,
	        LINK_CORE);
	ctl_open_after(rom, LINK_CORE);
	snprintf(g_page_file, sizeof g_page_file, "%s", sv->list.items[sv->cursor].file);
	g_page_next = true;
	plat_resident_line("STOP");
	return true;
}

void link_game_end(app *a)
{
	link_game_over();
	if (link_owes_wifi()) wait_panel(a, T(S_LINK_ROW), T(S_LINK_STOPPING));
	link_stop();
}

/* ---- what the link is done with (link_ops) -------------------------------- */

static bool lop_resident(const char *line) { return plat_resident_line("%s", line); }

static bool lop_hs_start(const char *ssid, const char *pass) { return hotspot_start_named(ssid, pass); }
static int  lop_hs_poll(void) { return (int)hotspot_poll(); }
static void lop_hs_stop(void) { hotspot_stop(); }
/* The radio as it was before the Join page's first scan: a scan brings the
 * supplicant up, and the end of the link puts it back down if it was */
static bool g_radio_known, g_radio_was_off;

static bool lop_scan_start(void)
{
	if (!g_radio_known) {
		g_radio_was_off = wifi_status(NULL, 0, NULL, 0) == WIFI_OFF;
		g_radio_known = true;
	}
	return wifi_up() && wifi_scan_start();
}
static int  lop_scan_poll(void) { return wifi_scan_poll(); }

static int lop_scan_take(char ssids[][LINK_SSID_MAX], int max)
{
	wifi_net nets[WIFI_MAX_NETS];
	int n = wifi_scan_take(nets, WIFI_MAX_NETS), i;

	for (i = 0; i < n && i < max; i++) snprintf(ssids[i], LINK_SSID_MAX, "%s", nets[i].ssid);
	return i;
}

static bool lop_join_start(const char *ssid, const char *pass) { return wifi_join_temp(ssid, pass); }

/* The supplicant asked at most twice a second: each ask is a fork of
 * wpa_cli out of the launcher */
static int lop_join_poll(char *addr, unsigned n)
{
	static unsigned at;
	unsigned now = plat_now_ms();

	if (at && now - at < 500u) return 0;
	at = now ? now : 1;
	return wifi_join_temp_poll(addr, (int)n);
}

static void lop_join_end(void)
{
	wifi_join_temp_end();
	if (g_radio_known && g_radio_was_off) wifi_down();
	g_radio_known = g_radio_was_off = false;
}
static unsigned lop_now(void) { return plat_now_ms(); }

/* The cable's host end: its network gadget bound, as USB File Transfer
 * binds it, and this plug never asked about (Computer Connected) after:
 * the other Pixel 2 that powers it looks like a computer */
static bool lop_cable_host_up(void)
{
	if (desk_cable()) return false;
	usblink_for_transfer();
	usblink_skip_ask();
	return plat_usb_link(-1);
}

static void lop_cable_host_down(void) { usblink_after_transfer(db_dev()); }

/* The cable's joining end: the port the host for the plug on it, through
 * usbrole and every one of its safety rules (rule.h): it never gives power
 * while power comes in, so two that both chose Join cannot both */
static bool lop_cable_join_start(void)
{
	usb_power p;

	if (desk_cable()) return true;
	if (!plat_usb_power(&p) || !p.known) return false;
	if (p.host) return true;
	if (!p.plug || !p.ask) return false;
	fprintf(stderr, "link: the port the host for plug %u, for the cable\n", p.plug);
	return plat_usb_power_grant(p.plug);
}

/* The other one's network gadget, met as a phone's is (cdc_ncm,
 * plat_phone_link), and the address its udhcpd gives; asked twice a second */
static int lop_cable_join_poll(char *addr, unsigned n)
{
	static unsigned at;
	unsigned now = plat_now_ms();
	char ifn[16], ip[32] = "";

	if (at && now - at < 500u) return 0;
	at = now ? now : 1;
	if (desk_cable()) return desk_cable_addr(addr, n);
	if (!plat_phone_link(true, ifn, sizeof ifn, ip, sizeof ip) || !ip[0]) return 0;
	snprintf(addr, n, "%s", ip);
	return 1;
}

/* The port stays the host until the cable comes out: usbrole alone writes
 * the role, and ends it with the plug */
static void lop_cable_join_end(void)
{
	fprintf(stderr, "link: the cable's link off; the port stays the host until the cable comes out\n");
}

/* "Linked with Pixel 2 0C11" over the game, in the queue with the other
 * pop-ups */
static void lop_notice(link_news what, const char *peer)
{
	char body[96];

	if (what == LINK_NEWS_LINKED) snprintf(body, sizeof body, T(S_LINK_LINKED_WITH), peer);
	else if (what == LINK_NEWS_LEFT) snprintf(body, sizeof body, T(S_LINK_LEFT), peer);
	else snprintf(body, sizeof body, "%s", T(S_LINK_LOST));
	notice_queue(NQ_SET, 0, 3000, T(S_LINK_ROW), body, NULL, 0, 0);
}

static const link_ops g_link_ops = {
	lop_resident, linknet_code, lop_hs_start, lop_hs_poll, lop_hs_stop,
	lop_scan_start, lop_scan_poll, lop_scan_take,
	lop_join_start, lop_join_poll, lop_join_end,
	lop_udp_open, linknet_send, linknet_recv, linknet_close,
	lop_cable_host_up, lop_cable_host_down, lop_cable_join_start, lop_cable_join_poll, lop_cable_join_end,
	lop_now, lop_notice,
};

void link_setup(void)
{
	char mac[32] = "";

	self_address(mac, sizeof mac);
	link_use(&g_link_ops, mac);
#ifdef ZYPEROS_DESK
	if (desk_cable()) link_use_cable_host(desk_cable_host());
#endif
}
