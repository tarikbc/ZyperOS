load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} /${slot}/Image
if test -n "${zy_dtb}"; then
	load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} /${slot}/${zy_dtb}
	setenv zy_fdt ${fdt_addr_r}
else
	setenv zy_fdt ${fdtcontroladdr}
fi
# A charger started the handheld while it was off (the Pixel 2's U-Boot sets
# zy_charge=1: board/px2/u-boot/patches/0005): zycharge=1 on the kernel's
# line, a word without a dot like zytheme's, so the splash finds it in init's
# environment and stays plain, and ZyperOS shows its charging screen (its
# DESIGN.md 11.25). Nothing on any other start, nor with a U-Boot that never
# sets it.
setenv zy_charge_arg
if test "${zy_charge}" = "1"; then setenv zy_charge_arg zycharge=1; fi
# The chip itself reset the handheld, its watchdog or its thermal cut-off
# (the Pixel 2's U-Boot sets zy_reset=chip from the RK817's ON_SOURCE:
# board/px2/u-boot/patches/0006): zyreset= on the kernel's line, which
# plastron's crashlog reports even when the RAM kept nothing. Nothing on any
# other start.
setenv zy_reset_arg
if test -n "${zy_reset}"; then setenv zy_reset_arg zyreset=${zy_reset}; fi
setenv bootargs "root=${zy_root}${zy_part} ${zy_args} zyperos.slot=${slot} zyperos.trial=${trial} ${zy_theme} ${zy_charge_arg} ${zy_test} ${zy_reset_arg}"
booti ${kernel_addr_r} - ${zy_fdt}
