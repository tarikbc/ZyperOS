/* SPDX-License-Identifier: GPL-3.0-only */
/* The release manifest: reading it, and checking its signature.
 *
 * The format is text, one record a line, so it reads the same in C, in the
 * shell and in the installer, and nothing in it needs a JSON parser on the
 * device:
 *
 *   zyperos-update 1
 *   device pixel2
 *   version 0.2.0
 *   min-system 0.2.0
 *   pack ZyperOS-v0.2.0-pixel2-update.zpk <size> <sha256>
 *   dir <path>
 *   file <sha256> <size> <offset> <stored> <g|r> <path>
 *   end <number of file lines>
 *   sign <key id> <base64 Ed25519 signature>
 *
 * The signature covers every byte up to and including the end line's
 * newline. A path is the rest of its line, so it may hold spaces; it is
 * relative to the ZyperOS folder, and ".card/" in front means the card's
 * root instead (Licenses/, the Roms folders), which the switch adds to and
 * never removes from. Unknown keywords before "end" are skipped, so a later
 * format can add lines an older updater reads past; a change that must not
 * be read past bumps the first line. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>

#include "update.h"

static bool fail(char *err, size_t n, const char *fmt, const char *a, int line)
{
	if (err && n) {
		char what[160];
		snprintf(what, sizeof what, fmt, a ? a : "");
		if (line > 0) snprintf(err, n, "manifest line %d: %s", line, what);
		else snprintf(err, n, "manifest: %s", what);
	}
	return false;
}

bool up_path_ok(const char *p)
{
	const char *s = p, *c;
	size_t len = strlen(p);

	if (len == 0 || len > UP_MAX_PATH || p[0] == '/') return false;
	for (c = p; *c; c++)
		if ((unsigned char)*c < 0x20 || *c == 0x7f || *c == '\\' || *c == ':') return false;
	while (*s) {
		const char *e = strchr(s, '/');
		size_t n = e ? (size_t)(e - s) : strlen(s);
		if (n == 0) return false;
		if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return false;
		if (!e) break;
		s = e + 1;
		if (!*s) return false;   /* a trailing slash */
	}
	return true;
}

bool up_path_reserved(const char *p)
{
	static const char *const names[] = { ".files", ".update", ".ready", ".trial", ".staged" };
	size_t i, n = strcspn(p, "/");

	for (i = 0; i < sizeof names / sizeof *names; i++)
		if (strlen(names[i]) == n && !strncmp(p, names[i], n)) return true;
	return false;
}

/* Up to three numbers, then an optional "-something" that sorts below none. */
int up_version_cmp(const char *a, const char *b)
{
	int i;
	for (i = 0; i < 3; i++) {
		char *ea, *eb;
		unsigned long x = strtoul(a, &ea, 10), y = strtoul(b, &eb, 10);
		if (x != y) return x < y ? -1 : 1;
		a = ea; b = eb;
		if (*a == '.') a++;
		if (*b == '.') b++;
	}
	if (*a == '-' && *b != '-') return -1;
	if (*b == '-' && *a != '-') return 1;
	return strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0;
}

