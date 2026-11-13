/* SPDX-License-Identifier: GPL-3.0-only */
/* See hare.h for what this is and what the PIN is worth. */
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

#include "captive.h"
#include "clients.h"
#include "fileio.h"
#include "hare.h"
#include "i18n.h"
#include "monoclock.h"
#include "muselib.h"
#include "xfer.h"
#include "sheet.h"

#define PIN_LEN        4
/* A move's files: the entry, and a sheet's names and side files with it */
#define MOVE_MAX       (1 + SHEET_NAMES)
#define TOKEN_LEN     32           /* hex characters */
#define MAX_SESSIONS   4           /* a phone and a laptop, with room spare */
#define MAX_TRIES      5
#define LOCKOUT_MS 30000u

/* One page of listing. A ROM folder of 500 entries is ordinary and the whole
 * thing has to fit in one reply, because there is no paging in the client. */
#define JSON_MAX (256 * 1024)

static char     g_web[XFER_PATH_MAX];
static char     g_captive[32];   /* the hotspot's address: hare_set_captive */
static bool     g_cable;         /* served over the USB cable: hare_set_cable */
static char     g_ssh[64];                   /* hare_set_ssh: user@address, or "" */
static unsigned (*g_folder_color)(const char *folder);   /* hare_set_folder_color */
static const char *(*g_folder_icon)(const char *folder); /* hare_set_folder_icon */
static char     g_icons[XFER_PATH_MAX];      /* hare_set_icons: the pictures */
static bool     g_closing;        /* hare_close_after: nothing new starts */
static int      g_sends;          /* downloads on their way */
static unsigned g_last_req;       /* when a browser last asked anything */
static char     g_file[128];      /* the upload on its way, or the last */
static hare_update_fn g_update;
static bool   (*g_space)(long long *free, long long *total);   /* hare_set_space */
static char     g_pin[PIN_LEN + 1];
static char     g_token[MAX_SESSIONS][TOKEN_LEN + 1];
static int      g_ntoken;
static int      g_tries;
static int      g_wrong;          /* every wrong PIN since hare_start */
static unsigned g_locked_until;
static int      g_uploads;
static bool     g_shelf_changed;  /* something the shelf would show has moved */
static char     g_last[128];
static bool   (*g_pack_logs)(char *path, size_t pn, char *name, size_t nn);
static void   (*g_before_delete)(const char *abs);
static void   (*g_after_rename)(const char *from, const char *to, bool tidied);
static char  *(*g_art_wanted)(size_t *len, char *why, size_t wn);
static hare_art_fn g_art_match, g_art_crc, g_art_nointro;
static void   (*g_after_write)(const char *abs);
static char  *(*g_albums_wanted)(size_t *len, char *why, size_t wn);
static char  *(*g_albums_pick)(int id, const char *json, size_t n,
                               size_t *len, char *why, size_t wn);
static hare_sgdb_fn g_sgdb;

/* A key as pasted, with room for white space around it: anything longer is
 * not one. */
#define SGDB_BODY_MAX 256

/* A MusicBrainz search reply, as the page passes it on: 25 releases is tens
 * of kilobytes. */
#ifndef PICK_TMP
#define PICK_TMP "/tmp/zyperos-albumpick"
#endif
#define PICK_MAX (2L * 1024 * 1024)

/* A collection's names, as the page sends them for /api/art/match. NES's box
 * art is 13,439 names, about 600 KB; this is room for a much bigger one. */
#ifndef ART_NAMES_TMP
#define ART_NAMES_TMP "/tmp/zyperos-artnames"
#endif
#define ART_NAMES_MAX (8L * 1024 * 1024)
static unsigned long g_in, g_out;
static unsigned long g_requests;     /* since hare_start (hare_stats) */
static clients g_clients;            /* browsers, not sockets (clients.h) */

/* ---- randomness ------------------------------------------------------ */

/* /dev/urandom or nothing.
 *
 * Not rand(): the PIN is the only thing standing between a stranger on the
 * same network and this device's filesystem, and a PIN seeded from the clock
 * is one a person can work out from when the screen was opened. If urandom
 * cannot be read, the feature refuses to start rather than falling back to
 * something weaker - a lock that is sometimes not a lock is worse than an
 * obvious failure to open the door. */
static bool rand_bytes(void *out, size_t n)
{
	FILE *f = fopen("/dev/urandom", "rb");
	size_t got;

	if (!f) return false;
	got = fread(out, 1, n, f);
	fclose(f);
	return got == n;
}

static bool make_pin(void)
{
	unsigned char b[PIN_LEN];
	int i;

	if (!rand_bytes(b, sizeof b)) return false;
	/* Modulo 10 on a uniform byte is very slightly biased toward the low
	 * digits - 256 is not a multiple of 10. At four digits against a lockout
	 * of five tries that is not a distinction anybody can use, and the
	 * rejection loop it would take to remove costs more clarity than it buys.
	 * Written down so the next reader does not have to work out whether it
	 * was noticed. */
	for (i = 0; i < PIN_LEN; i++) g_pin[i] = (char)('0' + b[i] % 10);
	g_pin[PIN_LEN] = '\0';
	return true;
}

static bool make_token(char *out)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char b[TOKEN_LEN / 2];
	int i;

	if (!rand_bytes(b, sizeof b)) return false;
	for (i = 0; i < (int)sizeof b; i++) {
		out[i * 2]     = hex[b[i] >> 4];
		out[i * 2 + 1] = hex[b[i] & 15];
	}
	out[TOKEN_LEN] = '\0';
	return true;
}

/* Length-independent compare, so the time this takes says nothing about how
 * much of the secret was right. Four digits and a lockout make that close to
 * theater, but it is four lines and the token it also guards is not four
 * digits. */
static bool secret_eq(const char *a, const char *b, size_t n)
{
	unsigned char diff = 0;
	size_t i;

	for (i = 0; i < n; i++) diff |= (unsigned char)(a[i] ^ b[i]);
	return diff == 0;
}

/* ---- sessions --------------------------------------------------------- */

static bool session_valid(const httpd_req *r)
{
	const char *c = httpd_header(r, "Cookie");
	const char *p;
	int i;

	if (!c) return false;
	p = strstr(c, "hare=");
	if (!p) return false;
	p += 5;
	if (strlen(p) < TOKEN_LEN) return false;
	for (i = 0; i < g_ntoken; i++)
		if (secret_eq(p, g_token[i], TOKEN_LEN)) return true;
	return false;
}

/* ---- JSON out, into a buffer that grows -------------------------------- */

static void hj_put(hj *o, const char *s, size_t n)
{
	if (o->bad) return;
	if (o->max && o->used + n + 1 > o->max) { o->bad = o->over = true; return; }
	if (o->used + n + 1 > o->cap) {
		size_t cap = o->cap ? o->cap : 4096;
		char *p;

		while (o->used + n + 1 > cap) cap *= 2;
		if (!(p = realloc(o->p, cap))) { o->bad = true; return; }
		o->p = p;
		o->cap = cap;
	}
	memcpy(o->p + o->used, s, n);
	o->used += n;
	o->p[o->used] = '\0';
}

/* Literals go through this, so the LENGTH IS THE COMPILER'S PROBLEM.
 *
 * Every one of these was hand-counted first, and one of them was wrong:
 * `{"path":"","entries":[` is 22 characters and was written as 21, which cut
 * off the opening bracket and emitted `"entries":{`. The browser's parser said
 * "Expected double-quoted property name at position 71", which is a true and
 * completely unhelpful description of a missing bracket sixty characters
 * earlier. Nothing in C requires a human to count a string literal. */
/* The "" in front makes anything but a literal a compile error: given an
 * expression, sizeof measures a pointer, which is how an artist's covers went
 * out as seven bytes of garbage on 2026-10-05. */
#define JLIT(j, s) hj_put((j), "" s, sizeof (s) - 1)

void hj_lit(hj *o, const char *s) { hj_put(o, s, strlen(s)); }

void hj_str(hj *o, const char *s)
{
	JLIT(o, "\"");
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char esc[8];

		/* Only what JSON requires. Everything else, including the UTF-8 that
		 * a filename on this card is, goes through untouched - re-encoding it
		 * would be inventing an opinion about text that nothing here has. */
		if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; hj_put(o, esc, 2); }
		else if (c == '\n') JLIT(o, "\\n");
		else if (c == '\r') JLIT(o, "\\r");
		else if (c == '\t') JLIT(o, "\\t");
		else if (c < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", c); hj_put(o, esc, 6); }
		else hj_put(o, (const char *)&c, 1);
	}
	JLIT(o, "\"");
}

void hj_int(hj *o, int v)
{
	char num[16];

	snprintf(num, sizeof num, "%d", v);
	hj_lit(o, num);
}

char *hj_done(hj *o, size_t *len, char *why, size_t wn)
{
	if (o->bad || !o->p) {
		free(o->p);
		snprintf(why, wn, "%s", T(S_HARE_NO_MEMORY));
		return NULL;
	}
	*len = o->used;
	return o->p;
}

