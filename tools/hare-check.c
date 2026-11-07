/* SPDX-License-Identifier: GPL-3.0-only */
/* Can a browser get at the card without the PIN?
 *
 *     make check-hare
 *
 * check-xfer proves a path cannot climb out of a root. This proves the routes
 * that use it are actually behind the lock - which is a separate claim, and the
 * one that fails when somebody adds a convenient endpoint and forgets. Every
 * route is tried twice: once with no session at all, and once with a session,
 * so a route that is accidentally public shows up as a pass where a 401 was
 * wanted.
 *
 * Also here: the lockout, which is the entire reason a four-digit PIN is a lock
 * rather than a doorbell. Ten thousand guesses is seconds of scripting; five
 * tries and a wait is not.
 *
 * A real server on a real socket, against a real directory tree in /tmp.
 */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <zlib.h>

#include "../src/hare.h"
#include "../src/i18n.h"
#include "../src/xfer.h"

static int failures;

/* The art match route's handler, standing in for the launcher's: how much of
 * the body it was handed. */
static size_t g_art_seen;

static char *art_seen(const char *folder, const char *body, size_t *len,
                      char *why, size_t wn)
{
	char *out = malloc(3);

	(void)folder; (void)why; (void)wn;
	g_art_seen = strlen(body);
	if (out) { memcpy(out, "{}", 3); *len = 2; }
	return out;
}

/* The SteamGridDB hook, standing in for the launcher's: what it was handed,
 * and a reply that, like the launcher's, carries the tail and not the key. */
static int  g_sgdb_op = -1;
static char g_sgdb_body[300];

static char *sgdb_seen(int op, const char *body, size_t n, size_t *len,
                       char *why, size_t wn)
{
	static const char reply[] = "{\"key\":true,\"tail\":\"cd9f\"}";
	char *out = malloc(sizeof reply);

	(void)why; (void)wn;
	g_sgdb_op = op;
	snprintf(g_sgdb_body, sizeof g_sgdb_body, "%.*s", (int)n, body);
	if (out) { memcpy(out, reply, sizeof reply); *len = sizeof reply - 1; }
	return out;
}

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static char g_root[128], g_card[160], g_roms[160], g_web[160], g_shared[160];
static char g_cookie[128];

/* ---- the server on its own thread ------------------------------------ */

static pthread_t    g_thread;
static volatile int g_stop;

static void *server_loop(void *u)
{
	(void)u;
	while (!g_stop) { hare_poll(); usleep(200); }
	return NULL;
}

static bool serve_up(const char *web)
{
	g_stop = 0;
	if (!hare_start(g_roms, g_card, g_shared, web)) return false;
	pthread_create(&g_thread, NULL, server_loop, NULL);
	return true;
}

static void serve_down(void)
{
	if (g_thread) { g_stop = 1; pthread_join(g_thread, NULL); g_thread = 0; }
	hare_stop();
}

/* A console's art and color, as the launcher answers them from systems.cfg */
static const char *icon_of(const char *folder) { return !strcmp(folder, "NES") ? "NES" : NULL; }
static unsigned color_of(const char *folder) { return !strcmp(folder, "NES") ? 0xC4443Au : 0xFFFFFFFFu; }

/* The update over the cable, as the launcher's hook takes it: release file
 * names into a folder of its own, everything else refused */
static char g_cable[200];
static int  g_update_op = -1;
static char *update_seen(int op, const char *name, char *path, size_t pn,
                         size_t *len, char *why, size_t wn)
{
	char *json = strdup("{\"on\":true}");

	g_update_op = op;
	if (op == HARE_UPDATE_FILE) {
		if (strcmp(name, "ZyperOS-pixel2-update.txt")) {
			free(json);
			snprintf(why, wn, "not one of the update's files");
			return NULL;
		}
		snprintf(path, pn, "%s/%s", g_cable, name);
	}
	if (json) *len = strlen(json);
	return json;
}

/* ---- an ordinary client ---------------------------------------------- */

static bool complete(const char *out, size_t got)
{
	const char *blank = strstr(out, "\r\n\r\n");
	const char *cl;
	long len;

	if (!blank) return false;
	cl = strstr(out, "Content-Length: ");
	if (!cl || cl > blank) return false;
	len = strtol(cl + 16, NULL, 10);
	return (size_t)(blank + 4 - out) + (size_t)len <= got;
}

/* One request, one answer. Returns the status, and copies the body out. */
static int req(const char *method, const char *path, const char *cookie,
               const void *body, size_t blen, char *outbody, size_t outn)
{
	struct sockaddr_in a;
	struct timeval tv = { 3, 0 };
	char head[1024], resp[65536];
	size_t got = 0, sent = 0;
	int fd, n, status = 0;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons((unsigned short)hare_port());
	if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
		printf("  FAIL: connect: %s\n", strerror(errno));
		failures++;
		close(fd);
		return -1;
	}
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

	n = snprintf(head, sizeof head,
	             "%s %s HTTP/1.1\r\nHost: x\r\n%s%s%s"
	             "Content-Length: %zu\r\nConnection: close\r\n\r\n",
	             method, path,
	             cookie ? "Cookie: hare=" : "", cookie ? cookie : "",
	             cookie ? "\r\n" : "", blen);
	while (sent < (size_t)n) {
		ssize_t w = write(fd, head + sent, (size_t)n - sent);
		if (w <= 0) break;
		sent += (size_t)w;
	}
	sent = 0;
	while (sent < blen) {
		ssize_t w = write(fd, (const char *)body + sent, blen - sent);
		if (w <= 0) break;
		sent += (size_t)w;
	}
	while (got + 1 < sizeof resp) {
		ssize_t r = recv(fd, resp + got, sizeof resp - got - 1, 0);
		if (r <= 0) break;
		got += (size_t)r;
		resp[got] = '\0';
		if (complete(resp, got)) break;
	}
	resp[got] = '\0';
	close(fd);
	sscanf(resp, "HTTP/1.1 %d", &status);
	if (outbody) {
		const char *b = strstr(resp, "\r\n\r\n");
		snprintf(outbody, outn, "%s", b ? b + 4 : "");
	}
	/* Stash a session cookie if one was handed out. */
	{
		const char *sc = strstr(resp, "Set-Cookie: hare=");
		if (sc) {
			sc += 17;
			snprintf(g_cookie, sizeof g_cookie, "%.*s",
			         (int)strcspn(sc, ";\r\n"), sc);
		}
	}
	return status;
}

/* Is this JSON, structurally?
 *
 * The check used to look for substrings - strstr(body, "\"roms\"") - and
 * passed with flying colors over a reply that no parser would accept: a
 * hand-counted string literal had swallowed the opening bracket, so the
 * listing read "entries":{...},{...}] and the page died on it. A check that
 * looks for words in a document it never parses is checking that the words
 * are there, which is not the claim being made.
 *
 * Not a full parser: brackets and braces balanced, in order, ignoring
 * anything inside a string. That is enough to catch every way a hand-built
 * emitter goes wrong, which is by dropping or doubling a delimiter. */
