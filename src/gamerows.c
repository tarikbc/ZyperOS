/* SPDX-License-Identifier: GPL-3.0-only */
/* See gamerows.h. No SDL and no platform header: db.h alone. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "gamerows.h"

#define GR_KEY  1024
#define GR_VAL  1024
#define GR_LINE (2 * (GR_KEY + GR_VAL) + 4)

static const char *const PREFIXES[] = { "fav.", "pak.", "sess." };

/* "<digits>." at s, and where it ends */
static const char *digits_dot(const char *s)
{
	const char *p = s;

	while (isdigit((unsigned char)*p)) p++;
	return p > s && *p == '.' ? p + 1 : NULL;
}

/* `s` is exactly a, then the separator, then b */
static bool is_pair(const char *s, const char *a, char sep, const char *b)
{
	size_t n = strlen(a);

	return !strncmp(s, a, n) && s[n] == sep && !strcmp(s + n + 1, b);
}

bool gamerows_mine(const char *key, const char *tag, const char *file)
{
	const char *p;

	if (!key || !tag || !file || !*tag || !*file) return false;
	if (!strncmp(key, "fav.", 4)) return is_pair(key + 4, tag, '\t', file);
	if (!strncmp(key, "pak.", 4)) return is_pair(key + 4, tag, '.', file);
	/* sess.<start>.<clock>.<tag>\t<file> */
	if (strncmp(key, "sess.", 5) || !(p = digits_dot(key + 5)) || !(p = digits_dot(p)))
		return false;
	return is_pair(p, tag, '\t', file);
}

bool gamerows_escape(const char *in, char *out, size_t n)
{
	size_t o = 0;

	for (; *in; in++) {
		char c = *in, e = 0;

		if (c == '\\') e = '\\';
		else if (c == '\t') e = 't';
		else if (c == '\n') e = 'n';
		else if (c == '\r') e = 'r';
		if (o + (e ? 2 : 1) >= n) return false;
		if (e) { out[o++] = '\\'; out[o++] = e; }
		else out[o++] = c;
	}
	if (o >= n) return false;
	out[o] = '\0';
	return true;
}

bool gamerows_unescape(const char *in, char *out, size_t n)
{
	size_t o = 0;

	for (; *in; in++) {
		char c = *in;

		if (c == '\\') {
			switch (*++in) {
			case '\\': c = '\\'; break;
			case 't': c = '\t'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			default: return false;
			}
		}
		if (o + 1 >= n) return false;
		out[o++] = c;
	}
	out[o] = '\0';
	return true;
}

/* --- out --------------------------------------------------------------- */

struct dump_ctx { const char *tag, *file; FILE *out; int n; bool bad; };

static bool dump_row(const char *key, const char *value, void *ctx)
{
	struct dump_ctx *c = ctx;
	char k[2 * GR_KEY], v[2 * GR_VAL];

	if (!gamerows_mine(key, c->tag, c->file)) return true;
	if (!gamerows_escape(key, k, sizeof k) || !gamerows_escape(value, v, sizeof v) ||
	    fprintf(c->out, "%s\t%s\n", k, v) < 0) {
		c->bad = true;
		return false;
	}
	c->n++;
	return true;
}

int gamerows_dump(db *d, const char *tag, const char *file, FILE *out)
{
	struct dump_ctx c = { tag, file, out, 0, false };
	size_t i;

	if (!d || !tag || !file || !out) return -1;
	for (i = 0; i < sizeof PREFIXES / sizeof PREFIXES[0] && !c.bad; i++)
		db_each_prefix(d, PREFIXES[i], dump_row, &c);
	if (c.bad || fflush(out) != 0) return -1;
	return c.n;
}

/* --- back -------------------------------------------------------------- */

typedef struct row { char *key, *value; struct row *next; } row;

static void rows_free(row *r)
{
	while (r) {
		row *next = r->next;

		free(r->key);
		free(r->value);
		free(r);
		r = next;
	}
}

static row *row_new(const char *key, const char *value, row *next)
{
	row *r = calloc(1, sizeof *r);

	if (!r) return NULL;
	r->key = strdup(key);
	r->value = strdup(value);
	r->next = next;
	if (!r->key || !r->value) { r->next = NULL; rows_free(r); return NULL; }
	return r;
}

struct gone_ctx { const char *tag, *file; row *head; bool bad; };

static bool collect_mine(const char *key, const char *value, void *ctx)
{
	struct gone_ctx *g = ctx;
	row *r;

	(void)value;
	if (!gamerows_mine(key, g->tag, g->file)) return true;
	if (!(r = row_new(key, "", g->head))) { g->bad = true; return false; }
	g->head = r;
	return true;
}

int gamerows_put(db *d, const char *tag, const char *file, FILE *in)
{
	static char line[GR_LINE];
	char key[GR_KEY], value[GR_VAL];
	row *want = NULL, *r;
	struct gone_ctx g = { tag, file, NULL, false };
	int n = 0;
	size_t i;

	if (!d || !tag || !file || !in) return -1;
	/* Everything read and held to this game first: nothing changes for a
	 * file that does not read, or that names another game's row */
	while (fgets(line, sizeof line, in)) {
		size_t len = strlen(line);
		char *tab;

		if (len && line[len - 1] == '\n') line[--len] = '\0';
		else if (!feof(in)) goto refuse;     /* longer than a row can be */
		if (!len) continue;
		if (!(tab = strchr(line, '\t'))) goto refuse;
		*tab = '\0';
		if (!gamerows_unescape(line, key, sizeof key) ||
		    !gamerows_unescape(tab + 1, value, sizeof value) ||
		    !gamerows_mine(key, tag, file))
			goto refuse;
		if (!(r = row_new(key, value, want))) goto refuse;
		want = r;
	}
	if (ferror(in)) goto refuse;

	for (i = 0; i < sizeof PREFIXES / sizeof PREFIXES[0] && !g.bad; i++)
		db_each_prefix(d, PREFIXES[i], collect_mine, &g);
	if (g.bad) { rows_free(g.head); goto refuse; }
	for (r = g.head; r; r = r->next) db_del(d, r->key);
	rows_free(g.head);
	for (r = want; r; r = r->next) {
		if (!db_set_str(d, r->key, r->value)) { rows_free(want); return -1; }
		n++;
	}
	rows_free(want);
	return n;

refuse:
	rows_free(want);
	return -1;
}
