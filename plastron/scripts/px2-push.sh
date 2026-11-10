#!/bin/sh
# Put a freshly built system on a GKD Pixel 2 over the USB cable, without
# taking the card out: `make push` (`make push BOOT=1` for boot.scr too,
# `make push LOADER=1` for the bootloader). Needs a development build's SSH
# key (local/authorized_keys) on the card already, and the device on the
# cable at 10.42.0.1.
#
# It writes the system slot the device is NOT running, as System Update does
# (ZyperOS's docs/updates.md, layer 2): the root into that slot's partition,
# read back past the cache; its kernel and device tree into /a or /b on the
# boot partition, read back; and bootstate.txt told to boot it on trial, so a
# system that does not come up goes back to the running one after three
# starts. The running slot is never written. On 2026-11-10 the old way, root
# A written over while the device ran from it, killed the writing shell with
# a Bus error (its pages read from the squashfs being overwritten) before the
# read-back and the slot step. A card without a second slot (no partition 4
# or no bootstate.txt), or whose kernel names no slot, gets nothing: write it
# with the ZyperOS Installer. The slot choice and the bootstate edits are
# scripts/px2-slot.sh, held by scripts/px2-slot-check.sh; the device works
# the plan out again from what it reads before it writes, and writes only
# when both agree.
#
# The device writes its own card. That is the point: a Mac refuses a raw write
# to a card reader from most tools ("Operation not permitted" even as root,
# its Removable Volumes privacy rule), and the card's games partition must
# never be part of a write anyway; it is made read-only first. LOADER=1 also
# writes the bootloader (from sector 64 to where p1 starts), which both slots
# share, and BOOT=1 boot.scr, which both slots share too.
#
#   PX2_SYSTEM=1.0.2 [PX2_LOADER=u-boot-px2.bin] [PX2_BOOTSCR=boot.scr] \
#     scripts/px2-push.sh rootfs.squashfs Image rk3326s-gkd-pixel2.dtb
set -eu

HOST=${PX2_HOST:-10.42.0.1}
KEY=${PX2_KEY:-$HOME/.ssh/zyperos_pixel2}
USAGE="usage: px2-push.sh rootfs.squashfs Image DTB"
ROOTFS=${1:?$USAGE}
KERNEL=${2:?$USAGE}
DTB=${3:?$USAGE}
BOOTSCR=${PX2_BOOTSCR:-}
LOADER=${PX2_LOADER:-}
SYSTEM=${PX2_SYSTEM:-dev}
LIB=$(dirname "$0")/px2-slot.sh
DTBNAME=$(basename "$DTB")
case "$DTBNAME" in
"" | *[!0-9A-Za-z._-]*) echo "px2-push: $DTB is not a device tree's file name" >&2; exit 1 ;;
esac
case "$SYSTEM" in
"" | *[!0-9A-Za-z.+-]*) echo "px2-push: PX2_SYSTEM \"$SYSTEM\" is not a version" >&2; exit 1 ;;
esac
# A development device on the cable: plastron's root is read-only, so
# dropbear makes a new host key at every boot and there is no key to check
# against. The device is on a cable of its own, at an address only it has.
OPTS="-o ConnectTimeout=8 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SSH="ssh $OPTS ${KEY:+-i $KEY} root@$HOST"
SCP="scp -O -q $OPTS ${KEY:+-i $KEY}"

sum() { shasum -a 256 "$1" | cut -d' ' -f1; }

. "$LIB"

$SSH 'grep -q "gamekiddy,gkd-pixel2" /proc/device-tree/compatible' ||
	{ echo "px2-push: $HOST is not a GKD Pixel 2" >&2; exit 1; }

