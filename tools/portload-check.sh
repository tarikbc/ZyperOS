#!/bin/sh
# The port loading screen keeps its words and its band readable over any
# cover, and keeps out of Zyper's frame.
#
#     make check-portload
#
# The screen (src/main.c, port_loading_draw) puts its words and the system's
# band on the cover itself, blurred to fill the screen under the theme's
# ground. The lab's scrim, 70% at the top to 90% at the foot, holds **text**
# over any cover but not **text-dim**: a white cover under Salt & Pepper's
# ground left the system's name at 2.4:1, a black one under Beard's at 2.5:1,
# where DESIGN.md 3.2 owes text 4.5:1 and a mark 3:1 against every ground it
# is drawn on. So the screen reads back what it drew under each line before
# the words go on, and raises the scrim until each has its 4.5:1. The band,
# a mark 48 by 6, does not raise it - for the band's sake the scrim took
# Beard's backdrop away over any dark cover - but keeps its place, read back
# the same way, while its color moves toward **text** until it has its 3:1.
# Whatever is still short it names on stderr ("port: loading screen short:
# ..."), and --portload then fails the shot. The same shot fails when the
# words or the cover reach Zyper's frame, where splash draws his loop.
#
# Nothing but a running launcher can show that this holds: the contrast is
# of pixels the renderer drew, the cover's lift among them. So this shoots
# the screen with the host build for the two covers that ask most of the
# scrim, a white one and a black one, in both themes - white under Salt &
# Pepper and black under Beard are the ones the lab's scrim failed - and
# fails on a short line, on the frame reached, and on a shot that failed or
# was not written. And a Nintendo DS game, its cover black: Nintendo DS's
# silver is pale on Beard, 2.19:1 as a mark on the plain ground, so no scrim
# could hold its band and only its color can.
#
# It needs the launcher built, which needs SDL2, and skips cleanly without
# it, as check-flip does. The card is a scratch tree under
# build-native/portload-check, made here: systems.cfg as shipped, a Ports
# shelf of two ports and a Nintendo DS shelf of one game ("run:", which hands
# its game over as a port is, and draws this screen), each with a solid
# cover 7 pixels by 10, written below
# byte for byte (a PNG of one color, made once with Python's zlib) so the
# check needs no image tool. Drawn by SDL's software renderer, as check-flip
# draws, so the pixels read back are the same on any host.
set -u

D=build-native/portload-check
BIN=build-native/zyperos

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then
	echo "skipped: no SDL2 here to build the launcher with"
	exit 0
fi
if [ ! -x "$BIN" ]; then
	echo "FAIL no $BIN: make native first" >&2
	exit 1
fi

rm -rf "$D"
mkdir -p "$D/root" "$D/Roms/Ports/.media" "$D/Roms/Nintendo DS/.media" "$D/userdata" \
	"$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
for g in Whiteout Blackout; do printf '#!/bin/sh\n' >"$D/Roms/Ports/$g.sh" || exit 1; done
: >"$D/Roms/Nintendo DS/Silver.nds" || exit 1
# 7x10, 8-bit RGB, every pixel 255,255,255; and every pixel 0,0,0
printf '\211\120\116\107\015\012\032\012\000\000\000\015\111\110\104\122\000\000\000\007\000\000\000\012\010\002\000\000\000\367\256\323\132\000\000\000\017\111\104\101\124\170\332\143\370\217\015\060\014\013\121\000\347\327\321\057\055\374\160\362\000\000\000\000\111\105\116\104\256\102\140\202' \
	>"$D/Roms/Ports/.media/Whiteout.png" || exit 1
printf '\211\120\116\107\015\012\032\012\000\000\000\015\111\110\104\122\000\000\000\007\000\000\000\012\010\002\000\000\000\367\256\323\132\000\000\000\014\111\104\101\124\170\332\143\140\030\276\000\000\000\334\000\001\317\071\175\114\000\000\000\000\111\105\116\104\256\102\140\202' \
	>"$D/Roms/Ports/.media/Blackout.png" || exit 1
cp "$D/Roms/Ports/.media/Blackout.png" "$D/Roms/Nintendo DS/.media/Silver.png" || exit 1
: >"$D/log"

fails=0
echo "the words keep 4.5:1 and the band 3:1 over a white and a black cover, out of Zyper's frame"
for game in Whiteout Blackout Silver; do
	for theme in dark light; do
		out=$D/$game-$theme.png
		ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
		ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
		ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
		ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" --shot "$out" --theme "$theme" \
			--portload "$game" >"$D/one.log" 2>&1
		st=$?
		cat "$D/one.log" >>"$D/log"
		said=$(sed -n 's/^port: loading screen in [0-9]* ms, //p' "$D/one.log")
		echo "  $game, $theme: ${said:-nothing said}"
		if grep -q "loading screen short\|reaches Zyper's frame\|no game called" "$D/one.log"; then
			echo "  FAIL $game, $theme:"
			grep "loading screen short\|reaches Zyper's frame\|no game called" "$D/one.log" | sed 's/^/    /'
			fails=$((fails + 1))
		elif [ "$st" -ne 0 ]; then
			echo "  FAIL $game, $theme: the shot failed, exit $st (see $D/log)"
			tail -n 3 "$D/one.log" | sed 's/^/    /'
			fails=$((fails + 1))
		elif [ ! -s "$out" ] || [ -z "$said" ]; then
			echo "  FAIL $game, $theme: no shot, or no contrast read (see $D/log)"
			fails=$((fails + 1))
		fi
	done
done

if [ "$fails" -ne 0 ]; then
	printf '\n%d FAILED\n' "$fails"
	exit 1
fi
printf '\nok: over a white and a black cover in both themes, and a Nintendo DS game,\n'
printf 'every line at 4.5:1, the band at 3:1, and nothing of the words or the cover\n'
printf 'in Zyper'"'"'s frame\n'
