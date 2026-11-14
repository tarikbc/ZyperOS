/* SPDX-License-Identifier: GPL-3.0-only */
/* p2d: NES player 2 in a browser.
 *
 * A friend joins the Pixel's hotspot, or a laptop its USB cable, and a page
 * in their browser shows the game and is player 2's pad. The launcher starts
 * this for that, once Diatom has made the share (src/p2share.h), and stops it
 * with SIGTERM:
 *
 *     p2d --root DIR --share PATH --addr IP [--port N] [--dns] [--dns-port N]
 *         [--lang CODE] [--theme PATH]
 *
 * It serves DIR/play/ over plain HTTP, and DIR/i18n/CODE.js as /i18n.js, the
 * page's words in the handheld's language, and PATH as /play/theme.css, the
 * handheld's look (src/webtheme.h), read afresh at each request so a page
 * reload after a theme change has the new one; with no --theme, or no file,
 * that is a 404 and the page keeps its own tokens. One WebSocket on /p2ws per
 * browser: the frames out (codec.h), the acks and the pad in (guest.h).
 *
 * The share says what the system gives a guest (p2share.h): its buttons and
 * how many play at once, the NES's 0x01FD and 2. Up to players-1 browsers
 * play together, each on a port of its own and told so with its player's
 * number and the buttons: the first is player 2, a newcomer takes the lowest
 * free port, and one that comes when every port is in use is told it is
 * taken and closed. Each has its own pad, its own chain of frames (its own
 * encoder) and every rule here for itself. With --dns it is the hotspot's
 * DNS too, every name the Pixel's (captive.h), so a phone's internet check
 * lands here and is sent to the page. It listens on --addr alone: on the
 * cable the Pixel's Wi-Fi may still be on a home or public network, and a
 * client there is not to reach it at the Pixel's address on that network.
 * Its replies say the page is never to be drawn
 * inside another site's (frame-ancestors), so a site in another of the
 * laptop's tabs cannot hold player 2's place in a hidden frame.
 *
 * Its own process, so the network is on neither the game's thread nor the
 * launcher's frame loop; nice(5), so both come first. One thread, and nothing
 * in it blocks: the loop moves the HTTP server, the guests, the share and the
 * DNS socket along, and sleeps 2 ms when none of them moved. With no guest
 * and no browser on the way it waits in poll() instead, for one, for a DNS
 * question, or for its next beat: a game nobody joins wakes it some 20 times
 * a second, not 500.
 *
 * It ends by itself with the game: Diatom clears the share's magic and
 * removes the file when player 2 goes off or the game ends, so either one is
 * the game ending, however it ended, and the guests are told "ended". A
 * SIGTERM with the share still there is the handheld turning Player 2 off
 * while the game goes on, and they are told "stopped": the page keeps
 * trying, and comes back if it is turned on again. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <zlib.h>

#include "../captive.h"
#include "../httpd.h"
#include "../monoclock.h"
#include "../p2share.h"
#include "codec.h"
#include "guest.h"
#include "ws.h"

#define REFUSING      3      /* slots for newcomers being told every port is taken */
#define GUESTS        (P2S_GUESTS + REFUSING)
#define SHARE_WAIT_MS 5000   /* the launcher sends SETPLAYER2 just before it starts p2d */
#define SHARE_LOOK_MS 100    /* how often the share is looked at for its end */
#define BEAT_MS       50     /* Diatom lets the pad go 500 ms after the last beat */
#define PAD_QUIET_MS  1000   /* the page repeats a held pad every 100 ms */
#define EXIT_MS       500    /* for the guests to hear "ended" before p2d goes */
#define IDLE_NS       2000000L

#define RAW_CAP ((size_t)P2S_MAX_W * P2S_MAX_H * 4)

static const char *g_root, *g_share_path, *g_addr, *g_lang = "en", *g_theme;
static int g_port = 80, g_dns_port = 53;
static bool g_want_dns;

static p2s_head *g_share;          /* mapped read-write: p2d writes the pad */
static int g_share_fd = -1;
static struct stat g_share_st;     /* the file it was, to tell it from a new one */

/* The system's, from the share once it is open */
static uint32_t g_buttons;         /* the RetroPad bits a guest may press */
static int g_places;               /* players - 1: the guests' ports */
static long g_start_ms;            /* when p2d started, for the log's times */

