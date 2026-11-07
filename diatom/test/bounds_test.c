/* SPDX-License-Identifier: MPL-2.0 */
/* Input that does not fit what reads it: a file too short to be a zip, a zip
 * whose directory says more than it holds, and a protocol line longer than
 * the buffer it is formatted in.
 *
 *   - A .zip under 22 bytes, the size of the end-of-directory record: the
 *     scan for that record started at (size - 22 + 1) in a size_t, so a file
 *     of 0 to 20 bytes wrapped it and read far past the stack. A 0-byte .zip
 *     ended Diatom with SIGSEGV.
 *   - An entry with no name: the directory test read the byte before the
 *     name's buffer.
 *   - A stored entry that claims more bytes than it holds: they were copied
 *     out of a buffer only as long as what it holds.
 *   - A line of 1022 characters or more: the newline went where vsnprintf
 *     said the line would end, past the buffer, and send() read on past it.
 *     A state saved under a 1,099-character path was enough.
 *
 * Offline and instant, like check-cheevos: it links src/zip.c and src/proto.c
 * and nothing else, builds its zips itself, and talks to the protocol over a
 * socket of its own. The Makefile builds it with AddressSanitizer where the
 * compiler has it, because a read past a buffer that lands on mapped memory
 * passes unseen otherwise; every case also fails without it, on what the call
 * returns or on what reaches the socket.
 *
 * Build and run:  make check-bounds
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <zlib.h>

#include "diatom.h"

static int g_fail, g_pass;
static char g_dir[256];

static void ck(int cond, const char *what)
{
	if (cond) { g_pass++; return; }
	g_fail++;
	printf("  FAIL %s\n", what);
}

/* proto.c logs through the port; nothing here needs to see it. */
void diatom_port_log(diatom_log_level lvl, const char *msg)
{
	(void)lvl; (void)msg;
}

/* ---- zips, written byte by byte ----------------------------------------- */

typedef struct {
	const char          *name;    /* "" writes an entry with no name */
	const unsigned char *data;
	uint32_t             len;
	bool                 deflate;
	uint32_t             usize;   /* what the directory claims; 0: the truth */
} entry;

typedef struct { unsigned char b[1 << 16]; size_t n; } zbuf;

static void put(zbuf *z, const void *p, size_t n) { memcpy(z->b + z->n, p, n); z->n += n; }
static void put16(zbuf *z, uint32_t v) { unsigned char b[2] = { v, v >> 8 }; put(z, b, 2); }
static void put32(zbuf *z, uint32_t v)
{ unsigned char b[4] = { v, v >> 8, v >> 16, v >> 24 }; put(z, b, 4); }

