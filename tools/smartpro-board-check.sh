#!/bin/sh
# The TrimUI Smart Pro's plastron board is the Brick's with a few lines of
# its own (plastron/buildroot/board/smartpro, configs/smartpro_defconfig).
# This holds it there: its Buildroot config, card layout and slot boot are
# the Brick's but for those lines, so a change to the Brick's is the Smart
# Pro's too, and its boot loader's memory values are the ones TrimUI's boot0
# carries for it (the device-modular spec's appendix, "the Smart Pro's
# firmware"), which nothing on this Mac can try.
set -u
cd "$(dirname "$0")/.."
B=plastron/buildroot
fails=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fails=$((fails + 1)); }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# Lines that are not comments or blank
lines() { grep -v '^[[:space:]]*#' "$1" | grep -v '^[[:space:]]*$'; }

# 1. Buildroot: the Brick's, with its own device, name and post-script argument
lines $B/configs/brick_defconfig |
	sed -e 's/^BR2_TARGET_GENERIC_HOSTNAME="brick"$/BR2_TARGET_GENERIC_HOSTNAME="smartpro"/' \
	    -e 's/^BR2_TARGET_GENERIC_ISSUE="brick"$/BR2_TARGET_GENERIC_ISSUE="smartpro"/' \
	    -e 's/^BR2_PLASTRON_DEVICE="brick"$/BR2_PLASTRON_DEVICE="smartpro"/' \
	    -e 's#^\(BR2_ROOTFS_POST_BUILD_SCRIPT=.*/board/brick/post-build.sh"\)$#\1\nBR2_ROOTFS_POST_SCRIPT_ARGS="smartpro"#' \
	> "$T/want"
lines $B/configs/smartpro_defconfig > "$T/have"
if cmp -s "$T/want" "$T/have"; then
	ok "smartpro_defconfig is brick_defconfig with its own device, name and post-script argument"
else
	bad "smartpro_defconfig is not brick_defconfig with its four lines:"; diff "$T/want" "$T/have" | sed 's/^/       /'
fi

# 2. The card: the Brick's layout, its own boot label and device tree
lines $B/board/brick/genimage.cfg |
	sed -e 's/label = "BRICKBOOT"/label = "SMARTBOOT"/' \
	    -e 's#file a/brick.dtb { image = "brick.dtb" }#file a/smartpro.dtb { image = "smartpro.dtb" }#' > "$T/want"
lines $B/board/smartpro/genimage.cfg > "$T/have"
cmp -s "$T/want" "$T/have" && ok "genimage.cfg is the Brick's with SMARTBOOT and smartpro.dtb" ||
	{ bad "genimage.cfg is not the Brick's with its two lines:"; diff "$T/want" "$T/have" | sed 's/^/       /'; }

# 3. The slot's boot: the Brick's with smartpro.dtb; the prelude the Brick's own
lines $B/board/brick/boot-slot.cmd | sed 's#/${slot}/brick.dtb; then#/${slot}/smartpro.dtb; then#' > "$T/want"
lines $B/board/smartpro/boot-slot.cmd > "$T/have"
cmp -s "$T/want" "$T/have" && ok "boot-slot.cmd is the Brick's with smartpro.dtb" ||
	{ bad "boot-slot.cmd is not the Brick's with smartpro.dtb:"; diff "$T/want" "$T/have" | sed 's/^/       /'; }
cmp -s $B/board/brick/boot-prelude.cmd $B/board/smartpro/boot-prelude.cmd &&
	ok "boot-prelude.cmd is the Brick's" || bad "boot-prelude.cmd is not the Brick's"

# 4. The boot loader's memory: boot0's values, LPDDR4 at 792 MHz, DCDC3 at 1.1 V
P=$B/board/smartpro/u-boot/patches/0001-trimui-smartpro-board.patch
for v in CONFIG_DRAM_CLK=792 CONFIG_SUNXI_DRAM_A133_LPDDR4=y CONFIG_AXP_DCDC3_VOLT=1100 \
	CONFIG_DRAM_SUNXI_DX_ODT=0x07070707 CONFIG_DRAM_SUNXI_DX_DRI=0x0d0d0d0d CONFIG_DRAM_SUNXI_CA_DRI=0x0e0e \
	CONFIG_DRAM_SUNXI_PARA0=0x0d0a050c CONFIG_DRAM_SUNXI_MR11=0x4 CONFIG_DRAM_SUNXI_MR12=0x72 \
	CONFIG_DRAM_SUNXI_MR14=0x7 CONFIG_DRAM_SUNXI_MR22=0x26 CONFIG_DRAM_SUNXI_TPR0=0x06060606 \
	CONFIG_DRAM_SUNXI_TPR1=0x04040404 CONFIG_DRAM_SUNXI_TPR6=0x48010101 CONFIG_DRAM_SUNXI_TPR10=0x273333 \
	CONFIG_DRAM_SUNXI_TPR11=0x241f1923 CONFIG_DRAM_SUNXI_TPR12=0x14151313 CONFIG_DRAM_SUNXI_TPR13=0x81d20 \
	CONFIG_DRAM_SUNXI_TPR14=0x2023211f; do
	grep -qx "+$v" "$P" || bad "the boot loader's defconfig has no $v"
done
grep -q '^+CONFIG_SUNXI_DRAM_A133_LPDDR3' "$P" && bad "the boot loader's defconfig asks for LPDDR3"
grep -q 'a/smartpro.dtb' "$P" && ! grep -q 'a/brick.dtb' "$P" &&
	ok "the boot loader: boot0's memory values, DCDC3 at 1.1 V, smartpro.dtb in its fallback" ||
	bad "the boot loader's fallback does not load smartpro.dtb"

# 5. Its start: sppad, never brick-pad, on a Smart Pro
I=$B/board/brick/rootfs-overlay/etc/init.d/brick
awk '/device smartpro/,/^\telse$/' "$I" > "$T/sp"
grep -q '/usr/lib/zyperos/sppad' "$T/sp" && grep -q 'gpio107' "$T/sp" && ! grep -q 'brick-pad' "$T/sp" &&
	ok "the start script: on a Smart Pro, GPIO 107 and sppad, no brick-pad" ||
	bad "the start script's Smart Pro branch does not start sppad alone"

# 6. Its part of the card script: the Brick's, which takes its folder from the
# system's device line (tools/launch-start-check.sh runs it as the Smart Pro)
cmp -s sd/brick/device.sh sd/smartpro/device.sh && grep -q '^smartpro) DEVICE_ID=smartpro ;;$' sd/brick/device.sh &&
	ok "the card script's device.sh is the Brick's, with the Smart Pro's own folder" ||
	bad "sd/smartpro/device.sh is not the Brick's, or the Brick's does not know the Smart Pro"

echo "smartpro-board: $fails failed"
[ "$fails" = 0 ]
