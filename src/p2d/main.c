/* SPDX-License-Identifier: GPL-3.0-only */
/* p2d: NES player 2 in a browser.
 *
 * A friend joins the Pixel's hotspot, or a laptop its USB cable, and a page
 * in their browser shows the game and is player 2's pad. The launcher starts
 * this for that, once Diatom has made the share (src/p2share.h), and stops it
 * with SIGTERM:
 *
 *     p2d --root DIR --share PATH --addr IP [--port N] [--dns] [--dns-port N]
 *         [--lang CODE]
 *
 * It serves DIR/play/ over plain HTTP, and DIR/i18n/CODE.js as /i18n.js, the
 * page's words in the handheld's language. One WebSocket on /p2ws: the
 * frames out (codec.h), the acks and the pad in (guest.h). The first browser
 * on it is player 2; any other is told player 2 is taken. With --dns it is
 * the hotspot's DNS too, every name the Pixel's (captive.h), so a phone's
 * internet check lands here and is sent to the page.
 *
 * Its own process, so the network is on neither the game's thread nor the
 * launcher's frame loop; nice(5), so both come first. One thread, and nothing
 * in it blocks: the loop moves the HTTP server, the guests, the share and the
 * DNS socket along, and sleeps 2 ms only when none of them moved.
 *
 * It ends by itself with the game: Diatom clears the share's magic and
 * removes the file when player 2 goes off or the game ends, so either one is
 * the game ending, however it ended. */
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

#define GUESTS        4      /* player 2, and a few being told it is taken */
#define SHARE_WAIT_MS 5000   /* the launcher sends SETPLAYER2 just before it starts p2d */
#define SHARE_LOOK_MS 100    /* how often the share is looked at for its end */
#define BEAT_MS       50     /* Diatom lets the pad go 500 ms after the last beat */
#define PAD_QUIET_MS  1000   /* the page repeats a held pad every 100 ms */
#define EXIT_MS       500    /* for the guests to hear "ended" before p2d goes */
#define IDLE_NS       2000000L

#define RAW_CAP ((size_t)P2S_MAX_W * P2S_MAX_H * 4)

static const char *g_root, *g_share_path, *g_addr, *g_lang = "en";
static int g_port = 80, g_dns_port = 53;
static bool g_want_dns;

static p2s_head *g_share;          /* mapped read-write: p2d writes the pad */
static int g_share_fd = -1;
static struct stat g_share_st;     /* the file it was, to tell it from a new one */

static guest g_guest[GUESTS];
static int g_p2 = -1;              /* the guest who is player 2, -1 for none */
static p2c_enc *g_enc;             /* player 2's: the only guest that has frames */

static uint8_t *g_raw;             /* the newest frame read from the share */
static unsigned g_raw_w, g_raw_h;
static uint32_t g_raw_frame;       /* its number, 0 for none */

static uint32_t g_pad;
static long g_pad_ms;              /* the last 'P' */

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

static void pad_set(uint32_t mask)
{
	g_pad = mask & P2S_PAD_MASK;
	__atomic_store_n(&g_share->pad, g_pad, __ATOMIC_RELAXED);
}

static void guests_set(uint32_t n)
{
	__atomic_store_n(&g_share->guests, n, __ATOMIC_RELAXED);
}

/* Diatom reads the pad only while this moves, so a p2d that dies or hangs
 * never leaves player 2's buttons held */
static void beat(void)
{
	__atomic_store_n(&g_share->pad_beat, g_share->pad_beat + 1u, __ATOMIC_RELEASE);
}

/* ---- the guests --------------------------------------------------------- */

static void p2_leave(void)
{
	if (g_p2 < 0) return;
	g_p2 = -1;
	pad_set(0);
	guests_set(0);
	fprintf(stderr, "p2d: player 2 left\n");
}

/* httpd's socket after the 101: player 2 if there is none, else told no */
static void on_taken(int fd, void *ctx)
{
	guest *g = NULL;

	(void)ctx;
	for (int i = 0; i < GUESTS && !g; i++)
		if (g_guest[i].fd < 0) g = &g_guest[i];
	if (!g) {
		close(fd);
		return;
	}
	guest_open(g, fd, now_ms());
	if (g_p2 < 0) {
		g_p2 = (int)(g - g_guest);
		/* A new browser has no palette: only a reset sends one (codec.h) */
		p2c_reset(g_enc);
		guest_text(g, "S{\"state\":\"player2\"}");
		guests_set(1);
		fprintf(stderr, "p2d: player 2 joined\n");
	} else {
		guest_text(g, "S{\"state\":\"taken\"}");
		guest_close(g, 1000);
	}
}

/* The share's newest frame to player 2, when it has not had it and can take
 * it now. Read from the share only then, and only when it moved on. */
