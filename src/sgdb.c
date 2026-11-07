/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See sgdb.h for what this is and why the key is handled as it is. */
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "sgdb.h"

#include "artscrape.h"
#include "db.h"
#include "i18n.h"
#include "net.h"
#include "rajson.h"

#define K_KEY   "sgdb.key"     /* hidden by db_dump: see db.c's secret_key */
#define K_STATE "sgdb.state"   /* "works", "unchecked" or "refused" */

/* How long the check on entry may take. Short, because someone is looking at
 * a wait panel; a slow answer is KEPT, and the next run asks again. */
#ifndef SGDB_CHECK_S
#define SGDB_CHECK_S 15
#endif

static char       g_key[SGDB_KEY_MAX];
static sgdb_state g_state;

/* ---- the key ------------------------------------------------------------ */

static const char *state_id(sgdb_state st)
{
	return st == SGDB_WORKS ? "works" : st == SGDB_REFUSED ? "refused" : "unchecked";
}

bool sgdb_key_clean(const char *in, char *out, size_t n)
{
	const char *b, *e;
	size_t len, i;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!in) return false;
	for (b = in; *b && isspace((unsigned char)*b); b++) {}
	/* Copied from an example request, the word comes with it */
	if (!strncasecmp(b, "Bearer ", 7)) for (b += 7; *b == ' '; b++) {}
	for (e = b + strlen(b); e > b && isspace((unsigned char)e[-1]); e--) {}
	len = (size_t)(e - b);
	if (len < 16 || len >= SGDB_KEY_MAX || len >= n) return false;
	for (i = 0; i < len; i++)
		if (!isalnum((unsigned char)b[i])) return false;
	memcpy(out, b, len);
	out[len] = '\0';
	return true;
}

bool sgdb_key_load(void)
{
	char key[SGDB_KEY_MAX * 2], st[16];

	memset(g_key, 0, sizeof g_key);
	g_state = SGDB_NONE;
	db_get_str(db_dev(), K_KEY, key, sizeof key, "");
	/* Refused rather than cut, like the other accounts: a key that was stored
	 * whole and read back short would be refused by SteamGridDB on every run,
	 * which looks like a wrong key rather than a bug. */
	if (key[0] && !sgdb_key_clean(key, g_key, sizeof g_key)) {
		fprintf(stderr, "sgdb: the stored key is not usable; ignoring it\n");
		memset(g_key, 0, sizeof g_key);
	}
	memset(key, 0, sizeof key);
	if (!g_key[0]) return false;
	db_get_str(db_dev(), K_STATE, st, sizeof st, "unchecked");
	g_state = !strcmp(st, "works") ? SGDB_WORKS
	        : !strcmp(st, "refused") ? SGDB_REFUSED : SGDB_UNCHECKED;
	return true;
}

bool sgdb_have_key(void) { return g_key[0] != '\0'; }
sgdb_state sgdb_key_state(void) { return g_key[0] ? g_state : SGDB_NONE; }
const char *sgdb_secret(void) { return g_key[0] ? g_key : NULL; }

bool sgdb_key_keep(const char *key, sgdb_state st)
{
	char clean[SGDB_KEY_MAX];

	if (!sgdb_key_clean(key, clean, sizeof clean) || st == SGDB_NONE) return false;
	/* The device database is 0600 (db_open), which is what makes a key
	 * storable at all. */
	if (!db_set_str(db_dev(), K_KEY, clean) ||
	    !db_set_str(db_dev(), K_STATE, state_id(st))) {
		memset(clean, 0, sizeof clean);
		return false;
	}
	memcpy(g_key, clean, sizeof g_key);
	memset(clean, 0, sizeof clean);
	g_state = st;
	return true;
}

void sgdb_key_forget(void)
{
	db_del(db_dev(), K_KEY);
	db_del(db_dev(), K_STATE);
	memset(g_key, 0, sizeof g_key);
	g_state = SGDB_NONE;
}

void sgdb_key_mark(sgdb_state st)
{
	if (!g_key[0] || st == SGDB_NONE || st == g_state) return;
	g_state = st;
	db_set_str(db_dev(), K_STATE, state_id(st));
}

void sgdb_key_tail(char *out, size_t n)
{
	size_t len = strlen(g_key);

	if (!out || !n) return;
	snprintf(out, n, "%s", len >= 4 ? g_key + len - 4 : "");
}

void sgdb_key_masked(char *out, size_t n)
{
	char tail[8];

	if (!out || !n) return;
	out[0] = '\0';
	if (!g_key[0]) return;
	sgdb_key_tail(tail, sizeof tail);
	snprintf(out, n, "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2%s", tail);
}

