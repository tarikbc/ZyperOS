#!/bin/sh
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
# ZyperOS in QEMU on a Mac, standing in for the GKD Pixel 2: plastron's QEMU
# image (`make qemu` there: out/qemu/sdcard.img and Image) booted on QEMU's
# arm64 virt machine under Apple's Hypervisor framework. One VM per name, each
# with its own copy of the card, its own SSH port and its own sockets, so
# several sessions can each run one. docs/qemu.md says what it covers and
# what still needs the device.
#
#   tools/qemu-run.sh start [-n NAME] [--window] [--sound] [--fresh] [--size 4G]
#                           [--image DIR] [--no-test-rom] [--charger]
#   tools/qemu-run.sh ssh   [-n NAME] [COMMAND...]
#   tools/qemu-run.sh shot  [-n NAME] [FILE.png]       (px2-shot, as a PNG)
#   tools/qemu-run.sh dump  [-n NAME] [FILE.png]       (what QEMU's display shows)
#   tools/qemu-run.sh input [-n NAME] BUTTON...        (px2-input's buttons)
#   tools/qemu-run.sh sim   [-n NAME] COMMAND...       (px2sim: battery 15, cable 0,
#                                                       headphones 1, press a, state)
#   tools/qemu-run.sh roms  [-n NAME] DIR              (DIR's NES/, SNES/... into Roms/)
#   tools/qemu-run.sh deploy [-n NAME] PAYLOAD_DIR     (tools/px2-deploy.sh, to the VM)
#   tools/qemu-run.sh stop  [-n NAME]                  (a clean power-off)
#   tools/qemu-run.sh kill  [-n NAME]
#   tools/qemu-run.sh list
#   tools/qemu-run.sh info  [-n NAME]                  (port, sockets, logs)
#   tools/qemu-run.sh prune [NAME... | --all]          (stopped VMs' folders)
#
# NAME defaults to $ZYPEROS_QEMU_NAME, then "zyperos". The card copy and the
# sockets live in $ZYPEROS_QEMU_HOME/NAME (~/Library/Caches/zyperos-qemu).
# The image comes from --image, $ZYPEROS_QEMU_IMAGE, plastron/out/qemu
# beside this repository's main checkout, or $ZYPEROS_QEMU_HOME/image, a copy
# kept for every checkout on the Mac.
#
# A card copy is gigabytes and stays when its VM stops. prune alone lists the
# stopped VMs, with their size and when they were last used; with names it
# removes those, and with --all every stopped one. Never a running VM, and
# never image/, the copy every checkout shares.
#
# start waits for SSH and returns with the VM running in the background,
# headless unless --window (QEMU's own window; the keyboard is the Pixel 2's
# buttons there, see plastron's package/px2sim), silent unless --sound. A
# fresh card gets Roms/NES/ZyperOS Test.nes (tools/qemu/make-test-nes.py).
# ZyperOS's file transfer page (port 80) is on the port info prints.
#
# The battery, the charger, the headphones, the backlight and the side
# lights are simulated (plastron's package/px2sim): `sim` sets and reads
# them. ZyperOS's simulator window (simulator/) shows the VM as a Pixel 2.
#
# start --charger starts it as a charger plugged into an off Pixel 2 does:
# the RK817 powers it on with the plug-in reason (0x40 in ON_SOURCE), U-Boot
# passes zycharge=1, and ZyperOS shows its charging screen (DESIGN.md 11.25).
# QEMU has no PMIC, so the reason goes into /px2sim-on.txt on the card's
# boot partition, which plastron's QEMU boot.scr reads and then sets to 0x02,
# the RK817's after a restart. An image from before that reads nothing, and
# the start is an ordinary one.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
STATE_HOME=${ZYPEROS_QEMU_HOME:-$HOME/Library/Caches/zyperos-qemu}
NAME=${ZYPEROS_QEMU_NAME:-zyperos}
KEY=${PX2_KEY:-$HOME/.ssh/zyperos_pixel2}
# dropbear makes a new host key at every boot of a read-only root: no key to check
SSH_OPTS="-o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
# The image's boot and root partitions: everything from 16 MiB to the
# image's end. The card's own partition table and its games partition after
# them are the card's, and a new image never touches them.
SYSTEM_START_MIB=16

