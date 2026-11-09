/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Which input device px2sim takes for QEMU's keyboard.
 *
 * Decided by where the kernel put the device, not by anything the device
 * says about itself. A program that makes a keyboard through uinput (PortMaster's
 * gptokeyb, "Fake Keyboard") chooses its own name, bus, ids and phys, so any
 * of those can look like QEMU's. But the kernel puts every uinput device
 * under /sys/devices/virtual/, and QEMU's keyboards under their bus: virtio
 * (-device virtio-keyboard-device/-pci) or USB (-device usb-kbd). The real
 * path of /sys/class/input/eventN is what is checked.
 *
 * Header only, so tests/kbdsel-check.c builds it on the host.
 */
#ifndef PX2SIM_KBDSEL_H
#define PX2SIM_KBDSEL_H

#include <string.h>

/* SYSPATH: realpath() of /sys/class/input/eventN. HAS_LETTERS: the device
 * reports KEY_A (the Pixel 2's four own devices, and mice, do not). */
static inline int kbdsel_is_qemu_keyboard(const char *syspath, int has_letters)
{
	if (!syspath || !has_letters) return 0;
	/* uinput, and anything else with no bus under it */
	if (strstr(syspath, "/devices/virtual/")) return 0;
	return strstr(syspath, "/virtio") || strstr(syspath, "/usb");
}

#endif