static bool is_hex64(const char *s)
{
	int i;
	for (i = 0; i < UP_SHA_HEX; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
	return s[UP_SHA_HEX] == 0;
}

static bool num(const char *s, uint64_t *out)
{
	char *e;
	if (!*s || *s == '-' || *s == '+') return false;
	errno = 0;
	*out = strtoull(s, &e, 10);
	return !errno && !*e;
}

static int cmp_path(const void *a, const void *b)
{
	return strcmp(((const up_file *)a)->path, ((const up_file *)b)->path);
}

/* A word of the line: the next space-separated token, NUL-terminated in place. */
static char *word(char **p)
{
	char *s = *p, *e;
	if (!s || !*s) return NULL;
	e = strchr(s, ' ');
	if (e) { *e = 0; *p = e + 1; }
	else *p = s + strlen(s);
	return s;
}

static bool b64_64(const char *in, unsigned char out[64])
{
	unsigned char buf[96];
	size_t n = strlen(in);
	int got;

	/* 64 bytes are 88 characters with two '=' of padding; EVP_DecodeBlock
	 * counts the padding as two zero bytes, which are dropped here */
	if (n != 88 || in[86] != '=' || in[87] != '=') return false;
	got = EVP_DecodeBlock(buf, (const unsigned char *)in, (int)n);
	if (got != 66) return false;
	memcpy(out, buf, 64);
	return true;
}

bool up_manifest_parse(const char *text, size_t len, up_manifest *m, char *err, size_t errn)
{
	char *copy, *line, *next;
	int lineno = 0, cap = 0, dcap = 0, i;
	bool ended = false, signed_ = false;
	uint64_t declared = 0;

	memset(m, 0, sizeof *m);
	if (len == 0 || len > UP_MAX_MANIFEST) return fail(err, errn, "empty or too big", NULL, 0);
	if (memchr(text, 0, len) || memchr(text, '\r', len))
		return fail(err, errn, "holds a NUL or CR", NULL, 0);
	if (text[len - 1] != '\n') return fail(err, errn, "does not end with a newline", NULL, 0);
	m->text = malloc(len + 1);
	copy = malloc(len + 1);
	if (!m->text || !copy) { free(copy); return fail(err, errn, "out of memory", NULL, 0); }
	memcpy(m->text, text, len);
	m->text[len] = 0;
	m->text_len = len;
	memcpy(copy, text, len);
	copy[len] = 0;

	for (line = copy; line && *line; line = next) {
		char *p, *kw;

		next = strchr(line, '\n');
		*next++ = 0;
		lineno++;
		p = line;
		if (lineno == 1) {
			if (strcmp(line, "zyperos-update 1")) { free(copy); return fail(err, errn, "not a format-1 ZyperOS update list", NULL, 1); }
			continue;
		}
		if (signed_) { free(copy); return fail(err, errn, "text after the signature", NULL, lineno); }
		kw = word(&p);
		if (!kw) { free(copy); return fail(err, errn, "empty line", NULL, lineno); }
		if (ended) {
			char *id, *sig;
			if (strcmp(kw, "sign")) { free(copy); return fail(err, errn, "%s after end", kw, lineno); }
			id = word(&p);
			sig = word(&p);
			if (!id || !sig || *p || strlen(id) != 16 || !b64_64(sig, m->sig)) {
				free(copy);
				return fail(err, errn, "bad sign line", NULL, lineno);
			}
			for (i = 0; i < 16; i++)
				if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
					free(copy);
					return fail(err, errn, "bad key id", NULL, lineno);
				}
			memcpy(m->keyid, id, 17);
			signed_ = true;
			continue;
		}
		if (!strcmp(kw, "kind")) {
			if ((strcmp(p, "folder") && strcmp(p, "system")) || m->kind[0]) { free(copy); return fail(err, errn, "bad kind line", NULL, lineno); }
			strcpy(m->kind, p);
		} else if (!strcmp(kw, "device") || !strcmp(kw, "version") || !strcmp(kw, "min-system")) {
			char *dst = kw[0] == 'd' ? m->device : kw[0] == 'v' ? m->version : m->min_system;
			if (!*p || strlen(p) >= 32 || strchr(p, ' ') || dst[0]) { free(copy); return fail(err, errn, "bad %s line", kw, lineno); }
			strcpy(dst, p);
		} else if (!strcmp(kw, "pack")) {
			char *name = word(&p), *sz = word(&p), *sha = word(&p);
			if (!name || !sz || !sha || *p || strlen(name) >= sizeof m->pack || !up_path_ok(name) || strchr(name, '/') ||
			    !num(sz, &m->pack_size) || !is_hex64(sha) || m->pack[0]) {
				free(copy);
				return fail(err, errn, "bad pack line", NULL, lineno);
			}
			strcpy(m->pack, name);
			strcpy(m->pack_sha, sha);
		} else if (!strcmp(kw, "dir")) {
			if (!up_path_ok(p) || up_path_reserved(p)) { free(copy); return fail(err, errn, "bad dir path", NULL, lineno); }
			if (m->ndirs == dcap) {
				char **d;
				dcap = dcap ? dcap * 2 : 64;
				d = realloc(m->dirs, (size_t)dcap * sizeof *d);
				if (!d) { free(copy); return fail(err, errn, "out of memory", NULL, 0); }
				m->dirs = d;
			}
			m->dirs[m->ndirs] = strdup(p);
			if (!m->dirs[m->ndirs++]) { free(copy); return fail(err, errn, "out of memory", NULL, 0); }
		} else if (!strcmp(kw, "file")) {
			up_file f;
			char *sha = word(&p), *sz = word(&p), *of = word(&p), *st = word(&p), *me = word(&p);
			memset(&f, 0, sizeof f);
			if (!sha || !me || !is_hex64(sha) || !num(sz, &f.size) || !num(of, &f.offset) ||
			    !num(st, &f.stored) || strlen(me) != 1 || (me[0] != 'g' && me[0] != 'r') ||
			    !up_path_ok(p) || up_path_reserved(p) || (me[0] == 'r' && f.stored != f.size)) {
				free(copy);
				return fail(err, errn, "bad file line", NULL, lineno);
			}
			if (m->nfiles >= UP_MAX_FILES) { free(copy); return fail(err, errn, "too many files", NULL, lineno); }
			if (m->nfiles == cap) {
				up_file *nf;
				cap = cap ? cap * 2 : 256;
				nf = realloc(m->files, (size_t)cap * sizeof *nf);
				if (!nf) { free(copy); return fail(err, errn, "out of memory", NULL, 0); }
				m->files = nf;
			}
			strcpy(f.sha, sha);
			f.method = me[0];
			f.path = strdup(p);
			if (!f.path) { free(copy); return fail(err, errn, "out of memory", NULL, 0); }
			m->files[m->nfiles++] = f;
			m->total += f.size;
		} else if (!strcmp(kw, "end")) {
			if (!num(p, &declared) || declared != (uint64_t)m->nfiles) {
				free(copy);
				return fail(err, errn, "the end line's count is not the number of files", NULL, lineno);
			}
			ended = true;
			m->body_len = (size_t)(next - copy);
		}
		/* anything else: a later format's line, read past */
	}
	free(copy);
	if (!ended) return fail(err, errn, "no end line (cut short?)", NULL, 0);
	if (!signed_) return fail(err, errn, "not signed", NULL, 0);
	if (!m->device[0] || !m->version[0] || !m->pack[0] || m->nfiles == 0)
		return fail(err, errn, "device, version, pack or files missing", NULL, 0);
	if (!m->min_system[0]) strcpy(m->min_system, "0");
	/* A list from before the kind line is the folder's */
	if (!m->kind[0]) strcpy(m->kind, "folder");
	/* The pack's members must lie inside it and not overlap */
	for (i = 0; i < m->nfiles; i++) {
		const up_file *f = &m->files[i];
		if (f->offset > m->pack_size || f->stored > m->pack_size - f->offset)
			return fail(err, errn, "%s lies outside the pack", f->path, 0);
	}
	qsort(m->files, (size_t)m->nfiles, sizeof *m->files, cmp_path);
	for (i = 1; i < m->nfiles; i++)
		if (!strcmp(m->files[i - 1].path, m->files[i].path))
			return fail(err, errn, "%s is listed twice", m->files[i].path, 0);
	return true;
}

