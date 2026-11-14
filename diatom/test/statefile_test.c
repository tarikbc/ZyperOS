/* make check-statefile: a save state's file (src/statefile.h).
 *
 * Packed and read back byte for byte, the achievement trailer with it; left
 * as it is when packing would not make it smaller; a version 1 state, as
 * every Diatom before 2026-11-12 wrote it, read as it was; and a file cut
 * short, damaged or not a state refused, never handed on. A packed block says
 * how long it is, so it is input like a zip's: this runs under
 * AddressSanitizer where the compiler has it, and damages a packed state a
 * few thousand ways to show no read or write goes past a buffer.
 *
 * Paths given are real states (make check-statefile STATEFILE_SAMPLES=...):
 * each is read back, packed again as Diatom packs it, and unpacked, with its
 * sizes and times. They are this machine's times, not a handheld's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "statefile.h"

static int failures;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); failures++; } } while (0)

static uint32_t rng = 12345;
static uint8_t rnd(void) { rng = rng * 1103515245u + 12345u; return (uint8_t)(rng >> 16); }

/* A core's bytes that look like one's: zeros, a stretch of noise, a table
 * that repeats */
static void fill(uint8_t *p, size_t n)
{
	size_t i;

	memset(p, 0, n);
	for (i = 0; i < n / 8; i++) p[n / 4 + i] = rnd();
	for (i = 0; i < n / 4; i++) p[n / 2 + i] = (uint8_t)(i % 61) * 3;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A file as save.c's state_build lays it out before packing: the header,
 * the core's `n` bytes, and `nprog` bytes of progress in a trailer */
static uint8_t *raw_file(const uint8_t *payload, size_t n, const uint8_t *prog, uint32_t nprog,
                         size_t *len)
{
	uint8_t *f = malloc(SF_HEADER + n + (nprog ? SF_PROGRESS_HDR + nprog : 0));

	sf_header(f, "Test Core", "1.2.3", n);
	memcpy(f + SF_HEADER, payload, n);
	*len = SF_HEADER + n;
	if (nprog) {
		memcpy(f + *len, SF_PROGRESS_MAGIC, 4);
		put32(f + *len + 4, nprog);
		memcpy(f + *len + SF_PROGRESS_HDR, prog, nprog);
		*len += SF_PROGRESS_HDR + nprog;
	}
	return f;
}

/* A version 1 file, as Diatom wrote every state until 2026-11-12: 112 bytes
 * of header, the core's bytes as they are, then the trailer */
static uint8_t *v1_file(const uint8_t *payload, size_t n, const uint8_t *prog, uint32_t nprog,
                        size_t *len)
{
	uint8_t *f = calloc(1, 112 + n + SF_PROGRESS_HDR + nprog);

	memcpy(f, "DIATOMST", 8);
	put32(f + 8, 1);
	put32(f + 12, (uint32_t)n);
	snprintf((char *)f + 16, 64, "%s", "Old Core");
	snprintf((char *)f + 80, 32, "%s", "0.9");
	memcpy(f + 112, payload, n);
	*len = 112 + n;
	if (nprog) {
		memcpy(f + *len, SF_PROGRESS_MAGIC, 4);
		put32(f + *len + 4, nprog);
		memcpy(f + *len + SF_PROGRESS_HDR, prog, nprog);
		*len += SF_PROGRESS_HDR + nprog;
	}
	return f;
}

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void sample(const char *path)
{
	FILE *f = fopen(path, "rb");
	const char *base = strrchr(path, '/');
	uint8_t *file, *again;
	size_t len, alen;
	long n;
	sf_state st, back;
	sf_error e;
	double t0, tp, tu;

	base = base ? base + 1 : path;
	if (!f) { printf("  FAIL %s: cannot open\n", base); failures++; return; }
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	file = malloc((size_t)n);
	len = fread(file, 1, (size_t)n, f);
	fclose(f);
	if ((e = sf_unpack(file, len, &st)) != SF_OK) {
		printf("  FAIL %s: %s\n", base, sf_error_text(e));
		failures++;
		free(file);
		return;
	}
	again = raw_file(st.payload, st.payload_n, st.prog, st.nprog, &alen);
	t0 = now_ms();
	again = sf_pack(again, &alen);
	tp = now_ms() - t0;
	t0 = now_ms();
	e = sf_unpack(again, alen, &back);
	tu = now_ms() - t0;
	if (e != SF_OK || back.payload_n != st.payload_n ||
	    memcmp(back.payload, st.payload, st.payload_n) != 0 || back.nprog != st.nprog) {
		printf("  FAIL %s: does not come back as it was\n", base);
		failures++;
	} else {
		printf("  ok   %-48.48s v%u %9zu -> %9zu bytes (%5.1f%%), pack %6.1f ms, unpack %5.1f ms\n",
		       base, st.version, len, alen, 100.0 * alen / len, tp, tu);
	}
	sf_state_free(&back);
	sf_state_free(&st);
	free(again);
	free(file);
}

int main(int argc, char **argv)
{
	enum { N = 1 << 20 };
	uint8_t *payload = malloc(N), prog[300], *f, *g;
	size_t len, glen, i;
	sf_state st;
	sf_error e;
	int damaged = 0, fine = 0;

	printf("statefile: a save state's file\n");
	fill(payload, N);
	for (i = 0; i < sizeof prog; i++) prog[i] = (uint8_t)(i * 7);

	/* Packed, and back */
	f = raw_file(payload, N, prog, sizeof prog, &len);
	CHECK(get32(f + 8) == 2 && get32(f + 12) == N && !strcmp((char *)f + 16, "Test Core"),
	      "version 2's header has version 1's fields where they were (src/corestate.c reads the name at 16)");
	f = sf_pack(f, &len);
	CHECK(get32(f + 112) == SF_LZ4 && len < N / 2, "a state that packs is written packed");
	CHECK(len == SF_HEADER + get32(f + 116) + SF_PROGRESS_HDR + sizeof prog,
	      "its length: the header, the packed bytes, the trailer");
	e = sf_unpack(f, len, &st);
	CHECK(e == SF_OK && st.version == 2 && st.codec == SF_LZ4 && st.payload_n == N &&
	      !memcmp(st.payload, payload, N), "and comes back byte for byte");
	CHECK(!strcmp(st.core_name, "Test Core") && !strcmp(st.core_version, "1.2.3"),
	      "with the core that wrote it");
	CHECK(st.prog && st.nprog == sizeof prog && !memcmp(st.prog, prog, sizeof prog),
	      "and its achievement progress");
	sf_state_free(&st);

	/* Cut short, anywhere: refused, or a state without its trailer */
	for (glen = 0; glen < len; glen += 1 + glen / 3) {
		e = sf_unpack(f, glen, &st);
		if (e == SF_OK) {
			if (glen < SF_HEADER + get32(f + 116) || st.prog || memcmp(st.payload, payload, N))
				damaged++;
			sf_state_free(&st);
		}
	}
	CHECK(!damaged, "cut short: refused, or whole with no trailer, never part of a state");

	/* Damaged packed bytes: refused or unpacked to the size, never past a
	 * buffer (AddressSanitizer is the judge of that) */
	damaged = 0;
	g = malloc(len);
	for (i = 0; i < 3000; i++) {
		size_t k, at;

		memcpy(g, f, len);
		for (k = 0; k < 1 + i % 4; k++) {
			at = SF_HEADER + (rnd() | (size_t)rnd() << 8 | (size_t)rnd() << 16) % get32(f + 116);
			g[at] = rnd();
		}
		e = sf_unpack(g, len, &st);
		if (e == SF_DAMAGED) damaged++;
		else if (e == SF_OK && st.payload_n == N) fine++;
		sf_state_free(&st);
	}
	printf("       (3000 damaged copies: %d refused, %d unpacked to the size)\n", damaged, fine);
	CHECK(damaged + fine == 3000, "damaged: refused, or unpacked to the size the header says");

	/* A header that says another size than the block holds */
	memcpy(g, f, len);
	put32(g + 12, N - 1);
	CHECK(sf_unpack(g, len, &st) == SF_DAMAGED, "a block longer than the header says: damaged");
	put32(g + 12, N + 5);
	CHECK(sf_unpack(g, len, &st) == SF_DAMAGED, "a block shorter than it says: damaged");
	memcpy(g, f, len);
	put32(g + 116, (uint32_t)(len - SF_HEADER + 1));
	CHECK(sf_unpack(g, len, &st) == SF_TRUNCATED, "stored past the file's end: truncated");
	free(g);
	free(f);

	/* Noise does not pack: written as it is */
	for (i = 0; i < 65536; i++) payload[i] = rnd();
	f = raw_file(payload, 65536, prog, 9, &len);
	g = f;
	f = sf_pack(f, &len);
	CHECK(f == g && get32(f + 112) == SF_RAW && len == SF_HEADER + 65536 + SF_PROGRESS_HDR + 9,
	      "a state that would not pack is written as it is");
	e = sf_unpack(f, len, &st);
	CHECK(e == SF_OK && st.codec == SF_RAW && st.payload == f + SF_HEADER &&
	      !memcmp(st.payload, payload, 65536) && st.nprog == 9, "and read back so");
	sf_state_free(&st);
	put32(f + 116, 65535);
	CHECK(sf_unpack(f, len, &st) == SF_DAMAGED, "unpacked, its two sizes must agree");
	free(f);

	/* Version 1: every state on a card before this */
	fill(payload, N);
	f = v1_file(payload, N, prog, sizeof prog, &len);
	e = sf_unpack(f, len, &st);
	CHECK(e == SF_OK && st.version == 1 && st.payload == f + 112 && st.payload_n == N &&
	      !memcmp(st.payload, payload, N) && !strcmp(st.core_name, "Old Core") &&
	      !strcmp(st.core_version, "0.9"), "a version 1 state is read as it was written");
	CHECK(st.prog && st.nprog == sizeof prog && !memcmp(st.prog, prog, sizeof prog),
	      "with its achievement progress");
	sf_state_free(&st);
	CHECK(sf_unpack(f, 112 + N, &st) == SF_OK && !st.prog, "one with no trailer has none");
	sf_state_free(&st);
	CHECK(sf_unpack(f, 112 + N - 1, &st) == SF_TRUNCATED, "one cut short is truncated");
	put32(f + 112 + N + 4, SF_PROGRESS_MAX + 1);
	CHECK(sf_unpack(f, len, &st) == SF_OK && !st.prog, "a trailer too long is no progress");
	sf_state_free(&st);
	put32(f + 112 + N + 4, sizeof prog + 1);
	CHECK(sf_unpack(f, len, &st) == SF_OK && !st.prog, "nor is one longer than the file");
	sf_state_free(&st);
	CHECK(sf_unpack(f, 111, &st) == SF_NOT_STATE, "shorter than a header: not a state");
	put32(f + 8, 3);
	CHECK(sf_unpack(f, len, &st) == SF_NOT_STATE, "a version not known here: not a state");
	put32(f + 8, 2);
	CHECK(sf_unpack(f, 119, &st) == SF_TRUNCATED, "version 2 shorter than its header: truncated");
	put32(f + 112, 7);
	CHECK(sf_unpack(f, len, &st) == SF_NOT_STATE, "a codec not known here: not a state");
	memcpy(f, "RASTATE1", 8);
	CHECK(sf_unpack(f, len, &st) == SF_NOT_STATE, "another magic: not a state");
	free(f);
	free(payload);

	for (i = 1; i < (size_t)argc; i++) sample(argv[i]);

	printf(failures ? "statefile: %d failed\n" : "ok: states packed, read back, old ones too\n",
	       failures);
	return failures ? 1 : 0;
}
