#!/bin/sh
# A menu's open, close and swap, as the launcher draws them, at their ends.
#
#     make check-menushots
#
# tools/menumotion-check.c holds the motion as numbers: where a panel is at
# each moment, which panel leaves and when. Nothing there draws, so nothing
# there sees a pass that leaves a panel a shade off at rest, a scrim that
# does not come all the way in or go all the way out, or a leave that ends
# on something other than what is under it. So this takes the shots that
# --shot-at presses on --menu (src/main.c, shot_motion) and holds each end
# of each motion to the picture it must be, byte for byte:
#
#   - --open at quick is the menu at rest, and at 0 the shelf with nothing
#     on it: the panel and the scrim come all the way, from nothing. On the
#     games shelf, whose system's menu has no Zyper: in the top menu he moves
#     on a clock of his own from the open, and is not at rest at quick;
#   - --close at 0 is the top menu at rest, and at exit the shelf with
#     nothing on it;
#   - --swap at exit and quick is MENU > Controls at rest, and nothing of the
#     menu it replaced;
#   - and each is another picture halfway, so a flag the shot does not know
#     is not taken for a motion that ends where it began.
#
# The host build's --shot, drawn by SDL's software renderer, so it needs
# SDL2 and skips cleanly without it, as check-flip does. The card is a scratch
# tree under build-native/menushots-check: systems.cfg as shipped and a Game
# Boy shelf of one empty file.
set -u

D=build-native/menushots-check
BIN=build-native/zyperos

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then
	echo "skipped: no SDL2 here to build the launcher with"
	exit 0
fi
if [ ! -x "$BIN" ]; then
	echo "FAIL no $BIN: make native first" >&2
	exit 1
fi
QUICK=$(sed -n 's/^#define DS_MS_QUICK \([0-9][0-9]*\).*/\1/p' src/design.h)
EXIT=$(sed -n 's/^#define DS_MS_EXIT \([0-9][0-9]*\).*/\1/p' src/design.h)
if [ -z "${QUICK:-}" ] || [ -z "${EXIT:-}" ]; then
	echo "FAIL src/design.h names no DS_MS_QUICK or DS_MS_EXIT" >&2
	exit 1
fi

rm -rf "$D"
mkdir -p "$D/root" "$D/Roms/Game Boy" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
: > "$D/Roms/Game Boy/Tetris.gb"

shot() {
	out=$1
	shift
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	SDL_AUDIODRIVER=dummy \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" --shot "$D/$out.png" "$@" >>"$D/log" 2>&1
}

fails=0

# same NAME A B: shots A and B (their names) are one picture
same() {
	if [ ! -s "$D/$2.png" ] || [ ! -s "$D/$3.png" ]; then
		echo "  FAIL $1: a shot was not written (see $D/log)"
		fails=$((fails + 1))
	elif ! cmp -s "$D/$2.png" "$D/$3.png"; then
		echo "  FAIL $1 ($D/$2.png against $D/$3.png)"
		fails=$((fails + 1))
	fi
}

# other NAME A B: and these two are not
other() {
	if [ ! -s "$D/$2.png" ] || [ ! -s "$D/$3.png" ]; then
		echo "  FAIL $1: a shot was not written (see $D/log)"
		fails=$((fails + 1))
	elif cmp -s "$D/$2.png" "$D/$3.png"; then
		echo "  FAIL $1 ($D/$2.png is $D/$3.png)"
		fails=$((fails + 1))
	fi
}

echo "a menu's open, close and swap end where they must, and move between"
for th in light dark; do
	shot "shelf-$th" --theme "$th"
	shot "menu-$th" --theme "$th" --menu
	shot "games-$th" --theme "$th" --screen games
	shot "games-menu-$th" --theme "$th" --screen games --menu
	shot "controls-$th" --theme "$th" --controls

	shot "open-0-$th" --theme "$th" --screen games --menu --open --shot-at 0
	shot "open-half-$th" --theme "$th" --screen games --menu --open --shot-at $((QUICK / 2))
	shot "open-end-$th" --theme "$th" --screen games --menu --open --shot-at "$QUICK"
	same "$th: the open starts from the shelf, with nothing on it" "open-0-$th" "games-$th"
	same "$th: the open is the menu at rest at quick" "open-end-$th" "games-menu-$th"
	other "$th: and is neither halfway" "open-half-$th" "games-menu-$th"

	shot "close-0-$th" --theme "$th" --menu --close --shot-at 0
	shot "close-half-$th" --theme "$th" --menu --close --shot-at $((EXIT / 2))
	shot "close-end-$th" --theme "$th" --menu --close --shot-at "$EXIT"
	same "$th: the close starts from the menu at rest" "close-0-$th" "menu-$th"
	same "$th: the close is the shelf, with nothing on it, at exit" "close-end-$th" "shelf-$th"
	other "$th: and is neither halfway" "close-half-$th" "shelf-$th"
	other "$th: (not the menu either)" "close-half-$th" "menu-$th"

	shot "swap-half-$th" --theme "$th" --menu --swap --shot-at $((EXIT / 2))
	shot "swap-end-$th" --theme "$th" --menu --swap --shot-at $((EXIT + QUICK))
	same "$th: the swap is Controls at rest at exit and quick" "swap-end-$th" "controls-$th"
	other "$th: and is not halfway" "swap-half-$th" "controls-$th"
done

if [ "$fails" -ne 0 ]; then
	echo "menushots: $fails FAILED"
	exit 1
fi
echo "ok: the open comes from nothing to the menu at rest, the close goes from it to the"
echo "bare shelf, and the swap lands on Controls at rest; each moves between"
