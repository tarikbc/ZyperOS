#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-xferhint: File Transfer's steps and their hint, for each step
# that can stall, over the USB cable and over Wi-Fi, drawn whole by the real
# launcher (--hare-steps, which draws through xs_view_draw, the function the
# screen draws each view with).
#
# The steps are a menu whose last row is the legend, a note (MENU_NOTE, its
# value MENU_NOTE_MARK, (const char *)1). From 2026-11-13 the screen drew
# them as the news, Flash Drive's page, whose foot took that last row's
# value for a key's name: ui_button_width read address 0x1 and the launcher
# stopped with SIGSEGV, 10 s into a USB File Transfer with no computer on
# the cable (my R36S on 1.1.1-r36s-test.3, 2026-11-28; every device has the
# same 10 s for a computer that does not answer, and Wi-Fi with no network).
#
# Each state: the launcher exits 0 and writes its picture. Needs the
# launcher built, which needs SDL2, and skips cleanly without it.
set -u

BIN=build-native/zyperos
D=build-native/xferhint-card

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

fails=0
# shot WAY STATE: the launcher draws it, exits 0, and the picture is there
shot() {
	rm -f "$D/shot.png"
	# in a subshell, so a crash's "Segmentation fault" goes to out.txt too
	( ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	  ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	  ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	  ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" --shot "$D/shot.png" --hare-steps "$@"
	) > "$D/out.txt" 2>&1
	rc=$?
	if [ "$rc" != 0 ]; then
		echo "  FAIL $*: the launcher exited $rc$( [ "$rc" -gt 128 ] && echo " (signal $((rc - 128)))")"
		grep -m 3 'crash:' "$D/out.txt" | sed 's/^/       /'
		fails=$((fails + 1))
	elif [ ! -s "$D/shot.png" ]; then
		echo "  FAIL $*: no picture"
		fails=$((fails + 1))
	else
		echo "  ok   $*"
	fi
}

echo "over the cable, each stalled step's hint"
for s in nocomputer noanswer nodriver noaddress replug; do shot usb "$s"; done
echo "over Wi-Fi"
for s in nonetwork noaddress; do shot wifi "$s"; done
echo "and the views with no hint, beside them"
shot usb taking
shot usb browser
shot wifi browser

rm -rf "$D"
[ "$fails" = 0 ] && echo "ok: every stalled step's hint drawn, over the cable and over Wi-Fi" && exit 0
exit 1
