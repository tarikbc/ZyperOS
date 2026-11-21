#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# A screenshot felt and heard as it is taken (DESIGN.md 9 and 11.61), the two
# places a shot is taken:
#
#   over your PICO-8   portkeys's shutter player (tools/pkshutter.c) on this
#                      computer's alsa-lib, into stand-ins for the system's
#                      mix and the DAC's that write down what reaches them,
#                      with PICO-8's own stream playing beside it
#                      (tools/shotfeel-alsa.c); Linux, with alsa-lib's headers
#   in a Diatom game   diatom/test/shotfeel.py against Diatom's desktop build
#                      and the stub core: one buzz a shot at the game's
#                      Rumble level and none at Off, HOTKEY key=shot as it is
#                      taken and before its file, the file the game's frame
#                      pixel for pixel, the game's sound as it was; when
#                      diatom/build/desktop is there (check-diatom builds it
#                      and runs it too)
#
# What a shot asks for over PICO-8, from the launcher's settings, is
# check-pkkeys's (tools/pkfeel.c); the shutter as the launcher plays it, and
# none under UI Sounds Off, check-uisound's and check-resident's.
#
#     sh tools/shotfeel-check.sh            (make check-shotfeel)
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
ALSA_CONF=${ALSA_CONF:-/usr/share/alsa/alsa.conf}
if [ "$(uname -s)" != Linux ]; then
	echo "shotfeel: PICO-8's shutter skipped, no ALSA on this computer"
elif ! pkg-config --exists alsa 2> /dev/null; then
	echo "shotfeel: PICO-8's shutter skipped, no alsa-lib headers (pkg-config alsa)"
elif [ ! -f "$ALSA_CONF" ]; then
	echo "shotfeel: PICO-8's shutter skipped, no $ALSA_CONF"
else
	W=$(mktemp -d)
	trap 'rm -rf "$W"' EXIT
	$CC -O1 -g -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -I"$REPO/src" -I"$REPO/tools" -o "$W/try" \
		"$REPO/tools/shotfeel-alsa.c" "$REPO/tools/pkshutter.c" "$REPO/src/pcm_out.c" "$REPO/src/uisound.c" \
		"$REPO/src/audioout.c" "$REPO/src/i18n.c" "$REPO/src/i18n_tables.c" -lasound -lpthread -lm
	# The Pixel 2's shape (plastron's /etc/asound.conf): default a plug into
	# the mix, the DAC's its own; each mix a stand-in that writes down what
	# reaches it, and PICO-8's stream another
	mkdir -p "$W/xdg/alsa"
	cat > "$W/xdg/alsa/asoundrc" <<CONF
pcm.!default { type plug; slave.pcm "mixed" }
pcm.mixed { type file; slave.pcm "null"; file "$W/mix.raw"; format "raw" }
pcm.usb { type plug; slave.pcm { type file; slave.pcm "null"; file "$W/usb.raw"; format "raw" } }
pcm.game { type plug; slave.pcm { type file; slave.pcm "null"; file "$W/game.raw"; format "raw" } }
CONF
	echo "shotfeel: PICO-8's shutter (tools/pkshutter.c) on alsa-lib:"
	XDG_CONFIG_HOME=$W/xdg HOME=$W ALSA_CONFIG_PATH=$ALSA_CONF "$W/try" "$REPO/res/sounds" "$W"
fi
echo "shotfeel: a Diatom game's shot (diatom/test/shotfeel.py):"
cd "$REPO/diatom" && python3 test/shotfeel.py
