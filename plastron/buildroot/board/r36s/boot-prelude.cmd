# The R36S's part of boot.scr; board/zyperos/boot-slots.cmd follows. The
# Pixel 2's (board/px2/boot-prelude.cmd says why each line is what it is),
# with the R36S's device tree, kernel messages on the screen and on the
# serial port while it is a test card, and r36s-boot.txt on the boot
# partition, which a tester edits on any computer to try another device tree
# or a panel's overlay without building anything (docs/devices/r36s.md, "On
# a test card").
setenv kernel_addr_r 0x02200000
setenv zy_root /dev/mmcblk0p
setenv zy_dtb rk3326-gameconsole-r36s-zyperos.dtb
# ROCKNIX's way for the R36S's other panels: an overlay on the boot
# partition, applied over the device tree when it is there
setenv zy_overlay overlays/mipi-panel.dtbo
setenv zy_ovl_addr 0x0a200000
setenv zy_args "rootfstype=squashfs ro rootwait console=ttyS2,1500000 console=tty1 panic=30 loglevel=7"
# The tester's own settings, KEY=VALUE lines (zy_dtb=, zy_overlay=,
# zy_args=), read over the ones above. Loaded where the kernel goes next,
# as nothing else is there yet.
if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} /r36s-boot.txt; then
	env import -t ${kernel_addr_r} ${filesize}
fi
# zy_panel=N: the overlay overlays/panel-N.dtbo, so a card can hold the
# overlays of several panels and r36s-boot.txt picks one with a digit
if test -n "${zy_panel}"; then
	setenv zy_overlay overlays/panel-${zy_panel}.dtbo
fi
