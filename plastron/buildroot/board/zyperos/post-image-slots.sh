#!/bin/sh
# The A/B card (ZyperOS's docs/updates.md, layer 2), for every board's
# post-image.sh: boot.scr from the board's prelude and boot-slots.cmd, the
# first bootstate.txt (slot a, the image's system version), then genimage,
# then root B moved to partition 4.
#
#   post-image-slots.sh BOARD_DIR
#
# genimage numbers partitions in the order they are written, so root B, the
# third it writes, is moved to the table's fourth entry afterwards: the games
# partition the first boot adds (/etc/init.d/datapart) stays partition 3, as
# every card before the slots has it, and starts after root B.
set -eu
BOARD_DIR=$1
COMMON=$(dirname "$0")

# Made here on every build, never kept from an earlier one: the output folder
# is shared, and a boot.scr left by a build of another layout (this one's in
# a build without slots, or the other way round) boots nothing (2026-11-06:
# a Pixel hung at boot on exactly that)
rm -f "$BINARIES_DIR/boot.cmd" "$BINARIES_DIR/boot.scr"
cat "$BOARD_DIR/boot-prelude.cmd" "$COMMON/boot-slots.cmd" > "$BINARIES_DIR/boot.cmd"
"$HOST_DIR/bin/mkimage" -A arm64 -O linux -T script -C none -n "ZyperOS A/B" \
	-d "$BINARIES_DIR/boot.cmd" "$BINARIES_DIR/boot.scr" > /dev/null

# The root against its slot, before anything is written: the slot's size is
# genimage.cfg's for the rootfs partition, and a card keeps the size it was
# laid out with, so a root that outgrows it needs a decision (a bigger slot
# means a fresh install), not a build that fails deep in genimage
SLOT=$(awk '/partition rootfs[ \t]*\{/ { in_root = 1 } in_root && /size *=/ { print $3; exit }' \
	"$BOARD_DIR/genimage.cfg")
case $SLOT in
*M) SLOT_BYTES=$(( ${SLOT%M} * 1048576 )) ;;
*) echo "post-image-slots: no size for the rootfs partition in $BOARD_DIR/genimage.cfg" >&2; exit 1 ;;
esac
ROOT_BYTES=$(stat -c %s "$BINARIES_DIR/rootfs.squashfs")
if [ "$ROOT_BYTES" -gt "$SLOT_BYTES" ]; then
	echo "post-image-slots: rootfs.squashfs is $(( ROOT_BYTES / 1048576 )) MB, and a system slot holds" \
		"$SLOT ($BOARD_DIR/genimage.cfg). Make the root smaller, or the slots bigger, which every" \
		"card then needs a fresh install for (ZyperOS's docs/updates.md)" >&2
	exit 1
fi
echo "post-image-slots: rootfs.squashfs $(( ROOT_BYTES / 1048576 )) MB of the $SLOT slot" \
	"($(( ROOT_BYTES * 100 / SLOT_BYTES ))%)"

VERSION=$(sed -n 's/^version //p' "$TARGET_DIR/etc/zyperos/system" 2>/dev/null || true)
printf 'slot=a\ntrial=0\ntry=0\nfell=-\na=%s\nb=-\n' "${VERSION:--}" > "$BINARIES_DIR/bootstate.txt"

support/scripts/genimage.sh -c "$BOARD_DIR/genimage.cfg"

# The boot partition as written: the boot.scr just made, a script for the
# slots, and slot a's kernel where it looks
fail() { echo "post-image-slots: boot.vfat $*" >&2; exit 1; }
"$HOST_DIR/bin/mcopy" -n -o -i "$BINARIES_DIR/boot.vfat" ::boot.scr "$BINARIES_DIR/boot.scr.check" ||
	fail "has no boot.scr"
cmp -s "$BINARIES_DIR/boot.scr" "$BINARIES_DIR/boot.scr.check" || fail "has another boot.scr than this build's"
rm -f "$BINARIES_DIR/boot.scr.check"
tail -c +65 "$BINARIES_DIR/boot.scr" | grep -aq 'load .*/${slot}/Image' || fail "boot.scr does not boot the slots"
"$HOST_DIR/bin/mdir" -i "$BINARIES_DIR/boot.vfat" ::a/Image > /dev/null 2>&1 || fail "has no a/Image"
"$HOST_DIR/bin/mdir" -i "$BINARIES_DIR/boot.vfat" ::bootstate.txt > /dev/null 2>&1 || fail "has no bootstate.txt"
echo "post-image-slots: boot.vfat holds this build's boot.scr, a/Image and bootstate.txt"

python3 - "$BINARIES_DIR/sdcard.img" <<'PY'
import struct, sys
p = sys.argv[1]
with open(p, "r+b") as f:
    mbr = bytearray(f.read(512))
    e = lambda i: 446 + 16 * i
    third, fourth = mbr[e(2):e(2) + 16], mbr[e(3):e(3) + 16]
    if any(fourth) or not any(third):
        sys.exit("post-image-slots: the image's table is not boot, root A, root B")
    mbr[e(3):e(3) + 16] = third
    mbr[e(2):e(2) + 16] = bytes(16)
    f.seek(0)
    f.write(mbr)
    start, size = struct.unpack("<II", third[8:16])
    print("post-image-slots: root B is partition 4, sectors %d to %d" % (start, start + size))
PY
