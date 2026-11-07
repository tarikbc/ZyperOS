/* SPDX-License-Identifier: GPL-3.0-only */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip(char *s)
{
	char *e = s + strlen(s);
	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		*--e = '\0';
	char *b = s;
	while (*b == ' ' || *b == '\t') b++;
	if (b != s) memmove(s, b, strlen(b) + 1);
}

/* split a line on '|' into up to n fields, in place */
static int split(char *line, char *fields[], int n)
{
	int c = 0;
	char *p = line;
	while (c < n) {
		fields[c++] = p;
		char *bar = strchr(p, '|');
		if (!bar) break;
		*bar = '\0';
		p = bar + 1;
	}
	for (int i = 0; i < c; i++) strip(fields[i]);
	return c;
}

/* The accent field: six hex digits, RRGGBB, and nothing else. Anything else -
 * empty, a word, a seventh digit - names no color, and is CFG_NO_COLOR rather
 * than whatever strtoul makes of it, which for a typo was black. */
static unsigned parse_color(const char *f)
{
	int i;

	for (i = 0; i < 6; i++)
		if (!isxdigit((unsigned char)f[i])) return CFG_NO_COLOR;
	if (f[6]) return CFG_NO_COLOR;
	return (unsigned)strtoul(f, NULL, 16);
}

/* sys|display name|rom folder|core|tag|card|RRGGBB|extensions */
bool cfg_load_systems(const char *path, systems_cfg *out)
{
	memset(out, 0, sizeof *out);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char line[1024];
	/* power|tag|level, map|tag|spec and controller|tag|n lines, applied once
	 * every system is read, so they may sit anywhere in the file */
	struct { char kind[12], tag[8], value[96]; } extra[CFG_MAX_SYSTEMS * 3];
	int nextra = 0;
	while (fgets(line, sizeof line, f) && out->count < CFG_MAX_SYSTEMS) {
		strip(line);
		if (!line[0] || line[0] == '#') continue;
		char *fld[9] = { 0 };
		int n = split(line, fld, 9);
		if (n == 3 && (!strcmp(fld[0], "power") || !strcmp(fld[0], "map") ||
		               !strcmp(fld[0], "controller"))) {
			if (nextra < (int)(sizeof extra / sizeof extra[0])) {
				snprintf(extra[nextra].kind, sizeof extra[0].kind, "%s", fld[0]);
				snprintf(extra[nextra].tag, sizeof extra[0].tag, "%s", fld[1]);
				snprintf(extra[nextra].value, sizeof extra[0].value, "%s", fld[2]);
				nextra++;
			}
			continue;
		}
		if (n < 5 || strcmp(fld[0], "sys") != 0) continue;
		system_cfg *s = &out->systems[out->count];
		snprintf(s->name, CFG_STR, "%s", fld[1]);
		snprintf(s->folder, CFG_STR, "%s", fld[2]);
		snprintf(s->cores, CFG_STR, "%s", fld[3]);
		if (!cfg_core_at(s, 0, s->core, CFG_STR)) s->core[0] = '\0';
		snprintf(s->tag, sizeof s->tag, "%s", fld[4]);
		if (n > 5) snprintf(s->card, CFG_STR, "%s", fld[5]);
		s->accent = n > 6 ? parse_color(fld[6]) : CFG_NO_COLOR;
		if (n > 7) snprintf(s->exts, CFG_STR, "%s", fld[7]);
		if (n > 8) snprintf(s->disc_bios, CFG_STR, "%s", fld[8]);
		if (!s->name[0] || !s->folder[0] || !s->core[0] || !s->tag[0]) continue;
		out->count++;
	}
	fclose(f);
	for (int i = 0; i < nextra; i++)
		for (int k = 0; k < out->count; k++) {
			system_cfg *s = &out->systems[k];

			if (strcmp(s->tag, extra[i].tag)) continue;
			if (!strcmp(extra[i].kind, "power"))
				snprintf(s->power, sizeof s->power, "%s", extra[i].value);
			else if (!strcmp(extra[i].kind, "map"))
				snprintf(s->map, sizeof s->map, "%s", extra[i].value);
			else
				s->controller = (unsigned)strtoul(extra[i].value, NULL, 10);
		}
	return out->count > 0;
}