static void jfmt(hj *j, const char *fmt, ...)
{
	char tmp[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0) hj_put(j, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

static const char *mime_for(const char *path)
{
	const char *dot = strrchr(path, '.');

	if (!dot) return "application/octet-stream";
	if (!strcasecmp(dot, ".html")) return "text/html; charset=utf-8";
	if (!strcasecmp(dot, ".css"))  return "text/css; charset=utf-8";
	if (!strcasecmp(dot, ".js"))   return "text/javascript; charset=utf-8";
	if (!strcasecmp(dot, ".json")) return "application/json";
	if (!strcasecmp(dot, ".png"))  return "image/png";
	/* The mark is an SVG, and X-Content-Type-Options: nosniff means a browser
	 * will NOT guess when this table does not know an extension - it refuses
	 * to render it at all. Which is the right behavior and exactly why the
	 * header is set; it also means a missing row here is a broken image
	 * rather than a slightly wrong one. */
	if (!strcasecmp(dot, ".svg"))  return "image/svg+xml";
	if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return "image/jpeg";
	if (!strcasecmp(dot, ".ttf"))  return "font/ttf";
	/* A ROM is a download, not something a browser should try to render. */
	return "application/octet-stream";
}

/* ---- the routes ------------------------------------------------------- */

static void note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(g_last, sizeof g_last, fmt, ap);
	va_end(ap);
}

/* Did that write land somewhere the shelf reads?
 *
 * Only the ROM, Music, Audiobooks and Videos roots count; MP4 is a shelf too. A save state, a
 * BIOS image or a renamed folder under Saves changes nothing the launcher
 * displays, and making those trigger a rescan would mean pulling a hundred
 * games off the card to react to a file nobody is looking at. Prefix match on
 * the resolved absolute path, so it is asking about where the bytes actually
 * went rather than what the URL said.
 */
static void note_write(const char *abs)
{
	int i;

	for (i = 0; i < xfer_root_count(); i++) {
		const xfer_root *rt = xfer_root_at(i);
		size_t n;

		if (strcmp(rt->name, "roms") != 0 && strcmp(rt->name, "music") != 0 &&
		    strcmp(rt->name, "books") != 0 && strcmp(rt->name, "videos") != 0)
			continue;
		n = strlen(rt->path);
		if (!strncmp(abs, rt->path, n) && (abs[n] == '/' || abs[n] == '\0'))
			g_shelf_changed = true;
	}
}

bool hare_shelf_changed(void) { return g_shelf_changed; }

void hare_set_logs(bool (*pack)(char *path, size_t pn, char *name, size_t nn))
{
	g_pack_logs = pack;
}

void hare_set_before_delete(void (*fn)(const char *abs)) { g_before_delete = fn; }
void hare_set_after_rename(void (*fn)(const char *from, const char *to, bool tidied))
{
	g_after_rename = fn;
}

/* ---- Tidy names --------------------------------------------------------
 *
 * A music or audiobook folder's names as the shelf shows them, made real on
 * the card: its tracks' and its own (muselib's ml_tidy_file and
 * ml_tidy_folder, the shelf's own rules, so the two cannot disagree). GET
 * lists what would change and POST changes it. A name that would land on one
 * already there, or on another track's new one, is skipped and never
 * overwritten. Files first, the folder last, and the launcher told of the
 * folder's new name to move what it keeps under the old one. */
#define TIDY_MAX 400
typedef struct {
	char from[XFER_NAME_MAX + 1], to[XFER_NAME_MAX + 1];
	bool folder, skip;
} tidy_item;

/* The music or audiobooks root a folder is inside, and not the root itself */
/* The root `abs`, resolved, is in or is: NULL for none */
static const xfer_root *move_root(const char *abs)
{
	for (int i = 0; i < xfer_root_count(); i++) {
		const xfer_root *rt = xfer_root_at(i);
		size_t n = strlen(rt->path);

		if (!strncmp(abs, rt->path, n) && (abs[n] == '/' || !abs[n])) return rt;
	}
	return NULL;
}

static const xfer_root *tidy_root(const char *abs)
{
	int i;

	for (i = 0; i < xfer_root_count(); i++) {
		const xfer_root *rt = xfer_root_at(i);
		size_t n = strlen(rt->path);

		if (strcmp(rt->name, "music") && strcmp(rt->name, "books")) continue;
		if (!strncmp(abs, rt->path, n) && abs[n] == '/' && abs[n + 1]) return rt;
	}
	return NULL;
}

static int tidy_by_from(const void *a, const void *b)
{
	return strcmp(((const tidy_item *)a)->from, ((const tidy_item *)b)->from);
}

/* What Tidy names would do to the folder at `abs`, or -1 when it can't be read */
static int tidy_plan(const char *abs, const xfer_root *rt, tidy_item *it, int max)
{
	const char *base = strrchr(abs, '/') + 1;
	size_t plen = (size_t)(base - 1 - abs);
	char path[XFER_PATH_MAX], artist[XFER_NAME_MAX + 1] = "";
	struct stat st;
	struct dirent *e;
	DIR *d;
	int n = 0, i, k;

	if (!(d = opendir(abs))) return -1;
	while ((e = readdir(d)) && n < max - 1) {
		if (e->d_name[0] == '.' || !ml_is_audio(e->d_name)) continue;
		if (!ml_tidy_file(e->d_name, it[n].to, sizeof it[n].to) || !xfer_name_ok(it[n].to))
			continue;
		snprintf(it[n].from, sizeof it[n].from, "%s", e->d_name);
		it[n].folder = false;
		n++;
	}
	closedir(d);
	qsort(it, (size_t)n, sizeof *it, tidy_by_from);
	for (i = 0; i < n; i++) {
		/* A name too long for the card's paths is skipped, like one taken */
		it[i].skip = snprintf(path, sizeof path, "%s/%s", abs, it[i].to) >= (int)sizeof path ||
		             stat(path, &st) == 0;
		for (k = 0; k < i && !it[i].skip; k++)
			if (!it[k].skip && !strcmp(it[k].to, it[i].to)) it[i].skip = true;
	}
	/* The folder, by its artist when it sits in one: Music/<artist>/<album> */
	if (plen > strlen(rt->path)) {
		const char *pb = abs + plen;

		while (pb > abs && pb[-1] != '/') pb--;
		snprintf(artist, sizeof artist, "%.*s", (int)(abs + plen - pb), pb);
	}
	if (ml_tidy_folder(base, artist[0] ? artist : NULL, it[n].to, sizeof it[n].to) &&
	    xfer_name_ok(it[n].to)) {
		snprintf(it[n].from, sizeof it[n].from, "%s", base);
		it[n].folder = true;
		it[n].skip = snprintf(path, sizeof path, "%.*s/%s", (int)plen, abs, it[n].to) >=
		             (int)sizeof path || stat(path, &st) == 0;
		n++;
	}
	return n;
}

void hare_set_art(char *(*wanted)(size_t *len, char *why, size_t wn),
                  hare_art_fn match, hare_art_fn crc, hare_art_fn nointro)
{
	g_art_wanted = wanted;
	g_art_match = match;
	g_art_crc = crc;
	g_art_nointro = nointro;
}

void hare_set_sgdb(hare_sgdb_fn fn)
{
	g_sgdb = fn;
}

void hare_set_albums(char *(*wanted)(size_t *len, char *why, size_t wn),
                     char *(*pick)(int id, const char *json, size_t n,
                                   size_t *len, char *why, size_t wn))
{
	g_albums_wanted = wanted;
	g_albums_pick = pick;
}

void hare_set_after_write(void (*fn)(const char *abs)) { g_after_write = fn; }

void hare_set_captive(const char *addr)
{
	snprintf(g_captive, sizeof g_captive, "%s", addr ? addr : "");
}

void hare_set_cable(bool cable) { g_cable = cable; }

void hare_set_ssh(const char *who)
{
	snprintf(g_ssh, sizeof g_ssh, "%s", who ? who : "");
}

void hare_set_folder_color(unsigned (*color)(const char *folder))
{
	g_folder_color = color;
}

void hare_set_folder_icon(const char *(*icon)(const char *folder))
{
	g_folder_icon = icon;
}

void hare_set_icons(const char *dir)
{
	snprintf(g_icons, sizeof g_icons, "%s", dir ? dir : "");
}

void hare_set_update(hare_update_fn fn) { g_update = fn; }
void hare_set_space(bool (*fn)(long long *free, long long *total)) { g_space = fn; }

void hare_close_after(void) { g_closing = true; note("%s", T(S_HARE_CLOSING)); }
bool hare_closing(void) { return g_closing; }
int  hare_inflight(void) { return g_uploads + g_sends; }
unsigned hare_quiet_ms(void) { return now_ms32() - g_last_req; }
void hare_shelf_seen(void) { g_shelf_changed = false; }

/* A picture's key, as the shelf names its art: capitals, digits, _ */
static bool icon_key_ok(const char *k, size_t n)
{
	size_t i;

	if (n < 1 || n > 32) return false;
	for (i = 0; i < n; i++)
		if (!((k[i] >= 'A' && k[i] <= 'Z') || (k[i] >= '0' && k[i] <= '9') || k[i] == '_'))
			return false;
	return true;
}

/* A hook's JSON answer, or its reason as a 500. */
static void reply_json(httpd_req *r, char *json, size_t len, const char *why,
                       const char *fallback)
{
	if (!json) {
		httpd_reply_status(r, 500, why[0] ? why : fallback);
		return;
	}
	httpd_reply(r, 200, "application/json", json, len, NULL);
	free(json);
}

/* ---- a folder and everything in it ----------------------------------------
 *
 * lstat, so a link is removed and never followed. The card is vfat and has no
 * links (see xfer.h), and this would still not walk out of a root if it ever
 * had one. Deep enough for any album or disc game; a folder deeper than this
 * is counted as too big rather than half deleted. */
#define TREE_DEPTH 12

/* Files and folders under `dir`, stopping once past `cap`. */
static int tree_count(const char *dir, int depth, int cap, int *files, int *folders)
{
	DIR *d;
	struct dirent *e;
	int n = 0;

	if (depth > TREE_DEPTH) return cap + 1;
	if (!(d = opendir(dir))) return 0;
	while ((e = readdir(d)) && n <= cap) {
		char p[XFER_PATH_MAX];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0) continue;
		n++;
		if (S_ISDIR(st.st_mode)) {
			(*folders)++;
			n += tree_count(p, depth + 1, cap - n, files, folders);
		} else {
			(*files)++;
		}
	}
	closedir(d);
	return n;
}

static bool tree_delete(const char *dir, int depth)
{
	DIR *d;
	struct dirent *e;
	bool ok = true;

	if (depth > TREE_DEPTH || !(d = opendir(dir))) return false;
	while ((e = readdir(d))) {
		char p[XFER_PATH_MAX];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) { ok = false; continue; }
		if (lstat(p, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) ok = tree_delete(p, depth + 1) && ok;
		else if (remove(p) != 0) ok = false;
	}
	closedir(d);
	return rmdir(dir) == 0 && ok;
}