/* Raw deflate, as a zip stores it. */
static uint32_t deflate_raw(const unsigned char *in, uint32_t len, unsigned char *out, uint32_t cap)
{
	z_stream s;

	memset(&s, 0, sizeof s);
	if (deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return 0;
	s.next_in = (unsigned char *)in;  s.avail_in = len;
	s.next_out = out;                 s.avail_out = cap;
	if (deflate(&s, Z_FINISH) != Z_STREAM_END) { deflateEnd(&s); return 0; }
	deflateEnd(&s);
	return (uint32_t)s.total_out;
}

/* A zip of `n` entries at `path`. `cd_off_override` moves where the end
 * record says the directory starts (0 leaves it where it is). */
static void write_zip(const char *path, const entry *e, int n, uint32_t cd_off_override)
{
	static zbuf z;
	static unsigned char comp[8][8192];
	uint32_t lho[8], csize[8], cd_off, cd_len;
	FILE *f;
	int i;

	z.n = 0;
	for (i = 0; i < n; i++) {
		const unsigned char *body = e[i].data;
		uint16_t nlen = (uint16_t)strlen(e[i].name);

		csize[i] = e[i].len;
		if (e[i].deflate) {
			csize[i] = deflate_raw(e[i].data, e[i].len, comp[i], sizeof comp[i]);
			body = comp[i];
		}
		lho[i] = (uint32_t)z.n;
		put32(&z, 0x04034b50); put16(&z, 20); put16(&z, 0);
		put16(&z, e[i].deflate ? 8 : 0); put16(&z, 0); put16(&z, 0);
		put32(&z, crc32(0, e[i].data, e[i].len));
		put32(&z, csize[i]); put32(&z, e[i].usize ? e[i].usize : e[i].len);
		put16(&z, nlen); put16(&z, 0);
		put(&z, e[i].name, nlen);
		put(&z, body, csize[i]);
	}
	cd_off = (uint32_t)z.n;
	for (i = 0; i < n; i++) {
		uint16_t nlen = (uint16_t)strlen(e[i].name);

		put32(&z, 0x02014b50); put16(&z, 20); put16(&z, 20); put16(&z, 0);
		put16(&z, e[i].deflate ? 8 : 0); put16(&z, 0); put16(&z, 0);
		put32(&z, crc32(0, e[i].data, e[i].len));
		put32(&z, csize[i]); put32(&z, e[i].usize ? e[i].usize : e[i].len);
		put16(&z, nlen); put16(&z, 0); put16(&z, 0); put16(&z, 0); put16(&z, 0);
		put32(&z, 0); put32(&z, lho[i]);
		put(&z, e[i].name, nlen);
	}
	cd_len = (uint32_t)z.n - cd_off;
	put32(&z, 0x06054b50); put16(&z, 0); put16(&z, 0);
	put16(&z, (uint32_t)n); put16(&z, (uint32_t)n);
	put32(&z, cd_len); put32(&z, cd_off_override ? cd_off_override : cd_off);
	put16(&z, 0);

	f = fopen(path, "wb");
	if (!f || fwrite(z.b, 1, z.n, f) != z.n) { perror(path); exit(2); }
	fclose(f);
}

/* Loads `path`; true when it loaded, with what it held in *data and *len. */
static bool load(const char *path, void **data, size_t *len, char *name, size_t name_n)
{
	bool ok = diatom_zip_load(path, data, len, name, name_n);

	if (!ok && (*data || *len)) { ck(0, "a refused zip hands back no bytes"); }
	return ok;
}

static void zips(void)
{
	static const size_t shorts[] = { 0, 1, 4, 20, 21 };
	unsigned char rom[4096], small[64];
	char path[512], name[512], what[128];
	void *data;
	size_t len, k;

	for (k = 0; k < sizeof rom; k++) rom[k] = (unsigned char)(k * 7 + k / 13);
	for (k = 0; k < sizeof small; k++) small[k] = (unsigned char)(0xA0 + k);

	/* Shorter than the end record. The four at the start are its signature,
	 * so a scan that looked would find one. */
	for (k = 0; k < sizeof shorts / sizeof shorts[0]; k++) {
		unsigned char b[21] = { 'P', 'K', 5, 6 };
		FILE *f;

		snprintf(path, sizeof path, "%s/short-%zu.zip", g_dir, shorts[k]);
		f = fopen(path, "wb");
		if (!f || fwrite(b, 1, shorts[k], f) != shorts[k]) { perror(path); exit(2); }
		fclose(f);
		snprintf(what, sizeof what, "a %zu-byte .zip is refused", shorts[k]);
		ck(!load(path, &data, &len, name, sizeof name), what);
	}

	/* A zip as a set ships one: a folder, a small file and the ROM, deflated.
	 * The largest entry is the game, under its own name, byte for byte. */
	{
		entry e[] = {
			{ "Game/",              (const unsigned char *)"", 0,              false, 0 },
			{ "Game/readme.txt",    small,                     sizeof small,   false, 0 },
			{ "Game/Game (World).gb", rom,                     sizeof rom,     true,  0 },
		};

		snprintf(path, sizeof path, "%s/good.zip", g_dir);
		write_zip(path, e, 3, 0);
		ck(load(path, &data, &len, name, sizeof name) && len == sizeof rom &&
		   !memcmp(data, rom, sizeof rom) && !strcmp(name, "Game/Game (World).gb"),
		   "a good zip loads its largest entry, deflated, as it always did");
		free(data);

		e[2].deflate = false;
		write_zip(path, e, 3, 0);
		ck(load(path, &data, &len, name, sizeof name) && len == sizeof rom &&
		   !memcmp(data, rom, sizeof rom) && !strcmp(name, "Game/Game (World).gb"),
		   "and stored");
		free(data);
	}

	/* An entry with no name is passed over, even as the largest */
	{
		entry e[] = {
			{ "",        rom,   sizeof rom,   false, 0 },
			{ "game.gb", small, sizeof small, false, 0 },
		};

		snprintf(path, sizeof path, "%s/noname.zip", g_dir);
		write_zip(path, e, 2, 0);
		ck(load(path, &data, &len, name, sizeof name) && len == sizeof small &&
		   !memcmp(data, small, sizeof small) && !strcmp(name, "game.gb"),
		   "an entry with no name is passed over for the one with a name");
		free(data);

		write_zip(path, e, 1, 0);
		ck(!load(path, &data, &len, name, sizeof name),
		   "a zip whose only entry has no name is refused");
	}

	/* A stored entry that claims 4096 bytes and holds 64 */
	{
		entry e[] = { { "game.gb", small, sizeof small, false, sizeof rom } };

		snprintf(path, sizeof path, "%s/liar.zip", g_dir);
		write_zip(path, e, 1, 0);
		ck(!load(path, &data, &len, name, sizeof name),
		   "a stored entry that claims more than it holds is refused");
		free(data);
	}

	/* The end record puts the directory past the end of the file */
	{
		entry e[] = { { "game.gb", small, sizeof small, false, 0 } };

		snprintf(path, sizeof path, "%s/far.zip", g_dir);
		write_zip(path, e, 1, 0x7fffff00u);
		ck(!load(path, &data, &len, name, sizeof name),
		   "a directory said to start past the end of the file is refused");
	}
}

/* ---- the protocol: one line longer than the buffer ----------------------- */

/* Reads lines until `want` have arrived or a second passes; returns how many
 * bytes came, all of them, into `buf`. */
static size_t read_lines(int fd, char *buf, size_t cap, int want)
{
	size_t got = 0;
	int lines = 0;

	while (lines < want && got < cap) {
		ssize_t r = recv(fd, buf + got, cap - got, 0);
		size_t k;

		if (r <= 0) break;
		for (k = got; k < got + (size_t)r; k++)
			if (buf[k] == '\n') lines++;
		got += (size_t)r;
	}
	return got;
}

static void proto(void)
{
	struct sockaddr_un addr;
	struct timeval tv = { 1, 0 };
	char path[512], *line, *buf, *nl;
	diatom_msg m;
	size_t got, k;
	int fd;

	snprintf(path, sizeof path, "%s/s", g_dir);
	if (strlen(path) >= sizeof addr.sun_path) {
		ck(0, "the socket path fits sockaddr_un");
		return;
	}
	ck(diatom_proto_listen(path), "listens");
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		perror("connect");
		ck(0, "connects");
		diatom_proto_close();
		return;
	}
	diatom_proto_poll(&m, 1000, false);   /* takes the connection, says READY */
	ck(diatom_proto_connected(), "the connection is taken");

	buf  = malloc(8192);
	line = malloc(2048);
	got = read_lines(fd, buf, 8192, 1);
	ck(got > 6 && !memcmp(buf, "READY\t", 6) && buf[got - 1] == '\n', "READY arrives");

	/* 1022 characters is the longest line the buffer holds whole */
	memset(line, 'x', 1022);
	line[1022] = '\0';
	diatom_proto_send("%s", line);
	got = read_lines(fd, buf, 8192, 1);
	ck(got == 1023 && !memcmp(buf, line, 1022) && buf[1022] == '\n',
	   "a line of 1022 characters arrives whole");

	/* A state saved under a 1,100-character path: the line is cut, and the
	 * line after it arrives as a line of its own */
	line[0] = '/';
	memset(line + 1, 'p', 1099);
	line[1100] = '\0';
	diatom_proto_send("SAVED\tpath=%s", line);
	diatom_proto_send("IDLE");
	got = read_lines(fd, buf, 8192, 2);
	nl = memchr(buf, '\n', got);
	ck(nl && nl - buf == 1022, "a longer line arrives cut at 1022 characters");
	ck(nl && !memcmp(buf, "SAVED\tpath=/ppp", 15), "its start as it was");
	for (k = 0; nl && k < (size_t)(nl - buf); k++) if (!buf[k]) break;
	ck(nl && k == (size_t)(nl - buf), "with no NUL in it");
	ck(nl && got == 1023 + 5 && !memcmp(nl + 1, "IDLE\n", 5),
	   "and nothing after it but the next line");

	free(buf);
	free(line);
	close(fd);
	diatom_proto_close();
}

int main(void)
{
	const char *t = getenv("TMPDIR");
	char tmpl[256];

	/* A socket's path must fit sockaddr_un; a long TMPDIR goes to /tmp */
	snprintf(tmpl, sizeof tmpl, "%s/diatom-bounds-XXXXXX", t && *t && strlen(t) < 60 ? t : "/tmp");
	if (!mkdtemp(tmpl)) { perror(tmpl); return 2; }
	snprintf(g_dir, sizeof g_dir, "%s", tmpl);

	zips();
	proto();

	{
		static const char *made[] = {
			"short-0.zip", "short-1.zip", "short-4.zip", "short-20.zip",
			"short-21.zip", "good.zip", "noname.zip", "liar.zip", "far.zip", "s",
		};
		char p[512];
		size_t k;

		for (k = 0; k < sizeof made / sizeof made[0]; k++) {
			snprintf(p, sizeof p, "%s/%s", g_dir, made[k]);
			unlink(p);
		}
		if (rmdir(g_dir) != 0) fprintf(stderr, "bounds: cannot remove %s: %s\n", g_dir, strerror(errno));
	}
	printf("bounds: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
