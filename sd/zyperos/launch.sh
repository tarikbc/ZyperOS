#!/bin/sh
# ZyperOS on every handheld it runs on: the GKD Pixel 2 and the TrimUI Brick.
# plastron's init starts this once the card's games partition is mounted at
# /mnt/SDCARD.
#
# The system is plastron, ours from the kernel up, so there is nothing here
# to work around: start the emulator, start the launcher, power off when it
# is done. What one handheld has and another doesn't is in device.sh beside
# this file (sd/<device>/device.sh in ZyperOS's tree, put here by
# mk/payload.sh): where its settings live, its drivers, its backlight, its
# radios, and which of the features below it has what they need for.

ZYPEROS_DIR=/mnt/SDCARD/ZyperOS
SDCARD=/mnt/SDCARD

# What device.sh says, besides its environment (USERDATA_PATH, its folder
# in .userdata, exported, and its drivers): which of these features the
# handheld has what it needs for, 1 for each it has. Unset is off.
#
#   HAS_PORTS        ports and "run:" programs, which the launcher hands over
#                    (run_port): bash in the system image, as their scripts
#                    are bash
#   HAS_PORTMASTER   PortMaster's files put back before each port, and its
#                    start timed (ZyperOS/portmaster in the payload)
#   HAS_PORTKEYS     the volume keys, POWER and the in-game menu over a port
#                    (ZyperOS/portkeys in the payload)
#   HAS_PORT_STILL   the loading screen held until a port draws (plastron's
#                    splash --still)
#   HAS_PORT_TURN    a port's picture turned onto a panel mounted turned
#                    (plastron's SDL2, its kmsdrm rotation patch)
#   HAS_ZYHOME       MENU+START held, back to the home (ZyperOS/zyhome in the
#                    payload)
#   HAS_USBDRIVE     Computer Connected > USB Drive (the image's usbdrive)
#   HAS_OFF_PENDING  the marker a charger that comes during a power-off is
#                    started for (plastron's lastoff, the power-off's last step)
#
# EQ_SLAVE, the dmix a port mixes into, for the equalizer over a port's
# sound (eq-port.sh): unset, a port plays as it always did. EQ_SLAVE_USB,
# the PCM in /etc/asound.conf your PICO-8 plays into instead while a USB-C
# DAC is in (run_port): unset, the DAC is not followed.
#
# BACKLIGHT, the system's tool that lights the panel at each start of the
# launcher, where the launcher does not light it itself; WIFI_DOWN, what the
# log says when Wi-Fi does not come up; and these, called where they are
# named below, each nothing where device.sh does not say otherwise.
device_card() { :; }       # before the card's folders are made
device_log() { :; }        # once this boot's log is open
device_boot_mount() { return 1; }  # the boot partition readable at BOOT_DIR
device_boot_unmount() { :; }       # and let go of again
device_resident() { :; }   # just before Diatom starts
device_radios() { :; }     # in the background, once Diatom has started
BACKLIGHT=
WIFI_DOWN="it did not come up"

# The handheld's part. Tested first: `.` on a missing file ends a
# non-interactive shell, and this one ending would leave no ZyperOS, the boot
# animation up and POWER doing nothing. plastron's zyperos.init copies
# device.sh onto the card before launch.sh, so a card with this file and no
# device.sh has had a file taken away (the card check after a power cut) or a
# copy by hand cut short: this file is set aside, as after five fast failures
# (start_failed, below), so the next start copies ZyperOS onto the card again,
# and the handheld goes off.
if [ -f "$ZYPEROS_DIR/device.sh" ]; then
	. "$ZYPEROS_DIR/device.sh"
else
	if [ -f /usr/share/zyperos/card/ZyperOS/launch.sh ]; then
		mv -f "$ZYPEROS_DIR/launch.sh" "$ZYPEROS_DIR/launch.sh.failed"
		echo "zyperos: no device.sh beside launch.sh; the next start copies ZyperOS onto the card again" 2> /dev/null > /dev/kmsg
	fi
	sync
	poweroff
	exit 1
fi

export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD/Roms
export SAVES_PATH=$SDCARD/Saves
export CORES_PATH=$ZYPEROS_DIR/cores
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs
export HOME=$USERDATA_PATH
export ZYPEROS_DIATOM_SOCKET=/tmp/diatom.sock

