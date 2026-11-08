#!/bin/sh
# ZyperOS on the GKD Pixel 2. plastron's init starts this once the card's
# games partition is mounted at /mnt/SDCARD.
#
# The Brick's launch.sh, less what the Pixel doesn't have: Bluetooth, and a
# radio of its own (its Wi-Fi is a USB adapter, when one is plugged in). The
# Pixel's system is plastron, ours from the kernel up, so there is nothing
# here to work around: start the emulator, start the launcher, power off when
# it is done.

ZYPEROS_DIR=/mnt/SDCARD/ZyperOS
SDCARD=/mnt/SDCARD

export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD/Roms
export SAVES_PATH=$SDCARD/Saves
export CORES_PATH=$ZYPEROS_DIR/cores
export USERDATA_PATH=$SDCARD/.userdata/pixel2
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs
export HOME=$USERDATA_PATH
export ZYPEROS_DIATOM_SOCKET=/tmp/diatom.sock

# Everything up to the launcher is on the boot's critical path, so it is done
# with the shell's own built-ins wherever one will do: every helper program
# (mkdir, pidof, rm, cut) is a process start, 5 to 15 ms each on this CPU
# while the rest of the boot runs, and they added up to 93 ms before the
# launcher started (measured 2026-10-01).

# Only on a fresh card are any of these missing
for d in "$BIOS_PATH" "$ROMS_PATH" "$SAVES_PATH" "$USERDATA_PATH" "$LOGS_PATH" \
         "$SHARED_USERDATA_PATH"; do
	[ -d "$d" ] || mkdir -p "$d"
done

# A card from before ZyperOS had its own name keeps the settings database and
# the shared states and library under their old names: moved once, before
# anything opens them, and the old logs dropped
if [ -e "$USERDATA_PATH/tortos.db" ] && [ ! -e "$USERDATA_PATH/zyperos.db" ]; then
	for x in "" -wal -shm; do
		[ -e "$USERDATA_PATH/tortos.db$x" ] &&
			mv "$USERDATA_PATH/tortos.db$x" "$USERDATA_PATH/zyperos.db$x"
	done
fi
[ -d "$SHARED_USERDATA_PATH/.tortos" ] && [ ! -e "$SHARED_USERDATA_PATH/.zyperos" ] &&
	mv "$SHARED_USERDATA_PATH/.tortos" "$SHARED_USERDATA_PATH/.zyperos"
[ -e "$LOGS_PATH/tortos.log" ] && rm -f "$LOGS_PATH"/tortos.log*

# The Dreamcast's two cores keep their VMUs apart: flycast2021, the default,
# in Bios/dc as ROGUE OS did, and the standalone Flycast in Saves/flycast. A
# card set up for Flycast first (a ROGUE VMU copied there) gets that VMU for
# flycast2021 too, once; the two go their own ways after that
[ -f "$SAVES_PATH/flycast/vmu_save_A1.bin" ] && [ -d "$BIOS_PATH/dc" ] &&
	[ ! -e "$BIOS_PATH/dc/vmu_save_A1.bin" ] &&
	cp -p "$SAVES_PATH/flycast/vmu_save_A1.bin" "$BIOS_PATH/dc/vmu_save_A1.bin"

# ScummVM's engine data and GUI theme (mk/build-scummvm.sh) where its core
# looks, Bios/scummvm, copied from ZyperOS/scummvm again whenever a new
# ZyperOS brings another build of them: the two .pin files name the ScummVM
# commit, compared with built-ins, so a boot with nothing new starts no
# process for it. The core's own settings, Bios/scummvm.ini, are left alone
pin_new=; pin_old=
[ -f "$ZYPEROS_DIR/scummvm/.pin" ] && read -r pin_new < "$ZYPEROS_DIR/scummvm/.pin"
[ -f "$BIOS_PATH/scummvm/.pin" ] && read -r pin_old < "$BIOS_PATH/scummvm/.pin"
if [ -n "$pin_new" ] && [ "$pin_new" != "$pin_old" ]; then
	rm -rf "$BIOS_PATH/scummvm/extra" "$BIOS_PATH/scummvm/theme"
	mkdir -p "$BIOS_PATH/scummvm" &&
		cp -R "$ZYPEROS_DIR/scummvm/extra" "$ZYPEROS_DIR/scummvm/theme" "$BIOS_PATH/scummvm/" &&
		cp "$ZYPEROS_DIR/scummvm/.pin" "$BIOS_PATH/scummvm/.pin"