# Which slot to write, before anything is sent: the device's command line,
# partition 4 and bootstate.txt (the boot partition mounted read-only)
facts=$($SSH 'cat /proc/cmdline
[ -e /sys/class/block/mmcblk0p4 ] && echo p4=1 || echo p4=0
mkdir -p /tmp/px2-push-boot
if mount -o ro -t vfat /dev/mmcblk0p1 /tmp/px2-push-boot 2> /dev/null; then
	[ -f /tmp/px2-push-boot/bootstate.txt ] && echo bs=1 || echo bs=0
	umount /tmp/px2-push-boot
else
	echo bs=0
fi')
cmdline=$(printf '%s\n' "$facts" | sed -n 1p)
p4=$(printf '%s\n' "$facts" | sed -n 's/^p4=//p')
bs=$(printf '%s\n' "$facts" | sed -n 's/^bs=//p')
plan=$(px2_slot_plan "$cmdline" "$p4" "$bs") || exit 1
# shellcheck disable=SC2086
set -- $plan
RUNNING=$1 TARGET=$2 PART=$3
echo "px2-push: the device runs slot $RUNNING; writing slot $TARGET ($PART)"

check_size() { # image partition
	have=$($SSH "echo \$((\$(cat /sys/class/block/$2/size) * 512))")
	need=$(wc -c < "$1" | tr -d ' ')
	[ "$need" -le "$have" ] || { echo "px2-push: $1 is $need bytes, $2 holds $have" >&2; exit 1; }
}
check_size "$ROOTFS" "$PART"
# The bootloader goes at sector 64 and must end before p1 starts
if [ -n "$LOADER" ]; then
	p1=$($SSH 'cat /sys/class/block/mmcblk0p1/start')
	need=$(wc -c < "$LOADER" | tr -d ' ')
	[ $((need % 512)) -eq 0 ] && [ $((64 + need / 512)) -le "$p1" ] ||
		{ echo "px2-push: $LOADER is $need bytes, more than the $(((p1 - 64) * 512)) before p1" >&2; exit 1; }
fi

$SSH 'rm -rf /tmp/px2-push && mkdir -p /tmp/px2-push'
$SCP "$ROOTFS" "root@$HOST:/tmp/px2-push/rootfs"
$SCP "$KERNEL" "root@$HOST:/tmp/px2-push/Image"
$SCP "$DTB" "root@$HOST:/tmp/px2-push/dtb"
[ -z "$BOOTSCR" ] || $SCP "$BOOTSCR" "root@$HOST:/tmp/px2-push/boot.scr"
[ -z "$LOADER" ] || $SCP "$LOADER" "root@$HOST:/tmp/px2-push/loader"
for f in rootfs Image dtb ${BOOTSCR:+boot.scr} ${LOADER:+loader}; do
	case $f in
	rootfs) src=$ROOTFS ;; Image) src=$KERNEL ;; dtb) src=$DTB ;;
	boot.scr) src=$BOOTSCR ;; loader) src=$LOADER ;;
	esac
	[ "$($SSH "sha256sum /tmp/px2-push/$f" | cut -d' ' -f1)" = "$(sum "$src")" ] ||
		{ echo "px2-push: $f arrived damaged" >&2; exit 1; }
done
echo "px2-push: images on the device, checked"

# On the device: px2-slot.sh's functions, the plan, then the script below
{
	cat "$LIB"
	printf 'RUNNING=%s TARGET=%s PART=%s SYSTEM=%s DTB=%s\n' "$RUNNING" "$TARGET" "$PART" "$SYSTEM" "$DTBNAME"
	cat <<'REMOTE'
set -e
cd /tmp/px2-push
# The plan again, from what the device reads now: written only when it says
# the same as the one the files were sent for
mkdir -p bootpart
mount -t vfat /dev/mmcblk0p1 bootpart
p4=0; bs=0
[ -e /sys/class/block/mmcblk0p4 ] && p4=1
[ -f bootpart/bootstate.txt ] && bs=1
now=$(px2_slot_plan "$(cat /proc/cmdline)" "$p4" "$bs") || { umount bootpart; exit 1; }
[ "$now" = "$RUNNING $TARGET $PART" ] ||
	{ echo "px2-push: the device now says \"$now\": nothing written" >&2; umount bootpart; exit 1; }
# And never the partition the root is mounted from
grep -q "^/dev/$PART " /proc/mounts &&
	{ echo "px2-push: /dev/$PART is mounted: nothing written" >&2; umount bootpart; exit 1; }

/etc/init.d/zyperos stop < /dev/null > /dev/null 2>&1 || true
# A port, or anything else, still holding a file or its folder on the card
# keeps the card from going read-only: asked to stop, then made to
on_card() {
	for p in /proc/[0-9]*; do
		[ "${p#/proc/}" = $$ ] && continue
		case "$(readlink $p/cwd) $(ls -l $p/fd 2> /dev/null)" in
		*/mnt/SDCARD*) echo "${p#/proc/}" ;;
		esac
	done
}
for sig in TERM KILL; do
	pids=$(on_card)
	[ -n "$pids" ] && kill -$sig $pids 2> /dev/null && sleep 1
