/* SPDX-License-Identifier: GPL-3.0-only */
/* See devmode.h. */
#include "devmode.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define KEY "devmode"        /* the device database: 1 unlocked, 0 or missing locked */

/* ---- the unlock ------------------------------------------------------- */

bool devmode_hold_step(devmode_hold *h, bool both_down, unsigned now_ms)
{
	if (!both_down) {
		h->held = h->fired = false;
		return false;
	}
	if (!h->held) {
		h->held = true;
		h->since = now_ms;
		return false;
	}
	if (!h->fired && now_ms - h->since >= DEVMODE_HOLD_MS) {
		h->fired = true;
		return true;
	}
	return false;
}

int devmode_hold_permille(const devmode_hold *h, unsigned now_ms)
{
	unsigned t;

	if (!h->held || h->fired) return 0;
	t = now_ms - h->since;
	return t >= DEVMODE_HOLD_MS ? 1000 : (int)(t * 1000u / DEVMODE_HOLD_MS);
}

void devmode_hold_reset(devmode_hold *h)
{
	h->held = h->fired = false;
}

/* ---- the setting ------------------------------------------------------ */

bool devmode_unlocked(db *dev)
{
	return dev && db_get_int(dev, KEY, 0) == 1;
}

void devmode_set(db *dev, bool on)
{
	if (dev) db_set_int(dev, KEY, on ? 1 : 0);
}

/* ---- the password ----------------------------------------------------- */

/* No i, l, o, 0 or 1: 31 characters, so a byte below 248 (8 times 31) maps
 * with no bias. Typed on a computer's keyboard from a 640x480 screen. */
static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
#define ALPHA_N 31

bool devmode_password(char *out, size_t n)
{
	size_t got = 0;
	int fd;

	if (!out || !n) return false;
	out[0] = '\0';
	if (n < DEVMODE_PASSWORD_LEN + 1) return false;
	if ((fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC)) < 0) return false;
	while (got < DEVMODE_PASSWORD_LEN) {
		unsigned char buf[32];
		ssize_t r = read(fd, buf, sizeof buf);
		ssize_t i;

		if (r <= 0) {
			if (r < 0 && errno == EINTR) continue;
			close(fd);
			memset(out, 0, n);
			return false;
		}
		for (i = 0; i < r && got < DEVMODE_PASSWORD_LEN; i++)
			if (buf[i] < 8 * ALPHA_N) out[got++] = ALPHABET[buf[i] % ALPHA_N];
		memset(buf, 0, sizeof buf);
	}
	close(fd);
	out[got] = '\0';
	return true;
}

/* ---- the keys --------------------------------------------------------- */

#define KEYS_MAX 65536       /* either file: a file that size is not a key list */
#define LINE_MAX_LEN 2048

static bool blob_char(int c)
{
	return isalnum(c) || c == '+' || c == '/' || c == '=';
}

/* [type][blob] of a key line, as two spans, or false: the first word is a
 * key type and the second a base64 blob, and only a comment may follow. */
static bool key_parts(const char *line, const char **type, size_t *tn,
                      const char **blob, size_t *bn)
{
	static const char *const TYPES[] = {
		"ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384",
		"ecdsa-sha2-nistp521", "sk-ssh-ed25519@openssh.com",
		"sk-ecdsa-sha2-nistp256@openssh.com",
	};
	const char *p = line, *e;
	size_t i, len;

	while (*p == ' ' || *p == '\t') p++;
	for (e = p; *e && *e != ' ' && *e != '\t'; e++) {}
	len = (size_t)(e - p);
	for (i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++)
		if (strlen(TYPES[i]) == len && !memcmp(TYPES[i], p, len)) break;
	if (i == sizeof TYPES / sizeof TYPES[0]) return false;
	*type = p;
	*tn = len;
	p = e;
	if (*p != ' ' && *p != '\t') return false;
	while (*p == ' ' || *p == '\t') p++;
	for (e = p; blob_char((unsigned char)*e); e++) {}
	/* Even the shortest (ed25519) is 68 characters */
	if (e - p < 60 || (*e && *e != ' ' && *e != '\t')) return false;
	*blob = p;
	*bn = (size_t)(e - p);
	/* What follows is a comment: text, never a control character */
	for (p = e; *p; p++)
		if ((unsigned char)*p < 0x20 && *p != '\t') return false;
	return true;
}

bool devmode_key_line(const char *line)
{
	const char *t, *b;
	size_t tn, bn;

	return key_parts(line, &t, &tn, &b, &bn);
}