static guest g_guest[GUESTS];
static int g_nguests;              /* slots in use: a guest a port, and REFUSING */

/* A guest's place in the game: port k+1, pad[k] in the share, player k+2 to
 * the page. Each its own encoder, so its chain of deltas is the frames it
 * was sent and no other guest's. */
typedef struct {
	int      guest;                /* the slot playing it, -1 for none */
	p2c_enc *enc;
	uint32_t pad;                  /* what it holds, within g_buttons */
	long     pad_ms;               /* its last 'P' */
} place;

static place g_place[P2S_GUESTS];

static uint8_t *g_raw;             /* the newest frame read from the share */
static unsigned g_raw_w, g_raw_h;
static uint32_t g_raw_frame;       /* its number, 0 for none */

static int g_dns = -1;
static uint8_t g_ip[4];

static volatile sig_atomic_t g_term;

static void on_term(int sig)
{
	(void)sig;
	g_term = 1;
}

/* ---- the share ---------------------------------------------------------- */

/* The share, open and mapped once Diatom has made it whole; false until then */
static bool share_open(void)
{
	struct stat st;
	p2s_head *hd;
	int fd = open(g_share_path, O_RDWR | O_CLOEXEC);

	if (fd < 0) return false;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)P2S_SIZE) {
		close(fd);
		return false;
	}
	hd = mmap(NULL, P2S_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (hd == MAP_FAILED) {
		close(fd);
		return false;
	}
	/* Diatom writes the magic last (p2share.h), so with it the rest is there */
	if (__atomic_load_n(&hd->magic, __ATOMIC_ACQUIRE) != P2S_MAGIC ||
	    hd->version != P2S_VERSION) {
		munmap(hd, P2S_SIZE);
		close(fd);
		return false;
	}
	g_share = hd;
	g_share_fd = fd;
	g_share_st = st;
	return true;
}

/* The game is over: the magic cleared, or the file gone from its name (a
 * file at that name now is another game's) */
static bool share_ended(void)
{
	struct stat st;

	if (__atomic_load_n(&g_share->magic, __ATOMIC_ACQUIRE) != P2S_MAGIC) return true;
	if (fstat(g_share_fd, &st) != 0 || st.st_nlink == 0) return true;
	if (stat(g_share_path, &st) != 0) return true;
	return st.st_ino != g_share_st.st_ino || st.st_dev != g_share_st.st_dev;
}

static void pad_set(int k, uint32_t mask)
{
	g_place[k].pad = mask & g_buttons;
	__atomic_store_n(&g_share->pad[k], g_place[k].pad, __ATOMIC_RELAXED);
}

/* guests: a bit for each port a browser plays */
static void guests_set(void)
{
	uint32_t bits = 0;

	for (int k = 0; k < g_places; k++)
		if (g_place[k].guest >= 0) bits |= 1u << k;
	__atomic_store_n(&g_share->guests, bits, __ATOMIC_RELAXED);
}

/* Diatom reads the pad only while this moves, so a p2d that dies or hangs
 * never leaves player 2's buttons held */
static void beat(void)
{
	__atomic_store_n(&g_share->pad_beat, g_share->pad_beat + 1u, __ATOMIC_RELEASE);
}

/* ---- the guests --------------------------------------------------------- */

#define TAKEN "S{\"state\":\"taken\"}"

/* The port slot `i` plays, -1 for none */
static int place_of(int i)
{
	for (int k = 0; k < g_places; k++)
		if (g_place[k].guest == i) return k;
	return -1;
}

static void leave(int k)
{
	int i = g_place[k].guest;

	if (i < 0) return;
	g_place[k].guest = -1;
	pad_set(k, 0);
	guests_set();
	/* With why, and when (ms since p2d started), so a log of a guest that
	 * came and went says what took it */
	fprintf(stderr, "p2d: player %d left at %ld ms: %s\n", k + 2, now_ms() - g_start_ms,
	        g_guest[i].why ? g_guest[i].why : "p2d is going");
}

/* httpd's socket after the 101: the lowest free port if there is one, told
 * which player it is and the buttons; else told every port is taken */