static bool json_ok(const char *s)
{
	int depth = 0;
	bool instr = false;

	if (!s || *s != '{') return false;
	for (; *s; s++) {
		if (instr) {
			if (*s == '\\' && s[1]) s++;
			else if (*s == '"') instr = false;
			continue;
		}
		if (*s == '"') { instr = true; continue; }
		if (*s == '{' || *s == '[') depth++;
		else if (*s == '}' || *s == ']') { if (--depth < 0) return false; }
	}
	return depth == 0 && !instr;
}

/* An "entries" array, opened with a bracket rather than whatever else. */
static bool has_array(const char *s, const char *key)
{
	char pat[64];

	snprintf(pat, sizeof pat, "\"%s\":[", key);
	return strstr(s, pat) != NULL;
}

static int get(const char *path, const char *cookie)
{
	return req("GET", path, cookie, NULL, 0, NULL, 0);
}
static int post(const char *path, const char *cookie)
{
	return req("POST", path, cookie, NULL, 0, NULL, 0);
}

/* A request for another host, as a phone on the hotspot sends its internet
 * check. Returns the status; the Location header, if any, into `loc`. */
/* A request written out whole, its headers as given; the whole answer back,
 * NUL-terminated, and its length (-1 when it could not connect) */
static long raw(const char *request, char *resp, size_t n)
{
	struct sockaddr_in a;
	struct timeval tv = { 3, 0 };
	size_t got = 0;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons((unsigned short)hare_port());
	if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	if (write(fd, request, strlen(request)) != (ssize_t)strlen(request)) { close(fd); return -1; }
	for (;;) {
		ssize_t r = read(fd, resp + got, n - 1 - got);
		if (r <= 0) break;
		got += (size_t)r;
		if (got == n - 1) break;
	}
	close(fd);
	resp[got] = '\0';
	return (long)got;
}

/* The body of a raw answer, past its blank line, and how long it is */
static const char *body_of(const char *resp, long len, long *blen)
{
	const char *b = strstr(resp, "\r\n\r\n");

	if (!b) { *blen = 0; return ""; }
	b += 4;
	*blen = len - (long)(b - resp);
	return b;
}

/* How many times a header line starts with `name` in the head of `resp` */
static int head_count(const char *resp, const char *name)
{
	const char *end = strstr(resp, "\r\n\r\n"), *p = resp;
	int n = 0;

	while ((p = strstr(p, name)) && (!end || p < end)) { n++; p += strlen(name); }
	return n;
}

static int host_req(const char *method, const char *host, const char *path,
                    const char *cookie, char *loc, size_t locn, char *outbody,
                    size_t outn)
{
	struct sockaddr_in a;
	struct timeval tv = { 3, 0 };
	char head[512], resp[8192];
	size_t got = 0;
	int fd, n, status = 0;
	const char *l, *b;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons((unsigned short)hare_port());
	if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	n = snprintf(head, sizeof head,
	             "%s %s HTTP/1.1\r\nHost: %s\r\n%s%s%s"
	             "Content-Length: 0\r\nConnection: close\r\n\r\n",
	             method, path, host, cookie ? "Cookie: hare=" : "",
	             cookie ? cookie : "", cookie ? "\r\n" : "");
	if (write(fd, head, (size_t)n) != n) { close(fd); return -1; }
	while (got + 1 < sizeof resp) {
		ssize_t r = recv(fd, resp + got, sizeof resp - got - 1, 0);
		if (r <= 0) break;
		got += (size_t)r;
		resp[got] = '\0';
		if (complete(resp, got)) break;
	}
	resp[got] = '\0';
	close(fd);
	sscanf(resp, "HTTP/1.1 %d", &status);
	if (loc && locn) {
		loc[0] = '\0';
		if ((l = strstr(resp, "Location: ")))
			snprintf(loc, locn, "%.*s", (int)strcspn(l + 10, "\r\n"), l + 10);
	}
	if (outbody) {
		b = strstr(resp, "\r\n\r\n");
		snprintf(outbody, outn, "%s", b ? b + 4 : "");
	}
	return status;
}

/* A file's bytes into `out`: how many, or -1 when it is not there or is
 * bigger than `n` */
static long file_bytes(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "rb");
	size_t got;

	if (!f) return -1;
	got = fread(out, 1, n, f);
	fclose(f);
	return got < n ? (long)got : -1;
}

static bool exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* ---- a small tree to serve --------------------------------------------- */

static void mkfile(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (f) { fputs(text, f); fclose(f); }
}

static void build_tree(void)
{
	char p[256];

	snprintf(g_root, sizeof g_root, "/tmp/zyperos-hare-%ld", (long)getpid());
	snprintf(g_card, sizeof g_card, "%s/card", g_root);
	snprintf(g_roms, sizeof g_roms, "%s/card/Roms", g_root);
	snprintf(g_web,  sizeof g_web,  "%s/web", g_root);
	snprintf(g_shared, sizeof g_shared, "%s/shared", g_root);

	mkdir(g_root, 0777);
	mkdir(g_card, 0777);
	mkdir(g_roms, 0777);
	mkdir(g_web, 0777);
	mkdir(g_shared, 0777);
	snprintf(p, sizeof p, "%s/.zyperos", g_shared);      mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/.zyperos/NES", g_shared);  mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/.zyperos/NES/Contra.state", g_shared);
	mkfile(p, "STATEDATA");
	/* The launcher's own, in the same directory as the states. */
	snprintf(p, sizeof p, "%s/.zyperos/cheevos.cfg", g_shared);
	mkfile(p, "1447\t6850\ts\n");
	snprintf(p, sizeof p, "%s/Bios", g_card);   mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/Saves", g_card);  mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/NES", g_roms);    mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/NES/Contra.nes", g_roms); mkfile(p, "ROMDATA");
	snprintf(p, sizeof p, "%s/index.html", g_web);      mkfile(p, "<h1>hare</h1>");
	/* Outside every root, and the thing an escape would be aiming at. */
	snprintf(p, sizeof p, "%s/secret.txt", g_card);     mkfile(p, "TOPSECRET");
}

static void rmtree(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d))) {
		char full[512];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
		if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) rmtree(full);
		else remove(full);
	}
	closedir(d);
	rmdir(dir);
}

