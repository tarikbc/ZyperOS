/* A save state's file, apart from the core that made it and the card it is on.
 *
 * Version 2, written since 2026-11-12: the header, the core's bytes packed
 * with LZ4, then the achievement trailer. The header is version 1's with two
 * fields after it, so a reader of the core's name (ZyperOS's src/corestate.c)
 * finds it where it always was:
 *
 *     magic "DIATOMST"   8
 *     version            4   2 (1: no fields below, the core's bytes as they are)
 *     payload            4   the core's bytes, as serialize gave them
 *     core_name         64
 *     core_version      32
 *     codec              4   SF_RAW, the bytes as they are, or SF_LZ4, a block
 *     stored             4   the payload's bytes in this file
 *     <stored bytes>
 *     <trailer>              "DCHV", a length, achievement progress (save.c)
 *
 * Packed because a state is mostly zeros and the card is the slow half of a
 * save: a PSP state was 40 MB for every quit (PPSSPP pads its size up to the
 * next 8 MB), an N64 one 16 MB, 87% of it zeros. LZ4 rather than the zlib
 * Diatom already links, because it is the one that costs less than the
 * write it saves on a Cortex-A35: on one of this Mac's cores zlib's fastest
 * level packed a Dreamcast state's 27 MB in 271 ms and LZ4 in 23 ms, to 39%
 * and 46% of it, and LZ4 unpacked it in 4 ms to zlib's 38 (2026-11-12, real
 * states from a ZyperOS card).
 *
 * Plain C, no core, no port, no card: test/statefile_test.c holds it. */
#ifndef DIATOM_STATEFILE_H
#define DIATOM_STATEFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SF_HEADER   120u         /* version 2's header; version 1's was 112 */
#define SF_RAW      0u
#define SF_LZ4      1u

/* The achievement-progress trailer: "DCHV", a little-endian length, the
 * bytes. Longer than SF_PROGRESS_MAX is not progress and is not read. */
#define SF_PROGRESS_MAGIC "DCHV"
#define SF_PROGRESS_HDR   8u
#define SF_PROGRESS_MAX   (1u << 20)

typedef enum {
	SF_OK,
	SF_NOT_STATE,                /* no Diatom state of a version known here */
	SF_TRUNCATED,                /* shorter than its header says */
	SF_DAMAGED,                  /* the packed bytes do not unpack to the size */
	SF_NO_MEMORY,
} sf_error;

/* A state read back: the core's bytes, ready for unserialize, and the
 * trailer's achievement progress, or none. `payload` and `prog` point into
 * the file read, or into `own`, so the file outlives this. */
typedef struct {
	char           core_name[64];
	char           core_version[32];
	unsigned       version, codec;
	const uint8_t *payload;
	size_t         payload_n;
	size_t         stored;       /* the payload's bytes in the file */
	size_t         file_n;       /* the whole file's */
	const uint8_t *prog;         /* NULL when there is none */
	uint32_t       nprog;
	uint8_t       *own;          /* malloc'd: the unpacked payload */
} sf_state;

/* A version 2 header at `file` for `n` bytes of the core's laid out as they
 * are after it (SF_RAW): the file save.c builds, the core serializing into
 * file + SF_HEADER, the trailer after that. */
void sf_header(uint8_t *file, const char *core_name, const char *core_version, size_t n);

/* That file, packed: its payload as an LZ4 block when that makes the file
 * smaller, the trailer after it. Returns the file to write, its length in
 * *len: a new buffer, `file` freed, or `file` as it was (smaller unpacked,
 * or no memory for the packed copy, which is still a whole state). */
uint8_t *sf_pack(uint8_t *file, size_t *len);

/* A state file of either version read back into `st`. Anything but SF_OK
 * leaves nothing to free. */
sf_error sf_unpack(const uint8_t *file, size_t len, sf_state *st);
void     sf_state_free(sf_state *st);

const char *sf_error_text(sf_error e);

#endif
