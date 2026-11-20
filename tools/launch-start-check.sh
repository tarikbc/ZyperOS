#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# launch.sh's start on a card a power cut broke, on each device: a copy of
# the card script (sd/zyperos/launch.sh) and of each device's device.sh beside
# it, with the card's paths, /tmp and the system's own file (/etc/zyperos/
# system, its device line: the Brick's device.sh is the Smart Pro's too) moved
# under a scratch folder and the device's commands stubbed (poweroff, reboot,
# pidof), run by sh. Each device's log is looked for in its own folder
# (.userdata/<id>), so a device.sh that names another's fails here.
#
#   1. boot.env is junk (what a power cut can leave): read, not sourced, so
#      the script goes on to the launcher, where a syntax error in a sourced
#      file ended it before ZyperOS (2026-11-15).
#   2. the launcher stops at once, five times: launch.sh is set aside, so the
#      next start copies ZyperOS again (plastron's zyperos.init), a marker
#      says so, and the handheld goes off.
#   3. it does again after that copy: left as it is, and off.
#   4. a power-off asked for (the launcher's /tmp/zyperos_poweroff) takes the
#      marker away.
#   5. launch.sh with no device.sh beside it (a file the card check took
#      away): launch.sh is set aside as in 2, and the handheld goes off,
#      where `.` on the missing file would have ended the script before
#      ZyperOS.
#   6. on the Pixel 2, the kernel's line about the battery's level at the
#      start (plastron's 9-px2-rk817-start-soc.patch) is in the start's log.
#   7. a fresh install's first start copies your PICO-8, which ZyperOS
#      Installer left in the boot partition's pico8/, into Bios/PICO-8; a
#      later start never reads the boot partition, and a first start with
#      no PICO-8 there makes nothing.
D=${TMPDIR:-/tmp}/zyperos-launch-check.$$
trap 'rm -rf "$D"' EXIT
fails=0
fail() { echo "FAIL $dev: $*"; fails=$((fails + 1)); }