static void on_taken(int fd, void *ctx)
{
	guest *g = NULL;
	int k;

	(void)ctx;
	for (int i = 0; i < g_nguests && !g; i++)
		if (g_guest[i].fd < 0) g = &g_guest[i];
	if (!g) {
		/* A burst of newcomers, more than REFUSING at once: each is still
		 * told, so its page says the game is full and tries again */
		guest_refuse(fd, TAKEN, 1000);
		fprintf(stderr, "p2d: no room for a newcomer: told it is taken\n");
		return;
	}
	guest_open(g, fd, now_ms());
	for (k = 0; k < g_places && g_place[k].guest >= 0; k++) ;
	if (k < g_places) {
		char st[80];

		g_place[k].guest = (int)(g - g_guest);
		/* A new browser has no palette: only a reset sends one (codec.h) */
		p2c_reset(g_place[k].enc);
		snprintf(st, sizeof st, "S{\"state\":\"playing\",\"player\":%d,\"buttons\":%u}",
		         k + 2, (unsigned)g_buttons);
		guest_text(g, st);
		guests_set();
		fprintf(stderr, "p2d: player %d joined at %ld ms\n", k + 2, now_ms() - g_start_ms);
	} else {
		guest_text(g, TAKEN);
		guest_close(g, 1000);
	}
}

/* The share's newest frame to each player, when it has not had it and can
 * take it now. Read from the share only then, and once for all of them. */
static bool send_frames(void)
{
	uint32_t newest = __atomic_load_n(&g_share->frame, __ATOMIC_ACQUIRE);
	bool moved = false;

	if (newest == 0) return false;
	for (int k = 0; k < g_places; k++) {
		guest *g;
		uint8_t *room;
		size_t cap;
		long n;

		if (g_place[k].guest < 0) continue;
		g = &g_guest[g_place[k].guest];
		if (newest == g->last_frame || !guest_ready(g)) continue;
		/* Read again only when g_raw is behind the newest seen: another
		 * player's read this round may have landed a newer frame still,
		 * which this player takes as it is */
		if (g_raw_frame == 0 || (int32_t)(newest - g_raw_frame) > 0) {
			/* A torn read leaves g_raw part-written: no frame in it until
			 * one is read whole */
			g_raw_frame = 0;
			if (!p2s_read(g_share, g_raw, RAW_CAP, &g_raw_w, &g_raw_h, &g_raw_frame)) {
				g_raw_frame = 0;
				return moved;
			}
		}
		if (g_raw_frame == g->last_frame) continue;
		room = guest_frame_room(g, &cap);
		n = p2c_encode(g_place[k].enc, g_raw, g_raw_w, g_raw_h, g_raw_frame, room, cap);
		if (n < 0) {
			/* More than 256 colors: not a frame the codec carries, so not
			 * sent. The encoder reset itself, so the next that is goes as
			 * KEY and PAL. */
			g->last_frame = g_raw_frame;
			continue;
		}
		guest_frame_put(g, (size_t)n, g_raw_frame);
		moved = true;
	}
	return moved;
}

static bool guests_poll(long now)
{
	bool moved = false;

	for (int i = 0; i < g_nguests; i++) {
		guest *g = &g_guest[i];
		guest_news news;
		bool live;
		int k;

		if (g->fd < 0) continue;
		live = guest_poll(g, now, &news);
		if (news.moved) moved = true;
		k = place_of(i);
		if (k >= 0 && news.pad >= 0) {
			pad_set(k, (uint32_t)news.pad);
			g_place[k].pad_ms = now;
		}
		/* It drew nothing for a while and has no frame to add the next
		 * delta to: a KEY with its palette next, and the newest frame sent
		 * again for it, so the picture is back at once even while the
		 * game's frames stand still (the handheld paused) */
		if (k >= 0 && news.key) {
			p2c_reset(g_place[k].enc);
			g->last_frame = 0;
		}
		if (!live) guest_drop(g);
		/* A player the moment its close starts, not when it ends: its
		 * buttons go now, and the next browser may have its port */
		if (k >= 0 && (g->fd < 0 || g->closing)) leave(k);
	}
	return moved;
}

/* ---- the page ----------------------------------------------------------- */

