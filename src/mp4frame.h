/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_MP4FRAME_H
#define ZYPEROS_MP4FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* MP4's pictures, on their way from the Muse daemon to the launcher.
 *
 * The daemon decodes a file's video next to its sound and knows when each
 * picture is due, because it is the one holding the sound's clock. The
 * launcher owns the screen. So the pictures cross between the two processes
 * as plain memory: one file in /tmp (RAM on the device), mapped by both, the
 * daemon writing and the launcher reading. No socket line per frame, and no
 * copy the launcher has to wait for: it hands the newest picture straight to
 * the GPU (SDL_UpdateYUVTexture), which turns YUV into the screen's colors.
 *
 * Three slots, so the daemon never writes the one the launcher may be reading:
 * it writes the slot after the newest, and a slot comes round again only two
 * pictures later - 80 ms at 25 fps, against the 1 to 4 ms a read takes. Each
 * slot carries a sequence number that is odd while it is being written (a
 * seqlock), and the reader checks it on both sides, so even a reader that
 * stalled for a whole frame cannot keep a half-written picture.
 *
 * A picture is 8-bit YUV 4:2:0 in three planes packed one after another, each
 * row exactly as wide as the plane: Y is w x h, U and V (w+1)/2 x (h+1)/2.
 * Anything the decoder hands over in another layout is converted to this by
 * the daemon; anything bigger than 1920x1088 is scaled down to fit it (and
 * would not play at full speed on this CPU anyway; see src/muse/vid.c).
 *
 * Shared by src/muse/vid.c (the writer) and src/mp4video.c (the reader), and
 * by tools/mp4frame-check.c, which runs both against each other. */

#define MPF_PATH      "/tmp/muse-video"
#define MPF_MAGIC     0x4d503446u           /* "MP4F" */
#define MPF_VERSION   1u
#define MPF_SLOTS     3
#define MPF_MAX_W     1920
#define MPF_MAX_H     1088
#define MPF_HEAD      4096u
#define MPF_SLOT_BYTES ((uint32_t)MPF_MAX_W * MPF_MAX_H * 3u / 2u)
#define MPF_SIZE      (MPF_HEAD + (uint32_t)MPF_SLOTS * MPF_SLOT_BYTES)

/* How a picture's YUV maps to color: the two broadcast matrices at studio
 * range (16-235), and full range (0-255, "yuvj"). SDL's names for the same. */
enum { MPF_BT601, MPF_BT709, MPF_FULL };

typedef struct {
	uint32_t seq;              /* odd while the slot is being written */
	uint32_t w, h;             /* the luma plane; see above for chroma */
	uint32_t color;            /* MPF_BT601 ... */
	uint32_t serial;           /* which picture this is, counted from 1 */
	uint32_t file;             /* the file it came from: mpf_head.file then */
	double   pts;              /* its time in the file, in seconds */
} mpf_slot;

typedef struct {
	uint32_t magic, version;
	uint32_t latest;           /* the slot holding the newest whole picture */
	uint32_t serial;           /* pictures published so far; 0 = none yet */
	uint32_t file;             /* bumped for every file opened */
	uint32_t active;           /* a file with a picture is open */
	uint32_t sar_num, sar_den; /* the pixels' own shape, 1:1 for most */
	mpf_slot slot[MPF_SLOTS];
} mpf_head;

static inline uint8_t *mpf_slot_data(void *base, int s)
{
	return (uint8_t *)base + MPF_HEAD + (uint32_t)s * MPF_SLOT_BYTES;
}

static inline uint32_t mpf_chroma(uint32_t n) { return (n + 1) / 2; }

static inline uint32_t mpf_bytes(uint32_t w, uint32_t h)
{
	return w * h + 2 * mpf_chroma(w) * mpf_chroma(h);
}

/* ---- the writer --------------------------------------------------------- */

/* The slot to write next, and the start of writing it: its sequence odd. */
static inline int mpf_begin(mpf_head *h)
{
	int s = (int)((__atomic_load_n(&h->latest, __ATOMIC_RELAXED) + 1) % MPF_SLOTS);

	__atomic_store_n(&h->slot[s].seq, h->slot[s].seq | 1u, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	return s;
}

/* The slot written: its sequence even again, and it the newest. */
static inline void mpf_publish(mpf_head *h, int s)
{
	__atomic_store_n(&h->slot[s].seq, (h->slot[s].seq | 1u) + 1u, __ATOMIC_RELEASE);
	__atomic_store_n(&h->latest, (uint32_t)s, __ATOMIC_RELEASE);
	__atomic_store_n(&h->serial, h->slot[s].serial, __ATOMIC_RELEASE);
}

/* ---- the reader --------------------------------------------------------- */

/* The newest picture's slot if it is newer than `seen` and whole right now,
 * with its sequence in `*seq` for mpf_still_good; -1 otherwise. */
static inline int mpf_peek(const mpf_head *h, uint32_t seen, uint32_t *seq)
{
	uint32_t serial = __atomic_load_n(&h->serial, __ATOMIC_ACQUIRE);
	int s;

	if (h->magic != MPF_MAGIC || h->version != MPF_VERSION) return -1;
	if (serial == 0 || serial == seen) return -1;
	s = (int)(__atomic_load_n(&h->latest, __ATOMIC_ACQUIRE) % MPF_SLOTS);
	*seq = __atomic_load_n(&h->slot[s].seq, __ATOMIC_ACQUIRE);
	if (*seq & 1u) return -1;
	if (h->slot[s].w == 0 || h->slot[s].h == 0 ||
	    h->slot[s].w > MPF_MAX_W || h->slot[s].h > MPF_MAX_H) return -1;
	return s;
}

/* After reading slot `s`: whether it was left alone the whole time. */
static inline bool mpf_still_good(const mpf_head *h, int s, uint32_t seq)
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	return __atomic_load_n(&h->slot[s].seq, __ATOMIC_RELAXED) == seq;
}

#endif
