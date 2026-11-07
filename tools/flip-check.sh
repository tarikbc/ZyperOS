#!/bin/sh
# Compare settled screens after a theme change with screens started in that
# theme. The system sprites retain their source colors; UI text, the clock
# and generated game/music covers must follow the new theme. Both home layouts
# are checked with and without art, at a fixed source-animation phase.
# The private card tree has no network, user files or real ROMs.
set -u

D=build-native/flip-check
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
for g in Contra Kirby Metroid Zelda "Super Mario Bros"; do : > "$D/Roms/NES/$g.nes"; done
for g in "Chrono Trigger" Earthbound F-Zero; do : > "$D/Roms/SNES/$g.sfc"; done
mkdir -p "$D/Music/Pixies/Doolittle" || exit 1
for t in "01 Debaser" "02 Tame" "03 Wave of Mutilation"; do
	: > "$D/Music/Pixies/Doolittle/$t.mp3"
done

# Real transparent sprites, independent of the theme. Only the two systems
# in this fixture need art; missing synthetic shelves remain explicit fallbacks.
A=$D/art
mkdir -p "$A/system-icons" || exit 1
cp config/systems.cfg "$A/systems.cfg" || exit 1
for c in NES SNES; do
    cp "res/system-icons/$c.png" "res/system-icons/$c-still.png" "$A/system-icons/" || exit 1
done

# One shot of the card from $root, its output added to $D/log; when it fails,
# what it found wrong with its own frame and the last three lines it said go
# to $D/why. Its exit status is the launcher's.
#
# Drawn by SDL's software renderer. A Mac's Metal one does not draw the same
# frame twice the same way: a second frame with nothing changed is another
# picture, a tenth to a fifth of its pixels off and most of those by one
# level, unless the focused card was uploaded again in it (measured
# 2026-11-04 on the system rows). The generated cards are
# made again after the turn and so never showed it; art is kept across the
# turn, and its pairs failed with nothing wrong. The software renderer draws
# a frame the same whatever came before it.
root=$D/root
shot() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	ZYPEROS_ROOT="$root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" --shot "$@" \
		--status "10:08 wifi=2 batt=82" --phase 1000 >"$D/one.log" 2>&1
	st=$?
	cat "$D/one.log" >>"$D/log"
	if [ "$st" -ne 0 ]; then
		{ grep "NO FOOT\|BLANK" "$D/one.log"; tail -n 3 "$D/one.log"; } >>"$D/why"
	fi
	return $st
}

fails=0

# pair NAME FROM TO [ARGS...]: a shot started in TO against one started in
# FROM and turned to TO.
pair() {
	name=$1 from=$2 to=$3
	shift 3
	ok=1
	: >"$D/why"
	shot "$D/$name-$to.png" --theme "$to" "$@" || ok=0
	shot "$D/$name-$from-to-$to.png" --theme "$from" --flip "$@" || ok=0
	if [ "$ok" -ne 1 ]; then
		echo "  FAIL $name, $from to $to: a shot failed (see $D/log)"
		awk '!seen[$0]++' "$D/why" | sed 's/^ */    /'
		fails=$((fails + 1))
	elif [ ! -s "$D/$name-$to.png" ] || [ ! -s "$D/$name-$from-to-$to.png" ]; then
		echo "  FAIL $name: a shot was not written (see $D/log)"
		fails=$((fails + 1))
	elif ! cmp -s "$D/$name-$to.png" "$D/$name-$from-to-$to.png"; then
		echo "  FAIL $name: turned from $from to $to, it is not the shot started in $to" \
		     "($D/$name-$from-to-$to.png against $D/$name-$to.png)"
		fails=$((fails + 1))
	fi
}

# Choose a known populated shelf in the scratch library, not whichever shelf
# the default happens to favor after a later startup-policy change.
shot "$D/bootstrap.png" --theme light || exit 1
python3 - "$D/shared/.zyperos/library.db" <<'PYSET'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    db.execute("INSERT OR REPLACE INTO settings VALUES ('startup_system','NES')")
PYSET
[ "$?" -eq 0 ] || exit 1

echo "a screen turned to a theme is the screen started in it"
for layout in spotlight gallery; do
    pair "missing-$layout" light dark --home "$layout"
    pair "missing-$layout" dark light --home "$layout"
done
pair games light dark --screen games
pair games dark light --screen games
pair nowplaying light dark --nowplaying 0 0 83
pair nowplaying dark light --nowplaying 0 0 83

echo "home sprites stay visible across a theme turn"
root=$A
for layout in spotlight gallery; do
    pair "art-$layout" light dark --home "$layout"
    pair "art-$layout" dark light --home "$layout"
    for theme in light dark; do
        if cmp -s "$D/art-$layout-$theme.png" "$D/missing-$layout-$theme.png"; then
            echo "  FAIL $layout $theme: adding sprite art did not change the frame"
            fails=$((fails + 1))
        fi
    done
done
shot "$D/current-systems.png" --theme light || exit 1

# A removed setting must never restore another layout or art set at boot.
# These are scratch database rows. Keep unrelated settings intact too.
echo "old saved layout and card values leave startup horizontal"
python3 - "$D/userdata/zyperos.db" <<'PYSET'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    assert not db.execute("SELECT key FROM settings WHERE key IN ('cards','cards_dir')").fetchall()
    db.execute("INSERT OR REPLACE INTO settings VALUES ('cleanup-sentinel','keep')")
PYSET
[ "$?" -eq 0 ] || exit 1
shot "$D/current-menu.png" --theme light --menu Appearance || exit 1
shot "$D/current-games.png" --theme light --screen games || exit 1
for old in vertical both unknown; do
	python3 - "$D/userdata/zyperos.db" "$old" <<'PYSET'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    db.executemany("INSERT OR REPLACE INTO settings VALUES (?,?)",
                   [("cards", "fancy"), ("cards_dir", sys.argv[2])])
PYSET
	[ "$?" -eq 0 ] || exit 1
	shot "$D/legacy-$old-systems.png" --theme light || exit 1
	shot "$D/legacy-$old-menu.png" --theme light --menu Appearance || exit 1
	shot "$D/legacy-$old-games.png" --theme light --screen games || exit 1
	for view in systems menu games; do
		before="$D/current-$view.png"
		[ "$view" != systems ] || before="$D/current-systems.png"
		if ! cmp -s "$before" "$D/legacy-$old-$view.png"; then
			echo "  FAIL old $old settings changed $view"
			fails=$((fails + 1))
		fi
	done
done
python3 - "$D/userdata/zyperos.db" <<'PYSET'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    assert db.execute("SELECT value FROM settings WHERE key='cleanup-sentinel'").fetchone() == ("keep",)
    assert db.execute("SELECT value FROM settings WHERE key='cards'").fetchone() == ("fancy",)
PYSET
[ "$?" -eq 0 ] || exit 1

if [ "$fails" -ne 0 ]; then
	printf '\n%d FAILED\n' "$fails"
	exit 1
fi
printf '\nok: the systems row, the games shelf and the Now Playing cover keep\n'
printf 'the current theme; both home layouts retain the source sprite art\n'
