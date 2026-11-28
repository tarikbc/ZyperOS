/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_EVCODE_H
#define ZYPEROS_EVCODE_H

/* The kernel's input codes a handheld's profile maps (dev_input,
 * src/device/profile.h), as Linux's input-event-codes.h numbers them, here
 * with no Linux header so the profiles stay plain data that the host build
 * and the checks link on a Mac too. A device file, which has Linux's, holds
 * every one to it (src/device/evcode_linux.h). */

/* EV_KEY */
#define EVC_KEY_VOLUMEDOWN     114
#define EVC_KEY_VOLUMEUP       115
#define EVC_KEY_POWER          116
#define EVC_BTN_SOUTH          0x130
#define EVC_BTN_EAST           0x131
#define EVC_BTN_NORTH          0x133
#define EVC_BTN_WEST           0x134
#define EVC_BTN_TL             0x136
#define EVC_BTN_TR             0x137
#define EVC_BTN_TL2            0x138
#define EVC_BTN_TR2            0x139
#define EVC_BTN_SELECT         0x13a
#define EVC_BTN_START          0x13b
#define EVC_BTN_MODE           0x13c
#define EVC_BTN_THUMBL         0x13d
#define EVC_BTN_THUMBR         0x13e
#define EVC_BTN_DPAD_UP        0x220
#define EVC_BTN_DPAD_DOWN      0x221
#define EVC_BTN_DPAD_LEFT      0x222
#define EVC_BTN_DPAD_RIGHT     0x223
#define EVC_BTN_TRIGGER_HAPPY1 0x2c0

/* EV_ABS */
#define EVC_ABS_X              0x00
#define EVC_ABS_Y              0x01
#define EVC_ABS_RX             0x03
#define EVC_ABS_RY             0x04

#endif