static bool header_for(const char *key, char *out, size_t n)
{
	int len;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!key || !key[0]) return false;
	len = snprintf(out, n, "Authorization: Bearer %s", key);
	if (len < 0 || (size_t)len >= n) { memset(out, 0, n); return false; }
	return true;
}

bool sgdb_auth_header(char *out, size_t n)
{
	return header_for(g_key, out, n);
}

/* ---- checking a key ----------------------------------------------------- */

/* The check itself, waiting for it: what the thread runs. SGDB_WORKS,
 * SGDB_REFUSED, or SGDB_UNCHECKED when nothing answered. */
static sgdb_state sgdb_check_key(const char *key)
{
	/* Room for a long list: a reply cut to fit is not read (net.h) */
	char hdr[SGDB_KEY_MAX + 32], body[16384];
	long got;
	int http = 0;

	if (!header_for(key, hdr, sizeof hdr)) return SGDB_REFUSED;
	/* Any search will do: the question is only whether they answer to the
	 * key. A good key gets 200 and a list; a bad one 401 and
	 * {"success":false,"errors":["Invalid API key"]}, measured 2026-11-06. */
	got = net_get_hdr_answer(SGDB_API "/search/autocomplete/tetris", hdr,
	                         body, sizeof body, SGDB_CHECK_S, &http);
	memset(hdr, 0, sizeof hdr);
	/* The key refused, by name: not kept, and the one before it stays.
	 * net_get_hdr_buf could not say so - `fail` made a 401 no answer - and a
	 * wrong key was kept as unchecked, to be refused again on every run. A
	 * 403 is the same refusal, as the runs read it (sgdbrun.c). */
	if (http == 401 || http == 403) return SGDB_REFUSED;
	if (got >= 0) {
		jsv v;

		if (js_member(js_root(body, (size_t)got), "success", &v) && js_is_true(v))
			return SGDB_WORKS;
	}
	/* Anything else is no answer: a network, a DNS that is not there over
	 * the USB link, a 429, their site down. None of those says the key is
	 * wrong, so none of them may throw it away. */
	return SGDB_UNCHECKED;
}

static char        g_try_key[SGDB_KEY_MAX];
static atomic_int  g_try_result;          /* 0 while running, else 1 + sgdb_state */
static bool        g_try_running;
static sgdb_try    g_try = SGDB_TRY_IDLE;
static sgdb_try    g_said = SGDB_TRY_IDLE;

static void *try_thread(void *unused)
{
	(void)unused;
	atomic_store(&g_try_result, 1 + (int)sgdb_check_key(g_try_key));
	return NULL;
}

/* The outcome kept, its sentence read in the language that is on when it is
 * shown (sgdb_try_said) */
static sgdb_try said(sgdb_try t)
{
	g_said = t;
	return t;
}

sgdb_try sgdb_try_begin(const char *typed)
{
	pthread_t th;
	pthread_attr_t at;
	int r;

	if (g_try_running) return SGDB_TRY_BUSY;
	if (!sgdb_key_clean(typed, g_try_key, sizeof g_try_key))
		return g_try = said(SGDB_TRY_BAD);
	atomic_store(&g_try_result, 0);
	if (pthread_attr_init(&at) != 0) return g_try = said(SGDB_TRY_BUSY);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	r = pthread_create(&th, &at, try_thread, NULL);
	pthread_attr_destroy(&at);
	if (r != 0) {
		/* No thread: keep it unchecked rather than wait here, and let the
		 * next run be the check. */
		atomic_store(&g_try_result, 1 + (int)SGDB_UNCHECKED);
	}
	g_try_running = true;
	return g_try = said(SGDB_TRY_RUNNING);
}

sgdb_try sgdb_try_poll(void)
{
	int r;

	if (!g_try_running) return g_try;
	if ((r = atomic_load(&g_try_result)) == 0) return SGDB_TRY_RUNNING;
	g_try_running = false;
	switch ((sgdb_state)(r - 1)) {
	case SGDB_WORKS:
		g_try = sgdb_key_keep(g_try_key, SGDB_WORKS) ? SGDB_TRY_WORKS : SGDB_TRY_KEPT;
		break;
	case SGDB_REFUSED:
		g_try = SGDB_TRY_REFUSED;
		break;
	default:
		g_try = sgdb_key_keep(g_try_key, SGDB_UNCHECKED) ? SGDB_TRY_KEPT : SGDB_TRY_REFUSED;
		break;
	}
	memset(g_try_key, 0, sizeof g_try_key);
	return said(g_try);
}