static const char *mime_for(const char *name)
{
	const char *dot = strrchr(name, '.');

	if (!dot) return "application/octet-stream";
	if (!strcasecmp(dot, ".html")) return "text/html; charset=utf-8";
	if (!strcasecmp(dot, ".css"))  return "text/css; charset=utf-8";
	if (!strcasecmp(dot, ".js"))   return "text/javascript; charset=utf-8";
	if (!strcasecmp(dot, ".json")) return "application/json";
	if (!strcasecmp(dot, ".png"))  return "image/png";
	/* nosniff (httpd.c): a type this does not know is not drawn at all */
	if (!strcasecmp(dot, ".svg"))  return "image/svg+xml";
	if (!strcasecmp(dot, ".ttf"))  return "font/ttf";
	if (!strcasecmp(dot, ".woff2")) return "font/woff2";
	return "application/octet-stream";
}

/* A file of the page's own: one name, plain letters, no dot first. The page
 * is a few files beside each other; nothing else in DIR is to be served. */
static bool name_ok(const char *s)
{
	size_t n = 0;

	if (!*s || *s == '.') return false;
	for (; *s; s++, n++) {
		char c = *s;

		if (n >= 96) return false;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		      c == '.' || c == '-' || c == '_'))
			return false;
	}
	return true;
}

/* The handheld's name for itself, --addr or zyperos.local (mDNS), with or
 * without the port */
static bool host_ours(const char *host)
{
	return captive_host_is(host, g_addr) || captive_host_is(host, "zyperos.local");
}

static void redirect(httpd_req *r, const char *to)
{
	char loc[160];

	snprintf(loc, sizeof loc, "Location: %s\r\nCache-Control: no-store\r\n", to);
	httpd_reply(r, 302, "text/plain; charset=utf-8", "", 0, loc);
}

/* Whether `origin` is "http://<host>" on p2d's own port: none or :80 when
 * that is 80, else :PORT. A scheme and a host have no case. */
static bool origin_is(const char *origin, const char *host)
{
	size_t k = strlen(host);
	char port[8];

	if (strncasecmp(origin, "http://", 7) || strncasecmp(origin + 7, host, k)) return false;
	origin += 7 + k;
	if (!*origin) return httpd_port() == 80;
	snprintf(port, sizeof port, ":%d", httpd_port());
	return !strcmp(origin, port);
}

/* GET /p2ws: the handshake (RFC 6455, 4.2), then the socket to on_taken */
static void upgrade(httpd_req *r)
{
	const char *up = httpd_header(r, "Upgrade");
	const char *key = httpd_header(r, "Sec-WebSocket-Key");
	const char *ver = httpd_header(r, "Sec-WebSocket-Version");
	const char *origin = httpd_header(r, "Origin");
	char accept[29], extra[160];

	if (!up || strcasecmp(up, "websocket") || !ver || strcmp(ver, "13") ||
	    !key || !*key || !ws_accept_key(key, accept)) {
		httpd_reply_status(r, 400, "not a WebSocket request");
		return;
	}
	/* The play page's own origin only. A browser lets any page open a
	 * WebSocket anywhere, and on the cable the laptop has the internet, so
	 * a site in another tab could open ws://zyperos.local/p2ws and take
	 * player 2 or watch the game. Every browser sends Origin, so none is
	 * not a browser on the page either. (Plan 2's HTTPS origin joins these.) */
	if (!origin || !(origin_is(origin, g_addr) || origin_is(origin, "zyperos.local"))) {
		httpd_reply_status(r, 403, "only the play page may open this");
		return;
	}
	snprintf(extra, sizeof extra,
	         "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n", accept);
	httpd_reply_upgrade(r, extra, on_taken, NULL);
}

