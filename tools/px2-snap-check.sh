#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-px2-snap: px2-snap (tools/px2-snap.sh) on a made-up card.
#
# Which files are a game's: its .srm and .sav, its Auto state and slots and
# their pictures, and by the core fbneo's Saves/fbneo, Flycast's NAOMI
# Saves/reicast and ScummVM's states by the game's folder; never another
# game's that only starts with the same name. Its rows in the library
# database through the real launcher (--game-rows, src/gamerows.h): its play
# sessions, favorite and Pak, and no other game's, of its own system or of
# another with a file of the same name. Then a test's changes undone: each
# file put back byte for byte, the ones it made removed, the rows as they
# were, another game's left as the test left them. And the refusals: a game
# Diatom's now file names (diatom/src/now.h) while its process lives, a
# snapshot name that is a path, a name taken, rows of another game's, and a
# launcher with no --game-rows, which keeps the files and says it keeps no
# rows.
#
# Needs the launcher built (SDL2) and sqlite3 to plant rows; skips without
# them. The card is a scratch tree under build-native/px2-snap-card.
set -u

D=$PWD/build-native/px2-snap-card
BIN=$PWD/build-native/zyperos
SNAP=$PWD/tools/px2-snap.sh

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then
	echo "skipped: no SDL2 here to build the launcher with"
	exit 0
fi
command -v sqlite3 > /dev/null || { echo "skipped: no sqlite3 to plant rows with"; exit 0; }
[ -x "$BIN" ] || { echo "FAIL no $BIN: make native first" >&2; exit 1; }

rm -rf "$D"
mkdir -p "$D/ZyperOS" "$D/Roms/NES" "$D/Roms/Arcade" "$D/Roms/NAOMI" "$D/Roms/ScummVM/monkey" \
	"$D/Saves/fbneo" "$D/Saves/reicast" "$D/.userdata/pixel2" "$D/.userdata/shared/.zyperos/NES" \
	"$D/.userdata/shared/.zyperos/ScummVM" "$D/bin" || exit 1
cp config/systems.cfg "$D/ZyperOS/systems.cfg" || exit 1

# The launcher, on this card: it answers --game-rows-put
cat > "$D/bin/zyperos.elf" <<EOF
#!/bin/sh
# the host launcher on the scratch card; it answers --game-rows-put
ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy ZYPEROS_ROOT="$D/ZyperOS" ZYPEROS_CARD="$D" \\
ZYPEROS_ROMS="$D/Roms" ZYPEROS_USERDATA="$D/.userdata/pixel2" \\
ZYPEROS_SHARED="$D/.userdata/shared" ZYPEROS_WEB="$D" exec "$BIN" "\$@"
EOF
# and one from before it, which answers nothing of the kind
printf '#!/bin/sh\necho "a launcher started" >&2\nexit 9\n' > "$D/bin/old.elf"
chmod +x "$D/bin/zyperos.elf" "$D/bin/old.elf"

snap() { PX2_SNAP_CARD=$D PX2_SNAP_ELF=$D/bin/zyperos.elf PX2_SNAP_NOW=$D/now sh "$SNAP" "$@"; }
elf() { "$D/bin/zyperos.elf" "$@"; }

