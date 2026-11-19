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

export USERDATA_PATH=$SDCARD/.userdata/brick
export SDL_VIDEODRIVER=pvrfb SDL_RENDER_DRIVER=opengles2
# .asoundrc named as a top-level ALSA config file, and not only reached through
# alsa.conf's @hooks load: alsa-lib re-reads its config at the next open when a
# file in this list changes, and never checks files a hook loaded. So a headset
# paired after Diatom or Muse started, whose PCM bt_write_asoundrc has just
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

WIFI_DOWN="the radio did not come up"

# The settings folder was named after TrimUI's model for the Brick, tg3040,
# until 2026-10-21. A card that still has it and not the new one has it renamed
# once, which on the same card costs nothing. With both there, neither is
# touched: merging two sets of settings is not a guess to make at boot.
OLD_USERDATA=$SDCARD/.userdata/tg3040
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
	# before start_resident found none. The emulator and Muse see the new
	# file at their next open (ALSA_CONFIG_PATH, above)
	command -v bt_write_asoundrc > /dev/null && bt_write_asoundrc
	sleep 3          # let both profiles register with BlueZ before any connect
	bt_reconnect
}

# Whether bluealsa has an A2DP stream for this address yet. The link comes up
# first and the stream a moment after, and a headset published in between
# is one that nothing can open: Muse got `No such device` and fell back to
# the speaker, and stayed there until something happened to re-send the
# route (seen 2026-09-26, switching to the OpenFit). -l lists each device's
# PCMs under its own line, so the A2DP line has to be under THIS one's.
bt_a2dp_ready() {
	bluealsa-aplay -l 2> /dev/null |
		awk -v m="$1" '/^hci/ { on = index($0, m) > 0 } on && /A2DP/ { f = 1 } END { exit !f }'
}

# Keep a remembered headset connected, so powering it on reconnects it wherever
# you are rather than only at a screen that happens to be watching. And tell
# ZyperOS where the sound can go, in /tmp/zyperos_btsink.
#
# Trusted devices with a key only, from bluetoothd's own /var/lib/bluetooth/
# <adapter>/. Judge success by `info`, never by the return of `connect`:
# bluetoothctl reports Failed for a2dp even when the link came up.
bt_reconnect() {
	adapter=$(hciconfig hci0 2> /dev/null | sed -n 's/.*BD Address: \([0-9A-F:]*\).*/\1/p')
	[ -n "$adapter" ] || return 0
	latest=
	prev_links=
	soon=0
	while :; do
		connected=
		bonds=
		for d in /var/lib/bluetooth/"$adapter"/*:*; do
			[ -d "$d" ] || continue
			grep -q '^Trusted=true' "$d/info" 2> /dev/null || continue
			# And a key, or it is not a bond: a memory-only pairing leaves
			# Trusted behind with no key after a restart, and dialing that
			# every pass connected it for four seconds at a time and published
			# a sink each time. See bond_name in src/bt.c.
			grep -q '^\[LinkKey\]' "$d/info" 2> /dev/null || continue
			bonds="$bonds $(basename "$d")"
		done
		# Whoever is already connected first, and nobody dialed while one is:
		# `connect` takes about five seconds to give up on a headset that is
		# off, and walking in address order made a live headset wait on a dead
		# one (measured 2026-09-25).
		#
		# And with two connected, the one connected LAST, as a phone does. The
		# order is kept here, pass to pass: a bond connected now and not last
		# pass, or on a different link handle (a reconnect), is the latest. Not
		# the handle's value itself, which is reused. A connect from the
		# Bluetooth screen asks for a pass at once, so the headset chosen there
		# takes the sound within a second.
		cons=$(hcitool con 2> /dev/null)
		links=
		for mac in $bonds; do
			bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes' || continue
			h=$(echo "$cons" | sed -n "s/.*ACL $mac handle \([0-9]*\).*/\1/p" | head -1)
			links="$links $mac/$h"
			case " $prev_links " in *" $mac/$h "*) ;; *) latest=$mac ;; esac
		done
		prev_links=$links
		# The latest while it stays connected; when it goes, one that has not.
		case "$links " in
		*" $latest/"*) connected=$latest ;;
		*) set -- $links; connected=${1%%/*} ;;
		esac
		# Nothing connected: say so NOW, before dialing. Dialing a headset that
		# is off takes about five seconds each, and a song that had failed over
		# to the speaker stayed there when its headset came back meanwhile
		# (seen 2026-09-25).
		[ -n "$connected" ] || rm -f /tmp/zyperos_btsink
		for mac in $bonds; do
			[ -z "$connected" ] || break
			bluetoothctl connect "$mac" > /dev/null 2>&1
			# Judge by info, NEVER by the return. Trusting the return published
			# "no sink" while a headset was connected, and every flip reopens
			# Diatom's audio device: nine route changes and 407236 dropped
			# audio frames in one game (2026-09-05).
			sleep 1
			bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes' &&
				connected=$mac
		done
		# A file rather than ZyperOS asking, because asking means forking
		# bluetoothctl out of a large process. This loop already knows.
		if [ -n "$connected" ] && ! bt_a2dp_ready "$connected"; then
			# Connected, its stream not there yet: leave what is published
			# (during a switch that is the old headset, still playing) and look
			# again in a second rather than twenty. Fifteen times at most, for
			# a bond that never gets an audio stream at all.
			soon=$((soon + 1))
		elif [ -n "$connected" ]; then
			soon=0
			# The PCM's name from .asoundrc, not a bluealsa device string,
			# which SDL opens and then never drains (measured 2026-09-05). And
			# the link's ACL handle on a second line, new on every connection:
			# a headset that dropped and came back between two looks keeps its
			# name, and without the handle whoever had fallen back to the
			# speaker meanwhile was never sent back to it.
			link=$(hcitool con 2> /dev/null |
				sed -n "s/.*ACL $connected handle \([0-9]*\).*/\1/p" | head -1)
			printf '%s\n%s\n' "$(bt_pcm_name "$connected")" "$link" \
				> /tmp/zyperos_btsink.tmp && mv /tmp/zyperos_btsink.tmp /tmp/zyperos_btsink
		else
			soon=0
			rm -f /tmp/zyperos_btsink
		fi
		# Twenty seconds, or less when the Bluetooth screen asks: it touches
		# /tmp/zyperos_btpass after a connect, a disconnect or a forget, so the
		# sink moves with the screen rather than up to a pass later. A file
		# and not a signal: ZyperOS has no pid for this loop, and a signal to
		# the wrong shell would be launch.sh itself.
		#
		# And at once when a link comes or goes: the links are looked at every
		# two seconds (hcitool's list, which dials nothing), so a headset
		# switched off is withdrawn, and one switched back on, which connects
		# by itself, is published on its new link, within about two seconds.
		# A headset turned off and on inside one pass was never seen to go,
		# so the song that had fallen back to the speaker was never sent back
		# to it (2026-10-22).
		cons=$(hcitool con 2> /dev/null)
		n=0
		wait=20
		[ $soon -gt 0 ] && [ $soon -le 15 ] && wait=1
		while [ $n -lt $wait ] && [ ! -e /tmp/zyperos_btpass ]; do
			sleep 1
			n=$((n + 1))
			[ $((n % 2)) -eq 0 ] &&
				[ "$(hcitool con 2> /dev/null)" != "$cons" ] && break
		done
		rm -f /tmp/zyperos_btpass
	done
}

# The headsets' ALSA config, bt_pcm_name and bt_write_asoundrc, in a file of
# its own so ZyperOS runs the same code after a pair or a forget. Tested first:
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
