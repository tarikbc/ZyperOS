#!/bin/sh
# The status corner keeps its place: clear of the focused system card in
# the horizontal row, with a long games title and every panel a grid step under
# its row, and MENU > Clock turns 12-hour into 24-hour and back, saved.
#
#     make check-corner
#
# The corner (DESIGN.md 11.12, src/main.c's draw_corner) is drawn last on the
# shelf, on Now Playing and over a paused game, at x 1000 back to its clock,
# on y 24 to 48. What stands near it is laid out elsewhere: the systems card
# by the horizontal layout (src/coverflow.c), the games title by TITLE_TOP,
# a panel by menu_draw_ex's MENU_TOP, the clock's place by draw_corner. A
# shot checks its own frame for all of them (shot_corner_clear: the clock 16
# from its marks, the corner off the focused card, the title's capitals and
# a panel's top 8 under the row) and exits 3 with a "CORNER:" line when one
# is not. So this shoots the widest corner, "00:00" in 24-hour with all three
# marks, on the screens that come nearest: the systems row and a long title
# sliding under it on the horizontal row, the ZyperOS menu with its first row and
# its last selected, a Nintendo DS game's ten-row menu over a paused game both
# ways, and the achievement list over the shelf and over a paused game (as
# the in-game menu opens it); in both themes. And it presses Right on the
# Clock row twice through the menu's own handler (--press), reading the
# setting back from the settings database (--dump) after each. And one shot
# with no --status, which must draw no clock: the host's would change it
# every minute.
#
# Needs the launcher built, which needs SDL2, and skips cleanly without it,
# as check-flip does. The card is a scratch tree under
# build-native/corner-check: systems.cfg as shipped, a NES shelf with a long
# title and a Game Boy one, every game drawn as its generated card.
set -u

D=build-native/corner-check
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
mkdir -p "$D/root" "$D/Roms/NES" "$D/Roms/Game Boy" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
for g in "Castlevania: Symphony of the Night" Contra Metroid; do
	: >"$D/Roms/NES/$g.nes" || exit 1
done
: >"$D/Roms/Game Boy/Tetris.gb" || exit 1
: >"$D/log"

run() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" "$@"
}

WIDEST="00:00 24h wifi=3 bt batt=100 chg"
fails=0
shot() {
	name=$1
	shift
	run --shot "$D/$name.png" "$@" --status "$WIDEST" >"$D/one.log" 2>&1
	st=$?
	cat "$D/one.log" >>"$D/log"
	if grep -q "CORNER:" "$D/one.log"; then
		echo "  FAIL $name:"
		grep "CORNER:" "$D/one.log" | sed 's/^ */    /'
		fails=$((fails + 1))
	elif [ "$st" -ne 0 ] || [ ! -s "$D/$name.png" ]; then
		echo "  FAIL $name: the shot failed, exit $st (see $D/log)"
		tail -n 3 "$D/one.log" | sed 's/^/    /'
		fails=$((fails + 1))
	fi
}

echo "the widest corner clear of the card, the title and every panel"
for theme in light dark; do
	shot "systems-$theme" --theme "$theme"
	shot "games-$theme" --theme "$theme" --screen games --phase 3200
	shot "menu-first-$theme" --theme "$theme" --menu "Play Time"
	shot "menu-last-$theme" --theme "$theme" --menu "About ZyperOS"
	shot "gamemenu-first-$theme" --theme "$theme" --screen games --gamemenu 0 dual
	shot "gamemenu-last-$theme" --theme "$theme" --screen games --gamemenu 10 dual
	shot "cheevos-$theme" --theme "$theme" --cheevos-screen 0
	shot "cheevos-ingame-$theme" --theme "$theme" --screen games --cheevos-screen 0 --ingame
done

# And no clock at all without --status: a shot of the host's would change
# every minute (the shot fails with CORNER if one is drawn)
run --shot "$D/no-status.png" >"$D/one.log" 2>&1
st=$?
cat "$D/one.log" >>"$D/log"
if [ "$st" -ne 0 ]; then
	echo "  FAIL a shot with no --status, exit $st:"
	grep "CORNER:" "$D/one.log" | sed 's/^ */    /'
	fails=$((fails + 1))
fi

echo "MENU > Clock turns over and is saved"
for want in 24h 12h; do
	run --shot "$D/clock-$want.png" --menu Clock --press right >"$D/one.log" 2>&1
	cat "$D/one.log" >>"$D/log"
	got=$(run --dump 2>/dev/null | awk '$1 == "clock" { print $2 }')
	if [ "$got" != "$want" ]; then
		echo "  FAIL Right on Clock saved \"${got:-nothing}\", not $want"
		fails=$((fails + 1))
	fi
done

# MENU > Battery Icon > Time Left writes "9 h 55 min" where "100%" went: the
# widest of it, in every language (Chinese's is the longest), with every mark
# on and File Transfer's too, must still fit the room the corner keeps for it
# (STATUS_PCT_MAX) and stay clear of the card, the title and the panels. The
# launcher says CORNER: when the text is wider than its room.
echo "the widest Time Left clear of the card, the title and every panel, in every language"
WIDEST_TIME="00:00 24h wifi=3 bt batt=100 chg xfer=on eta=595 icon=time"
for lang in en zh-Hans es pt-BR fr de ja it; do
	for screen in systems games menu-last; do
		case $screen in
		systems)   extra="" ;;
		games)     extra="--screen games --phase 3200" ;;
		menu-last) extra="--menu About\ ZyperOS" ;;
		esac
		# shellcheck disable=SC2086
		eval run --shot "$D/time-$lang-$screen.png" --theme light --lang $lang $extra \
		     --status "\"$WIDEST_TIME\"" >"$D/one.log" 2>&1
		st=$?
		cat "$D/one.log" >>"$D/log"
		if grep -q "CORNER:" "$D/one.log"; then
			echo "  FAIL $lang $screen:"
			grep "CORNER:" "$D/one.log" | sed 's/^ */    /'
			fails=$((fails + 1))
		elif [ "$st" -ne 0 ] || [ ! -s "$D/time-$lang-$screen.png" ]; then
			echo "  FAIL $lang $screen: the shot failed, exit $st (see $D/log)"
			fails=$((fails + 1))
		fi
	done
done

echo "MENU > Battery Icon goes round Bars, Percentage, Time Left and back, saved"
# Right goes forward from Bars; each press is its own process through the
# row's real handler, the setting read back from the database. Left goes back,
# and from Bars round to Time Left.
rm -f "$D"/userdata/*.db "$D"/userdata/*.sqlite 2>/dev/null
step() {
	key=$1
	want=$2
	run --shot "$D/battery-$want.png" --menu "Battery Icon" --press "$key" >"$D/one.log" 2>&1
	cat "$D/one.log" >>"$D/log"
	got=$(run --dump 2>/dev/null | awk '$1 == "battery_icon" { print $2 }')
	if [ "$got" != "$want" ]; then
		echo "  FAIL $key saved \"${got:-nothing}\", not $want"
		fails=$((fails + 1))
	fi
}
step right percentage
step right time
step right bars
step left time
step left percentage
step left bars
step left time
step right bars

if [ "$fails" -ne 0 ]; then
	printf '\n%d FAILED\n' "$fails"
	exit 1
fi
printf '\nok: the corner keeps clear of the card, the title and the panels in\n'
printf 'the horizontal row and both themes, Time Left fits in every language,\n'
printf 'and Clock and Battery Icon turn over and are saved\n'
