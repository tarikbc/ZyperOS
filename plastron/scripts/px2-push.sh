#!/bin/sh
# Put a freshly built system on a GKD Pixel 2 over the USB cable, without
# taking the card out: `make push` (and `make push BOOT=1` for the kernel and
# device tree too, `make push LOADER=1` for the bootloader). Needs a development build's SSH key (local/authorized_keys)
# on the card already, and the device on the cable at 10.42.0.1.
#
# The device writes its own card. That is the point: a Mac refuses a raw write
# to a card reader from most tools ("Operation not permitted" even as root,
# its Removable Volumes privacy rule), and the card's games partition must
# never be part of a write anyway. Only the system partition (p2), with
# BOOT=1 the boot partition (p1), and with LOADER=1 the bootloader (from
# sector 64 to where p1 starts) are written; the games partition (p3) is
# made read-only first.
#
# On the device, in order, while the old system is still whole: ZyperOS is
# stopped, the card's games partition goes read-only, and busybox is copied
# into RAM. Then the image is written with that busybox, read back and
# compared, and the device restarts with reboot -f, since a normal shutdown
# would run scripts out of the root it was just written over.
#
#   [PX2_LOADER=u-boot-px2.bin] scripts/px2-push.sh rootfs.squashfs [boot.vfat]
set -eu

HOST=${PX2_HOST:-10.42.0.1}
KEY=${PX2_KEY:-$HOME/.ssh/zyperos_pixel2}
ROOTFS=${1:?usage: px2-push.sh rootfs.squashfs [boot.vfat]}
BOOTFS=${2:-}
LOADER=${PX2_LOADER:-}
# A development device on the cable: plastron's root is read-only, so
# dropbear makes a new host key at every boot and there is no key to check
# against. The device is on a cable of its own, at an address only it has.
OPTS="-o ConnectTimeout=8 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SSH="ssh $OPTS ${KEY:+-i $KEY} root@$HOST"
SCP="scp -O -q $OPTS ${KEY:+-i $KEY}"

sum() { shasum -a 256 "$1" | cut -d' ' -f1; }

$SSH 'grep -q "gamekiddy,gkd-pixel2" /proc/device-tree/compatible' ||
	{ echo "px2-push: $HOST is not a GKD Pixel 2" >&2; exit 1; }

check_size() { # image partition
	have=$($SSH "echo \$((\$(cat /sys/class/block/$2/size) * 512))")
	need=$(wc -c < "$1" | tr -d ' ')
	[ "$need" -le "$have" ] || { echo "px2-push: $1 is $need bytes, $2 holds $have" >&2; exit 1; }
}
check_size "$ROOTFS" mmcblk0p2
[ -z "$BOOTFS" ] || check_size "$BOOTFS" mmcblk0p1
# The bootloader goes at sector 64 and must end before p1 starts
if [ -n "$LOADER" ]; then
	p1=$($SSH 'cat /sys/class/block/mmcblk0p1/start')
	need=$(wc -c < "$LOADER" | tr -d ' ')
	[ $((need % 512)) -eq 0 ] && [ $((64 + need / 512)) -le "$p1" ] ||
		{ echo "px2-push: $LOADER is $need bytes, more than the $(((p1 - 64) * 512)) before p1" >&2; exit 1; }
fi

$SSH 'rm -rf /tmp/px2-push && mkdir -p /tmp/px2-push'
$SCP "$ROOTFS" "root@$HOST:/tmp/px2-push/rootfs"
[ -z "$BOOTFS" ] || $SCP "$BOOTFS" "root@$HOST:/tmp/px2-push/boot"
[ -z "$LOADER" ] || $SCP "$LOADER" "root@$HOST:/tmp/px2-push/loader"
for f in rootfs ${BOOTFS:+boot} ${LOADER:+loader}; do
	src=$ROOTFS; [ "$f" = boot ] && src=$BOOTFS; [ "$f" = loader ] && src=$LOADER
	[ "$($SSH "sha256sum /tmp/px2-push/$f" | cut -d' ' -f1)" = "$(sum "$src")" ] ||
		{ echo "px2-push: $f arrived damaged" >&2; exit 1; }
done
echo "px2-push: images on the device, checked"

$SSH 'set -e
cd /tmp/px2-push
/etc/init.d/zyperos stop > /dev/null 2>&1 || true
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
awk "\$2 ~ /^\/mnt\/SDCARD\// || (\$2 ~ /^\/tmp\// && \$1 ~ /^\/dev\/loop/) { print \$2 }" /proc/mounts |
	sed -n "1!G;h;\$p" | while read -r m; do umount "$m" 2> /dev/null || umount -l "$m"; done
for l in /sys/block/loop*/loop/backing_file; do
	[ -f "$l" ] && case "$(cat "$l")" in /mnt/SDCARD/*) d=${l%/loop/backing_file}; losetup -d "/dev/${d##*/}" 2> /dev/null ;; esac
done
sync
mount -o remount,ro /mnt/SDCARD ||
	{ echo "px2-push: the card stayed busy and could not go read-only; nothing written" >&2; exit 1; }
cp /bin/busybox ./busybox
write() { # file partition
	n=$(./busybox wc -c < "$1")
	./busybox dd if="$1" of="/dev/$2" bs=1M conv=fsync 2> /dev/null
	./busybox sync
	got=$(./busybox head -c "$n" "/dev/$2" | ./busybox sha256sum | ./busybox cut -d" " -f1)
	want=$(./busybox sha256sum "$1" | ./busybox cut -d" " -f1)
	[ "$got" = "$want" ] || { echo "px2-push: $2 reads back wrong" >&2; exit 1; }
	echo "px2-push: $2 written and read back"
}
# The bootloader first, while the old system is whole, and read back past the
# page cache: a wrong one is a device that no longer starts, and the only fix
# is the card in a reader
if [ -f loader ]; then
	n=$(./busybox wc -c < loader)
	./busybox dd if=loader of=/dev/mmcblk0 bs=512 seek=64 conv=fsync 2> /dev/null
	./busybox sync
	echo 3 > /proc/sys/vm/drop_caches
	got=$(./busybox dd if=/dev/mmcblk0 bs=512 skip=64 count=$((n / 512)) 2> /dev/null | ./busybox sha256sum | ./busybox cut -d" " -f1)
	want=$(./busybox sha256sum loader | ./busybox cut -d" " -f1)
	[ "$got" = "$want" ] || { echo "px2-push: the bootloader reads back wrong" >&2; exit 1; }
	echo "px2-push: bootloader written and read back"
fi
[ -f boot ] && write boot mmcblk0p1
write rootfs mmcblk0p2
# A card with two system slots (docs/updates.md in ZyperOS) may be running
# slot b after a system update: root A, just written, boots only once
# bootstate.txt says slot a. A new boot partition (BOOT=1) brings its own.
if [ ! -f boot ]; then
	./busybox mkdir -p bootpart
	if ./busybox mount -t vfat /dev/mmcblk0p1 bootpart 2> /dev/null; then
		if [ -f bootpart/bootstate.txt ]; then
			./busybox sed -i -e "s/^slot=.*/slot=a/" -e "s/^trial=.*/trial=0/" \
				-e "s/^try=.*/try=0/" bootpart/bootstate.txt
			echo "px2-push: slot a set to boot"
		fi
		./busybox umount bootpart
	fi
fi
(./busybox sleep 1; ./busybox reboot -f) > /dev/null 2>&1 &
'
echo "px2-push: restarting; the new system is up in about ten seconds"
