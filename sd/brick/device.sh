#!/bin/sh
# The TrimUI Brick's part of ZyperOS's card script: sourced by launch.sh
# (sd/zyperos/launch.sh) before anything else, never run. mk/payload.sh puts
# it beside launch.sh as ZyperOS/device.sh.
#
# What the Brick has and the Pixel doesn't: Wi-Fi and Bluetooth, started at
# boot when they were left on, and the loop that keeps a remembered headset
# connected and tells ZyperOS where sound can go. Everything that worked
# around TrimUI's firmware (its input daemon, LEDs, boot logo and splash,
# procd's services) went with it: the system is plastron, ours from the
# kernel up.
#
# The TrimUI Smart Pro runs the Brick's system as a device of its own, so this
# is its device.sh too (sd/smartpro/device.sh links here). The two differ only
# in their folder, which is each one's id as the launcher's is (P_USERDATA,
# src/device/brick.c). Which one this is comes from the system's device line
# (/etc/zyperos/system, plastron's zyperos package), as its start script
# (/etc/init.d/brick) reads it; any other line, or none, is the Brick.
case $(sed -n 's/^device //p' /etc/zyperos/system 2> /dev/null) in
smartpro) DEVICE_ID=smartpro ;;
*) DEVICE_ID=brick ;;
esac

export USERDATA_PATH=$SDCARD/.userdata/$DEVICE_ID
export SDL_VIDEODRIVER=pvrfb SDL_RENDER_DRIVER=opengles2
# .asoundrc named as a top-level ALSA config file, and not only reached through
# alsa.conf's @hooks load: alsa-lib re-reads its config at the next open when a
# file in this list changes, and never checks files a hook loaded. So a headset
# paired after Diatom or MP4 started, whose PCM bt_write_asoundrc has just
# added, was `Unknown PCM` to both until they restarted (measured 2026-09-25).
# The .asoundrc in HOME, which launch.sh sets to USERDATA_PATH.
export ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:$USERDATA_PATH/.asoundrc

# The system's own tool, which lights the panel: ZyperOS turns it off on its
# way out (a power-off, Auto Off), and the next pass needs it back on
BACKLIGHT=/usr/lib/brick/backlight

# Off here, each for what the Brick lacks so far (launch.sh lists them all):
HAS_PORTS=0        # port scripts are bash, and the Brick's image has no bash
HAS_PORTMASTER=0   # its files (ZyperOS/portmaster) are in the Pixel 2's payload only
HAS_PORTKEYS=0     # portkeys is built for the Pixel 2 only (plastron's zyperos.mk)
HAS_PORT_STILL=0   # the Brick's splash (splash-fb.c) has no --still
HAS_PORT_TURN=0    # its panel is not turned, and its SDL2 is pvrfb's, not kmsdrm's
HAS_ZYHOME=0       # zyhome is built for the Pixel 2 only (plastron's zyperos.mk)
HAS_USBDRIVE=1     # Flash Drive: the image's usbdrive (plastron's package, the Brick's build)
HAS_OFF_PENDING=0  # its power-off has no last step (lastoff) to start it for a charger

# The boot partition, which the Brick's system keeps read-only at /boot for
# its closed files: what ZyperOS Installer left there for a card's first
# start (launch.sh: your PICO-8), already there
device_boot_mount() { [ -d /boot ] && BOOT_DIR=/boot; }

WIFI_DOWN="the radio did not come up"

# The settings folder was named after TrimUI's model for the Brick, tg3040,
# until 2026-10-21. A card that still has it and not the new one has it renamed
# once, which on the same card costs nothing. With both there, neither is
# touched: merging two sets of settings is not a guess to make at boot.
# The Brick's alone: a Smart Pro never had that folder.
OLD_USERDATA=
[ "$DEVICE_ID" = brick ] && OLD_USERDATA=$SDCARD/.userdata/tg3040
device_card() {
	[ -d "$OLD_USERDATA" ] && [ ! -e "$USERDATA_PATH" ] &&
		mv "$OLD_USERDATA" "$USERDATA_PATH"
}
device_log() {
	[ -d "$OLD_USERDATA" ] &&
		echo "launch: $OLD_USERDATA left as it is: $USERDATA_PATH is there too" >> "$LOG"
}

# Bluetooth at boot, when it was left on: the system's script brings it up
# (the modules, dbus, the chip attached, bluetoothd, bluealsa), then the
# reconnect loop below. Off needs nothing: the radio is off from power-on.
bt_on() {
	/etc/init.d/bluetooth start > /dev/null 2>&1 || {
		echo "bt: Bluetooth did not come up" >> "$LOG"
		return 1
	}
	# The headsets' PCMs again, now that their bonds can be read: they live on
	# plastron's state partition, which Bluetooth's start mounts, so the write
	# before start_resident found none. The emulator and MP4 see the new
	# file at their next open (ALSA_CONFIG_PATH, above)
	command -v bt_write_asoundrc > /dev/null && bt_write_asoundrc
	sleep 3          # let both profiles register with BlueZ before any connect
	command -v bt_reconnect > /dev/null && bt_reconnect
}

# bt_a2dp_ready and bt_reconnect, the loop that keeps a remembered headset
# connected and tells ZyperOS where the sound can go, are bt-alsa.sh's, so
# ZyperOS starts the same loop when Bluetooth is turned on from its screen.

# The headsets' ALSA config, bt_pcm_name and bt_write_asoundrc, and the
# reconnect loop, in a file of its own so ZyperOS runs the same code after a
# pair or a forget, and when Bluetooth is turned on from its screen. Tested first:
# `.` on a missing file ends a non-interactive shell, and this one ending
# would leave no ZyperOS. Without it the boot goes on, with game sound on the
# speaker.
[ -f "$ZYPEROS_DIR/bt-alsa.sh" ] && . "$ZYPEROS_DIR/bt-alsa.sh"

# The radios once ZyperOS has drawn its first frame, which is when plastron's
# splash ends: loading Wi-Fi's driver and firmware beside ZyperOS's startup
# made the shelf about 0.15 s later (measured 2026-10-18, three cold boots
# each way). They come up a second or so later for it, still long before
# anyone could open a menu. Bounded: a splash that never ends (no ZyperOS
# drawing) holds them 20 s at most.
radios_after_splash() {
	i=0
	while [ $i -lt 200 ] && pidof splash > /dev/null; do
		sleep 0.1
		i=$((i + 1))
	done
	[ "$WIFI" = 1 ] && wifi_on &
	[ "$BLUETOOTH" = 1 ] && bt_on &
}

# BEFORE start_resident, and that order is the whole point: alsa-lib caches
# its config at the first PCM open, so a definition written afterwards is
# invisible to that process for as long as it lives. Only when bt-alsa.sh
# was there to source.
device_resident() {
	command -v bt_write_asoundrc > /dev/null && bt_write_asoundrc
}
# And the radios at all only when one of them was left on (boot.env)
device_radios() {
	{ [ "$WIFI" = 1 ] || [ "$BLUETOOTH" = 1 ]; } && radios_after_splash
}