const char *sgdb_try_said(void)
{
	/* One line of the message panel each, at the Pixel 2's 640 wide: 34
	 * characters at most, so none of them has to scroll (DESIGN.md 11.8). The
	 * kept one is explained where it is shown: the Status row reads "not
	 * checked", and the page says the next Box Art run checks it. */
	switch (g_said) {
	case SGDB_TRY_WORKS:   return T(S_SGDB_TRY_WORKS);
	case SGDB_TRY_KEPT:    return T(S_SGDB_TRY_KEPT);
	case SGDB_TRY_REFUSED: return T(S_SGDB_TRY_REFUSED);
	case SGDB_TRY_BAD:     return T(S_SGDB_TRY_BAD);
	case SGDB_TRY_BUSY:    return T(S_SGDB_TRY_BUSY);
	case SGDB_TRY_RUNNING: return T(S_SGDB_TRY_RUNNING);
	default:               return "";
	}
}

/* ---- matching ------------------------------------------------------------ */

/* Step 1 of title(): `^\d{1,4}\s*-?\s+`, the regex's backtracking written
 * out. Returns how many bytes it covers, 0 for no match. */
static size_t numbering(const char *s)
{
	size_t d = 0, p, sp;

	while (isdigit((unsigned char)s[d])) d++;
	if (d < 1 || d > 4) return 0;
	for (p = d; isspace((unsigned char)s[p]); p++) {}
	sp = p - d;
	if (s[p] == '-' && isspace((unsigned char)s[p + 1])) {
		for (p++; isspace((unsigned char)s[p]); p++) {}
		return p;
	}
	return sp ? d + sp : 0;
}

static void title_from(const char *s, char *out, size_t n)
{
	char t[512], u[1024];
	size_t i = 0, o = 0, k;
	bool space = false;

	/* Every [..] and (..) as one space; an unclosed one stays. */
	while (*s && o + 1 < sizeof t) {
		const char *close = NULL;

		if (*s == '[') close = strchr(s + 1, ']');
		else if (*s == '(') close = strchr(s + 1, ')');
		if (close) { t[o++] = ' '; s = close + 1; continue; }
		t[o++] = *s++;
	}
	t[o] = '\0';

	/* StardewValley: only when there is no space at all */
	space = strchr(t, ' ') != NULL;
	for (i = 0, o = 0; t[i] && o + 2 < sizeof u; i++) {
		if (!space && i > 0 && t[i - 1] >= 'a' && t[i - 1] <= 'z' &&
		    t[i] >= 'A' && t[i] <= 'Z')
			u[o++] = ' ';
		u[o++] = (t[i] == '_' || t[i] == '-') ? ' ' : t[i];
	}
	u[o] = '\0';

	/* " ".join(t.split()) */
	for (i = 0, k = 0, space = false; u[i]; i++) {
		if (isspace((unsigned char)u[i])) { space = k > 0; continue; }
		if (space && k + 1 < n) out[k++] = ' ';
		space = false;
		if (k + 1 < n) out[k++] = u[i];
	}
	out[k] = '\0';
}

void sgdb_title(const char *stem, char *out, size_t n)
{
	size_t skip;

	if (!out || !n) return;
	out[0] = '\0';
	if (!stem) return;
	skip = numbering(stem);
	title_from(stem + skip, out, n);
	/* "2048 (homebrew)": the number is the whole title */
	if (!out[0] && skip) title_from(stem, out, n);
}

#define SEQ_MAX 200

/* difflib's find_longest_match with no junk: the longest block, the earliest
 * in `a` among equals and then the earliest in `b`. */
static int longest(const char *a, int alo, int ahi, const char *b, int blo, int bhi,
                   int *oi, int *oj)
{
	int rows[2][SEQ_MAX + 1], *prev = rows[0], *cur = rows[1], *sw;
	int i, j, bi = alo, bj = blo, bk = 0;

	for (j = 0; j <= bhi - blo; j++) prev[j] = 0;
	for (i = alo; i < ahi; i++) {
		cur[0] = 0;
		for (j = blo; j < bhi; j++) {
			int k = a[i] == b[j] ? prev[j - blo] + 1 : 0;

			cur[j - blo + 1] = k;
			if (k > bk) { bk = k; bi = i - k + 1; bj = j - k + 1; }
		}
		sw = prev; prev = cur; cur = sw;
	}
	*oi = bi;
	*oj = bj;
	return bk;
}

/* difflib.SequenceMatcher(None, a, b).ratio(), exactly: Python's longest-
 * block-first recursion, ties to the earliest in `a` and then in `b`. For
 * strings under 200 bytes, where difflib's junk heuristic never applies;
 * longer than that is 0, no match. */
