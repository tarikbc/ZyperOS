#!/bin/sh
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
# The launcher's two computer-side importers, end to end on a scratch card:
# --meta fills a game's year, genres and synopsis, and --titles then gives it a
# display title without losing any of that; a game with no row gets one with
# the title alone, an empty title clears nothing, and a line that is not
# folder<TAB>file<TAB>title is counted as rejected. The host build, so it
# needs SDL2 and skips cleanly without it; python3 reads the database back.
BIN=build-native/zyperos
D=${TMPDIR:-/tmp}/zyperos-importer-check.$$
trap 'rm -rf "$D"' EXIT

if [ ! -x "$BIN" ]; then
	if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then
		echo "FAIL no $BIN: make native first" >&2
		exit 1
	fi
	echo "skip: no SDL2 for the host build"
	exit 0
fi

mkdir -p "$D/root" "$D/Roms/Game Boy Advance" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1

run() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" \
	ZYPEROS_ROMS="$D/Roms" ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" \
	"$BIN" "$@"
}

syn="Command the Orange Star army.
Two lines, the second after a newline."
len=$(printf '%s' "$syn" | wc -c | tr -d ' ')
printf 'Game Boy Advance\t060 Advance Wars 2.gba\t2003\tNintendo\tIntelligent Systems\t2\tStrategy\tE\t18\t%s\n%s\n' \
	"$len" "$syn" >"$D/meta"
out=$(run --meta "$D/meta" 2>&1) || { echo "FAIL --meta: $out"; exit 1; }

printf 'Game Boy Advance\t060 Advance Wars 2.gba\tAdvance Wars 2: Black Hole Rising\n' >"$D/titles"
printf 'Game Boy Advance\t001 Pokemon Emerald Version.gba\tPok\303\251mon Emerald Version\n' >>"$D/titles"
printf 'Game Boy Advance\t002 Pokemon Ruby.gba\t\n' >>"$D/titles"
printf 'not a line\n\n' >>"$D/titles"
out=$(run --titles "$D/titles" 2>&1)
case $out in
*"2 titles written, 2 rejected"*) ;;
*) echo "FAIL --titles said: $out"; exit 1 ;;
esac

python3 - "$D/shared/.zyperos/library.db" "$syn" <<'EOF' || exit 1
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
row = lambda f: db.execute("select title, year, genres, synopsis from games where folder=? and file=?",
                           ("Game Boy Advance", f)).fetchone()
fails = []
aw = row("060 Advance Wars 2.gba")
if not aw or aw[0] != "Advance Wars 2: Black Hole Rising": fails.append(f"title not set: {aw}")
if not aw or aw[1] != "2003" or aw[2] != "Strategy" or aw[3] != sys.argv[2]:
    fails.append(f"--titles lost what --meta wrote: {aw}")
pk = row("001 Pokemon Emerald Version.gba")
if not pk or pk[0] != "Pokémon Emerald Version" or pk[3]: fails.append(f"a new row is not the title alone: {pk}")
if row("002 Pokemon Ruby.gba"): fails.append("an empty title made a row")
for f in fails: print("FAIL", f)
sys.exit(1 if fails else 0)
EOF
echo "ok: --meta writes the prose, --titles the name over it, and neither loses the other"
