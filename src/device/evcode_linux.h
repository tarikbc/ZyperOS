/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_EVCODE_LINUX_H
#define ZYPEROS_EVCODE_LINUX_H

/* src/device/evcode.h's numbers held to Linux's own, in a device file that
 * includes <linux/input.h>: a code mistyped there does not compile. */
#include <linux/input.h>

#include "evcode.h"

#define EVC_SAME(n) _Static_assert(EVC_##n == n, #n " is Linux's")
EVC_SAME(KEY_VOLUMEDOWN); EVC_SAME(KEY_VOLUMEUP); EVC_SAME(KEY_POWER);
EVC_SAME(BTN_SOUTH); EVC_SAME(BTN_EAST); EVC_SAME(BTN_NORTH); EVC_SAME(BTN_WEST);
EVC_SAME(BTN_TL); EVC_SAME(BTN_TR); EVC_SAME(BTN_TL2); EVC_SAME(BTN_TR2);
EVC_SAME(BTN_SELECT); EVC_SAME(BTN_START); EVC_SAME(BTN_MODE);
EVC_SAME(BTN_THUMBL); EVC_SAME(BTN_THUMBR);
EVC_SAME(BTN_DPAD_UP); EVC_SAME(BTN_DPAD_DOWN); EVC_SAME(BTN_DPAD_LEFT); EVC_SAME(BTN_DPAD_RIGHT);
EVC_SAME(BTN_TRIGGER_HAPPY1);
EVC_SAME(ABS_X); EVC_SAME(ABS_Y); EVC_SAME(ABS_RX); EVC_SAME(ABS_RY);
#undef EVC_SAME

#endif
