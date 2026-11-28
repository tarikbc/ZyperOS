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
# watchdog.open_timeout=60: the watchdog the Pixel 2's U-Boot starts is the
# kernel's to pet, 60 s at most, until plastron's hwdog opens it at init's
# first step, as on the Pixel 2 (its boot-prelude.cmd). The R36S's kernel
# left it unpetted until 2026-11-28, and the chip reset itself 21 s into
# every start (rk3326-gameconsole-r36s-zyperos.dts, &wdt). And panic=10, not
# 30: the watchdog's first stage is a panic (its pre-timeout interrupt), and
# its second, the chip's own reset, comes one TOP (21 s) after it, so a panic
# waiting 30 s never got to restart through the RK817 (the Pixel 2's
# boot-prelude.cmd chose 10 for the same reason).
setenv zy_args "rootfstype=squashfs ro rootwait console=ttyS2,1500000 console=tty1 panic=10 loglevel=7 watchdog.open_timeout=60"
# The tester's own settings, KEY=VALUE lines (zy_dtb=, zy_overlay=,
# zy_args=), read over the ones above. Loaded where the kernel goes next,
# as nothing else is there yet.
if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} /r36s-boot.txt; then
	env import -t ${kernel_addr_r} ${filesize}
fi
# The PX30's own resets (its watchdog, its thermal cut-off) restart the RK817
# too, the Pixel 2's zypmicrst (board/px2/boot-prelude.cmd says why, and
# board/px2/u-boot/patches/0006 what it writes): GPIO0_A4 in its function 2,
# the SoC's reset out, and the RK817's sleep pin set to restart it. Alone,
# the chip's reset leaves vccio_sd at 1.8 V and the card in its UHS
# signalling, which the boot ROM cannot read, and the R36S has nothing else
# to start from: black until a power-off. The R36S's wiring is the Pixel 2's
# there: its own firmware's device tree arms the same pair at every start,
# the RK817's "pmic-reset" pin state, soc_slppin_rst (GPIO0_A4, function 2)
# with rk817_slppin_rst (its sleep pin's reset function), on board R36S-V21
# 2024-12-18 2604 and R36S-Plus-V20 2551 (djparentx/dArkOSen-R36S at
# 28f5427f7ae953ee6e813618705baf569fef0f27, boot/dtb/r36s/*/
# rk3326-r36s-linux.dtb and rk3326-r36splus-linux.dts), and ROCKNIX's tree
# keeps the three soc_slppin states on GPIO0_A4 (rk3326-gameconsole-r3xs.dtsi).
# zypmicrst arms it only when the line reads high in function 2. zy_pmicrst=0
# in r36s-boot.txt leaves it off, for a clone wired otherwise.
if test "${zy_pmicrst}" != "0"; then
	zypmicrst
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
# rk3326-r36s-linux.dtb. dArkOSen-R36S's trees agree (djparentx/
# dArkOSen-R36S at 28f5427f7ae953ee6e813618705baf569fef0f27,
# boot/dtb/r36s/*/): panel 4 or 4v22 on R36S-V12 Panel 4, every R36S-V21
# (2503 to 2604), every R36S-V22 and R36S-V30 2603.
# No firmware found reads which panel a unit has:
# ROCKNIX picks it by hand (its R36S overlay, FDTOVERLAYS in
# packages/u-boot/config/extlinux/extlinux.conf), dArkOSRE by a script on a
# computer (files/BOOT/dtb/select_devices.sh), ArkOS R3XS by keys held at
# power-on (AeolusUX/ArkOS-R3XS README). docs/devices/r36s.md, "Panels".
setenv zy_panel_default 4