static void on_request(httpd_req *r, bool done, void *ctx)
{
	const char *path = httpd_path(r);
	char file[1024];

	(void)ctx;
	/* Every route answers on the first call, so a body is never wanted */
	if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);

	/* The hotspot names everything the Pixel (--dns), so a request for
	 * another host is a phone checking for the internet: the way to the
	 * page is the answer that opens its sign-in sheet (captive.h) */
	if (!host_ours(httpd_header(r, "Host"))) {
		char to[96];

		if (httpd_port() == 80) snprintf(to, sizeof to, "http://%s/play/", g_addr);
		else snprintf(to, sizeof to, "http://%s:%d/play/", g_addr, httpd_port());
		redirect(r, to);
		return;
	}
	if (strcmp(httpd_method(r), "GET")) {
		httpd_reply_status(r, 405, "GET only");
		return;
	}
	if (!strcmp(path, "/") || !strcmp(path, "/play")) {
		redirect(r, "/play/");
		return;
	}
	if (!strcmp(path, "/p2ws")) {
		upgrade(r);
		return;
	}
	/* The page's words, as File Transfer's (src/hare.c): one URL, the
	 * language's file, English's where it has none */
	if (!strcmp(path, "/i18n.js")) {
		struct stat st;
		int n = snprintf(file, sizeof file, "%s/i18n/%s.js", g_root, g_lang);

		if (n < 0 || n >= (int)sizeof file || stat(file, &st) != 0)
			n = snprintf(file, sizeof file, "%s/i18n/en.js", g_root);
		if (n < 0 || n >= (int)sizeof file) {
			httpd_reply_status(r, 404, "no");
			return;
		}
		httpd_reply_static(r, file, mime_for(file));
		return;
	}
	/* The handheld's theme is one file wherever the launcher writes it, not
	 * one in the page's folder; with no file the answer is httpd's 404 and
	 * the page wears its own tokens */
	if (!strcmp(path, "/play/theme.css")) {
		if (!g_theme) httpd_reply_status(r, 404, "no");
		else httpd_reply_static(r, g_theme, "text/css; charset=utf-8");
		return;
	}
	if (!strncmp(path, "/play/", 6)) {
		const char *name = path[6] ? path + 6 : "index.html";
		int n;

		if (!name_ok(name)) {
			httpd_reply_status(r, 404, "no");
			return;
		}
		n = snprintf(file, sizeof file, "%s/play/%s", g_root, name);
		if (n < 0 || n >= (int)sizeof file) {
			httpd_reply_status(r, 404, "no");
			return;
		}
		httpd_reply_static(r, file, mime_for(name));
		return;
	}
	httpd_reply_status(r, 404, "no");
}

/* ---- DNS ---------------------------------------------------------------- */

static void dns_open(void)
{
	struct sockaddr_in a;
	socklen_t al = sizeof a;
	int one = 1, fl;

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)g_dns_port);
	memcpy(&a.sin_addr, g_ip, 4);
	g_dns = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_dns < 0) {
		fprintf(stderr, "p2d: no DNS: %s\n", strerror(errno));
		return;
	}
	fl = fcntl(g_dns, F_GETFL, 0);
	if (fl >= 0) fcntl(g_dns, F_SETFL, fl | O_NONBLOCK);
	fcntl(g_dns, F_SETFD, FD_CLOEXEC);
	setsockopt(g_dns, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	/* Not fatal: the page still works by its address, only a phone's
	 * sign-in sheet does not open by itself */
	if (bind(g_dns, (struct sockaddr *)&a, sizeof a) != 0) {
		fprintf(stderr, "p2d: no DNS on %s port %d: %s\n", g_addr, g_dns_port, strerror(errno));
		close(g_dns);
		g_dns = -1;
		return;
	}
	if (getsockname(g_dns, (struct sockaddr *)&a, &al) == 0) g_dns_port = ntohs(a.sin_port);
	fprintf(stderr, "p2d: dns on port %d\n", g_dns_port);
}

/* Every question waiting, a handful at most, as src/hotspot.c answers them */
static bool dns_answer(void)
{
	uint8_t q[512], out[512];
	bool moved = false;

	if (g_dns < 0) return false;
	for (int k = 0; k < 16; k++) {
		struct sockaddr_in from;
		socklen_t fl = sizeof from;
		ssize_t n = recvfrom(g_dns, q, sizeof q, 0, (struct sockaddr *)&from, &fl);
		size_t len;

		if (n <= 0) break;
		moved = true;
		len = captive_dns_answer(q, (size_t)n, g_ip, out, sizeof out);
		if (len) sendto(g_dns, out, len, 0, (struct sockaddr *)&from, fl);
	}
	return moved;
}

/* ---- start and end ------------------------------------------------------ */

