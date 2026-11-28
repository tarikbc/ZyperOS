#!/bin/sh
# The R36S's part of ZyperOS's card script: sourced by launch.sh
# (sd/zyperos/launch.sh) before anything else, never run. mk/payload.sh puts
# it beside launch.sh as ZyperOS/device.sh.
#
# A test card (docs/devices/r36s.md): the GKD Pixel 2's (sd/pixel2/
# device.sh), whose system it runs, with what the R36S's first card leaves
# out until each is tried on one. It has no radio of its own either; a USB
# Wi-Fi adapter on its OTG port is followed as on the Pixel 2.

export USERDATA_PATH=$SDCARD/.userdata/r36s

# Off on the first test card (launch.sh lists them all):
HAS_PORTS=0        # ports need portkeys, which reads the Pixel 2's MENU code alone
HAS_PORTMASTER=0   # and PortMaster with them
HAS_PORTKEYS=0     # not in its payload (src/device/r36s_build.c)
HAS_PORT_STILL=0   # the splash's still is for a port's start
HAS_PORT_TURN=0    # its panel is not turned
HAS_ZYHOME=1       # MENU+START held: zyhome knows its FN (tools/zyhome.c)
HAS_USBDRIVE=0     # Flash Drive: its OTG port as a USB device is untried
HAS_OFF_PENDING=1  # its power-off has the Pixel 2's last step (plastron's lastoff)
# The equalizer over a port's sound, as on the Pixel 2 (the same RK817 dmix)
EQ_SLAVE=mixed
EQ_SLAVE_USB=usb

# The boot partition, read-only: the card's first partition, mmcblk0p1, the
# R36S's boot card being mmcblk0 (plastron's board/r36s/dts says why)
device_boot_mount() {
	mkdir -p /tmp/zyperos-boot &&
		mount -t vfat -o ro,noatime,nosuid,nodev /dev/mmcblk0p1 /tmp/zyperos-boot 2> /dev/null &&
		BOOT_DIR=/tmp/zyperos-boot
}
device_boot_unmount() { umount /tmp/zyperos-boot 2> /dev/null; }

WIFI_DOWN="no adapter, or it did not come up"

# Wi-Fi follows a USB adapter as it comes and goes, as on the Pixel 2
wifi_follow() {
	i=0
	while [ $i -lt 200 ] && pidof splash > /dev/null; do
		sleep 0.1
		i=$((i + 1))
	done
	had=
	while : ; do
		if [ -d /sys/class/net/wlan0 ]; then
			if [ -z "$had" ]; then
				had=1
				boot_env
				[ "$WIFI" = 1 ] && wifi_on &
			fi
		elif [ -n "$had" ]; then
			had=
			/etc/init.d/wpa_supplicant stop > /dev/null 2>&1
		fi
		sleep 2
	done
}
device_radios() { wifi_follow; }

# What a test card's log needs to say about the R36S it runs on: which tree
# booted, the kernel's lines about the panel, the cards and the battery, and
# the input devices with their names. In the background: nothing waits on it.
device_log() {
	{
		echo "r36s: model $(tr -d '\0' < /proc/device-tree/model 2> /dev/null)"
		echo "r36s: cmdline $(cat /proc/cmdline 2> /dev/null)"
		ls /dev/mmcblk* 2> /dev/null | sed 's/^/r36s: card /'
		dmesg 2> /dev/null | grep -iE 'panel|dsi|st7703|generic-dsi|mmc[0-9]|battery at start|joypad|rk817' |
			sed 's/^/kernel: /'
		grep -E '^(N|H):' /proc/bus/input/devices 2> /dev/null | sed 's/^/r36s: input /'
		# The charger and the battery, the USB port's role and its PHY's
		# cables: what a report on charging or the OTG port needs
		for f in /sys/class/power_supply/*/uevent; do
			sed "s|^|r36s: $(basename "$(dirname "$f")") |" "$f"
		done 2> /dev/null
		for f in /sys/class/usb_role/*/role /sys/class/extcon/*/state; do
			[ -e "$f" ] && echo "r36s: $f $(tr '\n' ' ' < "$f")"
		done 2> /dev/null
		cat /run/usbrole/state 2> /dev/null | sed 's/^/r36s: usbrole /'
		dmesg 2> /dev/null | grep -iE 'jackswitch' | sed 's/^/kernel: /'
	} >> "$LOG" 2>&1 &
	device_usb_follow >> "$LOG" 2>&1 &
}

# The kernel's lines about the USB port, from the start and as they come, for
# the whole session: usbrole's facts at each change (the role, every power
# supply, the extcon, the gadget controller's state, the DWC2's registers,
# the line probe), the gadget's binding and the controller's own lines, each
# with the kernel's time in seconds. device_log's one look at the kernel's
# log came a second into the start, before S40usbgadget and usbrole had
# said anything, so test.3's log of the session the cable went in had none
# of it. /dev/kmsg gives one record a read, so cat, never the shell's read;
# the shutdown's kill ends it before the card is unmounted.
device_usb_follow() {
	cat /dev/kmsg 2> /dev/null | while IFS= read -r rec; do
		case "$rec" in
		" "*) continue ;;                  # a record's key=value lines
		esac
		msg=${rec#*;}
		case "$msg" in
		*"Mode Mismatch"*) continue ;;      # a register read in the other mode
		*usbrole*|*dwc2*|*"usb "[0-9]*|*usb[0-9]*|*gadget*|*udc*|*UDC*|*ncm*|*extcon*) ;;
		*) continue ;;
		esac
		us=$(echo "${rec%%;*}" | cut -d, -f3)
		case "$us" in
		''|*[!0-9]*) echo "kernel: $msg" ;;
		*) printf 'kernel: [%5d.%06d] %s\n' $((us / 1000000)) $((us % 1000000)) "$msg" ;;
		esac
	done
}