# Root A's entry in an MBR (the second): "start sectors", from a file's
# first sector
root_a() {
	od -A n -t u4 -j 470 -N 8 "$1" | awk '{ print $1, $2 }'
}

die() { echo "qemu-run: $*" >&2; exit 1; }

default_image() {
	common=$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2> /dev/null) || common=$REPO/.git
	beside=$(dirname "$(dirname "$common")")/plastron/out/qemu
	if [ -f "$beside/sdcard.img" ] || [ ! -f "$STATE_HOME/image/sdcard.img" ]; then
		echo "$beside"
	else
		echo "$STATE_HOME/image"
	fi
}

vm_dir() { echo "$STATE_HOME/$NAME"; }

running() {
	pid=$(cat "$(vm_dir)/qemu.pid" 2> /dev/null) || return 1
	kill -0 "$pid" 2> /dev/null
}

port() { cat "$(vm_dir)/ssh.port" 2> /dev/null || die "no VM named $NAME (start it first)"; }

vm_ssh() {
	p=$(port)
	# shellcheck disable=SC2086
	ssh $SSH_OPTS -p "$p" -i "$KEY" root@127.0.0.1 "$@"
}

vm_scp() {
	p=$(port)
	# shellcheck disable=SC2086
	scp -O -q $SSH_OPTS -P "$p" -i "$KEY" "$@"
}

qmp() {
	# One command on the VM's QMP socket, after the capabilities handshake
	printf '{"execute":"qmp_capabilities"}\n%s\n' "$1" |
		nc -U -w 2 "$(vm_dir)/qmp.sock" 2> /dev/null || true
}

