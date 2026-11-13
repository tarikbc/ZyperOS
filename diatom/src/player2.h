/* SPDX-License-Identifier: MPL-2.0 */
/* NES player 2: the game's frames out, and player 2's buttons in.
 *
 * A second player joins from a browser, and ZyperOS's p2d serves the page.
 * The two cross as memory, one file mapped by both (p2share.h has the layout
 * and why): Diatom writes each frame into it and reads port 1 from it. Off
 * for every game until the launcher sends SETPLAYER2 on=1 while it runs, and
 * off again, the file gone, when the game ends, however it ends.
 *
 * Nothing here waits on p2d, because the Pixel must not drop a frame for a
 * guest: a frame is a copy into a slot, and the pad is a word read once a
 * frame, with no syscall and no lock. p2d bumps pad_beat at least every 100
 * ms while it runs. A beat that stands still for 500 ms is a p2d that died or
 * hung, and its last buttons are let go rather than held down for ever. */
#ifndef DIATOM_PLAYER2_H
#define DIATOM_PLAYER2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* On: the file made new, sized and mapped, its magic and version written
 * last. Off: its magic cleared, unmapped and removed. True when it is now as
 * asked; false only for an on that failed, which it logs. */
bool p2_set(bool on);
bool p2_on(void);

/* From the core's video callback, after Diatom's own copy: the frame into
 * the next slot as XRGB8888, fmt in libretro's numbering (0 0RGB1555,
 * 1 XRGB8888, 2 RGB565). Nothing while off, for a dupe (NULL) or for a frame
 * the GPU drew. */
void p2_frame(const void *data, unsigned w, unsigned h, size_t pitch, int fmt);

/* Player 2's RetroPad bits, the NES pad's only (P2S_PAD_MASK). 0 while off,
 * before p2d's first beat, and once its beat has stood still for 500 ms. */
uint32_t p2_pad(unsigned now_ms);

/* For a game that ends without Diatom's own end: a crash (a signal handler
 * may call it) or a core's exit(). The magic cleared and the file removed,
 * nothing else; the mapping goes with the process. */
void p2_abandon(void);

#endif /* DIATOM_PLAYER2_H */
