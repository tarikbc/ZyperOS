# The GKD Pixel 2's part of boot.scr; board/zyperos/boot-slots.cmd follows.
#
# The kernel goes at 0x02200000, not U-Boot's default kernel_addr_r of
# 0x02080000: the kernel runs from any 2 MiB-aligned address, and given an
# unaligned one U-Boot first moves all of it up to the next one, which
# measured 245 ms. The device tree stays at U-Boot's fdt_addr_r, below it.
setenv kernel_addr_r 0x02200000
setenv zy_root /dev/mmcblk0p
setenv zy_dtb rk3326s-gkd-pixel2.dtb
# No console=ttyS2: the serial port has no one on the other end, and every
# kernel message was being written to it. The kernel has no framebuffer
# console either (linux-px2.config): the panel powers up in the background
# from early in boot (kernel patch 6), and nothing draws on it until
# userspace does. For kernel messages on screen while debugging, turn
# CONFIG_FRAMEBUFFER_CONSOLE back on; panic=30 then keeps a panic visible
# instead of rebooting after 1 s. The root is squashfs, read-only.
setenv zy_args "rootfstype=squashfs ro rootwait console=tty1 panic=30"