# A port from $1 up that no other VM has as its $2 and nothing listens on
free_port() {
	p=$1
	while :; do
		used=
		for f in "$STATE_HOME"/*/"$2"; do
			[ -f "$f" ] && [ "$(cat "$f")" = "$p" ] && [ "$f" != "$(vm_dir)/$2" ] && used=1
		done
		[ -z "$used" ] && ! nc -z 127.0.0.1 "$p" 2> /dev/null && { echo "$p"; return; }
		p=$((p + 1))
	done
}

# The image's system on the card copy: the whole image on a new card, grown
# to its size so datapart has room for the games partition; on an existing
# card only the boot and root partitions, when the image is newer than the
# copy the card last had
prepare_card() {
	dir=$(vm_dir)
	img=$IMAGE/sdcard.img
	stamp=$(stat -f '%m %z' "$img")
	if [ -n "$FRESH" ] || [ ! -f "$dir/card.img" ]; then
		rm -f "$dir/card.img" "$dir/image.stamp"
		cp -c "$img" "$dir/card.img" 2> /dev/null || cp "$img" "$dir/card.img"
		qemu-img resize -q -f raw "$dir/card.img" "$SIZE"
		NEW_CARD=1
	elif [ "$(cat "$dir/image.stamp" 2> /dev/null)" != "$stamp" ]; then
		# Boot and root A, to the end of root A as the image has it: the
		# card's slots must be the image's (their size is fixed when a card
		# is laid out: 512 MB since 2026-11-06), or root A would run into
		# what follows it
		set -- $(root_a "$img")
		[ "$(root_a "$dir/card.img")" = "$1 $2" ] ||
			die "$NAME's card has system slots of another size than this image's (root A $(root_a "$dir/card.img"), the image's $1 $2 in sectors): start it with --fresh, which makes a new card"
		echo "qemu-run: a newer image: its system goes on $NAME's card, the games partition stays"
		dd if="$img" of="$dir/card.img" bs=1m skip=$SYSTEM_START_MIB seek=$SYSTEM_START_MIB \
			count=$((($1 + $2) / 2048 - SYSTEM_START_MIB)) conv=notrunc 2> /dev/null
	fi
	echo "$stamp" > "$dir/image.stamp"
	cp "$IMAGE/Image" "$dir/Image"
	# U-Boot, when the image has it (plastron's make qemu): it runs the card's
	# boot.scr, so the VM starts the system slot bootstate.txt names, as the
	# Pixel 2 does (docs/updates.md). Without it QEMU loads root A's kernel.
	# An if, not a test and &&: as the function's last command, a false test
	# was its status, and set -e ended start there, saying nothing, for every
	# image from before U-Boot.
	rm -f "$dir/u-boot.bin"
	if [ -f "$IMAGE/u-boot.bin" ]; then cp "$IMAGE/u-boot.bin" "$dir/u-boot.bin"; fi
}

wait_ssh() {
	i=0
	while [ $i -lt "${WAIT:-120}" ]; do
		running || die "QEMU exited; see $(vm_dir)/serial.log and $(vm_dir)/qemu.log"
		vm_ssh true < /dev/null 2> /dev/null && return 0
		sleep 1
		i=$((i + 1))
	done
	die "no SSH after ${i}s; see $(vm_dir)/serial.log"
}

# Whether ZyperOS is on the card yet: the first boot copies it there from the
# image after making the games partition
wait_zyperos() {
	i=0
	while [ $i -lt "${WAIT:-120}" ]; do
		vm_ssh 'test -f /mnt/SDCARD/ZyperOS/launch.sh && pidof zyperos.elf' > /dev/null 2>&1 &&
			return 0
		sleep 1
		i=$((i + 1))
	done
	echo "qemu-run: ZyperOS hasn't started after ${i}s; see 'qemu-run.sh ssh cat /mnt/SDCARD/.userdata/pixel2/logs/zyperos.log'" >&2
}

# The power-on reason the card's next start reads, as the RK817's ON_SOURCE
# (see start --charger above): written into the boot partition of the card
# copy while QEMU is not using it, the copy attached with diskutil (hdiutil
# before macOS had diskutil image) without mounting, its first partition
# mounted at a folder of the VM's, as the user, and all let go after
power_on_reason() {
	dir=$(vm_dir)
	att=$(diskutil image attach --noMount "$dir/card.img" 2> /dev/null ||
	      hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$dir/card.img" 2> /dev/null) ||
		die "could not attach $dir/card.img to write its power-on reason"
	dev=$(printf '%s\n' "$att" | awk '/^\/dev\/disk[0-9]+[ \t]/ { print $1; exit }')
	[ -n "$dev" ] || die "attaching $dir/card.img gave no disk"
	mkdir -p "$dir/bootpart"
	ok=
	if diskutil mount -mountPoint "$dir/bootpart" "${dev}s1" > /dev/null; then
		printf 'on_source=%s\n' "$1" > "$dir/bootpart/px2sim-on.txt" && ok=1
		diskutil unmount "$dir/bootpart" > /dev/null
	fi
	diskutil eject "$dev" > /dev/null 2>&1 || hdiutil detach -quiet "$dev"
	[ -n "$ok" ] || die "could not write the power-on reason onto $NAME's boot partition"
}

cmd_start() {
	WINDOW= SOUND= FRESH= SIZE=4G IMAGE=${ZYPEROS_QEMU_IMAGE:-} TEST_ROM=1 NEW_CARD= CHARGER=
	while [ $# -gt 0 ]; do
		case $1 in
		--window) WINDOW=1 ;;
		--sound) SOUND=1 ;;
		--fresh) FRESH=1 ;;
		--size) SIZE=$2; shift ;;
		--image) IMAGE=$2; shift ;;
		--no-test-rom) TEST_ROM= ;;
		--charger) CHARGER=1 ;;
		*) die "start: unknown option $1" ;;
		esac
		shift
	done
	[ -n "$IMAGE" ] || IMAGE=$(default_image)
	[ -f "$IMAGE/sdcard.img" ] && [ -f "$IMAGE/Image" ] ||
		die "no sdcard.img and Image in $IMAGE: run 'make qemu' in plastron, or pass --image DIR"
	command -v qemu-system-aarch64 > /dev/null || die "no qemu-system-aarch64: brew install qemu"
	[ -f "$KEY" ] || die "no SSH key $KEY (PX2_KEY): plastron's image takes local/authorized_keys"
	running && die "$NAME is running already (port $(port)); stop it first, or pick another -n NAME"

	dir=$(vm_dir)
	mkdir -p "$dir"
	# QEMU's own sockets from a run before; the simulator window's app.sock
	# is the window's, and stays
	rm -f "$dir/vnc.sock" "$dir/qmp.sock" "$dir/serial.sock" "$dir/sim.sock"
	prepare_card
	if [ -n "$CHARGER" ]; then
		power_on_reason 0x40
		echo "qemu-run: $NAME starts as a charger plugged in while it was off would start it"
	fi
	p=$(free_port 2222 ssh.port)
	echo "$p" > "$dir/ssh.port"
	web=$(free_port 8222 web.port)
	echo "$web" > "$dir/web.port"

	if [ -n "$WINDOW" ]; then
		display="-display cocoa,zoom-to-fit=on"
	else
		display="-display none"
	fi
	if [ -n "$SOUND" ]; then
		audio="-audiodev coreaudio,id=snd"
	else
		audio="-audiodev none,id=snd"
	fi
	# The last run's console kept beside this one's, for a shutdown or a
	# crash looked at after the fact
	[ -f "$dir/serial.log" ] && mv -f "$dir/serial.log" "$dir/serial.log.1"
	: > "$dir/serial.log"
	# The Pixel 2's four cores and 1 GB. The display is 640x480, landscape:
	# ZyperOS and Diatom draw on it unturned (docs/qemu.md). The sound card
	# plays only (streams=1): with a microphone stream as well, CoreAudio
	# refused it and QEMU said so at every start.
	# shellcheck disable=SC2086
	# How it starts, as the positional arguments (the -append line has spaces).
	# Through U-Boot on QEMU's own CPU (TCG): under Apple's hypervisor,
	# U-Boot writing into the flash it starts from stops QEMU (hvf's "isv"
	# assertion). Slower, but it is the card's own boot.scr that runs.
	if [ -f "$dir/u-boot.bin" ]; then
		set -- -accel tcg,thread=multi -cpu max -bios "$dir/u-boot.bin"
		accel=
		WAIT=900
	else
		set -- -kernel "$dir/Image" \
			-append "root=/dev/vda2 rootfstype=squashfs ro rootwait console=ttyAMA0 panic=30"
		accel="-accel hvf -cpu host"
	fi
	nohup qemu-system-aarch64 \
		-name "ZyperOS ($NAME)" \
		-machine virt $accel -smp 4 -m 1024 \
		"$@" \
		-drive if=none,id=card,file="$dir/card.img",format=raw,cache=writeback \
		-device virtio-blk-pci,drive=card \
		-netdev user,id=net,hostfwd=tcp:127.0.0.1:$p-:22,hostfwd=tcp:127.0.0.1:$web-:80 \
		-device virtio-net-pci,netdev=net \
		-device virtio-gpu-pci,xres=640,yres=480 \
		-device virtio-keyboard-pci \
		$audio -device virtio-sound-pci,audiodev=snd,streams=1 \
		-device virtio-rng-pci \
		-device virtio-serial-pci \
		-chardev socket,id=sim,path="$dir/sim.sock",server=on,wait=off \
		-device virtserialport,chardev=sim,name=org.zyperos.px2sim \
		-chardev socket,id=serial,path="$dir/serial.sock",server=on,wait=off,logfile="$dir/serial.log" \
		-serial chardev:serial \
		-qmp unix:"$dir/qmp.sock",server=on,wait=off \
		-vnc unix:"$dir/vnc.sock" \
		-monitor none -parallel none \
		$display > "$dir/qemu.log" 2>&1 &
	echo $! > "$dir/qemu.pid"
	echo "qemu-run: $NAME booting (SSH on 127.0.0.1:$p, serial log $dir/serial.log)"
	wait_ssh
	wait_zyperos
	if [ -n "$NEW_CARD" ] && [ -n "$TEST_ROM" ]; then
		python3 "$HERE/qemu/make-test-nes.py" "$dir/test.nes"
		vm_ssh 'mkdir -p /mnt/SDCARD/Roms/NES && cat > "/mnt/SDCARD/Roms/NES/ZyperOS Test.nes"' \
			< "$dir/test.nes"
		vm_ssh 'sync; kill $(pidof zyperos.elf) 2> /dev/null; true'
	fi
	echo "qemu-run: $NAME is up: tools/qemu-run.sh ssh -n $NAME"
}

cmd_shot() {
	out=${1:-$(vm_dir)/shot.png}
	vm_ssh 'px2-shot /tmp/qemu-shot.ppm > /dev/null' || die "px2-shot failed"
	vm_scp "root@127.0.0.1:/tmp/qemu-shot.ppm" "$(vm_dir)/shot.ppm"
	sips -s format png "$(vm_dir)/shot.ppm" --out "$out" > /dev/null || die "couldn't make $out"
	echo "$out"
}

cmd_dump() {
	out=${1:-$(vm_dir)/dump.png}
	case $out in /*) ;; *) out=$PWD/$out ;; esac
	qmp "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$out\",\"format\":\"png\"}}" > /dev/null
	[ -f "$out" ] || die "no screen dump (is $NAME running?)"
	echo "$out"
}

cmd_roms() {
	src=${1:?usage: qemu-run.sh roms [-n NAME] DIR}
	[ -d "$src" ] || die "no folder $src"
	for d in "$src"/*/; do
		[ -d "$d" ] || continue
		sys=$(basename "$d")
		# Through tar, so names with spaces and quotes arrive as they are
		COPYFILE_DISABLE=1 tar -C "$d" -cf - . |
			vm_ssh "mkdir -p '/mnt/SDCARD/Roms/$sys' && tar -C '/mnt/SDCARD/Roms/$sys' -xf -"
		echo "  $sys"
	done
	# The launcher reads the shelves when it starts
	vm_ssh 'sync; kill $(pidof zyperos.elf) 2> /dev/null; true'
}

