#!/bin/sh
# One named ALSA PCM per bonded headset: bt_pcm_name and bt_write_asoundrc.
# And the loop that keeps a remembered headset connected and says where the
# sound goes: bt_a2dp_ready and bt_reconnect, below.
#
# Sourced, never run. The Brick's device.sh sources it at boot, and launch.sh
# calls bt_write_asoundrc before the emulator starts (device_resident); the
# launcher sources it into a shell of its own after a pair or a forget
# (bt_asoundrc in src/bt.c) and calls the same function, and when Bluetooth
# is turned on from its screen it starts bt_reconnect the same way
# (bt_loop_start).
#
# ONE COPY. This used to live in launch.sh with a second implementation in C,
# held together by a check that compared the names and nothing else - not the
# block around them, and not which bonds get one. launch.sh cannot call the
# launcher, which does not exist yet at boot, but the launcher can call this,
# and pairing is rare enough that starting a shell for it costs nobody a wait.
# What boot pays is one more small file: reading one cold off this card
# measured about 0.4ms, 2026-09-14.
#
# The file has to exist before the emulator's first PCM open. alsa-lib loads
# its config once and tracks the files it actually read: a .asoundrc created
# later is never noticed, so a config written when a headset CONNECTS arrives
# too late for a resident emulator that opened the speaker at boot. Measured
# 2026-09-05 - it failed in place and then worked immediately on restarting the
# emulator with the file already there.
#
# The bond is persistent (/var/lib/bluetooth/<adapter>/<device>/), so every
# headset that could connect is known now, without waiting for one to. A PCM
# for a headset that is not connected simply fails to open, and the port falls
# back to the speaker and says so - which is Diatom's behavior anyway.
#
# One per device rather than one reused name, so switching headsets needs no
# restart. bt_pcm_name is the single place the naming is decided.

bt_pcm_name() {
	echo "bt_$(echo "$1" | tr ':' '_')"
}

bt_write_asoundrc() {
	rc=$USERDATA_PATH/.asoundrc
	: > "$rc.tmp"
	for d in /var/lib/bluetooth/*/*:*; do
		[ -d "$d" ] || continue
		grep -q '^Trusted=true' "$d/info" 2> /dev/null || continue
		grep -q '^\[LinkKey\]' "$d/info" 2> /dev/null || continue   # see bt_reconnect
		mac=$(basename "$d")
		cat >> "$rc.tmp" <<-EOF
		pcm.$(bt_pcm_name "$mac") {
		    type bluealsa
		    device "$mac"
		    profile "a2dp"
		}
		EOF
	done
	# `default` is deliberately not redefined: ALSA reads this in addition to
	# /etc/asound.conf, and the speaker path must stay exactly as it was.
	mv "$rc.tmp" "$rc"
}

# Whether bluealsa has an A2DP stream for this address yet. The link comes up
# first and the stream a moment after, and a headset published in between
# is one that nothing can open: MP4 got `No such device` and fell back to
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
#
# One loop: the boot's (device.sh's bt_on, when Bluetooth was left on) or
# ZyperOS's, when it is turned on from its screen (bt_loop_start, src/bt.c),
# whichever comes first; the other finds it in /tmp/zyperos_btloop and goes.
# Its own pid from /proc/self, read by the shell itself: $$ is launch.sh's in
# a function run in the background. While the radio is off (no hci0) it
# dials nothing and says no sink, and goes on when it is back.
#
# And no headset the player disconnected on the Bluetooth screen is dialed:
# those are in /tmp/zyperos_bthold until they are connected there again,
# forgotten, or the handheld restarts (src/bt.h). Dialing every bond
# whenever nothing was connected brought a headset just let go back within
# a pass (1.1.1-rc.1).
bt_reconnect() {
	read -r me _ < /proc/self/stat
	loop=$(cat /tmp/zyperos_btloop 2> /dev/null)
	[ -n "$loop" ] && [ "$loop" != "$me" ] && [ -d "/proc/$loop" ] && return 0
	echo "$me" > /tmp/zyperos_btloop
	adapter=
	latest=
	prev_links=
	soon=0
	while :; do
		if [ ! -d /sys/class/bluetooth/hci0 ]; then
			rm -f /tmp/zyperos_btsink
			sleep 2
			continue
		fi
		[ -n "$adapter" ] ||
			adapter=$(hciconfig hci0 2> /dev/null | sed -n 's/.*BD Address: \([0-9A-F:]*\).*/\1/p')
		[ -n "$adapter" ] || { sleep 2; continue; }
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
			grep -qix "$mac" /tmp/zyperos_bthold 2> /dev/null && continue
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
