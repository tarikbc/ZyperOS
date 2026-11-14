/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TDKEY_H
#define ZYPEROS_TDKEY_H

/* The keys of the handheld a pose can light (src/tourdevice.h), on their
 * own and with no SDL, so a pure part (src/ctltab.c, which says which keys a
 * row of the Controls tab is about) can name them as bits, 1u << TD_A. */
typedef enum { TD_DPAD, TD_A, TD_B, TD_X, TD_Y, TD_SELECT, TD_START, TD_MENU, TD_VOLUP,
               TD_VOLDN, TD_POWER, TD_L1, TD_R1, TD_L2, TD_R2, TD_KEYS } td_key;

/* How far the handheld tilts back, degrees, so its top shows with the
 * shoulders standing on it: for the shoulders, in the tutorial's lessons
 * that light them and on the Controls tab's rows that do (DESIGN.md 11.10,
 * 11.45). 25 shows a lit shoulder's front and top plainly at 640 x 480 and
 * the handheld still reads as standing. */
#define TD_PITCH_DEG 25.0f

#endif