cmd_stop() {
	running || { echo "qemu-run: $NAME isn't running"; return 0; }
	vm_ssh poweroff < /dev/null 2> /dev/null || qmp '{"execute":"system_powerdown"}' > /dev/null
	i=0
	while running && [ $i -lt 30 ]; do sleep 0.5; i=$((i + 1)); done
	if running; then
		qmp '{"execute":"quit"}' > /dev/null
		sleep 1
	fi
	running && kill "$(cat "$(vm_dir)/qemu.pid")"
	echo "qemu-run: $NAME stopped"
}

# A VM's folder has the SSH port start gave it. image/, the image kept for
# every checkout, has none, nor has anything else that is not a VM.
is_vm() { [ -f "$STATE_HOME/$1/ssh.port" ]; }

vm_size() { du -sh "$1" 2> /dev/null | awk '{ print $1 }'; }

# When anything in a VM's folder last changed: a start, or the card written
last_used() {
	newest=$(ls -t "$1" | head -1)
	stat -f '%Sm' -t '%Y-%m-%d %H:%M' "$1/$newest"
}

stopped_vms() {
	for d in "$STATE_HOME"/*/; do
		NAME=$(basename "$d")
		is_vm "$NAME" && ! running && echo "$NAME"
	done
	return 0
}

cmd_prune() {
	all=
	for a in "$@"; do [ "$a" = --all ] && all=1; done
	[ -z "$all" ] || [ $# -eq 1 ] || die "prune: names or --all, not both"
	if [ $# -eq 0 ]; then
		names=$(stopped_vms)
		[ -n "$names" ] || { echo "qemu-run: no stopped VMs in $STATE_HOME"; return 0; }
		printf '%-20s %6s  %s\n' NAME SIZE "LAST USED"
		for NAME in $names; do
			printf '%-20s %6s  %s\n' "$NAME" "$(vm_size "$(vm_dir)")" "$(last_used "$(vm_dir)")"
		done
		echo "qemu-run: remove with prune NAME..., or every stopped one with prune --all"
		return 0
	fi
	if [ -n "$all" ]; then
		# shellcheck disable=SC2046
		set -- $(stopped_vms)
		[ $# -gt 0 ] || { echo "qemu-run: no stopped VMs in $STATE_HOME"; return 0; }
	fi
	bad=
	for NAME in "$@"; do
		case $NAME in
		'' | . | .. | */* | image)
			echo "qemu-run: prune: '$NAME' is not a VM's name" >&2; bad=1; continue ;;
		esac
		if ! is_vm "$NAME"; then
			echo "qemu-run: prune: no VM named $NAME" >&2; bad=1; continue
		fi
		if running; then
			echo "qemu-run: prune: $NAME is running; stop it first" >&2; bad=1; continue
		fi
		size=$(vm_size "$(vm_dir)")
		rm -rf "$(vm_dir)"
		echo "qemu-run: $NAME removed ($size)"
	done
	[ -z "$bad" ]
}

