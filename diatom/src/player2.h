/* SPDX-License-Identifier: MPL-2.0 */
/* Guest players, NES player 2 first: the game's frames out, and the guests'
 * buttons in.
 *
 * A second player joins from a browser, and ZyperOS's p2d serves the page.
 * The two cross as memory, one file mapped by both (p2share.h has the layout
 * and why): Diatom writes each frame into it and reads the guests' ports
 * from it. Off for every game until the launcher sends SETPLAYER2 on=1 while
 * it runs, and off again, the file gone, when the game ends, however it ends.
 *
 * The launcher says what the system gives a guest, from its system table:
 * the RetroPad buttons it has (buttons=, the NES's 0x01FD when not said) and
 * how many play at once (players=, 2 when not said). The guests are ports 1
 * to players-1, and each reads only those buttons, so a guest never presses
 * one the game's own pad has not got, nor one of Diatom's hotkeys.
 *
 * Nothing here waits on p2d, because the Pixel must not drop a frame for a
 * guest: a frame is a copy into a slot, and the pads are words read once a
 * frame, with no syscall and no lock. p2d bumps pad_beat at least every 100
 * ms while it runs. A beat that stands still for 500 ms is a p2d that died or
 * hung, and every guest's last buttons are let go rather than held down for
 * ever. */
#ifndef DIATOM_PLAYER2_H
#define DIATOM_PLAYER2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p2share.h"

/* On: the file made new, sized and mapped, the system's buttons and players
 * in its header (as asked, by p2share.h's p2s_buttons and p2s_players: 0 for
 * "not said"), its magic and version written last. An on while on keeps
 * what it has. Off: its magic cleared, unmapped and removed; the buttons and
 * players are not read. True when it is now as asked; false only for an on
 * that failed, which it logs. */
bool p2_set(bool on, uint32_t buttons, long players);
bool p2_on(void);

/* While on, what the header says: the system's buttons and how many play */
uint32_t p2_buttons(void);
unsigned p2_players(void);

/* From the core's video callback, after Diatom's own copy: the frame into
 * the next slot as XRGB8888, fmt in libretro's numbering (0 0RGB1555,
 * 1 XRGB8888, 2 RGB565). Nothing while off, for a dupe (NULL) or for a frame
 * the GPU drew. */
void p2_frame(const void *data, unsigned w, unsigned h, size_t pitch, int fmt);

/* Every guest's RetroPad bits, pads[k] port k+1's, each within the system's
 * buttons. 0 for a port no guest may play (players-1 and up), and every one
 * 0 while off, before p2d's first beat, and once its beat has stood still
 * for 500 ms. */
void p2_pads(unsigned now_ms, uint32_t pads[P2S_GUESTS]);

/* For a game that ends without Diatom's own end: a crash (a signal handler
 * may call it) or a core's exit(). The magic cleared and the file removed,
 * nothing else; the mapping goes with the process. */
void p2_abandon(void);

#endif /* DIATOM_PLAYER2_H */