/* The name a listing shows for a path, or "" for a root. */
static const char *base_of(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

/* An album's cover where Muse reads one, as a path the page can ask for:
 * <parent>/.media/<album>.jpg or .png (what Album Art fetched, or the files
 * carried), else a cover or folder picture in the album's own folder.
 * `abs`/`req` are the folder being listed, `name` the album in it. */
static bool album_cover(const char *abs, const char *req, const char *name,
                        char *cover, size_t cn)
{
	static const struct { const char *pre, *post; } WAYS[] = {
		{ ".media/", ".jpg" }, { ".media/", ".png" },
		{ "", "/cover.jpg" }, { "", "/cover.png" }, { "", "/cover.jpeg" },
		{ "", "/folder.jpg" }, { "", "/folder.png" }, { "", "/folder.jpeg" },
	};
	size_t i;

	for (i = 0; i < sizeof WAYS / sizeof WAYS[0]; i++) {
		char have[XFER_PATH_MAX];
		struct stat cs;

		if (snprintf(have, sizeof have, "%s/%s%s%s", abs, WAYS[i].pre, name,
		             WAYS[i].post) >= (int)sizeof have) continue;
		if (stat(have, &cs) != 0 || cs.st_size <= 0) continue;
		return snprintf(cover, cn, "%s/%s%s%s", req, WAYS[i].pre, name,
		                WAYS[i].post) < (int)cn;
	}
	return false;
}

static int cmp_str(const void *a, const void *b)
{
	return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

/* An artist's folder, as a picture: up to four of its albums' covers, the
 * albums in name order, as "covers":[...]. Eric's, 2026-10-05 - an artist
 * row had nothing beside it while its albums one level in all had covers.
 * `abs`/`req` are the folder being listed, `artist` the folder in it. */
#define ARTIST_COVERS 4

static void artist_covers(hj *j, const char *abs, const char *req, const char *artist)
{
	char dir[XFER_PATH_MAX], rdir[XFER_PATH_MAX], cover[XFER_PATH_MAX];
	char *names[256];
	int n = 0, i, shown = 0;
	DIR *d;
	struct dirent *e;

	if (snprintf(dir, sizeof dir, "%s/%s", abs, artist) >= (int)sizeof dir ||
	    snprintf(rdir, sizeof rdir, "%s/%s", req, artist) >= (int)sizeof rdir ||
	    !(d = opendir(dir)))
		return;
	while ((e = readdir(d)) && n < (int)(sizeof names / sizeof names[0])) {
		if (e->d_name[0] == '.') continue;
		if ((names[n] = strdup(e->d_name))) n++;
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof names[0], cmp_str);
	for (i = 0; i < n; i++) {
		if (shown < ARTIST_COVERS && album_cover(dir, rdir, names[i], cover, sizeof cover)) {
			/* Two JLITs, not one with a ?: inside: JLIT takes the length of a
			 * literal from sizeof, and of an expression that is a pointer's.
			 * Each cover went out as seven bytes of whatever followed, and a
			 * Music folder of artists would not open (2026-10-05). */
			if (shown++) JLIT(j, ",");
			else         JLIT(j, ",\"covers\":[");
			hj_str(j, cover);
		}
		free(names[i]);
	}
	if (shown) JLIT(j, "]");
}

/* Whether a folder holds songs of its own, and so is an album where Muse
 * reads one, rather than an artist's folder of albums. Read until the first:
 * the page gives an album a cover's slot, and an artist none (2026-10-05, an
 * outline beside every artist read as a cover missing). */
static bool holds_songs(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	bool found = false;

	while (d && !found && (e = readdir(d)))
		found = e->d_name[0] != '.' && ml_is_audio(e->d_name);
	if (d) closedir(d);
	return found;
}

static void route_list(httpd_req *r)
{
	/* Two forms of the same path, and the difference matters.
	 *
	 * `raw` is what the browser sent, still percent-encoded, and it is what
	 * xfer_resolve must be given: that function decodes and validates in one
	 * step precisely so nobody can do those in the wrong order.
	 *
	 * `req` is the decoded form, and it is what goes into the JSON. The API
	 * speaks decoded paths in its bodies and encoded ones in its URLs, which
	 * is the only combination that survives a round trip: echoing the encoded
	 * form back put "roms%2FNES" in the breadcrumb and then encoded it again
	 * on the next click. */
	char  raw[XFER_PATH_MAX], req[XFER_PATH_MAX], abs[XFER_PATH_MAX], hid[4];
	hj    j;
	DIR  *d;
	struct dirent *e;
	bool  first = true;
	/* Dot names - .media, where covers go, and a Mac's ._ files - are not
	 * what anyone opened the page for, and .media was in every console
	 * (2026-11-06). Shown when the page asks: Settings' "Show hidden
	 * folders". */
	bool  hidden = httpd_query(r, "hidden", hid, sizeof hid)[0] == '1';
	/* A folder per system lives in two roots: the consoles in ROMs, and
	 * their save states, which the launcher files under the same names */
	bool  systems;

	httpd_query(r, "p", raw, sizeof raw);
	if (raw[0] && !xfer_decode(raw, req, sizeof req)) {
		httpd_reply_status(r, 400, T(S_HARE_NOT_PATH));
		return;
	}
	if (!raw[0]) req[0] = '\0';

	/* No path means the roots themselves, which are not a directory anywhere
	 * on the card and so cannot be listed by reading one. */
	if (!raw[0]) {
		int i;

		j = (hj){ .max = 4096 };
		JLIT(&j, "{\"path\":\"\",");
		/* USB File Transfer's SSH, user@address, for the page to show */
		if (g_ssh[0]) {
			JLIT(&j, "\"ssh\":");
			hj_str(&j, g_ssh);
			JLIT(&j, ",");
		}
		JLIT(&j, "\"entries\":[");
		for (i = 0; i < xfer_root_count(); i++) {
			const xfer_root *rt = xfer_root_at(i);

			if (!first) JLIT(&j, ",");
			first = false;
			JLIT(&j, "{\"name\":");
			hj_str(&j, rt->label);
			JLIT(&j, ",\"path\":");
			hj_str(&j, rt->name);
			JLIT(&j, ",\"dir\":true,\"size\":0}");
		}
		JLIT(&j, "]}");
		if (j.bad) httpd_reply_status(r, 500, j.over ? T(S_HARE_LISTING_OVER)
		                                             : T(S_HARE_NO_MEMORY));
		else httpd_reply(r, 200, "application/json", j.p, j.used, NULL);
		free(j.p);
		return;
	}

	if (!xfer_resolve(raw, abs, sizeof abs)) {
		httpd_reply_status(r, 403, T(S_HARE_CANNOT_LOOK));
		return;
	}
	d = opendir(abs);
	if (!d) { httpd_reply_status(r, 404, T(S_HARE_NO_FOLDER)); return; }
	systems = !strcmp(req, "roms") || !strcmp(req, "states");

	j = (hj){ .max = JSON_MAX };
	JLIT(&j, "{\"path\":");
	hj_str(&j, req);
	JLIT(&j, ",\"entries\":[");

	while ((e = readdir(d))) {
		char full[XFER_PATH_MAX];
		struct stat st;
		size_t n = strlen(e->d_name);

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (e->d_name[0] == '.' && !hidden) continue;
		/* A transfer that died left this behind; it is not a file anyone
		 * asked for and showing it invites someone to try to use it. */
		if (n > 5 && !strcmp(e->d_name + n - 5, ".part")) continue;
		if (snprintf(full, sizeof full, "%s/%s", abs, e->d_name) >= (int)sizeof full)
			continue;
		if (stat(full, &st) != 0) continue;

		{
			char child[XFER_PATH_MAX];

			/* Skipped rather than truncated. A cut path is not this file's
			 * path, it is some other file's - and the browser would send it
			 * back as though it meant this one. Better to be absent from the
			 * listing than to be a name for the wrong thing. */
			if (snprintf(child, sizeof child, "%s/%s", req, e->d_name)
			    >= (int)sizeof child)
				continue;
			if (!first) JLIT(&j, ",");
			first = false;
			JLIT(&j, "{\"name\":");
			hj_str(&j, e->d_name);
			JLIT(&j, ",\"path\":");
			hj_str(&j, child);

			/* A game's cover, where the shelf reads it: <folder>/.media/
			 * <stem>.png. Named in the listing so the page can draw it beside
			 * the game; a stat per file, in a folder of games only. */
			if (S_ISREG(st.st_mode) && !strncmp(req, "roms/", 5) &&
			    !strstr(req, "/.media")) {
				char stem[XFER_NAME_MAX], cover[XFER_PATH_MAX], have[XFER_PATH_MAX];
				char *dot;
				struct stat cs;

				snprintf(stem, sizeof stem, "%s", e->d_name);
				if ((dot = strrchr(stem, '.'))) *dot = '\0';
				if (snprintf(have, sizeof have, "%s/.media/%s.png", abs, stem)
				    < (int)sizeof have &&
				    snprintf(cover, sizeof cover, "%s/.media/%s.png", req, stem)
				    < (int)sizeof cover &&
				    stat(have, &cs) == 0 && cs.st_size > 0) {
					JLIT(&j, ",\"cover\":");
					hj_str(&j, cover);
				}
			}
			/* A console's color and its pixel art on its folder, in ROMs
			 * and Save States: the icon the shelf draws, on a tile in the
			 * console's color. Every system the card knows, not only the
			 * shelves with games: the shelf hides an empty one, and its
			 * folder showed no color (Atomiswave, 2026-11-06). */
			if (S_ISDIR(st.st_mode) && systems && e->d_name[0] != '.') {
				const char *icon = g_folder_icon ? g_folder_icon(e->d_name) : NULL;
				unsigned c = g_folder_color ? g_folder_color(e->d_name) : 0xFFFFFFFFu;

				if (c <= 0xFFFFFF) jfmt(&j, ",\"color\":\"%06X\"", c);
				if (icon && icon_key_ok(icon, strlen(icon))) {
					JLIT(&j, ",\"icon\":");
					hj_str(&j, icon);
				}
			}
			/* And an album's or a book's, for a folder in Music or
			 * Audiobooks. An artist's folder has none, and shows none. */
			if (S_ISDIR(st.st_mode) && e->d_name[0] != '.' &&
			    (!strncmp(req, "music", 5) || !strncmp(req, "books", 5))) {
				char cover[XFER_PATH_MAX];

				if (holds_songs(full)) {
					JLIT(&j, ",\"album\":true");
					if (album_cover(abs, req, e->d_name, cover, sizeof cover)) {
						JLIT(&j, ",\"cover\":");
						hj_str(&j, cover);
					}
				} else {
					artist_covers(&j, abs, req, e->d_name);
				}
			}
		}
		jfmt(&j, ",\"dir\":%s,\"size\":%lld}",
		     S_ISDIR(st.st_mode) ? "true" : "false", (long long)st.st_size);
	}
	closedir(d);
	JLIT(&j, "]}");

	if (j.bad) httpd_reply_status(r, 500, j.over ? T(S_HARE_FOLDER_OVER)
	                                              : T(S_HARE_NO_MEMORY));
	else httpd_reply(r, 200, "application/json", j.p, j.used, NULL);
	free(j.p);
}

/* ---- the page's trailer ---------------------------------------------------
 *
 * Over the USB cable Safari never hands the page the answer to a PUT whose
 * body is an exact multiple of 131,072 bytes (128 KiB): the file lands whole,
 * this answers, and the page sat at 100% on that file for good, the queue
 * behind it stopped with it. fetch() hears the answer, but Safari then keeps
 * that connection busy for good, and it has six to a host.
 *
 * So the page sends a file of such a size with these 16 bytes after it, and
 * says so in the URL, trailer=16 (app.js, trailed()), and the body is never
 * a multiple of 128 KiB. Here the bytes are checked, every one, and cut off
 * the part-file before it takes the file's name: on the card, exactly what
 * the person chose. Every PUT of a file the page makes goes that way - the
 * uploads, the covers it fetches, the System Update files - and from any
 * browser, over either link: there is no telling Safari apart, and no need.
 *
 * An upload that does not say so (curl, a script, an older page) is stored
 * as sent, whatever its end. One that says another length, is shorter than
 * the trailer, or does not end with it is refused: nothing it carried is
 * stored, the file of that name is as it was, and its part-file goes. The
 * bytes are text a person can read in a capture, and long enough that a file
 * that happens to end in them, sent by a page that forgot them, is not a
 * worry. */
#define TRAILER "--zyperos-pad--\n"
#define TRAILER_LEN (sizeof TRAILER - 1)

/* How many bytes at the end of a PUT's body are the page's trailer: 0 when
 * the request does not say, TRAILER_LEN when it does, -1 when it says
 * something else or its body is too short to end with one */
static long trailer_of(const httpd_req *r)
{
	char want[8], got[8];

	if (!httpd_query(r, "trailer", got, sizeof got)[0]) return 0;
	snprintf(want, sizeof want, "%zu", TRAILER_LEN);
	if (strcmp(got, want) || httpd_content_len(r) < (long)TRAILER_LEN) return -1;
	return (long)TRAILER_LEN;
}

/* The trailer off a part-file that arrived whole: its last TRAILER_LEN bytes
 * checked to be it, then cut. False, with the part-file as it was, when they
 * are not or the cut fails: the caller removes it, and nothing is stored. */
static bool trailer_off(const char *part)
{
	char end[TRAILER_LEN];
	struct stat st;
	bool ok;
	int fd = open(part, O_RDWR | O_CLOEXEC);

	if (fd < 0) return false;
	ok = fstat(fd, &st) == 0 && st.st_size >= (off_t)TRAILER_LEN &&
	     pread(fd, end, sizeof end, st.st_size - (off_t)TRAILER_LEN) == (ssize_t)sizeof end &&
	     !memcmp(end, TRAILER, sizeof end) &&
	     ftruncate(fd, st.st_size - (off_t)TRAILER_LEN) == 0;
	if (close(fd) != 0) ok = false;
	return ok;
}

/* Is there room for `want` more bytes where `path` is going? */
static bool room_for(const char *path, long want)
{
	struct statvfs vfs;
	char dir[XFER_PATH_MAX];
	char *slash;

	snprintf(dir, sizeof dir, "%s", path);
	slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	if (statvfs(dir, &vfs) != 0) return true;      /* cannot tell: allow it */
	return (double)vfs.f_bavail * (double)vfs.f_frsize >= (double)want + (double)HARE_RESERVE;
}

/* The card's free and whole size, as the page adds a batch up against */
static bool card_space(long long *free, long long *total)
{
	struct statvfs vfs;
	const xfer_root *rt = xfer_root_at(0);

	if (g_space) return g_space(free, total);
	if (!rt || statvfs(rt->path, &vfs) != 0) return false;
	*free = (long long)vfs.f_bavail * (long long)vfs.f_frsize;
	*total = (long long)vfs.f_blocks * (long long)vfs.f_frsize;
	return true;
}

/* ---- the entry points httpd calls ------------------------------------ */

/* What a request has put on the screen, so on_end knows it has more to say. */
enum { TAG_SENDING = 1, TAG_RECEIVING = 2 };

/* A request is over. Only the two that put something on the screen have
 * anything to add to it.
 *
 * An upload still tagged never reached its second call - its body did not
 * arrive whole - so it failed however the connection ended, and its part-file
 * goes when httpd releases the request. A download says whether it went out
 * whole. */
static void on_end(httpd_req *r, bool whole, void *ctx)
{
	char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];
	int tag = httpd_tag(r);

	(void)ctx;
	if (!tag) return;
	/* Logs and an update's file name no root path: counted all the same */
	httpd_query(r, "p", req, sizeof req);
	if (!xfer_resolve(req, abs, sizeof abs)) {
		if (tag == TAG_SENDING && g_sends > 0) g_sends--;
		if (tag == TAG_RECEIVING && g_uploads > 0) g_uploads--;
		return;
	}
	if (tag == TAG_SENDING && g_sends > 0) g_sends--;
	if (tag == TAG_RECEIVING) {
		if (g_uploads > 0) g_uploads--;
		note(T(S_HARE_FAILED), base_of(abs));
	} else if (whole) {
		note(T(S_HARE_SENT), base_of(abs));
	} else {
		note(T(S_HARE_FAILED), base_of(abs));
	}
}

static void on_request(httpd_req *r, bool done, void *ctx)
{
	const char *m = httpd_method(r);
	const char *path = httpd_path(r);

	(void)ctx;
	/* A browser asked: File Transfer's last step (linksteps.h), and who it
	 * is, by its address and its session, for the count of browsers */
	if (!done) {
		const char *c = httpd_header(r, "Cookie"), *p = c ? strstr(c, "hare=") : NULL;
		char key[CLIENTS_KEY] = "";

		g_requests++;
		if (p && session_valid(r)) snprintf(key, sizeof key, "%.*s", CLIENTS_KEY - 1, p + 5);
		clients_note(&g_clients, httpd_peer(r), key, now_ms32());
	}

	/* The hotspot (hotspot.h): every name is the handheld there, so a
	 * request for another host is a phone checking for the internet, and
	 * the way to the page is the answer that opens its sign-in sheet on it
	 * (captive.h). Out of it, captive.js, the sheet's note, is empty. */
	if (g_captive[0] && !captive_host_is(httpd_header(r, "Host"), g_captive)) {
		char body[128], loc[96];
		int n = snprintf(body, sizeof body, "<a href=\"/\">%s</a>\n", T(S_CAPTIVE_LINK));

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		snprintf(loc, sizeof loc,
		         "Location: http://%s/\r\nCache-Control: no-store\r\n", g_captive);
		httpd_reply(r, 302, "text/html; charset=utf-8", body,
		            n > 0 && n < (int)sizeof body ? (size_t)n : 0, loc);
		return;
	}
	if (!g_captive[0] && !strcmp(path, "/web/captive.js")) {
		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		httpd_reply(r, 200, "text/javascript; charset=utf-8", "", 0, NULL);
		return;
	}

	/* ---- the parts that need no session ----------------------------- */
	if (!done && !strcmp(m, "POST") && !strcmp(path, "/api/auth")) {
		if (httpd_content_len(r) > 16) {
			httpd_reply_status(r, 400, "that is not a PIN");
			return;
		}
		httpd_want_body(r, HTTPD_BODY_MEM, NULL);
		return;
	}
	if (done && !strcmp(path, "/api/auth")) {
		size_t n;
		const char *body = httpd_body(r, &n);
		char cookie[128];

		if (now_ms32() < g_locked_until) {
			note("%s", T(S_HARE_LOCKED));
			httpd_reply_status(r, 429, "too many wrong PINs - wait a moment");
			return;
		}
		if (n != PIN_LEN || !secret_eq(body, g_pin, PIN_LEN)) {
			g_wrong++;
			if (++g_tries >= MAX_TRIES) {
				g_locked_until = now_ms32() + LOCKOUT_MS;
				g_tries = 0;
				note("%s", T(S_HARE_LOCKED));
			} else {
				note(T(S_HARE_WRONG_PIN), g_tries, MAX_TRIES);
			}
			/* The page says how many are left before the wait, which the
			 * device alone knows: a reload starts the page's own count over. */
			if (g_tries == 0) {
				httpd_reply_status(r, 401, "wrong PIN, now wait 30 seconds");
			} else {
				char why[48];

				snprintf(why, sizeof why, "wrong PIN, %d %s left", MAX_TRIES - g_tries,
				         MAX_TRIES - g_tries == 1 ? "try" : "tries");
				httpd_reply_status(r, 401, why);
			}
			return;
		}
		g_tries = 0;
		if (g_ntoken >= MAX_SESSIONS) g_ntoken = 0;    /* oldest falls off */
		if (!make_token(g_token[g_ntoken])) {
			httpd_reply_status(r, 500, "no randomness available");
			return;
		}
		snprintf(cookie, sizeof cookie,
		         "Set-Cookie: hare=%s; Path=/; HttpOnly; SameSite=Strict\r\n",
		         g_token[g_ntoken]);
		g_ntoken++;
		note("%s", T(S_HARE_SIGNED_IN));
		httpd_reply(r, 200, "text/plain", "ok", 2, cookie);
		return;
	}

	/* The page's words, in the language that is on (MENU > Language): its
	 * file in res/web/i18n, which tools/i18n.py writes, or English's where
	 * the language has none (the host's pseudo-locale). The page loads it
	 * before its script, so the PIN prompt needs it too. One URL for every
	 * language, and the tag is the file's (httpd_reply_static), so a browser
	 * asks every time and a new language is never answered with the last. */
	if (!done && !strcmp(m, "GET") && !strcmp(path, "/i18n.js")) {
		char file[XFER_PATH_MAX];
		struct stat st;
		int n = snprintf(file, sizeof file, "%s/i18n/%s.js", g_web, lang_code(i18n_lang()));

		if (n < 0 || n >= (int)sizeof file || stat(file, &st) != 0)
			n = snprintf(file, sizeof file, "%s/i18n/en.js", g_web);
		if (n < 0 || n >= (int)sizeof file) { httpd_reply_status(r, 404, "no"); return; }
		httpd_reply_static(r, file, mime_for(file));
		return;
	}

	/* Which link the page came over, for the name it shows (hare_set_cable):
	 * a line of script the page reads before its own, answered here rather
	 * than kept in res/web, so the page's files stay the same over either
	 * link, a card's older web folder (which never asks) still loads, and
	 * nothing a browser keeps from one link names the other: no tag and
	 * build_head's no-store, where the page's files are tagged and kept.
	 * Asked by no page from before it, and missing from a server that does
	 * not answer it, the page is Wi-Fi's, as it always was. */
	if (!done && !strcmp(m, "GET") && !strcmp(path, "/link.js")) {
		static const char usb[] = "window.ZYPEROS_LINK = \"usb\";\n";
		static const char wifi[] = "window.ZYPEROS_LINK = \"wifi\";\n";

		httpd_reply(r, 200, "text/javascript; charset=utf-8", g_cable ? usb : wifi,
		            g_cable ? sizeof usb - 1 : sizeof wifi - 1, NULL);
		return;
	}

	/* The page and its assets. Served before the session check, because the
	 * page IS the PIN prompt - requiring a session to reach it would be a
	 * lock on the door to the lock. */
	if (!done && !strcmp(m, "GET") &&
	    (!strcmp(path, "/") || !strncmp(path, "/web/", 5))) {
		char file[XFER_PATH_MAX];
		const char *name = !strcmp(path, "/") ? "index.html" : path + 5;

		/* From res/web on the card, so the page can be restyled without a
		 * rebuild. The name is checked the same way a rename target is:
		 * these are assets, not a directory to browse. */
		if (!xfer_name_ok(name)) { httpd_reply_status(r, 404, "no"); return; }
		if (snprintf(file, sizeof file, "%s/%s", g_web, name) >= (int)sizeof file) {
			httpd_reply_status(r, 404, "no");
			return;
		}
		httpd_reply_static(r, file, mime_for(name));
		return;
	}

	/* ---- everything below needs one --------------------------------- */
	if (!session_valid(r)) {
		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		httpd_reply_status(r, 401, "sign in first");
		return;
	}
	/* A signed-in browser asking is someone using it: what keeps a File
	 * Transfer hidden behind the shelf open (hare_quiet_ms). Counted only
	 * here, past the session, so guessing at the PIN never does. */
	if (!done) g_last_req = now_ms32();

	if (!strcmp(path, "/api/list")) {
		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); }
		if (done || httpd_content_len(r) == 0) route_list(r);
		return;
	}

	/* The shelf's pixel art for a folder (hare_set_icons): /icons/NES.png is
	 * <dir>/NES-still.png. The key is checked letter by letter, so nothing
	 * but a picture of that folder can be named. */
	if (!strncmp(path, "/icons/", 7) && !strcmp(m, "GET")) {
		const char *k = path + 7, *dot = strrchr(k, '.');
		char file[XFER_PATH_MAX];

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		if (!g_icons[0] || !dot || strcmp(dot, ".png") || !icon_key_ok(k, (size_t)(dot - k)) ||
		    snprintf(file, sizeof file, "%s/%.*s-still.png", g_icons, (int)(dot - k), k) >= (int)sizeof file) {
			httpd_reply_status(r, 404, "no such picture");
			return;
		}
		httpd_reply_file(r, file, "image/png", "Cache-Control: max-age=86400\r\n");
		return;
	}

	/* What the page asks before a batch and while one goes: the card's room
	 * (free, whole, and what it keeps whatever is sent), and whether the
	 * handheld is closing File Transfer after the files on their way. */
	if (!strcmp(path, "/api/state") && !strcmp(m, "GET")) {
		long long free = -1, total = -1;
		char json[192];
		int n;

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		card_space(&free, &total);
		n = snprintf(json, sizeof json,
		             "{\"closing\":%s,\"free\":%lld,\"total\":%lld,\"reserve\":%lld,\"uploads\":%d}",
		             g_closing ? "true" : "false", free, total, (long long)HARE_RESERVE, g_uploads);
		httpd_reply(r, 200, "application/json", json, (size_t)n, NULL);
		return;
	}

	/* System Update over the cable: see hare_set_update. */
	if (!strncmp(path, "/api/update", 11)) {
		int op = !strcmp(path, "/api/update") && !strcmp(m, "GET") ? HARE_UPDATE_STATE
		       : !strcmp(path, "/api/update/file") && !strcmp(m, "PUT") ? HARE_UPDATE_FILE
		       : !strcmp(path, "/api/update/check") && !strcmp(m, "POST") ? HARE_UPDATE_CHECK
		       : !strcmp(path, "/api/update/install") && !strcmp(m, "POST") ? HARE_UPDATE_INSTALL
		       : !strcmp(path, "/api/update/restart") && !strcmp(m, "POST") ? HARE_UPDATE_RESTART
		       : -1;
		char name[128], dest[XFER_PATH_MAX], part[XFER_PATH_MAX + 8], why[160] = "";
		size_t len = 0;
		long trail;
		char *json;

		if (op < 0 || !g_update) {
			if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
			httpd_reply_status(r, 404, op < 0 ? "no such thing" : "no updates here");
			return;
		}
		httpd_query(r, "name", name, sizeof name);
		if (op != HARE_UPDATE_FILE) {
			if (!done) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				if (httpd_content_len(r) > 0) return;   /* answered once it is gone */
			}
			reply_json(r, g_update(op, name, NULL, 0, &len, why, sizeof why), len, why, T(S_HARE_COULD_NOT));
			return;
		}
		/* A file: where it goes is the hook's to say, before a byte lands */
		dest[0] = '\0';
		if (!done) {
			if (g_closing) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 503, T(S_HARE_CLOSING_REPLY));
				return;
			}
			/* The page's trailer, as for any file (trailer_of) */
			if ((trail = trailer_of(r)) < 0) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 400, T(S_HARE_NOT_AS_SENT));
				return;
			}
			json = g_update(HARE_UPDATE_FILE, name, dest, sizeof dest, &len, why, sizeof why);
			if (!json || !dest[0] || snprintf(part, sizeof part, "%s.part", dest) >= (int)sizeof part) {
				free(json);
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 400, why[0] ? why : T(S_HARE_NOT_UPDATE_FILE));
				return;
			}
			free(json);
			if (!room_for(dest, httpd_content_len(r) - trail)) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 507, T(S_HARE_NO_ROOM));
				return;
			}
			httpd_want_body(r, HTTPD_BODY_FILE, part);
			g_uploads++;
			snprintf(g_file, sizeof g_file, "%s", name);
			note(T(S_HARE_RECEIVING), name);
			httpd_set_tag(r, TAG_RECEIVING);
			return;
		}
		if (g_uploads > 0) g_uploads--;
		httpd_set_tag(r, 0);
		json = g_update(HARE_UPDATE_FILE, name, dest, sizeof dest, &len, why, sizeof why);
		free(json);
		if (!dest[0] || snprintf(part, sizeof part, "%s.part", dest) >= (int)sizeof part) {
			httpd_reply_status(r, 500, T(S_HARE_NOT_PLACED));
			return;
		}
		if ((trail = trailer_of(r)) < 0 || (trail && !trailer_off(part))) {
			remove(part);
			httpd_reply_status(r, 400, T(S_HARE_NOT_AS_SENT));
			note(T(S_HARE_FAILED), name);
			return;
		}
		if (rename(part, dest) != 0) {
			remove(part);
			httpd_reply_status(r, 500, T(S_HARE_NOT_PLACED));
			note(T(S_HARE_FAILED), name);
			return;
		}
		note(T(S_HARE_RECEIVED), name);
		reply_json(r, g_update(HARE_UPDATE_FILED, name, dest, sizeof dest, &len, why, sizeof why),
		           len, why, T(S_HARE_COULD_NOT));
		return;
	}

	/* Box art: which games want covers, and which cover each gets from a
	 * collection's names. See hare_set_art. */
	if (!strcmp(path, "/api/art/wanted") && !strcmp(m, "GET")) {
		char why[128] = "";
		size_t len = 0;
		char *json;

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		if (!g_art_wanted) { httpd_reply_status(r, 404, "no box art here"); return; }
		if (!(json = g_art_wanted(&len, why, sizeof why))) {
			httpd_reply_status(r, 500, why[0] ? why : T(S_HARE_COULD_NOT_LOOK));
			return;
		}
		httpd_reply(r, 200, "application/json", json, len, NULL);
		free(json);
		return;
	}
	/* The same body either way: a collection's names to match by, or its
	 * No-Intro list to match what they left by checksum. */
	if ((!strcmp(path, "/api/art/match") || !strcmp(path, "/api/art/crc") ||
	     !strcmp(path, "/api/art/nointro")) && !strcmp(m, "POST")) {
		hare_art_fn fn = !strcmp(path, "/api/art/crc") ? g_art_crc
		               : !strcmp(path, "/api/art/nointro") ? g_art_nointro : g_art_match;
		char raw[XFER_PATH_MAX], folder[XFER_PATH_MAX], why[128] = "";
		size_t len = 0;
		char *body, *json;

		if (!done) {
			long n = httpd_content_len(r);

			if (!fn) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 404, "no box art here");
				return;
			}
			if (n <= 0 || n > ART_NAMES_MAX) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 413, T(S_HARE_NOT_NAMES));
				return;
			}
			httpd_want_body(r, HTTPD_BODY_FILE, ART_NAMES_TMP);
			return;
		}
		httpd_query(r, "s", raw, sizeof raw);
		body = file_read(ART_NAMES_TMP, ART_NAMES_MAX + 1, &len);
		remove(ART_NAMES_TMP);
		if (!raw[0] || !xfer_decode(raw, folder, sizeof folder)) {
			free(body);
			httpd_reply_status(r, 400, T(S_HARE_WHICH_SHELF));
			return;
		}
		if (!body) { httpd_reply_status(r, 500, T(S_HARE_NAMES_LOST)); return; }
		json = fn(folder, body, &len, why, sizeof why);
		free(body);
		if (!json) {
			httpd_reply_status(r, 500, why[0] ? why : T(S_HARE_COULD_NOT_MATCH));
			return;
		}
		note(T(S_HARE_BOX_ART), folder);
		httpd_reply(r, 200, "application/json", json, len, NULL);
		free(json);
		return;
	}

	/* Album covers: which albums want one, and which release a search reply
	 * names. See hare_set_albums. */
	if (!strcmp(path, "/api/albums/wanted") && !strcmp(m, "GET")) {
		char why[128] = "";
		size_t len = 0;

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		if (!g_albums_wanted) { httpd_reply_status(r, 404, "no album covers here"); return; }
		reply_json(r, g_albums_wanted(&len, why, sizeof why), len, why, T(S_HARE_COULD_NOT_LOOK));
		return;
	}
	if (!strcmp(path, "/api/albums/pick") && !strcmp(m, "POST")) {
		char idq[16], why[128] = "";
		size_t len = 0, n;
		char *reply;

		if (!done) {
			long cl = httpd_content_len(r);

			if (!g_albums_pick) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 404, "no album covers here");
				return;
			}
			if (cl <= 0 || cl > PICK_MAX) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 413, T(S_HARE_NOT_SEARCH));
				return;
			}
			httpd_want_body(r, HTTPD_BODY_FILE, PICK_TMP);
			return;
		}
		httpd_query(r, "id", idq, sizeof idq);
		reply = file_read(PICK_TMP, PICK_MAX + 1, &n);
		remove(PICK_TMP);
		if (!idq[0] || !reply) {
			free(reply);
			httpd_reply_status(r, 400, T(S_HARE_WHICH_ALBUM));
			return;
		}
		n = strlen(reply);
		reply_json(r, g_albums_pick(atoi(idq), reply, n, &len, why, sizeof why),
		           len, why, T(S_HARE_COULD_NOT_PICK));
		free(reply);
		return;
	}

	/* The SteamGridDB key: given and taken off, never read back. See
	 * hare_set_sgdb. */
	if (!strncmp(path, "/api/sgdb", 9)) {
		int op = !strcmp(path, "/api/sgdb") && !strcmp(m, "GET") ? HARE_SGDB_STATE
		       : !strcmp(path, "/api/sgdb/key") && !strcmp(m, "POST") ? HARE_SGDB_KEY
		       : !strcmp(path, "/api/sgdb/remove") && !strcmp(m, "POST") ? HARE_SGDB_REMOVE
		       : -1;
		char why[128] = "";
		size_t len = 0, n = 0;
		const char *body = NULL;

		if (!done) {
			long cl = httpd_content_len(r);

			if (op < 0 || !g_sgdb) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 404, op < 0 ? "no such thing" : "no SteamGridDB here");
				return;
			}
			if (op == HARE_SGDB_KEY) {
				if (cl <= 0 || cl > SGDB_BODY_MAX) {
					httpd_want_body(r, HTTPD_BODY_NONE, NULL);
					httpd_reply_status(r, 413, T(S_HARE_NOT_KEY));
					return;
				}
				httpd_want_body(r, HTTPD_BODY_MEM, NULL);
				return;
			}
			httpd_want_body(r, HTTPD_BODY_NONE, NULL);
			if (cl > 0) return;     /* answered once the body is gone */
		}
		if (op == HARE_SGDB_KEY) body = httpd_body(r, &n);
		reply_json(r, g_sgdb(op, body ? body : "", n, &len, why, sizeof why),
		           len, why, T(S_HARE_COULD_NOT));
		/* The key itself, in the request's own buffer: gone before httpd
		 * frees it, so no later allocation can hand it on. */
		if (body && n) memset((char *)body, 0, n);
		if (op == HARE_SGDB_KEY) note("%s", T(S_HARE_SGDB_CHECKING));
		else if (op == HARE_SGDB_REMOVE) note("%s", T(S_HARE_SGDB_REMOVED));
		return;
	}

	/* Download logs: packed when asked, named for the day, and sent. The
	 * pack is removed as soon as it is open - the stream keeps it alive - so
	 * nothing is left in /tmp however the download ends. */
	if (!strcmp(path, "/api/logs") && !strcmp(m, "GET")) {
		char pack[XFER_PATH_MAX], name[128], extra[256];

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		if (!g_pack_logs || !g_pack_logs(pack, sizeof pack, name, sizeof name)) {
			httpd_reply_status(r, 500, T(S_HARE_LOGS_FAILED));
			return;
		}
		snprintf(extra, sizeof extra,
		         "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
		if (httpd_reply_file(r, pack, "application/gzip", extra)) {
			note("%s", T(S_HARE_LOGS));
			httpd_set_tag(r, TAG_SENDING);
			g_sends++;
		}
		remove(pack);
		return;
	}

	if (!strcmp(path, "/api/file") && !strcmp(m, "GET")) {
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];
		char extra[XFER_PATH_MAX + 64];

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_LOOK));
			return;
		}
		/* Named, so a browser saves it as itself rather than as "file". The
		 * name is quoted and cannot contain a quote - xfer_name_ok refused
		 * control bytes and the filesystem refuses the rest. */
		if (snprintf(extra, sizeof extra,
		             "Content-Disposition: attachment; filename=\"%s\"\r\n",
		             base_of(abs)) >= (int)sizeof extra) {
			/* Dropped whole rather than cut. A truncated header has no
			 * closing quote and no CRLF, so it does not become a shorter
			 * header - it becomes a broken response, with the next header
			 * running into this one. The download still works; the browser
			 * just names the file from the URL. */
			extra[0] = '\0';
		}
		/* A cover drawn beside a game in the list (thumb=1) is not a download
		 * anyone asked for: a folder of them would fill the screen's last
		 * line with covers, one after another. */
		{
			char thumb[4];

			if (httpd_query(r, "thumb", thumb, sizeof thumb)[0]) {
				httpd_reply_file(r, abs, mime_for(abs), NULL);
				return;
			}
		}
		if (g_closing) {
			httpd_reply_status(r, 503, T(S_HARE_CLOSING_REPLY));
			return;
		}
		/* Only once the file is really going out: a 404 said "sending" too. */
		if (httpd_reply_file(r, abs, mime_for(abs), extra[0] ? extra : NULL)) {
			note(T(S_HARE_SENDING), base_of(abs));
			httpd_set_tag(r, TAG_SENDING);
			g_sends++;
		}
		return;
	}

	if (!strcmp(path, "/api/file") && !strcmp(m, "PUT")) {
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];
		long trail = trailer_of(r);

		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		if (!done) {
			char part[XFER_PATH_MAX];

			if (g_closing) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 503, T(S_HARE_CLOSING_REPLY));
				return;
			}
			/* Before room_for, because a name the card cannot store is not a
			 * space problem and must not be reported as one. xfer_resolve
			 * checks where a path goes, not whether its last component is a
			 * legal filename here - that is this call. */
			if (!xfer_name_ok(base_of(abs))) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 400, T(S_HARE_BAD_NAME));
				return;
			}
			/* A trailer this does not know, before a byte lands: refused
			 * here, nothing is written and nothing replaced */
			if (trail < 0) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 400, T(S_HARE_NOT_AS_SENT));
				return;
			}
			/* The file's own size, without the page's trailer */
			if (!room_for(abs, httpd_content_len(r) - trail)) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 507, T(S_HARE_NO_ROOM));
				return;
			}
			/* A cover into a console's or an album's .media: the folder is
			 * made by what puts the first cover there (mk/payload.sh no
			 * longer ships an empty one per console), so it is made here,
			 * that one level and only under a folder that is there. */
			{
				char dir[XFER_PATH_MAX];
				char *sl;

				snprintf(dir, sizeof dir, "%s", abs);
				if ((sl = strrchr(dir, '/'))) {
					*sl = '\0';
					if (!strcmp(base_of(dir), ".media")) mkdir(dir, 0777);
				}
			}
			/* Into a part-file and renamed at the end, so a transfer that
			 * dies halfway leaves the original alone rather than replacing it
			 * with the front half of something else. The same argument as
			 * atomic.c makes for every config this launcher writes, and it
			 * matters more here: the thing being replaced may be the only
			 * copy of a save. */
			if (snprintf(part, sizeof part, "%s.part", abs) >= (int)sizeof part) {
				httpd_want_body(r, HTTPD_BODY_NONE, NULL);
				httpd_reply_status(r, 400, T(S_HARE_LONG_NAME));
				return;
			}
			httpd_want_body(r, HTTPD_BODY_FILE, part);
			g_uploads++;
			snprintf(g_file, sizeof g_file, "%s", base_of(abs));
			note(T(S_HARE_RECEIVING), base_of(abs));
			httpd_set_tag(r, TAG_RECEIVING);
			return;
		}
		{
			char part[XFER_PATH_MAX];

			/* Checked again, though the first call already refused a name
			 * this long: the two have to agree about where the bytes went,
			 * and "it cannot happen here because of something forty lines
			 * up" is a thing that stops being true when one of them moves. */
			if (g_uploads > 0) g_uploads--;
			/* The body is here. What happens to it is said below, not by
			 * on_end. */
			httpd_set_tag(r, 0);
			if (snprintf(part, sizeof part, "%s.part", abs) >= (int)sizeof part) {
				httpd_reply_status(r, 500, T(S_HARE_LONG_NAME));
				return;
			}
			/* What the page sent, and nothing it added: the trailer checked
			 * and cut, or the whole part-file goes and the file of this name
			 * stays as it was */
			if (trail < 0 || (trail && !trailer_off(part))) {
				remove(part);
				httpd_reply_status(r, 400, T(S_HARE_NOT_AS_SENT));
				note(T(S_HARE_FAILED), base_of(abs));
				return;
			}
			if (rename(part, abs) != 0) {
				remove(part);
				httpd_reply_status(r, 500, T(S_HARE_NOT_PLACED));
				note(T(S_HARE_FAILED), base_of(abs));
				return;
			}
			note_write(abs);
			if (g_after_write) g_after_write(abs);
			note(T(S_HARE_RECEIVED), base_of(abs));
			httpd_reply(r, 200, "text/plain", "ok", 2, NULL);
		}
		return;
	}

	if (!strcmp(path, "/api/rename") && !strcmp(m, "POST")) {
		char req[XFER_PATH_MAX], to[XFER_NAME_MAX];
		char abs[XFER_PATH_MAX], dst[XFER_PATH_MAX];
		char *slash;
		bool music;

		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); return; }
		httpd_query(r, "p", req, sizeof req);
		httpd_query(r, "to", to, sizeof to);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		/* The destination is decoded HERE and checked as one name. It is not
		 * run through xfer_resolve, because a rename names a sibling: letting
		 * it be a path would let a rename move a file to another root. */
		{
			char dec[XFER_NAME_MAX];

			if (!xfer_decode(to, dec, sizeof dec) || !xfer_name_ok(dec)) {
				httpd_reply_status(r, 400, T(S_HARE_BAD_NAME));
				return;
			}
			snprintf(dst, sizeof dst, "%s", abs);
			slash = strrchr(dst, '/');
			if (!slash) { httpd_reply_status(r, 400, T(S_HARE_CANNOT_RENAME)); return; }
			if ((size_t)(slash - dst) + 1 + strlen(dec) >= sizeof dst) {
				httpd_reply_status(r, 400, T(S_HARE_LONG_NAME));
				return;
			}
			strcpy(slash + 1, dec);
		}
		/* In Music or Audiobooks, Muse stopped first if it plays this or from
		 * in here, and the launcher told after, as for Tidy names */
		music = tidy_root(abs) != NULL;
		if (music && g_before_delete) g_before_delete(abs);
		if (rename(abs, dst) != 0) {
			httpd_reply_status(r, 500, strerror(errno));
			return;
		}
		if (music && g_after_rename) g_after_rename(abs, dst, false);
		note_write(dst);
		note_write(abs);   /* it left one name and arrived at another */
		note(T(S_HARE_RENAMED), base_of(dst));
		httpd_reply(r, 200, "text/plain", "ok", 2, NULL);
		return;
	}

	if (!strcmp(path, "/api/tidy") && (!strcmp(m, "GET") || !strcmp(m, "POST"))) {
		static tidy_item it[TIDY_MAX];
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX], from[XFER_PATH_MAX], to[XFER_PATH_MAX];
		const xfer_root *rt;
		bool apply = !strcmp(m, "POST");
		int n, i, renamed = 0, skipped = 0;
		struct stat st;
		hj j = { 0 };

		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); if (apply) return; }
		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		if (stat(abs, &st) != 0 || !S_ISDIR(st.st_mode)) {
			httpd_reply_status(r, 404, T(S_HARE_NOT_FOLDER));
			return;
		}
		if (!(rt = tidy_root(abs))) {
			httpd_reply_status(r, 400, T(S_HARE_TIDY_WHERE));
			return;
		}
		if ((n = tidy_plan(abs, rt, it, TIDY_MAX)) < 0) {
			httpd_reply_status(r, 500, strerror(errno));
			return;
		}
		if (apply) {
			/* Muse stopped first if it plays from in here */
			if (n && g_before_delete) g_before_delete(abs);
			for (i = 0; i < n; i++) {
				if (it[i].skip) { skipped++; continue; }
				if (it[i].folder) continue;
				if (snprintf(from, sizeof from, "%s/%s", abs, it[i].from) >= (int)sizeof from ||
				    snprintf(to, sizeof to, "%s/%s", abs, it[i].to) >= (int)sizeof to) {
					skipped++;
					continue;
				}
				if (rename(from, to) == 0) renamed++; else skipped++;
			}
			snprintf(from, sizeof from, "%s", abs);
			for (i = 0; i < n; i++) {
				if (!it[i].folder || it[i].skip) continue;
				if (snprintf(to, sizeof to, "%.*s/%s", (int)(strrchr(abs, '/') - abs), abs,
				             it[i].to) >= (int)sizeof to) {
					skipped++;
					continue;
				}
				if (rename(from, to) == 0) {
					renamed++;
					note_write(from);
					snprintf(abs, sizeof abs, "%s", to);
				} else {
					skipped++;
				}
			}
			/* Once, whatever was renamed: the folder's old path and its new */
			if (renamed && g_after_rename) g_after_rename(from, abs, true);
			note_write(abs);
			note(TN(S_HARE_TIDIED, renamed), renamed, base_of(abs));
		}
		j.max = 512 + (size_t)n * (2 * XFER_NAME_MAX + 64);
		if (apply) {
			jfmt(&j, "{\"renamed\":%d,\"skipped\":%d,\"folder\":", renamed, skipped);
			hj_str(&j, strrchr(abs, '/') + 1);
			JLIT(&j, "}");
		} else {
			JLIT(&j, "{\"items\":[");
			for (i = 0; i < n; i++) {
				if (i) JLIT(&j, ",");
				JLIT(&j, "{\"from\":");
				hj_str(&j, it[i].from);
				JLIT(&j, ",\"to\":");
				hj_str(&j, it[i].to);
				jfmt(&j, ",\"folder\":%s,\"skip\":%s}", it[i].folder ? "true" : "false",
				     it[i].skip ? "true" : "false");
			}
			JLIT(&j, "]}");
		}
		if (j.bad)  httpd_reply_status(r, 500, j.over ? T(S_HARE_REPLY_LONG)
		                                              : T(S_HARE_NO_MEMORY));
		else        httpd_reply(r, 200, "application/json", j.p, j.used, NULL);
		free(j.p);
		return;
	}

	if (!strcmp(path, "/api/delete") && !strcmp(m, "POST")) {
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];
		struct stat st;

		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); return; }
		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		if (stat(abs, &st) != 0) { httpd_reply_status(r, 404, T(S_HARE_NOT_THERE)); return; }
		/* A folder goes as xfer_delete_rule says, and only with everything
		 * in it when the page asked for that by name (all=1), having shown
		 * what is inside first - see /api/count. It was rmdir alone once:
		 * one click should not take a whole system's ROMs, and a folder
		 * that had to be emptied first was a confirmation nobody had to
		 * design. That still holds where it matters, and an album or a
		 * book no longer has to be emptied a file at a time. */
		if (S_ISDIR(st.st_mode)) {
			const char *why;
			xfer_del rule = xfer_delete_rule(abs, &why);
			char all[4];

			httpd_query(r, "all", all, sizeof all);
			if (rule == XFER_DEL_NO) { httpd_reply_status(r, 403, why); return; }
			if (rmdir(abs) != 0) {
				int files = 0, folders = 0, n;

				if (rule != XFER_DEL_ALL) { httpd_reply_status(r, 409, why); return; }
				if (strcmp(all, "1") != 0) {
					httpd_reply_status(r, 409, T(S_HARE_NOT_EMPTY));
					return;
				}
				n = tree_count(abs, 0, HARE_DELETE_MAX, &files, &folders);
				if (n > HARE_DELETE_MAX) {
					httpd_reply_status(r, 413, T(S_HARE_TOO_MUCH));
					return;
				}
				if (g_before_delete) g_before_delete(abs);
				if (!tree_delete(abs, 0)) {
					httpd_reply_status(r, 500, T(S_HARE_PARTLY_DELETED));
					note_write(abs);
					return;
				}
			}
		} else if (remove(abs) != 0) {
			httpd_reply_status(r, 500, strerror(errno));
			return;
		}
		note_write(abs);
		note(T(S_HARE_DELETED), base_of(abs));
		httpd_reply(r, 200, "text/plain", "ok", 2, NULL);
		return;
	}

	/* What deleting a folder would take, asked before the page asks you:
	 * how much is in it, and whether it may go whole, only empty, or not at
	 * all - xfer_delete_rule, with the reason when it will not. */
	if (!strcmp(path, "/api/count") && !strcmp(m, "GET")) {
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];
		const char *why, *rule;
		int files = 0, folders = 0, n;
		struct stat st;
		hj j = { 0 };

		if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_LOOK));
			return;
		}
		if (stat(abs, &st) != 0 || !S_ISDIR(st.st_mode)) {
			httpd_reply_status(r, 404, T(S_HARE_NOT_FOLDER));
			return;
		}
		switch (xfer_delete_rule(abs, &why)) {
		case XFER_DEL_ALL:   rule = "all";   break;
		case XFER_DEL_EMPTY: rule = "empty"; break;
		default:             rule = "no";    break;
		}
		n = tree_count(abs, 0, HARE_DELETE_MAX, &files, &folders);
		j.max = 512;
		jfmt(&j, "{\"files\":%d,\"folders\":%d,\"too_many\":%s,\"rule\":\"%s\",\"why\":",
		     files, folders, n > HARE_DELETE_MAX ? "true" : "false", rule);
		hj_str(&j, why);
		jfmt(&j, "}");
		if (j.bad)  httpd_reply_status(r, 500, j.over ? T(S_HARE_REPLY_LONG)
		                                              : T(S_HARE_NO_MEMORY));
		else        httpd_reply(r, 200, "application/json", j.p, j.used, NULL);
		free(j.p);
		return;
	}

	/* Drag and drop on the page (2026-11-13): entry `p` - a file, or a
	 * folder, a disc set whole - into folder `to`, on the card. A loose
	 * sheet takes the files it names beside it and the files kept by its
	 * stem (src/sheet.h), so a .cue never leaves its .bin behind. All or
	 * nothing: a name already in `to` refuses the whole move, and says
	 * which. A game's art, saves and states go by its name, which a move
	 * does not change, so they stay where they are. */
	if (!strcmp(path, "/api/move") && !strcmp(m, "POST")) {
		static char from[MOVE_MAX][XFER_PATH_MAX], into[MOVE_MAX][XFER_PATH_MAX];
		static sheet_names sn;
		char req[XFER_PATH_MAX], toq[XFER_PATH_MAX], abs[XFER_PATH_MAX], dir[XFER_PATH_MAX];
		char parent[XFER_PATH_MAX], msg[512];
		const char *base, *with[2];
		const xfer_root *rt;
		struct stat st, ds;
		int n = 0, i, moved;
		bool music;

		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); return; }
		httpd_query(r, "p", req, sizeof req);
		httpd_query(r, "to", toq, sizeof toq);
		if (!xfer_resolve(req, abs, sizeof abs) || !xfer_resolve(toq, dir, sizeof dir)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		/* Within one root, and never a root itself (the query is still
		 * encoded: the resolved paths are what say it). A game moves among
		 * its console's folders; nothing crosses into Saves or the states. */
		rt = move_root(abs);
		if (!rt || rt != move_root(dir) || !strcmp(abs, rt->path)) {
			httpd_reply_status(r, 400, T(S_HARE_CANNOT_MOVE));
			return;
		}
		if (lstat(abs, &st) != 0) { httpd_reply_status(r, 404, strerror(errno)); return; }
		/* A folder only when a delete would take it whole: a console's own
		 * folder under ROMs, or one in BIOS or Saves, stays where the shelf
		 * and the cores look for it (xfer_delete_rule) */
		if (S_ISDIR(st.st_mode)) {
			const char *why = NULL;

			if (xfer_delete_rule(abs, &why) != XFER_DEL_ALL) {
				httpd_reply_status(r, 403, why ? why : T(S_HARE_CANNOT_MOVE));
				return;
			}
		}
		if (stat(dir, &ds) != 0 || !S_ISDIR(ds.st_mode)) {
			httpd_reply_status(r, 400, T(S_HARE_CANNOT_MOVE));
			return;
		}
		base = base_of(abs);
		snprintf(parent, sizeof parent, "%.*s", (int)(base - 1 - abs), abs);
		/* Into the folder it is in already: nothing to do */
		if (!strcmp(parent, dir)) { httpd_reply(r, 200, "text/plain", "ok", 2, NULL); return; }
		/* A folder into itself, or a folder in it */
		if (!strcmp(dir, abs) || (!strncmp(dir, abs, strlen(abs)) && dir[strlen(abs)] == '/')) {
			httpd_reply_status(r, 400, T(S_HARE_MOVE_INTO_ITSELF));
			return;
		}
		snprintf(from[n], XFER_PATH_MAX, "%s", abs);
		if (snprintf(into[n], XFER_PATH_MAX, "%s/%s", dir, base) >= XFER_PATH_MAX) {
			httpd_reply_status(r, 400, T(S_HARE_LONG_NAME));
			return;
		}
		n++;
		if (S_ISREG(st.st_mode)) {
			char stem[XFER_NAME_MAX + 1];
			const char *dot = strrchr(base, '.');
			int k, nw;

			sn.n = 0;
			sn.outside = false;
			if (sheet_is(base)) sheet_read(parent, base, &sn, 0);
			nw = sheet_sides(base, with);
			snprintf(stem, sizeof stem, "%.*s", dot && dot != base ? (int)(dot - base) : (int)strlen(base), base);
			for (k = 0; k < nw && sn.n < SHEET_NAMES; k++)
				snprintf(sn.name[sn.n++], SHEET_PATH, "%s.%s", stem, with[k]);
			/* Only what is there, beside it in its own folder: a sheet's
			 * name in another folder, or one that leaves it, stays put */
			for (k = 0; k < sn.n && n < MOVE_MAX; k++) {
				struct stat ts;
				bool dup = false;

				size_t nl = strlen(sn.name[k]);

				if (strchr(sn.name[k], '/') || !strcasecmp(sn.name[k], base)) continue;
				/* Nothing a root keeps from the page, by a sheet's naming it */
				if (rt->no_cfg && nl > 4 && !strcasecmp(sn.name[k] + nl - 4, ".cfg")) continue;
				snprintf(from[n], XFER_PATH_MAX, "%s/%s", parent, sn.name[k]);
				for (i = 0; i < n; i++)
					if (!strcmp(from[i], from[n])) dup = true;
				if (dup || lstat(from[n], &ts) != 0 || !S_ISREG(ts.st_mode)) continue;
				if (snprintf(into[n], XFER_PATH_MAX, "%s/%s", dir, sn.name[k]) >= XFER_PATH_MAX) {
					httpd_reply_status(r, 400, T(S_HARE_LONG_NAME));
					return;
				}
				n++;
			}
		}
		/* A name already in the folder: the whole move refused, by name */
		for (i = 0; i < n; i++) {
			struct stat ts;

			if (lstat(into[i], &ts) == 0) {
				snprintf(msg, sizeof msg, T(S_HARE_MOVE_EXISTS), base_of(into[i]), base_of(dir));
				httpd_reply_status(r, 409, msg);
				return;
			}
		}
		/* In Music or Audiobooks, Muse stopped first if it plays this, and
		 * the launcher told after, as for a rename */
		music = tidy_root(abs) != NULL;
		if (music && g_before_delete) g_before_delete(abs);
		for (moved = 0; moved < n; moved++)
			if (rename(from[moved], into[moved]) != 0) break;
		if (moved < n) {
			int err = errno;

			/* Back where they were, what went, so a game is never split */
			while (moved-- > 0) rename(into[moved], from[moved]);
			httpd_reply_status(r, err == EXDEV ? 400 : 500,
			                   err == EXDEV ? T(S_HARE_CANNOT_MOVE) : strerror(err));
			return;
		}
		if (music && g_after_rename) g_after_rename(abs, into[0], false);
		for (i = 0; i < n; i++) {
			note_write(into[i]);
			note_write(from[i]);   /* it left one folder and arrived in another */
		}
		note(T(S_HARE_MOVED), base);
		httpd_reply(r, 200, "text/plain", "ok", 2, NULL);
		return;
	}

	if (!strcmp(path, "/api/mkdir") && !strcmp(m, "POST")) {
		char req[XFER_PATH_MAX], abs[XFER_PATH_MAX];

		if (!done) { httpd_want_body(r, HTTPD_BODY_NONE, NULL); return; }
		httpd_query(r, "p", req, sizeof req);
		if (!xfer_resolve(req, abs, sizeof abs)) {
			httpd_reply_status(r, 403, T(S_HARE_CANNOT_WRITE));
			return;
		}
		if (!xfer_name_ok(base_of(abs))) {
			httpd_reply_status(r, 400, T(S_HARE_BAD_NAME));
			return;
		}
		if (mkdir(abs, 0777) != 0) {
			httpd_reply_status(r, 409, strerror(errno));
			return;
		}
		note(T(S_HARE_MADE), base_of(abs));
		httpd_reply(r, 200, "text/plain", "ok", 2, NULL);
		return;
	}

	if (!done) httpd_want_body(r, HTTPD_BODY_NONE, NULL);
	httpd_reply_status(r, 404, "no such thing here");
}

