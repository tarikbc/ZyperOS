# QEMU's part of boot.scr (U-Boot's qemu_arm64, scripts/build-qemu-uboot.sh);
# board/zyperos/boot-slots.cmd follows. The virtio disk is vda, and QEMU
# hands U-Boot the device tree, so there is no file of one.
setenv zy_root /dev/vda
setenv zy_dtb
setenv zy_args "rootfstype=squashfs ro rootwait console=ttyAMA0 panic=30"
# The RK817's power-on reason, which the Pixel 2's U-Boot reads to tell a
# charger's start (board/px2/u-boot/patches/0005; ZyperOS's DESIGN.md 11.25).
# QEMU has no PMIC, so ZyperOS's tools/qemu-run.sh writes the reason into
# /px2sim-on.txt on this partition, on_source=0x40, when the cable goes into
# the VM while it is off; with no file the start is POWER's (0x80). A charger
# and not POWER makes zy_charge=1, which boot-slots.cmd passes on. Then the
# file says 0x02, what the RK817 says after a restart (read on a Pixel 2,
# 2026-11-06), so a restart never comes back as a charger's start.
setenv zy_charge
setexpr zy_sim ${scriptaddr} + 0x180000
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_sim} /px2sim-on.txt; then
	setenv on_source 0x80
	env import -t ${zy_sim} ${filesize} on_source
	setexpr zy_plug ${on_source} \& 0x40
	setexpr zy_key ${on_source} \& 0x80
	if itest ${zy_plug} != 0; then
		if itest ${zy_key} == 0; then setenv zy_charge 1; fi
	fi
	if itest ${on_source} != 0x02; then
		setenv on_source 0x02
		env export -t ${zy_sim} on_source
		fatwrite ${devtype} ${devnum}:${distro_bootpart} ${zy_sim} /px2sim-on.txt ${filesize}
	fi
fi