fi

# FreeJ2ME-Plus's Java app and the Java runtime it runs on (mk/build-
# freej2me.sh) where its core looks, Bios/, copied from ZyperOS/freej2me again
# whenever a new ZyperOS brings another build of them: the .pin files name the
# FreeJ2ME-Plus commit and what the jar and the runtime hold, compared with
# built-ins. The core unpacks the runtime's archive itself, into
# Bios/freej2me_plus_runtime, when it changes
pin_new=; pin_old=
[ -f "$ZYPEROS_DIR/freej2me/.pin" ] && read -r pin_new < "$ZYPEROS_DIR/freej2me/.pin"
[ -f "$BIOS_PATH/freej2me_plus.pin" ] && read -r pin_old < "$BIOS_PATH/freej2me_plus.pin"
if [ -n "$pin_new" ] && [ "$pin_new" != "$pin_old" ]; then
	cp "$ZYPEROS_DIR/freej2me/freej2me_plus-lr.jar" \
	   "$ZYPEROS_DIR/freej2me/freej2me_plus_runtime_linux-arm64.tar.gz" "$BIOS_PATH/" &&
		cp "$ZYPEROS_DIR/freej2me/.pin" "$BIOS_PATH/freej2me_plus.pin"
fi

# One log per boot, carrying the launcher and everything it starts. Ten boots
# kept: this one and zyperos.log.1 to .9, newest first. One wasn't enough: a
# player's report can come several reboots after the fault. Only the last
# boot's log is moved out of the way here; the older ones shift along in the
# background, where nothing waits on them (in line, the renames took 260 ms of
# the boot on TrimUI's firmware, measured 2026-10-02).
LOG=$LOGS_PATH/zyperos.log
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev"
: > "$LOG"
(
	i=9
	while [ $i -gt 1 ]; do
		[ -f "$LOG.$((i - 1))" ] && mv -f "$LOG.$((i - 1))" "$LOG.$i"
		i=$((i - 1))
	done
	[ -f "$LOG.prev" ] && mv -f "$LOG.prev" "$LOG.1"
) &

# The settings this script needs before ZyperOS runs, which ZyperOS writes out
# for it, as on the Brick. The default is for a fresh card's first boot.
WIFI=0
BOOT_ENV=$USERDATA_PATH/boot.env
[ -f "$BOOT_ENV" ] && . "$BOOT_ENV"

# Wi-Fi on, as the Brick does at boot, when it was left on and an adapter is
# in (wifi_follow, below): the supplicant rejoins a saved network by itself,
# and once it has, the DHCP client, started exactly as src/wifi.c starts it
# (the same PID file, no -q, so it stays and renews the lease), so ZyperOS
# finds it running rather than starting a second. /etc/init.d/wpa_supplicant
# stops it with Wi-Fi.
WPA_SOCK=/var/run/wpa_supplicant
DHCP_PID=/var/run/udhcpc.wlan0.pid
wifi_on() {
	/etc/init.d/wpa_supplicant start > /dev/null 2>&1 || {
		echo "wifi: no adapter, or it did not come up" >> "$LOG"
		return 1
	}
	i=0
	while [ $i -lt 25 ]; do
		if wpa_cli -p $WPA_SOCK -i wlan0 status 2> /dev/null |
		   grep -q '^wpa_state=COMPLETED'; then
			p=$(cat $DHCP_PID 2> /dev/null)
			[ -n "$p" ] && [ -d "/proc/$p" ] && return 0
			udhcpc -i wlan0 -S -t 5 -T 7 -b -p $DHCP_PID > /dev/null 2>&1
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	echo "wifi: no saved network joined after ${i}s" >> "$LOG"
}

