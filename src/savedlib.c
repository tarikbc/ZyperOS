/* SPDX-License-Identifier: GPL-3.0-only */
#include "savedlib.h"

#include "atomic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The file, a line each:
 *
 *   ZyperOS saved library 1 <TAB> version
 *   shelf <TAB> games <TAB> left out <TAB> extensions <TAB> folder
 *   skip <TAB> name          one per left-out name the listing kept
 *   game <TAB> file <TAB> name     one per game, in the folder's order
 *
 * Every text field escaped: backslash, tab, newline and return as \\ \t \n
 * \r. exFAT allows none of the last three in a name, but a host's card can. */
#define SAVEDLIB_MAGIC "ZyperOS saved library 1"
/* No library the launcher writes comes near this; a file that does is not
 * one, and is not read into memory */
#define SAVEDLIB_MAX   (64L << 20)
#define SAVEDLIB_GAMES 1000000

/* ---------- writing ---------- */

static void put_text(FILE *f, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '\\': fputs("\\\\", f); break;
		case '\t': fputs("\\t", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': fputs("\\r", f); break;
		default:   fputc(*s, f); break;
		}
	}
}

bool savedlib_write(const char *path, const char *version, const savedlib_row *rows, int n)
{
	FILE *f = atomic_open(path, 0644);
	int i, k;

	if (!f) return false;
	fputs(SAVEDLIB_MAGIC "\t", f);
	put_text(f, version);
	fputc('\n', f);
	for (i = 0; i < n; i++) {
		const game_list *l = rows[i].list;
		int count = l->items ? l->count : 0;
		int kept = l->skipped < LIB_SKIPS_SHOWN ? l->skipped : LIB_SKIPS_SHOWN;

		fprintf(f, "shelf\t%d\t%d\t", count, l->skipped);
		put_text(f, rows[i].exts);
		fputc('\t', f);
		put_text(f, rows[i].folder);
		fputc('\n', f);
		for (k = 0; k < kept; k++) {
			fputs("skip\t", f);
			put_text(f, l->skipped_eg[k]);
			fputc('\n', f);
		}
		for (k = 0; k < count; k++) {
			fputs("game\t", f);
			put_text(f, l->items[k].file);
			fputc('\t', f);
			put_text(f, l->items[k].name);
			fputc('\n', f);
		}
	}
	if (ferror(f)) {
		atomic_abort(f, path);
		return false;
	}
	return atomic_commit(f, path);
}

/* ---------- reading ---------- */

/* The next line, ended in place, or NULL at the end. A last line with no
 * newline is a file cut short, and is not a line. */
static char *next_line(char **at, char *end)
{
	char *s = *at, *nl;

	if (s >= end || !(nl = memchr(s, '\n', (size_t)(end - s)))) return NULL;
	*nl = '\0';
	*at = nl + 1;
	return s;
}

/* The next field of the line at *s, unescaped into `out`, which takes `n`
 * bytes. *s goes to the field after, or NULL when this was the line's last.
 * False for no field left, one that does not fit, or a bad escape. */
static bool field(char **s, char *out, size_t n)
{
	char *p = *s;
	size_t k = 0;

	if (!p) return false;
	for (; *p && *p != '\t'; p++) {
		char c = *p;

		if (c == '\\') {
			p++;
			c = *p == '\\' ? '\\' : *p == 't' ? '\t' : *p == 'n' ? '\n' : *p == 'r' ? '\r' : 0;
			if (!c) return false;
		}
		if (k + 1 >= n) return false;
		out[k++] = c;
	}
	out[k] = '\0';
	*s = *p == '\t' ? p + 1 : NULL;
	return true;
}

/* A field that is `word` */
static bool word(char **s, const char *w)
{
	char got[32];

	return field(s, got, sizeof got) && !strcmp(got, w);
}

