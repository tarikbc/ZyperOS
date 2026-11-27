/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CASSETTE_H
#define ZYPEROS_CASSETTE_H

/* MP4's Cassette look's drawing (DESIGN.md 11.64): a C-cassette at its real
 * proportions, 100.4 by 63.8 mm, 592 by 376 units - 74 and 47 grid steps,
 * the nearest pair of whole steps to its 1.5737 (1.5745). Its parts are in
 * units of that size, each one's millimetres beside it. The pixel tapes
 * (src/pixtape.h) are drawn from it on their grid of cells; the label's
 * words are placed by the look (src/nowplaying_looks.c). */

#define CAS_W 592
#define CAS_H 376

/* The label, the paper stuck on the shell: 87 x 35 mm from 6.8 and 5.4 */
#define CAS_LABEL_X0 40
#define CAS_LABEL_Y0 32
#define CAS_LABEL_X1 552
#define CAS_LABEL_Y1 240
/* The cover's stripe across it, over the window: 1.4 mm */
#define CAS_STRIPE_Y0 96
#define CAS_STRIPE_Y1 104
/* The side's letter in its box, the song beside it, its place at the
 * label's right: their row's middle, 11.5 mm down */
#define CAS_SIDE     40
#define CAS_ROW_MID  68
#define CAS_LABEL_PAD 16
/* The full-screen tape's label (my picks in the Tape Styles lab,
 * 2026-11-23): a small cover first, at the song's row, 52 across; the
 * side's letter after it; the song after that */
#define CAS_COVER_X0 52
#define CAS_COVER    52
#define CAS_SIDE_X1  116
/* Under the window, the artist and the album: their line's middle */
#define CAS_UNDER_MID 224
/* The window, a pill 54.3 x 16.3 mm, in the middle */
#define CAS_WIN_X0 136
#define CAS_WIN_Y0 112
#define CAS_WIN_X1 456
#define CAS_WIN_Y1 208
/* The reels: 42.5 mm apart, on the window's middle; the hub 21.5 mm
 * across, a full pack of a C60 or a C90 50.2 */
#define CAS_REEL_DX  125.3f
#define CAS_REEL_Y   160.0f
#define CAS_HUB_R    63.4f
#define CAS_PACK_R   148.0f
/* The tape's speed, 4.76 cm a second, in units */
#define CAS_TAPE_SPEED (47.6f * CAS_W / 100.4f)

#endif