/* No guest and no browser on the way: nothing moves until one comes */
static bool nobody(void)
{
	if (httpd_conn_count() > 0) return false;
	for (int i = 0; i < g_nguests; i++)
		if (g_guest[i].fd >= 0) return false;
	return true;
}

/* Until a browser knocks, a DNS question comes, or it is time for the next
 * beat or the next look at the share: a 2 ms nap all game long is 500
 * wakeups a second for nobody, and a handheld's battery. A SIGTERM ends the
 * wait too (EINTR). */
static void wait_for_somebody(long now, long last_beat, long last_look)
{
	struct pollfd p[2];
	long due = last_beat + BEAT_MS, ms;
	nfds_t n = 0;

	if (last_look + SHARE_LOOK_MS < due) due = last_look + SHARE_LOOK_MS;
	ms = due - now;
	if (ms <= 0) return;
	p[n].fd = httpd_fd();
	p[n].events = POLLIN;
	p[n++].revents = 0;
	if (g_dns >= 0) {
		p[n].fd = g_dns;
		p[n].events = POLLIN;
		p[n++].revents = 0;
	}
	(void)poll(p, n, (int)ms);
}

/* Every guest told `state` and closed, every player's buttons let go: within
 * EXIT_MS, as the launcher sends SIGKILL a second after SIGTERM. "ended" for
 * the game's end, "stopped" for Player 2 turned off with the game going on. */
static int finish(int code, const char *state)
{
	long end = now_ms() + EXIT_MS;
	bool open = true;
	char st[48];

	snprintf(st, sizeof st, "S{\"state\":\"%s\"}", state);
	for (int i = 0; i < g_nguests; i++) {
		if (g_guest[i].fd < 0) continue;
		guest_text(&g_guest[i], st);
		guest_close(&g_guest[i], 1001);
	}
	for (int k = 0; k < g_places; k++) leave(k);
	while (open && now_ms() < end) {
		struct timespec ts = { 0, 1000000L };

		guests_poll(now_ms());
		open = false;
		for (int i = 0; i < g_nguests; i++)
			if (g_guest[i].fd >= 0) open = true;
		if (open) nanosleep(&ts, NULL);
	}
	for (int i = 0; i < g_nguests; i++) guest_drop(&g_guest[i]);
	for (int k = 0; k < P2S_GUESTS; k++) __atomic_store_n(&g_share->pad[k], 0u, __ATOMIC_RELAXED);
	__atomic_store_n(&g_share->guests, 0u, __ATOMIC_RELAXED);
	httpd_stop();
	if (g_dns >= 0) close(g_dns);
	return code;
}

static void usage(void)
{
	fprintf(stderr, "usage: p2d --root DIR --share PATH --addr IP [--port N] [--dns]"
	                " [--dns-port N] [--lang CODE] [--theme PATH]\n");
	exit(2);
}

static int port_arg(const char *s)
{
	char *end;
	long v = strtol(s, &end, 10);

	if (!*s || *end || v < 0 || v > 65535) usage();
	return (int)v;
}

/* A language code goes into a path: letters, digits and dashes only, else
 * English */