void cfg_run_keys(const system_cfg *s, bool dual, char *out, int cap)
{
	int n = 0;

	out[0] = '\0';
	if (dual)
		n += snprintf(out + n, (size_t)(cap - n), "screens=2\tframeskip=late");
	if (s->power[0] && n < cap)
		n += snprintf(out + n, (size_t)(cap - n), "%spower=%s", n ? "\t" : "", s->power);
	if (s->controller && n < cap)
		snprintf(out + n, (size_t)(cap - n), "%scontroller=%u", n ? "\t" : "", s->controller);
}

int cfg_core_count(const system_cfg *s)
{
	char one[CFG_STR];
	int n = 0;

	while (cfg_core_at(s, n, one, sizeof one)) n++;
	return n;
}

/* Comma separated, spaces around each name dropped. A script path holds no
 * comma - the card's paths are ZyperOS's own - so nothing needs quoting. */
bool cfg_core_at(const system_cfg *s, int i, char *out, int cap)
{
	const char *p = s->cores;
	int k;

	for (k = 0; k < i; k++) {
		p = strchr(p, ',');
		if (!p) return false;
		p++;
	}
	while (*p == ' ' || *p == '\t') p++;
	{
		const char *e = strchr(p, ',');
		int len = e ? (int)(e - p) : (int)strlen(p);

		while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
		if (len <= 0 || len >= cap) return false;
		memcpy(out, p, (size_t)len);
		out[len] = '\0';
	}
	return true;
}

/* The base letter of a UTF-8 Latin-1 letter, U+00C0 to U+00FF, whose lead
 * byte is 0xC3: "é" is e. Anything else is itself, lowercased. */
static unsigned name_char(const unsigned char **p)
{
	static const char LATIN1[] =
		"aaaaaaaceeeeiiiidnooooox" "ouuuuyts"
		"aaaaaaaceeeeiiiidnooooo/" "ouuuuyty";
	unsigned c = **p;

	if (c == 0xC3 && (*p)[1] >= 0x80 && (*p)[1] <= 0xBF) {
		c = (unsigned char)LATIN1[(*p)[1] - 0x80];
		*p += 2;
		return c;
	}
	(*p)++;
	return (unsigned)tolower((int)c);
}

int cfg_name_cmp(const char *a, const char *b)
{
	const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;

	while (*p && *q) {
		if (isdigit(*p) && isdigit(*q)) {
			const unsigned char *pe, *qe;
			int c;

			while (*p == '0' && isdigit(p[1])) p++;
			while (*q == '0' && isdigit(q[1])) q++;
			for (pe = p; isdigit(*pe); pe++) {}
			for (qe = q; isdigit(*qe); qe++) {}
			if (pe - p != qe - q) return pe - p < qe - q ? -1 : 1;
			c = memcmp(p, q, (size_t)(pe - p));
			if (c) return c < 0 ? -1 : 1;
			p = pe;
			q = qe;
			continue;
		}
		{
			unsigned x = name_char(&p), y = name_char(&q);

			if (x != y) return x < y ? -1 : 1;
		}
	}
	return *p ? 1 : *q ? -1 : 0;
}

static bool sorts_last(const system_cfg *s)
{
	return !strcmp(s->core, "port");
}

void cfg_sort_systems(systems_cfg *s)
{
	int i, j;

	/* Insertion sort: stable, and forty-odd systems once at start */
	for (i = 1; i < s->count; i++) {
		system_cfg t = s->systems[i];

		for (j = i - 1; j >= 0; j--) {
			const system_cfg *o = &s->systems[j];
			int c = sorts_last(o) != sorts_last(&t) ? (sorts_last(o) ? 1 : -1)
			      : cfg_name_cmp(o->name, t.name);

			if (c <= 0) break;
			s->systems[j + 1] = *o;
		}
		s->systems[j + 1] = t;
	}
}
