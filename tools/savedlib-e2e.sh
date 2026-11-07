#!/bin/sh
# The saved library through the real launcher, headless on this machine: its
# own loop, not a shot, since the check runs after the first frame.
#
#   1. no saved library: the card is read, and the check writes one;
#   2. a start from it: the check finds the card as the shelves show it;
#   3. a game added between starts: the start shows the saved shelves, the
#      check sees NES changed, builds the shelves again, and the cursor stays
#      on the game it was on;
#   4. the next start shows the new game from the saved library at once.
#
# The private card tree has no network, user files or real ROMs.
set -u

D=build-native/savedlib-e2e
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
mkdir -p "$D/root" "$D/Roms/NES" "$D/Roms/SNES" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
for g in Contra Kirby Metroid Zelda; do : > "$D/Roms/NES/$g.nes"; done
for g in "Chrono Trigger" Earthbound; do : > "$D/Roms/SNES/$g.sfc"; done

run_env() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$@"
}

# The tutorial would wait for buttons before the shelf
run_env "$BIN" --set tour.done=1 > "$D/set.log" 2>&1 || { echo "FAIL --set tour.done"; cat "$D/set.log"; exit 1; }

fails=0
ok() { echo "  ok: $1"; }
bad() { echo "  FAIL $1 (see $D/start$n.log)"; fails=$((fails + 1)); }

# One start, until the check has said what it found (or 20 s), then a
# SIGTERM, as launch.sh's stop is
start() {
	n=$1
	run_env "$BIN" > "$D/start$n.log" 2>&1 &
	pid=$!
	t=0
	while [ $t -lt 200 ] && ! grep -q "saved library: the card" "$D/start$n.log"; do
		kill -0 $pid 2>/dev/null || break
		sleep 0.1
		t=$((t + 1))
	done
	# A rebuild says where it stayed on the line after
	sleep 0.3
	kill -TERM $pid 2>/dev/null
	wait $pid 2>/dev/null
}

has() { grep -q "$2" "$D/start$1.log"; }

echo "a start with no saved library reads the card and saves it"
start 1
has 1 "saved library: none to use" && ok "none to use at first" || bad "no 'none to use' line"
has 1 "saved library: the card is as the shelves show it" && ok "the check agrees" || bad "no check line"
[ -s "$D/userdata/saved-library" ] && ok "the file is written" || bad "no saved-library file"
grep -q "^game	Zelda.nes	Zelda$" "$D/userdata/saved-library" && ok "with its games" || bad "Zelda.nes not in the file"

echo "a start from the saved library, the card unchanged"
start 2
has 2 "saved library: [0-9]* of [0-9]* shelves from it" && ok "shelves from it" || bad "no 'from it' line"
has 2 "scan: NES  *4 games" && ok "NES shows its 4 games" || bad "NES is not 4 games"
has 2 "saved library: the card is as the shelves show it" && ok "the check agrees" || bad "no check line"

echo "a game added between starts: shown saved, then read again in place"
: > "$D/Roms/NES/Bubble Bobble.nes"
printf 'NES\nMetroid.nes\n' > "$D/root/.last"
start 3
has 3 "scan: NES  *4 games" && ok "the start shows the saved 4" || bad "the start did not show the saved 4"
has 3 "saved library: the card changed (NES)" && ok "the check sees NES changed" || bad "no 'changed (NES)' line"
has 3 "scan: NES  *5 games" && ok "the shelves are read again: 5" || bad "NES not rebuilt to 5"
has 3 "saved library: still on NES, Metroid.nes" && ok "the cursor stays on Metroid" || bad "the place was not kept"

echo "the next start has the new game from the saved library"
start 4
has 4 "saved library: [0-9]* of [0-9]* shelves from it" && ok "shelves from it" || bad "no 'from it' line"
has 4 "scan: NES  *5 games" && ok "NES shows 5 at once" || bad "NES is not 5"
has 4 "saved library: the card is as the shelves show it" && ok "the check agrees" || bad "no check line"

if [ $fails -gt 0 ]; then
	echo
	echo "$fails FAILED"
	exit 1
fi
echo
echo "ok: a start shows the saved library, and the check puts a changed card right in place"
