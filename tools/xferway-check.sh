#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-xferway: File Transfer's way in and heading put the address
# first and the name under it or beside the PIN, over the USB cable as over
# Wi-Fi (src/xfer_screen.c, xs_way_set and hare_head; DESIGN.md 11.35).
# Over the cable the name went first until 2026-11-23, and Windows does not
# resolve it there (Discord), while the address works on every computer.
#
# Read off the real launcher's shots (--hare-steps) through the lines it
# says it drew ("fit: drawn: ...", the host build's I18N_PSEUDO): the
# address is drawn before the name. The way in writes the cable's name with
# http://, the heading bare, as Wi-Fi's. With no name the address is alone.
# Needs the launcher built, which needs SDL2, and skips cleanly without it.
set -u

BIN=build-native/zyperos
D=build-native/xferway-card

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then
	echo "skipped: no SDL2 here to build the launcher with"
	exit 0
fi
if [ ! -x "$BIN" ]; then
	echo "FAIL no $BIN: make native first" >&2
	exit 1
fi
rm -rf "$D"
mkdir -p "$D/root" "$D/Roms" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1

drawn() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" --shot "$D/shot.png" --hare-steps "$@" 2>&1 |
		sed -n 's/^fit: drawn: //p'
}

fails=0
# want WAY STATE [noname] -- ADDRESS SECOND: ADDRESS is drawn, then SECOND
# right after it (empty: nothing of the name drawn at all)
want() {
	what=$1
	addr=$2
	second=$3
	shift 3
	lines=$(drawn "$@")
	a=$(printf '%s\n' "$lines" | grep -n -F -x "$addr" | head -n 1 | cut -d: -f1)
	if [ -z "$a" ]; then
		echo "  FAIL $what: \"$addr\" never drawn"
		fails=$((fails + 1))
		return
	fi
	if [ -n "$second" ]; then
		got=$(printf '%s\n' "$lines" | sed -n "$((a + 1))p")
		if [ "$got" != "$second" ]; then
			echo "  FAIL $what: after \"$addr\" came \"$got\", not \"$second\""
			fails=$((fails + 1))
			return
		fi
	elif printf '%s\n' "$lines" | grep -q 'zyperos\.local'; then
		echo "  FAIL $what: the name drawn with no name to show"
		fails=$((fails + 1))
		return
	fi
	n=$(printf '%s\n' "$lines" | grep -n 'zyperos\.local' | head -n 1 | cut -d: -f1)
	if [ -n "$n" ] && [ "$n" -lt "$a" ]; then
		echo "  FAIL $what: the name drawn before the address"
		fails=$((fails + 1))
		return
	fi
	echo "  ok   $what"
}

echo "the way in"
want "USB: the address, then http://zyperos.local" "10.42.0.1" "http://zyperos.local" usb browser
want "USB, a browser with the page: the same" "10.42.0.1" "http://zyperos.local" usb pin
want "USB with no name: the address alone" "10.42.0.1" "" usb browser noname
want "Wi-Fi: the address, then zyperos.local" "192.168.1.42" "zyperos.local" wifi browser
echo "the heading, while a step stalls"
want "USB: the address over the PIN and the name" "10.42.0.1" "PIN 4071 · zyperos.local" usb noaddress
want "Wi-Fi: the same" "192.168.1.42" "PIN 4071 · zyperos.local" wifi noaddress

rm -rf "$D"
[ "$fails" = 0 ] && echo "ok: the address first and the name second, over the cable as over Wi-Fi" && exit 0
exit 1