done
# And what a port mounted: its runtime, a squashfs file on the card on a loop
# device (which keeps the file open to write), and bind mounts of its save
# folders. Newest first, so a mount inside another comes off before it.
awk '$2 ~ /^\/mnt\/SDCARD\// || ($2 ~ /^\/tmp\// && $1 ~ /^\/dev\/loop/) { print $2 }' /proc/mounts |
	sed -n '1!G;h;$p' | while read -r m; do umount "$m" 2> /dev/null || umount -l "$m"; done
for l in /sys/block/loop*/loop/backing_file; do
	[ -f "$l" ] && case "$(cat "$l")" in /mnt/SDCARD/*) d=${l%/loop/backing_file}; losetup -d "/dev/${d##*/}" 2> /dev/null ;; esac
done
sync
mount -o remount,ro /mnt/SDCARD ||
	{ echo "px2-push: the card stayed busy and could not go read-only; nothing written" >&2; umount bootpart; exit 1; }

# bootstate.txt, written whole under another name, synced and renamed over,
# so a cut write leaves the old one, which U-Boot reads as it was
bootstate() { # edit-function args...
	cp bootpart/bootstate.txt bootstate.edit &&
	"$@" &&
	cp bootstate.edit bootpart/bootstate.new &&
	sync &&
	mv bootpart/bootstate.new bootpart/bootstate.txt &&
	sync
}
# Read back past the cache: the bytes the card holds, not the ones written
readback() { # file device-or-file [bytes]
	n=${3:-$(wc -c < "$1")}
	echo 3 > /proc/sys/vm/drop_caches
	got=$(head -c "$n" "$2" | sha256sum | cut -d" " -f1)
	want=$(sha256sum "$1" | cut -d" " -f1)
	[ "$got" = "$want" ]
}
fail() { echo "px2-push: $*; slot $RUNNING still boots" >&2; umount bootpart 2> /dev/null; exit 1; }

# The bootloader first, read back: a wrong one is a device that no longer
# starts, and the only fix is the card in a reader
if [ -f loader ]; then
	n=$(wc -c < loader)
	dd if=loader of=/dev/mmcblk0 bs=512 seek=64 conv=fsync 2> /dev/null
	sync
	echo 3 > /proc/sys/vm/drop_caches
	got=$(dd if=/dev/mmcblk0 bs=512 skip=64 count=$((n / 512)) 2> /dev/null | sha256sum | cut -d" " -f1)
	[ "$got" = "$(sha256sum loader | cut -d" " -f1)" ] || fail "the bootloader reads back wrong"
	echo "px2-push: bootloader written and read back"
fi

# The target slot holds no system from here until it is read back: the
# running slot boots, off trial, whatever happens to the write
bootstate px2_bootstate_before bootstate.edit "$RUNNING" "$TARGET" || fail "cannot write bootstate.txt"
dd if=rootfs of="/dev/$PART" bs=1M conv=fsync 2> /dev/null || fail "writing $PART"
sync
readback rootfs "/dev/$PART" || fail "$PART reads back wrong"
echo "px2-push: $PART written and read back"
# The slot's kernel and device tree, each renamed into place and read back
mkdir -p "bootpart/$TARGET"
for f in Image dtb; do
	name=$f; [ $f = dtb ] && name=$DTB
	cp "$f" "bootpart/$TARGET/$name.new" && sync &&
		mv "bootpart/$TARGET/$name.new" "bootpart/$TARGET/$name" && sync ||
		fail "cannot write /$TARGET/$name"
	readback "$f" "bootpart/$TARGET/$name" || fail "/$TARGET/$name reads back wrong"
done
echo "px2-push: /$TARGET/Image and /$TARGET/$DTB written and read back"
if [ -f boot.scr ]; then
	cp boot.scr bootpart/boot.scr.new && sync && mv bootpart/boot.scr.new bootpart/boot.scr && sync ||
		fail "cannot write boot.scr"
	readback boot.scr bootpart/boot.scr || fail "boot.scr reads back wrong"
	echo "px2-push: boot.scr written and read back"
fi
bootstate px2_bootstate_after bootstate.edit "$TARGET" "$SYSTEM" || fail "cannot write bootstate.txt"
echo "px2-push: slot $TARGET boots next, on trial ($(tr '\n' ' ' < bootpart/bootstate.txt))"
umount bootpart
# The running root was not touched, so a normal restart is safe
(sleep 1; reboot) > /dev/null 2>&1 &
REMOTE
} | $SSH 'cat > /tmp/px2-push/run.sh && sh /tmp/px2-push/run.sh < /dev/null'
echo "px2-push: restarting into slot $TARGET; if it does not come up, slot $RUNNING starts again after three tries"
