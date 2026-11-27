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
# With no panel picked and no overlays/mipi-panel.dtbo on the card, panel 4's
# overlay (boot-slots.cmd): a card as it comes lit board R36S-V12 only once
# zy_panel=4 was set (Sylver, 2026-11-27), the device tree's own panel 0
# stayed black. Panel 4 is the NV3051D that dArkOSRE's own device trees give
# its boards R36S-V12 2023-08-18 Panel 4 and HL-R36H-V20 2024-05-18; its
# boards R36S-V21 (2550, 2551, 2552), R36S-V22, R36XX-V21 and HL-R36H-V21
# have panel 4v22, whose init sequence is panel 4's without its first sleep
# out and display on (0x11 0x00, 0x29 0x00), which it then sends anyway, so
# panel 4's overlay is meant to light those too. Only R35S-V11 and R36S-V12
# Panel 0 there have panel 0. Each board's panel-init-sequence compared with
# overlays/panel-*.dts, from southoz/dArkOSRE-R36 at
# 3f372c85aa53a26b265c3dc0c2661ec0fae5faf0, files/BOOT/dtb/r36s/*/
# rk3326-r36s-linux.dtb. No firmware found reads which panel a unit has:
# ROCKNIX picks it by hand (its R36S overlay, FDTOVERLAYS in
# packages/u-boot/config/extlinux/extlinux.conf), dArkOSRE by a script on a
# computer (files/BOOT/dtb/select_devices.sh), ArkOS R3XS by keys held at
# power-on (AeolusUX/ArkOS-R3XS README). docs/devices/r36s.md, "Panels".
setenv zy_panel_default 4