# The copies of launch.sh and device.sh, and a check that each edit found its
# line in launch.sh: an edit that finds nothing would test the device's paths
# instead. device.sh names the card only through launch.sh's SDCARD.
edit() {
	sed -e "s#/mnt/SDCARD#$D/card#g" -e "s#/usr/share/zyperos/card#$D/seed#g" \
	    -e "s#/tmp/#$D/tmp/#g" -e "s#/dev/kmsg#$D/kmsg#g" -e 's#( sleep 8$#( exit#' \
	    -e "s#/etc/zyperos/system#$D/system#g" "$1"
}
prepare() {
	rm -rf "$D"
	mkdir -p "$D/card/ZyperOS" "$D/card/.userdata/$dev" "$D/seed/ZyperOS" "$D/bin" "$D/tmp"
	printf 'version 1.0.6\ndevice %s\n' "$dev" > "$D/system"
	for p in '/mnt/SDCARD' '/usr/share/zyperos/card' '( sleep 8$' '/dev/kmsg' \
	         '^device_radios &$'; do
		grep -q -- "$p" sd/zyperos/launch.sh || { fail "launch.sh no longer has '$p'"; return 1; }
	done
	edit sd/zyperos/launch.sh > "$D/card/ZyperOS/launch.sh"
	# The radios: none here. The Pixel 2's Wi-Fi follower loops for good in
	# the background; the Brick's radios start only with boot.env saying so,
	# which junk does not
	{ edit "sd/$dev/device.sh"; echo 'device_radios() { :; }'; } > "$D/card/ZyperOS/device.sh"
	cp "$D/card/ZyperOS/launch.sh" "$D/card/ZyperOS/device.sh" "$D/seed/ZyperOS/"
	for c in poweroff reboot; do
		printf '#!/bin/sh\necho %s >> "%s/calls"\n' "$c" "$D" > "$D/bin/$c"
	done
	printf '#!/bin/sh\nexit 1\n' > "$D/bin/pidof"
	# The kernel's log, with the gauge's two lines among others: the level,
	# and the full charge it measured (9-px2-rk817-wear.patch)
	printf '#!/bin/sh\necho "[    0.5] mmc0: new card"\necho "[    1.2] %s"\necho "[    1.2] %s"\necho "[    1.3] usb 1-1: new device"\n' \
	       'rk817-charger rk817-charger: battery at start: 49% from the voltage; off 120 min, on 0x80, sampled 3815 mV at -250 mA, at rest 3843 mV (49%), counter 85%' \
	       'rk817-charger rk817-charger: battery at start: full charge 1565 mAh of the design'"'"'s 1730, was 1730, measures 1; measured now: 700 mAh out since full, to 49% at rest' \
	       > "$D/bin/dmesg"
	printf '#!/bin/sh\nexit 0\n' > "$D/card/ZyperOS/diatom"
	chmod +x "$D"/bin/* "$D/card/ZyperOS/diatom"
}

# The launcher: $1 is what it does, "stop" (at once, an error) or "off" (a
# power-off asked for)
launcher() {
	if [ "$1" = off ]; then
		printf '#!/bin/sh\n: > "%s/tmp/zyperos_poweroff"\nexit 0\n' "$D" > "$D/card/ZyperOS/zyperos.elf"
	else
		printf '#!/bin/sh\necho "the launcher stops"\nexit 1\n' > "$D/card/ZyperOS/zyperos.elf"
	fi
	chmod +x "$D/card/ZyperOS/zyperos.elf"
}

run() {
	rm -f "$D/calls"
	PATH="$D/bin:$PATH" sh "$D/card/ZyperOS/launch.sh" < /dev/null > "$D/out" 2>&1
	LOG=$D/card/.userdata/$dev/logs/zyperos.log
}

for dev in pixel2 brick smartpro; do
	prepare || continue
	U=$D/card/.userdata/$dev
	printf "WIFI='1\nBLU\001\377'\"\`)(" > "$U/boot.env"
	launcher stop
	run
	grep -q 'the launcher stops' "$LOG" 2>/dev/null ||
		fail "the launcher never ran with a broken boot.env: $(tail -n 3 "$D/out")"
	[ "$(grep -c 'the launcher stops' "$LOG" 2>/dev/null)" = 5 ] ||
		fail "the launcher ran $(grep -c 'the launcher stops' "$LOG" 2>/dev/null) times, not 5"
	[ ! -f "$D/card/ZyperOS/launch.sh" ] && [ -f "$D/card/ZyperOS/launch.sh.failed" ] ||
		fail "launch.sh was not set aside after five fast stops"
	[ -f "$U/start-failed" ] || fail "no start-failed marker"
	grep -q 'copies ZyperOS onto the card again' "$LOG" || fail "the log does not say a copy comes"
	grep -q poweroff "$D/calls" 2>/dev/null || fail "no power-off after five fast stops"
	# The battery's line, copied in the background: a moment for it
	if [ "$dev" = pixel2 ]; then
		i=0
		while [ $i -lt 20 ] && ! grep -q '^kernel: battery at start: ' "$LOG"; do
			sleep 0.1
			i=$((i + 1))
		done
		[ "$(grep -c '^kernel: battery at start: 49% from the voltage; off 120 min, on 0x80, sampled 3815 mV at -250 mA, at rest 3843 mV (49%), counter 85%$' "$LOG")" = 1 ] ||
			fail "the start's log has not the kernel's battery line once: $(grep battery "$LOG")"
		[ "$(grep -c "^kernel: battery at start: full charge 1565 mAh of the design's 1730, was 1730, measures 1; measured now: 700 mAh out since full, to 49% at rest$" "$LOG")" = 1 ] ||
			fail "the start's log has not the kernel's full charge line once: $(grep battery "$LOG")"
	fi

	# The next start's copy, as zyperos.init makes it: launch.sh back
	cp "$D/seed/ZyperOS/launch.sh" "$D/card/ZyperOS/launch.sh"
	run
	[ -f "$D/card/ZyperOS/launch.sh" ] || fail "launch.sh set aside a second time"
	grep -q 'after a fresh copy too; left as it is' "$LOG" || fail "the second time is not said"
	grep -q poweroff "$D/calls" 2>/dev/null || fail "no power-off the second time"

	launcher off
	run
	[ ! -f "$U/start-failed" ] || fail "a power-off asked for left the marker"

	# device.sh gone: launch.sh set aside for the next start's copy, and off,
	# with the launcher never started
	rm -f "$D/card/ZyperOS/device.sh" "$D/card/ZyperOS/launch.sh.failed" "$LOG"
	launcher stop
	run
	[ ! -f "$D/card/ZyperOS/launch.sh" ] && [ -f "$D/card/ZyperOS/launch.sh.failed" ] ||
		fail "launch.sh was not set aside with no device.sh: $(tail -n 3 "$D/out")"
	grep -q 'the next start copies ZyperOS' "$D/kmsg" 2> /dev/null ||
		fail "the kernel's log does not say a copy comes with no device.sh"
	grep -q poweroff "$D/calls" 2>/dev/null || fail "no power-off with no device.sh"
	! grep -q 'the launcher stops' "$LOG" 2>/dev/null ||
		fail "the launcher ran with no device.sh"
	[ "$fails" = 0 ] && echo "ok $dev: a junk boot.env read as off, five fast stops or no device.sh set launch.sh aside once"

	# A fresh install's first start: your PICO-8, which ZyperOS Installer
	# left in the boot partition's pico8/, copied into Bios/PICO-8. The boot
	# partition is a folder here: the Pixel 2 mounts it (mount, stubbed,
	# copies the folder to the mount point), the Brick has it at /boot, and
	# so has the Smart Pro, whose device.sh is the Brick's
	prepare || continue
	U=$D/card/.userdata/$dev
	rm -rf "$U"
	mkdir -p "$D/bootfs/pico8"
	printf 'program' > "$D/bootfs/pico8/pico8_64"
	printf 'data' > "$D/bootfs/pico8/pico8.dat"
	echo 0.2.7 > "$D/bootfs/pico8/version.txt"
	printf '#!/bin/sh\nfor a; do m=$a; done\necho mount >> "%s/calls"\ncp -R "%s/bootfs/." "$m/"\n' "$D" "$D" > "$D/bin/mount"
	printf '#!/bin/sh\nfor a; do m=$a; done\necho umount >> "%s/calls"\nrm -rf "$m/"*\n' "$D" > "$D/bin/umount"
	chmod +x "$D/bin/mount" "$D/bin/umount"
	if [ "$dev" = brick ] || [ "$dev" = smartpro ]; then
		grep -q '\[ -d /boot \] && BOOT_DIR=/boot' "$D/card/ZyperOS/device.sh" ||
			fail "device.sh no longer has its /boot"
		sed "s#\[ -d /boot \] \&\& BOOT_DIR=/boot#[ -d $D/bootfs ] \&\& BOOT_DIR=$D/bootfs#" \
			"$D/card/ZyperOS/device.sh" > "$D/device.sh" && mv "$D/device.sh" "$D/card/ZyperOS/device.sh"
	fi
	launcher off
	run
	[ "$(cat "$D/card/Bios/PICO-8/pico8_64" 2> /dev/null)" = program ] &&
		[ "$(cat "$D/card/Bios/PICO-8/pico8.dat" 2> /dev/null)" = data ] &&
		[ "$(cat "$D/card/Bios/PICO-8/version.txt" 2> /dev/null)" = 0.2.7 ] ||
		fail "the first start did not copy PICO-8 into Bios/PICO-8: $(tail -n 3 "$D/out")"
	[ -x "$D/card/Bios/PICO-8/pico8_64" ] || fail "pico8_64 was copied not runnable"
	grep -q 'pico8: your PICO-8 copied from the boot partition into Bios/PICO-8' "$LOG" ||
		fail "the log does not say PICO-8 was copied"
	if [ "$dev" = pixel2 ]; then
		grep -q umount "$D/calls" 2> /dev/null || fail "the boot partition was not let go of"
	fi
	# Any later start, the boot partition never read again
	printf 'newer' > "$D/bootfs/pico8/pico8_64"
	rm -f "$D/card/Bios/PICO-8/pico8_64"
	run
	[ ! -f "$D/card/Bios/PICO-8/pico8_64" ] && ! grep -q mount "$D/calls" 2> /dev/null ||
		fail "a start after the first read the boot partition"
	# A first start with no PICO-8 in the boot partition: nothing made
	rm -rf "$U" "$D/bootfs/pico8" "$D/card/Bios/PICO-8"
	run
	[ ! -d "$D/card/Bios/PICO-8" ] || fail "a first start with no PICO-8 made Bios/PICO-8"
	rm -rf "$D/bootfs"
	[ "$fails" = 0 ] && echo "ok $dev: a fresh install's PICO-8 copied from the boot partition at the first start only"
done
[ "$fails" = 0 ] || exit 1
