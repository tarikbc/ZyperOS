/* See statefile.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"
#include "statefile.h"

#define MAGIC "DIATOMST"

/* Fixed-width and packed by construction: 8 + 4 + 4 + 64 + 32 is version 1's
 * 112, and the two fields after it make 120, with no padding on any target.
 * Native byte order, as version 1 was written: little-endian on every device
 * Diatom runs on. */
struct header {
	char     magic[8];
	uint32_t version;
	uint32_t payload;
	char     core_name[64];
	char     core_version[32];
	uint32_t codec;              /* version 2 on */
	uint32_t stored;
};
#define HEADER_V1 112u
_Static_assert(sizeof(struct header) == SF_HEADER, "the state header has padding");

void sf_header(uint8_t *file, const char *core_name, const char *core_version, size_t n)
{
	struct header h;

	memset(&h, 0, sizeof h);
	memcpy(h.magic, MAGIC, sizeof h.magic);
	h.version = 2;
	h.payload = (uint32_t)n;
	snprintf(h.core_name, sizeof h.core_name, "%s", core_name ? core_name : "?");
	snprintf(h.core_version, sizeof h.core_version, "%s", core_version ? core_version : "?");
	h.codec = SF_RAW;
	h.stored = (uint32_t)n;
	memcpy(file, &h, sizeof h);
}

uint8_t *sf_pack(uint8_t *file, size_t *len)
{
	struct header h;
	size_t   n, tail, bound;
	uint8_t *out;
	int      packed;

	memcpy(&h, file, sizeof h);
	n = h.payload;
	if (h.codec != SF_RAW || *len < SF_HEADER + n || n == 0 || n > LZ4_MAX_INPUT_SIZE)
		return file;
	tail = *len - SF_HEADER - n;
	/* The bound is what a block can take at worst, a little over n; only
	 * the pages the packed bytes reach are ever touched */
	bound = (size_t)LZ4_compressBound((int)n);
	if (!(out = malloc(SF_HEADER + bound + tail))) return file;
	packed = LZ4_compress_default((const char *)file + SF_HEADER, (char *)out + SF_HEADER,
	                              (int)n, (int)bound);
	if (packed <= 0 || (size_t)packed >= n) {
		free(out);
		return file;
	}
	h.codec = SF_LZ4;
	h.stored = (uint32_t)packed;
	memcpy(out, &h, sizeof h);
	memcpy(out + SF_HEADER + packed, file + SF_HEADER + n, tail);
	free(file);
	*len = SF_HEADER + (size_t)packed + tail;
	return out;
}

sf_error sf_unpack(const uint8_t *file, size_t len, sf_state *st)
{
	struct header h;
	size_t   at, left;
	const uint8_t *t;

	memset(st, 0, sizeof *st);
	if (len < HEADER_V1) return SF_NOT_STATE;
	memset(&h, 0, sizeof h);
	memcpy(&h, file, HEADER_V1);
	if (memcmp(h.magic, MAGIC, sizeof h.magic) != 0) return SF_NOT_STATE;
	if (h.version == 1) {
		/* The core's bytes as they are, the trailer after them */
		at = HEADER_V1;
		h.codec = SF_RAW;
		h.stored = h.payload;
	} else if (h.version == 2) {
		if (len < SF_HEADER) return SF_TRUNCATED;
		memcpy(&h, file, sizeof h);
		at = SF_HEADER;
		if (h.codec != SF_RAW && h.codec != SF_LZ4) return SF_NOT_STATE;
		if (h.codec == SF_RAW && h.stored != h.payload) return SF_DAMAGED;
	} else {
		return SF_NOT_STATE;
	}
	if (len - at < h.stored) return SF_TRUNCATED;

	memcpy(st->core_name, h.core_name, sizeof st->core_name);
	st->core_name[sizeof st->core_name - 1] = '\0';
	memcpy(st->core_version, h.core_version, sizeof st->core_version);
	st->core_version[sizeof st->core_version - 1] = '\0';
	st->version = h.version;
	st->codec = h.codec;
	st->payload_n = h.payload;
	st->stored = h.stored;
	st->file_n = len;

	if (h.codec == SF_LZ4) {
		/* One byte more than the payload, so a block that holds more than
		 * the header says shows as longer rather than as a fit */
		if (h.payload == 0 || h.stored > (uint32_t)LZ4_MAX_INPUT_SIZE ||
		    h.payload >= (uint32_t)LZ4_MAX_INPUT_SIZE)
			return SF_DAMAGED;
		if (!(st->own = malloc((size_t)h.payload + 1))) return SF_NO_MEMORY;
		if (LZ4_decompress_safe((const char *)file + at, (char *)st->own, (int)h.stored,
		                        (int)h.payload + 1) != (int)h.payload) {
			sf_state_free(st);
			return SF_DAMAGED;
		}
		st->payload = st->own;
	} else {
		st->payload = file + at;
	}

	/* The achievement trailer, when there is one whole */
	t = file + at + h.stored;
	left = len - at - h.stored;
	if (left >= SF_PROGRESS_HDR && !memcmp(t, SF_PROGRESS_MAGIC, 4)) {
		uint32_t k = (uint32_t)t[4] | ((uint32_t)t[5] << 8) |
		             ((uint32_t)t[6] << 16) | ((uint32_t)t[7] << 24);

		if (k && k <= SF_PROGRESS_MAX && left - SF_PROGRESS_HDR >= k) {
			st->prog = t + SF_PROGRESS_HDR;
			st->nprog = k;
		}
	}
	return SF_OK;
}

void sf_state_free(sf_state *st)
{
	free(st->own);
	memset(st, 0, sizeof *st);
}

const char *sf_error_text(sf_error e)
{
	switch (e) {
	case SF_OK:        return "ok";
	case SF_NOT_STATE: return "is not a Diatom state";
	case SF_TRUNCATED: return "is truncated";
	case SF_DAMAGED:   return "is damaged";
	case SF_NO_MEMORY: return "is more than there is memory for";
	}
	return "?";
}