void up_manifest_free(up_manifest *m)
{
	int i;
	for (i = 0; i < m->nfiles; i++) free(m->files[i].path);
	for (i = 0; i < m->ndirs; i++) free(m->dirs[i]);
	free(m->files);
	free(m->dirs);
	free(m->text);
	memset(m, 0, sizeof *m);
}

void up_sha256_hex(const unsigned char d[32], char out[UP_SHA_HEX + 1])
{
	static const char hx[] = "0123456789abcdef";
	int i;
	for (i = 0; i < 32; i++) {
		out[2 * i] = hx[d[i] >> 4];
		out[2 * i + 1] = hx[d[i] & 15];
	}
	out[UP_SHA_HEX] = 0;
}

static EVP_PKEY *read_key(const char *path)
{
	FILE *f = fopen(path, "r");
	EVP_PKEY *k;
	if (!f) return NULL;
	k = PEM_read_PUBKEY(f, NULL, NULL, NULL);
	fclose(f);
	if (k && EVP_PKEY_id(k) != EVP_PKEY_ED25519) { EVP_PKEY_free(k); k = NULL; }
	return k;
}

static bool key_id_of(EVP_PKEY *k, char out[17])
{
	unsigned char raw[32], d[32];
	size_t n = sizeof raw;
	unsigned int dn = 0;
	char hex[UP_SHA_HEX + 1];

	if (EVP_PKEY_get_raw_public_key(k, raw, &n) != 1 || n != 32) return false;
	if (EVP_Digest(raw, 32, d, &dn, EVP_sha256(), NULL) != 1) return false;
	up_sha256_hex(d, hex);
	memcpy(out, hex, 16);
	out[16] = 0;
	return true;
}

bool up_key_id(const char *pem_path, char out[17])
{
	EVP_PKEY *k = read_key(pem_path);
	bool ok = k && key_id_of(k, out);
	EVP_PKEY_free(k);
	return ok;
}

bool up_manifest_verify(const up_manifest *m, const char *keys_dir, char *err, size_t errn)
{
	char path[512], id[17];
	EVP_PKEY *k;
	EVP_MD_CTX *ctx;
	bool ok;

	snprintf(path, sizeof path, "%s/%s.pem", keys_dir, m->keyid);
	k = read_key(path);
	if (!k) return fail(err, errn, "signed with key %s, which this system does not have", m->keyid, 0);
	/* The file's name is only where to look: the key inside must be the one
	 * the manifest names */
	if (!key_id_of(k, id) || strcmp(id, m->keyid)) {
		EVP_PKEY_free(k);
		return fail(err, errn, "key file %s holds another key", m->keyid, 0);
	}
	ctx = EVP_MD_CTX_new();
	ok = ctx && EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, k) == 1 &&
	     EVP_DigestVerify(ctx, m->sig, 64, (const unsigned char *)m->text, m->body_len) == 1;
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(k);
	if (!ok) return fail(err, errn, "the signature does not match", NULL, 0);
	return true;
}

bool up_sha256_file(const char *path, char out[UP_SHA_HEX + 1], bool drop)
{
	unsigned char buf[1 << 16], d[32];
	unsigned int dn = 0;
	EVP_MD_CTX *ctx;
	ssize_t r;
	int fd = open(path, O_RDONLY);
	bool ok = false;

	if (fd < 0) return false;
#ifdef POSIX_FADV_DONTNEED
	if (drop) posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#else
	(void)drop;
#endif
	ctx = EVP_MD_CTX_new();
	if (ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1) {
		while ((r = read(fd, buf, sizeof buf)) > 0)
			if (EVP_DigestUpdate(ctx, buf, (size_t)r) != 1) break;
		if (r == 0 && EVP_DigestFinal_ex(ctx, d, &dn) == 1) {
			up_sha256_hex(d, out);
			ok = true;
		}
	}
	EVP_MD_CTX_free(ctx);
	close(fd);
	return ok;
}