int main(void)
{
	char body[4096];
	char pin[8];

	build_tree();
	printf("hare: the routes, and who may use them\n");

	if (!serve_up(g_web)) {
		printf("  FAIL: the server did not start\n");
		rmtree(g_root);
		return 1;
	}
	snprintf(pin, sizeof pin, "%s", hare_pin());
	CHECK(strlen(pin) == 4, "the PIN is \"%s\", wanted four digits", pin);
	{
		int i, digits = 0;
		for (i = 0; pin[i]; i++) if (pin[i] >= '0' && pin[i] <= '9') digits++;
		CHECK(digits == 4, "the PIN is not four digits");
	}

	printf("  the page is reachable without a PIN, because it IS the prompt:\n");
	CHECK(get("/", NULL) == 200, "the page was not served");

	/* The page's files are asked for again with a tag and answered 304 with
	 * nothing, and text goes gzipped to a browser that takes it; the API is
	 * never cached at all (src/httpd.c, httpd_reply_static). */
	printf("  the page's files: a tag to ask again with, and gzip for text:\n");
	{
		static char resp[65536], css[6000], plain[16384];
		char p[256], tag[64], gztag[64], q[512];
		const char *t, *b;
		long len, blen;
		z_stream z;
		int i;

		for (i = 0; i < (int)sizeof css - 1; i++) css[i] = "body { margin: 0 }\n"[i % 19];
		css[sizeof css - 1] = '\0';
		snprintf(p, sizeof p, "%s/app.css", g_web);
		mkfile(p, css);

		len = raw("GET /web/app.css HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", resp, sizeof resp);
		b = body_of(resp, len, &blen);
		CHECK(!strncmp(resp, "HTTP/1.1 200", 12) && blen == (long)strlen(css) && !memcmp(b, css, (size_t)blen),
		      "the stylesheet as it is: %.40s", resp);
		CHECK(head_count(resp, "Cache-Control:") == 1 && strstr(resp, "Cache-Control: no-cache") &&
		      !head_count(resp, "Content-Encoding:"), "its head: %.300s", resp);
		tag[0] = '\0';
		if ((t = strstr(resp, "ETag: "))) sscanf(t + 6, "%63s", tag);
		CHECK(tag[0] == '"', "no ETag on the page's file");

		snprintf(q, sizeof q, "GET /web/app.css HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n"
		         "Connection: close\r\n\r\n", tag);
		len = raw(q, resp, sizeof resp);
		body_of(resp, len, &blen);
		CHECK(!strncmp(resp, "HTTP/1.1 304", 12) && blen == 0, "asked with its tag: %.60s", resp);

		len = raw("GET /web/app.css HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip, deflate, br\r\n"
		          "Connection: close\r\n\r\n", resp, sizeof resp);
		b = body_of(resp, len, &blen);
		CHECK(!strncmp(resp, "HTTP/1.1 200", 12) && strstr(resp, "Content-Encoding: gzip") &&
		      strstr(resp, "Vary: Accept-Encoding"), "gzipped: %.300s", resp);
		CHECK(blen > 0 && blen < (long)strlen(css) / 4, "%ld gzipped bytes of %zu", blen, strlen(css));
		memset(&z, 0, sizeof z);
		inflateInit2(&z, 15 + 16);
		z.next_in = (unsigned char *)b;
		z.avail_in = (uInt)blen;
		z.next_out = (unsigned char *)plain;
		z.avail_out = sizeof plain;
		CHECK(inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == strlen(css) &&
		      !memcmp(plain, css, strlen(css)), "the gzip does not come back to the file");
		inflateEnd(&z);
		gztag[0] = '\0';
		if ((t = strstr(resp, "ETag: "))) sscanf(t + 6, "%63s", gztag);
		CHECK(gztag[0] && strcmp(gztag, tag), "the gzipped and the plain file share a tag: %s", tag);

		/* The plain one's tag is not the gzipped one's: a full answer */
		snprintf(q, sizeof q, "GET /web/app.css HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n"
		         "If-None-Match: %s\r\nConnection: close\r\n\r\n", tag);
		len = raw(q, resp, sizeof resp);
		CHECK(!strncmp(resp, "HTTP/1.1 200", 12), "another encoding's tag answered %.20s", resp);

		/* Changed on the card: a new tag, and the new file, gzipped */
		css[0] = 'h';
		css[100] = '\0';
		mkfile(p, css);
		snprintf(q, sizeof q, "GET /web/app.css HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n"
		         "If-None-Match: %s\r\nConnection: close\r\n\r\n", gztag);
		len = raw(q, resp, sizeof resp);
		b = body_of(resp, len, &blen);
		memset(&z, 0, sizeof z);
		inflateInit2(&z, 15 + 16);
		z.next_in = (unsigned char *)b;
		z.avail_in = (uInt)blen;
		z.next_out = (unsigned char *)plain;
		z.avail_out = sizeof plain;
		CHECK(!strncmp(resp, "HTTP/1.1 200", 12) && inflate(&z, Z_FINISH) == Z_STREAM_END &&
		      z.total_out == 100 && !memcmp(plain, css, 100), "a changed file was served stale");
		inflateEnd(&z);

		/* A picture is not text: not gzipped, still tagged */
		snprintf(p, sizeof p, "%s/mark.png", g_web);
		mkfile(p, "\x89PNG not really");
		len = raw("GET /web/mark.png HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\nConnection: close\r\n\r\n",
		          resp, sizeof resp);
		CHECK(!strncmp(resp, "HTTP/1.1 200", 12) && !head_count(resp, "Content-Encoding:") &&
		      head_count(resp, "ETag:") == 1, "a picture: %.300s", resp);

		/* And the API: never cached, one Cache-Control */
		len = raw("GET /api/list HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", resp, sizeof resp);
		CHECK(!strncmp(resp, "HTTP/1.1 401", 12) && head_count(resp, "Cache-Control:") == 1 &&
		      strstr(resp, "Cache-Control: no-store"), "the API's head: %.300s", resp);
	}

	/* The page's words, /i18n.js: the file of the language that is on, with
	 * no PIN (the PIN prompt is in it too), English's where a language has
	 * none, and never the last language's from a browser's cache: two files
	 * of one size and time still differ in their tag. */
	printf("  /i18n.js is the words of the language that is on:\n");
	{
		static char resp[4096];
		char p[256], b[256], q[512], tag[96];
		struct timeval same[2] = { { 1700000000, 0 }, { 1700000000, 0 } };
		const char *t;
		long len;

		snprintf(p, sizeof p, "%s/i18n", g_web);
		mkdir(p, 0777);
		snprintf(p, sizeof p, "%s/i18n/en.js", g_web);
		mkfile(p, "words('en');");
		utimes(p, same);
		snprintf(p, sizeof p, "%s/i18n/fr.js", g_web);
		mkfile(p, "words('fr');");
		utimes(p, same);
		CHECK(req("GET", "/i18n.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "words('en');"),
		      "English's, with no PIN: \"%s\"", b);
		len = raw("GET /i18n.js HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", resp, sizeof resp);
		tag[0] = '\0';
		if (len > 0 && (t = strstr(resp, "ETag: "))) sscanf(t + 6, "%95s", tag);
		CHECK(tag[0] == '"' && strstr(resp, "Cache-Control: no-cache") &&
		      strstr(resp, "Content-Type: text/javascript"), "its head: %.300s", resp);
		i18n_set(LANG_FR);
		CHECK(req("GET", "/i18n.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "words('fr');"),
		      "after MENU > Language, French's: \"%s\"", b);
		snprintf(q, sizeof q, "GET /i18n.js HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n"
		         "Connection: close\r\n\r\n", tag);
		len = raw(q, resp, sizeof resp);
		CHECK(len > 0 && !strncmp(resp, "HTTP/1.1 200", 12) && strstr(resp, "words('fr');"),
		      "English's tag, asked after the change, kept English: %.300s", resp);
		i18n_set(LANG_ES);
		CHECK(req("GET", "/i18n.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "words('en');"),
		      "a language with no file of its own got \"%s\", not English's", b);
		i18n_set(LANG_EN);
	}

	/* The page's name, after the link it is served over: /link.js, with no
	 * PIN (the PIN screen wears the name), the link that is on and never one
	 * a browser kept, and answered by the server, so a web folder from before
	 * it - this one, whose page never asks - still serves its page. */
	printf("  /link.js names the link, cable or Wi-Fi, and is never kept:\n");
	{
		static char resp[4096];
		static const char usb[] = "window.ZYPEROS_LINK = \"usb\";\n";
		static const char wifi[] = "window.ZYPEROS_LINK = \"wifi\";\n";
		char b[256], p[256];
		struct stat st;
		long len;

		CHECK(req("GET", "/link.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, wifi),
		      "before the launcher says, the link is not Wi-Fi: \"%s\"", b);
		hare_set_cable(true);
		CHECK(req("GET", "/link.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, usb),
		      "over the cable, with no PIN: \"%s\"", b);
		len = raw("GET /link.js HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"0-0-0\"\r\n"
		          "If-Modified-Since: Thu, 01 Jan 2099 00:00:00 GMT\r\nConnection: close\r\n\r\n",
		          resp, sizeof resp);
		CHECK(len > 0 && !strncmp(resp, "HTTP/1.1 200", 12) && strstr(resp, "Content-Type: text/javascript") &&
		      head_count(resp, "Cache-Control:") == 1 && strstr(resp, "Cache-Control: no-store") &&
		      !head_count(resp, "ETag:"), "a browser could keep it: %.300s", resp);
		hare_set_cable(false);
		CHECK(req("GET", "/link.js", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, wifi),
		      "back on Wi-Fi, the cable's answer stayed: \"%s\"", b);
		/* This web folder is one from before: no link.js, a page that does
		 * not ask. It is served over either link. */
		snprintf(p, sizeof p, "%s/link.js", g_web);
		CHECK(stat(p, &st) != 0, "the old web folder has a link.js");
		CHECK(req("GET", "/", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "<h1>hare</h1>"),
		      "an old web folder's page over Wi-Fi: \"%s\"", b);
		hare_set_cable(true);
		CHECK(req("GET", "/", NULL, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "<h1>hare</h1>"),
		      "an old web folder's page over the cable: \"%s\"", b);
		hare_set_cable(false);
	}

	printf("  nothing else is:\n");
	{
		/* Every route, unauthenticated. A 401 each. This is the check that
		 * fails the day somebody adds an endpoint and forgets the session
		 * test - which is why it lists them rather than sampling. */
		CHECK(get("/api/list", NULL) == 401, "listing the roots needed no PIN");
		CHECK(get("/api/list?p=roms", NULL) == 401, "listing ROMs needed no PIN");
		CHECK(get("/api/file?p=roms/NES/Contra.nes", NULL) == 401,
		      "DOWNLOADING A ROM NEEDED NO PIN");
		CHECK(req("PUT", "/api/file?p=roms/NES/x.nes", NULL, "X", 1, NULL, 0) == 401,
		      "UPLOADING NEEDED NO PIN");
		CHECK(post("/api/delete?p=roms/NES/Contra.nes", NULL) == 401,
		      "DELETING NEEDED NO PIN");
		CHECK(post("/api/rename?p=roms/NES/Contra.nes&to=x", NULL) == 401,
		      "RENAMING NEEDED NO PIN");
		CHECK(post("/api/mkdir?p=roms/NES/sub", NULL) == 401,
		      "MAKING A FOLDER NEEDED NO PIN");
		/* And the file is still there. */
		{
			char p[256];
			struct stat st;
			snprintf(p, sizeof p, "%s/NES/Contra.nes", g_roms);
			CHECK(stat(p, &st) == 0, "an unauthenticated call deleted the ROM");
		}
	}

	printf("  a wrong PIN is refused, and a right one is not:\n");
	CHECK(req("POST", "/api/auth", NULL, "0000", 4, NULL, 0) == 401 ||
	      !strcmp(pin, "0000"), "a wrong PIN was accepted");
	g_cookie[0] = '\0';
	CHECK(req("POST", "/api/auth", NULL, pin, 4, NULL, 0) == 200,
	      "the right PIN was refused");
	CHECK(g_cookie[0] != '\0', "no session cookie came back");
	CHECK(strlen(g_cookie) == 32, "the session token is %zu chars, wanted 32",
	      strlen(g_cookie));

	printf("  a made-up cookie is not a session:\n");
	CHECK(get("/api/list", "00000000000000000000000000000000") == 401,
	      "a guessed token was accepted");
	CHECK(get("/api/list", "short") == 401, "a short token was accepted");

	printf("  with a session, the routes work:\n");
	{
		CHECK(req("GET", "/api/list", g_cookie, NULL, 0, body, sizeof body) == 200,
		      "listing the roots failed");
		CHECK(json_ok(body), "the roots listing is not valid JSON: %s", body);
		CHECK(has_array(body, "entries"),
		      "entries is not an array: %s", body);
		CHECK(strstr(body, "\"roms\"") && strstr(body, "\"bios\"") &&
		      strstr(body, "\"saves\""), "the roots listing is wrong: %s", body);

		CHECK(req("GET", "/api/list?p=roms/NES", g_cookie, NULL, 0,
		          body, sizeof body) == 200, "listing NES failed");
		CHECK(json_ok(body), "the NES listing is not valid JSON: %s", body);
		CHECK(has_array(body, "entries"), "entries is not an array: %s", body);
		CHECK(strstr(body, "Contra.nes") != NULL,
		      "the ROM is missing from the listing: %s", body);

		/* A name with the characters JSON cares about, which is the other way
		 * a hand-built emitter breaks: unescaped, the reply stops parsing. */
		{
			char p[512];
			snprintf(p, sizeof p, "%s/NES/quote\"back\\slash.nes", g_roms);
			mkfile(p, "X");
			CHECK(req("GET", "/api/list?p=roms/NES", g_cookie, NULL, 0,
			          body, sizeof body) == 200, "listing after odd name failed");
			CHECK(json_ok(body),
			      "a name with a quote or backslash broke the JSON: %s", body);
			remove(p);
		}

		CHECK(req("GET", "/api/file?p=roms/NES/Contra.nes", g_cookie, NULL, 0,
		          body, sizeof body) == 200, "download failed");
		CHECK(!strcmp(body, "ROMDATA"), "downloaded \"%s\"", body);

		CHECK(req("PUT", "/api/file?p=roms/NES/New.nes", g_cookie,
		          "NEWROM", 6, NULL, 0) == 200, "upload failed");
		{
			char p[256];
			struct stat st;
			snprintf(p, sizeof p, "%s/NES/New.nes", g_roms);
			CHECK(stat(p, &st) == 0 && st.st_size == 6,
			      "the upload did not land");
			/* The part-file is renamed, not left behind. */
			snprintf(p, sizeof p, "%s/NES/New.nes.part", g_roms);
			CHECK(stat(p, &st) != 0, "a part-file was left behind");
		}

		CHECK(post("/api/rename?p=roms/NES/New.nes&to=Renamed.nes", g_cookie) == 200,
		      "rename failed");
		CHECK(post("/api/delete?p=roms/NES/Renamed.nes", g_cookie) == 200,
		      "delete failed");
	}

	printf("  and a session does not let a path climb out:\n");
	{
		/* xfer_resolve already refuses these; this proves the ROUTES ask it,
		 * which is a different claim and the one that regresses. */
		CHECK(get("/api/file?p=../secret.txt", g_cookie) == 403,
		      "read a file outside every root");
		CHECK(get("/api/file?p=roms/../../secret.txt", g_cookie) == 403,
		      "climbed out of a root");
		CHECK(get("/api/list?p=roms/..", g_cookie) == 403, "listed above a root");
		CHECK(post("/api/delete?p=roms/../../secret.txt", g_cookie) == 403,
		      "DELETED A FILE OUTSIDE EVERY ROOT");
		{
			char p[256];
			struct stat st;
			snprintf(p, sizeof p, "%s/secret.txt", g_card);
			CHECK(stat(p, &st) == 0, "the file outside the roots was deleted");
		}
		/* A rename names a sibling. If it could name a path, it could move a
		 * file into another root - or out of all of them. */
		CHECK(post("/api/rename?p=roms/NES/Contra.nes&to=../../escaped", g_cookie)
		      != 200, "a rename escaped its directory");
		CHECK(post("/api/rename?p=roms/NES/Contra.nes&to=%2e%2e%2fescaped",
		           g_cookie) != 200, "an encoded rename escaped its directory");
		CHECK(post("/api/rename?p=roms/NES/Contra.nes&to=sub%2fx", g_cookie)
		      != 200, "a rename carried a separator");
	}

	printf("  save states are reachable and the config beside them is not:\n");
	{
		CHECK(req("GET", "/api/file?p=states/NES/Contra.state", g_cookie,
		          NULL, 0, body, sizeof body) == 200,
		      "a save state could not be fetched");
		CHECK(!strcmp(body, "STATEDATA"), "the state came back as \"%s\"", body);
		CHECK(get("/api/file?p=states/cheevos.cfg", g_cookie) == 403,
		      "READ THE UNLOCK STORE THAT SITS BESIDE THE SAVE STATES");
		CHECK(post("/api/delete?p=states/cheevos.cfg", g_cookie) == 403,
		      "DELETED THE ONLY RECORD OF UNSUBMITTED UNLOCKS");
		{
			char p[512];
			struct stat st;
			snprintf(p, sizeof p, "%s/.zyperos/cheevos.cfg", g_shared);
			CHECK(stat(p, &st) == 0, "cheevos.cfg was deleted");
		}
	}

	/* A body read back by its handler, whole: the art routes stream theirs to
	 * a file and read it in the second call. The file was still open then, its
	 * last bytes still buffered, so a small body arrived empty - a console's
	 * whole cover list, 2026-10-05 - and a large one short by its tail. One
	 * small and one past a stdio buffer, to catch both. */
	printf("  a body is all there when its handler reads it:\n");
	{
		static char big[100000];
		char resp[256];
		size_t i;

		hare_set_art(NULL, art_seen, NULL, NULL);
		CHECK(req("POST", "/api/art/match?s=NES", g_cookie, "abc\ndef", 7,
		          resp, sizeof resp) == 200 && g_art_seen == 7,
		      "a 7-byte body reached the handler as %zu bytes", g_art_seen);
		for (i = 0; i < sizeof big; i++) big[i] = (char)('a' + i % 26);
		CHECK(req("POST", "/api/art/match?s=NES", g_cookie, big, sizeof big,
		          resp, sizeof resp) == 200 && g_art_seen == sizeof big,
		      "a %zu-byte body reached the handler as %zu bytes", sizeof big, g_art_seen);
		hare_set_art(NULL, NULL, NULL, NULL);
	}

	/* The SteamGridDB key: behind the PIN, handed to the hook whole, and a
	 * paste that cannot be a key is refused before it is read. */
	printf("  the SteamGridDB key goes to the launcher and no further:\n");
	{
		static const char key[] = "  0123456789abcdef0123456789abcd9f\n";
		static char big[600];
		char resp[512];

		CHECK(get("/api/sgdb", g_cookie) == 404, "no hook, and the key route answered");
		hare_set_sgdb(sgdb_seen);
		CHECK(req("POST", "/api/sgdb/key", NULL, key, sizeof key - 1, resp, sizeof resp)
		      == 401, "a key was taken without the PIN");
		CHECK(g_sgdb_op == -1, "and reached the launcher");
		CHECK(req("POST", "/api/sgdb/key", g_cookie, key, sizeof key - 1, resp, sizeof resp)
		      == 200 && g_sgdb_op == HARE_SGDB_KEY && !strcmp(g_sgdb_body, key),
		      "the key did not reach the hook as pasted (op %d, \"%s\")",
		      g_sgdb_op, g_sgdb_body);
		CHECK(strstr(resp, "cd9f") && !strstr(resp, "0123456789abcdef"),
		      "the reply carried the key: %s", resp);
		CHECK(req("GET", "/api/sgdb", g_cookie, NULL, 0, resp, sizeof resp) == 200 &&
		      g_sgdb_op == HARE_SGDB_STATE, "the state was not asked for");
		CHECK(post("/api/sgdb/remove", g_cookie) == 200 && g_sgdb_op == HARE_SGDB_REMOVE,
		      "remove did not reach the hook");
		memset(big, 'a', sizeof big);
		g_sgdb_op = -1;
		CHECK(req("POST", "/api/sgdb/key", g_cookie, big, sizeof big, resp, sizeof resp)
		      == 413 && g_sgdb_op == -1, "600 bytes were handed over as a key");
		CHECK(get("/api/sgdb/other", g_cookie) == 404, "an unknown key route answered");
		hare_set_sgdb(NULL);
	}

	/* Music listed as the page draws it: an album marked as one with its
	 * cover, an artist with their albums' covers. Two covers for the artist,
	 * because the second is the one that went out as seven bytes of garbage
	 * on 2026-10-05 and left a folder of artists unopenable. */
	printf("  Music's albums and artists come with their covers:\n");
	{
		static const char *const DIRS[] = {
			"Music", "Music/.media", "Music/Flat", "Music/Band", "Music/Band/.media",
			"Music/Band/One", "Music/Band/Two",
		};
		static const char *const FILES[] = {
			"Music/Flat/01 A.mp3", "Music/.media/Flat.jpg",
			"Music/Band/One/01 B.mp3", "Music/Band/Two/01 C.mp3",
			"Music/Band/.media/One.jpg", "Music/Band/.media/Two.png",
		};
		char p[512], body[4096];
		size_t i;

		for (i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++) {
			snprintf(p, sizeof p, "%s/%s", g_card, DIRS[i]);
			mkdir(p, 0777);
		}
		for (i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
			snprintf(p, sizeof p, "%s/%s", g_card, FILES[i]);
			mkfile(p, "X");
		}
		CHECK(req("GET", "/api/list?p=music", g_cookie, NULL, 0, body, sizeof body) == 200,
		      "listing Music failed");
		CHECK(json_ok(body), "the Music listing is not valid JSON: %s", body);
		CHECK(strstr(body, "\"album\":true,\"cover\":\"music/.media/Flat.jpg\"") != NULL,
		      "the album and its cover: %s", body);
		CHECK(strstr(body, "\"covers\":[\"music/Band/.media/One.jpg\","
		                   "\"music/Band/.media/Two.png\"]") != NULL,
		      "the artist and both albums' covers, in order: %s", body);
		for (i = sizeof FILES / sizeof FILES[0]; i-- > 0; ) {
			snprintf(p, sizeof p, "%s/%s", g_card, FILES[i]);
			remove(p);
		}
		for (i = sizeof DIRS / sizeof DIRS[0]; i-- > 1; ) {
			snprintf(p, sizeof p, "%s/%s", g_card, DIRS[i]);
			rmdir(p);
		}
	}

	printf("  deleting a folder with what is in it, where that is allowed:\n");
	{
		char p[512], body[1024];
		struct stat st;
		FILE *f;
		int i;

		snprintf(p, sizeof p, "%s/Music/Artist", g_card);          mkdir(p, 0777);
		snprintf(p, sizeof p, "%s/Music/Artist/Album", g_card);    mkdir(p, 0777);
		snprintf(p, sizeof p, "%s/Music/Artist/Album/01 A.mp3", g_card);
		if ((f = fopen(p, "w"))) fclose(f);
		snprintf(p, sizeof p, "%s/Music/Artist/Album/02 B.mp3", g_card);
		if ((f = fopen(p, "w"))) fclose(f);
		snprintf(p, sizeof p, "%s/NES/Kept.nes", g_roms);
		if ((f = fopen(p, "w"))) fclose(f);

		CHECK(req("GET", "/api/count?p=music/Artist", g_cookie, NULL, 0, body, sizeof body) == 200 &&
		      json_ok(body) && strstr(body, "\"files\":2") && strstr(body, "\"folders\":1") &&
		      strstr(body, "\"rule\":\"all\""),
		      "what an artist's folder holds, and that it may go whole: %s", body);
		CHECK(post("/api/delete?p=music/Artist", g_cookie) == 409,
		      "a full folder went without all=1");
		CHECK(post("/api/delete?p=music/Artist&all=1", g_cookie) == 200,
		      "an artist's folder did not go whole");
		snprintf(p, sizeof p, "%s/Music/Artist", g_card);
		CHECK(stat(p, &st) != 0, "the artist's folder is still there");

		CHECK(req("GET", "/api/count?p=roms/NES", g_cookie, NULL, 0, body, sizeof body) == 200 &&
		      strstr(body, "\"rule\":\"empty\""), "a console's folder is empty-only: %s", body);
		CHECK(post("/api/delete?p=roms/NES&all=1", g_cookie) == 409,
		      "A WHOLE CONSOLE'S GAMES WENT IN ONE CLICK");
		snprintf(p, sizeof p, "%s/NES/Kept.nes", g_roms);
		CHECK(stat(p, &st) == 0, "a game went with the refused console folder");

		CHECK(post("/api/delete?p=music", g_cookie) == 403, "an empty root was deleted");
		snprintf(p, sizeof p, "%s/Music", g_card);
		CHECK(stat(p, &st) == 0, "the Music folder is gone");
		CHECK(post("/api/delete?p=roms&all=1", g_cookie) == 403, "THE ROM ROOT WAS DELETED");

		/* Past the limit it is asked for in parts, and nothing is touched. */
		snprintf(p, sizeof p, "%s/Music/Big", g_card);             mkdir(p, 0777);
		for (i = 0; i <= HARE_DELETE_MAX; i++) {
			snprintf(p, sizeof p, "%s/Music/Big/%04d.mp3", g_card, i);
			if ((f = fopen(p, "w"))) fclose(f);
		}
		CHECK(post("/api/delete?p=music/Big&all=1", g_cookie) == 413,
		      "a folder past the limit was deleted");
		snprintf(p, sizeof p, "%s/Music/Big/0000.mp3", g_card);
		CHECK(stat(p, &st) == 0, "a refused folder lost files");
	}

	printf("  the lockout, which is what makes four digits a lock:\n");
	{
		char wrong[5];
		int i, last = 0;

		/* A PIN that is definitely not the right one. */
		snprintf(wrong, sizeof wrong, "%04d",
		         (atoi(pin) + 1) % 10000);
		for (i = 0; i < 6; i++)
			last = req("POST", "/api/auth", NULL, wrong, 4, NULL, 0);
		CHECK(last == 429, "after six wrong PINs the answer was %d, wanted 429",
		      last);
		/* And the right PIN is refused too while locked out - otherwise the
		 * lockout is advice rather than a lock. */
		CHECK(req("POST", "/api/auth", NULL, pin, 4, NULL, 0) == 429,
		      "the lockout let the right PIN through, so it only slows a "
		      "guesser down between attempts");
	}

	printf("  the hotspot: another host is a phone's internet check:\n");
	{
		char loc[128], b[256], p[256];
		struct stat st;

		snprintf(p, sizeof p, "%s/captive.js", g_web);
		mkfile(p, "note();");
		CHECK(host_req("GET", "x", "/web/captive.js", NULL, NULL, 0, b, sizeof b) == 200 &&
		      b[0] == '\0', "off the hotspot, captive.js is an empty script");
		hare_set_captive("10.42.1.1");
		CHECK(host_req("GET", "captive.apple.com", "/hotspot-detect.html", NULL,
		               loc, sizeof loc, NULL, 0) == 302, "Apple's check is not redirected");
		CHECK(!strcmp(loc, "http://10.42.1.1/"), "redirected to \"%s\", not the page", loc);
		CHECK(host_req("GET", "connectivitycheck.gstatic.com", "/generate_204", NULL,
		               loc, sizeof loc, NULL, 0) == 302, "Android's check is not redirected");
		CHECK(host_req("GET", "www.msftconnecttest.com", "/connecttest.txt", NULL,
		               loc, sizeof loc, NULL, 0) == 302, "Windows' check is not redirected");
		/* A redirect never runs a route: a page elsewhere on the internet
		 * cannot reach the card through the handheld's name for itself. */
		CHECK(host_req("POST", "evil.example", "/api/delete?p=roms/NES/Contra.nes",
		               g_cookie, loc, sizeof loc, NULL, 0) == 302, "a foreign POST ran");
		snprintf(p, sizeof p, "%s/NES/Contra.nes", g_roms);
		CHECK(stat(p, &st) == 0, "a request for another host deleted the ROM");
		CHECK(host_req("GET", "10.42.1.1", "/", NULL, loc, sizeof loc, NULL, 0) == 200,
		      "the page itself was redirected");
		CHECK(host_req("GET", "10.42.1.1:80", "/web/captive.js", NULL, NULL, 0, b, sizeof b) == 200 &&
		      !strcmp(b, "note();"), "on the hotspot, captive.js is not the file");
		hare_set_captive(NULL);
		CHECK(host_req("GET", "captive.apple.com", "/", NULL, loc, sizeof loc, NULL, 0) == 200,
		      "off the hotspot, another host is still redirected");
	}

	printf("  folders: dot names hidden, a console's art and color named:\n");
	{
		char p[300], b[8192];
		struct stat st;

		snprintf(p, sizeof p, "%s/NES/.media", g_roms);
		mkdir(p, 0777);
		snprintf(p, sizeof p, "%s/NES/.media/Contra.png", g_roms);
		mkfile(p, "PNG");
		CHECK(req("GET", "/api/list?p=roms/NES", g_cookie, NULL, 0, b, sizeof b) == 200 &&
		      !strstr(b, "\"name\":\".media\""), ".media is in the listing: %s", b);
		CHECK(req("GET", "/api/list?p=roms/NES&hidden=1", g_cookie, NULL, 0, b, sizeof b) == 200 &&
		      strstr(b, "\".media\""), "hidden=1 does not show .media: %s", b);
		hare_set_folder_icon(icon_of);
		hare_set_folder_color(color_of);
		CHECK(req("GET", "/api/list?p=roms", g_cookie, NULL, 0, b, sizeof b) == 200 &&
		      strstr(b, "\"color\":\"C4443A\",\"icon\":\"NES\""), "NES has no art in ROMs: %s", b);
		CHECK(req("GET", "/api/list?p=states", g_cookie, NULL, 0, b, sizeof b) == 200 &&
		      strstr(b, "\"icon\":\"NES\""), "NES's states have no art: %s", b);
		hare_set_folder_icon(NULL);
		hare_set_folder_color(NULL);

		/* The pictures: a key in capitals, nothing else */
		snprintf(p, sizeof p, "%s/icons", g_root);
		mkdir(p, 0777);
		hare_set_icons(p);
		snprintf(p, sizeof p, "%s/icons/NES-still.png", g_root);
		mkfile(p, "PNG!");
		CHECK(req("GET", "/icons/NES.png", g_cookie, NULL, 0, b, sizeof b) == 200 && !strcmp(b, "PNG!"),
		      "/icons/NES.png is not the still");
		CHECK(get("/icons/NES.png", NULL) == 401, "a picture needed no PIN");
		CHECK(get("/icons/nes.png", g_cookie) == 404, "a lower-case key was served");
		CHECK(get("/icons/..%2Fsecret.png", g_cookie) == 404, "a key climbed out");
		CHECK(get("/icons/NES.txt", g_cookie) == 404, "not a picture, served");

		/* A cover into a .media that is not there yet: made for it */
		snprintf(p, sizeof p, "%s/NES/.media/Contra.png", g_roms);
		remove(p);
		snprintf(p, sizeof p, "%s/NES/.media", g_roms);
		rmdir(p);
		CHECK(req("PUT", "/api/file?p=roms/NES/.media/Contra.png", g_cookie, "PNG", 3, NULL, 0) == 200,
		      "a cover into a missing .media was refused");
		snprintf(p, sizeof p, "%s/NES/.media/Contra.png", g_roms);
		CHECK(stat(p, &st) == 0 && st.st_size == 3, "the cover did not land in a new .media");
		/* ...and only .media: any other missing folder is still missing */
		CHECK(req("PUT", "/api/file?p=roms/NES/Nowhere/x.nes", g_cookie, "X", 1, NULL, 0) != 200,
		      "an upload made a folder that is not .media");
	}

	printf("  the room on the card, and the update over the cable:\n");
	{
		char p[300], b[4096];
		struct stat st;

		CHECK(req("GET", "/api/state", g_cookie, NULL, 0, b, sizeof b) == 200 && json_ok(b) &&
		      strstr(b, "\"closing\":false") && strstr(b, "\"reserve\":100000000") && strstr(b, "\"free\":"),
		      "/api/state is not the room: %s", b);
		CHECK(get("/api/state", NULL) == 401, "the room needed no PIN");
		CHECK(get("/api/update", g_cookie) == 404, "updates answered with no hook");
		snprintf(g_cable, sizeof g_cable, "%s/cable", g_root);
		mkdir(g_cable, 0777);
		hare_set_update(update_seen);
		CHECK(req("GET", "/api/update", g_cookie, NULL, 0, b, sizeof b) == 200 && strstr(b, "\"on\":true") &&
		      g_update_op == HARE_UPDATE_STATE, "/api/update did not ask the launcher");
		CHECK(req("PUT", "/api/update/file?name=evil.sh", g_cookie, "X", 1, NULL, 0) == 400,
		      "a file that is not the update's was taken");
		CHECK(req("PUT", "/api/update/file?name=ZyperOS-pixel2-update.txt", g_cookie, "LIST", 4, NULL, 0) == 200 &&
		      g_update_op == HARE_UPDATE_FILED, "the update's list was not taken");
		snprintf(p, sizeof p, "%s/ZyperOS-pixel2-update.txt", g_cable);
		CHECK(stat(p, &st) == 0 && st.st_size == 4, "the list is not where the launcher said");
		CHECK(post("/api/update/check", g_cookie) == 200 && g_update_op == HARE_UPDATE_CHECK,
		      "check did not reach the launcher");
		CHECK(post("/api/update/nothing", g_cookie) == 404, "an unknown update route answered");
		hare_set_update(NULL);
	}

	/* Safari over the cable never hears the answer to a PUT of an exact
	 * multiple of 128 KiB, so the page sends such a file with its trailer
	 * after it and trailer=16 in the URL, and the server takes it off: stored
	 * to the byte. A PUT that does not say so is stored as sent, trailer and
	 * all; one that says it wrongly is refused, the file of that name kept
	 * and no part-file left. The uploads' route and the update's. */
	printf("  128 KiB multiples: the page's trailer off, plain uploads as sent, the rest refused:\n");
	{
		static const char trailer[] = "--zyperos-pad--\n";
		static char data[262144 + 64], body[262144 + 64], back[262144 + 64];
		static const char *const WAYS[] = { "/api/file?p=roms/NES/", "/api/update/file?name=" };
		static const size_t SIZES[] = { 131072, 262144, 0 };
		char url[256], dest[300], part[310], resp[512];
		size_t i, w;
		long n;

		for (i = 0; i < sizeof data; i++) data[i] = (char)(i * 7 % 251);
		snprintf(g_cable, sizeof g_cable, "%s/cable", g_root);
		mkdir(g_cable, 0777);
		hare_set_update(update_seen);
		for (w = 0; w < sizeof WAYS / sizeof WAYS[0]; w++) {
			/* The update's hook takes one name, and the uploads' any */
			const char *name = w ? "ZyperOS-pixel2-update.txt" : "Even.bin";
			const char *what = w ? "the update's file" : "an upload";

			if (w) snprintf(dest, sizeof dest, "%s/%s", g_cable, name);
			else   snprintf(dest, sizeof dest, "%s/NES/%s", g_roms, name);
			snprintf(part, sizeof part, "%s.part", dest);

			for (i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++) {
				size_t sz = SIZES[i];

				memcpy(body, data, sz);
				memcpy(body + sz, trailer, sizeof trailer - 1);
				snprintf(url, sizeof url, "%s%s&trailer=16", WAYS[w], name);
				CHECK(req("PUT", url, g_cookie, body, sz + sizeof trailer - 1, resp, sizeof resp) == 200,
				      "%s of %zu bytes the page's way was refused: %s", what, sz, resp);
				n = file_bytes(dest, back, sizeof back);
				CHECK(n == (long)sz && !memcmp(back, data, sz),
				      "%s of %zu bytes the page's way is stored as %ld bytes", what, sz, n);
				CHECK(!exists(part), "%s the page's way left its part-file", what);
			}

			/* Plain: as sent, even when it ends with the trailer's bytes */
			memcpy(body, data, 131072);
			memcpy(body + 131072, trailer, sizeof trailer - 1);
			snprintf(url, sizeof url, "%s%s", WAYS[w], name);
			CHECK(req("PUT", url, g_cookie, body, 131072, resp, sizeof resp) == 200 &&
			      file_bytes(dest, back, sizeof back) == 131072 && !memcmp(back, data, 131072),
			      "%s of 128 KiB made plainly is not stored as sent", what);
			CHECK(req("PUT", url, g_cookie, body, 131072 + sizeof trailer - 1, resp, sizeof resp) == 200 &&
			      file_bytes(dest, back, sizeof back) == 131072 + (long)sizeof trailer - 1 &&
			      !memcmp(back, body, 131072 + sizeof trailer - 1),
			      "%s ending in the trailer's bytes, not saying so, lost them", what);

			/* Malformed, each with the file of that name as it was before */
			mkfile(dest, "ORIGINAL");
			memcpy(body + 131072, "--zyperos-pad--X", 16);     /* not its end */
			snprintf(url, sizeof url, "%s%s&trailer=16", WAYS[w], name);
			CHECK(req("PUT", url, g_cookie, body, 131072 + 16, resp, sizeof resp) == 400,
			      "%s that does not end with the trailer was taken", what);
			snprintf(url, sizeof url, "%s%s&trailer=17", WAYS[w], name);
			CHECK(req("PUT", url, g_cookie, body, 131072 + 17, resp, sizeof resp) == 400,
			      "%s with a trailer of another length was taken", what);
			snprintf(url, sizeof url, "%s%s&trailer=x", WAYS[w], name);
			CHECK(req("PUT", url, g_cookie, body, 131072 + 16, resp, sizeof resp) == 400,
			      "%s with a trailer that is no number was taken", what);
			snprintf(url, sizeof url, "%s%s&trailer=16", WAYS[w], name);
			CHECK(req("PUT", url, g_cookie, trailer, 5, resp, sizeof resp) == 400,
			      "%s shorter than its trailer was taken", what);
			CHECK(strstr(resp, "nothing was stored") != NULL, "the refusal does not say why: %s", resp);
			CHECK(file_bytes(dest, back, sizeof back) == 8 && !memcmp(back, "ORIGINAL", 8),
			      "a refused %s replaced the file of its name", what);
			CHECK(!exists(part), "a refused %s left a part-file", what);
			remove(dest);
		}
		hare_set_update(NULL);
	}

	printf("  stopping after the files on their way:\n");
	{
		char b[512];

		CHECK(hare_inflight() == 0 && !hare_closing(), "something is in flight at rest");
		hare_close_after();
		CHECK(hare_closing(), "close_after did not close");
		CHECK(req("PUT", "/api/file?p=roms/NES/Late.nes", g_cookie, "X", 1, b, sizeof b) == 503,
		      "an upload started while closing");
		CHECK(get("/api/file?p=roms/NES/Contra.nes", g_cookie) == 503, "a download started while closing");
		CHECK(req("GET", "/api/state", g_cookie, NULL, 0, b, sizeof b) == 200 && strstr(b, "\"closing\":true"),
		      "the page is not told it is closing: %s", b);
	}

	printf("  closing the screen takes the whole thing down:\n");
	serve_down();
	CHECK(hare_port() == 0, "still listening after stop");
	CHECK(hare_pin()[0] == '\0', "the PIN outlived the screen");
	CHECK(!hare_closing(), "closing outlived the screen");

	/* The repository's own page (res/web), as a card carries it, served the
	 * way the cable serves it: the page asks /link.js before its script,
	 * its words have the cable's name in the handheld's language, and the
	 * link is the cable's - and forgotten when the screen closes. Chrome
	 * draws the page itself in tools/portal-check.py. */
	printf("  the repository's page over the cable carries the cable's name:\n");
	hare_set_cable(true);
	if (!serve_up(REPO_WEB)) {
		CHECK(0, "the server did not start over %s", REPO_WEB);
	} else {
		static char b[32768];
		const char *link, *app;

		CHECK(req("GET", "/", NULL, NULL, 0, b, sizeof b) == 200, "the page was not served");
		link = strstr(b, "<script src=\"/link.js\"></script>");
		app = strstr(b, "<script src=\"/web/app.js\"></script>");
		CHECK(link && app && link < app, "the page does not ask /link.js before its script");
		i18n_set(LANG_FR);
		CHECK(req("GET", "/i18n.js", NULL, NULL, 0, b, sizeof b) == 200 &&
		      strstr(b, "\"name.usb\": \"Transfert de fichiers USB\"") &&
		      strstr(b, "\"name.wifi\": \"Transfert de fichiers Wi-Fi\""),
		      "French's words lack the two names: %.200s", b);
		i18n_set(LANG_EN);
		CHECK(req("GET", "/link.js", NULL, NULL, 0, b, sizeof b) == 200 && strstr(b, "\"usb\""),
		      "the link is not the cable's: \"%s\"", b);
		serve_down();
		CHECK(serve_up(REPO_WEB), "the server did not start again");
		CHECK(req("GET", "/link.js", NULL, NULL, 0, b, sizeof b) == 200 && strstr(b, "\"wifi\""),
		      "the cable outlived the screen: \"%s\"", b);
		serve_down();
	}

	rmtree(g_root);
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