/* ---- opening and closing --------------------------------------------- */

/* Part-files from a transfer that died. Swept at open rather than at close,
 * because the death that leaves one is usually the death that stops close
 * from running. */
static void sweep_parts(const char *dir, int depth)
{
	DIR *d;
	struct dirent *e;

	if (depth > 3) return;
	d = opendir(dir);
	if (!d) return;
	while ((e = readdir(d))) {
		char full[XFER_PATH_MAX];
		struct stat st;
		size_t n = strlen(e->d_name);

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >= (int)sizeof full)
			continue;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) sweep_parts(full, depth + 1);
		else if (n > 5 && !strcmp(e->d_name + n - 5, ".part")) remove(full);
	}
	closedir(d);
}

bool hare_start(const char *roms_dir, const char *card_dir,
                const char *shared_dir, const char *web_dir)
{
	/* 80 first, so the address on the screen is one somebody can type without
	 * a colon in it. It needs root, which this process has; 8080 is there for
	 * anything that has taken 80 already. The checks build with HARE_PORTS 0,
	 * a port the kernel picks, so two of them never want the same one. */
#ifndef HARE_PORTS
#define HARE_PORTS 80, 8080, 8081
#endif
	static const int ports[] = { HARE_PORTS };
	int i;

	xfer_init(roms_dir, card_dir, shared_dir);
	snprintf(g_web, sizeof g_web, "%s", web_dir);
	if (!make_pin()) return false;
	g_ntoken = 0;
	g_tries = 0;
	g_wrong = 0;
	g_locked_until = 0;
	g_uploads = 0;
	g_sends = 0;
	g_closing = false;
	g_last_req = now_ms32();
	g_file[0] = '\0';
	g_shelf_changed = false;
	g_in = g_out = 0;
	g_last[0] = '\0';
	g_requests = 0;
	clients_clear(&g_clients);

	/* A root that is missing is one nobody can make: the page does not edit
	 * the roots themselves, and the card's top level is not a root. v1.0
	 * shipped no Music/, and updating only ZyperOS/ does not bring one. EEXIST
	 * is the usual answer and is ignored with the rest. */
	for (i = 0; i < xfer_root_count(); i++) {
		mkdir(xfer_root_at(i)->path, 0777);
		sweep_parts(xfer_root_at(i)->path, 0);
	}

	if (!httpd_start(ports, (int)(sizeof ports / sizeof ports[0]))) return false;
	note("%s", T(S_HARE_WAITING));
	return true;
}

