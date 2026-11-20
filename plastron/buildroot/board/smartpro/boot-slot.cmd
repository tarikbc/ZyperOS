# The TrimUI Smart Pro's load and boot, the Brick's (board/brick/
# boot-slot.cmd) with its own device tree, smartpro.dtb. The Brick's note:
# The TrimUI Brick's load and boot, after board/zyperos/bootstate.cmd chose
# ${slot} (a or b) and ${zy_part} (root A's partition 2 or root B's 4). The
# kernel LZ4-compressed (Image.lz4, 4.1 MB for 8.8), unpacked straight to
# where it runs: U-Boot reads the card at about 17 MB/s, and the smaller file
# saves 194 ms of reading for 33 ms of unpacking (with U-Boot's patch 0010).
# The initramfs is rootswitch (/rdinit), which mounts ${zy_part} as the root.
# If anything here fails, U-Boot's own boot command boots slot a's
# uncompressed Image (u-boot patch 0011).
if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_comp_addr_r} /${slot}/Image.lz4 &&
   unlz4 ${kernel_comp_addr_r} ${kernel_addr_r} 0x2000000 &&
   load ${devtype} ${devnum}:${distro_bootpart} ${ramdisk_addr_r} /${slot}/initramfs.cpio.gz && setenv rdsize ${filesize} &&
   load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} /${slot}/smartpro.dtb; then
	fdt addr ${fdt_addr_r}
	fdt resize 4096
	fdt rsvmem add 0x40000000 0x40000
	# The Smart Pro's own storage (the eMMC, sdc2 at 04022000) is never
	# touched: smartpro.dtb, as brick.dtb, has no node for it at all, so
	# Linux never sees it. Keep it out of the device tree. The CPU PLL's
	# value goes by the Brick's name, which its kernel reads (cpufreq).
	fdt set /chosen brick,pll-cpux "${pllcpux}"
	# A new root waiting on the card, a developer's (rootswitch writes it to
	# root A, then removes this marker): taken when slot a boots, only
	setenv rootupdate
	if test "${slot}" = "a" && size ${devtype} ${devnum}:${distro_bootpart} root-update; then
		setenv rootupdate " rootupdate=1"
	fi
	setenv bootargs "console=ttyS0,115200 loglevel=1 rdinit=/rdinit cma=64M bootscr=1${rootupdate} zyperos.root=/dev/mmcblk0p${zy_part} zyperos.slot=${slot} zyperos.trial=${trial} ${zy_theme}"
	booti ${kernel_addr_r} ${ramdisk_addr_r}:${rdsize} ${fdt_addr_r}
fi