static bool send_frame(void)
{
	guest *g;
	uint32_t newest;
	uint8_t *room;
	size_t cap;
	long n;

	if (g_p2 < 0) return false;
	g = &g_guest[g_p2];
	newest = __atomic_load_n(&g_share->frame, __ATOMIC_ACQUIRE);
	if (newest == 0 || newest == g->last_frame || !guest_ready(g)) return false;
	if (newest != g_raw_frame) {
		/* A torn read leaves g_raw part-written: no frame in it until one
		 * is read whole */
		g_raw_frame = 0;
		if (!p2s_read(g_share, g_raw, RAW_CAP, &g_raw_w, &g_raw_h, &g_raw_frame)) {
			g_raw_frame = 0;
			return false;
		}
	}
	if (g_raw_frame == g->last_frame) return false;
	room = guest_frame_room(g, &cap);
	n = p2c_encode(g_enc, g_raw, g_raw_w, g_raw_h, g_raw_frame, room, cap);
	if (n < 0) {
		/* More than 256 colors: not an NES frame, so not sent. The encoder
		 * reset itself, so the next that is goes as KEY and PAL. */
		g->last_frame = g_raw_frame;
		return false;
	}
	guest_frame_put(g, (size_t)n, g_raw_frame);
	return true;
}

static bool guests_poll(long now)
{
	bool moved = false;

	for (int i = 0; i < GUESTS; i++) {
		guest *g = &g_guest[i];
		guest_news news;
		bool live;

		if (g->fd < 0) continue;
		live = guest_poll(g, now, &news);
		if (news.moved) moved = true;
		if (i == g_p2 && news.pad >= 0) {
			pad_set((uint32_t)news.pad);
			g_pad_ms = now;
		}
		if (!live) guest_drop(g);
		/* Player 2 the moment its close starts, not when it ends: its
		 * buttons go now, and the next browser may be player 2 */
		if (i == g_p2 && (g->fd < 0 || g->closing)) p2_leave();
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

/* Every guest told the game ended and closed, player 2's buttons let go:
 * within EXIT_MS, as the launcher sends SIGKILL a second after SIGTERM */
static int finish(int code)
{
	long end = now_ms() + EXIT_MS;
	bool open = true;

	for (int i = 0; i < GUESTS; i++) {
		if (g_guest[i].fd < 0) continue;
		guest_text(&g_guest[i], "S{\"state\":\"ended\"}");
		guest_close(&g_guest[i], 1001);
	}
	p2_leave();
	while (open && now_ms() < end) {
		struct timespec ts = { 0, 1000000L };

		guests_poll(now_ms());
		open = false;
		for (int i = 0; i < GUESTS; i++)
			if (g_guest[i].fd >= 0) open = true;
		if (open) nanosleep(&ts, NULL);
	}
	for (int i = 0; i < GUESTS; i++) guest_drop(&g_guest[i]);
	pad_set(0);
	guests_set(0);
	httpd_stop();
	if (g_dns >= 0) close(g_dns);
	return code;
}

static void usage(void)
{
	fprintf(stderr, "usage: p2d --root DIR --share PATH --addr IP [--port N] [--dns]"
	                " [--dns-port N] [--lang CODE]\n");
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

	t0 = now_ms();
	while (!share_open()) {
		struct timespec ts = { 0, 20000000L };

		if (g_term) return 0;
		if (now_ms() - t0 >= SHARE_WAIT_MS) {
			fprintf(stderr, "p2d: no share at %s after %d s\n", g_share_path, SHARE_WAIT_MS / 1000);
			return 1;
		}
		nanosleep(&ts, NULL);
	}

	{
		/* The longest message: the head, the whole palette, and deflate's
		 * worst case for a frame of indices, with room to spare */
		size_t cap = P2C_HEAD + 2 + 256 * 3 + compressBound((uLong)P2S_MAX_W * P2S_MAX_H) + 1024;
		bool ok;

		g_enc = p2c_new();
		g_raw = malloc(RAW_CAP);
		ok = g_enc && g_raw;
		for (int i = 0; i < GUESTS; i++)
			if (!guest_alloc(&g_guest[i], cap)) ok = false;
		if (!ok) {
			fprintf(stderr, "p2d: out of memory\n");
			return 1;
		}
	}
	pad_set(0);
	guests_set(0);
	beat();
	if (!httpd_start(&g_port, 1)) {
		fprintf(stderr, "p2d: cannot listen on port %d: %s\n", g_port, strerror(errno));
		return finish(1);
	}
	fprintf(stderr, "p2d: http on port %d\n", httpd_port());
	if (g_want_dns) dns_open();

	last_beat = last_look = now_ms();
	for (;;) {
		bool moved = false;

		if (g_term) {
			fprintf(stderr, "p2d: stopped\n");
			return finish(0);
		}
		if (httpd_poll(on_request, NULL, NULL) > 0) moved = true;
		now = now_ms();
		if (guests_poll(now)) moved = true;
		if (send_frame()) moved = true;
		/* A phone that locked or walked out of range mid-press */
		if (g_pad && now - g_pad_ms >= PAD_QUIET_MS) pad_set(0);
		if (now - last_beat >= BEAT_MS) {
			beat();
			last_beat = now;
		}
		if (now - last_look >= SHARE_LOOK_MS) {
			last_look = now;
			if (share_ended()) {
				fprintf(stderr, "p2d: the game ended\n");
				return finish(0);
			}
		}
		if (dns_answer()) moved = true;
		if (!moved) {
			struct timespec ts = { 0, IDLE_NS };

			nanosleep(&ts, NULL);
		}
	}
}