void hare_stop(void)
{
	httpd_stop();
	/* The PIN and every session die with the screen. There is nothing left
	 * listening and nothing left to guess at. */
	memset(g_pin, 0, sizeof g_pin);
	memset(g_token, 0, sizeof g_token);
	g_ntoken = 0;
	g_uploads = 0;
	g_sends = 0;
	g_closing = false;
	g_captive[0] = '\0';   /* the next screen says again whether it is one */
	g_cable = false;       /* and which link it is */
}

const char *hare_pin(void) { return g_pin; }
int hare_wrong_pins(void) { return g_wrong; }
int hare_port(void) { return httpd_port(); }

int hare_poll(void)
{
	unsigned long in = 0, out = 0;
	int did = httpd_poll(on_request, on_end, NULL);

	httpd_traffic(&in, &out);
	g_in += in;
	g_out += out;
	return did;
}

void hare_status(hare_stats *out)
{
	if (!out) return;
	{
		const char *open[64];
		int n = httpd_peers(open, 64);

		out->clients = clients_count(&g_clients, open, n, now_ms32());
	}
	out->requests = g_requests;
	out->in = g_in;
	out->out = g_out;
	out->uploads = g_uploads;
	out->sends = g_sends;
	snprintf(out->last, sizeof out->last, "%s", g_last);
	snprintf(out->file, sizeof out->file, "%s", g_file);
	httpd_body_progress(&out->up_got, &out->up_total);
	g_in = g_out = 0;
}