/* A field that is a count from 0 to `most` */
static bool count_field(char **s, int most, int *out)
{
	char got[16], *end;
	long v;

	if (!field(s, got, sizeof got) || !got[0]) return false;
	v = strtol(got, &end, 10);
	if (*end || v < 0 || v > most) return false;
	*out = (int)v;
	return true;
}

/* The whole file in memory, ended, or NULL */
static char *read_all(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	char *buf = NULL;

	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (*size = ftell(f)) > 0 && *size <= SAVEDLIB_MAX &&
	    fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)*size + 1)) &&
	    fread(buf, 1, (size_t)*size, f) == (size_t)*size) {
		buf[*size] = '\0';
	} else {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

bool savedlib_load(const char *path, const char *version, saved_lib *out)
{
	char tmp[LIB_NAME], *buf, *at, *end, *line, *s;
	long size = 0;
	int cap = 0;

	memset(out, 0, sizeof *out);
	if (!path || !version || !(buf = read_all(path, &size))) return false;
	at = buf;
	end = buf + size;

	s = line = next_line(&at, end);
	if (!line || !word(&s, SAVEDLIB_MAGIC) || !field(&s, tmp, sizeof tmp) || s ||
	    strcmp(tmp, version))
		goto bad;
	while ((s = line = next_line(&at, end))) {
		saved_shelf *sh;
		int games, skipped, kept, k;

		if (!word(&s, "shelf") || !count_field(&s, SAVEDLIB_GAMES, &games) ||
		    !count_field(&s, SAVEDLIB_GAMES, &skipped))
			goto bad;
		if (out->count == cap) {
			int more = cap ? cap * 2 : 64;
			saved_shelf *bigger = realloc(out->shelves, (size_t)more * sizeof *bigger);

			if (!bigger) goto bad;
			out->shelves = bigger;
			cap = more;
		}
		sh = &out->shelves[out->count++];
		memset(sh, 0, sizeof *sh);
		if (!field(&s, sh->exts, sizeof sh->exts) || !field(&s, sh->folder, sizeof sh->folder) || s)
			goto bad;
		sh->list.scanned = true;
		sh->list.skipped = skipped;
		kept = skipped < LIB_SKIPS_SHOWN ? skipped : LIB_SKIPS_SHOWN;
		for (k = 0; k < kept; k++) {
			if (!(s = next_line(&at, end)) || !word(&s, "skip") ||
			    !field(&s, sh->list.skipped_eg[k], sizeof sh->list.skipped_eg[k]) || s)
				goto bad;
		}
		if (!games) continue;
		if (!(sh->list.items = calloc((size_t)games, sizeof *sh->list.items))) goto bad;
		for (k = 0; k < games; k++) {
			game_entry *g = &sh->list.items[k];

			if (!(s = next_line(&at, end)) || !word(&s, "game") ||
			    !field(&s, g->file, sizeof g->file) || !field(&s, g->name, sizeof g->name) || s)
				goto bad;
			g->added = LIB_ADDED_UNKNOWN;
		}
		sh->list.count = games;
	}
	if (at != end) goto bad;      /* a tail with no newline */
	free(buf);
	return true;
bad:
	free(buf);
	savedlib_free(out);
	return false;
}

bool savedlib_take(saved_lib *s, const char *folder, const char *exts, game_list *out)
{
	int i;

	for (i = 0; s && i < s->count; i++) {
		saved_shelf *sh = &s->shelves[i];

		if (sh->taken || strcmp(sh->folder, folder) != 0 || strcmp(sh->exts, exts) != 0)
			continue;
		lib_free(out);
		*out = sh->list;
		memset(&sh->list, 0, sizeof sh->list);
		sh->taken = true;
		return true;
	}
	return false;
}

int savedlib_untaken(const saved_lib *s)
{
	int i, n = 0;

	for (i = 0; s && i < s->count; i++) n += !s->shelves[i].taken;
	return n;
}

void savedlib_free(saved_lib *s)
{
	int i;

	for (i = 0; i < s->count; i++) lib_free(&s->shelves[i].list);
	free(s->shelves);
	memset(s, 0, sizeof *s);
}