static const char *lang_arg(const char *s)
{
	const char *p;

	for (p = s; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '-' || *p == '_'))
			return "en";
	return *s && p - s < 16 ? s : "en";
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	long t0, now, last_beat, last_look;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--root") && i + 1 < argc) g_root = argv[++i];
		else if (!strcmp(argv[i], "--share") && i + 1 < argc) g_share_path = argv[++i];
		else if (!strcmp(argv[i], "--addr") && i + 1 < argc) g_addr = argv[++i];
		else if (!strcmp(argv[i], "--port") && i + 1 < argc) g_port = port_arg(argv[++i]);
		else if (!strcmp(argv[i], "--dns")) g_want_dns = true;
		else if (!strcmp(argv[i], "--dns-port") && i + 1 < argc) g_dns_port = port_arg(argv[++i]);
		else if (!strcmp(argv[i], "--lang") && i + 1 < argc) g_lang = lang_arg(argv[++i]);
		else if (!strcmp(argv[i], "--theme") && i + 1 < argc) g_theme = argv[++i];
		else usage();
	}
	if (!g_root || !g_share_path || !g_addr || inet_pton(AF_INET, g_addr, g_ip) != 1) usage();

	/* The game and the launcher come first; no core is ours alone */
	errno = 0;
	if (nice(5) == -1 && errno) fprintf(stderr, "p2d: nice: %s\n", strerror(errno));
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	/* A guest that hangs up mid-frame is an EPIPE to handle, not a death */
	signal(SIGPIPE, SIG_IGN);

	g_start_ms = t0 = now_ms();
	while (!share_open()) {
		struct timespec ts = { 0, 20000000L };

		if (g_term) return 0;
		if (now_ms() - t0 >= SHARE_WAIT_MS) {
			fprintf(stderr, "p2d: no share at %s after %d s\n", g_share_path, SHARE_WAIT_MS / 1000);
			return 1;
		}
		nanosleep(&ts, NULL);
	}

	/* What the system gives a guest, as Diatom wrote it from SETPLAYER2;
	 * read again by p2share.h's rule, since the tables are sized by it */
	g_buttons = p2s_buttons(g_share->buttons);
	g_places = (int)p2s_players((long)g_share->players) - 1;
	g_nguests = g_places + REFUSING;
	{
		/* The longest message: the head, the whole palette, and deflate's
		 * worst case for a frame of indices, with room to spare */
		size_t cap = P2C_HEAD + 2 + 256 * 3 + compressBound((uLong)P2S_MAX_W * P2S_MAX_H) + 1024;
		bool ok;

		g_raw = malloc(RAW_CAP);
		ok = g_raw != NULL;
		for (int k = 0; k < g_places; k++) {
			g_place[k].guest = -1;
			if (!(g_place[k].enc = p2c_new())) ok = false;
		}
		for (int i = 0; i < g_nguests; i++)
			if (!guest_alloc(&g_guest[i], cap)) ok = false;
		if (!ok) {
			fprintf(stderr, "p2d: out of memory\n");
			return 1;
		}
	}
	for (int k = 0; k < P2S_GUESTS; k++) __atomic_store_n(&g_share->pad[k], 0u, __ATOMIC_RELAXED);
	guests_set();
	beat();
	fprintf(stderr, "p2d: %d players at most, buttons 0x%04X\n", g_places + 1, (unsigned)g_buttons);
	/* A site in another of the laptop's tabs must not hold the page in a
	 * hidden frame: its socket would take player 2's place, and the friend
	 * would be told the game is full. Modern browsers read the first, older
	 * ones the second. */
	httpd_always("Content-Security-Policy: frame-ancestors 'none'\r\nX-Frame-Options: DENY\r\n");
	if (!httpd_start_on(g_addr, &g_port, 1)) {
		fprintf(stderr, "p2d: cannot listen on %s port %d: %s\n", g_addr, g_port, strerror(errno));
		return finish(1, "ended");
	}
	fprintf(stderr, "p2d: http on port %d\n", httpd_port());
	if (g_want_dns) dns_open();

	last_beat = last_look = now_ms();
	for (;;) {
		bool moved = false;

		if (g_term) {
			/* The share there still: Player 2 was turned off, and the game
			 * goes on */
			bool ended = share_ended();

			fprintf(stderr, "p2d: stopped%s\n", ended ? ", the game over" : "");
			return finish(0, ended ? "ended" : "stopped");
		}
		if (httpd_poll(on_request, NULL, NULL) > 0) moved = true;
		now = now_ms();
		if (guests_poll(now)) moved = true;
		if (send_frames()) moved = true;
		/* A phone that locked or walked out of range mid-press */
		for (int k = 0; k < g_places; k++)
			if (g_place[k].pad && now - g_place[k].pad_ms >= PAD_QUIET_MS) pad_set(k, 0);
		if (now - last_beat >= BEAT_MS) {
			beat();
			last_beat = now;
		}
		if (now - last_look >= SHARE_LOOK_MS) {
			last_look = now;
			if (share_ended()) {
				fprintf(stderr, "p2d: the game ended\n");
				return finish(0, "ended");
			}
		}
		if (dns_answer()) moved = true;
		if (moved) continue;
		if (nobody()) {
			wait_for_somebody(now, last_beat, last_look);
		} else {
			struct timespec ts = { 0, IDLE_NS };

			nanosleep(&ts, NULL);
		}
	}
}
