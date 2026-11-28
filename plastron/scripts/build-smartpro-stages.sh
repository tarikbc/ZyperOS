#!/bin/sh
# The Smart Pro stage card (board/smartpro/stages): a Smart Pro card whose
# boot partition says, after one power-on, how far the start got, for a
# handheld that shows a black screen and has no serial cable. A card image
# goes in and the same card comes out with two files changed on its boot
# partition, nothing else: boot.scr with U-Boot's marks (boot-marks.py) and
# a/initramfs.cpio.gz with stagemarks as /rdinit, which leaves Linux's marks
# and then runs the card's own rootswitch, moved to /rootswitch. The boot
# loader, the partitions and the other files stay byte for byte.
#
# Runs in plastron-builder with the volume (read-only is enough), after a
# build that has the aarch64 compiler, mkimage and mtools (make brick, or a
# Smart Pro release build):
#
#   docker run --rm -v plastron-work:/work:ro -v "$PWD/plastron:/src:ro" \
#     -v BASE_DIR:/in:ro -v OUT_DIR:/out plastron-builder \
#     sh /src/scripts/build-smartpro-stages.sh BASE.img NAME
#
# BASE.img, in BASE_DIR, uncompressed: the Smart Pro card to mark, with the
# boot loader to test at 128 KiB. Out: NAME.img.xz and NAME.img.xz.sha256 in
# OUT_DIR. What each mark means: boot-marks.py and stagemarks.c.
set -eu
STAGES=/src/buildroot/board/smartpro/stages
HOST=/work/output-brick/host
[ -x "$HOST/bin/aarch64-linux-gcc" ] || HOST=/work/release-output-smartpro/host
IN=/in/$1
NAME=$2
export SOURCE_DATE_EPOCH=1791936000
export MTOOLS_SKIP_CHECK=1
for f in "$HOST/bin/aarch64-linux-gcc" "$HOST/bin/mkimage" "$HOST/bin/mcopy" "$IN"; do
	[ -e "$f" ] || { echo "missing $f" >&2; exit 1; }
done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

"$HOST/bin/aarch64-linux-gcc" -static -O2 -Wall -Wextra -o "$T/stagemarks" "$STAGES/stagemarks.c"
"$HOST/bin/aarch64-linux-strip" "$T/stagemarks"

# The boot partition: the GPT's first entry, named boot
set -- $(python3 -I -c '
import struct, sys
f = open(sys.argv[1], "rb")
f.seek(512)
h = f.read(92)
assert h[:8] == b"EFI PART", "no GPT"
f.seek(struct.unpack("<Q", h[72:80])[0] * 512)
e = f.read(128)
first, last = struct.unpack("<QQ", e[32:48])
assert e[56:].decode("utf-16le").rstrip("\0") == "boot", "partition 1 is not boot"
print(first, last - first + 1)' "$IN")
START=$1
COUNT=$2
dd if="$IN" of="$T/boot.vfat" bs=512 skip="$START" count="$COUNT" status=none
M="$HOST/bin/mcopy -i $T/boot.vfat"

# boot.scr: the card's own script with the marks
$M ::boot.scr "$T/boot.scr.card"
dd if="$T/boot.scr.card" of="$T/boot.cmd.card" bs=72 skip=1 status=none
python3 -I "$STAGES/boot-marks.py" "$T/boot.cmd.card" "$T/boot.cmd"
"$HOST/bin/mkimage" -A arm64 -O linux -T script -C none -n "ZyperOS A/B" \
	-d "$T/boot.cmd" "$T/boot.scr" > /dev/null

# The initramfs: stagemarks first, then the card's rootswitch
mkdir "$T/initramfs"
$M ::a/initramfs.cpio.gz "$T/initramfs.card.gz"
(cd "$T/initramfs" && gzip -dc "$T/initramfs.card.gz" | cpio -idm --quiet)
[ -f "$T/initramfs/rdinit" ] && [ ! -e "$T/initramfs/rootswitch" ] ||
	{ echo "the card's initramfs is not rootswitch alone" >&2; exit 1; }
mv "$T/initramfs/rdinit" "$T/initramfs/rootswitch"
cp "$T/stagemarks" "$T/initramfs/rdinit"
find "$T/initramfs" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
(cd "$T/initramfs" && find . | LC_ALL=C sort | cpio -o -H newc --reproducible --quiet) |
	gzip -9 -n > "$T/initramfs.cpio.gz"

touch -d "@$SOURCE_DATE_EPOCH" "$T/boot.scr" "$T/initramfs.cpio.gz"
$M -o "$T/boot.scr" ::boot.scr
$M -o "$T/initramfs.cpio.gz" ::a/initramfs.cpio.gz

cp "$IN" "$T/card.img"
dd if="$T/boot.vfat" of="$T/card.img" bs=512 seek="$START" conv=notrunc status=none
xz -T0 -6 -c "$T/card.img" > "/out/$NAME.img.xz"
(cd /out && sha256sum "$NAME.img.xz" > "$NAME.img.xz.sha256")
grep -c "^[[:space:]]*fatwrite mmc 0:1 \${zy_mark}" "$T/boot.cmd" | sed 's/^/boot.scr marks: /'
gzip -dc "$T/initramfs.cpio.gz" | cpio -it --quiet | tr '\n' ' ' | sed 's/^/initramfs: /'
echo
cat "/out/$NAME.img.xz.sha256"