fails=0
ok() { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

put() { mkdir -p "$(dirname "$D/$1")" && printf '%s' "$2" > "$D/$1"; }
S=.userdata/shared/.zyperos
G="Contra (USA).nes"

# The games: Contra, and another NES game whose name starts as Contra's does
: > "$D/Roms/NES/$G"
: > "$D/Roms/NES/Contra (USA) (Beta).nes"
: > "$D/Roms/Arcade/mslug.zip"
: > "$D/Roms/NAOMI/doa2.zip"
: > "$D/Roms/ScummVM/monkey/monkey.scummvm"
put "Saves/Contra (USA).srm" "battery 1"
put "$S/NES/$G.auto.state" "auto 1"
put "$S/NES/$G.auto.bmp" "auto pic 1"
put "$S/NES/$G.2.state" "slot 2"
put "$S/NES/$G.2.bmp" "slot 2 pic"
put "Saves/Contra (USA) (Beta).srm" "beta battery"
put "$S/NES/Contra (USA) (Beta).nes.auto.state" "beta auto"
put "Saves/fbneo/mslug.nv" "nvram"
put "Saves/reicast/doa2.zip.eeprom" "eeprom"
put "$S/ScummVM/monkey.1.state" "monkey slot"

# Its rows, and other games' that look like them
elf --dump > /dev/null 2>&1
LIB=$D/.userdata/shared/.zyperos/library.db
[ -f "$LIB" ] || { echo "FAIL the launcher made no library database at $LIB"; exit 1; }
T=$(printf '\t')
sqlite3 "$LIB" "INSERT INTO settings(key,value) VALUES
 ('sess.1730000000.1234.NES${T}$G', '600${T}quit'),
 ('sess.1730100000.99.NES${T}$G', '45${T}quit'),
 ('fav.NES${T}$G', '1'),
 ('pak.NES.$G', 'memory'),
 ('sess.1730000001.5.NES${T}Contra (USA) (Beta).nes', '30${T}quit'),
 ('sess.1730000002.6.GB${T}$G', '20${T}quit'),
 ('fav.NE${T}S$G', '1');" || { echo "FAIL could not plant rows"; exit 1; }
rows0=$(elf --game-rows NES "$G")
check "--game-rows prints the game's four rows and no other game's" \
	'[ "$(printf "%s\n" "$rows0" | grep -c .)" = 4 ] && ! printf "%s" "$rows0" | grep -q "Beta\|GB\|fav.NE\\\\t"'
check "a session's tab in its key and value written as \\t" \
	'printf "%s\n" "$rows0" | grep -qF "sess.1730000000.1234.NES\\tContra (USA).nes${T}600\\tquit"'
dump0=$(sqlite3 "$LIB" "SELECT key||'='||value FROM settings ORDER BY key")

# Saved
out=$(snap save "Roms/NES/$G" contra 2>&1)
check "save says what it kept: 5 files, 4 rows ($out)" 'printf "%s" "$out" | grep -q "contra: 5 file(s), 4 row(s)"'
got=$(cd "$D/.userdata/px2-snap/contra/files" && find . -type f | sort)
want=$(printf '%s\n' "./$S/NES/$G.2.bmp" "./$S/NES/$G.2.state" "./$S/NES/$G.auto.bmp" \
	"./$S/NES/$G.auto.state" "./Saves/Contra (USA).srm" | sort)
check "the game's files, and not the Beta's that starts with its name" '[ "$got" = "$want" ]'
check "kept at their places, as they are" \
	'cmp -s "$D/.userdata/px2-snap/contra/files/Saves/Contra (USA).srm" "$D/Saves/Contra (USA).srm"'
check "its meta names the game" 'grep -qx "tag=NES" "$D/.userdata/px2-snap/contra/meta" && grep -qxF "file=$G" "$D/.userdata/px2-snap/contra/meta"'
check "a name taken is refused" '! snap save "$D/Roms/NES/$G" contra 2>/dev/null'
check "and replaced with --force" 'snap save "$D/Roms/NES/$G" contra --force > /dev/null 2>&1'
check "a name that is a path is refused" '! snap save "NES/$G" ../x 2>/dev/null && [ ! -e "$D/.userdata/x" ]'
check "a ROM that is not there is refused" '! snap save "NES/Nothing.nes" 2>/dev/null'
check "the name defaults to the ROM's" 'snap save "NES/$G" > /dev/null 2>&1 && [ -f "$D/.userdata/px2-snap/Contra_USA/meta" ]'

# By the core
snap save "Arcade/mslug.zip" > /dev/null 2>&1
check "fbneo's own save, in Saves/fbneo" '[ -f "$D/.userdata/px2-snap/mslug/files/Saves/fbneo/mslug.nv" ]'
snap save "NAOMI/doa2.zip" > /dev/null 2>&1
check "a NAOMI game's EEPROM, in Saves/reicast by the set's file" '[ -f "$D/.userdata/px2-snap/doa2/files/Saves/reicast/doa2.zip.eeprom" ]'
snap save "ScummVM/monkey/monkey.scummvm" > /dev/null 2>&1
check "a ScummVM game's states, by its folder's name" '[ -f "$D/.userdata/px2-snap/monkey/files/$S/ScummVM/monkey.1.state" ]'

# The test: Contra played. Its battery written, a slot gone, one made, a
# .sav made, the Auto state written; a session added, its favorite and Pak
# changed. And the Beta played too, which a restore of Contra leaves alone.
put "Saves/Contra (USA).srm" "battery 2, longer"
rm -f "$D/$S/NES/$G.2.state" "$D/$S/NES/$G.2.bmp"
put "$S/NES/$G.3.state" "slot 3"
put "Saves/Contra (USA).sav" "a sav"
put "$S/NES/$G.auto.state" "auto 2"
put "Saves/Contra (USA) (Beta).srm" "beta battery 2"
sqlite3 "$LIB" "INSERT INTO settings(key,value) VALUES ('sess.1730200000.7.NES${T}$G', '99${T}open');
 DELETE FROM settings WHERE key='fav.NES${T}$G';
 UPDATE settings SET value='rumble' WHERE key='pak.NES.$G';
 INSERT INTO settings(key,value) VALUES ('sess.1730200001.8.NES${T}Contra (USA) (Beta).nes', '5${T}quit');"

# Open: Diatom's now file naming it, with a live process (this shell)
printf 'pid=%s\nrom=%s\n' $$ "$D/Roms/NES/$G" > "$D/now"
check "restore refuses the game while Diatom has it open" '! snap restore contra 2>/dev/null && [ -f "$D/$S/NES/$G.3.state" ]'
check "save does too" '! snap save "NES/$G" open-one 2>/dev/null && [ ! -e "$D/.userdata/px2-snap/open-one" ]'
printf 'pid=%s\nrom=%s\n' $$ "$D/Roms/NES/Contra (USA) (Beta).nes" > "$D/now"
check "another game open is no reason" 'snap save "NES/$G" other-open > /dev/null 2>&1'
printf 'pid=999999\nrom=%s\n' "$D/Roms/NES/$G" > "$D/now"
check "nor a now file Diatom left when it died" 'snap save "NES/$G" dead-open > /dev/null 2>&1'
rm -f "$D/now"

# Rows of another game's are refused, and nothing changes
printf 'fav.NES\\tContra (USA) (Beta).nes\t1\n' > "$D/other.rows"
before=$(elf --game-rows NES "$G")
check "--game-rows-put refuses a row of another game's" '! elf --game-rows-put NES "$G" < "$D/other.rows" 2>/dev/null'
check "and leaves the game's rows as they were" '[ "$(elf --game-rows NES "$G")" = "$before" ]'

# Restored
out=$(snap restore contra --no-restart 2>&1)
check "restore says what it did ($out)" \
	'printf "%s" "$out" | grep -q "4 file(s) put back, 2 removed, the rows put back"'
for f in "Saves/Contra (USA).srm" "$S/NES/$G.auto.state" "$S/NES/$G.auto.bmp" "$S/NES/$G.2.state" "$S/NES/$G.2.bmp"; do
	check "$f byte for byte" 'cmp -s "$D/.userdata/px2-snap/contra/files/$f" "$D/$f"'
done
check "the slot the test made is gone, and the .sav" '[ ! -e "$D/$S/NES/$G.3.state" ] && [ ! -e "$D/Saves/Contra (USA).sav" ]'
check "the Beta's battery stays as the test left it" '[ "$(cat "$D/Saves/Contra (USA) (Beta).srm")" = "beta battery 2" ]'
check "the game's rows are as they were" '[ "$(elf --game-rows NES "$G")" = "$rows0" ]'
dump1=$(sqlite3 "$LIB" "SELECT key||'='||value FROM settings ORDER BY key")
check "and the database is as it was, but for the Beta's new session" \
	'[ "$(printf "%s\n" "$dump1" | grep -v "1730200001.8")" = "$dump0" ] && printf "%s" "$dump1" | grep -q "1730200001.8"'
out=$(snap restore contra --no-restart 2>&1)
check "a second restore has nothing to do" 'printf "%s" "$out" | grep -q "0 file(s) put back, 0 removed$"'

# A launcher from before --game-rows
out=$(PX2_SNAP_CARD=$D PX2_SNAP_ELF=$D/bin/old.elf PX2_SNAP_NOW=$D/now sh "$SNAP" save "NES/$G" old 2>&1)
check "with a launcher that has no --game-rows the files are kept, and it says the rows are not" \
	'printf "%s" "$out" | grep -q "no --game-rows" && printf "%s" "$out" | grep -q "old: 5 file(s), no row(s)" && [ ! -e "$D/.userdata/px2-snap/old/rows" ] && ! printf "%s" "$out" | grep -q "launcher started"'

# list and delete
out=$(snap list)
check "list names each snapshot and its game" 'printf "%s\n" "$out" | grep "^contra " | grep -qF "5 file(s), 4 row(s)  $D/Roms/NES/$G"'
check "delete removes one" 'snap delete monkey > /dev/null && [ ! -e "$D/.userdata/px2-snap/monkey" ]'
check "and refuses a name that is a path" '! snap delete .. 2>/dev/null && [ -d "$D/.userdata/px2-snap" ]'

if [ $fails -gt 0 ]; then
	echo "$fails failed"
	exit 1
fi
echo "ok: px2-snap keeps a game's files and rows, puts them back byte for byte, and refuses an open game"
