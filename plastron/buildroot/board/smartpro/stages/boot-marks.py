#!/usr/bin/env python3
"""The stage card's boot.cmd: a Smart Pro card's own boot.cmd (the text of its
boot.scr) with U-Boot's stage marks, for scripts/build-smartpro-stages.sh.

    boot-marks.py IN OUT

Each mark is a few lines of text, written with env export (which also sets
filesize to their length) and fatwrite, to a file of its own on the card's
boot partition, mmc 0:1, the partition U-Boot read boot.scr from. Nothing
else changes: the same loads, in the same order, to the same addresses, and
the same kernel line. The marks' text goes 2 MB above scriptaddr, between
bootstate's (1 MB above) and the initramfs (3 MB above).

    zy-1-bootscr.txt   boot.scr runs (its first line)
    zy-2-plls.txt      the PLL writes done (the CPU PLL's value)
    zy-3-image.txt     /a/Image.lz4 loaded (its size, and the slot)
    zy-4-unlz4.txt     the kernel unpacked (unlz4's size)
    zy-5-initrd.txt    /a/initramfs.cpio.gz loaded (its size)
    zy-6-dtb.txt       /a/smartpro.dtb loaded (its size)
    zy-7-booti.txt     the device tree set up, booti next (the kernel line)
    zy-x-fallback.txt  boot.scr ended without booting: U-Boot's own boot
                       command, slot a's uncompressed Image, runs next
"""
import sys


def mark(indent, n, name, stage, size, more=""):
    t = indent
    return (
        f'{t}setenv zy_stage "{n}: {stage}"\n'
        f"{t}setenv zy_size {size}\n"
        f"{t}env export -t ${{zy_mark}} zy_stage zy_size{more}\n"
        f"{t}fatwrite mmc 0:1 ${{zy_mark}} /zy-{n}-{name}.txt ${{filesize}}\n"
    )


HEAD = (
    "# The stage card's marks (board/smartpro/stages/boot-marks.py): a few\n"
    "# lines each, to a file of its own on this partition, so how far a start\n"
    "# got shows on the card afterwards\n"
    "setexpr zy_mark ${scriptaddr} + 0x200000\n"
    + mark("", 1, "bootscr", "boot.scr runs", "-")
    + "\n"
)

PLLS = "setexpr.l pllcpux *0x03001000\n"

LOADS = """if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_comp_addr_r} /${slot}/Image.lz4 &&
   unlz4 ${kernel_comp_addr_r} ${kernel_addr_r} 0x2000000 &&
   load ${devtype} ${devnum}:${distro_bootpart} ${ramdisk_addr_r} /${slot}/initramfs.cpio.gz && setenv rdsize ${filesize} &&
   load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} /${slot}/smartpro.dtb; then
"""

MARKED_LOADS = (
    "setenv zy_ok 0\n"
    "if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_comp_addr_r} /${slot}/Image.lz4; then\n"
    + mark("\t", 3, "image", "/${slot}/Image.lz4 loaded", "${filesize}", " slot trial try")
    + "\tif unlz4 ${kernel_comp_addr_r} ${kernel_addr_r} 0x2000000; then\n"
    + mark("\t\t", 4, "unlz4", "the kernel unpacked", "${filesize}")
    + "\t\tif load ${devtype} ${devnum}:${distro_bootpart} ${ramdisk_addr_r} /${slot}/initramfs.cpio.gz; then\n"
    "\t\t\tsetenv rdsize ${filesize}\n"
    + mark("\t\t\t", 5, "initrd", "/${slot}/initramfs.cpio.gz loaded", "${rdsize}")
    + "\t\t\tif load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} /${slot}/smartpro.dtb; then\n"
    + mark("\t\t\t\t", 6, "dtb", "/${slot}/smartpro.dtb loaded", "${filesize}")
    + "\t\t\t\tsetenv zy_ok 1\n"
    "\t\t\tfi\n"
    "\t\tfi\n"
    "\tfi\n"
    "fi\n"
    'if test "${zy_ok}" = "1"; then\n'
)

BOOTI = "\tbooti ${kernel_addr_r} ${ramdisk_addr_r}:${rdsize} ${fdt_addr_r}\nfi\n"

MARKED_BOOTI = (
    mark("\t", 7, "booti", "booti next", "${rdsize}", " bootargs")
    + BOOTI
    + mark("", "x", "fallback",
           "boot.scr ended without booting, the boot command of U-Boot (a/Image) runs next", "-")
)


def once(text, old, new):
    if text.count(old) != 1:
        sys.exit(f"boot-marks.py: {old.splitlines()[0]!r} is in the boot script "
                 f"{text.count(old)} times, not once")
    return text.replace(old, new)


def main():
    text = open(sys.argv[1]).read()
    if text.endswith("\0"):
        text = text.rstrip("\0")
    text = HEAD + text
    text = once(text, PLLS, PLLS + mark("", 2, "plls", "the PLLs are set", "${pllcpux}"))
    text = once(text, LOADS, MARKED_LOADS)
    text = once(text, BOOTI, MARKED_BOOTI)
    open(sys.argv[2], "w").write(text)


main()
