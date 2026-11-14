#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The in-game menu's Controls page, and the home's systems in A to Z order,
# drawn by the host launcher (My picks, 2026-11-06,
# docs/design/mock/controls-lab.html):
#
#     make check-controls-shots
#
# Every page of a few systems' Controls is drawn in both themes, over a
# paused game, and each must sit still: a list with no cursor that does not
# fit its panel scrolls itself (menu_draw), which is wrong for a page read a
# button at a time. tools/controls-check.c holds the rows to four a page;
# this is the picture that says four is what fits. The pages differ from one
# another, the themes differ, and the in-game menu has its Controls row.
#
# Then the order: a new card's home is A to Z, MENU > System Order turns it
# to systems.cfg's (release dates) and back, read from the launcher's own
# scan lines.
set -u

D=build-native/controls-shots-check
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
mkdir -p "$D/root" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
for g in "Genesis/Sonic the Hedgehog 2.md" "Nintendo 64/Mario Kart 64.z64" \
         "Nintendo DS/Mario Kart DS.nds" "NES/Zelda no Densetsu.fds" \
         "Neo Geo Pocket Color/Metal Slug.ngc" "Game Boy/Tetris.gb" \
         "PlayStation/Ridge Racer.chd" "PICO-8/Celeste.p8"; do
	mkdir -p "$D/Roms/$(dirname "$g")" || exit 1
	: >"$D/Roms/$g" || exit 1
done
# The made-up ports (port.json, keymaps, READMEs) on the Ports shelf, for
# their pages here and in the fit test below
cp -R tools/fixtures/ports "$D/Roms/Ports" || exit 1
: >"$D/log"

run() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	SDL_AUDIODRIVER=dummy \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="${SHARED:-$D/shared}" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" "$@"
}

fails=0
fail() { echo "  FAIL $*"; fails=$((fails + 1)); }

shot() {
	name=$1
	shift
	run --shot "$D/$name.png" "$@" --status "20:41 24h wifi=3 batt=80" >>"$D/log" 2>&1
	[ -s "$D/$name.png" ] || fail "$name: the shot was not written (see $D/log)"
}

same()  { cmp -s "$D/$2.png" "$D/$3.png" || fail "$1 ($D/$2.png against $D/$3.png)"; }
other() { cmp -s "$D/$2.png" "$D/$3.png" && fail "$1 ($D/$2.png is $D/$3.png)"; }

