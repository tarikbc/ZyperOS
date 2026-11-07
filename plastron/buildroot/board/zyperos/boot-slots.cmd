# The A/B slots (ZyperOS's docs/updates.md, layer 2), the same on every
# board: each board's post-image.sh puts its own few lines in front of this
# (boot-prelude.cmd: zy_root, the root device without its number, zy_dtb,
# the device tree's file or nothing, zy_args, the kernel's other arguments)
# and builds boot.scr from the two.
#
# /bootstate.txt on this partition says which slot boots: slot=a (root in
# partition 2, kernel in /a) or slot=b (partition 4, /b). A new system starts
# on trial (trial=1): each start adds one to try and writes it back, and the
# third start that the system has not confirmed (zyperos-update confirm,
# once the launcher is up) boots the other slot again and says so (fell=).
# Writes happen only on trial; a normal start only reads. A card with no
# bootstate.txt boots slot a, as one written before the slots did. A start a
# charger made (zy_charge, below) is not counted: ZyperOS shows only its
# charging screen then, and confirms nothing, so a charger plugged in and out
# would otherwise send a new system back.
setexpr zy_state ${scriptaddr} + 0x100000
setenv slot a
setenv trial 0
setenv try 0
# Never empty: env export stops at a name that is not set. a and b are the
# system versions in each slot, for Linux; fell is the slot that failed.
setenv fell -
setenv a -
setenv b -
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /bootstate.txt; then
	env import -t ${zy_state} ${filesize} slot trial try fell a b
fi
setenv zy_count ${trial}
if test "${zy_charge}" = "1"; then setenv zy_count 0; fi
if test "${zy_count}" = "1"; then
	setexpr try ${try} + 1
	if itest ${try} -ge 3; then
		setenv fell ${slot}
		if test "${slot}" = "b"; then setenv slot a; else setenv slot b; fi
		setenv trial 0
		setenv try 0
	fi
	env export -t ${zy_state} slot trial try fell a b
	fatwrite ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /bootstate.txt ${filesize}
fi
# The boot screen's theme and language (ZyperOS's src/boottheme.h, DESIGN.md
# 11.20 and 11.21): theme.txt, which the launcher keeps here, onto the
# kernel's line as three words without a dot, which the kernel hands to
# init's environment, where the splash reads them. "-" without the file, so
# the launcher knows this boot.scr asked and found none (and writes it),
# where an older one says nothing; the splash plays its default for "-", and
# its first start's line in English. After bootstate's write, which used the
# same memory.
setenv zytheme -
setenv zyzone -
setenv zylang -
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /theme.txt; then
	env import -t ${zy_state} ${filesize} zytheme zyzone zylang
fi
setenv zy_theme "zytheme=${zytheme} zyzone=${zyzone} zylang=${zylang}"
if test "${slot}" = "b"; then setenv zy_part 4; else setenv slot a; setenv zy_part 2; fi
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
setenv bootargs "root=${zy_root}${zy_part} ${zy_args} zyperos.slot=${slot} zyperos.trial=${trial} ${zy_theme} ${zy_charge_arg}"
booti ${kernel_addr_r} - ${zy_fdt}
