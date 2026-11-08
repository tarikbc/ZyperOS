/* SPDX-License-Identifier: GPL-3.0-only */
/* Which files on the Ports shelf are a port's own: src/portinfo.h says why. */
#include "portinfo.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define PI_JSON_MAX (256 * 1024)
#define PI_SH_MAX   (256 * 1024)

bool pi_runtime(const char *name)
{
	return name && (!strcasecmp(name, "PortMaster.sh") || !strcasecmp(name, "PortMaster") ||
	                !strcasecmp(name, "PortMaster/"));
}

static char *slurp(const char *path, size_t max, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	size_t n;

	if (!f) return NULL;
	if (!(buf = malloc(max + 1))) { fclose(f); return NULL; }
	n = fread(buf, 1, max, f);
	fclose(f);
	buf[n] = '\0';
	if (len) *len = n;
	return buf;
}

/* The UTF-8 of code point `c` into `out`, with room for 4 */
static size_t utf8(unsigned c, char *out)
{
	if (c < 0x80) { out[0] = (char)c; return 1; }
	if (c < 0x800) { out[0] = (char)(0xC0 | (c >> 6)); out[1] = (char)(0x80 | (c & 0x3F)); return 2; }
	out[0] = (char)(0xE0 | (c >> 12));
	out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
	out[2] = (char)(0x80 | (c & 0x3F));
	return 3;
}

/* The JSON string at s[*i] (on its opening quote) into `out`, *i after it.
 * False when there is none. */
static bool jstring(const char *s, size_t *i, char *out, size_t n)
{
	size_t o = 0;

	if (s[*i] != '"') return false;
	for ((*i)++; s[*i] && s[*i] != '"'; (*i)++) {
		char tmp[4];
		size_t k = 1;

		tmp[0] = s[*i];
		if (s[*i] == '\\' && s[*i + 1]) {
			char e = s[++(*i)];

			if (e == 'n') tmp[0] = '\n';
			else if (e == 't') tmp[0] = '\t';
			else if (e == 'u') {
				unsigned c = 0;
				int d;

				for (d = 0; d < 4 && s[*i + 1]; d++) {
					char h = s[++(*i)];

					c = c * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' :
					                        h >= 'a' && h <= 'f' ? h - 'a' + 10 :
					                        h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
				}
				k = utf8(c, tmp);
			} else {
				tmp[0] = e;
			}
		}
		if (o + k < n) { memcpy(out + o, tmp, k); o += k; }
	}
	if (s[*i] != '"') return false;
	(*i)++;
	if (n) out[o < n ? o : n - 1] = '\0';
	return true;
}

static void skip_ws(const char *s, size_t *i)
{
	while (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r') (*i)++;
}

/* Where the value of "key" begins, at or after `from`: the first such key
 * followed by a colon. 0 when there is none. */
static size_t jkey(const char *s, size_t from, const char *key)
{
	char pat[64];
	const char *p = s + from;

	snprintf(pat, sizeof pat, "\"%s\"", key);
	while ((p = strstr(p, pat))) {
		size_t i = (size_t)(p - s) + strlen(pat);

		skip_ws(s, &i);
		if (s[i] == ':') {
			i++;
			skip_ws(s, &i);
			return i;
		}
		p++;
	}
	return 0;
}

/* The folder at the top of `dir` that `name` is, by the directory's own
 * listing: its real name into `out`. Never by a path handed to the
 * filesystem, which on the card's exFAT can drop a name's trailing dots or
 * spaces and land on another folder ("PortMaster." on PortMaster); and a
 * name that ends in either is no folder's. Case does not count, as on the
 * card. */
bool pi_real_folder(const char *dir, const char *name, char *out, size_t n)
{
	DIR *d;
	struct dirent *e;
	size_t len = strlen(name);
	bool found = false;

	if (!len || name[len - 1] == '.' || name[len - 1] == ' ' || name[0] == '.' ||
	    name[0] == ' ' || strchr(name, '/'))
		return false;
	if (!(d = opendir(dir))) return false;
	while ((e = readdir(d)) && !found) {
		char p[PI_NAME * 3];
		struct stat st;

		if (e->d_name[0] == '.' || strcasecmp(e->d_name, name)) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		snprintf(out, n, "%s", e->d_name);
		found = true;
	}
	closedir(d);
	return found;
}

static void read_port(const char *dir, const char *folder, const char *json, pi_port *pt)
{
	size_t i;

	memset(pt, 0, sizeof *pt);
	snprintf(pt->folder, sizeof pt->folder, "%s", folder);
	if ((i = jkey(json, 0, "items")) && json[i] == '[') {
		for (i++;;) {
			char item[PI_NAME], real[PI_NAME];
			size_t len;

			skip_ws(json, &i);
			if (json[i] == ']' || !json[i]) break;
			if (json[i] == ',') { i++; continue; }
			if (!jstring(json, &i, item, sizeof item)) break;
			len = strlen(item);
			if (len && item[len - 1] == '/') item[--len] = '\0';
			/* One name at the top of the folder: nothing with a slash or a
			 * dot first, which no port's own folder or script is */
			if (!len || strchr(item, '/') || item[0] == '.') continue;
			/* A folder only as the listing names it (pi_real_folder) */
			if (pi_real_folder(dir, item, real, sizeof real)) {
				if (pt->nfolders < PI_ITEMS) snprintf(pt->folders[pt->nfolders++], PI_NAME, "%s", real);
			} else if (len > 3 && !strcasecmp(item + len - 3, ".sh") && pt->nscripts < PI_ITEMS) {
				snprintf(pt->scripts[pt->nscripts++], PI_NAME, "%s", item);
			}
		}
	}
	if ((i = jkey(json, 0, "attr")) && json[i] == '{' && (i = jkey(json, i, "title")))
		jstring(json, &i, pt->title, sizeof pt->title);
}

int pi_scan(const char *dir, pi_port *out, int max)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (!d) return 0;
	while ((e = readdir(d)) && n < max) {
		char path[PI_NAME * 3];
		struct stat st;
		char *json;

		if (e->d_name[0] == '.' || pi_runtime(e->d_name)) continue;
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
		if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		if (snprintf(path, sizeof path, "%s/%s/port.json", dir, e->d_name) >= (int)sizeof path) continue;
		if (!(json = slurp(path, PI_JSON_MAX, NULL))) continue;
		read_port(dir, e->d_name, json, &out[n++]);
		free(json);
	}
	closedir(d);
	return n;
}