# Everything up to the launcher is on the boot's critical path, so it is done
# with the shell's own built-ins wherever one will do: every helper program
# (mkdir, pidof, rm, cut) is a process start, 5 to 15 ms each on the Pixel 2's
# CPU while the rest of the boot runs, and they added up to 93 ms before the
# launcher started (measured 2026-10-01). The radios go in the background.

device_card

# A card's first start: its device's folder in .userdata is made below, by
# this start (the payload has none). What ZyperOS Installer left in the boot
# partition for it is read then, and never again (pico8_from_boot)
FIRST_START=
[ -d "$USERDATA_PATH" ] || FIRST_START=1

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

# PrBoom's base settings (config/prboom.cfg) where the Doom shelf's core
# looks for a base config, Bios/prboom/prboom.cfg, copied again whenever a new
# ZyperOS brings other ones. ZyperOS's file: a player's own settings go in
# Roms/Doom/prboom.cfg, which the core reads first, and its own saved ones in
# Saves/<game>/prboom.cfg
if [ -f "$ZYPEROS_DIR/prboom/prboom.cfg" ] &&
   ! cmp -s "$ZYPEROS_DIR/prboom/prboom.cfg" "$BIOS_PATH/prboom/prboom.cfg" 2> /dev/null; then
	mkdir -p "$BIOS_PATH/prboom" &&
		cp "$ZYPEROS_DIR/prboom/prboom.cfg" "$BIOS_PATH/prboom/prboom.cfg"
fi

# PPSSPP's assets (mk/build-ppsspp.sh) where the PSP's core looks,
# Bios/PPSSPP, copied from ZyperOS/ppsspp again whenever a new ZyperOS brings
# another build of them: the two .pin files name the PPSSPP commit. Copied
# over what is there, never removed first: the core keeps files of its own
# in the same folder
pin_new=; pin_old=
[ -f "$ZYPEROS_DIR/ppsspp/.pin" ] && read -r pin_new < "$ZYPEROS_DIR/ppsspp/.pin"
[ -f "$BIOS_PATH/PPSSPP/.pin" ] && read -r pin_old < "$BIOS_PATH/PPSSPP/.pin"
if [ -n "$pin_new" ] && [ "$pin_new" != "$pin_old" ]; then
	mkdir -p "$BIOS_PATH/PPSSPP" &&
		cp -R "$ZYPEROS_DIR/ppsspp/." "$BIOS_PATH/PPSSPP/" &&
		cp "$ZYPEROS_DIR/ppsspp/.pin" "$BIOS_PATH/PPSSPP/.pin"
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
device_log

# Your PICO-8, from a fresh install: ZyperOS Installer puts the player's own
# pico8_64 and pico8.dat (and its license and version.txt) in the image's boot
# partition, pico8/, as a card that never started has no games partition yet
# (installer/src-tauri/src/pico8.rs). Its first start copies them into
# Bios/PICO-8, where the PICO-8 shelf looks (src/pico8.h). Only on that
# start, and only with no PICO-8 there already: no other boot mounts anything.
# The files stay in the boot partition; ZyperOS never ships PICO-8, and these
# are this card's player's own.
if [ -n "$FIRST_START" ] && [ ! -f "$BIOS_PATH/PICO-8/pico8_64" ] && device_boot_mount; then
	if [ -f "$BOOT_DIR/pico8/pico8_64" ] && [ -f "$BOOT_DIR/pico8/pico8.dat" ]; then
		mkdir -p "$BIOS_PATH/PICO-8" &&
			cp "$BOOT_DIR/pico8/"* "$BIOS_PATH/PICO-8/" &&
			chmod +x "$BIOS_PATH/PICO-8/pico8_64" 2> /dev/null
		if [ -f "$BIOS_PATH/PICO-8/pico8_64" ]; then
			echo "pico8: your PICO-8 copied from the boot partition into Bios/PICO-8" >> "$LOG"
		else
			echo "pico8: your PICO-8 is in the boot partition, and the copy into Bios/PICO-8 failed" >> "$LOG"
		fi
	fi
	device_boot_unmount
fi

