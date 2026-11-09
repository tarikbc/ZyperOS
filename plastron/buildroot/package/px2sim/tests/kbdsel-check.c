/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host check for kbdsel.h: which input devices px2sim takes. */
#include <stdio.h>
#include "../src/kbdsel.h"

static int fails;
#define T(path, letters, want) do { \
	int got = kbdsel_is_qemu_keyboard((path), (letters)); \
	if (!!got != (want)) { fails++; printf("FAIL line %d: %s letters=%d -> %d\n", __LINE__, (path) ? (path) : "(null)", (letters), got); } \
} while (0)

int main(void)
{
	/* QEMU's virtio keyboard, MMIO and PCI */
	T("/sys/devices/platform/a003e00.virtio_mmio/virtio31/input/input2/event4", 1, 1);
	T("/sys/devices/platform/40000000.pcie/pci0000:00/0000:00:03.0/virtio1/input/input3/event4", 1, 1);
	/* QEMU's usb-kbd */
	T("/sys/devices/platform/xhci/usb1/1-1/1-1:1.0/0003:0627:0001.0001/input/input5/event5", 1, 1);
	/* uinput: gptokeyb's Fake Keyboard, whatever it calls itself */
	T("/sys/devices/virtual/input/input7/event7", 1, 0);
	/* QEMU's mouse/tablet, and px2sim's own pad: no letters */
	T("/sys/devices/platform/a003c00.virtio_mmio/virtio30/input/input1/event1", 0, 0);
	T("/sys/devices/virtual/input/input0/event0", 0, 0);
	/* a path named virtio that is still uinput */
	T("/sys/devices/virtual/input/virtio-fake/event9", 1, 0);
	T((const char *)0, 1, 0);
	if (!fails) puts("kbdsel-check: ok");
	return fails != 0;
}