# Running already? By the pid file and /proc, rather than pidof's walk of
# every process. A pid reused after a crash is caught by the name.
diatom_running() {
	local pid comm
	[ -f /tmp/diatom.pid ] && read pid < /tmp/diatom.pid || return 1
	[ -r "/proc/$pid/comm" ] && read comm < "/proc/$pid/comm" || return 1
	[ "$comm" = diatom ]
}

start_resident() {
	diatom_running && return
	[ -e "$ZYPEROS_DIATOM_SOCKET" ] && rm -f "$ZYPEROS_DIATOM_SOCKET"
	"$ZYPEROS_DIR/diatom" --socket "$ZYPEROS_DIATOM_SOCKET" \
		--cores "$CORES_PATH" \
		--save "$SAVES_PATH" --system "$BIOS_PATH" >> "$LOG" 2>&1 &
	echo $! > /tmp/diatom.pid
}

# A port from the Ports shelf. The launcher names its script in PORT_REQUEST
# and exits (launch_port in src/main.c), and the port runs here, with nothing
# of ZyperOS's left holding the display, the sound card or the memory: Diatom
# keeps every core mapped, and Stardew Valley's Mono wants most of the 1 GB.
PORT_REQUEST=/tmp/zyperos_port
PORTMASTER=$ROMS_PATH/Ports/PortMaster
# POWER over a port: the launcher, which reads the power key and the battery,
# is stopped, so portkeys reads them there and sends USR1 here for a press or
# a battery run flat (tools/portkeys.c). The port is asked to end with TERM,
# and whatever of it is left 4 s later is killed: the card must be let go of
# before the power goes. SDL turns TERM into a quit event, which a program
# acts on only when its loop next reads events; OpenBOR then saves and exits,
# 1.5 s after the press on a Pixel 2 (2026-11-06). Then the loop below powers
# off, as after the launcher's own power press. The port runs in a session of
# its own (setsid), so its group is the port, its script and what they
# started, and nothing of launch.sh's; its script leads it, PORT_PID.
PORT_PID=
# Every process of the port's group but its script, by the group in each
# one's stat line (after the name, which can hold spaces and parentheses)
port_procs() {
	local p s
	for p in /proc/[0-9]*; do
		read -r s < "$p/stat" 2> /dev/null || continue
		set -- ${s##*) }
		[ "$3" = "$PORT_PID" ] && [ "${p#/proc/}" != "$PORT_PID" ] && echo "${p#/proc/}"
	done
}
# Ended. The game and the helpers beside it first (gptokeyb, a runtime's
# program), so that the script goes on to what it does after its game: a
# PortMaster script stops gptokeyb and unmounts the runtime it mounted (Mono,
# LOVE), then ends by itself. 2 s later the whole group, the script too; and
# CONT with each TERM, for a port portkeys has stopped under its in-game menu
# (a stopped process takes TERM only once it goes on). Whatever is left 4 s
# after the first TERM is killed.
port_end() {
	local procs
	[ -n "$PORT_PID" ] || return
	procs=$(port_procs)
	[ -n "$procs" ] && kill -TERM $procs 2> /dev/null
	kill -CONT -"$PORT_PID" 2> /dev/null
	( sleep 2; kill -TERM -"$PORT_PID" 2> /dev/null; kill -CONT -"$PORT_PID" 2> /dev/null
	  sleep 2; kill -KILL -"$PORT_PID" 2> /dev/null ) &
}
# What a port left mounted once it has ended, killed before its script could
# unmount it, or never unmounted: a runtime mounted from a file on the card
# (PortMaster's libs/*.squashfs, on a loop device), a folder bound into $HOME
# (PortMaster's bind_directories). Each keeps the card in use, so it could
# not be let go of at the power-off and was checked at the next boot. Every
# mount under the card's folder and every loop device a file on the card
# backs, newest first; nothing of ZyperOS's own is mounted there. A mount
# point's spaces are \040 in /proc/mounts, which printf %b turns back.
port_unmount() {
	local dev mnt rest left= m b l
	while read -r dev mnt rest; do
		case $mnt in
		"$SDCARD"/*) left="$mnt
$left" ;;
		*)
			case $dev in
			/dev/loop*)
				b=
				read -r b < "/sys/block/${dev#/dev/}/loop/backing_file" 2> /dev/null
				case $b in "$SDCARD"/*) left="$mnt
$left" ;; esac
				;;
			esac
			;;
		esac
	done < /proc/mounts
	printf '%s' "$left" | while IFS= read -r m; do
		[ -n "$m" ] || continue
		m=$(printf '%b' "$m")
		echo "port: unmounting $m, left by the port" >> "$LOG"
		umount "$m" 2> /dev/null || umount -l "$m" 2> /dev/null
	done
	# and the loop devices still holding a file of the card's
	for l in /sys/block/loop*/loop/backing_file; do
		[ -f "$l" ] || continue
		b=
		read -r b < "$l" 2> /dev/null
		case $b in "$SDCARD"/*)
			l=${l#/sys/block/}
			losetup -d "/dev/${l%%/*}" 2> /dev/null ;;
		esac
	done
}
port_power() {
	echo "port: powering off (POWER or the battery), ending it" >> "$LOG"
	: > /tmp/zyperos_poweroff
	port_end
}
# Quit in the in-game menu over a port (OpenBOR's, a PortMaster port's,
# PortMaster's own; tools/portkeys.c --menu): USR2 from portkeys. The port
# ends as for POWER, and the launcher comes back on its shelf, as after a
# port that ended by itself
port_quit() {
	echo "port: Quit in the in-game menu, ending it" >> "$LOG"
	port_end
}
run_port() {
	local pid i rc
	if [ -f /tmp/diatom.pid ] && read pid < /tmp/diatom.pid; then
		kill "$pid" 2> /dev/null
		i=0
		while [ $i -lt 30 ] && diatom_running; do
			sleep 0.1
			i=$((i + 1))
		done
		diatom_running && kill -9 "$pid"
	fi
	killall -TERM muse 2> /dev/null
	# PortMaster's install and its updates write a control.txt for whichever
	# system they take this for; ours goes back before every port, with the
	# gl4es settings PortMaster reads for CFW_NAME=ZyperOS (libgl_ZyperOS.txt)
	# and the per-system lines port scripts source (mod_ZyperOS.txt)
	if [ -d "$PORTMASTER" ]; then
		cp -f "$ZYPEROS_DIR/portmaster/control.txt" "$PORTMASTER/control.txt"
		cp -f "$ZYPEROS_DIR/portmaster/libgl_ZyperOS.txt" "$PORTMASTER/libgl_ZyperOS.txt"
		cp -f "$ZYPEROS_DIR/portmaster/mod_ZyperOS.txt" "$PORTMASTER/mod_ZyperOS.txt"
	fi
	# A standalone emulator's script gets the game as its argument ("run:"
	# shelves); a port's script gets nothing
	echo "port: $1${2:+ $2}${3:+ (the in-game menu, $3)}" >> "$LOG"
	# The volume keys, which ZyperOS and Diatom read and which are both stopped
	# now (tools/portkeys.c); MENU held turns them to brightness, as in ZyperOS.
	# It steps ZyperOS's own levels, saved in its database, and shows the bar
	# over the port, in the theme the Appearance setting there makes; the
	# library database holds the time zone Auto's hour is read in
	local keys= still= turn= m
	trap port_power USR1
	trap port_quit USR2
	# The panel is a 480x640 portrait one mounted turned (below). A
	# landscape display is QEMU's (plastron's qemu_defconfig), where nothing
	# turns: the first mode of the first connector with one says which
	for m in /sys/class/drm/card*-*/modes; do
		read -r m < "$m" 2> /dev/null && [ -n "$m" ] && break
	done
	[ "$m" = 480x640 ] && turn=270
	# The loading screen the launcher left (launch_port), held on the panel
	# until the port draws, so a minute of loading is not a black panel
	if [ -n "$turn" ] && [ -f /tmp/zyperos_loading.raw ] && [ -x /usr/bin/splash ]; then
		/usr/bin/splash --still /tmp/zyperos_loading.raw &
		still=$!
	fi
	# A program of its own (OpenBOR, DraStic, the standalone Flycast, a port,
	# PortMaster) has the in-game menu on MENU too: the launcher names the
	# system's tag in the request, third, and portkeys shows the menu by
	# stopping the program and starting the launcher for the menu alone
	# (--menu: the launcher, the tag and the game; src/portmenu.h). A port's
	# game is its script, which its Controls pages are read beside
	if [ -x "$ZYPEROS_DIR/portkeys" ]; then
		if [ -n "$3" ]; then
			"$ZYPEROS_DIR/portkeys" --menu "$ZYPEROS_DIR/zyperos.elf" "$3" "${2:-$1}" \
				"$USERDATA_PATH/zyperos.db" \
				"$SHARED_USERDATA_PATH/.zyperos/library.db" 2>> "$LOG" &
		else
			"$ZYPEROS_DIR/portkeys" "$USERDATA_PATH/zyperos.db" \
				"$SHARED_USERDATA_PATH/.zyperos/library.db" 2>> "$LOG" &
		fi
		keys=$!
	fi
	# The panel is a 480x640 portrait one mounted turned. ZyperOS and Diatom
	# turn their own pictures; for anything else plastron's SDL2 hands out a
	# landscape window and turns each frame onto the panel (its kmsdrm
	# rotation patch). And SDL's 2D renderer on GLES2: its desktop GL one,
	# which SDL prefers once it has OpenGL, draws nothing but the clear color
	# on Panfrost here, so a port that draws with SDL_Renderer was black.
	( cd "${1%/*}" &&
	  if [ -n "$turn" ]; then export SDL_KMSDRM_ROTATION=$turn; fi &&
	  SDL_RENDER_DRIVER=opengles2 exec setsid /bin/bash "$1" ${2:+"$2"} ) >> "$LOG" 2>&1 &
	PORT_PID=$!
	# A USR1 (POWER) ends the wait early; the port has ended once its
	# process is gone
	while :; do
		wait "$PORT_PID"
		rc=$?
		[ -d "/proc/$PORT_PID" ] || break
	done
	echo "port: ended, $rc" >> "$LOG"
	# What the script left running in its group when it ended, a helper
	# started in the background and never stopped, goes with it
	if kill -0 -"$PORT_PID" 2> /dev/null; then
		echo "port: ending what it left running" >> "$LOG"
		kill -TERM -"$PORT_PID" 2> /dev/null
		kill -CONT -"$PORT_PID" 2> /dev/null
		i=0
		while [ $i -lt 10 ] && kill -0 -"$PORT_PID" 2> /dev/null; do
			sleep 0.1
			i=$((i + 1))
		done
		kill -KILL -"$PORT_PID" 2> /dev/null
	fi
	PORT_PID=
	# Waited for: while the bar is up it holds a copy of the port's display,
	# and the launcher cannot take the display until that copy is gone
	[ -n "$keys" ] && kill "$keys" 2> /dev/null && wait "$keys" 2> /dev/null
	# An in-game menu still up (a port that ended under it, the kernel's
	# doing) would hold the display the launcher is about to take
	[ -n "$3" ] && killall -TERM zyperos.elf 2> /dev/null
	trap - USR1 USR2
	[ -n "$still" ] && kill "$still" 2> /dev/null
	rm -f /tmp/zyperos_loading.raw
	# A helper left behind would keep the buttons typing keys under the
	# shelf: gptokeyb, or DraStic's stylus pad
	killall -9 gptokeyb gptokeyb2 dspen 2> /dev/null
	# and a runtime or a folder it left mounted would keep the card in use
	port_unmount
}

# Whole seconds since boot into NOW, for telling a crash loop from a normal
# exit. A variable rather than $(...), which would fork a subshell for it.
now_s() {
	local up rest
	read up rest < /proc/uptime
	NOW=${up%%.*}
}

start_resident
[ -e /tmp/zyperos_poweroff ] && rm -f /tmp/zyperos_poweroff
[ -e /tmp/zyperos_reboot ] && rm -f /tmp/zyperos_reboot

# Wi-Fi follows the adapter as it comes and goes: it isn't the Pixel's own, so
# it can be plugged in or pulled at any time, a game included. Looked for every
# 2 seconds with the shell's own test, no process started for it. When it
# appears, and Wi-Fi was left on (boot.env, read again, as ZyperOS rewrites it
# when the setting changes), the same as at boot; when it goes, the
# supplicant and the DHCP client stop, so the next one starts clean. From when
# the splash ends, as on the Brick, so the adapter's driver and the supplicant
# stay out of ZyperOS's way to its first frame; a splash that never ends holds
# it 20 s at most.
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
				WIFI=0
				[ -f "$BOOT_ENV" ] && . "$BOOT_ENV"
				[ "$WIFI" = 1 ] && wifi_on &
			fi
		elif [ -n "$had" ]; then
			had=
			/etc/init.d/wpa_supplicant stop > /dev/null 2>&1
		fi
		sleep 2
	done
}
wifi_follow &