static double sgdb_ratio(const char *a, const char *b)
{
	struct { int alo, ahi, blo, bhi; } st[SEQ_MAX * 2 + 4];
	int la = (int)strlen(a), lb = (int)strlen(b), sp = 0, m = 0;

	if (la + lb == 0) return 1.0;
	if (la >= SEQ_MAX || lb >= SEQ_MAX) return 0.0;
	st[sp].alo = 0; st[sp].ahi = la; st[sp].blo = 0; st[sp].bhi = lb; sp++;
	while (sp > 0) {
		int alo, ahi, blo, bhi, i, j, k;

		sp--;
		alo = st[sp].alo; ahi = st[sp].ahi; blo = st[sp].blo; bhi = st[sp].bhi;
		k = longest(a, alo, ahi, b, blo, bhi, &i, &j);
		if (!k) continue;
		m += k;
		if (alo < i && blo < j) {
			st[sp].alo = alo; st[sp].ahi = i; st[sp].blo = blo; st[sp].bhi = j; sp++;
		}
		if (i + k < ahi && j + k < bhi) {
			st[sp].alo = i + k; st[sp].ahi = ahi; st[sp].blo = j + k; st[sp].bhi = bhi; sp++;
		}
	}
	return 2.0 * m / (la + lb);
}

double sgdb_close(const char *want, const char *name)
{
	char a[512], b[512];

	art_norm(want ? want : "", a, sizeof a);
	art_norm(name ? name : "", b, sizeof b);
	return sgdb_ratio(a, b);
}

bool sgdb_pick_game(const char *json, size_t len, const char *want,
                    long *id, char *name, size_t nn, double *score)
{
	jsv root, data, it = { 0 }, e, v;
	double best = -1.0;
	bool have = false;

	if (score) *score = 0.0;
	if (name && nn) name[0] = '\0';
	if (id) *id = 0;
	if (!json || !want || !*want) return false;
	root = js_root(json, len);
	if (!js_member(root, "data", &data)) return false;
	while (js_next(data, &it, &e)) {
		char nm[256];
		double s;

		if (!js_member(e, "name", &v) || !js_str(v, nm, sizeof nm)) continue;
		if (!js_member(e, "id", &v)) continue;
		s = sgdb_close(want, nm);
		/* Strictly greater: the FIRST of equals, as max() keeps it */
		if (s > best) {
			best = s;
			have = true;
			if (id) *id = js_int(v);
			if (name && nn) snprintf(name, nn, "%s", nm);
		}
	}
	if (!have) return false;
	if (score) *score = best;
	return best >= SGDB_MATCH && (!id || *id > 0);
}

/* A URL curl may be handed: https, and nothing in it that would end the
 * config line or leave the host. */
static bool fetchable(const char *u)
{
	if (strncmp(u, "https://", 8) != 0) return false;
	for (; *u; u++)
		if ((unsigned char)*u <= ' ' || *u == '"' || *u == '\\' || *u == 0x7f)
			return false;
	return true;
}

bool sgdb_pick_grid(const char *json, size_t len, char *url, size_t n)
{
	jsv root, data, it = { 0 }, e, v;

	if (!url || !n) return false;
	url[0] = '\0';
	if (!json) return false;
	root = js_root(json, len);
	if (!js_member(root, "data", &data)) return false;
	while (js_next(data, &it, &e)) {
		char mime[32] = "", u[1024];

		if (js_member(e, "mime", &v)) js_str(v, mime, sizeof mime);
		if (strcmp(mime, "image/png") != 0 && strcmp(mime, "image/jpeg") != 0) continue;
		if (!js_member(e, "url", &v) || !js_str(v, u, sizeof u) || !fetchable(u)) continue;
		if (strlen(u) >= n) continue;
		snprintf(url, n, "%s", u);
		return true;
	}
	return false;
}

bool sgdb_search_url(const char *want, char *out, size_t n)
{
	char enc[256 * 3 + 1];
	int len;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!want || !*want || strlen(want) >= 256) return false;
	net_urlencode(want, enc, sizeof enc);
	len = snprintf(out, n, SGDB_API "/search/autocomplete/%s", enc);
	return len > 0 && (size_t)len < n;
}

bool sgdb_grids_url(long id, char *out, size_t n)
{
	int len;

	if (!out || !n || id <= 0) return false;
	/* The tool's filters, and the two formats SDL_image reads here */
	len = snprintf(out, n, SGDB_API "/grids/game/%ld?dimensions=600x900"
	               "&types=static&nsfw=false&mimes=image/png,image/jpeg", id);
	return len > 0 && (size_t)len < n;
}
