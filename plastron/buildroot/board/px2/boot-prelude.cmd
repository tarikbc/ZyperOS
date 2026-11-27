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
# CONFIG_FRAMEBUFFER_CONSOLE back on, and a longer panic= to keep a panic visible
# instead of rebooting after 1 s. The root is squashfs, read-only.
#
# And no start that hangs for good (a Pixel 2's cold starts stopped in the
# kernel under a second in, backlight on and no splash, until the battery
# ran out: 2026-11-23). The card's root is waited for 20 s at most, then the
# kernel panics ("VFS: Unable to mount root fs"), which pstore keeps and
# panic=10 restarts: it is there 0.14 s after the wait begins (0.57 s into
# the start, measured on a cold start of a 128 GB SDR104 card), so 20 s is
# a card more than a hundred times slower. The watchdog U-Boot started is
# the kernel's to pet until something opens it, 60 s at most
# (watchdog.open_timeout; plastron's hwdog opens it at init's first step),
# so a kernel that stops anywhere is reset. A task stuck in the kernel for
# 60 s panics too (the hung task sysctls, applied once the root is up),
# during the start only: zyperos.init sets the panic back to 0 once the
# launcher is starting, and around the card's long writes (a fresh card's
# format, the first start's copy), so a slow card's long write never does.
# 10 s, not more: the watchdog's first stage is a panic too (its pre-timeout
# interrupt, linux-px2.config), and its second, the chip's own reset, comes one
# TOP (21 s or more) after it. The panic's restart is the RK817's, which the
# card comes back from; the chip's reset left the card unreadable to the boot
# ROM, which started the handheld's own loader (2026-11-26).
setenv zy_args "rootfstype=squashfs ro rootwait=20 console=tty1 panic=10 watchdog.open_timeout=60 sysctl.kernel.hung_task_timeout_secs=60 sysctl.kernel.hung_task_panic=1"
# The PX30's own resets (its watchdog, its thermal cut-off) restart the RK817
# too: U-Boot's zypmicrst (its patches/0006) puts GPIO0_A4, the RK817's sleep
# pin on the GKD Pixel 2, in its function 2, the SoC's reset out, idle high,
# and sets the RK817 to restart the whole PMU when it goes low, as Rockchip's
# own kernel does. Alone, the chip's reset left the RK817 on: vccio_sd at
# 1.8 V and the card in its SDR104 signalling, which the boot ROM cannot read
# at 3.3 V, so it started the handheld's own loader instead (its charging
# screen, then its red "system failure": 1.1.1-test.2, 2026-11-26). With the
# RK817's restart the card is power cycled and boots, the RAM and pstore
# kept: measured on that Pixel 2, back in ZyperOS 16 s after the watchdog,
# ON_SOURCE 0x04, which U-Boot turns into zyreset=chip. Here, in the Pixel
# 2's own part, as the wiring is the Pixel 2's (the R36S shares U-Boot, not
# its board); at every start, as the RK817's restart sets its registers
# back. A U-Boot without the command says so and boots as before.
zypmicrst