# A Mac or PC leaves its own files on the card. Swept after the launcher is up,
# as on the Brick, so it costs the boot nothing.
( sleep 8
  rm -rf /mnt/SDCARD/.Trashes /mnt/SDCARD/.Spotlight-V100 /mnt/SDCARD/.fseventsd \
         /mnt/SDCARD/.TemporaryItems
  find /mnt/SDCARD \( -name '._*' -o -name '.DS_Store' -o -name 'Thumbs.db' \
       -o -name 'desktop.ini' \) -type f -exec rm -f {} \;
) > /dev/null 2>&1 &

# The launcher, restarted if it exits on its own; five fast failures in a row
# and the device powers off rather than looping on a broken build.
cd "$ZYPEROS_DIR"
FAILS=0
while : ; do
	now_s; START=$NOW
	./zyperos.elf >> "$LOG" 2>&1
	[ -f /tmp/zyperos_poweroff ] && break
	# System Update's Restart Now: the switch to the new version runs at
	# the next start (the image's update-apply.sh)
	[ -f /tmp/zyperos_reboot ] && break
	# The charging screen's Hold POWER to start (src/charging.h): ZyperOS
	# afresh, at once, as a start from off is
	if [ -f /tmp/zyperos_restart ]; then
		rm -f /tmp/zyperos_restart
		FAILS=0
		start_resident
		continue
	fi
	if [ -f "$PORT_REQUEST" ]; then
		# The script, the game and the menu's tag, a tab between each
		# (launch_port). Split by hand: read would run the two tabs around a
		# port's empty game together, as it does any whitespace in IFS
		TAB=$(printf '\t')
		IFS= read -r REQ < "$PORT_REQUEST"
		rm -f "$PORT_REQUEST"
		PORT=${REQ%%"$TAB"*}; GAME=; TAG=
		case $REQ in *"$TAB"*)
			REQ=${REQ#*"$TAB"}
			GAME=${REQ%%"$TAB"*}
			case $REQ in *"$TAB"*) TAG=${REQ#*"$TAB"} ;; esac
			;;
		esac
		run_port "$PORT" "$GAME" "$TAG"
		# POWER, or the battery run flat, over the port (port_power)
		[ -f /tmp/zyperos_poweroff ] && break
		cd "$ZYPEROS_DIR"
		FAILS=0
		start_resident
		continue
	fi
	now_s; END=$NOW
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		[ $FAILS -ge 5 ] && break
	else
		FAILS=0
	fi
	sleep 1
	# Bring the emulator back if it died, before the launcher restarts. Here
	# and not at the top of the loop: the first pass would ask the instant
	# after the start above, while the new process is still a copy of this
	# shell, and start a second Diatom. Measured 2026-10-01, two of them.
	start_resident
done
if [ -f /tmp/zyperos_reboot ]; then
	sync
	echo "restart for System Update" >> "$LOG"
	reboot
else
	# For the start after. A charger plugged in from here on comes while the
	# handheld is still on, and the PMIC starts it only for a cable that comes
	# while it is off: plastron's last step of the power-off restarts it
	# instead when the cable is in by then (lastoff), and the launcher, finding
	# this and the cable in, shows its charging screen first. Any other start
	# takes it away.
	: > "$USERDATA_PATH/off-pending"
	sync
	poweroff
fi