const pi_port *pi_of_folder(const pi_port *all, int n, const char *folder)
{
	int i;

	for (i = 0; i < n; i++)
		if (!strcasecmp(all[i].folder, folder)) return &all[i];
	return NULL;
}

/* The folder a script's GAMEDIR names: its last part, "" for none */
static void gamedir(const char *dir, const char *script, char *out, size_t n)
{
	char path[PI_NAME * 3], *sh, *p;

	out[0] = '\0';
	if (snprintf(path, sizeof path, "%s/%s", dir, script) >= (int)sizeof path) return;
	if (!(sh = slurp(path, PI_SH_MAX, NULL))) return;
	for (p = sh; (p = strstr(p, "GAMEDIR=")); p++) {
		const char *v = p + 8, *end;
		char val[PI_NAME * 2];
		size_t len;
		char *last;

		/* At the start of its line, or after "export " */
		if (p != sh && p[-1] != '\n' && p[-1] != ' ' && p[-1] != '\t') continue;
		if (*v == '"' || *v == '\'') v++;
		for (end = v; *end && *end != '"' && *end != '\'' && *end != '\n' && *end != ' '; end++) {}
		len = (size_t)(end - v);
		if (!len || len >= sizeof val) continue;
		memcpy(val, v, len);
		val[len] = '\0';
		while (len && val[len - 1] == '/') val[--len] = '\0';
		last = strrchr(val, '/');
		last = last ? last + 1 : val;
		if (!*last || strchr(last, '$') || last[0] == '.') continue;
		snprintf(out, n, "%s", last);
		break;
	}
	free(sh);
}

int pi_folders(const char *dir, const pi_port *all, int n, const char *script,
               char out[][PI_NAME], int max)
{
	int i, k, m = 0;
	char gd[PI_NAME];

	if (pi_runtime(script)) return 0;
	for (i = 0; i < n; i++)
		for (k = 0; k < all[i].nscripts; k++)
			if (!strcasecmp(all[i].scripts[k], script)) {
				int f;

				for (f = 0; f < all[i].nfolders && m < max; f++) {
					char real[PI_NAME];

					/* Held to the listing again: it may have gone since */
					if (!pi_real_folder(dir, all[i].folders[f], real, sizeof real) || pi_runtime(real)) continue;
					snprintf(out[m++], PI_NAME, "%s", real);
				}
				if (m) return m;
			}
	gamedir(dir, script, gd, sizeof gd);
	if (gd[0] && m < max) {
		char real[PI_NAME];

		if (pi_real_folder(dir, gd, real, sizeof real) && !pi_runtime(real))
			snprintf(out[m++], PI_NAME, "%s", real);
	}
	return m;
}