echo "the in-game Controls pages, still, each its own"
# tag:pages - Genesis's buttons, then Hotkeys and In a game; the Nintendo 64
# and the DS a page of their own between (C buttons, two screens); the
# PlayStation's buttons two pages, its faces drawn as their shapes
for sp in MD:3 N64:4 NDS:4 NES:3 NGPC:3 PS1:4 PICO8:3; do
	tag=${sp%:*}
	pages=${sp#*:}
	for th in light dark; do
		p=0
		while [ "$p" -lt "$pages" ]; do
			shot "$tag-$p-$th" --theme "$th" --screen games --gamecontrols "$p" "$tag"
			# the rows are the same in both themes, so one is enough to
			# say whether they fit
			if [ "$th" = light ]; then
				shot "$tag-$p-later" --theme "$th" --screen games \
				     --gamecontrols "$p" "$tag" --shot-at 4000
				same "$tag page $p moves by itself" "$tag-$p-$th" "$tag-$p-later"
			fi
			if [ "$p" -gt 0 ]; then
				other "$tag page $p ($th) is page $((p - 1))" "$tag-$p-$th" "$tag-$((p - 1))-$th"
			fi
			p=$((p + 1))
		done
		# past the last page is the first, as Left/Right comes round
		shot "$tag-past-$th" --theme "$th" --screen games --gamecontrols "$pages" "$tag"
		same "$tag: a page past the end is the first ($th)" "$tag-past-$th" "$tag-0-$th"
	done
	other "$tag: the themes are the same picture" "$tag-0-light" "$tag-0-dark"
done
# The Pixel 2's brightness is MENU with the volume keys, and its page says so
shot "MD-2-pixel" --theme light --screen games --gamecontrols 2 MD --fn-volume
other "In a game names the Pixel 2's brightness keys" "MD-2-pixel" "MD-2-light"

echo "a port's Controls pages, each its own"
# Star Farm (tools/fixtures/ports): its README's words, two pages of keys,
# In a game, About. The shelf opens on it, the only port on it for these
# (the others back for the fit test); About is prose and may scroll, so it
# alone is not held still
mkdir -p "$D/aside" && mv "$D/Roms/Ports/Lone Key.sh" "$D/Roms/Ports/lonekey" "$D/Roms/Ports/Odd Keys.sh" \
	"$D/Roms/Ports/oddkeys" \
	"$D/Roms/Ports/Plain.sh" "$D/aside/" || exit 1
port_shot() { shot "$@"; }
p=0
while [ "$p" -lt 5 ]; do
	port_shot "port-$p" --theme light --screen games --gamecontrols "$p" PRT
	if [ "$p" -lt 4 ]; then
		port_shot "port-$p-later" --theme light --screen games --gamecontrols "$p" PRT --shot-at 4000
		same "port page $p moves by itself" "port-$p" "port-$p-later"
	fi
	[ "$p" -gt 0 ] && other "port page $p is page $((p - 1))" "port-$p" "port-$((p - 1))"
	p=$((p + 1))
done
port_shot "port-past" --theme light --screen games --gamecontrols 5 PRT
same "a port: a page past the end is the first" "port-past" "port-0"
mv "$D/aside/"* "$D/Roms/Ports/" || exit 1

echo "every Controls page fits the panel, measured with the font"
# Width and height, on both devices: the home's six and every system's in
# systems.cfg (an .fds game too), each heading beside its page count. A row
# too wide is cut or slides, a page too tall scrolls itself. Found on the
# Pixel 2, 2026-11-06: "START/SELECT  Right/Left soft key"; and the home's
# In MP4, Anywhere and the Pixel 2's On the shelf, five rows over a footer.
run --shot "$D/fit.png" --theme light --controls-fit >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
if grep -q '^CONTROLS-FIT:' "$D/one.log"; then
	grep '^CONTROLS-FIT:' "$D/one.log" | sed 's/^/  FAIL /'
	fails=$((fails + 1))
fi
measured=$(sed -n 's/^controls-fit: \([0-9]*\) pages measured.*/\1/p' "$D/one.log")
[ "${measured:-0}" -gt 150 ] || fail "the fit test measured ${measured:-no} pages"

echo "the home's Controls pages, each its own"
for th in light dark; do
	p=0
	while [ "$p" -lt 6 ]; do
		shot "home-$p-$th" --theme "$th" --screen games --controls "$p"
		[ "$p" -gt 0 ] && other "home page $p ($th) is page $((p - 1))" "home-$p-$th" "home-$((p - 1))-$th"
		p=$((p + 1))
	done
done

echo "the in-game menu is in tabs: Game, Display and Controls, MENU's tab row"
# Quick Slot after Load on the Game tab, Display and Shader on the Display tab,
# and the Controls tab (its rows come with the remap screen) drawn empty
for th in light dark; do
	shot "gamemenu-$th" --theme "$th" --screen games --gamemenu 3 --gm-tab 0
	shot "gamemenu-display-$th" --theme "$th" --screen games --gamemenu 0 --gm-tab 1
	shot "gamemenu-shader-$th" --theme "$th" --screen games --gamemenu 1 --gm-tab 1
	shot "gamemenu-controls-$th" --theme "$th" --screen games --gamemenu 0 --gm-tab 2
	other "$th: the Game tab is not the Display tab" "gamemenu-$th" "gamemenu-display-$th"
	other "$th: Shader is not Display" "gamemenu-shader-$th" "gamemenu-display-$th"
	other "$th: the Controls tab is not the Display tab" "gamemenu-controls-$th" "gamemenu-display-$th"
done

echo "the Controls tab: its rows beside the handheld, in both themes, still"
# The PlayStation's game (the shelf on it, ZyperOS/.last): the tab at rest
# and later the same picture; the cursor on D-pad as Stick lights the
# d-pad; a swap of Y and B (two Lefts on Y) colors both values and lights
# both keys; A on Y opens its choices; the Hotkeys row's page; and a
# Nintendo 64 game's tab, with its Pak and SELECT held for the C buttons
printf 'PS1\nRidge Racer.chd\n' >"$D/root/.last"
for th in light dark; do
	shot "ctltab-$th" --theme "$th" --screen games --gamemenu 0 --gm-tab 2
	shot "ctltab-later-$th" --theme "$th" --screen games --gamemenu 0 --gm-tab 2 --shot-at 4000
	same "$th: the Controls tab sits still" "ctltab-$th" "ctltab-later-$th"
	other "$th: a PlayStation game's Controls tab is its own" "ctltab-$th" "gamemenu-controls-$th"
	shot "ctltab-stick-$th" --theme "$th" --screen games --gamemenu 1 --gm-tab 2
	other "$th: D-pad as Stick is its own row, its key lit" "ctltab-stick-$th" "ctltab-$th"
	shot "ctltab-y-$th" --theme "$th" --screen games --gamemenu 5 --gm-tab 2
	# The swap is kept in the library, so on a library of its own: the
	# shots after it show the card's own table
	rm -rf "$D/shared-swap" && mkdir -p "$D/shared-swap" || exit 1
	SHARED="$D/shared-swap"
	shot "ctltab-swap-$th" --theme "$th" --screen games --gamemenu 5 --gm-tab 2 \
	     --gm-press left,left --shot-at 400
	SHARED=""
	other "$th: a swap changes the rows and the handheld" "ctltab-swap-$th" "ctltab-y-$th"
	shot "ctltab-pick-$th" --theme "$th" --screen games --gamemenu 5 --gm-tab 2 --gm-pick
	other "$th: A on Y opens its choices" "ctltab-pick-$th" "ctltab-y-$th"
	shot "ctltab-keys-$th" --theme "$th" --screen games --gamemenu 13 --gm-tab 2 --gm-keys 0
	other "$th: the Hotkeys row opens its page" "ctltab-keys-$th" "ctltab-$th"
	# ...and the swap left nothing behind: the tab on Y is as it was
	shot "ctltab-y-after-$th" --theme "$th" --screen games --gamemenu 5 --gm-tab 2
	same "$th: the swap's shot left the card's table as it was" "ctltab-y-after-$th" "ctltab-y-$th"
done
other "the Controls tab's themes are the same picture" "ctltab-light" "ctltab-dark"
printf 'N64\nMario Kart 64.z64\n' >"$D/root/.last"
for th in light dark; do
	shot "ctltab-n64-$th" --theme "$th" --screen games --gamemenu 2 --gm-tab 2
	other "$th: the N64's tab is the PlayStation's" "ctltab-n64-$th" "ctltab-$th"
done
grep -q 'drawn: Pak' "$D/log" || fail "the N64's tab draws its Pak row"
rm -f "$D/root/.last"

echo "the home: A to Z on a new card, release dates as a choice"
order() {
	# the systems the launcher scanned, in its order (scan: NAME N games)
	grep -E '^scan: .*[0-9]+ games?$' "$D/one.log" | grep -v 'systems have games' |
		sed -E 's/^scan: (.*[^ ]) +[0-9]+ games?$/\1/' | sed -n "$1p"
}
run --shot "$D/home-az.png" --theme light >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
[ "$(order 1)" = "3DO" ] && [ "$(order 2)" = "32X" ] && [ "$(order 3)" = "Arcade" ] ||
	fail "a new card starts A to Z: 3DO, 32X, Arcade (got $(order 1), $(order 2), $(order 3))"
[ "$(order '$')" = "Ports" ] || fail "and Ports last (got $(order '$'))"
run --shot "$D/order-menu.png" --theme light --menu "System Order" --press a >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
run --shot "$D/home-release.png" --theme light >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
[ "$(order 1)" = "Atari 2600" ] && [ "$(order 2)" = "Game & Watch" ] ||
	fail "System Order to Release Date: Atari 2600 first (got $(order 1), $(order 2))"
run --shot "$D/order-menu2.png" --theme light --menu "System Order" --press a >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
run --shot "$D/home-az2.png" --theme light >"$D/one.log" 2>&1
cat "$D/one.log" >>"$D/log"
[ "$(order 1)" = "3DO" ] || fail "and back to A to Z (got $(order 1))"
other "the menu says which" "order-menu" "order-menu2"

if [ "$fails" -ne 0 ]; then
	echo "controls-shots: $fails FAILED"
	exit 1
fi
echo "ok: every page sits still and is its own, Controls follows Display, the"
echo "Controls tab sits still beside its handheld, and the"
echo "home goes A to Z, to release dates and back"