/* The whole of a file into memory, NUL-ended, or NULL (and *len 0) when it
 * is missing; an empty file is "" */
static char *slurp(const char *path, size_t *len)
{
	char *buf;
	ssize_t got;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	*len = 0;
	if (fd < 0) return NULL;
	if (!(buf = malloc(KEYS_MAX + 1))) { close(fd); return NULL; }
	got = read(fd, buf, KEYS_MAX);
	close(fd);
	if (got < 0) { free(buf); return NULL; }
	buf[got] = '\0';
	*len = (size_t)got;
	return buf;
}

/* `text` has a line holding this type and blob */
static bool has_key(const char *text, const char *type, size_t tn, const char *blob, size_t bn)
{
	const char *p = text;

	while (*p) {
		const char *eol = strchr(p, '\n'), *t, *b;
		char line[LINE_MAX_LEN + 1];
		size_t ll = eol ? (size_t)(eol - p) : strlen(p), tn2, bn2;

		if (ll <= LINE_MAX_LEN) {
			memcpy(line, p, ll);
			line[ll] = '\0';
			if (key_parts(line, &t, &tn2, &b, &bn2) && tn2 == tn && bn2 == bn &&
			    !memcmp(t, type, tn) && !memcmp(b, blob, bn)) return true;
		}
		if (!eol) break;
		p = eol + 1;
	}
	return false;
}

int devmode_import_keys(const char *src, const char *keydir)
{
	char path[640], tmp[660];
	char *in, *have = NULL, *out = NULL;
	size_t inn, haven = 0, outn = 0, outcap;
	int taken = 0, added = 0, fd, rc = -1;
	const char *p;

	if (!src || !keydir || !(in = slurp(src, &inn))) return -1;
	if (inn >= KEYS_MAX) { free(in); return -1; }
	snprintf(path, sizeof path, "%s/authorized_keys", keydir);
	have = slurp(path, &haven);
	if (haven >= KEYS_MAX) { free(in); free(have); return -1; }
	outcap = haven + inn + 4;
	if (!(out = malloc(outcap))) goto out;
	if (have) memcpy(out, have, haven);
	outn = haven;
	/* A last line with no end gets one before more follow it */
	if (outn && out[outn - 1] != '\n') out[outn++] = '\n';

	for (p = in; *p; ) {
		const char *eol = strchr(p, '\n'), *t, *b;
		char line[LINE_MAX_LEN + 1];
		size_t ll = eol ? (size_t)(eol - p) : strlen(p), tn, bn;

		while (ll && (p[ll - 1] == '\r' || p[ll - 1] == ' ' || p[ll - 1] == '\t')) ll--;
		if (ll <= LINE_MAX_LEN) {
			memcpy(line, p, ll);
			line[ll] = '\0';
			if (key_parts(line, &t, &tn, &b, &bn)) {
				taken++;
				out[outn] = '\0';
				if (!has_key(out, t, tn, b, bn)) {
					/* the line as written, from its first non-blank */
					const char *s = line;

					while (*s == ' ' || *s == '\t') s++;
					memcpy(out + outn, s, strlen(s));
					outn += strlen(s);
					out[outn++] = '\n';
					added++;
				}
			}
		}
		if (!eol) break;
		p = eol + 1;
	}
	rc = taken;
	if (!added) goto out;

	if (mkdir(keydir, 0700) != 0 && errno != EEXIST) { rc = -1; goto out; }
	snprintf(tmp, sizeof tmp, "%s.new", path);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600)) < 0) { rc = -1; goto out; }
	if (write(fd, out, outn) != (ssize_t)outn) {
		close(fd);
		unlink(tmp);
		rc = -1;
		goto out;
	}
	fsync(fd);
	close(fd);
	if (rename(tmp, path) != 0) { unlink(tmp); rc = -1; }
out:
	free(in);
	free(have);
	free(out);
	return rc;
}

static bool key_file_name(const char *name)
{
	size_t n = strlen(name);

	if (name[0] == '.') return false;          /* the Mac's ._ litter among them */
	return !strcmp(name, DEVMODE_KEYS_FILE) ||
	       (n > 4 && !strcasecmp(name + n - 4, ".pub"));
}

int devmode_import_dir(const char *dir, const char *keydir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int taken = 0, files = 0;

	if (!d) return -1;
	while ((e = readdir(d)) && files < DEVMODE_KEY_FILES) {
		char path[1024];
		struct stat st;
		int got;

		if (!key_file_name(e->d_name)) continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		files++;
		if ((got = devmode_import_keys(path, keydir)) > 0) taken += got;
	}
	closedir(d);
	return taken;
}