cmd_list() {
	for d in "$STATE_HOME"/*/; do
		[ -f "$d/ssh.port" ] || continue   # image/, and anything else not a VM
		NAME=$(basename "$d")
		if running; then state=running; else state=stopped; fi
		printf '%-20s %-8s ssh 127.0.0.1:%s\n' "$NAME" "$state" "$(cat "$d/ssh.port" 2> /dev/null)"
	done
}

[ $# -gt 0 ] || { sed -n '3,44p' "$0"; exit 1; }
CMD=$1
shift
if [ "${1:-}" = -n ]; then NAME=${2:?-n needs a name}; shift 2; fi

case $CMD in
start) cmd_start "$@" ;;
ssh) vm_ssh "$@" ;;
shot) cmd_shot "$@" ;;
dump) cmd_dump "$@" ;;
input) vm_ssh px2-input "$@" ;;
sim) vm_ssh px2sim "$@" ;;
roms) cmd_roms "$@" ;;
deploy) PX2_HOST=127.0.0.1 PX2_PORT=$(port) PX2_KEY=$KEY sh "$HERE/px2-deploy.sh" "$@" ;;
stop) cmd_stop ;;
kill) running && kill "$(cat "$(vm_dir)/qemu.pid")"; echo "qemu-run: $NAME killed" ;;
list) cmd_list ;;
prune) cmd_prune "$@" ;;
info)
	dir=$(vm_dir)
	echo "ssh     127.0.0.1:$(port) (key $KEY)"
	echo "web     http://127.0.0.1:$(cat "$dir/web.port" 2> /dev/null) (ZyperOS's file transfer)"
	echo "sim     $dir/sim.sock (px2sim's line protocol)"
	echo "card    $dir/card.img"
	echo "serial  $dir/serial.sock (nc -U), log $dir/serial.log"
	echo "qmp     $dir/qmp.sock"
	echo "vnc     $dir/vnc.sock"
	echo "qemu    $dir/qemu.log"
	;;
*) die "unknown command $CMD (start ssh shot dump input sim roms deploy stop kill list info prune)" ;;
esac
