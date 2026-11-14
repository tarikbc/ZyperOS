/* SPDX-License-Identifier: GPL-3.0-only */
/* Decode a file of raw FLAC frames with src/flacdec.c and write the samples
 * to stdout, so they can be compared with the PCM the encoder was given.
 *
 *     flacdec-check <raw-frames-file> <frames> <channels> <be|le>
 *
 * Driven by tools/flacdec-check.py. Exit 1 means flac_decode_s16 said false.
 * Both buffers are allocated to the byte, so that an AddressSanitizer build
 * catches the decoder reading or writing even one byte past either end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/flacdec.h"

int main(int argc, char **argv)
{
	FILE *f;
	uint8_t *src, *out;
	size_t len = 0, cap = 4096, outlen, got;
	unsigned long long frames;
	unsigned long channels;
	char *e1, *e2;

	if (argc != 5 || (strcmp(argv[4], "be") && strcmp(argv[4], "le"))) {
		fprintf(stderr, "usage: flacdec-check <raw-frames-file> <frames> <channels> <be|le>\n");
		return 2;
	}
	frames = strtoull(argv[2], &e1, 10);
	channels = strtoul(argv[3], &e2, 10);
	if (*e1 || *e2 || !*argv[2] || !*argv[3] || frames > (1ull << 32)) {
		fprintf(stderr, "flacdec-check: bad frames or channels\n");
		return 2;
	}
	if (!(f = fopen(argv[1], "rb"))) {
		perror(argv[1]);
		return 2;
	}
	src = malloc(cap);
	while (src && (got = fread(src + len, 1, cap - len, f)) > 0) {
		len += got;
		if (len == cap)
			src = realloc(src, cap *= 2);
	}
	fclose(f);
	if (!src)
		return 2;
	/* Down to the byte: one past the file is then outside the allocation. */
	if (len) {
		uint8_t *exact = malloc(len);

		if (!exact)
			return 2;
		memcpy(exact, src, len);
		free(src);
		src = exact;
	}

	outlen = (size_t)frames * channels * 2;
	if (!(out = malloc(outlen ? outlen : 1)))
		return 2;
	if (!flac_decode_s16(src, len, (unsigned)channels, out, (size_t)frames,
	                     argv[4][0] == 'b'))
		return 1;
	fwrite(out, 1, outlen, stdout);
	free(out);
	free(src);
	return 0;
}