# The settings this script needs before ZyperOS runs, which ZyperOS writes out
# for it. The defaults are for a fresh card's first boot, before ZyperOS has
# ever run.
BOOT_ENV=$USERDATA_PATH/boot.env
# boot.env read line by line, never sourced, and only the two lines ZyperOS
# writes (src/db.c, db_write_boot_env), with read and case, builtins, so no
# fork. A power cut can leave the file half written or full of another file's
# old bytes, and a syntax error in a sourced file ends the whole script: then
# nothing started ZyperOS, the boot animation stayed and POWER did nothing,
# boot after boot, as ZyperOS is what writes the file again (2026-11-15, a
# Pixel 2 after a forced power-off). Anything else in it is passed over.
boot_env() {
	local line
	WIFI=0
	BLUETOOTH=0
	[ -f "$BOOT_ENV" ] || return 0
	while IFS= read -r line || [ -n "$line" ]; do
		case $line in
		"WIFI='1'" | WIFI=1) WIFI=1 ;;
		"BLUETOOTH='1'" | BLUETOOTH=1) BLUETOOTH=1 ;;
		esac
	done < "$BOOT_ENV"
}
boot_env

# Wi-Fi on, when it was left on: at boot on a handheld with a radio of its
# own, and on one whose Wi-Fi is a USB adapter when the adapter is in
# (device_radios, device.sh's). The supplicant rejoins a saved network by
# itself, but nothing asks for an address: so once it has joined, the DHCP
# client, started exactly as src/wifi.c starts it (the same PID file, no -q,
# so it stays and renews the lease), and ZyperOS finds it running rather than
# starting a second. /etc/init.d/wpa_supplicant stops it with Wi-Fi.
WPA_SOCK=/var/run/wpa_supplicant
DHCP_PID=/var/run/udhcpc.wlan0.pid
wifi_on() {
	/etc/init.d/wpa_supplicant start > /dev/null 2>&1 || {
		echo "wifi: $WIFI_DOWN" >> "$LOG"
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
# Only on a handheld whose image runs port scripts (HAS_PORTS, the loop
# below).
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
# after the first TERM is killed, and said in the log with the kernel's last
# lines: a port that has to be killed, or that a kill cannot end (a process
# stuck in the kernel, a GPU hang, a read from the card), leaves the reason
# there, in Download logs, and not only on a player's description (1.0.6,
# ports stuck on their loading screen).
port_end() {
	local procs
	[ -n "$PORT_PID" ] || return
	procs=$(port_procs)
	[ -n "$procs" ] && kill -TERM $procs 2> /dev/null
	kill -CONT -"$PORT_PID" 2> /dev/null
	( sleep 2; kill -TERM -"$PORT_PID" 2> /dev/null; kill -CONT -"$PORT_PID" 2> /dev/null
	  sleep 2
	  if kill -0 -"$PORT_PID" 2> /dev/null; then
		echo "port: still there 4 s after TERM, killing it; the kernel's last lines:" >> "$LOG"
		dmesg | tail -n 20 >> "$LOG" 2> /dev/null
	  fi
	  kill -KILL -"$PORT_PID" 2> /dev/null
	  sleep 1
	  if kill -0 -"$PORT_PID" 2> /dev/null; then
		echo "port: a kill did not end it; what is left and where it waits:" >> "$LOG"
		for p in /proc/[0-9]*; do
			read -r s < "$p/stat" 2> /dev/null || continue
			set -- ${s##*) }
			[ "$3" = "$PORT_PID" ] || continue
			echo "  ${p#/proc/} $(grep -m1 '^State' "$p/status" 2> /dev/null) $(cat "$p/wchan" 2> /dev/null) $(tr '\0' ' ' < "$p/cmdline" 2> /dev/null | cut -c1-80)" >> "$LOG"
		done
	  fi ) &
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
# port that ended by itself.
#
# Or, with the in-game menu's restart file there, a start again: a Display
# mode picked over your PICO-8, which draws its picture where it was told as
# it started (src/portmenu.h, PORT_RESTART_FILE; the file says the mode). The
# program ends at once, TERM to its whole group (SDL turns it into a quit, so
# PICO-8 saves its settings on its way out), whatever is left of it 4 s later
# is killed, and run_port starts the script again with that Display. A word
# that is no mode's is no start again.
PORT_RESTART_FILE=/tmp/zyperos_port_restart
PORT_AGAIN=
port_quit() {
	local w=
	[ -f "$PORT_RESTART_FILE" ] && read -r w < "$PORT_RESTART_FILE"
	rm -f "$PORT_RESTART_FILE"
	case $w in *[!a-z-]*) w= ;; esac
	if [ -n "$w" ] && [ -n "$PORT_PID" ]; then
		echo "port: Display $w in the in-game menu, starting it again" >> "$LOG"
		PORT_AGAIN=$w
		kill -TERM -"$PORT_PID" 2> /dev/null
		kill -CONT -"$PORT_PID" 2> /dev/null
		( p=$PORT_PID; sleep 4; kill -KILL -"$p" 2> /dev/null ) &
		return
	fi
	echo "port: Quit in the in-game menu, ending it" >> "$LOG"
	port_end
}
# The loading screen the launcher left, held on the panel by splash until
# the port draws (run_port says why); again for a start again, with the one
# before ended first
port_still() {
	[ -n "$still" ] && kill "$still" 2> /dev/null
	still=
	if [ "$HAS_PORT_STILL" = 1 ] && [ -n "$turn" ] && [ -f /tmp/zyperos_loading.raw ] &&
	   [ -x /usr/bin/splash ]; then
		rm -f /tmp/zyperos_menu_up
		slater= sout=
		[ -f /tmp/zyperos_loading_later.raw ] && slater=/tmp/zyperos_loading_later.raw
		[ -f /tmp/zyperos_loading_out ] && sout=/tmp/zyperos_loading_out
		SPLASH_LATER=$slater SPLASH_OUT=$sout SPLASH_OUT_LOG=${sout:+$LOG} \
		SPLASH_OUT_PAUSE=${sout:+/tmp/zyperos_menu_up} \
			/usr/bin/splash --still /tmp/zyperos_loading.raw &
		still=$!
	fi
}
# The port's ALSA config (eq-port.sh): its PCMs into $1, and the hook file
# $2 for its ALSA_CONFIG_PATH that loads them
port_chain() {
	sh "$ZYPEROS_DIR/eq-port.sh" "$1" > /tmp/zyperos_port_eq.conf &&
		sh "$ZYPEROS_DIR/eq-port.sh" --after /tmp/zyperos_port_eq.conf > "$2"
}
run_port() {
	local pid i rc
	# Only a pid that is still a Diatom's: the file names the one that
	# crashed until something starts another, and its number may be
	# another process's by then
	if diatom_running && read pid < /tmp/diatom.pid; then
		kill "$pid" 2> /dev/null
		i=0
		while [ $i -lt 30 ] && diatom_running; do
			sleep 0.1
			i=$((i + 1))
		done
		diatom_running && kill -9 "$pid"
	fi
	# MP4 too, for every program but one that keeps it playing (the
	# request's fifth field, "mp4": your PICO-8, src/portmenu.h's
	# port_keeps_mp4), as a Diatom game keeps it: it plays on, its sound in
	# the same mix as the program's (the default is dmix), and the follower
	# below sends the next track as each one ends
	local follow=
	[ "$5" = mp4 ] || killall -TERM mp4d 2> /dev/null
	# PortMaster's install and its updates write a control.txt for whichever
	# system they take this for; ours goes back before every port, with the
	# gl4es settings PortMaster reads for CFW_NAME=ZyperOS (libgl_ZyperOS.txt)
	# and the per-system lines port scripts source (mod_ZyperOS.txt)
	if [ "$HAS_PORTMASTER" = 1 ] && [ -d "$PORTMASTER" ]; then
		cp -f "$ZYPEROS_DIR/portmaster/control.txt" "$PORTMASTER/control.txt"
		cp -f "$ZYPEROS_DIR/portmaster/libgl_ZyperOS.txt" "$PORTMASTER/libgl_ZyperOS.txt"
		cp -f "$ZYPEROS_DIR/portmaster/mod_ZyperOS.txt" "$PORTMASTER/mod_ZyperOS.txt"
	fi
	# PortMaster itself: the script the shelf runs is a copy of the one in
	# its folder (pm-install.py puts it there), and PortMaster's own update
	# replaces only the folder's on a system it has no layout for, so the
	# shelf went on running the script of the release first installed. Since
	# 2026.11 the folder's is a stub that hands over to its utils/
	# pm_launch.txt, and the old one runs the new program its own old way.
	# The folder's copy is put back over the shelf's before each start. And
	# the start is timed, for the launcher to tell a PortMaster that closed
	# as it started from one the player closed (pm_failure, src/pminstall.h)
	local pm_start=
	[ "$HAS_PORTMASTER" = 1 ] &&
	case $1 in "$ROMS_PATH"/Ports/[Pp][Oo][Rr][Tt][Mm][Aa][Ss][Tt][Ee][Rr].[Ss][Hh])
		if [ -f "$PORTMASTER/PortMaster.sh" ]; then
			cmp -s "$PORTMASTER/PortMaster.sh" "$1" 2> /dev/null ||
				cp -f "$PORTMASTER/PortMaster.sh" "$1"
		fi
		rm -f /tmp/zyperos_pm_ran
		pm_start=$(date +%s)
		;;
	esac
	# A standalone emulator's script gets the game as its argument ("run:"
	# shelves); a port's script gets nothing
	echo "port: $1${2:+ $2}${3:+ (the in-game menu, $3)}" >> "$LOG"
	# The volume keys, which ZyperOS and Diatom read and which are both stopped
	# now (tools/portkeys.c); MENU held turns them to brightness, as in ZyperOS.
	# It steps ZyperOS's own levels, saved in its database, and shows the bar
	# over the port, in the theme the Appearance setting there makes; the
	# library database holds the time zone Auto's hour is read in
	local keys= still= turn= m sout= slater= disp=$4
	# No start again asked for yet (port_quit): a file left from before is
	# none
	rm -f "$PORT_RESTART_FILE"
	PORT_AGAIN=
	trap port_power USR1
	trap port_quit USR2
	# The Pixel 2's panel is a 480x640 portrait one mounted turned (below). A
	# landscape display is QEMU's (plastron's qemu_defconfig), where nothing
	# turns: the first mode of the first connector with one says which
	if [ "$HAS_PORT_TURN" = 1 ]; then
		for m in /sys/class/drm/card*-*/modes; do
			read -r m < "$m" 2> /dev/null && [ -n "$m" ] && break
		done
		[ "$m" = 480x640 ] && turn=270
	fi
	# The loading screen the launcher left (launch_port), held on the panel
	# until the port draws, so a minute of loading is not a black panel; the
	# same screen after a minute, saying how to leave (SPLASH_LATER, which a
	# splash older than 1.0.6 ignores); and the port's latest line at its
	# foot, so an install that runs for hours shows that it runs (SPLASH_OUT,
	# the letters and the box the launcher wrote, which a splash older than
	# 1.0.6 ignores too). splash follows this log from its end, the port's
	# output being in it, and passes over what goes in while the in-game menu
	# is up over the screen (SPLASH_OUT_PAUSE, there while it is: portkeys's).
	# One left by a portkeys that ended with its menu up would hold the line
	# back for the whole start, so none is kept from before.
	port_still
	# MP4 kept playing over it: the launcher's queue handed to a follower
	# (zyperos.elf --mp4-follow, src/mp4hand.h), which does what the
	# launcher does beside a Diatom game, and which portkeys stops while the
	# in-game menu is up (--follow), the menu's MP4 being the one then
	if [ "$5" = mp4 ]; then
		"$ZYPEROS_DIR/zyperos.elf" --mp4-follow >> "$LOG" 2>&1 &
		follow=$!
	fi
	# A program of its own (OpenBOR, the standalone Flycast, a port,
	# PortMaster) has the in-game menu on MENU too: the launcher names the
	# system's tag in the request, third, and portkeys shows the menu by
	# stopping the program and starting the launcher for the menu alone
	# (--menu: the launcher, the tag and the game; src/portmenu.h). A port's
	# game is its script, which its Controls pages are read beside
	if [ "$HAS_PORTKEYS" = 1 ] && [ -x "$ZYPEROS_DIR/portkeys" ]; then
		if [ -n "$3" ]; then
			"$ZYPEROS_DIR/portkeys" ${follow:+--follow "$follow"} \
				--menu "$ZYPEROS_DIR/zyperos.elf" "$3" "${2:-$1}" \
				"$USERDATA_PATH/zyperos.db" \
				"$SHARED_USERDATA_PATH/.zyperos/library.db" 2>> "$LOG" &
		else
			"$ZYPEROS_DIR/portkeys" "$USERDATA_PATH/zyperos.db" \
				"$SHARED_USERDATA_PATH/.zyperos/library.db" 2>> "$LOG" &
		fi
		keys=$!
	fi
	# The Pixel 2's panel is a 480x640 portrait one mounted turned. ZyperOS
	# and Diatom turn their own pictures; for anything else plastron's SDL2
	# hands out a landscape window and turns each frame onto the panel (its
	# kmsdrm rotation patch, HAS_PORT_TURN). And SDL's 2D renderer on GLES2:
	# its desktop GL one, which SDL prefers once it has OpenGL, draws nothing
	# but the clear color on the Pixel 2's Panfrost, so a port that draws with
	# SDL_Renderer was black.
	# The equalizer over the port's sound (DESIGN.md 11.60): while the
	# launcher's bands are not flat and the system has ZyperOS's plugin
	# (plastron's zyperos-eq), the port gets an ALSA config of its own whose
	# default runs through it into the same mix (eq-port.sh), for the
	# equalizer and the volume boost alike, the boost's gain coming from
	# portkeys during the port; no bands file, no plugin (or an empty one),
	# or no EQ_SLAVE, and the port starts as it always did, the plugin
	# nowhere in its chain. Two files:
	# its PCMs, and the one in its ALSA_CONFIG_PATH that loads them after
	# /etc/asound.conf, whose default would replace them (eq-port.sh)
	# A USB-C DAC in (card 1 with a USB id, as the launcher tells one,
	# plat_usb_audio_present): your PICO-8's sound into it, as Diatom's and
	# MP4's go, a cable winning in every Audio Output policy (src/audioout.c);
	# through the system's own chain for it (EQ_SLAVE_USB, device.sh's), so
	# its volume is "USB Playback Volume", which portkeys writes with every
	# step. That chain has a softvol stage, and rc.4 showed PICO-8 failing to
	# open one where 44100 Hz stereo opened it: so it is opened first as
	# PICO-8 opens it (zyperos.elf --alsa-try, src/alsatry.h), and PICO-8
	# stays in the mix, as before, when it does not open, or when
	# /etc/asound.conf has no such PCM. Your PICO-8 alone: what other ports
	# ask of ALSA is not known, so they stay in the mix. Chosen as it
	# starts: a DAC plugged in or out while it plays is heard at the next
	# start. A Bluetooth headset stays MP4's, as bluealsa gives a headset to
	# one program at a time
	local eqcfg= into=${EQ_SLAVE:-}
	if [ "$5" = mp4 ] && [ -n "${EQ_SLAVE_USB:-}" ] && [ -e /proc/asound/card1/usbid ] &&
	   grep -q "^pcm\.$EQ_SLAVE_USB[ {]" /etc/asound.conf 2> /dev/null; then
		into=$EQ_SLAVE_USB
	fi
	if [ -n "${EQ_SLAVE:-}" ] && [ -s /usr/lib/ladspa/zyperos_eq.so ] &&
	   sh "$ZYPEROS_DIR/eq-port.sh" --on; then
		eqcfg=/tmp/zyperos_port_eq.hook
		if [ "$into" != "$EQ_SLAVE" ] && ! { port_chain "$into" "$eqcfg" &&
		     ALSA_CONFIG_PATH=${ALSA_CONFIG_PATH:-/usr/share/alsa/alsa.conf}:$eqcfg \
		       "$ZYPEROS_DIR/zyperos.elf" --alsa-try >> "$LOG" 2>&1; }; then
			echo "port: its sound would not open into $into as PICO-8 opens it; into $EQ_SLAVE, as before" >> "$LOG"
			into=$EQ_SLAVE
		fi
		if port_chain "$into" "$eqcfg"; then
			echo "port: the equalizer over its sound (into $into)" >> "$LOG"
		else
			eqcfg=
		fi
	fi
	# And the system's Display row ("integer" for Sharp Pixels, its
	# display.<TAG> name), fourth: a "run:" script that draws to it reads
	# ZYPEROS_DISPLAY (your PICO-8's, sd/pixel2/PICO-8.sh). Started again
	# with the one the in-game menu picked, for as long as it asks
	# (port_quit), unless POWER came meanwhile; portkeys, the follower and
	# MP4 go on through it
	while :; do
		( cd "${1%/*}" &&
		  if [ -n "$turn" ]; then export SDL_KMSDRM_ROTATION=$turn; fi &&
		  if [ -n "$eqcfg" ]; then
			export ALSA_CONFIG_PATH=${ALSA_CONFIG_PATH:-/usr/share/alsa/alsa.conf}:$eqcfg
		  fi &&
		  ZYPEROS_DISPLAY=$disp SDL_RENDER_DRIVER=opengles2 exec setsid /bin/bash "$1" ${2:+"$2"} ) >> "$LOG" 2>&1 &
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
		[ -n "$PORT_AGAIN" ] && [ ! -e /tmp/zyperos_poweroff ] || break
		disp=$PORT_AGAIN
		PORT_AGAIN=
		echo "port: $1${2:+ $2} again, Display $disp" >> "$LOG"
		port_still
	done
	PORT_AGAIN=
	# PortMaster's run, for the launcher that starts next: when it began and
	# ended, and how it ended
	[ -n "$pm_start" ] && echo "$pm_start $(date +%s) $rc" > /tmp/zyperos_pm_ran
	PORT_PID=
	# Waited for: while the bar is up it holds a copy of the port's display,
	# and the launcher cannot take the display until that copy is gone
	[ -n "$keys" ] && kill "$keys" 2> /dev/null && wait "$keys" 2> /dev/null
	# The follower ends, its queue written for the launcher that comes next
	# (a stopped one goes on to take the TERM), and is waited for, 2 s at
	# most: before the menu's killall below, which would end it too
	if [ -n "$follow" ]; then
		kill -TERM "$follow" 2> /dev/null
		kill -CONT "$follow" 2> /dev/null
		i=0
		while [ $i -lt 20 ] && kill -0 "$follow" 2> /dev/null; do
			sleep 0.1
			i=$((i + 1))
		done
		kill -KILL "$follow" 2> /dev/null
		wait "$follow" 2> /dev/null
	fi
	# An in-game menu still up (a port that ended under it, the kernel's
	# doing) would hold the display the launcher is about to take
	[ -n "$3" ] && killall -TERM zyperos.elf 2> /dev/null
	trap - USR1 USR2
	[ -n "$still" ] && kill "$still" 2> /dev/null
	rm -f /tmp/zyperos_loading.raw /tmp/zyperos_loading_out /tmp/zyperos_menu_up "$PORT_RESTART_FILE"
	# A helper left behind would keep the buttons typing keys under the
	# shelf: gptokeyb
	killall -9 gptokeyb gptokeyb2 2> /dev/null
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

device_resident
start_resident
[ -e /tmp/zyperos_poweroff ] && rm -f /tmp/zyperos_poweroff
[ -e /tmp/zyperos_reboot ] && rm -f /tmp/zyperos_reboot

# The radios (device.sh's): each handheld's own way of bringing them up
device_radios &

# MENU+START held a second: whatever runs ends and the home comes back
# (tools/zyhome.c). Once, for as long as this script runs: it is the way out
# when the launcher, Diatom or a port is the thing that is stuck, so nothing
# of theirs may own it. An older payload has none, and goes without.
[ "$HAS_ZYHOME" = 1 ] && [ -x "$ZYPEROS_DIR/zyhome" ] && "$ZYPEROS_DIR/zyhome" >> "$LOG" 2>&1 &

# A Mac or PC leaves its own files on the card: ._ files, .DS_Store,
# Thumbs.db and desktop.ini anywhere, and the folders macOS writes at the top
# of any card it mounts, .Trashes included (Eric's choice, 2026-09-30: on a
# handheld "deleted" means gone). Swept after the launcher is up, so it costs
# the boot nothing.
( sleep 8
  rm -rf /mnt/SDCARD/.Trashes /mnt/SDCARD/.Spotlight-V100 /mnt/SDCARD/.fseventsd \
         /mnt/SDCARD/.TemporaryItems
  find /mnt/SDCARD \( -name '._*' -o -name '.DS_Store' -o -name 'Thumbs.db' \
       -o -name 'desktop.ini' \) -type f -exec rm -f {} \;
) > /dev/null 2>&1 &

# Five fast failures in a row: something ZyperOS reads at its start is broken,
# often by a power cut, and then by the card check after it (plastron's
# datapart), which takes a damaged file away. Before the power-off, launch.sh
# is set aside, so the next start copies ZyperOS again from the system image,
# as a new card's first start does (plastron's zyperos.init copies it
# whenever launch.sh is missing): ZyperOS's own files only, never the games,
# saves or settings. Once: a ZyperOS that still fails after that copy is left
# as it is, and the handheld goes off as before, the log saying so. A start
# that lasts, or a power-off asked for, takes the marker away.
START_FAILED=$USERDATA_PATH/start-failed
start_failed() {
	echo "launch: ZyperOS stopped $FAILS times in a row within 5 s of starting" >> "$LOG"
	if [ -f "$START_FAILED" ]; then
		echo "launch: it did after a fresh copy too; left as it is" >> "$LOG"
		return
	fi
	[ -f /usr/share/zyperos/card/ZyperOS/launch.sh ] || return
	echo "launch: the next start copies ZyperOS onto the card again" >> "$LOG"
	date > "$START_FAILED"
	mv -f "$ZYPEROS_DIR/launch.sh" "$ZYPEROS_DIR/launch.sh.failed"
}

# The launcher, restarted if it exits on its own; five fast failures in a row
# and the device powers off rather than looping on a broken build.
cd "$ZYPEROS_DIR"
FAILS=0
while : ; do
	# The panel lit, where a system tool lights it (BACKLIGHT, device.sh's):
	# the launcher turns it off on its way out (a power-off, Auto Off), and
	# the next pass needs it back on
	[ -n "$BACKLIGHT" ] && [ -x "$BACKLIGHT" ] && "$BACKLIGHT" on
	now_s; START=$NOW
	./zyperos.elf >> "$LOG" 2>&1
	[ -f /tmp/zyperos_poweroff ] && break
	# System Update's Restart Now: the switch to the new version runs at
	# the next start (the image's update-apply.sh)
	[ -f /tmp/zyperos_reboot ] && break
	# Computer Connected > USB Drive (src/main.c, usb_drive): the card is
	# the computer's until it ejects it. usbdrive, the image's own, takes
	# over from here: it stops what still holds the card (this script's
	# helpers too), unmounts it, exports it, and when it is back runs this
	# script again. Nothing of the card's may run meanwhile, this shell
	# included, hence the exec, from /, with nothing open on the card.
	if [ "$HAS_USBDRIVE" = 1 ] && [ -f /tmp/zyperos_usbdrive ] && [ -x /usr/sbin/usbdrive ]; then
		rm -f /tmp/zyperos_usbdrive
		kill $(jobs -p) 2> /dev/null
		cd /
		exec /usr/sbin/usbdrive /tmp/zyperos_usbdrive.d "$ZYPEROS_DIR/launch.sh" \
			< /dev/null > /dev/null 2>&1
	fi
	# The charging screen's Hold POWER to start (src/charging.h): ZyperOS
	# afresh, at once, as a start from off is
	if [ -f /tmp/zyperos_restart ]; then
		rm -f /tmp/zyperos_restart
		FAILS=0
		start_resident
		continue
	fi
	if [ "$HAS_PORTS" = 1 ] && [ -f "$PORT_REQUEST" ]; then
		# The script, the game, the menu's tag, the system's Display row and
		# whether MP4 plays on ("mp4", your PICO-8's), a tab between each
		# (launch_port). Split by hand: read would run the two tabs around a
		# port's empty game together, as it does any whitespace in IFS
		TAB=$(printf '\t')
		IFS= read -r REQ < "$PORT_REQUEST"
		rm -f "$PORT_REQUEST"
		PORT=${REQ%%"$TAB"*}; GAME=; TAG=; DISP=; KEEP=
		case $REQ in *"$TAB"*)
			REQ=${REQ#*"$TAB"}
			GAME=${REQ%%"$TAB"*}
			case $REQ in *"$TAB"*)
				REQ=${REQ#*"$TAB"}
				TAG=${REQ%%"$TAB"*}
				case $REQ in *"$TAB"*)
					REQ=${REQ#*"$TAB"}
					DISP=${REQ%%"$TAB"*}
					case $REQ in *"$TAB"*) KEEP=${REQ#*"$TAB"} ;; esac
					;;
				esac
				;;
			esac
			;;
		esac
		run_port "$PORT" "$GAME" "$TAG" "$DISP" "$KEEP"
		# Its play time, closed now that it has ended (src/stats.h, "a
		# program of its own"): the launcher handed the session over as it
		# handed the program over, and this ends it, before a power-off
		# below too, so the game is on Recents with its time however it
		# ended. A start of the launcher with nothing on the screen.
		"$ZYPEROS_DIR/zyperos.elf" --port-played >> "$LOG" 2>&1
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
		[ -f "$START_FAILED" ] && rm -f "$START_FAILED"
	fi
	sleep 1
	# Bring the emulator back if it died, before the launcher restarts. Here
	# and not at the top of the loop: the first pass would ask the instant
	# after the start above, while the new process is still a copy of this
	# shell, and start a second Diatom. Measured 2026-10-01, two of them.
	start_resident
done
if [ $FAILS -ge 5 ]; then start_failed; else rm -f "$START_FAILED"; fi
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
	[ "$HAS_OFF_PENDING" = 1 ] && : > "$USERDATA_PATH/off-pending"
	sync
	poweroff
fi
