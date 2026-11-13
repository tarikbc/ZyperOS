/* SPDX-License-Identifier: GPL-3.0-only */
#include "sheet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool ext_is(const char *name, const char *ext)
{
	const char *slash = strrchr(name, '/');
	const char *base = slash ? slash + 1 : name;
	const char *dot = strrchr(base, '.');

	return dot && dot != base && !strcasecmp(dot + 1, ext);
}

/* A name a sheet gives, against the folder the sheet is in (`dir`, relative
 * to the console's folder, "" for its top): the path relative to the
 * console's folder, or false for one that leaves it - "..", an absolute
 * path, a drive letter. Windows' backslashes are slashes; "." parts go. */
static bool sheet_join(const char *dir, const char *name, char *out, size_t n)
{
	char tmp[SHEET_PATH * 2], *p, *save = NULL;
	size_t o = 0;

	if (!name[0] || name[0] == '/' || name[0] == '\\' ||
	    (((name[0] | 0x20) >= 'a' && (name[0] | 0x20) <= 'z') && name[1] == ':'))
		return false;
	if (snprintf(tmp, sizeof tmp, "%s%s%s", dir, dir[0] ? "/" : "", name) >= (int)sizeof tmp)
		return false;
	for (p = tmp; *p; p++)
		if (*p == '\\') *p = '/';
	out[0] = '\0';
	for (p = strtok_r(tmp, "/", &save); p; p = strtok_r(NULL, "/", &save)) {
		int w;

		if (!strcmp(p, ".")) continue;
		if (!strcmp(p, "..")) return false;
		w = snprintf(out + o, n - o, "%s%s", o ? "/" : "", p);
		if (w < 0 || (size_t)w >= n - o) return false;
		o += (size_t)w;
	}
	return o > 0;
}

/* ---- sheets ---------------------------------------------------------------
 *
 * A .cue, .gdi or .toc names its track files and an .m3u its discs; the
 * names are the game's files too. Read only for a game that is a sheet at
 * the top of its console's folder: a game in a folder of its own goes with
 * its folder. */

bool sheet_is(const char *name)
{
	return ext_is(name, "cue") || ext_is(name, "gdi") || ext_is(name, "toc") ||
	       ext_is(name, "m3u");
}

static void sheet_add(sheet_names *s, const char *dir, const char *raw)
{
	char rel[SHEET_PATH];
	int i;

	if (!sheet_join(dir, raw, rel, sizeof rel)) { s->outside = true; return; }
	for (i = 0; i < s->n; i++)
		if (!strcasecmp(s->name[i], rel)) return;
	if (s->n < SHEET_NAMES) snprintf(s->name[s->n++], SHEET_PATH, "%s", rel);
}

/* The quoted string at `p`, or the run up to whitespace */
static const char *token(const char *p, char *out, size_t n)
{
	size_t o = 0;

	while (*p == ' ' || *p == '\t') p++;
	if (*p == '"') {
		for (p++; *p && *p != '"'; p++)
			if (o + 1 < n) out[o++] = *p;
		if (*p == '"') p++;
	} else {
		for (; *p && *p != ' ' && *p != '\t'; p++)
			if (o + 1 < n) out[o++] = *p;
	}
	out[o] = '\0';
	return p;
}

/* What the sheet at `rel` (relative to the console's folder `root`) names,
 * into `s`; a .m3u's sheets are read for their tracks too, one level down */
void sheet_read(const char *root, const char *rel, sheet_names *s, int depth)
{
	char path[SHEET_PATH * 3], dir[SHEET_PATH], word[SHEET_PATH];
	char *buf, *line, *save = NULL;
	const char *slash = strrchr(rel, '/');
	size_t got;
	FILE *f;
	bool m3u = ext_is(rel, "m3u"), gdi = ext_is(rel, "gdi");
	int first = 1;

	snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - rel) : 0, rel);
	if (snprintf(path, sizeof path, "%s/%s", root, rel) >= (int)sizeof path) return;
	if (!(f = fopen(path, "rb"))) return;
	buf = malloc(SHEET_MAX + 1);
	if (!buf) { fclose(f); return; }
	got = fread(buf, 1, SHEET_MAX, f);
	fclose(f);
	buf[got] = '\0';
	for (line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
		const char *p = line;
		size_t len;

		while (*p == ' ' || *p == '\t') p++;
		/* A UTF-8 byte order mark, which Windows' editors put first */
		if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
		    (unsigned char)p[2] == 0xBF)
			p += 3;
		first = 0;
		if (!*p) continue;
		if (m3u) {
			if (*p == '#') continue;
			snprintf(word, sizeof word, "%s", p);
			for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
				word[len - 1] = '\0';
			if (word[0]) sheet_add(s, dir, word);
			continue;
		}
		if (gdi) {
			/* "1 0 4 2352 track01.bin 0": the fifth field. The first line
			 * is the count of tracks, one field. */
			int k;

			for (k = 0; k < 5 && *p; k++) {
				p = token(p, word, sizeof word);
				while (*p == ' ' || *p == '\t') p++;
			}
			if (k == 5 && word[0]) sheet_add(s, dir, word);
			continue;
		}
		/* .cue and .toc: FILE "name" TYPE, and a .toc's DATAFILE and
		 * AUDIOFILE */
		if (!strncasecmp(p, "FILE", 4) && (p[4] == ' ' || p[4] == '\t'))
			p += 4;
		else if (!strncasecmp(p, "DATAFILE", 8) && (p[8] == ' ' || p[8] == '\t'))
			p += 8;
		else if (!strncasecmp(p, "AUDIOFILE", 9) && (p[9] == ' ' || p[9] == '\t'))
			p += 9;
		else continue;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '"') {
			token(p, word, sizeof word);
		} else {
			/* Unquoted, which a hand or an old tool writes even around
			 * spaces: the rest of the line less its last word, the type
			 * (BINARY, WAVE), when there is more than one */
			char *sp;

			snprintf(word, sizeof word, "%s", p);
			for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
				word[len - 1] = '\0';
			if ((sp = strrchr(word, ' ')) || (sp = strrchr(word, '\t'))) {
				*sp = '\0';
				for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
					word[len - 1] = '\0';
			}
		}
		if (word[0]) sheet_add(s, dir, word);
	}
	free(buf);

	/* An .m3u's discs that are sheets themselves, for their tracks */
	if (m3u && depth == 0) {
		int i, n = s->n;

		for (i = 0; i < n; i++)
			if (sheet_is(s->name[i]) && !ext_is(s->name[i], "m3u"))
				sheet_read(root, s->name[i], s, depth + 1);
	}
}


int sheet_sides(const char *file, const char *with[2])
{
	static const struct { const char *ext, *with[2]; } SIDE[] = {
		{ "ccd", { "img", "sub" } }, { "mds", { "mdf", NULL } },
		{ "mdf", { "mds", NULL } },  { "cue", { "sbi", NULL } },
	};
	int n = 0;

	for (size_t i = 0; i < sizeof SIDE / sizeof SIDE[0]; i++) {
		if (!ext_is(file, SIDE[i].ext)) continue;
		for (int k = 0; k < 2 && SIDE[i].with[k]; k++) with[n++] = SIDE[i].with[k];
		break;
	}
	return n;
}
