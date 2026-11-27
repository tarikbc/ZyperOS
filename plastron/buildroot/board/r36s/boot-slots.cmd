# The R36S's boot.scr tail: board/zyperos/boot-slots.cmd, the slots' boot
# every handheld's card shares, with a panel's overlay laid on the device
# tree after it is loaded. Its own copy, so the Pixel 2's boot script stays
# as it is; a change to the shared one is made here too.
load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} /${slot}/Image
if test -n "${zy_dtb}"; then
	load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} /${slot}/${zy_dtb}
	setenv zy_fdt ${fdt_addr_r}
else
	setenv zy_fdt ${fdtcontroladdr}
fi
# The R36S's own: a device tree overlay on the boot partition, where its
# prelude names one (zy_overlay, loaded at zy_ovl_addr) and the file is
# there, as ROCKNIX's overlays/mipi-panel.dtbo for the R36S's other panels,
# applied over its device tree
if test -n "${zy_overlay}"; then
	if load ${devtype} ${devnum}:${distro_bootpart} ${zy_ovl_addr} /${zy_overlay}; then
		fdt addr ${zy_fdt}
		fdt resize 8192
		fdt apply ${zy_ovl_addr}
	fi
fi
# A charger started the handheld while it was off (the Pixel 2's U-Boot sets
# zy_charge=1: board/px2/u-boot/patches/0005): zycharge=1 on the kernel's
# line, a word without a dot like zytheme's, so the splash finds it in init's
# environment and stays plain, and ZyperOS shows its charging screen (its
# DESIGN.md 11.25). Nothing on any other start, nor with a U-Boot that never
# sets it.
setenv zy_charge_arg
if test "${zy_charge}" = "1"; then setenv zy_charge_arg zycharge=1; fi
setenv bootargs "root=${zy_root}${zy_part} ${zy_args} zyperos.slot=${slot} zyperos.trial=${trial} ${zy_theme} ${zy_charge_arg}"
booti ${kernel_addr_r} - ${zy_fdt}
