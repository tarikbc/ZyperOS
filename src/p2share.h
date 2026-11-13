/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
#ifndef ZYPEROS_P2SHARE_H
#define ZYPEROS_P2SHARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Guest players (NES player 2 first), between Diatom and the p2d daemon.
 *
 * A second player joins from a browser. p2d serves the page, so it needs the
 * game's picture, and Diatom needs the guest's buttons. The two cross
 * between the processes as plain memory: one file in /tmp (RAM on the
 * device), mapped by both. Diatom writes the frames and reads the pad words;
 * p2d reads the frames and writes the pad words. No socket line per frame,
 * and nothing for Diatom to wait for: the Pixel must never drop a frame for
 * a guest.
 *
 * What a guest gets is the system's: the launcher's system table says which
 * RetroPad buttons it has and how many play at once, SETPLAYER2 tells Diatom,
 * and Diatom writes them here (buttons, players) for p2d, which tells the
 * page. The NES is 0x01FD and 2. The Pixel's own player is port 0; the
 * guests are ports 1 to players-1, each with its own pad word (pad[k] is port
 * k+1's) and its bit in guests. One beat for all: it is p2d's, not a guest's.
 *
 * Three slots, as in src/mp4frame.h: Diatom writes the slot after the newest,
 * so it never writes the one p2d may be reading, and each slot's sequence
 * number is odd while it is being written (a seqlock), so even a reader that
 * stalled for a whole frame cannot keep a half-written picture.
 *
 * A picture is XRGB8888, each row exactly w x 4 bytes. Diatom converts what
 * the core hands over (libretro's 0RGB1555, XRGB8888 or RGB565) as it copies,
 * so p2d never has to know the core's format.
 *
 * Diatom writes magic and version last, once the rest of the file is set
 * (players and buttons too), so a reader that sees them sees a file it can
 * use.
 *
 * Shared by Diatom (diatom/src/p2share.h is a copy of this file, which is
 * where it is changed), by p2d (src/p2d/main.c) and its feed
 * (tools/p2d-feed.c), and by tools/p2share-check.c. Either license, for the
 * same reason as src/osdbar.c. */

#define P2S_PATH       "/tmp/zyperos-p2"
#define P2S_MAGIC      0x50325348u          /* "P2SH" */
#define P2S_VERSION    1u
#define P2S_SLOTS      3
#define P2S_MAX_W      512
#define P2S_MAX_H      480
#define P2S_HEAD       4096u
#define P2S_SLOT_BYTES ((uint32_t)P2S_MAX_W * P2S_MAX_H * 4u)   /* XRGB8888 */
#define P2S_SIZE       (P2S_HEAD + (uint32_t)P2S_SLOTS * P2S_SLOT_BYTES)
#define P2S_GUESTS      3                   /* ports 1 to 3; port 0 is the Pixel's player */
#define P2S_PLAYERS_MAX (P2S_GUESTS + 1)
/* RetroPad bits, libretro's ids. The NES's are the default, and no system
 * gets more than these twelve: never L2, R2, L3 or R3, which no guest's page
 * has. */
#define P2S_PAD_MASK    0x01FDu             /* B SELECT START UP DOWN LEFT RIGHT A */
#define P2S_PAD_CEILING 0x0FFFu             /* B Y SELECT START UP DOWN LEFT RIGHT A X L R */

typedef struct {
	uint32_t seq;              /* odd while the slot is being written */
	uint32_t w, h;
	uint32_t frame;            /* which picture this is, counted from 1 */
} p2s_slot;

typedef struct {
	uint32_t magic, version;
	uint32_t latest;           /* Diatom: the slot with the newest whole frame */
	uint32_t frame;            /* Diatom: frames published, 0 = none yet */
	uint32_t players;          /* Diatom: 2 to P2S_PLAYERS_MAX, before the magic */
	uint32_t buttons;          /* Diatom: the system's RetroPad bits, before the magic */
	uint32_t pad[P2S_GUESTS];  /* p2d: port k+1's RetroPad bits, within buttons */
	uint32_t pad_beat;         /* p2d: bumped at least every 100 ms while it runs */
	uint32_t guests;           /* p2d: bit k set while a browser plays port k+1 */
	p2s_slot slot[P2S_SLOTS];
} p2s_head;

/* What SETPLAYER2 asked for, as the share holds it: the buttons within the
 * ceiling, the NES's when none is left (or none was asked); 2 to
 * P2S_PLAYERS_MAX players, 2 for anything else. Diatom applies them to what
 * the launcher sends, and p2d again to what it reads, since it sizes its
 * tables by players. */
static inline uint32_t p2s_buttons(uint32_t asked)
{
	asked &= P2S_PAD_CEILING;
	return asked ? asked : P2S_PAD_MASK;
}

static inline uint32_t p2s_players(long asked)
{
	return asked >= 2 && asked <= P2S_PLAYERS_MAX ? (uint32_t)asked : 2u;
}

static inline uint8_t *p2s_slot_data(void *base, int s)
{
	return (uint8_t *)base + P2S_HEAD + (uint32_t)s * P2S_SLOT_BYTES;
}

/* ---- the writer (Diatom) ------------------------------------------------ */

/* libretro's numbering: 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565. A channel of
 * n bits widens by repeating its top bits, so full is full: 0xF800 is exactly
 * 0x00FF0000. */
static inline uint32_t p2s_from1555(uint16_t v)
{
	uint32_t r = (v >> 10) & 31u, g = (v >> 5) & 31u, b = v & 31u;

	return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

static inline uint32_t p2s_from565(uint16_t v)
{
	uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;

	return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

/* Diatom: copy a w x h frame (fmt: 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565,
 * libretro's numbering) into the slot after `latest` as XRGB8888, seqlocked.
 * A frame the slots cannot hold, or an empty one, is not written at all. */
static inline void p2s_write(void *base, const void *px, unsigned w, unsigned h,
                             size_t pitch, int fmt)
{
	p2s_head *hd = base;
	uint8_t *dst;
	uint32_t seq;
	int s;

	if (w == 0 || h == 0 || w > P2S_MAX_W || h > P2S_MAX_H) return;
	s = (int)((__atomic_load_n(&hd->latest, __ATOMIC_RELAXED) + 1) % P2S_SLOTS);
	dst = p2s_slot_data(base, s);

	seq = hd->slot[s].seq | 1u;
	__atomic_store_n(&hd->slot[s].seq, seq, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);

	for (unsigned y = 0; y < h; y++) {
		const uint8_t *row = (const uint8_t *)px + (size_t)y * pitch;
		uint32_t *out = (uint32_t *)(void *)(dst + (size_t)y * w * 4u);

		if (fmt == 1) {
			memcpy(out, row, (size_t)w * 4u);
		} else {
			for (unsigned x = 0; x < w; x++) {
				uint16_t v;

				memcpy(&v, row + (size_t)x * 2u, 2);
				out[x] = fmt == 2 ? p2s_from565(v) : p2s_from1555(v);
			}
		}
	}
	hd->slot[s].w = w;
	hd->slot[s].h = h;
	hd->slot[s].frame = hd->frame + 1u;

	__atomic_store_n(&hd->slot[s].seq, seq + 1u, __ATOMIC_RELEASE);
	__atomic_store_n(&hd->latest, (uint32_t)s, __ATOMIC_RELEASE);
	__atomic_store_n(&hd->frame, hd->slot[s].frame, __ATOMIC_RELEASE);
}

/* ---- the reader (p2d) --------------------------------------------------- */

/* p2d: the newest whole frame into dst (w*h*4 bytes at most cap); false when
 * none, when torn twice in a row, or when it does not fit */
static inline bool p2s_read(void *base, uint8_t *dst, size_t cap,
                            unsigned *w, unsigned *h, uint32_t *frame)
{
	const p2s_head *hd = base;

	if (hd->magic != P2S_MAGIC || hd->version != P2S_VERSION) return false;
	if (__atomic_load_n(&hd->frame, __ATOMIC_ACQUIRE) == 0) return false;
	for (int attempt = 0; attempt < 2; attempt++) {
		int s = (int)(__atomic_load_n(&hd->latest, __ATOMIC_ACQUIRE) % P2S_SLOTS);
		uint32_t seq = __atomic_load_n(&hd->slot[s].seq, __ATOMIC_ACQUIRE);
		uint32_t sw, sh, sf;
		size_t bytes;

		if (seq & 1u) continue;
		sw = hd->slot[s].w;
		sh = hd->slot[s].h;
		sf = hd->slot[s].frame;
		if (sw == 0 || sh == 0 || sw > P2S_MAX_W || sh > P2S_MAX_H) return false;
		bytes = (size_t)sw * sh * 4u;
		if (bytes > cap) return false;
		memcpy(dst, p2s_slot_data(base, s), bytes);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (__atomic_load_n(&hd->slot[s].seq, __ATOMIC_RELAXED) != seq) continue;
		*w = sw;
		*h = sh;
		*frame = sf;
		return true;
	}
	return false;
}

#endif
