#!/bin/sh
# After board/px2/post-build.sh, which QEMU's image shares (the SSH key,
# ZyperOS's seed version, the licenses): the Pixel 2's start scripts for
# parts the virt machine doesn't have. S01governor sets the CPU's clock
# governor (QEMU has no cpufreq) and S40usbgadget makes the USB cable a
# network (QEMU's is virtio-net); both come in with the Pixel 2's overlay.
set -eu
rm -f "$TARGET_DIR/etc/init.d/S01governor" "$TARGET_DIR/etc/init.d/S40usbgadget"
